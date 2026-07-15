# sega

Space-efficient graph traversal on succinct de Bruijn graphs.

Standard BFS and DFS use Θ(n log n) bits of auxiliary state — a 32-bit distance array for BFS, a node-identifier stack for DFS. On a succinct de Bruijn graph (dBG), at ~4 bits/edge, this state exceeds the graph. `sega` implements a space-efficient DFS (Elmasry–Hagerup–Kammer-style stack reconstruction; 4 bits/node) and a linear-time, low-memory BFS (a 1-bit visited bitmap plus an explicit frontier) on a succinct dBG, and measures their space and time on real graphs.

## Overview

- Node = k-mer; in-/out-degree ≤ 4.
- Traversal engine is backend-agnostic through the `NavOracle` interface.
- Auxiliary state: 1 bit/node visited + a frontier list (BFS), 4 bits/node (DFS) — vs 32 bits/node (BFS distance array) and 8+ bits/node plus a node stack (DFS baseline).
- DFS stores no node stack: parents are reconstructed on backtracking from incoming edges plus a 2-bit "which parent" trail (EHK).

## Components

| File | Contents |
|---|---|
| `include/sdbg_traverse/nav_oracle.hpp` | navigation interface; dependency-free in-memory backend |
| `include/sdbg_traverse/choice_dictionary.hpp` | 2-bit colored choice dictionary; 2-bit parent trail |
| `include/sdbg_traverse/bfs.hpp` | `se_bfs` + standard baseline |
| `include/sdbg_traverse/dfs.hpp` | `se_dfs` (EHK reconstruction) + standard baseline |
| `backends/megahit_oracle.hpp` | MEGAHIT SDBG backend (submodule `libs/megahit`) |
| `bench/sdbg_report.cpp` | check + benchmark + HTML report |

## Build

Tests (no dependencies):
```
g++ -std=c++17 -O2 -Iinclude tests/test_traversal.cpp -o test_traversal && ./test_traversal
```

MEGAHIT backend + report tool:
```
git clone --recursive https://github.com/feeka/sega.git
cd sega
cmake -B build && cmake --build build -j
./build/sdbg_report <graph_prefix> report.html
```
`<graph_prefix>` = MEGAHIT/MCAAT graph prefix (`<dir>/graph`). CMake enables the MEGAHIT backend when the submodule is present. For a single method on a large graph, use `./build/megahit_bench <graph_prefix> <bfs-se|bfs-base|dfs-se|dfs-base>` (no per-node result recording; see Limitations).

## Results

Graphs: three succinct dBGs, k = 23, N = 404,576, 10,585,591 (MCAAT), and 807,721,414 (a real MEGAHIT metagenome SDBG).

Correctness: on the two smaller graphs, `se_bfs` distances and `se_dfs` discovery/finish/parent maps match the standard baselines exactly (BFS also matches an independent in-memory reference). At N = 807,721,414, recording per-node results is infeasible (tens of GB), so correctness there is reachable-count parity — for both BFS and DFS the space-efficient and standard traversals reach the identical 587,091,258 nodes.

Peak resident memory (VmHWM from `/proc`, one method per process; 1 MiB = 1024 KiB).

N = 10,585,591:

| Traversal | space-efficient | standard |
|---|---:|---:|
| BFS | 41.5 MB | 78.6 MB |
| DFS | 42.9 MB | 48.0 MB |

N = 807,721,414 (1.78 GiB graph floor shared by both methods; one source reaches 587,091,258 nodes = 72.7% of N, forward reachability from a single seed):

| Traversal | space-efficient | standard | peak reduction |
|---|---:|---:|---:|
| BFS | 2.07 GiB | 4.87 GiB | 57.5% (2.36×) |
| DFS | 2.16 GiB | 3.55 GiB | 39.2% (1.64×) |

- Wall time, one run each: BFS 12.6 min (se) vs 13.8 min (standard) — se ~9% faster; DFS 32.4 min (se) vs 19.0 min (standard) — se ~1.7× slower. BFS wins on both axes; DFS trades time for space (the EHK stack reconstruction costs ~2 incoming-edge probes per finish, expensive on a cache-cold multi-GB graph).
- BFS fixed auxiliary is a 1-bit/node visited bitmap (96 MiB, ~1/32 of the 3.01 GiB 32-bit distance array it replaces); the explicit frontier lists add ~193 MiB on this graph, so the working set above the graph floor is ~289 MiB. The 57.5% peak-RSS reduction is smaller than the ~32× auxiliary-only ratio because the 1.78 GiB graph is shared by both methods.
- On N = 10,585,591 the BFS auxiliary is 1.32 MB (bitmap) + a small frontier vs 40.4 MB (distance array); `se_bfs` matches standard-BFS runtime (1.51 s vs 1.54 s).
- Scope: 3 graphs, 1 value of k, 1 machine, single traversal source, one run per method.

## Limitations

- `se_bfs` uses explicit frontier lists (O(max level width) node ids). This stays small on near-linear genomic graphs but can grow on wide-frontier graphs; the strict O(n)-bit variant would replace the lists with a Kammer–Sajenko choice dictionary (O(1) iterate). Time is linear O(N+M). (An earlier version scanned a choice dictionary per level → O(N·depth), 58 s on a CRISPR chain; that is fixed.)
- The correctness check in `sdbg_report`/`megahit_run` records per-node results (`std::map`/`std::vector` sized to N) to compare against the baseline, so it needs tens of GB at ~10⁸–10⁹ nodes. `megahit_bench` uses no-op callbacks and only the auxiliary state, and scales to large graphs.
- No benchmarks yet across multiple sources, k values, or machines; no branch-only-stack DFS baseline.

## Roadmap

1. Benchmarks across multiple sources and k values.
2. Branch-only-stack DFS baseline.
3. Strict O(n)-bit BFS frontier (Kammer–Sajenko choice dictionary) for wide-frontier graphs.

## References

- Bowe, Onodera, Sadakane, Shibuya. Succinct de Bruijn graphs. WABI 2012.
- Elmasry, Hagerup, Kammer. Space-efficient basic graph algorithms. STACS 2015.
- Kammer, Sajenko. Simple 2^f-color choice dictionaries. ISAAC 2018.
- Li, Liu, Luo, Sadakane, Lam. MEGAHIT. Bioinformatics, 2015.

Verify bibliographic details before formal citation.

## AI use

Implementation and documentation developed with LLM assistance (Anthropic Claude, Opus 4.8). All correctness and performance claims are reproducible via the tooling above.

## License

MIT. See [LICENSE](LICENSE).

## Status

Prototype. Correctness and memory results verified as above; roadmap items open.
