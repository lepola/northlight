#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Legacy flat oracle versus production immutable placement BLAS/region TLAS, CPU only."""
from pathlib import Path
import argparse
import json
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--world-cache',type=Path)
parser.add_argument('--output',type=Path)
args=parser.parse_args()
out=args.output or fp.out()/'shared-scene-tests';out.mkdir(parents=True,exist_ok=True)
prefix=(fp.TESTS/'test_local_scene_cache.cpp').read_text().split('int main(')[0]
prefix=prefix.replace('modelPath.c_str()','modelPath.string().c_str()').replace('(root/"tile.fg3").c_str()', '(root/"tile.fg3").string().c_str()').replace('(root/"models"/(std::string(64,\'0\')+".fgs")).c_str()', '(root/"models"/(std::string(64,\'0\')+".fgs")).string().c_str()')
prefix=prefix.replace('static void run(LocalSceneCache& cache', 'static SceneSourceProvider provider;\nstatic SceneSourceRevision revisionProvider;\nstatic void run(LocalSceneCache& cache')
prefix=prefix.replace('cache.build(files,models,map,low,high,incremental,error)', 'cache.build(files,models,map,low,high,incremental,error,{},true,provider,revisionProvider)')
prefix=prefix.replace('sceneEqual(old.scene(),incremental.scene());', 'sceneEqual(old.scene(),incremental.scene());std::printf(" shared=%zu blasBuild=%llu blasReuse=%llu live=%llu fallback=%llu subset=%llu",incremental.pieces().size(),(unsigned long long)cache.stats().blasBuilds,(unsigned long long)cache.stats().blasReuses,(unsigned long long)cache.stats().sharedLiveBytes,(unsigned long long)cache.stats().legacyFallbacks,(unsigned long long)cache.stats().fallbackTriangles);')
body=r'''
#include <map>
#include <fstream>
int main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);
 auto root=fs::temp_directory_path()/("fr-shared-scene-"+std::to_string(Clock::now().time_since_epoch().count()));fixtures(root);
 std::map<std::string,std::shared_ptr<const WorldScene>> sources;uint64_t revision=1;unsigned providerCalls=0;revisionProvider=[&](const std::string&){return revision;};
 provider=[&](const std::string& path,std::string& error,const AllocationAdmission& admit)->std::shared_ptr<const WorldScene>{++providerCalls;auto found=sources.find(path);if(found!=sources.end())return found->second;auto source=std::make_shared<WorldScene>();if(!loadScene(path.c_str(),*source,error,admit))return {};source->sourceIdentity=revision;sources[path]=source;return source;};
 std::vector<std::string> files={(root/"tile.fg3").string()};LocalSceneCache cache({16u*1024u*1024u});
 for(unsigned step=0;step<8;++step){const unsigned sector=step<4?step:7-step;char name[32];snprintf(name,sizeof name,"shared synthetic%u",step);run(cache,files,(root/"models").string(),"test",{float(sector)*32,0,0},name,2000);assert(!cache.stats().legacyFallbacks&&cache.stats().tlasPieces>0&&cache.stats().sharedLiveBytes<=16u*1024u*1024u);if(step)assert(cache.stats().blasReuses>0);}
 std::string error;auto warmFile=root/"warm.fg3";{std::ifstream in(root/"tile.fg3",std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(in)),{});for(size_t i=0;i<20;++i)bytes[12+i*116+40]=1;std::ofstream out(warmFile,std::ios::binary);out.write(bytes.data(),bytes.size());}std::vector<std::string> warmFiles={warmFile.string()};LocalSceneCache warm({16u*1024u*1024u});BVH warmA,warmB;assert(warm.build(warmFiles,(root/"models").string(),"test",{-288,-288,-320},{288,288,320},warmA,error,{},true,provider,revisionProvider));const auto warmCalls=providerCalls;sources.clear();assert(warm.build(warmFiles,(root/"models").string(),"test",{-288,-288,-320},{288,288,320},warmB,error,{},true,provider,revisionProvider));assert(providerCalls==warmCalls&&warm.stats().blasBuilds==0&&warm.stats().blasReuses==20&&warm.stats().modelReads==0);sceneEqual(warmA.scene(),warmB.scene());
 BVH held;assert(cache.build(files,(root/"models").string(),"test",{-288,-288,-320},{288,288,320},held,error,{},true,provider));assert(!held.pieces().empty());
 const auto piece=held.pieces().front().geometry;const auto before=piece->trace({0,0,100},{0,0,-1},0,700);cache.reset();assert(piece->trace({0,0,100},{0,0,-1},0,700).hit==before.hit);
 // Same map/crop after provider revision changes cannot alias an old BLAS.
 sources.clear();++revision;BVH changed;assert(cache.build(files,(root/"models").string(),"test",{-288,-288,-320},{288,288,320},changed,error,{},true,provider));assert(!changed.pieces().empty()&&changed.pieces().front().geometry!=piece);
 assert(cache.stats().sharedLiveBytes<=16u*1024u*1024u);
 // The exact original median path retains its traversal-dependent tie order.
 WorldScene flat;assert(loadInstancedScenes(files,(root/"models").string(),{-288,-288,-320},{288,288,320},flat,error));BVH median;assert(median.build(std::move(flat),error,false));BVH localMedian;assert(cache.build(files,(root/"models").string(),"test",{-288,-288,-320},{288,288,320},localMedian,error,{},false,provider));assert(localMedian.pieces().empty());sceneEqual(median.scene(),localMedian.scene());compareRays(median,localMedian,{},2000);
 // Tiny optional acceleration budget must preserve the complete scene via legacy fallback.
 LocalSceneCache tiny({1});run(tiny,files,(root/"models").string(),"test",{},"shared fallback",1000);assert(tiny.stats().legacyFallbacks==0&&tiny.stats().fallbackTriangles==80&&tiny.stats().sharedLiveBytes==0);
 BVH saved;assert(cache.build(files,(root/"models").string(),"test",{-288,-288,-320},{288,288,320},saved,error,{},true,provider));const auto oldTriangles=saved.triangleCount();
 assert(!cache.build(files,(root/"models").string(),"test",{-100,-100,-100},{100,100,100},saved,error,[](uint64_t){return false;},true,provider));assert(saved.triangleCount()==oldTriangles);
 // Provider failures are returned; no second legacy decode hides the error.
 unsigned calls=0;SceneSourceProvider fail=[&](const std::string&,std::string& e,const AllocationAdmission&)->std::shared_ptr<const WorldScene>{++calls;e="expected provider failure";return {};};
 assert(!cache.build(files,(root/"models").string(),"test",{-288,-288,-320},{288,288,320},saved,error,{},true,fail)&&calls==1&&error=="expected provider failure"&&saved.triangleCount()==oldTriangles);
 std::vector<std::thread> readers;for(unsigned i=0;i<8;++i)readers.emplace_back([&]{for(unsigned k=0;k<1000;++k)held.trace({0,0,100},{0,0,-1},0,700);});cache.reset();for(auto& t:readers)t.join();
 fs::remove_all(root);
 if(argc>1){sources.clear();++revision;LocalSceneCache real({16u*1024u*1024u});
  for(unsigned step=0;step<5;++step){Vec3 center(5244+float(step<3?step:4-step)*16,-35,492);char name[32];snprintf(name,sizeof name,"Northrend shared%u",step);run(real,tiles(argv[1],"Northrend",center),(fs::path(argv[1])/"models").string(),"Northrend",center,name,12000);assert(real.stats().sharedLiveBytes<=16u*1024u*1024u&&!real.stats().legacyFallbacks&&real.stats().tlasPieces>0);}
 }
 std::puts("PASS immutable source/placement BLAS + TLAS, exact flat scene/rays/occlusion/probes, crop boundaries, global ties, median fallback, revision/reset/lifetime/admission/provider failures/concurrent readers");
}
'''
(out/'shared.cpp').write_bytes((prefix+body).encode())
results=[]
for label,flags in [('O2',['-O2']),('debug',['-O0','-g'])]:
    exe=out/(label+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-UNDEBUG',*flags,'-Wall','-Wextra','-Werror',*fp.test_include_flags(),str(out/'shared.cpp'),str(fp.src('world_gi.cpp')),'-o',str(exe)],env=fp.zig_env(),check=True)
    command=[str(exe),*([str(args.world_cache)] if args.world_cache and label=='O2' else [])]
    result=subprocess.check_output(command,text=True);(out/(label+'.log')).write_text(result,encoding='utf-8');print(label+'\n'+result,end='');results.append({'configuration':label,'pass':True,'worldCache':bool(args.world_cache and label=='O2')})
(out/'validation.json').write_bytes((json.dumps({'gameLaunched':False,'gpuUsed':False,'runs':results},indent=2)+'\n').encode())
