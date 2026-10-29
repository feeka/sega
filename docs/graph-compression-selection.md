# Graph compression choices for the generic SEGA core

Research and implementation snapshot: 2026-10-29. This covers the principal families relevant to a traversable graph library; it is not a claim to enumerate every published variant.

## The denominator matters

Let `n` be the number of vertices and `m` the number of **stored directed arcs**, including self-loops. An undirected edge stored in both directions contributes two arcs. Unless specified otherwise, these are static, unweighted, simple graphs with dense vertex IDs. Weights, names, sequences, annotations, ID maps and construction scratch are additional storage.

Our outgoing CSR32 baseline uses `4m + 8(n+1)` payload bytes: 32-bit targets and 64-bit row offsets. CSR is already much smaller than a pointer/hash-table graph. Comparing a compressed graph to Python objects produces a different percentage from comparing it to CSR. Our saved files add 120 bytes for framing/checksum.

Space saved is `100 * (1 - compressed_bytes / baseline_bytes)`. File bytes, allocated payload capacity, process RSS, construction peak, traversal scratch and retained output are separate quantities. A smaller encoded graph does not imply a faster traversal or an equal percentage reduction in process RSS.

## Principal representation families

| Representation | How much space? | Where it helps / limitations | SEGA decision |
|---|---|---|---|
| COO edge list | Two 32-bit endpoints: `8m` bytes, before indexes. | Good interchange/build input. Efficient per-vertex navigation needs an index or sorting. | Input only. |
| Vector/pointer/hash adjacency | 32-bit targets alone need `4m`; per-vertex vector objects, spare capacity and allocator/hash metadata are additional. E.g. 24-byte vector objects would add `24n`, before allocations. | Convenient updates; overhead is implementation-dependent, not a fixed compression ratio. | Independent test reference, not the compact storage baseline. |
| CSR32 / CSR64 | `4m+8(n+1)` / `8m+8(n+1)` bytes with 64-bit offsets. A graph with `m <= UINT32_MAX` could also use 32-bit offsets. | Contiguous fast scans, O(1) row start. A stored transpose normally adds another adjacency structure. | Keep CSR32 as speed baseline. |
| Bit-packed CSR | `m ceil(log2 n)` target bits and `(n+1) ceil(log2(m+1))` offset bits, rounded to words. | Removes unused integer bits while preserving IDs and O(1) access. Does not exploit similar rows or small gaps. | Implemented as `PackedCsr32`. |
| Elias–Fano monotone lists | For `q` nondecreasing values bounded by `U`, low width `l=max(0,floor(log2(U/q)))`; our base encoding has `q*l + q + floor(U/2^l) + 1` bits, plus word padding and select directory. Common bound is about `q(log2(U/q)+2)` bits when `U>=q`, before query indexes. | Row offsets are monotone even when target IDs are random. EF can also encode each sorted target list, but per-row indexes/metadata must be counted. | Implemented for offsets as `EfCsr32`; targets remain bit-packed. Our select is logarithmic in directory blocks, not theoretical O(1) select. [Vigna](https://vigna.di.unimi.it/ftp/papers/Broadword.pdf). |
| Gap codes: variable-byte, nibble, Rice, gamma/delta | ULEB128 uses 1–5 bytes per 32-bit encoded value; small gaps can cut target bytes substantially, large values can exceed raw32 size. Add row offsets and restart information. Ligra+ reported **49–56% of original space on average** across its tested schemes/datasets. | Sorting/reordering affects gaps. Decoding costs time; parallel bandwidth savings can offset it. Our simple ULEB128 backend is not a Ligra+ reproduction. | Existing `ByteGraph32`; retain for comparison and local IDs. [Ligra+ paper](https://www.cs.umd.edu/~laxman/papers/Ligra%2B.pdf). |
| Block bit packing / frame-of-reference / patched exceptions | A block of `B` gaps at width `b` costs `B*b` bits plus headers, exceptions and row directories. No universal percentage. | Fast batch decoding; sparse exceptions can be handled separately. Block minima/maxima and SIMD affect the tradeoff. | Candidate after initial measurements; keep cursors independent of block size. |
| Wavelet tree/matrix, optionally RRR bitvectors | Plain target sequence about `m ceil(log2 n)` bits plus rank/select and row boundaries. Entropy-compressed variants can approach sequence entropy plus redundancy; actual indexes matter. | Rank/select enables reverse navigation without storing a complete transpose, but usually adds multiple bitvector operations per edge. | Existing optional SDSL experiment; not default after our slow scans. [Wavelet matrix](https://users.dcc.uchile.cl/~gnavarro/ps/spire12.4.pdf). |
| WebGraph/BVGraph reference and interval compression | Published WebBase example: **3.08 bits/link**; transpose **2.89 bits/link**. These are separate representations, not their combined size or a universal bound. Resident offset/query structures must also be accounted for. | Very effective when nearby rows share neighbors and IDs form intervals. Ordering and reference-chain depth affect access. | Strong next external baseline for web/social graphs; no promise of these ratios on random or biological graphs. [Authors' paper summary](https://vigna.di.unimi.it/papers.php), [implementation](https://github.com/vigna/webgraph). |
| k²-tree / compressed adjacency quadtree | The extended Web-graph study reports **1–3 bits/link** on its datasets, supporting forward and backward navigation. This is a workload-specific result. | Exploits large empty matrix regions and clustering. Unstructured sparsity may compress much less; navigation is a tree walk. | Candidate when incoming navigation/range queries are required. [Paper](https://www.sciencedirect.com/science/article/pii/S0306437913001051). |
| Dense bit matrix | Exactly `n²` bits before padding/indexes for a directed graph; no stored endpoint IDs. For large n it beats 32-bit target storage when density exceeds roughly **1/32**, ignoring CSR offsets. | Excellent for dense graphs and bitwise operations. Terrible for huge sparse graphs. Row scanning may visit empty words. | Required future dense backend; the initial CSR-family codecs are not a universal solution. |
| Roaring-style hybrid bitmaps | Within each 2^16-ID container: array **2c bytes**, bitmap **8,192 bytes**, or run encoding **2+4r bytes**, plus headers/keys/offsets. Here c is cardinality and r is number of runs. | Good for mixed density, long runs and intersections. A separate bitmap per low-degree vertex can have excessive overhead. | Candidate for dense rows and set operations, not an assumed global winner. [Official format](https://github.com/RoaringBitmap/RoaringFormatSpec/). |
| Grammar/reference compression | Depends on repeated subsequences/rows and grammar size, plus navigation indexes. No honest graph-independent percentage. | Potentially strong on repetitive graphs; random access and construction can be expensive. | Later, only with a repetitive workload and an external baseline. |
| Succinct trees: BP / LOUDS | Ordered tree topology can use **2n+o(n) bits** with suitable navigation support. Arbitrary original IDs and labels cost extra. | Applies to trees, not general directed graphs. Replacing a graph by a BFS tree loses non-tree edges. | Separate specialized backend if users need trees. [Succinct trees](https://users.dcc.uchile.cl/~gnavarro/ps/tcs16.2.pdf). |
| BOSS / succinct de Bruijn graph | DNA edge topology has the familiar **4m+o(m)-bit** representation under the BOSS model; practical sentinel edges, indexes, labels/coverage and lookup support add costs. | Relies on overlapping fixed-length labels over a small alphabet. It cannot encode an arbitrary graph at four bits/edge while preserving all information for free. | Keep the biological adapter separate. [Author explanation](https://www.alexbowe.com/succinct-debruijn-graphs/). |
| Bloom-filter de Bruijn graphs | Membership size depends on false-positive target; an ordinary Bloom filter alone is approximate. Exact graph implementations need correction information. | Small alphabet gives a bounded set of possible neighbors to probe; arbitrary graphs do not have that property. | No approximate generic graph backend in the exact API. |
| Dynamic packed arrays / compressed chunks | Add update slack, chunk directories and mutation indexes to a base codec. E.g. 70% payload occupancy alone multiplies its array space by `1/0.7`, before compression. | Supports updates but sacrifices some static density and complicates cursors. | Defer; initial API is immutable after construction. |

Reordering (BFS/DFS numbering, partitioning, locality order) is a preprocessing strategy, not a universal compressor. If arbitrary IDs must be recoverable, count the maps: two plain 32-bit forward/inverse maps add `8n` bytes. Unitig/path contraction changes the represented vertex set; recovering original vertices and sequences requires mappings/labels. General gzip/zstd files do not automatically support traversal in compressed form.

## A numerical example, not a benchmark

For exactly **1,000,000 vertices and 8,000,000 directed arcs**, preserved IDs, outgoing adjacency only, no attributes:

| Codec | Encoded bytes including our 120-byte framing | Saving against CSR32 |
|---|---:|---:|
| CSR32 with 64-bit offsets | 40,000,128 | 0% |
| Packed targets (20 bits), packed offsets (23 bits) | 22,875,128 | 42.81% |
| Packed targets plus our EF offsets and rank directory | 20,672,024 | 48.32% |

These are layout calculations. They say nothing about BFS/DFS runtime or total RSS. The actual old 1M random experiment had 7,999,975 arcs, so its exact bytes differ. A dense bit matrix for 1M vertices needs 125 GB before indexes and is clearly inappropriate for this sparse example.

For arbitrary labeled simple directed graphs with exactly m arcs, there are `choose(n²,m)` possible topologies. A representation capable of handling all of them needs at least `log2(choose(n²,m))` bits in the worst case, before access support/attributes. Structured graph families can do much better because their possibilities are restricted. This is why a universal four-bit-per-edge generic graph promise would be wrong.

## What we chose and implemented

Use one cursor interface with four explicit storage choices: `OutCsr32`, `ByteGraph32`, `PackedCsr32`, `EfCsr32`. CSR remains the speed reference. Packed CSR tests the cheap removal of unused bits; EF tests whether extra row compression is worth its lookup cost. Byte coding tests local neighbor gaps. There is no automatic claim that the smallest codec is the fastest.

The new core is header-only C++17, with an exported CMake target `sega::core` and an installable package. It does not link MEGAHIT or SDSL. Save/load currently uses POSIX file creation. The installed helper headers are our own MIT code. Native and wavelet adapters retain their separate build options/licenses.

BFS streams `(vertex,distance)` with one bitmap and one queue: about `n/8 + 4W` payload bytes with 32-bit queue entries, plus deque/allocator overhead. DFS streams discovery/finish events with two-bit colors and resumable frames: about `n/4 + frame_bytes*D`, plus deque overhead. W is maximum queue occupancy and D is depth. Current raw/packed cursors are 16 bytes; byte cursors are 24 bytes; resulting DFS frames on this machine are 24 and 32 bytes. These algorithms **do not compress graph topology**. Arbitrary-degree DFS here is not the four-bit bounded-degree predecessor-index algorithm.

Two-frontier `bfs-se` / `bfs-se32` are excluded from the generic core API and all new measurements. No incoming graph is built implicitly; reverse queries need an explicit backend/index. We do not claim that succinctness automatically gives cheap reverse adjacency.

See [core API and reproduction commands](generic-core.md). New measurements and exact logs are in `../../output/sega-core-2026-10-29/`; their generated result table is linked from that guide.
