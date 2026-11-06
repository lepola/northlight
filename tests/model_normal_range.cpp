#define NOMINMAX
#include <cassert>
#include <cstdio>
#include <fstream>
#include "model_normal_range.h"
#include "actor_client_programs.h"
#include "pose_bounds_handoff.h"
using Mesh=NorthlightDrawSnapshot::Mesh;
static std::vector<uint32_t> read(const char* path){std::ifstream f(path,std::ios::binary|std::ios::ate);assert(f);auto size=f.tellg();std::vector<uint32_t> result(size_t(size)/4);f.seekg(0);f.read((char*)result.data(),size);return result;}
static Mesh geometry(unsigned count=32,float z=0){Mesh m;m.vertexCount=count;m.streams[0].stride=12;m.streams[0].bytes.resize(size_t(count)*12);
    for(unsigned i=0;i<count;++i){const float p[]={float(i%7)-3,float(i%5)-2,z+float(i%3)};std::memcpy(m.streams[0].bytes.data()+i*12,p,12);}return m;}
static void singleBone(){
    using namespace NorthlightModelNormalRange;
    const auto check=[](const auto& code){assert(classify(code,sizeof(code)/4)==Single);};
    check(ClientShaders::OneBoneVs3);check(ClientShaders::OneBoneVs3WLast);check(ClientShaders::OneBoneVs3WMid);check(ClientShaders::OneBoneVs2);
    D3DVERTEXELEMENT9 layout[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},D3DDECL_END()};
    auto source=[](unsigned count,unsigned bone,bool mixed=false){Mesh m;m.vertexCount=count;m.streams[0].stride=16;m.streams[0].bytes.resize(size_t(count)*16);
        for(unsigned i=0;i<count;++i){float p[]={float(i%3),float(i%5),float(i%7)};auto* v=m.streams[0].bytes.data()+i*16;std::memcpy(v,p,12);v[12]=bone+(mixed&&i==count-1);v[13]=255;v[14]=i%255;v[15]=254;}return std::make_shared<const Mesh>(std::move(m));};
    Cache cache;float bank[1024]={};bank[8]=bank[13]=bank[18]=bank[22]=1;
    for(unsigned b=0;b<75;++b){bank[4*(31+b*3)]=bank[4*(32+b*3)+1]=bank[4*(33+b*3)+2]=1;bank[4*(33+b*3)+3]=80;}
    auto mesh=source(32,7),mixed=source(32,7,true);assert(cache.outside(mesh,layout,3,bank,Single));assert(!cache.outside(mixed,layout,3,bank,Single));assert(!cache.outside(mixed,layout,3,bank,Single));assert(cache.singleMixed==2);
    // An immutable mixed source always keeps capture. Its warm rejection
    // attempt must not fetch a projection/pose that cannot change that fact.
    unsigned mixedReads=0;
    for(unsigned draw=0;draw<1000;++draw){cache.beginFrame();assert(!cache.outsideRead(mixed,layout,3,[&](unsigned first,unsigned count,float* out){
        ++mixedReads;std::memcpy(out,bank+first*4,count*16);return true;
    },Single));}
    assert(mixedReads==0);
    // The same mesh can expose a different, uniform index lane via a new layout.
    auto lanes=std::make_shared<Mesh>();lanes->vertexCount=32;lanes->streams[0].stride=20;lanes->streams[0].bytes.resize(32*20);
    for(unsigned i=0;i<32;++i){auto* v=lanes->streams[0].bytes.data()+20*i;v[12]=7+(i==31);v[16]=7;}
    assert(!cache.outside(lanes,layout,3,bank,Single));layout[1].Offset=16;assert(cache.outside(lanes,layout,3,bank,Single));layout[1].Offset=12;
    const auto evaluate=[&](const auto& code){NorthlightActorDeformation::Program p;assert(NorthlightActorDeformation::compile(code,sizeof(code)/4,p,false,2));
        for(unsigned frame=0;frame<100;++frame){cache.beginFrame();bank[4*(33+21)+3]=frame%2?80:20;bank[4*(33+21)]=float(int(frame%7)-3)*.11f;
            const bool reject=cache.outside(mesh,layout,3,bank,Single);assert(reject==bool(frame%2));
            for(unsigned i=0;i<mesh->vertexCount;++i){NorthlightActorDeformation::Four in[16]={},v;assert(NorthlightActorDeformation::sampledVertexInputsAt(p,*mesh,layout,3,i,in)&&NorthlightActorDeformation::evaluateFast(p,in,bank,v));if(reject)assert(v[2]>48.01f);}}
    };
    evaluate(ClientShaders::OneBoneVs3);evaluate(ClientShaders::OneBoneVs3WLast);evaluate(ClientShaders::OneBoneVs3WMid);evaluate(ClientShaders::OneBoneVs2);
    bank[4*(33+21)]=0;
    // The narrow reader must agree with the full bank for every legal bone,
    // reading only c2..c5 and that bone's three rows. Missing spans fail open.
    for(unsigned bone=0;bone<75;++bone){auto owner=source(32,bone);Cache full,narrow;
        for(float z:{20.f,48.f,80.f}){bank[4*(33+3*bone)+3]=z;unsigned registers=0;
            const bool expected=full.outside(owner,layout,3,bank,Single);
            const bool actual=narrow.outsideRead(owner,layout,3,[&](unsigned first,unsigned count,float* out){
                assert((first==2&&count==4)||(first==31+3*bone&&count==3));registers+=count;std::memcpy(out,bank+first*4,count*16);return true;
            },Single);assert(actual==expected&&registers==7);
        }
        bank[4*(33+3*bone)+3]=80;
        for(unsigned fail:{2u,31+3*bone})assert(!narrow.outsideRead(owner,layout,3,[&](unsigned first,unsigned count,float* out){
            if(first==fail)return false;std::memcpy(out,bank+first*4,count*16);return true;
        },Single));
    }
    // A changed immutable source must not inherit a uniform result from its predecessor.
    alignas(Mesh) unsigned char storage[sizeof(Mesh)];auto owner=[&](bool changed){return std::shared_ptr<const Mesh>(new(storage) Mesh(*source(32,7,changed)),[](const Mesh* p){p->~Mesh();});};
    cache.beginFrame();auto old=owner(false);assert(cache.outside(old,layout,3,bank,Single));old.reset();auto next=owner(true);assert(!cache.outside(next,layout,3,bank,Single));next.reset();
    auto uniformAgain=owner(false);assert(cache.outside(uniformAgain,layout,3,bank,Single));uniformAgain.reset();
    bank[4*(33+21)+3]=20;assert(!cache.outside(mesh,layout,3,bank,Single));bank[4*(33+21)+3]=80;assert(cache.outside(mesh,layout,3,bank,Single));
    bank[4*(31+21)]=NAN;assert(!cache.outside(mesh,layout,3,bank,Single));bank[4*(31+21)]=1;
    assert(!cache.outside(source(32,75),layout,3,bank,Single));
    layout[1].Type=D3DDECLTYPE_UBYTE4N;assert(!cache.outside(mesh,layout,3,bank,Single));layout[1].Type=D3DDECLTYPE_UBYTE4;
    cache.clear();bank[4*(33+21)+3]=20;for(unsigned i=0;i<100;++i)assert(!cache.outside(source(32,7),layout,3,bank,Single));
    bank[4*(33+21)+3]=80;assert(cache.outside(source(2048,7),layout,3,bank,Single));
    auto altered=std::vector<uint32_t>(std::begin(ClientShaders::OneBoneVs3),std::end(ClientShaders::OneBoneVs3));
    altered.insert(altered.begin()+1,{0x05000051,0xa00f0034,0,0,0,0});assert(classify(altered.data(),altered.size())==None);
}
int main(int argc,char** argv){assert(argc==3);auto code=read(argv[1]),skin=read(argv[2]);assert(NorthlightModelNormalRange::eligible(code.data(),code.size()));assert(!NorthlightModelNormalRange::eligible(skin.data(),skin.size()));
    singleBone();
    assert(NorthlightModelNormalRange::classify(skin.data(),skin.size(),true)==NorthlightModelNormalRange::Envelope);
    {NorthlightPoseBounds::Handoff proof;NorthlightConstantEpoch::Clock clock;auto stamp=clock.stamp(true);stamp.held=true;
     float constants[1024]={};constants[22]=1;NorthlightReplayBounds::Bounds b{{-2,-3,80},{2,3,90},true};int source,shader,decl,other;
     proof.publish(b,constants,stamp,&source,&shader,&decl);assert(proof.outside(&source,&shader,&decl,stamp));
     assert(!proof.outside(&other,&shader,&decl,stamp)&&!proof.outside(&source,&other,&decl,stamp)&&!proof.outside(&source,&shader,&other,stamp));
     auto unheld=stamp;unheld.held=false;assert(!proof.outside(&source,&shader,&decl,unheld));
     auto invalid=stamp;invalid.valid=false;assert(!proof.outside(&source,&shader,&decl,invalid));
     for(bool integer:{false,true}){NorthlightConstantEpoch::Clock bankClock;auto original=bankClock.stamp(true);original.held=true;proof.publish(b,constants,original,&source,&shader,&decl);
         if(integer)bankClock.writeInt(1);else bankClock.writeBool(1);auto changedBank=bankClock.stamp(true);changedBank.held=true;assert(!proof.outside(&source,&shader,&decl,changedBank));}
     proof.publish(b,constants,stamp,&source,&shader,&decl);
     clock.writeFloat(31,1);auto changed=clock.stamp(true);changed.held=true;assert(!proof.outside(&source,&shader,&decl,changed));
     proof.publish(b,constants,changed,&source,&shader,&decl);clock.writeFloat(5,1);auto projection=clock.stamp(true);projection.held=true;assert(!proof.outside(&source,&shader,&decl,projection));
     proof.publish(b,constants,projection,&source,&shader,&decl);clock.invalidate();auto reset=clock.stamp(true);reset.held=true;assert(!proof.outside(&source,&shader,&decl,reset));
     for(float z:{48.f,std::nextafter(48.f,INFINITY),-80.f}){b.low[2]=z;proof.publish(b,constants,stamp,&source,&shader,&decl);assert(!proof.outside(&source,&shader,&decl,stamp));}
     b.low[2]=-90;b.high[2]=-80;constants[22]=-1;proof.publish(b,constants,stamp,&source,&shader,&decl);assert(proof.outside(&source,&shader,&decl,stamp));
     b.low[0]=NAN;proof.publish(b,constants,stamp,&source,&shader,&decl);assert(!proof.bounds.valid);b.low[0]=-2;constants[20]=.01f;proof.publish(b,constants,stamp,&source,&shader,&decl);assert(!proof.bounds.valid);
     auto def=skin;def.insert(def.begin()+1,{0x05000051,0xa00f0005,0,0,0x3f800000,0});assert(NorthlightModelNormalRange::classify(def.data(),def.size(),true)==NorthlightModelNormalRange::None);
     proof.publishFar(stamp,&source,&shader,&decl);assert(proof.outsideRange&&!proof.bounds.valid&&proof.outside(&source,&shader,&decl,stamp));
     assert(!proof.outside(&other,&shader,&decl,stamp)&&!proof.outside(&source,&other,&decl,stamp)&&!proof.outside(&source,&shader,&other,stamp));
     assert(!proof.outside(&source,&shader,&decl,unheld)&&!proof.outside(&source,&shader,&decl,invalid));
     for(unsigned bank=0;bank<4;++bank){auto stale=stamp;
         if(bank==0)++stale.reset;if(bank==1)++stale.floats;if(bank==2)++stale.booleans;if(bank==3)++stale.integers;
         assert(!proof.outside(&source,&shader,&decl,stale));}
     proof.publishFar(invalid,&source,&shader,&decl);assert(!proof.outsideRange);
     proof.publishFar(stamp,nullptr,&shader,&decl);assert(!proof.outsideRange);
    }
    NorthlightActorDeformation::Program program;assert(NorthlightActorDeformation::compile(code.data(),code.size(),program));
    D3DVERTEXELEMENT9 layout[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
    NorthlightModelNormalRange::Cache cache;float bank[1024]={};bank[8]=bank[13]=bank[18]=bank[22]=1;bank[31*4]=bank[32*4+1]=bank[33*4+2]=1;
    auto mesh=std::make_shared<const Mesh>(geometry());unsigned checks=0;
    for(unsigned frame=0;frame<200;++frame){cache.beginFrame();bank[33*4+3]=frame%2?80:20;
        const bool rejected=cache.outside(mesh,layout,2,bank);assert(rejected==bool(frame%2));
        for(unsigned i=0;i<mesh->vertexCount;++i){NorthlightActorDeformation::Four input[16]={},p;assert(NorthlightActorDeformation::sampledVertexInputsAt(program,*mesh,layout,2,i,input)&&NorthlightActorDeformation::evaluateFast(program,input,bank,p));if(rejected)assert(p[2]>48.01);++checks;}
        bank[33*4+3]=48;assert(!cache.outside(mesh,layout,2,bank));bank[33*4+3]=80;bank[20]=.1f;assert(!cache.outside(mesh,layout,2,bank));bank[20]=0;
        bank[31*4]=NAN;assert(!cache.outside(mesh,layout,2,bank));bank[31*4]=1;
    }
    cache.beginFrame();bank[22]=-1;bank[33*4+3]=-80;assert(cache.outside(mesh,layout,2,bank));bank[22]=1;bank[33*4+3]=80;
    // Same raw address and a fresh shared control block must rescan positions.
    alignas(Mesh) unsigned char storage[sizeof(Mesh)];auto owner=[&](float z){return std::shared_ptr<const Mesh>(new(storage) Mesh(geometry(32,z)),[](const Mesh* p){p->~Mesh();});};
    cache.beginFrame();auto old=owner(0);assert(cache.outside(old,layout,2,bank));old.reset();auto next=owner(-200);assert(!cache.outside(next,layout,2,bank));next.reset();
    cache.beginFrame();auto large=std::make_shared<const Mesh>(geometry(2049));assert(!cache.outside(large,layout,2,bank));
    auto changed=std::make_shared<const Mesh>(geometry(32,-200));assert(!cache.outside(changed,layout,2,bank));cache.clear();assert(cache.outside(mesh,layout,2,bank));
    cache.clear();bank[33*4+3]=20;
    for(unsigned i=0;i<100;++i){auto nearOwner=std::make_shared<const Mesh>(geometry(32,100));assert(!cache.outside(nearOwner,layout,2,bank));}
    // Near-origin/far-local geometry remains queued, and these optional refusals
    // leave the complete cold-scan allowance for the next eligible far draw.
    bank[33*4+3]=80;auto fullBudget=std::make_shared<const Mesh>(geometry(2048));assert(cache.outside(fullBudget,layout,2,bank));
    auto def=code;def.insert(def.begin()+1,{0x05000051,0xa00f0005,0,0,0x3f800000,0});assert(!NorthlightModelNormalRange::eligible(def.data(),def.size()));
    NorthlightWaterShaderPatch::Analysis a;assert(!NorthlightWaterShaderPatch::analyse(code.data(),code.size(),a));code.insert(code.end()-1,{0x02000001,NorthlightWaterShaderPatch::dst(a.posType,a.posIndex,8),NorthlightWaterShaderPatch::src(2,0)});assert(!NorthlightModelNormalRange::eligible(code.data(),code.size()));
    std::printf("MODEL_NORMAL_RANGE_CPU_PASS checks=%u cacheBytes=%zu current_pose source_owner_ABA negative_projection finite_lanes oversized_budget reset DEF_tail_skin_fail_open\n",checks,sizeof(cache));
}
