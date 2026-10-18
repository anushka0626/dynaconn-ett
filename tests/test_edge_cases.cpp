#include "BaselineBFS.hpp"
#include "BaselineDSU.hpp"
#include "CachedBFS.hpp"
#include "DNDTree.hpp"
#include "DTree.hpp"
#include "DynamicGraphETT.hpp"
#include "HDT.hpp"
#include "HKS.hpp"
#include "TestFramework.hpp"

// Hand-written cases, run against every structure.

namespace {

template <class S>
void self_loops() {
    S s(5);
    s.insert_edge(3, 3);
    CHECK(s.edge_count() == 0);
    CHECK(s.is_connected(3, 3));
    CHECK(!s.is_connected(3, 4));
    s.delete_edge(3, 3);
    CHECK(s.edge_count() == 0);
}

template <class S>
void duplicate_inserts() {
    S s(4);
    s.insert_edge(0, 1);
    s.insert_edge(0, 1);
    s.insert_edge(1, 0);
    CHECK(s.edge_count() == 1);
    s.delete_edge(0, 1);  // one delete removes the edge entirely
    CHECK(s.edge_count() == 0);
    CHECK(!s.is_connected(0, 1));
}

template <class S>
void deleting_absent_edges() {
    S s(4);
    s.delete_edge(0, 1);  // empty graph
    s.insert_edge(0, 1);
    s.insert_edge(1, 2);
    s.delete_edge(0, 2);  // connected by a path, but not an edge
    CHECK(s.edge_count() == 2);
    CHECK(s.is_connected(0, 2));
    s.delete_edge(2, 3);
    CHECK(s.edge_count() == 2);
}

template <class S>
void delete_then_reinsert() {
    S s(3);
    for (int round = 0; round < 3; ++round) {
        s.insert_edge(0, 1);
        s.insert_edge(1, 2);
        CHECK(s.is_connected(0, 2));
        s.delete_edge(1, 2);
        CHECK(!s.is_connected(0, 2));
        s.delete_edge(0, 1);
        CHECK(!s.is_connected(0, 1));
        CHECK(s.edge_count() == 0);
    }
}

template <class S>
void single_vertex() {
    S s(1);
    CHECK(s.is_connected(0, 0));
    s.insert_edge(0, 0);
    s.delete_edge(0, 0);
    CHECK(s.edge_count() == 0);
}

template <class S>
void isolated_vertices() {
    S s(6);
    s.insert_edge(0, 1);
    for (int i = 2; i < 6; ++i)
        for (int j = 0; j < 6; ++j)
            if (i != j) CHECK(!s.is_connected(i, j));
}

template <class S>
void bridge_without_replacement() {
    // Two triangles joined by the bridge (2, 3).
    S s(6);
    for (auto [u, v] : {std::pair{0, 1}, {1, 2}, {2, 0}, {3, 4}, {4, 5}, {5, 3}, {2, 3}}) s.insert_edge(u, v);
    CHECK(s.is_connected(0, 5));
    s.delete_edge(3, 2);  // reversed orientation
    CHECK(!s.is_connected(0, 5));
    CHECK(s.is_connected(0, 2));
    CHECK(s.is_connected(3, 5));
    CHECK(s.edge_count() == 6);
}

template <class S>
void cycle_cut_uses_replacement() {
    S s(5);
    s.insert_edge(0, 1);
    s.insert_edge(1, 2);
    s.insert_edge(2, 0);  // closes a cycle: non-tree edge
    s.insert_edge(2, 3);  // bridge
    s.delete_edge(0, 1);  // tree edge on the cycle
    CHECK(s.is_connected(0, 3));
    CHECK(s.is_connected(1, 0));
    s.delete_edge(2, 3);
    CHECK(!s.is_connected(0, 3));
}

template <class S>
void promoted_edge_reversed_endpoints() {
    // Path 0-1-2 plus non-tree edge (0, 2). Cutting (1, 2) makes {2} the smaller side, so
    // the replacement is found from vertex 2 and linked as (2, 0), reversed from the
    // normalized (0, 2). Deleting it again, in either orientation, must find it.
    for (bool reversed : {false, true}) {
        S s(3);
        s.insert_edge(0, 1);
        s.insert_edge(1, 2);
        s.insert_edge(0, 2);
        s.delete_edge(1, 2);
        CHECK(s.is_connected(0, 2));
        CHECK(s.is_connected(1, 2));
        if (reversed) s.delete_edge(2, 0);
        else s.delete_edge(0, 2);
        CHECK(!s.is_connected(1, 2));
        CHECK(s.is_connected(0, 1));
        CHECK(s.edge_count() == 1);
    }
}

template <class S>
void star_center_removal() {
    // Star around 0 with a ring on the leaves: removing every spoke keeps the leaves connected.
    const int leaves = 8;
    S s(leaves + 1);
    for (int i = 1; i <= leaves; ++i) s.insert_edge(0, i);
    for (int i = 1; i <= leaves; ++i) s.insert_edge(i, i % leaves + 1);
    for (int i = 1; i <= leaves; ++i) s.delete_edge(i, 0);
    CHECK(!s.is_connected(0, 1));
    for (int i = 2; i <= leaves; ++i) CHECK(s.is_connected(1, i));
    CHECK(s.edge_count() == static_cast<size_t>(leaves));
}

template <class S>
void run_all() {
    self_loops<S>();
    duplicate_inserts<S>();
    deleting_absent_edges<S>();
    delete_then_reinsert<S>();
    single_vertex<S>();
    isolated_vertices<S>();
    bridge_without_replacement<S>();
    cycle_cut_uses_replacement<S>();
    promoted_edge_reversed_endpoints<S>();
    star_center_removal<S>();
}

}  // namespace

TEST_CASE("edge_cases_baselines") {
    run_all<BaselineBFS>();
    run_all<BaselineDSU>();
    run_all<LazyDSU>();
    run_all<CachedBFS>();
}
TEST_CASE("edge_cases_ett_layouts") {
    run_all<EttP0>();
    run_all<EttF0>();
    run_all<EttF1>();
    run_all<EttF2>();
}
TEST_CASE("edge_cases_dtree") {
    run_all<DTreeP0>();
    run_all<DTreeF2>();
}
TEST_CASE("edge_cases_dnd") { run_all<DNDTree>(); }
TEST_CASE("edge_cases_hdt") {
    run_all<HdtP0>();
    run_all<HdtF2>();
}
TEST_CASE("edge_cases_hks") {
    run_all<HksP0>();
    run_all<HksF2>();
}
TEST_CASE("edge_cases_ett_khop") {
    run_all<EttF2KHop<1>>();
    run_all<EttF2KHop<2>>();
    run_all<EttF2KHop<3>>();
}
