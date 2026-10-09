#pragma once
#include "world_local_lights.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace NorthlightLocalLightSelection {
// Limit: the default selection (0.3.136..0.3.139). Capacity: the most a user may select
// (northlight-quality.ini LocalLightLimit up to 64); extra lights only add 8-/4-light batches.
inline constexpr unsigned Limit=32,Capacity=64,DirectBatchSize=8,FogBatchSize=4;
static_assert(Capacity%DirectBatchSize==0&&Capacity%FogBatchSize==0&&Limit<=Capacity,"whole batches");
// Viewer distance to the influence sphere, not the bulb. Keep nearby energy
// unchanged; introduce distant lights with a smooth ramp instead of a pop.
inline constexpr float VisibilityFull=192.f,VisibilityEnd=224.f;
inline float visibilityGain(float reach){
    const float t=std::clamp((VisibilityEnd-reach)/(VisibilityEnd-VisibilityFull),0.f,1.f);
    return t*t*(3.f-2.f*t);
}
using Constant=std::array<float,4>;
// 0.3.197: soft cap, incumbent bias and time-based fades (task 13).
inline constexpr unsigned Spare=8,Slots=Capacity+Spare;       // 72: whole 8-/4-light batches
static_assert(Slots%DirectBatchSize==0&&Slots%FogBatchSize==0,"whole batches");
inline constexpr float FadeSeconds=.4f;    // full 0<->1 change
inline constexpr float StickyBias=4.f;     // world units subtracted from an incumbent's score
inline constexpr float MinBandWidth=1.f;   // score width guard for exact ties
inline constexpr float MaxGapSeconds=.25f; // a longer gap (F10 off, loading, hitch) snaps the factors to their targets
inline unsigned capBand(unsigned limit){return std::max(1u,limit/4);}  // 32->8, 64->16, 8->2
inline float nightGain(float night){return 1.f-.20f*std::clamp(night,0.f,1.f);}
// Lamps in the sun (30%, no key): LocalDirect lamp light keeps SunlitGain where the
// sun shines directly on the receiver; sun shadow, interiors and night are unchanged. sunlitCut
// is the share removed in full sun (c52.z), faded with the sun's source weight (its elevation
// visibility), and 0 unless the first lighting pass is an active sun with real shadows, so the
// moon's visibility (a moon-first pass) never dims lamps. Lamp fog and the point lamp are unchanged.
inline constexpr float SunlitGain=.3f;
inline float sunlitCut(float sunWeight,bool sunVisibilityInBaseline){
    return sunVisibilityInBaseline&&std::isfinite(sunWeight)?(1-SunlitGain)*std::clamp(sunWeight,0.f,1.f):0.f;}
template<unsigned N> struct Batch {
    std::array<Constant,N> position{},color{},fog{};
    unsigned count=0;
};
struct Selection {
    std::array<Constant,Slots> position{},color{},fog{}; // 0.3.197: Slots, select() fills at most Capacity
    std::array<std::uint64_t,Slots> id{};                // 0.3.205 (gh#20): the source id of each slot, for FogRegroup
    unsigned count=0;float nearest=0,fogDistance=128.f;
    template<unsigned N> Batch<N> batch(unsigned first)const{
        Batch<N> b;
        for(unsigned i=0;i<N&&first+i<count;++i){
            b.position[i]=position[first+i];b.color[i]=color[first+i];b.fog[i]=fog[first+i];++b.count;
        }
        return b; // All unused shader slots must be zero, including partial batches.
    }
};
// 0.3.205 (gh#20): counts the fog batches whose set of lights differs from the previous frame's. The batches come from the closest-first order, so a lamp
// swapping across a batch boundary or a light entering the selection regroups them; the per-batch glow cap made that visible as flicker (the "LOCAL fog regroup"
// log line). Order inside a batch does not matter to the glow, so a batch is the sum of a mix of its ids; fixed arrays, no allocation.
struct FogRegroup {
    std::array<std::uint64_t,Slots/FogBatchSize> sig{};unsigned batches=0;bool known=false;
    static std::uint64_t mix(std::uint64_t x){x+=0x9e3779b97f4a7c15ull;x=(x^(x>>30))*0xbf58476d1ce4e5b9ull;x=(x^(x>>27))*0x94d049bb133111ebull;return x^(x>>31);}
    unsigned update(const Selection& s){
        const unsigned n=(s.count+FogBatchSize-1)/FogBatchSize;unsigned changed=0;
        for(unsigned b=0;b<n;++b){
            std::uint64_t h=0;for(unsigned i=b*FogBatchSize;i<s.count&&i<(b+1)*FogBatchSize;++i)h+=mix(s.id[i]);
            if(known&&(b>=batches||sig[b]!=h))++changed;
            sig[b]=h;
        }
        if(known&&n<batches)changed+=batches-n;
        batches=n;known=true;return changed;
    }
};
// `limit` (northlight-quality.ini LocalLightLimit, clamped to Capacity) keeps the closest lights;
// Limit (32) selects exactly what 0.3.136..0.3.139 did.
inline Selection select(const std::vector<NorthlightLocalLights::Light>& lights,const float* camera,unsigned limit=Limit){
    limit=std::min(limit,Capacity);
    struct Pick {float score;const NorthlightLocalLights::Light* light;};
    const auto before=[](const Pick& a,const Pick& b){return a.score!=b.score?a.score<b.score:a.light->sourceId<b.light->sourceId;};
    std::array<Pick,Capacity> picks{};unsigned count=0;
    for(const auto& l:lights){
        if(!NorthlightLocalLights::valid(l)||l.attenuationEnd<=.11f)continue;
        float d2=0;for(unsigned i=0;i<3;++i){const float d=l.position[i]-camera[i];d2+=d*d;}
        Pick p{std::sqrt(d2)-l.attenuationEnd,&l};if(p.score>=VisibilityEnd)continue;
        if(count<limit)picks[count++]=p;
        else if(limit){auto worst=std::max_element(picks.begin(),picks.begin()+limit,before);if(before(p,*worst))*worst=p;}
    }
    // Deterministic closest-first batches, including equal-distance ties.
    std::sort(picks.begin(),picks.begin()+count,before);
    Selection out;out.count=count;if(count)out.nearest=picks[0].score;
    for(unsigned i=0;i<count;++i){const auto& l=*picks[i].light;
        out.position[i]={l.position[0],l.position[1],l.position[2],l.attenuationEnd};
        out.id[i]=l.sourceId;
        const float gain=visibilityGain(picks[i].score);
        // Complete the selected influence sphere even for scaled bonfires.
        out.fogDistance=std::max(out.fogDistance,picks[i].score+2*l.attenuationEnd);
        out.color[i]={l.diffuse[0]*gain,l.diffuse[1]*gain,l.diffuse[2]*gain,1.f/std::max(l.attenuationEnd-std::min(l.attenuationStart,l.attenuationEnd*.9f),.05f)};
    }
    return out;
}
// 0.3.197: stateful layer over the same scoring as select(). Under the cap every light's factor is exactly
// select()'s; over it the factor eases to 0 across the last capBand(limit) ranks (incumbents get StickyBias)
// and every factor moves at most dt/FadeSeconds per update, keyed by sourceId. Lights that lose a slot stay
// at the tail of the output (at most Spare) until their factor reaches 0, so the 8-/4-light batches of the
// selected lights never change. No heap allocation: fixed arrays only.
// continuous=false snaps every factor to its target and drops the fading lights, but keeps which lights were
// selected (their StickyBias shapes the band: forgetting it on a hitch would drop the lamps at the cap at once).
// reset() forgets everything (map change, rebuilt device).
struct Tracker {
    struct Entry {std::uint64_t id;float shown;bool selected;NorthlightLocalLights::Light light;};
    std::array<Entry,Slots> entries{};unsigned count=0; // sorted by id
    unsigned fading=0;                                  // fading-out lights in the last update's output
    void reset(){count=0;fading=0;}
    bool incumbent(std::uint64_t id)const{
        unsigned lo=0,hi=count;
        while(lo<hi){const unsigned mid=(lo+hi)/2;if(entries[mid].id<id)lo=mid+1;else hi=mid;}
        return lo<count&&entries[lo].id==id&&entries[lo].selected;
    }
    Selection update(const std::vector<NorthlightLocalLights::Light>& lights,const float* camera,unsigned limit,float dtSeconds,bool continuous){
        limit=std::min(limit,Capacity);
        if(!limit)reset();
        Selection out;fading=0;if(!limit)return out;
        const float dt=dtSeconds>0.f?std::min(dtSeconds,MaxGapSeconds):0.f,step=dt/FadeSeconds;
        struct Pick {float score,biased;const NorthlightLocalLights::Light* light;};
        const auto better=[](const Pick& a,const Pick& b){return a.biased!=b.biased?a.biased<b.biased:a.light->sourceId<b.light->sourceId;};
        std::array<Pick,Capacity+1> heap;unsigned n=0;const unsigned cap=limit+1; // best limit+1 by (biased,id); heap[0] is the worst
        for(const auto& l:lights){
            float d2=0;for(unsigned i=0;i<3;++i){const float d=l.position[i]-camera[i];d2+=d*d;}
            const float reach=VisibilityEnd+l.attenuationEnd;
            if(d2>=reach*reach*(1+1e-5f))continue; // beyond VisibilityEnd with margin: no sqrt, no valid()
            if(!NorthlightLocalLights::valid(l)||l.attenuationEnd<=.11f)continue;
            const float score=std::sqrt(d2)-l.attenuationEnd;if(!(score<VisibilityEnd))continue;
            if(n==cap&&!better(Pick{score,score-StickyBias,&l},heap[0]))continue; // not even as an incumbent
            const bool inc=count&&incumbent(l.sourceId);
            const Pick p{score,score-(inc?StickyBias:0.f),&l};
            if(n<cap){heap[n++]=p;std::push_heap(heap.begin(),heap.begin()+n,better);}
            else if(better(p,heap[0])){std::pop_heap(heap.begin(),heap.begin()+cap,better);heap[cap-1]=p;std::push_heap(heap.begin(),heap.begin()+cap,better);}
        }
        std::sort_heap(heap.begin(),heap.begin()+n,better);
        const bool saturated=n>limit;const unsigned selected=saturated?limit:n;
        float edgeBiased=0,edgeGain=0,width=1;
        if(saturated){edgeBiased=heap[limit].biased;edgeGain=visibilityGain(heap[limit].score);
            width=std::max(edgeBiased-heap[limit-capBand(limit)].biased,MinBandWidth);}
        struct Sel {std::uint64_t id;const NorthlightLocalLights::Light* light;float score,shown;};
        std::array<Sel,Capacity> sel;
        for(unsigned i=0;i<selected;++i){const Pick& p=heap[i];
            float target=visibilityGain(p.score);
            if(saturated){const float t=std::clamp((edgeBiased-p.biased)/width,0.f,1.f),band=t*t*(3.f-2.f*t);target*=1+(band-1)*edgeGain;}
            sel[i]={p.light->sourceId,p.light,p.score,target};
        }
        std::sort(sel.begin(),sel.begin()+selected,[](const Sel& a,const Sel& b){return a.id<b.id;});
        // Walk the previous entries (id order) alongside: new factors for the selected, fade candidates for the rest.
        struct Fade {unsigned index;float score,shown;};
        std::array<Fade,Slots> fade;unsigned fades=0;
        const auto leaving=[&](unsigned index){const Entry& e=entries[index];
            float d2=0;for(unsigned i=0;i<3;++i){const float d=e.light.position[i]-camera[i];d2+=d*d;}
            const float score=std::sqrt(d2)-e.light.attenuationEnd,shown=e.shown-step;
            if(continuous&&score<VisibilityEnd&&shown>1e-6f)fade[fades++]={index,score,shown};}; // 1e-6: float residue of 1-k*step is gone, not fading
        unsigned old=0;
        for(unsigned i=0;i<selected;++i){
            while(old<count&&entries[old].id<sel[i].id)leaving(old++);
            const float target=sel[i].shown;
            const bool known=old<count&&entries[old].id==sel[i].id;const float prev=known?entries[old++].shown:0.f;
            sel[i].shown=!continuous||std::fabs(target-prev)<=step?target:target>prev?prev+step:prev-step;
        }
        while(old<count)leaving(old++);
        if(fades>Spare){ // keep the brightest, then restore id order
            std::sort(fade.begin(),fade.begin()+fades,[&](const Fade& a,const Fade& b){return a.shown!=b.shown?a.shown>b.shown:entries[a.index].id<entries[b.index].id;});
            fades=Spare;
            std::sort(fade.begin(),fade.begin()+fades,[](const Fade& a,const Fade& b){return a.index<b.index;});
        }
        // Output: selected closest-first (like select()), then the fading lights, so batches of the selected never move.
        std::array<unsigned char,Capacity> order;for(unsigned i=0;i<selected;++i)order[i]=(unsigned char)i;
        std::sort(order.begin(),order.begin()+selected,[&](unsigned char a,unsigned char b){return sel[a].score!=sel[b].score?sel[a].score<sel[b].score:sel[a].id<sel[b].id;});
        std::array<unsigned char,Spare> tail;for(unsigned i=0;i<fades;++i)tail[i]=(unsigned char)i;
        std::sort(tail.begin(),tail.begin()+fades,[&](unsigned char a,unsigned char b){return fade[a].score!=fade[b].score?fade[a].score<fade[b].score:entries[fade[a].index].id<entries[fade[b].index].id;});
        out.count=selected+fades;
        const auto emit=[&](unsigned slot,const NorthlightLocalLights::Light& l,float score,float gain){
            out.position[slot]={l.position[0],l.position[1],l.position[2],l.attenuationEnd};
            out.id[slot]=l.sourceId;
            out.fogDistance=std::max(out.fogDistance,score+2*l.attenuationEnd);
            out.color[slot]={l.diffuse[0]*gain,l.diffuse[1]*gain,l.diffuse[2]*gain,1.f/std::max(l.attenuationEnd-std::min(l.attenuationStart,l.attenuationEnd*.9f),.05f)};};
        for(unsigned i=0;i<selected;++i){const Sel& s=sel[order[i]];emit(i,*s.light,s.score,s.shown);}
        for(unsigned i=0;i<fades;++i){const Fade& f=fade[tail[i]];emit(selected+i,entries[f.index].light,f.score,f.shown);}
        if(out.count)out.nearest=selected?sel[order[0]].score:fade[tail[0]].score;
        // Next state, merged back into id order (both lists are id-sorted).
        Entry next[Slots];unsigned total=0,s=0,f=0;
        while(s<selected||f<fades){
            if(f>=fades||(s<selected&&sel[s].id<entries[fade[f].index].id)){next[total++]={sel[s].id,sel[s].shown,true,*sel[s].light};++s;}
            else{Entry e=entries[fade[f].index];e.shown=fade[f].shown;e.selected=false;next[total++]=e;++f;}
        }
        std::copy(next,next+total,entries.begin());count=total;fading=fades;
        return out;
    }
};
} // namespace NorthlightLocalLightSelection
