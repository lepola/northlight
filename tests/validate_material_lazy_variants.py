#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client,stormlib manual timing
"""Frozen-source D3D9 A/B for demand-created displacement shaders; never starts WoW."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import statistics
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from world_scene_builder import Assets
from extract_signatures import variants

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backend', required=True, type=Path)
parser.add_argument('--baseline-ref', required=True)
parser.add_argument('--output', required=True, type=Path)
parser.add_argument('--quick', action='store_true', help='Four representative pixel fixtures; all MSAA modes and lifecycle controls remain enabled')
parser.add_argument('--pairs', type=int, default=3)
args = parser.parse_args()
out = args.output.resolve()
assert not out.exists() or not any(out.iterdir()), 'Use a new, empty output directory'
assert args.pairs >= 3
out.mkdir(parents=True, exist_ok=True)
backend = args.backend.resolve(strict=True)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def source_hashes():
    files = [path for directory in ('src', 'shaders', 'scripts') for path in (fp.REPO / directory).rglob('*') if path.is_file() and path.suffix in ('.h', '.inl', '.cpp', '.hlsl', '.py', '.bin', '.json')]
    files += [fp.TESTS / name for name in ('material_lazy_variants_gpu.cpp', 'auto_normal_oblique_gpu.cpp', 'composite_distance_gpu.cpp', Path(__file__).name)]
    return {str(path.relative_to(fp.REPO)): sha(path) for path in sorted(files)}

before = source_hashes()
inputs = out / 'inputs'
inputs.mkdir()
archives = []
assets = Assets(archives='stock')

def extract(path, index, destination):
    data = assets.read(path)
    code = {number: code for number, _, code in variants(data)}[index]
    destination.write_bytes(code)
    archives.append(dict(path=path, index=index, archive_payload_sha256=hashlib.sha256(data).hexdigest(), output=str(destination.relative_to(out)), sha256=sha(destination)))
    return code

try:
    registration = inputs / 'registration'
    registration.mkdir()
    path = 'shaders\\vertex\\vs_3_0\\diffuse_t1.bls'
    data = assets.read(path)
    programs = list(variants(data))[:21]
    assert len(programs) == 21
    index = []
    for number, _, code in programs:
        file = registration / f'vs-{number}.bin'
        file.write_bytes(code)
        archives.append(dict(path=path, index=number, archive_payload_sha256=hashlib.sha256(data).hexdigest(), output=str(file.relative_to(out)), sha256=sha(file)))
        index.append(f'{file.name} 3\n')
    (registration / 'index.txt').write_bytes(''.join(index).encode())
    fixtures = []
    model_cases = [('3', 'weighted4', 51), ('2', 'weighted4', 51)] if args.quick else [(model, skin, number) for model in ('2', '3') for skin, number in [('rigid', 1), ('skinned', 11), ('weighted', 21), ('weighted4', 51)]]
    cases = [('model-' + model + '-' + skin, f'shaders\\vertex\\vs_{model}_0\\diffuse_t1.bls', number, 'shaders\\pixel\\ps_3_0\\combiners_opaque.bls', 'model4' if skin == 'weighted4' else 'model') for model, skin, number in model_cases]
    cases += [('terrain', 'shaders\\vertex\\vs_3_0\\terrain.bls', 0, 'shaders\\pixel\\ps_3_0\\terrain1.bls', None), ('wall', 'shaders\\vertex\\vs_3_0\\mapobjdiffuse_t1.bls', 0, 'shaders\\pixel\\ps_3_0\\mapobjdiffuse.bls', 'wall')]
    for name, vertex, number, pixel, family in cases:
        folder = inputs / name
        folder.mkdir()
        for file in (fp.out() / 'oblique-cobble').iterdir():
            if file.name.startswith('source.fgr') or file.name == 'PreviewBase.bin':
                shutil.copy2(file, folder / file.name)
        assert (folder / 'source.fgr').is_file() and (folder / 'PreviewBase.bin').is_file()
        extract(vertex, number, folder / 'terrain-vs.bin')
        extract(pixel, 0, folder / 'terrain-ps.bin')
        fixtures.append((name, folder, family))
finally:
    assets.close()

input_hashes = {str(path.relative_to(out)): sha(path) for path in sorted(inputs.rglob('*')) if path.is_file()}
override = out / 'baseline'
override.mkdir()
for name in ('auto_bump_renderer.h', 'auto_displacement_renderer.h'):
    data = subprocess.check_output(['git', 'show', args.baseline_ref + ':src/water/' + name], cwd=fp.REPO)
    (override / name).write_bytes(data)
env = fp.zig_env()
temporary = out / 'tmp'
temporary.mkdir()
env.update(TEMP=str(temporary), TMP=str(temporary))
commands = []
executables = {}
for label in ('old', 'new'):
    exe = out / (label + '.exe')
    command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17', '-Wno-microsoft-exception-spec', '-DNORTHLIGHT_MATERIAL_MIRROR', '-DNORTHLIGHT_OBLIQUE_WIDTH=320', '-DNORTHLIGHT_OBLIQUE_HEIGHT=200', *(['-I' + str(override)] if label == 'old' else ['-DNORTHLIGHT_TEST_LAZY_VARIANT_CURRENT']), *fp.test_include_flags(), str(fp.TESTS / 'material_lazy_variants_gpu.cpp'), '-luser32', '-luuid', '-o', str(exe)]
    commands.append(command)
    subprocess.run(command, env=env, check=True)
    executables[label] = exe

def run(label, arguments, name, flags=None):
    clean = {key: value for key, value in env.items() if not key.startswith('NORTHLIGHT_TEST_') and key != 'NORTHLIGHT_EXPERIMENTAL_SHARED_POM'}
    clean['NORTHLIGHT_TEST_VARIANT_AUDIT'] = '1'
    clean.update(flags or {})
    command = [str(executables[label]), str(backend), *map(str, arguments)]
    commands.append(command)
    result = subprocess.run(command, cwd=out, env=clean, capture_output=True, text=True, timeout=120)
    (out / (name + '-' + label + '.log')).write_bytes((result.stdout + result.stderr).encode())
    assert result.returncode == 0, (name, label, result.stdout[-3500:], result.stderr[-1500:])
    return result.stdout

records = []
life = run('new', ['--lifecycle', fixtures[0][1] / 'terrain-vs.bin'], 'lifecycle')
assert 'MATERIAL_VARIANT_LIFECYCLE_PASS' in life
for name, source, family in fixtures:
    for msaa in (0, 2, 4, 8):
        pair = []
        logs = []
        for label in ('old', 'new'):
            folder = out / f'{name}-msaa{msaa}-{label}'
            shutil.copytree(source, folder)
            arguments = [str(folder) + '/', '12', '.7', *([family] if family else [])]
            logs.append(run(label, arguments, f'{name}-msaa{msaa}', {'NORTHLIGHT_TEST_MSAA': str(msaa)}))
            pair.append(folder)
        files = sorted(pair[0].glob('*.fgr'))
        assert len(files) >= 12, (name, msaa, 'Incomplete material output')
        for file in files:
            assert file.read_bytes() == (pair[1] / file.name).read_bytes(), (name, msaa, file.name, 'Pixel mismatch')
        creates = [int(re.search(r'MATERIAL_VARIANT_AUDIT stage=registration creates=(\d+)', log)[1]) for log in logs]
        assert creates[1] == 1 and creates[0] > creates[1], (name, creates)
        assert 'MATERIAL_VARIANT_AUDIT stage=initial-normal creates=0' in logs[1]
        if family and family.startswith('model'):
            assert 'DISPLACEMENT_PREPARED mode=9' in logs[1]
        for mode in (1, 2, 3, 4):
            assert re.search(rf'MATERIAL_VARIANT_AUDIT stage=mode mode={mode} creates=0 ', logs[1]), (name, msaa, mode)
        records.append(dict(fixture=name, msaa=msaa, bit_exact_outputs=len(files), registration_creates=creates, first_use_scopes_logged=True, bindings_asserted=True))

# A far -> near transition tests actual admission, not just direct ensure().
name, source, family = next(item for item in fixtures if item[0] == 'model-3-weighted4')
folder = out / 'far-near'
shutil.copytree(source, folder)
far = run('new', [str(folder) + '/', '12', '.7', 'model4'], 'far-near', {'NORTHLIGHT_TEST_MODEL_RANGE': '1'})
assert 'MATERIAL_VARIANT_AUDIT stage=far creates=0' in far and 'MODEL_RANGE_PASS' in far

timing = []
for pair in range(args.pairs):
    for label in (('old', 'new') if pair % 2 == 0 else ('new', 'old')):
        log = run(label, ['--registration', str(registration) + '/'], 'registration-' + str(pair))
        rows = re.findall(r'MATERIAL_VARIANT_REGISTRATION mode=(\d+) sources=(\d+) creates=(\d+) cpuMs=([\d.]+)', log)
        assert len(rows) == 2
        for mode, count, creates, milliseconds in rows:
            assert int(count) == 21
            if label == 'new':
                assert int(creates) == 21
            else:
                assert int(creates) > 21
            timing.append(dict(pair=pair, label=label, mode=int(mode), creates=int(creates), cpu_ms=float(milliseconds)))
after = source_hashes()
assert before == after, 'Sources changed during the A/B'
assert input_hashes == {str(path.relative_to(out)): sha(path) for path in sorted(inputs.rglob('*')) if path.is_file()}, 'Inputs changed during the A/B'
report = dict(status='pass', baseline_ref=args.baseline_ref, source_before=before, source_after=after,
    backend=dict(path=str(backend), sha256=sha(backend)), compiler=dict(path=str(fp.zig()), sha256=sha(fp.zig()), version=subprocess.check_output([str(fp.zig()), 'version'], text=True).strip()),
    executables={label: sha(path) for label, path in executables.items()}, baseline_headers={path.name: sha(path) for path in override.iterdir()},
    commands=commands, inputs=input_hashes, mpq_programs=archives, pixels=records, registration_timings=timing,
    lifecycle='Pending COM source, reset, toggles, repeated ensure, read/create failure, re-registration recovery, deletion and unchanged constants/binding asserted',
    limitations='Synthetic geometry, authored constant palettes and copied source texture/helper shader; MPQ-authentic vertex/pixel programs. Registration uses21 actual programs, not a recorded live sequence. Firstnear variant creation remains synchronous and is logged; registration timing is not FPS. No game launch.')
for mode in (1, 3):
    medians = {label: statistics.median(row['cpu_ms'] for row in timing if row['label'] == label and row['mode'] == mode) for label in ('old', 'new')}
    report.setdefault('registration_medians', {})[mode] = medians
(out / 'validation.json').write_bytes((json.dumps(report, indent=2) + '\n').encode())
print(json.dumps(dict(status='pass', pixels=len(records), registration_medians=report['registration_medians']), indent=2))
