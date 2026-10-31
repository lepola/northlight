#define NorthlightFogClouds OldFogClouds
#include "fog_reference/noise-old.h"
#undef NorthlightFogClouds
#include "fog_clouds.h"
#include <cassert>
#include <cstring>
#include <vector>
int main(){using namespace NorthlightFogClouds;constexpr uint32_t seed=42;std::vector<uint8_t> table(N*N*N);
 for(unsigned o=0,P=4;o<3;++o,P*=2)for(unsigned z=0;z<P;++z)for(unsigned y=0;y<P;++y)for(unsigned x=0;x<P;++x)table[(o==0?0:o==1?64:576)+x+P*(y+P*z)]=uint8_t(detail::hash(x,y,z,seed+o*0x9e37u)%12);
 detail::preparePerlinCubes(table.data());
 for(unsigned o=0,P=4;o<3;++o,P*=2)for(unsigned z=0;z<P;++z)for(unsigned y=0;y<P;++y)for(unsigned x=0;x<P;++x){
  const unsigned base=o==0?0:o==1?64:576,cell=base+x+P*(y+P*z);
  for(unsigned k=0;k<8;++k)assert(table[19072+8*cell+k]==OldFogClouds::detail::hash((x+(k&1))%P,(y+((k>>1)&1))%P,(z+((k>>2)&1))%P,seed+o*0x9e37u)%12);
 }
 for(unsigned o=0,P=4;o<3;++o,P*=2){detail::PerlinAxis axis[N];for(unsigned i=0;i<N;++i){axis[i]=detail::perlinAxis(float(i),float(P)/N);const unsigned scaled=i*P;const float fraction=float(scaled%N)*(1.f/N);const detail::PerlinAxis fast={int(scaled/N),fraction,detail::fade(fraction)};assert(!std::memcmp(&axis[i],&fast,sizeof fast));}
  for(unsigned z=0;z<N;++z)for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){const float a=OldFogClouds::detail::perlin(P,seed,o,float(x),float(y),float(z));const float b=detail::perlinPrepared<true>(P,seed,o,axis[x],axis[y],axis[z],table.data()+19072);assert(!std::memcmp(&a,&b,4));}}
}
