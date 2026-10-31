#include "static_shadow_scene.h"
#include <cassert>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
using namespace StaticShadow;
namespace fs=std::filesystem;
static void u32(FILE*f,uint32_t v){unsigned char b[4]={uint8_t(v),uint8_t(v>>8),uint8_t(v>>16),uint8_t(v>>24)};assert(std::fwrite(b,1,4,f)==4);}
static void scalar(FILE*f,float v){uint32_t u;std::memcpy(&u,&v,4);u32(f,u);}
static void vec(FILE*f,Vec3 v){scalar(f,v.x);scalar(f,v.y);scalar(f,v.z);}
static std::string key(unsigned char v){static const char h[]="0123456789abcdef";std::string s;for(int i=0;i<32;++i){s+=h[v>>4];s+=h[v&15];}return s;}
static NorthlightGI::WorldScene triangle(){NorthlightGI::WorldScene s;s.vertices={{{0,0,0},{0,0,1},0,0},{{1,0,0},{0,0,1},1,0},{{0,1,0},{0,0,1},0,1}};s.triangles={{0,1,2,0}};NorthlightGI::WorldMaterial m;m.width=2;m.height=1;m.rgba={255,255,255,0,255,255,255,255};m.alphaCutoff=.37f;s.materials.push_back(m);return s;}
static void modelFile(const fs::path& path,bool opaque=false){auto s=triangle();if(opaque)s.materials[0].rgba[3]=255;FILE*f=std::fopen(path.string().c_str(),"wb");assert(f);std::fwrite("FGS2",1,4,f);u32(f,2);u32(f,3);u32(f,1);u32(f,1);for(const auto&v:s.vertices){vec(f,v.position);vec(f,v.normal);scalar(f,v.u);scalar(f,v.v);}for(const auto&t:s.triangles){u32(f,t.v0);u32(f,t.v1);u32(f,t.v2);u32(f,t.material);}const auto&m=s.materials[0];vec(f,m.albedo);u32(f,m.width);u32(f,m.height);scalar(f,m.alphaCutoff);u32(f,uint32_t(m.rgba.size()));std::fwrite(m.rgba.data(),1,m.rgba.size(),f);std::fclose(f);}
static Placement placement(uint64_t uid,Vec3 p,unsigned char hash=1){Placement x;x.uid=uid;x.category=1;x.translation=p;x.low=p;x.high=p+Vec3(1,1,1);x.modelKey=key(hash);return x;}
static void tileFile(const fs::path& path,const std::vector<Placement>& ps){FILE*f=std::fopen(path.string().c_str(),"wb");assert(f);std::fwrite("FGS3",1,4,f);u32(f,3);u32(f,uint32_t(ps.size()));for(const auto&p:ps){for(unsigned j=0;j<32;++j){unsigned char c=static_cast<unsigned char>(std::stoul(p.modelKey.substr(2*j,2),nullptr,16));std::fwrite(&c,1,1,f);}u32(f,uint32_t(p.uid));u32(f,uint32_t(p.uid>>32));u32(f,p.category);vec(f,p.low);vec(f,p.high);for(float v:p.matrix)scalar(f,v);vec(f,p.translation);}std::fclose(f);}
static void report(const char* label,const Snapshot& s){std::cout<<label<<" placements="<<s.placements.size()<<" models="<<s.models.size()<<" reads="<<s.stats.modelReads<<" metadata="<<s.stats.metadataReads<<" resident="<<s.stats.cpuBytes<<" peak="<<s.stats.cpuPeak<<" metadataBytes="<<s.stats.metadataBytes<<" publications="<<s.stats.publications<<" transient="<<s.stats.transientPeak<<" deferred="<<s.stats.deferred<<" failures="<<s.stats.failures<<" pending="<<s.stats.pendingModels<<" readMs="<<s.stats.readMs<<" packMs="<<s.stats.packMs<<" selectMs="<<s.stats.selectionMs<<" latencyMs="<<s.stats.latencyMs<<" maxJobMs="<<s.stats.maxJobMs<<"\n";}
int main(int argc,char**argv){
    Request r;r.map="Azeroth";r.center={0,0,0};r.directions[0]={1,0,.035f};
    assert(selected(r,{700,-2,15},{710,2,45})); // low-angle, outside local GI
    assert(!selected(r,{-20,700,0},{20,710,30})); // cross-light far volume
    Request turned=r;turned.directions[0]={0,1,.035f};assert(selected(turned,{-20,700,0},{20,710,30}));
    assert(selected(r,{300,600,0},{310,610,30})==false);
    // Large building spans selected volume although its center is outside it.
    assert(selected(r,{100,-20,0},{1900,20,30}));
    auto source=triangle();source.materials[0].addressU=3;source.materials[0].addressV=2;Model m;std::string error;assert(packModel(std::move(source),"test",m,error));assert(m.index16&&m.vertices.size()==3&&m.indexCount()==3&&m.indices.empty()&&m.uploadIndices16==std::vector<uint16_t>({0,1,2}));assert(m.materials[0].alphaCutoff==.37f&&m.materials[0].addressU==3&&m.materials[0].addressV==2);assert(m.alphaAt(0,0)==0&&m.alphaAt(0,1)==255&&m.materials[0].rgba.capacity()==0);
    auto changed=triangle();changed.materials[0].addressU=3;changed.materials[0].addressV=2;changed.vertices[1].position.x=2;Model m2;assert(packModel(std::move(changed),"test",m2,error));assert(m.contentRevision!=m2.contentRevision);assert(m.materialKeys==m2.materialKeys);
    auto opaqueScene=triangle();opaqueScene.materials[0].rgba[3]=255;Model opaque;
    assert(packModel(std::move(opaqueScene),"opaque",opaque,error));
    assert(opaque.materials[0].rgba.empty()&&opaque.materials[0].rgba.capacity()==0&&opaque.materials[0].width==0&&opaque.materials[0].height==0);
    assert(opaque.materials[0].alphaCutoff==.37f&&opaque.uploadIndices16==m.uploadIndices16&&opaque.vertices.size()==m.vertices.size());
    for(size_t i=0;i<m.vertices.size();++i)assert(std::memcmp(&m.vertices[i],&opaque.vertices[i],sizeof(Vertex))==0);
    auto whiteScene=triangle();whiteScene.materials[0].rgba.clear();whiteScene.materials[0].width=whiteScene.materials[0].height=0;Model white;
    assert(packModel(std::move(whiteScene),"white",white,error));assert(white.contentRevision==opaque.contentRevision&&white.materialKeys==opaque.materialKeys);
    auto nearOpaqueScene=triangle();nearOpaqueScene.materials[0].rgba[3]=254;Model nearOpaque;
    assert(packModel(std::move(nearOpaqueScene),"nearopaque",nearOpaque,error));assert(nearOpaque.materials[0].width==2&&nearOpaque.alphaAt(0,0)==254&&nearOpaque.materials[0].rgba.capacity()==0);
    auto largeSource=triangle();largeSource.vertices.resize(65537,largeSource.vertices[0]);largeSource.triangles[0].v2=65536;Model largeModel;
    assert(packModel(std::move(largeSource),"wide-index",largeModel,error));assert(!largeModel.index16&&largeModel.uploadPrepared&&largeModel.uploadIndices16.empty()&&largeModel.indexCount()==3&&largeModel.indexAt(2)==65536);
    Placement transformed;float mat[9]={0,-2,0,3,0,0,0,0,-1};std::copy(mat,mat+9,transformed.matrix);transformed.translation={4,5,6};Vec3 p=transformPoint(transformed,{1,2,3});assert(p.x==0&&p.y==8&&p.z==3);Vec3 lo,hi;transformBounds(transformed,{0,0,0},{1,2,3},lo,hi);
    const float exactLow[]={0,5,3},exactHigh[]={4,8,6},actualLow[]={lo.x,lo.y,lo.z},actualHigh[]={hi.x,hi.y,hi.z};
    for(unsigned axis=0;axis<3;++axis){assert(actualLow[axis]<=exactLow[axis]&&actualHigh[axis]>=exactHigh[axis]);assert(exactLow[axis]-actualLow[axis]<.001f&&actualHigh[axis]-exactHigh[axis]<.001f);}
    fs::path root=fs::temp_directory_path()/("fr-static-shadow-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));fs::create_directories(root/"Azeroth");fs::create_directories(root/"Kalimdor");fs::create_directories(root/"models");modelFile(root/"models"/(key(1)+".fgs"));modelFile(root/"models"/(key(2)+".fgs"));
    auto a=placement(1,{0,0,0}),b=placement(2,{500,0,0}),c=placement(3,{1400,0,0},2);auto wmo=a;wmo.category=2;
    // Tile duplicates dedup by category+UID; equal UID in a different category remains.
    tileFile(root/"Azeroth/32_32.fg3",{a,b,c,wmo});tileFile(root/"Azeroth/31_32.fg3",{a,b,c,wmo});tileFile(root/"Azeroth/32_29.fg3",{c});tileFile(root/"Kalimdor/32_32.fg3",{placement(9,{0,0,0},2)});
    {Streamer streamer(root.string());streamer.request(r);assert(streamer.waitIdle());auto first=streamer.snapshot();assert(first&&first->placements.size()==3&&first->models.size()==1&&first->stats.modelReads==1);auto original=first->models.begin()->second;assert(first->preparedIdentity==first.get()&&first->geometryRevision&&first->placementGroups.at(key(1)).size()==3);
        Model packed;assert(packModel(triangle(),key(1),packed,error));assert(packed.contentRevision==original->contentRevision);assert(packed.materialKeys==original->materialKeys);
        for(int i=0;i<1000;++i)streamer.request(r);assert(streamer.waitIdle());auto same=streamer.snapshot();assert(same==first);assert(same->stats.modelReads==1); // camera isn't an input
        Request move=r;move.center.x=64;streamer.request(move);assert(streamer.waitIdle());auto second=streamer.snapshot();assert(second->models.begin()->second==original&&second->stats.modelReads==1);
        streamer.request(r);assert(streamer.waitIdle());auto back=streamer.snapshot();assert(back->models.begin()->second==original&&back->stats.modelReads==1);
        move.center.x=768;move.allowLoads=false;streamer.request(move);assert(streamer.waitIdle());auto blocked=streamer.snapshot();assert(blocked->stats.modelReads==1&&blocked->stats.pendingModels==1&&!blocked->stats.complete);
        move.allowLoads=true;streamer.request(move);assert(streamer.waitIdle());auto edge=streamer.snapshot();assert(edge->stats.modelReads==2&&edge->models.size()==2);assert(edge->models.at(key(1))==original);assert(edge->stats.metadataReads-first->stats.metadataReads<=10);
        r.map="Kalimdor";streamer.request(r);assert(streamer.waitIdle());auto mapped=streamer.snapshot();assert(mapped&&mapped->mapGeneration!=first->mapGeneration&&mapped->map=="Kalimdor"&&mapped->placements.size()==1&&mapped->placements[0].uid==9);report("synthetic",*mapped);
    }
    // The streamed decoder and portable packer have identical opaque semantics.
    fs::create_directories(root/"Opaque");modelFile(root/"models"/(key(3)+".fgs"),true);tileFile(root/"Opaque/32_32.fg3",{placement(100,{0,0,0},3)});
    {Streamer stream(root.string());Request q;q.map="Opaque";stream.request(q);assert(stream.waitIdle());auto snap=stream.snapshot();assert(snap&&snap->models.size()==1);auto model=snap->models.begin()->second;assert(model->contentRevision==opaque.contentRevision&&model->materialKeys==opaque.materialKeys&&model->uploadIndices16==opaque.uploadIndices16&&model->materials[0].rgba.empty()&&model->materials[0].rgba.capacity()==0&&model->materials[0].alphaCutoff==.37f);}
    // A complete stationary request must still retire unused resources when
    // their hysteresis expires, without publishing/re-reading anything.
    {Limits l;l.retainSeconds=1;Streamer stream(root.string(),l);Request q;q.map="Azeroth";q.directions[0]={1,0,.035f};stream.request(q);assert(stream.waitIdle());auto first=stream.snapshot();assert(first&&first->models.size()==1);std::weak_ptr<const Model> old=first->models.at(key(1));
        q.center.x=1600;stream.request(q);assert(stream.waitIdle());auto current=stream.snapshot();assert(current&&current->models.size()==1&&current->models.count(key(2)));first.reset();assert(!old.expired());
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(!old.expired()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(20));assert(old.expired());
        assert(stream.snapshot()==current&&stream.snapshot()->stats.modelReads==current->stats.modelReads&&stream.snapshot()->stats.metadataReads==current->stats.metadataReads&&stream.snapshot()->stats.publications==current->stats.publications);
        assert(current->models.at(key(2))->vertices.size()==3);
        q.center.x=0;stream.request(q);assert(stream.waitIdle());auto returned=stream.snapshot();assert(returned->stats.metadataReads>current->stats.metadataReads&&returned->stats.modelReads==current->stats.modelReads+1);
    }
    // Budget failure retains ready snapshots and never publishes a partial model.
    // Recovery releases even the last-needed model. A consumer-held immutable
    // snapshot survives, paused requests do not repopulate, and a stationary
    // resume must publish the latest request rather than the pre-pause map.
    {Streamer stream(root.string());stream.setPaused(true);assert(stream.waitIdle());stream.setPaused(false);assert(stream.waitIdle());
     Request q;q.map="Azeroth";stream.request(q);assert(stream.waitIdle());
     auto held=stream.snapshot();assert(held&&!held->models.empty());std::weak_ptr<const Model> weak=held->models.begin()->second;
     const auto oldGeneration=held->mapGeneration;stream.setPaused(true);assert(stream.waitIdle());assert(!stream.snapshot());
     assert(!weak.expired()&&held->models.begin()->second->vertices.size()==3);held.reset();
     const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(4);
     while(!weak.expired()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(10));assert(weak.expired());
     q.map="Kalimdor";for(unsigned i=0;i<50;++i){stream.request(q);stream.setPaused(true);}assert(stream.waitIdle());assert(!stream.snapshot());
     stream.setPaused(false);assert(stream.waitIdle());auto resumed=stream.snapshot();assert(resumed&&resumed->map=="Kalimdor"&&resumed->mapGeneration>oldGeneration&&resumed->placements.size()==1&&resumed->placements[0].uid==9);
     stream.setPaused(true);assert(stream.waitIdle());assert(!stream.snapshot());}
    // Cancellation may race selection, I/O or publication. A paused worker
    // must finish all old work before waitIdle; destruction also joins it.
    {Streamer stream(root.string());Request q;
     for(unsigned i=0;i<100;++i){q.map=i%2?"Azeroth":"Kalimdor";stream.request(q);stream.setPaused(false);stream.setPaused(true);}
     assert(stream.waitIdle());assert(!stream.snapshot());q.map="Kalimdor";q.center={0,0,0};stream.request(q);stream.setPaused(false);assert(stream.waitIdle());
     auto snap=stream.snapshot();assert(snap&&snap->map=="Kalimdor"&&snap->placements.size()==1&&snap->placements[0].uid==9);}
    {Streamer stream(root.string());Request q;q.map="Azeroth";stream.request(q);stream.setPaused(true);} // stop with a pending reclaim
    {Limits l;l.residentBytes=1;Streamer s(root.string(),l);r.map="Azeroth";s.request(r);assert(s.waitIdle());auto snap=s.snapshot();assert(snap&&snap->models.empty()&&!snap->stats.complete&&snap->stats.deferred>0&&snap->stats.cpuBytes==0);}
    // A corrupt new resource must leave already valid shared models in place.
    {std::ofstream broken(root/"models"/(key(2)+".fgs"),std::ios::binary|std::ios::trunc);broken<<"broken";}
    {Streamer s(root.string());r.map="Azeroth";r.center.x=768;s.request(r);assert(s.waitIdle());auto snap=s.snapshot();assert(snap&&snap->models.size()==1&&snap->stats.failures>0&&!snap->stats.complete);}
    // Rapid map/position changes do not publish old-generation completions.
    modelFile(root/"models"/(key(2)+".fgs"));
    {Streamer s(root.string());for(int i=0;i<100;++i){r.map=(i%2)?"Kalimdor":"Azeroth";r.center.x=float((i%3)*64);s.request(r);}assert(s.waitIdle());auto snap=s.snapshot();assert(snap&&snap->map=="Kalimdor"&&snap->placements.size()==1&&snap->placements[0].uid==9);}
    // Publication comparisons run outside the reader mutex. Race dense scene
    // updates and map resets against a snapshot reader: every published object
    // must remain immutable and old generations must never reappear.
    fs::create_directories(root/"Crowded");std::vector<Placement> crowd;
    for(unsigned i=0;i<12000;++i)crowd.push_back(placement(1000+i,{float(i%100),float((i/100)%100),0}));
    tileFile(root/"Crowded/32_32.fg3",crowd);
    {Streamer stream(root.string());Request q;q.map="Crowded";stream.request(q);assert(stream.waitIdle());
     auto held=stream.snapshot();assert(held&&held->placements.size()==crowd.size());
     std::atomic<bool> done{false};std::thread reader([&]{uint64_t generation=0;while(!done.load()){
         auto snap=stream.snapshot();if(!snap)continue;assert(snap->mapGeneration>=generation);generation=snap->mapGeneration;
         assert(snap->preparedIdentity==snap.get());for(const auto& group:snap->placementGroups)for(auto index:group.second){assert(index<snap->placements.size());assert(snap->placements[index].modelKey==group.first);}
     }});
     for(unsigned i=0;i<60;++i){q.map=i%4==0?"Kalimdor":"Crowded";q.center.x=float(i%3)*64;stream.request(q);if(i%10==0)assert(stream.waitIdle());}
     q.map="Kalimdor";q.center={0,0,0};stream.request(q);assert(stream.waitIdle());done=true;reader.join();
     auto final=stream.snapshot();assert(final&&final->map=="Kalimdor"&&final->placements.size()==1);
     assert(held->map=="Crowded"&&held->placements.size()==crowd.size()&&held->placements.front().uid==1000);
    }
    fs::remove_all(root);
    if(argc>1){for(unsigned cell:{64u,128u,256u})for(unsigned city=0;city<2;++city){Limits limits;limits.cellSize=cell;Streamer s(argv[1],limits);Request q;q.map=city?"Kalimdor":"Azeroth";q.center=city?Vec3(1500,-4415,32):Vec3(-8833,628,97);q.directions[0]=NorthlightGI::normalized({.8f,.4f,.15f});s.request(q);assert(s.waitIdle(120000));auto initial=s.snapshot();assert(initial);char label[80];std::snprintf(label,sizeof label,"%s cell%u initial",city?"OG":"SW",cell);report(label,*initial);uint64_t before=initial->stats.modelReads,metadata=initial->stats.metadataReads;
        for(int i=0;i<300;++i)s.request(q);assert(s.waitIdle());auto rotate=s.snapshot();assert(rotate->stats.modelReads==before&&rotate->stats.metadataReads==metadata);
        q.center.x+=64;s.request(q);assert(s.waitIdle(120000));auto moved=s.snapshot();std::snprintf(label,sizeof label,"%s cell%u moved64",city?"OG":"SW",cell);report(label,*moved);
        size_t shared=0;for(auto& pair:initial->models){auto it=moved->models.find(pair.first);if(it!=moved->models.end()){assert(it->second==pair.second);++shared;}}std::cout<<" retainedShared="<<shared<<" deltaReads="<<moved->stats.modelReads-before<<"\n";
        q.center.x-=64;s.request(q);assert(s.waitIdle(120000));auto back=s.snapshot();assert(back->stats.modelReads==moved->stats.modelReads);report("return64",*back);
    }}
    std::cout<<"static shadow scene tests passed\n";
}
