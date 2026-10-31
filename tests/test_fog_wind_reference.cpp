#define NorthlightFogClouds OldFogClouds
#include "fog_reference/noise-wave2-old.h"
#undef NorthlightFogClouds
#include "fog_clouds.h"
#include <cassert>
#include <cfenv>
#include <cstring>
#include <limits>
#include <random>
static void same(const OldFogClouds::Wind& a,const NorthlightFogClouds::Wind& b){
    assert(!std::memcmp(a.large,b.large,sizeof a.large)&&!std::memcmp(a.small,b.small,sizeof a.small));
    assert(!std::memcmp(&a.clock,&b.clock,sizeof a.clock)&&!std::memcmp(&a.speed,&b.speed,sizeof a.speed)&&!std::memcmp(a.dir,b.dir,sizeof a.dir));
}
int main(){std::mt19937 rng(21371);OldFogClouds::Wind a;NorthlightFogClouds::Wind b;
    for(unsigned i=0;i<1000000;++i){float dt=float(rng()%101)/1000.f,fog=float(rng()%101)/100.f;a.advance(dt,fog);b.advance(dt,fog);same(a,b);}
    for(unsigned i=0;i<500000;++i){uint64_t bits=(uint64_t(rng())<<32)|rng();double clock;std::memcpy(&clock,&bits,8);if(!std::isfinite(clock))continue;OldFogClouds::Wind x;NorthlightFogClouds::Wind y;x.clock=y.clock=clock;x.advance(.016f,.5f);y.advance(.016f,.5f);same(x,y);}
    for(float dt:{-1.f,0.f,.016f,.1f,1.f,INFINITY,-INFINITY,std::numeric_limits<float>::quiet_NaN()})for(float fog:{-1.f,0.f,.5f,1.f,2.f,INFINITY,-INFINITY,std::numeric_limits<float>::quiet_NaN()}){OldFogClouds::Wind x;NorthlightFogClouds::Wind y;x.advance(dt,fog);y.advance(dt,fog);same(x,y);}
    for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){assert(std::fesetround(mode)==0);OldFogClouds::Wind x;NorthlightFogClouds::Wind y;for(unsigned i=0;i<10000;++i){x.advance(.016f,.5f);y.advance(.016f,.5f);same(x,y);}}assert(std::fesetround(FE_TONEAREST)==0);
}
