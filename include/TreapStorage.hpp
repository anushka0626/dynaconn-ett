#pragma once
#include "MemoryAccounting.hpp"
#include <cstdint>
#include <memory>
#include <vector>

// Node storage policies for EulerTourForest. The forest's algorithm code only touches
// nodes through Handle and storage[h]. Swapping the policy changes memory layout and
// nothing else.
//
// Nodes can carry an augmentation (a base class, so an empty one costs nothing):
//   NoAug      plain ETT
//   WeightAug  HKS: subtree count of non-tree edge endpoints at vertex nodes
//   LevelAug   HDT: the above, plus subtree count of edge nodes flagged as "level i"

struct NoAug {};

struct WeightAug {
    int32_t weight = 0;
};

struct LevelAug {
    int32_t weight = 0;
    int32_t tree_count = 0;  // flagged edge nodes in the subtree
    uint8_t own_tree = 0;    // this edge node is flagged
};

// Heap-allocated node addressed by raw 64-bit pointers. Without augmentation: 40 B, in a
// 48 B glibc chunk.
template <class Aug>
struct PointerNodeT : Aug {
    int32_t u, v;  // u == v for a vertex's own node; u != v for a directed tree-edge visit
    uint32_t priority;
    int32_t size;
    PointerNodeT* left = nullptr;
    PointerNodeT* right = nullptr;
    PointerNodeT* parent = nullptr;
};

// Arena node addressed by 32-bit indices. Without augmentation: 28 B of fields padded to
// 32 B (two per cache line). WeightAug also fits in 32 B.
template <class Aug>
struct alignas(32) ArenaNodeT : Aug {
    int32_t u, v;
    uint32_t priority;
    int32_t size;
    int32_t left = -1;
    int32_t right = -1;
    int32_t parent = -1;
};

using PointerNode = PointerNodeT<NoAug>;
using ArenaNode = ArenaNodeT<NoAug>;
static_assert(sizeof(PointerNode) == 40);
static_assert(sizeof(ArenaNode) == 32);
static_assert(sizeof(ArenaNodeT<WeightAug>) == 32);

template <class Aug>
class PointerStorageT {
public:
    using Node = PointerNodeT<Aug>;
    using Handle = Node*;
    static constexpr Handle nil = nullptr;
    static constexpr bool release_on_destroy = true;  // nodes are separate allocations

    void reserve(size_t) {}

    Handle allocate(int32_t u, int32_t v, uint32_t priority) {
        Handle h = alloc.allocate(1);
        ::new (h) Node{Aug{}, u, v, priority, 1};
        return h;
    }

    void release(Handle h) {
        std::destroy_at(h);
        alloc.deallocate(h, 1);
    }

    Node& operator[](Handle h) { return *h; }
    const Node& operator[](Handle h) const { return *h; }

private:
    dc::Alloc<Node, dc::MemCategory::TreapNodes> alloc;
};

// Contiguous pool with a LIFO free list. A Node& is only valid until the next allocate(),
// because the pool may grow, so hold indices across allocations, never references.
template <class Aug>
class ArenaStorageT {
public:
    using Node = ArenaNodeT<Aug>;
    using Handle = int32_t;
    static constexpr Handle nil = -1;
    static constexpr bool release_on_destroy = false;  // the pool vector frees everything

    void reserve(size_t nodes) { pool.reserve(nodes); }

    Handle allocate(int32_t u, int32_t v, uint32_t priority) {
        if (!free_list.empty()) {
            Handle idx = free_list.back();
            free_list.pop_back();
            pool[idx] = Node{Aug{}, u, v, priority, 1};
            return idx;
        }
        pool.push_back(Node{Aug{}, u, v, priority, 1});
        return static_cast<Handle>(pool.size() - 1);
    }

    void release(Handle h) { free_list.push_back(h); }

    Node& operator[](Handle h) { return pool[h]; }
    const Node& operator[](Handle h) const { return pool[h]; }

private:
    std::vector<Node, dc::Alloc<Node, dc::MemCategory::TreapNodes>> pool;
    std::vector<Handle, dc::Alloc<Handle, dc::MemCategory::FreeList>> free_list;
};

using PointerStorage = PointerStorageT<NoAug>;
using ArenaStorage = ArenaStorageT<NoAug>;
