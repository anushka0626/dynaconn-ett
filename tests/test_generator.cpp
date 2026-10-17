#include "ChurnGenerator.hpp"
#include "Random.hpp"
#include "TestFramework.hpp"
#include <algorithm>
#include <set>
#include <utility>

namespace {

uint64_t fnv1a(const std::vector<GraphOperation>& ops) {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](uint64_t x) {
        for (int i = 0; i < 8; ++i) {
            h ^= (x >> (8 * i)) & 0xff;
            h *= 1099511628211ULL;
        }
    };
    for (const auto& op : ops) {
        mix(static_cast<uint64_t>(op.type));
        mix(static_cast<uint64_t>(op.u));
        mix(static_cast<uint64_t>(op.v));
    }
    return h;
}

ChurnConfig base_config() {
    ChurnConfig cfg;
    cfg.num_vertices = 100;
    cfg.num_operations = 5000;
    cfg.p_insert = 0.4;
    cfg.p_delete = 0.3;
    cfg.seed = 7;
    return cfg;
}

// Replays a workload and checks its semantics: no self-loops, deletes hit present edges,
// inserts hit absent edges unless duplicates are allowed. Returns the final edge set.
std::set<std::pair<int, int>> replay(const Workload& w, const ChurnConfig& cfg) {
    std::set<std::pair<int, int>> present;
    size_t dup = 0;
    for (const auto& op : w.ops) {
        CHECK(op.u >= 0 && op.u < cfg.num_vertices && op.v >= 0 && op.v < cfg.num_vertices);
        auto e = std::pair{std::min(op.u, op.v), std::max(op.u, op.v)};
        if (op.type == OpType::INSERT) {
            CHECK(op.u != op.v);
            if (!present.insert(e).second) {
                CHECK(cfg.allow_duplicate_inserts);
                ++dup;
            }
        } else if (op.type == OpType::DELETE) {
            CHECK_MSG(present.erase(e) == 1, "delete of absent edge");
        }
    }
    CHECK(dup == w.stats.duplicate_inserts);
    CHECK(present.size() == w.stats.final_edges);
    return present;
}

}  // namespace

TEST_CASE("rng_bounded_is_in_range_and_unbiased") {
    dc::SplitMix64 rng(123);
    const int buckets = 7;
    std::vector<int> counts(buckets, 0);
    const int draws = 700000;
    for (int i = 0; i < draws; ++i) {
        uint64_t x = rng.below(buckets);
        CHECK(x < buckets);
        ++counts[x];
    }
    // Each bucket expects 100000; 5 sigma is about 1500.
    for (int c : counts) CHECK_MSG(c > 98500 && c < 101500, "bucket count " << c);
    for (int i = 0; i < 1000; ++i) {
        double d = rng.uniform01();
        CHECK(d >= 0.0 && d < 1.0);
    }
}

TEST_CASE("generator_exact_op_count_and_semantics") {
    for (bool dup : {false, true}) {
        ChurnConfig cfg = base_config();
        cfg.allow_duplicate_inserts = dup;
        Workload w = generate_churn_workload(cfg);
        CHECK(w.ops.size() == static_cast<size_t>(cfg.num_operations));
        CHECK(w.stats.inserts + w.stats.deletes + w.stats.queries == w.ops.size());
        replay(w, cfg);
        if (!dup) CHECK(w.stats.duplicate_inserts == 0);
    }
}

TEST_CASE("generator_deterministic") {
    ChurnConfig cfg = base_config();
    Workload a = generate_churn_workload(cfg);
    Workload b = generate_churn_workload(cfg);
    CHECK(fnv1a(a.ops) == fnv1a(b.ops));
    cfg.seed = 8;
    Workload c = generate_churn_workload(cfg);
    CHECK(fnv1a(a.ops) != fnv1a(c.ops));
}

// Pins the exact stream for one config. If this changes, every previously generated trace
// is invalid: bump the trace format version (Phase 6) instead of updating the constant.
// Must pass identically under GCC and Clang.
TEST_CASE("generator_golden_hash") {
    Workload w = generate_churn_workload(base_config());
    CHECK_MSG(fnv1a(w.ops) == 0x338d22c01f35b497ULL, "hash = 0x" << std::hex << fnv1a(w.ops));
}

TEST_CASE("generator_density_control") {
    ChurnConfig cfg = base_config();
    cfg.num_operations = 20000;
    cfg.p_insert = 0.45;
    cfg.p_delete = 0.45;
    cfg.target_edges = 300;
    Workload w = generate_churn_workload(cfg);
    replay(w, cfg);

    // Once the target is reached, the edge count never leaves [target - 1, target + 1].
    int64_t edges = 0;
    bool reached = false;
    for (const auto& op : w.ops) {
        if (op.type == OpType::INSERT) ++edges;
        if (op.type == OpType::DELETE) --edges;
        if (edges == cfg.target_edges) reached = true;
        if (reached) CHECK_MSG(edges >= cfg.target_edges - 1 && edges <= cfg.target_edges + 1, "edges=" << edges);
    }
    CHECK(reached);
}

TEST_CASE("generator_tiny_graphs") {
    for (int n : {1, 2, 3}) {
        ChurnConfig cfg = base_config();
        cfg.num_vertices = n;
        cfg.num_operations = 500;
        Workload w = generate_churn_workload(cfg);
        CHECK(w.ops.size() == 500);
        replay(w, cfg);
        if (n == 1) CHECK(w.stats.inserts == 0 && w.stats.deletes == 0);
        if (n == 2) CHECK(w.stats.final_edges <= 1);
    }
}
