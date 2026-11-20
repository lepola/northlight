#!/usr/bin/env python3
"""Manual DXVK3 receiver oracle from the actual HLSL functions; no game resources or window shown."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp


def function(source, name):
    start = source.index('float ' + name + '(')
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


def main():
    backend = os.environ['NORTHLIGHT_DXVK_D3D9']
    source = (fp.SHADERS / 'world_effects.hlsl').read_text()
    header = '''float4 ImageClip:register(c0);float4 Projection:register(c1);float4 DepthParams:register(c2);float4 WaterInfo:register(c30);
sampler2D Depth:register(s1);sampler2D WaterMask:register(s11);\n'''
    body = '\n'.join(function(source, name) for name in ('viewDistance', 'waterDistance', 'receiverDistance', 'fogReceiverDistance'))
    oracle = '''float4 WaterFogOracle(float2 uv:TEXCOORD0):COLOR0 {
float d=tex2Dlod(Depth,float4(uv,0,0)).r;
return float4(fogReceiverDistance(uv,d),receiverDistance(uv,d),receiverDistance(uv,min(d,.99999)),viewDistance(min(d,.99999)));}
'''
    with tempfile.TemporaryDirectory(prefix='northlight-water-fog-gpu-') as temporary:
        root = Path(temporary)
        hlsl, binary, shader = root / 'receiver.hlsl', root / 'oracle.exe', root / 'oracle.bin'
        hlsl.write_text(header + body + '\n' + oracle)
        subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                        *fp.test_include_flags(),str(fp.TESTS / 'water_fog_endpoint_gpu.cpp'), '-luser32', '-o', str(binary)], env=fp.zig_env(), check=True)
        compiler = root / 'compile_shaders.exe'
        subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', str(fp.REPO / 'scripts/shaders/compile_shaders.cpp'), '-ladvapi32', '-o', str(compiler)], env=fp.zig_env(), check=True)
        subprocess.run([str(compiler), str(hlsl), 'WaterFogOracle', 'ps_3_0', str(shader), '--native'], check=True)
        subprocess.run([str(binary), str(shader), backend], cwd=root, check=True, timeout=30)


if __name__ == '__main__':
    main()
