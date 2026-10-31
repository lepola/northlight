#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Focused material alias equivalence, ownership and timed comparison workload."""
import subprocess
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='material-alias-wave2-') as tmp:
    root = Path(tmp)
    fixture = (fp.TESTS / 'displacement_state_reuse.cpp').read_text().split('struct Backend:ForwardIDirect3DDevice9')[0]
    fixture = fixture.replace('struct DisplacementStateTestAccess {', 'struct DisplacementStateTestAccess {\n static void aliasPerf();')
    for name in ('auto_bump_renderer.h', 'device_mirror.h', 'mirror_resource_forwarders.h'):
        fixture = fixture.replace('#include "' + name + '"\n', '')
    (root / 'alias_fixture.h').write_text(fixture)
    exe = root / 'alias.exe'
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
                    '-Wno-microsoft-exception-spec', '-Wno-return-type', '-I', str(root), *fp.test_include_flags(),
                    str(fp.TESTS / 'material_alias_wave2.cpp'), '-luser32', '-luuid', '-o', str(exe)], check=True, creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
    subprocess.run([str(exe)], check=True, creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
