#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Deferred CPU actor packets, pose/UV/alpha, immutable output and generation hash."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import ast,subprocess,tempfile
HERE=Path(__file__).resolve().parent
def literal(path,name):
 return next(ast.literal_eval(n.value) for n in ast.parse(path.read_text()).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id==name for t in n.targets))
stub=literal(HERE/'test_terrain_snapshot.py','stub')
fixture=literal(HERE/'test_actor_deformation.py','harness').split('int main(')[0]
harness=fixture+r'''
#include "actor_scene_job.h"
#include <future>
using namespace NorthlightActorGeometry;
int main(){ActorJob job;job.center={102,13,4};Packet p;auto words=code(3);assert(compile(words.data(),words.size(),p.position));assert(compile(words.data(),words.size(),p.uv,true));p.hasUV=p.alphaTest=true;
 p.mesh.vertexCount=3;p.mesh.primitiveCount=1;p.mesh.indexed=true;p.mesh.indices={0,1,2};p.mesh.streams[0].stride=24;p.mesh.streams[0].bytes.resize(72);
 float positions[3][3]={{2,3,4},{3,3,4},{2,4,4}};float uv[]={.1f,.2f};for(unsigned i=0;i<3;++i){auto* out=p.mesh.streams[0].bytes.data()+i*24;std::memcpy(out,positions[i],12);std::memcpy(out+12,uv,8);out[20]=1;}
 p.elements={{0,0,2,0,0,0},{0,12,1,0,5,0},{0,20,5,0,2,0},{0xff,0,17,0,0,0}};
 for(unsigned j=0;j<4;++j)p.inverseView[j*5]=1;
 p.constants[34*4]=1;p.constants[35*4+1]=1;p.constants[36*4+2]=1;p.constants[34*4+3]=100;p.constants[35*4+3]=10;
 p.constants[6*4]=2;p.constants[6*4+1]=3;p.constants[7*4]=.25f;p.constants[7*4+1]=-.5f;
 p.material.albedo={1,1,1};p.material.width=p.material.height=1;p.material.rgba={255,0,0,255};p.material.alphaCutoff=.5f;p.material.addressU=3;
 job.packets.push_back(p);auto capturedPixels=p.material.rgba;
 // Destroy the original capture sources before the worker resolves: packet
 // bytes, constants, declaration, programs and material must all be owned.
 std::fill(p.mesh.streams[0].bytes.begin(),p.mesh.streams[0].bytes.end(),0xff);p.constants.fill(-999);p.inverseView.fill(0);p.material.rgba.clear();p.elements.clear();p.position.operations.clear();
 auto pending=std::async(std::launch::async,[&job]{return job.resolve();});auto first=pending.get();assert(first.scene&&first.scene->triangles.size()==1&&first.scene->vertices[0].position.x==102&&first.scene->vertices[0].normal.z==1);
 assert(std::fabs(first.scene->vertices[0].u-.45f)<1e-6f);assert(first.scene->materials[0].rgba==capturedPixels);
 ActorJob rotated=job;auto& r=rotated.packets[0];float row0[]={0,1,0,10},row1[]={-1,0,0,-100};std::memcpy(r.constants.data()+34*4,row0,16);std::memcpy(r.constants.data()+35*4,row1,16);r.inverseView[0]=r.inverseView[5]=0;r.inverseView[1]=1;r.inverseView[4]=-1;
 assert(rotated.resolve().hash==first.hash); // camera-only orbit cannot regenerate GI.
 auto changed=job;changed.packets[0].material.rgba[0]=200;assert(changed.resolve().hash!=first.hash);changed=job;changed.packets[0].material.alphaCutoff=.75f;assert(changed.resolve().hash!=first.hash);
 changed=job;changed.packets[0].material.addressU=1;assert(changed.resolve().hash!=first.hash);changed=job;changed.packets[0].constants[7*4]+=.1f;assert(changed.resolve().hash!=first.hash);
 changed=job;changed.packets[0].constants[34*4+3]+=1;assert(changed.resolve().hash!=first.hash&&first.scene->vertices[0].position.x==102);
 // Actual decoded alpha pixels drive dynamic BVH ray visibility.
 std::string error;NorthlightGI::BVH opaque;auto scene=*first.scene;assert(opaque.build(std::move(scene),error));assert(opaque.occluded({102.2f,13.2f,5},{0,0,-1}));
 changed=job;changed.packets[0].material.rgba[3]=0;auto transparent=changed.resolve();NorthlightGI::BVH cutout;scene=*transparent.scene;assert(cutout.build(std::move(scene),error));assert(!cutout.occluded({102.2f,13.2f,5},{0,0,-1}));
 changed=job;changed.packets[0].material.rgba.clear();assert(changed.resolve().scene->triangles.empty());changed=job;changed.packets[0].hasUV=false;assert(changed.resolve().scene->triangles.empty());
 changed=job;changed.packets[0].mesh.indices.clear();assert(changed.resolve().scene->triangles.empty());changed=job;changed.center={500,500,500};assert(changed.resolve().scene->triangles.empty());
 changed=job;changed.packets[0].mesh.primitiveCount=0xffffffff;assert(changed.resolve().scene->triangles.empty());
 // Immutable mesh sharing removes capture copies without retaining live buffers.
 ActorJob sharedJob=job;auto immutable=std::make_shared<const NorthlightDrawSnapshot::Mesh>(std::move(sharedJob.packets[0].mesh));
 std::weak_ptr<const NorthlightDrawSnapshot::Mesh> weakMesh=immutable;
 sharedJob.packets[0].sharedMesh=immutable;assert(sharedJob.packets[0].mesh.byteSize()==0);
 assert(&sharedJob.packets[0].meshView()==immutable.get());immutable.reset();
 auto sharedResult=sharedJob.resolve();assert(sharedResult.hash==first.hash&&sharedResult.scene->vertices.size()==first.scene->vertices.size());
 assert(std::memcmp(sharedResult.scene->vertices.data(),first.scene->vertices.data(),first.scene->vertices.size()*sizeof(NorthlightGI::WorldVertex))==0);
 auto queued=std::make_shared<const ActorJob>(std::move(sharedJob));auto future=std::async(std::launch::async,[queued]{return queued->resolve();});queued.reset();
 assert(future.get().hash==first.hash);assert(weakMesh.expired()); // binding cache holds no mesh/job owner
 // Prepared bindings memoize format only: pose, UV constants and vertex data
 // are evaluated on EVERY use, with exact equivalence to the original path.
 const auto& packet=job.packets[0];PreparedBindingCache bindings;
 auto prepared=bindings.acquire(packet.position,packet.meshView(),packet.elements.data(),packet.elements.size());assert(prepared&&bindings.preparations==1);
 assert(bindings.acquire(packet.position,packet.meshView(),packet.elements.data(),packet.elements.size())==prepared&&bindings.hits==1);
 std::vector<Position> originalPositions,cachedPositions;
 assert(worldPositions(packet.position,packet.meshView(),packet.elements.data(),packet.elements.size(),packet.constants.data(),packet.inverseView.data(),originalPositions));
 assert(worldPositionsPrepared(packet.position,packet.meshView(),*prepared,packet.constants.data(),packet.inverseView.data(),cachedPositions));
 assert(originalPositions.size()==cachedPositions.size()&&std::memcmp(originalPositions.data(),cachedPositions.data(),originalPositions.size()*sizeof(Position))==0);
 auto changedConstants=packet.constants;changedConstants[34*4+3]+=7;
 assert(worldPositionsPrepared(packet.position,packet.meshView(),*prepared,changedConstants.data(),packet.inverseView.data(),cachedPositions));assert(cachedPositions[0].x==originalPositions[0].x+7);
 auto changedMesh=packet.meshView();float differentX=9;std::memcpy(changedMesh.streams[0].bytes.data(),&differentX,4);
 assert(bindings.acquire(packet.position,changedMesh,packet.elements.data(),packet.elements.size())==prepared);
 assert(worldPositionsPrepared(packet.position,changedMesh,*prepared,packet.constants.data(),packet.inverseView.data(),cachedPositions));assert(cachedPositions[0].x==109);
 auto uvPrepared=bindings.acquire(packet.uv,packet.meshView(),packet.elements.data(),packet.elements.size());assert(uvPrepared);
 std::vector<std::array<float,2>> originalUV,cachedUV;assert(textureUVs(packet.uv,packet.meshView(),packet.elements.data(),packet.elements.size(),packet.constants.data(),originalUV));
 assert(textureUVsPrepared(packet.uv,packet.meshView(),*uvPrepared,packet.constants.data(),cachedUV));assert(originalUV==cachedUV);
 changedConstants=packet.constants;changedConstants[7*4]+=.25f;assert(textureUVsPrepared(packet.uv,packet.meshView(),*uvPrepared,changedConstants.data(),cachedUV));assert(cachedUV[0][0]==originalUV[0][0]+.25f);
 auto changedElements=packet.elements;changedElements[0].Offset=12;assert(bindings.acquire(packet.position,packet.meshView(),changedElements.data(),changedElements.size())!=prepared);
 auto truncated=packet.meshView();truncated.streams[0].bytes.resize(1);assert(!bindings.acquire(packet.position,truncated,packet.elements.data(),packet.elements.size()));assert(!worldPositionsPrepared(packet.position,truncated,*prepared,packet.constants.data(),packet.inverseView.data(),cachedPositions)&&cachedPositions.empty());
 auto invalidElements=packet.elements;invalidElements.insert(invalidElements.begin(),invalidElements[0]);assert(!bindings.acquire(packet.position,packet.meshView(),invalidElements.data(),invalidElements.size()));
 ActorJob many=job;for(unsigned i=1;i<5000;++i)many.packets.push_back(job.packets[0]);auto bulk=many.resolve();assert(bulk.scene->vertices.size()==15000&&bulk.scene->triangles.size()==5000&&bulk.scene->vertices.capacity()<=16384);for(unsigned i=0;i<bulk.scene->vertices.size();++i)assert(std::memcmp(&bulk.scene->vertices[i],&first.scene->vertices[i%3],sizeof(NorthlightGI::WorldVertex))==0);
 ActorJob empty;auto removal=empty.resolve();assert(removal.scene&&removal.scene->triangles.empty()&&removal.hash!=first.hash);
 std::puts("Deferred actor job: immutable pose, camera-invariant generation, UV/material invalidation, real alpha BVH visibility, removal and malformed packet gates passed");
}
'''
with tempfile.TemporaryDirectory(prefix='northlight-actor-job-') as tmp:
 p=Path(tmp);(p/'d3d9.h').write_text(stub);(p/'test.cpp').write_text(harness)
 for flags in (['-O2'],['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']):
  subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-I',str(p),*fp.test_include_flags(),str(p/'test.cpp'),str(fp.src('world_gi.cpp')),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
