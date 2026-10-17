#pragma once
#include <cstdint>

namespace dc {

// SplitMix64 with Lemire's unbiased bounded sampling. Unlike std::uniform_*_distribution,
// whose output is implementation-defined, this produces the same stream on every compiler
// and standard library, so generated workloads are reproducible bit for bit.
class SplitMix64 {
    uint64_t state;

    __extension__ typedef unsigned __int128 u128;

public:
    explicit SplitMix64(uint64_t seed) : state(seed) {}

    uint64_t next() {
        uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    // Uniform integer in [0, range). range must be > 0.
    uint64_t below(uint64_t range) {
        u128 m = static_cast<u128>(next()) * range;
        uint64_t low = static_cast<uint64_t>(m);
        if (low < range) {
            uint64_t threshold = -range % range;
            while (low < threshold) {
                m = static_cast<u128>(next()) * range;
                low = static_cast<uint64_t>(m);
            }
        }
        return static_cast<uint64_t>(m >> 64);
    }

    // Uniform double in [0, 1) with 53 random bits.
    double uniform01() { return static_cast<double>(next() >> 11) * 0x1.0p-53; }
};

}  // namespace dc
