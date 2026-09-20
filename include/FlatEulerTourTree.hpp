#pragma once
#include "DynamicConnectivity.hpp"
#include <vector>
#include <map>
#include <random>
#include <cstdint>
#include <utility>

class FlatEulerTourTree : public DynamicConnectivity {
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
    std::map<std::pair<int32_t, int32_t>, int32_t> edge_nodes;

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

public:
    explicit FlatEulerTourTree(int vertices) : n(vertices), rng(1337) {
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
        if (u == v || is_connected(u, v)) return;

        reroot(vertex_nodes[u]);
        reroot(vertex_nodes[v]);

        int32_t uv = allocate_node(u, v);
        int32_t vu = allocate_node(v, u);
        edge_nodes[{u, v}] = uv;
        edge_nodes[{v, u}] = vu;

        int32_t root_u = get_root(vertex_nodes[u]);
        int32_t root_v = get_root(vertex_nodes[v]);

        int32_t joined = merge(root_u, uv);
        joined = merge(joined, root_v);
        merge(joined, vu);
    }

    void delete_edge(int u, int v) override {
        auto it1 = edge_nodes.find({u, v});
        auto it2 = edge_nodes.find({v, u});
        if (it1 == edge_nodes.end() || it2 == edge_nodes.end()) return;

        int32_t uv = it1->second;
        int32_t vu = it2->second;
        edge_nodes.erase(it1);
        edge_nodes.erase(it2);

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
};