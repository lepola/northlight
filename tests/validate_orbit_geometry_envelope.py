#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Reproduce a rejected conservative orbit crop against a private MPQ-derived corpus.

No game, GPU, settings or installed-file access. --fixture is the explicit root
produced by prepare_rotation_fixture.py; missing raw self freshness is retained
as a limitation, while controlled cases independently test the policy.
"""
import argparse
import hashlib
import io
import json
import os
import math
from pathlib import Path
import subprocess
import struct
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fixture', type=Path)
parser.add_argument('--output', type=Path)
parser.add_argument('--sanitizers', action='store_true')
parser.add_argument('--probe-stride', type=int, default=1, help='One tests the complete production probe window')
args = parser.parse_args()
assert 1 <= args.probe_stride <= 5600
out = args.output or fp.out()/'orbit-envelope'
if out.exists() and any(out.iterdir()):
    raise SystemExit('Output must be new or empty; previous experiment evidence must remain immutable')
if args.fixture:
    fixture = args.fixture.resolve()
    manifest = json.loads((fixture/'manifest.json').read_text(encoding='utf-8'))
    if any(manifest[key] for key in ('missing_tiles', 'missing_assets', 'unsupported')) or manifest['mpq_view']['unreadable']:
        raise SystemExit('Incomplete MPQ fixture: resolve missing, unsupported or unreadable inputs before comparison')
    for name, record in manifest['outputs'].items():
        path = fixture/name
        assert path.resolve().is_relative_to(fixture), name
        data = path.read_bytes()
        if len(data) != record['bytes'] or hashlib.sha256(data).hexdigest() != record['sha256']:
            raise SystemExit('MPQ fixture output mismatch: '+name)
out.mkdir(parents=True, exist_ok=True)
baseline = 'fd6b3a4bb7853f36abfd299079b34e37d12c7fad'
frozen, current_sources = {}, {}
archive = subprocess.check_output(['git', 'archive', '--format=zip', baseline, 'src'], cwd=fp.REPO)
baseline_root = out/'baseline-source'
with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
    for entry in zipped.infolist():
        target = baseline_root/entry.filename
        assert target.resolve().is_relative_to(baseline_root.resolve())
        if not entry.is_dir():
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(zipped.read(entry))
for relative in ['src/gi/world_gi.cpp', 'src/gi/world_gi.h', 'src/world/world_mesh_plan.cpp']:
    raw = (baseline_root/relative).read_bytes()
    current_sources[relative] = hashlib.sha256((fp.REPO/relative).read_bytes()).hexdigest()
    frozen[relative] = hashlib.sha256(raw).hexdigest()
    (out/Path(relative).name).write_bytes(raw)

# Only the independent broad reference may exceed the runtime region-count
# limits. Candidate and baseline loads keep the exact original limits.
source = (out/'world_gi.cpp').read_text(encoding='utf-8')
guard = 'result.vertices.size()>4000000-3||result.triangles.size()>=6000000'
assert source.count(guard) == 1
source = 'extern thread_local bool northlightOrbitReferenceLoader;\n'+source.replace(
    guard, 'result.vertices.size()>(northlightOrbitReferenceLoader?16000000u:4000000u)-3||result.triangles.size()>=(northlightOrbitReferenceLoader?24000000u:6000000u)')
(out/'world_gi.cpp').write_text(source, encoding='utf-8')

exe = out/('orbit-envelope.exe' if os.name == 'nt' else 'orbit-envelope')
if os.name == 'nt':
    assert not args.sanitizers, 'Actual sanitizer checks require the native compiler'
    compiler = [str(fp.zig()), 'c++', '-target', 'x86_64-windows-gnu']
    env = fp.zig_env()
else:
    compiler, env = ['c++'], None
flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitizers else ['-O2']
includes = [flag for folder in sorted((baseline_root/'src').iterdir()) if folder.is_dir() for flag in ['-I', str(folder)]]
command = [*compiler, '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *flags,
           *includes, *fp.test_include_flags(), str(fp.TESTS/'test_orbit_geometry_envelope.cpp'),
           str(out/'world_gi.cpp'), str(out/'world_mesh_plan.cpp'), '-o', str(exe)]
subprocess.run(command, env=env, check=True)
run = [str(exe)]
fixture_evidence = None
if args.fixture:
    fixture = args.fixture.resolve()
    assert (fixture/'world-cache/models').is_dir() and (fixture/'world-cache/Northrend').is_dir()
    cameras = json.loads((fixture/'camera.json').read_text(encoding='utf-8'))
    center = cameras[0]['pivot']
    zero, tile_size = 17066.666666666666, 1600/3
    x0, x1 = (math.floor((zero-(center[1]+offset))/tile_size) for offset in [1280, -1280])
    y0, y1 = (math.floor((zero-(center[0]+offset))/tile_size) for offset in [1280, -1280])
    referenced = set()
    for y in range(y0, y1+1):
        for x in range(x0, x1+1):
            tile = fixture/'world-cache/Northrend'/f'{x}_{y}.fg3'
            assert tile.exists(), f'Missing oracle tile {tile}'
            data = tile.read_bytes()
            magic, version, count = struct.unpack_from('<4sII', data)
            assert magic == b'FGS3' and version == 3 and len(data) == 12+116*count
            referenced.update(data[offset:offset+32].hex() for offset in range(12, len(data), 116))
    for model in referenced:
        assert (fixture/'world-cache/models'/(model+'.fgs')).is_file(), model
    fixture_evidence = {'manifest_sha256': hashlib.sha256((fixture/'manifest.json').read_bytes()).hexdigest(),
                        'camera_sha256': hashlib.sha256((fixture/'camera.bin').read_bytes()).hexdigest(),
                        'complete_oracle_tiles': (x1-x0+1)*(y1-y0+1),
                        'referenced_models_present': len(referenced),
                        'unsupported_assets': len(manifest['unsupported']), 'missing_assets': len(manifest['missing_assets']),
                        'limitations': manifest['limitations']}
    run += [str(fixture/'world-cache'), str(fixture/'camera.bin'), str(args.probe_stride)]
chunks = []
with subprocess.Popen(run, stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as process:
    for line in process.stdout:
        chunks.append(line)
        print(line.decode('utf-8', errors='replace'), end='', flush=True)
    code = process.wait()
raw = b''.join(chunks)
(out/'raw.log').write_bytes(raw)
assert code == 0, code
report = {'baseline_ref': baseline, 'frozen_source_sha256': frozen,
          'current_source_sha256': current_sources,
          'sanitizers': args.sanitizers, 'mechanics_pass': True, 'fixture': str(args.fixture) if args.fixture else None,
          'raw_self_freshness': 'not captured; controlled policy unit cases only',
          'reference_only_loader_count_override': True,
          'fixture_evidence': fixture_evidence,
          'probe_stride': args.probe_stride,
          'reference_override_source_sha256': hashlib.sha256((out/'world_gi.cpp').read_bytes()).hexdigest(),
          'runtime_wired': False, 'accepted_for_runtime': False,
          'known_rejection': 'The full 78400-probe oracle reports 24 worse SH probes; do not wire this prototype',
          'game_launched': False, 'gpu_used': False,
          'command': command, 'rows': [line for line in raw.decode('utf-8').splitlines() if line.startswith('ORBIT_')]}
for line in report['rows']:
    if line.startswith('ORBIT_BAKE '):
        fields = dict(field.split('=', 1) for field in line.split()[1:])
        report['quality_gate_pass'] = (int(fields['regressedSH']) == 0 and int(fields['regressedVisibility']) == 0
                                       and fields['pathChanged'] == fields['pathExactReference'])
report['source_sha256'] = {name: hashlib.sha256((fp.REPO/name).read_bytes()).hexdigest() for name in [
    'tests/test_orbit_geometry_envelope.cpp', 'tests/validate_orbit_geometry_envelope.py',
    'tests/fixtures/reference/rejected_orbit_envelope-fd6b3a4.h', 'src/world/world_streaming.h']}
(out/'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
