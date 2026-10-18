# Pre-registration: Microarchitecture-Aware Study of Fully Dynamic Connectivity

| | |
|---|---|
| Status | **registered** |
| Draft date | 2026-10-16 |
| Registration | After review, the author sets Status to *Registered* and commits. That commit's SHA and timestamp are the registration record. It must precede the first measured sweep (Phase 7). |
| Plan | `docs/RESEARCH_PLAN.md` (revision 3); related work in `docs/RELATED_WORK.md` |
| Authorship of hypotheses | RQ1's prediction is the author's own. The hypotheses and thresholds for RQ2–RQ5 were drafted with AI assistance (Claude), at the author's request, from theory and the published literature, and adopted by the author before any measurement. |

This document records, before any measured experiment, what we expect to find, how we will measure it, and how we will decide whether each expectation held.
After registration it changes only through dated entries in **§6 Amendments**.

*Confirmatory* tests are the ones listed in **§5 Confirmatory test registry**. Every other analysis is *exploratory*: reported with confidence intervals, never described as "significant", and never used to support or refute a hypothesis.

---

## 1. Observations made before registration

These results were already known when this document was written. They are **not** predictions, and no confirmatory test relies on them.

1. **Memory footprint, P0 vs. F0** (Phase 4, `memory_profile`, insert-only workloads, glibc malloc):

   | Workload | P0 (pointer) | F0 (arena) | F0 saving |
   |---|---|---|---|
   | V = 500, 2,000 edges | 317.5 KiB | 308.3 KiB | ≈ 2.9% |
   | V = 20,000, 80,000 edges | 12,507 KiB | 12,121 KiB | ≈ 3.1% |

   In both, treap nodes are only about 23% of the ETT's heap bytes. The non-tree adjacency sets (≈ 57%) and the tree-edge map (≈ 20%) dominate.
   After Phase 5's layout ablation (2026-10-17, V = 20,000, 80,000 edges, insert-only), the measured heap bytes per edge were:

   | Structure | P0 | F0 | F1 | F2 |
   |---|---|---|---|---|
   | ETT | 153.4 | 148.4 | 129.6 | 115.6 |
   | D-tree | 117.2 | – | – | 87.5 |
   | HKS | 166.4 | – | – | 116.5 |
   | HDT | 581.4 | – | – | 470.0 |

   Also measured: DND-trees (F2) 94.6; BFS 83.6; eager/lazy DSU 41.3; CachedBFS 84.6.
   HDT's F2 number uses the uniform arena policy (reserve the V vertex nodes, grow geometrically). An earlier fixed 4V reservation per level made HDT's F2 *larger* than P0 (1,142.7 B/edge); that artifact was fixed before any timing.
   These numbers use Phase 5's containers (swap-remove ordered sets instead of `unordered_set`). Memory is not a hypothesis in this study; it is reported descriptively.
2. **Correctness:** all structures agree with an independent oracle in the randomized differential tests (V ∈ {2, 8, 30, 200, 2000}, 5 op mixes, 100 seeds each). Pointer and arena ETTs build identical treaps after every operation.
3. **No runtime or hardware-counter measurements** of any structure have been taken.
4. **SIGMOD 2025** (Chen, Böhlen & Helmer) results are known from the paper. They are used for RQ3b and summarized in `docs/RELATED_WORK.md`.

---

## 2. Methodology common to all research questions

### 2.1 Seeds and repetitions
- Every structure replays **identical binary traces** (Phase 6 format, identified by SHA-256).
- **12 trace seeds** per confirmatory scenario (seeds 1–12).
  - Twelve seeds let the Wilcoxon test reach p ≈ 2.4 × 10⁻⁴. That is below the strictest Holm threshold in §5 (0.05/18 ≈ 0.0028).
- **3 treap seeds** per trace seed for ETT-based structures. Their variance is reported separately from the trace-seed variance (exploratory). Structures without treap randomness run once per trace seed.
- **Repetitions:** set by the pilot (§2.5), at least 3 and at most 10 per (trace seed, treap seed). Each process starts with **1 warm-up repetition**, which is discarded.
- The initial graph build is timed separately from the measured churn phase.

### 2.2 Primary metric and environment
- **Mean nanoseconds per operation over the measured churn phase**, wall-clock `steady_clock`.
- Where a test names an operation type (e.g., "delete time"), the metric is the mean latency of that type, from the per-operation latency log.
- Each run uses one pinned P-core (CPU 2, sibling CPU 3 idle), turbo off, the `performance` governor, and one structure per process.
- The `Release` build, the `-march` value, the compiler and the git commit are recorded with every result.

### 2.3 The shared comparison rule
For each trace seed *s*, the **speedup** of A over B is S_s = t_B(s) / t_A(s). Each *t* is the median over repetitions, then over treap seeds.

- **A beats B** if both:
  1. **Size:** the median of S_s over seeds is > **1.05** (at least 5% faster); and
  2. **Reliability:** a one-sided **Wilcoxon signed-rank** test on log S_s gives a p-value below its Holm-adjusted level (§5).
- **A and B are equivalent** if, by one-sided Wilcoxon tests on log S_s − log 1.05 and log S_s + log 1.05, the speedup is shown to lie within [1/1.05, 1.05] at the adjusted level.
- The threshold 1.05 is the **smallest effect of practical interest**. It is a statement about what is worth reporting, not a prediction.
- Every comparison also reports the median S, a **paired bootstrap 95% CI** (BCa, 10,000 resamples over trace seeds), and the matched-pairs rank-biserial effect size.
- When two scenarios are compared, they are paired by trace seed. Scenarios that differ only in their queries share the identical update stream for a given seed (updates and queries draw from separate random streams derived from the trace seed).

### 2.4 Multiple comparisons
- Each confirmatory test in §5 contributes one p-value, and the whole family is corrected with **Holm–Bonferroni** at α = 0.05.
- A test whose claim requires *several* comparisons to all pass ("A beats every other structure") uses the intersection–union principle: its p-value is the largest of its component p-values, with no further internal correction.

### 2.5 Pilot and stopping rule
- **Pilot:** trace seeds 9001–9003 (disjoint from the confirmatory seeds), V ∈ {10⁴, 10⁵}, balanced mix. Every structure is run in isolation.
- The pilot analysis computes only each structure's run-to-run coefficient of variation (CV). It does not compare structures.
- **Repetitions** r = max(3, ⌈(CV / 0.01)²⌉), capped at 10. This targets a 1% standard error for each per-seed estimate. A capped value is noted as an amendment.
- The pilot's chosen r is recorded as an amendment **before** confirmatory runs. Pilot data is never used in confirmatory tests. Seed counts are fixed in advance, and no runs are added or stopped based on results.
- *Disclosure:* pilot timings will inevitably be visible to the author. This is why every hypothesis is fixed here, before the pilot.

### 2.6 Run cap (DNF)
- A run that exceeds **10 minutes** of wall-clock time is stopped and recorded as **DNF**, with the operations completed and the elapsed time. No runtime is ever imputed.
- A structure with a DNF on any seed in a scenario **cannot be declared that scenario's winner**.
- **A finished, B did not:** B's time is at least the cap, so the lower bound S_s ≥ cap / t_A(s) is used. This can only *understate* A's advantage.
- **Both DNF on a seed:** that seed counts as S_s = 1 (a tie).
- A test with fewer than 6 of its 12 seeds usable is **inconclusive**.
- DNF counts are reported per structure and scenario.

### 2.7 Exclusions
A run is discarded and rerun, with the exclusion logged, only if its recorded environment shows:
- a governor other than `performance`;
- turbo enabled;
- wrong core pinning;
- other logged interference;
- or a failed correctness check (all answers must match the trace's stored ground truth).

No other outlier removal.

### 2.8 Replication on a second machine
The confirmatory scenarios are rerun on the AMD machine (Phase 7.6).
- **Replicated:** the same direction, and the 95% CI excludes 1 in that direction.
- **Not replicated:** otherwise.

Both outcomes are reported. Replication status is descriptive and not part of the Holm family.

### 2.9 Standard workload definitions
- **ER graph:** G(n, m) with n = V; m edges drawn uniformly without replacement.
- **Random geometric graph (RGG):** V points uniform on the unit torus. *Potential edges* are pairs within distance r, with r chosen so that the expected degree is the stated value.
- **Churn stream (steady state):** starting from the initial graph, 10⁶ measured operations at the stated mix. The edge count is held at the initial count (density control).
  - A delete picks a uniform present edge.
  - An insert picks a uniform absent potential edge (any pair for ER; pairs within r for RGG).
- **Mixes** (query / insert / delete): read-heavy 80/10/10, balanced 34/33/33, churn-heavy 10/45/45.
- **u_r workload (SIGMOD 2025 design):** start empty, insert the target graph's edges in uniformly random order, and after every u_r insertions delete one uniformly random present edge.
- **Query types:**
  - **Uniform:** u, v uniform over all vertices, u ≠ v.
  - **Local (h = 2):** u uniform among vertices with at least one edge; v uniform among the vertices at distance 1 or 2 from u in the current graph.
  - **Cross-component:** u uniform; v uniform among vertices outside u's current component. Redraw u up to 64 times; if that fails, emit a uniform query and count it as a fallback.
- Every query's correct answer is stored in the trace. Each trace reports its fraction of true answers and its fallback count.

### 2.10 Layout definitions across algorithms
- **P0:** per-node heap allocation with raw pointers, plus standard containers (`std::map` / `std::unordered_set`) for edge lookup and adjacency.
- **F2:** all node records in contiguous arrays with 32-bit indices (no per-node allocation); edge lookup via **one shared** flat open-addressing hash; adjacency via **one shared** flat per-vertex vector type.
- These definitions apply identically to ETT, HKS, HDT and D-tree. The same container implementations, compiler and flags are used for all, with no algorithm-specific tuning beyond this list.
- Each algorithm's P0 and F2 variants must pass a **behavior-identity test** (identical internal structure after every operation). That requires a fixed, identical iteration order in both.
- Each algorithm's concrete P0/F2 mapping is written down in Phase 5 and recorded as an amendment before any measurement.

---

## 3. Research questions and hypotheses

### RQ1: Does memory layout improve one algorithm?

**Question:** with the ETT algorithm held fixed, is the flat arena (F0) faster than separately allocated pointer nodes (P0)?

**Author's prediction:** F0 will be faster than P0, especially on larger graphs, because arena storage reduces allocation overhead and improves memory locality.

**Hypotheses:**
- **H1a (large graphs):** in both large cells, F0 beats P0 (§2.3).
- **H1b (size trend):** F0's advantage grows with size. The per-seed log speedup at large-2 exceeds that at small (one-sided Wilcoxon on the paired differences), and the median speedup ratio large-2 / small is > 1.05.

**Cells:**

| Cell | V | 3V treap nodes (32 B arena / 48 B pointer) | Used for |
|---|---|---|---|
| small | 10³ | within L2 | H1b |
| large-1 | 10⁵ | beyond L2, within L3 | H1a |
| large-2 | 10⁶ | beyond L3 | H1a, H1b |

Workload in all cells: ER with m = 2V (average degree 4), balanced mix, uniform queries, 10⁶ measured operations.

**Outcomes:**
- H1a is **supported** if F0 beats P0 in both large cells, and **refuted** if P0 and F0 are equivalent, or P0 beats F0, in either large cell. Otherwise inconclusive.
- H1b is **supported** or **not supported** by its rule above.

**Secondary (exploratory):** LLC/L1d/dTLB misses per op, IPC, separate speedups per op type, and memory (already known to differ by about 3%, §1).

**Rationale:** the arena stores all treap nodes contiguously, with 32-bit links (32 B vs. 48 B per node with allocator overhead), and avoids per-node allocator calls. The benefit should show once nodes no longer fit in cache.

**Known risk:** F0 changes only the treap nodes. The edge map and adjacency sets, which dominate memory (§1), are identical in P0 and F0. That may keep the effect small, which is what RQ1 measures.

---

### RQ2: Can memory layout change which algorithm wins?

**Question:** does P0 → F2 benefit ETT, HDT and D-tree by different amounts?

**Hypothesis H2a:** the P0 → F2 speedup is **not the same** for all three algorithms. No direction is predicted, and the tests are two-sided.

**Test:** for each pair of algorithms (A, B) and each RQ1 cell (small, large-1, large-2; same workload as RQ1), take the per-seed ratio of speedups G_s = S_A(s) / S_B(s).
- That pair-cell is **different** if the median G is outside [1/1.05, 1.05] and a two-sided Wilcoxon test on log G_s passes its Holm-adjusted level.
- It is **equal** if equivalence holds (§2.3).
- Otherwise it is **inconclusive**.

**Outcomes:**
- H2a is **supported** if at least one pair-cell is *different*.
- It is **refuted** if all pair-cells are *equal* (by the intersection–union principle).
- Otherwise it is **inconclusive**.

There are 3 pairs × 3 cells = 9 tests. **If HDT is dropped** (plan fallback), this becomes ETT vs. D-tree only, 3 tests.

**Exploratory:** ranking flips (A beats B under P0 and B beats A under F2) are counted and reported, but not predicted.

**Rationale:** the three algorithms have different node structures and access patterns, so a layout change that helps one need not help another equally.

---

### RQ3: Which structure wins under different workloads?

**Question:** does the best structure depend on the query fraction and on how queries are distributed?

Each algorithm competes in its **F2** layout. Baselines (BFS, eager DSU, LazyDSU, CachedBFS) compete as implemented. BFS uses an epoch-stamped visited array, so a query costs only what it visits.

**Hypotheses:**
- **P1 (crossover):** at V = 10⁵, the winner under the read-heavy mix differs from the winner under the churn-heavy mix.
- **P3 (locality helps BFS):** at V = 10⁴ with the balanced mix, BFS's standing relative to ETT improves with local queries. With R = t_ETT / t_BFS per scenario and D_s = R_local(s) / R_uniform(s), the median D is > 1.05 and a one-sided Wilcoxon test on log D_s passes.

**Winner rule (for P1):** the candidate (lowest median time) is the scenario's **winner** if it beats every other structure (§2.3; intersection–union, so the test's p-value is the largest component p-value). Otherwise that scenario has no clear winner.

**Outcomes:**
- **P1** is **supported** if both scenarios have a clear winner and the two differ; **refuted** if the same structure is the clear winner in both; otherwise **inconclusive**.
- **P3** is **supported** by its rule, **refuted** if D is significantly below 1, and otherwise **not supported**.

**Confirmatory scenarios:**

| # | Mix | Queries | V | Used by |
|---|---|---|---|---|
| C1 | read-heavy | uniform | 10⁵ | P1 |
| C2 | churn-heavy | uniform | 10⁵ | P1 |
| C3 | balanced | uniform | 10⁴ | P3 |
| C4 | balanced | local (h = 2) | 10⁴ | P3 |

All four use an ER graph with m = 2V and 10⁶ measured operations. C3 and C4 share their update streams.

**Exploratory:** the full crossover map (V × query fraction × query distribution), including balanced/uniform, local and cross-component at 10⁵, and cross-component at 10⁴.

**Rationale:**
- Update-cheap and query-cheap structures should trade places as the query fraction changes.
- BFS stops as soon as it reaches the target, so nearby pairs are cheap for it, while ETT's query cost doesn't depend on distance.
- P3 is tested at 10⁴ because at 10⁵, uniform BFS runs are expected to hit the cap (a rough estimate, not a measurement), and the cap rather than BFS's behavior would then decide the result.

#### RQ3b: Do SIGMOD 2025's findings replicate in native code?

Only directions are predicted. The magnitudes they report (e.g., 1335×) are compared descriptively, since Python and C++ magnitudes are not expected to match.

- **B1:** on the path graph (V = 10⁵, u_r = 1000), HKS beats HDT in mean delete time. *Their finding:* HDT is 1335× slower than HKS on the path graph at u_r = 1000, because HDT pushes down every node of the smaller path. This is algorithmic work, which native code does not remove. *Dropped if HDT is dropped.*
- **B2:** on an ER graph (V = 10⁵, u_r = 1000), D-tree beats HKS in mean delete time. *Their finding:* D-tree has the fastest deletes on all datasets except the large-diameter ones (path, USA road).

**Outcomes:** each is **replicated** if the stated structure beats the other (§2.3), **contradicted** if the reverse holds, and otherwise **inconclusive**.

The ER graph's density follows their generator in `Imp_bench_dyn`, read before measurement and recorded as an amendment.

**Exploratory:** their other findings (rows F1–F9 in `docs/RELATED_WORK.md`), the USA road network, and other u_r values.

#### RQ3c: Is a dynamic structure worth it on small graphs?

**H3c:** at V = 10³ (balanced mix, uniform queries, ER with m = 2V; the RQ1 small cell), **no dynamic structure is more than 5% faster than the best baseline**.

**Test:** let B* be the baseline with the lowest median time. For every dynamic structure D, a one-sided Wilcoxon test shows that D's speedup over B* is below 1.05 (a non-inferiority test for B*; intersection–union over D).

**Outcomes:**
- **Supported** if all of those tests pass.
- **Refuted** if any dynamic structure beats B* (§2.3).
- Otherwise **inconclusive**.

**Rationale:** SIGMOD 2025 excludes small components because they "can be done with brute-force approaches". H3c tests that assumption directly. At V = 10³ the whole graph fits in L2, so simple baselines pay little for their worse asymptotics. Constellation-sized graphs (≈ 1,584 vertices) are close to this size; LEO traces are exploratory.

---

### RQ4: Do hardware and operation counters explain runtime better than asymptotics?

**Framing:** the counter model is fitted on counters measured in the runs themselves. So it tests whether hardware behavior and operation counts **explain** runtime, not whether runtime can be forecast without running.

**Models:**
- **Asymptotic baseline**, per structure: ns/op ≈ Σ_type f_type · (a_type + b_type · log₂ V), where f_type is the workload's fraction of each operation type. The op mix is included so the baseline is not a strawman.
- **Counter model:** ns/op ≈ a + b·(`get_root` steps/op) + c·(LLC misses/op) + d·(edges scanned/op).

Both are fitted on the synthetic workloads (ER, BA, RGG) and evaluated on the held-out SNAP sliding-window and LEO workloads.

**H4:** on the held-out cells, the counter model:
1. has a median absolute percentage error at least **10 percentage points lower** than the baseline's; and
2. identifies the fastest structure in **≥ 70%** of held-out cells, and in at least as many as the baseline does.

**Outcomes:** **supported** if both hold; otherwise **not supported**. This is a criterion-based evaluation, reported with bootstrap CIs over held-out cells, and is **not** part of the Holm family.

**Rationale:** one structure may do more operations with cheap memory accesses, while another does fewer but stalls on memory. We expect counters to capture this, and asymptotic bounds not to. The 70% and 10-point thresholds are moderate: well above chance, without assuming a near-perfect model.

---

### RQ5: Does a bounded k-hop search speed up finding a replacement edge?

**Setup:**
- V = 10⁴, balanced mix with uniform queries, average degree 8.
- RGG (clustered) vs. ER with the same edge count (no clustering).
- The confirmatory k is **2**; k = 1 and k = 3 are exploratory.
- The hit rate is the fraction of tree-edge deletes whose replacement the bounded search finds (deletes with no replacement count as misses).

**Hypotheses:**
- **H5a:** the k = 2 hit rate is higher on RGG than on ER. The difference in per-seed hit rates (median) is ≥ **10 percentage points**, and a one-sided **Mann–Whitney U** test passes. The two graph families are independent samples, so a paired test does not apply.
- **H5b:** on RGG, the heuristic reduces mean edges scanned per tree-edge delete by ≥ **20%** compared with the full search alone, on the same traces (paired one-sided Wilcoxon on the per-seed ratio).

**Outcomes:** each is **supported** by its rule. H5a is **refuted** if ER's hit rate is significantly higher. Otherwise each is not supported.

**Exploratory:** delete latency (median, p99), total runtime, and hit rates at k = 1 and 3.

**Rationale:**
- RGGs are highly clustered (a 2-D RGG's clustering coefficient is about 0.59), so a cut edge often has a short detour nearby. ER at this density has almost none.
- H5b measures *work*, which is deterministic given a trace, rather than time. Whether less work also means less time is left exploratory, because a failed bounded search adds overhead.
- A 20% reduction is a modest, practically meaningful bar.

Correctness is a precondition (the differential tests must pass), not an outcome.

---

## 4. Contingencies
- **HDT dropped** (decided at the end of Phase 5): H2a becomes ETT vs. D-tree (3 tests), B1 is removed, and the Holm family shrinks accordingly. This is recorded as an amendment.
- **A structure is not implemented in time** (Xu et al., HKS): it is removed from the RQ3 comparisons, and this is recorded as an amendment *before* confirmatory runs. A structure is never removed after its confirmatory results have been seen.

---

## 5. Confirmatory test registry (the Holm family)

| ID | Test | Scenario(s) | Component comparisons |
|---|---|---|---|
| T1 | H1a: F0 beats P0 | RQ1 large-1, large-2 | 2 (intersection–union) |
| T2 | H1b: speedup grows with size | RQ1 small vs. large-2 | 1 |
| T3–T11 | H2a: pair-cell differences | 3 pairs × 3 RQ1 cells | 1 each |
| T12 | P1: crossover | C1, C2 | winner rule in each (intersection–union) |
| T13 | P3: locality helps BFS | C3, C4 | 1 |
| T14 | B1: HKS beats HDT (deletes, path) | RQ3b path | 1 |
| T15 | B2: D-tree beats HKS (deletes, ER) | RQ3b ER | 1 |
| T16 | H3c: no dynamic structure > 5% faster than the best baseline | RQ1 small | intersection–union over dynamic structures |
| T17 | H5a: RGG hit rate > ER | RQ5 | 1 |
| T18 | H5b: ≥ 20% less work on RGG | RQ5 | 1 |

18 tests (13 if HDT is dropped). The strictest Holm level is 0.05/18 ≈ 0.0028. **H4 (RQ4) is criterion-based and outside this family.**

---

## 6. Amendments

*After registration, every change is a dated entry here: what changed, why, and whether any confirmatory data had been seen at the time.*

### A1 (2026-10-18): RGG churn definition (§2.9)
- **Problem:** as written, the initial RGG graph contains *every* potential edge. Steady-state density control then forces an insert after each delete, and the only absent potential edge is the one just deleted. The "churn" would re-insert the same edge forever.
- **Change:** potential edges are the pairs within distance r', with r' chosen so the potential graph's expected degree is **2d**. The initial graph is a uniform random subset of exactly ⌊d·V/2⌉ potential edges (expected degree d, as stated). Churn then inserts uniform absent potential edges, as stated.
- **Effect on RQ5:** the initial RGG's clustering coefficient is about half that of a full RGG (≈ 0.29 instead of ≈ 0.59, since keeping each edge with probability ½ keeps a triangle with probability ⅛ and a two-path with probability ¼). ER at the same density stays ≈ 0. H5a, H5b and their thresholds are unchanged.
- **Data seen:** none. No workload had been generated and nothing had been measured.

### A2 (2026-10-18): u_r workload details for RQ3b (§2.9, §3 RQ3b)
- **Change:**
  - **Insertion order** follows SIGMOD 2025's generator (`Imp_bench_dyn/utils/graph_generators.py`, read 2026-10-18): edges in the order their dataset files list them. That is generation order for G(n, m), and **sequential order (0–1, 1–2, …) for the path graph**. This replaces "uniformly random order".
  - **ER density:** m = 10·V (their `generate_random_graph(m // 10, m)`, average degree 20). For B2 at V = 10⁵ that is m = 10⁶; the path graph for B1 at V = 10⁵ has m = V − 1.
  - **Deletes** pick a uniform present edge after every u_r inserts, as before.
  - **Checkpoint query protocol** (exploratory, for comparability only): 100 evenly spaced testing points, each with 10,000 uniform queries, updates suspended. They used 50 M queries per point, which is not feasible here; the count is stated in the paper.
- **Reason:** replication fidelity. B1 concerns the path graph, where insertion order shapes every structure's trees. §3 RQ3b already says the ER density "follows their generator … recorded as an amendment".
- **Data seen:** none.

### A3 (2026-10-18): Clarifications to the query definitions (§2.9; no change in meaning)
- **Local (h = 2):** u is drawn by rejection among vertices with degree ≥ 1, up to 64 draws; then the query becomes a uniform fallback, which is counted.
- **Cross-component:** v is drawn by rejection (uniform among vertices outside u's component) with at most 4,096 draws per u, and u is redrawn up to 64 times; then a uniform fallback, counted.
- **Ground truth:** every query's answer is computed during generation by two independent structures (DND-trees and EttF2), which must agree, and stored in the trace.
- **Data seen:** none.
