#pragma once
#include "native_scene_lights.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif
using namespace NorthlightNativeLights;
using Bytes=std::vector<unsigned char>;
template<class T> void put(Bytes& b,unsigned at,T value){assert(at+sizeof value<=b.size());std::memcpy(b.data()+at,&value,sizeof value);}
template<class T> T get(const Bytes& b,unsigned at){return SceneDetail::at<T>(b.data(),at);}
static unsigned append(Bytes& b,const void* p,unsigned n){while(b.size()%4)b.push_back(0);const unsigned at=unsigned(b.size());b.resize(at+n);if(n)std::memcpy(b.data()+at,p,n);return at;}
static void authoredTrack(Bytes& b,unsigned at,unsigned width,const std::vector<std::vector<float>>& keys,unsigned channels=1){
    put<std::uint16_t>(b,at,width==1?0:1);put<std::int16_t>(b,at+2,-1);std::vector<std::array<unsigned,2>> times,values;
    for(const auto& row:keys){std::vector<unsigned> stamps;Bytes raw;for(unsigned i=0;i<row.size()/channels;++i){stamps.push_back(i*1000);for(unsigned k=0;k<channels;++k){if(width==1)raw.push_back(static_cast<unsigned char>(row[i*channels+k]));else{const auto n=raw.size();raw.resize(n+4);put(raw,unsigned(n),row[i*channels+k]);}}}
        times.push_back({unsigned(stamps.size()),append(b,stamps.data(),unsigned(stamps.size()*4))});values.push_back({unsigned(stamps.size()),append(b,raw.data(),unsigned(raw.size()))});}
    put(b,at+4,unsigned(keys.size()));put(b,at+8,append(b,times.data(),unsigned(times.size()*8)));put(b,at+12,unsigned(keys.size()));put(b,at+16,append(b,values.data(),unsigned(values.size()*8)));
}
static inline Bytes modelAsset(){
    Bytes b(304+88+4*64+156);std::memcpy(b.data(),"MD20",4);put(b,4,264u);put(b,28,4u);put(b,32,392u);put(b,44,1u);put(b,48,304u);put(b,264,1u);put(b,268,648u);
    put(b,304+4,0x200u);put<std::int16_t>(b,304+8,-1);put(b,304+84,1.f);
    for(unsigned i=0;i<4;++i){put(b,392+64*i+4,2000u);put(b,392+64*i+12,0u);}
    for(unsigned offset:{320u,340u,360u})put<std::int16_t>(b,offset+2,-1);
    put<std::uint16_t>(b,648,1);put<std::uint16_t>(b,650,0);put(b,660,1.f);
    authoredTrack(b,648+56,12,{{.97254908f,.92941183f,.627451f}},3);
    authoredTrack(b,648+76,4,{{.5f,1.f},{.5f,1.f},{.5f,1.f},{.5f,1.f}});
    authoredTrack(b,648+136,1,{{0},{1},{0,1},{1}});return b;
}
static inline void quaternionTrack(Bytes& b,unsigned at,const std::vector<std::vector<float>>& keys){
    put<std::uint16_t>(b,at,1);put<std::int16_t>(b,at+2,-1);std::vector<std::array<unsigned,2>> times,values;
    for(const auto& row:keys){std::vector<unsigned> stamps;std::vector<std::uint16_t> packed;for(unsigned i=0;i<row.size()/4;++i){stamps.push_back(i*1000);for(unsigned k=0;k<4;++k)packed.push_back(static_cast<std::uint16_t>(std::lround((std::clamp(row[i*4+k],-1.f,1.f)+1)*32767.5f)));}
        times.push_back({unsigned(stamps.size()),append(b,stamps.data(),unsigned(stamps.size()*4))});values.push_back({unsigned(stamps.size()),append(b,packed.data(),unsigned(packed.size()*2))});}
    put(b,at+4,unsigned(keys.size()));put(b,at+8,append(b,times.data(),unsigned(times.size()*8)));put(b,at+12,unsigned(keys.size()));put(b,at+16,append(b,values.data(),unsigned(values.size()*8)));
}
static void relocateTrack(Bytes& b,unsigned at,unsigned base){
    const unsigned n=get<unsigned>(b,at+4),v=get<unsigned>(b,at+12);const auto offset=[&](unsigned p){return p>=base&&p-base<b.size()?p-base:p;};const unsigned times=offset(get<unsigned>(b,at+8)),values=offset(get<unsigned>(b,at+16));
    if(n){assert(times+n*8<=b.size());for(unsigned i=0;i<n;++i){const auto p=get<unsigned>(b,times+i*8+4);put(b,times+i*8+4,offset(p)+base);}put(b,at+8,times+base);}
    if(v){assert(values+v*8<=b.size());for(unsigned i=0;i<v;++i){const auto p=get<unsigned>(b,values+i*8+4);put(b,values+i*8+4,offset(p)+base);}put(b,at+16,values+base);}
}
static void relocate(Bytes& b,unsigned base){
    const unsigned bones=get<unsigned>(b,44),boneAt=get<unsigned>(b,48),lights=get<unsigned>(b,264),lightAt=get<unsigned>(b,268);
    for(unsigned i=0;i<bones;++i)for(unsigned at:{16u,36u,56u})relocateTrack(b,boneAt+i*88+at,base);
    for(unsigned i=0;i<lights;++i)for(unsigned at:{16u,36u,56u,76u,96u,116u,136u})relocateTrack(b,lightAt+i*156+at,base);
    const unsigned attachments=get<unsigned>(b,240),attachmentAt=get<unsigned>(b,244);for(unsigned i=0;i<attachments;++i)relocateTrack(b,attachmentAt+i*40+20,base);
    for(unsigned at:{8u,20u,28u,44u,60u,240u,248u,264u})if(get<unsigned>(b,at))put(b,at+4,get<unsigned>(b,at+4)+base);
}
struct Memory {
    std::map<unsigned,Bytes> blocks;unsigned next=0x10000000;bool system=false;unsigned fail=0,calls=0,watchBegin=0,watchEnd=0,watchedReads=0;std::size_t bytes=0;
    unsigned add(Bytes value){const unsigned p=next;next+=(unsigned(value.size())+4095)&~4095u;blocks.emplace(p,std::move(value));return p;}
    Bytes& object(unsigned p){return blocks.at(p);}
    template<class T> void set(unsigned p,unsigned offset,T value){auto i=blocks.upper_bound(p);assert(i!=blocks.begin());--i;put(i->second,p-i->first+offset,value);}
    bool read(unsigned p,void* out,std::size_t n){++calls;bytes+=n;if(watchBegin&&p>=watchBegin&&p<watchEnd)++watchedReads;if(fail&&fail>=p&&fail-p<n)return false;
        auto i=blocks.upper_bound(p);if(i==blocks.begin())return false;--i;const auto offset=p-i->first;if(offset>i->second.size()||n>i->second.size()-offset)return false;
#ifdef _WIN32
        if(system){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),i->second.data()+offset,out,n,&got)&&got==n;}
#endif
        std::memcpy(out,i->second.data()+offset,n);return true;}
    void proof(){
        blocks[0x79abdf]={0x8b,0x35,0x4c,0x75,0xcd,0x00};
        blocks[0x834550]={0x8d,0x42,0x08,0x89,0x46,0x08,0x89,0x56,0x28,0x8b,0x38,0x8d,0x4e,0x0c};
        blocks[0x834575]={0x8b,0x46,0x2c,0x8b,0x80,0x50,0x01,0x00,0x00};
        blocks[0x834593]={0x8b,0x8e,0xd0,0x01,0x00,0x00,0x52,0x8d,0x4c,0x19,0x68};
        blocks[0x8316b0]={0x89,0x46,0x54,0x33,0xc0,0x3b,0xd3,0x0f,0x95,0xc0,0x89,0x7e,0x50,0x89,0x4e,0x48};
        blocks[0x82e625]={0x0f,0xb7,0x74,0xd0,0x04,0x8d,0x04,0xd0,0xc1,0xe6,0x06,0x03,0xb3,0x98,0x00,0x00,0x00};
        blocks[0x8286d2]={0x0f,0xb7,0x0c,0xd0,0x8d,0x04,0xd0,0x89,0x4d,0x0c};
        blocks[0x82738a]={0x69,0xc0,0xac,0x00,0x00,0x00,0x03,0x83,0x94,0x00,0x00,0x00,0x8b,0xf0,0x83,0xbe,0x88,0x00,0x00,0x00,0x00};
        blocks[0x821ab5]={0x8d,0x8e,0xc4,0x00,0x00,0x00};
    }
};
struct Fixture {
    Memory memory;unsigned scene,data,shared;std::vector<unsigned> models,bones,lights;Reader reader;Snapshot output;float camera[3]{};
    explicit Fixture(Bytes blob=modelAsset(),unsigned allocationBase=0x10000000){
        memory.next=allocationBase;memory.proof();scene=memory.add(Bytes(0x148));memory.blocks[SceneDetail::ScenePointer]=Bytes(4);put(memory.blocks[SceneDetail::ScenePointer],0,scene);
        put(memory.object(scene),12,600u);for(unsigned at:{0x84u,0xc4u})for(unsigned i=0;i<16;i+=5)put(memory.object(scene),at+4*i,1.f);data=memory.next;relocate(blob,data);assert(memory.add(std::move(blob))==data);
        shared=memory.add(Bytes(0x154));put(memory.object(shared),0x150,data);
    }
    void useAsset(Bytes blob){data=memory.next;relocate(blob,data);memory.add(std::move(blob));shared=memory.add(Bytes(0x154));put(memory.object(shared),0x150,data);}
    unsigned add(float x=0,unsigned sequence=1){
        const auto& asset=memory.object(data);const unsigned count=get<unsigned>(asset,44),lightCount=get<unsigned>(asset,264);
        const unsigned bone=memory.add(Bytes(count*(0xac+64)+lightCount*0xd4)),light=bone+count*(0xac+64),model=memory.add(Bytes(SceneDetail::ModelBytes));
        auto& b=memory.object(bone);for(unsigned i=0;i<count;++i){put<std::uint16_t>(b,i*0xac+0x48,0xffff);put<std::uint16_t>(b,i*0xac+0x6c,0xffff);}
        put<std::uint16_t>(b,0x48,static_cast<std::uint16_t>(sequence));put(b,0x4c,100u);put(b,0x50,2100u);put(b,0x54,1.f);
        for(unsigned i=0;i<lightCount;++i){memory.set(light,i*0xd4+0x64,1u);memory.set(light,i*0xd4+0xc0,.7f);memory.set(light,i*0xd4+0xc4,.03f);}
        auto& m=memory.object(model);put(m,8,scene+8);put(m,12,get<unsigned>(memory.object(scene),8));put(memory.object(scene),8,model);put(m,16,1u);put(m,40,scene);put(m,44,shared);
        put(m,0x94,bone);put(m,0x1d0,light);put(m,0x74,100u);put(m,0x178,1.f);put(m,0x198,0.f); // stale native effective alpha must not turn a source off
        for(unsigned i=0;i<16;i+=5)put(m,0xb4+4*i,1.f);put(m,0xe4,x);
        models.push_back(model);bones.push_back(bone);lights.push_back(light);return model;
    }
    void update(){reader.update([&](unsigned p,void* o,std::size_t n){return memory.read(p,o,n);},camera,output);assert(wellFormed(output));}
};
