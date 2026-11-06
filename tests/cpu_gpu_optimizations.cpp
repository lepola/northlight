#define NOMINMAX
#include "weather_lens.h"
#include "weather_motion.h"
#include "dither_noise.h"
#include "local_light_scissor.h"
#include "position_bounds_scan.h"
#include "terrain_indices.h"
#include "replay_bounds_schedule.h"
#include "replay_bounds_near.h"
#include "replay_bulk_layout.h"
#include "material_byte_ops.h"
#include "probe_blend.h"
#include "probe_activation.h"
#include "static_shadow_dedup.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <limits>

static FILE* output=nullptr;
template<class T> static void save(const T& value){assert(std::fwrite(&value,sizeof value,1,output)==1);}
template<class T> static void save(const T* values,size_t count){assert(std::fwrite(values,sizeof(T),count,output)==count);}
static void state(const NorthlightWeatherLens::State& s){
    save(s.count);save(s.fadeCount);save(s.merges);save(s.random);save(s.aspect);save(s.spawnDebt);save(s.size);save(s.frost);
    save(s.drops.data(),s.count);save(s.mergeFades.data(),s.fadeCount);
    std::vector<NorthlightWeatherLens::Vertex> vertices(NorthlightWeatherLens::MaxVertices);
    const unsigned count=s.vertices(vertices.data(),1080,true);assert(count<=vertices.size());save(count);save(vertices.data(),count);
}
static void scanPositions(){
    const uint32_t special[]={0,0x80000000,1,0x807fffff,0x7f800000,0xff800000,0x7fc12345,0x7f812345};
    for(unsigned stride:{0u,12u,16u,20u,32u})for(unsigned count=0;count<=33;++count)for(unsigned edge=0;edge<=8;++edge){
        const size_t span=count?(count-1)*stride+12:0;
        auto* block=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(block);
        DWORD old;assert(VirtualProtect(block+4096,4096,PAGE_NOACCESS,&old));auto* bytes=block+4096-span;
        std::memset(bytes,0xff,span);
        for(unsigned i=0;i<count;++i){const float p[]={float(i)-11,float(2*i)-19,float(3*i)-29};std::memcpy(bytes+i*stride,p,12);}
        if(count&&edge<8)std::memcpy(bytes+(count/2)*stride+4,&special[edge],4);
        float low[3],high[3];const bool valid=NorthlightPositionBounds::scanPositions(bytes,count,stride,low,high);save(valid);save(low,3);save(high,3);
        assert(VirtualFree(block,0,MEM_RELEASE));
    }
}
static void replayAndIndices(){
    for(unsigned count=0;count<48;++count)for(unsigned start=0;start<60;++start){
        unsigned budget=(count+start)%11,probes=0;std::vector<size_t> visited;
        const auto next=NorthlightReplaySchedule::pass(count,start,[&]{++probes;return budget>0;},[&](size_t i){visited.push_back(i);--budget;return i%3==0;});
        save(next);save(probes);save(visited.size());save(visited.data(),visited.size());
    }
    NorthlightReplayBulk::Layout bulk;NorthlightReplayBoundsNear::List rings;
    float a[16]={},b[16]={};a[0]=a[5]=b[0]=b[5]=.03f;b[12]=.75f;
    for(unsigned frame=0;frame<9;++frame){bulk.begin();rings.begin(a,frame&1,b,frame&2);
        for(unsigned i=0;i<4200;++i){const auto* key=reinterpret_cast<void*>(uintptr_t(0x10000+((i/3)%255)*16));
            const auto owner=bulk.add(i%23?key:nullptr,i);save(owner);save(bulk.unique(i));
            const float at[]={float(int(i%99)-49),float(int(i%81)-40),float(i%17)};save(rings.ringOf(at));}
    }
    for(unsigned count:{0u,1u,15u,129u})for(unsigned maximum:{255u,65535u,65536u,100000u}){
        NorthlightTerrainIndices::Indices indices;std::vector<uint32_t> reference;
        for(unsigned i=0;i<count;++i){const uint32_t value=i%3?i:maximum;indices.push_back(value);reference.push_back(value);}
        save(indices.size());for(auto i:indices)save(i);assert(indices==reference);auto copy=indices;assert(copy==indices);if(count)copy.set(count/2,200000);
        save(copy==indices);save(indices==reference);
    }
    for(unsigned length=0;length<512;++length){std::vector<unsigned char> bytes(length);for(unsigned i=0;i<length;++i)bytes[i]=uint8_t(i*71+length);
        save(NorthlightMaterialBytes::minimumAlpha(bytes.data(),bytes.size()));}
}
static void probes(){
    NorthlightProbeBlend::Mirror blend;NorthlightProbeActivation activation;
    std::vector<NorthlightGI::ProbeAtlasEntry> atlas(NorthlightGI::probeLayout().atlasSize());
    for(unsigned frame=0;frame<20;++frame){const char* map=frame<11?"A":"B";blend.begin(map);activation.begin(map);
        for(unsigned i=0;i<64;++i){auto& e=atlas[(i*37)%atlas.size()];e.key={int(i),int(frame/6),-int(i)};
            e.occupied=(i+frame)%7!=0;e.probe.valid=(i+frame)%5!=0;
            if(frame%3==0)for(auto& sh:e.probe.sh)sh={float(i)/13,float(frame)/19,-float(i)/23};}
        const float now=frame*.05f;save(blend.publish(atlas,now));
        for(unsigned i=0;i<atlas.size();++i){save(blend.previous(i),12);save(blend.start(i));save(activation.update(i,atlas[i],now));}
        if(frame%7==0){const std::vector<NorthlightGI::ProbeAtlasEntry> wrong(3);save(blend.publish(wrong,now));}
        if(frame%9==0){blend.reset();activation.reset();}
    }
}
static void triangles(){
    std::vector<NorthlightGI::Vec3> positions;std::vector<uint32_t> indices;
    for(unsigned i=0;i<64;++i){positions.push_back({float(int(i%7)-3),float(i%11),float(i%19)});indices.push_back(i*3%64);}
    for(bool indexed:{false,true})for(bool strip:{false,true})for(unsigned n=0;n<28;++n){
        std::vector<StaticShadowDedup::Triangle> result;const bool valid=StaticShadowDedup::makeTriangles(positions,indices,indexed,n,strip,result);
        save(valid);save(result.size());for(const auto& triangle:result){StaticShadowDedup::Canonical canonical;assert(StaticShadowDedup::canonical(triangle,canonical));save(canonical);}
    }
    for(unsigned invalid=0;invalid<10;++invalid){auto copy=positions;copy[invalid].y=std::numeric_limits<float>::quiet_NaN();
        std::vector<StaticShadowDedup::Triangle> result;save(StaticShadowDedup::makeTriangles(copy,indices,true,32,true,result));save(result.size());}
}
int main(int argc,char** argv){
    assert(argc==2);output=std::fopen(argv[1],"wb");assert(output);
    scanPositions();replayAndIndices();probes();triangles();
    auto ranks=NorthlightDitherNoise::ranks();assert(ranks.size()==4096);auto ordered=ranks;std::sort(ordered.begin(),ordered.end());
    for(unsigned i=0;i<ordered.size();++i)assert(ordered[i]==i);save(ranks.data(),ranks.size());
    const auto first=NorthlightDitherNoise::offsets(),second=NorthlightDitherNoise::offsets();assert(first==second);save(first.data(),first.size());
    NorthlightWeatherLens::State lens;NorthlightWeatherMotion::Tracker wind;NorthlightSurfaceFrame frame{};
    frame.c[1][0]=1.3f;frame.c[1][1]=1.7f;frame.c[1][2]=-1;
    frame.c[4][0]=frame.c[5][1]=frame.c[6][2]=1;
    for(unsigned i=0;i<480;++i){
        const bool snow=(i/80)%2;const auto kind=snow?2u:1u;const double time=i/60.;
        wind.update(frame,kind,(i/31)*53,70+(i/97)*100,true,time);save(wind.flow);
        lens.advance(time,.25f+float(i%60)/80,snow,float(i%47)/46,1.5f+float(i%3)*.125f,1.f,i%7!=0,wind.flow);
        assert(lens.count<=NorthlightWeatherLens::Capacity&&lens.fadeCount<=NorthlightWeatherLens::MergeCapacity);state(lens);
    }
    for(unsigned n:{0u,1u,2u,32u,192u})for(bool trails:{false,true}){
        NorthlightWeatherLens::State s;s.count=n;
        for(unsigned i=0;i<n;++i){auto& d=s.drops[i];d.x=float(i%9)*.002f;d.y=float(i/9)*.004f;
            d.previousX=d.x-.0003f;d.previousY=d.y-.001f;d.water=.00002f*(1+i%4);d.snow=.00001f*(i%3);
            d.vx=.01f;d.vy=.03f;d.seed=float(i%17)/17;d.age=.5f;d.runLength=.04f;}
        s.merge(trails);state(s);
    }
    const float inverse[16]={1,0,0,0,0,1,0,0,0,0,1,0,16,24,8,1},projection[3]={1.3f,1.7f,1};
    uint32_t rng=37;
    for(unsigned i=0;i<2000;++i){
        const unsigned width=2+i%97,height=2+(i*13)%89;auto view=NorthlightLocalLightScissor::view(inverse,projection,.1f,4,width,height);
        NorthlightLocalLightScissor::Bounds bounds;
        for(unsigned j=0;j<1+i%9;++j){float light[4];for(float& v:light){rng=rng*1664525u+1013904223u;v=float(rng%30000)/100-100;}light[3]=1+float(rng%500)/100;
            NorthlightLocalLightScissor::add(view,light,bounds);}
        const auto rect=NorthlightLocalLightScissor::rect(view,bounds);save(rect);
    }
    assert(std::fclose(output)==0);std::puts("CPU_GPU_OPTIMIZATION_CPU_PASS weatherFrames=480 mergeCases=10 scissorCases=2000 ditherRanks=4096");
}
