# Implementation Notes for Competitor Structures

Phase 3.2 notes for reimplementing the competitor algorithms in C++ (Phase 5.2). They were written from the papers' pseudocode, 2026-10-16.

**Licensing:** `qingchen3/D-tree` and `qingchen3/Imp_bench_dyn` have no license. Read them only to resolve ambiguities and cross-check outputs; **never copy code**. `tomtseng/dynamic-connectivity-hdt` is MIT-licensed.

Each structure must:
- implement `DynamicConnectivity`;
- pass the differential fuzz suite;
- have a P0 and an F2 variant that behave identically (`docs/PREREGISTRATION.md` §2.10).

Every **ambiguity** below is resolved by an explicit, deterministic choice, recorded here before any measurement.

**Canonical iteration order:** every algorithm's choices can depend on the order it iterates a neighbor or child set (which replacement edge is found first). So all such sets, in every layout, are **swap-remove ordered sequences**: an insert appends, and a delete moves the last element into the removed slot. There are two implementations with identical semantics:
- `StdOrderedSet` (P0/F0): a per-vertex `std::vector` plus a `std::unordered_map` of positions;
- `FlatOrderedSet` (F2): pooled flat storage plus the shared flat position hash.

`std::unordered_set` is not used for any iterated set: its hash order cannot be reproduced by a flat layout, so P0 and F2 could make different choices. *(Phase 4's ETT used `unordered_set` for its non-tree sets. It moves to `StdOrderedSet` in Phase 5, and the §1 memory observation in the pre-registration refers to the Phase 4 containers.)*

---

## D-tree (Chen, Lachish, Helmer & Böhlen, PVLDB 15(11) 2022; arXiv 2207.06887)

A k-ary spanning tree per component that tries to keep the root near the centroid and the tree shallow. Queries walk to the root.

**Node fields (Def. 6.3):** `key`, `parent`, `children` (set), `size` (subtree size), `nte` (non-tree neighbors).
There is one node per vertex, allocated once, so updates do **not** allocate nodes. That differs from ETT, where every tree edge allocates two nodes.

| Operation | Algorithm | Behavior |
|---|---|---|
| `reroot(w)` | Alg. 1 | Reverse parent links along the path from w to the root, moving each node between children sets. Then fix sizes walking back from the old root: `ch.size -= ch.parent.size; ch.parent.size += ch.size`. Cost O(depth(w)) |
| `link(u, r_u, v)` | Alg. 6 | v is a root. Add v as a child of u and walk u → root adding `v.size`. Remember the **first** (deepest) node m with `size > (r_u.size + v.size) / 2`, using r_u's size **before** the walk. If m exists and m ≠ r_u, call `reroot(m)` (centroid heuristic). Returns the root |
| `unlink(v)` | Alg. 7 | Walk the ancestors subtracting `v.size`, remove v from its parent's children, set `v.parent = null`. Returns (v, old root) |
| `conn(u, v)` | Alg. 2 | Walk u to its root, remembering the last node d before the root. If `d.size > root.size / 2`, call `reroot(d)`. Same for v. Compare the roots. **Queries modify the tree** |
| insert, tree edge | Alg. 4 | Roots r_u, r_v. If `r_u.size < r_v.size`: `link(v, r_v, reroot(u))`, else `link(u, r_u, reroot(v))` |
| insert, non-tree edge | Alg. 3 | l is the shallower endpoint, h the deeper (tie: l = u), Δ = depth(h) − depth(l). If Δ < 2, add the edge to both `nte` sets. Otherwise climb from h Δ − 2 times to node i, turn tree edge (i, i.parent) into a non-tree edge (add it to both `nte`), `unlink(i)`, then `link(l, r, reroot(h))`. The inserted edge becomes a tree edge |
| delete, non-tree edge | Alg. 8 | Remove from both `nte` sets |
| delete, tree edge | Alg. 5 | ch = the child endpoint; `(ch, r) = unlink(ch)`; r_s = ch if `ch.size < r.size`, else r. Build R = **all** candidate edges (x, y): x over a BFS of the smaller tree, y ∈ x.nte, y in the larger tree (checked by walking y to its root). If R is empty: fix the smaller tree's centroid, then stop. Otherwise pick (x, y) ∈ R with **minimal depth(y)**, `delete_nte(x, y)`, then `insert_te(x, y, r_s, r_l)` |

**Cost to note:** the delete enumerates *every* candidate replacement edge (no early exit), and each check walks to a root. Xu et al. (Obs. 2) identify this as D-tree's main deletion cost.

**Paper vs. the authors' code, and our choices** (implemented in `include/DTree.hpp`, 2026-10-17)

We read the authors' code to resolve these (`qingchen3/D-tree`, `Dtree/Dtree_utils.py`; SIGMOD 2025's `Imp_bench_dyn` has the same code at all four points). No code was copied.

**Rule:** where the paper and the code differ, we follow the **code**, because it produced the published results (D-tree's paper and SIGMOD 2025). The one exception is where both are wrong.

| # | Point | Paper | Authors' code | Ours |
|---|---|---|---|---|
| 1 | Roots on insert | "run conn(u, v)" (Alg. 2, with centroid reroots) | plain `find_root` walks, no reroot | code |
| 2 | Alg. 4 tie on equal tree sizes | reroot v's tree, attach below u | reroot u's tree, attach below v | code |
| 3 | Alg. 3 climb count | Δ − 2 | `range(1, Δ − 2)` = Δ − 3 | code |
| 4 | Alg. 5 centroid fix after a failed search | "a non-root m with m.size > r_s.size/2" | first such node in BFS order | code |
| 5 | **Alg. 2 query answer** | compares u's root from before v's reroot with v's root after it | same (`query`) | **fixed**: decide by v's root *before* its reroot. Same restructuring, correct answer |

- *Ties in minimal depth(y):* take the first candidate in BFS order (children, then `nte`, each in canonical order). The code uses the same strict `<`.
- *Depth* is the number of edges to the root, computed by walking parents (not stored).

**Finding (#5), worth reporting to the authors.** When both endpoint walks in one query reroot the *same* tree, u's recorded root is stale and a connected pair is answered "not connected".
- **Test:** the literal version, on our generator with V = 8, 100 seeds (1,500 ops each), gave **wrong answers in 30 seeds**. Used inside inserts (the paper's "run conn" wording), it also **corrupted the tree** in 27 seeds: an insert was misjudged as joining two trees, which linked a tree to itself.
- **Effect on published results:** the authors' evaluations time `query` but discard its answers, and their sanity checks use the non-rerooting `query_simple`. So their *timings* are unaffected (the fix does identical work) and the flaw would not have been visible. State this neutrally in the paper, and contact the authors before submission.

**P0 / F2 mapping** (draft; final version recorded as an amendment before measurement):
- **P0:** heap `Node` objects with a `parent` pointer; `children` and `nte` as `StdOrderedSet`.
- **F2:** `parent`, `size` as `int32_t` arrays; `children` and `nte` as `FlatOrderedSet`.

---

## ID-tree and DND-trees (Xu, Wen, Qin, Li, Zhang & Lin, SIGMOD 2024; extended arXiv 2601.17285)

**ID-tree** is a lighter D-tree: no `children` or `nte` sets, only `parent`, `st_size` and the graph's adjacency N(x). **DND-trees** add a disjoint-set tree (**DS-tree**) per component, which gives near-constant-time queries. "Xu et al." in the plan and pre-registration means **DND-trees**.

**ID-tree operations** (§4):
- `ReRoot`, `Link`, `Unlink`: as in D-tree, without the children/`nte` updates. `Link` keeps the centroid heuristic.
- **ID-Query:** compare roots, with **no rotation** during queries.
- **ID-Insert** (Alg. 1). The edge always goes into the adjacency.
  - Same root (non-tree edge): let u be the deeper endpoint. If depth(u) − depth(v) ≤ 1, return. Otherwise w ← u; for 1 ≤ i < (depth(u) − depth(v))/2: w ← parent(w). Then `Unlink(w)` and `Link(ReRoot(u), v, root_v)`. This differs from D-tree, which climbs Δ − 2.
  - Different roots (tree edge): make u the endpoint in the smaller tree, then `Link(ReRoot(u), v, root_v)`.
- **ID-Delete** (Alg. 2).
  - Not a parent–child pair: a non-tree edge, so just remove it from the adjacency.
  - Otherwise make u the child; root_v ← `Unlink(u)`; swap so u is the root of the **smaller** tree. Then run a BFS from u over N(x):
    - skip y = parent(x);
    - if x = parent(y), y is a child: enqueue it and add it to S;
    - otherwise walk w from y toward the root. If some w ∈ S, y is on the small side: stop the walk (adding the visited w to S). If the walk reaches the root, (x, y) is a replacement: `Link(ReRoot(x), y, root_v)` and **return immediately** (early exit, unlike D-tree).
  - Lemma 7: if no replacement exists, S ends as exactly the small tree's vertex set.

**DS-tree** (§6.2–6.3):
- `DSnode{id, parent, pre, next, children}`. Children form a doubly linked list with sentinel nodes, giving O(1) insert and remove. No subtree size is stored.
- `UnlinkDS`, `LinkDS(u, v)`: O(1) list operations. LinkDS assumes u's tree is not larger than v's.
- `FindDS(u)`: recursive path compression, implemented as UnlinkDS + LinkDS(u, root).
- `Isolate(u)`: remove u and re-attach its children to the root. Never called on the root.
- `ReRootDS(u)`: **swap the DSnode objects** of u and the root (vertex → DSnode mapping) and fix their ids. O(1) even when the root has many children.

**DND operations:**
- **Query:** compare `FindDS` roots.
- **Insert** (Alg. 5): `FindDS` both endpoints, then follow ID-Insert. For a tree edge: `ReRoot(u)`, `LinkDS(root_u, root_v)`, `Link(u, v, root_v)`.
- **Delete** (Alg. 6): run ID-Delete, which returns (u, root_v, succ, S); `ReRootDS(root_v)`. If succ, done. Otherwise `Isolate(u)`, then for each w ∈ S \ {u}: `Isolate(w)` and `LinkDS(w, u)`.
- The design keeps **the DS-tree root equal to the ID-tree root**, so `FindDS` also yields the ID-tree root, and `st_size(root)` is valid.

**Ambiguities and our choices** (implemented in `include/DNDTree.hpp`, F2 layout only, 2026-10-17):
1. *ID-Insert's loop bound* "1 ≤ i < (depth(u) − depth(v))/2" uses real division: the loop runs ⌈Δ/2⌉ − 1 times.
2. *Root consistency after `Link`'s centroid reroot.* Algs. 1 and 5 never say whether the DS root is re-synchronized when `Link` reroots the ID-tree.
   - **Our choice:** call `ReRootDS(new root)` whenever `Link` returns a different root.
   - **Tested:** without it, the invariant "DS root = ID root" (which the paper's design relies on, §6.4) breaks within 34 operations at V = 8 and 63 at V = 30. Keep the re-sync.
   - If the authors' code is found, compare.
3. *Adjacency iteration order* in the BFS: canonical order.
4. *Circular children lists:* one sentinel per DSnode (index node + n) instead of separate begin and end sentinels. Same O(1) operations. The sentinel moves with its node on `ReRootDS`.

**P0 / F2 mapping** (draft):
- **P0:** heap ID nodes (`parent` pointer, `st_size`), heap DSnodes with pointer links, `StdOrderedSet` adjacency.
- **F2:** `int32_t` arrays for parent and size, an arena of DSnodes with `int32_t` links (`ReRootDS` swaps the vertex → index mapping), and the shared flat adjacency.

---

## HKS (simplified Henzinger–King; no levels)

- SIGMOD 2025 cites **Alberts, Cattaneo & Italiano, JEA 1997** (their ref. [11]) for HKS and says only that HKS stores non-tree neighbors "in random search trees" and does not push edges down levels.
- It is an ET-tree structure like ours. The difference is how the replacement is found: HKS **samples** non-tree edges incident to the smaller tree (Henzinger–King sampling), with a fallback to exhaustive search. Ours scans them deterministically.
- **Open item before implementing (Phase 5.2):** read Alberts et al. 1997 and Henzinger–King 1999 for the exact sampling rule (sample count, and the fallback when sampling fails). Record it here.
- **Implementation plan:** build on `EulerTourForest<Storage>`, augmenting each treap node with the count of non-tree edges incident to its subtree, so a uniform random incident non-tree edge can be sampled in O(log n). The sampling RNG uses its own seed, which is reported.

## HDT (Holm, de Lichtenberg & Thorup, JACM 2001)

- ⌈log₂ V⌉ levels, one Euler-tour forest per level, with each edge assigned a level.
- On deleting a tree edge at level ℓ, for ℓ down to 0: push the smaller tree's level-ℓ tree edges to ℓ + 1, then scan its level-ℓ non-tree edges, promoting the first replacement or pushing scanned edges up a level.
- Augment the treap nodes with flags or counts ("has level-ℓ tree edge", "has level-ℓ non-tree edge") so the scan skips empty subtrees.
- SIGMOD 2025 attributes HDT's 1335× slowdown on the path graph to this push-down of every node in the smaller path.
- **Reference:** `tomtseng/dynamic-connectivity-hdt` (MIT), for correctness cross-checks and as an optional unmodified external baseline.
- **Implementation plan:** a level-indexed array of `EulerTourForest<Storage>` with augmented nodes. This is the largest Phase 5 item, and the plan's fallback drops it first.

---

## Shared F2 containers (used by every algorithm's F2)
- **Flat hash map** (open addressing, linear probing, power-of-two capacity, tombstone-free backward-shift deletion), keyed by packed 64-bit edge keys.
- **`FlatOrderedSet`** (per-vertex sets): all elements in pooled flat storage, plus a flat hash (edge → position) for O(1) swap-remove. Same semantics and iteration order as `StdOrderedSet`.
- Both are written once in `include/` and tested on their own before any algorithm uses them.
