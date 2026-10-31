// The old division and the production reciprocal multiply are monotone in x.
// Matching every integer transition in ordered-float space proves equal chunk
// indices for every finite float the production guard accepts.
#include "regional_fog.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
using namespace NorthlightRegionalFog;
static int reference(float x){return int(std::floor((WorldZero-x)/ChunkSize));}
static uint32_t ordered(float x){uint32_t b;std::memcpy(&b,&x,4);return (b&0x80000000u)?~b:(b^0x80000000u);}
static float value(uint32_t o){uint32_t b=(o&0x80000000u)?(o^0x80000000u):~o;float x;std::memcpy(&x,&b,4);return x;}
int main(){
 const uint64_t lo=ordered(-100000.f),hi=uint64_t(ordered(100000.f))+1;
 auto boundary=[&](int target,bool current){uint64_t l=lo,h=hi;while(l<h){const uint64_t m=l+(h-l)/2;float x=value(uint32_t(m));int v=current?chunkIndex(x):reference(x);if(v<target)h=m;else l=m+1;}return l;};
 assert(chunkIndex(-100000.f)==reference(-100000.f)&&chunkIndex(100000.f)==reference(100000.f));
 unsigned n=0;for(int target=reference(100000.f)+1;target<=reference(-100000.f);++target){assert(boundary(target,false)==boundary(target,true));++n;}
 for(float x:{-0.f,0.f,1.f,-1.f,100000.f,-100000.f})assert(chunkIndex(x)==reference(x));
 std::printf("PASS %u ordered-float chunk transition boundaries\n",n);
}
