// Built as its own executable with DC_STATS (see CMakeLists.txt).
#include "ChurnGenerator.hpp"
#include "DTree.hpp"
#include "DynamicGraphETT.hpp"
#include "HDT.hpp"
#include "HKS.hpp"
#include "Oracle.hpp"
#include "Stats.hpp"
#include "TestFramework.hpp"

#ifndef DC_STATS
#error "test_stats must be compiled with DC_STATS"
#endif

namespace {

Workload dense_churn(uint64_t seed) {
    ChurnConfig cfg;
    cfg.num_vertices = 300;
    cfg.num_operations = 6000;
    cfg.p_insert = 0.45;
    cfg.p_delete = 0.45;
    cfg.seed = seed;
    cfg.target_edges = 3 * 300;
    return generate_churn_workload(cfg);
}

template <class S>
dc::OpStats run(const Workload& w) {
    dc::stats() = {};
    S s(300);
    for (const auto& op : w.ops) {
        if (op.type == OpType::INSERT) s.insert_edge(op.u, op.v);
        else if (op.type == OpType::DELETE) s.delete_edge(op.u, op.v);
        else s.is_connected(op.u, op.v);
    }
    return dc::stats();
}

}  // namespace

// The counters must be internally consistent, since RQ4's model and RQ5's H5b rest on them.
TEST_CASE("stats_counters_consistent") {
    for (uint64_t seed = 1; seed <= 5; ++seed) {
        Workload w = dense_churn(seed);

        dc::OpStats full = run<EttF2>(w);
        CHECK(full.tree_edge_deletes > 0);
        CHECK(full.replacements_found <= full.tree_edge_deletes);
        CHECK(full.khop_searches == 0 && full.khop_edges_scanned == 0);
        CHECK(full.root_walk_steps > 0 && full.treap_splits > 0 && full.treap_merges > 0);

        dc::OpStats k2 = run<EttF2KHop<2>>(w);
        CHECK(k2.khop_searches == k2.tree_edge_deletes);
        CHECK(k2.khop_hits <= k2.khop_searches);
        CHECK(k2.replacements_found <= k2.tree_edge_deletes);
    }
}

// HKS's sampling branch must actually run (weight(T1) >= floor(log2(n)^2)) and stay correct:
// dense graphs, checked against the oracle.
TEST_CASE("hks_sampling_branch_exercised") {
    uint64_t draws = 0, hits = 0;
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        const int n = 64;  // k = 36
        ChurnConfig cfg;
        cfg.num_vertices = n;
        cfg.num_operations = 4000;
        cfg.p_insert = 0.3;
        cfg.p_delete = 0.3;
        cfg.seed = seed;
        cfg.target_edges = 12 * n;
        Workload w = generate_churn_workload(cfg);
        dc::stats() = {};
        HksF2 h(n);
        Oracle o(n);
        for (const auto& op : w.ops) {
            if (op.type == OpType::INSERT) {
                h.insert_edge(op.u, op.v);
                o.insert_edge(op.u, op.v);
            } else if (op.type == OpType::DELETE) {
                h.delete_edge(op.u, op.v);
                o.delete_edge(op.u, op.v);
            } else {
                CHECK(h.is_connected(op.u, op.v) == o.is_connected(op.u, op.v));
            }
        }
        CHECK(h.check_invariants().empty());
        draws += dc::stats().sample_draws;
        hits += dc::stats().sample_hits;
    }
    CHECK_MSG(draws > 0 && hits > 0, "draws=" << draws << " hits=" << hits);
}

// Layout must not change the operation counts: P0 and F2 do identical work on identical traces.
TEST_CASE("stats_identical_across_layouts") {
    Workload w = dense_churn(7);
    dc::OpStats p0 = run<EttP0>(w);
    dc::OpStats f2 = run<EttF2>(w);
    CHECK(p0.root_walk_steps == f2.root_walk_steps);
    CHECK(p0.index_walk_steps == f2.index_walk_steps);
    CHECK(p0.treap_splits == f2.treap_splits && p0.treap_merges == f2.treap_merges);
    CHECK(p0.replacement_edges_scanned == f2.replacement_edges_scanned);
    CHECK(p0.replacements_found == f2.replacements_found);

    dc::OpStats hp = run<HksP0>(w);
    dc::OpStats hf = run<HksF2>(w);
    CHECK(hp.tree_edge_deletes > 0);
    CHECK(hp.replacement_edges_scanned == hf.replacement_edges_scanned);
    CHECK(hp.replacements_found == hf.replacements_found);

    dc::OpStats tp = run<HdtP0>(w);
    dc::OpStats tf = run<HdtF2>(w);
    CHECK(tp.tree_edge_deletes > 0);
    CHECK(tp.replacement_edges_scanned == tf.replacement_edges_scanned);
    CHECK(tp.replacements_found == tf.replacements_found);
    CHECK(tp.root_walk_steps == tf.root_walk_steps);
    CHECK_MSG(tp.level_raises > 0 && tp.level_raises == tf.level_raises, "HDT never raised a level");

    dc::OpStats dp = run<DTreeP0>(w);
    dc::OpStats df = run<DTreeF2>(w);
    CHECK(dp.tree_edge_deletes > 0);
    CHECK(dp.root_walk_steps == df.root_walk_steps);
    CHECK(dp.replacement_edges_scanned == df.replacement_edges_scanned);
    CHECK(dp.replacements_found == df.replacements_found);
}
