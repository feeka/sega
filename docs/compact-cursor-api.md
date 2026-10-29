# Compact graph API: implemented revision

This revision provides sequential neighbor cursors, two outgoing-only storage
backends, saved graphs, a native MEGAHIT view, streamed BFS/DFS, and reusable BFS
workspace with an optional outgoing-hop radius. It does not include the legacy
two-frontier BFS methods. This is an engineering prototype, not a claim of a new
compression method or a universal improvement in memory or speed.

## Build and reproduce

From the `sega` repository directory:

```sh
cmake -S . -B build-cursor -DCMAKE_BUILD_TYPE=Release
cmake --build build-cursor -j 4
ctest --test-dir build-cursor --output-on-failure
python3 scripts/run_compact_examples.py --binary build-cursor/compact_graph_bench --out results/compact-1m --vertices 1000000 --repeats 3
```

`results/compact-1m` must be a new directory. This command builds random, path,
and outgoing-star graphs, each with one million vertices. For each graph and
backend it checks full BFS, full DFS, and 100 repeated radius-two BFS queries,
then runs three fresh-process timing repetitions of each workload. It executes
78 processes sequentially: six builders, 18 correctness runs and 54 timings.
`--repeats 3` means three timing runs; it does not repeat graph construction.
The local-query workload uses the same source (vertex zero) on every query.

The two new backends have no external library dependency. Existing MEGAHIT
integration tests also run when its submodule is present. To include the older
wavelet backends and their regression tests, add `-DSEGA_BUILD_GENERAL=ON` to
CMake after obtaining the pinned SDSL sources as described in
[the older prototype guide](general-graph-prototype.md).

A single small build/query example:

```sh
build-cursor/compact_graph_bench build --backend byte32 --graph tiny.sega --edges examples/general/tiny.edges --vertices 4
build-cursor/compact_graph_bench query --backend byte32 --graph tiny.sega --traversal bfs --source 0 --radius 2 --audit
build-cursor/compact_graph_bench query --backend byte32 --graph tiny.sega --traversal dfs --forest --verify none
```

Use `csr32-out` for the matched outgoing-only CSR baseline. Query with the same
backend used to build the file. Build and query are separate OS processes, so
query peak RSS excludes the input edge list and construction. The loader reads
the whole graph, verifies its checksum and validates every row; its cost is
reported in `load_ms`. Traversal timing starts afterwards. This is a warm-memory
traversal measurement, not a cold-cache or memory-mapped measurement.

`--verify hash` is the default and checks the streamed events; use `--verify
none` only for timing after correctness checks. `--audit` hashes canonical
source/target pairs. `--events NEWFILE` writes exact event bytes for comparison.
All hashes are computed locally. Each saved graph includes a SHA-256 checksum
of its header and payload. The runner also records binary and graph file hashes,
compiled-source provenance, full commands, raw stdout/stderr and, on macOS,
`vm_stat` before and after every process.

## C++ usage

```cpp
#include "sega/compact_graph.hpp"
#include "sega/general_traversal.hpp"

sega::ByteGraph32 graph(4, {{0,1}, {0,3}, {1,3}, {3,0}});
graph.save("example.sega"); // creates a new file; existing files are rejected
// A later process can use: auto graph = sega::ByteGraph32::load("example.sega");

sega::BfsWorkspace<uint32_t> workspace;
auto stats = sega::bfs(graph, 0,
    [](sega::Vertex vertex, uint64_t distance) { /* consume immediately */ },
    workspace, false, 2); // source 0, outgoing hop radius 2

// Reuse workspace for another query; results are not retained by the library.
sega::bfs(graph, 3, [](auto vertex, auto distance) { /* consume */ }, workspace);
sega::dfs(graph, 0,
    [](auto vertex, auto parent) { /* discovery; root parent = no_vertex */ },
    [](auto vertex) { /* finish */ });
```

BFS at radius zero emits the source only. Boundary vertices are emitted with
correct shortest-hop distances, but their outgoing edges are not scanned. The
radius is single-source and outgoing-only; it is not a base-pair distance or an
undirected biological neighborhood. Repeated queries clear the visited bitmap
in O(N/64) operations. The workspace retains capacity and cannot be shared by
concurrent queries. If a callback throws, the exception propagates; resetting
that workspace for a later query is tested.

## Backend contract

A backend supplies:

```cpp
uint64_t num_vertices() const; // ID universe, possibly containing invalid IDs
bool valid(sega::Vertex v) const;
using Cursor = /* backend state */;
Cursor neighbors(sega::Vertex v) const;
bool next(Cursor& cursor, sega::Vertex& target) const;
```

A cursor is returned by value. `next` advances it and returns one valid target,
or false at the end. Copies resume independently. Traversal retains no neighbor
array proportional to a row's degree. Backend data must remain alive and
unchanged; moving or replacing the graph invalidates its outstanding cursors.
Cursors must be passed back to the same graph and must not be manually altered.
Neighbor order must be stable for exact event comparison. The C++ templates do
not dispatch through a virtual function for every edge.

CSR and wavelet graphs preserve sorted target order. `OracleView<Oracle>` from
`sega/oracle_view.hpp` borrows an existing bounded-degree `NavOracle`, including
`MegaHitOracle`, preserving native order and invalid-ID holes. It keeps up to
four IDs inline in its cursor and never builds another full graph. The real
MEGAHIT k=1 fixture is checked against independently constructed DNA transitions;
it is a backend test, not biological performance evidence.

Incoming navigation is not required by BFS or this stack-based DFS. It remains
available on the older bidirectional CSR/wavelet types. The new `OutCsr32` and
`ByteGraph32` intentionally expose outgoing navigation only; no reverse index
is built. The four-bit bounded-degree reconstruction DFS remains a separate
specialized algorithm and is not applied to these arbitrary-degree backends.

## Representation and file format

Both new backends accept static simple directed graphs with dense 32-bit IDs,
self-loops and isolated vertices. They reject duplicate edges and out-of-range
endpoints. There are no weights, multiedges, labels, external-ID maps or dynamic
updates. The builder sorts edges in memory. Only the synthetic random generator
explicitly removes duplicate draws; edge-list import does not silently deduplicate.

`OutCsr32` stores 64-bit row offsets and 32-bit targets. `ByteGraph32` stores
64-bit byte offsets and unsigned LEB128 integers. The first target in a row is
absolute; subsequent sorted targets are gaps. Every 128th neighbor starts a new
block with an absolute ID. Sequential cursors need no block directory. There
is no random-access block-seek operation. This uses an established variable-byte
integer encoding; it is not a port of Ligra+ or GBBS and does not claim their
optimizations or benchmark results.

A CSR cursor occupies 16 bytes and a byte-coded cursor 24 bytes on this build.
With 32-bit traversal IDs, the padded DFS frames occupy 24 and 32 bytes,
respectively. This additional cursor state can offset graph savings on deep
DFS. The benchmark includes these actual frame allocations.

The version-one file format is little-endian: eight magic bytes `SEGACG01`, six
64-bit fields (version, codec, vertices, edges, payload bytes, block size), N+1
64-bit row offsets, the payload, and 64 ASCII hex checksum characters. Codec 1
is CSR32; codec 2 is byte coding with 128-neighbor restarts. Offsets are target
indices for CSR and byte positions for byte coding. Total file bytes include
all offsets, the 56-byte header and the 64-byte checksum. Loading checks file
length before payload allocation, codec/version, offsets, canonical integers,
bounds, sorted uniqueness, edge count and checksum. Saving uses exclusive file
creation and removes a partial output on handled write failure; it does not
promise crash-atomic publication or durable disk synchronization.

## Measurements and limits

`graph_file_bytes` is exact saved size. `graph_payload_capacity_bytes` counts
vector capacity for offsets and targets, excluding small C++ object headers.
`aux_peak_bytes` counts requested live bitmap/colors and queue/stack allocations,
including blocked-container overhead and retained capacity. It excludes graph,
output sink, allocator bookkeeping and stack locals. For repeated queries the
auxiliary figures describe the last query; process peak covers the whole run.
RSS is reported separately and includes runtime/allocator effects. No hard
allocation budget or process RAM cap is enforced by this revision.

The current direction is a reusable exploration API with a measured tradeoff.
Remaining work includes multi-source queries, sequence-to-ID mapping, sequence
output, reverse-neighborhood workloads, external-memory construction, ID
reordering, bindings and comparisons with optimized graph libraries. None is
required to use the implemented ID-based queries, and none should be described
as already delivered biological functionality.
