#pragma once
#include "DynamicConnectivity.hpp"
#include <vector>
#include <map>
#include <unordered_set>
#include <random>
#include <cstdint>
#include <utility>
#include <algorithm>

class FlatDynamicGraphETT : public DynamicConnectivity {
public:
    static constexpr int32_t NIL = -1;

    struct alignas(32) FlatNode {
        int32_t u, v;
        uint32_t priority;
        int32_t size;
        int32_t left = NIL;
        int32_t right = NIL;
        int32_t parent = NIL;

        FlatNode() = default;
        FlatNode(int32_t from, int32_t to, uint32_t prio)
            : u(from), v(to), priority(prio), size(1) {}
    };

private:
    int n;
    std::mt19937 rng;
    std::vector<FlatNode> pool;
    std::vector<int32_t> free_list;
    std::vector<int32_t> vertex_nodes;
    std::map<std::pair<int32_t, int32_t>, int32_t> tree_edges;
    std::vector<std::unordered_set<int>> non_tree_adj;

    int32_t allocate_node(int32_t u, int32_t v) {
        int32_t idx;
        if (!free_list.empty()) {
            idx = free_list.back();
            free_list.pop_back();
            pool[idx] = FlatNode(u, v, rng());
        } else {
            idx = static_cast<int32_t>(pool.size());
            pool.emplace_back(u, v, rng());
        }
        return idx;
    }

    void deallocate_node(int32_t idx) {
        if (idx == NIL) return;
        free_list.push_back(idx);
    }

    int32_t get_size(int32_t t) const {
        return (t == NIL) ? 0 : pool[t].size;
    }

    void update(int32_t t) {
        if (t == NIL) return;
        pool[t].size = 1 + get_size(pool[t].left) + get_size(pool[t].right);
        if (pool[t].left != NIL) pool[pool[t].left].parent = t;
        if (pool[t].right != NIL) pool[pool[t].right].parent = t;
    }

    int32_t merge(int32_t l, int32_t r) {
        if (l == NIL || r == NIL) return (l != NIL) ? l : r;
        if (pool[l].priority > pool[r].priority) {
            pool[l].right = merge(pool[l].right, r);
            update(l);
            return l;
        } else {
            pool[r].left = merge(l, pool[r].left);
            update(r);
            return r;
        }
    }

    int32_t get_index(int32_t idx) const {
        int32_t pos = get_size(pool[idx].left);
        int32_t curr = idx;
        while (pool[curr].parent != NIL) {
            int32_t p = pool[curr].parent;
            if (pool[p].right == curr) {
                pos += 1 + get_size(pool[p].left);
            }
            curr = p;
        }
        return pos;
    }

    void split(int32_t t, int32_t k, int32_t& l, int32_t& r) {
        if (t == NIL) {
            l = r = NIL;
            return;
        }
        pool[t].parent = NIL;
        int32_t left_size = get_size(pool[t].left);
        if (k <= left_size) {
            split(pool[t].left, k, l, pool[t].left);
            r = t;
        } else {
            split(pool[t].right, k - left_size - 1, pool[t].right, r);
            l = t;
        }
        update(t);
    }

    int32_t get_root(int32_t idx) const {
        if (idx == NIL) return NIL;
        while (pool[idx].parent != NIL) {
            idx = pool[idx].parent;
        }
        return idx;
    }

    void reroot(int32_t idx) {
        int32_t root = get_root(idx);
        int32_t pos = get_index(idx);
        if (pos == 0) return;

        int32_t A = NIL, B = NIL;
        split(root, pos, A, B);
        merge(B, A);
    }

    void link_trees(int u, int v) {
        reroot(vertex_nodes[u]);
        reroot(vertex_nodes[v]);

        int32_t uv = allocate_node(u, v);
        int32_t vu = allocate_node(v, u);
        tree_edges[{u, v}] = uv;
        tree_edges[{v, u}] = vu;

        int32_t root_u = get_root(vertex_nodes[u]);
        int32_t root_v = get_root(vertex_nodes[v]);

        int32_t joined = merge(root_u, uv);
        joined = merge(joined, root_v);
        merge(joined, vu);
    }

    void cut_tree_edge(int u, int v) {
        auto it1 = tree_edges.find({u, v});
        auto it2 = tree_edges.find({v, u});
        if (it1 == tree_edges.end() || it2 == tree_edges.end()) return;

        int32_t uv = it1->second;
        int32_t vu = it2->second;
        tree_edges.erase(it1);
        tree_edges.erase(it2);

        int32_t idx_uv = get_index(uv);
        int32_t idx_vu = get_index(vu);

        if (idx_uv > idx_vu) {
            std::swap(uv, vu);
            std::swap(idx_uv, idx_vu);
        }

        int32_t root = get_root(uv);

        int32_t T1 = NIL, Rest1 = NIL;
        split(root, idx_uv, T1, Rest1);

        int32_t UV_node = NIL, Rest2 = NIL;
        split(Rest1, 1, UV_node, Rest2);

        int32_t new_idx_vu = get_index(vu);
        int32_t B = NIL, Rest3 = NIL;
        split(Rest2, new_idx_vu, B, Rest3);

        int32_t VU_node = NIL, C = NIL;
        split(Rest3, 1, VU_node, C);

        deallocate_node(uv);
        deallocate_node(vu);

        merge(C, T1);
    }

    void collect_vertices(int32_t t, std::unordered_set<int>& vertices) {
        if (t == NIL) return;
        if (pool[t].u == pool[t].v) {
            vertices.insert(pool[t].u);
        }
        collect_vertices(pool[t].left, vertices);
        collect_vertices(pool[t].right, vertices);
    }

public:
    explicit FlatDynamicGraphETT(int vertices) : n(vertices), rng(1337), non_tree_adj(vertices) {
        pool.reserve(vertices * 4);
        vertex_nodes.resize(n);
        for (int32_t i = 0; i < n; ++i) {
            vertex_nodes[i] = allocate_node(i, i);
        }
    }

    bool is_connected(int u, int v) override {
        if (u == v) return true;
        return get_root(vertex_nodes[u]) == get_root(vertex_nodes[v]);
    }

    void insert_edge(int u, int v) override {
        if (u == v) return;
        int a = std::min(u, v), b = std::max(u, v);

        if (tree_edges.contains({a, b}) || non_tree_adj[a].contains(b)) {
            return;
        }

        if (!is_connected(a, b)) {
            link_trees(a, b);
        } else {
            non_tree_adj[a].insert(b);
            non_tree_adj[b].insert(a);
        }
    }

    void delete_edge(int u, int v) override {
        if (u == v) return;
        int a = std::min(u, v), b = std::max(u, v);

        if (non_tree_adj[a].contains(b)) {
            non_tree_adj[a].erase(b);
            non_tree_adj[b].erase(a);
            return;
        }

        if (!tree_edges.contains({a, b})) {
            return;
        }

        cut_tree_edge(a, b);

        int32_t root_a = get_root(vertex_nodes[a]);
        int32_t root_b = get_root(vertex_nodes[b]);

        int32_t small_root = (get_size(root_a) <= get_size(root_b)) ? root_a : root_b;
        int32_t large_root = (small_root == root_a) ? root_b : root_a;

        std::unordered_set<int> small_comp_vertices;
        collect_vertices(small_root, small_comp_vertices);

        int rep_u = -1, rep_v = -1;
        bool found_replacement = false;

        for (int x : small_comp_vertices) {
            for (int y : non_tree_adj[x]) {
                if (get_root(vertex_nodes[y]) == large_root) {
                    rep_u = x;
                    rep_v = y;
                    found_replacement = true;
                    break;
                }
            }
            if (found_replacement) break;
        }

        if (found_replacement) {
            non_tree_adj[rep_u].erase(rep_v);
            non_tree_adj[rep_v].erase(rep_u);
            link_trees(rep_u, rep_v);
        }
    }

    // Memory footprint calculation
    size_t memory_footprint_bytes() const {
        size_t bytes = sizeof(*this);
        bytes += pool.capacity() * sizeof(FlatNode);
        bytes += free_list.capacity() * sizeof(int32_t);
        bytes += vertex_nodes.capacity() * sizeof(int32_t);
        // std::map node overhead (~48 bytes per node on 64-bit libc++)
        bytes += tree_edges.size() * (sizeof(std::pair<std::pair<int32_t, int32_t>, int32_t>) + 32);
        for (const auto& set : non_tree_adj) {
            bytes += sizeof(set) + (set.bucket_count() * sizeof(void*)) + (set.size() * (sizeof(int) + 16));
        }
        return bytes;
    }
};