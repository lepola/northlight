#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""Run an independent hidden D3D9 stencil oracle; --compile-only performs CPU compilation only."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-gpu', action='store_true', help='Explicitly create and exercise a hidden D3D9 device')
    parser.add_argument('--compile-only', action='store_true', help='Cross-compile without starting a graphics device')
    parser.add_argument('--verified-msaa-mrt', action='store_true', help='Allow an occupied MSAA MRT only for an explicitly verified backend')
    parser.add_argument('--backend', default='system', help='system or an explicit backend DLL path; never auto-loads the installed game proxy')
    parser.add_argument('--corpus', type=Path, help='Optional directory of extracted stock PS .bin files, read only')
    parser.add_argument('--stock-limit', type=int, default=0, help='Limit stock shaders; zero exercises every supplied PS2/PS3 program')
    parser.add_argument('--timeout', type=int, default=120, help='GPU child-process timeout in seconds')
    parser.add_argument('--case', help='Run only synthetic names containing this text')
    parser.add_argument('--benchmark', action='store_true', help='Run short paired warm submission/timestamp measurements instead of correctness cases')
    args = parser.parse_args()
    out = fp.output_dir()
    executable = out / 'ao_actor_stencil_gpu.exe'
    source = fp.TESTS / 'ao_actor_stencil_gpu.cpp'
    command = [str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
               '-Wall', '-Wextra', '-Werror', *fp.include_flags(), str(source), '-luser32', '-o', str(executable)]
    subprocess.run(command, env=fp.zig_env(), check=True)
    sources = [source, fp.src('ao_actor_stencil.h'), Path(__file__).resolve()]
    evidence = {'gpu_executed': False, 'game_launched': False, 'compile_command': command,
                'source_sha256': {str(p.relative_to(fp.REPO)): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}}
    build_report = out / 'ao_actor_stencil_gpu_build.json'
    build_report.write_text(json.dumps(evidence, indent=2) + '\n')
    if not args.compile_only or args.run_gpu:
        assert os.name == 'nt', '--run-gpu requires Windows; compile-only works with a configured cross compiler'
        report = out / 'ao_actor_stencil_gpu.json'
        backend = args.backend if args.backend == 'system' else str(Path(args.backend).resolve())
        run = [str(executable), '--run-gpu', backend, str(report)]
        if args.corpus:
            files = [p.resolve() for p in sorted(args.corpus.rglob('*.bin'))
                     if p.read_bytes()[:4] in (b'\x00\x02\xff\xff', b'\x00\x03\xff\xff')]
            assert files, args.corpus
            if args.stock_limit:
                assert args.stock_limit > 0
                selected = min(args.stock_limit, len(files))
                files = [files[i * len(files) // selected] for i in range(selected)]
            listing = out / 'ao_actor_stencil_gpu_stock_files.txt'
            listing.write_text(''.join(str(p) + '\n' for p in files), encoding='utf-8')
            evidence['stock_sha256'] = {str(p.relative_to(args.corpus.resolve())): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
            run.append(str(listing))
        environment = dict(os.environ, DXVK_LOG_PATH=str(out), DXVK_STATE_CACHE_PATH=str(out))
        if args.case:
            environment['NORTHLIGHT_AO_ACTOR_MASK_GPU_CASE'] = args.case
        else:
            environment.pop('NORTHLIGHT_AO_ACTOR_MASK_GPU_CASE', None)
        environment['NORTHLIGHT_AO_ACTOR_MASK_GPU_MSAA'] = '1' if args.verified_msaa_mrt else '0'
        environment['NORTHLIGHT_AO_ACTOR_MASK_GPU_BENCH'] = '1' if args.benchmark else '0'
        try:
            execution = subprocess.run(run, cwd=out, capture_output=True, text=True, timeout=args.timeout, creationflags=subprocess.CREATE_NO_WINDOW, env=environment)
        except subprocess.TimeoutExpired as failure:
            evidence.update(gpu_executed=True, timeout=True, stdout=str(failure.stdout), stderr=str(failure.stderr))
            build_report.write_text(json.dumps(evidence, indent=2) + '\n')
            raise
        print(execution.stdout + execution.stderr, end='', flush=True)
        evidence.update(gpu_executed=True, returncode=execution.returncode, stdout=execution.stdout, stderr=execution.stderr)
        build_report.write_text(json.dumps(evidence, indent=2) + '\n')
        execution.check_returncode()
        result = json.loads(report.read_text())
        assert result['failures'] == 0, result
        if args.benchmark:
            assert len(result['benchmarks']) == 2
            evidence.update(gpu_executed=True, gpu_report=str(report), benchmark=result['benchmarks'])
            print('PASS paired warm synthetic benchmark; measurements are not in-game FPS')
        else:
            assert result['case_count'] >= (1 if args.case else 50), result
            assert all(case['pass'] for case in result['cases'] if case['status'] != 'UNAVAILABLE'), result
            assert result['unavailable'] == sum(case['status'] == 'UNAVAILABLE' for case in result['cases'])
            evidence.update(gpu_executed=True, gpu_report=str(report), cases=result['case_count'])
            print('PASS hidden-device AO actor-stencil GPU oracle:', result['passed'], 'passed;', result['unavailable'], 'explicitly unavailable native controls')
    else:
        print('COMPILE ONLY: AO actor-stencil GPU oracle built; no graphics library/device, GPU, visible window or game was started.')
    build_report.write_text(json.dumps(evidence, indent=2) + '\n')


if __name__ == '__main__':
    main()
