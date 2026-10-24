#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,world-cache
"""Compare real local-GI/terrain/shadow/page preparation against a frozen Git source.

Seven sequential regions cover cold, repeated, four orbit directions and return.
No client executable, settings write or GPU is involved. Timings are offline CPU
work, not an in-game FPS prediction. Source and output fingerprints use the
existing 128-bit mesh content-key policy.
"""
import argparse
import io
import json
import statistics
import subprocess
import sys
import zipfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline-ref', required=True)
parser.add_argument('--map', choices=['Northrend', 'Azeroth'], default='Northrend')
parser.add_argument('--terrain-radius', type=int, choices=[928, 1856, 2048], default=928)
parser.add_argument('--rounds', type=int, default=3)
parser.add_argument('--output', type=Path)
parser.add_argument('--shared-baseline', action='store_true', help='The frozen source already ships the shared-scene pipeline')
parser.add_argument('--bake-probes', action='store_true', help='Also solve the log-matched 5600-probe static window, 64 rays/3 bounces')
args = parser.parse_args()
assert 1 <= args.rounds <= 10
out = args.output or fp.out()/'shared-scene-pipeline'
out.mkdir(parents=True, exist_ok=True)
baseline = out/'baseline-source'
archive = subprocess.check_output(['git', 'archive', '--format=zip', args.baseline_ref, 'src'], cwd=fp.REPO)
with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
    for entry in zipped.infolist():
        target = baseline/entry.filename
        assert target.resolve().is_relative_to(baseline.resolve())
        if entry.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(zipped.read(entry))
binaries = {}
for name, root, shared in [('baseline', baseline, int(args.shared_baseline)), ('current', fp.REPO, 1)]:
    exe = out/(name+'.exe')
    includes = [flag for folder in sorted((root/'src').iterdir()) if folder.is_dir() for flag in ['-I', str(folder)]]
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                    '-Wall', '-Wextra', '-Werror', '-DSHARED_SCENE_PIPELINE='+str(shared),
                    '-DCURRENT_TERRAIN_CHUNKS='+str(int(name == 'current')), '-DPIPELINE_RADIUS='+str(args.terrain_radius), *includes,
                    str(fp.TESTS/'validate_shared_scene_pipeline.cpp'), str(root/'src/gi/world_gi.cpp'),
                    str(root/'src/world/world_mesh_plan.cpp'), '-o', str(exe)], env=fp.zig_env(), check=True)
    binaries[name] = exe
results = {'baseline': [], 'current': []}
for round_number in range(args.rounds):
    for name in (['baseline', 'current'] if round_number % 2 == 0 else ['current', 'baseline']):
        text = subprocess.check_output([str(binaries[name]), str(fp.world_cache()), args.map, *(['bake'] if args.bake_probes else [])], text=True)
        (out/f'{name}-{round_number}.log').write_text(text, encoding='utf-8')
        rows = [dict(part.split('=', 1) for part in line.split()[1:]) for line in text.splitlines() if line.startswith('PIPELINE ')]
        assert len(rows) == 7, text
        results[name].append(rows)
        print(name, round_number, 'pipelineMedianMs', statistics.median(float(row['totalMs']) for row in rows[1:]), flush=True)
checks = ['scene', 'plan', 'pagesHash', 'rays', 'bakeHash', 'localTriangles', 'planTriangles', 'pages', 'batches']
for round_number in range(args.rounds):
    for step in range(7):
        old, new = (results[name][round_number][step] for name in ['baseline', 'current'])
        for key in checks:
            assert old[key] == new[key], (round_number, step, key, old[key], new[key])
timings = {}
for name, rounds in results.items():
    timings[name] = {}
    for label, steps in [('cold', [0]), ('sameRegion', [1]), ('orbitAndReturn', list(range(2, 7))), ('warm', list(range(1, 7)))]:
        timings[name][label] = {key: statistics.median(float(rows[step][key]) for rows in rounds for step in steps)
                                for key in ['localMs', 'terrainMs', 'planMs', 'pagesMs', 'totalMs', 'traceMs', 'bakeMs', 'prepareAndBakeMs']}
report = {'baseline': subprocess.check_output(['git', 'rev-parse', args.baseline_ref], cwd=fp.REPO, text=True).strip(),
          'map': args.map, 'rounds': args.rounds, 'shared_baseline': args.shared_baseline, 'terrain_radius': args.terrain_radius, 'regions_per_round': 7, 'rays_per_region': 8192,
          'bake_probes': args.bake_probes, 'probe_count': 5600 if args.bake_probes else 0, 'probe_rays': 64, 'probe_bounces': 3,
          'canonical_checks': checks, 'all_equal': True, 'game_launched': False,
          'timings_cpu_ms': timings, 'results': results}
(out/'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
print(json.dumps({'all_equal': True, 'timings_cpu_ms': timings}, indent=2))
