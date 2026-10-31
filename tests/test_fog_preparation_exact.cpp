#include "fog_clouds.h"
#include "regional_fog.h"
#define NorthlightFogClouds ReferenceFogClouds
#include "fixtures/fog-preparation-reference/fog_clouds.h"
#undef NorthlightFogClouds
#define NorthlightRegionalFog ReferenceRegionalFog
#include "fixtures/fog-preparation-reference/regional_fog.h"
#undef NorthlightRegionalFog
#include <cassert>
#include <cstdio>
#include <cstring>
#include <chrono>
int main(){
 using Clock=std::chrono::steady_clock;
 double oldMs=0,newMs=0;
 for(uint32_t seed:{NorthlightFogClouds::Seed,0u,1u,0xdeadbeefu}){
  auto t=Clock::now();auto a=ReferenceFogClouds::generate(seed);oldMs+=std::chrono::duration<double,std::milli>(Clock::now()-t).count();
  t=Clock::now();auto b=NorthlightFogClouds::generate(seed);newMs+=std::chrono::duration<double,std::milli>(Clock::now()-t).count();assert(a==b);
 }
 unsigned cells=0;
 for(float cx:{-12345.f,0.f,5248.f})for(unsigned zone:{0u,12u,1519u,2817u}){
  namespace N=NorthlightRegionalFog;namespace R=ReferenceRegionalFog;
  NorthlightGI::WorldScene s;NorthlightGI::WorldMaterial m;m.terrain=true;s.materials.push_back(m);
  for(auto p:std::array<std::array<float,2>,4>{{{{-400,-400}},{{400,-400}},{{400,400}},{{-400,400}}}})s.vertices.push_back({{cx+p[0],p[1],100+p[0]*.02f},{0,0,1},0,0});s.triangles.push_back({0,1,2,0});s.triangles.push_back({0,2,3,0});
  N::Region n;R::Region r;int tx=int(std::floor(N::WorldZero/N::TileSize)),ty=int(std::floor((N::WorldZero-cx)/N::TileSize));
  for(int y=ty-1;y<=ty+1;++y)for(int x=tx-1;x<=tx+1;++x){N::Tile a;a.x=x;a.y=y;a.zones.fill(zone);n.tiles.push_back(a);R::Tile b;b.x=x;b.y=y;b.zones.fill(zone);r.tiles.push_back(b);}
  auto a=N::buildField(s,n,cx,0);auto b=R::buildField(s,r,cx,0);assert(a.originX==b.originX&&a.originY==b.originY&&a.lush==b.lush);
  for(size_t i=0;i<a.texels.size();++i){assert(!std::memcmp(&a.texels[i],&b.texels[i],sizeof(a.texels[i])));++cells;}
  assert(a.groundCells==b.groundCells&&a.fogCells==b.fogCells&&a.airCells==b.airCells&&a.indoorCells==b.indoorCells&&a.citySurfaceCells==b.citySurfaceCells);
 }
 std::printf("PASS noiseBytes=1048576 fieldCells=%u originalNoiseMs=%.3f optimizedNoiseMs=%.3f ratio=%.3f\n",cells,oldMs,newMs,newMs/oldMs);
}
