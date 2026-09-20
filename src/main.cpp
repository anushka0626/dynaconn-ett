#include <iostream>
#include <cassert>
#include "DynamicGraphETT.hpp"
#include "BaselineBFS.hpp"
#include "BaselineDSU.hpp"

int main() {
    std::cout << "========================================================\n";
    std::cout << "  PHASE 1 VALIDATION: Dynamic Graph ETT (Arbitrary Graphs)\n";
    std::cout << "========================================================\n\n";

    const int V = 8;
    DynamicGraphETT ett(V);
    BaselineBFS bfs(V);
    BaselineDSU dsu(V);

    auto insert_e = [&](int u, int v) {
        std::cout << "[OP] INSERT (" << u << ", " << v << ")\n";
        ett.insert_edge(u, v);
        bfs.insert_edge(u, v);
        dsu.insert_edge(u, v);
    };

    auto delete_e = [&](int u, int v) {
        std::cout << "[OP] DELETE (" << u << ", " << v << ")\n";
        ett.delete_edge(u, v);
        bfs.delete_edge(u, v);
        dsu.delete_edge(u, v);
    };

    auto verify_query = [&](int u, int v) {
        bool res_ett = ett.is_connected(u, v);
        bool res_bfs = bfs.is_connected(u, v);
        bool res_dsu = dsu.is_connected(u, v);

        std::cout << "  -> Query (" << u << " ~ " << v << "): ETT=" 
                  << res_ett << " | BFS=" << res_bfs << " | DSU=" << res_dsu;

        assert(res_ett == res_bfs && res_bfs == res_dsu);
        std::cout << " [PASSED]\n";
    };

    // 1. Build Triangle Component A: (0-1), (1-2), (2-0)
    std::cout << "--- Step 1: Form Triangle Cycle on (0, 1, 2) ---\n";
    insert_e(0, 1);
    insert_e(1, 2);
    insert_e(2, 0); // Non-tree edge cycle created

    verify_query(0, 2);
    verify_query(0, 1);
    verify_query(0, 3); // Disjoint

    // 2. Build Triangle Component B: (3-4), (4-5), (5-3)
    std::cout << "\n--- Step 2: Form Triangle Cycle on (3, 4, 5) ---\n";
    insert_e(3, 4);
    insert_e(4, 5);
    insert_e(5, 3);

    verify_query(3, 5);
    verify_query(1, 4); // Still disjoint

    // 3. Connect Components via Single Bridge (2, 3)
    std::cout << "\n--- Step 3: Add Bridge Edge (2, 3) ---\n";
    insert_e(2, 3);
    verify_query(0, 5);

    // 4. Delete tree edge within triangle cycle (0, 1) -> Alternate path (0-2-1) exists
    std::cout << "\n--- Step 4: Cut Tree Edge (0, 1) with Alternate Path Active ---\n";
    delete_e(0, 1);
    // (0, 1) was a tree edge; replacement search must locate non-tree edge (2, 0) or (1, 2)
    verify_query(0, 1); // Must REMAIN connected via replacement promotion
    verify_query(0, 5);

    // 5. Sever the bridge edge (2, 3) -> No replacement edge exists
    std::cout << "\n--- Step 5: Cut Bridge Edge (2, 3) ---\n";
    delete_e(2, 3);
    verify_query(0, 5); // Now false
    verify_query(0, 2); // Component A still intact
    verify_query(3, 5); // Component B still intact

    // 6. Test isolated vertices (6, 7)
    std::cout << "\n--- Step 6: Link and Cut Disjoint Nodes (6, 7) ---\n";
    insert_e(6, 7);
    verify_query(6, 7);
    verify_query(0, 6);
    delete_e(6, 7);
    verify_query(6, 7);

    std::cout << "\nAll test scenarios verified successfully across general cyclic graphs!\n";
    return 0;
}