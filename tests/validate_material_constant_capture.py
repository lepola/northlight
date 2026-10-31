#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual timing
"""Compare material constant capture against an immutable full-bank baseline."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import subprocess
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline-ref', required=True)
p.add_argument('--fixture-root', required=True, type=Path, help='Prepared terrain, wall and model-3-weighted4 folders')
p.add_argument('--backend', required=True, type=Path)
p.add_argument('--output', required=True, type=Path)
p.add_argument('--pairs', type=int, default=3)
p.add_argument('--fixture', choices=('terrain', 'wall', 'model-3-weighted4'))
p.add_argument('--msaa', type=int, choices=(0, 2))
p.add_argument('--scenario', choices=('stable', 'pose', 'mutation'))
a = p.parse_args()
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

assert re.fullmatch('[0-9a-f]{40}', a.baseline_ref) and a.pairs >= 2
assert subprocess.check_output(['git', 'rev-parse', a.baseline_ref + '^{commit}'], cwd=fp.REPO, text=True).strip() == a.baseline_ref
assert not a.output.exists() or not any(a.output.iterdir()), 'Use an empty output folder'
a.output.mkdir(parents=True, exist_ok=True)
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
paths = [f for tree in ('src', 'shaders', 'tests') for f in (fp.REPO / tree).rglob('*') if f.is_file() and '__pycache__' not in f.parts]
before = {str(f.relative_to(fp.REPO)): sha(f) for f in paths}
backend = a.backend.resolve(strict=True)
backend_hash = sha(backend)
baseline = a.output / 'baseline'
baseline.mkdir()
for name in ('auto_bump_renderer.h', 'auto_bump_vertex_patch.h'):
    (baseline / name).write_bytes(subprocess.check_output(['git', 'show', a.baseline_ref + ':src/water/' + name], cwd=fp.REPO))
env = {k: v for k, v in fp.zig_env().items() if not k.startswith('NORTHLIGHT_TEST_')}
env['DISABLE_VULKAN_OBS_CAPTURE'] = '1'
env['DXVK_CONFIG'] = 'd3d9.customVendorId = 1002; d3d9.cachedDynamicBuffers = True; d3d9.cachedWriteOnlyBuffers = True; ' + env.get('DXVK_CONFIG', '')
commands = []
executables = {}
for label in ('old', 'new'):
    exe = a.output / (label + '.exe')
    command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17', '-Wno-microsoft-exception-spec', '-DNORTHLIGHT_MATERIAL_MIRROR', '-DNORTHLIGHT_OBLIQUE_WIDTH=320', '-DNORTHLIGHT_OBLIQUE_HEIGHT=200', *(['-DNORTHLIGHT_TEST_CAPTURE_BASELINE', '-I' + str(baseline)] if label == 'old' else []), *fp.test_include_flags(), str(fp.TESTS / 'auto_normal_oblique_gpu.cpp'), '-luser32', '-luuid', '-o', str(exe)]
    commands.append(command)
    subprocess.run(command, env=env, check=True, timeout=240)
    executables[label] = exe
inputs = a.output / 'inputs'
input_hashes = {}
for name in ('terrain', 'wall', 'model-3-weighted4'):
    folder = inputs / name
    folder.mkdir(parents=True)
    for f in (a.fixture_root / name).iterdir():
        if f.name.startswith('source.fgr') or f.name in ('terrain-vs.bin', 'terrain-ps.bin', 'PreviewBase.bin'):
            shutil.copy2(f, folder / f.name)
    for required in ('source.fgr', 'terrain-vs.bin', 'terrain-ps.bin', 'PreviewBase.bin'):
        assert (folder / required).is_file(), (name, required)
input_hashes = {str(f.relative_to(inputs)): sha(f) for f in inputs.rglob('*') if f.is_file()}
records = []
comparisons = 0
for name, family in (('terrain', None), ('wall', 'wall'), ('model-3-weighted4', 'model4')):
    if a.fixture and name != a.fixture:
        continue
    for msaa in ((a.msaa,) if a.msaa is not None else (0, 2)):
        for scenario in ((a.scenario,) if a.scenario else ('stable', 'pose', 'mutation')):
            for pair in range(a.pairs):
                folders = {}
                for label in (('old', 'new') if pair % 2 == 0 else ('new', 'old')):
                    folder = a.output / f'{name}-msaa{msaa}-{scenario}-{pair}-{label}'
                    shutil.copytree(inputs / name, folder)
                    folders[label] = folder
                    runenv = dict(env, NORTHLIGHT_TEST_MSAA=str(msaa), NORTHLIGHT_TEST_DEFERRED_CAPTURE='1', NORTHLIGHT_TEST_TEXTURE_GUARD='1', NORTHLIGHT_TEST_MATERIAL_BATCH_TIMING='1', NORTHLIGHT_TEST_MATERIAL_BATCH_VALIDATE='1', NORTHLIGHT_TEST_CAPTURE_DRAWS='32')
                    if scenario != 'stable':
                        runenv['NORTHLIGHT_TEST_DEFERRED_' + scenario.upper()] = '1'
                    command = [str(executables[label]), str(backend), str(folder) + '/', '12', '.7', *([family] if family else [])]
                    commands.append(command)
                    result = subprocess.run(command, cwd=a.output, env=runenv, capture_output=True, text=True, timeout=120)
                    (folder / 'run.log').write_text(result.stdout + result.stderr)
                    assert result.returncode == 0, (folder, result.stdout[-3000:], result.stderr[-2000:])
                    assert 'MATERIAL_DEFERRED_BENCH' in result.stdout
                    timing = re.search(r'MATERIAL_BATCH_TIMING .*gpuMs=([\d.]+) submitMs=([\d.]+)', result.stdout)
                    counts = re.search(r'MATERIAL_CONSTANT_CAPTURE readRegisters=(\d+) writtenRegisters=(\d+)', result.stdout)
                    auxiliary = re.search(r'readInt=(\d+) readBool=(\d+) writtenInt=(\d+) writtenBool=(\d+)', result.stdout)
                    assert timing and counts
                    record = dict(fixture=name, msaa=msaa, scenario=scenario, pair=pair, label=label, gpu_ms=float(timing[1]), cpu_ms=float(timing[2]), read_registers=int(counts[1]), written_registers=int(counts[2]))
                    if auxiliary:
                        record.update(zip(('read_int', 'read_bool', 'written_int', 'written_bool'), map(int, auxiliary.groups())))
                    records.append(record)
                    print(json.dumps(record), flush=True)
                old = {f.name: f for f in folders['old'].glob('*.fgr') if not f.name.startswith('source.')}
                new = {f.name: f for f in folders['new'].glob('*.fgr') if not f.name.startswith('source.')}
                assert old.keys() == new.keys() and len(old) >= 5
                for key, f in old.items():
                    assert f.read_bytes() == new[key].read_bytes(), (name, msaa, scenario, pair, key)
                    comparisons += 1
assert before == {str(f.relative_to(fp.REPO)): sha(f) for f in paths}
assert backend_hash == sha(backend)
assert input_hashes == {str(f.relative_to(inputs)): sha(f) for f in inputs.rglob('*') if f.is_file()}
summary = {}
for label in ('old', 'new'):
    rows = [r for r in records if r['label'] == label and r['msaa'] == 2]
    summary[label] = {key: statistics.median(r[key] for r in rows) for key in ('cpu_ms', 'gpu_ms', 'read_registers', 'written_registers')}
report = dict(status='pass', baseline_ref=a.baseline_ref, source_hashes=before, inputs=input_hashes, backend=dict(path=str(backend), sha256=backend_hash), compatibility_defaults='Production process-local AMD D3D9 identity and cached buffer defaults; OBS capture layer disabled only for test processes', executables={k: sha(v) for k, v in executables.items()}, commands=commands, comparisons=comparisons, records=records, summary=summary, limitations='Authored 32-draw fixtures with prepared authentic client shaders and textures; no historical native trace, loading, producer work or game-FPS measurement. Submission means cover eight repeated frames after four warmups; GPU timestamps include feed gaps.')
(a.output / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(dict(status='pass', comparisons=comparisons, summary=summary)), flush=True)
