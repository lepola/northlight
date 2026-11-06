#include <chrono>
#include <cstdint>
struct BoundsTestClock {
    using duration=std::chrono::nanoseconds;
    using time_point=std::chrono::time_point<BoundsTestClock,duration>;
    static inline uint64_t calls=0,ticks=0,step=0;
    static inline bool realtime=false;
    static time_point now(){++calls;if(realtime)return time_point(std::chrono::duration_cast<duration>(std::chrono::steady_clock::now().time_since_epoch()));
        ticks+=step;return time_point(duration(ticks));}
};
#define NORTHLIGHT_REPLAY_BOUNDS_CLOCK BoundsTestClock
#include "replay_bounds.h"
#include <cassert>
#include <cstdio>
#include <random>
using namespace NorthlightReplayBounds;
using NorthlightDrawSnapshot::Mesh;
using Program=NorthlightActorDeformation::Program;
using Stage=EnvelopeCache::Stage;
using Info=EnvelopeCache::WorkInfo;

static Program program(bool defined){
    Program p;p.major=3;p.positionRegister=0;p.inputs={{0,D3DDECLUSAGE_POSITION,0},{1,D3DDECLUSAGE_BLENDINDICES,0}};
    NorthlightActorDeformation::Operation op;op.code=1;op.destination=0x800f0000;op.source[0].token=0x90e40000;p.operations.push_back(op);
    for(unsigned axis=0;axis<3;++axis){op={};op.code=9;op.destination=0x80000000|(1u<<(16+axis));op.source[0].token=0xa0e40000+axis;op.source[1].token=0x90e40000;p.operations.push_back(op);}
    op={};op.code=2;op.destination=0x80070000;op.source[0].token=0x80e40000;op.source[1].token=0x90e40001;p.operations.push_back(op);
    if(defined)p.definitions.push_back({0,{1,0,0,17}});
    return p;
}
static const D3DVERTEXELEMENT9 declaration[]={{0,0,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},D3DDECL_END()};
static std::shared_ptr<const Mesh> mesh(unsigned groups){
    auto m=std::make_shared<Mesh>();m->vertexCount=96;m->primitiveCount=32;m->topology=D3DPT_TRIANGLELIST;m->indexed=false;
    m->streams[0].stride=20;m->streams[0].bytes.resize(96*20);
    for(unsigned i=0;i<96;++i){const float p[]={float(int(i%7)-3)*.25f,float(int(i%5)-2),float(i%11)*.5f,1};
        std::memcpy(m->streams[0].bytes.data()+i*20,p,16);m->streams[0].bytes[i*20+16]=uint8_t(i%groups);}
    return m;
}
static float inverse[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
static void warm(EnvelopeCache& cache,const Prepared& prepared,const std::shared_ptr<const Mesh>& m,const float* c){
    cache.beginFrame(false);Budget b;Bounds out;Info info;
    cache.calculatePrepared(prepared,*m,m,c,inverse,b,out,Stage::Build,&info);
    assert(cache.stats().completed==1);
}
static bool same(const Bounds& a,const Bounds& b){return a.valid==b.valid&&(!a.valid||(!std::memcmp(a.low,b.low,12)&&!std::memcmp(a.high,b.high,12)));}
static uint64_t checksum=14695981039346656037ull;
static void hash(const Bounds& b){for(const float* part:{b.low,b.high})for(unsigned i=0;i<12;++i){checksum^=reinterpret_cast<const uint8_t*>(part)[i];checksum*=1099511628211ull;}}
static void benchmark(){
    for(bool defined:{false,true}){
        auto p=program(defined);auto m=mesh(1);auto prepared=EnvelopeCache::prepareProgram(p,declaration,3);assert(prepared);
        float c[1024]={};c[0]=c[5]=c[10]=1;EnvelopeCache cache;BoundsTestClock::realtime=false;warm(cache,*prepared,m,c);
        BoundsTestClock::realtime=true;BoundsTestClock::calls=0;size_t valid=0;const auto start=std::chrono::steady_clock::now();
        for(unsigned frame=0;frame<200;++frame){cache.beginFrame(false,1222);Budget b;b.maxOperations=1000000;
            for(unsigned draw=0;draw<1222;++draw){c[7]=float(draw)*.125f;Bounds out;
                auto status=cache.calculatePrepared(*prepared,*m,m,c,inverse,b,out,Stage::Cheap);
                // A preempted evaluation may classify this source as heavy.
                // Complete that work too, so both benchmarks evaluate every pose.
                if(status==Status::Budget){cache.beginFrame(false,1222);b={};status=cache.calculatePrepared(*prepared,*m,m,c,inverse,b,out,Stage::Ready);}
                assert(status==Status::Valid);++valid;hash(out);}
            cache.settleClock();}
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/200;
        std::printf("RIGID_BENCH definitions=%u packets=1222 valid=%zu clockCalls=%llu msPerFrame=%.6f checksum=%016llx\n",unsigned(defined),valid,(unsigned long long)BoundsTestClock::calls,ms,(unsigned long long)checksum);
    }
}
int main(int argc,char**){
    if(argc>1){benchmark();return 0;}
    std::mt19937 rng(73129);std::uniform_real_distribution<float> value(-3,3);
    size_t comparisons=0,vertices=0,fast=0;
    for(unsigned groups:{1u,8u,9u})for(bool defined:{false,true}){
        auto p=program(defined);auto m=mesh(groups);auto prepared=EnvelopeCache::prepareProgram(p,declaration,3);assert(prepared);
        float c[1024]={};EnvelopeCache cache;warm(cache,*prepared,m,c);
        for(unsigned pose=0;pose<512;++pose){
            for(unsigned i=0;i<12;++i)c[i]=value(rng);c[3]*=100;c[7]*=100;c[11]*=100;
            const float angle=value(rng);inverse[0]=inverse[5]=std::cos(angle);inverse[1]=std::sin(angle);inverse[4]=-inverse[1];
            inverse[12]=value(rng)*10000;inverse[13]=value(rng)*10000;inverse[14]=value(rng)*100;
            cache.beginFrame(false);Budget a,b;Bounds quick,reference;Info info;
            auto status=cache.calculatePrepared(*prepared,*m,m,c,inverse,a,quick,Stage::Cheap,&info);
            assert(info.groups==groups&&!info.skin);
            if(groups>8){assert(status==Status::Budget&&!quick.valid&&info.kind==EnvelopeCache::Kind::Heavy);continue;}
            assert(status==Status::Valid&&cache.stats().fastValid==1);++fast;
            assert(cache.calculatePrepared(*prepared,*m,m,c,inverse,b,reference,Stage::Heavy)==Status::Valid);
            assert(same(quick,reference)&&a.operations==b.operations&&a.vertices==0);hash(quick);++comparisons;
            for(unsigned i=0;i<m->vertexCount;++i){NorthlightActorDeformation::Four inputs[16]={},position;
                std::memcpy(inputs[0].data(),m->streams[0].bytes.data()+i*20,16);inputs[1][0]=float(i%groups);
                assert(NorthlightActorDeformation::evaluate(p,inputs,c,position));
                for(unsigned axis=0;axis<3;++axis){float world=inverse[12+axis];for(unsigned j=0;j<3;++j)world+=position[j]*inverse[4*j+axis];
                    assert(world>=quick.low[axis]&&world<=quick.high[axis]);}++vertices;}
        }
        cache.beginFrame(false);Budget cap;cap.maxOperations=0;Bounds out;
        assert(cache.calculatePrepared(*prepared,*m,m,c,inverse,cap,out,Stage::Cheap)!=Status::Valid&&!out.valid);
        const float old=inverse[3];inverse[3]=.25f;cache.beginFrame(false);Budget b;
        assert(cache.calculatePrepared(*prepared,*m,m,c,inverse,b,out,Stage::Heavy)==Status::Invalid&&!out.valid);inverse[3]=old;
        c[7]=std::numeric_limits<float>::quiet_NaN();cache.beginFrame(false);b={};
        assert(cache.calculatePrepared(*prepared,*m,m,c,inverse,b,out,Stage::Heavy)==Status::Unsupported&&!out.valid);
        if(groups<=8){cache.beginFrame(false);b={};assert(cache.calculatePrepared(*prepared,*m,m,c,inverse,b,out,Stage::Cheap)==Status::Unsupported&&!out.valid);}
        // A distinct owner or changed program must never borrow a warm envelope.
        auto other=mesh(groups);cache.beginFrame(false);b={};Info info;
        assert(cache.calculatePrepared(*prepared,*other,other,c,inverse,b,out,Stage::Cheap,&info)==Status::Budget&&!out.valid&&info.kind==EnvelopeCache::Kind::Cold);
        p.operations.back().source[1].token=0xa0e40001;auto changed=EnvelopeCache::prepareProgram(p,declaration,3);assert(changed);
        cache.beginFrame(false);b={};assert(cache.calculatePrepared(*changed,*m,m,c,inverse,b,out,Stage::Cheap,&info)==Status::Budget&&!out.valid&&info.kind==EnvelopeCache::Kind::Cold);
    }
    // The generic fast lane stops at 128 operations even for <=8 groups.
    for(unsigned operations:{16u,17u}){
        auto p=program(false);NorthlightActorDeformation::Operation mov;mov.code=1;mov.destination=0x800f0000;mov.source[0].token=0x80e40000;
        while(p.operations.size()<operations)p.operations.push_back(mov);
        auto m=mesh(8);auto prepared=EnvelopeCache::prepareProgram(p,declaration,3);assert(prepared);float c[1024]={};c[0]=c[5]=c[10]=1;
        EnvelopeCache cache;warm(cache,*prepared,m,c);cache.beginFrame(false);Budget b;Bounds out;Info info;
        const auto status=cache.calculatePrepared(*prepared,*m,m,c,inverse,b,out,Stage::Cheap,&info);
        assert(info.operations==8*operations&&bool(cache.stats().fastValid)==(operations==16));
        assert((status==Status::Valid)==(operations==16));
    }
    // Account all batched time before lending; stop at the next batch boundary.
    auto p=program(false);auto m=mesh(1);auto prepared=EnvelopeCache::prepareProgram(p,declaration,3);float c[1024]={};c[0]=c[5]=c[10]=1;
    EnvelopeCache cache;warm(cache,*prepared,m,c);cache.beginFrame(false);BoundsTestClock::calls=0;
    Budget budget;Bounds out;for(unsigned i=0;i<64;++i)assert(cache.calculatePrepared(*prepared,*m,m,c,inverse,budget,out,Stage::Cheap)==Status::Valid);
    cache.settleClock();assert(BoundsTestClock::calls<=12);
    cache.beginFrame(false);BoundsTestClock::step=AdaptiveTimeBudget::Cheap;budget={};unsigned admitted=0;
    while(cache.calculatePrepared(*prepared,*m,m,c,inverse,budget,out,Stage::Cheap)==Status::Valid)++admitted;
    assert(admitted==ReadyClockStride&&!out.valid);cache.finishReservedTurnsAndLend();assert(cache.readyMilliseconds()>=.28);BoundsTestClock::step=0;
    std::printf("RIGID_BOUNDS_PASS comparisons=%zu vertices=%zu fast=%zu checksum=%016llx heavyDeferred=1 currentPose=1 defs=1 ownerAndKey=1 clockBudget=1\n",comparisons,vertices,fast,(unsigned long long)checksum);
}
