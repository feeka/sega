#!/usr/bin/env python3
"""Retry an experiment using its saved settings, without a time limit by default."""
import argparse
import json
import os
from pathlib import Path
import sys
import tempfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from representations import suite

def require_idle(out):
    import psutil
    out=Path(out).resolve()
    for process in psutil.process_iter(['pid','cmdline','cwd']):
        if process.pid==os.getpid():continue
        try:
            command=process.info['cmdline'] or []
            if not any(Path(s).name in ('run_representations.py','resume_representations.py') for s in command):continue
            value='results/representations'
            for i,s in enumerate(command):
                if s=='--out' and i+1<len(command):value=command[i+1]
                elif s.startswith('--out='):value=s.split('=',1)[1]
            candidate=Path(value)
            if not candidate.is_absolute():
                if not process.info['cwd']:raise ValueError(f'Cannot check active runner PID {process.pid}; let it finish first')
                candidate=Path(process.info['cwd'])/candidate
            if candidate.resolve()==out:
                raise ValueError(f'This experiment is still running (PID {process.pid}). Let it finish before recovering.')
        except (psutil.NoSuchProcess,psutil.AccessDenied):continue

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True,help='Existing results directory')
    parser.add_argument('--timeout',type=float,help='Optional longer limit in seconds per operation; omit for no time limit')
    args=parser.parse_args(argv)
    out=args.out.resolve();require_idle(out)
    signature=json.loads((out/'manifest.json').read_text())['signature']
    command=[signature['mode'],'--out',str(out),'--resume']
    for name in ('k','threads','memory_gib','repeats','queries','sources','traversal_scope','seed'):
        command+=['--'+name.replace('_','-'),str(signature[name])]
    for name,key in (('input','input_order'),('backends','backends'),('operations','operations')):
        command+=['--'+name,*signature[key]]
    if args.timeout is not None:command+=['--timeout',str(args.timeout)]
    with tempfile.TemporaryDirectory(prefix='sega-resume-') as temporary:
        tools=Path(temporary)/'tools.json';tools.write_text(json.dumps(signature['tools']))
        return suite.main(command+['--tools',str(tools)])

if __name__=='__main__':
    try:raise SystemExit(main())
    except (ValueError,RuntimeError,OSError,KeyError,ImportError) as error:
        print(f'Recovery failed: {error}',file=sys.stderr);raise SystemExit(2)
