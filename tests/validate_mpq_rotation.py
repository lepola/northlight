#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig manual timing
"""Run actual MPQ terrain and the recorded camera orbit through frozen/current capture and pressure policy."""
import argparse
import ast
import hashlib
import io
import json
from pathlib import Path
import re
import statistics
import subprocess
import sys
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

MODES = {'0': 'stable_revisions', '1': 'rewritten_same_xyz',
         '2': 'rewritten_same_xyz_and_indices', '3': 'synthetic_rebased_same_geometry'}


def source_hashes():
    paths = [p for p in fp.SRC.rglob('*') if p.is_file()]
    paths += [fp.REPO / 'northlight_paths.py', fp.TESTS / 'test_terrain_snapshot.py',
              fp.TESTS / 'rotation_mpq.cpp', Path(__file__).resolve()]
    return {p.relative_to(fp.REPO).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths)}


def method(text):
    start = text.index('void setMemoryPressure(')
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=3)
    args = parser.parse_args()
    assert 1 <= args.rounds <= 10
    if args.output.exists() and any(args.output.iterdir()):
        raise SystemExit('Output must be new or empty; previous evidence must remain immutable')
    before = source_hashes()
    manifest = json.loads((args.fixture / 'manifest.json').read_text(encoding='utf-8'))
    if any(manifest[key] for key in ('missing_tiles', 'missing_assets', 'unsupported')) or manifest['mpq_view']['unreadable']:
        raise SystemExit('Incomplete MPQ fixture: resolve missing or unsupported inputs before comparison')
    for name, record in manifest['outputs'].items():
        path = args.fixture / name
        assert path.resolve().is_relative_to(args.fixture.resolve()), name
        data = path.read_bytes()
        assert len(data) == record['bytes'] and hashlib.sha256(data).hexdigest() == record['sha256'], name
    args.output.mkdir(parents=True, exist_ok=True)
    baseline = args.output / 'baseline-source'
    archive = subprocess.check_output(['git', 'archive', '--format=zip', manifest['baseline_ref'], 'src'], cwd=fp.REPO)
    with zipfile.ZipFile(io.BytesIO(archive)) as zipped:
        for entry in zipped.infolist():
            path = baseline / entry.filename
            assert path.resolve().is_relative_to(baseline.resolve())
            if not entry.is_dir():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(zipped.read(entry))
    tree = ast.parse((fp.TESTS / 'test_terrain_snapshot.py').read_text(encoding='utf-8'))
    fixtures = {n.targets[0].id: ast.literal_eval(n.value) for n in tree.body
                if isinstance(n, ast.Assign) and isinstance(n.targets[0], ast.Name)
                and n.targets[0].id in ('stub', 'harness')}
    executables, commands = {}, {}
    for name, root in [('baseline', baseline), ('current', fp.REPO)]:
        out = args.output / name
        out.mkdir(exist_ok=True)
        (out / 'd3d9.h').write_text(fixtures['stub'], encoding='utf-8')
        (out / 'rotation_device.h').write_text(fixtures['harness'].split('int main(){')[0], encoding='utf-8')
        setter = method((root / 'src/world/world_memory_guard.inl').read_text(encoding='utf-8'))
        (out / 'rotation_setter.h').write_text(setter, encoding='utf-8')
        exe = out / 'rotation.exe'
        includes = [v for folder in sorted((root / 'src').iterdir()) if folder.is_dir() for v in ['-I', str(folder)]]
        command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                   '-Wall', '-Wextra', '-Werror', '-DBASELINE_PRESSURE=' + str(int(name == 'baseline')),
                   '-I', str(out), *includes, str(fp.TESTS / 'rotation_mpq.cpp'), '-o', str(exe)]
        subprocess.run(command, env=fp.zig_env(), check=True)
        executables[name] = exe
        commands[name] = command
    results = {'baseline': [], 'current': []}
    for number in range(args.rounds):
        for name in (['baseline', 'current'] if number % 2 == 0 else ['current', 'baseline']):
            text = subprocess.check_output([str(executables[name]), str(args.fixture / 'terrain.bin'),
                                            str(args.fixture / 'camera.bin'), str(manifest['terrain_radius'])], text=True)
            (args.output / f'{name}-{number}.log').write_text(text, encoding='utf-8')
            rows = [{**dict(re.findall(r'(\w+)=([^\s]+)', line)), 'summary': line.startswith('ROTATE_SUMMARY ')}
                    for line in text.splitlines() if line.startswith(('ROTATE ', 'ROTATE_SUMMARY '))]
            results[name].append(rows)
            print(name, number, [r for r in rows if r['summary']], flush=True)
    for number in range(args.rounds):
        old, new = results['baseline'][number], results['current'][number]
        assert len(old) == len(new)
        for a, b in zip(old, new):
            for key in ('low', 'writes', 'layout', 'cycle', 'frame', 'calls', 'digest', 'geometry'):
                assert a.get(key) == b.get(key), (number, key, a, b)
            if a['low'] == '1':
                for key in ('hits', 'misses', 'reads', 'evictions', 'exactReuses', 'vertexChanged',
                            'indexChanged', 'indexTokenHits', 'rebuilds', 'cap', 'retained'):
                    assert a.get(key) == b.get(key), ('true-low behavior changed', key, a, b)
        for writes in ('0', '1', '2'):
            a, b = ([r for r in side if r['summary'] and r['low'] == '0' and r['writes'] == writes][0] for side in (old, new))
            assert int(a['misses']) > 0, 'Fixture does not reproduce rotation churn'
            assert all(int(b[key]) == 0 for key in ('misses', 'evictions', 'rebuilds')), (a, b)
            if writes == '0':
                assert int(b['reads']) == 0
            else:
                assert int(b['reads']) > 0 and int(b['exactReuses']) == int(b['calls']), b
                assert int(b['vertexChanged']) == int(b['calls']), b
                if writes == '2':
                    assert int(b['indexChanged']) == int(b['calls']) and int(b['indexTokenHits']) == 0, b
                else:
                    assert int(b['indexChanged']) == 0 and int(b['indexTokenHits']) == int(b['calls']), b
    controls = []
    for name, exe in executables.items():
        for mode in ('xyz', 'index-order', 'stale-xyz', 'stale-index-order'):
            command = [str(exe), str(args.fixture / 'terrain.bin'), str(args.fixture / 'camera.bin'),
                       str(manifest['terrain_radius']), mode]
            result = subprocess.run(command, capture_output=True, text=True)
            (args.output / f'{name}-{mode}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
            negative = mode.startswith('stale-')
            if negative:
                assert result.returncode != 0 and 'MPQ_TRIANGLE_XYZ_MISMATCH' in result.stderr, (name, mode, result)
            else:
                assert result.returncode == 0 and f'MUTATION kind={mode} exactGeometry=1 rebuilds=1' in result.stdout, (name, mode, result)
            controls.append({'implementation': name, 'mode': mode, 'command': command,
                             'returncode': result.returncode, 'expected_failure': negative, 'pass': True})
    timings = {mode: {name: statistics.median(float(r['captureMs']) for rows in rounds for r in rows
                                              if r['summary'] and r['low'] == '0' and r['writes'] == writes)
                     for name, rounds in results.items()} for writes, mode in MODES.items()}
    report = {'baseline_ref': manifest['baseline_ref'], 'fixture_manifest_sha256': hashlib.sha256((args.fixture / 'manifest.json').read_bytes()).hexdigest(),
              'rounds': args.rounds, 'exact_triangle_outputs': True, 'actual_low_pressure_unchanged': True,
              'timings_cpu_path_ms': timings, 'results': results, 'mutation_controls': controls, 'game_launched': False,
              'source_before': before, 'source_after': source_hashes(), 'compiler_commands': commands,
              'compiler_version': subprocess.check_output([str(fp.zig()), 'version'], text=True).strip(),
              'executable_sha256': {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in executables.items()},
              'limitations': manifest['limitations'] + [
                  'Fake D3D buffers execute production capture; no GPU/driver or client draw-list replay.',
                  'VB/IB revision changes are explicit scenarios, not recovered per-draw writes from the log.',
                  'The two alternating buffer layouts are synthetic; source logs do not record D3D base/start/stride layouts.',
                  'Rebased layouts deliberately increase cache identity diversity; zero misses are required only for fixed layouts.',
                  'captureMs times production readMeshShared only; fixture copies and the independent triangle oracle are excluded.']}
    assert report['source_before'] == report['source_after'], 'Tested source changed during validation'
    (args.output / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'exact_triangle_outputs': True, 'timings_cpu_path_ms': timings}, indent=2))


if __name__ == '__main__':
    main()
