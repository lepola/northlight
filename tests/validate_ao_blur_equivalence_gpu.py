#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Compare genuine old/new AOBlur bytecode on a hidden D3D9 device; no game."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import DLL, DLL_SHA256, COMPATIBLE_DLL_SHA256, validate_assembly
from validate_native_scene_trace import processes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    baseline = parser.add_mutually_exclusive_group(required=True)
    baseline.add_argument('--baseline-ref', help='Git revision containing shaders/effects.hlsl')
    baseline.add_argument('--baseline-dir', type=Path, help='Frozen shader source directory containing effects.hlsl')
    parser.add_argument('--backend', type=Path, required=True)
    parser.add_argument('--source', type=Path, default=ROOT / 'shaders/effects.hlsl')
    parser.add_argument('--output', type=Path, default=fp.out() / 'nonm2-ao/validation')
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--profile-split', action='store_true')
    args = parser.parse_args()
    assert os.name == 'nt', 'Windows D3D9 is required'
    processes()
    folder = args.output.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    assert hashlib.sha256(DLL.read_bytes()).hexdigest() in (DLL_SHA256, COMPATIBLE_DLL_SHA256)
    compiler = folder / 'compile_shaders.exe'
    executable = folder / 'ao-blur-equivalence.exe'
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2',
                    '-UNDEBUG', '-Wno-return-type', *fp.test_include_flags(),
                    *(['-DNORTHLIGHT_AO_SPLIT_PROFILE'] if args.profile_split else []), str(ROOT / 'tests/ao_blur_equivalence_gpu.cpp'),
                    '-luser32', '-o', str(executable)], env=fp.zig_env(), check=True)
    # The pinned compiler DLL is x64; the D3D9 fixture matches the x86 renderer.
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86_64-windows-gnu', '-O2', '-s', '-static',
                    str(ROOT / 'scripts/shaders/compile_shaders.cpp'), '-o', str(compiler)],
                   env=fp.zig_env(), check=True)
    shutil.copyfile(DLL, folder / DLL.name)
    if args.baseline_dir:
        old = (args.baseline_dir / 'effects.hlsl').read_bytes()
    else:
        old = subprocess.run(['git', 'show', f'{args.baseline_ref}:shaders/effects.hlsl'],
                             cwd=ROOT, capture_output=True, check=True).stdout
    sources = {'old': old, 'new': args.source.read_bytes()}
    metadata = {}
    for name, source in sources.items():
        path = folder / f'{name}.hlsl'
        path.write_bytes(source)
        binary = folder / f'{name}.bin'
        subprocess.run([str(compiler), str(path), 'AOBlur', 'ps_3_0', str(binary)], check=True)
        metadata[name] = {'source_sha256': hashlib.sha256(source).hexdigest(),
                          'shader_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                          **validate_assembly(folder / f'{name}.bin.asm', 'ps_3_0')}
    cases = []
    for bits in (16, 32):
        sizes = [(256, 192), (137, 79)] + ([(1920, 1080), (3840, 2160)] if args.benchmark and bits == 16 else [])
        for width, height in sizes:
            processes()
            result = subprocess.run([str(executable), str(folder / 'old.bin'), str(folder / 'new.bin'),
                                     str(args.backend.resolve()), str(width), str(height), str(bits),
                                     *(['--benchmark'] if args.benchmark else [])],
                                    cwd=folder, check=True, capture_output=True, text=True, timeout=120)
            print(result.stdout, end='', flush=True)
            if args.profile_split:
                assert 'AO_SPLIT_PROFILE_PASS' in result.stdout
            cases.extend(json.loads(line) for line in result.stdout.splitlines() if line.startswith('{'))
    assert all(row['changed_bytes'] == 0 and row['timings'] == args.benchmark for row in cases)
    report = {'baseline_ref': args.baseline_ref,
              'baseline_dir': str(args.baseline_dir.resolve()) if args.baseline_dir else None,
              'game_launched': False, 'benchmark': args.benchmark, 'profile_split': args.profile_split, 'metadata': metadata, 'cases': cases}
    (folder / 'validation.json').write_bytes((json.dumps(report, indent=2) + '\n').encode('utf-8'))
    print(f'PASS {len(cases)} genuine old/new AOBlur comparisons; all FP16 components bit exact')


if __name__ == '__main__':
    main()
