[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.21402042.svg)](https://doi.org/10.5281/zenodo.21402042)
# sega

Memory-conscious graph storage and traversal. The standalone generic C++17 core provides outgoing CSR32, byte-coded adjacency, packed CSR and Elias–Fano row offsets, with streamed BFS/DFS and an installable `sega::core` target. It requires neither MEGAHIT nor SDSL. See [generic core installation and tests](docs/generic-core.md) and [compression comparison and choices](docs/graph-compression-selection.md).

The specialized bounded-degree algorithms and MEGAHIT succinct de Bruijn graph backend remain separate research components. Their stronger degree-dependent space bounds below do not apply to arbitrary-degree generic DFS.

BFS uses a visited bitmap and a FIFO queue with level counters: its auxiliary bound is **O(N + W log N) bits**, where W is maximum frontier width. DFS uses two-bit colors and two-bit predecessor indices: its fixed arrays occupy **4N bits, rounded to machine words**, independent of depth. It relies on stable reverse adjacency and maximum in/out-degree four. See [Methods and correctness](docs/methods.md). The DFS is a bounded-degree predecessor-index algorithm, not a faithful implementation of the general EHK algorithm. The two-frontier BFS methods are excluded from the paper and default experiments; their code remains available for reproducing historical runs.

## Build and test

Requires C++17 and CMake. Python 3 is needed for the experiment runner (standard library only).

```sh
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
python3 -m unittest discover -s scripts/tests -v
```

MEGAHIT tools are built when `libs/megahit` is available. `sega_bench` operates on dependency-free synthetic graphs.

## General graph API

The compact API now provides outgoing-only CSR32, byte-coded adjacency, packed CSR and Elias–Fano row offsets,
resumable neighbor cursors, save/load, streamed BFS/DFS, reusable BFS workspace,
and outgoing-hop-limited queries. A native MEGAHIT adapter shares these traversals
without copying the graph. All four generic storage backends build without SDSL.

See [implemented API, exact commands and limits](docs/compact-cursor-api.md).
The older optional bidirectional CSR and SDSL wavelet backends remain available
for comparison; see [the wavelet prototype guide](docs/general-graph-prototype.md)
and [third-party notices](THIRD_PARTY.md). Neither generic API generalizes the
four-bit reconstruction DFS to arbitrary degrees. Existing server experiment
commands remain available.

## Server experiment

```sh
python3 scripts/run_experiments.py \
  --binary build/megahit_bench --graph /data/sample/graph \
  --out results/sample-k23-first --repeats 5 --sources first \
  --audit --hash-graph-files
```

Use `--forest` instead of `--sources` to traverse every valid ID. Every method runs in its own process. The nine default methods exclude `bfs-se` and `bfs-se32`; `dfs-se` remains included. The runner performs **one correctness process per method first**, followed by **five timing processes per method** in randomized repetition blocks: **54 processes per source/root policy**. `--repeats` controls timing only; `--correctness-repeats` separately controls hash checks and defaults to one. A first pass with `--repeats 1` requires 18 processes. Add `--dry-run` to print the counts without executing or creating output files.

Output directories must be new. The runner preserves raw stdout/stderr, metrics, provenance, and a pass/fail summary. See the [Server run guide](docs/server-run.md) for multiple sources, exact disk-backed comparisons, log interpretation, and outstanding paper experiments.

## Synthetic smoke test

```sh
python3 scripts/run_experiments.py \
  --binary build/sega_bench --synthetic wide --nodes 10000 \
  --out results/wide-smoke --repeats 1 --forest --exact-events --audit
```

Shapes: `path`, `wide`, `cycle`, `disconnected`, `random`. `--seed` controls graph generation and run shuffling. These tests do not replace biological datasets.

## Compared methods

| Method | Fixed state | Dynamic state |
|---|---|---|
| `bfs-base`, `bfs-base32` | 32-bit distances | FIFO queue |
| `bfs-stream`, `bfs-stream32` | One-bit bitmap | FIFO queue and level counters |
| `dfs-se` | Two-bit colors + two-bit predecessor indices | Parent reconstruction |
| `dfs-base`, `dfs-base32` | Byte colors | Explicit stack |
| `dfs-packed`, `dfs-packed32` | Two-bit colors | Explicit stack |

The `32` suffix selects 32-bit identifiers in queue/stack containers; unsuffixed queue/stack methods use 64-bit IDs. BFS variants emit identical ordered `(vertex, distance)` streams for identical source/root policies and adjacency order. DFS variants emit identical interleaved discovery `(vertex,parent)` and finish `(vertex)` events. Downstream output retention consumes additional memory or disk outside these traversal-state bounds.

## Graph contract

```text
N() valid(v) outdeg(v) indeg(v) outgoing(v, out[4]) incoming(v, in[4])
```

`N()` is the identifier-universe size. In MEGAHIT these are **stored SDBG edge IDs**, including invalid IDs. Traversal vertices are valid edge IDs; adjacency arcs connect consecutive edges. Universe size, valid count, and arc count differ. Neighbor order must remain stable. Two-bit predecessor values encode incoming-list positions, not nucleotide labels.

## Verification and measurements

- `summary.json` reports pass/fail and measurement medians/minima/maxima. A failed process, malformed record, output mismatch, or changed input makes the runner exit nonzero.
- SHA-256 equality is strong probabilistic evidence. `--exact-events` additionally compares serialized output byte for byte with bounded RAM and substantial disk use.
- Independent small reference tests and optional graph navigation auditing supplement output agreement, which alone cannot exclude a shared backend defect.
- Requested allocation bytes are not RSS. Graph loading, allocator metadata, output retention, and lifetime high-water marks are reported separately or explicitly excluded.
- New biological results, revised performance ratios, and downstream benefits still require server experiments.

The old `megahit_run` and `sdbg_report` retain large in-memory result collections. They are legacy small-graph utilities; use the new runner for large-graph verification and publication measurements. `example_report_10M.html` is historical.

## Historical results (not revalidated by this revision)

At k=23 and N=807,721,414, the original WSL2 report measured BFS 2.07 versus 4.87 GiB and 12.6 versus 13.8 minutes; DFS 2.16 versus 3.55 GiB and 32.4 versus 19.0 minutes. It reported matching reached counts of 587,091,258 from one source. These remain provenance, not claims about new baselines or instrumentation. Rerun the methods selected for the paper: backend validation and instrumentation have changed.

## Measurements across de Bruijn graph representations

The [three-tool benchmark guide](docs/representation-benchmarks.md) covers MEGAHIT,
MetaGraph and Bifrost. It includes pinned setup, independent
mini-graph checks, graph/component measurements, native traversal adapters,
resource logging, and a resumable runner for large FASTA/FASTQ datasets.
All three supply native de Bruijn graph representations: MEGAHIT and MetaGraph
are succinct; Bifrost is compacted. The two-frontier BFS variants are excluded.

```sh
python3 scripts/setup_representations.py --prefix ./representation-tools --jobs 2
./representation-tools/run mini --out results/representations-mini --repeats 3
```

## References

- Bowe, Onodera, Sadakane, Shibuya. Succinct de Bruijn graphs. WABI 2012.
- Elmasry, Hagerup, Kammer. Space-efficient basic graph algorithms. STACS 2015. Related general-graph theory, not this library's precise algorithm specification.
- Kammer, Sajenko. Simple 2^f-color choice dictionaries. ISAAC 2018. The packed color array here does not implement that work's constant-time choice operations.
- Li, Liu, Luo, Sadakane, Lam. MEGAHIT. Bioinformatics 2015.

## AI use and license

The original project disclosed assistance from Anthropic Claude, Opus 4.8. This revision used OpenAI Codex assistance. Authors remain responsible for implementation review, experimental validation, and archiving the precise release. MIT; see [LICENSE](LICENSE).
