# northlight-test: requires=windows-gpu,zig manual timing
"""Measure the production base WorldRenderer on frozen inputs; no release DLL or game launch."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,statistics,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tests'),str(ROOT/'scripts')]
import northlight_paths as fp
from windows_gpu_guard import processes
from build_renderer import source_identity

def main():
 p=argparse.ArgumentParser(__doc__);p.add_argument('--backend',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
 p.add_argument('--capture',type=Path,default=Path('D:/Games/335_engb_mod/northlight-diagnostics/session-30804-228518734-0'))
 p.add_argument('--base-composite',action='store_true',help='Enable the specialized compositor on the verified DXVK 3 backend.')
 p.add_argument('--world-header',type=Path,help='Frozen world_renderer.h for a CPU preparation/state-binding control; all other inputs stay current.')
 p.add_argument('--compare-current',action='store_true',help='Compare the frozen --world-header with current code using identical shaders and exact SDR/HDR readbacks.')
 p.add_argument('--no-directional-source',action='store_true',help='Zero fixture source weights to check the TemporalLight distance fallback.')
 p.add_argument('--keep-world-state-save',action='store_true',help='Retain the nested WorldRenderer state save as a control for caller-owned restoration.')
 a=p.parse_args();processes();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 if a.base_composite:
  manifest=json.loads((ROOT/'shaders/world-shader-build.json').read_text())
  assert manifest['shaders']['WorldBaseCompositeHdr']['source_sha256']==hashlib.sha256((ROOT/'shaders/world_effects.hlsl').read_bytes()).hexdigest(),'Recompile WorldBaseCompositeHdr before profiling the changed source'
 if a.compare_current:
  if not a.world_header:p.error('--compare-current requires --world-header')
  identity=source_identity(ROOT);reports={}
  for label in ('reference','current'):
   command=[sys.executable,str(Path(__file__).resolve()),'--backend',str(a.backend.resolve()),'--capture',str(a.capture.resolve()),'--output',str(out/label)]
   if a.base_composite:command.append('--base-composite')
   if a.no_directional_source:command.append('--no-directional-source')
   if a.keep_world_state_save:command.append('--keep-world-state-save')
   if label=='reference':command+=['--world-header',str(a.world_header.resolve())]
   with (out/(label+'-driver.log')).open('w') as log:subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
   reports[label]=json.loads((out/label/'profile.json').read_text());assert reports[label]['pass_']
   assert reports[label]['source_identity']==identity,'Production source changed before a control'
  assert identity==source_identity(ROOT),'Production source changed between controls'
  common=reports['reference']['inputs'].keys()&reports['current']['inputs'].keys()
  assert all(reports['reference']['inputs'][k]==reports['current']['inputs'][k] for k in common),'Shared inputs changed between controls'
  for stage,expected in (('WorldNormals',0 if a.no_directional_source else 1),('TemporalLight',1 if a.no_directional_source else 0)):
   rows=[r for r in reports['current']['raw_cpu'] if r['stage']==stage]
   assert rows and all(r['mrtBinds']==expected for r in rows),(stage,'unexpected distance producer')
  fog_rows=[r for r in reports['current']['raw_cpu'] if r['stage']=='Fog']
  assert fog_rows and all(r['clears']==0 for r in fog_rows),'Warm neutral fog was cleared again'
  expected=int(not reports['current']['world_state_shared'])
  for stage,counter in (('WorldStateCapture','captures'),('WorldReturn','applies')):
   rows=[r for r in reports['current']['raw_cpu'] if r['stage']==stage]
   assert rows and all(r[counter]==expected for r in rows),(stage,'unexpected nested state block')
  images={label:{mode:hashlib.sha256((out/label/('baseline-output-'+mode+'.fgr')).read_bytes()).hexdigest() for mode in ('sdr','hdr','sdr-distance','hdr-distance','sdr-fog','hdr-fog')} for label in reports}
  result=dict(pass_=images['reference']==images['current'],source_identity=identity,images=images,world_header=str(a.world_header.resolve()),no_directional_source=a.no_directional_source,game_fps_measured=False,timing_comparison_valid=False,
   scope='Frozen/current WorldRenderer preparation with identical shaders/inputs; exact SDR/HDR, R32F current distance and raw fog control. One run per variant; use ABBA for performance conclusions.',
   profiles={label:str(out/label/'profile.json') for label in reports})
  (out/'comparison.json').write_bytes((json.dumps(result,indent=2)+'\n').encode());assert result['pass_'],'WorldRenderer changed color/distance/fog pixels'
  print('PASS frozen/current WorldRenderer color/distance/fog=exact; raw API/register counters in profiles');return
 baseline=out/'baseline';baseline.mkdir();shutil.copytree(ROOT/'src',baseline/'src')
 world_input=a.world_header.resolve() if a.world_header else ROOT/'src/world/world_renderer.h'
 shutil.copyfile(world_input,baseline/'input-world_renderer.h')
 overlay=out/'probe-headers';overlay.mkdir()
 header=(ROOT/'src/core/gpu_profile.h').read_text()
 header=header.replace('#include <cstring>','#include <cstring>\nextern void northlightBaseStage(const char*,const char*,unsigned long long);',1)
 marker='return issue(s.stamps[0],D3DISSUE_END,"start timestamp");';assert header.count(marker)==1
 header=header.replace(marker,'const bool started=issue(s.stamps[0],D3DISSUE_END,"start timestamp");if(started)northlightBaseStage(prefix_,"begin",frameNumber);return started;',1)
 header=header.replace('label(s.labels[s.marks],name);','northlightBaseStage(prefix_,name,s.frame);label(s.labels[s.marks],name);',1)
 header=header.replace('Slot& s=slots_[active_];label(s.labels[s.marks],"tail");','Slot& s=slots_[active_];northlightBaseStage(prefix_,"tail",s.frame);label(s.labels[s.marks],"tail");',1)
 header=header.replace('%.3fms','%.6fms')
 (overlay/'gpu_profile.h').write_bytes(header.encode())
 world=world_input.read_text()
 caller_state_api='const SavedState* enclosingState=nullptr' in world
 marker='if(!callerRestores){save.emplace(d,&stateBlocks);if(!save->ok)return false;}' if caller_state_api else 'SavedState save(d,&stateBlocks);if(!save.ok)return false;';assert world.count(marker)==1
 world=world.replace(marker,marker+'if(profile)profile->mark("WorldStateCapture");')
 (overlay/'world_renderer.h').write_bytes(world.encode())
 if not caller_state_api:
  proxy=(ROOT/'src/proxy/renderer.cpp').read_text();marker='inlineHdrBase&&hdrSkyValid,!contactAO,&saved);';assert proxy.count(marker)==1
  (overlay/'renderer.cpp').write_bytes(proxy.replace(marker,'inlineHdrBase&&hdrSkyValid,!contactAO);').encode())
 sources=['world_gi.cpp','world_mesh_plan.cpp','static_shadow_scene.cpp','settings_overlay.cpp','settings_panel.cpp','imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','imgui_impl_dx9.cpp']
 exe=out/'base-weather.exe';env={k:v for k,v in fp.zig_env().items() if not k.startswith(('NORTHLIGHT_','DXVK_'))}
 env.update(DISABLE_VULKAN_OBS_CAPTURE='1',DXVK_CONFIG='d3d9.customVendorId = 1002',DXVK_LOG_PATH=str(out))
 if a.base_composite:env['NORTHLIGHT_BASE_BACKEND_VERIFIED']='1'
 if a.no_directional_source:env['NORTHLIGHT_BASE_NO_DIRECTIONAL']='1'
 if a.keep_world_state_save:env['NORTHLIGHT_BASE_KEEP_WORLD_STATE_SAVE']='1'
 for key,name in [('TEMP','temp'),('TMP','temp'),('ZIG_LOCAL_CACHE_DIR','zig-local'),('ZIG_GLOBAL_CACHE_DIR','zig-global')]:
  (out/name).mkdir(exist_ok=True);env[key]=str(out/name)
 inputs=[Path(__file__).resolve(),ROOT/'tests/base_weather_pipeline_gpu.cpp',ROOT/'tests/world_screen_effects_gpu.cpp',world_input,a.backend.resolve(),*(a.capture/name for name in ['capture-1-scene.fgr','capture-1-depth.fgr','capture-1-constants.f32'])]
 sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
 report=dict(pass_=False,source_identity=source_identity(ROOT),world_header_override=str(world_input) if a.world_header else None,no_directional_source=a.no_directional_source,world_state_shared=caller_state_api and not a.keep_world_state_save,inputs={str(path):sha(path) for path in inputs},game_launched=False,whole_scene_replay=False,game_fps_measured=False,
  scope='Production WorldRenderer, captured scene/depth textures and camera bank, Weather only, SDR/HDR. SceneCopy1x is an offscreen 1x copy; live RESZ, native game draw hooks and model capture are outside this first measurement. CPU includes probe/query overhead; per-stage logs are buffered until after the measured interval. GPU elapsed includes command-supply gaps.')
 def run(label,cmd):
  processes();r=subprocess.run(list(map(str,cmd)),cwd=out,env=env,capture_output=True,text=True,timeout=300);(out/(label+'.log')).write_bytes((r.stdout+r.stderr).encode());print(label,r.returncode,flush=True);r.check_returncode();return r.stdout
 try:
  run('compile',[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DIMGUI_USER_CONFIG="northlight_imconfig.h"',*(['-DNORTHLIGHT_TEST_WORLD_ENCLOSING_STATE'] if caller_state_api else []),'-I'+str(overlay),*fp.test_include_flags(),ROOT/'tests/base_weather_pipeline_gpu.cpp',*(fp.src(name) for name in sources),'-luser32','-luuid','-o',exe])
  settings=out/'settings';settings.mkdir();(settings/'northlight-renderer.ini').write_bytes(b'[Renderer]\nSettingsOverlay=0\nExperimentalHDR=0\n')
  text=run('baseline',[exe,a.backend.resolve(),settings,a.capture/'capture-1-scene.fgr',a.capture/'capture-1-depth.fgr',a.capture/'capture-1-constants.f32',out/'baseline-output'])
  assert 'BASE_WEATHER_PROFILE_PASS' in text
  if caller_state_api:
   assert 'BASE_STATE_PASS hdr=0 validEnclosure=1 invalidEnclosureFallback=1 bindingsConstantsRestored=1' in text
   assert 'BASE_STATE_PASS hdr=1 validEnclosure=1 invalidEnclosureFallback=1 bindingsConstantsRestored=1 drawFailureRestored=1' in text
  rows=[];gpu=[]
  for line in text.splitlines():
   if line.startswith('BASE_CPU '):
    fields=dict(re.findall(r'(\w+)=([^\s]+)',line));fields={k:(v if k=='stage' else float(v) if k=='ms' else int(v)) for k,v in fields.items()}
    if fields['frame']%1000>=16:rows.append(fields)
   if line.startswith('BASE_GPU frame='):
    frame=int(re.search(r'frame=(\d+)',line)[1])
    if frame%1000>=16:gpu += [dict(frame=frame,stage=k,ms=float(v)) for k,v in re.findall(r'(\w+)=([0-9.]+)ms',line)]
  def summaries(rows):
   groups={}
   for row in rows:groups.setdefault(('HDR' if row['frame']>=1000 else 'SDR',row['stage']),[]).append(row)
   return {mode:{stage:{key:dict(median=statistics.median(r[key] for r in values),maximum=max(r[key] for r in values),samples=len(values)) for key in values[0] if key not in ('frame','stage')} for (m,stage),values in groups.items() if m==mode} for mode in ('SDR','HDR')}
  report.update(pass_=True,cpu=summaries(rows),gpu=summaries(gpu),raw_cpu=rows,raw_gpu=gpu)
 finally:
  report['inputs_unchanged']=all(sha(Path(path))==value for path,value in report['inputs'].items()) and source_identity(ROOT)==report['source_identity']
  report['pass_'] &= report['inputs_unchanged'];(out/'profile.json').write_bytes((json.dumps(report,indent=2)+'\n').encode())
 assert report['pass_'];print(json.dumps(dict(cpu=report['cpu'],gpu=report['gpu']),indent=2))

if __name__=='__main__':main()
