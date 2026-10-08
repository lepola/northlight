#pragma once
#include "world_probe_cache.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

// 0.3.197: same-key probe re-publication blends on the GPU from what was on screen (task 13).
// The activation fade (probe_activation.h) only covers a key's first residency; when the GI worker
// re-solves a resident probe the renderer would otherwise switch its SH in one frame. The mirror
// tracks the SH last published per atlas slot and the SH that was on screen at that moment.
namespace NorthlightProbeBlend {
inline constexpr float BlendSeconds=.3f; // shaders/world_effects.hlsl probeIrradiance uses (1/.3)
inline constexpr float None=-1000.f;     // blend start meaning "no blend": saturate((now-None)/BlendSeconds)==1
class Mirror {
    struct Slot {NorthlightGI::ProbeGridKey key;bool valid=false;float cur[12]={},prev[12]={};float start=None;};
    std::vector<Slot> slots;
    std::string map;
public:
    void reset(){slots.assign(NorthlightGI::probeLayout().atlasSize(),Slot{});map.clear();}
    // Sized by the process probe layout (fixed before the first upload), like NorthlightProbeActivation.
    void begin(const std::string& next){if(map!=next||slots.size()!=NorthlightGI::probeLayout().atlasSize()){reset();map=next;}}
    // One pass over the published atlas; returns how many slots started or restarted a blend.
    unsigned publish(const std::vector<NorthlightGI::ProbeAtlasEntry>& atlas,float now){
        if(atlas.size()!=slots.size()){for(auto& slot:slots){slot.valid=false;slot.start=None;}return 0;}
        unsigned blended=0;
        for(size_t i=0;i<slots.size();++i){
            auto& slot=slots[i];const auto& entry=atlas[i];
            float next[12];
            for(int k=0;k<4;++k){next[k]=entry.probe.sh[k].x;next[4+k]=entry.probe.sh[k].y;next[8+k]=entry.probe.sh[k].z;}
            if(!entry.occupied||!entry.probe.valid){slot.valid=false;slot.start=None;std::memcpy(slot.cur,next,sizeof next);std::memcpy(slot.prev,next,sizeof next);continue;}
            if(!slot.valid||!(slot.key==entry.key)){slot.key=entry.key;slot.valid=true;std::memcpy(slot.cur,next,sizeof next);std::memcpy(slot.prev,next,sizeof next);slot.start=None;continue;}
            if(std::memcmp(next,slot.cur,sizeof next)==0)continue;
            const float t=slot.start==None?1.f:std::clamp((now-slot.start)/BlendSeconds,0.f,1.f);
            for(int k=0;k<12;++k)slot.prev[k]=slot.prev[k]+(slot.cur[k]-slot.prev[k])*t;
            std::memcpy(slot.cur,next,sizeof next);slot.start=now;++blended;
        }
        return blended;
    }
    // SH [R0..R3,G0..G3,B0..B3] that was on screen when the slot's current value was published.
    const float* previous(size_t index)const{return slots.at(index).prev;}
    float start(size_t index)const{return slots.at(index).start;}
};
}
