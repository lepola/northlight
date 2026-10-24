#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Exact static-shadow worker selection, portable CPU fixture; never starts the game."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.out()/'static-shadow-selection'
out.mkdir(parents=True, exist_ok=True)
for flags, name in ((['-O2'], 'release'), (['-O0', '-g'], 'debug')):
    exe = out/(name+'.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                    '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *flags, *fp.test_include_flags(),
                    str(fp.TESTS/'test_static_shadow_selection.cpp'), str(fp.src('world_gi.cpp')),
                    '-o', str(exe)], env=fp.zig_env(), check=True)
    subprocess.run([str(exe)], check=True)
