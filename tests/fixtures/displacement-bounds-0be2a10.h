#pragma once
// Frozen bounds-cache logic from 0be2a10, before intrusive eviction.
// Original auto_displacement_renderer.h SHA-256: e150ea8990746b12e2645f9a4ede692d29a9742dbc9cf1862b4ce876c1d9d481
// Only unrelated shader/device fields and methods are omitted. Bound layout,
// admission, weak-owner proof, timestamps and eviction logic are verbatim.
class NorthlightAutoDisplacement {
    friend struct DisplacementStateTestAccess;
    using Mesh=NorthlightDrawSnapshot::Mesh;
    struct Bounds {std::weak_ptr<const Mesh> source;float low[3],high[3];uint64_t used=0;size_t bytes=0;std::unique_ptr<NorthlightReplayBounds::SkinEnvelope> skin;};
    std::unordered_map<const Mesh*,Bounds> bounds;
    size_t boundsBytes=0;uint64_t clock=0,boundsHits=0,boundsMisses=0;
    auto cachedBounds(const std::shared_ptr<const Mesh>& source){
        auto found=bounds.find(source.get());
        // Weak ownership avoids charging/retaining full geometry for a tiny bound.
        // Control-block identity still rejects expired snapshots and address reuse.
        if(found!=bounds.end()&&(found->second.source.owner_before(source)||source.owner_before(found->second.source))){boundsBytes-=found->second.bytes;bounds.erase(found);found=bounds.end();}
        if(found==bounds.end())++boundsMisses;else ++boundsHits;return found;
    }
    auto retainBounds(const std::shared_ptr<const Mesh>& source,Bounds&& value){
        value.source=source;value.used=++clock;
        value.bytes=sizeof(Bounds)+4*sizeof(void*)+(value.skin?sizeof(NorthlightReplayBounds::SkinEnvelope):0);
        // A city frame can reference more than 1024 tiny bounds. Keep the byte
        // cap, but avoid rescanning that working set when only 64 KiB is used.
        while(!bounds.empty()&&(boundsBytes+value.bytes>(8u<<20)||bounds.size()>=4096)){
            auto old=std::min_element(bounds.begin(),bounds.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});boundsBytes-=old->second.bytes;bounds.erase(old);}
        auto added=bounds.emplace(source.get(),std::move(value));if(added.second)boundsBytes+=added.first->second.bytes;return added.first;
    }

public:
    explicit NorthlightAutoDisplacement(IDirect3DDevice9*){}
    void reset(){bounds.clear();boundsBytes=0;}
};
