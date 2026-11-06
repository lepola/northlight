#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client,stormlib
"""Replay native transparent draws through production HDR sky ownership, offline."""
import argparse
import hashlib
import json
import os
import struct
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--renderer', type=Path, default=fp.src('renderer.cpp'))
parser.add_argument('--output', type=Path, default=fp.output_dir()/'smoke-sky')
parser.add_argument('--benchmark', action='store_true')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
renderer = args.renderer.read_text()
start = renderer.index('    bool hdrWorldAlphaDraw(')
end = renderer.index('    template<class Draw> HRESULT projectedDraw(', start)
methods = renderer[start:end]
start = renderer.index('    template<class Draw> void hdrReplaySky(')
end = renderer.index('    template<class Capture,class Draw> HRESULT drawHook(', start)
(out/'smoke_sky_methods.inc').write_text(methods+renderer[start:end])
fp.use_source_modules()
from world_scene_builder import Assets
from extract_signatures import variants
from shader_compiler import Compiler
compiler = Compiler(allow_compatible_binary=True)
arrays, entries, provenance = [], [], []
def array(name, code):
    words = struct.unpack('<%dI' % (len(code)//4), code)
    arrays.append('static const DWORD '+name+'[]={'+','.join(hex(w) for w in words)+'};\n')
for major in (3,):
    code, _ = compiler.compile(fp.TESTS/'hdr_foreground_fixture.hlsl', 'ForegroundVS', f'vs_{major}_0', out/f'foreground-vs{major}.bin')
    array(f'kForegroundVs{major}', code)
assets = Assets(archives='stock')
try:
    for major in (3,):
        path = f'shaders\\pixel\\ps_{major}_0\\Combiners_Mod_Mod.bls'
        for index, _, code in variants(assets.read(path)):
            name = f'kForegroundPs{major}_{index}'
            array(name, code)
            entries.append('{'+f'{major},{index},{name},sizeof({name})/4'+'}')
            provenance.append(dict(path=path, index=index, sha256=hashlib.sha256(code).hexdigest()))
finally:
    assets.close()
(out/'smoke_sky_native.h').write_text(''.join(arrays)+'struct NativeForeground{unsigned major,index;const DWORD* code;size_t count;};\nstatic const NativeForeground kNativeForeground[]={'+','.join(entries)+'};\n')
(out/'native-shaders.json').write_text(json.dumps(provenance, indent=2)+'\n')
assert renderer.count('effectCoverageDraw([&]{return ext->Draw') == 4
assert 'owner.effectCoverageDraw([&]{return args.submit(device);},true)' in renderer
exe = out/'smoke-sky.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                '-Wno-return-type', '-Wno-microsoft-exception-spec', *fp.include_flags(), '-I'+str(out), str(fp.TESTS/'smoke_sky_gpu.cpp'),
                '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
pe = bytearray(exe.read_bytes()); flags = struct.unpack_from('<I', pe, 0x3c)[0]+22
struct.pack_into('<H', pe, flags, struct.unpack_from('<H', pe, flags)[0] | 0x20); exe.write_bytes(pe)
backends = [Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll',
            fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll',
            fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll']
for index, backend in enumerate(backends):
    for samples in ((0,) if index == 0 else (0, 2, 8)):
        for draws in ((40, 220) if args.benchmark and index and samples == 2 else (0,)):
            command = [str(exe), str(backend), str(samples)]
            if draws:
                command.append(str(draws))
            run = subprocess.run(command, cwd=out, text=True, capture_output=True, timeout=90)
            (out/f'backend-{index}-msaa-{samples}-draws-{draws}.log').write_text(run.stdout+run.stderr)
            print(backend, run.stdout+run.stderr, end='', flush=True)
            run.check_returncode()
            assert 'SMOKE_SKY_GPU_PASS' in run.stdout
