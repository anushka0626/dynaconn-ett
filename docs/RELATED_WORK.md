# Related Work & Positioning

*Draft, 2026-10-16. The SIGMOD 2025 facts below were checked against the arXiv v2 text (2501.02278v2); quotes are verbatim.
The "Our stance" column is **proposed**. The author confirms or changes each row after reading the paper in full. Until then, treat this as a working draft, not a final position.*

## Chen, Böhlen & Helmer, SIGMOD 2025 (closest prior work)

*An Experimental Comparison of Tree-data Structures for Connectivity Queries on Fully-dynamic Undirected Graphs*. PACMMOD 3(1), DOI 10.1145/3709660; extended version arXiv 2501.02278. Code: `qingchen3/Imp_bench_dyn` (Python, no license).

### Setup facts

| Aspect | What they did | Quote / source |
|---|---|---|
| Language | Python 3 throughout | "All data structures and algorithms were implemented in Python 3." |
| Repetitions | 5, no CIs | "We repeated all experiments for 5 times on the same machine, which show very similar results." |
| Machine | 200 GB RAM, Debian 10 | Setup section |
| Structures | D-tree, LCT, HK, HKS, HDT, ST, STV, LT, LTV, LzT | Contributions list |
| Update workload | Start empty, insert edges, delete a random existing edge after every u_r inserts; u_r ∈ {1000, 100, 20, 5} | §5.4 |
| Why not sliding windows | Fixed-lifetime deletions make deletions depend on insertions; delete-everything workloads aren't general because graphs grow | §5.4 |
| Queries | Updates **suspended** at testing points; 50 M uniform random pairs per point; average reported | "When running queries, we suspend the updates." |
| Small graphs | Out of scope | "Processing small components is fast and can be done with brute-force approaches and is not the focus of our paper." |
| Memory metric | Python object sizes | Memory section |
| Baselines | No BFS/DSU baselines. Xu et al. 2024 not cited | Reference list (their [45] is Song et al. 2024, not Xu et al.) |

### Findings → our stance

| # | Their finding | Proposed stance | Our experiment |
|---|---|---|---|
| F1 | HK uses up to 85× the D-tree's memory. Only the D-tree fits Semantic Scholar | **Extend:** measure C++ bytes/edge per layout. Does a flat arena close the ET-tree memory gap? | Phase 4.4, ablations 5.3, fig. 7 |
| F2 | HKS ≈ HDT for deletes ("HDT optimizations do not pay off"). HDT up to 1335× slower than HKS on the path graph (u_r = 1000) | **Test / confirm:** does it hold in C++, and under each layout? It motivates our levelless ETT | RQ3b, fig. 9–10 |
| F3 | Better amortized bounds lead to slower runtimes (lazy local trees slowest; ST beats LT/LzT) | **Extend:** explain *why* with stall breakdowns and counters. Scope: only if we implement ST/LT | RQ4 (optional) |
| F4 | D-tree is fastest for deletes on small-diameter graphs but degrades on the path graph and road networks | **Test:** reimplemented D-tree on path, road, and LEO grids; does a flat layout change how badly it degrades? | RQ2, 6.2 |
| F5 | ST is best for queries (height ≤ log n) | **Extend:** add query distributions (local / cross-component) and interleaved streams | 6.4, RQ3 |
| F6 | Lesson 1: no standardized benchmark exists; one should be built | **Respond:** versioned trace format plus a released suite, with converters to their format | 6.1 |
| F7 | Lesson 2: recommends strengthening unbalanced spanning trees against worst-case degradation | Out of scope unless RQ2 suggests a layout-based fix | (none) |
| F8 | Lesson 3: level partitioning is expensive in practice | **Confirm or contradict** with native HDT vs. levelless ETT/HKS | 5.2, fig. 9 |
| F9 | Lesson 4: computing vs. materializing tree attributes (size, rank, bitmap…) is a key cost | **Extend:** layout ablations measure this cost directly (hot/cold split, F0–F3) | RQ1 |

### Gaps they leave (our contribution)
1. Native code and hardware: cache misses, IPC, top-down stall analysis, layout.
2. Interleaved read/write streams, and the crossover map over query fraction × V.
3. Tail latency (they report averages only).
4. Simple baselines (BFS, DSU, lazy DSU) and Xu et al.'s DSU hybrid.
5. The small and medium regime (cache-resident, LEO-sized).
6. Statistics: CIs, effect sizes, seed-variance decomposition, a pre-registered design.

## Other papers (notes to fill in)

- **D-tree** (Chen, Lachish, Helmer, Böhlen, PVLDB 15(11) 2022). TODO: implementation notes for the C++ reimplementation. The reference code (`qingchen3/D-tree`) is Python with no license; use it only to cross-check outputs.
- **Xu et al.** (SIGMOD 2024, DOI 10.1145/3698805; arXiv 2601.17285). TODO: implementation notes. Not compared in SIGMOD 2025, so including it is part of our contribution.
- **Iyer, Karger, Rahul & Thorup** (JEA 2001): the original ET-tree experiments, and the source of the "simplified ET-tree without levels" idea that SIGMOD 2025's lesson 3 refers to. TODO: confirm.
- **Tseng, Dhulipala & Blelloch** (ALENEX 2019): ET-trees on skip lists. Relevant to the sequence-structure ablation.
- **Hanauer, Henzinger & Schulz** (ACM JEA 27, 2022): survey; mine it for other empirical studies.
