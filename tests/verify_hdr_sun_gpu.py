#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig
"""Authored sun/horizon HDR regression with generated wrong-output controls."""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parents[1]/'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import Compiler
from windows_gpu_guard import processes

def main():
    p=argparse.ArgumentParser(__doc__)
    for name in ('dxvk2','dxvk3','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();processes();out=a.output.resolve()
    if fp.is_client(out):raise SystemExit('Use an isolated test output directory.')
    out.mkdir(parents=True,exist_ok=True);source=fp.SHADERS/'celestial_disc_effects.hlsl'
    wrong=source.read_text(encoding='utf-8')
    wrong=wrong.replace('rawDepth>=1.0-1.0/16777216.0','saturate((rawDepth-DiscImage.z)*DiscImage.w)>=.99999994').replace('rawDepth<1.0-1.0/16777216.0','saturate((rawDepth-DiscImage.z)*DiscImage.w)<.99999994')
    expression='if(DiscEmission.w>.5){encoded=glowShoulder(alpha,encoded);if(DiscPolicy.x>1.5)encoded=discDecode(encoded);}'
    assert expression in wrong;wrong=wrong.replace(expression,'if(DiscEmission.w>.5)encoded=DiscPolicy.x>1.5?8*alpha*discDecode(encoded):glowShoulder(alpha,encoded);')
    # Retain the former veil transfer as an independent failing control.
    expression='float3 encoded=glowShoulder(alpha,DiscGlowHue.rgb);if(DiscPolicy.x>1.5)encoded=discDecode(encoded);'
    assert expression in wrong;wrong=wrong.replace(expression,'float3 encoded=DiscPolicy.x>1.5?1-exp2(-1.442695*max(alpha,0)*discDecode(DiscGlowHue.rgb)):glowShoulder(alpha,DiscGlowHue.rgb);')
    counter=out/'wrong-celestial.hlsl';counter.write_text(wrong,encoding='utf-8',newline='\n');compiler=Compiler(allow_compatible_binary=True)
    files=[]
    for name in ('CelestialDiscPS','CelestialHaloVisibilityPS','CelestialVeilPS'):
        binary=out/('wrong-'+name+'.bin');compiler.compile(counter,name,'ps_3_0',binary);files.append(binary)
    binary=out/'hdr_sun_glare_gpu.exe'
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-fno-rtti',*fp.include_flags(),str(fp.TESTS/'hdr_sun_glare_gpu.cpp'),'-luser32','-o',str(binary)],env=fp.zig_env(),check=True)
    digest=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    report=dict(source_sha256=digest(source),harness_sha256=digest(fp.TESTS/'hdr_sun_glare_gpu.cpp'),
                executable_sha256=digest(binary),production_header_sha256=digest(fp.GENERATED/'celestial_disc_compiled_shaders.h'),
                counterfactual_source_sha256=digest(counter),counterfactual_shaders_sha256={p.name:digest(p) for p in files},
                compiler=compiler.provenance,runs=[],game_launched=False,client_modified=False)
    for name,backend in [('dxvk2',a.dxvk2),('dxvk3',a.dxvk3)]:
        folder=out/name;folder.mkdir(exist_ok=True)
        r=subprocess.run([str(binary),str(backend.resolve()),*map(str,files)],cwd=folder,env=dict(os.environ,DXVK_LOG_PATH=str(folder)),capture_output=True,text=True,timeout=60)
        (out/(name+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');report['runs'].append(dict(backend=name,backend_sha256=digest(backend),exit_code=r.returncode,stdout=r.stdout))
        (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
        print(name,r.stdout.strip(),flush=True)
        if r.returncode or 'HDR_SUN_GPU_PASS' not in r.stdout:raise SystemExit(r.stdout+r.stderr)

if __name__=='__main__':main()
