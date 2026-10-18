#pragma once
#include "DNDTree.hpp"
#include "DynamicGraphETT.hpp"
#include "FlatHashMap.hpp"
#include "OrderedSets.hpp"
#include "Random.hpp"
#include "Trace.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Phase 6 workload generators. Definitions follow docs/PREREGISTRATION.md §2.9 and
// amendments A1-A3. The format and parameters are documented in docs/TRACE_FORMAT.md.
//
// Determinism: all randomness comes from dc::SplitMix64 streams derived from the trace seed
// (one stream per purpose: graph, op mix, updates, queries). Floating-point use (RGG) is
// plain IEEE arithmetic without transcendental functions, compiled with -ffp-contract=off.
// Traces are bit-identical across compilers.

namespace dc::wl {

struct Edge {
    int32_t u, v;  // normalized: u < v
    bool operator==(const Edge&) const = default;
};

inline Edge norm(int32_t a, int32_t b) { return a < b ? Edge{a, b} : Edge{b, a}; }

enum class Stream : uint64_t { Graph = 1, Mix = 2, Update = 3, Query = 4 };

// An independent stream per purpose, so scenarios that differ only in their queries share
// the identical op-type and update sequences (pre-registration §2.3).
inline SplitMix64 stream(uint64_t seed, Stream s) {
    SplitMix64 mixer(seed ^ (static_cast<uint64_t>(s) * 0xd1b54a32d192ed03ULL));
    return SplitMix64(mixer.next());
}

// Present edges with O(1) insert, erase and uniform pick.
class EdgeSet {
public:
    size_t size() const { return edges.size(); }
    bool empty() const { return edges.empty(); }
    bool contains(Edge e) const { return pos.contains(edge_key(e.u, e.v)); }

    bool insert(Edge e) {
        uint64_t k = edge_key(e.u, e.v);
        if (pos.contains(k)) return false;
        pos.insert_or_assign(k, static_cast<uint32_t>(edges.size()));
        edges.push_back(e);
        return true;
    }

    bool erase(Edge e) {
        uint64_t k = edge_key(e.u, e.v);
        const uint32_t* p = pos.find(k);
        if (!p) return false;
        uint32_t i = *p;
        pos.erase(k);
        if (i + 1 != edges.size()) {
            edges[i] = edges.back();
            pos.insert_or_assign(edge_key(edges[i].u, edges[i].v), i);
        }
        edges.pop_back();
        return true;
    }

    Edge pick(SplitMix64& rng) const { return edges[rng.below(edges.size())]; }
    const std::vector<Edge>& all() const { return edges; }

private:
    std::vector<Edge> edges;
    FlatHashMap<uint32_t, MemCategory::Scratch> pos;
};

// ---------------------------------------------------------------- graph builders

// G(n, m): m distinct uniform edges, in generation order. Requires m <= n(n-1)/4, so that
// rejection sampling stays cheap.
inline std::vector<Edge> gnm(int n, int64_t m, SplitMix64& rng) {
    if (n < 2 || 4 * m > static_cast<int64_t>(n) * (n - 1)) throw std::invalid_argument("gnm: m too large for n");
    EdgeSet set;
    std::vector<Edge> out;
    out.reserve(m);
    while (static_cast<int64_t>(out.size()) < m) {
        auto a = static_cast<int32_t>(rng.below(n));
        auto b = static_cast<int32_t>(rng.below(n - 1));
        if (b >= a) ++b;
        Edge e = norm(a, b);
        if (set.insert(e)) out.push_back(e);
    }
    return out;
}

inline std::vector<Edge> path_graph(int n) {
    std::vector<Edge> out;
    for (int i = 0; i + 1 < n; ++i) out.push_back({i, i + 1});
    return out;
}

inline std::vector<Edge> star_graph(int n) {
    std::vector<Edge> out;
    for (int i = 1; i < n; ++i) out.push_back({0, i});
    return out;
}

inline std::vector<Edge> complete_graph(int n) {
    std::vector<Edge> out;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) out.push_back({i, j});
    return out;
}

// Barabasi-Albert: a seed clique on k + 1 vertices; each later vertex attaches to k distinct
// earlier vertices chosen with probability proportional to degree.
inline std::vector<Edge> barabasi_albert(int n, int k, SplitMix64& rng) {
    if (k < 1 || n <= k + 1) throw std::invalid_argument("barabasi_albert: need n > k + 1 >= 2");
    std::vector<Edge> out = complete_graph(k + 1);
    std::vector<int32_t> endpoints;  // each vertex listed once per incident edge
    for (const Edge& e : out) {
        endpoints.push_back(e.u);
        endpoints.push_back(e.v);
    }
    std::vector<int32_t> chosen;
    for (int32_t t = k + 1; t < n; ++t) {
        chosen.clear();
        while (static_cast<int>(chosen.size()) < k) {
            int32_t c = endpoints[rng.below(endpoints.size())];
            if (std::find(chosen.begin(), chosen.end(), c) == chosen.end()) chosen.push_back(c);
        }
        for (int32_t c : chosen) {
            out.push_back(norm(c, t));
            endpoints.push_back(c);
            endpoints.push_back(t);
        }
    }
    return out;
}

// Random geometric graph on the unit torus: V uniform points; potential edges are the pairs
// at distance <= r, with r chosen so that the expected degree is `expected_degree`. Returned
// sorted by (u, v).
inline std::vector<Edge> rgg_potential(int n, double expected_degree, SplitMix64& rng) {
    std::vector<double> x(n), y(n);
    for (int i = 0; i < n; ++i) {
        x[i] = rng.uniform01();
        y[i] = rng.uniform01();
    }
    const double pi = 3.14159265358979323846;
    const double r2 = expected_degree / ((n - 1) * pi);
    const double r = std::sqrt(r2);
    auto close = [&](int i, int j) {
        double dx = std::fabs(x[i] - x[j]), dy = std::fabs(y[i] - y[j]);
        dx = std::min(dx, 1.0 - dx);
        dy = std::min(dy, 1.0 - dy);
        return dx * dx + dy * dy <= r2;
    };

    std::vector<Edge> out;
    const int g = std::max(1, static_cast<int>(1.0 / r));  // grid cells at least r wide
    if (g < 3) {  // tiny graphs: all pairs
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
                if (close(i, j)) out.push_back({i, j});
    } else {
        std::vector<std::vector<int32_t>> cell(static_cast<size_t>(g) * g);
        auto cell_of = [&](double c) { return std::min(g - 1, static_cast<int>(c * g)); };
        for (int i = 0; i < n; ++i) cell[static_cast<size_t>(cell_of(x[i])) * g + cell_of(y[i])].push_back(i);
        for (int cx = 0; cx < g; ++cx) {
            for (int cy = 0; cy < g; ++cy) {
                for (int i : cell[static_cast<size_t>(cx) * g + cy]) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        for (int dy = -1; dy <= 1; ++dy) {
                            int nx = (cx + dx + g) % g, ny = (cy + dy + g) % g;
                            for (int j : cell[static_cast<size_t>(nx) * g + ny])
                                if (i < j && close(i, j)) out.push_back({i, j});
                        }
                    }
                }
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const Edge& a, const Edge& b) { return a.u != b.u ? a.u < b.u : a.v < b.v; });
    return out;
}

// k edges sampled uniformly without replacement from pool (partial Fisher-Yates), in
// sampled order.
inline std::vector<Edge> sample_subset(std::vector<Edge> pool, size_t k, SplitMix64& rng) {
    if (k > pool.size()) throw std::invalid_argument("sample_subset: k > pool");
    for (size_t i = 0; i < k; ++i) std::swap(pool[i], pool[i + rng.below(pool.size() - i)]);
    pool.resize(k);
    return pool;
}

// ---------------------------------------------------------------- ground truth

// Answers queries during generation with two independent structures that must agree, and
// keeps adjacency for local-query sampling.
class GroundTruth {
public:
    explicit GroundTruth(int n) : dnd(n), ett(n), adj(n), stamp(n, 0) {}

    void insert(Edge e) {
        dnd.insert_edge(e.u, e.v);
        ett.insert_edge(e.u, e.v);
        adj.insert(e.u, e.v);
        adj.insert(e.v, e.u);
    }
    void erase(Edge e) {
        dnd.delete_edge(e.u, e.v);
        ett.delete_edge(e.u, e.v);
        adj.erase(e.u, e.v);
        adj.erase(e.v, e.u);
    }
    bool connected(int u, int v) {
        bool a = dnd.is_connected(u, v);
        if (a != ett.is_connected(u, v)) throw std::logic_error("ground truth disagreement (DND vs ETT)");
        return a;
    }
    size_t degree(int u) const { return adj.size(u); }

    // Vertices at distance 1..h from u, in BFS order.
    const std::vector<int32_t>& ball(int u, int h) {
        if (++epoch == 0) {
            std::fill(stamp.begin(), stamp.end(), 0);
            epoch = 1;
        }
        found.clear();
        frontier.assign(1, u);
        stamp[u] = epoch;
        for (int d = 0; d < h; ++d) {
            next.clear();
            for (int x : frontier)
                for (int y : adj.items(x))
                    if (stamp[y] != epoch) {
                        stamp[y] = epoch;
                        next.push_back(y);
                        found.push_back(y);
                    }
            frontier.swap(next);
        }
        return found;
    }

private:
    DNDTree dnd;
    EttF2 ett;
    FlatOrderedSets<MemCategory::Scratch> adj;
    std::vector<uint32_t> stamp;
    uint32_t epoch = 0;
    std::vector<int32_t> frontier, next, found;
};

enum class QueryKind { Uniform, Local, Cross };

inline const char* to_string(QueryKind k) {
    switch (k) {
        case QueryKind::Uniform: return "uniform";
        case QueryKind::Local: return "local";
        case QueryKind::Cross: return "cross";
    }
    return "?";
}

inline TraceRecord make_query(QueryKind kind, int n, int local_h, SplitMix64& rng, GroundTruth& gt) {
    auto uniform_pair = [&](int32_t& u, int32_t& v) {
        u = static_cast<int32_t>(rng.below(n));
        v = static_cast<int32_t>(rng.below(n - 1));
        if (v >= u) ++v;
    };
    TraceRecord q;
    q.op = TraceOp::Query;
    int32_t u = 0, v = 0;
    bool found = kind == QueryKind::Uniform;
    if (kind == QueryKind::Uniform) {
        uniform_pair(u, v);
    } else if (kind == QueryKind::Local) {
        for (int attempt = 0; attempt < 64 && !found; ++attempt) {
            u = static_cast<int32_t>(rng.below(n));
            if (gt.degree(u) == 0) continue;
            const auto& b = gt.ball(u, local_h);
            v = b[rng.below(b.size())];
            found = true;
        }
    } else {
        for (int attempt = 0; attempt < 64 && !found; ++attempt) {
            u = static_cast<int32_t>(rng.below(n));
            for (int tries = 0; tries < 4096; ++tries) {
                v = static_cast<int32_t>(rng.below(n - 1));
                if (v >= u) ++v;
                if (!gt.connected(u, v)) {
                    found = true;
                    break;
                }
            }
        }
    }
    if (!found) {
        uniform_pair(u, v);
        q.flags = kQueryFallback;
    }
    q.u = static_cast<uint32_t>(u);
    q.v = static_cast<uint32_t>(v);
    q.answer = gt.connected(u, v) ? 1 : 0;
    if (!(q.flags & kQueryFallback)) {
        if (kind == QueryKind::Local && !q.answer) throw std::logic_error("local query not connected");
        if (kind == QueryKind::Cross && q.answer) throw std::logic_error("cross query connected");
    }
    return q;
}

inline TraceRecord update_record(TraceOp op, Edge e, SplitMix64& rng) {
    TraceRecord r;
    r.op = op;
    bool flip = rng.next() & 1;  // random orientation exercises (u, v) normalization
    r.u = static_cast<uint32_t>(flip ? e.v : e.u);
    r.v = static_cast<uint32_t>(flip ? e.u : e.v);
    return r;
}

// ---------------------------------------------------------------- streams

struct ChurnSpec {
    double p_query = 0.34, p_insert = 0.33, p_delete = 0.33;
    int64_t ops = 1'000'000;
    QueryKind query = QueryKind::Uniform;
    int local_h = 2;
};

// Steady-state churn (§2.9): the initial graph is the build phase; then `ops` measured
// operations at the given mix, with the edge count held at the initial count. Deletes pick a
// uniform present edge; inserts pick a uniform absent potential edge (any pair if `potential`
// is null, else from that list).
inline Trace churn_trace(std::string meta, uint64_t seed, int n, const std::vector<Edge>& initial,
                         const std::vector<Edge>* potential, const ChurnSpec& spec) {
    if (n < 2) throw std::invalid_argument("churn_trace: n < 2");
    Trace t;
    t.num_vertices = static_cast<uint32_t>(n);
    t.seed = seed;
    t.meta = std::move(meta);
    GroundTruth gt(n);
    EdgeSet present, absent;
    SplitMix64 mix = stream(seed, Stream::Mix), upd = stream(seed, Stream::Update), qry = stream(seed, Stream::Query);

    for (const Edge& e : initial) {
        if (!present.insert(e)) throw std::invalid_argument("churn_trace: duplicate initial edge");
        gt.insert(e);
        t.build.push_back(update_record(TraceOp::Insert, e, upd));
    }
    if (potential)
        for (const Edge& e : *potential)
            if (!present.contains(e)) absent.insert(e);
    const auto target = static_cast<int64_t>(initial.size());

    t.measured.reserve(spec.ops);
    for (int64_t i = 0; i < spec.ops; ++i) {
        double roll = mix.uniform01();
        TraceOp op = roll < spec.p_query ? TraceOp::Query
                     : roll < spec.p_query + spec.p_insert ? TraceOp::Insert
                                                           : TraceOp::Delete;
        if (op != TraceOp::Query) {
            auto e = static_cast<int64_t>(present.size());
            if (e < target) op = TraceOp::Insert;
            else if (e > target) op = TraceOp::Delete;
            if (op == TraceOp::Delete && present.empty()) op = TraceOp::Insert;
        }

        if (op == TraceOp::Insert) {
            bool ok = false;
            Edge e{};
            if (potential) {
                if (!absent.empty()) {
                    e = absent.pick(upd);
                    absent.erase(e);
                    ok = true;
                }
            } else {
                for (int attempt = 0; attempt < 64 && !ok; ++attempt) {
                    auto a = static_cast<int32_t>(upd.below(n));
                    auto b = static_cast<int32_t>(upd.below(n - 1));
                    if (b >= a) ++b;
                    e = norm(a, b);
                    ok = !present.contains(e);
                }
            }
            if (ok) {
                present.insert(e);
                gt.insert(e);
                t.measured.push_back(update_record(TraceOp::Insert, e, upd));
                continue;
            }
            op = present.empty() ? TraceOp::Query : TraceOp::Delete;  // no absent edge
        }
        if (op == TraceOp::Delete) {
            Edge e = present.pick(upd);
            present.erase(e);
            if (potential) absent.insert(e);
            gt.erase(e);
            t.measured.push_back(update_record(TraceOp::Delete, e, upd));
            continue;
        }
        t.measured.push_back(make_query(spec.query, n, spec.local_h, qry, gt));
    }
    return t;
}

// SIGMOD 2025's u_r workload (amendment A2): start empty, insert `order` (deduplicated,
// self-loops dropped) in the given order, and after every u_r inserts delete a uniform
// present edge. Optionally, uniform query blocks at evenly spaced testing points (their
// checkpoint protocol). All operations are measured; there is no build phase.
inline Trace ur_trace(std::string meta, uint64_t seed, int n, const std::vector<Edge>& order, int64_t ur,
                      int checkpoints, int queries_per_checkpoint) {
    Trace t;
    t.num_vertices = static_cast<uint32_t>(n);
    t.seed = seed;
    t.meta = std::move(meta);
    GroundTruth gt(n);
    EdgeSet seen, present;
    SplitMix64 upd = stream(seed, Stream::Update), qry = stream(seed, Stream::Query);

    std::vector<Edge> inserts;
    for (const Edge& e : order)
        if (e.u != e.v && seen.insert(e)) inserts.push_back(e);
    const int64_t m = static_cast<int64_t>(inserts.size());
    const int64_t total_updates = m + m / ur;
    int next_point = 1;
    int64_t updates_done = 0;
    auto after_update = [&] {
        ++updates_done;
        while (checkpoints > 0 && next_point <= checkpoints && updates_done == next_point * total_updates / checkpoints) {
            for (int q = 0; q < queries_per_checkpoint; ++q) t.measured.push_back(make_query(QueryKind::Uniform, n, 2, qry, gt));
            ++next_point;
        }
    };

    int64_t since_delete = 0;
    for (const Edge& e : inserts) {
        present.insert(e);
        gt.insert(e);
        t.measured.push_back(update_record(TraceOp::Insert, e, upd));
        after_update();
        if (++since_delete == ur) {
            since_delete = 0;
            Edge d = present.pick(upd);
            present.erase(d);
            gt.erase(d);
            t.measured.push_back(update_record(TraceOp::Delete, d, upd));
            after_update();
        }
    }
    return t;
}

struct TemporalEvent {
    int64_t time;
    int32_t u, v;
};

// Sliding window over a temporal edge stream (events sorted by time): an edge is inserted
// when it first appears, its expiry is pushed to time + window on every reappearance, and it
// is deleted at expiry. After each update, queries follow with probability q each
// (geometric), so queries make up a fraction q of all operations. Uniform queries.
inline Trace window_trace(std::string meta, uint64_t seed, int n, const std::vector<TemporalEvent>& events,
                          int64_t window, double q) {
    Trace t;
    t.num_vertices = static_cast<uint32_t>(n);
    t.seed = seed;
    t.meta = std::move(meta);
    GroundTruth gt(n);
    SplitMix64 upd = stream(seed, Stream::Update), qry = stream(seed, Stream::Query), mix = stream(seed, Stream::Mix);
    FlatHashMap<int64_t, MemCategory::Scratch> expiry;  // edge -> current expiry time
    using Item = std::pair<int64_t, uint64_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> heap;

    auto queries = [&] {
        while (mix.uniform01() < q) t.measured.push_back(make_query(QueryKind::Uniform, n, 2, qry, gt));
    };
    auto expire_until = [&](int64_t now) {
        while (!heap.empty() && heap.top().first <= now) {
            auto [when, key] = heap.top();
            heap.pop();
            const int64_t* cur = expiry.find(key);
            if (!cur || *cur != when) continue;  // stale: the edge was extended
            expiry.erase(key);
            Edge e{key_first(key), key_second(key)};
            gt.erase(e);
            t.measured.push_back(update_record(TraceOp::Delete, e, upd));
            queries();
        }
    };

    for (const TemporalEvent& ev : events) {
        if (ev.u == ev.v) continue;
        expire_until(ev.time);
        Edge e = norm(ev.u, ev.v);
        uint64_t key = edge_key(e.u, e.v);
        bool present = expiry.contains(key);
        expiry.insert_or_assign(key, ev.time + window);
        heap.push({ev.time + window, key});
        if (!present) {
            gt.insert(e);
            t.measured.push_back(update_record(TraceOp::Insert, e, upd));
            queries();
        }
    }
    return t;
}

// Adversarial (plan 6.2): a clique on s vertices joined by one bridge to a larger connected
// part (a path plus random chords). The stream alternates deleting and reinserting the
// bridge, with queries at fraction p_query. Every bridge delete is a tree-edge cut whose
// smaller side holds ~s^2/2 non-tree edges and no replacement: the worst case for
// smaller-side scanning.
inline Trace adversarial_trace(std::string meta, uint64_t seed, int n, int s, int64_t ops, double p_query) {
    if (s < 2 || 2 * s >= n) throw std::invalid_argument("adversarial_trace: need 2 <= s < n/2");
    SplitMix64 g = stream(seed, Stream::Graph);
    std::vector<Edge> initial = complete_graph(s);
    const int rest = n - s;
    for (int i = s; i + 1 < n; ++i) initial.push_back({i, i + 1});
    for (const Edge& e : gnm(rest, rest, g)) {  // chords: average extra degree 2
        Edge c{e.u + s, e.v + s};
        if (c.v != c.u + 1) initial.push_back(c);
    }
    const Edge bridge{0, s};
    initial.push_back(bridge);

    Trace t;
    t.num_vertices = static_cast<uint32_t>(n);
    t.seed = seed;
    t.meta = std::move(meta);
    GroundTruth gt(n);
    SplitMix64 mix = stream(seed, Stream::Mix), upd = stream(seed, Stream::Update), qry = stream(seed, Stream::Query);
    for (const Edge& e : initial) {
        gt.insert(e);
        t.build.push_back(update_record(TraceOp::Insert, e, upd));
    }
    bool bridge_present = true;
    for (int64_t i = 0; i < ops; ++i) {
        if (mix.uniform01() < p_query) {
            t.measured.push_back(make_query(QueryKind::Uniform, n, 2, qry, gt));
            continue;
        }
        if (bridge_present) gt.erase(bridge);
        else gt.insert(bridge);
        t.measured.push_back(update_record(bridge_present ? TraceOp::Delete : TraceOp::Insert, bridge, upd));
        bridge_present = !bridge_present;
    }
    return t;
}

}  // namespace dc::wl
