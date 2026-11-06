#pragma STDC FENV_ACCESS ON
#define NorthlightFogClouds FrozenClouds
#include "fog_reference/dense-smoothing-before.h"
#undef NorthlightFogClouds
#include "fog_clouds.h"
#include <cassert>
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <limits>
int main(){
 const float q=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
 const float values[]={-1,-0.f,0,.01f,.5f,1,2,q,inf,-inf};const float times[]={-.1f,-0.f,0,1e-40f,.0167f,.1f,1,q,inf};unsigned cases=0;
 for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){assert(std::fesetround(mode)==0);
  for(float a:values)for(float ta:values)for(float b:values)for(float tb:values)for(float dt:times){
   const float old[]={FrozenClouds::smoothDense(a,ta,dt),FrozenClouds::smoothDense(b,tb,dt)};float now[]={a,b};
   NorthlightFogClouds::smoothDensePair(now[0],ta,now[1],tb,dt);assert(!std::memcmp(old,now,sizeof old));
   const float single=NorthlightFogClouds::smoothDense(a,ta,dt);assert(!std::memcmp(old,&single,sizeof single));++cases;
  }
 }
 assert(std::fesetround(FE_TONEAREST)==0);std::printf("CLOUD_DENSE_PAIR_PASS cases=%u exact=1 timings=0\n",cases);
}
