#pragma STDC FENV_ACCESS ON
#include "gi_trig.h"
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
int main(){std::mt19937 random(92346);unsigned cases=0,different=0;double maxError=0;
 for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){assert(std::fesetround(mode)==0);for(unsigned i=0;i<250000;++i){float angle=(float(random())/float(UINT32_MAX))*2600000.f;if(i<10000)angle=float(i)*.017f;float oldS=std::sin(angle),oldC=std::cos(angle),s,c;NorthlightGI::sincosAngle(angle,s,c);different+=std::memcmp(&oldS,&s,4)!=0||std::memcmp(&oldC,&c,4)!=0;maxError=std::max(maxError,std::max(std::fabs(double(oldS)-s),std::fabs(double(oldC)-c)));assert(std::isfinite(s)&&std::isfinite(c));++cases;}}
 assert(maxError<=1.2e-7);assert(std::fesetround(FE_TONEAREST)==0);std::printf("GI_TRIG_PASS cases=%u different=%u maxError=%.9g roundingModes=4 timings=0\n",cases,different,maxError);
}
