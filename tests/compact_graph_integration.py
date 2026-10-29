#!/usr/bin/env python3
"""Independent file framing/checksum, exact output, corrupt data and CLI checks."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

binary = str(Path(sys.argv[1]).resolve())
processes = 0

def run(args, ok=True):
    global processes
    processes += 1
    p = subprocess.run([binary, *map(str, args)], capture_output=True, text=True, timeout=60)
    d = json.loads(p.stdout)
    assert (p.returncode == 0) == ok, (args, p.stdout, p.stderr)
    assert d['status'] == ('ok' if ok else 'failed'), d
    return d

def event_bytes(family, pairs, n, source=0, forest=True, radius=None):
    from collections import deque
    adj = [[] for _ in range(n)]
    for u, v in sorted(pairs): adj[u].append(v)
    seen = set()
    data = bytearray(b'SEGA-EVENTS-v1\n')
    def dfs(u, parent):
        seen.add(u)
        data.extend(b'D' + struct.pack('<QQ', u, parent))
        for v in adj[u]:
            if v not in seen: dfs(v, u)
        data.extend(b'F' + struct.pack('<Q', u))
    for root in range(n) if forest else [source]:
        if root in seen: continue
        if family == 'dfs': dfs(root, 2**64-1); continue
        q = deque([(root, 0)]); seen.add(root)
        while q:
            u, d = q.popleft(); data.extend(b'B' + struct.pack('<QQ', u, d))
            if radius is not None and d == radius: continue
            for v in adj[u]:
                if v not in seen: seen.add(v); q.append((v, d+1))
    return bytes(data)

with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    edge_file = root/'tiny.edges'; edge_file.write_text('3 0\n0 3\n1 3\n0 1\n')
    pairs = [(0, 1), (0, 3), (1, 3), (3, 0)]
    topo = hashlib.sha256(struct.pack('<QQ', 4, 4)+b''.join(struct.pack('<QQ', *e) for e in pairs)).hexdigest()
    for backend in ('csr32-out', 'byte32'):
        graph = root/(backend+'.graph')
        build = ['build', '--backend', backend, '--graph', graph, '--edges', edge_file, '--vertices', 4]
        r = run(build); original = graph.read_bytes()
        assert original[:8] == b'SEGACG01'
        assert original[-64:].decode() == hashlib.sha256(original[:-64]).hexdigest()
        assert len(original) == r['graph_file_bytes']
        run(build, False); assert graph.read_bytes() == original
        base = ['query', '--backend', backend, '--graph', graph]
        for family in ('bfs', 'dfs'):
            events = root/(backend+family+'.events')
            d = run(base+['--traversal', family, '--forest', '--audit', '--events', events])
            want = event_bytes(family, pairs, 4)
            assert events.read_bytes() == want
            assert d['event_sha256'] == hashlib.sha256(want).hexdigest() and d['topology_sha256'] == topo
            assert d['visited'] == 4 and d['arcs_scanned'] == 4
            assert d['graph_file_bytes'] == len(original)
            assert d['aux_peak_bytes'] == d['aux_fixed_bytes']+d['aux_dynamic_peak_bytes']
            timing = run(base+['--traversal', family, '--forest', '--verify', 'none'])
            assert timing['event_sha256'] is None and timing['event_count'] == 0
        for radius in (0, 1, 2, 100):
            d = run(base+['--source', 0, '--radius', radius, '--queries', 3])
            one = event_bytes('bfs', pairs, 4, forest=False, radius=radius)
            repeated = one + one[len(b'SEGA-EVENTS-v1\n'):]*2
            assert d['event_sha256'] == hashlib.sha256(repeated).hexdigest()
        for extra in (['--source', 4], ['--forest', '--source', 0], ['--radius', 1, '--forest'],
                      ['--traversal', 'dfs', '--radius', 1], ['--traversal', 'bfs-se'],
                      ['--queries', 0], ['--queries', 2, '--traversal', 'dfs'], ['--verify', 'bad'],
                      ['--verify', 'none', '--audit'], ['--events', graph], ['--source', '-1'],
                      ['--vertices', 4], ['--source', 0, '--source', 1], ['--wat', 1]):
            run(base+extra, False)
        assert graph.read_bytes() == original
        # Recompute checksums so structural failures exercise validation itself.
        def malformed(raw, repair=True):
            if repair: raw[-64:] = hashlib.sha256(raw[:-64]).hexdigest().encode()
            graph.write_bytes(raw); run(base, False)
        for pos, val in ((8, 2), (16, 3), (24, 2**64-1), (32, 99), (40, 2**64-1),
                         (48, 77), (56, 1), (64, 999), (88, 0)):
            b = bytearray(original); struct.pack_into('<Q', b, pos, val); malformed(b)
        b = bytearray(original); b[96] = 9; malformed(b)  # out-of-range target
        b = bytearray(original); b[96] ^= 1; malformed(b, False)  # checksum
        if backend == 'byte32':
            b = bytearray(original); b[96:98] = b'\x80\x00'; malformed(b)  # overlong zero
            b = bytearray(original); b[96:98] = b'\x80\x80'; malformed(b)  # unterminated row
            b = bytearray(original); b[97] = 0; malformed(b)  # duplicate via zero gap
        for cut in (0, 7, 55, 56, 100, len(original)-1): malformed(bytearray(original[:cut]), False)
        malformed(bytearray(original+b'x'), False)
        graph.write_bytes(original)
        for shape, n in (('empty', 0), ('empty', 17), ('path', 257), ('cycle', 257), ('star-out', 300),
                         ('star-in', 300), ('disconnected', 65), ('random', 65)):
            f = root/(backend+shape+str(n)+'.graph')
            run(['build', '--backend', backend, '--graph', f, '--shape', shape, '--vertices', n])
            for family in ('bfs', 'dfs'):
                d = run(['query', '--backend', backend, '--graph', f, '--traversal', family, '--forest', '--audit'])
                assert d['visited'] == n and d['arcs_scanned'] == d['edges']
        for bad in ('0 1\n0 1\n', '0 4\n', '-1 2\n', '0\n', '0 1 2\n'):
            edge_file.write_text(bad)
            run(['build', '--backend', backend, '--graph', root/'unused', '--edges', edge_file, '--vertices', 4], False)
        edge_file.write_text('3 0\n0 3\n1 3\n0 1\n')
    run(['build', '--graph', root/'unused', '--shape', 'bad'], False)
    run(['query', '--graph', root/'missing'], False)
print(json.dumps({'status':'ok', 'processes':processes, 'independent_exact_event_files':4}))
