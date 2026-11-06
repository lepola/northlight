#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Bounded BLP loop, live-thread installation, rollback and optional local-client oracle."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--client', type=Path, help='read-only local x86 12340 executable; never launched')
parser.add_argument('--samples', type=Path, help='directory of local BLP samples')
parser.add_argument('--dll', type=Path, help='also check production proxy startup/reload and offscreen D3D9')
parser.add_argument('--backend-dll', type=Path, help='local DXVK runtime for the optional GPU check')
parser.add_argument('--backend', choices=['dxvk', 'dxvk2'], default='dxvk')
args = parser.parse_args()
if bool(args.client) != bool(args.samples):
    parser.error('--client and --samples must be provided together')
if args.dll and not args.client:
    parser.error('--dll requires --client and --samples')
if args.dll and not args.backend_dll:
    parser.error('--dll requires --backend-dll (native D3D9 may lack RESZ)')
samples = sorted(args.samples.glob('*.blp')) if args.samples else []
if args.samples and not samples:
    parser.error('no BLP samples')
inputs = [args.client, *samples] if args.client else []
if args.dll:
    inputs.append(args.dll)
    inputs.append(args.backend_dll)
before = {str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
out = fp.output_dir()
report = {'client': str(args.client) if args.client else None, 'samples': len(samples), 'runs': []}
env = os.environ.copy()
if args.client:
    oracle = out / 'blp-guard-oracle.dll'
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2',
                    '-shared', '-Wall', '-Wextra', '-Werror', *fp.include_flags(),
                    str(fp.TESTS / 'blp_mip_guard_module.cpp'), '-o', str(oracle)], env=fp.zig_env(), check=True)
    env['NORTHLIGHT_BLP_ORACLE_MODULE'] = str(oracle.resolve())
if args.dll:
    proxy = out / 'd3d9.dll'
    shutil.copy2(args.dll, proxy)
    runtime = out / 'renderer-backends' / args.backend / (args.backend + '_d3d9.dll')
    runtime.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(args.backend_dll, runtime)
    (out / 'northlight-renderer.ini').write_text(f'[Renderer]\nBackend={args.backend}\nBlpMipGuard=1\nCommandStream=0\nExperimentalHDR=0\nSettingsOverlay=0\n')
    (out / 'dxvk.conf').write_text('dxvk.allowFse = False\n')
    env.update(DXVK_LOG_PATH=str(out.resolve()), DXVK_CONFIG_FILE=str((out / 'dxvk.conf').resolve()), DXVK_CONFIG='')
    env['NORTHLIGHT_BLP_PROXY_DLL'] = str(proxy.resolve())
    report['proxy_sha256'] = hashlib.sha256(proxy.read_bytes()).hexdigest()
    report['backend'] = args.backend
    report['backend_sha256'] = hashlib.sha256(runtime.read_bytes()).hexdigest()


for mode in ['-O0', '-O2']:
    exe = out / ('blp-mip-guard' + mode + '.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', mode,
                    '-UNDEBUG', '-Wall', '-Wextra', '-Werror', *fp.test_include_flags(),
                    str(fp.TESTS / 'blp_mip_guard.cpp'), '-Wl,--image-base,0x400000,--stack,1048576',
                    '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
    # Our stand reserves scratch space at the audited VAs in its own PE image.
    # Disable ASLR on this freshly compiled test EXE only; Wow.exe stays read-only.
    test_image = bytearray(exe.read_bytes())
    optional = struct.unpack_from('<I', test_image, 0x3c)[0] + 24
    flags = struct.unpack_from('<H', test_image, optional + 70)[0]
    struct.pack_into('<H', test_image, optional + 70, flags & ~0x40)
    exe.write_bytes(test_image)
    result = subprocess.run([str(exe), *map(str, [args.client, *samples] if args.client else [])],
                            env=env, capture_output=True, text=True, timeout=60)
    (out / ('blp-mip-guard' + mode + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
    report['runs'].append({'mode': mode, 'exit': result.returncode})
    (out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(result.stdout, end='', flush=True)
    if result.stderr:
        print(result.stderr, file=sys.stderr)
    result.check_returncode()
    if args.dll:
        assert result.stdout.count('gpuReadbackExact=1') == 3
    if args.client:
        assert 'BLP_GUARD_UNLOAD_PASS moduleResident=0 clientCodeCallable=1' in result.stdout
assert before == {str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest() for path in inputs}
report['input_hashes_unchanged'] = before
(out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print('PASS: ' + str(out / 'validation.json'))
