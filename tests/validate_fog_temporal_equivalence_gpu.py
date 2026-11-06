#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Compare genuine baseline/candidate fog filters in a hidden D3D9 device, no game."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import DLL, DLL_SHA256, COMPATIBLE_DLL_SHA256, validate_assembly


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    baseline = parser.add_mutually_exclusive_group(required=True)
    baseline.add_argument('--baseline-ref', help='Git revision containing the reference shader sources')
    baseline.add_argument('--baseline-dir', type=Path, help='Frozen shaders directory containing world_effects.hlsl and auto_bump_normal.hlsl')
    parser.add_argument('--backend', type=Path, required=True)
    parser.add_argument('--source', type=Path, default=ROOT / 'shaders/world_effects.hlsl')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--entry', choices=('FogTemporal', 'FogBlur'), default='FogTemporal')
    parser.add_argument('--benchmark', action='store_true')
    parser.add_argument('--reuse-compiled', action='store_true')
    args = parser.parse_args()
    assert os.name == 'nt', 'Windows D3D9 is required'
    baseline_info = {'baseline_ref': args.baseline_ref,
                     'baseline_dir': str(args.baseline_dir.resolve()) if args.baseline_dir else None}
    if args.baseline_dir:
        for filename in ('world_effects.hlsl', 'auto_bump_normal.hlsl'):
            if not (args.baseline_dir / filename).is_file():
                parser.error(f'Baseline directory is missing {filename}: {args.baseline_dir}')
    folder = args.output.resolve()
    folder.mkdir(parents=True, exist_ok=True)
    compiler, executable = folder / 'compile_shaders.exe', folder / 'fog-temporal-equivalence.exe'
    metadata = {}
    distance_producer = None
    if args.entry == 'FogBlur':
        header = (ROOT / 'src/generated/world_compiled_shaders.h').read_bytes()
        array = re.search(rb'static const DWORD kTemporalLightShader\[\]\s*=\s*\{([^}]+)\}', header)
        assert array, 'Production TemporalLight bytecode is missing from the generated header'
        code = b''.join(struct.pack('<I', int(word, 16)) for word in re.findall(rb'0x[0-9a-fA-F]+', array[1]))
        assert code == (fp.COMPILED / 'TemporalLight.bin').read_bytes(), 'Production TemporalLight header/bin mismatch'
        distance_producer = {'entry': 'TemporalLight', 'history_weight': 0, 'target': 'COLOR1/R32F',
                             'shader_sha256': hashlib.sha256(code).hexdigest(),
                             'generated_header_sha256': hashlib.sha256(header).hexdigest()}
    if not args.reuse_compiled:
        assert hashlib.sha256(DLL.read_bytes()).hexdigest() in (DLL_SHA256, COMPATIBLE_DLL_SHA256)
        subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                        '-Wno-return-type', *fp.test_include_flags(), str(ROOT / 'tests/fog_temporal_equivalence_gpu.cpp'),
                        '-luser32', '-o', str(executable)], env=fp.zig_env(), check=True)
        subprocess.run([str(fp.zig()), 'c++', '-target', 'x86_64-windows-gnu', '-O2', '-s', '-static',
                        str(ROOT / 'scripts/shaders/compile_shaders.cpp'), '-o', str(compiler)], env=fp.zig_env(), check=True)
        shutil.copyfile(DLL, folder / DLL.name)
        for name in ('old', 'new'):
            source_folder = folder / name
            source_folder.mkdir(exist_ok=True)
            for filename in ('world_effects.hlsl', 'auto_bump_normal.hlsl'):
                if name == 'old':
                    data = ((args.baseline_dir / filename).read_bytes() if args.baseline_dir else
                            subprocess.check_output(['git', 'show', f'{args.baseline_ref}:shaders/{filename}'], cwd=ROOT))
                else:
                    data = (args.source if filename == 'world_effects.hlsl' else ROOT / 'shaders' / filename).read_bytes()
                (source_folder / filename).write_bytes(data)
            binary = source_folder / f'{args.entry}.bin'
            subprocess.run([str(compiler), str(source_folder / 'world_effects.hlsl'), args.entry, 'ps_3_0', str(binary)], check=True)
            metadata[name] = {'shader_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                              'source_sha256': hashlib.sha256((source_folder / 'world_effects.hlsl').read_bytes()).hexdigest(),
                              'bump_include_sha256': hashlib.sha256((source_folder / 'auto_bump_normal.hlsl').read_bytes()).hexdigest(),
                              **validate_assembly(Path(str(binary) + '.asm'), 'ps_3_0')}
    else:
        previous = json.loads((folder / 'validation.json').read_text(encoding='utf-8'))
        assert previous.get('entry', 'FogTemporal') == args.entry
        assert all(previous.get(key) == value for key, value in baseline_info.items()), 'Reused baseline differs from the requested baseline'
        assert previous.get('distance_producer') == distance_producer, 'Reused executable has different production distance bytecode'
        metadata = previous['metadata']
    cases, failures = [], []
    configs = [(w, h, ib, ob) for w, h in ((256, 192), (137, 79)) for ib in (16, 32) for ob in (16, 32)]
    if args.benchmark:
        configs += [(3840, 2160, 16, 16)]
    for width, height, input_bits, output_bits in configs:
        result = subprocess.run([str(executable), str(folder / 'old' / f'{args.entry}.bin'), str(folder / 'new' / f'{args.entry}.bin'),
                                 str(args.backend.resolve()), str(width), str(height), str(input_bits), str(output_bits), args.entry],
                                cwd=folder, capture_output=True, text=True, timeout=180)
        print(result.stdout, end='', flush=True)
        cases.extend(json.loads(line) for line in result.stdout.splitlines() if line.startswith('{'))
        (folder / 'validation.json').write_bytes((json.dumps({**baseline_info, 'entry': args.entry, 'game_launched': False,
            'distance_producer': distance_producer, 'metadata': metadata, 'cases': cases}, indent=2) + '\n').encode('utf-8'))
        if result.returncode:
            print(result.stderr, file=sys.stderr)
            failures.append((width, height, input_bits, output_bits, result.returncode))
    assert not failures, failures
    assert cases and all(row['changed_bytes'] == 0 for row in cases)
    print(f'PASS {len(cases)} bit-exact old/new {args.entry} images, including FP16, HDR, signed zero and nonfinite stress')


if __name__ == '__main__':
    main()
