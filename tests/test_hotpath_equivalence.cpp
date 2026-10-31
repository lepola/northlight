#include "local_light_batches.h"
#include "point_light_shadow.h"
#include "worker_actor_memo.h"
#include "world_probe_cache.h"
#include "gpu_budget.h"
#include "fixtures/hotpaths-9537631/local_light_selection.h"
#include "fixtures/hotpaths-9537631/local_light_batches.h"
#include "fixtures/hotpaths-9537631/point_light_shadow.h"
#include "fixtures/hotpaths-9537631/worker_actor_memo.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <limits>
#include <random>

namespace L=NorthlightLocalLightSelection;
namespace P=NorthlightPointShadow;
namespace R=ReferenceNorthlightPointShadow;
static std::mt19937 rng(291029);
static float number(float low,float high){return std::uniform_real_distribution<float>(low,high)(rng);}
static NorthlightLocalLights::Light lamp(unsigned id){
    return {{number(-300,300),number(-300,300),number(-300,300)},
        {number(.01f,8),number(.01f,8),number(.01f,8)},.1f,number(.12f,200),id,1,1};
}
template<class A,class B> static void sameSelection(const A& a,const B& b){
    assert(a.count==b.count&&a.nearest==b.nearest&&a.fogDistance==b.fogDistance);
    assert(a.position==b.position&&a.color==b.color&&a.fog==b.fog);
    for(unsigned i=0;i<a.count;++i)assert(std::memcmp(&a.sources[i],&b.sources[i],sizeof(a.sources[i]))==0);
}
static void lightTests(){
    for(unsigned i=0;i<100000;++i){NorthlightLocalLightBatches::Rect a{},b{};
        if(i%3){a={long(rng()%1024),long(rng()%1024),long(rng()%1024),long(rng()%1024)};}
        if(i%5){b={long(rng()%1024),long(rng()%1024),long(rng()%1024),long(rng()%1024)};}
        assert(NorthlightLocalLightBatches::wide(a,b)==ReferenceNorthlightLocalLightBatches::wide(a,b));}
    float identity[16]={};for(unsigned i=0;i<4;++i)identity[5*i]=1;
    const float proj[]={1.1f,1.4f,1};
    auto v=NorthlightLocalLightScissor::view(identity,proj,.1f,.5f,3840,2160);
    L::Tracker a;ReferenceNorthlightLocalLightSelection::Tracker b;
    std::vector<NorthlightLocalLights::Light> lights;
    for(unsigned frame=0;frame<4000;++frame){
        if(frame%5==0){lights.clear();for(unsigned i=0;i<frame%260;++i)lights.push_back(lamp(i));}
        float camera[]={number(-20,20),number(-20,20),number(-20,20)};
        const unsigned limit=frame%129;const bool continuous=frame%11!=0;
        const float dt=frame%7==0?.25f:1.f/120;
        const auto x=a.update(lights,camera,limit,dt,continuous,&v);
        const auto y=b.update(lights,camera,limit,dt,continuous,&v);
        sameSelection(x,y);assert(a.count==b.count&&a.fading==b.fading&&a.offscreen==b.offscreen);
        for(unsigned i=0;i<a.count;++i){assert(a.entries[i].id==b.entries[i].id&&a.entries[i].shown==b.entries[i].shown&&a.entries[i].selected==b.entries[i].selected);}
        auto s=x,t=x;
        const auto c=NorthlightLocalLightBatches::arrange(s,v);const auto d=ReferenceNorthlightLocalLightBatches::arrange(t,v);
        assert(c.arranged==d.arranged&&c.before==d.before&&c.after==d.after);sameSelection(s,t);
        if(frame%5==0){for(unsigned i=0;i<s.count;++i)s.position[i]={0,0,0,100};t=s;
            const auto e=NorthlightLocalLightBatches::arrange(s,v);const auto f=ReferenceNorthlightLocalLightBatches::arrange(t,v);
            assert(!e.arranged&&!f.arranged&&e.before==f.before&&e.after==f.after);sameSelection(s,t);}
        NorthlightLocalLightScissor::Bounds bounds;
        for(unsigned i=0;i<x.count;++i)NorthlightLocalLightScissor::add(v,x.position[i].data(),bounds);
        auto all=NorthlightLocalLightScissor::rect(v,bounds),fast=NorthlightLocalLightScissor::batchRect(v,x.position,x.count);
        assert(all.left==fast.left&&all.top==fast.top&&all.right==fast.right&&all.bottom==fast.bottom);
    }
}
static void pointTests(){
    std::array<uint64_t,P::MaxLights> history{};
    for(unsigned trial=0;trial<4000;++trial){
        unsigned weights[6],a[6],b[6];for(auto& w:weights)w=rng();
        for(unsigned n=0;n<9;++n){assert(P::partitionFaces(n,weights,a)==R::partitionFaces(n,weights,b));assert(std::equal(a,a+6,b));}
        float lo[3],hi[3],matrices[6][16];for(unsigned k=0;k<3;++k){lo[k]=number(-1e4,1e4);hi[k]=lo[k]+number(0,500);}
        for(auto& m:matrices)for(auto& f:m)f=number(-10,10);
        if(trial%13==0)lo[trial%3]=std::numeric_limits<float>::quiet_NaN();
        if(trial%17==0)matrices[trial%6][trial%16]=std::numeric_limits<float>::infinity();
        for(auto& m:matrices)assert(P::clipReject(lo,hi,m)==R::clipReject(lo,hi,m));
        assert(P::faceMask(lo,hi,true,matrices)==R::faceMask(lo,hi,true,matrices));
        std::vector<NorthlightLocalLights::Light> lights;for(unsigned i=0;i<trial%400;++i)lights.push_back(lamp(i%190));
        // Empty, sparse, full, zero-ID and duplicate-score histories.
        history.fill(0);for(unsigned i=0;i<trial%129;++i)history[i]=rng()%190;
        if(trial%9==0)for(auto& l:lights){l.position[0]=l.position[1]=l.position[2]=0;l.diffuse[0]=l.diffuse[1]=l.diffuse[2]=1;l.attenuationEnd=50;}
        float camera[]={number(-100,100),number(-100,100),number(-100,100)};
        const unsigned limit=trial%140;
        auto x=P::selectMany(lights.data(),lights.size(),camera,limit,history);auto y=R::selectMany(lights.data(),lights.size(),camera,limit,history);
        assert(x.count==y.count&&x.index==y.index);
        const auto exists=P::selectMany(lights.data(),lights.size(),camera,limit,{},48,true);
        assert((exists.count!=0)==(R::selectMany(lights.data(),lights.size(),camera,limit).count!=0));
    }
}
static void memoTests(){
    struct Pair {int first=1,second=2;};auto owner=std::make_shared<Pair>();
    auto a=std::shared_ptr<const int>(owner,&owner->first),b=std::shared_ptr<const int>(owner,&owner->second);
    NorthlightWorkerActorMemo<int> x;ReferenceNorthlightWorkerActorMemo<int> y;
    auto check=[&](const std::shared_ptr<const int>& p){assert(x.matches(p)==y.matches(p));};
    check(a);x.remember(a);y.remember(a);check(a);check(b);check({});
    std::shared_ptr<const int> other(a.get(),[](const int*){});check(other);
    std::shared_ptr<const int> noOwner(std::shared_ptr<const int>{},a.get());
    x.remember(noOwner);y.remember(noOwner);check(noOwner);check(a);
    x.remember(b);y.remember(b);owner.reset();a.reset();b.reset();check(other);
    x.remember({});y.remember({});check({});
}
static void atlasTests(){
    using namespace NorthlightGI;const auto saved=probeLayout();
    for(unsigned n=2;n<=22;n+=2){auto l=probeLayoutFor(n);assert(configureProbeLayout(l));
        for(unsigned i=0;i<10000;++i){ProbeGridKey k{int32_t(rng()),int32_t(rng()),int32_t(rng())};
            auto wrap=[&](int32_t x){auto r=x%int32_t(l.atlas);return size_t(r<0?r+l.atlas:r);};
            assert(probeAtlasIndex(k)==wrap(k.x)+wrap(k.y)*l.atlas+wrap(k.z)*l.atlas*l.atlas);}}
    assert(configureProbeLayout(saved));
}
static void waitTests(){
    using namespace NorthlightGpuBudget;WaitSignal s;float value=0;bool primed=false;
    for(unsigned i=0;i<100000;++i){float input=i%7?0:number(0,10),dt=number(-1,1);
        if(i%101==0)input=std::numeric_limits<float>::quiet_NaN();
        if(i%127==0)dt=std::numeric_limits<float>::infinity();
        s.update(input,dt);
        if(!std::isfinite(input)||input<0)input=0;if(input>MaxWaitMs)input=MaxWaitMs;
        dt=std::isfinite(dt)&&dt>0?(dt<MaxDt?dt:MaxDt):0.f;
        if(!primed){value=input;primed=true;}else value+=(input-value)*(1.f-std::exp(-dt/SmoothSeconds));
        assert(s.smoothed==value&&s.primed==primed&&s.bound()==(value>BoundWaitMs));
    }
}
int main(){lightTests();pointTests();memoTests();atlasTests();waitTests();
    std::puts("PASS baseline equivalence: light fades, three batch paths, full scissors, point history/ranking/face partitions/clip bounds, weak ownership and signed atlas indices");}
