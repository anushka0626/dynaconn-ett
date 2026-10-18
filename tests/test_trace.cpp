#include "LeoConstellation.hpp"
#include "Oracle.hpp"
#include "Sha256.hpp"
#include "TestFramework.hpp"
#include "Trace.hpp"
#include "WorkloadInputs.hpp"
#include "Workloads.hpp"
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

// Trace format and workload generators (Phase 6).

namespace {

using namespace dc;
using namespace dc::wl;

std::string sha(const std::string& s) {
    Sha256 h;
    h.update(s.data(), s.size());
    return h.hex_digest();
}

// Replays a trace against the independent Oracle. Checks that every update is valid (deletes
// hit present edges, inserts absent ones, no self-loops) and every stored answer is right.
// Returns the oracle's final edge count.
size_t replay_and_check(const Trace& t) {
    Oracle o(static_cast<int>(t.num_vertices));
    std::set<std::pair<uint32_t, uint32_t>> present;
    for (const auto* part : {&t.build, &t.measured}) {
        for (const TraceRecord& r : *part) {
            auto e = std::pair{std::min(r.u, r.v), std::max(r.u, r.v)};
            if (r.op == TraceOp::Insert) {
                CHECK(r.u != r.v);
                CHECK_MSG(present.insert(e).second, "insert of present edge");
                o.insert_edge(r.u, r.v);
            } else if (r.op == TraceOp::Delete) {
                CHECK_MSG(present.erase(e) == 1, "delete of absent edge");
                o.delete_edge(r.u, r.v);
            } else {
                CHECK(r.u != r.v);
                CHECK_MSG(o.is_connected(r.u, r.v) == (r.answer == 1), "stored answer wrong");
            }
        }
    }
    return present.size();
}

std::vector<TraceRecord> updates_only(const std::vector<TraceRecord>& recs) {
    std::vector<TraceRecord> out;
    for (const auto& r : recs)
        if (r.op != TraceOp::Query) out.push_back(r);
    return out;
}

ChurnSpec spec(double pq, double pi, double pd, int64_t ops, QueryKind k) {
    ChurnSpec s;
    s.p_query = pq;
    s.p_insert = pi;
    s.p_delete = pd;
    s.ops = ops;
    s.query = k;
    return s;
}

}  // namespace

TEST_CASE("sha256_nist_vectors") {
    CHECK(sha("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(sha(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    // Incremental updates across block boundaries give the same digest.
    Sha256 h;
    std::string msg(1000, 'x');
    for (size_t i = 0; i < msg.size(); i += 7) h.update(msg.data() + i, std::min<size_t>(7, msg.size() - i));
    CHECK(h.hex_digest() == sha(msg));
}

TEST_CASE("trace_roundtrip_and_validation") {
    SplitMix64 g = stream(5, Stream::Graph);
    Trace t = churn_trace("gen=test", 5, 50, gnm(50, 100, g), nullptr, spec(0.3, 0.35, 0.35, 2000, QueryKind::Uniform));
    std::string bytes = serialize_trace(t);
    Trace back = parse_trace(bytes);
    CHECK(back.num_vertices == t.num_vertices && back.seed == t.seed && back.meta == t.meta);
    CHECK(back.build == t.build && back.measured == t.measured);
    CHECK(serialize_trace(back) == bytes);

    bool threw = false;
    try {
        std::string bad = bytes;
        bad[0] = 'X';
        parse_trace(bad);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
    threw = false;
    try {
        parse_trace(bytes.substr(0, bytes.size() - 1));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("graph_builders") {
    SplitMix64 g(1);
    auto er = gnm(200, 800, g);
    CHECK(er.size() == 800);
    std::set<std::pair<int, int>> s;
    for (auto e : er) {
        CHECK(e.u < e.v && e.u >= 0 && e.v < 200);
        CHECK(s.insert({e.u, e.v}).second);
    }
    CHECK(path_graph(10).size() == 9 && star_graph(10).size() == 9 && complete_graph(10).size() == 45);

    auto ba = barabasi_albert(500, 3, g);
    CHECK(ba.size() == 6 + 3 * (500 - 4));
    s.clear();
    for (auto e : ba) CHECK(e.u < e.v && s.insert({e.u, e.v}).second);

    // RGG: matches a brute-force all-pairs check on the same points (same seed).
    for (int n : {50, 400}) {
        SplitMix64 a(9), b(9);
        auto pot = rgg_potential(n, 8.0, a);
        std::vector<double> x(n), y(n);
        for (int i = 0; i < n; ++i) {
            x[i] = b.uniform01();
            y[i] = b.uniform01();
        }
        double r2 = 8.0 / ((n - 1) * 3.14159265358979323846);
        std::vector<Edge> brute;
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j) {
                double dx = std::fabs(x[i] - x[j]), dy = std::fabs(y[i] - y[j]);
                dx = std::min(dx, 1.0 - dx);
                dy = std::min(dy, 1.0 - dy);
                if (dx * dx + dy * dy <= r2) brute.push_back({i, j});
            }
        CHECK(pot == brute);
        double avg_deg = 2.0 * pot.size() / n;
        CHECK_MSG(avg_deg > 6.0 && avg_deg < 10.0, "avg degree " << avg_deg);
    }
}

TEST_CASE("churn_trace_semantics") {
    for (QueryKind k : {QueryKind::Uniform, QueryKind::Local, QueryKind::Cross}) {
        for (uint64_t seed = 1; seed <= 5; ++seed) {
            const int n = 300;
            SplitMix64 g = stream(seed, Stream::Graph);
            auto initial = gnm(n, 2 * n, g);
            Trace t = churn_trace("gen=test", seed, n, initial, nullptr, spec(0.34, 0.33, 0.33, 5000, k));
            CHECK(t.build.size() == initial.size() && t.measured.size() == 5000);
            size_t final_edges = replay_and_check(t);
            CHECK(final_edges + 1 >= initial.size() && final_edges <= initial.size() + 1);  // density control

            size_t queries = 0, fallbacks = 0;
            for (const auto& r : t.measured)
                if (r.op == TraceOp::Query) {
                    ++queries;
                    fallbacks += (r.flags & kQueryFallback) != 0;
                    if (k == QueryKind::Cross && !(r.flags & kQueryFallback)) CHECK(r.answer == 0);
                    if (k == QueryKind::Local) CHECK(r.answer == 1);
                }
            CHECK_MSG(queries > 1500 && queries < 1900, "queries " << queries);
            if (k != QueryKind::Cross) CHECK(fallbacks == 0);
        }
    }
}

// Local queries really are within 2 hops (checked by BFS on the replayed graph).
TEST_CASE("local_queries_within_two_hops") {
    const int n = 200;
    SplitMix64 g = stream(3, Stream::Graph);
    Trace t = churn_trace("gen=test", 3, n, gnm(n, 400, g), nullptr, spec(0.5, 0.25, 0.25, 3000, QueryKind::Local));
    std::set<std::pair<int, int>> present;
    auto dist_le2 = [&](int u, int v) {
        for (auto [a, b] : present) {
            if ((a == u && b == v) || (a == v && b == u)) return true;
        }
        for (int w = 0; w < n; ++w) {
            bool uw = present.contains({std::min(u, w), std::max(u, w)});
            bool wv = present.contains({std::min(w, v), std::max(w, v)});
            if (uw && wv) return true;
        }
        return false;
    };
    for (const auto* part : {&t.build, &t.measured})
        for (const auto& r : *part) {
            auto e = std::pair{static_cast<int>(std::min(r.u, r.v)), static_cast<int>(std::max(r.u, r.v))};
            if (r.op == TraceOp::Insert) present.insert(e);
            else if (r.op == TraceOp::Delete) present.erase(e);
            else CHECK_MSG(dist_le2(r.u, r.v), "local query beyond 2 hops");
        }
}

// Scenarios that differ only in query distribution share the identical update stream and
// query positions (pre-registration §2.3: C3/C4 pairing).
TEST_CASE("query_kinds_share_update_stream") {
    const int n = 500;
    SplitMix64 g1 = stream(7, Stream::Graph), g2 = stream(7, Stream::Graph);
    auto init1 = gnm(n, 2 * n, g1), init2 = gnm(n, 2 * n, g2);
    CHECK(init1 == init2);
    Trace a = churn_trace("u", 7, n, init1, nullptr, spec(0.34, 0.33, 0.33, 4000, QueryKind::Uniform));
    Trace b = churn_trace("l", 7, n, init2, nullptr, spec(0.34, 0.33, 0.33, 4000, QueryKind::Local));
    CHECK(a.build == b.build);
    CHECK(updates_only(a.measured) == updates_only(b.measured));
    for (size_t i = 0; i < a.measured.size(); ++i)
        CHECK((a.measured[i].op == TraceOp::Query) == (b.measured[i].op == TraceOp::Query));
}

TEST_CASE("rgg_churn_uses_potential_edges") {
    const int n = 400;
    SplitMix64 g = stream(11, Stream::Graph);
    auto potential = rgg_potential(n, 16.0, g);  // amendment A1: potential degree 2d
    auto initial = sample_subset(potential, static_cast<size_t>(8.0 * n / 2), g);
    Trace t = churn_trace("gen=test", 11, n, initial, &potential, spec(0.34, 0.33, 0.33, 6000, QueryKind::Uniform));
    replay_and_check(t);
    std::set<std::pair<int, int>> pot;
    for (auto e : potential) pot.insert({e.u, e.v});
    size_t distinct_inserted = 0;
    std::set<std::pair<int, int>> inserted;
    for (const auto& r : t.measured)
        if (r.op == TraceOp::Insert) {
            auto e = std::pair{static_cast<int>(std::min(r.u, r.v)), static_cast<int>(std::max(r.u, r.v))};
            CHECK(pot.contains(e));
            distinct_inserted += inserted.insert(e).second;
        }
    CHECK_MSG(distinct_inserted > 500, "churn must not just flip one edge (amendment A1): " << distinct_inserted);
}

TEST_CASE("ur_trace_structure") {
    const int n = 1000;
    auto order = path_graph(n);
    Trace t = ur_trace("gen=test", 2, n, order, 20, 10, 50);
    replay_and_check(t);
    size_t ins = 0, del = 0, qs = 0, since = 0;
    for (const auto& r : t.measured) {
        if (r.op == TraceOp::Insert) {
            ++ins;
            ++since;
        } else if (r.op == TraceOp::Delete) {
            ++del;
            CHECK(since == 20);
            since = 0;
        } else {
            ++qs;
        }
    }
    CHECK(ins == static_cast<size_t>(n - 1) && del == static_cast<size_t>((n - 1) / 20) && qs == 10u * 50);
    CHECK(t.measured.front().op == TraceOp::Insert);  // sequential path order (amendment A2)
    CHECK(std::min(t.measured.front().u, t.measured.front().v) == 0);
}

TEST_CASE("window_and_adversarial_traces") {
    std::vector<TemporalEvent> ev;
    SplitMix64 g(4);
    for (int i = 0; i < 3000; ++i)
        ev.push_back({i, static_cast<int32_t>(g.below(100)), static_cast<int32_t>(g.below(100))});
    Trace w = window_trace("gen=test", 4, 100, ev, 200, 0.3);
    replay_and_check(w);
    size_t q = 0;
    for (const auto& r : w.measured) q += r.op == TraceOp::Query;
    double frac = static_cast<double>(q) / w.measured.size();
    CHECK_MSG(frac > 0.25 && frac < 0.35, "query fraction " << frac);

    Trace a = adversarial_trace("gen=test", 6, 400, 40, 2000, 0.2);
    replay_and_check(a);
    bool expect_delete = true;
    for (const auto& r : a.measured) {
        if (r.op == TraceOp::Query) continue;
        CHECK((r.op == TraceOp::Delete) == expect_delete);
        CHECK(std::min(r.u, r.v) == 0 && std::max(r.u, r.v) == 40);
        expect_delete = !expect_delete;
    }
}

// Pins the exact bytes of one trace of each main kind. If this changes, every
// previously generated trace is invalid: bump kTraceFormatVersion and regenerate the
// manifest instead of editing the constants. Must match under GCC and Clang.
TEST_CASE("trace_golden_hashes") {
    SplitMix64 g = stream(42, Stream::Graph);
    Trace er = churn_trace("golden", 42, 300, gnm(300, 600, g), nullptr, spec(0.34, 0.33, 0.33, 3000, QueryKind::Cross));
    SplitMix64 g2 = stream(42, Stream::Graph);
    auto pot = rgg_potential(300, 16.0, g2);
    auto init = sample_subset(pot, 1200, g2);
    Trace rgg = churn_trace("golden", 42, 300, init, &pot, spec(0.34, 0.33, 0.33, 3000, QueryKind::Local));
    std::string h_er = sha256_hex(serialize_trace(er)), h_rgg = sha256_hex(serialize_trace(rgg));
    CHECK_MSG(h_er == "c83527a86e164d701d7e846b0dbe5cb06894df6c8019857a3b89d1903473f9de", "er=" << h_er);
    CHECK_MSG(h_rgg == "8e6ab10c592e9cd6fdc95a500a9062fef338afcff413c24310ed089b87d328a4", "rgg=" << h_rgg);
}

TEST_CASE("leo_trace_semantics") {
    LeoSpec spec;
    spec.planes = 12;
    spec.sats_per_plane = 10;
    spec.steps = 300;
    spec.dt_s = 20.0;
    spec.p_query = 0.3;
    Trace a = leo_trace("gen=test", 3, spec);
    Trace b = leo_trace("gen=test", 3, spec);
    CHECK(serialize_trace(a) == serialize_trace(b));  // deterministic
    replay_and_check(a);
    CHECK(a.num_vertices == 120);
    // Ring links are in the build phase and never change; cross-plane links do churn.
    LeoModel model(spec);
    std::set<std::pair<int, int>> rings;
    for (auto e : model.ring_edges()) rings.insert({e.u, e.v});
    size_t churn = 0;
    for (const auto& r : a.measured)
        if (r.op != TraceOp::Query) {
            ++churn;
            CHECK(!rings.contains({static_cast<int>(std::min(r.u, r.v)), static_cast<int>(std::max(r.u, r.v))}));
        }
    CHECK_MSG(churn > 50, "cross-plane links must switch over an orbit: " << churn);
    // Polar cut-off: at 53 deg inclination and a 45 deg cut, some links are down at t = 0.
    auto up = model.cross_links_up(0.0);
    size_t n_up = 0;
    for (auto x : up) n_up += x;
    CHECK(n_up > 0 && n_up < up.size());
}

TEST_CASE("dataset_loaders") {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "dc_test_loaders";
    fs::create_directories(dir);
    {
        std::ofstream f(dir / "g.txt");
        f << "# comment\n10 20\n20 30\n20 10\n30 30\n40\t10\n";
    }
    LoadedGraph g = read_edge_list((dir / "g.txt").string());
    CHECK(g.n == 4);
    CHECK((g.edges == std::vector<Edge>{{0, 1}, {1, 2}, {0, 3}}));  // ids by first appearance; dup + self-loop dropped
    {
        std::ofstream f(dir / "r.gr");
        f << "c road\np sp 3 4\na 1 2 7\na 2 1 7\na 2 3 5\na 3 2 5\n";
    }
    LoadedGraph r = read_dimacs((dir / "r.gr").string());
    CHECK(r.n == 3 && (r.edges == std::vector<Edge>{{0, 1}, {1, 2}}));
    {
        std::ofstream f(dir / "t.txt");
        f << "5 6 300\n6 7 100\n5 6 200\n";
    }
    LoadedTemporal t = read_temporal((dir / "t.txt").string());
    CHECK(t.n == 3 && t.events.size() == 3);
    CHECK(t.events[0].time == 100 && t.events[1].time == 200 && t.events[2].time == 300);
    fs::remove_all(dir);
}
