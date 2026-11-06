#pragma once
#include "world_probe_cache.h"
#include <vector>

namespace NorthlightGI {
// Solve the camera's eight interpolation corners first, then expand outwards.
// This changes scheduling only: every point in the window (probeLayout()) keeps
// its exact world key, ray seed and full transport solve.
inline Vec3 probeWindowOrigin(Vec3 camera) {
    const auto& l=probeLayout();
    const float offset=float(l.n/2-1)*8,offsetZ=float(l.nz/2-1)*8;
    return {std::floor(camera.x/8)*8-offset,std::floor(camera.y/8)*8-offset,
            std::floor(camera.z/8)*8-offsetZ};
}
inline Vec3 probeWindowPosition(Vec3 origin,unsigned index) {
    const unsigned g=probeLayout().n;
    const unsigned row=index/g;
    return {origin.x+float(index%g)*8,origin.y+float(row%g)*8,origin.z+float(row/g)*8};
}
inline std::vector<unsigned> probeSolveOrder(Vec3 camera) {
    // Valid layouts contain fewer than 2^31 probes; the high bit marks non-corners.
    struct Item {unsigned index;float distance;};
    const auto& l=probeLayout();const unsigned count=l.count();
    std::vector<Item> items(count);
    const Vec3 origin=probeWindowOrigin(camera);
    const unsigned g=l.n,base=g/2-1,baseZ=l.nz/2-1;
    float axes[3][ProbeGridMaxN];
    for(unsigned axis=0;axis<3;++axis)for(unsigned at=0;at<(axis==2?l.nz:g);++at){
        const float o=axis==0?origin.x:axis==1?origin.y:origin.z,c=axis==0?camera.x:axis==1?camera.y:camera.z;
        const float delta=(o+float(at)*8)-c;axes[axis][at]=delta*delta;
    }
    unsigned i=0;
    for(unsigned z=0;z<l.nz;++z)for(unsigned y=0;y<g;++y)for(unsigned x=0;x<g;++x,++i) {
        const bool corner=x>=base&&x<=base+1&&y>=base&&y<=base+1&&z>=baseZ&&z<=baseZ+1;
        items[i]={i|(corner?0u:0x80000000u),axes[0][x]+axes[1][y]+axes[2][z]};
    }
    std::sort(items.begin(),items.end(),[](const Item& a,const Item& b){
        if((a.index^b.index)&0x80000000u)return a.index<b.index;
        if(a.distance!=b.distance)return a.distance<b.distance;
        return a.index<b.index;
    });
    std::vector<unsigned> order(count);
    for(unsigned i=0;i<count;++i)order[i]=items[i].index&0x7fffffffu;
    return order;
}

// Display continuity only. The old atlas never enters the new generation's
// solve cache, and moving corrections must be applied before this merge.
// Occupied invalid results replace old values too; absence is not invalidity.
inline void retainProbeDisplayFallback(std::vector<ProbeAtlasEntry>& current,
                                       const std::vector<ProbeAtlasEntry>& previous) {
    const size_t size=probeLayout().atlasSize();
    if(current.size()!=size||previous.size()!=size)return;
    for(size_t i=0;i<size;++i)if(!current[i].occupied)current[i]=previous[i];
}

// Worker-owned and shared across camera requests in one static generation.
// Camera motion must not restart this clock and postpone publication indefinitely.
// Completion still publishes immediately using the normal final path.
class ProbePublicationCadence {
    uint32_t last_=0;
    bool published_=false,dirty_=false;
public:
    // Publish during camera motion: 250 ms was thirty frames at 120 Hz.
    static constexpr uint32_t IntervalMs=33;
    void reset(){last_=0;published_=dirty_=false;}
    void solved(){dirty_=true;}
    bool due(uint32_t now,unsigned processed)const {
        return dirty_&&processed>=8&&(!published_||uint32_t(now-last_)>=IntervalMs);
    }
    void published(uint32_t now){last_=now;published_=true;dirty_=false;}
};
} // namespace NorthlightGI
