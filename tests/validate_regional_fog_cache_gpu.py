#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual timing
"""Exact fog shader oracle using production MPQ fields/BVH depth and synthetic controls."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'scripts/shaders')]
import northlight_paths as fp


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def fresh_directory(path):
    require(not path.exists() or (path.is_dir() and not any(path.iterdir())),
            f'Use a fresh empty output directory: {path}')
    path.mkdir(parents=True, exist_ok=True)


def file_records(root, names):
    root = root.resolve()
    result = {}
    for name in sorted(names):
        path = (root / name).resolve()
        require(path.is_relative_to(root) and path.is_file(), f'Missing or external input: {name}')
        result[name] = {'bytes': path.stat().st_size, 'sha256': sha(path)}
    return result


def verify_files(root, expected, exact=False):
    if exact:
        actual = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
        require(actual == set(expected), f'Unexpected file inventory: {root}')
    require(file_records(root, expected) == expected, f'File content changed: {root}')


def source_snapshot(root=ROOT):
    # Conservative project-source superset, not a compiler per-TU dependency closure.
    names = {p.relative_to(root).as_posix() for p in (root / 'src').rglob('*') if p.is_file()}
    names.update(('northlight_paths.py', 'scripts/build_environment.py',
                  'scripts/shaders/shader_compiler.py', 'scripts/shaders/compile_shaders.cpp',
                  'tests/regional_fog_cache_gpu.cpp', 'tests/build_regional_fog_fixture.cpp',
                  'tests/removal_distance_gpu.cpp', 'tests/validate_regional_fog_cache_gpu.py'))
    return file_records(root, names)


def fog_names(mpq):
    names = {'manifest.json'}
    for camera in json.loads((mpq / 'camera.json').read_text()):
        tx = math.floor((17066.666666666668-camera['eye'][1])/(1600/3))
        ty = math.floor((17066.666666666668-camera['eye'][0])/(1600/3))
        names.update(f'Northrend/{x}_{y}.frf' for y in range(ty-1, ty+2) for x in range(tx-1, tx+2))
    return names


def request_identity(args):
    baseline = subprocess.check_output(['git', 'rev-parse', args.baseline_ref + '^{commit}'],
                                       cwd=ROOT, text=True).strip()
    return {'baseline_ref': baseline, 'source': str(args.source.resolve()),
            'shader_sources': file_records(args.source.parent, (args.source.name, 'auto_bump_normal.hlsl')),
            'output': str(args.output.resolve()), 'mpq': str(args.mpq.resolve()),
            'mpq_manifest_sha256': sha(args.mpq / 'manifest.json'),
            'fog_metadata': str(args.fog_metadata.resolve()),
            'fog_inputs': file_records(args.fog_metadata, fog_names(args.mpq))}


def verify_identity(expected, current):
    require(expected == current, 'Baseline, source, output, MPQ or fog arguments/inputs changed')


def verify_mpq(root):
    manifest = json.loads((root / 'manifest.json').read_text())
    require(not any(manifest[k] for k in ('missing_tiles', 'missing_assets', 'unsupported'))
            and not manifest['mpq_view']['unreadable'], 'Incomplete MPQ manifest')
    # The producer reads these roots. Every available model/tile must be authenticated,
    # including files omitted from an edited manifest; unused corpus files are a superset.
    consumed = {'terrain.bin', 'camera.bin', 'camera.json'}
    consumed.update(p.relative_to(root).as_posix() for p in (root / 'world-cache').rglob('*') if p.is_file())
    require(consumed <= set(manifest['outputs']), 'MPQ manifest omits a producer input')
    verify_files(root, manifest['outputs'])
    return manifest


def verify_build(out, report):
    require(report.get('compile_complete') is True, 'Compile phase is missing or incomplete')
    require(source_snapshot() == report['source_snapshot'], 'Project source/tool superset changed since compile')
    verify_files(out, report['artifacts'])
    verify_files(out, report['shader_inputs'])
    for key, record in report['shaders'].items():
        require(sha(out / (key + '.bin')) == record['shader_sha256'], 'Compiled shader changed')
    zig = Path(report['zig']['path'])
    require(sha(zig) == report['zig']['sha256'], 'Zig executable changed since compile')


def main():
    require(__debug__, "Assertions are required; do not run this oracle with python -O")
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline-ref', required=True)
    p.add_argument('--source', type=Path, default=ROOT / 'shaders/world_effects.hlsl',
                   help='Candidate HLSL; saved rejected experiments can be replayed without editing production sources')
    p.add_argument('--mpq', type=Path, required=True)
    p.add_argument('--fog-metadata', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--backend', type=Path, action='append', default=[])
    p.add_argument('--phase', choices=('compile', 'fixture', 'check', 'benchmark'), required=True)
    args = p.parse_args()
    assert os.name == 'nt'
    out = args.output.resolve()
    if args.phase == 'compile':
        fresh_directory(out)
    report_path = out / 'validation.json'
    identity = request_identity(args)
    if args.phase == 'compile':
        report = {'schema': 2, 'request': identity, 'baseline_ref': identity['baseline_ref'],
                  'source_snapshot_kind': 'project src superset + used tools; not per-TU dependencies',
                  'source_snapshot': source_snapshot(), 'game_launched': False,
                  'installed_files_changed': False, 'checks': [], 'benchmarks': []}
    else:
        report = json.loads(report_path.read_text())
        require(report.get('schema') == 2, 'Legacy report cannot be resumed; use a fresh compile output')
        verify_identity(report['request'], identity)
        verify_build(out, report)
    def save():
        report_path.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    def unchanged():
        verify_identity(report['request'], request_identity(args))
        verify_build(out, report)
    if args.phase == 'compile':
        from shader_compiler import Compiler, validate_assembly
        report['zig'] = {'path': str(fp.zig().resolve()), 'sha256': sha(fp.zig())}
        os.environ['NORTHLIGHT_OUT'] = str(out / 'compiler')
        compiler = Compiler(allow_compatible_binary=True)
        report['compiler'] = compiler.provenance
        report['shaders'] = {}
        report['shader_inputs'] = {}
        for variant in ('old', 'new'):
            directory = out / variant
            directory.mkdir()
            for name in ('world_effects.hlsl', 'auto_bump_normal.hlsl'):
                raw = (subprocess.check_output(['git', 'show', f'{report["baseline_ref"]}:shaders/{name}'], cwd=ROOT)
                       if variant == 'old' else (args.source if name == 'world_effects.hlsl' else args.source.with_name(name)).read_bytes())
                (directory / name).write_bytes(raw)
                report['shader_inputs'][f'{variant}/{name}'] = {'bytes': len(raw), 'sha256': hashlib.sha256(raw).hexdigest()}
                if variant == 'new':
                    original_name = args.source.name if name == 'world_effects.hlsl' else name
                    require(report['shader_inputs'][f'{variant}/{name}'] == identity['shader_sources'][original_name],
                            'Candidate shader changed while copying')
            for entry in ('WorldFog', 'FogClouds', 'FogBlur'):
                binary = directory / (entry + '.bin')
                compiler.compile(directory / 'world_effects.hlsl', entry, 'ps_3_0', binary)
                record = {'source_sha256': sha(directory / 'world_effects.hlsl'), 'shader_sha256': sha(binary),
                          **validate_assembly(Path(str(binary) + '.asm'), 'ps_3_0')}
                report['shaders'][variant + '/' + entry] = record
                print(variant, entry, record['static_instruction_slots'], record['temporary_registers'], flush=True)
                save()
        commands = [
            [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
             *fp.test_include_flags(), str(fp.TESTS / 'regional_fog_cache_gpu.cpp'), '-luser32', '-o', str(out / 'fog-oracle.exe')],
            [str(fp.zig()), 'c++', '-target', 'x86_64-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
             *fp.test_include_flags(), str(fp.TESTS / 'build_regional_fog_fixture.cpp'), str(fp.src('world_gi.cpp')),
             '-o', str(out / 'build-fixture.exe')]]
        for command in commands:
            subprocess.run(command, env=fp.zig_env(), check=True)
        report['commands'] = commands
        names = ['fog-oracle.exe', 'build-fixture.exe', 'compiler/shader-compiler/compile_shaders.exe',
                 'compiler/shader-compiler/d3dcompiler_47.dll']
        names += [f'{variant}/{name}' for variant in ('old', 'new')
                  for name in ('world_effects.hlsl', 'auto_bump_normal.hlsl',
                               'WorldFog.bin', 'FogClouds.bin', 'FogBlur.bin')]
        report['artifacts'] = file_records(out, names)
        report['compile_complete'] = True
        unchanged()
        report['tool_sources'] = {name: sha(fp.TESTS / name) for name in (
            'regional_fog_cache_gpu.cpp', 'build_regional_fog_fixture.cpp', Path(__file__).name)}
        save()
        return
    if args.phase == 'fixture':
        assert 'fixture' not in report
        report['production_source_sha256'] = {name: sha(fp.src(name)) for name in (
            'world_gi.cpp', 'world_gi.h', 'regional_fog.h', 'forest_regions.inc')}
        manifest = verify_mpq(args.mpq.resolve())
        cameras = json.loads((args.mpq / 'camera.json').read_text())
        paths = fog_names(args.mpq)
        fresh_directory(out / 'mpq-input')
        metadata = out / 'fog-metadata'
        (metadata / 'Northrend').mkdir(parents=True)
        evidence = {}
        for name in sorted(paths | {'manifest.json'}):
            source, target = args.fog_metadata / name, metadata / name
            shutil.copyfile(source, target)
            evidence[name] = {'bytes': target.stat().st_size, 'sha256': sha(target)}
            assert sha(source) == evidence[name]['sha256']
        verify_files(metadata, identity['fog_inputs'], exact=True)
        unchanged()
        raw = subprocess.check_output([str(out / 'build-fixture.exe'), str(args.mpq.resolve()), str(metadata),
                                       str(out / 'mpq-input'), '137', '79'], text=True)
        print(raw, end='', flush=True)
        (out / 'fixture.log').write_text(raw)
        expected = {f'pose-{pose}-{suffix}' for pose in range(len(cameras)*2)
                    for suffix in ('constants.f32', 'depth.fgr', 'regional.fgr', 'scene.fgr')}
        outputs = file_records(out / 'mpq-input', expected)
        # A water.fgr injection is significant: the oracle automatically consumes it.
        verify_files(out / 'mpq-input', outputs, exact=True)
        verify_files(metadata, identity['fog_inputs'], exact=True)
        verify_mpq(args.mpq.resolve())
        unchanged()
        report['fixture'] = {'source': str(args.mpq.resolve()), 'manifest_sha256': sha(args.mpq / 'manifest.json'),
                             'fog_metadata_source': str(args.fog_metadata.resolve()), 'fog_metadata': evidence,
                             'outputs': outputs,
                             'camera_count': len(cameras)*2, 'recorded_camera_count': len(cameras), 'limitations': manifest['limitations'] + [
                                 'The second camera set uses recorded XY/yaw, field ground+2 height and forward.z=-0.1; these are labeled terrain-relative controls, not recorded eyes.',
                                 'Projection is assumed (fx=1.2686, fy=1.9626, near=0.2, far=1277); camera eye/forward are recorded.',
                                 'Depth is production BVH visibility within 128 units, not a captured game depth buffer.',
                                 'Lighting, shadow pattern and cloud noise are controlled test inputs; static geometry omits live actor animation.',
                                 'Large render-target timings resample the 137x79 MPQ depth fixture; they are not a measured game FPS uplift.']}
        save()
        return
    assert args.backend and report.get('fixture')
    verify_files(out / 'mpq-input', report['fixture']['outputs'], exact=True)
    verify_files(out / 'fog-metadata', identity['fog_inputs'], exact=True)
    rows_key = 'checks' if args.phase == 'check' else 'benchmarks'
    # Coverage is scoped to this invocation, never satisfied by an earlier run.
    report[rows_key] = []
    report[args.phase + '_complete'] = False
    report['unchanged_entries'] = {}
    save()
    for backend in args.backend:
        assert backend.is_file()
        backend_info = {'path': str(backend.resolve()), 'sha256': sha(backend)}
        for entry in ('WorldFog', 'FogClouds'):
            if report['shaders']['old/'+entry]['shader_sha256'] == report['shaders']['new/'+entry]['shader_sha256']:
                report.setdefault('unchanged_entries', {})[entry] = 'Bytecode identical; no differential rendering or performance claim'
                save()
                continue
            entry_rows = []
            if args.phase == 'benchmark':
                quality = [row for row in report['checks']
                           if row['backend']['sha256'] == backend_info['sha256'] and row['shader'] == entry]
                require(report.get('check_complete') and len(quality) == 40 + 14*report['fixture']['camera_count'],
                        'Run the complete quality check for each benchmark entry/backend first')
            if args.phase == 'check':
                cases = [(0, scenario, bits, 0, 0, 1, False) for scenario in range(20) for bits in (16, 32)]
                cases += [(pose, scenario, bits, 0, 0, 1, False) for pose in range(report['fixture']['camera_count'])
                          for scenario in range(20, 27) for bits in (16, 32)]
            else:
                poses = sorted(set((0, report['fixture']['camera_count']//3, 2*report['fixture']['camera_count']//3, report['fixture']['camera_count']-1)))
                cases = [(pose, 20, 16, 1920, 1080, 8, reverse) for pose in poses for reverse in (False, True)]
            for pose, scenario, bits, width, height, iterations, reverse in cases:
                binaries = [str(out / variant / (entry + '.bin')) for variant in ('old', 'new')]
                if reverse:
                    binaries.reverse()
                command = [str(out / 'fog-oracle.exe'), str(out / 'mpq-input' / f'pose-{pose}-'), *binaries,
                           entry, str(backend.resolve()), str(scenario), str(bits), str(width), str(height), str(iterations)]
                result = subprocess.run(command, cwd=out, capture_output=True, text=True, timeout=120)
                rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
                for row in rows:
                    if reverse:
                        row['old_ms'], row['new_ms'] = row['new_ms'], row['old_ms']
                    row.update(backend=backend_info, pose=pose, reverse=reverse)
                    report[rows_key].append(row)
                    entry_rows.append(row)
                    print(json.dumps(row), flush=True)
                save()
                assert result.returncode == 0 and len(rows) == 1, (command, result.returncode, result.stdout, result.stderr)
                require((rows[0]['shader'], rows[0]['scenario'], rows[0]['output_bits'], rows[0]['authentic_field_depth'])
                        == (entry, scenario, bits, scenario >= 20), 'Oracle returned a different case')
                assert rows[0]['changed'] == 0 and rows[0]['nonfinite'] == 0 and rows[0]['max_abs'] == 0
            require(len(entry_rows) == len(cases), 'Incomplete per-entry/backend coverage')
            if args.phase == 'check':
                require(any(row['active_components'] for row in entry_rows if not row['authentic_field_depth']),
                        'Synthetic cases were all neutral')
                require(any(row['active_components'] for row in entry_rows
                            if row['pose'] >= report['fixture']['recorded_camera_count']),
                        'Grounded MPQ cases were all neutral')
        require(sha(backend) == backend_info['sha256'], 'Backend DLL changed during invocation')
    verify_files(out / 'mpq-input', report['fixture']['outputs'], exact=True)
    verify_files(out / 'fog-metadata', identity['fog_inputs'], exact=True)
    unchanged()
    report[args.phase + '_complete'] = bool(report[rows_key])
    save()
    if report[rows_key]:
        print(args.phase, 'PASS; no game launched', flush=True)
    else:
        print(args.phase, 'SKIPPED: bytecode identical; no differential coverage or performance claim', flush=True)


if __name__ == '__main__':
    main()
