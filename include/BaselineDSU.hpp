#pragma once
#include "DynamicConnectivity.hpp"
#include "FlatHashMap.hpp"
#include "MemoryAccounting.hpp"
#include "Stats.hpp"
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <utility>
#include <vector>

// Union-find (path compression + union by rank) over the current edge list, which is
// rebuilt from scratch when a deletion has invalidated it.
//   Lazy = false (BaselineDSU): rebuild on every delete. A naive reference point.
//   Lazy = true  (LazyDSU): a delete only marks the forest stale; the next query rebuilds.
//                This is "batched static recomputation": consecutive deletes cost one rebuild.
// Neither is the DSU state of the art (see Xu et al.'s DND-trees).
template <bool Lazy>
class DSUConnectivity : public DynamicConnectivity {
    using IntVec = std::vector<int32_t, dc::Alloc<int32_t, dc::MemCategory::VertexIndex>>;

    int n;
    IntVec parent;
    IntVec rank;
    // Edge list with O(1) delete: edges[i] = (a, b), position[(a, b)] = i.
    std::vector<std::pair<int32_t, int32_t>, dc::Alloc<std::pair<int32_t, int32_t>, dc::MemCategory::EdgeMap>> edges;
    dc::FlatHashMap<uint32_t, dc::MemCategory::EdgeMap> position;
    bool stale = false;

    int32_t find(int32_t i) {
        int32_t root = i;
        while (parent[root] != root) root = parent[root];
        while (parent[i] != root) {  // path compression
            int32_t next = parent[i];
            parent[i] = root;
            i = next;
        }
        return root;
    }

    void unite(int32_t i, int32_t j) {
        int32_t root_i = find(i);
        int32_t root_j = find(j);
        if (root_i == root_j) return;
        if (rank[root_i] < rank[root_j]) std::swap(root_i, root_j);
        parent[root_j] = root_i;
        if (rank[root_i] == rank[root_j]) rank[root_i]++;
    }

    void rebuild() {
        DC_COUNT(dsu_rebuilds, 1);
        std::iota(parent.begin(), parent.end(), 0);
        std::fill(rank.begin(), rank.end(), 0);
        for (const auto& [a, b] : edges) unite(a, b);
        stale = false;
    }

public:
    explicit DSUConnectivity(int vertices) : n(vertices), parent(vertices), rank(vertices, 0) {
        std::iota(parent.begin(), parent.end(), 0);
    }

    void insert_edge(int u, int v) override {
        if (u == v) return;
        int32_t a = std::min(u, v), b = std::max(u, v);
        uint64_t key = dc::edge_key(a, b);
        if (position.contains(key)) return;  // duplicate: a no-op (must not touch the stored position)
        position.insert_or_assign(key, static_cast<uint32_t>(edges.size()));
        edges.push_back({a, b});
        if (!stale) unite(a, b);  // a stale forest is rebuilt from the full list anyway
    }

    void delete_edge(int u, int v) override {
        int32_t a = std::min(u, v), b = std::max(u, v);
        const uint32_t* p = position.find(dc::edge_key(a, b));
        if (!p) return;
        uint32_t i = *p;
        position.erase(dc::edge_key(a, b));
        if (i + 1 != edges.size()) {
            edges[i] = edges.back();
            position.insert_or_assign(dc::edge_key(edges[i].first, edges[i].second), i);
        }
        edges.pop_back();
        if constexpr (Lazy) {
            stale = true;
        } else {
            rebuild();
        }
    }

    bool is_connected(int u, int v) override {
        if (stale) rebuild();
        return find(u) == find(v);
    }

    size_t edge_count() const override { return edges.size(); }
};

using BaselineDSU = DSUConnectivity<false>;
using LazyDSU = DSUConnectivity<true>;
