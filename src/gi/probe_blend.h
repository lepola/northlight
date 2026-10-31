#pragma once
#include "world_probe_cache.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

// 0.3.197: same-key probe re-publication blends on the GPU from what was on screen (task 13).
// The activation fade (probe_activation.h) only covers a key's first residency; when the GI worker
// re-solves a resident probe the renderer would otherwise switch its SH in one frame. The mirror
// tracks the SH last published per atlas slot and the SH that was on screen at that moment.
namespace NorthlightProbeBlend {
inline constexpr float BlendSeconds=.3f; // shaders/world_effects.hlsl probeIrradiance uses (1/.3)
inline constexpr float None=-1000.f;     // blend start meaning "no blend": saturate((now-None)/BlendSeconds)==1
// Fixed 48-byte SH equality, including the exact bit patterns of NaNs and signed zeros.
inline bool sameCoefficients(const void* a,const void* b){
#if defined(__SSE2__)
    const auto* x=static_cast<const char*>(a);const auto* y=static_cast<const char*>(b);
    const __m128i a0=_mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(x)),_mm_loadu_si128(reinterpret_cast<const __m128i*>(y)));
    const __m128i a1=_mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(x+16)),_mm_loadu_si128(reinterpret_cast<const __m128i*>(y+16)));
    const __m128i a2=_mm_cmpeq_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(x+32)),_mm_loadu_si128(reinterpret_cast<const __m128i*>(y+32)));
    return _mm_movemask_epi8(_mm_and_si128(_mm_and_si128(a0,a1),a2))==0xffff;
#else
    return !std::memcmp(a,b,12*sizeof(float));
#endif
}
class Mirror {
    struct Slot {NorthlightGI::ProbeGridKey key;bool valid=false;NorthlightGI::Vec3 cur[4];float prev[12]={};float start=None;};
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
            static_assert(sizeof slot.cur==12*sizeof(float),"SH comparison excludes padding");
            // Resident unchanged probes need no transpose to the GPU channel order.
            const bool resident=slot.valid&&entry.occupied&&entry.probe.valid&&slot.key==entry.key;
            if(resident&&sameCoefficients(slot.cur,entry.probe.sh))continue;
            // Empty default slots need no transpose when both cached copies are also all zero.
            static constexpr float zero[12]={};
            if(!entry.occupied&&!slot.valid&&slot.start==None&&sameCoefficients(slot.cur,zero)&&sameCoefficients(entry.probe.sh,zero)&&sameCoefficients(slot.prev,zero))continue;
            float next[12];
            for(int k=0;k<4;++k){next[k]=entry.probe.sh[k].x;next[4+k]=entry.probe.sh[k].y;next[8+k]=entry.probe.sh[k].z;}
            // An unchanged invalid slot already has both exact cached copies.
            if((!entry.occupied||!entry.probe.valid)&&!slot.valid&&slot.start==None&&sameCoefficients(slot.cur,entry.probe.sh)&&sameCoefficients(slot.prev,next))continue;
            if(!entry.occupied||!entry.probe.valid){slot.valid=false;slot.start=None;std::memcpy(slot.cur,entry.probe.sh,sizeof slot.cur);std::memcpy(slot.prev,next,sizeof next);continue;}
            if(!resident){slot.key=entry.key;slot.valid=true;std::memcpy(slot.cur,entry.probe.sh,sizeof slot.cur);std::memcpy(slot.prev,next,sizeof next);slot.start=None;continue;}
            const float t=slot.start==None?1.f:std::clamp((now-slot.start)/BlendSeconds,0.f,1.f);
            for(int k=0;k<4;++k){
                slot.prev[k]=slot.prev[k]+(slot.cur[k].x-slot.prev[k])*t;
                slot.prev[4+k]=slot.prev[4+k]+(slot.cur[k].y-slot.prev[4+k])*t;
                slot.prev[8+k]=slot.prev[8+k]+(slot.cur[k].z-slot.prev[8+k])*t;
            }
            std::memcpy(slot.cur,entry.probe.sh,sizeof slot.cur);slot.start=now;++blended;
        }
        return blended;
    }
    // SH [R0..R3,G0..G3,B0..B3] that was on screen when the slot's current value was published.
    const float* previous(size_t index)const{return slots.at(index).prev;}
    // Call only after covers(index+1) is established.
    const float* previousKnownValid(size_t index)const{return slots[index].prev;}
    float start(size_t index)const{return slots.at(index).start;}
    bool covers(size_t count)const{return slots.size()>=count;}
    // Call only after covers(index+1) is established.
    float startKnownValid(size_t index)const{return slots[index].start;}
};
}
