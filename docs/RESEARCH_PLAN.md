# Research & Implementation Plan

*Revision 3 (2026-10-16). Adds findings from reading SIGMOD 2025 (verified against the arXiv v2 text; details in `docs/RELATED_WORK.md`). Revision 2 reframed the plan after the related-work review; revision 1 dated from 2026-10-12.*

This plan picks up after Phase 2 (pointer + flat dynamic-graph ETT, memory profiling) and runs through to the paper.
Each phase ends with **exit criteria**, and the next phase depends on them.
Don't run any measured sweep (Phase 7) until Phase 4's correctness criteria pass and the pre-registration (Phase 3) is committed.

## Scope (frozen 2026-10-16)

The scope is **Phases 3–9 below, aimed at the six gaps** in `docs/RELATED_WORK.md`. Nothing new is added. New ideas go to the *Parking lot* at the end of this file and become future work in the paper. Moving an item between tiers needs a dated note here.

| Gap | Covered by |
|---|---|
| G1. Native code & hardware (layout, counters, top-down stalls) | 4.1 template, 5.3 ablations, 7.3 counters/TMA/cachegrind, RQ1, RQ2, RQ4 |
| G2. Interleaved read/write streams & crossover map | 6.3 op-mix sweep, 6.4 query distributions, fig. 1, RQ3 |
| G3. Tail latency | 7.2 per-op latency histograms, fig. 6 |
| G4. Simple baselines + Xu et al. | 5.1 (BFS, DSU, LazyDSU, CachedBFS), 5.2 (Xu et al.) |
| G5. Small & medium regime | 7.5 cache-boundary size sweep, LEO traces, RQ3c |
| G6. Statistics & rigor | 3.3 pre-registration, 7.4 nested seeds/pilot/bias controls, Phase 8 stats |

**Core (committed):**
- Phase 3: RELATED_WORK confirmation, pre-registration.
- Phase 4: entirely core.
- Structures: ETT (ours), HKS, D-tree, Xu et al., HDT, plus the four baselines.
- Ablations P0 → F2 on ETT, D-tree, and HDT.
- k-hop search (RQ5) and `DC_STATS` counters.
- Trace format, manifest, and converters.
- Workloads: ER/BA/RGG/adversarial/path/road graphs, the op-mix sweep, query distributions, u_r compatibility, SNAP sliding windows, LEO.
- Phase 7 except energy.
- Second machine.
- Phase 8 stats, cost model, and figures 1–11.
- Paper + artifact.

**Fallback if time runs short** (decide at the end of Phase 5; record the decision here):
- Drop HDT first. RQ2 then uses ETT/HKS vs. D-tree, and the paper says so.

**Optional (only if the core is done):**
- F3 compaction;
- ST and LCT;
- sequence-structure swap (splay / skip list);
- hot/cold field split;
- RAPL energy;
- BGP traces.

## Framing

> **A microarchitecture-aware study of fully dynamic connectivity: when do memory layout and constant factors matter more than the algorithm?**

**Why this framing:** a line of database-venue work already compares dynamic-connectivity *algorithms* by wall-clock time on real graphs:
- D-tree (VLDB 2022);
- Xu et al.'s constant-time querying (SIGMOD 2024);
- Chen–Böhlen–Helmer's experimental comparison (SIGMOD 2025).

The SIGMOD 2025 benchmark (`qingchen3/Imp_bench_dyn`) and the D-tree reference code (`qingchen3/D-tree`) are **written entirely in Python** (checked 2026-10-16). Their rankings therefore include interpreter overhead and say nothing about memory layout, caches, or tail latency.
Verified from the SIGMOD 2025 text:
- everything is Python 3, with 5 repetitions ("show very similar results") and no CIs or hardware counters;
- updates are **suspended** at testing points, where 50 M uniform queries run, so there is no mixed read/write stream;
- only averages are reported;
- there are no BFS/DSU baselines, and Xu et al. 2024 is neither cited nor compared;
- small components are explicitly out of scope ("can be done with brute-force approaches and is not the focus of our paper").

**Our gap:** layout, hardware counters, behavior across the cache hierarchy, tail latency, and a full crossover map. We also ask whether those algorithm rankings still hold in optimized native code.

**Headline experiment:** apply the same layout ablations (P0 → F3) to *several* algorithms: the smaller-side ETT, HDT, and D-tree. If a flat simple ETT beats a pointer HDT, or if layout changes which algorithm wins, that is the result.

## Research questions

- **RQ1. Layout within an algorithm.** How much do the node store, edge maps, adjacency and compaction (P0 → F3) change cache misses, IPC, bytes/edge and latency for one fixed algorithm?
- **RQ2. Layout vs. algorithm (headline).** Applied across ETT, HDT and D-tree, does layout change the *ranking* of algorithms? Where on the size axis does it matter most?
- **RQ3. Crossover map.** Which structure wins as a function of query fraction × V × query distribution? Include the fair baselines and Xu et al.'s DSU + spanning-tree structure.
  - **RQ3b:** Do the SIGMOD 2025 conclusions (from Python) hold in optimized C++? Record which ones we confirm, extend or contradict. Their key claims:
    - HKS ≈ HDT for deletes, and HDT is up to 1335× slower on the path graph;
    - D-tree degrades on large-diameter graphs;
    - HK uses up to 85× the D-tree's memory;
    - ST is best for queries.
  - **RQ3c:** The small and medium regime they skip (V ≤ ~10⁵, cache-resident, LEO-sized). Is a dynamic structure worth it at all there, against BFS/DSU?
- **RQ4. Microarchitecture.** Where do asymptotics stop predicting time? Use a top-down stall breakdown (frontend / backend / memory / core bound). Can a small cost model fitted on our counters *predict* the winner on held-out workloads?
- **RQ5. Locality.** How often does a bounded k-hop search find the replacement edge, and what does that save?

---

## Phase 3: Positioning & pre-registration (before more code)

*Goal: know exactly what's new before building, and commit to hypotheses before measuring.*

**3.1 Read the SIGMOD 2025 paper in full.** A first draft of `docs/RELATED_WORK.md` was made from the verified text on 2026-10-16. The author still has to read the paper and confirm each row. (arXiv 2501.02278, extended version). In `docs/RELATED_WORK.md`, write a table: *their finding → we confirm / extend / contradict → which experiment of ours tests it*. Also record:
- their structure list (D-tree, ET, HDT, HK, HKS, LCT, LT/LTV, LzT, ST/STV);
- their datasets and workload generators (`datasets/`, `workloads/` in their repo), so our RQ3b runs use **the same inputs** for direct comparability;
- their metrics and their memory-footprint method.

**3.2 Read** D-tree (VLDB 2022, arXiv 2207.06887) and Xu et al. (SIGMOD 2024, DOI 10.1145/3698805; extended arXiv 2601.17285, which adds 2-edge connectivity). Write implementation notes for both. These algorithms get reimplemented in Phase 5.

**3.3 Pre-registration (`docs/PREREGISTRATION.md`).** For each RQ write:
- the hypothesis and its expected direction;
- the metric;
- the decision criterion (e.g., "flat beats pointer if median speedup > 1.05 with 95% CI excluding 1");
- the planned analysis.

Commit it; the git timestamp is the registration date. Optionally also deposit it on OSF for an external timestamp. Log later changes as dated **amendments**, never as silent edits. The paper then reports which results were surprising.

**Exit criteria:** `RELATED_WORK.md` and `PREREGISTRATION.md` are committed. The contributions list in the intro draft names at least one thing none of the three papers measure.

**Status: ✅ content complete 2026-10-17**, waiting for the author's commit, which is the registration.
- **3.1:** `RELATED_WORK.md` is finalized. Its stance column is aligned with the pre-registration, and it has a draft contributions list naming five things none of the three papers measure.
- **3.2:** `IMPLEMENTATION_NOTES.md` covers D-tree, ID-tree/DND-trees and HDT, with our resolutions of the papers' ambiguities. Open items:
  - HKS's exact sampling rule (read Alberts et al. 1997 and Henzinger–King 1999 before implementing HKS);
  - D-tree's centroid-fix choice and DND's root re-synchronization, both to be cross-checked against the reference code.
- **3.3:** `PREREGISTRATION.md` is complete: 18 confirmatory tests (T1–T18) plus the criterion-based H4.
- **Design consequence (applies to Phase 5):** every iterated neighbor or child set uses one canonical **swap-remove order** in all layouts, so the P0 and F2 variants of each algorithm behave identically (`IMPLEMENTATION_NOTES.md`).

---

## Phase 4: Correctness & measurement foundation

*Goal: every number later in the paper comes from code that is tested, measured the same way for every structure, and free of duplicated implementations.*

**4.1 Unify the treap.**
- Write one `EulerTourForest<Storage>` template with two storage policies: `PointerStorage` (`new`/`delete`, raw pointers) and `ArenaStorage` (vector pool, `int32_t` index, free list). `DynamicGraphETT` and `FlatDynamicGraphETT` become thin aliases over it.
- Keep the sequence operations (`merge`/`split`/`index`/`root`) behind one interface. This allows an optional later swap of the sequence structure: treap vs. splay tree vs. skip list (Tseng et al. use skip lists).
- *Research value:* the template guarantees that both layouts run the same algorithm with the same RNG draws, so layout differences are isolated. The paper can state this as a design property.
- Delete the forest-only `EulerTourTree` / `FlatEulerTourTree`.

**4.2 Test suite (`tests/`, `ctest`).**
- **Differential fuzz:** replay random op streams through every structure and compare every query. Cover V ∈ {2, 8, 30, 200, 2000}, ≥ 100 seeds, and all op mixes. (The prototype passed 146,617 queries with 0 mismatches.)
- **Edge cases:** self-loops, duplicate inserts, deleting absent edges, deleting then reinserting, V = 1, isolated vertices, deleting a bridge with no replacement.
- **Invariant checker** (`DC_CHECK_INVARIANTS`), run after every op in the fuzz tests:
  - treap heap property, subtree sizes, and parent/child consistency;
  - each tour is a valid Euler tour;
  - the tree-edge and non-tree-edge sets are disjoint and together form the edge set;
  - forest components equal BFS components.
- **Shape test:** pointer and flat ETTs produce identical treap shapes for the same op stream.
- **Two compilers:** run the tests under GCC 16 and Clang 22.

**4.3 Build options (CMake).**
- `DC_SANITIZE=ON` adds ASan + UBSan (verified to work with both compilers).
- `DC_BUILD_TESTS=ON`.
- A `RelWithChecks` config: `-O3` with asserts kept on.
- Pin and record `-march`. Keep a portable `-march=x86-64-v3` build for the second machine.

**4.4 One way of measuring memory.**
- Use a counting `operator new`/`delete` hook in the bench binary, plus `mallinfo2()` deltas. Measure every structure the same way and delete the hand estimates in `main.cpp`.
- Break bytes/edge down into nodes, edge maps, adjacency, and free list.
- Cross-check the numbers against the SIGMOD 2025 memory method (`evaluate_memory_footprints.py`).

**4.5 Fix `ChurnGenerator`.**
- Delete-target selection is O(E). Replace it with a vector of edges, swap-remove, and an index map.
- Make duplicate inserts an explicit option and report their rate.
- Add **target-density control**.

**Exit criteria:** `ctest` passes under ASan+UBSan, under invariant checks, and with both compilers. All memory numbers are measured.

**Status: ✅ done 2026-10-16.**
- **Code:** `EulerTourForest<Storage>` with `PointerStorage` / `ArenaStorage`; the forest-only ETTs are deleted.
- **Tests:** 39 ctest cases pass under GCC 16 and Clang 22, each plain and with ASan+UBSan (`scripts/check.sh`). They include the shape-identity test, invariants after every op, and the differential fuzz (V ∈ {2, 8, 30, 200, 2000} × 5 mixes × 100 seeds).
- **Test strength:** checked by planting three bugs (replacement search disabled, a missing parent link, asymmetric adjacency); all three were caught.
- **Generator:** portable SplitMix64 + Lemire sampling. Its stream is pinned by a golden hash that matches across both compilers.
- **Memory:** measured by `memory_profile` (tagged allocators, global hooks, mallinfo2 cross-check).

**Deviations from the text above:**
- The invariant checker is a method (`check_invariants()`) that tests call, not a `DC_CHECK_INVARIANTS` build flag. That way release code doesn't change.
- The replacement search now collects vertices in treap preorder into a reused vector, where it used to build an `unordered_set`. The answers are identical, but which replacement edge gets picked can differ from the Phase 2 code. No results depend on Phase 2.

**First measurement worth keeping** (V = 20k, E = 80k, insert-only): treap nodes are ≈ 23% of the ETT's bytes, non-tree adjacency ≈ 57%, the edge map ≈ 20%. P0 → F0 saves only ≈ 3% of total memory. This supports the ablation design, since the bookkeeping steps (F1, F2) should matter more than node layout.

---

## Phase 5: Structures, baselines, ablations

*Goal: a fair, complete field of competitors, with layout ablations applicable to more than one algorithm.*

**5.1 Fair baselines** (answers the strawman critique):
- `LazyDSU`: mark the DSU dirty on delete and rebuild on the next query.
- `CachedBFS`: lazily invalidated component labels.
- The current eager-rebuild `BaselineDSU` and plain `BaselineBFS` stay **only as naive reference points**. The paper must not present eager-rebuild DSU as "the DSU approach", because Xu et al. keep a DSU-style tree alongside a spanning tree and that is the state of the art.

**5.2 Competitor algorithms in C++** (all behind `DynamicConnectivity`, all in the differential fuzz):

| Structure | Source | Notes |
|---|---|---|
| ETT + smaller-side search (ours) | existing | No polylog worst case for deletes; say so |
| HDT | own implementation on `EulerTourForest<Storage>` | Comes in both layouts at no extra cost. Cross-check correctness and rough speed against `tomtseng/dynamic-connectivity-hdt` (C++, MIT), which can also be used as an external, unmodified baseline |
| D-tree | **reimplement from the paper** | The reference is Python with **no license**, so don't copy code. Use it to cross-check outputs on small inputs |
| Xu et al. (DSU tree + spanning tree, O(1) query) | reimplement from the paper | Check for released code first; Rust crates `dndtree`/`idtree` exist, so verify where they came from before trusting them |
| **HKS** (Henzinger–King without levels) | reimplement from the paper | SIGMOD 2025's preferred ET-tree variant. It differs from ours: HKS keeps non-tree neighbors in randomized search trees (sampling); ours scans the hash sets of the smaller side. Having both separates "ET-tree" from "replacement-search strategy" |
| Structural tree (ST) | optional | SIGMOD 2025's best for queries (height ≤ log n) |
| Link-cut-tree spanning forest | optional | Present in SIGMOD 2025's set |

HDT is still the largest item. If time runs short, the ETT and D-tree layout comparisons alone can carry RQ2.

**5.3 Layout ablations across algorithms (RQ1, RQ2).** Each step changes one thing:

| Variant | Node store | Edge map | Adjacency | Free list / order |
|---|---|---|---|---|
| P0 | pointer | `std::map` | `unordered_set` | – |
| F0 (current flat) | arena | `std::map` | `unordered_set` | LIFO |
| F1 | arena | flat open-addressing hash | `unordered_set` | LIFO |
| F2 | arena | flat hash | flat per-vertex vectors | LIFO |
| F3 | arena | flat hash | flat vectors | LIFO + **periodic compaction into tour order** (see Slotin's Eytzinger/B-tree layout chapters) |

- Apply at least P0 vs. F2 to **ETT, HDT and D-tree**. D-tree and Xu et al. are parent-pointer trees, so their storage policy is simpler than the treap's.
- Optional extras: a hot/cold field split, and swapping the sequence structure (treap / splay / skip list) via the 4.1 interface.

**5.4 k-hop replacement heuristic (RQ5).** Before the full scan, run a bounded BFS (k ∈ {1, 2, 3}) and fall back to the scan on a miss. Count hits and misses. Note that D-tree also exploits locality in its replacement search; compare against it.

**5.5 Built-in instrumentation (`DC_STATS`).** Counters for:
- replacement-search edges scanned and vertices collected;
- `get_root` steps, splits and merges;
- DSU rebuilds;
- BFS nodes visited;
- tree depth (D-tree).

These feed the cost model (RQ4).

**Exit criteria:** every structure and variant passes the Phase 4 fuzz suite, and outputs agree with the reference implementations on shared small inputs.

**Status: ✅ complete (2026-10-17).** 45 ctest cases pass under GCC and Clang, each plain and with ASan+UBSan.

| Item | State |
|---|---|
| 5.1 Baselines | ✅ BFS (epoch-stamped visits, flat adjacency), eager and lazy DSU, CachedBFS |
| 5.2 D-tree | ✅ P0 and F2, with a behavior-identity test. Follows the authors' code where it differs from the paper; the Alg. 2 query is fixed (see `IMPLEMENTATION_NOTES.md`) |
| 5.2 DND-trees (Xu et al.) | ✅ F2. The DS-root re-sync is shown necessary |
| 5.2 HKS | ✅ P0 and F2. Spec from SIGMOD 2025's code (Alberts et al. 1997 is not freely accessible). The sampling branch is exercised and oracle-checked |
| 5.2 HDT | ✅ P0 and F2, with every level checked (including the n/2^i size invariant) and level raises confirmed. The fallback is **not needed**: HDT stays in scope. Cross-check vs. `tomtseng` is still optional |
| 5.3 ETT P0 → F0 → F1 → F2 | ✅ identical treaps after every op (tested). Shared containers: `FlatHashMap`, `Std/FlatOrderedSets` (canonical swap-remove order) |
| 5.3 F3 compaction | optional tier |
| 5.4 k-hop (k = 1, 2, 3) | ✅ `EttF2KHop<k>`. Mutation-tested; one surviving mutant was equivalent and the redundant check was removed |
| 5.5 `DC_STATS` counters | ✅ consistent, and identical across layouts (tested) |

---

## Phase 6: Workloads

**6.1 Trace format (also a contribution).** Binary `{op, u, v}` traces whose header holds V, generator, params, and seed. SHA-256 recorded in a manifest. Every structure replays identical bytes, and generation time is never measured.
SIGMOD 2025 says no standardized benchmark exists and asks for one (first lesson). So:
- document and version the format (`docs/TRACE_FORMAT.md`);
- ship converters to and from their workload format;
- release the full trace suite (synthetic, u_r, temporal, LEO, BGP) as a citable benchmark.

**6.2 Initial graphs.** Include SIGMOD 2025's synthetic families (star, path, complete, random, power-law) and **large-diameter graphs**: a path graph and one DIMACS state road network. These are the worst case for D-tree and ST. Report each graph's diameter. The LEO 72 × 22 grid has moderate diameter, so D-tree behavior on constellation graphs is a small, distinctive result.
- Erdős–Rényi G(n, m) below, at, and above the giant-component threshold, plus dense;
- Barabási–Albert;
- random geometric graphs;
- **adversarial:** repeatedly cut a bridge whose small side has many non-tree edges and no replacement. This is the worst case for the naive search, where HDT should win.

**6.3 Op mix sweep.**
- Query fraction q ∈ {0, 0.05, 0.1, 0.2, …, 0.95, 1} at insert:delete 1:1 with density control.
- Named reference points 80/10/10, 34/33/33 and 10/45/45.
- Growing, shrinking and steady-state regimes.

**6.4 Query distributions** (new). Uniform random pairs in a graph with a giant component mostly return `true`, which flatters some structures. Generate:
- **uniform** pairs;
- **local** pairs (within h hops);
- **cross-component** pairs.

**Report the fraction of queries that return true** for every workload.

**6.5 Realistic and comparable traces.**
- **SIGMOD 2025 compatibility family (RQ3b):**
  - their workload design: start empty, insert edges, and delete a random existing edge after every u_r inserts, with u_r ∈ {1000, 100, 20, 5};
  - their datasets where they fit in 15 GB RAM (YouTube, and StackOverflow if it fits; Semantic Scholar is out of scope);
  - their **checkpoint query protocol** (updates suspended, uniform queries at evenly spaced testing points), *in addition to* our interleaved streams.

  Compare rankings and ratios only. Their memory numbers are Python object sizes and can't be compared with C++ bytes.
- **Sliding windows** over SNAP temporal graphs (`CollegeMsg`, `email-Eu-core-temporal`, `sx-mathoverflow`), sweeping the window W. SIGMOD 2025 argues against fixed-lifetime deletions because they tie each deletion to its insertion. Keep the windows anyway, since they model real expiring links (e.g., satellite link lifetimes), but **justify the choice explicitly** and treat u_r as the neutral family.
- **LEO constellation model:** Walker-delta, e.g. 72 × 22 = **1,584 satellites**. Inter-satellite links within a plane are stable, links between planes switch with geometry, and links near the poles drop. Report honestly that this is small enough for BFS to possibly win. "At constellation scale, X wins" is itself a useful practitioner finding. Also state that routing needs paths, not just connectivity: connectivity is a building block, not a routing solution.
- *Optional:* RouteViews / RIPE RIS BGP link up/down events.

`scripts/fetch_data.sh` downloads into `data/` and verifies checksums.

**Exit criteria:** all traces regenerate bit-for-bit, and the manifest lists every trace with its hash.

---

## Phase 7: Benchmark harness & hardware profiling

**7.1 `bench` CLI:** `--structure --trace --reps --warmup --treap-seed --out --perf`. One structure per process.

**7.2 Timing.**
- Throughput: `steady_clock` over the whole measured phase.
- Per-op latency: `rdtscp`, with the timer's overhead measured and subtracted, stored in a log-bucketed histogram. Report p50/p95/p99/p99.9/max.
- Separate the build phase from the churn phase.
- Keep a `volatile` result sink so queries can't be optimized away.

**7.3 Hardware counters.**
- A `perf_event_open` group on the **`cpu_core` PMU**: cycles, instructions, L1d/LLC load misses, LLC loads, branch misses, dTLB misses.
- **Top-down microarchitecture analysis (TMA)** via `toplev` (pmu-tools at `~/tools/pmu-tools`, verified working with Golden Cove event tables). Use level 1, then drill down on Backend_Bound → Memory_Bound. This is the main evidence for RQ4.
- **Cachegrind cross-check** (`valgrind --tool=cachegrind`) on a reduced sweep. It gives deterministic, machine-independent miss counts, which answers the "it's just your CPU" critique.
- **Energy (optional):** RAPL `power/energy-pkg/`. On this machine it needs root and measures system-wide (`sudo perf stat -a -e power/energy-pkg/`). So use runs ≥ 1 s (the counter's resolution is coarse) on an idle machine, and subtract a measured idle baseline. Report joules/op, relevant for satellites and edge devices.

**7.4 Experimental design.**
- **Two seeds, kept separate:** treap-priority seed × trace seed, in a nested design. Report the variance from each source separately.
- **Pilot study first:** measure variance at each level and choose rep counts from it (per Kalibera & Jones), rather than fixing "10 × 5" up front.
- **Measurement-bias controls** (per Mytkowicz et al.): in a subset of runs, randomize link order and environment size; check that headline conclusions don't flip.
- **Machine:** pin to P-core CPU 2 with sibling CPU 3 idle; turbo off; `performance` governor (resets on reboot); AC power; quiet desktop. Every CSV row records: commit + dirty flag, compiler/flags, CPU, governor, turbo, kernel, trace hash, both seeds, rep.

**7.5 Size sweep across the cache hierarchy.**
- Treap nodes ≈ 3V. Per P-core: L1d 48 KiB, L2 1.25 MiB; L3 18 MiB shared.
- Flat (32 B/node) boundaries: V ≈ 500 / 13k / 190k. Pointer (48 B/node): ≈ 340 / 9k / 130k (treap nodes only).
- Sweep V ∈ {10², 3·10², …, 10⁶} and draw the boundaries on the plots.

**7.6 Second machine** (the cheapest boost to external validity). Use an AMD Zen system: a bare-metal or dedicated cloud instance, not a shared VM if avoidable, since noisy neighbors distort results. Rerun at least the headline figures (crossover map, RQ2 ranking, RQ1 ablation). AMD has different PMU events and its own top-down metrics, so compare *conclusions*, not raw counter values.

**7.7 Driver.** `scripts/run_sweep.py`: can resume and writes one CSV per (structure, trace, machine).

**Exit criteria:** pilot variance is documented, and repeated runs on different days agree within the measured noise.

---

## Phase 8: Analysis & figures (`analysis/`, `.venv`)

- **Statistics:**
  - median + bootstrap 95% CI everywhere;
  - Mann–Whitney U + Cliff's δ for pairwise comparisons;
  - variance decomposition by seed type.

  Follow the Hoefler & Belli reporting rules.
- **Cost model (RQ4).** Fit `ns/op ≈ a + b·(get_root steps) + c·(LLC misses) + …` from the `DC_STATS` counters and hardware counters. **Validate on held-out workloads** (train on synthetic, test on SNAP/LEO). Report how often it predicts the correct winner.
- **Figures:**
  1. **Crossover map** (RQ3): query fraction × V, colored by winner, one panel per query distribution.
  2. **Layout × algorithm ranking** (RQ2, headline): ETT/HDT/D-tree × P0/F2, showing rank changes.
  3. Ablation P0 → F3: misses/op, IPC, ns/op (RQ1).
  4. TMA stacked bars per structure and V (RQ4).
  5. ns/op vs. V with cache boundaries, measured against the cost model.
  6. Latency CDFs (tail behavior).
  7. Bytes/edge for all structures.
  8. k-hop hit rate vs. density and clustering (RQ5).
  9. HDT vs. naive ETT: random vs. adversarial workloads.
  10. RQ3b: our C++ ranking vs. the SIGMOD 2025 ranking on the same u_r workloads and graphs (including path and road, where they report D-tree degrading and HDT 1335× slower than HKS).
  11. Second-machine replication of 1–3.
- One command regenerates every figure.

---

## Phase 9: Paper & artifact

**Wording rules for the draft:**
- Our ETT is a *levelless* ET-tree like HKS, but with a deterministic smaller-side scan instead of HKS's random sampling. Say this precisely. Cite SIGMOD 2025's HKS ≈ HDT result as motivation for going levelless, noting that their result was measured in Python.
- Never say "DSU can't handle deletes". Say an *incremental-only* DSU can't, and that DSU-with-spanning-tree designs (Xu et al.) can.
- Describe our ETT's deletion bound honestly.
- Credit SIGMOD 2025 as the closest prior work and position against it.

**Outline:**
1. Introduction
2. Background: ETT, HDT, D-tree, Xu et al.
3. Related work
4. Design: storage-policy template, ablations, k-hop
5. Methodology: workloads, machines, pre-registration, statistics, bias controls
6. Results by RQ, including the pre-registered vs. surprising results
7. Threats to validity
8. Conclusion

**Related work** (✅ = verified 2026-10-16):
- ✅ Chen, Böhlen & Helmer, *An Experimental Comparison of Tree-data Structures for Connectivity Queries on Fully-dynamic Undirected Graphs*, PACMMOD 3(1) / SIGMOD 2025, DOI 10.1145/3709660, arXiv 2501.02278. **Closest prior work.**
- ✅ Chen, Lachish, Helmer & Böhlen, *Dynamic Spanning Trees for Connectivity Queries on Fully-dynamic Undirected Graphs* (D-tree), PVLDB 15(11) 2022, DOI 10.14778/3551793.3551868.
- ✅ Xu, Wen, Qin, Li, Zhang & Lin, *Constant-time Connectivity Querying in Dynamic Graphs*, PACMMOD / SIGMOD 2024, DOI 10.1145/3698805. Extended version with 2-edge connectivity: arXiv 2601.17285 (2026).
- ✅ Hanauer, Henzinger & Schulz, *Recent Advances in Fully Dynamic Graph Algorithms – A Quick Reference Guide*, ACM JEA 27 (2022), 1–45, as SIGMOD 2025 cites it. A SAND 2022 (LIPIcs 221) version also exists.
- Iyer, Karger, Rahul & Thorup, JEA 2001.
- Alberts, Cattaneo & Italiano, JEA 1997.
- Tseng, Dhulipala & Blelloch, ALENEX 2019 (they use skip lists, not treaps).
- Acar et al. 2019.
- HDT 2001; Henzinger–King 1999; Thorup 2000.
- Theory context: Wulff-Nilsen (SODA 2013) and Huang–Huang–Kopelowitz–Pettie–Thorup (SODA 2017) on improved amortized bounds; Pătrașcu–Demaine Ω(log n) cell-probe lower bound.

Verify every unchecked item on the ACM DL or DBLP before submission.

**Threats to validity:**
- hybrid CPU (mitigated by the second machine);
- compilers (mitigated by GCC + Clang);
- glibc malloc (cross-check with jemalloc);
- how realistic synthetic workloads are;
- query-distribution bias (mitigated by 6.4);
- treap vs. other sequence structures;
- RNG seeds (mitigated by the nested design);
- measurement bias (mitigated by link-order randomization);
- timer overhead;
- cache counters being machine-specific (mitigated by cachegrind);
- reimplementations of D-tree and Xu et al. not being the authors' code (mitigated by output cross-checks, and by sharing them with the authors before submission).

**Venues:**
- **DaMoN** (Data Management on New Hardware, at SIGMOD) fits a microarchitecture-aware study very well.
- The VLDB *Experiment, Analysis & Benchmark* track, where the competitor papers live.
- ALENEX, SEA, or ACM JEA for the algorithms-engineering audience.

**Artifact:**
- `reproduce.sh` (build → traces → reduced sweep → figures);
- a tagged release archived on Zenodo with a DOI;
- aim for the ACM Available / Evaluated / Reproduced badges.

---

## Reading list

*Domain:* SIGMOD 2025 (read first, in full); D-tree; Xu et al. + extension; the Hanauer–Henzinger–Schulz survey; Iyer et al. 2001; Tseng et al. 2019; Wulff-Nilsen 2013; Huang et al. 2017; Pătrașcu–Demaine.

*Methodology* (these shape the threats-to-validity section):
- McGeoch, *A Guide to Experimental Algorithmics* (2012);
- Mytkowicz et al., *Producing Wrong Data Without Doing Anything Obviously Wrong!* (ASPLOS 2009);
- Kalibera & Jones, *Rigorous Benchmarking in Reasonable Time* (ISMM 2013);
- Hoefler & Belli, *Scientific Benchmarking of Parallel Computing Systems* (SC 2015).

*Hardware:*
- Bakhvalov, *Performance Analysis and Tuning on Modern CPUs* (TMA, perf);
- Slotin, *Algorithms for Modern Hardware* (Eytzinger/B-tree layouts, for F3);
- Drepper, *What Every Programmer Should Know About Memory*.

*Implementation:*
- Demaine MIT 6.851 L19–20;
- the cp-algorithms treap article;
- `tomtseng/dynamic-connectivity-hdt`, `tomtseng/parallel-euler-tour-tree`;
- `qingchen3/Imp_bench_dyn` and `qingchen3/D-tree` (read only; no license).

---

## Setup status (2026-10-16)

| Item | Status |
|---|---|
| GCC 16.2, Clang 22.1, CMake 4.3, Ninja | ✅ |
| ASan + UBSan (GCC and Clang) | ✅ verified |
| `perf`, user-mode counters on `cpu_core` (paranoid = 2) | ✅ |
| `toplev` / pmu-tools at `~/tools/pmu-tools` (commit 2568760) | ✅ verified; use `--single-thread` with `taskset -c 2` |
| Valgrind 3.27 (cachegrind) | ✅ |
| jemalloc 5.3 | ✅ |
| mimalloc | ❌ optional (`sudo dnf install mimalloc`) |
| Python venv `.venv/` (pinned in `analysis/requirements.txt`) | ✅ |
| Turbo off (`no_turbo = 1`) | ✅ |
| Governor `performance` | ✅ now, **resets on reboot**: `sudo cpupower frequency-set -g performance` |
| RAPL energy | ⚠️ root only, system-wide: `sudo perf stat -a -e power/energy-pkg/` |
| Second (AMD) machine | ❌ to arrange before Phase 7 |

Recreate the venv: `python3 -m venv .venv && .venv/bin/pip install -r analysis/requirements.txt`.

---

## Parking lot (out of scope; future work)

Ideas that come up after the 2026-10-16 scope freeze go here, with the date. They are not implemented in this paper.

- *(empty)*
