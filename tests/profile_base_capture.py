# northlight-test: requires=windows-gpu,zig,client manual timing
"""Measure the base model capture with the production state mirror, no material producer."""
import argparse,hashlib,json,os,re,shutil,statistics,struct,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tests'),str(ROOT/'scripts')]
import northlight_paths as fp
from windows_gpu_guard import processes
from build_renderer import source_identity

def main():
 p=argparse.ArgumentParser(__doc__);p.add_argument('--backend',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
 p.add_argument('--normal-header',type=Path,help='Frozen model_normal_renderer.h for a preparation control using the same shaders and fixture.')
 a=p.parse_args()
 processes();fp.use_source_modules();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);overlay=out/'probe';overlay.mkdir();shutil.copytree(ROOT/'src',out/'baseline/src')
 for name in ['base_capture_pipeline_gpu.cpp','model_normals_gpu.cpp','model_normal_batch.inc']:
  (out/'baseline/tests').mkdir(exist_ok=True);shutil.copyfile(ROOT/'tests'/name,out/'baseline/tests'/name)
 (overlay/'base_capture_probe.h').write_text('#pragma once\n#include "capture_phase_profile.h"\nextern NorthlightCapturePhases::Stats<8> baseCapture,baseRecord,baseFlush;extern bool baseProbe;\n')
 normal_input=a.normal_header.resolve() if a.normal_header else ROOT/'src/proxy/model_normal_renderer.h'
 shutil.copyfile(normal_input,out/'baseline/input-model_normal_renderer.h')
 header=normal_input.read_text().replace('#include <d3d9.h>','#include <d3d9.h>\n#include "base_capture_probe.h"',1)
 def insert(marker,code,before=False):
  nonlocal header
  assert header.count(marker)==1,(marker,header.count(marker));header=header.replace(marker,code+marker if before else marker+code,1)
 insert('if(bad||!source)return;','NorthlightCapturePhases::Scope<8> phase(baseProbe?&baseCapture:nullptr);')
 insert('auto* vs=vertex(source);','phase.next(1);',True)
 insert('State saved(d,ps->control,userMemory);','phase.next(2);',True)
 insert('const float control[]={0,48.f,','phase.next(3);',True)
 insert('if(!deferred||!vs->reads.exact||','NorthlightCapturePhases::Scope<8> phase(baseProbe?&baseRecord:nullptr);',True)
 insert('const auto& mesh=packet->geometry();','phase.next(1);',True)
 insert('if(!admit())return false;','phase.next(2);',True)
 insert('if(geometryCache&&packet->sharedMesh&&packet->sharedMesh->sourceIdentity&&','phase.next(3);',True)
 insert('packet->ps.resize(ps->count*4);','phase.next(4);',True)
 insert('packet->viewport=viewport;','phase.next(5);',True)
 insert('bool ok=!bad;','NorthlightCapturePhases::Scope<8> phase(baseProbe?&baseFlush:nullptr);')
 insert('State saved(d,0,true);','phase.next(1);',True)
 insert('const Pending* previous=nullptr;','phase.next(2);',True)
 insert('ok=saved.restore()&&ok;\n                ok=(SUCCEEDED(batchState->Apply())','phase.next(3);',True)
 insert('clearPending();bad|=!ok;return ok;','phase.next(4);',True)
 (overlay/'model_normal_renderer.h').write_bytes(header.encode())
 from world_scene_builder import Assets
 from extract_signatures import variants
 assets=Assets(archives='stock');model='world/generic/human/passive doodads/lamps/stormwindstreetlamp01.m2'
 try:
  data=assets.read(model);skin=assets.read(model[:-3]+'00.skin');nv,at=struct.unpack_from('<II',data,60)
  n,o=struct.unpack_from('<II',skin,4);lookup=struct.unpack_from('<%dH'%n,skin,o);n,o=struct.unpack_from('<II',skin,12);indices=[lookup[x] for x in struct.unpack_from('<%dH'%n,skin,o)]
  n,o=struct.unpack_from('<II',skin,28);subset=next(s for s in struct.iter_unpack('<10H7f',skin[o:o+n*48]) if s[5]);first=subset[4]+(subset[1]<<16);used=indices[first:first+subset[5]];lo,hi=min(used),max(used)
  assert hi<nv and used and len(used)%3==0
  mesh=struct.pack('<III',0,hi-lo+1,len(used))+data[at+lo*48:at+(hi+1)*48]+struct.pack('<%dI'%len(used),*(i-lo for i in used));(out/'lamp.mesh').write_bytes(mesh)
  for path,index,name in [('shaders/vertex/vs_3_0/diffuse_t1.bls',1,'model-vs3-rigid.bin'),('shaders/pixel/ps_3_0/combiners_opaque.bls',0,'ModelPS.bin')]:
   codes={i:code for i,_,code in variants(assets.read(path))};(out/name).write_bytes(codes[index])
 finally:assets.close()
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 inputs=[ROOT/'tests/base_capture_pipeline_gpu.cpp',ROOT/'tests/profile_base_capture.py',ROOT/'tests/model_normals_gpu.cpp',ROOT/'tests/model_normal_batch.inc',normal_input,a.backend.resolve(),out/'lamp.mesh',out/'model-vs3-rigid.bin',out/'ModelPS.bin']
 report=dict(pass_=False,source_identity=source_identity(ROOT),normal_header_override=str(normal_input) if a.normal_header else None,inputs={str(p):sha(p) for p in inputs},model=model,model_sha256=hashlib.sha256(data).hexdigest(),skin_sha256=hashlib.sha256(skin).hexdigest(),vertices=hi-lo+1,triangles=len(used)//3,
  game_fps_measured=False,whole_scene_replay=False,scope='Controlled repeated stock streetlamp subset: 133 near + 310 far draws; 4K MSAA2. Actual ExtensionDevice mirror, shared source pool, resident GPU store, no AutoBump or pose/source handoff. Native submission is separate. Capture CPU includes per-draw clocks; instrumented phases include QPC overhead. Replay GPU excludes preceding game draws; isolated from game/other benchmarks.')
 env={k:v for k,v in fp.zig_env().items() if not k.startswith(('NORTHLIGHT_','DXVK_'))};env.update(DISABLE_VULKAN_OBS_CAPTURE='1',DXVK_CONFIG='d3d9.customVendorId = 1002',DXVK_LOG_PATH=str(out))
 for key,name in [('TEMP','temp'),('TMP','temp'),('ZIG_LOCAL_CACHE_DIR','zig-local'),('ZIG_GLOBAL_CACHE_DIR','zig-global')]:
  (out/name).mkdir(exist_ok=True);env[key]=str(out/name)
 def run(label,cmd):
  processes();r=subprocess.run(list(map(str,cmd)),env=env,cwd=out,capture_output=True,text=True,timeout=300);(out/(label+'.log')).write_bytes((r.stdout+r.stderr).encode());print(label,r.returncode,flush=True);r.check_returncode();return r.stdout
 try:
  exe=out/'base-capture.exe';run('compile',[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-I'+str(overlay),*fp.test_include_flags(),ROOT/'tests/base_capture_pipeline_gpu.cpp',fp.src('world_gi.cpp'),'-luser32','-o',exe])
  text=run('baseline',[exe,a.backend.resolve(),out]);assert 'BASE_CAPTURE_PROFILE_PASS' in text
  frames=[];phases=[];frame=None
  for line in text.splitlines():
   if line.startswith('CAPTURE_FRAME '):
    frame={k:float(v) for k,v in re.findall(r'(\w+)=([0-9.]+)',line)}
    if not frame['warm']:frames.append(frame)
   elif line.startswith('CAPTURE_PHASE ') and frame and not frame['warm']:
    row=dict(re.findall(r'(\w+)=([^\s]+)',line));phases.append(dict(frame=frame['frame'],group=row['group'],phase=int(row['phase']),ms=float(row['ms'])))
  stats=lambda xs:dict(median=statistics.median(xs),p95=sorted(xs)[int((len(xs)-1)*.95)],maximum=max(xs),samples=len(xs))
  report['frames']={str(mode):{key:stats([r[key] for r in frames if r['profiled']==mode]) for key in frames[0] if key not in ('frame','warm','profiled')} for mode in (0,1)}
  report['phases']={group:{str(phase):stats([r['ms'] for r in phases if r['group']==group and r['phase']==phase]) for phase in sorted({r['phase'] for r in phases if r['group']==group})} for group in sorted({r['group'] for r in phases})}
  report.update(pass_=True,raw_frames=frames,raw_phases=phases)
 finally:
  report['inputs_unchanged']=all(sha(Path(p))==v for p,v in report['inputs'].items()) and source_identity(ROOT)==report['source_identity'];report['pass_'] &= report['inputs_unchanged'];(out/'profile.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
 assert report['pass_'];print(json.dumps({k:report[k] for k in ('frames','phases')},indent=2))

if __name__=='__main__':main()
