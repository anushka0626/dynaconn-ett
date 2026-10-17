#pragma once
#include "FlatHashMap.hpp"
#include "MemoryAccounting.hpp"
#include <cstdint>
#include <map>
#include <utility>

// Directed-edge -> value maps for tree-edge lookup. Lookups only: no algorithm decision
// depends on their iteration order, so the two layouts behave identically.
//   find(u, v) -> V* (nullptr if absent)   insert(u, v, value)   erase(u, v) -> bool
//   contains(u, v)   size()   for_each(f(u, v, value))

namespace dc {

// P0/F0: std::map (one heap node per entry).
template <class V>
class StdEdgeMap {
public:
    V* find(int32_t u, int32_t v) {
        auto it = map.find({u, v});
        return it == map.end() ? nullptr : &it->second;
    }
    const V* find(int32_t u, int32_t v) const {
        auto it = map.find({u, v});
        return it == map.end() ? nullptr : &it->second;
    }
    bool contains(int32_t u, int32_t v) const { return map.contains({u, v}); }
    void insert(int32_t u, int32_t v, V value) { map[{u, v}] = value; }
    bool erase(int32_t u, int32_t v) { return map.erase({u, v}) > 0; }
    size_t size() const { return map.size(); }

    template <class F>
    void for_each(F&& f) const {
        for (const auto& [key, value] : map) f(key.first, key.second, value);
    }

private:
    std::map<std::pair<int32_t, int32_t>, V, std::less<>,
             Alloc<std::pair<const std::pair<int32_t, int32_t>, V>, MemCategory::EdgeMap>>
        map;
};

// F1/F2: the shared flat hash map.
template <class V>
class FlatEdgeMap {
public:
    V* find(int32_t u, int32_t v) { return map.find(edge_key(u, v)); }
    const V* find(int32_t u, int32_t v) const { return map.find(edge_key(u, v)); }
    bool contains(int32_t u, int32_t v) const { return map.contains(edge_key(u, v)); }
    void insert(int32_t u, int32_t v, V value) { map.insert_or_assign(edge_key(u, v), value); }
    bool erase(int32_t u, int32_t v) { return map.erase(edge_key(u, v)); }
    size_t size() const { return map.size(); }

    template <class F>
    void for_each(F&& f) const {
        map.for_each([&](uint64_t k, const V& value) { f(key_first(k), key_second(k), value); });
    }

private:
    FlatHashMap<V, MemCategory::EdgeMap> map;
};

}  // namespace dc
