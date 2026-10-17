#pragma once
#include <array>
#include <cstddef>
#include <memory>

#if defined(DC_MEMORY_ACCOUNTING) || defined(DC_INSTALL_GLOBAL_ALLOC_HOOKS)
#include <malloc.h>
#endif

// Memory accounting for the footprint comparison.
//
// Every container in the data structures takes its allocator from dc::Alloc<T, Category>.
// In normal builds that is exactly std::allocator<T>, so the timed code is unchanged.
// When DC_MEMORY_ACCOUNTING is defined, it becomes a TaggedAllocator that counts live
// bytes per category. That gives the breakdown into nodes / edge maps / adjacency / etc.
// Define the macro for a whole executable, never for a single translation unit (ODR).
//
// DC_INSTALL_GLOBAL_ALLOC_HOOKS (define in exactly one TU) also replaces global
// operator new/delete to count every heap allocation. That total is the cross-check.

namespace dc {

enum class MemCategory : int {
    TreapNodes,   // ETT node storage (pointer nodes or the arena pool)
    TreeNodes,    // other tree node storage (D-tree / ID-tree nodes, child sets)
    FreeList,     // arena free list
    VertexIndex,  // per-vertex arrays (vertex -> node handle, DSU parent/rank)
    EdgeMap,      // edge lookup structures (tree-edge map, DSU edge set)
    Adjacency,    // adjacency / non-tree incidence sets
    Scratch,      // persistent scratch buffers
    Count
};

inline constexpr int kNumMemCategories = static_cast<int>(MemCategory::Count);

inline const char* to_string(MemCategory c) {
    switch (c) {
        case MemCategory::TreapNodes: return "treap_nodes";
        case MemCategory::TreeNodes: return "tree_nodes";
        case MemCategory::FreeList: return "free_list";
        case MemCategory::VertexIndex: return "vertex_index";
        case MemCategory::EdgeMap: return "edge_map";
        case MemCategory::Adjacency: return "adjacency";
        case MemCategory::Scratch: return "scratch";
        case MemCategory::Count: break;
    }
    return "?";
}

struct MemCounters {
    size_t requested_bytes = 0;  // sum of n * sizeof(T) asked for
    size_t usable_bytes = 0;     // what malloc actually reserved (malloc_usable_size)
    size_t allocations = 0;      // live allocation count
};

// glibc puts an 8-byte size header in front of every in-use chunk (usable = chunk - 8),
// so the heap footprint of a set of live allocations is usable + 8 * count.
inline constexpr size_t kGlibcChunkHeader = 8;

inline size_t heap_bytes(const MemCounters& c) {
    return c.usable_bytes + kGlibcChunkHeader * c.allocations;
}

#ifdef DC_MEMORY_ACCOUNTING

inline std::array<MemCounters, kNumMemCategories>& category_counters() {
    static std::array<MemCounters, kNumMemCategories> counters{};
    return counters;
}

template <class T, MemCategory C>
struct TaggedAllocator {
    using value_type = T;

    // Needed because allocator_traits cannot rebind a template with a non-type parameter.
    template <class U>
    struct rebind {
        using other = TaggedAllocator<U, C>;
    };

    TaggedAllocator() noexcept = default;
    template <class U>
    TaggedAllocator(const TaggedAllocator<U, C>&) noexcept {}

    T* allocate(size_t n) {
        T* p = std::allocator<T>{}.allocate(n);
        auto& c = category_counters()[static_cast<int>(C)];
        c.requested_bytes += n * sizeof(T);
        c.usable_bytes += malloc_usable_size(p);
        ++c.allocations;
        return p;
    }

    void deallocate(T* p, size_t n) noexcept {
        auto& c = category_counters()[static_cast<int>(C)];
        c.requested_bytes -= n * sizeof(T);
        c.usable_bytes -= malloc_usable_size(p);
        --c.allocations;
        std::allocator<T>{}.deallocate(p, n);
    }

    template <class U>
    friend bool operator==(const TaggedAllocator&, const TaggedAllocator<U, C>&) noexcept {
        return true;
    }
};

template <class T, MemCategory C>
using Alloc = TaggedAllocator<T, C>;

#else

template <class T, MemCategory C>
using Alloc = std::allocator<T>;

#endif

}  // namespace dc

#ifdef DC_INSTALL_GLOBAL_ALLOC_HOOKS
#include <cstdlib>
#include <new>

namespace dc {
// Totals over every operator new/delete in the process (usable bytes + live count).
inline MemCounters& global_alloc_counters() {
    static MemCounters counters{};
    return counters;
}

namespace detail {
inline void* counted_alloc(size_t n, size_t align) {
    if (n == 0) n = 1;
    void* p = nullptr;
    if (align <= alignof(std::max_align_t)) {
        p = std::malloc(n);
    } else if (posix_memalign(&p, align, n) != 0) {
        p = nullptr;
    }
    if (!p) throw std::bad_alloc();
    auto& c = global_alloc_counters();
    c.usable_bytes += malloc_usable_size(p);
    ++c.allocations;
    return p;
}

inline void counted_free(void* p) noexcept {
    if (!p) return;
    auto& c = global_alloc_counters();
    c.usable_bytes -= malloc_usable_size(p);
    --c.allocations;
    std::free(p);
}
}  // namespace detail
}  // namespace dc

void* operator new(size_t n) { return dc::detail::counted_alloc(n, 0); }
void* operator new[](size_t n) { return dc::detail::counted_alloc(n, 0); }
void* operator new(size_t n, std::align_val_t a) {
    return dc::detail::counted_alloc(n, static_cast<size_t>(a));
}
void* operator new[](size_t n, std::align_val_t a) {
    return dc::detail::counted_alloc(n, static_cast<size_t>(a));
}
void operator delete(void* p) noexcept { dc::detail::counted_free(p); }
void operator delete[](void* p) noexcept { dc::detail::counted_free(p); }
void operator delete(void* p, size_t) noexcept { dc::detail::counted_free(p); }
void operator delete[](void* p, size_t) noexcept { dc::detail::counted_free(p); }
void operator delete(void* p, std::align_val_t) noexcept { dc::detail::counted_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { dc::detail::counted_free(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { dc::detail::counted_free(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { dc::detail::counted_free(p); }
#endif
