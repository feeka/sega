#!/usr/bin/env python3
"""One entry point for six-tool de Bruijn graph measurements."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from representations.suite import main

if __name__=="__main__":
    try:raise SystemExit(main())
    except (ValueError,RuntimeError,OSError) as e:
        print(f"Benchmark failed: {e}",file=sys.stderr);raise SystemExit(2)
