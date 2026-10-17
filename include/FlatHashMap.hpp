#pragma once
#include "MemoryAccounting.hpp"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dc {

// murmur3 fmix64: spreads packed edge keys, whose low and high halves are small ints.
inline uint64_t mix64(uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

// Packs an ordered vertex pair into one key. Vertex ids are non-negative int32, so the
// all-ones key (FlatHashMap::kEmpty) never occurs.
inline uint64_t edge_key(int32_t a, int32_t b) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) | static_cast<uint32_t>(b);
}
inline int32_t key_first(uint64_t k) { return static_cast<int32_t>(k >> 32); }
inline int32_t key_second(uint64_t k) { return static_cast<int32_t>(k & 0xffffffffULL); }

// The shared flat hash map used by every F1/F2 layout: one contiguous slot array, open
// addressing with linear probing, backward-shift deletion (no tombstones), power-of-two
// capacity, max load 3/4. It never shrinks.
template <class V, MemCategory C>
class FlatHashMap {
public:
    static constexpr uint64_t kEmpty = ~0ULL;

    FlatHashMap() { rehash(16); }

    size_t size() const { return count; }
    bool empty() const { return count == 0; }

    V* find(uint64_t key) {
        size_t i = find_slot(key);
        return i == kNotFound ? nullptr : &slots[i].value;
    }
    const V* find(uint64_t key) const {
        size_t i = find_slot(key);
        return i == kNotFound ? nullptr : &slots[i].value;
    }
    bool contains(uint64_t key) const { return find(key) != nullptr; }

    // Inserts key -> value, or overwrites an existing value. Returns true if the key was new.
    bool insert_or_assign(uint64_t key, V value) {
        if ((count + 1) * 4 > slots.size() * 3) rehash(slots.size() * 2);
        for (size_t i = home(key);; i = (i + 1) & mask) {
            if (slots[i].key == key) {
                slots[i].value = value;
                return false;
            }
            if (slots[i].key == kEmpty) {
                slots[i] = {key, value};
                ++count;
                return true;
            }
        }
    }

    bool erase(uint64_t key) {
        size_t i = home(key);
        while (slots[i].key != key) {
            if (slots[i].key == kEmpty) return false;
            i = (i + 1) & mask;
        }
        // Backward shift: pull later entries of the probe run into the hole whenever the
        // hole lies between an entry's home slot and its current slot.
        for (size_t j = (i + 1) & mask; slots[j].key != kEmpty; j = (j + 1) & mask) {
            size_t k = home(slots[j].key);
            bool k_in_hole_to_j = (i <= j) ? (i < k && k <= j) : (i < k || k <= j);
            if (!k_in_hole_to_j) {
                slots[i] = slots[j];
                i = j;
            }
        }
        slots[i].key = kEmpty;
        --count;
        return true;
    }

    // Calls f(key, value) for every entry, in slot order.
    template <class F>
    void for_each(F&& f) const {
        for (const auto& s : slots)
            if (s.key != kEmpty) f(s.key, s.value);
    }

private:
    struct Slot {
        uint64_t key;
        V value;
    };

    std::vector<Slot, Alloc<Slot, C>> slots;
    size_t mask = 0;
    size_t count = 0;

    static constexpr size_t kNotFound = ~size_t{0};

    size_t home(uint64_t key) const { return static_cast<size_t>(mix64(key)) & mask; }

    size_t find_slot(uint64_t key) const {
        for (size_t i = home(key);; i = (i + 1) & mask) {
            if (slots[i].key == key) return i;
            if (slots[i].key == kEmpty) return kNotFound;
        }
    }

    void rehash(size_t capacity) {
        std::vector<Slot, Alloc<Slot, C>> old(capacity, Slot{kEmpty, V{}});
        old.swap(slots);
        mask = capacity - 1;
        count = 0;
        for (const auto& s : old)
            if (s.key != kEmpty) insert_or_assign(s.key, s.value);
    }
};

}  // namespace dc
