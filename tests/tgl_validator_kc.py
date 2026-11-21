#!/usr/bin/env python3
"""Replay inspected standalone and KC callback bytes; never read live kernel memory."""
import json, pathlib, subprocess, tempfile
r=pathlib.Path(__file__).resolve().parents[1]
f=json.loads((r/'tests/fixtures/tgl_validator_layouts.json').read_text())
a,b=bytes.fromhex(f['kernel']),bytes.fromhex(f['boot_kc'])
assert len(a)==len(b)==308
changed={i for i in range(308) if a[i]!=b[i]}
assert changed=={0x43,0x44,0x45,0x50,0x51,0x52,0x5d,0x5e,0x5f,0x84,0x85,0x86}
# All four exact relinks; no code/branch/call/ABI difference allowed.
for i in [0x42,0x4f,0x5c,0x83]:
 assert int.from_bytes(b[i:i+4],'little',signed=True)-int.from_bytes(a[i:i+4],'little',signed=True)==-0xdb2000
with tempfile.TemporaryDirectory() as td:
 t=pathlib.Path(td);(t/'kc').write_bytes(b);(t/'kernel').write_bytes(a)
 subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',str(r/'tests/tgl_validator_kc_fixture.cpp'),'-o',str(t/'check')],check=True)
 subprocess.run([str(t/'check'),str(t/'kc'),str(t/'kernel')],check=True)
