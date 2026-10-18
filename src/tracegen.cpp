// tracegen: generates binary workload traces (docs/TRACE_FORMAT.md).
//
//   tracegen churn       --graph er|rgg|ba --n N --deg D --mix Q,I,D --ops N --query uniform|local|cross --seed S --out F
//   tracegen ur          --graph path|er|edgelist|dimacs [--n N] [--m M] [--input PATH] --ur U
//                        [--checkpoints 100 --qpc 10000] --seed S --out F
//   tracegen window      --input PATH --window W --q Q --seed S --out F
//   tracegen adversarial --n N --clique S --ops N --q Q --seed S --out F
//   tracegen leo         [--planes 72 --sats 22 --phasing 1 --inclination 53 --altitude 550 --lat-cut 45
//                         --range 5000 --dt 10 --steps 600] --q Q --seed S --out F
//   tracegen info F      header, op counts, fraction of true answers, fallbacks, SHA-256
//
// Every parameter is written to the trace's metadata in sorted-key order, so the
// metadata alone specifies how to regenerate the trace. File inputs also record their SHA-256.
#include "LeoConstellation.hpp"
#include "Trace.hpp"
#include "WorkloadInputs.hpp"
#include "Workloads.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace dc;
using namespace dc::wl;

constexpr const char* kTracegenVersion = "1";

struct Args {
    std::string command;
    std::map<std::string, std::string> kv;

    bool has(const std::string& k) const { return kv.contains(k); }
    std::string str(const std::string& k) const {
        auto it = kv.find(k);
        if (it == kv.end()) throw std::invalid_argument("missing --" + k);
        return it->second;
    }
    std::string str(const std::string& k, const std::string& def) const { return has(k) ? kv.at(k) : def; }
    int64_t i64(const std::string& k) const { return std::stoll(str(k)); }
    int64_t i64(const std::string& k, int64_t def) const { return has(k) ? std::stoll(kv.at(k)) : def; }
    double f64(const std::string& k) const { return std::stod(str(k)); }
    double f64(const std::string& k, double def) const { return has(k) ? std::stod(kv.at(k)) : def; }
};

Args parse(int argc, char** argv) {
    if (argc < 2) throw std::invalid_argument("usage: tracegen <command> --key value ... (see source header)");
    Args a;
    a.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string key = argv[i];
        if (a.command == "info" && i == 2) {
            a.kv["file"] = key;
            continue;
        }
        if (key.rfind("--", 0) != 0 || i + 1 >= argc) throw std::invalid_argument("expected --key value, got " + key);
        a.kv[key.substr(2)] = argv[++i];
    }
    return a;
}

// Canonical metadata: tracegen version, command, then every argument except --out, sorted.
std::string meta_of(const Args& a, const std::map<std::string, std::string>& extra = {}) {
    std::map<std::string, std::string> all = a.kv;
    all.erase("out");
    for (const auto& [k, v] : extra) all[k] = v;
    std::string m = std::string("tracegen=") + kTracegenVersion + ";gen=" + a.command;
    for (const auto& [k, v] : all) m += ";" + k + "=" + v;
    return m;
}

QueryKind query_kind(const std::string& s) {
    if (s == "uniform") return QueryKind::Uniform;
    if (s == "local") return QueryKind::Local;
    if (s == "cross") return QueryKind::Cross;
    throw std::invalid_argument("unknown --query " + s);
}

ChurnSpec mix_spec(const std::string& mix, int64_t ops) {
    ChurnSpec s;
    if (std::sscanf(mix.c_str(), "%lf,%lf,%lf", &s.p_query, &s.p_insert, &s.p_delete) != 3)
        throw std::invalid_argument("--mix must be Q,I,D");
    if (std::fabs(s.p_query + s.p_insert + s.p_delete - 1.0) > 1e-9) throw std::invalid_argument("--mix must sum to 1");
    s.ops = ops;
    return s;
}

// Initial graph (and potential-edge list for RGG) for a churn trace. Average degree `deg`.
Trace make_churn(const Args& a) {
    const int n = static_cast<int>(a.i64("n"));
    const double deg = a.f64("deg");
    const uint64_t seed = static_cast<uint64_t>(a.i64("seed"));
    ChurnSpec spec = mix_spec(a.str("mix"), a.i64("ops"));
    spec.query = query_kind(a.str("query", "uniform"));
    spec.local_h = static_cast<int>(a.i64("h", 2));
    const std::string graph = a.str("graph");
    SplitMix64 g = stream(seed, Stream::Graph);
    const auto m = static_cast<int64_t>(std::llround(deg * n / 2.0));

    if (graph == "er") return churn_trace(meta_of(a), seed, n, gnm(n, m, g), nullptr, spec);
    if (graph == "ba") {
        int k = static_cast<int>(std::llround(deg / 2.0));
        return churn_trace(meta_of(a), seed, n, barabasi_albert(n, k, g), nullptr, spec);
    }
    if (graph == "rgg") {  // amendment A1: potential degree 2 * deg, initial = random subset
        std::vector<Edge> potential = rgg_potential(n, 2.0 * deg, g);
        std::vector<Edge> initial = sample_subset(potential, static_cast<size_t>(m), g);
        return churn_trace(meta_of(a), seed, n, initial, &potential, spec);
    }
    throw std::invalid_argument("unknown --graph " + graph);
}

Trace make_ur(const Args& a) {
    const uint64_t seed = static_cast<uint64_t>(a.i64("seed"));
    const std::string graph = a.str("graph");
    const int64_t ur = a.i64("ur");
    const int checkpoints = static_cast<int>(a.i64("checkpoints", 100));
    const int qpc = static_cast<int>(a.i64("qpc", 10000));
    if (graph == "path") {
        int n = static_cast<int>(a.i64("n"));
        return ur_trace(meta_of(a), seed, n, path_graph(n), ur, checkpoints, qpc);
    }
    if (graph == "er") {  // amendment A2: m = 10 n by default, generation order
        int n = static_cast<int>(a.i64("n"));
        int64_t m = a.i64("m", 10LL * n);
        SplitMix64 g = stream(seed, Stream::Graph);
        return ur_trace(meta_of(a), seed, n, gnm(n, m, g), ur, checkpoints, qpc);
    }
    if (graph == "edgelist" || graph == "dimacs") {
        std::string path = a.str("input");
        LoadedGraph lg = graph == "dimacs" ? read_dimacs(path) : read_edge_list(path);
        std::map<std::string, std::string> extra = {{"input", std::filesystem::path(path).filename().string()},
                                                    {"input_sha256", file_sha256(path)}};
        Args clean = a;
        clean.kv.erase("input");  // recorded by name + hash, not by local path
        return ur_trace(meta_of(clean, extra), seed, lg.n, lg.edges, ur, checkpoints, qpc);
    }
    throw std::invalid_argument("unknown --graph " + graph);
}

Trace make_window(const Args& a) {
    std::string path = a.str("input");
    LoadedTemporal lt = read_temporal(path);
    Args clean = a;
    clean.kv.erase("input");
    std::map<std::string, std::string> extra = {{"input", std::filesystem::path(path).filename().string()},
                                                {"input_sha256", file_sha256(path)}};
    return window_trace(meta_of(clean, extra), static_cast<uint64_t>(a.i64("seed")), lt.n, lt.events, a.i64("window"),
                        a.f64("q"));
}

Trace make_leo(const Args& a) {
    LeoSpec s;
    s.planes = static_cast<int>(a.i64("planes", s.planes));
    s.sats_per_plane = static_cast<int>(a.i64("sats", s.sats_per_plane));
    s.phasing = static_cast<int>(a.i64("phasing", s.phasing));
    s.inclination_deg = a.f64("inclination", s.inclination_deg);
    s.altitude_km = a.f64("altitude", s.altitude_km);
    s.lat_cut_deg = a.f64("lat-cut", s.lat_cut_deg);
    s.max_range_km = a.f64("range", s.max_range_km);
    s.dt_s = a.f64("dt", s.dt_s);
    s.steps = a.i64("steps", s.steps);
    s.p_query = a.f64("q");
    return leo_trace(meta_of(a), static_cast<uint64_t>(a.i64("seed")), s);
}

void print_info(const Trace& t, const std::string& sha) {
    size_t ins = 0, del = 0, q = 0, yes = 0, fb = 0;
    for (const auto& r : t.measured) {
        if (r.op == TraceOp::Insert) ++ins;
        else if (r.op == TraceOp::Delete) ++del;
        else {
            ++q;
            yes += r.answer;
            fb += (r.flags & kQueryFallback) != 0;
        }
    }
    std::printf("sha256    %s\nvertices  %u\nseed      %llu\nmeta      %s\n", sha.c_str(), t.num_vertices,
                static_cast<unsigned long long>(t.seed), t.meta.c_str());
    std::printf("build     %zu inserts\nmeasured  %zu ops: %zu inserts, %zu deletes, %zu queries\n", t.build.size(),
                t.measured.size(), ins, del, q);
    if (q) std::printf("queries   %.4f answered true, %zu fallbacks\n", static_cast<double>(yes) / q, fb);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Args a = parse(argc, argv);
        if (a.command == "info") {
            std::string sha;
            Trace t = read_trace(a.str("file"), &sha);
            print_info(t, sha);
            return 0;
        }
        Trace t;
        if (a.command == "churn") t = make_churn(a);
        else if (a.command == "ur") t = make_ur(a);
        else if (a.command == "window") t = make_window(a);
        else if (a.command == "adversarial")
            t = adversarial_trace(meta_of(a), static_cast<uint64_t>(a.i64("seed")), static_cast<int>(a.i64("n")),
                                  static_cast<int>(a.i64("clique")), a.i64("ops"), a.f64("q"));
        else if (a.command == "leo") t = make_leo(a);
        else throw std::invalid_argument("unknown command " + a.command);

        std::string sha = write_trace(a.str("out"), t);
        print_info(t, sha);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "tracegen: %s\n", e.what());
        return 1;
    }
}
