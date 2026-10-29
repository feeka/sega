# Standalone generic SEGA core

The core provides immutable simple directed graphs with arbitrary degree, dense IDs in `[0,n)`, up to 2^32 vertices, self-loops and isolated vertices. Duplicates and invalid endpoints are rejected. Input pairs are sorted into ascending source/target order; vertex IDs are preserved. All four backends expose the same cursor interface and use the same single-queue BFS and iterative DFS.

| Class / CLI name | Targets | Row boundaries | Cursor bytes on this machine |
|---|---|---|---:|
| `sega::OutCsr32` / `csr32-out` | raw uint32 | raw uint64 | 16 |
| `sega::ByteGraph32` / `byte32` | ULEB128 gaps, absolute restart every 128 neighbors | raw uint64 byte positions | 24 |
| `sega::PackedCsr32` / `packed32` | fixed width `ceil(log2 n)` | packed integer offsets | 16 |
| `sega::EfCsr32` / `ef32` | fixed width `ceil(log2 n)` | Elias–Fano plus 512-bit rank directory | 16 |

Empty/one-vertex universes allow zero-bit packed values. EF supports repeated offsets (isolated vertices). It finds a select block by binary search and scans at most eight words, followed by a six-step in-word selection. Row lookup is logarithmic in directory size; we are not claiming an O(1) succinct-select implementation. A cursor only stores start/end edge positions, so EF does not enlarge DFS frames.

This is an integration of established storage ideas, not a new compression theorem. The generic algorithms currently have no parallel execution, dynamic graph updates, weights/attribute columns, sparse external-ID dictionary, hard workspace limit, out-of-core constructor or Python bindings. Construction sorts an in-memory edge vector and uses temporary row offsets; construction peak exceeds final graph storage. Algorithms stream output, but a user callback may retain arbitrarily much of it. Incoming navigation is not provided by these outgoing-only backends.

## Install without any biological or SDSL dependency

From the `sega` repository:

```sh
cmake -S . -B build-core-package -DSEGA_BUILD_TOOLS=OFF -DCMAKE_INSTALL_PREFIX="$PWD/install-core"
cmake --install build-core-package
cmake -S examples/core -B build-core-consumer -DCMAKE_PREFIX_PATH="$PWD/install-core"
cmake --build build-core-consumer --parallel 2
./build-core-consumer/sega_example
```

A consuming CMake project uses:

```cmake
find_package(sega CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE sega::core)
```

For an in-tree dependency, `add_subdirectory` exposes the same `sega::core` target. Set `SEGA_BUILD_TOOLS=OFF` first to avoid building the research executables. Includes are under `sega/`; serialization uses POSIX exclusive creation, currently tested on macOS. No Windows portability claim has been tested.

```cpp
#include <sega/sega.hpp>
sega::EfCsr32 graph(6, {{0,1}, {0,2}, {1,3}, {2,3}, {3,4}});
sega::BfsWorkspace<> workspace;
auto stats = sega::bfs(graph, 0, [](sega::Vertex v, uint64_t hops) {
    // Consume results here without retaining all vertices.
}, workspace);
```

Choose a different graph class to compare storage; the traversal call is unchanged. Graphs can be read concurrently with separate cursors/workspaces. A workspace is reusable sequentially and clears its visited bitmap in O(n/64) per query.

## Build and verify

```sh
cmake -S . -B build-core -DCMAKE_BUILD_TYPE=Release -DSEGA_BUILD_MEGAHIT=OFF -DSEGA_BUILD_GENERAL=OFF
cmake --build build-core --parallel 2
ctest --test-dir build-core --output-on-failure --parallel 1
```

The graph test compares exact adjacency and BFS/DFS event order against an independent reference on 672 graph cases, including every simple directed graph up to three vertices, random graphs, paths, stars, empty graphs, self-loops, disconnected graphs, and word/restart boundaries. Additional tests cover all packed widths 0–64, monotone sequences with duplicates and large gaps, copy/move, bounded BFS, reusable workspace recovery after a throwing callback, save/load framing, checksum and structural corruption, and a relocated external install consumer.

Sanitizer configuration:

```sh
cmake -S . -B build-core-sanitize -DCMAKE_BUILD_TYPE=Debug -DSEGA_BUILD_MEGAHIT=OFF -DSEGA_BUILD_GENERAL=OFF -DSEGA_SANITIZERS=ON
cmake --build build-core-sanitize --parallel 2 --target test_compact_graph test_packed_storage compact_graph_bench
ctest --test-dir build-core-sanitize --output-on-failure --parallel 1 -R '^(compact_graph|packed_storage|packed_graph_integration|compact_graph_integration)$'
```

## Measurement and file contract

`storage().total()` is exact serialized bytes including indexes, padding and 120 bytes of framing/checksum. `resident_payload_bytes()` is the sum of vector capacities; it excludes C++ object headers, allocator metadata and process overhead. Traversal stats report requested live auxiliary allocations separately. The CLI also records fresh-process load/traversal times and RSS. SHA-256 is used to compare files/topologies/events; no network is involved.

Packed files use `SEGAPG01`, version 1, codec 3 or 4. Six little-endian uint64 fields follow the eight-byte magic: version, codec, n, m, target bytes, offset bytes. Payload is target words, then either packed offsets or EF low words / high words / rank directory. A 64-byte ASCII SHA-256 digest terminates the file. Dimensions and expected length are checked before allocation; checksum, padding, offset monotonicity, directory correctness, sorted uniqueness and target bounds are checked on loading. Existing `SEGACG01` CSR/byte files keep their old format. Saving refuses to overwrite an existing path.

New experiment results: [measured comparison](../../output/sega-core-2026-10-29/results.md). The runner builds once, audits topology and ordered event hashes separately, then times three fresh-process repetitions in seeded shuffled order. File validation warms graph pages before traversal; this is not a cold-cache benchmark. These are small synthetic tests, not a biological dataset or a publication-quality evaluation.

Generic reproduction (choose a new output directory):

```sh
python3 scripts/run_compact_examples.py --binary build-core/compact_graph_bench --out results/core-100k --vertices 100000 --repeats 3 --backends csr32-out byte32 packed32 ef32 --shapes random path star-out clustered
```

The study in the linked output used an additional sampled process-tree RSS stop guard at 2,048 MiB; the generic runner above does not itself impose a RAM cap. The saved `run_guarded.py` records the guarded command used on this machine. Construction and query processes are separate; audit hashes are not included in traversal timings.
