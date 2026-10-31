#pragma once
#include "world_gi.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace NorthlightGI {

// Coordinates are integer cells on the fixed, world-aligned eight-unit grid.
struct ProbeGridKey {
    int32_t x=0,y=0,z=0;
    bool operator==(const ProbeGridKey& other)const {
        return x==other.x&&y==other.y&&z==other.z;
    }
};

// Solved request grid around the camera (cells of 8 units): n x n horizontal,
// nz vertical, in a cubic wrapping atlas of `atlas` cells per axis. Both window
// extents stay 2 below the atlas so world keys never wrap onto each other.
// 0.3.153 GIDistance: n is runtime (10..22 even), configured once before the
// worker starts and fixed for the process; n=14 is the 0.3.151 14^3 in 16^3.
struct ProbeLayout {
    unsigned n=14,nz=14,atlas=16;
    unsigned count()const{return n*n*nz;}
    size_t atlasSize()const{return size_t(atlas)*atlas*atlas;}
};
inline constexpr unsigned ProbeGridNz=14,ProbeGridMaxN=22;
inline ProbeLayout probeLayoutFor(unsigned n){ProbeLayout l;l.n=n;l.atlas=std::max(n,ProbeGridNz)+2;return l;}
inline bool validProbeLayout(const ProbeLayout& l){
    return l.n>=2&&l.n%2==0&&l.nz>=2&&l.nz%2==0&&l.n<=ProbeGridMaxN&&l.n+2<=l.atlas&&l.nz+2<=l.atlas&&l.atlas<=ProbeGridMaxN+2;
}
namespace detail {inline ProbeLayout probeLayoutValue;}
inline const ProbeLayout& probeLayout(){return detail::probeLayoutValue;}
// Call before any worker, cache or atlas exists; a rejected layout keeps the current one.
inline bool configureProbeLayout(const ProbeLayout& layout){
    if(!validProbeLayout(layout))return false;
    detail::probeLayoutValue=layout;return true;
}
struct ProbeAtlasEntry {
    ProbeGridKey key;
    Probe probe;
    bool occupied=false;
};
inline size_t probeAtlasIndex(ProbeGridKey key) {
    // Signed remainder must be corrected for negative world coordinates. This
    // also handles INT_MIN without negation or signed multiplication overflow.
    const int32_t n=int32_t(probeLayout().atlas);
    const auto wrap=[n](int32_t cell)->size_t {
        const int32_t remainder=cell%n;
        return size_t(remainder<0?remainder+n:remainder);
    };
    return wrap(key.x)+wrap(key.y)*size_t(n)+wrap(key.z)*size_t(n)*size_t(n);
}

// Preserve the renderer's existing world-coordinate random sequence, including
// negative coordinates. Unsigned multiplication makes wraparound well-defined.
inline uint32_t probeSeed(ProbeGridKey key) {
    return uint32_t(key.x)*73856093u ^ uint32_t(key.y)*19349663u ^
           uint32_t(key.z)*83492791u;
}

// Reject off-grid positions instead of silently aliasing two different probes.
// Failure leaves key unchanged. A renderer can also construct integer keys
// directly while iterating the probe grid.
inline bool probeGridKey(Vec3 position,ProbeGridKey& key) {
    const float values[]={position.x,position.y,position.z};
    int32_t cells[3];
    for(unsigned i=0;i<3;++i) {
        const double cell=double(values[i])/8.0;
        if(!std::isfinite(cell))return false;
        const double rounded=std::round(cell);
        if(std::fabs(cell-rounded)>.00001 ||
           rounded<double(std::numeric_limits<int32_t>::min()) ||
           rounded>double(std::numeric_limits<int32_t>::max()))return false;
        cells[i]=int32_t(rounded);
    }
    key={cells[0],cells[1],cells[2]};return true;
}

// Worker-thread-only cache: no D3D objects and no internal synchronization.
// Caller MUST clear when map, geometry, lighting, ray count, or solver settings
// change. Invalid probes are cached too; invalidity is a useful solve result.
// LRU retention keeps recently visited orbit positions without unbounded growth.
class ProbeCache {
    struct Hash {
        size_t operator()(ProbeGridKey key)const noexcept{return probeSeed(key);}
    };
    struct Entry {ProbeGridKey key;Probe value;};
    using Entries=std::list<Entry>;
    Entries entries_;
    std::unordered_map<ProbeGridKey,Entries::iterator,Hash> lookup_;
    size_t capacity_;
public:
    // Default: one entry per atlas slot (4096 at the 0.3.151 layout), >=1.5x every window.
    static constexpr size_t MaxCapacity=size_t(ProbeGridMaxN+2)*(ProbeGridMaxN+2)*(ProbeGridMaxN+2);
    explicit ProbeCache(size_t capacity=probeLayout().atlasSize()):capacity_(std::max<size_t>(1,std::min<size_t>(capacity,MaxCapacity))) {
        lookup_.reserve(capacity_);
    }
    ProbeCache(const ProbeCache&)=delete;
    ProbeCache& operator=(const ProbeCache&)=delete;
    ProbeCache(ProbeCache&&)=delete;
    ProbeCache& operator=(ProbeCache&&)=delete;

    bool get(ProbeGridKey key,Probe& output) {
        auto found=lookup_.find(key);if(found==lookup_.end())return false;
        entries_.splice(entries_.begin(),entries_,found->second);
        output=found->second->value;return true;
    }
    void put(ProbeGridKey key,const Probe& value) {
        auto found=lookup_.find(key);
        if(found!=lookup_.end()) {
            found->second->value=value;
            entries_.splice(entries_.begin(),entries_,found->second);return;
        }
        entries_.push_front({key,value});
        try{lookup_.emplace(key,entries_.begin());}
        catch(...){entries_.pop_front();throw;}
        if(entries_.size()>capacity_) {
            lookup_.erase(entries_.back().key);entries_.pop_back();
        }
    }
    // Membership without touching LRU order (GIThreads prefetch; 0.3.138).
    bool contains(ProbeGridKey key)const{return lookup_.count(key)!=0;}
    void clear(){lookup_.clear();entries_.clear();}
    size_t size()const{return entries_.size();}
    size_t capacity()const{return capacity_;}
    void accountMemory(NorthlightMemoryFootprint::Counter& memory)const{
        using namespace NorthlightMemoryFootprint;
        if(!memory.visit(this))return;
        memory.add(Metadata,sizeof(*this));
        for(const auto& entry:entries_)memory.allocation(Probes,&entry,sizeof(entry));
        for(const auto& entry:lookup_)memory.allocation(Metadata,&entry,sizeof(entry));
        // Node links and the implementation's bucket array are allocator/STL
        // overhead; their sizes are deliberately not claimed as measured bytes.
    }
    // Export persistent world slots, independent of the current solve window.
    // Most recently touched entry wins a modulo collision. Consumers MUST
    // compare the full key before sampling; matching modulo index is not enough.
    // Occupancy and probe validity differ: a solved invalid probe occupies its
    // slot and must prevent an older colliding valid probe from leaking through.
    std::vector<ProbeAtlasEntry> atlas()const {
        std::vector<ProbeAtlasEntry> result(probeLayout().atlasSize());
        for(const auto& entry:entries_) {
            auto& slot=result[probeAtlasIndex(entry.key)];
            if(slot.occupied)continue;
            slot.key=entry.key;slot.probe=entry.value;slot.occupied=true;
        }
        return result;
    }
};
// Fixed world slots retain transport across colour changes. Exact geometry /
// direction / light-layout changes invalidate them; there is no brightness
// quantization. Bounded storage and per-access locking also cover GIThreads>1.
class ProbeRelightCache {
    struct Entry {ProbeGridKey key;ProbeResponse response;uint64_t epoch=0;uint32_t seed=0;};
    std::vector<Entry> entries_;
    Lighting shape_;unsigned rays_=0,mask_=0;uint64_t epoch_=1;
    bool ready_=false,allocationTried_=false;
    mutable std::mutex mutex_;
    static bool same(Vec3 a,Vec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
    bool sameShape(const Lighting& l,unsigned rays,unsigned mask)const{
        if(rays_!=rays||mask_!=mask||!same(l.sunDirection,shape_.sunDirection)||!same(l.worldUp,shape_.worldUp)||
           l.maxDistance!=shape_.maxDistance||l.rayBias!=shape_.rayBias||l.maxBounces!=shape_.maxBounces||
           l.additionalDirections.size()!=shape_.additionalDirections.size()||l.points.size()!=shape_.points.size())return false;
        for(size_t i=0;i<l.additionalDirections.size();++i)if(!same(l.additionalDirections[i].direction,shape_.additionalDirections[i].direction))return false;
        for(size_t i=0;i<l.points.size();++i){const auto& a=l.points[i];const auto& b=shape_.points[i];
            if(!same(a.position,b.position)||!same(a.irradiance,b.irradiance)||a.attenuationStart!=b.attenuationStart||a.attenuationEnd!=b.attenuationEnd)return false;}
        return true;
    }
public:
    static constexpr size_t BudgetBytes=4u*1024u*1024u;
    struct Stats {uint64_t hits=0,misses=0,resets=0;size_t bytes=0;};
private:
    Stats stats_;
public:
    void reset(){std::lock_guard<std::mutex> lock(mutex_);ready_=false;++epoch_;}
    // Frees the slots (memory trim, map change); the next configure() allocates them again.
    void release(){std::lock_guard<std::mutex> lock(mutex_);std::vector<Entry>().swap(entries_);ready_=allocationTried_=false;++epoch_;stats_.bytes=0;}
    // Called between solve groups, never while a pool job is running.
    void configure(const PreparedLighting& prepared,unsigned rays){
        std::lock_guard<std::mutex> lock(mutex_);
        if(!prepared.valid()||prepared.values().additionalDirections.size()>1){ready_=false;return;}
        const auto& l=prepared.values();
        const unsigned mask=(dot(l.sunIrradiance,l.sunIrradiance)>=1e-16f?1u:0u)|
            (!l.additionalDirections.empty()&&dot(l.additionalDirections[0].irradiance,l.additionalDirections[0].irradiance)>=1e-16f?2u:0u);
        if(ready_&&sameShape(l,rays,mask))return;
        ready_=false;
        try{
            if(!allocationTried_){allocationTried_=true;entries_.resize(std::min(probeLayout().atlasSize(),BudgetBytes/sizeof(Entry)));}
            if(entries_.empty())return;
            shape_=l;rays_=rays;mask_=mask;++epoch_;++stats_.resets;stats_.bytes=entries_.capacity()*sizeof(Entry);ready_=true;
        }catch(...){ready_=false;} // optional cache: allocation failure keeps the ordinary solver
    }
    Probe solve(const BVH& bvh,ProbeGridKey key,Vec3 position,const PreparedLighting& prepared,unsigned rays,uint32_t seed){
        bool cached=false;
        {std::lock_guard<std::mutex> lock(mutex_);cached=ready_;
         if(cached){const auto& e=entries_[probeAtlasIndex(key)%entries_.size()];
             if(e.epoch==epoch_&&e.key==key&&e.seed==seed&&same(e.response.fixed.position,position)){
                 ++stats_.hits;return relightProbe(e.response,prepared);
             }}++stats_.misses;}
        if(!cached)return solveProbePrepared(bvh,position,prepared,rays,seed);
        ProbeResponse response;
        Probe result=solveProbeResponse(bvh,position,prepared,rays,seed,response);
        {std::lock_guard<std::mutex> lock(mutex_);
         auto& e=entries_[probeAtlasIndex(key)%entries_.size()];e.key=key;e.seed=seed;e.response=response;e.epoch=epoch_;}
        return result;
    }
    void accountMemory(NorthlightMemoryFootprint::Counter& memory)const{
        using namespace NorthlightMemoryFootprint;
        std::lock_guard<std::mutex> lock(mutex_);
        if(!memory.visit(this))return;
        memory.add(Metadata,sizeof(*this));memory.vector(Probes,entries_);
        memory.vector(Metadata,shape_.points);memory.vector(Metadata,shape_.additionalDirections);
    }
    Stats stats()const{std::lock_guard<std::mutex> lock(mutex_);return stats_;}
};
} // namespace NorthlightGI
