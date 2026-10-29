#!/usr/bin/env python3
"""Small reproducible backend comparison. Separate correctness and timing jobs."""
import argparse
from collections import defaultdict
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import random
import statistics
import subprocess
import sys

BACKENDS = ("csr32", "csr64", "wavelet", "wavelet-rrr")
SHAPES = (("tiny", 4), ("path", 64), ("cycle", 64), ("star-out", 65),
          ("star-in", 65), ("disconnected", 64), ("random", 64))

def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024*1024), b""):
            h.update(block)
    return h.hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path, help="New output directory")
    parser.add_argument("--repeats", type=int, default=3, help="Timing repetitions only; one correctness run always precedes them")
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error("--repeats must be positive")
    binary = args.binary.resolve(strict=True)
    args.out.mkdir(parents=True, exist_ok=False)
    raw = args.out / "raw"
    raw.mkdir()
    manifest = {"created_utc": datetime.now(timezone.utc).isoformat(), "binary": str(binary),
                "binary_sha256": sha(binary), "runner_sha256": sha(Path(__file__)),
                "platform": platform.platform(), "machine": platform.machine(), "python": sys.version,
                "backends": BACKENDS, "shapes": SHAPES, "traversals": ["bfs", "dfs"],
                "correctness_repeats": 1, "timing_repeats": args.repeats, "shuffle_seed": 20261026,
                "note": "Tiny-example correctness and smoke timings; not paper-quality performance estimates."}
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")
    jobs = [(s,n,b,t) for s,n in SHAPES for b in BACKENDS for t in ("bfs","dfs")]
    schedule = [("correctness",0,job) for job in jobs]
    rng = random.Random(20261026)
    for repeat in range(1,args.repeats+1):
        block = jobs.copy()
        rng.shuffle(block)
        schedule.extend(("timing",repeat,job) for job in block)
    records, reference, groups = [], {}, defaultdict(list)
    try:
        for number,(phase,repeat,(shape,n,backend,traversal)) in enumerate(schedule,1):
            command = [str(binary), "--shape",shape,"--vertices",str(n),"--backend",backend,
                       "--traversal",traversal,"--forest","--verify","hash" if phase=="correctness" else "none"]
            if phase=="correctness":
                command += ["--audit"]
            p = subprocess.run(command,text=True,capture_output=True)
            stem = f"{number:04d}-{shape}-{backend}-{traversal}-{phase}-{repeat}"
            (raw / (stem+".stdout.json")).write_text(p.stdout)
            (raw / (stem+".stderr.txt")).write_text(p.stderr)
            record = {"run":number,"phase":phase,"repeat":repeat,"command":command,"returncode":p.returncode}
            try:
                data = json.loads(p.stdout)
                record["result"] = data
            except json.JSONDecodeError:
                raise RuntimeError(f"Invalid JSON in {stem}")
            records.append(record)
            if p.returncode or data.get("status")!="ok":
                raise RuntimeError(f"Failed job: {stem}; see raw stderr")
            key = shape,traversal
            identity = tuple(data[k] for k in ("vertices","edges","visited","finished","roots","arcs_scanned"))
            if phase=="correctness":
                signature = identity+(data["topology_sha256"],data["event_sha256"])
                if key in reference and reference[key]!=signature:
                    raise RuntimeError(f"Backend correctness mismatch for {key}")
                reference[key]=signature
            elif identity!=reference[key][:6]:
                raise RuntimeError(f"Timing/count mismatch for {key}")
            else:
                groups[shape,backend,traversal].append(data)
        if sha(binary)!=manifest["binary_sha256"]:
            raise RuntimeError("Binary changed during experiment")
        if len({r["result"]["source_sha256"] for r in records})!=1:
            raise RuntimeError("Mixed source builds")
        rows=[]
        for (shape,backend,traversal),runs in groups.items():
            times=[r["traversal_ms"] for r in runs]
            rows.append({"shape":shape,"backend":backend,"traversal":traversal,
                         "repeats":len(runs),"median_ms":statistics.median(times),
                         "min_ms":min(times),"max_ms":max(times),
                         "graph_encoded_bytes":runs[0]["graph_encoded_bytes"],
                         "aux_peak_bytes":runs[0]["aux_peak_bytes"]})
        summary={"status":"ok","processes":len(records),"correctness_processes":len(jobs),
                 "timing_processes":len(jobs)*args.repeats,"rows":sorted(rows,key=lambda r:(r["shape"],r["traversal"],r["backend"]))}
    except Exception as exc:
        summary={"status":"failed","error":str(exc),"completed_processes":len(records)}
    (args.out / "runs.json").write_text(json.dumps(records,indent=2)+"\n")
    (args.out / "summary.json").write_text(json.dumps(summary,indent=2)+"\n")
    print(json.dumps({k:v for k,v in summary.items() if k!="rows"},indent=2))
    return 0 if summary["status"]=="ok" else 1

if __name__=="__main__":
    raise SystemExit(main())
