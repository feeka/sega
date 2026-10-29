# Proposal: compact graph storage and memory-conscious traversal

Implementation update: the cursor/byte-storage/save-load revision is now available.
See [the implemented API and its limits](compact-cursor-api.md). The proposals
below that go beyond that guide remain future work.

Status: broader design proposal, 2026-10-26. A smaller C++ wavelet-tree prototype
is now implemented; see [implemented scope and commands](general-graph-prototype.md).
The million-vertex pilot informs the [next generic API direction](general-graph-direction.md).
The Python API examples below remain illustrative and
are not implemented commands or bindings. Biological MEGAHIT measurements and
the synthetic general-graph pilot cover different backends and workloads; they
do not establish performance for the unimplemented API features below.

## Purpose and scope

Provide an immutable graph representation and traversal API for applications
that need to explore large graphs within limited RAM. Separate three questions:

1. How much memory does the graph representation require?
2. How much additional memory does each traversal require?
3. What do construction, loading, traversal and output cost in time?

Any finite static graph topology can be represented. A particular encoding
cannot promise a size reduction for every graph, and compression cannot promise
a runtime improvement. Use “compact” or “compressed” for the general library;
reserve “succinct” for a representation with a stated information-theoretic
space bound, including its navigation indexes.

The first implementation should support arbitrary-degree, static, unweighted
simple directed graphs, with an explicit undirected import mode. Include
isolated vertices through a vertex list or declared dense vertex count; do not
infer the entire vertex set from edges. Preserve self-loops. Reject duplicate
edges unless the caller explicitly requests deduplication. Undirected import
creates both directions, with one stored arc for a self-loop.

Multigraphs, weights, attributes and dynamic updates need separate contracts.
Reject unsupported input instead of silently dropping information. An ID map
must preserve the association with original vertex identifiers; account for
its storage. Existing compressed biological graphs should also be usable
through an adapter without converting them to a general representation.

## Proposed user interface

C++ should remain the implementation core. A thin Python interface can expose
construction and coarse operations later. Python callbacks on every vertex
must not be the performance baseline for billion-vertex traversals; provide
native sinks and batched output.

Illustrative Python interface, not executable today:

```python
graph = sega.build(
    edges="edges.tsv",
    vertices="vertices.tsv",
    directed=True,
    representation="compressed_csr",
    incoming_index=True,
    destination="graph.sega",
)

graph = sega.open("graph.sega")
print(graph.storage_report())

run = graph.bfs(source=source_id, output=sega.file_sink("distances.bin"))
run = graph.dfs(source=source_id, policy="packed_blocked_stack",
                output=sega.count_sink())
print(run.report())
```

Construction and opening are separate operations. Conversion does not mutate
or release a caller's original graph. Copying an already resident graph may
temporarily increase memory use; report that construction peak explicitly.
For inputs that do not fit during construction, an external-sort builder is a
separate milestone, with disk use and temporary files included in its report.

Default traversal output should be a count/statistics sink. Explicit sinks can
write distances, parents, discovery/finish events or batched records. Returning
an N-element Python list would defeat the intended memory savings. Output
memory and I/O must be counted when an application requires them.

## Storage and navigation layers

| Layer | Initial design |
| --- | --- |
| Import | Edge list and C++ graph adapter; validate endpoints and declared semantics; explicit ID mapping |
| General baseline | CSR with the narrowest safe identifier width; optional incoming CSR |
| General compression | Sorted adjacency with gap coding, list offsets and decoding indexes; compare against the baseline including all indexes |
| Biological backend | Keep the existing MEGAHIT navigation adapter and its native representation |
| Traversal | Streaming bitmap/FIFO BFS, packed colors with a blocked DFS stack, and eligible predecessor-reconstruction DFS |
| Output and reporting | Native sinks, completion status, storage breakdown, allocations, RSS and phase timings |

The general navigation interface needs a vertex range, outgoing-neighbor
iterators and stable ordering. Incoming-neighbor access, indexed predecessor
lookup and degree bounds are optional advertised capabilities. Avoid temporary
arrays proportional to a vertex's degree. Iterators/cursors must have explicit
lifetime, allocation and decoding costs.

The present `NavOracle` uses fixed arrays of four neighbors and enforces degrees
at most four. Keep it as a specialized adapter while introducing a general
interface; removing its degree check alone would be unsafe.

Store an incoming index only when requested or needed by a selected policy.
BFS and explicit-stack DFS need outgoing access; predecessor reconstruction
needs incoming access too. Include the full cost of that reverse index when
comparing policies. An adapter changes how a graph is accessed; it does not
itself compress an existing graph further.

If reordering vertices helps compression, make it explicit, preserve the
inverse mapping, and record the permutation. Exact DFS event comparisons
require matched neighbor and root order. For different orders, validate DFS
semantics instead of expecting identical trees.

## Traversal policies and the arbitrary-degree obstacle

Streaming BFS uses a visited bitmap, a queue and layer counters. Its queue can
still be large. A callback can consume distances without retaining a complete
distance array; applications requiring that array must pay for it.

The general DFS baseline should use packed colors, a blocked stack and
resumable neighbor cursors. Blocking avoids the vector-growth artifact in the
current comparison. Cursor storage belongs in the memory report.

The current reconstruction DFS remains a specialized policy for the supported
bounded-degree navigation contract. It stores two color bits and two parent
index bits per identifier. It cannot retain that four-bit bound unchanged on
arbitrary-degree graphs:

- Choosing among d incoming neighbors needs ceil(log2(d)) parent-choice bits
  for d > 1. Degree-zero and degree-one vertices need no choice bits. A packed
  variable-width layout also needs a way to locate each entry; count that index.
- The current algorithm rescans successors after returning from a child. This
  is bounded work per return at degree four. On an arbitrary-degree star it
  can inspect quadratically many neighbor entries. General support needs
  resumable cursors or another analyzed mechanism, with its state included.
- Finding and decoding an incoming parent position has backend-dependent cost.
  A compressed representation does not imply constant-time indexed navigation.

Degree-aware parent storage is a candidate to investigate, not a demonstrated
new result. Prove its space/time bounds and check prior work before presenting
it as a contribution. Do not select it automatically simply because a graph
has an incoming index.

Keep policy selection explicit initially. A later planner could report
compatible policies and estimates, including assumptions about traversal
depth or frontier size. A memory budget for library allocations is different
from a process RSS limit: callbacks, allocators and mapped pages affect RSS.
If a supported allocation budget is exceeded, fail with an incomplete-run
status; never silently omit vertices or claim the traversal finished.

The two-frontier BFS implementations `bfs-se` and `bfs-se32` remain excluded
from the paper, default experimental commands and proposed public policies.

## Evidence required for the paper

The central research question is whether joint choices of graph representation
and traversal state improve total memory/time tradeoffs on useful workloads.
An interface by itself does not establish algorithmic novelty.

Compare four combinations where capabilities allow: ordinary storage with an
ordinary traversal, compressed storage with that same traversal, ordinary
storage with reduced traversal state, and compressed storage with reduced
state. Match input semantics, output requirements, ID widths and thread counts.
Report necessary reverse indexes even if only one policy needs them.

Record construction and load time, construction peak RSS, graph and ID-map
bytes, traversal allocation peak, traversal/process RSS, decoding/navigation
counts, wall time, output bytes, checksums and completion status. Show both
single-query end-to-end cost and repeated-query cost; construction may only
pay off after reuse. Runtime improvements are hypotheses about locality and
memory bandwidth, weighed against decoding and reconstruction overhead.

Validate empty graphs, isolated vertices, self-loops, disconnected graphs,
paths, cycles, high-degree stars and skewed-degree graphs. Check BFS distances
against an independent reference; check DFS parent edges, discovery/finish
invariants and complete reachability, plus exact events when order is matched.
Audit decoded topology and ID-map round trips. The current bounded-degree
tests cannot establish arbitrary-degree correctness.

Use real graphs from multiple structural families and at least one useful
application, such as bounded neighborhood extraction around biological seed
sequences. The existing single-graph result does not yet show a compelling
total-RAM advantage for reconstruction DFS over the strongest explicit-stack
alternative; test the blocked-stack baseline before building the narrative
around that advantage.

Relevant existing work to compare with, rather than claim the general idea is
new:

- [WebGraph](https://vigna.di.unimi.it/ftp/papers/WebGraphI.pdf) provides graph
  compression and access to compressed adjacency through a library interface.
- [Ligra+](https://www.cs.umd.edu/~laxman/papers/Ligra%2B.pdf) studies graph
  processing on compressed representations, including memory/runtime
  tradeoffs. Match parallelism when interpreting comparisons.
- [SDSL](https://github.com/simongog/sdsl-lite) provides reusable succinct data
  structures; it is a possible building block, not a traversal baseline.

This is a starting set of related work, not a completed novelty review.

## Implementation order

1. Add the blocked-stack baseline and freeze a trustworthy memory comparison.
2. Add the arbitrary-degree navigation interface, ordinary CSR backend and
   streaming BFS/general DFS with independent correctness tests.
3. Add one compressed backend and storage/construction reports. Measure all
   four combinations above before adding more formats.
4. Investigate variable-width predecessor reconstruction, including cursor and
   reverse-index costs. Retain it only where measured tradeoffs justify it.
5. Add a thin Python interface and one end-to-end application; evaluate against
   relevant existing systems and publish reproducible construction recipes.

The first useful release is therefore a small C++ library with a general graph
backend and honest accounting. A universal converter, automatic policy chooser,
dynamic graph engine and complete biology toolkit are separate projects.
