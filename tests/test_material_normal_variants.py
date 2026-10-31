#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Material variant selection and resource ownership without a GPU or game."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out = fp.output_dir() / 'material-normal-variants'
out.mkdir(parents=True, exist_ok=True)
exe = out / 'variants.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2',
                '-UNDEBUG', '-std=c++17', '-Wno-microsoft-exception-spec',
                *fp.test_include_flags(), str(fp.TESTS / 'shader_opt_material_variants.cpp'),
                '-luser32', '-luuid', '-o', str(exe)], env=fp.zig_env(), check=True)
subprocess.run([str(exe)], check=True)
