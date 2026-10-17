#pragma once
#include <cstdint>

// Operation counters (plan 5.5), compiled in only when DC_STATS is defined for the whole
// executable. They feed the RQ4 cost model and RQ5's work comparison. With DC_STATS off,
// DC_COUNT expands to nothing, so timed builds are unaffected.

namespace dc {

struct OpStats {
    uint64_t root_walk_steps = 0;          // parent steps while finding a root
    uint64_t index_walk_steps = 0;         // parent steps while computing a tour position
    uint64_t treap_splits = 0;
    uint64_t treap_merges = 0;
    uint64_t tree_edge_deletes = 0;        // deletes that cut a spanning-tree edge
    uint64_t replacement_vertices = 0;     // vertices collected on the small side
    uint64_t replacement_edges_scanned = 0;  // non-tree edges examined by the full search
    uint64_t replacements_found = 0;
    uint64_t khop_searches = 0;
    uint64_t khop_hits = 0;
    uint64_t khop_edges_scanned = 0;       // adjacency entries examined by the bounded search
    uint64_t dsu_rebuilds = 0;
    uint64_t bfs_vertices_visited = 0;
};

#ifdef DC_STATS
inline OpStats& stats() {
    static OpStats s;
    return s;
}
#define DC_COUNT(field, n) (::dc::stats().field += static_cast<uint64_t>(n))
#else
#define DC_COUNT(field, n) ((void)0)
#endif

}  // namespace dc
