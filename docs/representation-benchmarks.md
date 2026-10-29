# Native compressed de Bruijn graph measurements

## How to read the results

A **starting point** is the graph vertex where a search begins. The raw files call it a `source`. Every tool receives the same starting DNA fragment for a given comparison. We try several starting points because different parts of a graph can behave differently. Results from different starting points are shown separately.

**Repeats** means running the same measurement again. `--repeats 3` runs each search or other operation three times in fresh programs, on the same graph, with the same starting point or query list. The report shows the middle of those three measurements. This does not mean three datasets or three different starting points. Construction is measured once per tool.

The graph tools are **MEGAHIT, MetaGraph, Bifrost, SBWT and SSHash**. **BCALM2** is tested for building/exporting DNA paths. **BFS** means breadth-first search: explore nearby vertices first. **DFS** means depth-first search: follow a branch before returning to explore another. **SEGA** supplies our search code; its stackless DFS is applied to each indexed tool. The comparisons measure these search implementations through each tool's graph interface, not entire genome assemblers.

There are four separate size measurements:

| Report label | Meaning |
|---|---|
| Saved graph size | Files on disk, not RAM |
| RAM after opening | Resident RAM of the whole program after it opens the graph, including runtime and access code |
| Extra search memory | Tracked queue, stack and visited/status-array storage; excludes the graph and allocator bookkeeping |
| Peak program RAM | Highest resident RAM during graph opening and the operation |

Use the same starting point and same search method when comparing tools. Native neighbor order can still differ; an identical starting vertex does not promise an identical visit order. Whole-graph searches (`forest`) restart at unvisited vertices in each tool's own order. They are shown separately because different restart orders can produce different path depths and memory usage.

The default is now `--traversal-scope sampled`: searches from the same selected starting points across tools. Existing saved experiments keep their original settings and measurements.

To rewrite an existing report in plain language without rebuilding graphs or rerunning measurements:

```sh
python3 scripts/report_representations.py /path/to/completed/results
```

## Tools and graph representations

This suite builds the same **unweighted, strand-closed, node-centric K-mer graph** in six established tools. The selection covers different representations; it is not a measured popularity ranking. Five tools provide persistent native indexes and traversal adapters. BCALM2 provides compacted graph output and the spectrum-preserving input used by SSHash.

| Tool | Representation tested | Navigation used by the probe | Persistent adapter state |
|---|---|---|---|
| [MEGAHIT](https://github.com/voutcn/megahit) | BOSS-style succinct de Bruijn graph | Native incoming/outgoing stored-edge navigation | No extra graph index; abundance data released after loading |
| [MetaGraph](https://github.com/ratschlab/metagraph) | Uncolored succinct BOSS, `stat` state | Native incoming/outgoing k-mer navigation | One validity bit per native identifier slot, rounded to words |
| [Bifrost](https://github.com/pmelsted/bifrost) | Compacted graph with its native k-mer index, BFG + BFI | Position arithmetic within unitigs; native unitig neighbor API at boundaries | Unitig descriptors, prefix offsets, and head-to-unitig map; included in process RSS |
| [BCALM2](https://github.com/GATB/bcalm) | Unitig FASTA with topology headers | No persistent query index supplied by this integration | Not applicable; no synthetic query benchmark is substituted |
| [SBWT](https://github.com/algbio/SBWT) | Plain-matrix subset BWT with streaming support | Native forward steps; inverse subset-select step followed by suffix-group enumeration for incoming neighbors | Native select support plus a bitmap for dummy columns |
| [SSHash](https://github.com/jermp/sshash) | Unweighted sparse/skew k-mer dictionary over BCALM2 unitigs | Native k-mer neighborhood API | ID/orientation arithmetic; no adjacency or inverse-ID array |

All algorithms operate through these native adapters. They do **not** first convert the graph to CSR, a Python graph, or stored reverse adjacency. The independent Python graph exists only in `mini` verification and is excluded from native process measurements.

## Installation and commands

From the `sega` directory, on a machine with Git, make, and a C++20 compiler:

```sh
python3 scripts/setup_representations.py --prefix ./representation-tools --jobs 2
./representation-tools/run mini --out results/representations-mini --repeats 3
```

Setup installs dependencies into its own directory, checks out exact source commits, builds probes, and creates `representation-tools/tools.json` and the `run` launcher. It does not install system-wide software or require sudo. If micromamba is unavailable, setup downloads its standalone binary. Environment packages, source/submodule revisions, compatibility patches, and build logs are retained. On macOS the current MetaGraph source also needs modern LLVM/libc++; setup detects Homebrew LLVM or accepts `--llvm-prefix /path/to/llvm`. The default Apple standard library supplied with older SDKs lacks `std::atomic_ref`.

The initial setup compiles native libraries, including MetaGraph's full core, and can take substantially longer than a mini experiment. Keep the prefix for subsequent runs; benchmarking itself does not repeat compilation.

Probes are compiled in **Release** mode. The manifest includes their actual compilation commands, compiler versions, source revisions, recorded compatibility patches, and the explicit package environment.

The MetaGraph builder is the Bioconda 0.5.2 executable; its probe is compiled from the separately pinned source. Both binary fingerprints and that distinction are recorded. The mini checks validate their file-format compatibility. On x86, the pinned SSHash implementation uses AVX2/BMI2 instructions; use a compatible CPU. The native mini tests in this workspace run on macOS; the Linux setup path still needs execution on the target server.

To run on a real dataset, replace the input filenames below with the actual FASTA/FASTQ paths:

```sh
./representation-tools/run run \
  --input reads_1.fastq.gz reads_2.fastq.gz \
  --out results/real-k31 \
  --k 31 --threads 8 --memory-gib 8 --repeats 5 \
  --queries 10000 --sources 8 --traversal-scope both
```

`--memory-gib 8` is a **sampled process-tree RSS stop threshold**, not a claim that this dataset fits in 8 GiB. Set it below the RAM available to this job. The controller stops only its own process group on over-budget samples or timeout. Sampling can miss brief peaks and can double-count shared pages; it is not a kernel-enforced memory cap. The Python controller and operating system also need memory. Construction tools receive their own, separately logged buffer settings. SBWT's KMC builder accepts a minimum requested budget of 2 GB, which is not a strict allocation guarantee.

`--repeats` repeats measurements in fresh processes. It does **not** rebuild every graph that many times: construction is measured once per tool. For repeated construction measurements, run into separate output directories. The schedule is reproducibly shuffled. OS caches are uncontrolled, so these are not cold-cache experiments.

`--traversal-scope forest` visits the full graph, selecting roots in each backend's native ID order. This is useful for comparing traversal implementations within a backend, but root order can differ across representations. `sampled` uses the same sampled source k-mers across all backends. `both` records these two workloads separately, including each source's identity; it does not pool their times.

Useful options:

```sh
# Check that the requested executables are present.
./representation-tools/run doctor

# A smaller measurement selection, still using the same validation protocol.
./representation-tools/run run --input reads.fa --out results/selected \
  --operations stats lookup neighbors bfs-stream dfs-packed dfs-se \
  --traversal-scope sampled --sources 8 --repeats 5

# Continue interrupted measurements with identical inputs, code, tools and settings.
# Repeat the original command and append --resume.
```

You can also invoke `python scripts/run_representations.py` directly with `--tools /absolute/path/tools.json`; that Python environment must contain `psutil`. Input is FASTA/FASTQ, optionally gzipped. An existing MEGAHIT graph prefix is **not** a reads filename and is not accepted as `--input` by this common-input construction suite. The existing MEGAHIT-specific experiment runner remains available for measuring already-built SDBG files.

## Graph semantics

- Shared K is currently **odd, 11–31**. The odd-K restriction avoids reverse-complement-palindromic vertex aliases in the two-orientation adapters. It is an explicit supported range, not an assertion that the upstream tools all have this limit.
- A logical vertex is an oriented DNA K-mer. An arc exists whenever its suffix of length K−1 overlaps another present K-mer's prefix. Arcs are **overlap-induced**, not restricted to consecutive k-mers observed in an individual input read.
- Input is normalized to A/C/G/T, split at ambiguity symbols, and closed under reverse complementation. FASTA/FASTQ records are chunked with K−1 overlap. This preserves the k-mer **set**, but changes occurrence counts. The suite therefore makes no abundance or coverage comparisons and uses an abundance threshold of one with graph simplification disabled.
- Bifrost uses `-r`, which retains singleton k-mers. Using its read-input default would silently change the graph.
- MEGAHIT stores traversal vertices as edges labeled with its internal `k+1` bases. Therefore common **K=31 uses MEGAHIT internal k=30**. Identical numerical k arguments across all six tools would compare different graphs.
- Canonical tools store a k-mer and its reverse complement together. Their adapters expose both logical orientations without storing both DNA strings. Native dummy identifiers and adapter-expanded identifiers are reported separately from valid vertices.
- Native unitig counts and oriented maximal nonbranching path counts have different definitions and are not treated as interchangeable.
- MetaGraph's raw BOSS adjacency can expose source/sink sentinel nodes. Its adapter filters those identifiers from both navigation and degree calculations; they are not biological k-mers.

## Measurements

Every native probe starts a fresh process, loads one index, constructs its documented adapter state, and performs one operation. Construction and probe logs are separate.

Probes set `OMP_NUM_THREADS=1` and `OMP_DYNAMIC=FALSE`; Bifrost loading explicitly uses one thread. `--threads` controls the builders. The effective OpenMP environment is included in each process record.

| Group | Recorded values |
|---|---|
| Construction | Exact argv, wall time, return code, OS-reported max RSS, sampled process-tree RSS peak, sampled CPU lower bound, buffer settings, sampled temporary/build-directory footprint, artifact sizes and hashes |
| Storage | Serialized bytes; bits per valid oriented and canonical k-mer when a graph census is available; SSHash's BCALM2 dependency identified and charged to total construction time |
| Loading | Load-plus-adapter time, loaded process RSS, process high-water mark after loading, adapter payload accounting |
| Topology | Valid oriented/canonical k-mers, native identifier slots, arcs, in/out-degree histograms, tips, isolates, branching vertices, self-loops |
| Components | Weak component count/largest component; strong component count/largest component/singletons |
| Nonbranching structure | Oriented maximal nonbranching paths, isolated directed cycles, isolated vertices, partitioned arc count, maximum path/cycle length in arcs |
| Queries | Lookup count/hits/time, both-direction neighbor queries, bounded deterministic walks with lexically ordered neighbors; branch-label recovery is included in walk timing |
| BFS | Distance-array baseline and bitmap/FIFO streaming BFS, each with 64- and 32-bit IDs; visited vertices, scanned arcs, layer width, pending queue maximum, auxiliary allocation peak |
| DFS | Byte-color explicit-stack baseline, packed-color explicit-stack baseline, and predecessor-index stackless DFS; depth, discoveries/finishes, parent recoveries, incoming calls, scans, and auxiliary allocation peak |

**The two-frontier `bfs-se` and `bfs-se32` variants are excluded and rejected by this runner and its native probes.** Streaming BFS is a conventional bitmap + FIFO implementation, not a new algorithm or a worst-case constant-width queue.

Graph storage, loaded process RSS, overall process peak RSS, and traversal auxiliary bytes are different quantities. `loaded_process_rss_kib` includes executable/runtime overhead, input query buffers, the native graph, and adapter state. It is not a graph-only allocator measurement. `peak_process_rss_kib` can reflect a construction-of-adapter peak during loading. Do not subtract two high-water marks and call the difference traversal memory.

Shared input normalization and query sampling are recorded separately in `input.json`, including wall/CPU time and generated file sizes. Native construction timings exclude this common preprocessing and provenance hashing. Normalization time in `mini` also includes building its explicit reference, so use `run` records for preprocessing costs on real data.

The Bifrost adapter payload field is a lower bound because hash-table allocation overhead is implementation-dependent. SBWT's select-support size uses its serialized support size plus allocated bitmap payload. Both adapters' actual resident allocations are included in process RSS. Auxiliary fields count the algorithm's tracked allocation payload, excluding allocator metadata, graph/adapter state, and output retention. Missing auxiliary measurements remain missing rather than being reported as zero.

SBWT's dummy-node mask is built with its native dummy-tree traversal, avoiding a reconstruction of every k-mer merely to identify sentinel columns.

For SBWT incoming navigation, subset select inverts the index's native forward step to find the predecessor suffix group. Its valid members are the incoming neighbors. This uses the stored suffix-group markers and select support; it does not build reverse adjacency or replace navigation with dictionary searches. The exact mini audit checks this against every reference edge, including vertices with all four incoming and outgoing DNA extensions.

Weak components use union-find with O(identifier slots) extra state. SCCs use Kosaraju with incoming navigation; their finishing-order array and second-pass stack remain substantial even though the first DFS pass uses predecessor reconstruction. None of these measurements implies a minimum RAM requirement or a memory-saving result in advance.

## Correctness and audit trail

`mini` constructs an independent explicit graph with at most 100,000 oriented k-mers. It checks the complete k-mer set, every directed edge in both directions, label/lookup round trips, reverse complements, degree statistics, weak/strong components and nonbranching structure. It checks BFS distances and DFS parent edges, discovery uniqueness, finish nesting, complete reachability, and within-backend DFS event equality for sampled sources and the full forest. BCALM2's exported canonical spectrum and absence of duplicate canonical k-mers are checked to validate the input used by SSHash.

`run` avoids building a Python graph for large data. It checks sampled membership and both-direction neighbor agreement across at least two indexed backends, plus full aggregate graph/component statistics for the operations selected. These checks are useful evidence but **not an exhaustive proof of large-graph equivalence**. Positive query seeds come from a reproducible occurrence reservoir; random/mutated candidate strings are not assumed absent. Results distinguish this sampling from exact mini verification.

Correctness traces are generated in separate runs. Timed traversal runs do not hash or write event streams. Failures, timeouts, missing operations and unsupported 32-bit ID ranges are explicit. A known semantic failure prevents that backend's performance measurements. Resume verifies the same settings, source files, input/binary hashes and stored graph hashes.

The output directory contains:

- `report.html` and `summary.json`: readable comparisons and aggregated timing ranges.
- `manifest.json`: inputs, binaries, source-code hashes, parameters, platform and provenance.
- `builds.json`, `runs.json`, `schedule.json`: complete construction and measurement records.
- `verification.json`: independent checks, sampled checks, failures and verification scope.
- `build/`, `verification/`, `measurements/`: native artifacts, raw logs, traces and per-process resource records.

All measurements here are single-machine and uncolored. This suite does not yet benchmark colored annotations, updates, multi-machine execution, every compressed variant of each library, or biological accuracy. Small fixtures establish correctness and expose integration mistakes; performance conclusions require representative real datasets and repeated measurements at several sizes.

## Verified local results — 2026-10-29

All six selected tools completed the final native tests on macOS x86-64. All five query probes were checked for Release optimization flags and matching executable hashes. BCALM2 was checked for its exported spectrum and canonical uniqueness.

| Shared K | Oriented vertices | Directed arcs | Measurement repeats | Recorded correctness checks | Measurement rows | Outcome |
|---|---:|---:|---:|---:|---:|---|
| 11 | 6,000 | 6,013 | 1 | 174 | 175 | Passed |
| 21 | 5,990 | 5,984 | 1 | 174 | 175 | Passed |
| 31 | 5,970 | 5,964 | 3 | 174 | 525 | Passed |

Each fixture includes isolated vertices, tips, four-way branches, cycles, disconnected components, duplicates, ambiguous bases and short records. Verification covers full forests and two shared source k-mers; timed rows keep those three traversal workloads separate.

The `run` path also passed on the small K=31 fixture: 13 recorded checks and 175 additional measurement rows. This exercises the code intended for large data without claiming that a large dataset was tested. Across the four runs there were **535 recorded checks and 1,050 successful measurement rows**. One recorded check can verify thousands of individual vertices or edges.

The 14 supporting Python tests passed. A resume check reused all 175 completed measurement files without changing their hashes or modification times; changed settings were rejected. All five native probes also rejected both excluded two-frontier variants before attempting to open an index.

These are correctness and integration results, not evidence of a runtime or memory advantage at scale. The Linux installation path has not yet been executed on the target server.

For a compact review of a server run, retain `manifest.json`, `input.json`, `builds.json`, `runs.json`, `summary.json`, `verification.json` and `report.html`. If a stage fails, retain its referenced build/probe log as well. The full output directory preserves the native graph artifacts and detailed audit trail.
