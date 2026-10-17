#pragma once
#include "DynamicConnectivity.hpp"
#include "MemoryAccounting.hpp"
#include "OrderedSets.hpp"
#include "Stats.hpp"
#include <cstdint>
#include <numeric>
#include <vector>

// Cached component labels. A full BFS flood fill labels every vertex with its component.
// Inserts keep the cache valid by merging labels in a small union-find over label ids.
// A delete may split a component, so it invalidates the cache, and the next query
// recomputes all labels: O(V + E).
// Compared with LazyDSU, it rebuilds by traversing adjacency (sequential, cache-friendly)
// rather than by unioning an edge list.
class CachedBFS : public DynamicConnectivity {
    using IntVec = std::vector<int32_t, dc::Alloc<int32_t, dc::MemCategory::VertexIndex>>;

    int n;
    dc::FlatOrderedSets<dc::MemCategory::Adjacency> adj;
    size_t edges = 0;
    IntVec label;         // vertex -> component label from the last flood fill
    IntVec label_parent;  // union-find over labels, for merges since that fill
    std::vector<int32_t, dc::Alloc<int32_t, dc::MemCategory::Scratch>> queue;
    bool valid = false;

    int32_t find_label(int32_t l) {
        int32_t root = l;
        while (label_parent[root] != root) root = label_parent[root];
        while (label_parent[l] != root) {
            int32_t next = label_parent[l];
            label_parent[l] = root;
            l = next;
        }
        return root;
    }

    void relabel() {
        DC_COUNT(dsu_rebuilds, 1);
        std::fill(label.begin(), label.end(), -1);
        int32_t next_label = 0;
        for (int32_t s = 0; s < n; ++s) {
            if (label[s] != -1) continue;
            label[s] = next_label;
            queue.clear();
            queue.push_back(s);
            for (size_t head = 0; head < queue.size(); ++head) {
                DC_COUNT(bfs_vertices_visited, 1);
                for (int32_t y : adj.items(queue[head])) {
                    if (label[y] == -1) {
                        label[y] = next_label;
                        queue.push_back(y);
                    }
                }
            }
            ++next_label;
        }
        std::iota(label_parent.begin(), label_parent.begin() + next_label, 0);
        valid = true;
    }

public:
    explicit CachedBFS(int vertices) : n(vertices), adj(vertices), label(vertices, -1), label_parent(vertices) {}

    void insert_edge(int u, int v) override {
        if (u == v || !adj.insert(u, v)) return;
        adj.insert(v, u);
        ++edges;
        if (valid) {
            int32_t lu = find_label(label[u]), lv = find_label(label[v]);
            if (lu != lv) label_parent[lu] = lv;
        }
    }

    void delete_edge(int u, int v) override {
        if (!adj.erase(u, v)) return;
        adj.erase(v, u);
        --edges;
        valid = false;
    }

    bool is_connected(int u, int v) override {
        if (u == v) return true;
        if (!valid) relabel();
        return find_label(label[u]) == find_label(label[v]);
    }

    size_t edge_count() const override { return edges; }
};
