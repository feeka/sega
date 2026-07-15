# sega

*Space-efficient graph algorithms on succinct de Bruijn graphs.*

Breadth-first search (BFS) and depth-first search (DFS), in their textbook form, allocate auxiliary bookkeeping that costs Θ(n log n) bits: a 32-bit distance array for BFS and an explicit node-identifier stack for DFS. On a *succinct* de Bruijn graph (dBG) — which stores each edge in roughly four bits — this auxiliary state can exceed the size of the graph itself. **sega** ports the space-efficient graph-algorithms line (Elmasry–Hagerup–Kammer and related work) onto a succinct dBG, reducing the auxiliary state to a few bits per node, and measures the resulting space and time on real graphs. On the graphs tested, the traversals reproduce their textbook counterparts exactly; the reduction in auxiliary memory is substantial, and it carries a time cost that is modest for DFS but, in the current implementation, large for BFS on high-diameter inputs (see [Limitations](#limitations)).

## Background

A de Bruijn graph over the DNA alphabet (σ = 4) has in- and out-degree at most four. Its succinct representations — for example BOSS (Bowe et al., 2012) — answer navigation queries through rank/select at about four bits per edge. Textbook BFS and DFS were not designed for this regime: once the graph is compressed, their auxiliary arrays and stacks dominate memory. The space-efficient graph-algorithms literature shows that BFS and DFS can instead run in O(n) bits by replacing those structures with a colored *choice dictionary* and reconstructing search state from the graph's own navigation primitives. To our knowledge, that construction had not been instantiated on a succinct dBG. sega does so, treating BFS and DFS as one substrate — a navigation interface, a choice dictionary, and a reconstruction policy — and measures the space–time trade-off directly.

## Design

The traversal engine is written against a small navigation interface (`NavOracle`) and is therefore independent of the graph backend.

- `include/sdbg_traverse/nav_oracle.hpp` — the interface (node count, validity, and up to four out-/in-neighbours), plus a dependency-free in-memory backend used for testing.
- `include/sdbg_traverse/choice_dictionary.hpp` — a colored choice dictionary at two bits per node. It serves as both the DFS visited-set and the BFS frontier, replacing the distance array and the node stack.
- `include/sdbg_traverse/bfs.hpp` — space-efficient BFS (`se_bfs`) with streamed distances, and a textbook baseline.
- `include/sdbg_traverse/dfs.hpp` — space-efficient DFS (`se_dfs`) using Elmasry–Hagerup–Kammer (EHK) stack reconstruction: the parent of each node is recovered on backtracking from the graph's incoming edges plus a two-bit "which parent" trail, so no node-identifier stack is stored.
- `backends/megahit_oracle.hpp` — the primary backend, over MEGAHIT's succinct dBG (the representation the MCAAT assembler is built on), vendored as a git submodule at `libs/megahit`.
- `backends/cosmo_oracle.hpp` — a second backend over cosmo/VARI BOSS; it compiles, but its graph-construction pipeline is not yet wired.

## Reproducing the results

The dependency-free test suite compiles with any C++17 compiler:

```bash
g++ -std=c++17 -O2 -Iinclude tests/test_traversal.cpp -o test_traversal && ./test_traversal
```

The MEGAHIT backend and the benchmark/report tool build through CMake, which uses the vendored submodule:

```bash
git clone --recursive https://github.com/feeka/sega.git
cd sega
cmake -B build && cmake --build build -j
./build/sdbg_report <graph_prefix> report.html   # traverse, check, benchmark; write an HTML report
```

`<graph_prefix>` is any MEGAHIT/MCAAT-built graph (the `<dir>/graph` prefix). `sdbg_report` runs the correctness checks and the memory/time benchmark and writes a self-contained HTML report; `example_report_10M.html` is one such output.

## Results

We evaluated sega on two succinct dBGs built by prior runs of MCAAT (k = 23): one with 0.4 million nodes and one with 10.6 million nodes.

**Correctness.** On both graphs, `se_bfs` reproduced the textbook BFS distances exactly, and `se_dfs` reproduced the textbook discovery order, finish order, and parent assignment exactly. BFS was additionally cross-checked against an independent in-memory reference.

**Memory.** Auxiliary memory is two bits per node for BFS and four bits per node for DFS, against 32 bits per node (the distance array) and eight-plus bits per node (the state array and node stack) for the baselines. On the 10.6-million-node graph, peak resident memory — read from `/proc`, one method per process — was:

| Traversal | space-efficient | textbook |
|---|---:|---:|
| BFS | 40.3 MB | 78.6 MB |
| DFS | 42.9 MB | 48.0 MB |

The 32-bit distance array accounts for roughly 40 MB of resident memory and approximately doubles the BFS process; the two-bit choice dictionary reduces that auxiliary term by a factor of sixteen. The measured cost of the DFS no-stack reconstruction was about two incoming-edge probes per node finished.

These figures are measured rather than asymptotic. They derive from two graphs, one value of k, one machine, and a single traversal source, and should be read as such rather than as a general characterization.

## Limitations

- **BFS runtime.** The current choice dictionary rescans its bit-array once per BFS level, so `se_bfs` runs in O(N · depth). On a high-diameter, chain-like graph (a CRISPR array) this was pathological: 58 s, against 15 ms for the textbook baseline. A constant-time frontier structure (a Kammer–Sajenko choice dictionary) is the planned remedy; DFS is unaffected, since it uses only constant-time reads and writes.
- **Scope of evidence.** Two graphs, one k, one machine, single-source traversal. A multi-source, multi-k, multi-machine sweep and a branch-only-stack DFS baseline are needed before any general claim.

## Roadmap

In order: the constant-time frontier structure; a multi-source and multi-k benchmark sweep; a branch-only-stack DFS baseline; and extension of the same substrate to strongly connected components, which reuses the DFS and obtains the graph transpose for free from the incoming-edge navigation.

## Selected related work

- Bowe, Onodera, Sadakane, Shibuya. Succinct de Bruijn graphs. WABI 2012.
- Elmasry, Hagerup, Kammer. Space-efficient basic graph algorithms. STACS 2015.
- Kammer, Sajenko. Simple 2^f-color choice dictionaries. ISAAC 2018.
- Li, Liu, Luo, Sadakane, Lam. MEGAHIT: an ultra-fast single-node solution for large and complex metagenomics assembly via succinct de Bruijn graph. Bioinformatics, 2015.

Bibliographic details above should be verified against the primary sources before formal citation.

## Disclosure of AI assistance

The implementation and this document were developed with substantial assistance from a large language model (Anthropic Claude, Opus 4.8). Every correctness and performance claim is backed by the included test and benchmark tooling, whose exact output is reproducible with the commands above; the author reviewed the code and results and takes full responsibility for them. This disclosure follows current journal and funder guidance on reporting the use of LLMs in scholarly work.

## Status and license

Prototype. Correctness and memory results are verified as described above; the roadmap items remain open. License: to be determined.
