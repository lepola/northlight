#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Exact compatibility-layer parity and optional isolated sparse-layer timing."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from shader_compiler import Compiler

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--output', type=Path, default=fp.output_dir()/'hdr-layer-sparse')
parser.add_argument('--benchmark', action='store_true')
parser.add_argument('--coverage-only', action='store_true')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
compiler = Compiler(allow_compatible_binary=True)
code, _ = compiler.compile(fp.TESTS/'hdr_layer_reference.hlsl', 'HdrLayerReference', 'ps_3_0', out/'reference.bin')
words = struct.unpack('<%dI' % (len(code)//4), code)
(out/'hdr_layer_reference.h').write_text('static const std::uint32_t kHdrLayerReference[]={'+','.join(hex(w) for w in words)+'};\n')
exe = out/'hdr-layer-sparse.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-Wno-return-type', '-Wno-microsoft-exception-spec', '-I'+str(out), *fp.include_flags(), str(fp.TESTS/'hdr_layer_sparse_gpu.cpp'),
                '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
pe = bytearray(exe.read_bytes()); flags = struct.unpack_from('<I', pe, 0x3c)[0]+22
struct.pack_into('<H', pe, flags, struct.unpack_from('<H', pe, flags)[0] | 0x20); exe.write_bytes(pe)
backends = [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll', fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll', fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']
for index, backend in enumerate(backends):
    for samples in (0, 2):
        command = [str(exe), str(backend), str(samples)]
        if args.coverage_only:
            command.append('--coverage-only')
        elif args.benchmark and index and samples == 2:
            command.append('--benchmark')
        run = subprocess.run(command, cwd=out, capture_output=True, text=True, timeout=180)
        (out/f'backend-{index}-msaa-{samples}.log').write_text(run.stdout+run.stderr)
        print(backend, run.stdout+run.stderr, end='', flush=True)
        run.check_returncode()
        assert ('HDR_LAYER_SPARSE_WORK_ONLY_PASS' if args.coverage_only else 'HDR_LAYER_SPARSE_GPU_PASS') in run.stdout
