#pragma once
#include <algorithm>
#include <numeric>
#include <set>
#include <utility>
#include <vector>

// Ground truth for the tests, independent of every structure under test: an edge set,
// with component labels recomputed from scratch (plain union-find) when the graph changed.
class Oracle {
    int n;
    std::set<std::pair<int, int>> edges;
    std::vector<int> label;
    bool dirty = true;

    void relabel() {
        std::vector<int> parent(n);
        std::iota(parent.begin(), parent.end(), 0);
        auto find = [&](int x) {
            while (parent[x] != x) x = parent[x] = parent[parent[x]];
            return x;
        };
        for (auto [a, b] : edges) parent[find(a)] = find(b);
        label.resize(n);
        for (int i = 0; i < n; ++i) label[i] = find(i);
        dirty = false;
    }

public:
    explicit Oracle(int vertices) : n(vertices) {}

    void insert_edge(int u, int v) {
        if (u != v && edges.insert({std::min(u, v), std::max(u, v)}).second) dirty = true;
    }
    void delete_edge(int u, int v) {
        if (edges.erase({std::min(u, v), std::max(u, v)})) dirty = true;
    }
    bool is_connected(int u, int v) {
        if (dirty) relabel();
        return label[u] == label[v];
    }
    size_t edge_count() const { return edges.size(); }
};
