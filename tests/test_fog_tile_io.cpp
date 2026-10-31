// Real production loader, with valid and malformed on-disk FRF records.
#include "regional_fog.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <new>
#include <string>
static bool countAllocations=false;static size_t allocatedBytes=0;
void* operator new(size_t n){if(countAllocations)allocatedBytes+=n;if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,size_t) noexcept{std::free(p);}
using namespace NorthlightRegionalFog;
static void fixture(const std::string& path,unsigned n,unsigned mode){
 FILE* f=std::fopen(path.c_str(),"wb");assert(f);const uint32_t header[6]={0x31465246u,mode==4?99u:1u,3,5,1,n};assert(std::fwrite(header,4,6,f)==6);
 uint32_t zones[256];for(unsigned i=0;i<256;++i)zones[i]=i;assert(std::fwrite(zones,4,256,f)==256);
 const float water[3]={10,20,30};const uint64_t mask=0x1248;assert(std::fwrite(water,4,3,f)==3&&std::fwrite(&mask,8,1,f)==1);
 for(unsigned i=0;i<n;++i){float box[6]={float(i%100),0,0,float(i%100)+1,1,1};if(i==0&&mode==1)box[0]=std::numeric_limits<float>::quiet_NaN();if(i==0&&mode==2)box[0]=100001;if(i==0&&mode==3)box[0]=10;
  assert(std::fwrite(box,4,mode==5&&i+1==n?5:6,f)==(mode==5&&i+1==n?5u:6u));}
 assert(!std::fclose(f));
}
static void waterFixture(const std::string& path,unsigned n,unsigned mode){
 FILE* f=std::fopen(path.c_str(),"wb");assert(f);const uint32_t header[6]={0x31465246u,1,3,5,n,0};uint32_t zones[256]={};assert(std::fwrite(header,4,6,f)==6&&std::fwrite(zones,4,256,f)==256);
 for(unsigned i=0;i<n;++i){float values[3]={float(i),float(i%13),float(i%17)};if(i==0&&mode==1)values[0]=std::numeric_limits<float>::quiet_NaN();if(i+1==n&&mode==2)values[2]=100001;const uint64_t mask=uint64_t(i)*12345;assert(std::fwrite(values,4,3,f)==3);if(mode!=3||i+1!=n)assert(std::fwrite(&mask,8,1,f)==1);}
 assert(!std::fclose(f));
}
static void emptyTile(const std::string& path,int x,int y){FILE* f=std::fopen(path.c_str(),"wb");assert(f);const uint32_t header[6]={0x31465246u,1,unsigned(x),unsigned(y),0,0};uint32_t zones[256]={};assert(std::fwrite(header,4,6,f)==6&&std::fwrite(zones,4,256,f)==256&&!std::fclose(f));}
int main(int argc,char** argv){assert(argc==2);const std::string path=argv[1];
 for(unsigned n:{0u,1u,512u,8192u}){fixture(path,n,0);Tile t;assert(loadTile(path,3,5,t));assert(t.x==3&&t.y==5&&t.indoors.size()==n&&t.water.size()==1&&t.water[0].mask==0x1248);for(unsigned i=0;i<256;++i)assert(t.zones[i]==i);for(unsigned i=0;i<n;++i){assert(t.indoors[i].low[0]==float(i%100)&&t.indoors[i].high[0]==float(i%100)+1&&t.indoors[i].low[1]==0&&t.indoors[i].high[2]==1);}}
 for(unsigned mode=1;mode<=5;++mode){fixture(path,2,mode);Tile t;t.x=77;t.zones[0]=123;assert(!loadTile(path,3,5,t));assert(t.x==77&&t.zones[0]==123&&t.indoors.empty());}
 for(unsigned n:{0u,1u,512u,4096u}){waterFixture(path,n,0);Tile t;assert(loadTile(path,3,5,t)&&t.water.size()==n);for(unsigned i=0;i<n;++i){const auto& w=t.water[i];assert(w.x==float(i)&&w.y==float(i%13)&&w.height==float(i%17)&&w.mask==uint64_t(i)*12345);}}
 for(unsigned mode=1;mode<=3;++mode){waterFixture(path,20,mode);Tile t;t.x=77;t.zones[0]=123;assert(!loadTile(path,3,5,t)&&t.x==77&&t.zones[0]==123&&t.water.empty());}
 {fixture(path,8192,0);FILE* f=std::fopen(path.c_str(),"r+b");assert(f&&std::fseek(f,1048,SEEK_SET)==0);const float nan=std::numeric_limits<float>::quiet_NaN();assert(std::fwrite(&nan,4,1,f)==1&&!std::fclose(f));Tile t;t.x=77;allocatedBytes=0;countAllocations=true;const bool ok=loadTile(path,3,5,t);countAllocations=false;assert(!ok&&t.x==77&&t.indoors.empty()&&allocatedBytes<=sizeof(Water));}
 {const std::string folder=path+".region",map=folder+"/Azeroth";std::filesystem::create_directories(map);for(unsigned count=0;count<=9;++count){for(unsigned i=0;i<count;++i){const int x=31+int(i%3),y=31+int(i/3);emptyTile(map+"/"+std::to_string(x)+"_"+std::to_string(y)+".frf",x,y);}const auto r=loadRegion(folder,"Azeroth",0,0);assert(r.tiles.size()==count&&r.missing==9-count);if(count==9){std::vector<Tile> baseline;for(unsigned i=0;i<9;++i)baseline.emplace_back();assert(r.tiles.capacity()<=baseline.capacity());}for(unsigned i=0;i<count;++i){const int x=31+int(i%3),y=31+int(i/3);assert(r.tiles[i].x==x&&r.tiles[i].y==y);assert(!std::remove((map+"/"+std::to_string(x)+"_"+std::to_string(y)+".frf").c_str()));}}assert(std::filesystem::remove(map)&&std::filesystem::remove(folder));}
 fixture(path,1,0);Tile t;t.x=77;assert(!loadTile(path,4,5,t)&&t.x==77);assert(!std::remove(path.c_str()));std::puts("PASS fog tile records and malformed transactional controls");
}
