#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""Offline HDR acceptance and performance graph; never launches or modifies WoW.

Run from the repository root: python tests/verify_hdr_pipeline.py --output .local/hdr-check
Use --no-build when testing an already built renderer DLL.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from windows_gpu_guard import processes


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--dxvk2', type=Path)
    parser.add_argument('--dxvk3', type=Path)
    parser.add_argument('--capture-prefix', type=Path, help='Optional local capture-1- prefix; only native scene/depth are used.')
    parser.add_argument('--captured-ui', type=Path, help='Optional local UI shader; defaults to the authored fixture compiled by the proxy oracle.')
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--max-regression', type=float, default=.15, help='Within-run HDR/reference tolerance, plus 0.03 ms noise allowance.')
    args = parser.parse_args()
    if os.name != 'nt':
        raise SystemExit('The GPU graph requires Windows D3D9; portable CPU tests also run under Linux/WSL.')
    if not 0 <= args.max_regression <= 1:
        raise SystemExit('--max-regression must be between 0 and 1')
    output = args.output.resolve()
    if fp.is_client(output):
        raise SystemExit('Choose an isolated output directory outside the game client.')
    processes()
    output.mkdir(parents=True, exist_ok=True)
    backends = {
        'dxvk2': args.dxvk2 or fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',
        'dxvk3': args.dxvk3 or fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll',
    }
    for path in backends.values():
        if not path.is_file():
            raise SystemExit(f'Backend DLL not found: {path}')
    report = dict(schema=1, status='running', game_launched=False, client_modified=False,
                  scope='Production HDR Frame/Color and world/material shaders with controlled native MSAA inputs, projected effects and UI; composition graph, not client FPS.',
                  exclusions=['game simulation and game-thread draw recording', 'GI/shadow/AO/fog generation (composition consumes frozen inputs)', 'OS presentation', 'physical monitor transitions'],
                  cpu_sanitizers='Separate Linux/WSL test invocation; native Windows runs optimized and negative-control cases.',
                  source_sha256={}, runs=[], checks=[], errors=[])
    report_path = output/'validation.json'

    def save():
        report_path.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')

    def run(label, command, cwd=fp.REPO, timeout=300):
        started = time.monotonic()
        result = subprocess.run([str(x) for x in command], cwd=cwd, env=fp.zig_env(),
                                capture_output=True, text=True, errors='replace', timeout=timeout)
        (output/(label+'.log')).write_text(result.stdout+result.stderr, encoding='utf-8')
        report['checks'].append(dict(name=label, exit_code=result.returncode, seconds=round(time.monotonic()-started, 3)))
        save()
        print(label, 'PASS' if result.returncode == 0 else 'FAIL', flush=True)
        if result.returncode:
            raise RuntimeError(f'{label} failed; see {output/(label+".log")}\n{result.stdout[-1200:]}{result.stderr[-1200:]}')
        return result

    try:
        if not args.no_build:
            run('build', [sys.executable, fp.REPO/'scripts/build_renderer.py'])
        report['renderer_sha256'] = sha(fp.dll())
        for path in [fp.src('hdr_frame.h'), fp.src('hdr_quad.h'), fp.src('hdr_color.h'), fp.src('renderer.cpp'),
                     fp.SHADERS/'hdr_effects.hlsl', fp.SHADERS/'world_effects.hlsl', fp.TESTS/'hdr_pipeline_gpu.cpp']:
            report['source_sha256'][str(path.relative_to(fp.REPO))] = sha(path)
        for test in ['test_hdr_boundary.py', 'test_ui_boundary.py']:
            run(Path(test).stem, [sys.executable, fp.TESTS/test])
        binary = output/'hdr_pipeline_gpu.exe'
        run('compile-graph', [fp.zig(), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-fno-rtti', '-Wno-microsoft-exception-spec',
                              *fp.include_flags(), fp.TESTS/'hdr_pipeline_gpu.cpp', '-luser32', '-o', binary])
        # Match the game's address space for 4K readbacks and MSAA buffers.
        pe = bytearray(binary.read_bytes()); flags = struct.unpack_from('<I', pe, 0x3c)[0]+22
        struct.pack_into('<H', pe, flags, struct.unpack_from('<H', pe, flags)[0] | 0x20); binary.write_bytes(pe)
        report['graph_executable_sha256'] = sha(binary)
        for backend, source in backends.items():
            folder = output/backend; folder.mkdir(exist_ok=True)
            runtime = folder/'backend_d3d9.dll'; shutil.copy2(source, runtime)
            command = [binary, runtime]
            if args.capture_prefix:
                prefix = args.capture_prefix.resolve()
                report['capture_inputs'] = {str(prefix)+name: sha(str(prefix)+name) for name in ['scene.fgr', 'depth.fgr']}
                command.append(prefix)
            result = run('graph-'+backend, command, cwd=folder, timeout=180)
            events = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
            scenarios = [event for event in events if event.get('event') == 'scenario']
            if len(scenarios) < 9 or 'HDR_PIPELINE_GPU_PASS' not in result.stdout:
                raise RuntimeError(f'{backend}: incomplete graph result')
            for case in scenarios:
                limit = case['reference_gpu_ms']*(1+args.max_regression)+.03
                case['regression_limit_ms'] = limit
                # Every case contains native alpha-only UI masks; cases with
                # explicit decode also isolate their compatibility-layer cost.
                case['performance_applicable'] = True
                case['performance_pass'] = case['optimized_gpu_ms'] <= limit
                if case['performance_pass'] is False:
                    report['errors'].append(f'{backend}/{case["name"]}: GPU regression exceeds tolerance')
            report['runs'].append(dict(backend=backend, backend_sha256=sha(runtime), events=events))
            save()
        run('frame-acceptance', [sys.executable, fp.TESTS/'verify_hdr_frame_gpu.py',
                                 '--dxvk2', backends['dxvk2'], '--dxvk3', backends['dxvk3'], '--output', output/'frame'])
        run('sun-acceptance', [sys.executable, fp.TESTS/'verify_hdr_sun_gpu.py',
                               '--dxvk2', backends['dxvk2'], '--dxvk3', backends['dxvk3'], '--output', output/'sun'])
        run('proxy-acceptance', [sys.executable, fp.TESTS/'verify_hdr_integration_gpu.py',
                                 '--dxvk2', backends['dxvk2'], '--dxvk3', backends['dxvk3'],
                                 '--captured-ui', args.captured_ui.resolve() if args.captured_ui else output/'proxy/shaders/HdrFixturePS-3.bin',
                                 '--output', output/'proxy', '--modes', 'hdr', 'sdr', 'hdr-msaa8-triple'])
        report['status'] = 'failed' if report['errors'] else 'passed'
    except Exception as error:
        report['status'] = 'failed'; report['errors'].append(str(error)); save()
        raise
    save()
    lines = ['# Offline HDR validation', '', f'Status: **{report["status"]}**', '', report['scope'], '',
             '| Backend / case | SDR graph, ms | HDR reference, ms | Optimized HDR, ms | Draws saved | Max scRGB error |',
             '| --- | ---: | ---: | ---: | ---: | ---: |']
    for run_result in report['runs']:
        for case in run_result['events']:
            if case.get('event') == 'scenario':
                lines.append(f'| {run_result["backend"]} / {case["name"]} | {case["sdr_gpu_ms"]:.3f} | {case["reference_gpu_ms"]:.3f} | {case["optimized_gpu_ms"]:.3f} | {case["draws_reference"]-case["draws_optimized"]} | {case["max_abs_scrgb"]:.5f} |')
    lines += ['', 'Timing uses interleaved warmed GPU timestamp batches and reports CPU submission separately in validation.json.',
              'GPU elapsed intervals may include CPU submission stalls. These numbers do not predict client FPS.',
              'Checks include finite output, unchanged alpha, UI over sky/horizon, no warm resource allocations, fallback cases, SDR copies, CPU boundary tests and the production DLL proxy.', '',
              'Not covered: '+ '; '.join(report['exclusions'])+'.', '']
    (output/'report.md').write_text('\n'.join(lines), encoding='utf-8')
    print(report_path, flush=True)
    if report['errors']:
        raise SystemExit('\n'.join(report['errors']))


if __name__ == '__main__':
    main()
