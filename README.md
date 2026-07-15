# sega

Space-efficient BFS and DFS on succinct de Bruijn graphs.

A succinct de Bruijn graph stores a *k*-mer graph in about 4 bits per edge. A standard BFS distance array (32 bits/node) or DFS node stack is then larger than the graph itself. `sega` runs BFS with a 1-bit/node visited bitmap and DFS with 4 bits/node (Elmasry–Hagerup–Kammer stack reconstruction), and reports peak memory and running time on real graphs.

## Interface

Traversals call a backend through the `NavOracle` interface:

```
N()   valid(v)   outdeg(v)   indeg(v)   outgoing(v, out[4])   incoming(v, in[4])
```

Alphabet σ = 4 (in/out-degree ≤ 4); node indices are 64-bit. Two backends are provided: `InMemoryOracle` (header-only, used by the tests) and `MegaHitOracle` for a MEGAHIT SDBG (submodule `libs/megahit`).

## Build

```
# tests only, no dependencies
g++ -std=c++17 -O2 -Iinclude tests/test_traversal.cpp -o test_traversal && ./test_traversal

# MEGAHIT backend and tools
git clone --recursive https://github.com/feeka/sega.git
cd sega
cmake -B build && cmake --build build -j
```

CMake builds the MEGAHIT backend when the submodule is present.

## Usage

`<prefix>` is a MEGAHIT/MCAAT SDBG prefix (`<dir>/graph`).

```
./build/megahit_run   <prefix>                                    # run and check correctness on a real graph
./build/megahit_bench <prefix> {bfs-se|bfs-base|dfs-se|dfs-base}  # one method: peak memory and time
./build/sdbg_report   <prefix> report.html                        # check, benchmark, HTML report
```

## Files

| File | Contents |
|---|---|
| `include/sdbg_traverse/nav_oracle.hpp` | `NavOracle` interface and `InMemoryOracle` |
| `include/sdbg_traverse/choice_dictionary.hpp` | 2-bit choice dictionary and parent-symbol trail |
| `include/sdbg_traverse/bfs.hpp` | `se_bfs` and `baseline_bfs` |
| `include/sdbg_traverse/dfs.hpp` | `se_dfs` and `baseline_dfs_explicit` |
| `backends/megahit_oracle.hpp` | `MegaHitOracle` |
| `tests/test_traversal.cpp` | correctness tests on `InMemoryOracle` |
| `tests/megahit_run.cpp` | correctness on a MEGAHIT SDBG |
| `bench/megahit_bench.cpp` | peak-memory and time measurement |
| `bench/sdbg_report.cpp` | check, benchmark, HTML report |

## Methods

`se_bfs` uses a 1-bit/node visited bitmap and two frontier arrays; distances are passed to a callback, not stored. `baseline_bfs` uses a 32-bit distance array and a FIFO queue.

`se_dfs` keeps no node stack. Visited state is a 2-bit/node choice dictionary, and the edge used to enter each node is stored in a 2-bit/node array; on backtracking the parent is found from the incoming edges. Auxiliary state is 4 bits/node. `baseline_dfs_explicit` uses a node-id stack and a 1-byte/node state array.

## Results

*k* = 23. Peak resident set is VmHWM from `/proc`, one method per process.

*N* = 10,585,591:

| Traversal | sega | standard |
|---|---:|---:|
| BFS | 41.5 MB | 78.6 MB |
| DFS | 42.9 MB | 48.0 MB |

*N* = 807,721,414 (MEGAHIT metagenome SDBG):

| Traversal | sega memory | standard memory | sega time | standard time |
|---|---:|---:|---:|---:|
| BFS | 2.07 GiB | 4.87 GiB | 12.6 min | 13.8 min |
| DFS | 2.16 GiB | 3.55 GiB | 32.4 min | 19.0 min |

`se_bfs` and `se_dfs` return the same output as the baselines (BFS distances; DFS discovery, finish and parent order). On the 807,721,414-edge graph all four runs reach the same 587,091,258 nodes from one source.

## References

- Bowe, Onodera, Sadakane, Shibuya. Succinct de Bruijn graphs. WABI 2012.
- Elmasry, Hagerup, Kammer. Space-efficient basic graph algorithms. STACS 2015.
- Kammer, Sajenko. Simple 2ᶠ-color choice dictionaries. ISAAC 2018.
- Li, Liu, Luo, Sadakane, Lam. MEGAHIT. Bioinformatics 2015.

## AI use

Developed with assistance from a large language model (Anthropic Claude, Opus 4.8).

## License

MIT. See [LICENSE](LICENSE).
