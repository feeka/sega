# Direction after the million-vertex pilot

Implementation update: the cursor/byte-storage/save-load revision is now available.
See [the implemented API and its limits](compact-cursor-api.md). The proposals
below that go beyond that guide remain future work.

Proposed scope, 2026-10-26. This document identifies the next implementation and
evaluation target; it does not claim those changes are already implemented.

## Useful purpose

Build a graph exploration API for large static graphs that can execute local
queries and traversals on native compact storage, stream results, and account
for query workspace. The initial application is exact, explicitly defined
neighborhood extraction around biological seed sequences. Generic graph import
and native biological adapters share traversal code without requiring every
backend to adopt the same storage representation.

The million-vertex pilot rejects the current wavelet backends as defaults for
fast neighbor scans. It does not establish that a generic graph API is a bad
idea. The pilot compared one family of representations with CSR, not the wider
space of practical compressed adjacency representations.

## Evidence and design consequences

The pilot used 1,000,000 vertices and 7,999,975 directed edges. Encoded graph
sizes were 76.29 MiB (bidirectional CSR32), 29.98 MiB (plain wavelet), and
21.90 MiB (RRR wavelet). Unhashed BFS times were 0.186, 12.68 and 178.33 seconds;
DFS times were 0.300, 12.32 and 178.24 seconds. There was one timing sample per
method, and all topology and traversal hashes matched. Generation dominated the
roughly 495 MiB peak RSS. These are observations about this implementation and
one synthetic graph, not universal graph-compression bounds.

1. Prioritize sequential neighbor decoding. A BFS/DFS scan should not require a
   fresh traversal of the wavelet tree for every neighbor.
2. Make incoming navigation optional. BFS and the blocked-stack DFS do not need
   it. Include reverse storage when a workload actually requires that capability.
3. Build and save a graph once, then load and query it in separate fresh
   processes. Report construction peak as well as loaded/query RSS. Memory
   mapping alone does not guarantee a small resident set or a hard memory cap.
4. Replace the set-of-all-edges synthetic construction path for scale tests.
   Arbitrary unsorted input still requires sorting/deduplication or validation;
   account for that work, with a bounded-memory external builder as a later step.
5. Retain CSR as the fallback for graphs where compression does not help. Report
   the choice and its cost; do not advertise automatic universal improvements.

## API boundary that needs to change

The current general prototype exposes global edge positions through
`out_begin`, `out_end` and `target(position)`. This suits CSR and the wavelet
sequence but imposes an unnecessary global layout on implicit graphs.

The next core interface should expose a resumable outgoing-neighbor cursor,
vertex enumeration/validity, and optional incoming-neighbor cursors. The cursor
owns or refers to backend-specific decoding state; DFS preserves that state in
its frames. The interface must document cursor lifetime, ordering, scratch
allocation and cost. Avoid virtual dispatch per edge in the C++ core.

This lets a MEGAHIT adapter enumerate native neighbors without constructing a
separate adjacency graph. A CSR cursor stores positions; a byte-coded cursor
stores a byte position and previous decoded ID; the wavelet adapter can remain
an experimental reference. Arbitrary-degree and bounded-degree algorithms must
check their own capability requirements. In particular, the four-bit DFS cannot
be used on arbitrary-degree or combined incoming/outgoing neighborhoods unchanged.

## First ordinary-graph backend

Prototype sorted adjacency encoded as gaps with an established byte-oriented
codec, stored in independently decodable blocks when rows are large. Keep row
offsets and only the restart metadata that the cursor requires. Preserve a
stable ID mapping if a later ordering optimization is introduced.

This is established engineering, not a claim to a new compression method:
[Ligra+](https://www.cs.umd.edu/~laxman/papers/Ligra%2B.pdf) and
[GBBS](https://github.com/ParAlg/gbbs) provide relevant designs and comparison
targets. Reuse a compatible implementation or codec after inspecting its
interface and license. The first comparison is outgoing-only CSR versus
outgoing-only compressed adjacency using identical streamed outputs. Add a
matched bidirectional comparison for reverse-neighborhood workloads.

Success means a useful measured memory/time tradeoff on relevant graph
families, not necessarily a speedup over CSR. Do not choose a favorable baseline
or graph ordering just to create a positive compression result. Include weakly
compressible graphs and the cost of mappings, offsets and reverse indexes.

## First complete application

Input: a large biological assembly graph, a seed sequence or sequence set, and
an explicit neighborhood definition. Map seeds to valid graph IDs, retrieve
the exact specified neighborhood, and stream graph IDs or sequence output.

Start with a hop-limited neighborhood whose direction is explicit. Base-pair
distance on compacted unitigs is a different, weighted query; do not call it
ordinary BFS or equate it with a fixed hop radius. Sequence reconstruction,
reverse-complement handling and query mapping need separate validation.

Support multiple queries against the same loaded graph. Investigate reusable
workspace and sparse-versus-dense visited state to avoid repeatedly clearing a
graph-sized array for a tiny local query. This is a candidate engineering
improvement, not an established novel algorithm. Stream output so a query does
not accidentally accumulate a large result object.

[spacegraphcats](https://doi.org/10.1186/s13059-020-02066-4) demonstrates biological
value for assembly-graph neighborhoods. It is related work and a useful
application comparator, not evidence that our tool already recovers additional
sequence. Its partition-based neighborhoods differ from exact hop neighborhoods;
comparisons must explicitly account for those semantics.

## Scope of a workspace budget

An initial budget can cover library-owned traversal scratch allocations,
including visited state, queue/stack and cursor buffers. It is not a promise
about total RSS, mapped-file page cache, or caller-owned output. Return a clear
incomplete/error status if the budget is exceeded; never silently truncate a
result and report success. An exact disk-spilling policy would be a subsequent
feature with separately reported I/O costs.

## Next deliverable and decision point

Implement the neighbor cursor contract, an outgoing-only CSR baseline, one
byte-coded adjacency backend, and save/load support. Test exact navigation and
events on the existing small cases, then repeat the million-vertex comparison
in fresh query processes. Keep the wavelet results as an ablation.

If this backend achieves a reasonable tradeoff, implement bounded multi-source
neighborhood queries and the first biological adapter/output path. Evaluate an
actual task: correctness and completeness of requested output, query latency,
loaded and peak memory, construction cost, repeated-query cost and independent
biological validation. Existing optimized libraries belong in the comparison.

The potential paper contribution is the demonstrated combination of native
graph adapters, workspace-conscious query execution and a useful biological
workflow. An API alone does not establish research novelty. Novel traversal
policies, if developed, require their own analysis and prior-work review.

The two-frontier BFS implementations remain excluded throughout. The generic
API is the continuing objective; one unsuccessful default backend does not
determine the outcome of the whole library.
