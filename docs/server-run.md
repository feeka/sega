# Server experiment and reporting guide

## Prepare

Transfer the complete revised checkout, including local changes, to the server. A new clone of the upstream default branch does not contain this revision until it is published. For a Git checkout, initialize the pinned MEGAHIT submodule. The supplied source archive already contains the pinned MEGAHIT files: extract it and start with the CMake configure/build commands, without running Git submodule commands. Run CTest/Python tests as shown in the README.

Keep sequencing accessions, preprocessing commands, read subset/seed, MEGAHIT version, and exact graph-construction commands alongside each experiment. File hashes identify data but cannot reconstruct missing preparation steps. Use new result directories for every dataset/k-value/experiment.

## First pass

```sh
python3 scripts/run_experiments.py \
  --binary build/megahit_bench --graph /data/sample/graph \
  --out results/sample-first-pass --repeats 1 --sources first \
  --audit --hash-graph-files
```

This creates eighteen fresh processes: nine correctness and nine measurement runs. The two-frontier BFS methods are excluded by default; keep them excluded from paper experiments. Audit runs on the first correctness process per target. Failures stop the experiment and preserve records; exit 0 requires every requested run/comparison to pass.

Inspect `summary.json` and the audit record. Confirm `valid_nodes`, source, reached counts, degree histograms, and arc counts. Use `visited/valid_nodes` for reachable coverage when appropriate, because the stored ID universe may include invalid IDs.

## Repetitions and sources

```sh
python3 scripts/run_experiments.py \
  --binary build/megahit_bench --graph /data/sample/graph \
  --out results/sample-multiple-sources --repeats 5 \
  --sources first,12345,67890 --audit --hash-graph-files
```

Replace numeric placeholders with valid IDs selected by a recorded rule before examining performance, and report their reachable fractions. Nine methods and five timing repetitions require 54 processes per source: nine correctness checks followed by 45 measurements. `--repeats` now controls timing only; `--correctness-repeats` defaults to one. Correctness runs finish before timing begins; methods/targets are randomized within each timing repetition. Add `--dry-run` to see the counts without launching anything. Each method reloads the graph; cache flushing is not performed and load time is separate. Document this policy, other workloads, storage, resource limits, CPU affinity, and frequency settings.

Use `--forest` instead of `--sources` in another output directory to cover all valid IDs. Forest roots are in ascending ID order; they do not count connected components. Distances restart per root. Source and forest results are compared only within their own policy.

32-bit variants reject universes they cannot encode. For those graphs, select applicable methods explicitly and report the exclusion. `--timeout SECONDS` sets a per-process limit; timeouts are failures, never successful partial runs.

## Exact output comparison

```sh
python3 scripts/run_experiments.py \
  --binary build/megahit_bench --graph /data/sample/graph \
  --out results/sample-exact --repeats 1 --sources first --exact-events
```

Files contain `15+17*visited` bytes for BFS and `15+26*visited` bytes for DFS. At 587 million reached vertices, that is about 10 GB/BFS run and 15 GB/DFS run (decimal). Every correctness process retains its file; provision disk space. Use exact events on small fixtures/subsets and hashes on the largest graphs. The runner verifies file size/SHA-256 and compares bytes with bounded RAM. Hash/file-I/O timings are excluded from performance summaries.

## Logs to retain and report

| File | Evidence |
|---|---|
| `summary.json` | Overall status/errors, completed/planned counts, measurement median/min/max |
| `manifest.json` | Binary/source hashes, revision/submodule status, graph metadata/hashes, machine details, thread settings, arguments |
| `working-tree.patch` | Tracked edits relative to recorded commit (empty for archive builds) |
| `source_snapshot.tar.gz` | Benchmark/library/test/script/docs source snapshot, excluding vendored MEGAHIT |
| `SOURCE_BUNDLE.json` | Source archive manifest and original revision metadata when supplied |
| `runs.json` | Actual execution order, invocations, repeat/phase, status, duration, results/errors |
| `NNNN-*.json` | Parsed per-process metrics, audit, digest |
| `NNNN-*.stdout`, `NNNN-*.stderr` | Raw output and diagnostic logs |
| `NNNN-*.events` | Optional complete canonical output streams |
| Build configuration files | Available CMake cache/compile-command provenance |

Send JSON files, patch, build configuration, and stderr logs for routine review. Keep graphs/event files on the server unless needed to investigate a mismatch. A pass establishes the implemented checks and requested comparisons passed; it does not establish novelty or downstream biological benefit.

## Remaining paper work

### Planned study (2026-10-26)

These are proposed study sizes, not journal acceptance thresholds. Exclude `bfs-se` and `bfs-se32` from all paper plots, tables, and future experiment commands. Keep the raw historical records intact and keep `dfs-se` in scope.

Before the expensive matrix, check a packed-color DFS baseline using a stack allocated in blocks rather than a growing contiguous vector. On the supplied SRR14017557 k=23 graph, N=1,099,909,480 and maximum DFS depth D=55,566,912. Ideal 32-bit packed-stack payload is about 474.2 MiB (`8*ceil(N/32)+4*D` bytes), versus 524.5 MiB for the stackless fixed arrays. The measured vector baseline used 646.2 MiB including capacity and reallocation overlap. This is an analytical estimate, not a measured replacement implementation; it motivates a baseline check before claiming an inherent memory advantage. Freeze the final implementation/baselines before collecting publication timings.

- Main biological matrix: three independent datasets, spanning different sizes and at least two biological contexts; three k-values per dataset (proposed 23, 31, 55, subject to read length and build support). This is nine graphs, not nine independent biological samples. Use the same documented preprocessing and construction stage for comparisons.
- Use full-forest traversal as the primary coverage policy. For the current nine retained methods, one correctness pass plus five timing runs means 54 processes per graph: 81 correctness processes and 405 timing processes across nine graphs, 486 total. Audit once per graph/root policy and hash graph inputs before/after. Counts must be revised if the baseline set changes.
- Source sensitivity: on one representative graph per dataset, use three preselected valid source IDs, with one correctness and one measurement per method/source. This adds 162 processes for the current nine methods. Treat these as exploratory source/coverage observations, not repeated timing evidence. If a main claim depends on source choice, increase these to five timing repetitions too.
- Controlled synthetic study: paths, broad layered graphs, cycles/disconnected graphs and seeded random graphs, over at least three sizes. Add a parameterized depth family spanning D/N below and above 1/32 and 1/16 to test the 64-bit and 32-bit packed-stack payload crossovers. These bounded-degree graph tests explain traversal behaviour; do not label arbitrary synthetic graphs as biological de Bruijn graphs. Record random seeds separately from repeated timing runs.
- Report all five timing values, median and range (and IQR), plus per-run total RSS and auxiliary allocation. Use a preset noise trigger: if `(max-min)/median` exceeds 5%, inspect system load and collect five additional timing observations for affected comparisons; retain the original observations. Do not claim statistical significance from five runs alone or treat repetitions as independent biological samples.
- Application evidence: choose a real consumer that needs the traversal output (for example, extraction and reporting of a bounded neighborhood around a mapped seed sequence). Compare identical output and include output-storage cost in end-to-end time/memory. A traversal callback discarded by the benchmark is not an end-to-end application. A DFS-specific downstream benefit requires a task that actually uses DFS.
- Publish graph-construction recipes, accession IDs, read filters/subsampling seeds, graph statistics, source-selection rule, compiler flags, machine/load/affinity policy, all raw results, and scripts for regenerating figures. Archive a reviewed source release and small reproducible test data with a persistent identifier.

The current single-source measurements suggest roughly 18.1 hours of traversal work for one correctness set plus five timing sets of the nine retained methods, and approximately 19 hours including the observed graph audit/loading overhead. This is a planning estimate for this graph only; full forests and other graphs can differ. Do not multiply it into a promised completion time.

After the baseline check, the updated runner can collect the main full-forest measurements on the known graph prefix with:

```sh
python3 scripts/run_experiments.py --binary build/megahit_bench --graph /vol/d/data/copsac/SRR14017557/gpt_opt/graph/graph --out results/SRR14017557-k23-forest-paper-v2 --forest --repeats 5 --correctness-repeats 1 --methods bfs-base,bfs-base32,bfs-stream,bfs-stream32,dfs-se,dfs-base,dfs-base32,dfs-packed,dfs-packed32 --audit --hash-graph-files
```

Use a new output directory. The old server runner does not support `--correctness-repeats`: update the runner before using this command. No C++ rebuild is needed for the scheduling-only update; adding a new DFS baseline later will require one. The supplied source/binary hashes distinguish these versions.

### Manuscript and release checklist

1. Test multiple independent biological datasets and supported k-values; preserve accessions/construction commands.
2. Test several sources plus full forests.
3. Relate performance to broad frontiers, deep/shallow DFS, allocation capacity, and navigation statistics.
4. Report repeated measurement results, stronger baselines, total RSS, and auxiliary requested allocation separately.
5. Integrate a meaningful downstream task and measure its full output correctness, runtime, and memory. This integration is not supplied by the traversal benchmark.
6. Update manuscript claims from `methods.md`: frontier-dependent BFS bound, edge-ID universe, predecessor-index DFS, faithful attribution, and output/storage contract. The manuscript source is not in this repository; the PDF has not been rewritten.
7. Regenerate figures/tables from archived measurements. Original ratios are historical until rerun.
8. Freeze a reviewed release and archive source, raw results, scripts, and data manifest with a persistent identifier.

Avoid claims of universal runtime neutrality, universal memory improvement, first implementation, or new biological impact without supporting evidence. Keep graph compression size distinct from loaded process memory, and do not infer exact auxiliary allocation by subtracting RSS peaks.
