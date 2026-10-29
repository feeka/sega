#!/usr/bin/env python3
"""Sequential build-once/fresh-process query comparison. Standard library only."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import platform
import random
import statistics
import subprocess
import time


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1024*1024), b''): h.update(block)
    return h.hexdigest()


def write(path, value):
    tmp = path.with_suffix(path.suffix+'.tmp')
    tmp.write_text(json.dumps(value, indent=2)+'\n'); tmp.replace(path)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', required=True, type=Path)
    p.add_argument('--out', required=True, type=Path)
    p.add_argument('--vertices', type=int, default=1000000)
    p.add_argument('--repeats', type=int, default=3)
    p.add_argument('--seed', type=int, default=1)
    p.add_argument('--shapes', nargs='+', choices=['random', 'path', 'star-out', 'cycle', 'disconnected', 'clustered', 'dense'], default=['random', 'path', 'star-out'])
    p.add_argument('--backends', nargs='+', choices=['csr32-out', 'byte32', 'packed32', 'ef32'], default=['csr32-out', 'byte32'])
    a = p.parse_args()
    if len(a.backends)<2 or len(set(a.backends))!=len(a.backends):p.error('choose at least two distinct backends')
    if not 1 <= a.vertices <= 10000000 or a.repeats < 1 or not 0 <= a.seed < 2**64 or len(set(a.shapes)) != len(a.shapes):
        p.error('vertices must be 1..10000000, repeats positive, seed uint64, shapes unique')
    binary = a.binary.resolve(); out = a.out.resolve()
    if not binary.is_file(): p.error('binary does not exist')
    out.mkdir(parents=True, exist_ok=False); (out/'raw').mkdir(); (out/'graphs').mkdir()
    manifest = dict(schema_version=1, started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    binary=str(binary), binary_sha256=sha(binary), runner_sha256=sha(Path(__file__)),
                    platform=platform.platform(), machine=platform.machine(), python=platform.python_version(),
                    vertices=a.vertices, seed=a.seed, timing_repeats=a.repeats, shapes=a.shapes, backends=a.backends,
                    concurrency=1, build_processes_separate=True, graph_files={},
                    query_policy='32-bit IDs; full BFS/DFS forests plus 100 repeated outgoing radius-2 BFS queries from vertex 0',
                    cache_policy='No OS cache flush; load includes SHA-256 and structural validation. Traversal starts after all graph data has been read.',
                    excluded_methods=['bfs-se', 'bfs-se32'])
    write(out/'manifest.json', manifest)
    runs = []
    def invoke(label, args, **tags):
        cmd = [str(binary), *map(str, args)]
        stamp = str(len(runs)+1).zfill(3)+'-'+label
        before = None
        if platform.system() == 'Darwin':
            before = subprocess.run(['vm_stat'], capture_output=True, text=True)
            (out/'raw'/f'{stamp}.vm-before.txt').write_text(before.stdout+before.stderr)
        start = time.monotonic()
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=900)
        elapsed = time.monotonic()-start
        (out/'raw'/f'{stamp}.stdout.json').write_text(proc.stdout)
        (out/'raw'/f'{stamp}.stderr.txt').write_text(proc.stderr)
        if before is not None:
            after = subprocess.run(['vm_stat'], capture_output=True, text=True)
            (out/'raw'/f'{stamp}.vm-after.txt').write_text(after.stdout+after.stderr)
        try: data = json.loads(proc.stdout)
        except ValueError: data = {'status':'failed', 'error':'invalid JSON'}
        run = dict(label=label, command=cmd, exit_code=proc.returncode, wall_seconds=elapsed, metrics=data, **tags)
        runs.append(run); write(out/'runs.json', runs)
        if proc.returncode or data.get('status') != 'ok': raise RuntimeError(f'{label} failed; see {out}/raw/{stamp}.stderr.txt')
        print(f'{len(runs):03d} {label}: {elapsed:.3f}s', flush=True)
        return data
    backends = a.backends
    workloads = {'bfs':['--traversal', 'bfs', '--forest'], 'dfs':['--traversal', 'dfs', '--forest'],
                 'local-bfs-100':['--traversal', 'bfs', '--source', 0, '--radius', 2, '--queries', 100]}
    for shape in a.shapes:
        for backend in backends:
            graph = out/'graphs'/f'{shape}-{backend}.sega'
            invoke(f'build-{shape}-{backend}', ['build', '--backend', backend, '--graph', graph,
                   '--shape', shape, '--vertices', a.vertices, '--seed', a.seed], phase='build', shape=shape, backend=backend)
            manifest['graph_files'][f'{shape}/{backend}'] = dict(path=str(graph), sha256=sha(graph), bytes=graph.stat().st_size)
            write(out/'manifest.json', manifest)
        # Correctness is checked before timing this graph family.
        for workload, policy in workloads.items():
            evidence = []
            for backend in backends:
                graph = out/'graphs'/f'{shape}-{backend}.sega'
                args = ['query', '--backend', backend, '--graph', graph, *policy]
                if workload == 'bfs': args += ['--audit']
                evidence.append(invoke(f'check-{shape}-{backend}-{workload}', args,
                                       phase='correctness', shape=shape, backend=backend, workload=workload))
            for key in ['event_sha256', 'visited', 'finished', 'roots', 'arcs_scanned', 'max_depth', 'max_pending', 'event_count', 'vertices', 'edges']:
                if any(evidence[0][key] != item[key] for item in evidence[1:]): raise RuntimeError(f'{shape}/{workload}: mismatch in {key}')
            if workload == 'bfs' and any(evidence[0]['topology_sha256'] != item['topology_sha256'] for item in evidence[1:]):
                raise RuntimeError(f'{shape}: topology mismatch')
        order = [(backend, workload) for backend in backends for workload in workloads]
        rng = random.Random(a.seed)
        for repeat in range(1, a.repeats+1):
            rng.shuffle(order)
            for backend, workload in order:
                graph = out/'graphs'/f'{shape}-{backend}.sega'
                d = invoke(f'time-{shape}-{backend}-{workload}-{repeat}',
                           ['query', '--backend', backend, '--graph', graph, *workloads[workload], '--verify', 'none'],
                           phase='timing', shape=shape, backend=backend, workload=workload, repeat=repeat)
                ref = next(r['metrics'] for r in runs if r['phase']=='correctness' and r['shape']==shape and r['backend']==backend and r['workload']==workload)
                for key in ['visited', 'finished', 'roots', 'arcs_scanned', 'max_depth', 'max_pending', 'vertices', 'edges']:
                    if d[key] != ref[key]: raise RuntimeError(f'{shape}/{backend}/{workload}: timing coverage mismatch in {key}')
    if sha(binary) != manifest['binary_sha256']: raise RuntimeError('binary changed during run')
    source_hashes = {r['metrics']['source_sha256'] for r in runs}
    if len(source_hashes)!=1: raise RuntimeError('inconsistent compiled sources')
    for value in manifest['graph_files'].values():
        if sha(value['path']) != value['sha256']: raise RuntimeError('saved graph changed during run')
    manifest['source_sha256'] = source_hashes.pop(); manifest['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    write(out/'manifest.json', manifest)
    rows = []
    for shape in a.shapes:
        for backend in backends:
            for workload in workloads:
                records = [r['metrics'] for r in runs if r['phase']=='timing' and r['shape']==shape and r['backend']==backend and r['workload']==workload]
                row = dict(shape=shape, backend=backend, workload=workload, repeats=len(records), vertices=records[0]['vertices'], edges=records[0]['edges'])
                for key in ['traversal_ms', 'load_ms', 'process_peak_kib', 'rss_after_load_kib', 'aux_peak_bytes', 'graph_file_bytes']:
                    values = [r[key] for r in records]
                    row[key] = dict(median=statistics.median(values), minimum=min(values), maximum=max(values))
                row['visited_per_query'] = records[0]['visited']; rows.append(row)
    summary = dict(status='ok', processes=len(runs), all_topology_and_event_hashes_match=True, all_timing_coverage_matches=True,
                   source_sha256=manifest['source_sha256'], timing_repeats=a.repeats, rows=rows,
                   total_process_wall_seconds=sum(r['wall_seconds'] for r in runs))
    write(out/'summary.json', summary)
    print(f'Complete: {out}/summary.json', flush=True)

if __name__ == '__main__':
    main()
