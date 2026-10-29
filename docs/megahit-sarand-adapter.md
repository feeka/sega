# Sarand on native MEGAHIT SDBG

Implemented and verified on 28 October 2026. This adapter uses MEGAHIT's actual
BOSS/SDBG representation through SEGA's `MegaHitOracle`. It does not use the
wavelet backend or SDSL. Both upstream source trees remain unmodified.

## Results

| Example | Valid native edges | Unitigs | Complete workflow comparison |
|---|---:|---:|---|
| Synthetic mepA gene with branching flanks | 6,021 | 12 | All 11 checks match |
| Internal gene variant plus branching flanks | 6,085 | 15 | All 11 checks match; two gene hits each span three unitigs |

Both examples also contain isolated edges, a palindromic unitig and two circular
components. They incorporate the mepA target from Sarand's pinned fixture with
deterministically generated surrounding DNA. These are controlled software
examples, not environmental metagenomes or population-level biological evidence.

The comparisons cover target sequences, overlap groups, target anchors, raw
contexts, path coverage, ORF annotations, filtered annotations, final CSV/FASTA
contexts, pre-clustering candidates and their exact order. Each example produced
four unclustered directional candidates and one final context after the unchanged
clustering/annotation/filtering stages.

Additional checks passed:

- **1,332 ordered directional queries**, covering **13,392 terminal path
  records**, compared against unmodified Sarand. These include a complete k=1
  graph, both directions, distance boundaries, cycles and palindromes.
- Every native k-mer label, incoming/outgoing adjacency and unitig coverage
  checked against the original input sequences on both k=31 examples.
  Self-reverse-complement k-mers count once per occurrence, as in MEGAHIT's
  `read_to_sdbg_s2.cpp`; they must not be counted twice by the reference audit.
- **15 Python tests** in the paired runner and **two additional failure tests**
  for mismatched GFA input and an exceeded path-output limit.
- Native checks on all three graphs, including AddressSanitizer and
  UndefinedBehaviorSanitizer. Leak detection is unsupported on this Mac and
  was disabled.
- A separate smoke run of the general `run_native_sarand.py` command, passing
  the graph directory directly, completed successfully.

## Memory interpretation

| Whole-pipeline sampled process-tree peak | Original Sarand | Native adapter |
|---|---:|---:|
| Branching-flank fixture | 199.0 MiB | 221.5 MiB |
| Gene-spanning fixture | 203.7 MiB | 220.8 MiB |

**These small examples do not demonstrate a memory saving.** They establish a
working native integration. The earlier wavelet/Zymo savings are a different
result and must not be transferred to this backend. One diagnostic repetition
with variable startup and external-tool times does not establish a speedup.

Summed parent/child RSS can count shared pages repeatedly. Each comparison
command uses a 2,048 MiB sampled stop threshold, not an OS hard limit. All
comparison commands finished below it. No 350 GB or large-metagenome run was
attempted.

## Representation and traversal

The adapter builds maximal nonbranching unitigs from the supplied SDBG. It
retains each unitig's forward/reverse endpoints, length and summed multiplicity,
plus sorted endpoint-to-unitig indexes. It retains no outgoing adjacency,
incoming adjacency or additional DNA array. Neighbors come from
`MegaHitOracle::outgoing` and `MegaHitOracle::incoming`; sequences are spelled
from the SDBG labels. Python caches a bounded number of decoded node records.

The current index costs **120 bytes per unitig** in the measured build. During
construction, a temporary one-bit-per-stored-edge visited bitmap is also needed.
These costs are additional to the SDBG and are included in RSS. Per-edge
multiplicities are released after coverage has been summed. This is a first
unitig index, not a claim of optimal succinct unitig metadata.

The exported GFA supplies stable names for Bandage/Sarand gene alignment and
exactly the same graph for the baseline. Names, orientations and base offsets
map back to native unitigs. Optimized extraction loads SDBG, not GFA into
NetworkX. It recomputes a deterministic GFA digest from the native view and
rejects mismatched input before accepting the anchors.

GFA still exists on disk for alignment, and Bandage loads its own graph.
Exporting, reloading, digest verification, alignment, clustering, annotation
and context pairing still have costs. Export and extraction use separate
processes with separate logs. Read-to-SDBG construction is logged separately
and is excluded from the reported pipeline measurements.

Palindromic unitigs have two orientation aliases to match GFA/Sarand semantics;
they can share physical native edges. Simple-path membership is defined on
oriented unitig IDs, as in Sarand, not on unique physical k-mers. The complete
k=1 fixture therefore has 16 native edges but 20 oriented unitig vertices and
100 logical arcs.

The exported `KC` tag is summed native multiplicity. The runner selects Sarand's
`bcalm` option solely for its `KC / (sequence_length - k)` coverage formula.
The underlying graph is built by MEGAHIT; no BCALM graph builder is used.

The path enumerator preserves Sarand's weighted ego ball plus one step,
oriented simple paths and record order. It uses one active path, pending branch
choices and streamed completed candidates. This is application-specific
enumeration, **not the paper's visited-once `dfs-se` algorithm**. The two-frontier
BFS variants are excluded. Branching can still produce exponentially many
paths, and context pairing/clustering can still dominate memory.

## Larger native scaling test (28 October 2026)

`../../output/megahit-sarand-scaling/report.html` contains a larger, fully
audited synthetic comparison. It uses the same pinned native library and
upstream tools; no traversal implementation was changed for this experiment.
Two graphs contain 1,011,810 and 5,034,210 valid native edges, compacted into
11,246 and 56,046 unitigs respectively. Background components contain SNP
bubbles. Both graphs have the same mepA query with an internal SNP and seven
SNP bubbles on each flank, yielding 128 paths in each direction before
clustering. The local induced neighborhood has 22 oriented vertices per
direction; this is not an exhaustive traversal of millions of edges.

The 1m pilot had essentially equal pipeline peaks (227.7 MiB baseline and
228.2 MiB native). At 5m, three sequential paired repetitions gave:

| Measurement (median; min–max) | Sarand baseline | Native MEGAHIT/SEGA |
| --- | --- | --- |
| Full-pipeline peak tree RSS, MiB | 454.5 (449.0–454.7) | 246.1 (246.1–246.5) |
| Extraction-stage peak tree RSS, MiB | 454.5 (449.0–454.7) | 216.0 (161.4–241.6) |
| Full pipeline, seconds | 156.9 (147.2–203.8) | 122.6 (114.8–126.0) |
| Extraction, seconds | 65.2 (64.8–66.2) | 12.6 (8.5–17.1) |

This is a 45.8% reduction in median full-pipeline RSS and a 5.16x reduction in
median extraction time on this workload. Extraction includes graph loading
and native input verification. Alignment is unchanged and varies substantially
in duration; construction and GFA export are measured separately. RSS is a
sampled process-tree sum, not PSS, and stage peaks are absolute RSS rather
than allocation deltas. Every command stayed below the 2,048 MiB guard.

All 44 paired output checks passed across the four pairs: identical target
anchors, exact order of 256 candidate records per run, clustered paths,
coverage, annotations and one final context. The independent input audit
checks all canonical 32-mers, every unitig's coverage and all exported arcs;
256 native orientations per graph are additionally sampled for sequence and
bidirectional navigation. All eight profiles have complete sampling without
reported sampling errors, and saved source/binary/output hashes were verified.

This supports a storage/integration benefit from avoiding expanded Python
graphs and their reverse copy. It does not isolate a speedup from the path
enumerator, validate the original visited-once DFS for this application, or
establish real-metagenome performance. Background components are disconnected,
and the same local query is used at both sizes. Uncolored SNP combinations are
intentional stress paths; clustering collapses the similar candidates.

To repeat with the existing native build and environment, from the workspace:

```sh
research/neighborhoods/.venv/bin/python research/neighborhoods/run_megahit_scaling.py --out output/megahit-sarand-scaling-repeat
research/neighborhoods/.venv/bin/python research/neighborhoods/render_megahit_scaling.py output/megahit-sarand-scaling-repeat
```

The output includes inputs and seeds, graph statistics, independent audits,
full commands, per-stage profiles, traversal counters, all pipeline products,
memory guards, version/source/binary hashes and a file checksum inventory.
The original completed run also includes `verification.json`.

## Reproduce in the configured workspace

From `/Users/feeka/Documents/ChatGPT/algorithmic_dbg`, using a fresh output name:

```sh
research/neighborhoods/.venv/bin/python research/neighborhoods/run_megahit_comparison.py --out output/megahit-sarand-repeat --build
```

This builds the pinned tools, prepares the fixtures, audits the native graph,
compares paths and runs both complete workflows. It requires the installed
Sarand environment and external tools. Native SEGA is built with
`SEGA_BUILD_GENERAL=OFF`.

The two supplementary failure tests can then be repeated against that output:

```sh
SEGA_MEGAHIT_FIXTURES="$PWD/output/megahit-sarand-repeat" research/neighborhoods/.venv/bin/python -m pytest -q research/neighborhoods/test_megahit_failures.py
```

## One command for a supplied graph

The general runner normalizes the prefix, exports the matching GFA, derives k,
verifies the mapping and runs native extraction. This exact local example works:

```sh
research/neighborhoods/.venv/bin/python research/neighborhoods/run_native_sarand.py --graph output/megahit-sarand-comparison/cross_gene --targets output/megahit-sarand-comparison/cross_gene/query.fa --out output/megahit-native-example
```

`--graph` accepts a prefix, its `.sdbg_info` file, or a directory containing one
graph. The previously supplied server directory
`/vol/d/data/copsac/SRR14017557/gpt_opt/graph` is therefore accepted without
manually appending another `/graph`. Multiple prefixes in one directory are
rejected as ambiguous. A target-gene nucleotide FASTA is required; no server
target-file location has been assumed.

The default RSS threshold is 2,048 MiB and the per-direction output limit is
100,000 paths. Exceeding a limit fails the run, rather than accepting partial
results. This implementation has not been run on the billion-edge server graph.
Its preprocessing time and SDBG/index memory still need measurement with an
appropriate server budget. macOS binaries/environments are not portable to
Linux; build the sources and install the dependencies there. No remote server
was accessed.

## Evidence and source

- Paired results: `../../output/megahit-sarand-comparison/summary.json`
- Commands, versions, source and library hashes: `manifest.json`
- Inputs, unitig mappings and GFA fingerprints: `fixtures/`, `cross_gene/`
- Per-stage memory/timing and per-query counters: `*-profile/`
- Tests, builds, memory guards and sanitizers: `logs/`
- Original-run hashes: `sha256.json`; later checks: `final-provenance.json`
- General-runner smoke output: `../../output/megahit-native-cli-smoke/`
- Native implementation: `../backends/megahit_unitig.hpp`
- Python integration: `../../research/neighborhoods/sega_adapter.py`

MEGAHIT is pinned to `c77d45c1f516e4f4a90c255c5c6d0c8ba27c017f`; Sarand to
`a7e7a8a22efc6e66a496708d01b15196f5cb2980`. Both are GPL components; see their
retained licenses and `../THIRD_PARTY.md`.

The adapter traverses the supplied graph state. It does not replay assembly
cleaning/pruning or claim equivalence to MEGAHIT's final contigs. A larger real
graph with representative gene queries, stage-level attribution and repeated
isolated measurements is still required for a performance claim.
