#pragma STDC FENV_ACCESS ON
#define NorthlightRegionalFog OldFog
#include "fog_reference/regional-wave2-old.h"
#undef NorthlightRegionalFog
#include "regional_fog.h"
#include <cassert>
#include <cfenv>
#include <cstring>
#include <random>
int main(){std::mt19937 rng(3131);for(unsigned i=0;i<1000000;++i){uint32_t words[4]={rng(),rng(),rng(),rng()};NorthlightRegionalFog::Texel t;std::memcpy(&t,words,sizeof t);const bool expected=std::isfinite(t.ground)&&std::isfinite(t.day)&&std::isfinite(t.nightExtra)&&std::isfinite(t.height);assert(NorthlightRegionalFog::texelFinite(t)==expected);}
 for(uint32_t bits:{0u,0x80000000u,0x7f800000u,0xff800000u,0x7fc00001u,0x7f800001u,0xff800001u,0x3f800000u})for(unsigned lane=0;lane<4;++lane){uint32_t words[4]={};words[lane]=bits;NorthlightRegionalFog::Texel t;std::memcpy(&t,words,16);std::feclearexcept(FE_ALL_EXCEPT);volatile bool old=std::isfinite(t.ground)&&std::isfinite(t.day)&&std::isfinite(t.nightExtra)&&std::isfinite(t.height);int before=std::fetestexcept(FE_ALL_EXCEPT);std::feclearexcept(FE_ALL_EXCEPT);volatile bool now=NorthlightRegionalFog::texelFinite(t);int after=std::fetestexcept(FE_ALL_EXCEPT);assert(old==now&&before==after);}
 for(unsigned mode=0;mode<4;++mode){OldFog::Field a;NorthlightRegionalFog::Field b;for(unsigned i=0;i<OldFog::N*OldFog::N;++i){float g=float(int(i%23)-10),h=mode==1&&(i%3)?0.f:5.f;if((mode==2&&i%251==0)||mode==3){uint32_t bits=(i&1)?0x7f800001u:0x7fc00001u;std::memcpy(&g,&bits,4);}a.texels[i]={g,.02f,.01f,h};b.texels[i]={g,.02f,.01f,h};}
  for(unsigned y=0;y<OldFog::N;++y)for(unsigned x=0;x<OldFog::N;++x){auto old=OldFog::heightBounds(a,x,y),now=NorthlightRegionalFog::heightBounds(b,x,y);assert(!std::memcmp(old.data(),now.data(),sizeof old));}
  const auto old=OldFog::globalHeightBounds(a),now=NorthlightRegionalFog::globalHeightBounds(b);assert(!std::memcmp(old.data(),now.data(),sizeof old));}
}
