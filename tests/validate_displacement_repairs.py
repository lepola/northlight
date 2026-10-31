"""Reproduce the prior pose/grass/cache failures against the installed source."""
from pathlib import Path
import argparse,json,re,shutil,subprocess,sys
import numpy as np
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root));import northlight_paths as fp
p=argparse.ArgumentParser();p.add_argument('--backend',required=True);p.add_argument('--reference',type=Path);args=p.parse_args()
name='native' if 'windows' in args.backend.lower() else 'dxvk';out=fp.out()/('repairs-'+name);out.mkdir(exist_ok=True)
env=fp.zig_env();env.update(TEMP=str(fp.out()/'tmp'),TMP=str(fp.out()/'tmp'))
def build(source,output,reference=False):
 flags=['-I'+str(args.reference/'src'/d) for d in ('water','core','generated','replay')] if reference else []
 subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-DNORTHLIGHT_OBLIQUE_WIDTH=320','-DNORTHLIGHT_OBLIQUE_HEIGHT=200',*flags,*fp.test_include_flags(),str(root/'tests'/source),'-luser32','-o',str(output)],env=env,check=True)
def run(exe,extra=()):
 r=subprocess.run([str(exe),args.backend,*extra],cwd=out,capture_output=True,text=True,env=env,timeout=90);return r
results={}
for reference in ((True,False) if args.reference else (False,)):
 label='before' if reference else 'after';exe=out/('cache-'+label+'.exe');build('material_cache_working_gpu.cpp',exe,reference);r=run(exe);(out/(label+'-cache.log')).write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
 stats=[tuple(map(int,m)) for m in re.findall(r'AUTONORMAL cached=(\d+) bytes=(\d+) generated=(\d+) hits=(\d+) evicted=(\d+)',r.stdout)];assert len(stats)==2
 regenerated=stats[1][2]-stats[0][2];assert regenerated==(20 if reference else 0),(label,stats)
 if not reference:assert stats[1][0]==20 and stats[1][1]<=32<<20 and stats[1][4]==0
 results[label]={'working_set':20,'revisit_generations':regenerated,'cache':stats}
 print('CACHE_REPAIR_PASS',label,results[label],flush=True)
 if reference:
  exe=out/'pose-before.exe';build('auto_normal_oblique_gpu.cpp',exe,True)
  fixture=fp.out()/'model-materials-native/3-varying-combiners_opaque';assert fixture.is_dir(),'Run validate_model_materials.py first (creates flat-height pose fixtures).'
  folder=out/'before-pose';folder.mkdir(exist_ok=True)
  for f in fixture.iterdir():
   if f.name.startswith('source.fgr') or f.suffix=='.bin':shutil.copy2(f,folder/f.name)
  r=run(exe,[str(folder)+'/', '12','.7','modelunsafe']);(out/'before-pose.log').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stdout+r.stderr
  def frame(path):
   import struct
   b=path.read_bytes();_,w,h,fmt,stride=struct.unpack_from('<5I',b);return np.frombuffer(b,dtype='<f4' if stride==16 else '<f2',offset=20).reshape(h,w,4).astype(np.float32)
  a=frame(folder/'geometry-off-depth.fgr');b=frame(folder/'geometry-on-depth.fgr');valid=(a[:,:,3]>.01)&(b[:,:,3]>.01);delta=np.abs(a[:,:,2]/np.maximum(a[:,:,3],1e-6)-b[:,:,2]/np.maximum(b[:,:,3],1e-6));assert delta[valid].max()>.005
  results[label]['flat_height_pose_error']=float(delta[valid].max());print('POSE_BUG_REPRODUCED',results[label]['flat_height_pose_error'],flush=True)
(out/'validation.json').write_text(json.dumps(results,indent=2)+'\n')
