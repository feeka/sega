# sdbg-traverse

Space-efficient (O(n)-bit) **BFS** and **DFS** on **succinct de Bruijn graphs** (BOSS).

The thesis, the research questions, the design, and the benchmark plan live in
[PLAN.md](PLAN.md). Short version: on a ~4-bit/edge BOSS index the textbook
Θ(n log n)-bit auxiliary structures (BFS distance array, DFS node stack) *dwarf
the graph itself*, so porting the space-efficient graph-algorithms line
(Elmasry–Hagerup–Kammer et al.) onto BOSS turns "use the O(n)-bit variant" from a
nicety into the only thing that fits at 10⁹ nodes. Both traversals are the **same
substrate** — a navigation oracle + a colored choice dictionary + a
reconstruction policy — which is why #1 (DFS) and #3 (BFS) are one project.

## Layout
```
include/sdbg_traverse/
  nav_oracle.hpp        navigation interface + dependency-free InMemoryOracle
  choice_dictionary.hpp 2-bit/cell colored choice dictionary + parent-trail array
  bfs.hpp               se_bfs (O(n)-bit, streamed distances) + baseline_bfs
  dfs.hpp               se_dfs (EHK stack reconstruction)      + baseline_dfs
backends/
  megahit_oracle.hpp    PRIMARY backend — MEGAHIT SDBG (verified on real graphs)
  cosmo_oracle.hpp      second backend — cosmo/VARI BOSS (compiles; construction TBD)
tests/
  test_traversal.cpp    dependency-free: se_* == baselines (hand graphs + random stress)
  megahit_run.cpp       correctness on a real MEGAHIT SDBG (se_* == textbook)
bench/
  megahit_bench.cpp     memory+time of one method (isolated peak RSS)
  sdbg_report.cpp       one command: check + benchmark + emit self-contained HTML
```

## Build & test (no dependencies)
```bash
g++ -std=c++17 -O2 -Iinclude tests/test_traversal.cpp -o test_traversal && ./test_traversal
# or:
cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

## Real graphs + HTML report (MEGAHIT SDBG backend)
MEGAHIT is vendored as a git submodule (`libs/megahit`). Clone with submodules, build,
and point the report at any MCAAT/MEGAHIT-built graph (the `<dir>/graph` prefix):
```bash
git clone --recursive <repo>       # existing clone: git submodule update --init --recursive
cmake -B build && cmake --build build -j
./build/sdbg_report <graph_prefix> report.html   # traverse + check + benchmark -> HTML
```
CMake auto-enables the MEGAHIT backend when the submodule is present, building
`megahit_run` (correctness), `megahit_bench` (one method) and `sdbg_report`.
A generated example is `example_report_10M.html`. Manual g++ (from repo root):
`-Ilibs/megahit/src` plus `libs/megahit/src/sdbg/{sdbg_meta,sdbg_raw_content}.cpp`
(`gnu++17` / `-D_GNU_SOURCE` for `popen`). cosmo backend: `-DSDBGT_WITH_COSMO=ON`.

## Status
- **Engine + dependency-free validation: green** — `se_bfs`/`se_dfs` == textbook
  baselines (`tests/test_traversal.cpp`, plain g++).
- **Real run + measured memory: green on MEGAHIT SDBG** (0.4M & 10.5M-node graphs,
  k=23). Exact-match correctness vs textbook BFS/DFS. Peak RSS on the 10.5M graph:
  BFS **40.3 MB vs 78.6 MB** textbook (the 2-bit choice dict is 16× smaller than the
  32-bit distance array); DFS **42.9 MB vs 48.0 MB**. DFS reconstruction tax ≈ 2.0
  backward probes/finish. Regenerate with `sdbg_report`.
- **Known caveat (measured):** `se_bfs` with the v1 choice dictionary rescans the
  bit-array per level → O(N·depth), pathological on high-diameter graphs (58 s on a
  CRISPR chain vs 15 ms textbook). Fix = constant-time frontier (Kammer–Sajenko).
- Backends: **MegaHit SDBG** (verified) · cosmo (compiles) · MetaGraph (parked).
- **Next:** v1.1 frontier fix; multi-source/multi-k sweep; branch-only DFS baseline;
  then extend the substrate to **SCC** (PLAN.md §12); write-up.
