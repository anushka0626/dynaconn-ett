#pragma once
#include "DynamicConnectivity.hpp"
#include "EdgeMaps.hpp"
#include "EulerTourForest.hpp"
#include "MemoryAccounting.hpp"
#include "OrderedSets.hpp"
#include "Random.hpp"
#include "Stats.hpp"
#include "TreapStorage.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

// HKS: simplified Henzinger-King without levels, as benchmarked by SIGMOD 2025 (behavior
// specified by their Imp_bench_dyn/HKS code; no code copied; docs/IMPLEMENTATION_NOTES.md).
//
// Same Euler-tour forest as our ETT, but the treap nodes carry the number of non-tree edge
// endpoints in their subtree (WeightAug). The replacement search for a cut tree edge:
//   1. T1 = the side with the smaller weight;
//   2. if weight(T1) >= k = floor(log2(n)^2): k uniform samples of an incident non-tree
//      edge; promote the first one that leaves T1;
//   3. otherwise, or if all samples miss: enumerate every incident non-tree edge (BFS over
//      weighted treap nodes), collect the crossing ones, promote the first.
template <class Storage, template <class> class EdgeMapT, class Adjacency>
class HKSImpl : public DynamicConnectivity {
    static_assert(EulerTourForest<Storage, EdgeMapT>::kWeighted, "HKS needs weighted treap nodes");

public:
    static constexpr uint64_t default_sample_seed = 0x5eed'4b5c;

    explicit HKSImpl(int vertices, uint32_t treap_seed = kDefaultTreapSeed,
                     uint64_t sample_seed = default_sample_seed)
        : n(vertices), forest(vertices, treap_seed), nte(vertices), rng(sample_seed) {
        double lg = vertices > 1 ? std::log2(static_cast<double>(vertices)) : 0.0;
        sample_k = static_cast<int64_t>(lg * lg);
    }

    bool is_connected(int u, int v) override { return forest.connected(u, v); }

    void insert_edge(int u, int v) override {
        if (u == v) return;
        int a = std::min(u, v), b = std::max(u, v);
        if (forest.has_tree_edge(a, b) || nte.contains(a, b)) return;
        if (!forest.connected(a, b)) {
            forest.link(a, b);
        } else {
            add_non_tree(a, b);
        }
    }

    void delete_edge(int u, int v) override {
        if (u == v) return;
        int a = std::min(u, v), b = std::max(u, v);
        if (nte.contains(a, b)) {
            remove_non_tree(a, b);
            return;
        }
        if (!forest.cut(a, b)) return;
        DC_COUNT(tree_edge_deletes, 1);
        replace(a, b);
    }

    size_t edge_count() const override { return forest.num_tree_edges() + non_tree_count; }

    const EulerTourForest<Storage, EdgeMapT>& euler_forest() const { return forest; }

    std::string check_invariants() const {
        std::string err = forest.check_invariants();
        if (!err.empty()) return err;
        size_t directed = 0;
        for (int x = 0; x < n; ++x) {
            if (static_cast<size_t>(forest.vertex_weight(x)) != nte.size(x)) return "vertex weight != non-tree degree";
            for (int y : nte.items(x)) {
                ++directed;
                if (!nte.contains(y, x)) return "non-tree adjacency is not symmetric";
                if (forest.has_tree_edge(x, y)) return "edge is both tree and non-tree";
                if (!forest.connected(x, y)) return "non-tree edge spans two trees";
            }
        }
        if (directed != 2 * non_tree_count) return "non-tree edge count mismatch";
        return {};
    }

private:
    int n;
    EulerTourForest<Storage, EdgeMapT> forest;
    Adjacency nte;
    size_t non_tree_count = 0;
    dc::SplitMix64 rng;
    int64_t sample_k = 0;

    void add_non_tree(int a, int b) {
        nte.insert(a, b);
        nte.insert(b, a);
        forest.add_vertex_weight(a, 1);
        forest.add_vertex_weight(b, 1);
        ++non_tree_count;
    }

    void remove_non_tree(int a, int b) {
        nte.erase(a, b);
        nte.erase(b, a);
        forest.add_vertex_weight(a, -1);
        forest.add_vertex_weight(b, -1);
        --non_tree_count;
    }

    void promote(int x, int y) {
        remove_non_tree(x, y);
        forest.link(x, y);
        DC_COUNT(replacements_found, 1);
    }

    void replace(int u, int v) {
        auto ru = forest.root_of(u), rv = forest.root_of(v);
        auto t1 = forest.subtree_weight(ru) < forest.subtree_weight(rv) ? ru : rv;
        const int32_t w = forest.subtree_weight(t1);

        if (w > 0 && w >= sample_k) {
            for (int64_t i = 0; i < sample_k; ++i) {
                int32_t offset = 0;
                int x = forest.locate_by_weight(t1, static_cast<int64_t>(rng.below(static_cast<uint64_t>(w))), offset);
                int y = nte.items(x)[offset];
                DC_COUNT(replacement_edges_scanned, 1);
                DC_COUNT(sample_draws, 1);
                if (forest.root_of(y) != t1) {
                    DC_COUNT(sample_hits, 1);
                    promote(x, y);
                    return;
                }
            }
        }

        // Exhaustive: every incident non-tree edge; promote the first crossing one.
        int best_x = -1, best_y = -1;
        forest.for_each_weighted_vertex_bfs(t1, [&](int x) {
            for (int y : nte.items(x)) {
                DC_COUNT(replacement_edges_scanned, 1);
                if (best_x < 0 && forest.root_of(y) != t1) {
                    best_x = x;
                    best_y = y;
                }
            }
        });
        if (best_x >= 0) promote(best_x, best_y);
    }
};

using HksP0 = HKSImpl<PointerStorageT<WeightAug>, dc::StdEdgeMap, dc::StdOrderedSets<dc::MemCategory::Adjacency>>;
using HksF2 = HKSImpl<ArenaStorageT<WeightAug>, dc::FlatEdgeMap, dc::FlatOrderedSets<dc::MemCategory::Adjacency>>;
