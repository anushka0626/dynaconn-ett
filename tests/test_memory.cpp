// Built as its own executable with DC_MEMORY_ACCOUNTING (see CMakeLists.txt).
#define DC_INSTALL_GLOBAL_ALLOC_HOOKS
#include "MemoryAccounting.hpp"

#include "BaselineBFS.hpp"
#include "BaselineDSU.hpp"
#include "CachedBFS.hpp"
#include "ChurnGenerator.hpp"
#include "DNDTree.hpp"
#include "DTree.hpp"
#include "DynamicGraphETT.hpp"
#include "TestFramework.hpp"
#include <memory>

#ifndef DC_MEMORY_ACCOUNTING
#error "test_memory must be compiled with DC_MEMORY_ACCOUNTING"
#endif

namespace {

using dc::MemCategory;

const dc::MemCounters& cat(MemCategory c) { return dc::category_counters()[static_cast<int>(c)]; }

bool all_categories_zero() {
    for (const auto& c : dc::category_counters())
        if (c.requested_bytes || c.usable_bytes || c.allocations) return false;
    return true;
}

Workload insert_only(int n, int edges) {
    ChurnConfig cfg;
    cfg.num_vertices = n;
    cfg.num_operations = edges;
    cfg.p_insert = 1.0;
    cfg.seed = 99;
    return generate_churn_workload(cfg);
}

template <class S>
void build_and_destroy_returns_to_zero() {
    Workload w = insert_only(300, 1200);
    CHECK(all_categories_zero());
    size_t global_before = dc::global_alloc_counters().allocations;
    {
        S s(300);
        for (const auto& op : w.ops) s.insert_edge(op.u, op.v);
        for (size_t i = 0; i < w.ops.size(); i += 2) s.delete_edge(w.ops[i].u, w.ops[i].v);
        CHECK(!all_categories_zero());
        // The global hooks see every tagged allocation (and more).
        size_t tagged_usable = 0;
        for (const auto& c : dc::category_counters()) tagged_usable += c.usable_bytes;
        CHECK(dc::global_alloc_counters().usable_bytes >= tagged_usable);
    }
    CHECK_MSG(all_categories_zero(), "tagged counters not back to zero after destruction");
    CHECK(dc::global_alloc_counters().allocations == global_before);
}

}  // namespace

TEST_CASE("memory_counters_return_to_zero") {
    build_and_destroy_returns_to_zero<BaselineBFS>();
    build_and_destroy_returns_to_zero<BaselineDSU>();
    build_and_destroy_returns_to_zero<LazyDSU>();
    build_and_destroy_returns_to_zero<CachedBFS>();
    build_and_destroy_returns_to_zero<EttP0>();
    build_and_destroy_returns_to_zero<EttF0>();
    build_and_destroy_returns_to_zero<EttF1>();
    build_and_destroy_returns_to_zero<EttF2>();
    build_and_destroy_returns_to_zero<EttF2KHop<2>>();
    build_and_destroy_returns_to_zero<DTreeP0>();
    build_and_destroy_returns_to_zero<DTreeF2>();
    build_and_destroy_returns_to_zero<DNDTree>();
}

// Node storage is accounted exactly: pointer nodes are one 40 B allocation each; the arena
// is one reserved block of 4V * 32 B (no growth while nodes <= 4V).
TEST_CASE("memory_treap_node_bytes_exact") {
    const int n = 500;
    Workload w = insert_only(n, 2000);

    {
        DynamicGraphETT p(n);
        for (const auto& op : w.ops) p.insert_edge(op.u, op.v);
        size_t nodes = n + 2 * p.euler_forest().num_tree_edges();
        CHECK(cat(MemCategory::TreapNodes).allocations == nodes);
        CHECK(cat(MemCategory::TreapNodes).requested_bytes == nodes * sizeof(PointerNode));
    }
    {
        FlatDynamicGraphETT f(n);
        for (const auto& op : w.ops) f.insert_edge(op.u, op.v);
        CHECK(cat(MemCategory::TreapNodes).allocations == 1);
        CHECK(cat(MemCategory::TreapNodes).requested_bytes == 4 * n * sizeof(ArenaNode));
    }
}
