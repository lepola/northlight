#define NorthlightRegionalFog ReferenceFog
#include "fog_reference/regional-old.h"
#undef NorthlightRegionalFog
#include "regional_fog.h"
#include <cassert>
#include <cstring>
#include <random>
int main(){
 std::mt19937 rng(20261027);ReferenceFog::Region old;NorthlightRegionalFog::Region now;
 ReferenceFog::Tile a;NorthlightRegionalFog::Tile b;
 for(unsigned i=0;i<96;++i){ReferenceFog::Water x;NorthlightRegionalFog::Water y;
  x.x=y.x=float(int(rng()%600)-300);x.y=y.y=float(int(rng()%600)-300);x.height=y.height=float(int(rng()%60)-30);
  x.mask=y.mask=i%4==0?0:i%4==1?1ull<<(rng()%64):((uint64_t(rng())<<32)|rng());a.water.push_back(x);b.water.push_back(y);}
 old.tiles.push_back(a);now.tiles.push_back(b);
 for(unsigned i=0;i<100000;++i){float x=float(int(rng()%900)-450),y=float(int(rng()%900)-450),ground=float(int(rng()%80)-40),u,v;
  float first=ReferenceFog::wetness(old,x,y,ground,u),second=NorthlightRegionalFog::wetness(now,x,y,ground,v);
  assert(!std::memcmp(&first,&second,sizeof first)&&!std::memcmp(&u,&v,sizeof u));}
 for(unsigned bit=0;bit<=64;++bit){ReferenceFog::Region a;NorthlightRegionalFog::Region b;ReferenceFog::Tile x;NorthlightRegionalFog::Tile y;ReferenceFog::Water p{};NorthlightRegionalFog::Water q{};
  p.x=q.x=60;p.y=q.y=60;p.height=q.height=5;p.mask=q.mask=bit==64?~uint64_t(0):uint64_t(1)<<bit;x.water.push_back(p);y.water.push_back(q);a.tiles.push_back(x);b.tiles.push_back(y);
  for(unsigned i=0;i<64;++i){float u,v,px=60.f-float(i%8)*5.f,py=60.f-float(i/8)*5.f;float first=ReferenceFog::wetness(a,px,py,5,u),second=NorthlightRegionalFog::wetness(b,px,py,5,v);assert(!std::memcmp(&first,&second,4)&&!std::memcmp(&u,&v,4));}}
}
