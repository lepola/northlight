#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Production receiver oracle: shadow floor preserves both current local colors."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser = argparse.ArgumentParser()
parser.add_argument('--legacy-counterexample', type=Path, metavar='OLD_SHADER_DIRECTORY',
                    help='Execute the original combined-buffer pipeline using immutable old production bins.')
args = parser.parse_args()
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
backend = os.environ.get('NORTHLIGHT_TEST_D3D9', str(fp.client_root() / 'renderer-backends/dxvk2/dxvk2_d3d9.dll'))
exe = out / 'local-overlap.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG', '-Wno-return-type',
                *fp.include_flags(), str(fp.TESTS / 'local_overlap_gpu.cpp'), '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
compiled = args.legacy_counterexample or fp.COMPILED
entries = ['WorldLighting', 'RemovalSmooth', 'TemporalLight', 'LocalDirect', 'LocalDirect16', 'WorldComposite']
identity = {name: hashlib.sha256((compiled / (name + '.bin')).read_bytes()).hexdigest() for name in entries}
if args.legacy_counterexample:
    # Frozen terrain-frame-candidate-1/old-shaders production bytecode.
    expected = dict(zip(entries, [
        '40ae49f7447ad9ccf04d7940919c16d45bc51a8a9ef76c4b21c7cb0e4fa21f75',
        '03dc46f9c27e90c304c68cccabde3059cd8c107d712743e76dc64170325b43a8',
        '9392d2a1566c6f88d7a4e613fa67ff4fd5b4c58e1cd9edef0850637171b45574',
        'c2ef6f898580dc7d961bd23d3e2c191eef724f6d7dfe54e01ebccd4f9102b77b',
        '46ceb575a4c9f6e471c555012818b310220d78783f9bd8bb4a39e04256eebee4',
        '433d6bc476a2516ce93060b30bb14a673f340605d0abc06d8944ac75fd481cae']))
    assert identity == expected, 'Legacy counterexample requires the immutable original production bytecode.'
mode = ['--legacy-counterexample'] if args.legacy_counterexample else []
result = subprocess.run([str(exe), str(compiled), backend, *mode], cwd=out, timeout=90, text=True, capture_output=True)
(out / ('legacy.log' if mode else 'fixed.log')).write_text(result.stdout + result.stderr)
print(result.stdout, end='')
if result.stderr:
    print(result.stderr, file=sys.stderr, end='')
assert result.returncode == 0, f'GPU receiver oracle failed: {result.returncode}'
report = {'legacy_counterexample': bool(mode), 'shader_directory': str(compiled.resolve()), 'shader_sha256': identity,
          'stdout': result.stdout, 'actual_item_geometry': False, 'msaa_silhouettes': False}
(out / ('local-overlap-legacy.json' if mode else 'local-overlap.json')).write_text(json.dumps(report, indent=2) + '\n')
