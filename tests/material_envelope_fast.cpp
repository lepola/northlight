#define NOMINMAX
#undef NDEBUG
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <chrono>
#include <thread>
#include "replay_bounds.h"
#include "actor_client_programs.h"
namespace B=NorthlightReplayBounds;
using E=B::EnvelopeCache;
using Mesh=NorthlightDrawSnapshot::Mesh;
using Program=NorthlightActorDeformation::Program;
static D3DVERTEXELEMENT9 decl[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,16,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0xff,0,D3DDECLTYPE_UNUSED,0,0,0}};
static std::shared_ptr<const Mesh> mesh(unsigned groups){
    auto m=std::make_shared<Mesh>();m->topology=D3DPT_TRIANGLELIST;m->vertexCount=groups*6;m->primitiveCount=m->vertexCount/3;
    auto& s=m->streams[0];s.stride=32;s.bytes.resize(m->vertexCount*32);
    for(unsigned i=0;i<m->vertexCount;++i){float p[3]={float(int(i%5)-2)*.3f,float(i%7)*.2f,float(i%3)*.1f};
        const float weights[4]={1,0,0,0};std::memcpy(s.bytes.data()+32*i,p,12);s.bytes[32*i+12]=i%groups;std::memcpy(s.bytes.data()+32*i+16,weights,16);}
    return m;
}
static void pose(float* c,unsigned frame){
    std::fill(c,c+1024,0.f);c[0]=19;c[1]=-4; // The real shader's DEF must override these.
    for(unsigned b=0;b<75;++b){float* r=c+4*(31+3*b);float a=float(frame)*.001f;
        r[0]=std::cos(a);r[1]=std::sin(a);r[3]=float(b)*.1f;r[4]=-r[1];r[5]=r[0];r[7]=-.3f;r[10]=1;r[11]=float(frame%400)+.5f;}
}
static void warm(E& e,const E::Prepared& p,const std::shared_ptr<const Mesh>& m,float* c,float* inverse){
    for(unsigned i=0;i<1000;++i){e.beginFrame(false);B::Budget budget;B::Bounds b;
        if(e.calculatePrepared(p,*m,m,c,inverse,budget,b)==B::Status::Valid)return;}
    assert(false);
}
template<size_t N> static Program program(const uint32_t(&code)[N]){Program p;assert(NorthlightActorDeformation::compile(code,N,p));return p;}
static void unusedIndices(const std::vector<Program>& programs){
    unsigned checked=0;
    for(const auto& original:programs){
        if(!original.kernel.singleSkin())continue;
        auto source=std::make_shared<Mesh>(*mesh(128));
        for(unsigned i=0;i<source->vertexCount;++i){auto* indices=source->streams[0].bytes.data()+32*i+12;
            indices[0]=i%4;indices[1]=i%256;indices[2]=i/256;indices[3]=(i*17)%251;}
        const std::shared_ptr<const Mesh> owner=source;
        for(bool overridePalette:{false,true}){
            auto p=original;if(overridePalette)p.definitions.push_back({31,{1,0,0,7}});
            auto prepared=E::prepareProgram(p,decl,4);assert(prepared);E cache;
            float c[1024],inverse[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};pose(c,0);
            warm(cache,*prepared,owner,c,inverse);
            for(unsigned frame=0;frame<32;++frame){
                pose(c,frame);const float angle=float(frame)*.03f;inverse[0]=inverse[5]=std::cos(angle);inverse[1]=std::sin(angle);inverse[4]=-inverse[1];inverse[12]=frame*10.f;
                cache.beginFrame(false);B::Budget budget;B::Bounds bounds;E::WorkInfo info;
                const auto status=cache.calculatePrepared(*prepared,*owner,owner,c,inverse,budget,bounds,E::Stage::All,&info);
                assert(status==B::Status::Valid&&bounds.valid&&info.groups==4);
                assert(budget.operations==4*p.operations.size()&&!budget.vertices);
                assert(overridePalette?cache.stats().effectiveCopiedBytes>0:cache.stats().effectiveCopiedBytes==0);
                std::vector<NorthlightActorDeformation::Position> positions;
                assert(NorthlightActorDeformation::worldPositions(p,*owner,decl,4,c,inverse,positions));
                for(const auto& v:positions){const float xyz[]={v.x,v.y,v.z};for(unsigned k=0;k<3;++k)assert(xyz[k]>=bounds.low[k]&&xyz[k]<=bounds.high[k]);}
                ++checked;
            }
        }
    }
    std::printf("MATERIAL_SINGLE_BONE_GROUPS_PASS poses=%u vertices=768 groups=4 unused_index_lanes palette_override no_control_bank_copy\n",checked);
}
int main(){
    auto programs=std::vector<Program>{program(ClientShaders::OneBoneVs3),program(ClientShaders::OneBoneVs3WLast),program(ClientShaders::OneBoneVs3WMid),program(ClientShaders::OneBoneVs2),program(ClientShaders::FourBoneVs3)};
    unusedIndices(programs);
    float c[1024],inverse[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};pose(c,0);unsigned checks=0,fast=0;
    for(const auto& p:programs)for(unsigned groups:{1u,4u,8u,9u,32u}){
        auto prepared=E::prepareProgram(p,decl,4);assert(prepared);auto m=mesh(groups);E current,reference;
        warm(current,*prepared,m,c,inverse);warm(reference,*prepared,m,c,inverse);
        for(unsigned frame=0;frame<250;++frame){
            pose(c,frame);inverse[12]=float(frame%7)*100;inverse[13]=-50;
            if(frame%19==0)c[4*31]=std::numeric_limits<float>::denorm_min();
            if(frame%29==0)c[4*31]=std::numeric_limits<float>::quiet_NaN();
            if(frame%31==0)inverse[15]=2;else inverse[15]=1;
            B::Bounds a,b;B::Budget ba,bb;current.beginFrame(false);reference.beginFrame(false);
            if(frame%23==0)ba.maxOperations=bb.maxOperations=0;
            auto sa=current.calculatePrepared(*prepared,*m,m,c,inverse,ba,a,E::Stage::All);
            auto sb=reference.calculatePrepared(*prepared,*m,m,c,inverse,bb,b,E::Stage::Ready);
            // The wall clock may refuse either path; a refusal must never publish a bound.
            if(sa==B::Status::Budget){assert(!a.valid);continue;}if(sb==B::Status::Budget){assert(!b.valid);continue;}
            assert(sa==sb&&a.valid==b.valid);assert(ba.operations==bb.operations);
            if(a.valid){for(unsigned k=0;k<3;++k){assert(a.low[k]==b.low[k]);assert(a.high[k]==b.high[k]);}++checks;}
            fast+=unsigned(current.stats().fastValid);
        }
    }
    assert(checks>3500&&fast>1500);
    pose(c,0);std::fill(inverse,inverse+16,0.f);for(unsigned k=0;k<4;++k)inverse[5*k]=1;
    auto prepared=E::prepareProgram(programs[0],decl,4);auto m=mesh(1);E cache;warm(cache,*prepared,m,c,inverse);
    // Unrelated work between material draws must not consume the ready bank.
    cache.beginFrame(false);B::Budget budget;B::Bounds bounds;
    assert(cache.calculatePrepared(*prepared,*m,m,c,inverse,budget,bounds)==B::Status::Valid);
    const auto charged=cache.phaseNanoseconds(0);std::this_thread::sleep_for(std::chrono::milliseconds(2));
    assert(cache.phaseNanoseconds(0)==charged);
    assert(cache.calculatePrepared(*prepared,*m,m,c,inverse,budget,bounds)==B::Status::Valid);
    cache.chargeCheapPreparation(1000000);assert(!cache.canReady());
    assert(cache.calculatePrepared(*prepared,*m,m,c,inverse,budget,bounds)==B::Status::Budget&&!bounds.valid);
    std::vector<std::shared_ptr<const Mesh>> crowd;
    for(unsigned draw=0;draw<512;++draw){crowd.push_back(mesh(1));warm(cache,*prepared,crowd.back(),c,inverse);}
    double times[2][7];unsigned accepted[2]={};volatile float sink=0;
    for(unsigned round=0;round<7;++round)for(unsigned turn=0;turn<2;++turn){const unsigned variant=(round+turn)%2;
        const auto stage=variant?E::Stage::All:E::Stage::Ready;auto start=std::chrono::steady_clock::now();
        for(unsigned frame=0;frame<100;++frame){cache.beginFrame(false);budget.reset();pose(c,frame);
            for(unsigned draw=0;draw<512;++draw){c[127]=float(draw)+.5f;
                const auto& actor=crowd[draw];
                if(cache.calculatePrepared(*prepared,*actor,actor,c,inverse,budget,bounds,stage)==B::Status::Valid){++accepted[variant];sink=bounds.low[2];}}}
        times[variant][round]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/100;
    }
    std::sort(times[0],times[0]+7);std::sort(times[1],times[1]+7);
    std::printf("MATERIAL_ENVELOPE_FAST_PASS exact_bounds=%u fast=%u referenceMs=%.6f currentMs=%.6f referenceValid=%u currentValid=%u sink=%g\n",checks,fast,times[0][3],times[1][3],accepted[0],accepted[1],float(sink));
}
