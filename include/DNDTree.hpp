#pragma once
#include "DynamicConnectivity.hpp"
#include "MemoryAccounting.hpp"
#include "OrderedSets.hpp"
#include "Stats.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// DND-trees (Xu, Wen, Qin, Li, Zhang & Lin, SIGMOD 2024; extended arXiv 2601.17285),
// reimplemented from the paper's pseudocode (Algorithms 1-6); no reference code was used.
// Ambiguity resolutions are in docs/IMPLEMENTATION_NOTES.md.
//
// Two trees per component:
//  - ID-tree: a spanning tree with parent and subtree size only, kept shallow by the
//    D-tree-style centroid heuristic on Link. Updates search the graph adjacency directly.
//    A tree-edge delete stops at the first replacement found.
//  - DS-tree: a disjoint-set tree (path compression, children in doubly linked lists) whose
//    root is kept equal to the ID-tree root. Queries are FindDS: amortized near-constant.
//
// Flat layout only (F2): all fields are int32 arrays, and adjacency is the shared
// FlatOrderedSets. The pre-registration uses Xu et al. only in its F2 layout (RQ3).
class DNDTree : public DynamicConnectivity {
    using IntVec = std::vector<int32_t, dc::Alloc<int32_t, dc::MemCategory::TreeNodes>>;
    static constexpr int32_t nil = -1;

public:
    explicit DNDTree(int vertices)
        : n(vertices), adj(vertices), parent(vertices, nil), st_size(vertices, 1), dsn(vertices),
          ds_id(2 * vertices), ds_parent(2 * vertices), ds_pre(2 * vertices), ds_next(2 * vertices),
          stamp(vertices, 0) {
        // DSnode i < n starts as vertex i's node. Node i + n is the sentinel of node i's
        // circular children list, and stays attached to node i when nodes swap owners.
        for (int32_t i = 0; i < n; ++i) {
            dsn[i] = i;
            ds_id[i] = i;
            ds_parent[i] = i;
            ds_pre[i] = ds_next[i] = nil;
            ds_pre[i + n] = ds_next[i + n] = i + n;
        }
    }

    bool is_connected(int u, int v) override { return u == v || find_ds(u) == find_ds(v); }

    // Algorithm 5 (DND-Insert, with Algorithm 1's ID-tree logic).
    void insert_edge(int u, int v) override {
        if (u == v || !adj.insert(u, v)) return;
        adj.insert(v, u);
        ++edges;
        int32_t root_u = find_ds(u), root_v = find_ds(v);

        if (root_u == root_v) {
            // Non-tree edge: optionally pull the deeper endpoint's path up (BFS heuristic).
            int du = depth_of(u), dv = depth_of(v);
            if (du < dv) {
                std::swap(u, v);
                std::swap(du, dv);
            }
            int delta = du - dv;
            if (delta <= 1) return;
            int32_t w = u;
            for (int i = 1; 2 * i < delta; ++i) w = parent[w];  // 1 <= i < delta / 2
            unlink(w);
            int32_t r = link(reroot(u), v, root_v);
            if (r != root_v) reroot_ds(r);  // keep DS root == ID root
            return;
        }

        // Tree edge: merge the smaller tree into the larger.
        if (st_size[root_u] > st_size[root_v]) {
            std::swap(u, v);
            std::swap(root_u, root_v);
        }
        reroot(u);
        link_ds(root_u, root_v);
        int32_t r = link(u, v, root_v);
        if (r != root_v) reroot_ds(r);
    }

    // Algorithm 6 (DND-Delete, with Algorithm 2's ID-tree logic).
    void delete_edge(int u, int v) override {
        if (u == v || !adj.erase(u, v)) return;
        adj.erase(v, u);
        --edges;
        if (parent[u] != v && parent[v] != u) return;  // non-tree edge: adjacency only

        DC_COUNT(tree_edge_deletes, 1);
        if (parent[v] == u) std::swap(u, v);  // u is the child
        int32_t root_v = unlink(u);
        if (st_size[root_v] < st_size[u]) std::swap(u, root_v);  // u: root of the smaller tree

        // BFS over the smaller tree via graph adjacency; stop at the first replacement.
        // S (epoch-stamped) holds every vertex known to be on the small side.
        if (++epoch == 0) {
            std::fill(stamp.begin(), stamp.end(), 0);
            epoch = 1;
        }
        queue.clear();
        queue.push_back(u);
        stamp[u] = epoch;
        for (size_t head = 0; head < queue.size(); ++head) {
            int32_t x = queue[head];
            DC_COUNT(replacement_vertices, 1);
            for (int32_t y : adj.items(x)) {
                if (y == parent[x]) continue;
                if (parent[y] == x) {  // tree child
                    queue.push_back(y);
                    stamp[y] = epoch;
                    continue;
                }
                DC_COUNT(replacement_edges_scanned, 1);
                bool succ = true;
                for (int32_t w = y; w != nil; w = parent[w]) {
                    DC_COUNT(root_walk_steps, 1);
                    if (stamp[w] == epoch) {
                        succ = false;
                        break;
                    }
                    stamp[w] = epoch;
                }
                if (succ) {
                    DC_COUNT(replacements_found, 1);
                    int32_t r = link(reroot(x), y, root_v);
                    reroot_ds(r);
                    return;
                }
            }
        }

        // No replacement: S is exactly the smaller tree (Lemma 7). Split the DS-tree.
        reroot_ds(root_v);
        isolate(u);
        for (int32_t w : queue) {  // the BFS visited exactly the small tree's vertices
            if (w == u) continue;
            isolate(w);
            link_ds(w, u);
        }
    }

    size_t edge_count() const override { return edges; }

    // ID-tree sizes and edges, DS-tree list consistency, and agreement of the two trees'
    // partitions and roots. O(n * depth). Empty string if all hold.
    std::string check_invariants() const {
        std::vector<int32_t> expected(n, 0);
        for (int32_t x = 0; x < n; ++x) {
            if (parent[x] != nil && !adj.contains(x, parent[x])) return "ID-tree edge is not a graph edge";
            for (int32_t w = x; w != nil; w = parent[w]) ++expected[w];
        }
        for (int32_t x = 0; x < n; ++x)
            if (expected[x] != st_size[x]) return "ID-tree subtree size is wrong at " + std::to_string(x);

        for (int32_t x = 0; x < n; ++x) {
            int32_t id_root = x;
            while (parent[id_root] != nil) id_root = parent[id_root];
            int32_t node = dsn[x];
            if (ds_id[node] != x) return "DSnode id mismatch";
            while (ds_parent[node] != node) node = ds_parent[node];
            if (ds_id[node] != id_root) return "DS root differs from ID root for " + std::to_string(x);
            // Children lists: every child's parent is the list owner.
            int32_t owner = dsn[x], sentinel = owner + n;
            for (int32_t c = ds_next[sentinel]; c != sentinel; c = ds_next[c]) {
                if (ds_parent[c] != owner) return "DS child list holds a non-child";
                if (ds_next[ds_pre[c]] != c) return "DS list links broken";
            }
        }
        if (adj.total() != 2 * edges) return "edge count mismatch";
        return {};
    }

private:
    int n;
    dc::FlatOrderedSets<dc::MemCategory::Adjacency> adj;  // all graph edges, tree and non-tree
    size_t edges = 0;

    // ID-tree
    IntVec parent;
    IntVec st_size;

    // DS-tree: DSnode arrays. dsn[v] is vertex v's node; ds_id[node] its vertex.
    IntVec dsn, ds_id, ds_parent, ds_pre, ds_next;

    // Delete-search scratch
    std::vector<uint32_t, dc::Alloc<uint32_t, dc::MemCategory::Scratch>> stamp;
    std::vector<int32_t, dc::Alloc<int32_t, dc::MemCategory::Scratch>> queue;
    uint32_t epoch = 0;

    // ---- ID-tree operators (D-tree's, without children / non-tree sets) ----

    int depth_of(int32_t x) const {
        int d = 0;
        for (; parent[x] != nil; x = parent[x]) ++d;
        return d;
    }

    int32_t reroot(int32_t w) {
        int32_t ch = w, cur = parent[w];
        parent[w] = nil;
        while (cur != nil) {
            int32_t g = parent[cur];
            parent[cur] = ch;
            ch = cur;
            cur = g;
        }
        while (parent[ch] != nil) {  // fix sizes from the old root back to w
            int32_t p = parent[ch];
            st_size[ch] -= st_size[p];
            st_size[p] += st_size[ch];
            ch = p;
        }
        return w;
    }

    // Attach the tree rooted at u below v (v's tree has root root_v), with the centroid
    // heuristic. Returns the merged tree's root.
    int32_t link(int32_t u, int32_t v, int32_t root_v) {
        parent[u] = v;
        const int64_t total = static_cast<int64_t>(st_size[root_v]) + st_size[u];
        const int32_t added = st_size[u];
        int32_t m = nil;
        for (int32_t i = v; i != nil; i = parent[i]) {
            st_size[i] += added;
            if (m == nil && 2 * static_cast<int64_t>(st_size[i]) > total) m = i;
        }
        if (m != nil && m != root_v) root_v = reroot(m);
        return root_v;
    }

    // Cut u (not a root) from its parent; returns the root of the remainder.
    int32_t unlink(int32_t u) {
        int32_t i = u;
        while (parent[i] != nil) {
            i = parent[i];
            st_size[i] -= st_size[u];
        }
        parent[u] = nil;
        return i;
    }

    // ---- DS-tree operators (Algorithm 4), on DSnode indices ----

    void unlink_ds_node(int32_t x) {
        ds_next[ds_pre[x]] = ds_next[x];
        ds_pre[ds_next[x]] = ds_pre[x];
        ds_parent[x] = x;
        ds_pre[x] = ds_next[x] = nil;
    }

    void link_ds_node(int32_t x, int32_t p) {  // x becomes p's first child
        int32_t sentinel = p + n;
        ds_parent[x] = p;
        ds_pre[x] = sentinel;
        ds_next[x] = ds_next[sentinel];
        ds_pre[ds_next[x]] = x;
        ds_next[sentinel] = x;
    }

    void link_ds(int32_t u, int32_t v) { link_ds_node(dsn[u], dsn[v]); }

    // FindDS with path compression: every node on the path is re-linked under the root.
    int32_t find_ds(int32_t u) {
        int32_t x = dsn[u];
        int32_t root = x;
        while (ds_parent[root] != root) root = ds_parent[root];
        while (x != root) {
            int32_t next = ds_parent[x];
            unlink_ds_node(x);
            link_ds_node(x, root);
            x = next;
        }
        return ds_id[root];
    }

    // Remove u from its DS-tree, re-attaching u's children to the root. Never the root.
    void isolate(int32_t u) {
        int32_t root = dsn[find_ds(u)];
        int32_t x = dsn[u];
        unlink_ds_node(x);
        int32_t sentinel = x + n;
        while (ds_next[sentinel] != sentinel) {
            int32_t c = ds_next[sentinel];
            unlink_ds_node(c);
            link_ds_node(c, root);
        }
    }

    // Make u the DS root in O(1): swap which vertex owns the root node and u's node.
    void reroot_ds(int32_t u) {
        int32_t r = find_ds(u);
        if (r == u) return;
        std::swap(dsn[u], dsn[r]);
        ds_id[dsn[u]] = u;
        ds_id[dsn[r]] = r;
    }
};
