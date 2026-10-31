#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,world-cache manual
"""Actual x86 scene builds under reserved-VA pressure, without the game or GPU.

Compare every completed region with the frozen unpressured scene/page/ray oracle.
The first region seeds a complete previous generation; ballast remains reserved
through every subsequent attempt. Refusals must preserve that generation, and
all observed in-build samples must retain the configured process reserve.
"""
from pathlib import Path
import argparse,hashlib,io,json,os,struct,subprocess,sys,zipfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline-ref',default='393c533')
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
baseline=out/'baseline-source'
with zipfile.ZipFile(io.BytesIO(subprocess.check_output(['git','archive','--format=zip',a.baseline_ref,'src'],cwd=fp.REPO))) as archive:
    archive.extractall(baseline)
driver=(fp.TESTS/'validate_shared_scene_pipeline.cpp').read_text()
prefix=r'''
#define NOMINMAX
#include <windows.h>
#include "geometry_memory.h"
using namespace NorthlightGeometryMemory;
static Sample memoryNow(){
    SYSTEM_INFO info;GetSystemInfo(&info);const uint64_t limit=uint64_t(reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress))+1;
    Sample s;s.valid=true;
    for(uint64_t at=0;at<limit;){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<void*>(uintptr_t(at)),&m,sizeof m)){s.valid=false;break;}
        const auto end=uint64_t(reinterpret_cast<uintptr_t>(m.BaseAddress))+uint64_t(m.RegionSize);if(end<=at){s.valid=false;break;}
        if(m.State==MEM_FREE){s.available+=m.RegionSize;s.largest=std::max<uint64_t>(s.largest,m.RegionSize);}at=end;}
    return s;
}
struct Ballast{
    std::vector<void*> blocks;
    ~Ballast(){for(auto* p:blocks)assert(VirtualFree(p,0,MEM_RELEASE));}
    void leave(uint64_t desired){for(unsigned attempt=0;attempt<64;++attempt){const auto s=memoryNow();assert(s.valid);if(s.available<=desired+65536)return;
        const auto bytes=std::min(s.available-desired,s.largest/2)/65536*65536;assert(bytes);
        auto* p=VirtualAlloc(nullptr,size_t(bytes),MEM_RESERVE,PAGE_NOACCESS);assert(p);blocks.push_back(p);}assert(false);}
};
'''
driver=driver.replace('int main(int argc,char** argv){',prefix+'\nint main(int argc,char** argv){')
needle='NorthlightMeshPageKey::KeySet resident;SceneSizeHint hint;'
assert driver.count(needle)==1
driver=driver.replace(needle,needle+'std::shared_ptr<BVH> previous;')
start=r'''
        Ballast ballast;const auto target=std::getenv("NORTHLIGHT_SCENE_TEST_FREE_MIB");
        if(step&&target)ballast.leave(uint64_t(std::strtoul(target,nullptr,10))*MiB);
        const auto entry=memoryNow();const auto entryBudget=buildBudget(64*MiB);
        if(!admits(entry,entryBudget)){assert(previous&&previous->triangleCount());std::printf("RESERVE_REFUSED step=%u stage=entry available=%llu required=%llu previous=%zu\n",step,(unsigned long long)entry.available,(unsigned long long)entryBudget.available,previous->triangleCount());continue;}
        uint64_t minimum=entry.available;bool allocationRefused=false;
        const auto phaseSample=[&]{const auto sample=memoryNow();assert(sample.valid);minimum=std::min(minimum,sample.available);assert(sample.available>=ProcessReserve);};
        const AllocationAdmission checked=[&](uint64_t bytes){if(bytes<MiB)return true;
            const auto sample=memoryNow();const auto budget=inBuildBudget(bytes);assert(sample.valid);minimum=std::min(minimum,sample.available);
            if(admits(sample,budget))return true;allocationRefused=true;return false;};
        const auto refused=[&](const char* stage){assert(allocationRefused);assert(previous&&previous->triangleCount());phaseSample();std::printf("RESERVE_REFUSED step=%u stage=%s minimum=%llu reserve=%llu previous=%zu\n",step,stage,(unsigned long long)minimum,(unsigned long long)ProcessReserve,previous->triangleCount());};
'''
needle='        auto all=Clock::now(),t=all;';assert driver.count(needle)==1;driver=driver.replace(needle,start+needle)
for old,new in [('local,error,{},true,provider,revision','local,error,checked,true,provider,revision'),
                ('terrain,error,1,{},terrainFilter','terrain,error,1,checked,terrainFilter'),
                ('plan,error,{},radius','plan,error,checked,radius'),('pages,error,{},options','pages,error,checked,options')]:
    assert old in driver;driver=driver.replace(old,new)
for function,stage in [('cache.build','local'),('loadInstancedScenes','terrain'),('NorthlightShadowTerrain::build','plan'),('NorthlightWorldMeshPages::build','pages')]:
    token='assert('+function+'(';cursor=0
    while (start:=driver.find(token,cursor))>=0:
        expression=start+len('assert(');depth=1;end=expression
        while depth:
            if driver[end]=='(':depth+=1
            if driver[end]==')':depth-=1
            end+=1
        assert driver[end]==';'
        replacement='if(!('+driver[expression:end-1]+')){refused("'+stage+'");continue;}phaseSample();'
        driver=driver[:start]+replacement+driver[end+1:];cursor=start+len(replacement)
driver=driver.replace('        std::puts("");','        std::puts("");phaseSample();std::printf("RESERVE_OK step=%u minimum=%llu reserve=%llu entry=%llu largest=%llu\\n",step,(unsigned long long)minimum,(unsigned long long)ProcessReserve,(unsigned long long)entry.available,(unsigned long long)entry.largest);previous=std::make_shared<BVH>(std::move(local));')
source=out/'driver.cpp';source.write_text(driver)
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
inputs={str(path):sha(path) for path in [Path(__file__),fp.TESTS/'validate_shared_scene_pipeline.cpp',source,*[p for root in [fp.REPO,baseline] for p in (root/'src').rglob('*') if p.suffix in ['.h','.inl','.cpp','.inc']]]}
binaries={}
for name,root in [('old',baseline),('new',fp.REPO)]:
    includes=[flag for folder in sorted((root/'src').iterdir()) if folder.is_dir() for flag in ['-I',str(folder)]]
    exe=out/(name+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra','-Werror','-DSHARED_SCENE_PIPELINE=1','-DCURRENT_TERRAIN_CHUNKS=1','-DCURRENT_MEMORY_PROFILE=0','-DPIPELINE_RADIUS=928',*includes,str(source),str(root/'src/gi/world_gi.cpp'),str(root/'src/world/world_mesh_plan.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    # Match the client's LAA process, as in validate_parallax_msaa.py. Only
    # this freshly compiled private executable's COFF characteristic changes.
    image=bytearray(exe.read_bytes());nt=struct.unpack_from('<I',image,0x3c)[0];assert image[:2]==b'MZ' and image[nt:nt+4]==b'PE\0\0'
    flags=nt+22;struct.pack_into('<H',image,flags,struct.unpack_from('<H',image,flags)[0]|0x20);exe.write_bytes(image)
    binaries[name]=exe
def run(name,available):
    env=fp.zig_env();env.pop('NORTHLIGHT_SCENE_TEST_FREE_MIB',None)
    if available:env['NORTHLIGHT_SCENE_TEST_FREE_MIB']=str(available)
    raw=subprocess.check_output([str(binaries[name]),str(fp.world_cache()),'Northrend'],env=env,text=True)
    (out/f'{name}-{available}.log').write_text(raw)
    return {prefix:[dict(part.split('=',1) for part in line.split()[1:]) for line in raw.splitlines() if line.startswith(prefix+' ')] for prefix in ['PIPELINE','RESERVE_OK','RESERVE_REFUSED']}
reference=run('old',0);assert len(reference['PIPELINE'])==7
oracle={row['step']:row for row in reference['PIPELINE']};results={}
for name,available in [('old',680),('new',680),('new',600),('new',512),('new',400),('new',255)]:
    rows=run(name,available);assert len(rows['RESERVE_OK'])+len(rows['RESERVE_REFUSED'])==7
    for row in rows['PIPELINE']:
        for key in ['scene','plan','pagesHash','rays','localTriangles','planTriangles','pages','batches']:assert row[key]==oracle[row['step']][key],(name,available,row['step'],key)
    for row in rows['RESERVE_OK']:assert int(row['minimum'])>=int(row['reserve'])
    if name=='old' or available<512:assert len(rows['RESERVE_REFUSED'])==6
    if name=='new' and available==680:assert len(rows['RESERVE_OK'])==7,'The measured runtime pressure must recover complete scenes'
    results[f'{name}-{available}']=rows
    print(name,available,'completed',len(rows['RESERVE_OK']),'refused',len(rows['RESERVE_REFUSED']),flush=True)
assert all(sha(Path(path))==value for path,value in inputs.items()),'Sources changed during validation'
(out/'validation.json').write_text(json.dumps({'pass':True,'game_launched':False,'gpu_used':False,'actual_x86_virtual_query':True,'ballast_held_during_build':True,'canonical_geometry_pages_and_rays_equal':True,'baseline':a.baseline_ref,'source_inputs':inputs,'results':results},indent=2)+'\n')
