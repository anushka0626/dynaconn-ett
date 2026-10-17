#pragma once
#include "DynamicConnectivity.hpp"
#include "MemoryAccounting.hpp"
#include "OrderedSets.hpp"
#include "Stats.hpp"
#include <cstdint>
#include <string>
#include <vector>

// D-tree (Chen, Lachish, Helmer & Böhlen, PVLDB 15(11) 2022), reimplemented from the
// paper's pseudocode (Algorithms 1-8). Where the pseudocode and the authors' code
// (qingchen3/D-tree, also used by SIGMOD 2025's benchmark) differ, we follow the authors'
// behavior, since it produced the published results. The one exception is the query, where
// both can return a wrong answer (see query_roots). No code was copied; the full list of
// differences is in docs/IMPLEMENTATION_NOTES.md.
//
// Each component is a spanning tree kept shallow by heuristics (centroid-ward rerooting,
// BFS-tree repair on non-tree inserts). Queries walk to the root, and may reroot. A tree
// edge delete enumerates every candidate replacement from the smaller side and picks the
// one whose far endpoint is shallowest.
//
// Storage policies (layout ablation): DTreePointerStorage (P0) and DTreeFlatStorage (F2).
// Both expose Handle, nil, handle(v), key(h), parent(h), size(h). Children and non-tree
// neighbor sets are keyed by vertex id, in the canonical swap-remove order, so the P0 and
// F2 variants make identical choices.

struct DTreePointerNode {
    DTreePointerNode* parent = nullptr;
    int32_t key;
    int32_t size = 1;
};

class DTreePointerStorage {
public:
    using Handle = DTreePointerNode*;
    static constexpr Handle nil = nullptr;

    explicit DTreePointerStorage(int n) : nodes(n) {
        for (int v = 0; v < n; ++v) {
            nodes[v] = alloc.allocate(1);
            ::new (nodes[v]) DTreePointerNode{nullptr, v, 1};
        }
    }
    ~DTreePointerStorage() {
        for (Handle h : nodes) alloc.deallocate(h, 1);
    }
    DTreePointerStorage(const DTreePointerStorage&) = delete;
    DTreePointerStorage& operator=(const DTreePointerStorage&) = delete;

    Handle handle(int v) const { return nodes[v]; }
    static int32_t key(Handle h) { return h->key; }
    static Handle& parent(Handle h) { return h->parent; }
    static int32_t& size(Handle h) { return h->size; }

private:
    dc::Alloc<DTreePointerNode, dc::MemCategory::TreeNodes> alloc;
    std::vector<Handle, dc::Alloc<Handle, dc::MemCategory::VertexIndex>> nodes;
};

class DTreeFlatStorage {
public:
    using Handle = int32_t;  // the vertex id itself
    static constexpr Handle nil = -1;

    explicit DTreeFlatStorage(int n) : nodes(n) {}

    static Handle handle(int v) { return v; }
    static int32_t key(Handle h) { return h; }
    Handle& parent(Handle h) { return nodes[h].parent; }
    int32_t& size(Handle h) { return nodes[h].size; }

private:
    struct Node {
        int32_t parent = -1;
        int32_t size = 1;
    };
    std::vector<Node, dc::Alloc<Node, dc::MemCategory::TreeNodes>> nodes;
};

template <class Storage, template <dc::MemCategory> class Sets>
class DTreeImpl : public DynamicConnectivity {
    using Handle = typename Storage::Handle;
    static constexpr Handle nil = Storage::nil;

public:
    explicit DTreeImpl(int vertices)
        : n(vertices), st(vertices), children(vertices), nte(vertices), bfs_queue() {}

    bool is_connected(int u, int v) override {
        if (u == v) return true;
        Handle ru, rv;
        return query_roots(u, v, ru, rv);
    }

    void insert_edge(int u, int v) override {
        if (u == v || has_edge(u, v)) return;
        ++edges;
        // Plain root walks, without the query's centroid reroots (as in the authors' code;
        // the paper says "run conn(u, v)").
        Handle ru = root_of(st.handle(u)), rv = root_of(st.handle(v));
        if (ru == rv) {
            insert_nte(u, v, ru);
        } else {
            insert_te(u, v, ru, rv);
        }
    }

    void delete_edge(int u, int v) override {
        if (u == v) return;
        Handle hu = st.handle(u), hv = st.handle(v);
        if (st.parent(hu) == hv || st.parent(hv) == hu) {
            --edges;
            delete_te(hu, hv);
        } else if (nte.erase(u, v)) {
            nte.erase(v, u);
            --edges;
        }
    }

    size_t edge_count() const override { return edges; }

    // Structural check: sizes, parent/children consistency, non-tree edges symmetric and
    // within one tree, edge count. Empty string if all hold.
    std::string check_invariants() {
        size_t tree_edges = 0, directed_nte = 0;
        for (int v = 0; v < n; ++v) {
            Handle h = st.handle(v);
            int32_t expected = 1;
            for (int c : children.items(v)) {
                if (st.parent(st.handle(c)) != h) return "child's parent link is wrong";
                expected += st.size(st.handle(c));
                ++tree_edges;
            }
            if (st.size(h) != expected) return "subtree size is wrong at " + std::to_string(v);
            Handle p = st.parent(h);
            if (p != nil && !children.contains(Storage::key(p), v)) return "parent does not list child";
            for (int y : nte.items(v)) {
                ++directed_nte;
                if (y == v || !nte.contains(y, v)) return "non-tree set is not symmetric";
                Handle hy = st.handle(y);
                if (st.parent(hy) == h || p == hy) return "edge is both tree and non-tree";
                if (root_of(h) != root_of(hy)) return "non-tree edge spans two trees";
            }
        }
        if (tree_edges + directed_nte / 2 != edges) return "edge count mismatch";
        return {};
    }

    // Exact tree structure (parent, size, children order, non-tree order) for the
    // P0/F2 behavior-identity test.
    std::vector<int64_t> serialize() {
        std::vector<int64_t> out;
        for (int v = 0; v < n; ++v) {
            Handle p = st.parent(st.handle(v));
            out.push_back(p == nil ? -1 : Storage::key(p));
            out.push_back(st.size(st.handle(v)));
            for (int c : children.items(v)) out.push_back(c);
            out.push_back(-2);
            for (int y : nte.items(v)) out.push_back(y);
            out.push_back(-3);
        }
        return out;
    }

private:
    int n;
    Storage st;
    Sets<dc::MemCategory::TreeNodes> children;
    Sets<dc::MemCategory::Adjacency> nte;
    std::vector<int32_t, dc::Alloc<int32_t, dc::MemCategory::Scratch>> bfs_queue;
    size_t edges = 0;

    bool has_edge(int u, int v) {
        Handle hu = st.handle(u), hv = st.handle(v);
        return st.parent(hu) == hv || st.parent(hv) == hu || nte.contains(u, v);
    }

    Handle root_of(Handle x) {
        while (st.parent(x) != nil) {
            x = st.parent(x);
            DC_COUNT(root_walk_steps, 1);
        }
        return x;
    }

    int depth_of(Handle x) {
        int d = 0;
        while (st.parent(x) != nil) {
            x = st.parent(x);
            ++d;
            DC_COUNT(root_walk_steps, 1);
        }
        return d;
    }

    // Walks x to its root; returns the root and sets `last` to the node just below it
    // (nil if x is the root).
    Handle walk_to_root(Handle x, Handle& last) {
        last = nil;
        while (st.parent(x) != nil) {
            last = x;
            x = st.parent(x);
            DC_COUNT(root_walk_steps, 1);
        }
        return x;
    }

    // Algorithm 2 with its centroid reroots. Returns whether u and v are connected and
    // sets ru, rv to their trees' roots after the reroots.
    // Fix: connectivity is decided by v's root *before* v's own reroot. As written (in the
    // paper and in the authors' `query`), the answer compares u's root from before v's reroot
    // with v's root from after it. When both walks reroot the same tree, u's recorded root is
    // stale and a connected pair is reported as disconnected. The restructuring (the
    // timed work) is unchanged by the fix (docs/IMPLEMENTATION_NOTES.md).
    bool query_roots(int u, int v, Handle& ru, Handle& rv) {
        Handle last;
        ru = walk_to_root(st.handle(u), last);
        if (last != nil && 2 * st.size(last) > st.size(ru)) ru = reroot(last);
        rv = walk_to_root(st.handle(v), last);
        bool connected = ru == rv;
        if (last != nil && 2 * st.size(last) > st.size(rv)) {
            rv = reroot(last);
            if (connected) ru = rv;
        }
        return connected;
    }

    // Algorithm 1: make w the root of its tree.
    Handle reroot(Handle w) {
        Handle ch = w;
        Handle cur = st.parent(w);
        st.parent(w) = nil;
        while (cur != nil) {
            Handle g = st.parent(cur);
            st.parent(cur) = ch;
            children.erase(Storage::key(cur), Storage::key(ch));
            children.insert(Storage::key(ch), Storage::key(cur));
            ch = cur;
            cur = g;
        }
        // ch is the old root; fix sizes walking back up to w.
        while (st.parent(ch) != nil) {
            Handle p = st.parent(ch);
            st.size(ch) -= st.size(p);
            st.size(p) += st.size(ch);
            ch = p;
        }
        return w;
    }

    // Algorithm 6: attach the tree rooted at v below u (u's tree has root ru). Applies the
    // centroid heuristic and returns the merged tree's root.
    Handle link(Handle u, Handle ru, Handle v) {
        children.insert(Storage::key(u), Storage::key(v));
        st.parent(v) = u;
        const int64_t total = static_cast<int64_t>(st.size(ru)) + st.size(v);
        const int32_t added = st.size(v);
        Handle m = nil;
        for (Handle i = u; i != nil; i = st.parent(i)) {
            st.size(i) += added;
            if (m == nil && 2 * static_cast<int64_t>(st.size(i)) > total) m = i;
        }
        if (m != nil && m != ru) ru = reroot(m);
        return ru;
    }

    // Algorithm 7: cut v (not a root) from its parent. Returns the root of the remainder.
    Handle unlink(Handle v) {
        Handle i = v;
        while (st.parent(i) != nil) {
            i = st.parent(i);
            st.size(i) -= st.size(v);
        }
        children.erase(Storage::key(st.parent(v)), Storage::key(v));
        st.parent(v) = nil;
        return i;
    }

    // Algorithm 4: join two trees with tree edge (u, v), rerooting the smaller one. On equal
    // sizes, u's tree is rerooted and attached below v (the authors' tie rule; the paper's
    // Algorithm 4 breaks ties the other way).
    Handle insert_te(int u, int v, Handle ru, Handle rv) {
        Handle hu = st.handle(u), hv = st.handle(v);
        if (st.size(rv) < st.size(ru)) return link(hu, ru, reroot(hv));
        return link(hv, rv, reroot(hu));
    }

    // Algorithm 3: edge (u, v) inside the tree rooted at r. If the depth gap is >= 2, the
    // edge becomes a tree edge and a tree edge on the deeper endpoint's path becomes
    // non-tree, bringing the tree closer to a BFS tree.
    void insert_nte(int u, int v, Handle r) {
        Handle hu = st.handle(u), hv = st.handle(v);
        int du = depth_of(hu), dv = depth_of(hv);
        Handle l = du <= dv ? hu : hv;
        Handle h = du <= dv ? hv : hu;
        int delta = (du <= dv ? dv : du) - (du <= dv ? du : dv);
        if (delta < 2) {
            nte.insert(u, v);
            nte.insert(v, u);
            return;
        }
        // Climb delta - 3 times (the authors' code: `for i in range(1, delta - 2)`); the
        // paper's Algorithm 3 says delta - 2.
        Handle i = h;
        for (int x = 1; x < delta - 2; ++x) i = st.parent(i);
        int ki = Storage::key(i), kp = Storage::key(st.parent(i));
        nte.insert(kp, ki);
        nte.insert(ki, kp);
        unlink(i);
        link(l, r, reroot(h));
    }

    // Algorithm 5: delete tree edge (u, v) and search the smaller side for a replacement.
    void delete_te(Handle hu, Handle hv) {
        DC_COUNT(tree_edge_deletes, 1);
        Handle ch = st.parent(hv) == hu ? hv : hu;
        Handle r = unlink(ch);
        Handle rs = st.size(ch) < st.size(r) ? ch : r;
        Handle rl = rs == ch ? r : ch;

        // BFS over the smaller tree; candidates are non-tree edges whose far endpoint is in
        // the larger tree. Keep the first with minimal far-endpoint depth. Also note the first
        // non-root node (in BFS order) holding more than half the tree, for the centroid fix.
        const int32_t small_size = st.size(rs);
        Handle best_x = nil, best_y = nil, new_root = nil;
        int best_depth = -1;
        bfs_queue.clear();
        bfs_queue.push_back(Storage::key(rs));
        for (size_t head = 0; head < bfs_queue.size(); ++head) {
            int x = bfs_queue[head];
            Handle hx = st.handle(x);
            DC_COUNT(replacement_vertices, 1);
            if (new_root == nil && hx != rs && 2 * st.size(hx) > small_size) new_root = hx;
            for (int y : nte.items(x)) {
                DC_COUNT(replacement_edges_scanned, 1);
                Handle hy = st.handle(y);
                int d = 0;
                Handle top = hy;
                while (st.parent(top) != nil) {
                    top = st.parent(top);
                    ++d;
                    DC_COUNT(root_walk_steps, 1);
                }
                if (top == rl && (best_depth < 0 || d < best_depth)) {
                    best_depth = d;
                    best_x = st.handle(x);
                    best_y = hy;
                }
            }
            for (int c : children.items(x)) bfs_queue.push_back(c);
        }

        if (best_x == nil) {
            // No replacement: restore the smaller tree's centroid property by rerooting to the
            // first such node in BFS order (the authors' choice; the paper only says "a
            // non-root m with m.size > r_s.size / 2").
            if (new_root != nil) reroot(new_root);
            return;
        }

        int x = Storage::key(best_x), y = Storage::key(best_y);
        nte.erase(x, y);
        nte.erase(y, x);
        DC_COUNT(replacements_found, 1);
        insert_te(x, y, rs, rl);
    }
};

using DTreeP0 = DTreeImpl<DTreePointerStorage, dc::StdOrderedSets>;
using DTreeF2 = DTreeImpl<DTreeFlatStorage, dc::FlatOrderedSets>;
