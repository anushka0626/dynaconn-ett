#include "BaselineBFS.hpp"
#include "BaselineDSU.hpp"
#include "CachedBFS.hpp"
#include "ChurnGenerator.hpp"
#include "DNDTree.hpp"
#include "DTree.hpp"
#include "DynamicGraphETT.hpp"
#include "Oracle.hpp"
#include "TestFramework.hpp"
#include <cstdlib>
#include <string>
#include <tuple>

// Randomized differential tests: every structure replays the same op stream and must agree
// with the independent Oracle on every query.

namespace {

struct Mix {
    const char* name;
    double p_insert;
    double p_delete;
    int64_t target_edges_per_vertex;  // < 0: no density control
};

// Named mixes from the plan, plus growth and dense steady-state streams that exercise
// replacement search on graphs with many cycles.
constexpr Mix kMixes[] = {
    {"read_heavy", 0.10, 0.10, -1},
    {"balanced", 0.33, 0.33, -1},
    {"churn_heavy", 0.45, 0.45, -1},
    {"growth", 0.60, 0.20, -1},
    {"dense_steady", 0.45, 0.45, 3},
};

constexpr int kSizes[] = {2, 8, 30, 200, 2000};

// Seeds per (size, mix). DC_FUZZ_SEEDS overrides it, e.g. for a longer local soak run.
int seed_count() {
    if (const char* env = std::getenv("DC_FUZZ_SEEDS")) return std::atoi(env);
    return 100;
}

ChurnConfig make_config(int n, const Mix& mix, uint64_t seed, int ops) {
    ChurnConfig cfg;
    cfg.num_vertices = n;
    cfg.num_operations = ops;
    cfg.p_insert = mix.p_insert;
    cfg.p_delete = mix.p_delete;
    cfg.seed = seed;
    cfg.target_edges = mix.target_edges_per_vertex < 0 ? -1 : mix.target_edges_per_vertex * n;
    return cfg;
}

std::string where(int n, const Mix& mix, uint64_t seed, size_t op) {
    return "V=" + std::to_string(n) + " mix=" + mix.name + " seed=" + std::to_string(seed) +
           " op#" + std::to_string(op);
}

// Structures are neither copyable nor movable, so tuple elements are built in place from a
// single argument each.
struct NameAndSize {
    const char* name;
    int n;
};

template <class S>
struct Named {
    const char* name;
    S impl;
    Named(NameAndSize a) : name(a.name), impl(a.n) {}
};

// Every structure under test. BFS comes first because the all-pairs check skips it (it is
// O(V + E) per query).
using AllStructures = std::tuple<Named<BaselineBFS>, Named<BaselineDSU>, Named<LazyDSU>, Named<CachedBFS>,
                                 Named<EttP0>, Named<EttF0>, Named<EttF1>, Named<EttF2>,
                                 Named<EttF2KHop<1>>, Named<EttF2KHop<2>>, Named<EttF2KHop<3>>,
                                 Named<DTreeP0>, Named<DTreeF2>, Named<DNDTree>>;

template <class T>
constexpr bool has_invariants = requires(T& t) { t.check_invariants(); };

struct Structures {
    Oracle oracle;
    AllStructures all;

    explicit Structures(int n)
        : oracle(n),
          all(NameAndSize{"bfs", n}, NameAndSize{"dsu_eager", n}, NameAndSize{"dsu_lazy", n},
              NameAndSize{"cached_bfs", n}, NameAndSize{"ett_p0", n}, NameAndSize{"ett_f0", n},
              NameAndSize{"ett_f1", n}, NameAndSize{"ett_f2", n}, NameAndSize{"ett_f2_k1", n},
              NameAndSize{"ett_f2_k2", n}, NameAndSize{"ett_f2_k3", n}, NameAndSize{"dtree_p0", n},
              NameAndSize{"dtree_f2", n}, NameAndSize{"dnd", n}) {}

    template <class F>
    void each(F&& f) {
        std::apply([&](auto&... s) { (f(s.impl, s.name), ...); }, all);
    }
};

void apply(Structures& s, const GraphOperation& op) {
    if (op.type == OpType::INSERT) {
        s.oracle.insert_edge(op.u, op.v);
        s.each([&](auto& d, const char*) { d.insert_edge(op.u, op.v); });
    } else if (op.type == OpType::DELETE) {
        s.oracle.delete_edge(op.u, op.v);
        s.each([&](auto& d, const char*) { d.delete_edge(op.u, op.v); });
    }
}

void check_query(Structures& s, int u, int v, const std::string& ctx) {
    bool expected = s.oracle.is_connected(u, v);
    s.each([&](auto& d, const char* name) {
        CHECK_MSG(d.is_connected(u, v) == expected,
                  name << " says connected(" << u << "," << v << ")=" << !expected << " at " << ctx);
    });
}

void check_state(Structures& s, const std::string& ctx) {
    s.each([&](auto& d, const char* name) {
        CHECK_MSG(d.edge_count() == s.oracle.edge_count(), name << " edge_count mismatch at " << ctx);
        if constexpr (has_invariants<std::decay_t<decltype(d)>>) {
            std::string err = d.check_invariants();
            CHECK_MSG(err.empty(), name << ": " << err << " at " << ctx);
        }
    });
}

// All-pairs check on small graphs, skipping BFS (O(V + E) per pair).
void check_all_pairs(Structures& s, int n, const std::string& ctx) {
    for (int u = 0; u < n; ++u) {
        for (int v = u + 1; v < n; ++v) {
            bool expected = s.oracle.is_connected(u, v);
            s.each([&](auto& d, const char* name) {
                if constexpr (std::is_same_v<std::decay_t<decltype(d)>, BaselineBFS>) return;
                CHECK_MSG(d.is_connected(u, v) == expected, name << " all-pairs (" << u << "," << v << ") at " << ctx);
            });
        }
    }
}

void run_differential(int n, const Mix& mix) {
    const int ops = n <= 200 ? 1500 : 3000;
    const int seeds = seed_count();
    for (int seed = 1; seed <= seeds; ++seed) {
        Workload w = generate_churn_workload(make_config(n, mix, seed, ops));
        Structures s(n);
        for (size_t i = 0; i < w.ops.size(); ++i) {
            const auto& op = w.ops[i];
            apply(s, op);
            if (op.type == OpType::QUERY) check_query(s, op.u, op.v, where(n, mix, seed, i));
            if (i % 64 == 63) check_state(s, where(n, mix, seed, i));
            if (n <= 30 && i % 16 == 15) check_all_pairs(s, n, where(n, mix, seed, i));
        }
        check_state(s, where(n, mix, seed, w.ops.size()));
    }
}

template <class... Ett>
struct EttSet {
    std::tuple<Ett...> impls;
    explicit EttSet(int n) : impls(((void)sizeof(Ett), n)...) {}

    void apply(const GraphOperation& op) {
        std::apply(
            [&](auto&... e) {
                if (op.type == OpType::INSERT) (e.insert_edge(op.u, op.v), ...);
                else if (op.type == OpType::DELETE) (e.delete_edge(op.u, op.v), ...);
            },
            impls);
    }
};

// Invariants after every single op (catches transient corruption that later ops repair).
void run_invariants_every_op(int n, int seeds) {
    for (const Mix& mix : kMixes) {
        for (int seed = 1; seed <= seeds; ++seed) {
            Workload w = generate_churn_workload(make_config(n, mix, 1000 + seed, 800));
            EttSet<EttP0, EttF2, EttF2KHop<2>, DTreeP0, DTreeF2, DNDTree> set(n);
            for (size_t i = 0; i < w.ops.size(); ++i) {
                if (w.ops[i].type == OpType::QUERY) continue;
                set.apply(w.ops[i]);
                std::apply(
                    [&](auto&... e) {
                        (([&] {
                             std::string err = e.check_invariants();
                             CHECK_MSG(err.empty(), err << " at " << where(n, mix, 1000 + seed, i));
                         }()),
                         ...);
                    },
                    set.impls);
            }
        }
    }
}

// The four ETT layouts must build identical treaps (shape, priorities, sizes) after every
// op. This is the property that isolates layout in RQ1 and RQ2.
void run_shape_identity(int n, int seeds) {
    for (const Mix& mix : kMixes) {
        for (int seed = 1; seed <= seeds; ++seed) {
            Workload w = generate_churn_workload(make_config(n, mix, 2000 + seed, 800));
            EttSet<EttP0, EttF0, EttF1, EttF2> set(n);
            auto all_equal = [&] {
                auto& [p0, f0, f1, f2] = set.impls;
                auto ref = p0.euler_forest().serialize();
                return ref == f0.euler_forest().serialize() && ref == f1.euler_forest().serialize() &&
                       ref == f2.euler_forest().serialize();
            };
            CHECK(all_equal());
            for (size_t i = 0; i < w.ops.size(); ++i) {
                if (w.ops[i].type == OpType::QUERY) continue;
                set.apply(w.ops[i]);
                CHECK_MSG(all_equal(), "treaps diverge at " << where(n, mix, 2000 + seed, i));
            }
        }
    }
}

}  // namespace

// One test per (size, mix), named differential_v<size>_<mix>, so ctest -j runs them in parallel.
[[maybe_unused]] static const bool differential_registered = [] {
    for (int n : kSizes) {
        for (const Mix& mix : kMixes) {
            std::string name = "differential_v" + std::to_string(n) + "_" + mix.name;
            dctest::registry().push_back({name, [n, &mix] { run_differential(n, mix); }});
        }
    }
    return true;
}();

// D-tree's P0 and F2 variants must build the identical tree (parents, sizes, child and
// non-tree orders) after every op, including queries, which can reroot.
void run_dtree_identity(int n, int seeds) {
    for (const Mix& mix : kMixes) {
        for (int seed = 1; seed <= seeds; ++seed) {
            Workload w = generate_churn_workload(make_config(n, mix, 3000 + seed, 800));
            DTreeP0 p(n);
            DTreeF2 f(n);
            for (size_t i = 0; i < w.ops.size(); ++i) {
                const auto& op = w.ops[i];
                if (op.type == OpType::INSERT) {
                    p.insert_edge(op.u, op.v);
                    f.insert_edge(op.u, op.v);
                } else if (op.type == OpType::DELETE) {
                    p.delete_edge(op.u, op.v);
                    f.delete_edge(op.u, op.v);
                } else {
                    CHECK(p.is_connected(op.u, op.v) == f.is_connected(op.u, op.v));
                }
                CHECK_MSG(p.serialize() == f.serialize(), "D-trees diverge at " << where(n, mix, 3000 + seed, i));
            }
        }
    }
}

TEST_CASE("dtree_layout_identity") {
    run_dtree_identity(8, 20);
    run_dtree_identity(30, 20);
    run_dtree_identity(200, 10);
}

TEST_CASE("invariants_every_op") {
    run_invariants_every_op(8, 30);
    run_invariants_every_op(30, 30);
    run_invariants_every_op(200, 10);
}

TEST_CASE("layout_shape_identity") {
    run_shape_identity(8, 20);
    run_shape_identity(30, 20);
    run_shape_identity(200, 10);
}
