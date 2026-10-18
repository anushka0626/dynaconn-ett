#pragma once
#include "Workloads.hpp"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// Loaders for external datasets (plan 6.5). Vertex ids are remapped to 0..n-1 in order of
// first appearance, so the same file always gives the same graph.

namespace dc::wl {

class IdMap {
public:
    int32_t operator()(int64_t raw) {
        auto [it, fresh] = ids.emplace(raw, static_cast<int32_t>(ids.size()));
        return it->second;
    }
    int size() const { return static_cast<int>(ids.size()); }

private:
    std::unordered_map<int64_t, int32_t> ids;
};

struct LoadedGraph {
    int n = 0;
    std::vector<Edge> edges;  // file order, normalized; duplicates and self-loops dropped
};

inline LoadedGraph dedupe(int n, const std::vector<Edge>& raw) {
    LoadedGraph g;
    g.n = n;
    EdgeSet seen;
    for (const Edge& e : raw)
        if (e.u != e.v && seen.insert(e)) g.edges.push_back(e);
    return g;
}

// Whitespace- or tab-separated "u v ..." lines; '#' and '%' start comments.
inline LoadedGraph read_edge_list(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read " + path);
    IdMap ids;
    std::vector<Edge> raw;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '%') continue;
        std::istringstream ss(line);
        int64_t a, b;
        if (!(ss >> a >> b)) continue;
        int32_t ia = ids(a);  // separate statements: argument evaluation order is unspecified,
        int32_t ib = ids(b);  // and the ids must not depend on the compiler
        raw.push_back(norm(ia, ib));
    }
    return dedupe(ids.size(), raw);
}

// DIMACS shortest-path format (9th DIMACS challenge road networks): "p sp n m" and
// "a u v w" arc lines, 1-indexed. Each road appears as two arcs; the duplicate is dropped.
inline LoadedGraph read_dimacs(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read " + path);
    int n = 0;
    std::vector<Edge> raw;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        char tag;
        ss >> tag;
        if (tag == 'p') {
            std::string kind;
            int64_t nn, mm;
            ss >> kind >> nn >> mm;
            n = static_cast<int>(nn);
        } else if (tag == 'a') {
            int64_t a, b;
            ss >> a >> b;
            raw.push_back(norm(static_cast<int32_t>(a - 1), static_cast<int32_t>(b - 1)));
        }
    }
    if (n == 0) throw std::runtime_error("DIMACS file without 'p' line: " + path);
    return dedupe(n, raw);
}

struct LoadedTemporal {
    int n = 0;
    std::vector<TemporalEvent> events;  // sorted by time (stable)
};

// SNAP temporal format: "src dst unixtime" per line.
inline LoadedTemporal read_temporal(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot read " + path);
    IdMap ids;
    LoadedTemporal g;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '%') continue;
        std::istringstream ss(line);
        int64_t a, b, t;
        if (!(ss >> a >> b >> t)) continue;
        int32_t ia = ids(a);
        int32_t ib = ids(b);
        g.events.push_back({t, ia, ib});
    }
    std::stable_sort(g.events.begin(), g.events.end(),
                     [](const TemporalEvent& x, const TemporalEvent& y) { return x.time < y.time; });
    g.n = ids.size();
    return g;
}

inline std::string file_sha256(const std::string& path) { return sha256_hex(read_file(path)); }

}  // namespace dc::wl
