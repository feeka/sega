"""New codec framing and structural validation, including rehashed corrupt files."""
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
    p = subprocess.run([binary, *map(str,args)], capture_output=True, text=True, timeout=30)
    result = json.loads(p.stdout)
    assert (p.returncode==0)==ok, (args,p.stdout,p.stderr)
    assert result['status']==('ok' if ok else 'failed')
    return result

with tempfile.TemporaryDirectory(prefix='sega-packed-') as tmp:
    root=Path(tmp)
    for shape,n in [('empty',0),('empty',65),('tiny',4),('path',129),('star-out',257),('random',127)]:
        expected={}
        for backend in ['csr32-out','packed32','ef32']:
            graph=root/f'{shape}-{n}-{backend}.graph'
            run(['build','--backend',backend,'--graph',graph,'--shape',shape,'--vertices',n])
            for traversal in ['bfs','dfs']:
                output=root/f'{shape}-{n}-{backend}-{traversal}.events'
                d=run(['query','--backend',backend,'--graph',graph,'--forest','--traversal',traversal,'--audit','--events',output])
                assert d['graph_file_bytes']==graph.stat().st_size
                assert d['graph_payload_capacity_bytes']>=d['graph_file_bytes']-120
                result=(d['topology_sha256'], d['event_sha256'], output.read_bytes())
                if backend=='csr32-out': expected[traversal]=result
                else: assert result==expected[traversal]
        # The C++ tests compare all four backends against an independent reference;
        # this process test additionally verifies saved and loaded event bytes.
    for backend in ['packed32','ef32']:
        original=root/f'tiny-4-{backend}.graph'
        blob=original.read_bytes()
        assert blob[:8]==b'SEGAPG01'
        assert hashlib.sha256(blob[:-64]).hexdigest().encode()==blob[-64:]
        fields=struct.unpack_from('<6Q',blob,8)
        assert fields[:4]==(1,3 if backend=='packed32' else 4,4,4)
        assert len(blob)==120+fields[4]+fields[5]
        assert (struct.unpack_from('<Q',blob,56)[0]&255)==61 # targets 1,3,3,0 at two bits each
        bad=root/'bad.graph'
        def reject(body, rehash=True):
            bad.write_bytes(bytes(body)+(hashlib.sha256(body).hexdigest().encode() if rehash else b''))
            run(['query','--backend',backend,'--graph',bad,'--forest'],False)
        for offset,value in [(8,2),(16,99),(24,2**32+1),(32,17),(40,0),(48,0)]:
            body=bytearray(blob[:-64]);struct.pack_into('<Q',body,offset,value);reject(body)
        body=bytearray(blob[:-64]);body[56]|=2;reject(body) # repeated target in row 0
        body=bytearray(blob[:-64]);body[63]|=128;reject(body) # nonzero target padding
        body=bytearray(blob[:-64])
        if backend=='packed32': body[64]|=1 # first offset must be zero
        else: struct.pack_into('<Q',body,len(body)-8,999) # false select-directory total
        reject(body)
        reject(blob[:-1],False);reject(blob+b'x',False)
        body=bytearray(blob);body[56]^=1;reject(body,False) # unchanged checksum
        other='ef32' if backend=='packed32' else 'packed32'
        run(['query','--backend',other,'--graph',original,'--forest'],False)
    run(['query','--backend','unknown','--graph',original],False)
print(json.dumps({'status':'ok','processes':processes}))
