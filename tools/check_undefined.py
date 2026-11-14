#!/usr/bin/env python3
"""Fail when the built kext imports a symbol the known-good build did not.

A variable defined inside an unnamed namespace of one file and declared extern in another stays
undefined. kxld still loads the kext, and the first use reads a wild address: on 2026-11-14 that
panicked in the page-in hook while WindowServer started and the machine stuck at the Apple logo.
Run after tools/build.py; --update rewrites the baseline after an import was reviewed.
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build/WhateverXe.kext/Contents/MacOS/WhateverXe'
BASELINE = ROOT / 'tools/undefined_symbols.txt'

now = sorted(set(subprocess.run(['nm', '-u', str(BINARY)], capture_output=True, text=True, check=True).stdout.split()))
if '--update' in sys.argv:
    BASELINE.write_text('\n'.join(now) + '\n')
    print(f'{len(now)} imported symbols written to {BASELINE.name}')
    sys.exit(0)
new = [s for s in now if s not in set(BASELINE.read_text().split())]
if new:
    print('imported symbols that the known-good build did not have:')
    for s in new:
        print('  ' + s)
    sys.exit(1)
print(f'imports match the baseline ({len(now)} symbols)')
