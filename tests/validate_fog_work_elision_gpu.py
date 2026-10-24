#!/usr/bin/env python3
"""Offscreen old/new fog oracle: exact FP16 images, alternating GPU timestamps, no game."""
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
    parser.add_argument('--backend', default=os.environ.get('NORTHLIGHT_DXVK_D3D9'), required=not os.environ.get('NORTHLIGHT_DXVK_D3D9'))
    parser.add_argument('--prefix', required=True, help='Saved capture prefix, ending in capture-N-')
    parser.add_argument('--baseline-ref', required=True)
    parser.add_argument('--output', type=Path, default=fp.out() / 'fog-work-elision')
    parser.add_argument('--cases', nargs='+', type=int, default=list(range(13)))
    parser.add_argument('--compile-only', action='store_true')
    parser.add_argument('--reuse-compiled', action='store_true')
    parser.add_argument('--compatible-compiler', action='store_true')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('Requires Windows and a standalone D3D9 backend')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(fp.SCRIPTS / 'shaders'))
    from shader_compiler import Compiler, validate_assembly
    manifest = {'baseline_ref': args.baseline_ref, 'game_launched': False, 'shaders': {}, 'checks': []}
    if not args.reuse_compiled:
        # Keep the compiler helper private so parallel shader checks cannot replace its executable/DLL.
        os.environ['NORTHLIGHT_OUT'] = str(output / 'compiler')
        compiler = Compiler(allow_compatible_binary=args.compatible_compiler)
        manifest['compiler'] = compiler.provenance
        for variant in ('old', 'new'):
            folder = output / variant
            folder.mkdir(exist_ok=True)
            for name in ('world_effects.hlsl', 'auto_bump_normal.hlsl'):
                data = subprocess.check_output(['git', 'show', f'{args.baseline_ref}:shaders/{name}'], cwd=fp.REPO) if variant == 'old' else (fp.SHADERS / name).read_bytes()
                (folder / name).write_bytes(data)
            for entry in ('WorldFog', 'FogClouds'):
                binary = folder / f'{entry}.bin'
                compiler.compile(folder / 'world_effects.hlsl', entry, 'ps_3_0', binary)
                manifest['shaders'][f'{variant}/{entry}'] = {'sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), **validate_assembly(Path(str(binary) + '.asm'), 'ps_3_0')}
        subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', *fp.test_include_flags(), str(fp.TESTS / 'world_pass_gpu.cpp'), '-luser32', '-o', str(output / 'replay.exe')], env=fp.zig_env(), check=True)
        (output / 'validation.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    else:
        manifest = json.loads((output / 'validation.json').read_text(encoding='utf-8'))
        manifest['checks'] = []
    if args.compile_only:
        return
    for entry in ('WorldFog', 'FogClouds'):
        for scenario in args.cases:
            for reverse in (False, True):
                shaders = [output / 'old' / f'{entry}.bin', output / 'new' / f'{entry}.bin']
                if reverse:
                    shaders.reverse()
                result = subprocess.run([str(output / 'replay.exe'), args.prefix, *map(str, shaders), entry, args.backend, str(scenario)], capture_output=True, text=True, check=True, cwd=output)
                row = json.loads(next(line for line in result.stdout.splitlines() if line.startswith('{')))
                assert row['changed'] == 0 and row['max_abs'] == 0 and row['nonfinite'] == 0, row
                old_ms, new_ms = (row['new_ms'], row['old_ms']) if reverse else (row['old_ms'], row['new_ms'])
                row.update(scenario=scenario, reverse=reverse, old_ms=old_ms, new_ms=new_ms, ratio=new_ms / old_ms)
                manifest['checks'].append(row)
                (output / 'validation.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
                print(json.dumps(row), flush=True)
    print(f'PASS {len(manifest["checks"])} bit-exact fog comparisons; no game launched')


if __name__ == '__main__':
    main()
