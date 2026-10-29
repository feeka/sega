#!/usr/bin/env python3
"""CLI protocol, exact event bytes, independent topology hashes and failures."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

BINARY = str(Path(sys.argv[1]).resolve())
processes = 0

def run(args, success=True):
    global processes
    processes += 1
    p = subprocess.run([BINARY, *args], text=True, capture_output=True)
    data = json.loads(p.stdout)
    assert (p.returncode == 0) == success, (args, p.stdout, p.stderr)
    assert data["status"] == ("ok" if success else "failed"), data
    if success:
        assert data["source_sha256"] and len(data["source_sha256"]) == 64
        assert data["aux_peak_bytes"] == data["aux_fixed_bytes"] + data["aux_dynamic_peak_bytes"]
        assert data["graph_encoded_bytes"] == 8 + sum(data[k] for k in
            ("targets_encoded_bytes", "boundaries_encoded_bytes", "incoming_encoded_bytes"))
    return data

with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    edge_file = root / "tiny.edges"
    edge_file.write_text("# unsorted, vertex 2 isolated\n3 0\n0 3\n1 3\n0 1\n")
    pairs = [(0, 1), (0, 3), (1, 3), (3, 0)]
    topology = hashlib.sha256(struct.pack("<QQ", 4, 4) + b"".join(struct.pack("<QQ", *p) for p in pairs)).hexdigest()
    expected = {
        "bfs": b"SEGA-EVENTS-v1\n" + b"".join(b"B" + struct.pack("<QQ", v, d)
                                               for v, d in [(0, 0), (1, 1), (3, 1), (2, 0)]),
        "dfs": b"SEGA-EVENTS-v1\n" + b"D" + struct.pack("<QQ", 0, 2**64-1)
            + b"D" + struct.pack("<QQ", 1, 0) + b"D" + struct.pack("<QQ", 3, 1)
            + b"F" + struct.pack("<Q", 3) + b"F" + struct.pack("<Q", 1)
            + b"F" + struct.pack("<Q", 0) + b"D" + struct.pack("<QQ", 2, 2**64-1)
            + b"F" + struct.pack("<Q", 2),
    }
    for backend in ("csr32", "csr64", "wavelet", "wavelet-rrr"):
        for traversal in ("bfs", "dfs"):
            base = ["--backend", backend, "--traversal", traversal, "--forest",
                    "--edges", str(edge_file), "--vertices", "4"]
            event_file = root / f"{backend}-{traversal}.events"
            data = run(base + ["--audit", "--events", str(event_file)])
            assert data["topology_sha256"] == topology
            assert event_file.read_bytes() == expected[traversal]
            assert data["event_sha256"] == hashlib.sha256(expected[traversal]).hexdigest()
            assert data["event_file_bytes"] == len(expected[traversal])
            assert data["event_count"] == (4 if traversal == "bfs" else 8)
            assert data["visited"] == 4 and data["roots"] == 2 and data["arcs_scanned"] == 4
            timing = run(base + ["--verify", "none", "--id-bits", "64"])
            assert timing["event_sha256"] is None and timing["event_count"] == 0
            assert timing["visited"] == data["visited"] and timing["arcs_scanned"] == data["arcs_scanned"]
            for shape, n in (("empty", 0), ("empty", 17), ("path", 65), ("cycle", 65),
                             ("star-out", 257), ("star-in", 257), ("disconnected", 65), ("random", 65)):
                result = run(["--backend", backend, "--traversal", traversal, "--forest",
                              "--shape", shape, "--vertices", str(n), "--audit"])
                assert result["visited"] == n and result["arcs_scanned"] == result["edges"]
    for args in (["--backend", "bad"], ["--traversal", "bfs-se"], ["--source", "4"],
                 ["--source", "0", "--forest"], ["--id-bits", "16"], ["--id-bits", "4294967328"],
                 ["--vertices", "-1"], ["--vertices", "18446744073709551615"], ["--source"],
                 ["--wat", "1"], ["--verify", "bad"], ["--verify", "none", "--audit"],
                 ["--verify", "none", "--events", str(root / "unused")],
                 ["--edges", str(edge_file)], ["--shape", "path", "--edges", str(edge_file), "--vertices", "4"],
                 ["--source", "0", "--source", "1"], ["--shape", "empty", "--vertices", "0"],
                 ["--events", str(edge_file)],
                 ["--edges", str(root / "missing"), "--vertices", "4"]):
        run(args, False)
    assert edge_file.read_text().startswith("# unsorted")  # output collision did not truncate input
    for invalid in ("0 1\n0 1\n", "0 4\n", "-1 2\n", "0\n", "0 1 2\n", "a b\n"):
        edge_file.write_text(invalid)
        for backend in ("csr32", "wavelet", "wavelet-rrr"):
            run(["--backend", backend, "--edges", str(edge_file), "--vertices", "4"], False)

print(json.dumps({"status": "ok", "processes": processes, "independent_exact_event_files": 8}))
