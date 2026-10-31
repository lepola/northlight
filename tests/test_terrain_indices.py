#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Boundary, wide-fallback and immutable-reader tests of production terrain indices."""
import ast
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.out()/'terrain-indices'
out.mkdir(parents=True, exist_ok=True)
fixture = ast.parse((fp.TESTS/'test_terrain_snapshot.py').read_text())
strings = {node.targets[0].id: ast.literal_eval(node.value) for node in fixture.body
           if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name)
           and node.targets[0].id in ('stub', 'harness')}
(out/'d3d9.h').write_text(strings['stub'])
(out/'terrain-compact-fixture.h').write_text(strings['harness'].split('int main(){')[0])
for mode, flags in (('release', ['-O2']), ('debug', ['-O0', '-g'])):
    exe = out/(mode+'.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                    '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *flags, '-I', str(out),
                    *fp.test_include_flags(), str(fp.TESTS/'test_terrain_indices.cpp'), '-o', str(exe)],
                   env=fp.zig_env(), check=True)
    subprocess.run([str(exe)], check=True)
    live = out/('live-'+mode+'.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                    '-UNDEBUG', '-DNORTHLIGHT_TERRAIN_COMPACT_TEST', '-Wall', '-Wextra', '-Werror',
                    *flags, *fp.test_include_flags(), str(fp.TESTS/'test_live_terrain_gpu.cpp'), '-o', str(live)],
                   env=fp.zig_env(), check=True)
    subprocess.run([str(live)], check=True)
