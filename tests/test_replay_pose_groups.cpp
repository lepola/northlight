#include "replay_pose_groups.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>
using namespace NorthlightReplayPoses;
using namespace NorthlightReplayConstants;
struct Replay {
    NorthlightShaderConstants::Usage constantUsage;
    float constants[1024]={};int32_t bools[16]={};int ints[64]={};
    unsigned projectionKind=2,constantGroup=0;
};
static void group(std::vector<Replay>& p){
    for(size_t i=0;i<p.size();++i)p[i].constantGroup=i?p[i-1].constantGroup+(!sameCaptured(p[i-1],p[i])):0;
}
static void equality(){
    Replay a,b;assert(sameCaptured(a,b));
    for(unsigned n=0;n<1024;++n){
        b=a;b.constants[n]=1;
        assert(sameCaptured(a,b)==(n>=8&&n<24));
    }
    for(unsigned n=0;n<16;++n){b=a;b.bools[n]=1;assert(!sameCaptured(a,b));}
    for(unsigned n=0;n<64;++n){b=a;b.ints[n]=1;assert(!sameCaptured(a,b));}
    b=a;b.constants[100]=-0.f;assert(!sameCaptured(a,b));
    const uint32_t nan=0x7fc01234;std::memcpy(a.constants+100,&nan,4);b=a;assert(sameCaptured(a,b));
    uint32_t other=nan+1;std::memcpy(b.constants+100,&other,4);assert(!sameCaptured(a,b));
    a=Replay{};a.constantUsage.floats={2,26};a.constantUsage.booleans={};a.constantUsage.integers={};b=a;
    b.constants[0]=1;b.constants[1023]=1;b.bools[0]=1;b.ints[0]=1;assert(sameCaptured(a,b));
    b.constantUsage.floats={1,27};assert(!sameCaptured(a,b));
    a=Replay{};b=a;b.projectionKind=1;assert(!sameCaptured(a,b));a=b;b.constants[16]=1;assert(sameCaptured(a,b));
    a.constantUsage.floats={0,257};b=a;assert(!sameCaptured(a,b));
    a=Replay{};a.constantUsage.booleans={17,0};b=a;assert(!sameCaptured(a,b));
    a=Replay{};a.constantUsage.floats={8,2};b=a;assert(!sameCaptured(a,b));
    std::puts("PASS exact captured ranges, both projection layouts, all bones, bool/int banks, signed zero and NaN payloads");
}
template<class T> void apply(T* bank,const T* desired,DirtySpan span,unsigned width){
    if(span.count)std::memcpy(bank+span.first*width,desired+span.first*width,span.count*width*sizeof(T));
}
static void reference(){
    std::mt19937 rng(41459);std::vector<Replay> draws(1200);
    size_t visited=0,reused=0;
    for(unsigned frame=0;frame<20;++frame){
        for(size_t n=0;n<draws.size();++n){
            auto& p=draws[n];
            if(n&&rng()%4){p=draws[n-1];p.constants[(p.projectionKind==1?4:2)*4]=float(rng()%10);}
            else{
                p=Replay{};p.projectionKind=rng()%2+1;
                p.constantUsage.floats=rng()%2?NorthlightShaderConstants::Range{0,256}:NorthlightShaderConstants::Range{0,40};
                p.constantUsage.booleans=rng()%2?NorthlightShaderConstants::Range{2,5}:NorthlightShaderConstants::Range{};
                p.constantUsage.integers=rng()%2?NorthlightShaderConstants::Range{3,6}:NorthlightShaderConstants::Range{};
                for(auto& x:p.constants)x=float(int(rng()%200)-100)/32;
                for(auto& x:p.bools)x=rng()%2;for(auto& x:p.ints)x=int(rng()%20);
            }
        }
        group(draws);
        // Every cascade/cube face creates fresh banks and its own projection.
        for(unsigned face=0;face<8;++face){
            float rows[16];for(auto& x:rows)x=float(int(rng()%200)-100)/64;
            Pass<int32_t> optimized;
            BoundedDirtyRegisters<float,256,4> f;BoundedDirtyRegisters<int32_t,16,1> b;BoundedDirtyRegisters<int,16,4> i;
            float expectedF[1024]={},actualF[1024]={};int32_t expectedB[16]={},actualB[16]={};int expectedI[64]={},actualI[64]={};
            for(const auto& p:draws){
                if(rng()%3==0)continue; // cascade-specific bounds/static-proof skips
                float desired[1024];assert(prepareFloatConstants(desired,p.constants,p.constantUsage,p.projectionKind,rows));
                auto sf=f.update(desired,p.constantUsage.floats),sb=b.update(p.bools,p.constantUsage.booleans),si=i.update(p.ints,p.constantUsage.integers);
                apply(expectedF,desired,sf,4);apply(expectedB,p.bools,sb,1);apply(expectedI,p.ints,si,4);
                assert(optimized.prepare(p,rows));
                assert(sf.first==optimized.floats.first&&sf.count==optimized.floats.count);
                assert(sb.first==optimized.booleans.first&&sb.count==optimized.booleans.count);
                assert(si.first==optimized.integers.first&&si.count==optimized.integers.count);
                apply(actualF,optimized.desired(),optimized.floats,4);apply(actualB,p.bools,optimized.booleans,1);apply(actualI,p.ints,optimized.integers,4);
                assert(!std::memcmp(expectedF,actualF,sizeof actualF));assert(!std::memcmp(expectedB,actualB,sizeof actualB));assert(!std::memcmp(expectedI,actualI,sizeof actualI));++visited;
            }
            reused+=optimized.reused;
        }
    }
    std::printf("PASS %zu reference draw bank comparisons across 20 frames x 8 passes, %zu preparations reused, with changing cameras/poses/ranges and culled draws\n",visited,reused);
}
static void worldProjection(){
    Replay grass;grass.projectionKind=1;grass.constantUsage.floats={0,14};grass.constantUsage.booleans={};grass.constantUsage.integers={};
    for(unsigned i=0;i<1024;++i)grass.constants[i]=float(i+100);
    float rows[16],world[16];for(unsigned i=0;i<16;++i){rows[i]=float(int(i)-8);world[i]=float(1000+i);}
    Pass<int32_t> pass;assert(pass.prepare(grass,rows,world));
    assert(std::memcmp(pass.desired()+16,world,sizeof world)==0);
    assert(std::memcmp(pass.desired(),grass.constants,16*sizeof(float))==0);
    assert(std::memcmp(pass.desired()+32,grass.constants+32,24*sizeof(float))==0); // includes native c9 fade
    assert(pass.prepare(grass,rows,world)&&pass.reused==1&&!pass.floats.count);
    ++grass.constantGroup;grass.constants[36]=-.125f;assert(pass.prepare(grass,rows,world));
    assert(pass.desired()[36]==-.125f&&std::memcmp(pass.desired()+16,world,sizeof world)==0);
    Replay actor=grass;actor.projectionKind=2;++actor.constantGroup;
    assert(pass.prepare(actor,rows,world)&&std::memcmp(pass.desired()+8,rows,sizeof rows)==0);
    std::puts("PASS grass world projection preserves captured view/fade and grouping; actors retain view projection");
}
static volatile uint64_t sink;
static void benchmark(unsigned parts){
    std::vector<Replay> draws(1200);
    for(size_t n=0;n<draws.size();++n){auto& p=draws[n];p.constantUsage.booleans={};p.constantUsage.integers={};
        // Full bone palette, with a mix of world and palette differences.
        for(unsigned j=32;j<1024;++j)p.constants[j]=float((n/parts)*7+j)/128;
    }
    constexpr unsigned frames=500;
    auto run=[&](bool optimized){
        uint64_t checksum=0;auto start=std::chrono::steady_clock::now();
        for(unsigned frame=0;frame<frames;++frame){
            if(optimized)group(draws); // include the new capture comparison cost
            for(unsigned cascade=0;cascade<2;++cascade){
                float rows[16]={float(frame),float(cascade)};
                if(optimized){Pass<int32_t> pass;for(const auto& p:draws){assert(pass.prepare(p,rows));checksum+=pass.floats.count;}}
                else{BoundedDirtyRegisters<float,256,4> f;BoundedDirtyRegisters<int32_t,16,1> b;BoundedDirtyRegisters<int,16,4> i;
                    for(const auto& p:draws){float desired[1024];assert(prepareFloatConstants(desired,p.constants,p.constantUsage,p.projectionKind,rows));checksum+=f.update(desired,p.constantUsage.floats).count+b.update(p.bools,p.constantUsage.booleans).count+i.update(p.ints,p.constantUsage.integers).count;}}
            }
        }
        sink=checksum;return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    };
    double old=run(false),now=run(true);
    std::printf("BENCH native helper only %u parts/pose, 1200 draws x 2 cascades x %u frames including grouping: baseline %.3f ms, grouped %.3f ms, %.3fx; prepares/frame 2400 -> %u; extra persistent bytes/draw %zu\n",parts,frames,old,now,old/now,2400/parts,sizeof(unsigned));
}
int main(){equality();reference();worldProjection();benchmark(8);benchmark(4);benchmark(1);}
