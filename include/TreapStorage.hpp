#pragma once
#include "MemoryAccounting.hpp"
#include <cstdint>
#include <memory>
#include <vector>

// Node storage policies for EulerTourForest. The forest's algorithm code only touches
// nodes through Handle and storage[h]. Swapping the policy changes memory layout and
// nothing else.

// Heap-allocated node addressed by raw 64-bit pointers. 40 B; glibc puts it in a 48 B chunk.
struct PointerNode {
    int32_t u, v;  // u == v for a vertex's own node; u != v for a directed tree-edge visit
    uint32_t priority;
    int32_t size;
    PointerNode* left = nullptr;
    PointerNode* right = nullptr;
    PointerNode* parent = nullptr;
};
static_assert(sizeof(PointerNode) == 40);

// Arena node addressed by 32-bit indices. 28 B of fields padded to 32 B (two per cache line).
struct alignas(32) ArenaNode {
    int32_t u, v;
    uint32_t priority;
    int32_t size;
    int32_t left = -1;
    int32_t right = -1;
    int32_t parent = -1;
};
static_assert(sizeof(ArenaNode) == 32);

class PointerStorage {
public:
    using Node = PointerNode;
    using Handle = PointerNode*;
    static constexpr Handle nil = nullptr;
    static constexpr bool release_on_destroy = true;  // nodes are separate allocations

    void reserve(size_t) {}

    Handle allocate(int32_t u, int32_t v, uint32_t priority) {
        Handle h = alloc.allocate(1);
        ::new (h) Node{u, v, priority, 1};
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
class ArenaStorage {
public:
    using Node = ArenaNode;
    using Handle = int32_t;
    static constexpr Handle nil = -1;
    static constexpr bool release_on_destroy = false;  // the pool vector frees everything

    void reserve(size_t nodes) { pool.reserve(nodes); }

    Handle allocate(int32_t u, int32_t v, uint32_t priority) {
        if (!free_list.empty()) {
            Handle idx = free_list.back();
            free_list.pop_back();
            pool[idx] = Node{u, v, priority, 1};
            return idx;
        }
        pool.push_back(Node{u, v, priority, 1});
        return static_cast<Handle>(pool.size() - 1);
    }

    void release(Handle h) { free_list.push_back(h); }

    Node& operator[](Handle h) { return pool[h]; }
    const Node& operator[](Handle h) const { return pool[h]; }

private:
    std::vector<Node, dc::Alloc<Node, dc::MemCategory::TreapNodes>> pool;
    std::vector<Handle, dc::Alloc<Handle, dc::MemCategory::FreeList>> free_list;
};
