#define NorthlightRegionalFog FogOld
#include "fog_reference/regional-old.h"
#undef NorthlightRegionalFog
#include "regional_fog.h"
#include <cassert>
#include <limits>
#include <random>
int main(){std::mt19937 rng(9421);NorthlightGI::WorldMaterial m;
 for(unsigned size:{1u,2u,23u,64u})for(unsigned uMode:{1u,2u,3u})for(unsigned vMode:{1u,2u,3u}){
  m.width=size;m.height=size+1;m.addressU=uMode;m.addressV=vMode;m.rgba.resize(size_t(m.width)*m.height*4);for(auto& v:m.rgba)v=uint8_t(rng());
  for(float cutoff:{0.f,.01f,.5f,1.f,std::numeric_limits<float>::quiet_NaN()}){m.alphaCutoff=cutoff;
   for(unsigned i=0;i<4000;++i){float u=float(int(rng()%100000)-50000)/997.f,v=float(int(rng()%100000)-50000)/1003.f;assert(FogOld::cityOpaqueAt(m,u,v)==NorthlightRegionalFog::cityOpaqueAt(m,u,v));}
   for(float u:{-1.f,-0.f,0.f,1.f,2.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})for(float v:{-.5f,0.f,1.f,std::numeric_limits<float>::quiet_NaN()})assert(FogOld::cityOpaqueAt(m,u,v)==NorthlightRegionalFog::cityOpaqueAt(m,u,v));}}
 for(unsigned width:{1u,2u,255u,65535u,65536u}){m.width=width;m.height=1;m.rgba.resize(size_t(width)*4);for(auto& v:m.rgba)v=uint8_t(rng());m.alphaCutoff=.5f;
  for(unsigned mode:{1u,2u,3u}){m.addressU=m.addressV=mode;for(float u:{-std::numeric_limits<float>::max(),-1.f,-0.f,0.f,1.f,std::numeric_limits<float>::max()})for(float v:{-.5f,0.f,1.f})assert(FogOld::cityOpaqueAt(m,u,v)==NorthlightRegionalFog::cityOpaqueAt(m,u,v));
   for(unsigned k=0;k<2000;++k){float u=float(rng()%width)/float(width),v=float(rng()%1000)/1000.f;for(float edge:{std::nextafter(u,-INFINITY),u,std::nextafter(u,INFINITY)})assert(FogOld::cityOpaqueAt(m,edge,v)==NorthlightRegionalFog::cityOpaqueAt(m,edge,v));}}}
 m.width=37;m.height=23;m.rgba.resize(size_t(m.width)*m.height*4);for(auto& v:m.rgba)v=uint8_t(rng());m.alphaCutoff=.5f;
 for(unsigned mode:{1u,2u,3u}){m.addressU=m.addressV=mode;for(unsigned i=0;i<100000;++i){uint32_t a=rng(),b=rng();float u,v;std::memcpy(&u,&a,4);std::memcpy(&v,&b,4);if(std::isfinite(u)&&std::isfinite(v))assert(FogOld::cityOpaqueAt(m,u,v)==NorthlightRegionalFog::cityOpaqueAt(m,u,v));}}
 m.rgba.clear();assert(FogOld::cityOpaqueAt(m,0,0)==NorthlightRegionalFog::cityOpaqueAt(m,0,0));m.width=2;m.height=2;m.rgba.resize(3);assert(FogOld::cityOpaqueAt(m,0,0)==NorthlightRegionalFog::cityOpaqueAt(m,0,0));
 FogOld::Region old;NorthlightRegionalFog::Region now;for(int y=31;y<=33;++y)for(int x=31;x<=33;++x){FogOld::Tile a;NorthlightRegionalFog::Tile b;a.x=b.x=x;a.y=b.y=y;a.zones.fill(1519);b.zones=a.zones;old.tiles.push_back(a);now.tiles.push_back(b);}
 NorthlightGI::WorldScene scene;m.width=m.height=16;m.rgba.resize(16*16*4);for(auto& v:m.rgba)v=uint8_t(rng());m.wmo=true;m.terrain=false;m.alphaCutoff=.5f;scene.materials.push_back(m);
 scene.vertices={{{-400,-400,100},{0,0,1},0,0},{{400,-400,100},{0,0,1},3,0},{{400,400,100},{0,0,1},3,3},{{-400,400,100},{0,0,1},0,3}};scene.triangles={{0,1,2,0},{0,2,3,0}};
 for(unsigned mode:{1u,2u,3u}){scene.materials[0].addressU=scene.materials[0].addressV=mode;for(int x=-32;x<=32;x+=16)for(int y=-32;y<=32;y+=16){auto a=FogOld::buildField(scene,old,float(x),float(y));auto b=NorthlightRegionalFog::buildField(scene,now,float(x),float(y));assert(!std::memcmp(a.texels.data(),b.texels.data(),sizeof a.texels));assert(!std::memcmp(a.lush.data(),b.lush.data(),sizeof a.lush));assert(a.groundCells==b.groundCells&&a.citySurfaceCells==b.citySurfaceCells&&a.fogCells==b.fogCells&&a.airCells==b.airCells&&a.indoorCells==b.indoorCells);assert(a.citySurfaceCells>0);}}
}
