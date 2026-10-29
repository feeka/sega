#!/usr/bin/env python3
"""Run isolated sega measurements and streamed correctness checks (stdlib only)."""

import argparse
import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import re
import statistics
import subprocess
import sys
import tarfile
import time

METHODS = (
    "bfs-se", "bfs-se32", "bfs-base", "bfs-base32", "bfs-stream", "bfs-stream32",
    "dfs-se", "dfs-base", "dfs-base32", "dfs-packed", "dfs-packed32",
)
# Paper scope: retain the implementations for historical reproducibility, but
# never include the two-frontier BFS variants in default experiments.
DEFAULT_METHODS = tuple(m for m in METHODS if m not in ("bfs-se", "bfs-se32"))
REPO = Path(__file__).resolve().parents[1]


def atomic_json(path, obj):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(obj, indent=2, sort_keys=True, allow_nan=False) + "\n")
    temporary.replace(path)


def sha256_file(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def exact_equal(a, b):
    a, b = Path(a), Path(b)
    if a.stat().st_size != b.stat().st_size:
        return False
    with a.open("rb") as left, b.open("rb") as right:
        while True:
            x, y = left.read(1024 * 1024), right.read(1024 * 1024)
            if x != y:
                return False
            if not x:
                return True


def capture(command, cwd=REPO):
    try:
        r = subprocess.run(command, cwd=cwd, capture_output=True, text=True, timeout=20)
        return {"command": command, "returncode": r.returncode,
                "stdout": r.stdout, "stderr": r.stderr}
    except (OSError, subprocess.TimeoutExpired) as e:
        return {"command": command, "error": str(e)}


def file_metadata(path, with_hash=False):
    stat = path.stat()
    result = {"path": str(path.resolve()), "bytes": stat.st_size,
              "mtime_ns": stat.st_mtime_ns}
    if with_hash:
        result["sha256"] = sha256_file(path)
    return result


def graph_files(prefix):
    prefix = Path(prefix).resolve()
    # Only the MEGAHIT serialized graph, never unrelated files sharing a prefix.
    candidates = [prefix.parent / (prefix.name + ".sdbg_info")]
    candidates += list(prefix.parent.glob(prefix.name + ".sdbg.*"))
    return sorted(p for p in candidates if p.is_file())


def provenance(args, binary, out):
    if (REPO / ".git").exists():
        git = {
            "head": capture(["git", "rev-parse", "HEAD"]),
            "status": capture(["git", "status", "--short"]),
            "submodules": capture(["git", "submodule", "status", "--recursive"]),
            "diff": capture(["git", "diff", "--binary", "HEAD", "--", "."]),
        }
        (out / "working-tree.patch").write_text(git.pop("diff").get("stdout", ""))
    else:
        git = {"available": False, "reason": "No .git entry in source root; archive build"}
        (out / "working-tree.patch").write_text("")
    # Record source content even when edits have not been committed. Never dump
    # the environment, git remotes, credentials, home directory, or /proc/environ.
    sources = []
    for directory in ("include", "backends", "bench", "tests", "scripts", "docs", "cmake"):
        for p in sorted((REPO / directory).rglob("*")):
            if p.is_file() and "__pycache__" not in p.parts:
                sources.append({"path": str(p.relative_to(REPO)), "sha256": sha256_file(p)})
    for name in ("CMakeLists.txt", "README.md"):
        p = REPO / name
        sources.append({"path": name, "sha256": sha256_file(p)})
    bundle = None
    if (REPO / "SOURCE_BUNDLE.json").is_file():
        bundle = json.loads((REPO / "SOURCE_BUNDLE.json").read_text())
        (out / "SOURCE_BUNDLE.json").write_bytes((REPO / "SOURCE_BUNDLE.json").read_bytes())
    with tarfile.open(out / "source_snapshot.tar.gz", "w:gz") as archive:
        for source in sources:
            archive.add(REPO / source["path"], arcname=source["path"], recursive=False)
    graphs = []
    if args.graph:
        prefix = Path(args.graph).resolve()
        files = graph_files(prefix)
        if not files:
            raise ValueError("No graph files found for prefix: " + str(prefix))
        graphs = [file_metadata(p, args.hash_graph_files) for p in files]
    cpu = None
    cpuinfo = Path("/proc/cpuinfo")
    if cpuinfo.exists():
        models = [line.split(":", 1)[1].strip() for line in cpuinfo.read_text().splitlines()
                  if line.startswith("model name") and ":" in line]
        cpu = models[0] if models else None
    memory = {}
    meminfo = Path("/proc/meminfo")
    if meminfo.exists():
        for line in meminfo.read_text().splitlines():
            key, val = line.split(":", 1)
            if key in ("MemTotal", "MemAvailable", "SwapTotal", "SwapFree"):
                memory[key] = val.strip()
    build_files = []
    for name in ("CMakeCache.txt", "compile_commands.json", "build_info.json"):
        candidate = binary.parent / name
        if candidate.is_file():
            # Copy build provenance, not arbitrary process environment.
            (out / name).write_bytes(candidate.read_bytes())
            build_files.append(name)
    return {
        "schema_version": 1, "created_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "argv": sys.argv, "binary": file_metadata(binary, True),
        "platform": {"system": platform.system(), "release": platform.release(),
                     "machine": platform.machine(), "python": platform.python_version(),
                     "logical_cpus": os.cpu_count(), "cpu_model": cpu, "memory": memory},
        "environment": {k: os.environ[k] for k in
                        ("OMP_NUM_THREADS", "OMP_PROC_BIND", "OMP_PLACES", "MKL_NUM_THREADS", "OPENBLAS_NUM_THREADS")
                        if k in os.environ},
        "git": git, "source_bundle": bundle, "source_sha256": sources, "graph_files": graphs,
        "build_files": build_files,
        "compiler_candidates": [capture(["c++", "--version"]), capture(["cmake", "--version"])],
        "cache_policy": "No page-cache flush; randomized fresh-process runs, graph load excluded from wall_ms.",
        "measurement_policy": "Correctness hashing/event I/O is disabled in measurement processes.",
        "schedule": schedule_summary(args),
    }


def validate_record(record, method, phase, forest):
    if not isinstance(record, dict) or type(record.get("schema_version")) is not int or record.get("schema_version") != 1 or record.get("status") != "ok":
        raise ValueError("Benchmark did not return schema_version=1, status=ok")
    family = method.split("-", 1)[0]
    if record.get("method") != method or record.get("family") != family:
        raise ValueError("Benchmark method/family differs from request")
    if record.get("forest") is not forest:
        raise ValueError("Benchmark forest flag differs from request")
    for key in ("N", "valid_nodes", "visited", "roots", "event_count", "k", "event_file_bytes"):
        value = record.get(key)
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise ValueError("Invalid integer metric: " + key)
    if record["valid_nodes"] > record["N"] or record["visited"] > record["valid_nodes"] or record["roots"] > record["visited"]:
        raise ValueError("Impossible visited/root count")
    if forest and record["visited"] != record["valid_nodes"]:
        raise ValueError("Forest does not cover all valid vertices")
    expected_events = record["visited"] * (1 if family == "bfs" else 2)
    if record["event_count"] != expected_events:
        raise ValueError("Event count disagrees with visited count")
    if "source" not in record or "k" not in record or "backend" not in record:
        raise ValueError("Missing graph/source metadata")
    if forest and record["source"] is not None:
        raise ValueError("Forest source must be null")
    if not forest and (isinstance(record["source"], bool) or
                       not isinstance(record["source"], int) or
                       not 0 <= record["source"] < record["N"]):
        raise ValueError("Invalid resolved source")
    for key in ("wall_ms", "load_ms"):
        value = record.get(key)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
            raise ValueError("Invalid timing: " + key)
    digest = record.get("digest_sha256")
    expected_verification = "sha256" if phase == "correctness" else "none"
    if record.get("verification") != expected_verification:
        raise ValueError("Verification mode differs from requested phase")
    if phase == "correctness" and (not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest)):
        raise ValueError("Missing or malformed correctness SHA-256")
    if phase == "measurement" and digest is not None:
        raise ValueError("Measurement run unexpectedly hashed output")
    if phase == "measurement" and record["event_file_bytes"] != 0:
        raise ValueError("Measurement run unexpectedly wrote events")
    for key in ("aux_fixed_bytes", "aux_dynamic_peak_bytes", "aux_peak_bytes"):
        if type(record.get(key)) is not int or record[key] < 0:
            raise ValueError("Invalid allocation metric: " + key)
    for key in ("graph_rss_kb", "graph_peak_rss_kb", "peak_rss_kb", "rss_after_kb", "swap_kb"):
        if type(record.get(key)) is not int or record[key] < -1:
            raise ValueError("Invalid process memory metric: " + key)
    audit = record.get("audit")
    if audit is not None:
        if not isinstance(audit, dict) or audit.get("status") != "ok" or audit.get("valid_nodes") != record["valid_nodes"]:
            raise ValueError("Graph audit failed or disagrees with valid-node count")
        if not re.fullmatch(r"[0-9a-f]{64}", str(audit.get("topology_sha256", ""))):
            raise ValueError("Missing graph topology SHA-256")
        if type(audit.get("transitions")) is not int or audit["transitions"] < 0:
            raise ValueError("Invalid audited adjacency count")
        for key in ("indegree_histogram", "outdegree_histogram"):
            histogram = audit.get(key)
            if (not isinstance(histogram, list) or len(histogram) != 5 or
                    any(type(x) is not int or x < 0 for x in histogram) or
                    sum(histogram) != record["valid_nodes"] or
                    sum(degree * count for degree, count in enumerate(histogram)) != audit["transitions"]):
                raise ValueError("Invalid graph audit degree histogram: " + key)
    return record


def compare_records(records, exact=False, references=None):
    if references is None:
        references = {}
    errors = []
    for entry in records:
        if entry.get("error") or "result" not in entry:
            continue
        result = entry["result"]
        shared_key = (entry["target"], "graph")
        shared = tuple(result.get(k) for k in ("backend", "N", "k", "source", "forest", "valid_nodes", "visited", "roots"))
        if references.setdefault(shared_key, shared) != shared:
            errors.append("Cross-family graph/coverage mismatch: " + entry["run_id"])
        key = (entry["target"], result["family"])
        invariant = tuple(result.get(k) for k in ("backend", "N", "k", "source", "forest", "valid_nodes", "visited", "roots", "event_count"))
        ref = references.setdefault(key, {"invariant": invariant})
        if invariant != ref["invariant"]:
            errors.append("Graph/output metadata mismatch: " + entry["run_id"])
        if entry["phase"] == "correctness":
            if "digest" in ref and result["digest_sha256"] != ref["digest"]:
                errors.append("Correctness digest mismatch: " + entry["run_id"])
            else:
                ref["digest"] = result["digest_sha256"]
            if exact:
                path = entry.get("events")
                if path is None or not Path(path).is_file():
                    errors.append("Missing exact event file: " + entry["run_id"])
                elif "events" in ref and not exact_equal(path, ref["events"]):
                    errors.append("Exact event stream mismatch: " + entry["run_id"])
                else:
                    ref["events"] = path
    return errors


def summarize(records):
    groups = {}
    for entry in records:
        if entry["phase"] != "measurement" or entry.get("error") or "result" not in entry:
            continue
        groups.setdefault((entry["target"], entry["method"]), []).append(entry["result"])
    summaries = []
    metrics = ("wall_ms", "load_ms", "valid_scan_ms", "cpu_ms", "major_faults", "minor_faults", "peak_rss_kb", "graph_rss_kb", "aux_fixed_bytes",
               "aux_dynamic_peak_bytes", "aux_peak_bytes", "max_frontier", "max_pending", "max_depth")
    for (target, method), runs in sorted(groups.items()):
        summary = {"target": target, "method": method, "runs": len(runs), "metrics": {}}
        for key in metrics:
            vals = [r[key] for r in runs if isinstance(r.get(key), (int, float)) and r[key] >= 0]
            if vals:
                summary["metrics"][key] = {"median": statistics.median(vals), "min": min(vals), "max": max(vals)}
        summaries.append(summary)
    return summaries


def make_jobs(args):
    """Validate once by default, then measure in randomized repetition blocks."""
    rng = random.Random(args.seed)
    jobs = [(target, method, "correctness", repeat) for target in args.sources
            for method in args.methods for repeat in range(1, args.correctness_repeats + 1)]
    rng.shuffle(jobs)
    for repeat in range(1, args.repeats + 1):
        block = [(target, method, "measurement", repeat) for target in args.sources
                 for method in args.methods]
        rng.shuffle(block)
        jobs.extend(block)
    return jobs


def schedule_summary(args):
    combinations = len(args.methods) * len(args.sources)
    return {"methods": args.methods, "targets": args.sources,
            "measurement_repeats": args.repeats,
            "correctness_repeats": args.correctness_repeats,
            "measurement_processes": combinations * args.repeats,
            "correctness_processes": combinations * args.correctness_repeats,
            "total_processes": combinations * (args.repeats + args.correctness_repeats),
            "order": "correctness first, then randomized measurement repetition blocks"}


def parse_args(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--binary", required=True, type=Path)
    graphs = p.add_mutually_exclusive_group(required=True)
    graphs.add_argument("--graph", help="MEGAHIT graph file prefix")
    graphs.add_argument("--synthetic", choices=("path", "wide", "cycle", "disconnected", "random"))
    p.add_argument("--nodes", type=int, default=10000)
    p.add_argument("--out", required=True, type=Path, help="New output directory (must not exist)")
    p.add_argument("--repeats", type=int, default=5, help="Timing repetitions per method/target (default: 5)")
    p.add_argument("--correctness-repeats", type=int, default=1,
                   help="Separate hash checks per method/target (default: 1)")
    p.add_argument("--dry-run", action="store_true", help="Print process counts without running or writing files")
    targets = p.add_mutually_exclusive_group()
    targets.add_argument("--sources", default="first", help="Comma-separated first or numeric source IDs")
    targets.add_argument("--forest", action="store_true")
    p.add_argument("--methods", default=",".join(DEFAULT_METHODS),
                   help="Comma-separated methods; default excludes the two-frontier BFS variants")
    p.add_argument("--seed", type=int, default=42, help="Deterministic run shuffle and synthetic graph seed")
    p.add_argument("--exact-events", action="store_true", help="Retain and compare binary streams; potentially large disk use")
    p.add_argument("--audit", action="store_true", help="Audit graph once per target, in a correctness process")
    p.add_argument("--hash-graph-files", action="store_true", help="SHA-256 every graph file before and after experiment")
    p.add_argument("--timeout", type=float, default=None, help="Seconds per process; default unlimited")
    args = p.parse_args(argv)
    if args.repeats < 1 or args.correctness_repeats < 1 or args.nodes < 0 or (args.timeout is not None and (not math.isfinite(args.timeout) or args.timeout <= 0)):
        p.error("repeats must be positive, nodes nonnegative, and timeout positive")
    if not 0 <= args.seed <= 2**64 - 1:
        p.error("seed must fit an unsigned 64-bit integer")
    args.methods = args.methods.split(",")
    if not args.methods or len(set(args.methods)) != len(args.methods) or any(m not in METHODS for m in args.methods):
        p.error("methods must be distinct members of: " + ",".join(METHODS))
    for family in ("bfs", "dfs"):
        members = [m for m in args.methods if m.startswith(family)]
        if len(members) == 1:
            p.error("Select at least two methods per included traversal family for comparison")
    args.sources = ["forest"] if args.forest else args.sources.split(",")
    if not args.forest and (len(set(args.sources)) != len(args.sources) or any(s != "first" and (not s.isdecimal() or int(s) > 2**64 - 1) for s in args.sources)):
        p.error("sources must be unique comma-separated first or nonnegative integers")
    return args


def main(argv=None):
    args = parse_args(argv)
    schedule = schedule_summary(args)
    if args.dry_run:
        print(json.dumps(schedule, indent=2))
        return 0
    binary = args.binary.resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        print("Binary missing or not executable: " + str(binary), file=sys.stderr)
        return 2
    out = args.out.resolve()
    try:
        out.mkdir(parents=True, exist_ok=False)
    except FileExistsError:
        print("Refusing to overwrite existing output directory: " + str(out), file=sys.stderr)
        return 2
    records, errors = [], []
    try:
        meta = provenance(args, binary, out)
        atomic_json(out / "manifest.json", meta)
        jobs = make_jobs(args)
        print(f"Plan: {schedule['correctness_processes']} correctness + "
              f"{schedule['measurement_processes']} measurement = {len(jobs)} processes", flush=True)
        audited, references = set(), {}
        for number, (target, method, phase, repeat) in enumerate(jobs, 1):
            run_id = f"{number:04d}-{target}-{method}-{phase}-r{repeat}"
            cmd = [str(binary), str(Path(args.graph).resolve()) if args.graph else args.synthetic, method]
            if args.synthetic:
                cmd += ["--nodes", str(args.nodes), "--seed", str(args.seed)]
            cmd += ["--forest"] if args.forest else ["--source", target]
            cmd += ["--verify", "hash" if phase == "correctness" else "none"]
            entry = {"run_id": run_id, "target": target, "method": method,
                     "phase": phase, "repeat": repeat, "command": cmd}
            if phase == "correctness" and args.audit and target not in audited:
                cmd.append("--audit")
                audited.add(target)
            if phase == "correctness" and args.exact_events:
                events = out / (run_id + ".events")
                cmd += ["--events", str(events)]
                entry["events"] = str(events)
            print(f"[{number}/{len(jobs)}] {run_id}", flush=True)
            start = time.monotonic()
            with (out / (run_id + ".stdout")).open("wb") as stdout, (out / (run_id + ".stderr")).open("wb") as stderr:
                try:
                    process = subprocess.run(cmd, stdout=stdout, stderr=stderr, timeout=args.timeout)
                    entry["returncode"] = process.returncode
                    if process.returncode:
                        raise ValueError("Benchmark exited with code " + str(process.returncode))
                except (OSError, subprocess.TimeoutExpired, ValueError) as e:
                    entry["error"] = str(e)
            entry["process_wall_seconds"] = time.monotonic() - start
            if "error" not in entry:
                try:
                    raw = (out / (run_id + ".stdout")).read_text()
                    entry["result"] = validate_record(json.loads(raw), method, phase, args.forest)
                    if "--audit" in cmd and entry["result"].get("audit") is None:
                        raise ValueError("Requested graph audit was not reported")
                    if target.isdecimal() and entry["result"]["source"] != int(target):
                        raise ValueError("Resolved source differs from requested source")
                    if args.synthetic and entry["result"]["N"] != args.nodes:
                        raise ValueError("Synthetic node count differs from request")
                    if phase == "correctness" and args.exact_events:
                        events = Path(entry["events"])
                        bytes_expected = 15 + entry["result"]["visited"] * (17 if method.startswith("bfs") else 26)
                        if events.stat().st_size != bytes_expected:
                            raise ValueError("Exact event file length disagrees with recorded event count")
                        if sha256_file(events) != entry["result"]["digest_sha256"]:
                            raise ValueError("Exact event file SHA-256 differs from benchmark digest")
                    atomic_json(out / (run_id + ".json"), entry["result"])
                except (OSError, UnicodeError, ValueError, TypeError) as e:
                    entry["error"] = str(e)
            records.append(entry)
            atomic_json(out / "runs.json", records)
            if entry.get("error"):
                errors.append(run_id + ": " + entry["error"])
            # Compare immediately; do not spend hours after a detected mismatch.
            errors += compare_records([entry], args.exact_events, references)
            if errors:
                break
        for item in meta["graph_files"]:
            now = file_metadata(Path(item["path"]), args.hash_graph_files)
            if now != item:
                errors.append("Graph file changed during experiment: " + item["path"])
        if args.graph and [str(p.resolve()) for p in graph_files(args.graph)] != [p["path"] for p in meta["graph_files"]]:
            errors.append("Graph file list changed during experiment")
        if sha256_file(binary) != meta["binary"]["sha256"]:
            errors.append("Benchmark binary changed during experiment")
        if not errors and len(records) != len(jobs):
            errors.append("Experiment did not complete all requested runs")
        summary = {"schema_version": 1, "status": "failed" if errors else "passed",
                   "schedule": schedule,
                   "comparison": "exact binary streams plus SHA-256" if args.exact_events else "SHA-256 stream equality (probabilistic)",
                   "errors": list(dict.fromkeys(errors)), "completed_runs": len(records), "planned_runs": len(jobs),
                   "measurement_summaries": summarize(records),
                   "notes": ["Hash equality is strong probabilistic evidence, not a proof of algorithm correctness.",
                             "Equal outputs between implementations do not exclude a shared graph/backend error.",
                             "Only measurement runs contribute timing/memory summaries; correctness runs are retained separately."]}
        atomic_json(out / "summary.json", summary)
        print("Result: " + summary["status"] + " (" + str(out / "summary.json") + ")", flush=True)
        return 1 if errors else 0
    except (OSError, ValueError, KeyboardInterrupt) as e:
        atomic_json(out / "summary.json", {"schema_version": 1, "status": "failed",
                    "errors": [str(e) or "Interrupted"], "completed_runs": len(records)})
        print("Experiment failed: " + str(e), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
