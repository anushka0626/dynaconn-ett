#pragma once
#include "FlatHashMap.hpp"
#include "MemoryAccounting.hpp"
#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

// Per-vertex sets of ints in the canonical swap-remove order: insert appends; erase moves
// the set's last element into the erased slot. Every iterated neighbor or child set uses
// this order in every layout, so an algorithm's P0 and F2 variants iterate identically
// and make identical choices (docs/IMPLEMENTATION_NOTES.md).
//
// Both classes share one API:
//   insert(x, y) -> bool (true if new)   erase(x, y) -> bool   contains(x, y)
//   items(x) -> std::span<const int32_t>  size(x)   total()
// A span from items() is invalidated by the next insert into any set.

namespace dc {

// P0/F0: a std::vector per vertex, plus one std::unordered_map of positions.
template <MemCategory C>
class StdOrderedSets {
public:
    explicit StdOrderedSets(int n) : sets(n) {}

    bool insert(int32_t x, int32_t y) {
        if (!pos.emplace(edge_key(x, y), static_cast<uint32_t>(sets[x].size())).second) return false;
        sets[x].push_back(y);
        return true;
    }

    bool erase(int32_t x, int32_t y) {
        auto it = pos.find(edge_key(x, y));
        if (it == pos.end()) return false;
        uint32_t p = it->second;
        pos.erase(it);
        auto& s = sets[x];
        int32_t last = s.back();
        s.pop_back();
        if (p < s.size()) {
            s[p] = last;
            pos[edge_key(x, last)] = p;
        }
        return true;
    }

    bool contains(int32_t x, int32_t y) const { return pos.contains(edge_key(x, y)); }
    std::span<const int32_t> items(int32_t x) const { return {sets[x].data(), sets[x].size()}; }
    size_t size(int32_t x) const { return sets[x].size(); }
    size_t total() const { return pos.size(); }

private:
    using Vec = std::vector<int32_t, Alloc<int32_t, C>>;
    std::vector<Vec, Alloc<Vec, C>> sets;
    std::unordered_map<uint64_t, uint32_t, std::hash<uint64_t>, std::equal_to<uint64_t>,
                       Alloc<std::pair<const uint64_t, uint32_t>, C>>
        pos;
};

// F2: every set's elements live in one pooled array. Set x owns a block of capacity
// 4 * 2^k; a full block moves to the next size class, and freed blocks are reused per class.
// Positions are in the shared FlatHashMap.
template <MemCategory C>
class FlatOrderedSets {
public:
    explicit FlatOrderedSets(int n) : blocks(n) {}

    bool insert(int32_t x, int32_t y) {
        uint64_t k = edge_key(x, y);
        if (pos.contains(k)) return false;
        Block& b = blocks[x];
        if (b.size == capacity(b)) grow(x);
        Block& g = blocks[x];
        pool[g.offset + g.size] = y;
        pos.insert_or_assign(k, g.size);
        ++g.size;
        return true;
    }

    bool erase(int32_t x, int32_t y) {
        uint64_t k = edge_key(x, y);
        const uint32_t* p = pos.find(k);
        if (!p) return false;
        uint32_t i = *p;
        pos.erase(k);
        Block& b = blocks[x];
        int32_t last = pool[b.offset + b.size - 1];
        --b.size;
        if (i < b.size) {
            pool[b.offset + i] = last;
            pos.insert_or_assign(edge_key(x, last), i);
        }
        return true;
    }

    bool contains(int32_t x, int32_t y) const { return pos.contains(edge_key(x, y)); }
    std::span<const int32_t> items(int32_t x) const {
        const Block& b = blocks[x];
        return {pool.data() + b.offset, b.size};
    }
    size_t size(int32_t x) const { return blocks[x].size; }
    size_t total() const { return pos.size(); }

private:
    static constexpr uint8_t kNoBlock = 0xff;
    static constexpr uint32_t kMinCapacity = 4;

    struct Block {
        uint32_t offset = 0;
        uint32_t size = 0;
        uint8_t size_class = kNoBlock;
    };

    std::vector<int32_t, Alloc<int32_t, C>> pool;
    std::vector<Block, Alloc<Block, C>> blocks;
    std::array<std::vector<uint32_t, Alloc<uint32_t, C>>, 32> free_blocks;  // offsets, per class
    FlatHashMap<uint32_t, C> pos;

    static uint32_t capacity(const Block& b) {
        return b.size_class == kNoBlock ? 0 : kMinCapacity << b.size_class;
    }

    uint32_t take_block(uint8_t size_class) {
        auto& fl = free_blocks[size_class];
        if (!fl.empty()) {
            uint32_t off = fl.back();
            fl.pop_back();
            return off;
        }
        uint32_t off = static_cast<uint32_t>(pool.size());
        pool.resize(pool.size() + (kMinCapacity << size_class));
        return off;
    }

    void grow(int32_t x) {
        Block old = blocks[x];
        uint8_t cls = old.size_class == kNoBlock ? 0 : static_cast<uint8_t>(old.size_class + 1);
        uint32_t off = take_block(cls);
        for (uint32_t i = 0; i < old.size; ++i) pool[off + i] = pool[old.offset + i];
        if (old.size_class != kNoBlock) free_blocks[old.size_class].push_back(old.offset);
        blocks[x] = {off, old.size, cls};
    }
};

}  // namespace dc
