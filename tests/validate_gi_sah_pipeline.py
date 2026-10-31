#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,world-cache slow
"""Compare the full MPQ GI pipeline with a frozen source, without the game.

Missing, stale or changed inputs fail. Outputs, ray hits and the 5,600-probe
bake must be exact; timings include all preparation stages and the bake.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import statistics
import struct
import subprocess
import sys
import zipfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def verify_fixture(root):
    manifest = root / 'manifest.json'
    data = json.loads(manifest.read_text())
    assert data['format'] == 1 and data['map'] == 'Northrend'
    for key in ['missing_tiles', 'unsupported', 'missing_assets']:
        assert data[key] == [], (key, data[key])
    for key in ['unreadable', 'unlisted']:
        assert data['mpq_view'][key] == [], (key, data['mpq_view'][key])
    assert data['outputs'] and data['tiles']
    actual = {path.relative_to(root).as_posix() for path in root.rglob('*') if path.is_file() and path != manifest}
    assert actual == set(data['outputs']), 'Missing or unmanifested fixture files'
    for relative, expected in data['outputs'].items():
        path = (root / relative).resolve()
        assert path.is_relative_to(root), 'Fixture path escapes root'
        assert path.stat().st_size == expected['bytes'] and digest(path) == expected['sha256'], relative
    for tile in (root / 'world-cache/Northrend').glob('*.fg3'):
        raw = tile.read_bytes()
        assert len(raw) >= 12
        magic, version, count = struct.unpack_from('<4sII', raw)
        assert magic == b'FGS3' and version == 3 and len(raw) == 12 + 116 * count
        for offset in range(12, len(raw), 116):
            model = root / 'world-cache/models' / (raw[offset:offset + 32].hex() + '.fgs')
            assert model.is_file(), ('Missing referenced model', model)
    return {'manifest_sha256': digest(manifest), 'outputs': data['outputs'], 'limitations': data['limitations']}


def sources(root):
    return {str(path.resolve()): digest(path) for path in (root / 'src').rglob('*') if path.is_file()}


def dependencies(path):
    rule = re.split(r':\s+', path.read_text().replace('\\\n', ''), maxsplit=1)
    assert len(rule) == 2, ('Malformed depfile', path)
    return {str(Path(re.sub(r'\\([ :#\\])', r'\1', value)).resolve())
            for value in re.findall(r'(?:\\.|[^\s])+', rule[1])}


p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--fixture', type=Path, required=True)
p.add_argument('--camera-log', type=Path, required=True)
p.add_argument('--baseline-ref', default='9be300a')
p.add_argument('--output', type=Path, required=True)
p.add_argument('--rounds', type=int, default=3)
p.add_argument('--check-inputs-only', action='store_true')
args = p.parse_args()
assert 1 <= args.rounds <= 5
out = args.output.resolve()
assert not out.exists() or (out.is_dir() and not any(out.iterdir())), 'Output must be absent or empty'
out.mkdir(parents=True, exist_ok=True)
fixture = args.fixture.resolve()
fixture_before = verify_fixture(fixture)
log_hash = digest(args.camera_log)
baseline = out / 'baseline-source'
ref = subprocess.check_output(['git', 'rev-parse', args.baseline_ref], cwd=fp.REPO, text=True).strip()
archive = subprocess.check_output(['git', 'archive', '--format=zip', ref, 'src'], cwd=fp.REPO)
with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
    for entry in zipped.infolist():
        target = (baseline / entry.filename).resolve()
        assert target.is_relative_to(baseline)
        if not entry.is_dir():
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(zipped.read(entry))

eyes = []
for line in args.camera_log.read_text().splitlines():
    if line.startswith('WORLD camera '):
        eye = [float(value) for value in re.search(r'eye=\(([^)]+)', line)[1].split()]
        if 5000 < eye[0] < 6000 and 300 < eye[2] < 600 and (not eyes or eyes[-1] != eye):
            eyes.append(eye)
assert len(eyes) >= 7
selected = [eyes[0], eyes[0], *[eyes[(i + 1) * (len(eyes) - 1) // 5] for i in range(5)]]
import math
for x, y, _ in selected:
    tx, ty = math.floor((17066.6666667 - y) / 533.3333333), math.floor((17066.6666667 - x) / 533.3333333)
    for tile_y in range(ty - 2, ty + 3):
        for tile_x in range(tx - 2, tx + 3):
            assert (fixture / f'world-cache/Northrend/{tile_x}_{tile_y}.fg3').is_file(), ('Missing selected tile', tile_x, tile_y)

driver = (fp.TESTS / 'validate_shared_scene_pipeline.cpp').read_text()
driver = driver.replace('#include "world_gi.h"', '''#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#define private public
#include "world_gi.h"
#undef private''')
driver = driver.replace('int main(int argc,char** argv){', '''static void tree(Hash& h,const BVH& b){
    h.word(b.root_);h.bytes(&b.rootLow_,sizeof b.rootLow_);h.bytes(&b.rootHigh_,sizeof b.rootHigh_);
    h.word(b.order_.size());h.bytes(b.order_.data(),b.order_.size()*sizeof(uint32_t));
    h.word(b.pairs_.size());h.bytes(b.pairs_.data(),b.pairs_.size()*sizeof(BVH::Pair));
    h.word(b.regionRoot_);h.bytes(&b.regionLow_,sizeof b.regionLow_);h.bytes(&b.regionHigh_,sizeof b.regionHigh_);
    h.word(b.regionOrder_.size());h.bytes(b.regionOrder_.data(),b.regionOrder_.size()*sizeof(uint32_t));
    h.word(b.regionPairs_.size());h.bytes(b.regionPairs_.data(),b.regionPairs_.size()*sizeof(BVH::Pair));
    for(const auto& p:b.pieces_)tree(h,*p.geometry);
}
int main(int argc,char** argv){''')
assert driver.count('const Vec3 c=origin+moves[step];') == 1
driver = driver.replace('const Vec3 c=origin+moves[step];',
    'const Vec3 captured[]={' + ','.join('{' + ','.join(format(v, '.9g') for v in eye) + '}' for eye in selected) + '};const Vec3 c=captured[step];')
driver = driver.replace('const Vec3 origin=', '[[maybe_unused]] const Vec3 origin=').replace('const Vec3 moves[]=', '[[maybe_unused]] const Vec3 moves[]=')
driver = driver.replace(' sourceHits=%llu', ' bvhMs=%.6f fallbackTriangles=%llu sourceHits=%llu')
driver = driver.replace('(unsigned long long)s.legacyFallbacks,(unsigned long long)sources.stats().hits',
                        '(unsigned long long)s.legacyFallbacks,s.bvhMs,(unsigned long long)s.fallbackTriangles,(unsigned long long)sources.stats().hits')
driver = driver.replace('        std::puts("");', '        Hash treeHash;tree(treeHash,local);printHash("treeHash",treeHash);std::puts("");')
assert driver.count(' bvhMs=%.6f fallbackTriangles=%llu') == 1 and driver.count('printHash("treeHash"') == 1
driver_path = out / 'pipeline.cpp'
driver_path.write_bytes(driver.encode())
source_before = {**sources(fp.REPO), **sources(baseline), str(driver_path): digest(driver_path)}
if args.check_inputs_only:
    (out / 'input-check.json').write_text(json.dumps({'fixture': fixture_before, 'log_sha256': log_hash,
        'baseline': ref, 'selected_cameras': selected, 'compiler_invoked': False}, indent=2) + '\n')
    print('PASS complete hashed MPQ inputs and selected camera coverage; no compiler invoked')
    sys.exit(0)

commands = []


def run(command, **kwargs):
    commands.append([str(word) for word in command])
    (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    return subprocess.run(command, check=True, **kwargs)


zig = fp.zig().resolve()
compiler = {'path': str(zig), 'sha256': digest(zig), 'version': subprocess.check_output([str(zig), 'version'], text=True).strip()}
env = fp.zig_env()
for key, folder in [('TEMP', 'tmp'), ('TMP', 'tmp'), ('ZIG_LOCAL_CACHE_DIR', 'zig-local'), ('ZIG_GLOBAL_CACHE_DIR', 'zig-global')]:
    (out / folder).mkdir(exist_ok=True)
    env[key] = str(out / folder)
executables, depfiles = {}, []
for name, root in [('baseline', baseline), ('current', fp.REPO)]:
    includes = [flag for folder in sorted((root / 'src').iterdir()) if folder.is_dir() for flag in ['-I', str(folder)]]
    flags = ['-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
             '-DSHARED_SCENE_PIPELINE=1', '-DCURRENT_TERRAIN_CHUNKS=1', '-DPIPELINE_RADIUS=928', *includes]
    objects = []
    for number, source in enumerate([driver_path, root / 'src/gi/world_gi.cpp', root / 'src/world/world_mesh_plan.cpp']):
        obj, dep = out / f'{name}-{number}.o', out / f'{name}-{number}.d'
        run([str(zig), 'c++', *flags, '-MMD', '-MF', str(dep), '-c', str(source), '-o', str(obj)], env=env)
        objects.append(str(obj));depfiles.append(dep)
    executable = out / (name + '.exe')
    run([str(zig), 'c++', '-target', 'x86-windows-gnu', *objects, '-o', str(executable)], env=env)
    executables[name] = executable

results = {'baseline': [], 'current': []}
for round_number in range(args.rounds):
    for name in (['baseline', 'current'] if round_number % 2 == 0 else ['current', 'baseline']):
        result = run([str(executables[name]), str(fixture / 'world-cache'), 'Northrend', 'bake'], text=True, capture_output=True)
        (out / f'{name}-{round_number}.log').write_text(result.stdout)
        rows = [dict(part.split('=', 1) for part in line.split()[1:]) for line in result.stdout.splitlines() if line.startswith('PIPELINE ')]
        assert len(rows) == 7, result.stdout
        results[name].append(rows)
        print(name, round_number, 'prepareAndBakeMs', sum(float(row['prepareAndBakeMs']) for row in rows), flush=True)

checks = ['scene', 'plan', 'pagesHash', 'rays', 'bakeHash', 'localTriangles', 'planTriangles', 'pages', 'batches', 'treeHash']
for round_number in range(args.rounds):
    for step in range(7):
        old, new = (results[name][round_number][step] for name in ['baseline', 'current'])
        for key in checks:
            assert old[key] == new[key], (round_number, step, key, old[key], new[key])
assert verify_fixture(fixture) == fixture_before and digest(args.camera_log) == log_hash, 'Inputs changed during run'
used = set().union(*(dependencies(path) for path in depfiles))
verified = {path: value for path, value in source_before.items() if path in used}
assert str(fp.src('world_gi.cpp').resolve()) in verified and str(driver_path) in verified
assert all(digest(Path(path)) == value for path, value in verified.items()), 'Compiled source changed during run'
timings = {name: {key: statistics.median(sum(float(row[key]) for row in rows) for rows in rounds)
    for key in ['localMs', 'bvhMs', 'terrainMs', 'planMs', 'pagesMs', 'totalMs', 'traceMs', 'bakeMs', 'prepareAndBakeMs']}
    for name, rounds in results.items()}
report = {'baseline': ref, 'all_equal': True, 'canonical_checks': checks, 'selected_cameras': selected,
          'fixture': fixture_before, 'log_sha256': log_hash, 'compiler': compiler, 'compiled_sources': verified,
          'executables': {name: digest(path) for name, path in executables.items()}, 'results': results,
          'median_sequential_cpu_ms': timings, 'game_launched': False, 'probe_count_per_region': 5600,
          'probe_rays': 64, 'probe_bounces': 3, 'rays_per_region': 8192, 'timing_scope': 'Preparation and bake; static MPQ geometry, no game/GPU/animated actors or concurrent worker contention'}
(out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({'all_equal': True, 'median_sequential_cpu_ms': timings}, indent=2))
