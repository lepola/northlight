#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig manual timing
"""Frozen/current MPQ capture at identical caps, with exact independent coverage.

This manual comparison requires the selected immutable commit in local Git history.
The automatic coverage test uses its embedded immutable oracle without Git.
"""
import argparse
import hashlib
import io
import json
import math
from pathlib import Path
import re
import statistics
import struct
import subprocess
import sys
import zipfile
repo = Path(__file__).resolve().parents[1]
helper_paths = [Path(__file__), repo / 'northlight_paths.py', repo / 'tests/test_terrain_snapshot.py',
                repo / 'tests/validate_mpq_rotation.py', repo / 'tests/test_terrain_classification.py',
                repo / 'tests/rotation_mpq.cpp', repo / 'tests/fixtures/terrain_classification_reference.h']
helper_before = {str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest() for path in helper_paths}
sys.path.insert(0, str(repo))
import northlight_paths as fp
from test_terrain_classification import BASELINE, headers, sha
from validate_mpq_rotation import method

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fixture', type=Path, required=True)
parser.add_argument('--camera-log', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--rounds', type=int, default=3)
parser.add_argument('--camera-count', type=int, default=8)
parser.add_argument('--baseline-ref', default=BASELINE, help='Full immutable baseline commit; defaults to the original classifier baseline')
parser.add_argument('--fitting-control', action='store_true', help='Explicit 8-pose storage comparison; requires zero baseline 32 MiB capacity misses')
parser.add_argument('--storage-comparison', action='store_true', help='Keep exact geometry/coverage/caps; allow intended storage and miss counters to differ')
parser.add_argument('--provenance-negative-control', action='store_true', help='Corrupt a generated header; the provenance guard must reject before compilation')
args = parser.parse_args()
assert 1 <= args.rounds <= 3 and 8 <= args.camera_count <= 16
assert not args.fitting_control or (args.storage_comparison and args.camera_count == 8), 'Fitting control requires explicit storage comparison and eight poses'
assert re.fullmatch('[0-9a-f]{40}', args.baseline_ref), 'Baseline must be a full immutable commit'
out = args.output.resolve()
assert not out.exists() or not any(out.iterdir()), 'Use a new/empty evidence directory'
out.mkdir(parents=True, exist_ok=True)
fixture = args.fixture.resolve()
manifest = json.loads((fixture / 'manifest.json').read_text())
assert manifest['complete'] and manifest['map'] == 'Northrend'
for key in ('missing_tiles', 'missing_assets', 'unsupported'):
    assert not manifest[key], key
assert not manifest['mpq_view']['unreadable'] and not manifest['mpq_view']['unlisted']
inputs = {str(fixture / 'manifest.json'): sha(fixture / 'manifest.json'), str(args.camera_log.resolve()): sha(args.camera_log)}
for name, expected in manifest['outputs'].items():
    path = (fixture / name).resolve();assert path.is_relative_to(fixture)
    assert path.stat().st_size == expected['bytes'] and sha(path) == expected['sha256'], name
    inputs[str(path)] = expected['sha256']
rows = []
for line in args.camera_log.read_text().splitlines():
    if not line.startswith('WORLD camera '):
        continue
    eye = [float(v) for v in re.search(r'eye=\(([^)]+)', line)[1].split()]
    forward = [float(v) for v in re.search(r'forward=\(([^)]+)', line)[1].split()]
    if 5000 < eye[0] < 6000 and 300 < eye[2] < 600:
        rows.append({'tick': int(re.search(r'tick=(\d+)', line)[1]), 'eye': eye, 'forward': forward,
                     'distance': float(re.search(r'pivotDistance=([^ ]+)', line)[1])})
assert len(rows) >= args.camera_count
# Long stationary settings windows must not crowd the actual turning workload
# out of the bounded trace. Keep the first observed pose in each heading sector,
# in its original chronological order; every selected pose is from the log.
selected, sectors = [], set()
for row in rows:
    angle = math.atan2(row['forward'][1], row['forward'][0])
    sector = min(args.camera_count - 1, int((angle + math.pi) * args.camera_count / (2 * math.pi)))
    if sector not in sectors:
        selected.append(row);sectors.add(sector)
assert len(selected) == args.camera_count, 'Recorded trace does not cover every requested heading sector'
tiles = {tuple(row['tile']) for row in manifest['tiles']}
reach = float(manifest['terrain_radius'])
for row in selected:
    x, y, _ = row['eye'];zero, size = 17066.666666666666, 1600.0 / 3.0
    for tx in range(math.floor((zero - y - reach) / size), math.floor((zero - y + reach) / size) + 1):
        for ty in range(math.floor((zero - x - reach) / size), math.floor((zero - x + reach) / size) + 1):
            assert (tx, ty) in tiles, ('Selected camera exceeds complete terrain fixture', tx, ty)
raw = bytearray(b'NLC1' + struct.pack('<I', len(selected)))
for row in selected:
    raw += struct.pack('<Q7f', row['tick'], *row['eye'], *row['forward'], row['distance'])
(out / 'camera.bin').write_bytes(raw)
baseline = out / 'baseline-source'
baseline_commit = subprocess.check_output(['git', 'rev-parse', args.baseline_ref], cwd=fp.REPO, text=True).strip()
assert baseline_commit == args.baseline_ref, 'Frozen baseline must resolve to its full immutable commit'
archive = subprocess.check_output(['git', 'archive', '--format=zip', baseline_commit, 'src'], cwd=fp.REPO)
baseline_archive_sha256 = hashlib.sha256(archive).hexdigest()
with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
    for entry in zipped.infolist():
        target = (baseline / entry.filename).resolve();assert target.is_relative_to(baseline)
        if not entry.is_dir():
            target.parent.mkdir(parents=True, exist_ok=True);target.write_bytes(zipped.read(entry))

driver = (fp.TESTS / 'rotation_mpq.cpp').read_text()
driver = driver.replace('#include "rotation_device.h"', '#include "rotation_device.h"\n#include "terrain_reference.h"')
oracle = r'''
static void exactCoverage(const MeshSnapshot& mesh,const Device& d,const Draw& draw){
    Bounds reference;const auto vertexAt=[&](size_t i){uint16_t index;std::memcpy(&index,d.ib.bytes.data()+2*(draw.start+i),2);
        Position p;std::memcpy(&p,d.vb.bytes.data()+16+24*(draw.base+index)+4,12);return p;};
    assert(referenceTriangleSequence<true>(vertexAt,size_t(draw.count)*3,D3DPT_TRIANGLELIST,reference));
    assert(!std::memcmp(reference.low,mesh.bounds.low,sizeof reference.low)&&!std::memcmp(reference.high,mesh.bounds.high,sizeof reference.high)&&"MPQ_COVERAGE_MISMATCH");
    assert(reference.chunks.size()==mesh.bounds.chunks.size()&&"MPQ_COVERAGE_MISMATCH");
    for(size_t i=0;i<reference.chunks.size();++i){const auto& a=reference.chunks[i];const auto& b=mesh.bounds.chunks[i];
        assert(a.x==b.x&&a.y==b.y&&a.minX==b.minX&&a.maxX==b.maxX&&a.minY==b.minY&&a.maxY==b.maxY&&"MPQ_COVERAGE_MISMATCH");}
}
'''
driver = driver.replace('static uint64_t geometry(', oracle + '\nstatic uint64_t geometry(')
assert driver.count('    assert(!mesh.bounds.chunks.empty());return h;') == 1
driver = driver.replace('    assert(!mesh.bounds.chunks.empty());return h;', '    exactCoverage(mesh,d,draw);return h;')
if args.storage_comparison:
    driver = driver.replace('static uint64_t geometry(', 'template<class Storage,class Cache>\nstatic auto decodedStorage(const Storage& s,const Cache& cache,bool low,unsigned writes,unsigned cycle,unsigned frame,int)->decltype(s.physicalBytes,void()){\n    std::printf("DECODED low=%u writes=%u cycle=%u frame=%u logicalBytes=%llu physicalBytes=%llu blocks=%zu blockAllocationBytes=%llu liveBlockBytes=%llu weakOwners=%zu expiredWeakOwners=%zu positionBytes=%llu positionCapacityBytes=%llu liveBytes=%zu metadataBytes=%zu retained=%llu cap=%llu\\n",unsigned(low),writes,cycle,frame,(unsigned long long)s.bytes,(unsigned long long)s.physicalBytes,s.sharedBlocks,(unsigned long long)s.sharedAllocationBytes,(unsigned long long)s.liveBlockBytes,s.weakOwners,s.expiredWeakOwners,(unsigned long long)s.positionBytes,(unsigned long long)s.positionCapacityBytes,cache.sourceLiveBytes(),cache.sourceMetadataBytes(),(unsigned long long)cache.persistentBytes(),(unsigned long long)cache.persistentByteLimit());\n}\ntemplate<class Storage,class Cache>\nstatic void decodedStorage(const Storage&,const Cache&,bool,unsigned,unsigned,unsigned,long){}\n' + '\nstatic uint64_t geometry(')
    marker = '            std::printf("ROTATE low='
    assert driver.count(marker) == 1
    storage = '''            const auto storage=cache->indexStorage();
            decodedStorage(storage,*cache,trueLow,writes,cycle,frame,0);
            std::printf("STORAGE low=%u writes=%u cycle=%u frame=%u entries=%zu blocks=%zu rawBytes=%llu avoidedRawBytes=%llu decodedBytes=%llu offsetBytes=%llu retained=%llu cap=%llu\\n",unsigned(trueLow),writes,cycle,frame,cache->persistentEntries(),storage.sourceBlocks,(unsigned long long)storage.sourceBytes,(unsigned long long)storage.sourceAvoided,(unsigned long long)storage.bytes,(unsigned long long)storage.offsetBytes,(unsigned long long)cache->persistentBytes(),(unsigned long long)cache->persistentByteLimit());
'''
    driver = driver.replace(marker, storage + marker)
(out / 'rotation.cpp').write_bytes(driver.encode())
watched = [path for source_root in (fp.SRC, baseline / 'src') for path in source_root.rglob('*') if path.is_file()]
watched += [fp.TESTS / 'rotation_mpq.cpp']
source_before = {str(path.resolve()): sha(path) for path in watched}
source_before.update(helper_before)
generated_inputs = {str(path.resolve()): sha(path) for path in (out / 'camera.bin', out / 'rotation.cpp')}
commands = []


def run(command, **kwargs):
    commands.append([str(v) for v in command]);(out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    return subprocess.run(command, check=True, **kwargs)


def unchanged(records, label):
    changed = [path for path, value in records.items() if not Path(path).is_file() or sha(Path(path)) != value]
    assert not changed, label + ': ' + ', '.join(changed)


env = fp.zig_env()
for key, folder in [('TEMP', 'tmp'), ('TMP', 'tmp'), ('ZIG_LOCAL_CACHE_DIR', 'zig-local'), ('ZIG_GLOBAL_CACHE_DIR', 'zig-global')]:
    (out / folder).mkdir(exist_ok=True);env[key] = str(out / folder)
executables = {}
for name, root in [('baseline', baseline), ('current', fp.REPO)]:
    target = out / name;target.mkdir();headers(target)
    setter = method((root / 'src/world/world_memory_guard.inl').read_text())
    (target / 'rotation_setter.h').write_bytes(setter.encode())
    generated_inputs.update({str((target / name).resolve()): sha(target / name)
                            for name in ('d3d9.h', 'rotation_device.h', 'rotation_setter.h', 'terrain_reference.h')})
    if args.provenance_negative_control:
        with (target / 'rotation_device.h').open('ab') as stream:
            stream.write(b'\n// Deliberate provenance drift control.\n')
    unchanged(source_before, 'Source/helper input changed')
    unchanged(generated_inputs, 'Generated input changed')
    includes = [value for folder in sorted((root / 'src').iterdir()) if folder.is_dir() for value in ['-I', str(folder)]]
    exe = target / 'rotation.exe'
    run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
         '-DBASELINE_PRESSURE=0', '-MMD', '-MF', str(target / 'rotation.d'), '-I', str(target), *includes,
         str(out / 'rotation.cpp'), '-o', str(exe)], env=env)
    executables[name] = exe
output_artifacts = {str(path.resolve()): sha(path) for name, exe in executables.items()
                    for path in (exe, out / name / 'rotation.d')}
results = {'baseline': [], 'current': []}
for number in range(args.rounds):
    for name in (['baseline', 'current'] if number % 2 == 0 else ['current', 'baseline']):
        unchanged(output_artifacts, 'Compiler output changed')
        result = run([str(executables[name]), str(fixture / 'terrain.bin'), str(out / 'camera.bin'), str(manifest['terrain_radius'])], text=True, capture_output=True)
        (out / f'{name}-{number}.log').write_text(result.stdout)
        parsed = [dict(re.findall(r'(\w+)=([^\s]+)', line)) for line in result.stdout.splitlines() if line.startswith(('ROTATE ', 'ROTATE_SUMMARY '))]
        assert len(parsed) == 8 * (4 * args.camera_count + 1)
        results[name].append(parsed)
        print(name, number, [row for row in parsed if 'geometry' in row and row['writes'] == '3' and row['low'] == '0'], flush=True)
for number in range(args.rounds):
    for a, b in zip(results['baseline'][number], results['current'][number]):
        semantic = {'low', 'writes', 'layout', 'cycle', 'frame', 'calls', 'digest', 'geometry', 'cap'}
        assert {k: v for k, v in a.items() if k in semantic or (not args.storage_comparison and k != 'captureMs')} == {k: v for k, v in b.items() if k in semantic or (not args.storage_comparison and k != 'captureMs')}, (a, b)
    churn = [row for row in results['baseline'][number] if 'geometry' in row and row['writes'] == '3' and row['low'] == '0'][0]
    if args.fitting_control:
        assert all(int(row['misses']) == 0 and int(row['evictions']) == 0 for row in results['baseline'][number] if 'geometry' in row and row['low'] == '0'), 'Fitting control unexpectedly churned'
    else:
        assert int(churn['misses']) > 0 and int(churn['evictions']) > 0, 'Fixture did not reproduce 32 MiB capacity churn'
controls = []
for name, exe in executables.items():
    for mode in ('xyz', 'index-order', 'stale-xyz', 'stale-index-order'):
        command = [str(exe), str(fixture / 'terrain.bin'), str(out / 'camera.bin'), str(manifest['terrain_radius']), mode]
        commands.append(command);result = subprocess.run(command, capture_output=True, text=True)
        (out / f'{name}-{mode}.log').write_text(result.stdout + result.stderr)
        negative = mode.startswith('stale-')
        assert (result.returncode != 0 and 'MPQ_TRIANGLE_XYZ_MISMATCH' in result.stderr) if negative else result.returncode == 0, (mode, result)
        controls.append({'implementation': name, 'mode': mode, 'expected_failure': negative, 'pass': True})
assert all(sha(Path(path)) == value for path, value in inputs.items()), 'Fixture/log changed'
unchanged(source_before, 'Source/helper input changed')
unchanged(generated_inputs, 'Generated input changed')
unchanged(output_artifacts, 'Compiler output changed')
timings = {writes: {name: statistics.median(float(row['captureMs']) for rows in rounds for row in rows
            if 'geometry' in row and row['low'] == '0' and row['writes'] == writes) for name, rounds in results.items()} for writes in ('0', '1', '2', '3')}
report = {'baseline': args.baseline_ref, 'baseline_commit': baseline_commit, 'baseline_archive_sha256': baseline_archive_sha256,
          'complete_fixture': True, 'inputs': inputs, 'selected_cameras': selected,
          'camera_selection': 'First observed pose in each angular heading sector, preserving recorded chronology', 'synthetic_layouts': True,
          'identical_geometry_coverage_counters_caps_and_bytes': not args.storage_comparison,
          'identical_geometry_coverage_and_caps': True, 'identical_counters_and_bytes': not args.storage_comparison,
          'fitting_control': args.fitting_control, 'storage_comparison': args.storage_comparison, 'timings_cpu_ms': timings, 'results': results,
          'mutation_controls': controls, 'source_before': source_before, 'source_after_identical': True,
          'helper_inputs_before_import': helper_before, 'generated_inputs': generated_inputs, 'generated_inputs_unchanged': True,
          'baseline_sources_unchanged': True, 'output_artifacts': output_artifacts, 'output_artifacts_unchanged': True,
          'compiler_version': subprocess.check_output([str(fp.zig()), 'version'], text=True).strip(), 'compiler_sha256': sha(fp.zig()),
          'executables': {name: sha(path) for name, path in executables.items()}, 'commands': commands,
          'reference_classifier_sha256': sha(out / 'current/terrain_reference.h'), 'game_launched': False,
          'limitations': ['Recorded camera poses, actual MPQ XYZ/holes/winding; original native D3D layouts and LOD are not logged.',
                          'Explicit110-degree candidate cone and synthetic alternating layout, not an original client draw stream.',
                          'Capture timer excludes fixture copies and exact independent XYZ/coverage oracle; no GPU/driver.']}
(out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({'geometry_coverage_and_caps_equal': True, 'storage_comparison': args.storage_comparison,
                  'timings_cpu_ms': timings}, indent=2))
