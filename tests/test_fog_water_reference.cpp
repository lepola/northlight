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
}
