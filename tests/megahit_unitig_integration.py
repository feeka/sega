"""Native unitig smoke test; uses no two-frontier traversal or optional SDSL."""
import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="sega-native-unitig-") as directory:
    prefix = pathlib.Path(directory) / "graph"
    subprocess.run([sys.argv[1], str(prefix)], check=True, timeout=30)
    subprocess.run([sys.argv[2], str(prefix)], check=True, timeout=30)
    subprocess.run([sys.argv[2], directory], check=True, timeout=30)
