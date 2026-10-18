#pragma once
#include "DynamicConnectivity.hpp"
#include "EdgeMaps.hpp"
#include "EulerTourForest.hpp"
#include "MemoryAccounting.hpp"
#include "OrderedSets.hpp"
#include "Stats.hpp"
#include "TreapStorage.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// HDT (Holm, de Lichtenberg & Thorup, JACM 2001): fully dynamic connectivity in amortized
// O(log^2 n) per update. Reimplemented from the paper; docs/IMPLEMENTATION_NOTES.md.
//
// Every edge has a level in [0, L], L = floor(log2 n). F[i] is an Euler-tour forest of the
// tree edges with level >= i, so F[0] is the spanning forest and answers queries.
// Invariant: a tree of F[i] has at most n / 2^i vertices.
// Treap nodes in F[i] carry (LevelAug):
//   weight      level-i non-tree edge endpoints at each vertex (subtree sums)
//   tree_count  edge nodes of tree edges whose level is exactly i
// so the level-i search finds the next edge to examine in O(log n).
//
// Deleting a tree edge of level l: cut it from F[0..l]; then for i = l down to 0, with T
// the smaller side in F[i]:
//   1. push every level-i tree edge of T to level i + 1 (keeps the invariant);
//   2. scan T's level-i non-tree edges: one that leaves T is the replacement (link it in
//      F[0..i] at level i, stop). Otherwise both ends are in T, so raise it to level i + 1.
template <class Storage, template <class> class EdgeMapT, class Adjacency>
class HDTImpl : public DynamicConnectivity {
    using Forest = EulerTourForest<Storage, EdgeMapT>;
    static_assert(Forest::kWeighted && Forest::kFlagged, "HDT needs LevelAug treap nodes");

    struct EdgeInfo {
        int32_t level = 0;
        bool tree = false;
    };

public:
    explicit HDTImpl(int vertices) : n(vertices) {
        int levels = 1;
        while ((int64_t{1} << levels) <= vertices) ++levels;  // floor(log2 n) + 1 levels
        for (int i = 0; i < levels; ++i) {
            forests.push_back(std::make_unique<Forest>(vertices, Forest::default_seed + static_cast<uint32_t>(i)));
            non_tree.push_back(std::make_unique<Adjacency>(vertices));
        }
    }

    bool is_connected(int u, int v) override { return forests[0]->connected(u, v); }

    void insert_edge(int u, int v) override {
        if (u == v) return;
        int a = std::min(u, v), b = std::max(u, v);
        if (info.contains(a, b)) return;
        ++edges;
        if (!forests[0]->connected(a, b)) {
            forests[0]->link(a, b);
            forests[0]->set_edge_flag(a, b, true);
            info.insert(a, b, EdgeInfo{0, true});
        } else {
            add_non_tree(0, a, b);
            info.insert(a, b, EdgeInfo{0, false});
        }
    }

    void delete_edge(int u, int v) override {
        if (u == v) return;
        int a = std::min(u, v), b = std::max(u, v);
        const EdgeInfo* e = info.find(a, b);
        if (!e) return;
        EdgeInfo edge = *e;
        info.erase(a, b);
        --edges;

        if (!edge.tree) {
            remove_non_tree(edge.level, a, b);
            return;
        }
        DC_COUNT(tree_edge_deletes, 1);
        for (int i = 0; i <= edge.level; ++i) forests[i]->cut(a, b);

        for (int i = edge.level; i >= 0; --i) {
            if (replace_at_level(i, a, b)) return;
        }
    }

    size_t edge_count() const override { return edges; }

    // Per-level forest invariants plus the HDT ones: levels, flags, weights, nesting.
    std::string check_invariants() const {
        const int levels = static_cast<int>(forests.size());
        for (int i = 0; i < levels; ++i) {
            std::string err = forests[i]->check_invariants();
            if (!err.empty()) return "F[" + std::to_string(i) + "]: " + err;
            for (int x = 0; x < n; ++x)
                if (static_cast<size_t>(forests[i]->vertex_weight(x)) != non_tree[i]->size(x))
                    return "level-" + std::to_string(i) + " weight != non-tree degree";
        }
        size_t tree_edges = 0, non_tree_edges = 0;
        std::string err;
        info.for_each([&](int32_t a, int32_t b, const EdgeInfo& e) {
            if (!err.empty()) return;
            if (e.level < 0 || e.level >= levels) {
                err = "edge level out of range";
                return;
            }
            if (e.tree) {
                ++tree_edges;
                for (int i = 0; i < levels; ++i) {
                    bool in = forests[i]->has_tree_edge(a, b);
                    if (in != (i <= e.level)) err = "tree edge in the wrong forests";
                    else if (in && forests[i]->edge_flag(a, b) != (i == e.level)) err = "level flag is wrong";
                }
            } else {
                ++non_tree_edges;
                if (!non_tree[e.level]->contains(a, b) || !non_tree[e.level]->contains(b, a)) err = "non-tree edge missing from its level";
                else if (!forests[e.level]->connected(a, b)) err = "non-tree edge endpoints not connected at its level";
            }
        });
        if (!err.empty()) return err;
        if (tree_edges + non_tree_edges != edges) return "edge count mismatch";
        size_t nt_entries = 0;
        for (const auto& s : non_tree) nt_entries += s->total();
        if (nt_entries != 2 * non_tree_edges) return "stray non-tree entries";
        // Size invariant: trees of F[i] have at most n / 2^i vertices (3k - 2 treap nodes).
        for (int i = 1; i < levels; ++i)
            for (int x = 0; x < n; ++x) {
                int64_t k = (forests[i]->tour_size(forests[i]->root_of(x)) + 2) / 3;
                if (k > (static_cast<int64_t>(n) >> i)) return "level-" + std::to_string(i) + " tree too large";
            }
        return {};
    }

    // All levels' treap shapes, for the P0/F2 behavior-identity test.
    std::vector<int64_t> serialize() const {
        std::vector<int64_t> out;
        for (const auto& f : forests) {
            auto part = f->serialize();
            out.insert(out.end(), part.begin(), part.end());
            out.push_back(-7);
        }
        return out;
    }

private:
    int n;
    std::vector<std::unique_ptr<Forest>> forests;     // forests[i] = F[i]
    std::vector<std::unique_ptr<Adjacency>> non_tree; // non_tree[i]: level-i non-tree edges
    EdgeMapT<EdgeInfo> info;                          // normalized (a < b) -> level, tree?
    size_t edges = 0;

    void add_non_tree(int level, int x, int y) {
        non_tree[level]->insert(x, y);
        non_tree[level]->insert(y, x);
        forests[level]->add_vertex_weight(x, 1);
        forests[level]->add_vertex_weight(y, 1);
    }

    void remove_non_tree(int level, int x, int y) {
        non_tree[level]->erase(x, y);
        non_tree[level]->erase(y, x);
        forests[level]->add_vertex_weight(x, -1);
        forests[level]->add_vertex_weight(y, -1);
    }

    EdgeInfo& edge_info(int x, int y) { return *info.find(std::min(x, y), std::max(x, y)); }

    // One level of the replacement search. Returns true if a replacement was found.
    bool replace_at_level(int i, int a, int b) {
        Forest& f = *forests[i];
        auto ra = f.root_of(a), rb = f.root_of(b);
        auto small = f.tour_size(ra) <= f.tour_size(rb) ? ra : rb;
        DC_COUNT(replacement_vertices, (f.tour_size(small) + 2) / 3);

        // 1. Push the small tree's level-i tree edges to level i + 1.
        int x, y;
        while (f.first_flagged_edge(small, x, y)) {
            assert(i + 1 < static_cast<int>(forests.size()));
            f.set_edge_flag(x, y, false);
            edge_info(x, y).level = i + 1;
            DC_COUNT(level_raises, 1);
            forests[i + 1]->link(x, y);
            forests[i + 1]->set_edge_flag(x, y, true);
        }

        // 2. Scan its level-i non-tree edges.
        for (int s = f.first_weighted_vertex(small); s >= 0; s = f.first_weighted_vertex(small)) {
            int t = non_tree[i]->items(s)[0];
            DC_COUNT(replacement_edges_scanned, 1);
            remove_non_tree(i, s, t);
            if (f.root_of(t) != small) {
                // Replacement: becomes a level-i tree edge, in F[0..i].
                EdgeInfo& e = edge_info(s, t);
                e.tree = true;
                for (int j = 0; j <= i; ++j) forests[j]->link(s, t);
                f.set_edge_flag(s, t, true);
                DC_COUNT(replacements_found, 1);
                return true;
            }
            assert(i + 1 < static_cast<int>(forests.size()));
            add_non_tree(i + 1, s, t);
            edge_info(s, t).level = i + 1;
            DC_COUNT(level_raises, 1);
        }
        return false;
    }
};

using HdtP0 = HDTImpl<PointerStorageT<LevelAug>, dc::StdEdgeMap, dc::StdOrderedSets<dc::MemCategory::Adjacency>>;
using HdtF2 = HDTImpl<ArenaStorageT<LevelAug>, dc::FlatEdgeMap, dc::FlatOrderedSets<dc::MemCategory::Adjacency>>;
