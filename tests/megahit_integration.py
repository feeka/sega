#!/usr/bin/env python3
"""Real MEGAHIT writer/loader/oracle integration on a known complete tiny dBG.

Framing failures exercise our preflight, not arbitrary malformed BOSS payload
resilience. Biological correctness and performance still require real datasets.
"""
import collections
import hashlib
import json
import pathlib
import struct
import subprocess
import sys
import tempfile

generator = pathlib.Path(sys.argv[1]).resolve()
binary = pathlib.Path(sys.argv[2]).resolve()
methods = ['bfs-se', 'bfs-se32', 'bfs-base', 'bfs-base32', 'bfs-stream', 'bfs-stream32',
           'dfs-se', 'dfs-base', 'dfs-base32', 'dfs-packed', 'dfs-packed32']
count = 0


def require(condition, context):
    if not condition:
        raise RuntimeError(context)


def invoke(args, success=True):
    global count
    run = subprocess.run([str(binary), *map(str, args)], text=True, capture_output=True, timeout=30)
    result = json.loads(run.stdout)
    require(run.returncode == 0 if success else run.returncode != 0, (args, run.stdout, run.stderr))
    require(result['status'] == ('ok' if success else 'error'), (args, result))
    count += 1
    return result


def reference(source, family):
    """Construct transitions from strings (a,b)->(b,c), independent of oracle.

    The backend enumerates each outgoing source-label group in descending edge
    ID order. Preserve that documented ordering for exact DFS/event comparison.
    """
    data = bytearray(b'SEGA-EVENTS-v1\n')
    adjacency = [[(v % 4) * 4 + c for c in (3, 2, 1, 0)] for v in range(16)]
    if family == 'bfs':
        distance = {source: 0}
        queue = collections.deque([source])
        while queue:
            vertex = queue.popleft()
            data += b'B' + struct.pack('<QQ', vertex, distance[vertex])
            for neighbor in adjacency[vertex]:
                if neighbor not in distance:
                    distance[neighbor] = distance[vertex] + 1
                    queue.append(neighbor)
    else:
        visited = set()

        def dfs(vertex, parent):
            visited.add(vertex)
            data.extend(b'D' + struct.pack('<QQ', vertex, parent))
            for neighbor in adjacency[vertex]:
                if neighbor not in visited:
                    dfs(neighbor, vertex)
            data.extend(b'F' + struct.pack('<Q', vertex))

        dfs(source, 2**64 - 1)
    return bytes(data)


with tempfile.TemporaryDirectory(prefix='sega-megahit-') as temp:
    prefix = pathlib.Path(temp) / 'graph'
    generated = subprocess.run([str(generator), str(prefix)], text=True, capture_output=True, timeout=30)
    require(generated.returncode == 0, (generated.stdout, generated.stderr))
    if len(sys.argv) > 3:
        native = subprocess.run([sys.argv[3], str(prefix)], text=True, capture_output=True, timeout=30)
        require(native.returncode == 0, (native.stdout, native.stderr))
        print(native.stdout.strip())

    info = pathlib.Path(str(prefix) + '.sdbg_info')
    data = pathlib.Path(str(prefix) + '.sdbg.0')
    original_info, original_data = info.read_bytes(), data.read_bytes()
    overwrite = subprocess.run([str(generator), str(prefix)], text=True, capture_output=True, timeout=30)
    require(overwrite.returncode != 0, 'fixture generator must refuse overwrite')
    require(info.read_bytes() == original_info and data.read_bytes() == original_data, 'overwrite attempt modified fixture')
    topology = None
    for forest in [False, True]:
        source = 0 if forest else 7
        for mode in methods:
            output = pathlib.Path(temp) / (mode + '.events')
            args = [prefix, mode, '--audit', '--events', output]
            args += ['--forest'] if forest else ['--source', str(source)]
            r = invoke(args)
            family = mode[:3]
            exact = output.read_bytes()
            expected = reference(source, family)
            require(exact == expected, (mode, forest, 'events differ from independently constructed graph reference'))
            require(r['digest_sha256'] == hashlib.sha256(exact).hexdigest(), 'event SHA256 does not match file')
            require(r['event_file_bytes'] == len(exact), 'event file byte count')
            require(r['event_count'] == (16 if family == 'bfs' else 32), 'event count')
            require(r['N'] == r['valid_nodes'] == r['visited'] == 16, 'identifier and visited counts')
            require(r['k'] == 1 and r['roots'] == 1 and r['family'] == family, 'fixture metadata')
            require(r['source'] is None if forest else r['source'] == source, 'source metadata')
            audit = r['audit']
            require(audit['status'] == 'ok' and audit['transitions'] == 64, 'audit counts')
            require(audit['indegree_histogram'] == audit['outdegree_histogram'] == [0, 0, 0, 0, 16], 'degree histograms')
            if topology is None:
                topology = audit['topology_sha256']
            require(audit['topology_sha256'] == topology, 'topology digest changed')
    invoke([pathlib.Path(temp) / 'missing', 'bfs-se'], False)
    for bad_info in [b'', original_info.replace(b'k 1\n', b'k 0\n'),
                     original_info.replace(b'num_buckets 4\n', b'num_buckets 3\n'),
                     original_info.replace(b'0 0 0 4 0 0', b'18446744073709551615 0 0 4 0 0'),
                     original_info.replace(b'0 0 0 4 0 0', b'0 18446744073709551615 0 4 0 0'),
                     original_info[:-5], original_info + b'unexpected\n']:
        info.write_bytes(bad_info)
        invoke([prefix, 'bfs-se'], False)
    info.write_bytes(original_info)
    for bad_data in [original_data[:-1], original_data + b'\0']:
        data.write_bytes(bad_data)
        invoke([prefix, 'bfs-se'], False)
    data.unlink()
    invoke([prefix, 'bfs-se'], False)
    data.write_bytes(original_data)
    invoke([prefix, 'dfs-se', '--source', '16'], False)
    invoke([prefix, 'bfs-se', '--events', pathlib.Path(temp) / 'missing' / 'events'], False)
print(f'MEGAHIT integration passed: {count} benchmark subprocess checks plus generator and overwrite checks')
