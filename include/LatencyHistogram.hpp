#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>

// Log-bucketed latency histogram (HDR-style, plan 7.2). Values below 128 are exact. Above
// that, each power of two is split into 64 buckets, so a reported percentile is within 1/64
// (about 1.6%) of the true value. Fixed size (~30 KB) regardless of the sample count.

namespace dc {

class LatencyHistogram {
public:
    static constexpr int kExact = 128;
    static constexpr int kSub = 64;
    static constexpr int kBuckets = kExact + (64 - 7) * kSub;

    void add(uint64_t x) {
        ++counts[bucket_of(x)];
        ++n;
        sum += x;
        max_seen = std::max(max_seen, x);
    }

    uint64_t count() const { return n; }
    uint64_t max() const { return max_seen; }
    double mean() const { return n ? static_cast<double>(sum) / static_cast<double>(n) : 0.0; }

    // Value at quantile q in [0, 1]: the midpoint of the bucket holding the
    // ceil(q * count)-th smallest sample (the max for q = 1).
    double quantile(double q) const {
        if (n == 0) return 0.0;
        if (q >= 1.0) return static_cast<double>(max_seen);
        auto rank = static_cast<uint64_t>(q * static_cast<double>(n));
        if (rank < 1) rank = 1;
        uint64_t seen = 0;
        for (int b = 0; b < kBuckets; ++b) {
            seen += counts[b];
            if (seen >= rank) {
                double lo = static_cast<double>(lower_bound(b));
                double hi = static_cast<double>(lower_bound(b + 1));
                return std::min((lo + hi) / 2.0, static_cast<double>(max_seen));
            }
        }
        return static_cast<double>(max_seen);
    }

    static int bucket_of(uint64_t x) {
        if (x < kExact) return static_cast<int>(x);
        int e = 63 - std::countl_zero(x);                          // e >= 7
        auto m = static_cast<int>((x >> (e - 6)) & (kSub - 1));  // next 6 bits after the leading 1
        return kExact + (e - 7) * kSub + m;
    }

    static uint64_t lower_bound(int b) {
        if (b < kExact) return static_cast<uint64_t>(b);
        int e = (b - kExact) / kSub + 7;
        int m = (b - kExact) % kSub;
        return static_cast<uint64_t>(kSub + m) << (e - 6);
    }

private:
    std::array<uint64_t, kBuckets> counts{};
    uint64_t n = 0;
    uint64_t sum = 0;
    uint64_t max_seen = 0;
};

}  // namespace dc
