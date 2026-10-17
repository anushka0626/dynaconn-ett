// Phase 2 driver, ported to the Phase 4 code: a parity check across all four structures,
// then each structure's measured memory footprint on the same graph.
// For the full per-category breakdown, use memory_profile.
#define DC_INSTALL_GLOBAL_ALLOC_HOOKS
#include "MemoryAccounting.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>
#include "BaselineBFS.hpp"
#include "BaselineDSU.hpp"
#include "DynamicGraphETT.hpp"

namespace {

// Heap bytes (usable + glibc chunk header) and allocation count used by one structure,
// measured with the global operator new/delete hooks while it is the only thing being built.
template <class S>
std::pair<size_t, size_t> measure_heap(int vertices, const std::vector<std::pair<int, int>>& edges,
                                       size_t& distinct_edges) {
    dc::MemCounters before = dc::global_alloc_counters();
    auto s = std::make_unique<S>(vertices);
    for (auto [u, v] : edges) s->insert_edge(u, v);
    dc::MemCounters after = dc::global_alloc_counters();
    distinct_edges = s->edge_count();
    size_t allocations = after.allocations - before.allocations;
    size_t usable = after.usable_bytes - before.usable_bytes;
    return {usable + dc::kGlibcChunkHeader * allocations, allocations};
}

void print_memory_row(const std::string& name, size_t bytes, size_t allocations, size_t edges) {
    std::cout << std::left << std::setw(25) << name << " | Total: " << std::setw(8) << std::fixed
              << std::setprecision(1) << (bytes / 1024.0) << " KB"
              << " | Bytes/Edge: " << std::setw(8) << std::setprecision(2) << static_cast<double>(bytes) / edges
              << " | Allocations: " << allocations << "\n";
}

}  // namespace

int main() {
    std::cout << "===============================================================\n";
    std::cout << "  PHASE 2: Flat Arena Dynamic Graph ETT & Memory Footprint Eval\n";
    std::cout << "===============================================================\n\n";

    const int V = 500;
    const int EDGES_TO_INSERT = 2000;

    BaselineBFS bfs(V);
    BaselineDSU dsu(V);
    DynamicGraphETT pointer_ett(V);
    FlatDynamicGraphETT flat_ett(V);

    std::cout << "1. Verifying Behavioral Parity across complex cycle cuts...\n";

    // Structural parity verification
    auto insert_all = [&](int u, int v) {
        bfs.insert_edge(u, v);
        dsu.insert_edge(u, v);
        pointer_ett.insert_edge(u, v);
        flat_ett.insert_edge(u, v);
    };

    auto delete_all = [&](int u, int v) {
        bfs.delete_edge(u, v);
        dsu.delete_edge(u, v);
        pointer_ett.delete_edge(u, v);
        flat_ett.delete_edge(u, v);
    };

    // An explicit check rather than assert(), so a Release (NDEBUG) build cannot print
    // [PASSED] without checking anything.
    auto check_all = [&](int u, int v) {
        bool b = bfs.is_connected(u, v);
        bool d = dsu.is_connected(u, v);
        bool p = pointer_ett.is_connected(u, v);
        bool f = flat_ett.is_connected(u, v);
        if (!(b == d && d == p && p == f)) {
            std::cerr << "   -> Parity FAILED on (" << u << ", " << v << "): BFS=" << b << " DSU=" << d
                      << " PointerETT=" << p << " FlatETT=" << f << "\n";
            std::exit(1);
        }
    };

    // Cycle & bridge assertions
    insert_all(10, 20);
    insert_all(20, 30);
    insert_all(30, 10); // Cycle
    insert_all(30, 40); // Bridge
    check_all(10, 40);

    delete_all(10, 20); // Internal cycle cut (replacement promoted)
    check_all(10, 40);

    delete_all(30, 40); // Bridge severed
    check_all(10, 40);
    check_all(10, 30);

    std::cout << "   -> All 4 structures produced identical connectivity verdicts. [PASSED]\n";
    std::cout << "      (Randomized parity over many seeds: ctest, tests/test_fuzz.cpp)\n\n";

    std::cout << "2. Populating graph with V=" << V << ", " << EDGES_TO_INSERT << " insert attempts for Memory Profiling...\n";

    // Populate a denser graph. Each structure is built separately from the same edge list,
    // so its heap usage can be measured on its own.
    std::mt19937 rng(42);
    std::vector<std::pair<int, int>> edges;
    for (int i = 0; i < EDGES_TO_INSERT; ++i) {
        int u = rng() % V;
        int v = rng() % V;
        if (u != v) edges.push_back({u, v});
    }

    size_t distinct = 0;
    auto [dsu_bytes, dsu_allocs] = measure_heap<BaselineDSU>(V, edges, distinct);
    auto [bfs_bytes, bfs_allocs] = measure_heap<BaselineBFS>(V, edges, distinct);
    auto [pointer_bytes, pointer_allocs] = measure_heap<DynamicGraphETT>(V, edges, distinct);
    auto [flat_bytes, flat_allocs] = measure_heap<FlatDynamicGraphETT>(V, edges, distinct);

    std::cout << "   -> " << distinct << " distinct edges (duplicate draws are no-ops)\n";
    std::cout << "\n---------------------------------------------------------------\n";
    std::cout << " DATA STRUCTURE MEMORY FOOTPRINT COMPARISON (measured heap bytes)\n";
    std::cout << "---------------------------------------------------------------\n";
    print_memory_row("Baseline DSU (Edge List)", dsu_bytes, dsu_allocs, distinct);
    print_memory_row("Baseline BFS (Adj Set)", bfs_bytes, bfs_allocs, distinct);
    print_memory_row("Pointer Treap ETT", pointer_bytes, pointer_allocs, distinct);
    print_memory_row("Flat Arena ETT (Vector)", flat_bytes, flat_allocs, distinct);
    std::cout << "---------------------------------------------------------------\n";

    return 0;
}
