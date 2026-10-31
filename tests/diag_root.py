#!/usr/bin/env python3
"""Diagnostics must not land on the registry root unless -wexdiag is set."""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1] / 'WhateverXe'
for p in sorted(root.glob('*.[ch]pp')):
    if p.name.startswith('kern_diag.'):
        continue
    assert 'getRegistryRoot' not in p.read_text(), p.name
diag = (root / 'kern_diag.cpp').read_text()
assert 'checkKernelArgument("-wexdiag")' in diag
assert re.search(r'OSTypeAlloc\(IORegistryEntry\);\s*if \(!entry \|\| !entry->init\(\)\)', diag)
start = (root / 'kern_start.cpp').read_text()
assert start.index('wexDiagRoot();') < start.index('host.init();')
