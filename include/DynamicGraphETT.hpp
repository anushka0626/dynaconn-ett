#pragma once
#include "DynamicConnectivity.hpp"
#include <random>
#include <map>
#include <unordered_set>
#include <vector>
#include <utility>
#include <algorithm>
#include <cstdint>

class DynamicGraphETT : public DynamicConnectivity {
public:
    struct Node {
        int u, v; // u == v for primary vertex nodes; u != v for directed tree edge visits
        uint32_t priority;
        int size;
        Node* left = nullptr;
        Node* right = nullptr;
        Node* parent = nullptr;

        Node(int from, int to, uint32_t prio)
            : u(from), v(to), priority(prio), size(1) {}
    };

private:
    int n;
    std::mt19937 rng;

    // ETT Tree tracking
    std::vector<Node*> vertex_nodes;
    std::map<std::pair<int, int>, Node*> tree_edges;

    // Non-tree incident adjacency lists
    std::vector<std::unordered_set<int>> non_tree_adj;

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

    void reroot(Node* node) {
        Node* root = get_root(node);
        int idx = get_index(node);
        if (idx == 0) return;

        Node *A = nullptr, *B = nullptr;
        split(root, idx, A, B);
        merge(B, A);
    }

    // Direct link of two disjoint trees in the Euler forest
    void link_trees(int u, int v) {
        reroot(vertex_nodes[u]);
        reroot(vertex_nodes[v]);

        Node* uv = new Node(u, v, random_priority());
        Node* vu = new Node(v, u, random_priority());
        tree_edges[{u, v}] = uv;
        tree_edges[{v, u}] = vu;

        Node* root_u = get_root(vertex_nodes[u]);
        Node* root_v = get_root(vertex_nodes[v]);

        Node* joined = merge(root_u, uv);
        joined = merge(joined, root_v);
        merge(joined, vu);
    }

    // Direct cut of a tree edge, separating it into two components
    void cut_tree_edge(int u, int v) {
        auto it1 = tree_edges.find({u, v});
        auto it2 = tree_edges.find({v, u});
        if (it1 == tree_edges.end() || it2 == tree_edges.end()) return;

        Node* uv = it1->second;
        Node* vu = it2->second;
        tree_edges.erase(it1);
        tree_edges.erase(it2);

        int idx_uv = get_index(uv);
        int idx_vu = get_index(vu);

        if (idx_uv > idx_vu) {
            std::swap(uv, vu);
            std::swap(idx_uv, idx_vu);
        }

        Node* root = get_root(uv);

        Node *T1 = nullptr, *Rest1 = nullptr;
        split(root, idx_uv, T1, Rest1);

        Node *UV_node = nullptr, *Rest2 = nullptr;
        split(Rest1, 1, UV_node, Rest2);

        int new_idx_vu = get_index(vu);
        Node *B = nullptr, *Rest3 = nullptr;
        split(Rest2, new_idx_vu, B, Rest3);

        Node *VU_node = nullptr, *C = nullptr;
        split(Rest3, 1, VU_node, C);

        delete uv;
        delete vu;

        merge(C, T1);
    }

    // Traverses all nodes in a treap to collect unique vertices
    void collect_vertices(Node* t, std::unordered_set<int>& vertices) {
        if (!t) return;
        if (t->u == t->v) {
            vertices.insert(t->u);
        }
        collect_vertices(t->left, vertices);
        collect_vertices(t->right, vertices);
    }

public:
    explicit DynamicGraphETT(int vertices) : n(vertices), rng(1337), non_tree_adj(vertices) {
        vertex_nodes.resize(n);
        for (int i = 0; i < n; ++i) {
            vertex_nodes[i] = new Node(i, i, random_priority());
        }
    }

    ~DynamicGraphETT() override {
        for (auto& pair : tree_edges) delete pair.second;
        for (Node* node : vertex_nodes) delete node;
    }

    bool is_connected(int u, int v) override {
        if (u == v) return true;
        return get_root(vertex_nodes[u]) == get_root(vertex_nodes[v]);
    }

    void insert_edge(int u, int v) override {
        if (u == v) return;
        int a = std::min(u, v), b = std::max(u, v);

        // Check if edge already exists in either set
        if (tree_edges.contains({a, b}) || non_tree_adj[a].contains(b)) {
            return;
        }

        if (!is_connected(a, b)) {
            // Edge bridges two disjoint trees -> Promoted to Tree Edge
            link_trees(a, b);
        } else {
            // Vertices already reachable -> Stored as Non-Tree Edge
            non_tree_adj[a].insert(b);
            non_tree_adj[b].insert(a);
        }
    }

    void delete_edge(int u, int v) override {
        if (u == v) return;
        int a = std::min(u, v), b = std::max(u, v);

        // Case 1: Edge is a non-tree edge
        if (non_tree_adj[a].contains(b)) {
            non_tree_adj[a].erase(b);
            non_tree_adj[b].erase(a);
            return;
        }

        // Case 2: Edge is not present
        if (!tree_edges.contains({a, b})) {
            return;
        }

        // Case 3: Edge is a tree edge -> Cut and find replacement
        cut_tree_edge(a, b);

        // Determine which resulting component is smaller
        Node* root_a = get_root(vertex_nodes[a]);
        Node* root_b = get_root(vertex_nodes[b]);

        Node* small_root = (get_size(root_a) <= get_size(root_b)) ? root_a : root_b;
        Node* large_root = (small_root == root_a) ? root_b : root_a;

        // Collect all vertices in the smaller component
        std::unordered_set<int> small_comp_vertices;
        collect_vertices(small_root, small_comp_vertices);

        // Search incident non-tree edges for a replacement edge
        int rep_u = -1, rep_v = -1;
        bool found_replacement = false;

        for (int x : small_comp_vertices) {
            std::vector<int> to_remove;
            for (int y : non_tree_adj[x]) {
                // If y belongs to the other component, replacement is found
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
            // Remove from non-tree edge sets
            non_tree_adj[rep_u].erase(rep_v);
            non_tree_adj[rep_v].erase(rep_u);

            // Promote candidate to tree edge
            link_trees(rep_u, rep_v);
        }
    }
};