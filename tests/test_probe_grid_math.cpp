#pragma STDC FENV_ACCESS ON
#include "world_probe_cache.h"
#include <cassert>
#include <cfenv>
#include <cstdio>
#include <cstring>
#include <random>
using namespace NorthlightGI;
static bool reference(Vec3 position,ProbeGridKey& key){const float values[]={position.x,position.y,position.z};int32_t cells[3];for(unsigned i=0;i<3;++i){const double cell=double(values[i])/8.0;if(!std::isfinite(cell))return false;const double rounded=std::round(cell);if(std::fabs(cell-rounded)>.00001||rounded<double(INT32_MIN)||rounded>double(INT32_MAX))return false;cells[i]=int32_t(rounded);}key={cells[0],cells[1],cells[2]};return true;}
int main(){std::mt19937 random(458291);unsigned cases=0;
 for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){assert(std::fesetround(mode)==0);for(unsigned i=0;i<200000;++i){float p[3];for(auto& value:p){auto bits=random();std::memcpy(&value,&bits,4);if(i%3==0)value=float(int(random()%200000)-100000)*8;else if(i%3==1)value=float(int(random()%200000)-100000)*8+float(int(random()%9)-4)*.00001f;}
  ProbeGridKey a{1,2,3},b=a;bool ok=reference({p[0],p[1],p[2]},a);assert(probeGridKey({p[0],p[1],p[2]},b)==ok&&a==b);++cases;
 }}
 assert(std::fesetround(FE_TONEAREST)==0);
 for(unsigned width=4;width<=24;++width){assert(configureProbeLayout({2,2,width}));for(unsigned i=0;i<20000;++i){ProbeGridKey key{int32_t(random()),int32_t(random()),int32_t(random())};if(i==0)key={INT32_MIN,INT32_MAX,-1};const auto wrap=[width](int32_t v){const int64_t r=int64_t(v)%int64_t(width);return size_t(r<0?r+width:r);};assert(probeAtlasIndex(key)==wrap(key.x)+wrap(key.y)*width+wrap(key.z)*width*width);++cases;}}
 assert(configureProbeLayout({14,14,16}));std::printf("PROBE_GRID_MATH_PASS cases=%u exactKeys=1 exactModulo=1 roundingModes=4 timings=0\n",cases);
}
