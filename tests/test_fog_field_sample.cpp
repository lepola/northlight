#define NorthlightSpellFog OldSpellFog
#include "fog_reference/march-wave2-old.h"
#undef NorthlightSpellFog
#include "spell_fog_march.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <random>
int main(){using namespace NorthlightRegionalFog;Field f;f.originX=f.originY=-256;std::mt19937 rng(78312);
 for(unsigned mode=0;mode<4;++mode){for(unsigned i=0;i<N*N;++i)f.texels[i]={float(int(i%23)-10),.02f,.01f,mode==0?5.f:mode==1?(i%7?5.f:0.f):mode==2?0.f:(i%2?5.f:0.f)};
  for(unsigned i=0;i<100000;++i){float x=float(int(rng()%700)-350)+float(rng()%8)/8.f,y=float(int(rng()%700)-350)+float(rng()%8)/8.f;const auto a=OldSpellFog::fieldAt(f,x,y);const auto b=NorthlightSpellFog::fieldAt(f,x,y);static_assert(sizeof a==sizeof b);assert(!std::memcmp(&a,&b,sizeof a));}
  for(float x:{-257.f,-256.f,0.f,248.f,249.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})for(float y:{-256.f,0.f,248.f,std::numeric_limits<float>::quiet_NaN()}){const auto a=OldSpellFog::fieldAt(f,x,y);const auto b=NorthlightSpellFog::fieldAt(f,x,y);assert(!std::memcmp(&a,&b,sizeof a));}}
}
