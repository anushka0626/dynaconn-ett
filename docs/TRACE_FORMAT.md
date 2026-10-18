# Trace Format and Workload Suite (v1)

This is the benchmark answer to SIGMOD 2025's call for standardized workloads (their lesson 1). Every experiment replays a **trace**: a binary file of operations with the correct answer to every query stored in it. Every result records the trace's SHA-256.

## File format (version 1)

All integers are little-endian.

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | magic `DCTRACE1` |
| 8 | 4 | format version (`1`) |
| 12 | 4 | number of vertices V (ids are `0 … V−1`) |
| 16 | 8 | trace seed |
| 24 | 8 | number of **build** records |
| 32 | 8 | number of **measured** records |
| 40 | 4 | metadata length L |
| 44 | L | metadata: `key=value` pairs separated by `;` |
| 44 + L | 12 × records | build records, then measured records |

Each **record** is 12 bytes:

| Bytes | Field |
|---|---|
| 0–3 | `u` (u32) |
| 4–7 | `v` (u32) |
| 8 | `op`: 0 insert, 1 delete, 2 query |
| 9 | `answer`: queries only, 1 if `u` and `v` are connected |
| 10–11 | `flags` (u16): bit 0 = the query is a uniform *fallback* (see below) |

**Build records** construct the initial graph and are timed separately (pre-registration §2.1). **Measured records** are the experiment.

Semantics:
- Every insert adds an absent edge, and every delete removes a present one. There are no self-loops.
- Update orientation `(u, v)` vs. `(v, u)` is random, which exercises each structure's normalization.
- Reading and writing: `include/Trace.hpp` (`read_trace`, `write_trace`, `parse_trace`).

The **metadata** holds `tracegen=<version>;gen=<command>` followed by every generator argument in sorted key order. It is enough to regenerate the trace. For file inputs it records the input file's name and SHA-256, not its local path.

## Generators (`tracegen`, `include/Workloads.hpp`)

Definitions follow `docs/PREREGISTRATION.md` §2.9 and amendments A1–A3.

| Command | Initial graph (build) | Measured stream |
|---|---|---|
| `churn --graph er` | G(n, m), m = round(deg·V/2), generation order | steady-state churn at mix Q,I,D; edge count held at m; deletes pick a uniform present edge; inserts a uniform absent pair |
| `churn --graph rgg` | **A1:** potential edges = pairs within r' on the unit torus, with r' set for expected degree 2·deg; initial = uniform random subset of round(deg·V/2) of them | as above, but inserts pick a uniform *absent potential* edge |
| `churn --graph ba` | Barabási–Albert: a seed clique of k + 1 vertices, k = deg/2 attachments ∝ degree | as for ER (uniform absent pairs; the degree distribution drifts over long runs, so this family is exploratory) |
| `ur` | none (starts empty) | **A2:** SIGMOD 2025's u_r design. Insert the graph's edges in **their generator's order** (path: sequential; G(n, m) with m = 10V: generation order; files: file order); after every u_r inserts, delete a uniform present edge. Then, at `--checkpoints` evenly spaced testing points, `--qpc` uniform queries each |
| `window` | none | a sliding window over a SNAP temporal graph: an edge is inserted at its first appearance, its expiry is pushed to t + W on every reappearance, and it is deleted at expiry. Each update is followed by queries with probability q each (geometric), so queries are a fraction q of all ops |
| `adversarial` | a clique on s vertices + one bridge + a path with random chords on the other V − s | alternately delete and reinsert the bridge (queries at fraction q). Every delete is a tree-edge cut whose smaller side has about s²/2 non-tree edges and no replacement |
| `leo` | Walker-delta constellation (default 72 × 22, 53°, 550 km): in-plane rings plus the cross-plane links up at t = 0 | each time step, cross-plane links switch with latitude (off above `--lat-cut`) and range; updates are followed by uniform queries with probability q (see `include/LeoConstellation.hpp` for the model and its caveats) |

### Query distributions (churn)

| Kind | Definition |
|---|---|
| `uniform` | u, v uniform, u ≠ v |
| `local` (h = 2) | u uniform among vertices with degree ≥ 1 (≤ 64 draws); v uniform among vertices at distance 1–2. Answer is always 1 |
| `cross` | v uniform among vertices outside u's component (≤ 4,096 draws per u, ≤ 64 values of u). Answer is always 0 |

If a local or cross query can't be drawn, the record is a **uniform fallback** with flag bit 0 set. `tracegen info` reports the fallback count and the fraction of true answers for every trace.

### Ground truth

Every stored answer is computed during generation by **two independent structures**, DND-trees and `EttF2`. Generation aborts if they ever disagree. The test suite also replays generated traces against the oracle (`tests/test_trace.cpp`), and every benchmark run checks each structure's answers against the stored ones (pre-registration §2.7).

## Randomness and determinism

- A trace seed is split into independent `SplitMix64` streams: **graph**, **op-type mix**, **updates** and **queries** (`dc::wl::stream`).
- So two `churn` traces with the same seed that differ only in `--query` have **identical update records and identical query positions**; only the queried pairs differ. This is what pairs scenarios C3 and C4 (tested: `query_kinds_share_update_stream`).
- The generators avoid `std::*_distribution`, whose output is implementation-defined, and are compiled with `-ffp-contract=off` (no FMA contraction). RGG geometry uses only `+ − × √`.
- Traces are therefore **bit-identical across GCC and Clang** (pinned by `trace_golden_hashes`, checked under both).
- **Exception: `leo`.** Satellite positions use `sin`/`cos`, which can differ in the last bit between math libraries, so near-threshold link switches could differ across platforms. LEO traces are bit-identical on the same machine and toolchain. They are archived and identified by hash, not assumed to regenerate everywhere.

## The suite: specs, manifest, data

| File | Role | Committed? |
|---|---|---|
| `scripts/trace_specs.py` → `traces/specs.tsv` | every trace the study uses: name, pre-registration group, `tracegen` arguments | yes |
| `scripts/build_traces.py` | builds missing traces into `data/traces/`; `--verify` regenerates and compares hashes | yes |
| `traces/MANIFEST.tsv` | name, group, SHA-256, bytes, arguments for every built trace | yes |
| `scripts/fetch_data.sh` → `data/` | downloads SNAP and DIMACS inputs, verified against `traces/DATA_SHA256SUMS` | script and sums yes; data no |

Groups map to the pre-registration:
- `rq1_rq2_rq3c`, `rq3_p1`, `rq3_p3`, `rq3b`, `rq5`: confirmatory, seeds 1–12;
- `pilot`: seeds 9001–9003;
- `rq3_exploratory`, `exploratory`, `held_out_snap`, `held_out_leo`, `rq3b_exploratory`: 3 seeds.

External inputs:
- SNAP: `CollegeMsg`, `email-Eu-core-temporal`, `sx-mathoverflow` (windows of 1 day, 1 week, 30 days), and `com-youtube`;
- DIMACS: `USA-road-d.NY`.

SIGMOD 2025's StackOverflow graph (6.3 × 10⁷ edges) is out of scope: it doesn't fit in 15 GB of RAM for the levelled structures.

## Versioning rules
- Any change that alters generated bytes (generator logic, RNG use, record layout) **must** bump `kTraceFormatVersion` or `tracegen`'s version, and regenerate the manifest.
- The golden-hash tests exist to make such a change impossible to miss. Never edit their constants to make a failing test pass.
- Traces used for confirmatory results are frozen by their manifest hashes. A regenerated trace with a different hash is a different trace.
