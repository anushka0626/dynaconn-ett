#pragma once
#include "Random.hpp"
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

enum class OpType : uint8_t { INSERT, DELETE, QUERY };

struct GraphOperation {
    OpType type;
    int u;
    int v;
};

struct ChurnConfig {
    int num_vertices = 0;
    int num_operations = 0;
    double p_insert = 0.0;  // probability of an insert
    double p_delete = 0.0;  // probability of a delete; the rest are queries
    uint64_t seed = 42;

    // If false, an insert always targets an absent edge. If true, an insert may repeat a
    // present edge (a no-op for every structure); these are counted in duplicate_inserts.
    bool allow_duplicate_inserts = false;

    // Density control. If >= 0, an update op is forced to be an insert while the graph has
    // fewer edges than this, and a delete while it has more. At the target, the drawn type
    // is kept. Without this, equal insert/delete rates make the edge count a random walk.
    int64_t target_edges = -1;
};

struct ChurnStats {
    size_t inserts = 0;
    size_t deletes = 0;
    size_t queries = 0;
    size_t duplicate_inserts = 0;  // inserts of an edge that was already present
    size_t delete_fallbacks = 0;   // delete drawn on an empty graph -> emitted as insert
    size_t insert_fallbacks = 0;   // no absent edge found -> emitted as delete (or query if V < 2)
    size_t final_edges = 0;
};

struct Workload {
    std::vector<GraphOperation> ops;
    ChurnStats stats;
};

// Uniform-random churn stream. Exactly num_operations ops: no self-loops, deletes always
// target a present edge (in a random orientation), and queries are uniform vertex pairs.
inline Workload generate_churn_workload(const ChurnConfig& cfg) {
    const int n = cfg.num_vertices;
    dc::SplitMix64 rng(cfg.seed);
    Workload w;
    if (n < 1) return w;
    w.ops.reserve(cfg.num_operations);

    // Present edges (normalized u < v) with O(1) random pick and removal.
    std::vector<std::pair<int, int>> edges;
    std::unordered_map<uint64_t, size_t> position;
    auto key = [n](int a, int b) { return static_cast<uint64_t>(a) * n + b; };

    auto random_pair = [&]() {
        int u = static_cast<int>(rng.below(n));
        int v = static_cast<int>(rng.below(n - 1));
        if (v >= u) ++v;  // uniform over ordered pairs with u != v, in one draw
        return std::pair{u, v};
    };

    auto emit_query = [&]() {
        w.ops.push_back({OpType::QUERY, static_cast<int>(rng.below(n)), static_cast<int>(rng.below(n))});
        ++w.stats.queries;
    };

    auto emit_delete = [&]() {
        size_t i = rng.below(edges.size());
        auto [a, b] = edges[i];
        if (rng.next() & 1) std::swap(a, b);
        w.ops.push_back({OpType::DELETE, a, b});
        ++w.stats.deletes;

        position.erase(key(std::min(a, b), std::max(a, b)));
        if (i != edges.size() - 1) {
            edges[i] = edges.back();
            position[key(edges[i].first, edges[i].second)] = i;
        }
        edges.pop_back();
    };

    // Returns false if no insert could be emitted.
    auto emit_insert = [&]() {
        if (n < 2) return false;
        constexpr int kMaxAttempts = 64;
        for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
            auto [u, v] = random_pair();
            uint64_t k = key(std::min(u, v), std::max(u, v));
            bool present = position.contains(k);
            if (present && !cfg.allow_duplicate_inserts) continue;

            w.ops.push_back({OpType::INSERT, u, v});
            ++w.stats.inserts;
            if (present) {
                ++w.stats.duplicate_inserts;
            } else {
                position[k] = edges.size();
                edges.push_back({std::min(u, v), std::max(u, v)});
            }
            return true;
        }
        return false;
    };

    for (int i = 0; i < cfg.num_operations; ++i) {
        double roll = rng.uniform01();
        OpType type = roll < cfg.p_insert              ? OpType::INSERT
                      : roll < cfg.p_insert + cfg.p_delete ? OpType::DELETE
                                                          : OpType::QUERY;

        if (type != OpType::QUERY && cfg.target_edges >= 0) {
            auto e = static_cast<int64_t>(edges.size());
            if (e < cfg.target_edges) type = OpType::INSERT;
            else if (e > cfg.target_edges) type = OpType::DELETE;
        }

        if (type == OpType::DELETE && edges.empty()) {
            ++w.stats.delete_fallbacks;
            type = OpType::INSERT;
        }

        if (type == OpType::INSERT) {
            if (emit_insert()) continue;
            ++w.stats.insert_fallbacks;
            if (edges.empty()) {
                emit_query();
            } else {
                emit_delete();
            }
        } else if (type == OpType::DELETE) {
            emit_delete();
        } else {
            emit_query();
        }
    }

    w.stats.final_edges = edges.size();
    return w;
}
