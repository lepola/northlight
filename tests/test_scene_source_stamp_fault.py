#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""GI source stamp allocation fallback, lease recovery and missing-file controls."""
import subprocess
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix='scene-source-') as folder:
    exe = Path(folder)/'test.exe'
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                    '-Wall', '-Wextra', '-Werror', *fp.test_include_flags(),
                    str(fp.TESTS/'test_scene_source_stamp_fault.cpp'), str(fp.src('world_gi.cpp')),
                    '-o', str(exe)], env=fp.zig_env(), check=True)
    subprocess.run([str(exe), str(Path(folder)/'data')], check=True)
