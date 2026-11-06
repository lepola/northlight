#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual timing
"""Exact pre/post terrain coverage images, strict boundaries and separate 4K timings."""
import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--baseline-ref', default='3856b58')
parser.add_argument('--backend', action='append', required=True)
parser.add_argument('--benchmark', action='store_true')
args = parser.parse_args()
out = args.output.resolve()
assert not out.exists() or not any(out.iterdir()), 'Use a fresh output directory'
out.mkdir(parents=True, exist_ok=True)
assert 'wow.exe' not in subprocess.check_output(['tasklist', '/fi', 'IMAGENAME eq wow.exe', '/fo', 'csv', '/nh'], text=True).lower(), 'Close the game before GPU tests'
baseline = subprocess.check_output(['git', 'show', args.baseline_ref+':src/proxy/patch_terrain_shadow_coverage.h'], cwd=ROOT)
(out/'terrain_coverage_before.h').write_bytes(baseline.replace(b'NorthlightTerrainCoverage', b'NorthlightTerrainCoverageBefore'))
inputs = [fp.src('patch_terrain_shadow_coverage.h'), fp.src('patch_terrain_shadow.h'), fp.src('patch_shadow_shader.h'), Path(__file__).with_name('terrain_coverage_gpu.cpp')]
hashes = lambda: {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
before = hashes()
result = {'pass': False, 'source': before, 'baseline_ref': args.baseline_ref, 'baseline_sha256': hashlib.sha256(baseline).hexdigest(), 'game_launched': False, 'checks': [], 'benchmarks': []}
command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG', '-I', str(out), *fp.test_include_flags(), str(inputs[-1]), '-luser32', '-o', str(out/'coverage.exe')]
with (out/'compile.log').open('w') as log:
    subprocess.run(command, cwd=ROOT, env=fp.zig_env(), stdout=log, stderr=subprocess.STDOUT, check=True)
for index, backend in enumerate(args.backend):
    run = subprocess.run([str(out/'coverage.exe'), backend, 'check'], cwd=out, capture_output=True, text=True, timeout=180)
    (out/f'check-{index}.log').write_text(run.stdout+run.stderr)
    assert run.returncode == 0 and 'TERRAIN_COVERAGE_GPU_PASS cases=234 timing=0' in run.stdout, run.stdout+run.stderr
    result['checks'].append({'backend': backend, 'sha256': hashlib.sha256(Path(backend).read_bytes()).hexdigest(), 'cases': 234, 'exact': True, 'reference_sub_normalized': 'REFERENCE_SUB_NORMALIZED' in run.stdout, 'exit_code': run.returncode})
    print(backend, '234 exact images PASS', flush=True)
if args.benchmark:
    for index, backend in enumerate(args.backend):
        run = subprocess.run([str(out/'coverage.exe'), backend, 'benchmark'], cwd=out, capture_output=True, text=True, timeout=240)
        (out/f'benchmark-{index}.log').write_text(run.stdout+run.stderr)
        assert run.returncode == 0 and 'TERRAIN_COVERAGE_GPU_PASS cases=0 timing=1' in run.stdout, run.stdout+run.stderr
        result['benchmarks'].append({'backend': backend, 'output': run.stdout})
        print(backend, 'benchmark PASS', flush=True)
assert hashes() == before, 'Inputs changed during validation'
result['pass'] = True
(out/'validation.json').write_text(json.dumps(result, indent=2)+'\n')
