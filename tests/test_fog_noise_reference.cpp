#define NorthlightFogClouds OldFogClouds
#include "fog_reference/noise-old.h"
#undef NorthlightFogClouds
#include "fog_clouds.h"
#include <cassert>
#include <cstring>
#include <random>
int main(){std::mt19937 rng(2352);for(unsigned i=0;i<1000000;++i){uint32_t bits=rng();float value;std::memcpy(&value,&bits,4);if(std::isfinite(value)){const uint8_t old=uint8_t(std::clamp(std::floor(value),0.f,255.f));assert(old==NorthlightFogClouds::detail::normalizedByte(value));}}for(unsigned p=1;p<32;++p)for(int i=0;i<2000;++i){float x=float(int(rng()%10000)-5000)/100.f,y=float(int(rng()%10000)-5000)/100.f,z=float(int(rng()%10000)-5000)/100.f;auto seed=rng(),o=rng()%3;float a=OldFogClouds::detail::perlin(p,seed,o,x,y,z),b=NorthlightFogClouds::detail::perlin(p,seed,o,x,y,z);assert(!std::memcmp(&a,&b,4));}for(unsigned o=0,P=4;o<3;++o,P*=2){NorthlightFogClouds::detail::PerlinAxis axis[64];for(unsigned i=0;i<64;++i)axis[i]=NorthlightFogClouds::detail::perlinAxis(float(i),float(P)/64.f);for(unsigned z=0;z<64;++z)for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){float a=OldFogClouds::detail::perlin(P,42,o,float(x),float(y),float(z)),b=NorthlightFogClouds::detail::perlinPrepared(P,42,o,axis[x],axis[y],axis[z],nullptr);assert(!std::memcmp(&a,&b,4));}}for(unsigned seed:{0u,1u,42u,0xffffffffu})assert(OldFogClouds::generate(seed)==NorthlightFogClouds::generate(seed));for(unsigned i=0;i<12;++i){const auto seed=rng();assert(OldFogClouds::generate(seed)==NorthlightFogClouds::generate(seed));}}
