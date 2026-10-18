#pragma once
#include "DynamicConnectivity.hpp"
#include "MemoryAccounting.hpp"
#include "OrderedSets.hpp"
#include "Stats.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

// Adjacency sets with O(1) updates; every query runs a BFS from u until it reaches v,
// O(V + E) in the worst case. The strategy is naive, the engineering is not:
// - visited marks are epoch-stamped, so a query costs only what it visits (no O(V) clear;
//   this matters for local queries, P3 in the pre-registration);
// - adjacency is the shared flat layout, and the BFS queue is a reused vector.
class BaselineBFS : public DynamicConnectivity {
    dc::FlatOrderedSets<dc::MemCategory::Adjacency> adj;
    size_t edges = 0;
    std::vector<uint32_t, dc::Alloc<uint32_t, dc::MemCategory::Scratch>> visit_stamp;
    std::vector<int32_t, dc::Alloc<int32_t, dc::MemCategory::Scratch>> queue;
    uint32_t epoch = 0;

public:
    explicit BaselineBFS(int vertices) : adj(vertices), visit_stamp(vertices, 0) {}

    void insert_edge(int u, int v) override {
        if (u == v) return;
        if (adj.insert(u, v)) {
            adj.insert(v, u);
            ++edges;
        }
    }

    void delete_edge(int u, int v) override {
        if (adj.erase(u, v)) {
            adj.erase(v, u);
            --edges;
        }
    }

    bool is_connected(int u, int v) override {
        if (u == v) return true;
        if (++epoch == 0) {  // stamp wrap-around
            std::fill(visit_stamp.begin(), visit_stamp.end(), 0);
            epoch = 1;
        }
        queue.clear();
        queue.push_back(u);
        visit_stamp[u] = epoch;

        for (size_t head = 0; head < queue.size(); ++head) {
            int32_t curr = queue[head];
            DC_COUNT(bfs_vertices_visited, 1);
            for (int32_t neighbor : adj.items(curr)) {
                if (neighbor == v) return true;
                if (visit_stamp[neighbor] != epoch) {
                    visit_stamp[neighbor] = epoch;
                    queue.push_back(neighbor);
                }
            }
        }
        return false;
    }

    size_t edge_count() const override { return edges; }
};
