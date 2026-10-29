# General graph prototype

Implemented on 2026-10-26. This is a small C++17 prototype, separate from the
MEGAHIT experiment runner. No two-frontier BFS policies are included.

## Build and run all tests

From the `sega` source directory:

```sh
python3 scripts/fetch_sdsl.py
cmake -S . -B build-general -DSEGA_BUILD_GENERAL=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-general -j 4
ctest --test-dir build-general --output-on-failure
```

Source bundles include the pinned SDSL headers and implementation files, so
omit the first command when using a bundle. The remaining build is offline;
there are no implicit downloads during configuration. A Git checkout can use
the explicit fetch script, which never resets an existing checkout.

Run the four-vertex example, including isolated vertex 2:

```sh
./build-general/general_graph_bench --edges examples/general/tiny.edges --vertices 4 --backend wavelet-rrr --traversal bfs --forest --audit
```

The program emits one JSON result. To run DFS, replace `bfs` with `dfs`. The
backend choices are `csr32`, `csr64`, `wavelet` and `wavelet-rrr`. Both CSR
variants store outgoing and incoming adjacency; their target/source IDs use
32 or 64 bits respectively. Their offsets use 64 bits. Traversal ID width is
independent: `--id-bits 32` is the default; `--id-bits 64` is also tested.
These are comparisons with matching bidirectional navigation capability. BFS
itself only needs outgoing adjacency; an outgoing-only CSR baseline is still
needed before making broader BFS storage-efficiency claims.

`--forest` visits every vertex, starting each new tree at the smallest unseen
ID. Use `--source 0` instead for single-source reachability. BFS distances reset
to zero at each forest root. Empty graphs require `--forest`.

Input is two unsigned vertex IDs per line, with blank lines and full-line `#`
comments allowed. IDs must lie in `[0, --vertices)`. Self-loops and isolated
vertices are supported. Duplicate edges, invalid IDs and extra fields are
rejected; there is no implicit deduplication, remapping or attribute removal.
For an undirected graph, explicitly supply both arcs, and supply each self-loop
once. Native undirected import and external identifier maps are not implemented.

## Repeatable little-example comparison

```sh
python3 scripts/run_general_examples.py --binary build-general/general_graph_bench --out results/general-examples --repeats 3
```

Choose a new output directory for each experiment. This runs seven little
graphs, four backends and two traversals: **56 correctness processes, then
168 timing processes = 224 processes**. Correctness runs audit the entire
topology and compare ordered event SHA-256 digests across backends. Timing
runs disable event hashing and audit. Their order is shuffled within each
repetition using a recorded seed. Counts must agree with correctness runs.

`--repeats 3` means three timing runs, not three correctness runs. Timings on
these tiny examples are smoke measurements, not publication performance
evidence. The script writes `manifest.json`, `runs.json`, `summary.json`, and
per-process stdout/stderr under `raw/`. Binary and runner hashes are recorded;
source/build/dependency provenance accompanies each benchmark result.

## C++ API

```cpp
#include "sega/general_traversal.hpp"

std::vector<sega::Edge> edges{{0,1}, {0,3}, {1,3}, {3,0}};
sega::WaveletGraph<true> graph(4, edges); // RRR-compressed wavelet tree

auto bfs_stats = sega::bfs(graph, 0,
    [](sega::Vertex vertex, uint64_t distance) { /* consume record */ });
auto dfs_stats = sega::dfs(graph, 0,
    [](sega::Vertex vertex, sega::Vertex parent) { /* discovery */ },
    [](sega::Vertex vertex) { /* finish */ });
```

Templates are in `include/sega/`. Build consumers with the `sega_sdsl` CMake
target and the project's include directory. Graphs expose:

```text
num_vertices(), num_edges()
out_begin(v), out_end(v), outdegree(v)
target(edge_position), source(edge_position)
indegree(v), incoming(v, ordinal), incoming_rank(edge_position)
storage()
```

Edges have canonical source-then-target order; outgoing and incoming neighbors
are sorted by vertex ID. `incoming_rank(e)` returns the zero-based position of
the edge's source in its target's incoming list. Invalid vertices, edge
positions and neighbor ordinals throw exceptions. Callback exceptions propagate.
Graph objects support copying and moving; rank/select pointers are rebound.
Moved-from graphs may be destroyed or assigned to, but not queried.

## Representation and algorithms

For outdegrees d[0] through d[n-1], the boundary bitvector is
`1 0^d[0] 1 0^d[1] ... 1 0^d[n-1] 1`. It has n+m+1 bits. Rank/select supports
map between source vertices and positions in the concatenated target sequence.
That sequence is represented by `sdsl::wt_int<>` for `wavelet`, or
`sdsl::wt_int<sdsl::rrr_vector<63>>` for `wavelet-rrr`. Both use the same ordinary
boundary bitvector. Incoming neighbors are located by wavelet select and then
mapped back to sources; neither wavelet backend retains reverse adjacency.

Wavelet access/rank/select cost depends on the integer alphabet (logarithmic
for these balanced trees), and RRR adds decoding work. Construction uses SDSL's
existing implementation at commit `c32874cb2d8524119f25f3b501526fe692df29f4`.
No new compression algorithm is claimed. A wavelet representation is not
automatically succinct relative to the information content of an arbitrary graph.

BFS uses a one-bit visited bitmap plus a FIFO and layer counters. DFS uses
two-bit colors plus a `std::deque` of frames containing a vertex and its next/end
edge positions. Each reached outgoing edge is scanned once. The stack grows
in blocks rather than reallocating a single contiguous vector. Cursor and
padding bytes are included in allocation measurements. This general DFS has
different frame requirements from the earlier bounded-degree rescanning baseline.

Outputs go directly to the supplied callbacks. Optional `--events NEW_FILE`
writes the existing canonical event format; an existing output path is rejected.
`--verify hash` (default) hashes events; `--verify none` uses no-op sinks and is
the timing mode. Audit or event output cannot be combined with `--verify none`.

## What the measurements mean

- `graph_encoded_bytes`: sum of encoded component sizes using SDSL's
  serialization-size accounting, including component headers and navigation
  supports, plus the vertex count. CSR vectors use that same accounting. This
  is a reproducible representation-size proxy, **not resident memory** and not
  a promised stable saved-graph file format.
- `aux_fixed_bytes`, `aux_dynamic_peak_bytes`, `aux_peak_bytes`: requested live
  bitmap/color/queue/stack allocations, including deque blocks/maps. Excludes
  graph storage, sink storage, allocator metadata and stack-local objects.
- `rss_after_build_kib`, `rss_after_traversal_kib`: current process RSS when
  available. It can include retained construction allocations and library state.
- `peak_after_parse_kib`, `peak_after_build_kib`, `process_peak_kib`: lifetime
  process high-water marks. Never subtract these to estimate traversal state.
- `parse_ms`, `build_ms`, `audit_ms`, `traversal_ms`: separate phase times.
  Correctness traversal time includes hashing/output. Timing mode does not.
- `topology_sha256`: little-endian uint64 vertex count, edge count, then every
  canonical (source,target) pair. `event_sha256` uses SEGA-EVENTS-v1.

Construction is currently **in memory**: edges are sorted, and SDSL's builder
uses temporary buffers and its RAM-file mechanism. This can cost far more RAM
than the finished representation, especially on tiny inputs with fixed buffer
overhead. The CLI releases the input edge container after construction. The
C++ caller owns and controls any original input it retains.

## Test coverage and remaining scope

`test_general_graph` checks all 531 directed simple graphs on zero through
three vertices, including self-loops, against independent adjacency-list
references. It also checks 120 seeded random graphs; 31/32/63/64/65/127/128/129
vertex boundaries; isolated vertices; cycles; in/out stars with degree 256;
and a 20,000-vertex path. Both traversal ID widths and all four backends are
checked. Navigation is checked in both directions, including incoming ranks.
Copies, moves, assignments, invalid inputs and callback failures are exercised.

The CLI integration test independently constructs expected event bytes and
topology hashes for the four-vertex example, tests generated shapes, and rejects
malformed input/options and existing event output paths. Existing bounded-degree,
MEGAHIT, SHA/event and experiment-runner tests remain in CTest.

Sanitizer build:

```sh
cmake -S . -B build-general-sanitize -DSEGA_BUILD_GENERAL=ON -DSEGA_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-general-sanitize -j 4
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-general-sanitize --output-on-failure
```

Not implemented: arbitrary-degree predecessor-reconstruction DFS, a Python
binding, stable saved-graph loading, weights/multiedges/attributes, dynamic
updates, external-memory construction, parallel traversal or a hard RAM budget.
The specialized MEGAHIT backend remains unchanged. Real-data performance and
comparison with other compressed graph libraries are still needed.
