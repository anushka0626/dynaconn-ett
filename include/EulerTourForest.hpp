#pragma once
#include "EdgeMaps.hpp"
#include "MemoryAccounting.hpp"
#include "Stats.hpp"
#include "TreapStorage.hpp"
#include <cstdint>
#include <limits>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Euler-tour forest on implicit treaps, generic over node storage and tree-edge map.
//
// Each vertex owns one node <v, v>. Each tree edge (u, v) is two directed nodes
// <u, v> and <v, u>. A tree's Euler tour is the in-order sequence of its treap, and two
// vertices are connected iff their nodes share a treap root.
//
// Both storage policies run exactly this code and draw priorities from one RNG stream
// in the same order. So for a given op sequence, the pointer and arena variants build
// identical treaps and only memory layout differs (see tests: layout_shape_identity).
// The tree-edge map is only ever looked up, so its type cannot change any decision either.
template <class Storage, template <class> class EdgeMapT = dc::StdEdgeMap>
class EulerTourForest {
public:
    using Handle = typename Storage::Handle;
    static constexpr Handle nil = Storage::nil;
    static constexpr uint32_t default_seed = 1337;

    explicit EulerTourForest(int vertices, uint32_t seed = default_seed)
        : n(vertices), rng(seed) {
        storage.reserve(static_cast<size_t>(vertices) * 4);
        vertex_nodes.resize(n);
        for (int i = 0; i < n; ++i) vertex_nodes[i] = allocate_node(i, i);
    }

    ~EulerTourForest() {
        if constexpr (Storage::release_on_destroy) {
            tree_edges.for_each([&](int32_t, int32_t, Handle h) { storage.release(h); });
            for (Handle h : vertex_nodes) storage.release(h);
        }
    }

    EulerTourForest(const EulerTourForest&) = delete;
    EulerTourForest& operator=(const EulerTourForest&) = delete;

    int num_vertices() const { return n; }
    size_t num_tree_edges() const { return tree_edges.size() / 2; }

    Handle root_of(int v) const { return get_root(vertex_nodes[v]); }
    bool connected(int u, int v) const { return u == v || root_of(u) == root_of(v); }
    bool has_tree_edge(int u, int v) const { return tree_edges.contains(u, v); }

    // Number of treap nodes in a tree: 3k - 2 for a tree with k vertices.
    int32_t tour_size(Handle root) const { return get_size(root); }

    // Joins the trees of u and v with tree edge (u, v). Precondition: !connected(u, v).
    void link(int u, int v) {
        reroot(vertex_nodes[u]);
        reroot(vertex_nodes[v]);

        Handle uv = allocate_node(u, v);
        Handle vu = allocate_node(v, u);
        tree_edges.insert(u, v, uv);
        tree_edges.insert(v, u, vu);

        // Splice: Tour(u) + <u, v> + Tour(v) + <v, u>
        Handle joined = merge(get_root(vertex_nodes[u]), uv);
        joined = merge(joined, get_root(vertex_nodes[v]));
        merge(joined, vu);
    }

    // Removes tree edge (u, v), splitting its tree in two. Returns false if (u, v) is not
    // a tree edge.
    bool cut(int u, int v) {
        Handle* p1 = tree_edges.find(u, v);
        Handle* p2 = tree_edges.find(v, u);
        if (!p1 || !p2) return false;

        Handle uv = *p1;
        Handle vu = *p2;
        tree_edges.erase(u, v);
        tree_edges.erase(v, u);

        int32_t idx_uv = get_index(uv);
        int32_t idx_vu = get_index(vu);
        if (idx_uv > idx_vu) {
            std::swap(uv, vu);
            std::swap(idx_uv, idx_vu);
        }

        // Sequence structure: [A] + <uv> + [B] + <vu> + [C]
        Handle a = nil, rest1 = nil;
        split(get_root(uv), idx_uv, a, rest1);
        Handle uv_node = nil, rest2 = nil;
        split(rest1, 1, uv_node, rest2);
        Handle b = nil, rest3 = nil;
        split(rest2, get_index(vu), b, rest3);
        Handle vu_node = nil, c = nil;
        split(rest3, 1, vu_node, c);

        storage.release(uv);
        storage.release(vu);

        // B is one component; C + A is the other.
        merge(c, a);
        return true;
    }

    // Calls f(vertex) for every vertex in the tree rooted at `root`, in treap preorder.
    // f must not modify the forest.
    template <class F>
    void for_each_vertex(Handle root, F&& f) const {
        visit_vertices(root, f);
    }

    // Full structural check: returns an empty string if every invariant holds, else a
    // description of the first violation. O(n + tree edges).
    std::string check_invariants() const {
        size_t total_nodes = 0;
        size_t edge_nodes_seen = 0;
        std::set<Handle> roots_seen;
        std::vector<char> vertex_seen(n, 0);

        for (int s = 0; s < n; ++s) {
            const auto& vnode = storage[vertex_nodes[s]];
            if (vnode.u != s || vnode.v != s) return "vertex node of " + std::to_string(s) + " has wrong identity";

            Handle root = root_of(s);
            if (!roots_seen.insert(root).second) continue;
            if (storage[root].parent != nil) return "root has a parent";

            std::vector<Handle> seq;
            std::string err = check_subtree(root, seq);
            if (!err.empty()) return err;
            total_nodes += seq.size();

            // The in-order sequence must be a closed walk: each element starts where the
            // previous one ended, and vertex nodes <x, x> stay at x.
            int32_t start = storage[seq.front()].u;
            int32_t current = start;
            size_t vertices_in_tree = 0;
            for (Handle h : seq) {
                const auto& node = storage[h];
                if (node.u != current) return "tour is not a walk at node (" + std::to_string(node.u) + "," + std::to_string(node.v) + ")";
                current = node.v;
                if (node.u == node.v) {
                    if (vertex_nodes[node.u] != h) return "stray vertex node for " + std::to_string(node.u);
                    if (vertex_seen[node.u]++) return "vertex " + std::to_string(node.u) + " appears twice";
                    ++vertices_in_tree;
                } else {
                    const Handle* it = tree_edges.find(node.u, node.v);
                    if (!it || *it != h) return "edge node not registered in tree_edges";
                    ++edge_nodes_seen;
                }
            }
            if (current != start) return "tour is not closed";
            if (seq.size() != 3 * vertices_in_tree - 2) return "tour size is not 3k - 2";
        }

        if (edge_nodes_seen != tree_edges.size()) return "tree_edges has entries not in any tour";
        if (total_nodes != static_cast<size_t>(n) + tree_edges.size()) return "node count mismatch";
        std::string err;
        tree_edges.for_each([&](int32_t a, int32_t b, Handle h) {
            if (!err.empty()) return;
            if (!tree_edges.contains(b, a)) err = "tree edge missing its reverse";
            else if (get_root(h) != root_of(a) || get_root(h) != root_of(b)) err = "tree edge in wrong tour";
        });
        return err;
    }

    // Exact treap shapes as a flat vector: for each tree (in order of its smallest
    // vertex), a preorder listing of (u, v, priority, size) with a marker for nil
    // children. Two forests with equal serializations have identical treaps.
    std::vector<int64_t> serialize() const {
        std::vector<int64_t> out;
        std::set<Handle> roots_seen;
        for (int s = 0; s < n; ++s) {
            Handle root = root_of(s);
            if (roots_seen.insert(root).second) serialize_subtree(root, out);
        }
        return out;
    }

private:
    int n;
    std::mt19937 rng;
    Storage storage;
    using EdgeMap = EdgeMapT<Handle>;
    std::vector<Handle, dc::Alloc<Handle, dc::MemCategory::VertexIndex>> vertex_nodes;
    EdgeMap tree_edges;  // keyed by both directions of each tree edge

    Handle allocate_node(int u, int v) { return storage.allocate(u, v, rng()); }

    int32_t get_size(Handle t) const { return t == nil ? 0 : storage[t].size; }

    // Recomputes size and re-points the children's parent links; call after any child change.
    void update(Handle t) {
        if (t == nil) return;
        auto& node = storage[t];
        node.size = 1 + get_size(node.left) + get_size(node.right);
        if (node.left != nil) storage[node.left].parent = t;
        if (node.right != nil) storage[node.right].parent = t;
    }

    // Concatenates l and r (every element of l precedes every element of r).
    Handle merge(Handle l, Handle r) {
        if (l == nil || r == nil) return l != nil ? l : r;
        DC_COUNT(treap_merges, 1);
        if (storage[l].priority > storage[r].priority) {
            storage[l].right = merge(storage[l].right, r);
            update(l);
            return l;
        }
        storage[r].left = merge(l, storage[r].left);
        update(r);
        return r;
    }

    // Splits t into l = positions [0, k-1] and r = [k, end].
    void split(Handle t, int32_t k, Handle& l, Handle& r) {
        if (t == nil) {
            l = r = nil;
            return;
        }
        DC_COUNT(treap_splits, 1);
        storage[t].parent = nil;
        int32_t left_size = get_size(storage[t].left);
        if (k <= left_size) {
            split(storage[t].left, k, l, storage[t].left);
            r = t;
        } else {
            split(storage[t].right, k - left_size - 1, storage[t].right, r);
            l = t;
        }
        update(t);
    }

    // Zero-based position of a node in its tour.
    int32_t get_index(Handle h) const {
        int32_t pos = get_size(storage[h].left);
        Handle curr = h;
        while (storage[curr].parent != nil) {
            Handle p = storage[curr].parent;
            if (storage[p].right == curr) pos += 1 + get_size(storage[p].left);
            curr = p;
            DC_COUNT(index_walk_steps, 1);
        }
        return pos;
    }

    Handle get_root(Handle h) const {
        while (storage[h].parent != nil) {
            h = storage[h].parent;
            DC_COUNT(root_walk_steps, 1);
        }
        return h;
    }

    // Rotates the tour so that node h comes first.
    void reroot(Handle h) {
        Handle root = get_root(h);
        int32_t pos = get_index(h);
        if (pos == 0) return;
        Handle a = nil, b = nil;
        split(root, pos, a, b);
        merge(b, a);
    }

    template <class F>
    void visit_vertices(Handle t, F& f) const {
        if (t == nil) return;
        const auto& node = storage[t];
        if (node.u == node.v) f(node.u);
        visit_vertices(node.left, f);
        visit_vertices(node.right, f);
    }

    // Checks size, parent links and the heap property below t; appends the in-order
    // sequence to seq.
    std::string check_subtree(Handle t, std::vector<Handle>& seq) const {
        const auto& node = storage[t];
        int32_t expected = 1;
        for (Handle child : {node.left, node.right}) {
            if (child == nil) continue;
            if (storage[child].parent != t) return "child's parent link is wrong";
            if (storage[child].priority > node.priority) return "heap property violated";
            expected += storage[child].size;
        }
        if (node.left != nil) {
            std::string err = check_subtree(node.left, seq);
            if (!err.empty()) return err;
        }
        seq.push_back(t);
        if (node.right != nil) {
            std::string err = check_subtree(node.right, seq);
            if (!err.empty()) return err;
        }
        if (node.size != expected) return "subtree size is wrong";
        return {};
    }

    void serialize_subtree(Handle t, std::vector<int64_t>& out) const {
        if (t == nil) {
            out.push_back(std::numeric_limits<int64_t>::min());
            return;
        }
        const auto& node = storage[t];
        out.insert(out.end(), {node.u, node.v, static_cast<int64_t>(node.priority), node.size});
        serialize_subtree(node.left, out);
        serialize_subtree(node.right, out);
    }
};
