"""Compare native POM output and real GPU timestamps against an explicit source baseline."""
from pathlib import Path
import argparse,json,re,shutil,subprocess,sys
import numpy as np
root=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(root),str(root/'scripts/shaders')]
import northlight_paths as fp
from shader_compiler import Compiler,validate_assembly
p=argparse.ArgumentParser();p.add_argument('--backend',required=True);p.add_argument('--baseline',type=Path,required=True);args=p.parse_args()
out=fp.out()/'material-kernel-20261022';out.mkdir(parents=True,exist_ok=True)
compiler=Compiler(allow_compatible_binary=True)
for label,source in [('before',args.baseline/'auto_parallax.hlsl'),('after',fp.SHADERS/'auto_parallax.hlsl')]:
    fixture=out/(label+'.hlsl')
    fixture.write_text(source.read_text()+'''\nsampler2D HeightMap:register(s14);
float4 Projection:register(c220);float4 Control:register(c221);
float4 Fixture(float2 uv:TEXCOORD0,float4 p:TEXCOORD1):COLOR0{return float4(parallaxUV(HeightMap,uv,p,Projection,Control),0,1);}
''')
    compiler.compile(fixture,'Fixture','ps_3_0',out/(label+'.bin'));validate_assembly(out/(label+'.bin.asm'),'ps_3_0')
shutil.copy2(fp.out()/'oblique-cobble/source.fgr',out/'source.fgr')
exe=out/'timing.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17',*fp.test_include_flags(),str(root/'tests/material_shader_timing_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
run=subprocess.run([str(exe),str(Path(args.backend).resolve()),str(out)+'/'],cwd=out,capture_output=True,text=True,timeout=120)
(out/'gpu.log').write_text(run.stdout+run.stderr);print(run.stdout,flush=True)
assert run.returncode==0,run.stderr
comparisons=[]
for mode in range(4):
    before=np.fromfile(out/f'before-{mode}.f32',dtype='<f4');after=np.fromfile(out/f'after-{mode}.f32',dtype='<f4')
    assert before.size==after.size==256*256*4 and np.isfinite(after).all()
    delta=float(np.max(np.abs(after-before)));assert delta<.000002,(mode,delta)
    comparisons.append(dict(mode=mode,max_delta=delta))
timings=[dict(mode=int(m[0]),before_ms=float(m[1]),after_ms=float(m[2])) for m in re.findall(r'GPU_TIMING mode=(\d+).*beforeMs=([\d.]+) afterMs=([\d.]+)',run.stdout)]
assert len(timings)==4
(out/'validation.json').write_text(json.dumps(dict(comparisons=comparisons,timings=timings,scope='4K production POM subkernel, not game FPS'),indent=2)+'\n')
print('MATERIAL_KERNEL_OUTPUT_PASS cases=4',flush=True)
