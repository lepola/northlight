#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Compile/run the production AO kernels on a separate hidden D3D9 device."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as fp


def main():
    if os.name != 'nt':
        raise SystemExit('This GPU validation requires Windows D3D9.')
    output = ROOT / 'out/ao-methods'
    output.mkdir(parents=True, exist_ok=True)
    executable = output / 'ao_methods_gpu.exe'
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17',
                    '-O2', '-UNDEBUG', '-Wno-return-type', *fp.include_flags(),
                    str(ROOT / 'tests/ao_methods_gpu.cpp'), '-luser32', '-o', str(executable)],
                   env=fp.zig_env(), check=True)
    backend = Path(os.environ['NORTHLIGHT_TEST_D3D9']) if os.environ.get('NORTHLIGHT_TEST_D3D9') else fp.client_root() / 'renderer-backends/dxvk2/dxvk2_d3d9.dll'
    subprocess.run([str(executable), str(fp.COMPILED), str(backend), str(output / 'gpu-validation.json')],
                   cwd=output, check=True, timeout=120)
    manifest = json.loads((fp.SHADERS / 'shader-build.json').read_text())
    for entry, digest in json.loads((ROOT / 'tests/fixtures/ao_baseline_sha256.json').read_text()).items():
        assert hashlib.sha256((fp.COMPILED / (entry + '.bin')).read_bytes()).hexdigest() == digest, entry + ' changed'
    for entry, data in manifest['shaders'].items():
        assert hashlib.sha256((fp.COMPILED / (entry + '.bin')).read_bytes()).hexdigest() == data['sha256']
    print('PASS approved SSAO, AOContactBloom, AOBlur and Composite bytecode hashes; manifest verified')


if __name__ == '__main__':
    main()
