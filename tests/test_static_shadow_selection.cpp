#include "static_shadow_scene.cpp"
#include <cassert>
#include <cstdio>
#include <limits>

// Frozen pre-optimization arithmetic: request preparation must never change
// near retention, source order, degenerate directions or a boundary decision.
static bool reference(const StaticShadow::Request& r,StaticShadow::Vec3 low,StaticShadow::Vec3 high,float margin=96.f){
    using namespace NorthlightGI;
    Vec3 center=(low+high)*.5f,extent=(high-low)*.5f,delta=center-r.center;
    if(std::fabs(delta.x)<=288+margin+extent.x&&std::fabs(delta.y)<=288+margin+extent.y&&std::fabs(delta.z)<=320+margin+extent.z)return true;
    for(unsigned i=0;i<2;++i)if(r.active[i]){
        Vec3 d=normalized(r.directions[i]);if(dot(d,d)<.5f)continue;
        Vec3 ref=std::fabs(d.z)>.95f?Vec3(0,1,0):Vec3(0,0,1);
        Vec3 right=normalized(cross(d,ref)),up=cross(right,d);
        auto overlaps=[&](Vec3 a,float reach){float projected=std::fabs(a.x)*extent.x+std::fabs(a.y)*extent.y+std::fabs(a.z)*extent.z;return std::fabs(dot(delta,a))<=reach+margin+projected;};
        if(overlaps(right,240)&&overlaps(up,240)&&overlaps(d,640))return true;
    }
    return false;
}
struct Box {StaticShadow::Vec3 low,high;};
static uint32_t seed=20261022;
static float randomFloat(float span){seed=seed*1664525u+1013904223u;return (float(seed>>8)/16777216.f-.5f)*span;}
static volatile size_t checksum=0;
int main(){
    using namespace StaticShadow;
    const Vec3 special[]={{0,0,0},{0,0,1},{0,0,-1},{1,0,0},{0,1,0},{.1f,.1f,.95f},{.1f,.1f,.9500001f},{1e-30f,0,0},{1,0,.035f}};
    size_t compared=0;
    for(unsigned request=0;request<1024;++request){
        Request r;r.center={randomFloat(40000),randomFloat(40000),randomFloat(2000)};
        for(unsigned s=0;s<2;++s){r.active[s]=(request>>(s*2))&1;r.directions[s]=request%3?special[(request+s)%9]:Vec3(randomFloat(2),randomFloat(2),randomFloat(2));}
        SelectionVolume volume(r);
        for(unsigned box=0;box<256;++box){
            Vec3 center=r.center+Vec3(randomFloat(3000),randomFloat(3000),randomFloat(2000));
            Vec3 extent(std::fabs(randomFloat(1200)),std::fabs(randomFloat(1200)),std::fabs(randomFloat(800)));
            float margin=box%5?96.f:randomFloat(250);
            if(box<24){extent={};center=r.center;const unsigned axis=box%3;const float edge=axis==2?320.f+margin:288.f+margin;const float value=box&1?std::nextafter(edge,INFINITY):edge;if(axis==0)center.x+=value;else if(axis==1)center.y+=value;else center.z+=value;}
            const Vec3 low=center-extent,high=center+extent;
            const bool expected=reference(r,low,high,margin);
            assert(selected(r,low,high,margin)==expected);
            assert(volume.contains(low,high,margin)==expected);++compared;
        }
    }
    // Reconstructed request volumes cannot retain a previous light, map or center.
    Request r;r.directions[0]={1,0,.035f};assert(SelectionVolume(r).contains({700,-2,15},{710,2,45}));
    r.directions[0]={0,1,.035f};assert(!SelectionVolume(r).contains({700,-2,15},{710,2,45}));
    r.active[0]=false;assert(!SelectionVolume(r).contains({700,-2,15},{710,2,45}));
    r.center={705,0,20};assert(SelectionVolume(r).contains({700,-2,15},{710,2,45}));
    std::vector<Box> boxes(4096);
    for(auto& b:boxes){Vec3 center(randomFloat(2200),randomFloat(2200),randomFloat(1400));Vec3 extent(std::fabs(randomFloat(120)),std::fabs(randomFloat(120)),std::fabs(randomFloat(80)));b={center-extent,center+extent};}
    r.center={};r.directions[0]={1,.37f,.035f};r.directions[1]={-.3f,1,.6f};r.active[0]=r.active[1]=true;
    std::vector<double> legacy,prepared;size_t expected=0,actual=0;
    for(unsigned round=0;round<10;++round){
        auto measure=[&](bool reuse){const auto start=std::chrono::steady_clock::now();size_t accepted=0;
            for(unsigned request=0;request<128;++request){SelectionVolume volume(r);for(const auto& b:boxes)accepted+=reuse?volume.contains(b.low,b.high):reference(r,b.low,b.high);}
            checksum+=accepted;const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            if(reuse){actual=accepted;prepared.push_back(ms);}else{expected=accepted;legacy.push_back(ms);}};
        if(round&1){measure(true);measure(false);}else{measure(false);measure(true);}assert(actual==expected);
    }
    std::sort(legacy.begin(),legacy.end());std::sort(prepared.begin(),prepared.end());
    std::printf("PASS static selection compared=%zu benchmarkQueries=%zu accepted=%zu baselineMedianMs=%.6f preparedMedianMs=%.6f speedup=%.4f\n",compared,boxes.size()*128,actual,legacy[5],prepared[5],legacy[5]/prepared[5]);
}
