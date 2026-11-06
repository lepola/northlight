#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual
"""Visible normal/shadow/cavity, near cutoff, sampled timings and a batching experiment. Never starts WoW."""
from pathlib import Path
import argparse,json,re,shutil,statistics,struct,subprocess,sys
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--backend',required=True);p.add_argument('--benchmark',action='store_true');p.add_argument('--output-subdir',default='material-stages');a=p.parse_args()
out=fp.out()/a.output_subdir;out.mkdir(exist_ok=True)
source=fp.out()/'model-materials-dxvk/3-weighted4-combiners_opaque';assert source.exists(),'Prepare validate_model_materials.py fixtures first'
def compile_fixture(width,height,name):
    exe=out/(name+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DNORTHLIGHT_MATERIAL_MIRROR',f'-DNORTHLIGHT_OBLIQUE_WIDTH={width}',f'-DNORTHLIGHT_OBLIQUE_HEIGHT={height}',*fp.test_include_flags(),str(fp.TESTS/'auto_normal_oblique_gpu.cpp'),'-luser32','-luuid','-o',str(exe)],env=fp.zig_env(),check=True)
    data=bytearray(exe.read_bytes());flags=struct.unpack_from('<I',data,0x3c)[0]+22;struct.pack_into('<H',data,flags,struct.unpack_from('<H',data,flags)[0]|0x20);exe.write_bytes(data);return exe
def prepare(name):
    folder=out/name;folder.mkdir(exist_ok=True)
    for f in source.iterdir():
        if f.name.startswith('source.fgr') or f.name in ('terrain-vs.bin','terrain-ps.bin','PreviewBase.bin'):shutil.copy2(f,folder/f.name)
    return folder
def run(exe,folder,label,**flags):
    env=fp.zig_env();env.pop('NORTHLIGHT_EXPERIMENTAL_SHARED_POM',None)
    for k in list(env):
        if k.startswith('NORTHLIGHT_TEST_'):env.pop(k)
    # Match the packaged renderer: DXVK implements RESZ only for AMD identity.
    env['DXVK_CONFIG']='d3d9.customVendorId = 1002; '+env.get('DXVK_CONFIG','')
    env.update(NORTHLIGHT_TEST_MSAA='2');env.update({'NORTHLIGHT_TEST_'+k:str(v) for k,v in flags.items()})
    r=subprocess.run([str(exe),a.backend,str(folder)+'/', '12','.7','model4'],env=env,cwd=out,capture_output=True,text=True,timeout=180)
    (out/(label+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8');assert r.returncode==0,(label,r.stdout[-3000:],r.stderr[-2000:]);return r.stdout
def image(path):
    b=path.read_bytes();magic,w,h,fmt,stride=struct.unpack_from('<5I',b);assert magic==0x31524746
    return np.frombuffer(b,dtype='<f4' if stride==16 else '<f2' if stride==8 else 'u1',offset=20).reshape(h,w,4).astype(np.float32)
def delta(folder,before,after):return float(np.max(np.abs(image(folder/before)[:,:,:3]-image(folder/after)[:,:,:3])))
exe=compile_fixture(640,400,'stages');near=prepare('near');far=prepare('far')
log=run(exe,near,'near',MATERIAL_PROFILE=1)
assert 'MATERIAL_CPU frame=127' in log and 'MATERIAL_GPU_COVERAGE frame=127' in log and 'complete=1' in log
run(exe,far,'far',MATERIAL_DEPTH=12)
metrics={}
for label,folder in [('near',near),('far',far)]:
    metrics[label]=dict(normal=delta(folder,'native-normal-off.fgr','normal-on.fgr'),shadow=delta(folder,'pom-on.fgr','pom-shadow.fgr'),cavity=delta(folder,'pom-shadow.fgr','pom-shadow-cavity.fgr'))
print('MATERIAL_VISIBLE_EFFECTS',json.dumps(metrics),flush=True)
assert metrics['near']['normal']>.0001 and metrics['near']['shadow']>.0001 and metrics['near']['cavity']>.0001,metrics
assert metrics['far']['normal']>.0001 and metrics['far']['shadow']==0 and metrics['far']['cavity']==0,metrics
(out/'visibility.json').write_text(json.dumps(metrics,indent=2)+'\n',encoding='utf-8')
if a.benchmark:
    folder=prepare('batch');records=[]
    for batched in [0,1]:
        run(exe,folder,'batch-check-'+str(batched),MATERIAL_BATCH_TIMING=1,MATERIAL_BATCH_VALIDATE=1,**({'MATERIAL_BATCHED':1} if batched else {}))
        images={f.name:image(f) for f in folder.glob('batch-*.fgr')}
        if not batched:baseline=images
        else:
            errors={name:float(np.max(np.abs(value-baseline[name]))) for name,value in images.items()};assert len(errors)==3 and max(errors.values())==0,errors
            (out/'batch-equivalence.json').write_text(json.dumps(errors,indent=2)+'\n',encoding='utf-8')
    exe=compile_fixture(3840,2160,'batch-timing')
    for repeat in range(3):
        for batched in ([0,1] if repeat%2==0 else [1,0]):
            log=run(exe,folder,f'batch-timing-{repeat}-{batched}',MATERIAL_BATCH_TIMING=1,**({'MATERIAL_BATCHED':1} if batched else {}))
            m=re.search(r'MATERIAL_BATCH_TIMING .*gpuMs=([\d.]+) submitMs=([\d.]+)',log);assert m
            records.append(dict(repeat=repeat,batched=batched,gpu_ms=float(m[1]),submit_ms=float(m[2])))
    timing=dict(width=3840,height=2160,msaa=2,draws=32,runs=records)
    for b in [0,1]:timing['batched_ms' if b else 'interleaved_ms']=statistics.median(r['gpu_ms'] for r in records if r['batched']==b)
    timing['reduction_percent']=100*(1-timing['batched_ms']/timing['interleaved_ms'])
    (out/'batch-timing.json').write_text(json.dumps(timing,indent=2)+'\n',encoding='utf-8');print('MATERIAL_BATCH_EXPERIMENT',json.dumps(timing),flush=True)
print('MATERIAL_STAGES_PASS',flush=True)
