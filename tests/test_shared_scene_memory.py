#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Immutable scene failure, held-generation budget and publication regression."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
out = args.output or fp.out()/'shared-scene-memory'
out.mkdir(parents=True, exist_ok=True)
prefix = (fp.TESTS/'test_local_scene_cache.cpp').read_text().split('int main(')[0]
prefix = prefix.replace('modelPath.c_str()', 'modelPath.string().c_str()').replace('(root/"tile.fg3").c_str()', '(root/"tile.fg3").string().c_str()').replace('(root/"models"/(std::string(64,\'0\')+".fgs")).c_str()', '(root/"models"/(std::string(64,\'0\')+".fgs")).string().c_str()')
body = r'''
#include <cstdlib>
#include <new>
static int64_t failAllocation=-1;
void* operator new(size_t n){if(failAllocation==0){failAllocation=-1;throw std::bad_alloc();}if(failAllocation>0)--failAllocation;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}
static void provenance(const BVH& b){uint32_t triangles=0;for(const auto& r:b.scene().sourcePieces){assert(r.sourceIdentity&&r.triangleBase==triangles&&r.triangleCount);triangles+=r.triangleCount;assert(uint64_t(r.vertexBase)+r.vertexCount<=b.scene().vertices.size());for(uint32_t i=r.triangleBase;i<r.triangleBase+r.triangleCount;++i){const auto&t=b.scene().triangles[i];assert(std::find(r.materialMap.begin(),r.materialMap.end(),t.material)!=r.materialMap.end());assert(t.v0>=r.vertexBase&&t.v1>=r.vertexBase&&t.v2>=r.vertexBase&&t.v0<r.vertexBase+r.vertexCount&&t.v1<r.vertexBase+r.vertexCount&&t.v2<r.vertexBase+r.vertexCount);}}assert(triangles==b.scene().triangles.size());}
int main(){std::setvbuf(stdout,nullptr,_IONBF,0);auto root=fs::temp_directory_path()/("shared-memory-"+std::to_string(Clock::now().time_since_epoch().count()));fixtures(root);std::string error;auto source=std::make_shared<WorldScene>();assert(loadScene((root/"models"/(std::string(64,'0')+".fgs")).string().c_str(),*source,error));source->sourceIdentity=1;SceneSourceProvider provider=[&](const std::string&,std::string&,const AllocationAdmission&)->std::shared_ptr<const WorldScene>{return source;};std::vector<std::string> files={(root/"tile.fg3").string()};const auto directory=(root/"models").string();WorldScene oracle;assert(loadInstancedScenes(files,directory,{-288,-288,-320},{288,288,320},oracle,error));
 // Optional caches may be exhausted by held generations. Complete mandatory
 // fallback geometry is still built; reset never forgives a live shared lease.
 LocalSceneCache cache({12000});std::vector<std::unique_ptr<BVH>> held;
 for(unsigned i=0;i<24;++i){auto b=std::make_unique<BVH>();assert(cache.build(files,directory,"test",{-288,-288,-320},{288,288,320},*b,error,{},true,provider));assert(cache.stats().sharedLiveBytes<=12000&&!cache.stats().legacyFallbacks);sceneEqual(oracle,b->scene());provenance(*b);if(!i)assert(!b->pieces().empty()&&b->fallbackTriangleCount());held.push_back(std::move(b));if(i%3==0)cache.reset();}
 std::vector<std::thread> readers;for(unsigned i=0;i<8;++i)readers.emplace_back([&,i]{for(unsigned j=0;j<3000;++j)held[i]->trace({0,0,100},{0,0,-1},0,700);});cache.reset();for(auto& t:readers)t.join();held.clear();BVH fresh;assert(cache.build(files,directory,"test",{-288,-288,-320},{288,288,320},fresh,error,{},true,provider));assert(!fresh.pieces().empty()&&cache.stats().sharedLiveBytes<=12000);provenance(fresh);
 // Unknown revision must never enter the reusable geometry path.
 source=std::make_shared<WorldScene>(*source);source->sourceIdentity=0;BVH unknown;assert(cache.build(files,directory,"test",{-288,-288,-320},{288,288,320},unknown,error,{},true,provider));assert(unknown.pieces().empty()&&unknown.scene().sourcePieces.empty()&&unknown.fallbackTriangleCount()==80);sceneEqual(oracle,unknown.scene());
 source->sourceIdentity=2;
 // Every ordinary allocation site can fail once. Success has exact coverage;
 // failure leaves the old published output intact. Retry recovers normally.
 unsigned failures=0,successes=0;
 for(unsigned nth=0;nth<=600;++nth){LocalSceneCache pressure({12000});BVH result;assert(result.build(WorldScene(oracle),error));failAllocation=nth==600?100000:nth;bool ok=false;try{ok=pressure.build(files,directory,"fault",{-288,-288,-320},{288,288,320},result,error,{},true,provider);}catch(...){failAllocation=-1;assert(false&&"allocation failure escaped cache API");}failAllocation=-1;if(ok)++successes;else ++failures;sceneEqual(oracle,result.scene());assert(pressure.stats().sharedLiveBytes<=12000);assert(pressure.build(files,directory,"fault",{-288,-288,-320},{288,288,320},result,error,{},true,provider));sceneEqual(oracle,result.scene());provenance(result);assert(pressure.stats().sharedLiveBytes<=12000);}
 // Atomic accounting rollback is independent of cache ownership.
 auto budget=std::make_shared<SceneMemoryBudget>(128);auto all=SceneMemoryCharge::acquire(budget,128);assert(all&&!SceneMemoryCharge::acquire(budget,1));all.reset();assert(!budget->used);std::vector<std::thread> chargeThreads;for(unsigned i=0;i<8;++i)chargeThreads.emplace_back([&]{for(unsigned j=0;j<1000;++j){auto lease=SceneMemoryCharge::acquire(budget,32);assert(budget->used<=128);}});for(auto& t:chargeThreads)t.join();assert(!budget->used);
 printf("PASS allocationSites=600 plusSuccessControl=1 caughtFailures=%u exactSuccesses=%u heldGenerations=24 readers=8 concurrentLeases=8000 sourceProvenance=exact cap=12000\n",failures,successes);fs::remove_all(root);
}
'''
(out/'memory.cpp').write_bytes((prefix+body).encode())
results = []
for label, flags in [('O2', ['-O2']), ('debug', ['-O0', '-g'])]:
    exe = out/(label+'.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-UNDEBUG', *flags,
                    *fp.test_include_flags(), str(out/'memory.cpp'), str(fp.src('world_gi.cpp')), '-o', str(exe)], env=fp.zig_env(), check=True)
    text = subprocess.check_output([str(exe)], text=True)
    (out/(label+'.log')).write_text(text, encoding='utf-8')
    print(label+'\n'+text, end='')
    results.append({'configuration': label, 'pass': True})
(out/'validation.json').write_text(json.dumps({'gameLaunched': False, 'gpuUsed': False, 'runs': results}, indent=2)+'\n', encoding='utf-8')
