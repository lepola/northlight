#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""Run issue-106 hidden production shaders; --compile-only just builds the oracle.

Fixtures contain independent ray truth, never a CPU copy of the AO kernel.
The installed game DLL, client settings and game process are not used.
"""
from array import array
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import statistics
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tests')]
import northlight_paths as fp
from ao_actor_reference import validate_gpu_result
from ao_actor_fixture_pack import load_fixtures


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-gpu', action='store_true')
    parser.add_argument('--compiled', type=Path, default=fp.COMPILED, help='Shader directory with a verified adjacent shader-build.json')
    parser.add_argument('--compile-only', action='store_true')
    parser.add_argument('--fixture', type=Path, action='append', default=[])
    parser.add_argument('--backend', type=Path)
    parser.add_argument('--benchmark', action='store_true', help='Short interleaved batches, fixtures >=1000px wide only')
    args = parser.parse_args()
    out = fp.output_dir()
    out.mkdir(parents=True, exist_ok=True)
    executable = out / 'ao_actor_gpu.exe'
    source = ROOT / 'tests/ao_actor_gpu.cpp'
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                    '-O2', '-UNDEBUG', '-Wno-return-type', *fp.include_flags(), str(source),
                    '-luser32', '-o', str(executable)], env=fp.zig_env(), check=True)
    evidence = {'game_launched': False, 'gpu_executed': False, 'test_source_sha256': digest(source),
                'actor_shader_source_sha256': digest(ROOT / 'shaders/ao_actor.hlsl'),
                'actor_policy_sha256': digest(ROOT / 'src/core/ao_actor_policy.h'), 'cases': []}
    if not args.compile_only:
        assert os.name == 'nt', 'GPU execution requires Windows D3D9'
        if not args.fixture:
            evidence['fixture_pack_sha256'] = digest(ROOT / 'tests/fixtures/ao_actor_106.jsonl')
            folder = out / 'fixtures'
            args.fixture = [folder / name for name, _ in load_fixtures(folder)]
        if args.backend:
            backend = args.backend.resolve()
        else:
            directory = ctypes.create_unicode_buffer(32768)
            assert ctypes.windll.kernel32.GetSystemDirectoryW(directory, len(directory)) and directory.value
            system = Path(directory.value)
            wow = system.parent / 'SysWOW64/d3d9.dll'
            backend = wow if wow.is_file() else system / 'd3d9.dll'
        client = fp.client_root(required=False)
        assert backend.is_file() and (client is None or backend != (client / 'd3d9.dll').resolve()), backend
        evidence['backend_sha256'] = digest(backend)
        import windows_gpu_guard
        windows_gpu_guard.processes()
        manifest_path = args.compiled.parent / 'shader-build.json'
        manifest = json.loads(manifest_path.read_text())
        evidence['shader_manifest_sha256'] = digest(manifest_path)
        evidence['compiled_source_sha256'] = manifest['source_sha256']
        evidence['compatibility_mode'] = manifest.get('compatibility_mode', 'production')
        evidence['compiled_sha256'] = {}
        for entry in ('AO', 'HBAOPlusContactBloom', 'GTAOContactBloom', 'HBAOActorContactBloom',
                      'GTAOActorContactBloom', 'HBAOActorOnly', 'GTAOActorOnly', 'AOActorPrepare', 'AOBlur'):
            binary = args.compiled / (entry + '.bin')
            actual = digest(binary)
            assert actual == manifest['shaders'][entry]['sha256'], entry
            evidence['compiled_sha256'][entry] = actual
        rows, fixtures = [], []
        for path in args.fixture:
            path = path.resolve()
            fixture = json.loads(path.read_text())
            for key, suffix in (('depth', '.depth.f32'), ('actor', '.actor.f32')):
                original = path.parent / fixture[key + '_file']
                assert digest(original) == fixture[key + '_sha256'], original
                # Keep all test writes in the configured output folder.
                (out / (path.stem + suffix)).write_bytes(original.read_bytes())
            prefix = out / path.stem
            values = [*fixture['size'], fixture['radius'], fixture['near'], fixture['far'],
                      fixture['partition'], *fixture['projection'], fixture['strength']]
            rows.append(json.dumps(str(prefix)) + ' ' + ' '.join(map(str, values)))
            fixtures.append((fixture, prefix))
        listing = out / 'cases.txt'
        # std::quoted reads native path backslashes literally only when escaped.
        listing.write_text('\n'.join(rows) + '\n', encoding='utf-8')
        run = [str(executable), str(args.compiled), str(backend), str(listing), str(out / 'renders.tsv')]
        if args.benchmark:
            run.append('--benchmark')
        subprocess.run(run,
                       cwd=out, check=True, timeout=120)
        errors = []
        for fixture, prefix in fixtures:
            aw, ah = fixture['ao_size']
            for method, suffix in (('HBAO+', 'hbao'), ('GTAO', 'gtao')):
                report = {'method': method, **{key: fixture[key] for key in
                          ('scene', 'size', 'radius', 'depth_sha256', 'actor_sha256')}}
                images = {}
                for variant in ('baseline', 'candidate'):
                    values = array('f')
                    values.frombytes(Path(str(prefix) + '.' + suffix + '.' + variant + '.f32').read_bytes())
                    assert len(values) == aw * ah * 4
                    if sys.byteorder != 'little':
                        values.byteswap()
                    images[variant] = values
                    report[variant] = {name: [values[(p['y'] * aw + p['x']) * 4 + 3]
                                             for p in roi['samples']] for name, roi in fixture['rois'].items()}
                rgb_delta = max(abs(a - b) for i, (a, b) in enumerate(zip(images['baseline'], images['candidate'])) if i % 4 != 3)
                assert rgb_delta == 0, ('bloom RGB changed', fixture['scene'], method, rgb_delta)
                if method in fixture.get('expect_baseline_methods', []):
                    assert images['baseline'] == images['candidate'], ('baseline dispatch changed', fixture['scene'], method, fixture['radius'])
                fallback = {}
                for variant in ('baseline', 'candidate'):
                    pixels = array('f')
                    pixels.frombytes(Path(str(prefix) + '.' + suffix + '.legacy.' + variant + '.f32').read_bytes())
                    assert len(pixels) == len(images[variant])
                    if sys.byteorder != 'little':
                        pixels.byteswap()
                    assert all(a == b for a, b in zip(pixels[3::4], images[variant][3::4])), ('legacy AO alpha changed', fixture['scene'], method)
                    fallback[variant] = pixels
                fallback_rgb_delta = max(abs(a - b) for i, (a, b) in enumerate(zip(fallback['baseline'], fallback['candidate'])) if i % 4 != 3)
                assert fallback_rgb_delta == 0, ('legacy color bounce changed', fixture['scene'], method, fallback_rgb_delta)
                failures = validate_gpu_result(fixture, report)
                errors.extend((fixture['scene'], method, failure) for failure in failures)
                destination = out / (prefix.name + '.' + suffix + '.json')
                destination.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
                evidence['cases'].append({'scene': fixture['scene'], 'method': method, 'failures': failures,
                                          'rgb_max_delta': rgb_delta, 'legacy_rgb_max_delta': fallback_rgb_delta, 'report': str(destination)})
            timing = Path(str(prefix) + '.perf.tsv')
            if args.benchmark and fixture['size'][0] >= 1000:
                groups = {}
                for line in timing.read_text().splitlines():
                    method, actor, round_, gpu_us, cpu_us = line.split()
                    groups.setdefault((int(method), int(actor)), []).append((float(gpu_us), float(cpu_us)))
                medians = {str(key): {'gpu_median_us': statistics.median(row[0] for row in values),
                                      'cpu_submit_median_us': statistics.median(row[1] for row in values),
                                      'pairs': len(values)} for key, values in groups.items()}
                evidence.setdefault('timing', []).append({'scene': fixture['scene'], 'size': fixture['size'],
                    'scope': 'AO preparation/kernel/blur only; excludes native actor marking. Batched GPU timestamps include submission gaps.',
                    'medians': medians, 'raw': str(timing)})
        evidence['gpu_executed'] = True
        evidence['failures'] = errors
        (out / 'ao_actor_gpu_validation.json').write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
        assert not errors, errors
        print('PASS production actor AO + AOBlur against independent ray truth:', len(evidence['cases']), 'cases')
    else:
        print('COMPILE ONLY: no graphics device or game started.')
        (out / 'ao_actor_gpu_build.json').write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
