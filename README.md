# Cache-Conscious Dynamic Graph Connectivity via Euler Tour Trees

An empirical evaluation framework and high-performance C++20 implementation comparing fully dynamic graph connectivity data structures under real-world link churn.

## 📌 Problem Statement & Overview
Standard graph traversal algorithms (BFS/DFS) require $O(V + E)$ full-graph recomputation on topological changes, while Disjoint-Set Union (DSU) collapses structural edge records, demanding an $O(E \cdot \alpha(V))$ full rebuild on edge deletions. 

This repository investigates **Euler Tour Trees (ETT)** backed by implicit randomized Treaps supporting polylogarithmic updates. Beyond theoretical Big-O analysis, this project addresses two core systems research gaps:
1. **Memory Layout & Cache Consciousness:** Comparing traditional heap-pointer node trees against contiguous arena-backed, 32-bit index-referenced flat trees to measure L1/LLC cache miss impacts.
2. **Churn-Regime Crossovers:** Determining the exact write/read (churn vs. query) boundary conditions where dynamic structural maintenance outperforms batched static recomputation and DSU.

## 🏗️ Architecture & Features
* **General Dynamic Graph Engine:** Extends spanning-forest ETTs to arbitrary cyclic graphs via auxiliary incidence partitioning and dynamic replacement edge promotion.
* **Dual Memory Backends:**
  * `DynamicGraphETT`: Traditional heap-allocated pointer-based Treap.
  * `FlatDynamicGraphETT`: Contiguous arena-allocated vector pool using 32-bit integer indexes with embedded freelists.
* **Baselines for Empirical Validation:**
  * `BaselineBFS`: Adjacency list traversals with $O(1)$ updates and $O(V + E)$ queries.
  * `BaselineDSU`: Path compression + union-by-rank with edge-list reconstruction on deletion.

## 🚀 Quickstart

### Prerequisites
* C++20 compatible compiler (`g++` $\ge$ 11 or `clang++` $\ge$ 13)
* CMake $\ge$ 3.20
* Linux `perf` (optional, for hardware performance counter profiling)

### Build & Run
```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
./benchmark_runner