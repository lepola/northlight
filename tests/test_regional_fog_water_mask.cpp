#include "regional_fog.h"
#include <cassert>
#include <cstring>
#include <random>
using namespace NorthlightRegionalFog;
static float reference(const Region& region,float x,float y,float ground,float& surface){
 float best=24.f*24.f;surface=ground;
 for(const auto& tile:region.tiles)for(const auto& w:tile.water){
  if(std::fabs(ground-w.height)>8)continue;
  float dx=std::max({float(w.x-ChunkSize)-x,0.f,x-w.x}),dy=std::max({float(w.y-ChunkSize)-y,0.f,y-w.y});if(dx*dx+dy*dy>=best)continue;
  for(unsigned row=0;row<8;++row)for(unsigned col=0;col<8;++col)if(w.mask&(uint64_t(1)<<(row*8+col))){
   float highX=float(w.x-row*Unit),lowX=float(w.x-(row+1)*Unit),highY=float(w.y-col*Unit),lowY=float(w.y-(col+1)*Unit);
   float a=std::max({lowX-x,0.f,x-highX}),b=std::max({lowY-y,0.f,y-highY});float distance=a*a+b*b;
   if(distance<best){best=distance;if(distance==0)surface=std::max(ground,w.height);}
  }
 }
 return 1-smooth(std::sqrt(best)/24.f);
}
int main(){std::mt19937_64 rng(78235);Region r;r.tiles.resize(3);for(auto& tile:r.tiles)for(unsigned i=0;i<70;++i){Water w;w.x=float(int(rng()%256)-128);w.y=float(int(rng()%256)-128);w.height=float(int(rng()%20)-10);w.mask=i%4==0?0:i%4==1?~uint64_t(0):i%4==2?uint64_t(1)<<(rng()%64):rng();tile.water.push_back(w);}for(int i=0;i<10000;++i){float x=float(int(rng()%256)-128),y=float(int(rng()%256)-128),ground=float(int(rng()%20)-10),a,b;float old=reference(r,x,y,ground,a),now=wetness(r,x,y,ground,b);assert(!std::memcmp(&old,&now,4)&&!std::memcmp(&a,&b,4));}}
