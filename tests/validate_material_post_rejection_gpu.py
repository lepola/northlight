#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Genuine previous-commit material consumers, production textures, and exact old/new pixels."""
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

ENTRIES = ('MaterialComposite', 'WorldLighting', 'LocalDirect', 'LocalDirect16',
           'MaterialAudit', 'LocalLighting', 'BumpHelper')
UNCHANGED = ('WorldComposite', 'WorldFog', 'FogClouds', 'FogTemporal', 'TemporalLight')
HELPER = b'''
float4 BumpHelper(float2 uv:TEXCOORD0):COLOR0 {
    float2 q=depthUV(uv);float z=viewDistance(normalizedDepth(q));
    float3 n=tex2Dlod(NormalBuffer,float4(uv,0,0)).xyz;
    return float4(autoBumpNormal(q,0,n,z,InverseView[0].xyz,InverseView[1].xyz,InverseView[2].xyz),1);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-ref', required=True)
    parser.add_argument('--backend', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=fp.out() / 'rotation-post/reproducible')
    parser.add_argument('--benchmark', action='store_true')
    args = parser.parse_args()
    assert os.name == 'nt', 'Windows D3D9 is required'
    folder = args.output.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    assert hashlib.sha256(DLL.read_bytes()).hexdigest() in (DLL_SHA256, COMPATIBLE_DLL_SHA256)
    compiler, executable = folder / 'compile_shaders.exe', folder / 'material-post.exe'
    for source, target, arch, extra in [
        (ROOT / 'scripts/shaders/compile_shaders.cpp', compiler, 'x86_64-windows-gnu', ['-s', '-static']),
        (ROOT / 'tests/material_post_rejection_gpu.cpp', executable, 'x86-windows-gnu', ['-UNDEBUG', '-Wno-return-type', '-luser32'])]:
        subprocess.run([str(fp.zig()), 'c++', '-target', arch, '-std=c++17', '-O2', str(source),
                        *extra, '-o', str(target)], env=fp.zig_env(), check=True)
    shutil.copyfile(DLL, folder / DLL.name)
    metadata = {}
    for version in ('old', 'new'):
        directory = folder / version
        directory.mkdir(exist_ok=True)
        metadata[version] = {'sources': {}, 'shaders': {}}
        for name in ('world_effects.hlsl', 'local_light_effects.hlsl', 'auto_bump_normal.hlsl'):
            data = subprocess.run(['git', 'show', f'{args.baseline_ref}:shaders/{name}'], cwd=ROOT,
                                  capture_output=True, check=True).stdout if version == 'old' else (ROOT / 'shaders' / name).read_bytes()
            (directory / name).write_bytes(data)
            metadata[version]['sources'][name] = hashlib.sha256(data).hexdigest()
        (directory / 'helper.hlsl').write_bytes((directory / 'world_effects.hlsl').read_bytes() + HELPER)
        for entry in ENTRIES + UNCHANGED:
            source = directory / ('local_light_effects.hlsl' if entry == 'LocalLighting' else 'helper.hlsl' if entry == 'BumpHelper' else 'world_effects.hlsl')
            binary = directory / f'{entry}.bin'
            subprocess.run([str(compiler), str(source), entry, 'ps_3_0', str(binary)], check=True)
            stats = validate_assembly(directory / f'{entry}.bin.asm', 'ps_3_0')
            # Four cube fetches add three slots each beyond the shared 2D accounting.
            if entry == 'LocalLighting':
                stats['static_instruction_slots'] += 12
                assert stats['static_instruction_slots'] <= 512
            metadata[version]['shaders'][entry] = {'sha256': hashlib.sha256(binary.read_bytes()).hexdigest(), **stats}
    assert all(metadata['old']['shaders'][entry]['sha256'] == metadata['new']['shaders'][entry]['sha256'] for entry in UNCHANGED)
    cases = []

    def run(entry, width, height, samples=0, repeat=None):
        result = subprocess.run([str(executable), str(folder), entry, str(args.backend.resolve()),
                                 str(width), str(height), str(samples)], cwd=folder,
                                capture_output=True, text=True, check=True, timeout=120)
        rows = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{')]
        assert rows and all(row['changed_bytes'] == 0 and row['nonfinite'] == 0 for row in rows)
        cases.extend(dict(row, **({'repeat': repeat} if repeat is not None else {})) for row in rows)
        print(entry, width, height, 'MSAA', samples, 'PASS', len(rows), flush=True)

    for width, height in ((128, 96), (137, 79)):
        for entry in ENTRIES:
            for samples in ((0, 2, 4, 8) if entry == 'MaterialComposite' else (0,)):
                run(entry, width, height, samples)
    if args.benchmark:
        for repeat in range(3):
            for width, height in ((1920, 1080), (3840, 2160)):
                run('MaterialComposite', width, height, repeat=repeat)
    report = {'baseline_ref': args.baseline_ref, 'game_launched': False, 'metadata': metadata, 'cases': cases}
    (folder / 'validation.json').write_bytes((json.dumps(report, indent=2) + '\n').encode('utf-8'))
    print(f'PASS {len(cases)} exact consumer/helper comparisons; five unrelated shaders unchanged')


if __name__ == '__main__':
    main()
