#!/usr/bin/env python3
# northlight-test: requires=zig
"""Scene-owned light lifetime and command-stream/archive value transport."""
from pathlib import Path
import importlib.util
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

spec = importlib.util.spec_from_file_location('prepare_native_scene', fp.SCRIPTS / 'prepare_native_scene.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)
with tempfile.TemporaryDirectory(prefix='native-light-snapshot-') as tmp:
    for opt in ('-O0', '-O2'):
        exe = Path(tmp) / ('test.exe' if sys.platform == 'win32' else 'test')
        archive = Path(tmp) / 'lights.nlsnap'
        target = ['-target', 'x86-windows-gnu'] if sys.platform == 'win32' else []
        subprocess.run([str(fp.zig()), 'c++', *target, '-std=c++17', opt, '-UNDEBUG',
                        '-Wall', '-Wextra', '-Werror', *fp.test_include_flags(),
                        str(fp.TESTS / 'native_light_snapshot.cpp'), '-o', str(exe)], env=fp.zig_env(), check=True)
        subprocess.run([str(exe), str(archive)], check=True)
        assert prepare.inspect_snapshot(archive)['draw_ordinal'] == 123
        assert prepare.inspect_snapshot(Path(str(archive)+'.fixture'))['draw_ordinal'] == 123
print('PASS: scene light publication and snapshot import')
