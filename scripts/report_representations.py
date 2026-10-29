#!/usr/bin/env python3
"""Rewrite readable reports from saved results, without running experiments."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from representations.reporting import render_report, render_overview

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('directory', type=Path, help='A completed experiment or a review directory containing experiment subdirectories')
args = parser.parse_args()
root = args.directory.resolve()
if (root / 'runs.json').is_file():
    render_report(root)
else:
    cases = sorted(path.parent for path in root.glob('*/runs.json'))
    if not cases:
        parser.error('No saved runs.json found in this directory or its immediate subdirectories')
    for case in cases:
        render_report(case)
    render_overview(root, 'k31' if (root / 'k31/runs.json').is_file() else cases[0].name)
print(root / 'report.html')
