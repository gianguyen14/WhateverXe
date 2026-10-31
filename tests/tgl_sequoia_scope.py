#!/usr/bin/env python3
"""Pin the TGL Sequoia recovery scope and keep optional GPU overrides disabled."""
from pathlib import Path
import re
repo = Path(__file__).resolve().parents[1]
source = (repo/'WhateverXe/kern_xe.cpp').read_text()
sequoia = 'XELP::kernelOS(version)==XELP::OS::Sequoia'
tahoe = 'XELP::kernelOS(version)==XELP::OS::Tahoe'
# Optional overrides stay disabled on every OS: 1 completion, 2 indirect,
# 4 same-tail, 8 GSYNC, 16 RPCS.
assert re.search(r'static constexpr uint32_t tglTahoeDisabledTrials=31;', source)
scope = re.search(r'const bool tahoeTrialScope=(.*?);', source, re.S).group(1)
assert '!adlnDetected' in scope and 'checkKernelArgument("-disablegfxfirmware")' in scope
assert tahoe in scope and sequoia in scope
# Single-pass recovery requires host scheduling and the admitted OS families.
recovery = re.search(r'const bool recoveryRequested=(.*?);', source, re.S).group(1)
assert 'checkKernelArgument("-disablegfxfirmware")' in recovery
assert {m for m in re.findall(r'XELP::OS::(\w+)', recovery)} == {'BigSur', 'Sequoia', 'Tahoe'}
assert 'recoveryOK=hostOK && size>0xDF668' in source
# Failure-only pre-STOP wake and hang detail share the Tahoe/Sequoia scope.
prestop = source[source.index('// Exact failure-only wake call.'):source.index('TGLHangPreStopReady",ok')]
assert tahoe in prestop and sequoia in prestop and 'bool ok=hangOK && size>0x569c6' in prestop
detail = re.search(r'bool tglHangDetailKernel\(\) \{(.*?)\}', source, re.S).group(1)
assert tahoe in detail and sequoia in detail
# Published registry state comes from qualified install results, not device identity.
for path in (repo/'WhateverXe').glob('*.[ch]pp'):
    assert 'Latitude' not in path.read_text(), path.name
