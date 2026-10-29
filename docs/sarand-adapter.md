# Sarand on SEGA: experimental application adapter

This page records the wavelet experiment. The subsequent working
[native MEGAHIT adapter](megahit-sarand-adapter.md) has separate implementation,
tests and results; the savings below do not automatically apply to it.

This prototype replaces Sarand's gfapy/NetworkX graph construction and bounded
path enumeration. Target alignment, CD-HIT clustering, target-context merging,
ORF annotation and coverage filtering still use the pinned upstream Sarand code.
The full runnable harness is in `../../research/neighborhoods/`.

## Observed results — 28 October 2026

Both complete workflows passed all eight paired output comparisons. The minimal
example also passed all seven comparisons against Sarand's upstream fixture.
The raw pre-clustering candidate files matched byte-for-byte in their original
record order (three paths in the minimal example, five in Zymo).

| Diagnostic measurement | Original Sarand | SEGA adapter |
|---|---:|---:|
| Minimal: 95 segments, extraction peak tree RSS | 191.5 MiB | 197.6 MiB |
| Minimal: whole-pipeline peak tree RSS | 292.6 MiB | 295.9 MiB |
| Zymo: 63,231 segments, extraction peak tree RSS | 610.7 MiB | 150.7 MiB |
| Zymo: whole-pipeline peak tree RSS | 610.7 MiB | 432.6 MiB |
| Zymo: extraction wall time, including graph load | 37.23 s | 3.09 s |
| Zymo: entire profiled process wall time | 111.35 s | 85.46 s |

On Zymo the extraction peak was about 75% lower and the pipeline peak about 29%
lower. Target alignment, which is unchanged, now sets the pipeline's memory
peak. The tiny example has no memory win. These are one-repetition diagnostic
measurements, not significance-tested performance estimates. Some compilation
and native tests overlapped the early part of this experiment, so timing is
especially provisional. Repeat isolated runs before making performance claims.

Tree RSS is sampled every 50 ms and can double-count shared pages. It is not
unique physical memory or a stage allocation delta. The separate 100 ms guard
can observe a slightly different peak. All four pipeline runs completed under
the 2,048 MiB guard with no profiler sampling errors.

Zymo's retained graph has 126,462 oriented vertices and 54,044 arcs. Its topology
occupies 168,958 serialized component bytes; packed forward DNA is 12,173,879
bytes, and reported numeric metadata is 1,160,346 bytes. These totals omit name
strings, hash-map allocation, caches and build scratch and must not replace RSS
as the memory comparison. Reverse adjacency storage is zero.

Only two directional graph searches were needed in each real example; many
gene contexts lie inside long segments. This is evidence for replacing graph
storage/loading, **not evidence of a difficult traversal workload being solved**.
The 816 synthetic differential queries cover additional traversal cases.

The paired Python suite passed 48 tests (40 adapter tests and 8 harness tests).
Native assembly and general-graph suites also passed in the Release build and
under AddressSanitizer/UndefinedBehaviorSanitizer. LeakSanitizer is unsupported
on this Mac; the successful run disabled leak detection. The initial invocation
that requested unsupported leak detection is retained separately in the logs.
Complete logs, source/input/library hashes, profiles and checks are in
[`../../output/sega-sarand-comparison/`](../../output/sega-sarand-comparison/).
The original `sha256.json` covers files present when the paired run completed;
`supplemental-audit.json` records the later raw-candidate and hash audit.
`final-provenance.json` records native build/dependency information and hashes
for the later verification artifacts.

## Representation and navigation

`AssemblyGraph` imports the same compacted GFA1 segments and links used by
Sarand. Each segment has two oriented vertex IDs, but only one forward DNA
sequence is stored, packed into two bits per base. Reverse-complement strings
are decoded on demand. The topology uses SEGA's existing
`WaveletGraph<true>` (SDSL RRR wavelet tree with rank/select).

An incoming neighbor is recovered by selecting its occurrence in the same
forward target sequence. The new `incoming_edge` accessor also returns the
original edge position, so both directions share the same overlap array. No
reverse topology or reverse adjacency array is allocated. Node names, coverage,
overlaps, and forward insertion order are retained as separate metadata.
Rank/select support is included in topology storage; zero reverse-adjacency
bytes does not mean navigation support itself is free.

In either traversal direction the edge weight is the destination segment length
minus overlap. Reversing direction can therefore change the weight. A logical
reverse view computes that weight without mutating shared edge attributes.

This is SEGA's wavelet backend, **not the MEGAHIT/BOSS backend**. MEGAHIT already
supports both navigation directions, but its stored k-mer-edge IDs differ from
Sarand's compacted segment IDs. A faithful port required unitig/sequence/coverage
mapping, now implemented in the linked native adapter. Rebuilding a different graph and comparing
only reachable vertices would not establish Sarand equivalence.

## Traversal contract

Sarand searches a weighted ego ball extended by one adjacency step, then
enumerates simple paths in the induced graph. It emits paths reaching the
base-pair threshold and paths ending at dead ends or blocked cycles. Clustering
can depend on record order, so the adapter preserves both ordered paths and
their spelled sequences, including the original adjacency insertion order.

The adapter uses one active path, a path-membership set and pending neighbor
choices at each depth. It writes completed candidates directly to CD-HIT input
instead of retaining a dictionary of all completed path sequences. Its weighted
distance map, local adjacency cache, frontier, pending choices and downstream
candidate merging still consume memory. Enumeration can remain exponential in
branching; no asymptotic solution to that problem is claimed.

This is a new application-specific enumerator. It is **not** an execution or
validation of the paper's visited-once `dfs-se` algorithm. The two-frontier
`bfs-se`/`bfs-se32` variants are excluded.

The pinned Sarand code has special Python zero-slice behaviour: `[-0:]` selects
the whole start sequence, while `[:-0]` selects an empty prefix. Compatibility
tests deliberately preserve those cases. Matching the implementation does not
establish biological correctness of those boundary conventions.

## Scope and failure behaviour

- GFA1 H/S/L/P records; P records do not create extra links.
- Explicit uppercase A/C/G/T sequences, KC integer coverage, integer `M`
  overlaps no longer than either incident segment.
- Identical repeated links collapse; conflicting parallel-edge overlaps fail.
- One core in the Sarand orchestration adapter. Other core counts fail explicitly.
- Default maximum of 100,000 emitted paths per directional query. Exceeding it
  or the time limit is a failure, not an accepted truncated answer.
- Per-command sampled process-tree RSS stop threshold: 2,048 MiB. This is not a
  kernel hard limit and shared pages can be counted more than once.
- Builder scratch, labels, lookup maps and Python caches are not hidden in the
  measurements: process RSS is recorded as well as selected component sizes.
- No 350 GB runs, no read-to-graph construction, and no spacegraphcats port.

## Build and reproduce on this configured workspace

From `/Users/feeka/Documents/ChatGPT/algorithmic_dbg`:

```sh
cmake -S sega -B sega/build-general -DSEGA_BUILD_GENERAL=ON
cmake --build sega/build-general --target sega_assembly test_assembly_graph -j 2
research/neighborhoods/.venv/bin/python research/neighborhoods/compare_sega.py --out output/sega-sarand-repeat --zymo
```

Use a fresh output directory. Omitting `--zymo` runs only the 95-segment fixture.
The runner selects `.dylib` on macOS and `.so` on Linux, but a Linux machine needs
its own dependency installation and build; the existing macOS environment is
not portable. The original Sarand examples and native tools must be installed.

The runner records source/library/input hashes, package versions, raw outputs,
per-stage events, OS process peaks, sampled process-tree peaks, guard outcomes,
and exact output-comparison results. Timings are one diagnostic repetition with
profiling overhead; they are not a controlled publication benchmark.

## Correctness checks

The Python differential suite compares the adapter directly with unmodified
Sarand on 816 directional queries: 24 seeded random graphs and targeted zero
overlap, zero weight, cyclic and branching fixtures. It compares ordered path
tuples and sequence strings, not just visited-vertex counts. It also checks all
oriented nodes/edges of the real minimal fixture, degree greater than four,
invalid input, cancellation, output limits and sink failures.

Full pipeline checks compare target sequences, overlap groups, raw neighborhood
sequences, path coverage, ORF annotations, filtered annotations, final-context
CSV and final-context FASTA. FASTA headers remain paired with their sequences;
duplicates and letter case are preserved. These are regression/equivalence
checks, not comparisons with biological ground truth.

Any gain combines lower-overhead GFA loading, graph encoding, packed DNA,
elimination of the reverse copy and the changed path enumerator. Attribution to
one change requires ablation experiments. The old SEGA DFS memory claim does
not follow from this experiment.
