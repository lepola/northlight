#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Native production readback/worker scene parity against the backed-up 0.3.127 code.

No game or D3D device is launched. The fixture supplies thread-checked fake
textures while compiling the actual actorMaterial methods and old/new resolvers.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import ast
import hashlib
import json
import subprocess

HERE = Path(__file__).resolve().parent
OUT = fp.output_dir()
FIXTURE = OUT / 'actor-decode-fixture'
BASE = fp.FIXTURES / 'actor-decode-0.3.127'   # archived 0.3.127 sources (from records/validation-0.3.128/actor-decode-fixture/baseline)


def literal(path, name):
    return next(ast.literal_eval(node.value) for node in ast.parse(path.read_text()).body
                if isinstance(node, ast.Assign) and any(isinstance(target, ast.Name) and target.id == name for target in node.targets))


def material_method(source):
    start = source.index('    bool actorMaterial(')
    # Stop at this method's closing brace, independent of subsequent GI helpers.
    return source[start:source.index('\n    }', start) + len('\n    }')]


stub = literal(HERE / 'test_terrain_snapshot.py', 'stub').replace(
    'D3DFMT_INDEX16=101,D3DFMT_INDEX32=102',
    'D3DFMT_INDEX16=101,D3DFMT_INDEX32=102,D3DFMT_A8R8G8B8=21,D3DFMT_X8R8G8B8=22,D3DFMT_R5G6B5=23,D3DFMT_X1R5G5B5=24,D3DFMT_A1R5G5B5=25,D3DFMT_A4R4G4B4=26,D3DFMT_DXT1=1001,D3DFMT_DXT3=1003,D3DFMT_DXT5=1005')
stub += r'''
constexpr unsigned D3DRTYPE_TEXTURE=3,D3DUSAGE_RENDERTARGET=1,D3DUSAGE_DEPTHSTENCIL=2;
struct D3DSURFACE_DESC { unsigned Width=0,Height=0,Usage=0;D3DFORMAT Format=D3DFMT_A8R8G8B8; };
struct D3DLOCKED_RECT { int Pitch=0;void* pBits=nullptr; };
struct IDirect3DBaseTexture9 { virtual unsigned GetType()=0; };
struct IDirect3DTexture9:IDirect3DBaseTexture9 {
 virtual unsigned GetLevelCount()=0;
 virtual HRESULT GetLevelDesc(UINT,D3DSURFACE_DESC*)=0;
 virtual HRESULT LockRect(UINT,D3DLOCKED_RECT*,void*,DWORD)=0;
 virtual HRESULT UnlockRect(UINT)=0;
};
'''
fixture = literal(HERE / 'test_actor_deformation.py', 'harness').split('int main(')[0]
harness = fixture + r'''
#include "actor_scene_job.h"
#include "before_actor_texture.h"
#include "before_actor_scene_job.h"
#include <future>
#include <thread>
#include <tuple>
namespace Current=NorthlightActorGeometry;
namespace Before=NorthlightActorGeometryBefore;
struct BeforeReader { using V=NorthlightGI::Vec3;
BEFORE_MATERIAL_METHOD
};
struct CurrentReader {
CURRENT_MATERIAL_METHOD
};
struct Texture:IDirect3DTexture9 {
 std::thread::id renderThread=std::this_thread::get_id();
 D3DSURFACE_DESC desc;
 std::vector<std::uint8_t> bytes;
 unsigned levels=1,kind=D3DRTYPE_TEXTURE,locks=0,unlocks=0,descCalls=0,lockedLevel=~0u;
 int pitch=0;
 bool largeFirst=false,failDesc=false,failLock=false,failUnlock=false,nullBits=false;
 void thread()const {assert(std::this_thread::get_id()==renderThread);}
 unsigned GetType()override{thread();return kind;}
 unsigned GetLevelCount()override{thread();return levels;}
 HRESULT GetLevelDesc(UINT level,D3DSURFACE_DESC* out)override{thread();++descCalls;if(failDesc)return D3DERR_INVALIDCALL;*out=desc;if(largeFirst&&level==0)out->Width=out->Height=256;return D3D_OK;}
 HRESULT LockRect(UINT level,D3DLOCKED_RECT* out,void* area,DWORD flags)override{thread();assert(!area&&flags==D3DLOCK_READONLY);++locks;if(failLock)return D3DERR_INVALIDCALL;lockedLevel=level;out->Pitch=pitch;out->pBits=nullBits?nullptr:bytes.data();return D3D_OK;}
 HRESULT UnlockRect(UINT level)override{thread();assert(lockedLevel==level);++unlocks;return failUnlock?D3DERR_INVALIDCALL:D3D_OK;}
};
std::uint32_t randomState=0x174ab81u;
std::uint32_t randomWord(){randomState=randomState*1664525u+1013904223u;return randomState;}
Texture texture(D3DFORMAT format,unsigned width,unsigned height,unsigned padding=0){
 Texture t;t.desc.Width=width;t.desc.Height=height;t.desc.Format=format;
 bool compressed=format==D3DFMT_DXT1||format==D3DFMT_DXT3||format==D3DFMT_DXT5;
 unsigned rows=compressed?(height+3)/4:height;
 t.pitch=int((compressed?((width+3)/4)*(format==D3DFMT_DXT1?8:16):width*(format==D3DFMT_R5G6B5?2:4))+padding);
 t.bytes.resize(std::size_t(t.pitch)*rows);for(auto& byte:t.bytes)byte=std::uint8_t(randomWord()>>24);return t;
}
NorthlightGI::WorldMaterial neutral(bool alpha){NorthlightGI::WorldMaterial m;m.albedo={.35f,.35f,.35f};m.alphaCutoff=alpha?.5f:0.f;m.addressU=3;m.addressV=2;return m;}
Current::Packet packet(){
 Current::Packet p;auto words=code(3);assert(compile(words.data(),words.size(),p.position));assert(compile(words.data(),words.size(),p.uv,true));p.hasUV=true;
 p.mesh.vertexCount=3;p.mesh.primitiveCount=1;p.mesh.indexed=true;p.mesh.indices={0,1,2};p.mesh.streams[0].stride=24;p.mesh.streams[0].bytes.resize(72);
 float positions[3][3]={{2,3,4},{3,3,4},{2,4,4}};float uv[]={.1f,.2f};for(unsigned i=0;i<3;++i){auto* out=p.mesh.streams[0].bytes.data()+i*24;std::memcpy(out,positions[i],12);std::memcpy(out+12,uv,8);out[20]=1;}
 p.elements={{0,0,2,0,0,0},{0,12,1,0,5,0},{0,20,5,0,2,0},{0xff,0,17,0,0,0}};
 for(unsigned j=0;j<4;++j)p.inverseView[j*5]=1;
 p.constants[34*4]=1;p.constants[35*4+1]=1;p.constants[36*4+2]=1;p.constants[34*4+3]=100;p.constants[35*4+3]=10;
 p.constants[6*4]=2;p.constants[6*4+1]=3;p.constants[7*4]=.25f;p.constants[7*4+1]=-.5f;
 p.material=neutral(false);return p;
}
Before::Packet oldPacket(const Current::Packet& p){
 Before::Packet out;out.mesh=p.mesh;out.sharedMesh=p.sharedMesh;out.position=p.position;out.uv=p.uv;out.elements=p.elements;out.constants=p.constants;out.inverseView=p.inverseView;out.material=p.material;out.hasUV=p.hasUV;out.alphaTest=p.alphaTest;
 if(!p.texture.bytes.empty()){
  const auto& t=p.texture;
  if(NorthlightActorTextureBefore::decode(t.bytes.data(),t.bytes.size(),t.width,t.height,t.pitch,static_cast<NorthlightActorTextureBefore::Format>(t.format),out.material.rgba)){
   out.material.width=t.width;out.material.height=t.height;out.material.albedo={1,1,1};
  }else{out.material.rgba.clear();out.material.width=out.material.height=0;out.material.albedo={.35f,.35f,.35f};}
 }
 return out;
}
void sameMaterial(const NorthlightGI::WorldMaterial& a,const NorthlightGI::WorldMaterial& b){
 assert(a.rgba==b.rgba&&a.width==b.width&&a.height==b.height&&a.alphaCutoff==b.alphaCutoff&&a.addressU==b.addressU&&a.addressV==b.addressV&&a.terrain==b.terrain&&a.wmo==b.wmo);
 assert(std::memcmp(&a.albedo,&b.albedo,sizeof(a.albedo))==0);
}
void sameScene(const Current::Result& a,const Before::Result& b){
 assert(a.hash==b.hash&&a.scene&&b.scene);assert(a.scene->vertices.size()==b.scene->vertices.size()&&a.scene->triangles.size()==b.scene->triangles.size()&&a.scene->materials.size()==b.scene->materials.size());
 if(!a.scene->vertices.empty())assert(std::memcmp(a.scene->vertices.data(),b.scene->vertices.data(),a.scene->vertices.size()*sizeof(NorthlightGI::WorldVertex))==0);
 if(!a.scene->triangles.empty())assert(std::memcmp(a.scene->triangles.data(),b.scene->triangles.data(),a.scene->triangles.size()*sizeof(NorthlightGI::WorldTriangle))==0);
 for(std::size_t i=0;i<a.scene->materials.size();++i)sameMaterial(a.scene->materials[i],b.scene->materials[i]);
}
Current::Result compare(Current::ActorJob job){
 Before::ActorJob old;old.center=job.center;for(auto& p:job.packets)old.packets.push_back(oldPacket(p));
 // The texture fake aborts on any worker-side D3D access. Neither packet can
 // retain that interface, and the queued owner outlives all capture sources.
 auto queued=std::make_shared<const Current::ActorJob>(std::move(job));
 auto future=std::async(std::launch::async,[queued]{return queued->resolve();});queued.reset();
 auto result=future.get();sameScene(result,old.resolve());return result;
}
int main(){
 BeforeReader before;CurrentReader current;unsigned paired=0;
 for(auto format:{D3DFMT_DXT1,D3DFMT_DXT3,D3DFMT_DXT5,D3DFMT_A8R8G8B8,D3DFMT_X8R8G8B8,D3DFMT_R5G6B5})
 for(unsigned width:{1u,3u,4u,5u,31u,128u})for(unsigned height:{1u,2u,4u,7u,17u,128u})for(unsigned padding:{0u,13u}){
  auto source=texture(format,width,height,padding);source.largeFirst=true;source.levels=2;auto oldSource=source;
  auto m=neutral(true);NorthlightActorTexture::Snapshot snapshot;
  assert(before.actorMaterial(&oldSource,m)&&current.actorMaterial(&source,snapshot)&&snapshot.valid());
  assert(source.locks==oldSource.locks&&source.unlocks==oldSource.unlocks&&source.descCalls==oldSource.descCalls&&source.lockedLevel==oldSource.lockedLevel&&source.lockedLevel==1);
  std::vector<std::uint8_t> decoded;assert(NorthlightActorTexture::decode(snapshot.bytes.data(),snapshot.bytes.size(),snapshot.width,snapshot.height,snapshot.pitch,snapshot.format,decoded));assert(decoded==m.rgba);
  Current::ActorJob job;job.center={102,13,4};auto p=packet();p.alphaTest=true;p.material=neutral(true);p.texture=std::move(snapshot);job.packets.push_back(p);
  std::fill(source.bytes.begin(),source.bytes.end(),0xff);std::fill(p.texture.bytes.begin(),p.texture.bytes.end(),0);p.mesh.streams[0].bytes.clear();p.constants.fill(-99);
  auto result=compare(job);assert(result.texturesDecoded==1&&result.textureEncodedBytes==job.packets[0].texture.bytes.size());sameMaterial(result.scene->materials[0],m);++paired;
 }
 std::printf("Actual readback + immutable worker parity: %u scenes across six formats, partial blocks, padded rows and identical mip selection\n",paired);
 for(unsigned failure=0;failure<12;++failure){
  auto source=texture(D3DFMT_DXT5,4,4);switch(failure){
   case 0:source.kind=0;break;case 1:source.levels=0;break;case 2:source.failDesc=true;break;case 3:source.desc.Width=0;break;
   case 4:source.desc.Width=129;break;case 5:source.desc.Format=D3DFMT_INDEX16;break;case 6:source.desc.Usage=D3DUSAGE_RENDERTARGET;break;
   case 7:source.failLock=true;break;case 8:source.failUnlock=true;break;case 9:source.nullBits=true;break;case 10:source.pitch=15;break;case 11:source.pitch=-16;break;
  }
  auto oldSource=source;auto m=neutral(true);NorthlightActorTexture::Snapshot snapshot;
  assert(!before.actorMaterial(&oldSource,m)&&!current.actorMaterial(&source,snapshot)&&!snapshot.valid());
  assert(source.locks==oldSource.locks&&source.unlocks==oldSource.unlocks&&source.descCalls==oldSource.descCalls);
  Current::ActorJob job;job.center={102,13,4};auto p=packet();p.material=m;job.packets.push_back(p);
  assert(compare(job).scene->triangles.size()==1);job.packets[0].alphaTest=true;assert(compare(job).scene->triangles.empty());
 }
 NorthlightActorTexture::Snapshot empty;auto m=neutral(true);assert(!before.actorMaterial(nullptr,m)&&!current.actorMaterial(nullptr,empty));
 std::puts("Null/unsupported/failed lock/unlock/malformed pitch reads retain opaque-neutral and alpha-skip behavior; D3D lock counts identical");
 Current::ActorJob job;job.center={102,13,4};auto p=packet();auto source=texture(D3DFMT_DXT5,5,7,9);assert(current.actorMaterial(&source,p.texture));p.alphaTest=true;p.material=neutral(true);job.packets.push_back(p);auto original=compare(job);
 for(unsigned gate=0;gate<10;++gate){auto changed=job;auto& c=changed.packets[0];switch(gate){
  case 0:c.hasUV=false;break;case 1:c.constants[7*4]=std::numeric_limits<float>::quiet_NaN();break;case 2:c.mesh.indices.clear();break;case 3:c.mesh.primitiveCount=0xffffffffu;break;
  case 4:changed.center={500,500,500};break;case 5:c.mesh.indices={0,0,0};break;case 6:c.elements.clear();break;
  case 7:c.texture.bytes.resize(1);break;case 8:c.texture.width=129;break;case 9:c.mesh.topology=static_cast<D3DPRIMITIVETYPE>(0);break;
  }auto result=compare(changed);assert(result.scene->triangles.empty()&&result.texturesDecoded==0);
  c.alphaTest=false;compare(changed);
 }
 // Mutations retain full RGBA/UV/cutoff/address/pose hashing and transparent
 // texels retain the same dynamic ray visibility as predecoded materials.
 for(unsigned kind=0;kind<5;++kind){auto changed=job;auto& c=changed.packets[0];switch(kind){
  case 0:c.texture.bytes[8]^=0xff;break;case 1:c.material.alphaCutoff=.7f;break;case 2:c.material.addressU=1;break;
  case 3:c.constants[7*4]+=.1f;break;case 4:c.constants[34*4+3]+=1;break;
  }assert(compare(changed).hash!=original.hash);}
 source=texture(D3DFMT_A8R8G8B8,1,1);source.bytes={0,0,255,0};assert(current.actorMaterial(&source,job.packets[0].texture));
 auto transparent=compare(job);NorthlightGI::BVH bvh;std::string error;auto scene=*transparent.scene;assert(bvh.build(std::move(scene),error));assert(!bvh.occluded({102.2f,13.2f,5},{0,0,-1}));
 job.packets[0].texture.bytes[3]=255;auto opaque=compare(job);scene=*opaque.scene;assert(bvh.build(std::move(scene),error));assert(bvh.occluded({102.2f,13.2f,5},{0,0,-1}));
 // Exactly 16,384 evaluated vertices: an invalid alpha/UV packet still consumes
 // the worker's vertex budget, so later packets have the same admission.
 auto large=job.packets[0];large.mesh.vertexCount=8192;large.mesh.streams[0].bytes.resize(8192*24);
 for(unsigned i=3;i<8192;++i)std::memcpy(large.mesh.streams[0].bytes.data()+i*24,large.mesh.streams[0].bytes.data(),24);
 job.packets={large,large,p};assert(compare(job).scene->triangles.size()==2);
 job.packets[0].hasUV=false;assert(compare(job).scene->triangles.size()==1);
 job.packets[0].mesh.primitiveCount=0;assert(compare(job).scene->triangles.size()==2);
 // Shared immutable mesh ownership and repeat resolutions remain deterministic.
 job.packets={p};auto mesh=std::make_shared<const NorthlightDrawSnapshot::Mesh>(std::move(job.packets[0].mesh));job.packets[0].sharedMesh=mesh;mesh.reset();assert(compare(job).hash==original.hash);
 auto first=std::async(std::launch::async,[job]{return job.resolve();});auto second=std::async(std::launch::async,[job]{return job.resolve();});assert(first.get().hash==second.get().hash);
 std::puts("Bit-exact vertices, normals, UVs, materials, scene hash, alpha BVH, geometry/UV gates, 16,384-vertex budgets and concurrent immutable resolution passed");
}
'''
harness = harness.replace('BEFORE_MATERIAL_METHOD', material_method((BASE / 'world_renderer.h').read_text()).replace('NorthlightActorTexture::', 'NorthlightActorTextureBefore::'))
harness = harness.replace('CURRENT_MATERIAL_METHOD', material_method(fp.src('world_renderer.h').read_text()))
FIXTURE.mkdir(parents=True, exist_ok=True)
(FIXTURE / 'd3d9.h').write_text(stub)
(FIXTURE / 'test.cpp').write_text(harness)
(FIXTURE / 'before_actor_texture.h').write_text((BASE / 'actor_texture.h').read_text().replace('NorthlightActorTexture', 'NorthlightActorTextureBefore'))
(FIXTURE / 'before_actor_scene_job.h').write_text((BASE / 'actor_scene_job.h').read_text().replace('NorthlightActorGeometry', 'NorthlightActorGeometryBefore'))
logs = {}
for name, flags in [('O2', ['-O2']), ('ASan-UBSan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]:
    binary = FIXTURE / ('test-' + name)
    command = ['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags, '-I', str(FIXTURE), *fp.test_include_flags(), str(FIXTURE / 'test.cpp'), str(fp.src('world_gi.cpp')), '-o', str(binary)]
    subprocess.run(command, check=True)
    completed = subprocess.run([str(binary)], check=True, text=True, capture_output=True)
    logs[name] = completed.stdout
    (OUT / ('actor-texture-worker-' + name + '.txt')).write_text(completed.stdout + completed.stderr)
    print(name + ':\n' + completed.stdout, end='')
report = {
    'source_sha256': {name: hashlib.sha256(fp.tracked(name).read_bytes()).hexdigest() for name in ['actor_texture.h', 'actor_scene_job.h', 'world_renderer.h', 'actor_deformation.h', 'world_gi.h', 'world_gi.cpp', Path(__file__).name]},
    'baseline_sha256': json.loads((BASE / 'sha256.json').read_text()),
    'actual_production_readback_methods': True,
    'actual_production_old_and_new_scene_resolvers': True,
    'paired_texture_scenes': 432,
    'all_six_formats_partial_blocks_padded_mips': True,
    'failed_read_alpha_skip_opaque_neutral_parity': True,
    'geometry_uv_hash_normal_alpha_bvh_parity': True,
    'vertex_budget_parity': True,
    'immutable_ownership_and_concurrent_resolution': True,
    'worker_no_d3d_interface': True,
    'O2': 'passed', 'ASan_UBSan': 'passed', 'game_launched': False,
}
(OUT / 'actor-texture-worker-validation.json').write_text(json.dumps(report, indent=2) + '\n')
