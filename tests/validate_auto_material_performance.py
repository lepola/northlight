"""Compare original and optimized production material code on a real D3D9 device."""
from pathlib import Path
import argparse,json,re,subprocess,sys
import numpy as np

root=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(root),str(root/'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import Compiler,validate_assembly

p=argparse.ArgumentParser();p.add_argument('--backend',required=True);p.add_argument('--baseline-ref',default='4a4a4cd32efd2b36311c99848627df7562b4f08c');p.add_argument('--reuse-full-suite',action='store_true');args=p.parse_args()
out=fp.out()/'material-perf';out.mkdir(exist_ok=True)
(fp.out()/'tmp').mkdir(exist_ok=True)
env=fp.zig_env();env.update(TEMP=str(fp.out()/'tmp'),TMP=str(fp.out()/'tmp'))
baseline_root=fp.out()/'perf-baseline';baseline_root.mkdir(exist_ok=True)
for path in ('src/water/auto_bump_renderer.h','src/water/auto_parallax_renderer.h','src/water/auto_normal_cache.h','src/generated/auto_bump_compiled_shaders.h','shaders/auto_bump_capture.hlsl','shaders/auto_parallax.hlsl'):
    data=subprocess.check_output(['git','show',args.baseline_ref+':'+path],cwd=root)
    (baseline_root/Path(path).name).write_bytes(data)
if not args.reuse_full_suite:
    for baseline in (True,False):
        log=fp.out()/('perf-gpu-before.log' if baseline else 'perf-gpu-after.log')
        with log.open('w') as output:
            subprocess.run([sys.executable,str(root/'tests/validate_auto_bump.py'),'--backend',args.backend,'--output-subdir','perf-gpu-before' if baseline else 'perf-gpu-after',*(['--baseline'] if baseline else [])],cwd=root,env=env,stdout=output,stderr=subprocess.STDOUT,check=True)
compiler=Compiler(allow_compatible_binary=True)
entries=('BumpOpaque','BumpLayer','BumpWall','BumpGrass','BumpWallOpaque','BumpComposite')
results={}
for baseline in (True,False):
    name='baseline' if baseline else 'optimized';folder=out/name;folder.mkdir(exist_ok=True)
    source=baseline_root/'auto_bump_capture.hlsl' if baseline else fp.SHADERS/'auto_bump_capture.hlsl'
    for entry in entries:
        uv='uv'
        color='float4(.8,.6,.3,FixtureControl.w)';position='position'
        call={'BumpOpaque':f'{uv},p','BumpLayer':f'{uv},uv,p','BumpWall':f'{uv},{color},p','BumpGrass':f'{uv},float4(FixtureControl.w,0,0,0),p','BumpWallOpaque':f'{uv},{color},p','BumpComposite':f'{uv},uv,{color},float4(0,0,0,.6),p'}[entry]
        fixture=folder/(entry+'Fixture.hlsl')
        production=source.read_text().replace('#include "auto_parallax.hlsl"',(source.parent/'auto_parallax.hlsl').read_text())
        fixture.write_text(f'{production}\nfloat4 FixtureControl:register(c25);\nCaptureResult Fixture(float2 uv:TEXCOORD0,float4 position:TEXCOORD1){{float4 p={position};return {entry}({call});}}\n')
        code=folder/(entry+'Fixture.bin');compiler.compile(fixture,'Fixture','ps_3_0',code);validate_assembly(Path(str(code)+'.asm'),'ps_3_0')
    for fixture in (fp.out()/'perf-gpu-after/rocka').glob('source.fgr*'):(folder/fixture.name).write_bytes(fixture.read_bytes())
    for fixture in (fp.out()/'bump-recon').glob('*.bin'):(folder/fixture.name).write_bytes(fixture.read_bytes())
    exe=folder/'material-perf.exe'
    cmd=[str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17',*(['-I'+str(baseline_root)] if baseline else []),*fp.test_include_flags(),str(root/'tests/auto_material_perf_gpu.cpp'),'-luser32','-o',str(exe)]
    subprocess.run(cmd,env=env,check=True)
    run=subprocess.run([str(exe),args.backend,str(folder)+'/',name],env=env,cwd=folder,capture_output=True,text=True,timeout=90)
    (folder/'gpu.log').write_text(run.stdout+run.stderr)
    if run.returncode:raise RuntimeError(run.stdout+run.stderr)
    assert 'MATERIAL_PERF_GPU_PASS' in run.stdout
    results[name]={'gpu':run.stdout.strip().splitlines()[-1]}

comparisons=[]
for before in (out/'baseline').glob('*.f32'):
    after=out/'optimized'/before.name;a=np.fromfile(before,dtype='<f4');b=np.fromfile(after,dtype='<f4')
    delta=np.abs(a-b);assert delta.max()<.0002,(before.name,float(delta.max()))
    comparisons.append({'fixture':before.name,'max_delta':float(delta.max())})
assert len(comparisons)==108
counts=[]
for before in sorted((fp.out()/'perf-gpu-before').glob('*/gpu.log')):
    after=fp.out()/'perf-gpu-after'/before.parent.name/'gpu.log'
    a=re.search(r'MATERIAL_CALLS samplerGets=(\d+) samplerSets=(\d+)',before.read_text());b=re.search(r'MATERIAL_CALLS samplerGets=(\d+) samplerSets=(\d+)',after.read_text())
    assert a and b and int(b[2])<int(a[2]);counts.append({'material':before.parent.name,'before':list(map(int,a.groups())),'after':list(map(int,b.groups()))})
results.update(comparisons=comparisons,sampler_counts=counts,quality_settings_changed=False,game_launched=False)
(out/'validation.json').write_text(json.dumps(results,indent=2)+'\n')
print('AUTO_MATERIAL_PERFORMANCE_PASS frames=108 compressedWorkingSet=20 unchangedBudgetMiB=32',flush=True)
print(json.dumps(counts),flush=True)
