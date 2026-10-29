#!/usr/bin/env python3
"""Exercise the actual benchmark protocol and its failure behavior."""
import hashlib
import json
import pathlib
import subprocess
import sys
import tempfile

binary = pathlib.Path(sys.argv[1]).resolve()
methods = ['bfs-se','bfs-se32','bfs-base','bfs-base32','bfs-stream','bfs-stream32',
           'dfs-se','dfs-base','dfs-base32','dfs-packed','dfs-packed32']
count = 0

def invoke(args, success=True):
    global count
    run = subprocess.run([str(binary), *args], text=True, capture_output=True)
    result = json.loads(run.stdout)
    if success:
        assert run.returncode == 0, (args,run.stderr,result)
        assert result['status'] == 'ok'
        assert result['schema_version'] == 1
        assert result['aux_peak_bytes'] >= result['aux_fixed_bytes']
        assert result['aux_peak_bytes'] == result['aux_fixed_bytes'] + result['aux_dynamic_peak_bytes']
    else:
        assert run.returncode != 0 and result['status'] == 'error', (args,result)
    count += 1
    return result

with tempfile.TemporaryDirectory(prefix='sega-cli-') as temp:
    for shape in ['path','wide','cycle','disconnected','random']:
        for forest in [False,True]:
            reference = {}
            for mode in methods:
                path = pathlib.Path(temp) / (mode + '.bin')
                args = [shape,mode,'--nodes','129','--seed','91','--events',str(path)]
                args += ['--forest'] if forest else ['--source','2']
                r = invoke(args)
                family = mode[:3]
                expected = r['visited'] * (1 if family == 'bfs' else 2)
                assert r['event_count'] == expected
                assert hashlib.sha256(path.read_bytes()).hexdigest() == r['digest_sha256']
                assert r['event_file_bytes'] == path.stat().st_size
                assert path.read_bytes().startswith(b'SEGA-EVENTS-v1\n')
                signature = (r['visited'],r['roots'],r['digest_sha256'],path.read_bytes())
                if family in reference:
                    assert signature == reference[family], (args,r,reference[family][:3])
                reference[family] = signature
                if forest:
                    assert r['visited'] == r['valid_nodes'] == 129
                quiet = invoke([shape,mode,'--nodes','129','--seed','91','--verify','none'] + (['--forest'] if forest else ['--source','2']))
                assert quiet['digest_sha256'] is None and quiet['event_file_bytes'] == 0
                assert quiet['event_count'] == r['event_count']
                assert quiet['aux_peak_bytes'] == r['aux_peak_bytes']
    for mode in methods:
        r=invoke(['disconnected',mode,'--nodes','0','--forest'])
        assert r['visited'] == r['roots'] == r['event_count'] == 0
    audit=invoke(['wide','bfs-se','--nodes','129','--audit'])
    assert audit['audit']['status']=='ok'
    assert audit['audit']['transitions']==128
    assert sum(audit['audit']['outdegree_histogram'])==129
    for args in [[],['path','bad'],['path','bfs-se','--nodes','0'],['path','dfs-se','--nodes','5','--source','5'],
                 ['path','bfs-se','--source','-1'],['path','bfs-se','--source','18446744073709551615'],['path','bfs-se','--forest','--source','first'],
                 ['path','bfs-se','--verify','none','--audit'],['path','bfs-se','--nodes','no'],
                 ['path','bfs-se','--events',str(pathlib.Path(temp)/'missing'/'out.bin')]]:
        invoke(args,False)
print(f'Benchmark integration passed: {count} subprocess checks')
