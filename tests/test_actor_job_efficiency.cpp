#include <cstdlib>
#include <new>
#include <cstdio>
static unsigned allocations=0;
void* operator new(size_t n){++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
#include "actor_scene_job.h"
#include "actor_job_reference.h"
#include <cassert>
#include <chrono>
using namespace NorthlightActorDeformation;
#include "actor_probe_program.h"
static void equal(const NorthlightGI::WorldScene& a,const NorthlightGI::WorldScene& b){
    assert(a.vertices.size()==b.vertices.size()&&a.triangles.size()==b.triangles.size()&&a.materials.size()==b.materials.size());
    for(size_t i=0;i<a.vertices.size();++i){auto& x=a.vertices[i];auto& y=b.vertices[i];assert(!std::memcmp(&x.position,&y.position,sizeof x.position)&&!std::memcmp(&x.normal,&y.normal,sizeof x.normal)&&x.u==y.u&&x.v==y.v);}
    for(size_t i=0;i<a.triangles.size();++i)assert(!std::memcmp(&a.triangles[i],&b.triangles[i],sizeof a.triangles[i]));
    for(size_t i=0;i<a.materials.size();++i){auto& x=a.materials[i];auto& y=b.materials[i];assert(!std::memcmp(&x.albedo,&y.albedo,sizeof x.albedo)&&x.alphaCutoff==y.alphaCutoff&&x.width==y.width&&x.height==y.height&&x.addressU==y.addressU&&x.addressV==y.addressV&&x.rgba==y.rgba);}
}
static NorthlightActorGeometry::Packet packet(){
    NorthlightActorGeometry::Packet p;auto words=code(3);assert(compile(words.data(),words.size(),p.position)&&compile(words.data(),words.size(),p.uv,true));p.hasUV=true;
    p.mesh.vertexCount=3;p.mesh.primitiveCount=1;p.mesh.indexed=true;p.mesh.indices={0,1,2};p.mesh.streams[0].stride=24;p.mesh.streams[0].bytes.resize(72);
    float positions[3][3]={{2,3,4},{3,3,4},{2,4,4}},uv[]={.1f,.2f};for(unsigned i=0;i<3;++i){auto* out=p.mesh.streams[0].bytes.data()+i*24;std::memcpy(out,positions[i],12);std::memcpy(out+12,uv,8);out[20]=1;}
    p.elements={{0,0,2,0,0,0},{0,12,1,0,5,0},{0,20,5,0,2,0},{0xff,0,17,0,0,0}};
    for(unsigned j=0;j<4;++j)p.inverseView[j*5]=1;
    p.constants[34*4]=1;p.constants[35*4+1]=1;p.constants[36*4+2]=1;p.constants[34*4+3]=100;p.constants[35*4+3]=10;
    p.constants[6*4]=2;p.constants[6*4+1]=3;p.constants[7*4]=.25f;p.constants[7*4+1]=-.5f;
    p.material.albedo={1,1,1};p.material.alphaCutoff=.5f;p.material.addressU=3;
    p.texture.width=p.texture.height=1;p.texture.pitch=4;p.texture.format=NorthlightActorTexture::Format::BGRA8;p.texture.bytes={0,0,255,255};return p;
}
int main(){
    NorthlightActorGeometry::ActorJob job;ActorJobReference::ActorJob before;job.center=before.center={102,13,4};
    for(unsigned i=0;i<128;++i){auto p=packet();p.constants[34*4+3]+=float(i%8);switch(i%13){case 0:p.hasUV=false;p.alphaTest=true;break;case 1:p.mesh.indices[0]=999;break;case 2:p.mesh.primitiveCount=0;break;case 3:p.constants[34*4+3]=999;break;case 4:p.mesh.topology=D3DPT_TRIANGLESTRIP;break;case 5:p.texture.bytes.clear();break;case 6:p.mesh.streams[0].bytes.resize(1);break;case 7:p.material.rgba={50,60,70,255};p.texture.bytes.clear();p.material.width=p.material.height=1;break;}
        ActorJobReference::Packet q;q.mesh=p.mesh;q.sharedMesh=p.sharedMesh;q.position=p.position;q.uv=p.uv;q.elements=p.elements;q.constants=p.constants;q.inverseView=p.inverseView;q.material=p.material;q.texture=p.texture;q.hasUV=p.hasUV;q.alphaTest=p.alphaTest;
        job.packets.push_back(std::move(p));before.packets.push_back(std::move(q));}
    auto expected=before.resolve();auto result=job.resolve();equal(*expected.scene,*result.scene);assert(expected.hash==result.hash&&expected.texturesDecoded==result.texturesDecoded&&expected.textureEncodedBytes==result.textureEncodedBytes);
    auto sourcePosition=std::make_shared<const Program>(job.packets[0].position),sourceUV=std::make_shared<const Program>(job.packets[0].uv);
    std::weak_ptr<const Program> weakPosition=sourcePosition,weakUV=sourceUV;
    unsigned copyAllocations=0,sharedAllocations=0;double captureNs[2]={};
    for(unsigned round=0;round<20;++round)for(unsigned mode=0;mode<2;++mode){auto start=std::chrono::steady_clock::now();unsigned count=allocations;
        for(unsigned i=0;i<128;++i){NorthlightActorGeometry::Packet p;if(mode){p.sharedPosition=sourcePosition;p.sharedUV=sourceUV;assert(&p.positionView()==sourcePosition.get()&&&p.uvView()==sourceUV.get());}
            else{p.position=*sourcePosition;p.uv=*sourceUV;assert(p.position.operations.size()==sourcePosition->operations.size()&&p.uv.operations.size()==sourceUV->operations.size());}}
        (mode?sharedAllocations:copyAllocations)+=allocations-count;captureNs[mode]+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/128;}
    assert(copyAllocations>0&&!sharedAllocations);
    for(auto& p:job.packets){p.sharedPosition=sourcePosition;p.sharedUV=sourceUV;p.position={};p.uv={};}
    sourcePosition.reset();sourceUV.reset();assert(!weakPosition.expired()&&!weakUV.expired());
    auto sharedResult=job.resolve();equal(*result.scene,*sharedResult.scene);assert(result.hash==sharedResult.hash);
    std::printf("PASS actor immutable program capture: oldAllocations=%u newAllocations=%u oldNs=%.3f newNs=%.3f sourceLifetime=1 exactSharedOutput=1\n",copyAllocations,sharedAllocations,captureNs[0]/20,captureNs[1]/20);
    double time[2]={};unsigned alloc[2]={};
    for(unsigned round=0;round<20;++round)for(unsigned turn=0;turn<2;++turn){unsigned mode=(round+turn)&1,startAlloc=allocations;auto start=std::chrono::steady_clock::now();
        if(mode){auto x=job.resolve();assert(x.hash==result.hash);}else{auto x=before.resolve();assert(x.hash==result.hash);}
        time[mode]+=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();alloc[mode]+=allocations-startAlloc;}
    assert(alloc[1]<alloc[0]);std::printf("PASS actor worker: packets=128 exactVertices=%zu exactTriangles=%zu oldAllocations=%u newAllocations=%u oldUs=%.3f newUs=%.3f ratio=%.4f\n",result.scene->vertices.size(),result.scene->triangles.size(),alloc[0]/20,alloc[1]/20,time[0]/20,time[1]/20,time[1]/time[0]);
    job.packets.clear();assert(weakPosition.expired()&&weakUV.expired());
}
