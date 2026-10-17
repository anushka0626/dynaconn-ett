# Related Work & Positioning

*Phase 3.1, finalized 2026-10-17. The SIGMOD 2025 facts below were checked against the arXiv v2 text (2501.02278v2); quotes are verbatim.
The stance column is aligned with `docs/PREREGISTRATION.md`. "Confirmatory" rows cite a registered test ID (§5 there); every other row is exploratory or out of scope. Stances were drafted with AI assistance and adopted by the author.*

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

| # | Their finding | Our stance | Our experiment |
|---|---|---|---|
| F1 | HK uses up to 85× the D-tree's memory. Only the D-tree fits Semantic Scholar | **Extend (exploratory):** measured C++ bytes/edge for every structure and layout. Does a flat layout narrow the ET-tree memory gap? Semantic Scholar is out of scope (15 GB RAM) | `memory_profile`, ablations P0 → F2, fig. 7 |
| F2 | HKS ≈ HDT for deletes ("HDT optimizations do not pay off"). HDT up to 1335× slower than HKS on the path graph (u_r = 1000) | **Replication (confirmatory, direction only):** HKS beats HDT on path-graph deletes. The 1335× magnitude is compared descriptively. Random graphs exploratory. It motivates our levelless ETT | **T14 (B1)**; fig. 9–10 |
| F3 | Better amortized bounds lead to slower runtimes (lazy local trees slowest; ST beats LT/LzT) | **Out of core scope:** ST/LT/LzT are optional. The general claim (bounds vs. runtime) is addressed by RQ4's counter model over the structures we build | H4 (criterion-based) |
| F4 | D-tree is fastest for deletes on small-diameter graphs but degrades on the path graph and road networks | **Replication (confirmatory, direction only):** D-tree beats HKS on ER-graph deletes. Degradation on path, road and LEO grids is exploratory, as is whether layout changes it | **T15 (B2)**; RQ2 exploratory |
| F5 | ST is best for queries (height ≤ log n) | **Extend:** ST is optional, so no direct test. We add interleaved streams and query distributions, which they never varied | **T12 (P1)**, **T13 (P3)** |
| F6 | Lesson 1: no standardized benchmark exists; one should be built | **Respond (contribution):** versioned trace format plus a released suite, with converters to their format | Plan 6.1 |
| F7 | Lesson 2: recommends strengthening unbalanced spanning trees against worst-case degradation | **Out of scope** (parking lot if RQ2 points at a layout-based fix) | (none) |
| F8 | Lesson 3: level partitioning is expensive in practice | **Confirmatory via B1** (path graph). HDT vs. our levelless ETT on random and adversarial graphs is exploratory | **T14 (B1)**; fig. 9 |
| F9 | Lesson 4: computing vs. materializing tree attributes (size, rank, bitmap…) is a key cost | **Extend (confirmatory):** the layout ablations measure the cost of *how* attributes are stored | **T1–T2 (H1a/H1b)**, **T3–T11 (H2a)** |

### Gaps they leave (our contribution)
1. Native code and hardware: cache misses, IPC, top-down stall analysis, layout.
2. Interleaved read/write streams, and the crossover map over query fraction × V.
3. Tail latency (they report averages only).
4. Simple baselines (BFS, DSU, lazy DSU) and Xu et al.'s DSU hybrid.
5. The small and medium regime (cache-resident, LEO-sized).
6. Statistics: CIs, effect sizes, seed-variance decomposition, a pre-registered design.

### Draft contributions list (for the introduction)
None of SIGMOD 2025, D-tree or Xu et al. measure any of the following.
1. The first **native-code, microarchitecture-level** comparison of fully dynamic connectivity structures: cache misses, IPC, top-down stalls, and behavior across the cache hierarchy.
2. **Layout ablations applied across algorithms** (P0 → F2 on ETT, HDT, D-tree), testing whether layout changes algorithm rankings.
3. A **crossover map** over interleaved read/write streams, query fraction, query distribution and graph size, with tail latency, including simple baselines and Xu et al.'s DSU hybrid.
4. A **replication in C++** of SIGMOD 2025's directional findings (B1, B2).
5. A **pre-registered** design with paired statistics, plus a versioned, hashed **trace benchmark** answering SIGMOD 2025's call for one.

## Other papers

- **D-tree** (Chen, Lachish, Helmer, Böhlen, PVLDB 15(11) 2022). Implementation notes are in `docs/IMPLEMENTATION_NOTES.md`. **Finding:** the published query (Alg. 2) and the authors' `query` can answer "not connected" for a connected pair when both endpoint walks reroot the same tree. Reproduced in 30 of 100 seeds at V = 8. Their timings are unaffected, since answers are discarded and the sanity checks use a non-rerooting query. Report neutrally, and contact the authors first. Their own evaluation is Python, 10 runs. Deletions use a fixed survival time (an edge is deleted t_de after its last insertion: 14 days, or 5 years for Semantic Scholar), and queries run at 100 testing points (all pairs for graphs under 10K vertices, else 50 M uniform pairs). The reference code (`qingchen3/D-tree`) is Python with no license; it is used only to cross-check outputs.
- **Xu et al.** (SIGMOD 2024, DOI 10.1145/3698805; extended arXiv 2601.17285). The **ID-tree** (a lighter D-tree with early-exit replacement search) plus a disjoint-set **DS-tree** gives **DND-trees**, with near-constant queries. Implementation notes are in `docs/IMPLEMENTATION_NOTES.md`. Not compared in SIGMOD 2025, so including it is part of our contribution.
- **Alberts, Cattaneo & Italiano** (JEA 1997): *An empirical study of dynamic graph algorithms*. SIGMOD 2025 cites it (their [11]) as the source of **HKS**, the simplified, levelless Henzinger–King. This is the ET-tree lineage our ETT belongs to. Its exact sampling rule must be read before implementing HKS (open item).
- **Iyer, Karger, Rahul & Thorup** (JEA 2001): experimental study of polylogarithmic fully dynamic connectivity (ET-trees, HDT, heuristics). It is related work for the ET-tree lineage; it is *not* SIGMOD 2025's cited source for HKS (that is Alberts et al. 1997).
- **Tseng, Dhulipala & Blelloch** (ALENEX 2019): ET-trees on skip lists. Relevant to the sequence-structure ablation.
- **Hanauer, Henzinger & Schulz** (ACM JEA 27, 2022): survey; mine it for other empirical studies.
