#define NOMINMAX
#undef NDEBUG
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <chrono>
#include "replay_bounds.h"
#include "actor_client_programs.h"
using namespace NorthlightActorDeformation;
namespace B=NorthlightReplayBounds;
static uint32_t seed=12345;
static float next(){seed=seed*1664525u+1013904223u;return float(int(seed%20001)-10000)*.01f;}
template<size_t N> static Program load(const uint32_t (&code)[N]){Program p;assert(compile(code,N,p)&&p.kernel.singleSkin());return p;}
int main(){
    std::vector<Program> programs={load(ClientShaders::OneBoneVs3),load(ClientShaders::OneBoneVs3WLast),load(ClientShaders::OneBoneVs3WMid),load(ClientShaders::OneBoneVs2)};
    auto vs1=programs.back();vs1.major=1;for(auto& op:vs1.operations){if(op.code==46)op.code=1;for(auto& src:op.source)src.address=0;}buildKernel(vs1);assert(vs1.kernel.singleSkin());programs.push_back(vs1);
    float bank[1024]{};B::detail::Vector input[16]={},reference,fast;uint64_t accepted=0,refused=0;
    for(auto& p:programs)for(unsigned trial=0;trial<5000;++trial){
        for(auto& f:bank)f=next();bank[0]=3;bank[1]=1;
        for(const auto& def:p.definitions)std::copy(def.value.begin(),def.value.end(),bank+4*def.reg);
        for(auto& v:input[0])v={next(),std::fabs(double(next()))*.01};input[0][3]={1,0};input[1][0]={float(trial%75),0};
        if(trial%17==0)input[0][0]={-0.f,0};
        if(trial%19==0)bank[4*(31+3*(trial%75))]=std::numeric_limits<float>::denorm_min();
        if(trial%23==0)input[1][0].error=.001;
        if(trial%29==0)bank[0]=2.5f;
        if(trial%31==0)bank[1]=1.00001f;
        if(trial%37==0)bank[4*(31+3*(trial%75))+3]=std::numeric_limits<float>::quiet_NaN();
        if(trial%41==0)input[1][0].value=75;
        if(trial%43==0)input[0][0].value=std::numeric_limits<float>::max();
        const bool old=B::detail::evaluateIntervals(p,input,bank,reference,true);
        const bool eligible=B::detail::singleSkinIntervals(p,input,bank,fast);
        const bool current=eligible||B::detail::evaluateIntervals(p,input,bank,fast,true);
        assert(old==current);if(old)for(unsigned i=0;i<4;++i){assert(reference[i].value==fast[i].value);assert(reference[i].error==fast[i].error);}
        eligible?++accepted:++refused;
    }
    assert(accepted>10000&&refused>1000);
    for(auto& f:bank)f=next();bank[0]=3;bank[1]=1;for(auto& v:input[0])v={next(),.125};input[0][3]={1,0};
    volatile float sink=0;double oldNs[5],newNs[5];const auto& p=programs.front();
    auto timed=[&](bool direct){const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<20000;++i){input[1][0]={float(i%75),0};B::detail::Vector v;
            const bool valid=direct?B::detail::singleSkinIntervals(p,input,bank,v):B::detail::evaluateIntervals(p,input,bank,v,true);assert(valid);sink=v[0].value;}
        return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/20000;};
    for(unsigned i=0;i<5;++i){if(i%2){newNs[i]=timed(true);oldNs[i]=timed(false);}else{oldNs[i]=timed(false);newNs[i]=timed(true);}}
    std::sort(oldNs,oldNs+5);std::sort(newNs,newNs+5);
    std::printf("SINGLE_SKIN_INTERVALS_PASS accepted=%llu fallback=%llu bit_exact_values_and_errors=1 genericNs=%.3f specializedNs=%.3f speedup=%.3f sink=%g\n",accepted,refused,oldNs[2],newNs[2],oldNs[2]/newNs[2],float(sink));
}
