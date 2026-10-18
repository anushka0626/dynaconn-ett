# Cache-Conscious Fully Dynamic Graph Connectivity

**A microarchitecture-aware study of fully dynamic connectivity: when do memory layout and constant factors matter more than the algorithm?**

Many real networks keep gaining and losing links: satellite constellations, data-centre and service-mesh topologies, routing networks. A recurring question in them is *"can A still reach B?"* Theory offers clever **fully dynamic connectivity** structures with polylogarithmic guarantees. This project asks how they behave on real hardware, where cache misses and memory layout often decide the running time.

The repository holds:
- optimized C++20 implementations of the main structures, each in **two memory layouts** (pointer-based and flat arrays) that run identical algorithm code;
- a versioned, hashed **benchmark suite** of 231 workload traces;
- a **pre-registered** experimental methodology.

> **Status (October 2026):** implementation, tests and workloads are complete. Measurements (Phase 7) are in progress, starting with the pilot study. **No timing results are reported yet.** Memory figures below are measured.

---

## Contents
1. [Research questions](#research-questions)
2. [How this differs from prior work](#how-this-differs-from-prior-work)
3. [Structures implemented](#structures-implemented)
4. [Workloads](#workloads)
5. [Methodology](#methodology)
6. [Results so far](#results-so-far)
7. [Repository layout](#repository-layout)
8. [Getting started](#getting-started)
9. [Running experiments](#running-experiments)
10. [Documentation](#documentation)
11. [Roadmap](#roadmap)
12. [Data sources and acknowledgements](#data-sources-and-acknowledgements)

---

## Research questions

| | Question |
|---|---|
| **RQ1** | With the algorithm fixed, how much does **memory layout** (pointer nodes → flat arena → flat hash maps → flat adjacency) change speed, cache misses and memory? |
| **RQ2** *(headline)* | Applied to several algorithms (ETT, HDT, D-tree), does layout change which **algorithm wins**? |
| **RQ3** | Which structure wins as a function of **query fraction, graph size and query distribution** (a crossover map)? |
| RQ3b | Do the findings of the SIGMOD 2025 benchmark, which was measured in Python, hold in optimized C++? |
| RQ3c | On small graphs, which earlier work leaves out, is a dynamic structure worth it at all? |
| **RQ4** | Do hardware and operation counters explain running time better than asymptotic bounds? |
| **RQ5** | Does a bounded *k*-hop search find replacement edges cheaply on clustered graphs? |

Hypotheses, metrics and decision rules for all of these were registered **before any measurement**: [`docs/PREREGISTRATION.md`](docs/PREREGISTRATION.md).

## How this differs from prior work

The closest work compares dynamic-connectivity *algorithms* by wall-clock time:
- Chen, Böhlen & Helmer, SIGMOD 2025;
- D-tree, VLDB 2022;
- Xu et al., SIGMOD 2024.

The SIGMOD 2025 benchmark is written in Python, pauses updates whenever it runs queries, reports averages only, and leaves small graphs out of scope. This project adds:

1. **Native code with hardware counters:** cache misses, IPC, top-down stall analysis, behavior across the cache hierarchy.
2. **Layout ablations applied across algorithms**, with tests proving each layout pair runs identical logic.
3. **Interleaved read/write workloads** and a crossover map, with **tail latency** (p99) as well as averages.
4. **Simple and state-of-the-art baselines together:** BFS, eager and lazy DSU, cached labels, and Xu et al.'s DND-trees.
5. **Statistical rigor:** paired designs, confidence intervals, a multiple-comparison correction, and pre-registration.
6. **A reproducible benchmark suite,** answering SIGMOD 2025's call for standardized workloads.

The full positioning is in [`docs/RELATED_WORK.md`](docs/RELATED_WORK.md).

## Structures implemented

Every structure implements one interface ([`DynamicConnectivity.hpp`](include/DynamicConnectivity.hpp)): `insert_edge`, `delete_edge`, `is_connected`, `edge_count`.

| Structure | Idea | Layouts | Name in `bench` |
|---|---|---|---|
| **ETT** (ours) | Euler-tour trees on treaps, with a smaller-side replacement search (no edge levels) | P0, F0, F1, F2 | `ett_p0` … `ett_f2` |
| ETT + k-hop | F2 plus a bounded *k*-hop replacement search (RQ5) | F2 | `ett_f2_k1/k2/k3` |
| **HKS** | Levelless Henzinger–King (random sampling, then exhaustive search), as SIGMOD 2025 implemented it | P0, F2 | `hks_p0`, `hks_f2` |
| **HDT** | Holm–de Lichtenberg–Thorup: one Euler-tour forest per level, amortized O(log² n) | P0, F2 | `hdt_p0`, `hdt_f2` |
| **D-tree** | Shallow spanning trees with centroid heuristics (Chen et al., VLDB 2022) | P0, F2 | `dtree_p0`, `dtree_f2` |
| **DND-trees** | Xu et al.: an ID-tree plus a disjoint-set tree, near-constant-time queries | F2 | `dnd` |
| BFS | O(1) updates; each query runs a BFS | flat | `bfs` |
| Eager DSU | Union–find, rebuilt on every delete (naive reference point) | – | `dsu_eager` |
| Lazy DSU | Union–find, rebuilt at the next query after deletes | – | `dsu_lazy` |
| Cached BFS | Component labels from a flood fill, invalidated by deletes | flat | `cached_bfs` |

**Layouts** (each step changes one thing):

| Layout | Tree nodes | Edge lookup | Adjacency |
|---|---|---|---|
| P0 | heap nodes, 64-bit pointers | `std::map` | `std::vector` + `std::unordered_map` positions |
| F0 | contiguous arena, 32-bit indices | `std::map` | as P0 |
| F1 | arena | flat open-addressing hash map | as P0 |
| F2 | arena | flat hash map | flat pooled per-vertex sets |

Notes on the implementations:
- D-tree, DND-trees and HKS were **reimplemented from their papers**. The authors' unlicensed code was read only to resolve ambiguities and check outputs.
- Every choice where a paper is ambiguous, and every place we follow the authors' code instead of the paper, is listed in [`docs/IMPLEMENTATION_NOTES.md`](docs/IMPLEMENTATION_NOTES.md).

Honest complexity note: our ETT's delete is **not** polylogarithmic in the worst case. It scans the non-tree edges on the smaller side of a cut. HDT is the structure with the O(log² n) amortized bound.

## Workloads

All experiments replay **traces**: binary files of operations in which the correct answer to every query is stored. Answers are computed by two independent structures during generation. The format and every generator are documented in [`docs/TRACE_FORMAT.md`](docs/TRACE_FORMAT.md).

| Family | What it models | Used for |
|---|---|---|
| Erdős–Rényi churn (V = 10³ … 10⁶) | controlled random graphs at a fixed density, with read-heavy / balanced / churn-heavy mixes | RQ1, RQ2, RQ3, RQ3c |
| Query distributions | uniform pairs, local pairs (≤ 2 hops), cross-component pairs | RQ3 |
| Random geometric graphs | clustered, spatial networks | RQ5 |
| SIGMOD 2025 *u_r* workloads | their design: path graph, G(n, 10n), delete one edge every u_r inserts | RQ3b |
| Adversarial bridge | worst case for smaller-side scanning | exploratory |
| Barabási–Albert | scale-free degree distribution | exploratory |
| SNAP temporal graphs (CollegeMsg, email-Eu-core, sx-mathoverflow) | real link churn, as sliding windows | exploratory, held-out |
| com-YouTube | a large real social graph (one of SIGMOD 2025's datasets) | exploratory |
| DIMACS road network, New York | large-diameter real graph (a subset of SIGMOD 2025's USA Road) | exploratory |
| LEO constellation (72 × 22 Walker-delta) | inter-satellite links switching with orbital geometry | exploratory, held-out |

- **The suite:** 231 traces (about 3.1 GB), defined in [`scripts/trace_specs.py`](scripts/trace_specs.py).
- **Hashes:** listed in [`traces/MANIFEST.tsv`](traces/MANIFEST.tsv).
- **Reproducibility:** every trace regenerates byte-for-byte, with GCC and Clang. The exception is LEO, which uses `sin`/`cos` and is bit-identical on the same machine and toolchain.

## Methodology

- **Pre-registration:** 18 confirmatory tests, with thresholds and decision rules fixed in advance. Later changes are dated amendments.
- **Statistics:**
  - paired comparisons by trace seed (Wilcoxon signed-rank, paired bootstrap 95% confidence intervals);
  - Holm–Bonferroni correction across the confirmatory tests;
  - the smallest effect of interest is a 5% speedup.
- **Seeds:** 12 trace seeds per confirmatory scenario; 3 treap seeds for randomized structures; repetition counts set by a pilot study.
- **Machine discipline:**
  - one structure per process, pinned to one performance core;
  - `performance` governor, turbo off, AC power;
  - hardware counters from the performance-core PMU;
  - a 10-minute cap per run, recorded as DNF and never imputed;
  - environment and git state recorded on every result row.
- **Correctness:**
  - 62 tests: an independent oracle, randomized differential testing of every structure (5 sizes × 5 mixes × 100 seeds), invariant checks after every operation, and layout behavior-identity tests;
  - run under GCC and Clang, with and without ASan/UBSan, warnings as errors;
  - the suite itself was checked by planting bugs, all caught;
  - every benchmark run also checks every answer against the trace.
- **Replication:** the confirmatory scenarios will be rerun on a second, AMD machine.

## Results so far

**Memory** (measured by `memory_profile`: V = 20,000, 80,000 distinct edges, insert-only, glibc malloc; bytes per edge):

| Structure | P0 | F0 | F1 | F2 |
|---|---|---|---|---|
| ETT | 153.4 | 148.4 | 129.6 | 115.6 |
| HKS | 166.4 | – | – | 116.5 |
| HDT | 581.4 | – | – | 470.0 |
| D-tree | 117.2 | – | – | 87.5 |
| DND-trees | – | – | – | 94.6 |

Baselines: BFS 83.6, eager / lazy DSU 41.3, cached BFS 84.6.

Bookkeeping, not tree nodes, dominates the memory. In the P0 ETT, treap nodes are only about a quarter of the bytes; adjacency sets and the edge map make up the rest.

**Timing:** in progress. Results will be reported against the pre-registered tests.

## Repository layout

```
include/            header-only library: all structures, containers, workloads, trace format
  DynamicConnectivity.hpp   common interface
  EulerTourForest.hpp       shared treap / Euler-tour code (all ETT-based structures)
  TreapStorage.hpp          pointer vs. arena node storage (+ HKS/HDT augmentations)
  DynamicGraphETT.hpp       our ETT (layouts P0–F2, k-hop variants)
  HKS.hpp HDT.hpp DTree.hpp DNDTree.hpp      competitors
  BaselineBFS.hpp BaselineDSU.hpp CachedBFS.hpp   baselines
  FlatHashMap.hpp OrderedSets.hpp EdgeMaps.hpp    shared containers for the layouts
  Trace.hpp Sha256.hpp Workloads.hpp WorkloadInputs.hpp LeoConstellation.hpp   workloads
  PerfCounters.hpp LatencyHistogram.hpp Stats.hpp MemoryAccounting.hpp         measurement
src/
  bench.cpp           benchmark harness (one structure × one trace per process)
  tracegen.cpp        trace generator
  memory_profile.cpp  per-structure memory breakdown
  main.cpp            Phase 2 demo: parity check + memory table
tests/              test suite (ctest)
scripts/
  check.sh            full correctness matrix (GCC/Clang × sanitizers)
  fetch_data.sh       downloads SNAP + DIMACS inputs, verifies checksums
  trace_specs.py      defines every trace in the study → traces/specs.tsv
  build_traces.py     builds or verifies traces → data/traces/, traces/MANIFEST.tsv
  run_sweep.py        runs experiment plans (resumable) → results/raw/
traces/             committed: specs, manifest, dataset checksums
docs/               research plan, pre-registration, related work, implementation notes, trace format
paper/              IEEE-format paper draft (LaTeX; compiles on Overleaf)
analysis/           Python analysis environment (requirements.txt)
data/, results/raw/ generated, gitignored
```

## Getting started

**Requirements:**
- Linux, CMake ≥ 3.20, Ninja;
- a C++20 compiler (tested with GCC 16.2 and Clang 22.1);
- Python 3 for the scripts;
- `perf_event_paranoid` ≤ 2 for hardware counters.

Optional: `perf`, valgrind (cachegrind), [pmu-tools](https://github.com/andikleen/pmu-tools) (`toplev`).

```bash
# Correctness build and tests
cmake -S . -B build -G Ninja                 # RelWithChecks: -O3 with assertions
cmake --build build
ctest --test-dir build -j"$(nproc)"          # 62 tests
scripts/check.sh                             # GCC + Clang, with and without ASan/UBSan (~15 min)

# Benchmark build (the only build used for timing)
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DDC_BUILD_TESTS=OFF
cmake --build build-release
./build-release/memory_profile 20000 80000   # memory table above
./build-release/bench --list                 # available structures
```

For another machine, set `-DDC_MARCH=x86-64-v3` (the default is `native`) and record it with the results.

**Analysis environment:**
```bash
python3 -m venv .venv && .venv/bin/pip install -r analysis/requirements.txt
```

## Running experiments

**1. Get the inputs and build the traces** (about 10 minutes and 3 GB):
```bash
scripts/fetch_data.sh                        # SNAP + DIMACS, checked against traces/DATA_SHA256SUMS
python3 scripts/build_traces.py -j 8         # builds every trace in traces/specs.tsv
python3 scripts/build_traces.py --verify     # regenerates and compares hashes with the manifest
./build-release/tracegen info data/traces/<name>.dct
```

**2. Prepare the machine** (resets on reboot):
```bash
sudo cpupower frequency-set -g performance
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo   # Intel; AMD: disable boost
```
Then plug in to AC power and close other heavy applications.

**3. Run a plan.** Plans: `pilot`, `rq1_rq2`, `rq3c`, `rq3_p1`, `rq3_p3`, `rq3b`, `rq5_work`.
```bash
python3 scripts/run_sweep.py --list
python3 scripts/run_sweep.py --plan pilot --dry-run
systemd-inhibit --what=sleep:idle:handle-lid-switch \
  nohup python3 scripts/run_sweep.py --plan pilot >> results/raw/pilot_progress.log 2>&1 &
```
- Jobs run one at a time on one pinned core.
- A plan **resumes** where it stopped. Interrupted jobs, and jobs that saw battery power, are moved to a quarantine file and rerun.
- Confirmatory plans stay locked until the pilot has fixed the repetition count.
- To run a single structure by hand:
```bash
taskset -c 2 ./build-release/bench --structure ett_f2 --trace data/traces/<name>.dct \
  --reps 3 --warmup 1 [--mode latency] --out results.csv
```

## Documentation

| Document | Contents |
|---|---|
| [`docs/RESEARCH_PLAN.md`](docs/RESEARCH_PLAN.md) | phases, scope, status of each item |
| [`docs/PREREGISTRATION.md`](docs/PREREGISTRATION.md) | hypotheses, decision rules, statistics, amendments |
| [`docs/RELATED_WORK.md`](docs/RELATED_WORK.md) | positioning against SIGMOD 2025, D-tree, Xu et al. |
| [`docs/IMPLEMENTATION_NOTES.md`](docs/IMPLEMENTATION_NOTES.md) | how each competitor was reimplemented, and every ambiguity resolved |
| [`docs/TRACE_FORMAT.md`](docs/TRACE_FORMAT.md) | trace format v1, generators, determinism, suite |
| [`paper/main.tex`](paper/main.tex) | paper draft (results marked `[TBD]`) |

## Roadmap

| Phase | Content | Status |
|---|---|---|
| 1–2 | First ETT, baselines, flat arena | ✅ |
| 3 | Related work, pre-registration | ✅ |
| 4 | Correctness and measurement foundation | ✅ |
| 5 | Competitors, layouts, k-hop, counters | ✅ |
| 6 | Workloads and trace suite | ✅ |
| 7 | Benchmarking: pilot, confirmatory sweeps, latency, top-down analysis, AMD replication | 🔄 in progress |
| 8 | Statistical analysis, cost model, figures | ⏳ |
| 9 | Paper and archived artifact (Zenodo DOI) | ⏳ |

## Data sources and acknowledgements

- **SNAP** (Stanford Network Analysis Project): CollegeMsg, email-Eu-core-temporal, sx-mathoverflow, com-YouTube.
- **9th DIMACS Implementation Challenge:** USA-road-d.NY road network.

The datasets are downloaded by `scripts/fetch_data.sh`, not redistributed. Please cite their original sources.

Related implementations consulted for cross-checking (no code copied):
- [qingchen3/D-tree](https://github.com/qingchen3/D-tree) and [qingchen3/Imp_bench_dyn](https://github.com/qingchen3/Imp_bench_dyn);
- [tomtseng/dynamic-connectivity-hdt](https://github.com/tomtseng/dynamic-connectivity-hdt) (MIT).
