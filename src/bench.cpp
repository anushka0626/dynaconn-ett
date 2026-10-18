// bench: replays one trace through one structure, in its own process (plan 7.1).
//
//   bench --structure NAME --trace FILE [--treap-seed S] [--reps R] [--warmup W]
//         [--mode throughput|latency] [--cap-seconds 600] [--out results.csv] [--meta k=v;k=v]
//   bench --list
//
// Per repetition: a fresh structure; the build records (timed separately); then the measured
// records. Every query's answer is checked against the trace (pre-registration §2.7).
//   throughput: wall time of the measured phase, plus hardware counters (cpu_core PMU)
//   latency:    rdtscp around every measured op, timer overhead subtracted, per-op-type
//               histograms (p50/p95/p99/p99.9/max). This is a separate pass because
//               per-op timing perturbs throughput.
// A repetition over --cap-seconds stops and is recorded as DNF (§2.6). If the warm-up
// repetition hits the cap, the measured repetitions are recorded as DNF without running.
// One CSV row per repetition (warm-up rows included, flagged); environment checks are
// recorded on every row.
// Built twice: `bench` (timing) and `bench_stats` (DC_STATS operation counters; never
// used for timing).
#include "BaselineBFS.hpp"
#include "BaselineDSU.hpp"
#include "CachedBFS.hpp"
#include "DNDTree.hpp"
#include "DTree.hpp"
#include "DynamicGraphETT.hpp"
#include "HDT.hpp"
#include "HKS.hpp"
#include "LatencyHistogram.hpp"
#include "PerfCounters.hpp"
#include "Stats.hpp"
#include "Trace.hpp"
#include <sched.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <x86intrin.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#ifndef DC_BUILD_TYPE
#define DC_BUILD_TYPE "unknown"
#endif
#ifndef DC_MARCH
#define DC_MARCH "unknown"
#endif

namespace {

using Clock = std::chrono::steady_clock;

// ---------------------------------------------------------------- structure registry

template <class F>
bool with_structure(const std::string& name, F&& f) {
#define DC_STRUCT(label, type)            \
    if (name == label) {                  \
        f.template operator()<type>();    \
        return true;                      \
    }
    DC_STRUCT("bfs", BaselineBFS)
    DC_STRUCT("dsu_eager", BaselineDSU)
    DC_STRUCT("dsu_lazy", LazyDSU)
    DC_STRUCT("cached_bfs", CachedBFS)
    DC_STRUCT("ett_p0", EttP0)
    DC_STRUCT("ett_f0", EttF0)
    DC_STRUCT("ett_f1", EttF1)
    DC_STRUCT("ett_f2", EttF2)
    DC_STRUCT("ett_f2_k1", EttF2KHop<1>)
    DC_STRUCT("ett_f2_k2", EttF2KHop<2>)
    DC_STRUCT("ett_f2_k3", EttF2KHop<3>)
    DC_STRUCT("dtree_p0", DTreeP0)
    DC_STRUCT("dtree_f2", DTreeF2)
    DC_STRUCT("dnd", DNDTree)
    DC_STRUCT("hks_p0", HksP0)
    DC_STRUCT("hks_f2", HksF2)
    DC_STRUCT("hdt_p0", HdtP0)
    DC_STRUCT("hdt_f2", HdtF2)
#undef DC_STRUCT
    return false;
}

const char* kStructureNames[] = {"bfs", "dsu_eager", "dsu_lazy", "cached_bfs", "ett_p0", "ett_f0",
                                 "ett_f1", "ett_f2", "ett_f2_k1", "ett_f2_k2", "ett_f2_k3", "dtree_p0",
                                 "dtree_f2", "dnd", "hks_p0", "hks_f2", "hdt_p0", "hdt_f2"};

// Structures with treap randomness take the treap seed; the others ignore it.
template <class S>
std::unique_ptr<S> make_structure(int n, uint32_t treap_seed) {
    if constexpr (std::is_constructible_v<S, int, uint32_t>) return std::make_unique<S>(n, treap_seed);
    else return std::make_unique<S>(n);
}

template <class S>
constexpr bool uses_treap_seed = std::is_constructible_v<S, int, uint32_t>;

// ---------------------------------------------------------------- timing

inline uint64_t rdtscp() {
    unsigned aux;
    return __rdtscp(&aux);
}

struct TscCalibration {
    double cycles_per_ns = 0;
    uint64_t overhead_cycles = 0;  // median cost of an empty rdtscp pair
};

TscCalibration calibrate() {
    TscCalibration c;
    auto t0 = Clock::now();
    uint64_t c0 = rdtscp();
    while (Clock::now() - t0 < std::chrono::milliseconds(200)) {
    }
    uint64_t c1 = rdtscp();
    auto t1 = Clock::now();
    c.cycles_per_ns = static_cast<double>(c1 - c0) / std::chrono::duration<double, std::nano>(t1 - t0).count();
    std::vector<uint64_t> d(100000);
    for (auto& x : d) {
        uint64_t a = rdtscp();
        uint64_t b = rdtscp();
        x = b - a;
    }
    std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
    c.overhead_cycles = d[d.size() / 2];
    return c;
}

// ---------------------------------------------------------------- environment

std::string read_line(const std::string& path) {
    std::ifstream f(path);
    std::string s;
    if (!std::getline(f, s)) return "na";
    return s;
}

struct Environment {
    std::string cpu_model, pinned_cpus, governor, no_turbo, boost, kernel, compiler, host;
};

Environment environment() {
    Environment e;
    std::ifstream ci("/proc/cpuinfo");
    for (std::string line; std::getline(ci, line);)
        if (line.rfind("model name", 0) == 0) {
            e.cpu_model = line.substr(line.find(':') + 2);
            break;
        }
    cpu_set_t set;
    CPU_ZERO(&set);
    sched_getaffinity(0, sizeof set, &set);
    int first = -1, count = 0;
    for (int i = 0; i < CPU_SETSIZE; ++i)
        if (CPU_ISSET(i, &set)) {
            if (first < 0) first = i;
            ++count;
        }
    e.pinned_cpus = count == 1 ? std::to_string(first) : "unpinned(" + std::to_string(count) + ")";
    int cpu = first < 0 ? 0 : first;
    e.governor = read_line("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/cpufreq/scaling_governor");
    e.no_turbo = read_line("/sys/devices/system/cpu/intel_pstate/no_turbo");
    e.boost = read_line("/sys/devices/system/cpu/cpufreq/boost");
    utsname u;
    e.kernel = uname(&u) == 0 ? std::string(u.release) : "na";
    e.compiler = __VERSION__;
    char host[256] = {};
    gethostname(host, sizeof host - 1);
    e.host = host;
    return e;
}

// ---------------------------------------------------------------- CSV

std::string csv(std::string s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) out += c == '"' ? std::string("\"\"") : std::string(1, c);
    return out + "\"";
}

struct Row {
    std::vector<std::pair<std::string, std::string>> fields;
    template <class T>
    void add(const std::string& k, const T& v) {
        std::ostringstream os;
        os.precision(10);
        os << v;
        fields.push_back({k, os.str()});
    }
};

void append_csv(const std::string& path, const Row& r) {
    bool fresh = !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
    std::ofstream out(path, std::ios::app);
    if (!out) throw std::runtime_error("cannot write " + path);
    if (fresh) {
        for (size_t i = 0; i < r.fields.size(); ++i) out << (i ? "," : "") << r.fields[i].first;
        out << "\n";
    }
    for (size_t i = 0; i < r.fields.size(); ++i) out << (i ? "," : "") << csv(r.fields[i].second);
    out << "\n";
}

// ---------------------------------------------------------------- runs

struct Options {
    std::string structure, trace_path, out = "-", mode = "throughput", meta;
    uint32_t treap_seed = 1337;
    int reps = 3, warmup = 1;
    double cap_seconds = 600;
};

struct RepResult {
    double build_ns = 0, measured_ns = 0;
    uint64_t ops_done = 0, mismatches = 0;
    bool dnf = false;
    std::vector<int64_t> counters;
    bool multiplexed = false;
    dc::LatencyHistogram hist[3];
    dc::OpStats stats{};
};

volatile uint64_t g_sink = 0;  // query results feed this, so queries cannot be optimized away

template <class S>
RepResult run_rep(const dc::Trace& t, const Options& o, const TscCalibration& tsc, dc::PerfCounters* perf) {
    RepResult r;
    const auto cap = std::chrono::duration<double>(o.cap_seconds);
    auto s = make_structure<S>(static_cast<int>(t.num_vertices), o.treap_seed);
#ifdef DC_STATS
    dc::stats() = {};
#endif

    auto b0 = Clock::now();
    for (const auto& rec : t.build) s->insert_edge(static_cast<int>(rec.u), static_cast<int>(rec.v));
    r.build_ns = std::chrono::duration<double, std::nano>(Clock::now() - b0).count();
#ifdef DC_STATS
    dc::stats() = {};  // count the measured phase only
#endif

    uint64_t sink = 0, mismatches = 0;
    const auto deadline = Clock::now() + std::chrono::duration_cast<Clock::duration>(cap);
    const size_t m = t.measured.size();
    size_t i = 0;
    if (o.mode == "latency") {
        auto m0 = Clock::now();
        for (; i < m; ++i) {
            if ((i & 4095) == 0 && Clock::now() > deadline) {
                r.dnf = true;
                break;
            }
            const auto& rec = t.measured[i];
            uint64_t c0 = rdtscp();
            if (rec.op == dc::TraceOp::Insert) s->insert_edge(static_cast<int>(rec.u), static_cast<int>(rec.v));
            else if (rec.op == dc::TraceOp::Delete) s->delete_edge(static_cast<int>(rec.u), static_cast<int>(rec.v));
            else {
                bool a = s->is_connected(static_cast<int>(rec.u), static_cast<int>(rec.v));
                sink += a;
                mismatches += a != (rec.answer == 1);
            }
            uint64_t c1 = rdtscp();
            uint64_t d = c1 - c0;
            r.hist[static_cast<int>(rec.op)].add(d > tsc.overhead_cycles ? d - tsc.overhead_cycles : 0);
        }
        r.measured_ns = std::chrono::duration<double, std::nano>(Clock::now() - m0).count();
    } else {
        if (perf) perf->start();
        auto m0 = Clock::now();
        for (; i < m; ++i) {
            if ((i & 4095) == 0 && Clock::now() > deadline) {
                r.dnf = true;
                break;
            }
            const auto& rec = t.measured[i];
            if (rec.op == dc::TraceOp::Insert) s->insert_edge(static_cast<int>(rec.u), static_cast<int>(rec.v));
            else if (rec.op == dc::TraceOp::Delete) s->delete_edge(static_cast<int>(rec.u), static_cast<int>(rec.v));
            else {
                bool a = s->is_connected(static_cast<int>(rec.u), static_cast<int>(rec.v));
                sink += a;
                mismatches += a != (rec.answer == 1);
            }
        }
        r.measured_ns = std::chrono::duration<double, std::nano>(Clock::now() - m0).count();
        if (perf) r.counters = perf->stop(r.multiplexed);
    }
    g_sink = g_sink + sink;
    r.ops_done = i;
    r.mismatches = mismatches;
#ifdef DC_STATS
    r.stats = dc::stats();
#endif
    return r;
}

std::string trace_name(const std::string& path) { return std::filesystem::path(path).stem().string(); }

template <class S>
void run_all(const Options& o) {
    std::string sha;
    dc::Trace t = dc::read_trace(o.trace_path, &sha);
    Environment env = environment();
    TscCalibration tsc = calibrate();
    std::unique_ptr<dc::PerfCounters> perf;
    if (o.mode == "throughput") perf = std::make_unique<dc::PerfCounters>();
    const uint32_t seed = uses_treap_seed<S> ? o.treap_seed : 0;  // 0: structure has no treap

    bool warmup_dnf = false;
    for (int rep = -o.warmup; rep < o.reps; ++rep) {
        RepResult r;
        bool skipped = false;
        if (warmup_dnf && rep >= 0) {
            r.dnf = true;
            skipped = true;
        } else {
            r = run_rep<S>(t, o, tsc, perf.get());
            if (rep < 0 && r.dnf) warmup_dnf = true;
        }
        Row row;
        row.add("structure", o.structure);
        row.add("trace", trace_name(o.trace_path));
        row.add("trace_sha256", sha);
        row.add("treap_seed", seed);
        row.add("mode", o.mode);
        row.add("rep", rep);
        row.add("warmup", rep < 0 ? 1 : 0);
        row.add("vertices", t.num_vertices);
        row.add("build_ops", t.build.size());
        row.add("measured_ops", t.measured.size());
        row.add("ops_done", r.ops_done);
        row.add("dnf", r.dnf ? 1 : 0);
        row.add("dnf_skipped", skipped ? 1 : 0);
        row.add("mismatches", r.mismatches);
        row.add("build_ns", r.build_ns);
        row.add("measured_ns", r.measured_ns);
        row.add("ns_per_op", r.ops_done ? r.measured_ns / static_cast<double>(r.ops_done) : 0.0);
        if (perf) {
            const auto& names = perf->event_names();
            for (size_t k = 0; k < names.size(); ++k)
                row.add(names[k], k < r.counters.size() ? r.counters[k] : -1);
            row.add("counters_multiplexed", r.multiplexed ? 1 : 0);
        }
        if (o.mode == "latency") {
            const char* op_names[] = {"insert", "delete", "query"};
            for (int k = 0; k < 3; ++k) {
                const auto& h = r.hist[k];
                double f = 1.0 / tsc.cycles_per_ns;
                std::string p = std::string(op_names[k]) + "_";
                row.add(p + "count", h.count());
                row.add(p + "mean_ns", h.mean() * f);
                row.add(p + "p50_ns", h.quantile(0.50) * f);
                row.add(p + "p95_ns", h.quantile(0.95) * f);
                row.add(p + "p99_ns", h.quantile(0.99) * f);
                row.add(p + "p999_ns", h.quantile(0.999) * f);
                row.add(p + "max_ns", static_cast<double>(h.max()) * f);
            }
            row.add("timer_overhead_cycles", tsc.overhead_cycles);
        }
#ifdef DC_STATS
        const dc::OpStats& st = r.stats;
        row.add("stat_root_walk_steps", st.root_walk_steps);
        row.add("stat_index_walk_steps", st.index_walk_steps);
        row.add("stat_treap_splits", st.treap_splits);
        row.add("stat_treap_merges", st.treap_merges);
        row.add("stat_tree_edge_deletes", st.tree_edge_deletes);
        row.add("stat_replacement_vertices", st.replacement_vertices);
        row.add("stat_replacement_edges_scanned", st.replacement_edges_scanned);
        row.add("stat_replacements_found", st.replacements_found);
        row.add("stat_sample_draws", st.sample_draws);
        row.add("stat_sample_hits", st.sample_hits);
        row.add("stat_level_raises", st.level_raises);
        row.add("stat_khop_searches", st.khop_searches);
        row.add("stat_khop_hits", st.khop_hits);
        row.add("stat_khop_edges_scanned", st.khop_edges_scanned);
        row.add("stat_dsu_rebuilds", st.dsu_rebuilds);
        row.add("stat_bfs_vertices_visited", st.bfs_vertices_visited);
#endif
        rusage ru;
        getrusage(RUSAGE_SELF, &ru);
        row.add("peak_rss_kb", ru.ru_maxrss);
        row.add("tsc_cycles_per_ns", tsc.cycles_per_ns);
        row.add("pinned_cpus", env.pinned_cpus);
        row.add("governor", env.governor);
        row.add("no_turbo", env.no_turbo);
        row.add("boost", env.boost);
        row.add("cpu_model", env.cpu_model);
        row.add("kernel", env.kernel);
        row.add("compiler", env.compiler);
        row.add("build_type", DC_BUILD_TYPE);
        row.add("march", DC_MARCH);
        row.add("stats_build",
#ifdef DC_STATS
                1
#else
                0
#endif
        );
        row.add("host", env.host);
        row.add("unix_time", static_cast<long long>(std::time(nullptr)));
        row.add("meta", o.meta);

        if (o.out == "-") {
            for (const auto& [k, v] : row.fields) std::printf("%s=%s ", k.c_str(), v.c_str());
            std::printf("\n");
        } else {
            append_csv(o.out, row);
        }
        if (r.mismatches) std::fprintf(stderr, "bench: %llu WRONG ANSWERS (%s on %s)\n",
                                       static_cast<unsigned long long>(r.mismatches), o.structure.c_str(),
                                       trace_name(o.trace_path).c_str());
    }
}

}  // namespace

int main(int argc, char** argv) {
    try {
        Options o;
        for (int i = 1; i < argc; ++i) {
            std::string k = argv[i];
            if (k == "--list") {
                for (const char* s : kStructureNames) std::printf("%s\n", s);
                return 0;
            }
            if (i + 1 >= argc) throw std::invalid_argument("missing value for " + k);
            std::string v = argv[++i];
            if (k == "--structure") o.structure = v;
            else if (k == "--trace") o.trace_path = v;
            else if (k == "--treap-seed") o.treap_seed = static_cast<uint32_t>(std::stoul(v));
            else if (k == "--reps") o.reps = std::stoi(v);
            else if (k == "--warmup") o.warmup = std::stoi(v);
            else if (k == "--mode") o.mode = v;
            else if (k == "--cap-seconds") o.cap_seconds = std::stod(v);
            else if (k == "--out") o.out = v;
            else if (k == "--meta") o.meta = v;
            else throw std::invalid_argument("unknown option " + k);
        }
        if (o.mode != "throughput" && o.mode != "latency") throw std::invalid_argument("--mode throughput|latency");
        if (o.trace_path.empty()) throw std::invalid_argument("--trace required");
        bool found = with_structure(o.structure, [&]<class S>() { run_all<S>(o); });
        if (!found) throw std::invalid_argument("unknown --structure " + o.structure + " (see --list)");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bench: %s\n", e.what());
        return 1;
    }
}
