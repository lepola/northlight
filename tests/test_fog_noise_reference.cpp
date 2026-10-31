#define NorthlightFogClouds OldFogClouds
#include "fog_reference/noise-old.h"
#undef NorthlightFogClouds
#include "fog_clouds.h"
#include <cassert>
#include <cstring>
#include <random>
int main(){std::mt19937 rng(2352);for(unsigned p=1;p<32;++p)for(int i=0;i<2000;++i){float x=float(int(rng()%10000)-5000)/100.f,y=float(int(rng()%10000)-5000)/100.f,z=float(int(rng()%10000)-5000)/100.f;auto seed=rng(),o=rng()%3;float a=OldFogClouds::detail::perlin(p,seed,o,x,y,z),b=NorthlightFogClouds::detail::perlin(p,seed,o,x,y,z);assert(!std::memcmp(&a,&b,4));}for(unsigned seed:{0u,1u,42u,0xffffffffu})assert(OldFogClouds::generate(seed)==NorthlightFogClouds::generate(seed));}
