"""Plain-language reports from saved measurements; never runs a benchmark."""
import html
import json
from collections import defaultdict
from pathlib import Path
from statistics import median

TOOLS = {"bifrost": "Bifrost", "metagraph": "MetaGraph", "megahit": "MEGAHIT"}
METHODS = {
    "bfs-base": ("Standard breadth-first search", "Visits nearby vertices first and keeps a distance for every vertex. Uses 64-bit queue entries."),
    "bfs-base32": ("Standard breadth-first search, smaller queue entries", "The same search with 32-bit queue entries. The distance array is unchanged."),
    "bfs-stream": ("Streaming breadth-first search", "Visits nearby vertices first. Keeps visited flags and a waiting queue; reports distances as it goes instead of keeping a full distance array. Uses 64-bit queue entries."),
    "bfs-stream32": ("Streaming breadth-first search, smaller queue entries", "The streaming search with 32-bit queue entries."),
    "dfs-base": ("Standard depth-first search", "Follows a branch before returning to explore another. Keeps a stack of the active path and one status byte per vertex. Uses 64-bit vertex identifiers."),
    "dfs-base32": ("Standard depth-first search, smaller stack entries", "The same search with 32-bit vertex identifiers in the stack."),
    "dfs-packed": ("Depth-first search with compact status flags", "Still keeps an explicit stack, but packs vertex status flags into fewer bits. Uses 64-bit vertex identifiers."),
    "dfs-packed32": ("Depth-first search with compact flags and smaller stack entries", "Compact status flags plus 32-bit vertex identifiers in the stack. This is the stronger memory baseline for our stackless search."),
    "dfs-se": ("Our stackless depth-first search", "SEGA's search: stores compact state and recovers the way back through incoming graph links instead of keeping an explicit path stack."),
}
OPERATIONS = {
    "load": ("Open the graph", "Read the saved graph and prepare its access code."),
    "stats": ("Count graph features", "Count vertices, links, dead ends, isolated vertices and branching points."),
    "lookup": ("Find DNA fragments", "Check whether each DNA fragment in the shared query list is present."),
    "neighbors": ("Find immediate neighbors", "For the same query list, find the incoming and outgoing neighbors of every fragment that is present."),
    "walk": ("Follow short paths", "For each present query fragment, follow up to 100 links using the same branch-selection rule."),
    "components": ("Find connected groups", "Find groups connected by links when link direction is ignored."),
    "scc": ("Find mutually reachable groups", "Find groups where every vertex can reach every other vertex by following link directions."),
    "unitigs": ("Find paths without branches", "Count maximal paths without internal branching, separate cycles and isolated vertices."),
    **METHODS,
}

STYLE = """body{font:16px/1.6 system-ui,sans-serif;background:#f5f8fa;color:#18313a;max-width:1180px;margin:32px auto;padding:0 22px}h1,h2,h3{line-height:1.25}h1{max-width:900px}h2{margin-top:36px}a{color:#06648f}table{border-collapse:collapse;background:white;width:100%;margin:16px 0}th,td{border:1px solid #cedbe0;padding:10px;text-align:left;vertical-align:top}th{background:#e8eff3}.scroll{overflow-x:auto}details{border:1px solid #cedbe0;background:white;padding:14px;margin:16px 0;border-radius:6px}summary{cursor:pointer;font-weight:650}.note{padding:14px 18px;background:#e8f1f5;border-left:4px solid #33788a}.warning{background:#fff3dc;border-left-color:#a96b00}code{background:#e8eff3;padding:2px 4px;overflow-wrap:anywhere}p{max-width:1000px}.small{font-size:14px;color:#405965}td.num{font-variant-numeric:tabular-nums}nav a{margin-right:16px}dl dt{font-weight:700;margin-top:12px}dl dd{margin-left:0}footer{margin:36px 0;color:#405965}"""


def esc(value):
    return html.escape(str(value))


def fmt(value, digits=3):
    return "Not measured" if value is None else f"{value:,.{digits}f}"


def span(values):
    values = sorted(set(v for v in values if v is not None))
    if not values:
        return "Not measured"
    return f"{values[0]:,}" if len(values) == 1 else f"{values[0]:,}–{values[-1]:,} (varies)"


def table(headers, rows):
    return ('<div class="scroll"><table><thead><tr>' + ''.join(f'<th scope="col">{esc(h)}</th>' for h in headers)
            + '</tr></thead><tbody>' + ''.join('<tr>' + ''.join(f'<td>{esc(v)}</td>' for v in row) + '</tr>' for row in rows)
            + '</tbody></table></div>')


def document(title, body):
    return ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width,initial-scale=1">'
            f'<title>{esc(title)}</title><style>{STYLE}</style></head><body>{body}</body></html>')


def read_case(out):
    out = Path(out)
    def read(name):
        return json.loads((out / name).read_text())
    return read('builds.json'), read('runs.json'), read('verification.json'), read('manifest.json')['signature']


def render_body(builds, runs, verification, workload):
    historical_extra_tools = (set(builds) | {r['backend'] for r in runs}) - set(TOOLS)
    builds = {name: b for name, b in builds.items() if name in TOOLS}
    runs = [r for r in runs if r['backend'] in TOOLS]
    groups = defaultdict(list)
    for r in runs:
        if r.get('status') == 'ok':
            groups[(r['backend'], r['operation'], r.get('source', 'n/a'))].append(r)
    names = sorted(set(builds) | {r['backend'] for r in runs})
    def rows(name, op, source='n/a'):
        return groups.get((name, op, source), [])
    def value(name, op, field, source='n/a', scale=1):
        data = rows(name, op, source)
        values = [r.get(field) for r in data]
        return median(values) / scale if values and all(v is not None for v in values) else None
    def aux(name, op, source='n/a'):
        data = rows(name, op, source)
        values = [r.get('auxiliary_peak_bytes', r.get('auxiliary_payload_bytes')) for r in data]
        return median(values) / 1024 if values and all(v is not None for v in values) else None
    starts = sorted({s for _, op, s in groups if op in METHODS and s not in ('forest', 'n/a')})
    stats = [r for r in runs if r.get('status') == 'ok' and r['operation'] == 'stats']
    k = workload['k']
    body = f'<h1>Comparing graph tools on the same data</h1><p>Each vertex represents a {k}-letter DNA fragment. '
    body += f'The recorded graph counts are <strong>{span(r.get("oriented_kmers") for r in stats)} vertices</strong> and '
    body += f'<strong>{span(r.get("arcs") for r in stats)} directed links</strong>. The two DNA orientations count separately.</p>'
    body += '<p><strong>Tools in this comparison: MEGAHIT, MetaGraph and Bifrost.</strong></p>'
    if historical_extra_tools:
        body += '<p class="small">This page shows only the three retained tools. Original measurement files are preserved and may contain earlier configurations.</p>'
    if workload.get('mode') == 'mini':
        body += '<p class="note">This is a small correctness test. Its timings and memory readings describe this example; they do not establish which tool wins on large biological datasets.</p>'
    else:
        body += '<p class="note">This uses the real-data runner. Check the graph size above: using that runner does not itself make an experiment large.</p>'
    body += '<nav><a href="#fairness">Same task?</a><a href="#memory">Memory</a><a href="#searches">Search results</a><a href="#names">Names explained</a></nav>'
    body += '<h2 id="fairness">What was kept the same?</h2>'
    if verification.get('status') == 'passed' and verification.get('scope') == 'exhaustive-independent-mini':
        body += '<p>The small-example checks passed: the indexed tools were checked against an independent graph for the same vertices and links. Search correctness was checked separately from timing.</p>'
    else:
        body += f'<p>Recorded check result: <strong>{esc(verification.get("status", "unknown"))}</strong>. '
        body += 'For real-data runs, sampled checks and matching totals are evidence of agreement, not a complete comparison of every vertex and link.</p>'
    body += '<p><strong>A starting point is the vertex where a search begins.</strong> The old report called it a “source”. '
    body += 'A search follows outgoing links until it has visited everything reachable from that vertex. It may reach only part of the graph.</p>'
    if starts:
        start_rows = []
        for i, source in enumerate(starts, 1):
            for name in names:
                traversals = [r for op in METHODS for r in rows(name, op, source)]
                completed = [len(rows(name, op, source)) for op in METHODS if op in workload.get('operations', METHODS)]
                start_rows.append([f'Starting point {i}', TOOLS.get(name, name),
                                   span(r.get('visited') for r in traversals), span(completed)])
        body += '<p>Every tool receives the same starting DNA fragment for each case below. Different cases use different starting points; their results are kept separate.</p>'
        body += table(['Search case', 'Tool', 'Vertices reached in each search', 'Completed runs per search method'], start_rows)
        body += '<details><summary>Exact starting DNA fragments, shared by every tool</summary>'
        body += table(['Search case', 'Starting DNA fragment'], [(f'Starting point {i}', s) for i, s in enumerate(starts, 1)]) + '</details>'
    else:
        body += '<p class="note warning">No searches from shared starting points were measured. The whole-graph results below use each tool’s own restart order and should not be used as an identical-search tool ranking.</p>'
    body += '<p><strong>The input and requested task must match; the measured time and memory need not match.</strong> '
    body += 'Each tool stores and navigates the graph differently. For breadth-first and depth-first searches, native neighbor order is retained, so the visit order can differ even from the same start. '
    body += 'The separate short-path test uses a shared branch-selection rule.</p>'
    repeats = workload['repeats']
    body += f'<p><strong>“Repeats = {repeats}” means doing the same measurement {repeats} time(s).</strong> '
    body += ('With three completed runs, the table shows the middle of the three values (the median). ' if repeats == 3 else
             'With one completed run, there is no measurement of run-to-run variation. ' if repeats == 1 else
             'The table shows the median of the completed runs; for an even count this averages the two middle values. ')
    body += 'Each run starts a fresh program and reloads the graph. Search timing starts after loading. Graph construction is measured once per tool, not once per repeat. '
    body += f'Searches use one thread; graph builders were given {workload["threads"]} threads. Filesystem caching was not controlled.</p>'
    body += '<h2 id="memory">What does “memory” mean here?</h2>'
    body += table(['Measurement', 'In ordinary language'], [
        ['Saved graph size', 'Space occupied by the saved graph/index files on disk. This is not RAM.'],
        ['RAM after opening', 'Resident RAM used by the whole measurement program after it opens the graph: graph, access code, query buffer and runtime. This is not graph storage alone.'],
        ['Extra search memory', 'Tracked storage for the search’s queue, stack and visited/status arrays. It excludes the graph and allocator bookkeeping.'],
        ['Peak program RAM', 'The highest resident RAM reported for the measurement program, including opening the graph and performing the operation. It can peak during loading.'],
    ])
    body += '<p>MiB means 1,048,576 bytes; KiB means 1,024 bytes. A smaller search workspace does not necessarily make the whole program’s peak RAM smaller. These readings are observed usage, not minimum RAM requirements.</p>'
    body += '<h3>Opening the same graph</h3>'
    memory_rows = []
    for name in names:
        memory_rows.append([TOOLS.get(name, name), fmt(value(name, 'load', 'loaded_process_rss_kib', scale=1024), 2),
                            fmt(builds.get(name, {}).get('serialized_bytes'), 0),
                            fmt(value(name, 'load', 'load_and_adapter_ms')), len(rows(name, 'load'))])
    body += table(['Tool', 'RAM after opening (MiB)', 'Saved graph/index (bytes)', 'Time to open (ms)', 'Completed runs'], memory_rows)
    body += '<h2 id="searches">Search results: compare the same column and starting point</h2>'
    body += '<p>Breadth-first search (BFS) explores nearby vertices first. Depth-first search (DFS) follows a branch before returning to explore another. '
    body += 'All search methods here are run by our measurement program through each tool’s graph interface; these are not end-to-end assembler timings. '
    body += 'Times are in milliseconds (ms): 1,000 ms = 1 second. Lower time means faster; lower memory means less space used.</p>'
    for i, source in enumerate(starts, 1):
        traversals = [r for r in runs if r.get('status') == 'ok' and r['operation'] in METHODS and r.get('source') == source]
        body += f'<h3>Starting point {i} — {span(r.get("visited") for r in traversals)} vertices reached</h3>'
        reached = {r.get('visited') for r in traversals}
        if len(reached) != 1:
            body += '<p class="note warning">Reached-vertex counts differ or are missing. Do not treat these rows as matching searches.</p>'
        body += '<p>Every row starts at the same DNA fragment. Each column uses the same search method.</p>'
        body += table(['Tool', 'Standard breadth-first search (ms)', 'Standard depth-first search (ms)', 'Our stackless depth-first search (ms)'],
                      [[TOOLS.get(n, n)] + [fmt(value(n, op, 'operation_ms', source)) for op in ('bfs-base', 'dfs-base', 'dfs-se')] for n in names])
        body += '<p><strong>Memory comparison for depth-first search.</strong> The comparison method already uses compact status flags and smaller (32-bit) stack entries. Both methods solve the same search task.</p>'
        body += table(['Tool', 'Compact-stack search: extra memory (KiB)', 'Our stackless search: extra memory (KiB)',
                       'Compact-stack search: peak program RAM (MiB)', 'Our stackless search: peak program RAM (MiB)'],
                      [[TOOLS.get(n, n), fmt(aux(n, 'dfs-packed32', source), 2), fmt(aux(n, 'dfs-se', source), 2),
                        fmt(value(n, 'dfs-packed32', 'peak_process_rss_kib', source, 1024), 2),
                        fmt(value(n, 'dfs-se', 'peak_process_rss_kib', source, 1024), 2)] for n in names])
        body += '<details><summary>All search methods for this same starting point, including timing variation</summary>'
        for op, (label, description) in METHODS.items():
            if not any(rows(n, op, source) for n in names):
                continue
            body += f'<h4>{esc(label)}</h4><p>{esc(description)}</p>'
            detailed = []
            for n in names:
                data = rows(n, op, source)
                times = [r['operation_ms'] for r in data]
                detailed.append([TOOLS.get(n, n), span(r.get('visited') for r in data), len(data),
                                 fmt(median(times) if times else None), fmt(min(times) if times else None),
                                 fmt(max(times) if times else None), fmt(aux(n, op, source), 2),
                                 fmt(value(n, op, 'peak_process_rss_kib', source, 1024), 2)])
            body += table(['Tool', 'Vertices reached', 'Runs', 'Middle time (ms)', 'Fastest (ms)', 'Slowest (ms)',
                           'Extra search memory (KiB)', 'Peak program RAM (MiB)'], detailed)
        body += '</details>'
    body += '<h2>Other measurements on the same graph</h2>'
    for op in ('lookup', 'neighbors', 'walk', 'stats', 'components', 'scc', 'unitigs'):
        if not any(rows(n, op) for n in names):
            continue
        label, explanation = OPERATIONS[op]
        body += f'<details><summary>{esc(label)}</summary><p>{esc(explanation)}</p>'
        data = [r for n in names for r in rows(n, op)]
        if op in ('lookup', 'neighbors', 'walk'):
            body += f'<p>Same shared query file; recorded batch size: {span(r.get("query_count") for r in data)} DNA fragments. Time below is for the whole batch.</p>'
        else:
            body += '<p>This operation covers the whole graph, rather than starting at one selected vertex.</p>'
        body += table(['Tool', 'Time (ms)', 'Peak program RAM (MiB)', 'Extra working memory (KiB)', 'Completed runs'],
                      [[TOOLS.get(n, n), fmt(value(n, op, 'operation_ms')),
                        fmt(value(n, op, 'peak_process_rss_kib', scale=1024), 2), fmt(aux(n, op), 2), len(rows(n, op))] for n in names])
        body += '</details>'
    if any(s == 'forest' for _, _, s in groups):
        body += '<details><summary>Whole-graph searches: different restart orders, kept separate</summary>'
        body += '<p class="note warning">The old name was “forest”. When a search runs out of reachable vertices, it restarts from an unvisited vertex until it has covered the entire graph. '
        body += 'Each tool selects these restarts in its own internal vertex order. This changes the search trees and can change stack depth and queue size. '
        body += '<strong>These are not identical search schedules across tools and are not used for the starting-point comparisons above.</strong> '
        body += 'The number of starting searches is not the number of connected components.</p>'
        for op, (label, _) in METHODS.items():
            if not any(rows(n, op, 'forest') for n in names):
                continue
            body += f'<h4>{esc(label)}</h4>'
            body += table(['Tool', 'Vertices reached', 'Starting searches', 'Maximum path depth (DFS only)', 'Time (ms)', 'Extra search memory (KiB)', 'Peak program RAM (MiB)', 'Completed runs'],
                          [[TOOLS.get(n, n), span(r.get('visited') for r in rows(n, op, 'forest')),
                            span(r.get('roots') for r in rows(n, op, 'forest')),
                            span(r.get('max_dfs_depth') for r in rows(n, op, 'forest')) if op.startswith('dfs') else 'Not applicable',
                            fmt(value(n, op, 'operation_ms', 'forest')), fmt(aux(n, op, 'forest'), 2),
                            fmt(value(n, op, 'peak_process_rss_kib', 'forest', 1024), 2), len(rows(n, op, 'forest'))] for n in names])
        body += '</details>'
    body += '<details><summary>Time and memory for building each graph</summary><p>One construction per tool. '
    body += 'Memory is sampled across the builder and its child processes; brief peaks can be missed, and shared pages can be counted more than once. '
    body += 'This is different from the single-program RAM measurements above. Common input preparation is excluded from these construction times.</p>'
    construction = []
    for name, b in builds.items():
        ok = b.get('status') == 'ok'
        seconds = b.get('build_wall_seconds') if ok else None
        peak_bytes = b.get('sampled_peak_build_tree_rss_bytes') if ok else None
        peak = peak_bytes / 1024**2 if peak_bytes is not None else None
        construction.append([TOOLS.get(name, name), b.get('status', 'unknown'), fmt(seconds), fmt(peak, 2), fmt(b.get('serialized_bytes'), 0)])
    body += table(['Tool', 'Build result', 'Construction time (seconds)', 'Sampled construction RAM (MiB)', 'Saved output (bytes)'], construction)
    body += '</details>'
    body += '<h2 id="names">What the names mean</h2><p><strong>MEGAHIT and MetaGraph</strong> supply succinct de Bruijn graphs. <strong>Bifrost</strong> supplies a compacted de Bruijn graph. '
    body += '<strong>SEGA</strong> is our traversal code, applied to these three graph libraries. '
    body += 'BFS and DFS name search methods, not extra competing graph tools.</p>'
    body += '<details><summary>Translate the old codes in the raw files</summary>'
    body += table(['Old code', 'Readable name', 'Meaning'], [(op, *entry) for op, entry in OPERATIONS.items()])
    body += '<p>“32” means smaller vertex-number entries; it does not mean 32 repeats. “Source” means starting vertex. '
    body += '“n/a” means that the operation has no single starting vertex. “Auxiliary” means extra working memory. '
    body += '“RSS” means the program’s resident RAM. “Serialized bytes” means saved file size. The excluded two-frontier BFS methods are not included.</p></details>'
    issues = [r for r in verification.get('failures', []) if not r.get('backend') or r['backend'] in TOOLS]
    issues += [r for r in runs if r.get('status') != 'ok']
    issues += [b for b in builds.values() if b.get('status') != 'ok']
    if issues:
        body += '<h2>Incomplete or failed measurements</h2><p class="note warning">Missing measurements are not zero and are not successful runs.</p>'
        issue_rows = set()
        for r in issues:
            op = r.get('operation', r.get('stage', 'Build'))
            source = r.get('source', 'n/a')
            source_label = f'Starting point {starts.index(source)+1}' if source in starts else 'Whole graph' if source == 'forest' else 'No single starting point'
            issue_rows.add((TOOLS.get(r.get('backend'), r.get('backend', 'Shared checks')),
                            OPERATIONS.get(op, (op, ''))[0], source_label,
                            r.get('error', r.get('reason', r.get('status', 'See correctness record')))))
        body += table(['Tool', 'Task', 'Search case', 'Reason'], sorted(issue_rows))
    return body


def render_report(out, builds=None, runs=None, verification=None, workload=None):
    out = Path(out)
    if builds is None:
        builds, runs, verification, workload = read_case(out)
    body = render_body(builds, runs, verification, workload)
    body += '<footer>Saved measurements: <a href="runs.json">individual runs</a> · <a href="builds.json">construction</a> · '
    body += '<a href="verification.json">correctness checks</a> · <a href="manifest.json">exact settings</a> · <a href="summary.json">numeric summary</a>. '
    body += 'This page can be regenerated without rerunning the experiments.</footer>'
    (out / 'report.html').write_text(document('Graph comparisons in plain language', body))


def render_overview(root, main_case='k31'):
    root = Path(root)
    case = root / main_case
    builds, runs, verification, workload = read_case(case)
    body = render_body(builds, runs, verification, workload)
    body += '<h2>Other example sizes and saved files</h2><p>The results above are the 31-letter-fragment example. '
    body += 'The 11- and 21-letter examples define different graphs and are separate experiments.</p><ul>'
    for name in ('k11', 'k21', 'k31', 'large-mode-smoke'):
        if (root / name / 'report.html').is_file():
            label = {'k11': '11-letter fragments', 'k21': '21-letter fragments', 'k31': '31-letter fragments',
                     'large-mode-smoke': 'Real-data runner tested on the same small input'}[name]
            body += f'<li><a href="{name}/report.html">{label}</a></li>'
    body += '</ul><p><a href="sega-dbg-measurements.zip">Server source package</a> · '
    body += '<a href="../../sega/docs/representation-benchmarks.md">Instructions</a> · '
    body += '<a href="validation.json">Original validation record</a> · <a href="release-manifest.json">Package checksums</a> · '
    if (root / 'report-update.json').is_file():
        body += '<a href="report-update.json">Checks of matching searches and this report update</a> · '
    body += f'<a href="{esc(main_case)}/runs.json">Individual runs behind these tables</a></p>'
    body += '<footer>Report presentation updated from existing measurements. The experiments were not rerun and the recorded measurements were not altered.</footer>'
    (root / 'report.html').write_text(document('Graph comparisons in plain language', body))
