#pragma STDC FENV_ACCESS ON
#include "probe_moment_pack.h"
#include <cassert>
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <random>
int main(){std::mt19937_64 rng(48171);unsigned cases=0;
 for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){assert(std::fesetround(mode)==0);
  for(unsigned i=0;i<200000;++i){double a,b,w;auto bits=rng();std::memcpy(&a,&bits,8);bits=rng();std::memcpy(&b,&bits,8);bits=rng();std::memcpy(&w,&bits,8);if(!(w>1e-10))continue;
   NorthlightGI::DistanceMoment old{float(a/w),float(b/w)},now{};NorthlightGI::packDistanceMoment(now,a,b,w);assert(!std::memcmp(&old,&now,sizeof old));++cases;
  }
 }
 assert(std::fesetround(FE_TONEAREST)==0);std::printf("PROBE_MOMENT_PACK_PASS cases=%u roundingModes=4 exactBits=1 timings=0\n",cases);
}
