#include "world_gi.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <random>
#include <thread>
using namespace NorthlightGI;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
static double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
static void u32(FILE* f,uint32_t v){uint8_t b[4]={uint8_t(v),uint8_t(v>>8),uint8_t(v>>16),uint8_t(v>>24)};assert(fwrite(b,1,4,f)==4);}
static void f32(FILE* f,float v){uint32_t b;memcpy(&b,&v,4);u32(f,b);}
static void vec(FILE* f,Vec3 v){f32(f,v.x);f32(f,v.y);f32(f,v.z);}
static bool exact(Vec3 a,Vec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
static void sceneEqual(const WorldScene& a,const WorldScene& b){
    assert(a.vertices.size()==b.vertices.size()&&a.triangles.size()==b.triangles.size()&&a.materials.size()==b.materials.size()&&a.completePlacements.size()==b.completePlacements.size());
    for(size_t i=0;i<a.vertices.size();++i){const auto& x=a.vertices[i];const auto& y=b.vertices[i];assert(exact(x.position,y.position)&&exact(x.normal,y.normal)&&x.u==y.u&&x.v==y.v);}
    for(size_t i=0;i<a.triangles.size();++i){const auto& x=a.triangles[i];const auto& y=b.triangles[i];assert(x.v0==y.v0&&x.v1==y.v1&&x.v2==y.v2&&x.material==y.material);}
    for(size_t i=0;i<a.materials.size();++i){const auto& x=a.materials[i];const auto& y=b.materials[i];assert(exact(x.albedo,y.albedo)&&x.width==y.width&&x.height==y.height&&x.alphaCutoff==y.alphaCutoff&&x.rgba==y.rgba&&x.addressU==y.addressU&&x.addressV==y.addressV&&x.terrain==y.terrain&&x.wmo==y.wmo);}
    for(size_t i=0;i<a.completePlacements.size();++i){const auto& x=a.completePlacements[i];const auto& y=b.completePlacements[i];assert(x.uid==y.uid&&x.category==y.category&&x.modelKey==y.modelKey&&exact(x.translation,y.translation)&&exact(x.low,y.low)&&exact(x.high,y.high));for(unsigned j=0;j<9;++j)assert(x.matrix[j]==y.matrix[j]);}
}
static void hitEqual(const RayHit& a,const RayHit& b){
    if(a.hit!=b.hit||(a.hit&&(a.triangle!=b.triangle||a.distance!=b.distance||!exact(a.normal,b.normal)||!exact(a.albedo,b.albedo)))){fprintf(stderr,"hit mismatch hit=%d/%d id=%u/%u distance=%.9g/%.9g\n",a.hit,b.hit,a.triangle,b.triangle,a.distance,b.distance);abort();}
    assert(a.hit==b.hit);if(!a.hit)return;assert(a.backFace==b.backFace&&exact(a.position,b.position)&&exact(a.geometricNormal,b.geometricNormal));
}
static void compareRays(const BVH& a,const BVH& b,Vec3 center,unsigned n){
    std::mt19937 random(917);std::uniform_real_distribution<float> d(-1,1);unsigned hits=0;auto start=Clock::now();
    for(unsigned i=0;i<n;++i){Vec3 origin=center+Vec3(d(random)*280,d(random)*280,d(random)*100+80);Vec3 direction=i%3?Vec3(d(random),d(random),d(random)):Vec3(0,0,-1);auto x=a.trace(origin,direction,0,700),y=b.trace(origin,direction,0,700);hitEqual(x,y);hits+=x.hit;assert(a.occluded(origin,direction,700,0)==b.occluded(origin,direction,700,0));}
    Lighting light;light.maxDistance=220;light.maxBounces=2;
    for(unsigned i=0;i<24;++i){Vec3 p=center+Vec3(d(random)*120,d(random)*120,10+d(random)*40);auto x=solveProbe(a,p,light,32,i+1),y=solveProbe(b,p,light,32,i+1);assert(x.valid==y.valid);for(Vec3 normal:{Vec3(0,0,1),Vec3(1,0,0),Vec3(0,-1,0)})assert(exact(evaluateIrradiance(x,normal),evaluateIrradiance(y,normal)));}
    double differential=ms(start);std::vector<std::pair<Vec3,Vec3>> queries;queries.reserve(n);for(unsigned i=0;i<n;++i)queries.push_back({center+Vec3(d(random)*280,d(random)*280,d(random)*100+80),Vec3(d(random),d(random),d(random))});
    auto throughput=[&](const BVH& world){volatile unsigned tally=0;auto t=Clock::now();for(unsigned repeat=0;repeat<3;++repeat)for(const auto& q:queries){tally+=world.trace(q.first,q.second,0,700).hit;tally+=world.occluded(q.first,q.second,700,0);}return ms(t);};
    double oldTrace=throughput(a),newTrace=throughput(b);
    std::vector<Probe> oldProbes,newProbes;Lighting bakeLight;bakeLight.sunDirection=normalized({.7f,.3f,.2f});bakeLight.sunIrradiance={3.14f,2.98f,2.67f};
    auto bake=[&](const BVH& world,std::vector<Probe>& probes){auto t=Clock::now();Vec3 origin={std::floor(center.x/8)*8-24,std::floor(center.y/8)*8-24,std::floor(center.z/8)*8-24};for(unsigned i=0;i<512;++i)probes.push_back(solveProbe(world,origin+Vec3(float(i%8)*8,float((i/8)%8)*8,float(i/64)*8),bakeLight,64,i+1));return ms(t);};
    double oldBake=bake(a,oldProbes),newBake=bake(b,newProbes);for(size_t i=0;i<oldProbes.size();++i){assert(oldProbes[i].valid==newProbes[i].valid);for(unsigned j=0;j<4;++j)assert(exact(oldProbes[i].sh[j],newProbes[i].sh[j]));for(unsigned j=0;j<6;++j)assert(oldProbes[i].moments[j].mean==newProbes[i].moments[j].mean&&oldProbes[i].moments[j].meanSquare==newProbes[i].moments[j].meanSquare);}

    printf(" rays=%u hits=%u differentialMs=%.1f trace3x=%.1f/%.1fms bake512x64=%.1f/%.1fms",n,hits,differential,oldTrace,newTrace,oldBake,newBake);
}
static std::vector<std::string> tiles(const fs::path& root,const std::string& map,Vec3 center){
    int tx=int(floor(32-center.y/(1600.f/3))),ty=int(floor(32-center.x/(1600.f/3)));std::vector<std::string> result;
    for(int y=ty-1;y<=ty+1;++y)for(int x=tx-1;x<=tx+1;++x){char name[80];snprintf(name,sizeof name,"%02d_%02d.fg3",x,y);auto file=root/map/name;if(fs::exists(file))result.push_back(file.string());}return result;
}
static void run(LocalSceneCache& cache,const std::vector<std::string>& files,const std::string& models,const std::string& map,Vec3 center,const char* label,unsigned rayCount){
    Vec3 low=center-Vec3(288,288,320),high=center+Vec3(288,288,320);std::string error;WorldScene scene;auto start=Clock::now();assert(loadInstancedScenes(files,models,low,high,scene,error));double load=ms(start);BVH old;start=Clock::now();assert(old.build(std::move(scene),error));double build=ms(start);
    BVH incremental;if(!cache.build(files,models,map,low,high,incremental,error)){fprintf(stderr,"cache failure: %s\n",error.c_str());abort();}sceneEqual(old.scene(),incremental.scene());assert(old.triangleCount()==incremental.triangleCount());const auto& s=cache.stats();
    printf("%s tris=%zu pieces=%zu reads=%llu reused=%llu/%llu reusedTris=%llu old=%.1f+%.1fms incremental=%.1fms(load%.1f piece%.1f flat%.1f bvh%.1f) retain=%.2fMiB flat=%.2f bvhReserve=%.2f peak=%.2f",label,old.triangleCount(),size_t(s.pieceBuilds+s.pieceReuses),(unsigned long long)s.modelReads,(unsigned long long)s.pieceReuses,(unsigned long long)(s.pieceReuses+s.pieceBuilds),(unsigned long long)s.reusedTriangles,load,build,s.totalMs,s.loadMs,s.pieceMs,s.assembleMs,s.bvhMs,s.retainedBytes/1048576.,s.flatBytes/1048576.,s.bvhBytes/1048576.,s.peakBytes/1048576.);compareRays(old,incremental,center,rayCount);puts("");fflush(stdout);
}
static void fixtures(const fs::path& root){
    fs::create_directories(root/"models");FILE* f=fopen((root/"models"/(std::string(64,'0')+".fgs")).string().c_str(),"wb");assert(f);fwrite("FGS2",1,4,f);u32(f,2);u32(f,8);u32(f,4);u32(f,2);
    for(int plane=0;plane<2;++plane)for(Vec3 p:{Vec3(-80,-60,0),Vec3(80,-60,0),Vec3(80,60,0),Vec3(-80,60,0)}){vec(f,p+Vec3(0,0,float(plane)*2));vec(f,{.2f,.3f,1});f32(f,p.x/37);f32(f,p.y/43);}
    for(int plane=0;plane<2;++plane)for(auto tri:{std::array<uint32_t,3>{0,1,2},std::array<uint32_t,3>{0,2,3}}){for(auto index:tri)u32(f,index+plane*4);u32(f,plane);}
    for(int m=0;m<2;++m){vec(f,m?Vec3(.1f,.8f,.2f):Vec3(.8f,.3f,.2f));u32(f,2);u32(f,2);f32(f,.5f);u32(f,16);const uint8_t bytes[16]={200,50,100,255,80,210,40,0,30,30,180,255,240,140,20,255};fwrite(bytes,1,16,f);}fclose(f);
    f=fopen((root/"tile.fg3").string().c_str(),"wb");assert(f);fwrite("FGS3",1,4,f);u32(f,3);u32(f,20);
    for(uint32_t i=0;i<20;++i){uint8_t hash[32]={};fwrite(hash,1,32,f);u32(f,i);u32(f,0);u32(f,i%4);Vec3 offset(float((i%5)*90)-180,float((i/5)*100)-150,float(i%2)*3);if(i==19)offset={0,0,0};vec(f,offset-Vec3(160,120,8));vec(f,offset+Vec3(160,120,8));const float matrix[9]={0,-1.2f,0,2,0,0,0,0,1};for(auto value:matrix)f32(f,value);vec(f,offset);}fclose(f);
}
int main(int argc,char** argv){
    auto root=fs::temp_directory_path()/("fr-local-cache-"+std::to_string(Clock::now().time_since_epoch().count()));fixtures(root);std::vector<std::string> files={(root/"tile.fg3").string(),(root/"tile.fg3").string()};LocalSceneCache cache;
    for(unsigned step=0;step<5;++step){char name[32];snprintf(name,sizeof name,"synthetic%u",step);run(cache,files,(root/"models").string(),"test",{float(step)*64,0,0},name,2000);if(step)assert(cache.stats().pieceReuses>0);}
    run(cache,files,(root/"models").string(),"different-map",{0,0,0},"map reset",200);assert(cache.stats().pieceReuses==0);
    BVH previous;std::string error;assert(cache.build(files,(root/"models").string(),"test",{-288,-288,-320},{288,288,320},previous,error));size_t count=previous.triangleCount();assert(!cache.build(files,(root/"models").string(),"test",{-100,-100,-100},{100,100,100},previous,error,[](uint64_t){return false;}));assert(previous.triangleCount()==count);
    LocalSceneCache bounded({1});run(bounded,files,(root/"models").string(),"test",{0,0,0},"zero-retention complete scene",1000);assert(bounded.stats().retainedBytes==0);
    // Published BVH reads remain thread safe after cache builds and eviction.
    BVH a;assert(cache.build(files,(root/"models").string(),"test",{-288,-288,-320},{288,288,320},a,error));std::vector<std::thread> readers;for(unsigned i=0;i<8;++i)readers.emplace_back([&]{for(unsigned k=0;k<1000;++k)a.trace({0,0,100},{0,0,-1},0,1000);});for(auto& thread:readers)thread.join();
    // Force eviction within a repeated-model group, then fail at different
    // allocations and retry the same crop. Materials-only reads must never
    // turn a subsequently evicted placement into an empty model.
    for(unsigned failAt:{3u,8u,15u,30u,60u}){LocalSceneCache pressured({2048});BVH world;unsigned calls=0;pressured.build(files,(root/"models").string(),"test",{-288,-288,-320},{288,288,320},world,error,[&](uint64_t){return ++calls!=failAt;});assert(pressured.stats().retainedBytes<=2048);assert(pressured.build(files,(root/"models").string(),"test",{-288,-288,-320},{288,288,320},world,error));WorldScene reference;assert(loadInstancedScenes(files,(root/"models").string(),{-288,-288,-320},{288,288,320},reference,error));sceneEqual(reference,world.scene());assert(pressured.build(files,(root/"models").string(),"test",{-288,-288,-320},{288,288,320},world,error));sceneEqual(reference,world.scene());}
    // Bulk decode retains scalar decoder validation and failure atomicity.
    auto modelPath=root/"models"/(std::string(64,'0')+".fgs");FILE* input=fopen(modelPath.string().c_str(),"rb");assert(input);fseek(input,0,SEEK_END);std::vector<uint8_t> original(size_t(ftell(input)));rewind(input);assert(fread(original.data(),1,original.size(),input)==original.size());fclose(input);
    WorldScene saved;assert(loadScene(modelPath.string().c_str(),saved,error));
    for(auto mutation:{std::pair<size_t,uint32_t>{20,0x7fc00000u},{32,0x7f800000u},{44,0x7fc00000u},{20+8*32,8u},{20+8*32+12,2u},{8,UINT32_MAX},{20+8*32+4*16+20,0x7fc00000u}}){auto bytes=original;for(unsigned j=0;j<4;++j)bytes[mutation.first+j]=uint8_t(mutation.second>>(8*j));FILE* file=fopen(modelPath.string().c_str(),"wb");assert(file);fwrite(bytes.data(),1,bytes.size(),file);fclose(file);assert(!loadScene(modelPath.string().c_str(),saved,error));assert(saved.vertices.size()==8&&saved.triangles.size()==4);}
    for(size_t length:{size_t(19),size_t(100),original.size()-1}){FILE* file=fopen(modelPath.string().c_str(),"wb");assert(file);fwrite(original.data(),1,length,file);fclose(file);assert(!loadScene(modelPath.string().c_str(),saved,error));assert(saved.vertices.size()==8&&saved.triangles.size()==4);}
    fs::remove_all(root);
    if(argc>1){for(unsigned city=0;city<2;++city){std::string map=city?"Kalimdor":"Azeroth";Vec3 center=city?Vec3(1500,-4415,32):Vec3(-8833,628,97);LocalSceneCache real({size_t(argc>2?std::stoul(argv[2]):32)*1024*1024});for(unsigned step=0;step<4;++step){Vec3 c=center+Vec3(float(step)*64,0,0);char name[32];snprintf(name,sizeof name,"%s move%u",city?"OG":"SW",step);run(real,tiles(argv[1],map,c),(fs::path(argv[1])/"models").string(),map,c,name,12000);}}}
    puts("PASS incremental exact crop/vertices/triangles/normals/materials/owners, rays, probes, movement reuse, map reset, allocation failure, bounded retention, malformed bulk input and concurrent tracing");
}
