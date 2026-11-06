#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Exact MCNK run membership, chunk reuse, material/vertex maps and fallback."""
from pathlib import Path
import argparse
import json
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
out = args.output or fp.out()/'shared-scene-chunks'
out.mkdir(parents=True, exist_ok=True)
prefix = (fp.TESTS/'test_local_scene_cache.cpp').read_text().split('int main(')[0]
prefix = prefix.replace('modelPath.c_str()', 'modelPath.string().c_str()').replace('(root/"tile.fg3").c_str()', '(root/"tile.fg3").string().c_str()').replace('(root/"models"/(std::string(64,\'0\')+".fgs")).c_str()', '(root/"models"/(std::string(64,\'0\')+".fgs")).string().c_str()')
body = r'''
static void chunkFixtures(const fs::path& root,bool interleaved){
 fs::create_directories(root/"models");WorldScene model;WorldMaterial material;material.albedo={.7f,.3f,.2f};material.width=2;material.height=2;material.rgba={200,50,100,255,80,210,40,0,30,30,180,255,240,140,20,255};model.materials.push_back(material);
 constexpr float step=100.f/3.f;
 // Shared boundary vertices deliberately exercise non-contiguous local-to-flat
 // vertex maps; one chunk must not assume a private consecutive vertex span.
 for(unsigned y=0;y<3;++y)for(unsigned x=0;x<4;++x)model.vertices.push_back({{float(x)*step,float(y)*step,0},{.2f,.3f,1},float(x)*1.37f,float(y)*1.19f});
 for(unsigned y=0;y<2;++y)for(unsigned x=0;x<3;++x){uint32_t a=y*4+x,b=a+1,c=a+5,d=a+4;model.triangles.push_back({a,b,c,0});model.triangles.push_back({a,c,d,0});
  const auto base=uint32_t(model.vertices.size());for(Vec3 p:{Vec3(x*step+2,y*step+2,2),Vec3(x*step+8,y*step+2,2),Vec3(x*step+8,y*step+8,2),Vec3(x*step+2,y*step+8,2)})model.vertices.push_back({p,{0,0,1},p.x/17,p.y/19});model.triangles.push_back({base,base+1,base+2,0});model.triangles.push_back({base,base+2,base+3,0});
 }
 // Neither an unclassifiable straddler nor a degenerate may disappear.
 const auto base=uint32_t(model.vertices.size());for(Vec3 p:{Vec3(-40,10,4),Vec3(110,10,4),Vec3(0,80,4)})model.vertices.push_back({p,{0,0,1},p.x/13,p.y/11});model.triangles.push_back({base,base+1,base+2,0});model.triangles.push_back({0,0,0,0});
 if(interleaved)std::swap(model.triangles[1],model.triangles[5]);
 FILE*f=fopen((root/"models"/(std::string(64,'0')+".fgs")).string().c_str(),"wb");assert(f);fwrite("FGS2",1,4,f);u32(f,2);u32(f,uint32_t(model.vertices.size()));u32(f,uint32_t(model.triangles.size()));u32(f,1);for(const auto&v:model.vertices){vec(f,v.position);vec(f,v.normal);f32(f,v.u);f32(f,v.v);}for(const auto&t:model.triangles){u32(f,t.v0);u32(f,t.v1);u32(f,t.v2);u32(f,t.material);}vec(f,material.albedo);u32(f,2);u32(f,2);f32(f,.5f);u32(f,16);fwrite(material.rgba.data(),1,16,f);fclose(f);
 f=fopen((root/"tile.fg3").string().c_str(),"wb");assert(f);fwrite("FGS3",1,4,f);u32(f,3);u32(f,1);uint8_t hash[32]={};fwrite(hash,1,32,f);u32(f,17);u32(f,0);u32(f,0);vec(f,{-40,0,-5});vec(f,{110,80,10});for(float v:{1.f,0.f,0.f,0.f,1.f,0.f,0.f,0.f,1.f})f32(f,v);vec(f,{});fclose(f);
}
static void refs(const BVH& b){for(const auto& ref:b.pieces()){const auto&s=ref.geometry->scene();for(size_t i=0;i<s.triangles.size();++i){const auto&a=s.triangles[i];const auto&t=b.scene().triangles[ref.triangleBase+i];const uint32_t local[]={a.v0,a.v1,a.v2},global[]={t.v0,t.v1,t.v2};for(unsigned j=0;j<3;++j){const auto&v=s.vertices[local[j]];const auto&w=b.scene().vertices[global[j]];assert(exact(v.position,w.position)&&exact(v.normal,w.normal)&&v.u==w.u&&v.v==w.v);}assert(ref.materialMap[a.material]==t.material);const auto&m=ref.geometry->material(a.material);const auto&n=b.scene().materials[t.material];assert(exact(m.albedo,n.albedo)&&m.rgba==n.rgba&&m.alphaCutoff==n.alphaCutoff&&m.addressU==n.addressU&&m.addressV==n.addressV);}}}
int main(){auto root=fs::temp_directory_path()/("shared-chunks-"+std::to_string(Clock::now().time_since_epoch().count()));chunkFixtures(root,false);std::string error;std::shared_ptr<WorldScene> source;uint64_t revision=1;const auto directory=(root/"models").string();const auto model=(root/"models"/(std::string(64,'0')+".fgs")).string();SceneSourceProvider provider=[&](const std::string&,std::string& e,const AllocationAdmission& admit)->std::shared_ptr<const WorldScene>{if(!source){source=std::make_shared<WorldScene>();if(!loadScene(model.c_str(),*source,e,admit))return {};source->sourceIdentity=revision;source->materials[0].addressU=2;source->materials[0].addressV=3;}return source;};SceneSourceRevision lookup=[&](const std::string&){return revision;};std::vector<std::string> files={(root/"tile.fg3").string()};LocalSceneCache cache({64*1024});
 uint64_t totalBuilds=0,totalReuses=0;
 for(unsigned i=0;i<7;++i){const float x=i<4?float(i)*8:float(6-i)*8;Vec3 lo{x,-1,-10},hi{x+70,67,10};WorldScene flat;assert(loadInstancedScenes(files,(root/"models").string(),lo,hi,flat,error,15,{}, {}, {},nullptr,provider));BVH baseline,b;assert(baseline.build(std::move(flat),error));assert(cache.build(files,(root/"models").string(),"chunks",lo,hi,b,error,{},true,provider,lookup));sceneEqual(baseline.scene(),b.scene());refs(b);compareRays(baseline,b,{45,30,0},3000);totalBuilds+=cache.stats().chunkBuilds;totalReuses+=cache.stats().chunkReuses;assert(cache.stats().sharedLiveBytes<=64*1024&&!cache.stats().legacyFallbacks&&b.fallbackTriangleCount()>0);if(i==1)assert(cache.stats().chunkReuses>0);}
 assert(totalBuilds&&totalReuses);
 // Different original ordinal order must never reuse an earlier chunk BLAS.
 chunkFixtures(root,true);source.reset();++revision;BVH interleaved;assert(cache.build(files,(root/"models").string(),"chunks",{-1,-1,-10},{101,67,10},interleaved,error,{},true,provider,lookup));refs(interleaved);assert(!cache.stats().chunkReuses);WorldScene exactScene;assert(loadInstancedScenes(files,(root/"models").string(),{-1,-1,-10},{101,67,10},exactScene,error,15,{}, {}, {},nullptr,provider));BVH exactBvh;assert(exactBvh.build(std::move(exactScene),error));sceneEqual(exactBvh.scene(),interleaved.scene());compareRays(exactBvh,interleaved,{45,30,0},3000);
 LocalSceneCache denied({64*1024});BVH deniedWorld;unsigned deniedMaps=0,smallAdmissions=0;assert(denied.build(files,(root/"models").string(),"chunks",{-1,-1,-10},{101,67,10},deniedWorld,error,[&](uint64_t bytes){if(bytes==4&&++smallAdmissions==2){++deniedMaps;return false;}return true;},true,provider,lookup));assert(deniedMaps==1&&deniedWorld.pieces().size()+1==interleaved.pieces().size());sceneEqual(exactBvh.scene(),deniedWorld.scene());compareRays(exactBvh,deniedWorld,{45,30,0},1000);
 LocalSceneCache tiny({1});BVH fallback;assert(tiny.build(files,(root/"models").string(),"chunks",{-1,-1,-10},{101,67,10},fallback,error,{},true,provider,lookup));assert(fallback.pieces().empty()&&!tiny.stats().sharedLiveBytes);sceneEqual(exactBvh.scene(),fallback.scene());compareRays(exactBvh,fallback,{45,30,0},1000);
 unsigned faultFailures=0,faultSuccesses=0;for(unsigned nth=0;nth<600;++nth){LocalSceneCache pressure({64*1024});BVH result;assert(result.build(WorldScene(exactBvh.scene()),error));failAllocation=nth;bool ok=false;try{ok=pressure.build(files,directory,"fault",{-1,-1,-10},{101,67,10},result,error,{},true,provider,lookup);}catch(...){failAllocation=-1;assert(false&&"chunk allocation escaped");}failAllocation=-1;if(ok){++faultSuccesses;refs(result);}else ++faultFailures;sceneEqual(exactBvh.scene(),result.scene());assert(pressure.stats().sharedLiveBytes<=64*1024);assert(pressure.build(files,directory,"fault",{-1,-1,-10},{101,67,10},result,error,{},true,provider,lookup));sceneEqual(exactBvh.scene(),result.scene());refs(result);}
 assert(faultFailures&&faultSuccesses);printf("CHUNK_FAULTS sites=600 preservedFailures=%u exactOptionalFallbackSuccesses=%u\n",faultFailures,faultSuccesses);
 LocalSceneCache noUnused({64*1024});BVH noUnusedWorld;unsigned deniedUnused=0;const auto rawOrder=uint64_t(source->triangles.size())*sizeof(uint32_t);assert(noUnused.build(files,(root/"models").string(),"guard",{-1,-1,-10},{101,67,10},noUnusedWorld,error,[&](uint64_t bytes){if(bytes==rawOrder){++deniedUnused;return false;}return true;},true,provider,lookup));assert(!deniedUnused);sceneEqual(exactBvh.scene(),noUnusedWorld.scene());refs(noUnusedWorld);
 printf("PASS chunkBuilds=%llu chunkReuses=%llu exactGlobalVertexTriangleMaterialMaps=1 interleavedRevision=1 straddlingBoundaryDegenerateFallback=1 sameBudget=1\n",(unsigned long long)totalBuilds,(unsigned long long)totalReuses);fs::remove_all(root);
}
'''
allocation_code=(fp.TESTS/'test_shared_scene_memory.py').read_text().split("body = r'''")[1].split('static void provenance')[0]
(out/'chunks.cpp').write_bytes((prefix+allocation_code+body).encode())
results = []
for label, flags in [('O2', ['-O2']), ('debug', ['-O0', '-g'])]:
    exe = out/(label+'.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-UNDEBUG', *flags,
                    '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', *fp.test_include_flags(), str(out/'chunks.cpp'), str(fp.src('world_gi.cpp')), '-o', str(exe)], env=fp.zig_env(), check=True)
    text = subprocess.check_output([str(exe)], text=True)
    (out/(label+'.log')).write_text(text, encoding='utf-8')
    print(label+'\n'+text, end='')
    results.append({'configuration': label, 'pass': True})
(out/'validation.json').write_text(json.dumps({'gameLaunched': False, 'gpuUsed': False, 'runs': results}, indent=2)+'\n', encoding='utf-8')
