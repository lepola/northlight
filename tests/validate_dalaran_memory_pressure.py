#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,world-cache
"""Compare current and proposed 256/128 MiB admission in an isolated x86 scene replay.

Uses the configured client's cached Dalaran scene and the production CPU pipeline.
Private copied headers implement the proposed policy; production files and the
installed client are read only. Reserve-only ballast belongs to the test process.
Run: python tests/validate_dalaran_memory_pressure.py --output <empty-directory>
No game, GPU, image/FPS acceptance or personal settings changes.
"""
from pathlib import Path
import subprocess,sys,json
repo=Path(__file__).resolve().parents[1]
import argparse,datetime,hashlib
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path)
args=parser.parse_args()
sys.path.insert(0,str(repo));import northlight_paths as fp
out=(args.output or fp.output_dir()/datetime.datetime.now().strftime('%Y%m%d-%H%M%S')).resolve()
if out.exists() and any(out.iterdir()):raise RuntimeError('Output must be empty; prior evidence is retained')
out.mkdir(parents=True,exist_ok=True)
cache=fp.world_cache()
protected=[fp.src('geometry_memory.h'),fp.src('world_gi.cpp'),fp.src('world_mesh_plan.cpp'),fp.TESTS/'validate_shared_scene_pipeline.cpp']
fingerprints={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
results=[]
s=(fp.TESTS/'validate_shared_scene_pipeline.cpp').read_text().replace('Vec3(5248,10,508)','Vec3(5728,615,659)').replace('step<7','step<1')
prefix=r"""
#include <windows.h>
#include "geometry_memory.h"
static NorthlightGeometryMemory::Sample addressSpace(){
 NorthlightGeometryMemory::Sample s;uint64_t p=0;
 while(p<=UINT32_MAX){MEMORY_BASIC_INFORMATION m{};if(VirtualQuery((const void*)uintptr_t(p),&m,sizeof m)!=sizeof m)break;
  const uint64_t end=uint64_t(uintptr_t(m.BaseAddress))+m.RegionSize;if(end<=p)break;
  if(m.State==MEM_FREE){s.available+=m.RegionSize;s.largest=std::max(s.largest,uint64_t(m.RegionSize));}p=end;}
 s.valid=s.largest>0;return s;
}
struct Ballast{
 std::vector<void*> blocks;
 ~Ballast(){for(void* p:blocks)assert(VirtualFree(p,0,MEM_RELEASE));}
 void leave(uint64_t target){for(unsigned n=0;n<80;++n){auto s=addressSpace();assert(s.valid);if(s.available<=target+65536)return;
  const uint64_t bytes=std::min(s.available-target,s.largest/2)/65536*65536;assert(bytes);void* p=VirtualAlloc(nullptr,size_t(bytes),MEM_RESERVE,PAGE_NOACCESS);assert(p);blocks.push_back(p);}assert(false);}
};
"""
s=s.replace('int main(int argc,char** argv){',prefix+'\nint main(int argc,char** argv){')
s=s.replace('const bool bake=argc==4;', 'const bool bake=false;const bool pressure=argc==4;')
needle='        auto all=Clock::now(),t=all;'
checks=r"""
        Ballast ballast;if(pressure)ballast.leave(uint64_t(433.24*1048576));
        auto sample=[&]{auto s=addressSpace();if(pressure)s.largest=std::min(s.largest,uint64_t(166.06*1048576));return s;};
        auto entry=NorthlightGeometryMemory::buildBudget(64*NorthlightGeometryMemory::MiB);auto first=sample();
        std::printf("TRIAL entryAvailableMiB=%.3f blockMiB=%.3f requiredEntryMiB=%llu reserveMiB=%llu pressure=%u\n",double(first.available)/1048576,double(first.largest)/1048576,(unsigned long long)(entry.available>>20),(unsigned long long)(NorthlightGeometryMemory::ProcessReserve>>20),unsigned(pressure));
        if(!NorthlightGeometryMemory::admits(first,entry)){std::puts("TRIAL ENTRY_REFUSED");return 0;}
        const char* phase="local";uint64_t largestRequest=0;unsigned calls=0;NorthlightGI::AllocationAdmission admission=[&](uint64_t bytes){
          if(bytes<NorthlightGeometryMemory::MiB)return true;auto current=sample();auto required=NorthlightGeometryMemory::inBuildBudget(bytes);largestRequest=std::max(largestRequest,bytes);++calls;
          const bool pass=NorthlightGeometryMemory::admits(current,required);
          if(!pass)std::printf("TRIAL REFUSED phase=%s availableMiB=%.3f blockMiB=%.3f requestMiB=%.3f requiredMiB=%.3f contiguousMiB=%.3f calls=%u\n",phase,double(current.available)/1048576,double(current.largest)/1048576,double(bytes)/1048576,double(required.available)/1048576,double(required.largest)/1048576,calls);
          return pass;};
"""
assert s.count(needle)==1;s=s.replace(needle,checks+needle)
replacements=[
('assert(cache.build(nearFiles,root+"/models",map,c-Vec3(288,288,320),c+Vec3(288,288,320),local,error,{},true,provider,revision));','if(!cache.build(nearFiles,root+"/models",map,c-Vec3(288,288,320),c+Vec3(288,288,320),local,error,admission,true,provider,revision)){std::printf("TRIAL LOCAL_REFUSED %s\\n",error.c_str());return 0;}'),
('assert(loadInstancedScenes(farFiles,root+"/models",c-Vec3(radius,radius,radius),c+Vec3(radius,radius,radius),terrain,error,1,{},terrainFilter,terrainChunkFilter,&hint,provider','if(!loadInstancedScenes(farFiles,root+"/models",c-Vec3(radius,radius,radius),c+Vec3(radius,radius,radius),terrain,error,1,admission,terrainFilter,terrainChunkFilter,&hint,provider'),
('        ));','        )){std::printf("TRIAL TERRAIN_REFUSED %s\\n",error.c_str());return 0;}'),
('assert(NorthlightShadowTerrain::build(local.scene(),terrain,c,plan,error,{},radius,{},true,&local,&preparation));','if(!NorthlightShadowTerrain::build(local.scene(),terrain,c,plan,error,admission,radius,{},true,&local,&preparation)){std::printf("TRIAL PLAN_REFUSED %s\\n",error.c_str());return 0;}'),
('assert(NorthlightWorldMeshPages::build(plan,pages,error,{},options));','if(!NorthlightWorldMeshPages::build(plan,pages,error,admission,options)){std::printf("TRIAL PAGES_REFUSED %s\\n",error.c_str());return 0;}'),
('double localMs=ms(t);t=Clock::now();','double localMs=ms(t);t=Clock::now();phase="terrain";'),
('double terrainMs=ms(t);t=Clock::now();','double terrainMs=ms(t);t=Clock::now();phase="plan";'),
('double planMs=ms(t);t=Clock::now();','double planMs=ms(t);t=Clock::now();phase="pages";')]
for old,new in replacements:
 assert old in s,old;s=s.replace(old,new)
(out/'trial.cpp').write_text(s)
original=(fp.src('geometry_memory.h')).read_text()
assert all(original.count(value)==1 for value in ('ProcessReserve=256*MiB','EntryHeadroom=256*MiB','InBuildHeadroom=128*MiB')), 'Production policy changed; rebase the explicit trial first'
for name,header in [('current',original),('256-128',original.replace('ProcessReserve=256*MiB','ProcessReserve=128*MiB').replace('EntryHeadroom=256*MiB','EntryHeadroom=128*MiB').replace('InBuildHeadroom=128*MiB','InBuildHeadroom=64*MiB'))]:
 folder=out/name;folder.mkdir(exist_ok=True);(folder/'geometry_memory.h').write_text(header);exe=folder/'trial.exe'
 cmd=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-DSHARED_SCENE_PIPELINE=1','-DCURRENT_TERRAIN_CHUNKS=1','-DCURRENT_MEMORY_PROFILE=1','-I',str(folder),*fp.test_include_flags(),str(out/'trial.cpp'),str(fp.src('world_gi.cpp')),str(fp.src('world_mesh_plan.cpp')),'-o',str(exe)]
 with (folder/'compile.log').open('w') as log:subprocess.run(cmd,env=fp.zig_env(),stdout=log,stderr=subprocess.STDOUT,check=True,creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
 for pressure in (False,True):
  with (folder/('pressure.log' if pressure else 'healthy.log')).open('w') as log:subprocess.run([str(exe),str(cache),'Northrend',*(['pressure'] if pressure else [])],stdout=log,stderr=subprocess.STDOUT,check=True,creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
  log_path=folder/('pressure.log' if pressure else 'healthy.log')
  results.append({'profile':name,'pressure':pressure,'log':str(log_path),'output':log_path.read_text()})
  print(name,'pressure',pressure,flush=True)
  print((folder/('pressure.log' if pressure else 'healthy.log')).read_text()[:2500],flush=True)

import re
healthy=[r for r in results if not r['pressure']]
for key in ('scene','plan','pagesHash','rays'):
 values=[re.search(r'\b'+key+r'=([0-9a-f]+)',r['output'])[1] for r in healthy]
 assert values[0]==values[1],('Healthy scene output differs',key,values)
for result in results:
 if result['pressure'] and 'PIPELINE step=' in result['output']:
  reference=next(r for r in healthy if r['profile']==result['profile'])
  for key in ('scene','plan','pagesHash','rays'):
   assert re.search(r'\b'+key+r'=([0-9a-f]+)',result['output'])[1]==re.search(r'\b'+key+r'=([0-9a-f]+)',reference['output'])[1], ('Pressure scene output differs',key)
assert 'TRIAL ENTRY_REFUSED' in next(r['output'] for r in results if r['profile']=='current' and r['pressure'])
assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==h for p,h in fingerprints.items()),'Production inputs changed during test'
report={'game_launched':False,'gpu_used':False,'production_inputs_unchanged':True,'healthy_scene_hashes_equal':True,'sources':fingerprints,'results':results,'limitations':['Reserve-only pressure is confined to an isolated x86 process, not the game.','The reported free-block size is conservatively capped to the screenshot witness; it is not a replay of every game mapping.','This validates CPU scene preparation and admission, not rendered images or FPS.']}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS CPU pressure experiment; healthy scene/plan/page/ray outputs match; production thresholds unchanged')
