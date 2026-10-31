#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Compare material ray output and GPU work against the full-work reference paths."""
from pathlib import Path
import argparse
import json
import os
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0, str(fp.REPO / 'scripts/shaders'))
from shader_compiler import Compiler, validate_assembly

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--compatible-compiler", action="store_true")
args = parser.parse_args()
out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
source = (fp.SHADERS / 'auto_parallax.hlsl').read_text()
# The reference completes every shadow tap, including fully blocked rays.
reference = '''
float2 referenceUV(sampler2D map,float2 uv,float distance,float4 control,MaterialFrame f) {
    float fade=parallaxFade(distance,f.tx,f.ty,control.y);
    if(control.x<=0||fade<=0)return uv;
    float3 view=float3(dot(-f.p,f.tangent),dot(-f.p,f.bitangent),abs(dot(-f.p,f.normal)));
    float steps=clamp(control.z,8,32),stepDepth=1/steps;
    float2 rayUV=clamp(view.xy/max(view.z,.15*length(f.p)),-4,4)*control.x*fade;
    float2 stepUV=rayUV*stepDepth;
    float depth=0;float2 sampleUV=uv+rayUV*.5;
    float sampled=1-tex2Dgrad(map,sampleUV,f.tx,f.ty).b;
    [loop]for(int i=0;i<32&&i<steps&&depth<sampled;++i){sampleUV-=stepUV;depth+=stepDepth;sampled=1-tex2Dgrad(map,sampleUV,f.tx,f.ty).b;}
    float previous=1-tex2Dgrad(map,sampleUV+stepUV,f.tx,f.ty).b-(depth-stepDepth);
    float after=sampled-depth;
    return sampleUV+stepUV*saturate(after/min(after-previous,-.00001));
}
float referenceShadow(sampler2D map,float2 uv,float height,float distance,MaterialFrame f,float4 control,float4 light,float steps) {
    float3 ray=float3(dot(light.xyz,f.tangent),dot(light.xyz,f.bitangent),dot(light.xyz,f.normal));
    float fade=parallaxFade(distance,f.tx,f.ty,control.y);
    if(control.x<=0||light.w<=0||ray.z<=.04||fade<=0)return 1;
    float span=1-height;
    float2 extent=clamp(ray.xy/max(ray.z,.08),-6,6)*control.x*span*fade;
    float blocked=0;
    [loop]for(int i=1;i<=16&&i<=steps;++i){
        float part=i/steps;
        float h=tex2Dgrad(map,uv+extent*part,f.tx,f.ty).b;
        blocked=max(blocked,saturate((h-height-span*part-.012)*16));
    }
    return 1-blocked;
}
'''
fixture = '''
sampler2D HeightMap:register(s0);
float4 Projection:register(c0);
float4 Control:register(c1);
float4 ShadowControl:register(c2);
float4 Ray(float2 uv:TEXCOORD0,float4 p:TEXCOORD1):COLOR0 {
    return float4(parallaxUV(HeightMap,uv,p,Projection,Control),0,1);
}
float4 ReferenceRay(float2 uv:TEXCOORD0,float4 p:TEXCOORD1):COLOR0 {
    MaterialFrame f=materialFrame(uv,p,Projection);
    return float4(referenceUV(HeightMap,uv,p.w,Control,f),0,1);
}
float4 Shadow(float2 uv:TEXCOORD0,float4 p:TEXCOORD1):COLOR0 {
    MaterialFrame f=materialFrame(uv,p,Projection);
    return parallaxShadow(HeightMap,uv,ShadowControl.x,p.w,f,Control,float4(.96,0,.28,1),ShadowControl.y);
}
float4 ReferenceShadow(float2 uv:TEXCOORD0,float4 p:TEXCOORD1):COLOR0 {
    MaterialFrame f=materialFrame(uv,p,Projection);
    return referenceShadow(HeightMap,uv,ShadowControl.x,p.w,f,Control,float4(.96,0,.28,1),ShadowControl.y);
}
void selectZeroIteration(float2 uv,float4 p) {
    MaterialFrame f=materialFrame(uv,p,Projection);
    float3 view=float3(dot(-f.p,f.tangent),dot(-f.p,f.bitangent),abs(dot(-f.p,f.normal)));
    float2 rayUV=clamp(view.xy/max(view.z,.15*length(f.p)),-4,4)*Control.x*parallaxFade(p.w,f.tx,f.ty,Control.y);
    float2 start=uv+rayUV*.5,stepUV=rayUV/clamp(Control.z,8,32);
    float initial=tex2Dgrad(HeightMap,start,f.tx,f.ty).b;
    float prior=tex2Dgrad(HeightMap,start+stepUV,f.tx,f.ty).b;
    clip(initial-.999999);
    clip(abs(initial-prior)-.001);
}
float4 ZeroRay(float2 uv:TEXCOORD0,float4 p:TEXCOORD1):COLOR0 {selectZeroIteration(uv,p);return Ray(uv,p);}
float4 ReferenceZeroRay(float2 uv:TEXCOORD0,float4 p:TEXCOORD1):COLOR0 {selectZeroIteration(uv,p);return ReferenceRay(uv,p);}
'''
path = out / 'material-rays.hlsl'
compiler = Compiler(allow_compatible_binary=args.compatible_compiler)
exe = out / 'material-ray-work.exe'
subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-O2', '-UNDEBUG', '-std=c++17',
                *fp.test_include_flags(), str(fp.TESTS / 'material_ray_work_gpu.cpp'), '-luser32', '-o', str(exe)],
               env=fp.zig_env(), check=True)
backend = os.environ.get('NORTHLIGHT_TEST_D3D9', str(Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'SysWOW64/d3d9.dll'))
for capture in (False, True):
    prefix = '#define NORTHLIGHT_MATERIAL_CAPTURE\n' if capture else ''
    path.write_text(prefix + source + reference + fixture)
    for entry in ('Ray', 'ReferenceRay', 'Shadow', 'ReferenceShadow', 'ZeroRay', 'ReferenceZeroRay'):
        compiler.compile(path, entry, 'ps_3_0', out / (entry + '.bin'))
        validate_assembly(out / (entry + '.bin.asm'), 'ps_3_0')
    subprocess.run([str(exe), backend, str(out)], cwd=out, check=True, timeout=120)
    rows = json.loads((out / 'material-ray-work.json').read_text())
    assert len(rows) == 55 and all(row['max_error'] <= .00001 for row in rows)
    (out / ('material-rays-capture.json' if capture else 'material-rays-native.json')).write_text(json.dumps(rows, indent=2)+'\n')
    print(f'PASS 55 paired material ray cases: {"capture" if capture else "native"}, round trips, seams, gradients and zero-iteration rays')
