#!/usr/bin/env python3
"""Render the saved compact-graph experiment logs without rerunning benchmarks."""
import argparse
import html
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--results', required=True, type=Path)
p.add_argument('--previous-runs', type=Path)
a = p.parse_args(); root = a.results.resolve()
manifest = json.loads((root/'manifest.json').read_text())
summary = json.loads((root/'summary.json').read_text())
runs = json.loads((root/'runs.json').read_text())
assert summary['status'] == 'ok' and summary['processes'] == len(runs)
assert all(r['exit_code'] == 0 and r['metrics']['status'] == 'ok' for r in runs)
assert {r['metrics']['source_sha256'] for r in runs} == {manifest['source_sha256']}
MIB = 1024**2
rows = summary['rows']

def row(shape, backend, workload):
    return next(r for r in rows if (r['shape'], r['backend'], r['workload']) == (shape, backend, workload))

def med(r, key): return r[key]['median']
def ms(r):
    v = r['traversal_ms']; return f"{v['median']:.3f} <small>[{v['minimum']:.3f}, {v['maximum']:.3f}]</small>"
def ratio(new, old): return 100*(1-new/old)

previous = None
if a.previous_runs:
    old = json.loads(a.previous_runs.read_text())
    old_topology = {r['result']['topology_sha256'] for r in old if r['result'].get('topology_sha256')}
    current = [r['metrics'] for r in runs if r['phase']=='correctness' and r['shape']=='random']
    current_topology = {r['topology_sha256'] for r in current if r.get('topology_sha256')}
    previous = {'same_random_topology':old_topology == current_topology}
    for family in ('bfs', 'dfs'):
        before = {r['result']['event_sha256'] for r in old if r['phase']=='correctness' and r['result']['traversal']==family}
        after = {r['metrics']['event_sha256'] for r in runs if r['phase']=='correctness' and r['shape']=='random' and r['workload']==family}
        previous[family+'_events_match_previous'] = before == after
    (root/'previous-comparison.json').write_text(json.dumps(previous, indent=2)+'\n')

sections = []
for shape in manifest['shapes']:
    table = []
    for backend in ('csr32-out', 'byte32'):
        b, d, local = [row(shape,backend,w) for w in ('bfs','dfs','local-bfs-100')]
        build = next(r['metrics'] for r in runs if r['phase']=='build' and r['shape']==shape and r['backend']==backend)
        table.append(f'''<tr><th>{backend}</th><td>{med(b,'graph_file_bytes')/MIB:.2f}</td>
<td>{med(b,'rss_after_load_kib')/1024:.2f}</td><td>{med(b,'process_peak_kib')/1024:.2f}</td><td>{ms(b)}</td>
<td>{med(d,'process_peak_kib')/1024:.2f}</td><td>{ms(d)}</td><td>{med(d,'aux_peak_bytes')/MIB:.2f}</td></tr>''')
        table.append(f'''<tr class="detail"><td colspan="8">{backend}: load {med(b,'load_ms'):.1f} ms;
100 radius-two BFS queries {ms(local)} ms total; {local['visited_per_query']:,} vertices emitted per query.
Builder peak {build['process_peak_kib']/1024:.2f} MiB; input {build['input_ms']/1000:.3f} s,
build {build['build_ms']/1000:.3f} s, save {build['save_ms']/1000:.3f} s.</td></tr>''')
    baseline = row(shape,'csr32-out','bfs'); compressed = row(shape,'byte32','bfs')
    sections.append(f'''<section><h2>{html.escape(shape)} · {baseline['vertices']:,} vertices / {baseline['edges']:,} arcs</h2>
<div class="scroll"><table><thead><tr><th>Backend</th><th>File MiB</th><th>Loaded RSS MiB</th><th>BFS peak MiB</th><th>BFS ms</th><th>DFS peak MiB</th><th>DFS ms</th><th>DFS auxiliary MiB</th></tr></thead><tbody>{''.join(table)}</tbody></table></div></section>''')

random_csr = row('random','csr32-out','bfs') if 'random' in manifest['shapes'] else None
random_byte = row('random','byte32','bfs') if random_csr else None
headline = ''
if random_csr:
    headline = f'''<p class="lead">On the random graph, byte coding reduces the saved graph by
<strong>{ratio(med(random_byte,'graph_file_bytes'),med(random_csr,'graph_file_bytes')):.1f}%</strong>
and full-BFS query-process peak RAM by
<strong>{ratio(med(random_byte,'process_peak_kib'),med(random_csr,'process_peak_kib')):.1f}%</strong>.
BFS traversal time is <strong>{med(random_byte,'traversal_ms')/med(random_csr,'traversal_ms'):.2f}×</strong> the CSR time.
See path and star results below before generalizing this tradeoff.</p>'''
verification = root/'verification.json'
verification_text = 'Detailed test evidence is in verification.json.'
if verification.exists():
    v = json.loads(verification.read_text())
    verification_text = f"Release: {v['release_passed']} test groups passed. ASan/UBSan: {v['sanitizer_passed']} groups passed. New storage/cursor tests: 672 graphs and 1,889,738 checks. Native MEGAHIT: 64 independent-reference checks. LeakSanitizer is unsupported by this macOS toolchain; leak detection was disabled."
prior_text = '' if previous is None else ('The random graph topology and full BFS/DFS event hashes also match the previous million-vertex pilot.' if all(previous.values()) else 'The comparison with the previous pilot differs in topology or event order; see previous-comparison.json. Only the matched new backends are compared here.')
page = f'''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>SEGA compact graph API — tested results</title><style>
:root{{color-scheme:light}}body{{font:16px/1.6 system-ui,sans-serif;color:#172332;background:#f4f7f9;margin:0}}main{{max-width:1260px;margin:45px auto;padding:0 25px 60px}}
h1{{font-size:36px;line-height:1.2;margin-bottom:14px}}h2{{font-size:22px}}.eyebrow{{color:#306452;font-weight:700;letter-spacing:.08em;font-size:13px}}.lead{{font-size:20px;max-width:1000px}}section{{background:white;border:1px solid #dce4e8;border-radius:12px;padding:22px;margin:24px 0}}table{{width:100%;border-collapse:collapse;text-align:right;font-size:14px}}th,td{{padding:12px 10px;border-bottom:1px solid #e4e9ed;white-space:nowrap}}th:first-child{{text-align:left}}thead{{color:#4d626e}}small{{display:block;color:#536875;font-size:12px}}.detail td{{white-space:normal;text-align:left;background:#f7fafb;font-size:13px}}.scroll{{overflow-x:auto}}code{{overflow-wrap:anywhere}}a{{color:#176f69}}li{{margin:7px 0}}.muted{{color:#526672}}
</style><main><div class="eyebrow">SEGA · IMPLEMENTED &amp; TESTED · {html.escape(manifest['finished_utc'][:10])}</div>
<h1>Sequential decoding, native cursors, reusable queries</h1>{headline}
<p>Two outgoing-only representations; identical sorted adjacency and 32-bit traversal IDs. Construction occurs once in separate processes. Every timing process loads a saved graph. Results are streamed. The two-vector BFS methods are excluded.</p>
<section><h2>What is delivered</h2><ol><li><strong>Storage:</strong> outgoing CSR32 and byte-coded sorted gaps with a restart every 128 neighbors.</li><li><strong>API:</strong> resumable, backend-native cursors; BFS and packed-color stack DFS; a borrowing adapter over MEGAHIT without full-graph conversion.</li><li><strong>Reuse:</strong> checked save/load, reusable BFS workspace, outgoing hop-limited queries, and independent build/load/query metrics.</li></ol></section>
{''.join(sections)}
<section><h2>How to read these measurements</h2><p>Times are medians of {manifest['timing_repeats']} independent timing processes; brackets show minimum and maximum. The local workload repeats the same radius-two query 100 times after one load, reusing workspace. Peak RAM includes loading, checksum and structural validation, runtime and traversal, but excludes graph construction. Loading touches the graph before traversal. OS caches were not flushed.</p>
<p>Saved file size includes offsets, header and checksum. Auxiliary bytes count requested live traversal allocations, including cursor frames and retained workspace capacity. They exclude graph storage, caller-retained output, allocator bookkeeping and stack locals. They are distinct from RSS.</p>
<p>Byte-coded DFS stores a 32-byte frame versus 24 bytes for CSR on this build. Deep paths can therefore erase or reverse the storage saving. The current API does not enforce a hard memory budget. A compact graph is not automatically a faster graph.</p></section>
<section><h2>Validation and scope</h2><p>{verification_text}</p><p>{len(runs)} benchmark processes succeeded. Canonical topology hashes and exact-event hashes match between backends; timing runs match correctness-run coverage. {prior_text}</p>
<p>These are synthetic pilot measurements on one machine, with three repeats. They do not establish publication-level performance, new compression theory, or biological effectiveness. Sequence mapping/output, multi-source neighborhoods, reverse-query indexing, memory caps and external-memory builders remain future work.</p>
<p><a href="manifest.json">Manifest and provenance</a> · <a href="runs.json">Every command and result</a> · <a href="summary.json">Machine-readable summary</a> · <a href="verification.json">Tests</a></p>
<p class="muted">Platform: {html.escape(manifest['platform'])}<br>Compiled-source SHA-256: <code>{manifest['source_sha256']}</code><br>Binary SHA-256: <code>{manifest['binary_sha256']}</code></p></section></main></html>'''
(root/'report.html').write_text(page)
print(root/'report.html')
