#include "FlatHashMap.hpp"
#include "OrderedSets.hpp"
#include "Random.hpp"
#include "TestFramework.hpp"
#include <algorithm>
#include <unordered_map>
#include <vector>

// The shared F1/F2 containers, checked against standard-library references under random ops.

namespace {

using dc::MemCategory;

// Tests a FlatHashMap with keys drawn from a small range, so inserts, overwrites and
// erases collide often (long probe runs exercise the backward-shift deletion).
void hash_map_matches_reference(uint64_t seed, uint64_t key_range, int ops) {
    dc::SplitMix64 rng(seed);
    dc::FlatHashMap<uint32_t, MemCategory::Scratch> map;
    std::unordered_map<uint64_t, uint32_t> ref;
    for (int i = 0; i < ops; ++i) {
        uint64_t key = dc::edge_key(static_cast<int32_t>(rng.below(key_range)), static_cast<int32_t>(rng.below(key_range)));
        uint64_t action = rng.below(3);
        if (action == 0) {
            auto value = static_cast<uint32_t>(rng.next());
            bool is_new = map.insert_or_assign(key, value);
            CHECK(is_new == !ref.contains(key));
            ref[key] = value;
        } else if (action == 1) {
            CHECK(map.erase(key) == (ref.erase(key) > 0));
        } else {
            const uint32_t* v = map.find(key);
            auto it = ref.find(key);
            CHECK((v != nullptr) == (it != ref.end()));
            if (v) CHECK(*v == it->second);
        }
        CHECK(map.size() == ref.size());
    }
    size_t seen = 0;
    map.for_each([&](uint64_t k, uint32_t v) {
        ++seen;
        CHECK(ref.contains(k) && ref[k] == v);
    });
    CHECK(seen == ref.size());
}

// Std and Flat ordered sets must hold the same elements in the same order after every op:
// that order decides which replacement edge an algorithm finds first.
void ordered_sets_agree(uint64_t seed, int n, int ops) {
    dc::SplitMix64 rng(seed);
    dc::StdOrderedSets<MemCategory::Scratch> std_sets(n);
    dc::FlatOrderedSets<MemCategory::Scratch> flat_sets(n);
    std::vector<std::vector<int32_t>> ref(n);  // swap-remove reference
    for (int i = 0; i < ops; ++i) {
        auto x = static_cast<int32_t>(rng.below(n));
        auto y = static_cast<int32_t>(rng.below(n * 4));
        auto& r = ref[x];
        auto it = std::find(r.begin(), r.end(), y);
        if (rng.below(5) < 3) {  // insert-biased, so sets grow and blocks relocate
            bool is_new = it == r.end();
            if (is_new) r.push_back(y);
            CHECK(std_sets.insert(x, y) == is_new);
            CHECK(flat_sets.insert(x, y) == is_new);
        } else {
            bool present = it != r.end();
            if (present) {
                *it = r.back();
                r.pop_back();
            }
            CHECK(std_sets.erase(x, y) == present);
            CHECK(flat_sets.erase(x, y) == present);
        }
        auto a = std_sets.items(x);
        auto b = flat_sets.items(x);
        CHECK(std::equal(a.begin(), a.end(), r.begin(), r.end()));
        CHECK(std::equal(b.begin(), b.end(), r.begin(), r.end()));
        CHECK(std_sets.contains(x, y) == flat_sets.contains(x, y));
    }
    size_t total = 0;
    for (int x = 0; x < n; ++x) {
        total += ref[x].size();
        CHECK(std_sets.size(x) == ref[x].size() && flat_sets.size(x) == ref[x].size());
    }
    CHECK(std_sets.total() == total && flat_sets.total() == total);
}

}  // namespace

TEST_CASE("flat_hash_map_matches_reference") {
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        hash_map_matches_reference(seed, 8, 5000);      // dense collisions
        hash_map_matches_reference(seed, 300, 20000);   // growth and rehash
    }
}

TEST_CASE("ordered_sets_identical_order") {
    for (uint64_t seed = 1; seed <= 20; ++seed) {
        ordered_sets_agree(seed, 4, 3000);    // few large sets: many block relocations
        ordered_sets_agree(seed, 200, 20000);
    }
}
