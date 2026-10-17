// Measured memory footprint of every structure after the same insert-only workload.
//
// Usage: memory_profile [vertices=500] [edges=2000] [seed=42] [--csv]
//
// Primary metric: heap bytes = usable bytes + 8 B glibc chunk header per live allocation,
// from global operator new/delete hooks. This is deterministic and counts the same way for
// every structure. Per-category numbers come from the tagged container allocators.
// "other" is the remainder: the structure object itself and anything untagged.
// The mallinfo2 delta is printed as a cross-check; it can differ slightly because of
// glibc's tcache and top-chunk behavior.
#define DC_INSTALL_GLOBAL_ALLOC_HOOKS
#include "MemoryAccounting.hpp"

#include "BaselineBFS.hpp"
#include "BaselineDSU.hpp"
#include "CachedBFS.hpp"
#include "ChurnGenerator.hpp"
#include "DNDTree.hpp"
#include "DTree.hpp"
#include "DynamicGraphETT.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <memory>
#include <string>
#include <vector>

#ifndef DC_MEMORY_ACCOUNTING
#error "memory_profile must be compiled with DC_MEMORY_ACCOUNTING"
#endif

namespace {

struct Snapshot {
    dc::MemCounters global;
    std::array<dc::MemCounters, dc::kNumMemCategories> categories;
    size_t mallinfo_bytes;
};

Snapshot take_snapshot() {
    struct mallinfo2 mi = mallinfo2();
    return {dc::global_alloc_counters(), dc::category_counters(), mi.uordblks + mi.hblkhd};
}

dc::MemCounters diff(const dc::MemCounters& after, const dc::MemCounters& before) {
    return {after.requested_bytes - before.requested_bytes, after.usable_bytes - before.usable_bytes,
            after.allocations - before.allocations};
}

struct Row {
    std::string name;
    size_t edges;
    size_t heap_bytes;
    size_t allocations;
    long long mallinfo_delta;
    std::array<size_t, dc::kNumMemCategories> category_heap_bytes;
    size_t other_bytes;
};

template <class S>
Row measure(const char* name, int n, const Workload& w) {
    Snapshot before = take_snapshot();
    auto s = std::make_unique<S>(n);
    for (const auto& op : w.ops) s->insert_edge(op.u, op.v);
    Snapshot after = take_snapshot();

    Row row{name, s->edge_count(), 0, 0, 0, {}, 0};
    dc::MemCounters total = diff(after.global, before.global);
    row.heap_bytes = dc::heap_bytes(total);
    row.allocations = total.allocations;
    row.mallinfo_delta = static_cast<long long>(after.mallinfo_bytes) - static_cast<long long>(before.mallinfo_bytes);

    size_t tagged = 0;
    for (int c = 0; c < dc::kNumMemCategories; ++c) {
        row.category_heap_bytes[c] = dc::heap_bytes(diff(after.categories[c], before.categories[c]));
        tagged += row.category_heap_bytes[c];
    }
    row.other_bytes = row.heap_bytes - tagged;
    return row;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> positional;
    bool csv = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--csv") == 0) csv = true;
        else positional.push_back(argv[i]);
    }
    const int n = positional.size() > 0 ? std::atoi(positional[0].c_str()) : 500;
    const int m = positional.size() > 1 ? std::atoi(positional[1].c_str()) : 2000;
    const uint64_t seed = positional.size() > 2 ? std::strtoull(positional[2].c_str(), nullptr, 10) : 42;

    ChurnConfig cfg;
    cfg.num_vertices = n;
    cfg.num_operations = m;
    cfg.p_insert = 1.0;  // m distinct edges, no duplicates
    cfg.seed = seed;
    Workload w = generate_churn_workload(cfg);

    std::vector<Row> rows;
    rows.push_back(measure<BaselineBFS>("bfs", n, w));
    rows.push_back(measure<BaselineDSU>("dsu_eager", n, w));
    rows.push_back(measure<LazyDSU>("dsu_lazy", n, w));
    rows.push_back(measure<CachedBFS>("cached_bfs", n, w));
    rows.push_back(measure<EttP0>("ett_p0", n, w));
    rows.push_back(measure<EttF0>("ett_f0", n, w));
    rows.push_back(measure<EttF1>("ett_f1", n, w));
    rows.push_back(measure<EttF2>("ett_f2", n, w));
    rows.push_back(measure<DTreeP0>("dtree_p0", n, w));
    rows.push_back(measure<DTreeF2>("dtree_f2", n, w));
    rows.push_back(measure<DNDTree>("dnd_xu", n, w));

    if (csv) {
        std::printf("structure,vertices,edges,heap_bytes,bytes_per_edge,allocations,mallinfo_delta");
        for (int c = 0; c < dc::kNumMemCategories; ++c) std::printf(",%s", dc::to_string(static_cast<dc::MemCategory>(c)));
        std::printf(",other\n");
        for (const auto& r : rows) {
            std::printf("%s,%d,%zu,%zu,%.2f,%zu,%lld", r.name.c_str(), n, r.edges, r.heap_bytes,
                        static_cast<double>(r.heap_bytes) / r.edges, r.allocations, r.mallinfo_delta);
            for (size_t b : r.category_heap_bytes) std::printf(",%zu", b);
            std::printf(",%zu\n", r.other_bytes);
        }
        return 0;
    }

    std::printf("Memory footprint after inserting %zu distinct edges on V=%d (seed %llu)\n\n", rows[0].edges, n,
                static_cast<unsigned long long>(seed));
    std::printf("%-12s %10s %10s %8s %12s |", "structure", "heap KiB", "B/edge", "allocs", "mallinfo KiB");
    for (int c = 0; c < dc::kNumMemCategories; ++c) std::printf(" %12s", dc::to_string(static_cast<dc::MemCategory>(c)));
    std::printf(" %8s\n", "other");
    for (const auto& r : rows) {
        std::printf("%-12s %10.1f %10.2f %8zu %12.1f |", r.name.c_str(), r.heap_bytes / 1024.0,
                    static_cast<double>(r.heap_bytes) / r.edges, r.allocations, r.mallinfo_delta / 1024.0);
        for (size_t b : r.category_heap_bytes) std::printf(" %12zu", b);
        std::printf(" %8zu\n", r.other_bytes);
    }
    std::printf("\nCategory columns and 'other' are heap bytes (usable + 8 B header per allocation).\n");
    return 0;
}
