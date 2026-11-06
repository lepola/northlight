#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Compile and run focused frozen fog comparisons with x86 Zig."""
import subprocess
import sys
from pathlib import Path
root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root))
import northlight_paths as fp

output = fp.out() / 'fog-optimization-proofs'
output.mkdir(parents=True, exist_ok=True)
tests = ('test_fog_tile_io', 'test_fog_zone_boundaries', 'test_fog_water_reference', 'test_fog_city_alpha', 'test_fog_field_reference', 'test_fog_height_finite', 'test_fog_field_sample', 'test_fog_noise_reference', 'test_fog_cloud_sample', 'test_fog_quantile_samples', 'test_fog_wind_reference', 'test_fog_perlin_cubes', 'test_fog_worley_reference', 'test_fog_fit_reference', 'test_fog_hold_reference', 'test_spell_fog_pair')
for name in tests:
    binary = output / (name + '.exe')
    cmd = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG', *fp.test_include_flags(), str(root / 'tests' / (name + '.cpp')), '-o', str(binary)]
    subprocess.run(cmd, env=fp.zig_env(), check=True, cwd=root)
    subprocess.run([str(binary), *([str(output / 'tile-io.frf')] if name == 'test_fog_tile_io' else [])], check=True, cwd=root)
    print(name, 'PASS', flush=True)
