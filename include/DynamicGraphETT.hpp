#pragma once
#include "DynamicConnectivity.hpp"
#include "EdgeMaps.hpp"
#include "EulerTourForest.hpp"
#include "MemoryAccounting.hpp"
#include "OrderedSets.hpp"
#include "Stats.hpp"
#include "TreapStorage.hpp"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// Fully dynamic connectivity on arbitrary graphs: an Euler-tour spanning forest plus
// non-tree edges in per-vertex incidence sets.
//
// Deleting a tree edge searches for a replacement by scanning the non-tree edges of every
// vertex on the smaller side of the cut. There are no HDT edge levels, so a delete costs
// O(sum of degrees on the small side * log n) in the worst case. Inserts and queries are
// expected O(log n).
//
// Template parameters (layout ablation, plan 5.3, plus RQ5):
//   Storage    treap node storage: PointerStorage or ArenaStorage
//   EdgeMapT   tree-edge map: dc::StdEdgeMap or dc::FlatEdgeMap
//   Adjacency  per-vertex sets: dc::StdOrderedSets<...> or dc::FlatOrderedSets<...>
//   KHop       0: full search only. k > 0: first a bounded search up to k hops from the
//              cut, falling back to the full search (also maintains tree adjacency)
template <class Storage, template <class> class EdgeMapT = dc::StdEdgeMap,
          class Adjacency = dc::StdOrderedSets<dc::MemCategory::Adjacency>, int KHop = 0>
class DynamicGraphETTImpl : public DynamicConnectivity {
    static_assert(KHop >= 0);

public:
    explicit DynamicGraphETTImpl(int vertices)
        : n(vertices), forest(vertices), non_tree_adj(vertices), tree_adj(KHop > 0 ? vertices : 0) {
        if constexpr (KHop > 0) visit_stamp.assign(vertices, 0);
    }

    bool is_connected(int u, int v) override { return forest.connected(u, v); }

    void insert_edge(int u, int v) override {
        if (u == v) return;
        int a = std::min(u, v), b = std::max(u, v);
        if (forest.has_tree_edge(a, b) || non_tree_adj.contains(a, b)) return;

        if (!forest.connected(a, b)) {
            link(a, b);  // bridges two trees -> tree edge
        } else {
            non_tree_adj.insert(a, b);  // already connected -> non-tree edge
            non_tree_adj.insert(b, a);
            ++non_tree_count;
        }
    }

    void delete_edge(int u, int v) override {
        if (u == v) return;
        int a = std::min(u, v), b = std::max(u, v);

        if (non_tree_adj.erase(a, b)) {
            non_tree_adj.erase(b, a);
            --non_tree_count;
            return;
        }
        if (!forest.cut(a, b)) return;  // edge not present
        if constexpr (KHop > 0) {
            tree_adj.erase(a, b);
            tree_adj.erase(b, a);
        }
        DC_COUNT(tree_edge_deletes, 1);

        auto root_a = forest.root_of(a);
        auto root_b = forest.root_of(b);
        bool a_smaller = forest.tour_size(root_a) <= forest.tour_size(root_b);
        auto small_root = a_smaller ? root_a : root_b;
        auto large_root = a_smaller ? root_b : root_a;

        if constexpr (KHop > 0) {
            if (bounded_search(a_smaller ? a : b, large_root)) return;
        }

        // Full search: the smaller tree's non-tree edges, for one that reconnects the sides.
        small_vertices.clear();
        forest.for_each_vertex(small_root, [&](int x) { small_vertices.push_back(x); });
        DC_COUNT(replacement_vertices, small_vertices.size());

        for (int x : small_vertices) {
            for (int y : non_tree_adj.items(x)) {
                DC_COUNT(replacement_edges_scanned, 1);
                if (forest.root_of(y) == large_root) {
                    promote(x, y);
                    return;
                }
            }
        }
    }

    size_t edge_count() const override { return forest.num_tree_edges() + non_tree_count; }

    const EulerTourForest<Storage, EdgeMapT>& euler_forest() const { return forest; }

    // Forest invariants plus the graph-level ones. Empty string if all hold.
    std::string check_invariants() const {
        std::string err = forest.check_invariants();
        if (!err.empty()) return err;

        size_t directed = 0;
        for (int x = 0; x < n; ++x) {
            for (int y : non_tree_adj.items(x)) {
                ++directed;
                if (y < 0 || y >= n || y == x) return "bad non-tree neighbor";
                if (!non_tree_adj.contains(y, x)) return "non-tree adjacency is not symmetric";
                if (forest.has_tree_edge(x, y)) return "edge is both tree and non-tree";
                if (!forest.connected(x, y)) return "non-tree edge spans two trees";
            }
            if constexpr (KHop > 0) {
                for (int y : tree_adj.items(x))
                    if (!forest.has_tree_edge(x, y)) return "tree adjacency has a non-tree edge";
            }
        }
        if (directed != 2 * non_tree_count) return "non-tree edge count mismatch";
        if constexpr (KHop > 0) {
            if (tree_adj.total() != 2 * forest.num_tree_edges()) return "tree adjacency count mismatch";
        }
        return {};
    }

private:
    int n;
    EulerTourForest<Storage, EdgeMapT> forest;
    Adjacency non_tree_adj;
    Adjacency tree_adj;  // only maintained when KHop > 0
    size_t non_tree_count = 0;
    std::vector<int, dc::Alloc<int, dc::MemCategory::Scratch>> small_vertices;  // reused per delete

    // Bounded-search scratch (KHop > 0): epoch-stamped visited marks and a BFS frontier.
    std::vector<uint32_t, dc::Alloc<uint32_t, dc::MemCategory::Scratch>> visit_stamp;
    std::vector<std::pair<int, int>, dc::Alloc<std::pair<int, int>, dc::MemCategory::Scratch>> frontier;
    uint32_t epoch = 0;

    void link(int x, int y) {
        forest.link(x, y);
        if constexpr (KHop > 0) {
            tree_adj.insert(x, y);
            tree_adj.insert(y, x);
        }
    }

    void promote(int x, int y) {
        non_tree_adj.erase(x, y);
        non_tree_adj.erase(y, x);
        --non_tree_count;
        link(x, y);  // the replacement becomes a tree edge
        DC_COUNT(replacements_found, 1);
    }

    // BFS from s, the cut endpoint on the small side, over tree and non-tree edges. Vertices
    // within distance KHop - 1 of s have their non-tree edges checked for an endpoint on the
    // large side. With the cut edge gone, tree edges from the small side stay on it, and a
    // non-tree edge leaves it only if it is a replacement, so the search never needs to know
    // a vertex's side beyond that check. Returns true if a replacement was promoted.
    bool bounded_search(int s, typename Storage::Handle large_root) {
        DC_COUNT(khop_searches, 1);
        if (++epoch == 0) {  // stamp wrap-around: clear and restart at 1
            std::fill(visit_stamp.begin(), visit_stamp.end(), 0);
            epoch = 1;
        }
        frontier.clear();
        frontier.push_back({s, 0});
        visit_stamp[s] = epoch;

        for (size_t head = 0; head < frontier.size(); ++head) {
            auto [x, depth] = frontier[head];
            bool expand = depth + 1 < KHop;
            for (int y : non_tree_adj.items(x)) {
                DC_COUNT(khop_edges_scanned, 1);
                auto ry = forest.root_of(y);
                if (ry == large_root) {
                    DC_COUNT(khop_hits, 1);
                    promote(x, y);
                    return true;
                }
                // Not a replacement, so y is on the small side (a cut leaves exactly two trees).
                if (expand && visit_stamp[y] != epoch) {
                    visit_stamp[y] = epoch;
                    frontier.push_back({y, depth + 1});
                }
            }
            if (!expand) continue;
            for (int y : tree_adj.items(x)) {
                DC_COUNT(khop_edges_scanned, 1);
                if (visit_stamp[y] != epoch) {
                    visit_stamp[y] = epoch;
                    frontier.push_back({y, depth + 1});
                }
            }
        }
        return false;
    }
};

namespace dc {
template <MemCategory C>
using StdSets = StdOrderedSets<C>;
template <MemCategory C>
using FlatSets = FlatOrderedSets<C>;
}  // namespace dc

// Layout ablation (plan 5.3). Each step changes one thing.
using EttP0 = DynamicGraphETTImpl<PointerStorage, dc::StdEdgeMap, dc::StdSets<dc::MemCategory::Adjacency>>;
using EttF0 = DynamicGraphETTImpl<ArenaStorage, dc::StdEdgeMap, dc::StdSets<dc::MemCategory::Adjacency>>;
using EttF1 = DynamicGraphETTImpl<ArenaStorage, dc::FlatEdgeMap, dc::StdSets<dc::MemCategory::Adjacency>>;
using EttF2 = DynamicGraphETTImpl<ArenaStorage, dc::FlatEdgeMap, dc::FlatSets<dc::MemCategory::Adjacency>>;

// RQ5 variants: F2 plus the bounded k-hop search.
template <int K>
using EttF2KHop = DynamicGraphETTImpl<ArenaStorage, dc::FlatEdgeMap, dc::FlatSets<dc::MemCategory::Adjacency>, K>;

// Names used since Phase 2.
using DynamicGraphETT = EttP0;
using FlatDynamicGraphETT = EttF0;
