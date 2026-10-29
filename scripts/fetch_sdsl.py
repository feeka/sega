#!/usr/bin/env python3
"""Fetch the pinned optional dependency. Never reset an existing checkout."""
from pathlib import Path
import subprocess

REVISION = "c32874cb2d8524119f25f3b501526fe692df29f4"
ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "libs" / "sdsl-lite"
if DEST.exists():
    head = subprocess.check_output(["git", "-C", str(DEST), "rev-parse", "HEAD"], text=True).strip()
    if head != REVISION:
        raise SystemExit(f"Existing SDSL checkout is {head}; expected {REVISION}. Left unchanged.")
else:
    subprocess.run(["git", "clone", "https://github.com/simongog/sdsl-lite.git", str(DEST)], check=True)
    subprocess.run(["git", "-C", str(DEST), "checkout", "--detach", REVISION], check=True)
print(f"SDSL available at {DEST}, revision {REVISION}")
