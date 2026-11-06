#include "replay_bounds.h"
#include "fixtures/reference/replay_interval_eager.h"
#include <cstdio>
#include <cassert>
#include <chrono>
#include <algorithm>
using namespace NorthlightReplayBounds;
using namespace NorthlightActorDeformation;
static uint32_t rng=0xa359651a;
static uint32_t randomWord(){rng=rng*1664525u+1013904223u;return rng;}
static float randomFloat(){return (float(randomWord()>>8)/16777216.f-.5f)*1000.f;}
static Operation op(unsigned code,uint32_t dst,uint32_t a,uint32_t b=0,uint32_t c=0){Operation o;o.code=code;o.destination=dst;o.source[0].token=a;o.source[1].token=b;o.source[2].token=c;return o;}
static Program program(unsigned mode){Program p;p.major=3;p.positionRegister=0;p.inputs.push_back({0,0,0});
    p.operations.push_back(op(1,0x800f0000,0x90e40000));
    p.operations.push_back(op(1,0x800f0001,0x90e40000));p.operations.push_back(op(1,0x800f0002,0x90e40000));
    for(unsigned n=0;n<mode;++n)p.operations.push_back(op(4,0x80070000+(n%3),0x80e40000+((n+2)%3),0xa0e40001+(n%4),0xa0e40005));
    p.operations.push_back(op(1,0x80080000,0xa0ff00fa));return p;}
template<bool Fast> __attribute__((noinline)) static bool evaluate(const Program& p,const detail::Vector* inputs,const float* c,detail::Vector& output,bool defs){
    if constexpr(Fast)return detail::evaluateIntervals(p,inputs,c,output,defs);
    else return detail::legacyEvaluateIntervals(p,inputs,c,output,defs);
}
int main(int argc,char**){
    float constants[1024]={};detail::Vector inputs[16]={};unsigned accepted=0,cases=0;
    for(unsigned trial=0;trial<50000;++trial){
        Program p=program(trial%31);for(auto& v:constants)v=randomFloat();constants[250*4+3]=1;
        for(unsigned i=0;i<4;++i)inputs[0][i]={randomFloat(),std::fabs(randomFloat())*.00001};inputs[0][3]={1,0};
        if(trial%13==0)inputs[0][trial%3].value=NAN;
        if(trial%17==0)inputs[0][trial%3].error=INFINITY;
        if(trial%19==0){p.operations[0].source[0].token=0x80e4001f;} // uninitialized source
        if(trial%23==0)p.operations[0].destination=0x80010000; // undefined swizzle lanes
        if(trial%7==0)p.definitions.push_back({1,{1,2,3,4}});
        for(bool defs:{false,true}){detail::Vector a,b;const bool old=evaluate<false>(p,inputs,constants,a,defs),now=evaluate<true>(p,inputs,constants,b,defs);assert(old==now);
            if(old){++accepted;for(unsigned j=0;j<4;++j){assert(!std::memcmp(&a[j].value,&b[j].value,sizeof(float)));assert(!std::memcmp(&a[j].error,&b[j].error,sizeof(double)));}}++cases;}
    }
    assert(accepted>10000);std::printf("PASS %u interval cases exact (%u accepted), masked/undefined/NaN/Inf/DEF\n",cases,accepted);
    if(argc>1){
        for(unsigned mode:{0u,4u,18u,32u}){
            Program p=program(mode==32?18:mode);
            if(mode==32)for(unsigned reg=3;reg<32;++reg)p.operations.insert(p.operations.begin(),op(1,0x800f0000+reg,0x90e40000));
            for(float& c:constants)c=.1f;constants[250*4+3]=1;
            inputs[0][0]={1,0};inputs[0][1]={2,0};inputs[0][2]={3,0};inputs[0][3]={1,0};
            std::vector<double> before,after;unsigned checksum=0;
            for(unsigned run=0;run<9;++run){
                auto timed=[&](bool fast){auto start=std::chrono::steady_clock::now();detail::Vector result;for(unsigned n=0;n<100000;++n){constants[4]=float(n%11)*.0001f;checksum+=fast?evaluate<true>(p,inputs,constants,result,false):evaluate<false>(p,inputs,constants,result,false);}
                    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();};
                if(run%2){after.push_back(timed(true));before.push_back(timed(false));}else{before.push_back(timed(false));after.push_back(timed(true));}
            }
            std::sort(before.begin(),before.end());std::sort(after.begin(),after.end());std::printf("BENCH mode=%u old_ms=%.3f new_ms=%.3f ratio=%.4f checksum=%u\n",mode,before[4],after[4],after[4]/before[4],checksum);
        }
    }
}
