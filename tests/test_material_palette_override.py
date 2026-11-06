#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Exact local weighted-skin envelope and shader DEF palette regression."""
import subprocess
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='material-palette-override-') as tmp:
    root = Path(tmp)
    fixture = (fp.TESTS / 'displacement_state_reuse.cpp').read_text().split('struct Backend:ForwardIDirect3DDevice9')[0]
    fixture = fixture.replace('struct DisplacementStateTestAccess {', 'struct DisplacementStateTestAccess {\n static void paletteProof();')
    for name in ('auto_bump_renderer.h', 'device_mirror.h', 'mirror_resource_forwarders.h'):
        fixture = fixture.replace('#include "' + name + '"\n', '')
    (root / 'palette_fixture.h').write_text(fixture)
    exe = root / 'palette.exe'
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
                    '-Wno-microsoft-exception-spec', '-Wno-return-type', '-I', str(root), *fp.test_include_flags(),
                    str(fp.TESTS / 'material_palette_override.cpp'), '-luser32', '-luuid', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
