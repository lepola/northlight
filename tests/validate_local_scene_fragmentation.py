#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,world-cache
"""Compare complete local scenes and rays in genuinely fragmented x86 virtual address space."""
from pathlib import Path
import argparse,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--available-mib',type=int,default=0,help='Reserve free virtual address space until this approximate amount remains')
parser.add_argument('--gap-mib',type=int,default=40,help='Maximum spacing between reserved gaps')
parser.add_argument('--cold-recovery',action='store_true',help='Exercise bounded cold-recovery admission with its retained guard')
parser.add_argument('--expect-defer',action='store_true',help='Expect a safe refusal during the cold shadow plan')
parser.add_argument('--center',type=float,nargs=3,default=[5834,680,646])
args=parser.parse_args()
assert args.available_mib>=0 and 34<=args.gap_mib<=512
out=args.output;out.mkdir(parents=True,exist_ok=False)
s=(fp.TESTS/'validate_shared_scene_pipeline.cpp').read_text().replace('Vec3(5248,10,508)','Vec3('+','.join(str(x) for x in args.center)+')')
s=s.replace('int main(int argc,char** argv){', 'int main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);')
s=s.replace('const bool bake=argc==4;', 'const bool bake=true;const bool fragmented=argc==4;').replace('i<probeLayout().count();++i','i<64;++i')
s=s.replace('h.bytes(s.vertices.data(),s.vertices.size()*sizeof(WorldVertex));h.bytes(s.triangles.data(),s.triangles.size()*sizeof(WorldTriangle));','for(const auto& v:s.vertices)h.bytes(&v,sizeof v);for(const auto& t:s.triangles)h.bytes(&t,sizeof t);')
prefix=r"""
#include <windows.h>
#undef near
#undef far
#include <vector>
#include <algorithm>
#include <cassert>
#include "geometry_memory.h"
using namespace NorthlightGeometryMemory;
static Sample physicalSample(){SYSTEM_INFO info;GetSystemInfo(&info);const uint64_t limit=uint64_t(reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress))+1;Sample s;s.valid=true;for(uint64_t p=0;p<limit;){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery((const void*)uintptr_t(p),&m,sizeof m)){s.valid=false;break;}const uint64_t end=uint64_t(uintptr_t(m.BaseAddress))+m.RegionSize;if(end<=p){s.valid=false;break;}if(m.State==MEM_FREE){s.available+=m.RegionSize;s.largest=std::max(s.largest,uint64_t(m.RegionSize));}p=end;}return s;}
static constexpr uint64_t targetAvailable=TARGET_AVAILABLE*MiB,gap=GAP_MIB*MiB;
struct Fragment{std::vector<void*> blocks;explicit Fragment(bool enable){blocks.reserve(256);if(!enable)return;SYSTEM_INFO info;GetSystemInfo(&info);const uint64_t limit=uint64_t(reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress))+1;for(uint64_t p=0;p<limit;){MEMORY_BASIC_INFORMATION m{};assert(VirtualQuery((const void*)uintptr_t(p),&m,sizeof m));const uint64_t end=uint64_t(uintptr_t(m.BaseAddress))+m.RegionSize;assert(end>p);if(m.State==MEM_FREE){const uint64_t base=(uint64_t(uintptr_t(m.BaseAddress))+65535)&~uint64_t(65535);for(uint64_t x=base+gap;x+65536<=end;x+=gap){void* v=VirtualAlloc((void*)uintptr_t(x),65536,MEM_RESERVE,PAGE_NOACCESS);assert(v);blocks.push_back(v);}}p=end;}if(targetAvailable){for(;;){auto sample=physicalSample();if(sample.available<=targetAvailable+MiB)break;const uint64_t amount=std::min<uint64_t>(16*MiB,(sample.available-targetAvailable)&~uint64_t(65535));if(!amount)break;void* v=VirtualAlloc(nullptr,size_t(amount),MEM_RESERVE,PAGE_NOACCESS);assert(v);blocks.push_back(v);}}}~Fragment(){for(void* v:blocks)assert(VirtualFree(v,0,MEM_RELEASE));}};
"""
s=prefix.replace('TARGET_AVAILABLE',str(args.available_mib)).replace('GAP_MIB',str(args.gap_mib))+s
s=s.replace('        auto all=Clock::now(),t=all;',r"""        Fragment gaps(fragmented);const auto initial=physicalSample();if(fragmented){std::printf("FRAGMENT freeMiB=%.3f largestMiB=%.3f blocks=%zu\\n",double(initial.available)/MiB,double(initial.largest)/MiB,gaps.blocks.size());assert(initial.valid&&initial.largest<=gap+MiB);assert(admits(initial,chunkBuildBudget()));}
        uint64_t minimum=initial.available;unsigned refusals=0;const AllocationAdmission admission=[&](uint64_t n){if(!fragmented||n<MiB)return true;auto current=physicalSample();minimum=std::min(minimum,current.available);auto budget=inBuildBudget(n);if(!admits(current,budget)){++refusals;std::printf("DENIED bytes=%llu freeMiB=%.3f largestMiB=%.3f requiredMiB=%.3f\\n",(unsigned long long)n,double(current.available)/MiB,double(current.largest)/MiB,double(budget.largest)/MiB);return false;}return true;};
        auto all=Clock::now(),t=all;""")
s=s.replace('assert(cache.build(nearFiles,root+"/models",map,c-Vec3(288,288,320),c+Vec3(288,288,320),local,error,{},true,provider,revision));','if(!cache.build(nearFiles,root+"/models",map,c-Vec3(288,288,320),c+Vec3(288,288,320),local,error,admission,true,provider,revision,fragmented)){std::printf("LOCAL_FAIL %s refusals=%u\\n",error.c_str(),refusals);return 2;}')
s=s.replace('plan,error,{},radius,{},true,&local,&preparation','plan,error,admission,radius,{},true,&local,&preparation')
s=s.replace('terrain,error,1,{},terrainFilter','terrain,error,1,admission,terrainFilter')
s=s.replace('pages,error,{},options','pages,error,admission,options')
s=s.replace(',true\n#endif\n        ));',',true,fragmented\n#endif\n        ));')
s=s.replace('assert(NorthlightShadowTerrain::build(local.scene(),terrain,c,plan,error,admission,radius,{},true,&local,&preparation));','assert(fragmented?NorthlightShadowTerrain::buildConsuming(local.scene(),terrain,c,plan,error,admission,radius,{},&local):NorthlightShadowTerrain::build(local.scene(),terrain,c,plan,error,admission,radius,{},true,&local,&preparation));')
s=s.replace('        double localMs=ms(t);t=Clock::now();','        std::printf("LOCAL_CHUNKED mode=%u vertices=%zu triangles=%zu refusals=%u\\n",unsigned(fragmented),local.scene().vertices.size(),local.scene().triangles.size(),refusals);\n        double localMs=ms(t);t=Clock::now();')
# The worker drops the temporary source terrain after the shadow plan owns its data.
s=s.replace('        double planMs=ms(t);t=Clock::now();NorthlightWorldMeshPages::Plan pages;', '        terrain=WorldScene{};double planMs=ms(t);t=Clock::now();NorthlightWorldMeshPages::Plan pages;')
s=s.replace('        Hash localHash,planHash,pageHash,rayHash,bakeHash;',r'''        {auto current=physicalSample();minimum=std::min(minimum,current.available);std::printf("SAMPLED_MINIMUM freeMiB=%.3f\n",double(minimum)/MiB);assert(minimum>=ProcessReserve);}
        Hash indexDebug,batchDebug,materialDebug,chunkDebug,recordDebug;
        indexDebug.word(plan.pagingIndexCount());for(size_t n=0;n<plan.pagingIndexCount();++n)indexDebug.word(plan.pagingIndex(n));
        for(const auto& b:plan.batches)batch(batchDebug,b);for(const auto& m:plan.materials){materialDebug.word(m.width);materialDebug.word(m.height);materialDebug.bytes(m.bgra.data(),m.bgra.size());materialDebug.bytes(&m.alphaCutoff,4);materialDebug.word(m.terrain);}
        for(const auto& c:plan.fixedTerrainChunks){chunkDebug.word(uint32_t(c.first));chunkDebug.word(uint32_t(c.second));}
        if(plan.localShadowRecords)for(const auto& r:*plan.localShadowRecords){vec(recordDebug,r.low);vec(recordDebug,r.high);recordDebug.word(r.content.words[0]);recordDebug.word(r.content.words[1]);recordDebug.word(r.content.triangles);recordDebug.word(uint32_t(r.chunkX));recordDebug.word(uint32_t(r.chunkY));recordDebug.word(r.terrain);}
        std::printf("DETAIL records=%zu",plan.localShadowRecords?plan.localShadowRecords->size():0);printHash("indices",indexDebug);printHash("batches",batchDebug);printHash("materials",materialDebug);printHash("chunks",chunkDebug);printHash("records",recordDebug);std::puts("");
        Hash localHash,planHash,pageHash,rayHash,bakeHash;''')
if args.cold_recovery:
 s=s.replace('admits(initial,chunkBuildBudget())','admits(initial,coldChunkBuildBudget())').replace('auto budget=inBuildBudget(n);','auto budget=coldInBuildBudget(n);').replace('assert(minimum>=ProcessReserve);','assert(minimum>=ProcessReserve+ColdInBuildHeadroom-MiB);')
if args.expect_defer:
 old='assert(fragmented?NorthlightShadowTerrain::buildConsuming(local.scene(),terrain,c,plan,error,admission,radius,{},&local):NorthlightShadowTerrain::build(local.scene(),terrain,c,plan,error,admission,radius,{},true,&local,&preparation));'
 assert old in s
 s=s.replace(old,'if(!(fragmented?NorthlightShadowTerrain::buildConsuming(local.scene(),terrain,c,plan,error,admission,radius,{},&local):NorthlightShadowTerrain::build(local.scene(),terrain,c,plan,error,admission,radius,{},true,&local,&preparation))){auto now=physicalSample();std::printf("SAFE_DEFER freeMiB=%.3f refusals=%u\\n",double(now.available)/MiB,refusals);assert(refusals&&now.available>=ProcessReserve+ColdInBuildHeadroom-MiB);return 17;}')
(out/'probe.cpp').write_text(s);exe=out/'test.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-DSHARED_SCENE_PIPELINE=1','-DCURRENT_TERRAIN_CHUNKS=1','-DPIPELINE_RADIUS=928',*fp.test_include_flags(),str(out/'probe.cpp'),str(fp.src('world_gi.cpp')),str(fp.src('world_mesh_plan.cpp')),'-o',str(exe)],env=fp.zig_env(),check=True)
results={}
for mode in ['normal','fragmented']:
 p=subprocess.run([str(exe),str(fp.world_cache()),'Northrend',*(['fragmented'] if mode=='fragmented' else [])],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);text=p.stdout.replace('\\n','\n');(out/(mode+'.log')).write_text(text);print(mode,text,flush=True)
 if mode=='fragmented' and args.expect_defer:
  assert p.returncode==17 and 'SAFE_DEFER' in text;print('PASS safe cold recovery refusal retains process reserve');sys.exit(0)
 p.check_returncode()
 results[mode]=text
for prefix in ['DETAIL ']:
 a=[line for line in results['normal'].splitlines() if line.startswith(prefix)];b=[line for line in results['fragmented'].splitlines() if line.startswith(prefix)];assert len(a)==7 and a==b,(a,b)
for field in ['scene','pagesHash','rays','bakeHash','localTriangles','planTriangles','pages','batches']:
 rows=[[dict(part.split('=',1) for part in l.split()[1:]) for l in results[mode].splitlines() if l.startswith('PIPELINE ')] for mode in ['normal','fragmented']];assert len(rows[0])==len(rows[1])==7
 for a,b in zip(*rows):assert a[field]==b[field],(field,a,b)
print('PASS bounded local, terrain, BVH, shadow pages and rays match')
