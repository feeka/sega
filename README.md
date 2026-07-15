# sega

Space-efficient graph traversal on succinct de Bruijn graphs.

Textbook BFS and DFS use Θ(n log n) bits of auxiliary state — a 32-bit distance array for BFS, a node-identifier stack for DFS. On a succinct de Bruijn graph (dBG), at ~4 bits/edge, this state exceeds the graph. `sega` implements O(n)-bit BFS and DFS (Elmasry–Hagerup–Kammer) on a succinct dBG and measures space and time on real graphs.

## Overview

- Node = k-mer; in-/out-degree ≤ 4.
- Traversal engine is backend-agnostic through the `NavOracle` interface.
- Auxiliary state: 2 bits/node (BFS), 4 bits/node (DFS) — vs 32 bits/node (BFS baseline) and 8+ bits/node plus a node stack (DFS baseline).
- DFS stores no node stack: parents are reconstructed on backtracking from incoming edges plus a 2-bit "which parent" trail (EHK).

## Components

| File | Contents |
|---|---|
| `include/sdbg_traverse/nav_oracle.hpp` | navigation interface; dependency-free in-memory backend |
| `include/sdbg_traverse/choice_dictionary.hpp` | 2-bit colored choice dictionary; 2-bit parent trail |
| `include/sdbg_traverse/bfs.hpp` | `se_bfs` + textbook baseline |
| `include/sdbg_traverse/dfs.hpp` | `se_dfs` (EHK reconstruction) + textbook baseline |
| `backends/megahit_oracle.hpp` | MEGAHIT SDBG backend (primary; submodule `libs/megahit`) |
| `backends/cosmo_oracle.hpp` | cosmo/VARI BOSS backend (compiles; construction not wired) |
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
`<graph_prefix>` = MEGAHIT/MCAAT graph prefix (`<dir>/graph`). CMake enables the MEGAHIT backend when the submodule is present.

## Results

Graphs: two MCAAT-built succinct dBGs, k = 23, N = 404,576 and 10,585,591.

Correctness: `se_bfs` distances and `se_dfs` discovery/finish/parent maps match the textbook baselines exactly on both graphs; BFS additionally matches an independent in-memory reference.

Peak resident memory (N = 10,585,591; VmHWM from `/proc`, one method per process):

| Traversal | space-efficient | textbook |
|---|---:|---:|
| BFS | 40.3 MB | 78.6 MB |
| DFS | 42.9 MB | 48.0 MB |

- BFS auxiliary: 2.52 MB (2 bits/node) vs 40.4 MB (32-bit distance array) — factor 16.
- DFS reconstruction cost: ~2.0 incoming-edge probes per finish.
- Scope: 2 graphs, 1 value of k, 1 machine, single traversal source.

## Limitations

- `se_bfs` rescans the choice-dictionary bit-array once per BFS level → O(N·depth). On a high-diameter graph (CRISPR array): 58 s vs 15 ms textbook. Fix: constant-time frontier (Kammer–Sajenko choice dictionary). DFS unaffected.
- No multi-source, multi-k, or multi-machine sweep; no branch-only-stack DFS baseline.

## Roadmap

1. Constant-time frontier structure (fixes `se_bfs` runtime).
2. Multi-source / multi-k benchmark sweep.
3. Branch-only-stack DFS baseline.
4. Strongly connected components (reuses DFS; transpose obtained from incoming-edge navigation).

## References

- Bowe, Onodera, Sadakane, Shibuya. Succinct de Bruijn graphs. WABI 2012.
- Elmasry, Hagerup, Kammer. Space-efficient basic graph algorithms. STACS 2015.
- Kammer, Sajenko. Simple 2^f-color choice dictionaries. ISAAC 2018.
- Li, Liu, Luo, Sadakane, Lam. MEGAHIT. Bioinformatics, 2015.

Verify bibliographic details before formal citation.

## AI use

Implementation and documentation developed with LLM assistance (Anthropic Claude, Opus 4.8). All correctness and performance claims are reproducible via the tooling above.

## Status

Prototype. Correctness and memory results verified as above; roadmap items open. License: TBD.
