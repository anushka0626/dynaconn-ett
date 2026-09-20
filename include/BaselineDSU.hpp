#pragma once
#include "DynamicConnectivity.hpp"
#include <vector>
#include <set>
#include <numeric>
#include <utility>

class BaselineDSU : public DynamicConnectivity {
    int n;
    std::vector<int> parent;
    std::vector<int> rank;
    std::set<std::pair<int, int>> edges;

    int find(int i) {
        if (parent[i] == i)
            return i;
        return parent[i] = find(parent[i]);
    }

    void unite(int i, int j) {
        int root_i = find(i);
        int root_j = find(j);
        if (root_i != root_j) {
            if (rank[root_i] < rank[root_j])
                std::swap(root_i, root_j);
            parent[root_j] = root_i;
            if (rank[root_i] == rank[root_j])
                rank[root_i]++;
        }
    }

    void rebuild() {
        std::iota(parent.begin(), parent.end(), 0);
        std::fill(rank.begin(), rank.end(), 0);
        for (const auto& [u, v] : edges) {
            unite(u, v);
        }
    }

public:
    explicit BaselineDSU(int vertices) : n(vertices), parent(vertices), rank(vertices, 0) {
        std::iota(parent.begin(), parent.end(), 0);
    }

    void insert_edge(int u, int v) override {
        if (u == v) return;
        int a = std::min(u, v), b = std::max(u, v);
        if (edges.insert({a, b}).second) {
            unite(a, b);
        }
    }

    void delete_edge(int u, int v) override {
        int a = std::min(u, v), b = std::max(u, v);
        if (edges.erase({a, b})) {
            rebuild(); // Full structural invalidation on removal
        }
    }

    bool is_connected(int u, int v) override {
        return find(u) == find(v);
    }
};