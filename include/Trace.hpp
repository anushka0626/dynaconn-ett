#pragma once
#include "Sha256.hpp"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

// Binary workload traces (docs/TRACE_FORMAT.md). Every structure replays the identical
// bytes, and results record the file's SHA-256. All integers are little-endian.
//
//   offset  size  field
//   0       8     magic "DCTRACE1"
//   8       4     format version (kTraceFormatVersion)
//   12      4     number of vertices
//   16      8     trace seed
//   24      8     number of build records (initial graph, timed separately)
//   32      8     number of measured records
//   40      4     metadata length L
//   44      L     metadata: UTF-8 "key=value" pairs separated by ';' (generator and params)
//   44+L    12 each  records: build records, then measured records
//
// Record: u32 u, u32 v, u8 op (0 insert, 1 delete, 2 query), u8 answer (queries: 1 if
// connected), u16 flags (bit 0: the query is a uniform fallback).

namespace dc {

inline constexpr uint32_t kTraceFormatVersion = 1;
inline constexpr char kTraceMagic[8] = {'D', 'C', 'T', 'R', 'A', 'C', 'E', '1'};

enum class TraceOp : uint8_t { Insert = 0, Delete = 1, Query = 2 };
inline constexpr uint16_t kQueryFallback = 1;

struct TraceRecord {
    uint32_t u = 0;
    uint32_t v = 0;
    TraceOp op = TraceOp::Insert;
    uint8_t answer = 0;
    uint16_t flags = 0;

    bool operator==(const TraceRecord&) const = default;
};

struct Trace {
    uint32_t num_vertices = 0;
    uint64_t seed = 0;
    std::string meta;
    std::vector<TraceRecord> build;
    std::vector<TraceRecord> measured;
};

namespace detail {
inline void put_u16(std::string& s, uint16_t x) {
    for (int i = 0; i < 2; ++i) s += static_cast<char>((x >> (8 * i)) & 0xff);
}
inline void put_u32(std::string& s, uint32_t x) {
    for (int i = 0; i < 4; ++i) s += static_cast<char>((x >> (8 * i)) & 0xff);
}
inline void put_u64(std::string& s, uint64_t x) {
    for (int i = 0; i < 8; ++i) s += static_cast<char>((x >> (8 * i)) & 0xff);
}
inline uint64_t get_le(const unsigned char* p, int bytes) {
    uint64_t x = 0;
    for (int i = 0; i < bytes; ++i) x |= static_cast<uint64_t>(p[i]) << (8 * i);
    return x;
}
}  // namespace detail

// Serializes a trace to its exact on-disk bytes.
inline std::string serialize_trace(const Trace& t) {
    std::string s;
    s.reserve(44 + t.meta.size() + 12 * (t.build.size() + t.measured.size()));
    s.append(kTraceMagic, 8);
    detail::put_u32(s, kTraceFormatVersion);
    detail::put_u32(s, t.num_vertices);
    detail::put_u64(s, t.seed);
    detail::put_u64(s, t.build.size());
    detail::put_u64(s, t.measured.size());
    detail::put_u32(s, static_cast<uint32_t>(t.meta.size()));
    s += t.meta;
    for (const auto* part : {&t.build, &t.measured}) {
        for (const TraceRecord& r : *part) {
            detail::put_u32(s, r.u);
            detail::put_u32(s, r.v);
            s += static_cast<char>(r.op);
            s += static_cast<char>(r.answer);
            detail::put_u16(s, r.flags);
        }
    }
    return s;
}

inline std::string sha256_hex(const std::string& bytes) {
    Sha256 h;
    h.update(bytes.data(), bytes.size());
    return h.hex_digest();
}

// Writes the trace and returns the SHA-256 of the written bytes.
inline std::string write_trace(const std::string& path, const Trace& t) {
    std::string bytes = serialize_trace(t);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("write failed: " + path);
    return sha256_hex(bytes);
}

inline Trace parse_trace(const std::string& bytes) {
    auto fail = [](const char* why) { throw std::runtime_error(std::string("bad trace: ") + why); };
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    if (bytes.size() < 44 || std::memcmp(p, kTraceMagic, 8) != 0) fail("magic");
    if (detail::get_le(p + 8, 4) != kTraceFormatVersion) fail("unsupported format version");
    Trace t;
    t.num_vertices = static_cast<uint32_t>(detail::get_le(p + 12, 4));
    t.seed = detail::get_le(p + 16, 8);
    uint64_t nb = detail::get_le(p + 24, 8), nm = detail::get_le(p + 32, 8);
    uint64_t meta_len = detail::get_le(p + 40, 4);
    if (bytes.size() != 44 + meta_len + 12 * (nb + nm)) fail("length");
    t.meta.assign(bytes.data() + 44, meta_len);
    const unsigned char* r = p + 44 + meta_len;
    auto read_records = [&](uint64_t count, std::vector<TraceRecord>& out) {
        out.resize(count);
        for (uint64_t i = 0; i < count; ++i, r += 12) {
            TraceRecord& rec = out[i];
            rec.u = static_cast<uint32_t>(detail::get_le(r, 4));
            rec.v = static_cast<uint32_t>(detail::get_le(r + 4, 4));
            if (r[8] > 2) fail("op");
            rec.op = static_cast<TraceOp>(r[8]);
            rec.answer = r[9];
            rec.flags = static_cast<uint16_t>(detail::get_le(r + 10, 2));
            if (rec.u >= t.num_vertices || rec.v >= t.num_vertices) fail("vertex out of range");
        }
    };
    read_records(nb, t.build);
    read_records(nm, t.measured);
    return t;
}

inline std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Reads a trace; if sha is non-null, also returns the file's SHA-256.
inline Trace read_trace(const std::string& path, std::string* sha = nullptr) {
    std::string bytes = read_file(path);
    if (sha) *sha = sha256_hex(bytes);
    return parse_trace(bytes);
}

}  // namespace dc
