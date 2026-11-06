#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Exact actor scene/BVH output and allocation-free exclusive-owner handoff."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
for opt in ['2', '0']:
    exe = out / ('actor-scene-transfer-O' + opt + '.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O' + opt,
                    '-UNDEBUG', '-std=c++17', *fp.test_include_flags(),
                    str(fp.TESTS / 'actor_scene_transfer.cpp'), str(fp.src('world_gi.cpp')),
                    '-o', str(exe)], env=fp.zig_env(), check=True)
    result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
    (out / ('O' + opt + '.log')).write_text(result.stdout)
    print(result.stdout, end='')
