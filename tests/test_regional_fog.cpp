#include "regional_fog.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
using namespace NorthlightRegionalFog;
static Region region(float x,float y,uint32_t zone){Region r;int tx=int(std::floor((WorldZero-y)/TileSize)),ty=int(std::floor((WorldZero-x)/TileSize));for(int yy=ty-1;yy<=ty+1;++yy)for(int xx=tx-1;xx<=tx+1;++xx){Tile t;t.x=xx;t.y=yy;t.zones.fill(zone);r.tiles.push_back(std::move(t));}return r;}
static NorthlightGI::WorldScene plane(float cx,float cy,bool terrain=true){NorthlightGI::WorldScene s;NorthlightGI::WorldMaterial m;m.terrain=terrain;s.materials.push_back(m);
 for(auto p:std::array<std::array<float,2>,4>{{{{-400,-400}},{{400,-400}},{{400,400}},{{-400,400}}}})s.vertices.push_back({{cx+p[0],cy+p[1],100+p[0]*.02f},{0,0,1},0,0});
 s.triangles.push_back({0,1,2,0});s.triangles.push_back({0,2,3,0});return s;}
int main(int argc,char** argv){
 // 0.3.199 (fog clouds): lush zones (forests, grass, Duskwood, STV, Mulgore, Stormwind) get dry-weather fog banks; deserts and Orgrimmar do not
 for(uint32_t z:{10u,33u,215u,1519u,12u,40u,44u,45u,267u,38u,28u,139u,36u,3518u,3537u,394u})assert(lushZone(z));
 for(uint32_t z:{14u,440u,17u,3u,4u,1637u,0u,1377u,405u,400u})assert(!lushZone(z));
 for(uint32_t z:{40u,3518u})assert(!forestZone(z)&&grassZone(z)&&policy(z,100,1,1,false).height==policy(14,100,1,1,false).height); /* grass: the general outdoor fog, unchanged */

 unsigned cases=0;
 for(unsigned i=0;i<10000;++i){float time=float(i)/10000;float night=nightFactor(time);assert(night>=0&&night<=1);auto d=policy(440,100,1,1,false);assert(d.day+night*d.nightExtra==0);++cases;}
 assert(nightFactor(0)==1&&nightFactor(.5f)==0&&nightFactor(7.f/24)>.49f&&nightFactor(7.f/24)<.51f);
 assert(nightFactor(-1)==0&&nightFactor(NAN)==0);assert(std::fabs(nightFactor(.999999f)-nightFactor(0))<1e-6);
 assert(policy(33,100,0,0,false).day==0);assert(policy(33,100,1,0,false).day>0);assert(policy(33,100,0,1,false).day>0);
 // Every catalog forest has nighttime coverage away from water, stronger low
 // and waterside pockets, no daytime ground fog, and no indoor spill.
 const unsigned forests[]={
#define FOREST_REGION(id,name) id,
#include "forest_regions.inc"
#undef FOREST_REGION
 };
 for(unsigned zone:forests){auto dry=policy(zone,100,0,0,false),wet=policy(zone,100,1,0,false),basin=policy(zone,100,0,1,false),inside=policy(zone,100,1,1,true);
   assert(dry.day==0&&wet.day==0&&basin.day==0&&dry.height==1.25f);
   assert(dry.nightExtra>0&&wet.nightExtra>dry.nightExtra*5&&basin.nightExtra>dry.nightExtra*3);
   for(float w:{0.f,.5f,1.f})for(float b:{0.f,.5f,1.f})
     assert(std::fabs(policy(zone,100,w,b,false).nightExtra-1.44f*(.003f+.020f*w+.012f*b))<1e-7f);
   assert(dry.day+nightFactor(.5f)*dry.nightExtra==0&&dry.day+nightFactor(0)*dry.nightExtra>0);
   assert(inside.day==0&&inside.nightExtra==0&&inside.height==0);++cases;
 }
 assert(policy(33,100,1,0,false).nightExtra>policy(33,100,0,0,false).nightExtra*5);
 assert(policy(9999,100,1,1,false).day==0&&policy(10,100,1,1,true).day==0);
 float x=-10480,y=240;auto r=region(x,y,10);auto scene=plane(x,y);auto a=buildField(scene,r,x,y),b=buildField(scene,r,x+8,y-8);
 assert(a.groundCells==N*N&&a.fogCells>3000&&a.airCells>3000);
 auto dryForest=buildField(scene,region(x,y,33),x,y);assert(dryForest.fogCells>3000&&dryForest.airCells>3000);
 for(const auto& cell:dryForest.texels)assert(cell.day==0);
 auto elwynn=buildField(scene,region(x,y,12),x,y);assert(elwynn.fogCells>3000&&elwynn.airCells>3000);
 for(const auto& cell:elwynn.texels)assert(cell.day==0);
 assert(policy(0,100,1,1,false).height==0);
 // Mulgore is a lighter continuous air layer, with no added ground blanket.
 // Indoor exclusion and camera-independent field overlap still apply.
 auto mulgore=buildField(scene,region(x,y,215),x,y);
 auto mulgoreMoved=buildField(scene,region(x,y,215),x+8,y-8);
 assert(mulgore.fogCells==0&&mulgore.airCells>3000);
 for(const auto& cell:mulgore.texels)assert(cell.height==1&&cell.day==0&&cell.nightExtra==0);
 assert(policy(215,100,1,1,true).height==0);
 for(unsigned yy=4;yy<N-5;++yy)for(unsigned xx=4;xx<N-5;++xx)
   assert(std::memcmp(&mulgore.texels[yy*N+xx],&mulgoreMoved.texels[(yy+1)*N+xx-1],sizeof(Texel))==0);
 auto stormwind=buildField(scene,region(x,y,1519),x,y);
 assert(stormwind.fogCells==0&&stormwind.airCells>3000);
 for(const auto& cell:stormwind.texels)assert(cell.height==1.15625f&&cell.day==0&&cell.nightExtra==0);
 assert(policy(1519,100,1,1,true).height==0);
 auto orgrimmar=buildField(scene,region(x,y,1637),x,y);
 for(unsigned i=0;i<N*N;++i)assert(std::memcmp(&orgrimmar.texels[i],&stormwind.texels[i],sizeof(Texel))==0);
 assert(policy(1637,100,1,1,true).height==0);
 for(unsigned zone:{440u,14u,4197u}){
   assert(policy(zone,100,0,0,false).height==.9375f);
   assert(policy(zone,100,1,1,true).height==0);
 }
 assert(buildField(scene,Region{},x,y).airCells==0);
 auto desert=buildField(scene,region(x,y,440),x,y);assert(desert.fogCells==0&&desert.airCells>3000);
 for(unsigned yy=4;yy<N-5;++yy)for(unsigned xx=4;xx<N-5;++xx){const auto& aa=a.texels[yy*N+xx];const auto& bb=b.texels[(yy+1)*N+xx-1];
   assert(std::fabs(aa.ground-bb.ground)<.00001f&&aa.day==bb.day&&aa.nightExtra==bb.nightExtra&&aa.height==bb.height);++cases;}
 assert(a.texels[0].day==0&&a.texels[N*N-1].day==0);assert(a.texels[32*N+32].height==5);
 auto tree=plane(x,y,false);assert(buildField(tree,r,x,y).fogCells==0); // never uses foliage/roofs as ground
 // A nonterrain roof cannot change ground samples or exclude outdoor fog.
 unsigned base=unsigned(scene.vertices.size());scene.materials.push_back({});for(auto v:tree.vertices){v.position.z+=50;scene.vertices.push_back(v);}
 scene.triangles.push_back({base,base+1,base+2,1});scene.triangles.push_back({base,base+2,base+3,1});auto c=buildField(scene,r,x,y);
 for(unsigned i=0;i<N*N;++i)assert(std::memcmp(&a.texels[i],&c.texels[i],sizeof(Texel))==0);
 Indoor indoor;indoor.low[0]=x-4;indoor.low[1]=y-4;indoor.low[2]=100;indoor.high[0]=x+4;indoor.high[1]=y+4;indoor.high[2]=110;r.tiles[0].indoors.push_back(indoor);
 auto inside=buildField(scene,r,x,y);assert(inside.texels[32*N+32].height==0&&inside.texels[32*N+32].day==0&&inside.indoorCells>=9&&inside.texels[32*N+36].day>0);
 // An underground room cannot remove an outdoor field above its ceiling.
 auto basement=r;basement.tiles[0].indoors[0].low[2]=-70;basement.tiles[0].indoors[0].high[2]=20;
 auto above=buildField(scene,basement,x,y);assert(above.indoorCells==0);
 for(unsigned i=0;i<N*N;++i)assert(std::memcmp(&a.texels[i],&above.texels[i],sizeof(Texel))==0);
 assert(indoorColumn(basement,x,y,21)&&!indoorColumn(basement,x,y,23));
 // City indoor group bounds enclose open streets. Use actual WMO surfaces:
 // road top for open air, roof top for air above rooms, never M2 foliage.
 for(unsigned zone:{1519u,1637u}){
   auto cityRegion=region(x,y,zone);Indoor broad;
   broad.low[0]=x-300;broad.low[1]=y-300;broad.low[2]=70;
   broad.high[0]=x+300;broad.high[1]=y+300;broad.high[2]=180;
   cityRegion.tiles[0].indoors.push_back(broad);
   auto terrain=plane(x,y);for(auto& v:terrain.vertices)v.position.z=60;
   assert(buildField(terrain,cityRegion,x,y).texels[32*N+32].height==0); // fallback without WMO
   auto cityScene=plane(x,y,false);cityScene.materials[0].wmo=true;
   for(auto& v:cityScene.vertices)v.position.z=100;
   // No underlying ADT needed: streets may cover genuine terrain holes.
   auto open=buildField(cityScene,cityRegion,x,y);
   assert(open.texels[32*N+32].ground==100&&open.texels[32*N+32].height>0&&open.citySurfaceCells==N*N);
   assert(open.indoorCells==0);
   auto addRoof=[&](float height,bool wmo){
     uint32_t first=uint32_t(cityScene.vertices.size()),mat=uint32_t(cityScene.materials.size());
     cityScene.materials.push_back({});cityScene.materials.back().wmo=wmo;
     for(auto p:std::array<std::array<float,2>,4>{{{{-32,-32}},{{32,-32}},{{32,32}},{{-32,32}}}})
       cityScene.vertices.push_back({{x+p[0],y+p[1],height},{0,0,1},.5f,.5f});
     cityScene.triangles.push_back({first,first+1,first+2,mat});cityScene.triangles.push_back({first,first+2,first+3,mat});
   };
   addRoof(200,false);auto foliage=buildField(cityScene,cityRegion,x,y);
   assert(foliage.texels[32*N+32].ground==100); // M2 tree/actor ignored
   addRoof(130,true);auto roof=buildField(cityScene,cityRegion,x,y);
   assert(roof.texels[32*N+32].ground==130); // indoor sample z=110 lies below fog
   assert(roof.texels[32*N+40].ground==100); // neighbouring street remains open
   auto moved=buildField(cityScene,cityRegion,x+8,y-8);
   for(unsigned yy=4;yy<N-5;++yy)for(unsigned xx=4;xx<N-5;++xx)
     assert(std::memcmp(&roof.texels[yy*N+xx],&moved.texels[(yy+1)*N+xx-1],sizeof(Texel))==0);
   auto& mask=cityScene.materials.back();mask.width=mask.height=1;mask.rgba={255,255,255,0};
   assert(buildField(cityScene,cityRegion,x,y).texels[32*N+32].ground==100); // cutout
   mask.rgba[3]=255;assert(buildField(cityScene,cityRegion,x,y).texels[32*N+32].ground==130);
   assert(buildField(cityScene,region(x,y,12),x,y).groundCells==0); // forest still terrain-only
 }
 Region wet=region(x,y,33);Water water;water.x=x;water.y=y;water.height=100;water.mask=1;wet.tiles[0].water.push_back(water);
 float surface;assert(wetness(wet,x-1,y-1,98,surface)==1&&surface==100);assert(wetness(wet,x-100,y-100,98,surface)==0);assert(wetness(wet,x-1,y-1,50,surface)==0);
 // Mask hole cannot fabricate water; only bit0 is present.
 assert(wetness(wet,x-31,y-31,100,surface)==0);assert(buildField(scene,region(x,y,440),x,y).fogCells==0);
 unsigned actual=0,actualIndoors=0;double elapsed=0;
 if(argc>1){auto t=std::chrono::steady_clock::now();auto real=loadRegion(argv[1],"Azeroth",x,y);assert(!real.tiles.empty());assert(zoneAt(real,x,y)==10);for(auto& tile:real.tiles){actual+=tile.water.size();actualIndoors+=tile.indoors.size();}auto f=buildField(scene,real,x,y);assert(f.groundCells==N*N);
   auto goldshire=loadRegion(argv[1],"Azeroth",-9465,64);assert(zoneAt(goldshire,-9465,64)==12);unsigned interiors=0;for(const auto& tile:goldshire.tiles)interiors+=tile.indoors.size();assert(interiors>0);
   auto undercity=loadRegion(argv[1],"Azeroth",1580,239);
   assert(zoneAt(undercity,1580,239)==85);
   assert(indoorColumn(undercity,1580,239,0));
   assert(!indoorColumn(undercity,1580,239,80)); // above every authored roof here
   auto e=buildField(plane(-9465,64),goldshire,-9465,64);assert(e.fogCells>0);for(const auto& cell:e.texels)assert(cell.day==0);
   struct Fixture {const char* map;float x,y;unsigned zone;};
   // Coordinates are centers of authored supported MCNK areas, not guessed
   // map-wide classifications. These also exercise new map allowlist paths.
   for(const auto& fixture:std::array<Fixture,3>{{{"Expansion01",-1050,5916.6665f,3519},{"Northrend",4416.6665f,-1550,394},{"Azeroth",-11216.6665f,4250,33}}}){
      auto area=loadRegion(argv[1],fixture.map,fixture.x,fixture.y);assert(!area.tiles.empty());assert(zoneAt(area,fixture.x,fixture.y)==fixture.zone);++cases;
   }
   elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();assert(loadRegion(argv[1],"../Azeroth",x,y).tiles.empty());}
 std::printf("{\"status\":\"pass\",\"cases\":%u,\"actualWaterLayers\":%u,\"actualIndoorGroups\":%u,\"regionLoadAnd4096SamplesMs\":%.3f,\"cameraGridOverlapIdentical\":true,\"tanarisGroundZero\":true}\n",cases,actual,actualIndoors,elapsed);
}
