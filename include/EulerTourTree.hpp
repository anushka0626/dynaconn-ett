#pragma once
#include "DynamicConnectivity.hpp"
#include <random>
#include <map>
#include <utility>
#include <cassert>

class EulerTourTree : public DynamicConnectivity {
public:
    struct Node {
        int u, v;               // Edge or vertex identity (u == v for identity visit)
        uint32_t priority;
        int size;
        Node *left = nullptr;
        Node *right = nullptr;
        Node *parent = nullptr;

        Node(int from, int to, uint32_t prio) 
            : u(from), v(to), priority(prio), size(1) {}
    };

private:
    int n;
    std::mt19937 rng;
    std::vector<Node*> vertex_nodes; // Maps vertex id -> primary loop node (u, u)
    std::map<std::pair<int, int>, Node*> edge_nodes; // Maps directed (u, v) -> Node*

    uint32_t random_priority() {
        return rng();
    }

    static int get_size(Node* t) {
        return t ? t->size : 0;
    }

    static void update(Node* t) {
        if (!t) return;
        t->size = 1 + get_size(t->left) + get_size(t->right);
        if (t->left) t->left->parent = t;
        if (t->right) t->right->parent = t;
    }

    // Merges trees L and R. Assumes all elements in L precede all elements in R.
    Node* merge(Node* l, Node* r) {
        if (!l || !r) return l ? l : r;
        if (l->priority > r->priority) {
            l->right = merge(l->right, r);
            update(l);
            return l;
        } else {
            r->left = merge(l, r->left);
            update(r);
            return r;
        }
    }

    // Computes zero-based index of a node in its containing Treap sequence
    int get_index(Node* node) {
        int idx = get_size(node->left);
        Node* curr = node;
        while (curr->parent) {
            if (curr->parent->right == curr) {
                idx += 1 + get_size(curr->parent->left);
            }
            curr = curr->parent;
        }
        return idx;
    }

    // Splits treap T into L ([0, k-1]) and R ([k, end])
    void split(Node* t, int k, Node*& l, Node*& r) {
        if (!t) {
            l = r = nullptr;
            return;
        }
        t->parent = nullptr;
        int left_size = get_size(t->left);
        if (k <= left_size) {
            split(t->left, k, l, t->left);
            r = t;
        } else {
            split(t->right, k - left_size - 1, t->right, r);
            l = t;
        }
        update(t);
    }

    Node* get_root(Node* node) {
        if (!node) return nullptr;
        while (node->parent) node = node->parent;
        return node;
    }

    // Makes node u the first element of its Euler tour sequence
    void reroot(Node* node) {
        Node* root = get_root(node);
        int idx = get_index(node);
        if (idx == 0) return;

        Node *A = nullptr, *B = nullptr;
        split(root, idx, A, B); // A is [0, idx - 1], B is [idx, end]
        merge(B, A);
    }

public:
    explicit EulerTourTree(int vertices) : n(vertices), rng(1337) {
        vertex_nodes.resize(n);
        for (int i = 0; i < n; ++i) {
            vertex_nodes[i] = new Node(i, i, random_priority());
        }
    }

    ~EulerTourTree() override {
        for (auto& pair : edge_nodes) delete pair.second;
        for (Node* node : vertex_nodes) delete node;
    }

    bool is_connected(int u, int v) override {
        if (u == v) return true;
        return get_root(vertex_nodes[u]) == get_root(vertex_nodes[v]);
    }

    void insert_edge(int u, int v) override {
        if (u == v || is_connected(u, v)) return;

        reroot(vertex_nodes[u]);
        reroot(vertex_nodes[v]);

        Node* uv = new Node(u, v, random_priority());
        Node* vu = new Node(v, u, random_priority());
        edge_nodes[{u, v}] = uv;
        edge_nodes[{v, u}] = vu;

        Node* root_u = get_root(vertex_nodes[u]);
        Node* root_v = get_root(vertex_nodes[v]);

        // Splice: Tour(u) + <u, v> + Tour(v) + <v, u>
        Node* joined = merge(root_u, uv);
        joined = merge(joined, root_v);
        merge(joined, vu);
    }

    void delete_edge(int u, int v) override {
        auto it1 = edge_nodes.find({u, v});
        auto it2 = edge_nodes.find({v, u});
        if (it1 == edge_nodes.end() || it2 == edge_nodes.end()) return;

        Node* uv = it1->second;
        Node* vu = it2->second;
        edge_nodes.erase(it1);
        edge_nodes.erase(it2);

        int idx_uv = get_index(uv);
        int idx_vu = get_index(vu);

        if (idx_uv > idx_vu) {
            std::swap(uv, vu);
            std::swap(idx_uv, idx_vu);
        }

        Node* root = get_root(uv);

        // Sequence structure: [A] + <uv> + [B] + <vu> + [C]
        Node *T1 = nullptr, *Rest1 = nullptr;
        split(root, idx_uv, T1, Rest1); // T1 = [A], Rest1 = <uv> + [B] + <vu> + [C]

        Node *UV_node = nullptr, *Rest2 = nullptr;
        split(Rest1, 1, UV_node, Rest2); // UV_node = <uv>, Rest2 = [B] + <vu> + [C]

        int new_idx_vu = get_index(vu);
        Node *B = nullptr, *Rest3 = nullptr;
        split(Rest2, new_idx_vu, B, Rest3); // B = [B], Rest3 = <vu> + [C]

        Node *VU_node = nullptr, *C = nullptr;
        split(Rest3, 1, VU_node, C); // VU_node = <vu>, C = [C]

        delete uv;
        delete vu;

        // Reconnect remaining pieces: Component 1 is B; Component 2 is C + A
        merge(C, T1);
    }
};