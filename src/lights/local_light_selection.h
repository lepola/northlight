#pragma once
#include "world_local_lights.h"
#include "local_light_scissor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace NorthlightLocalLightSelection {
// Limit: the default selection (0.3.136..0.3.139). Capacity: the most a user may select
// (northlight-quality.ini LocalLightLimit up to 128); extra lights only add 8-/4-light batches.
inline constexpr unsigned Limit=32,Capacity=128,DirectBatchSize=8,FogBatchSize=4;
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
inline constexpr unsigned Spare=8,Slots=Capacity+Spare;       // 136: whole 8-/4-light batches
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
// Full direct-light batches borrow the already contiguous shader columns.
// A partial batch owns zero-padded columns; no fog payload is copied.
template<unsigned N> struct DirectBatchView {
    std::array<Constant,N> paddedPosition,paddedColor;
    const Constant* position;const Constant* color;unsigned count;
    DirectBatchView(const std::array<Constant,Slots>& p,const std::array<Constant,Slots>& c,unsigned available,unsigned first){
        available=std::min(available,Slots);count=first<available?std::min(N,available-first):0;
        if(count==N){position=p.data()+first;color=c.data()+first;}
        else{paddedPosition.fill({});paddedColor.fill({});std::copy_n(p.data()+std::min(first,available),count,paddedPosition.begin());std::copy_n(c.data()+std::min(first,available),count,paddedColor.begin());position=paddedPosition.data();color=paddedColor.data();}
    }
    DirectBatchView(const DirectBatchView&)=delete;DirectBatchView& operator=(const DirectBatchView&)=delete;
};
struct Selection {
    std::array<NorthlightLocalLights::Light,Slots> sources{}; // Same source, colour and fade as the direct-light pass.
    std::array<Constant,Slots> position{},color{},fog{}; // 0.3.197: Slots, select() fills at most Capacity
    unsigned count=0;float nearest=0,fogDistance=128.f;
    template<unsigned N> DirectBatchView<N> directBatch(unsigned first)const{return DirectBatchView<N>(position,color,count,first);}
    template<unsigned N> Batch<N> batch(unsigned first)const{
        Batch<N> b;
        for(unsigned i=0;i<N&&first+i<count;++i){
            b.position[i]=position[first+i];b.color[i]=color[first+i];b.fog[i]=fog[first+i];++b.count;
        }
        return b; // All unused shader slots must be zero, including partial batches.
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
        const float gain=visibilityGain(picks[i].score);
        // Complete the selected influence sphere even for scaled bonfires.
        out.fogDistance=std::max(out.fogDistance,picks[i].score+2*l.attenuationEnd);
        out.color[i]={l.diffuse[0]*gain,l.diffuse[1]*gain,l.diffuse[2]*gain,1.f/std::max(l.attenuationEnd-std::min(l.attenuationStart,l.attenuationEnd*.9f),.05f)};
        out.sources[i]=l;for(unsigned k=0;k<3;++k)out.sources[i].diffuse[k]=out.color[i][k];
    }
    return out;
}
// Off-screen preference (view given): a light whose attenuation sphere lies outside the view pyramid lights no pixel this
// frame. Over the cap it ranks after every light in view (OffscreenPenalty on its biased score), so the slots go to lights the
// camera can see; under the cap nothing changes. The test is the sphere against the pyramid's four side planes (they meet at
// the eye, so a light behind the camera fails them), each plane pushed out by the light's distance times the margin sine:
// lights within about 20 degrees of the screen edge count as in view (they are lit before they enter it), incumbents within 30
// (no flip at the edge). Conservative (the plane test keeps the pyramid's edges and corners). An invalid view counts every
// light in view.
inline constexpr float OffscreenPenalty=2*VisibilityEnd;
inline constexpr float ViewMarginSin=.34f,IncumbentMarginSin=.5f; // about 20 and 30 degrees
inline bool inView(const NorthlightLocalLightScissor::View& v,const NorthlightLocalLights::Light& l,float marginSin,const double* planes=nullptr){
    if(!v.valid)return true;
    double d[3],c[3]={};
    for(int i=0;i<3;++i){if(!std::isfinite(l.position[i]))return true;d[i]=double(l.position[i])-v.eye[i];}
    for(int i=0;i<3;++i)for(int k=0;k<3;++k)c[i]+=v.inverse[i][k]*d[k];  // (x, y, forward) as the scissor reconstructs them
    const double len=std::sqrt(c[0]*c[0]+c[1]*c[1]+c[2]*c[2]),r=double(l.attenuationEnd)*v.stretch+len*marginSin;
    if(!std::isfinite(r))return true;
    const double px=planes?planes[0]:1/std::fabs(v.projX),py=planes?planes[1]:1/std::fabs(v.projY),nx=planes?planes[2]:1/std::sqrt(1+px*px),ny=planes?planes[3]:1/std::sqrt(1+py*py);
    return (c[0]-px*c[2])*nx<=r&&(-c[0]-px*c[2])*nx<=r&&(c[1]-py*c[2])*ny<=r&&(-c[1]-py*c[2])*ny<=r;
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
    // A newly published static lamp replaces a dynamic observation immediately;
    // retaining its fading tail would add the same physical source twice.
    void forget(std::uint64_t id){
        const auto end=entries.begin()+count;
        const auto at=std::lower_bound(entries.begin(),end,id,[](const Entry& e,std::uint64_t key){return e.id<key;});
        if(at!=end&&at->id==id){std::move(at+1,end,at);--count;}
    }
    // Both lists are id-sorted; consume one entry per id, including duplicates.
    void forgetSorted(const std::uint64_t* ids,unsigned size){
        if(!ids||!size||!count)return;
        unsigned next=0,write=0;
        for(unsigned read=0;read<count;++read){
            while(next<size&&ids[next]<entries[read].id)++next;
            if(next<size&&ids[next]==entries[read].id){++next;continue;}
            if(write!=read)entries[write]=entries[read];++write;
        }
        count=write;
    }
    bool incumbent(std::uint64_t id)const{
        unsigned lo=0,hi=count;
        while(lo<hi){const unsigned mid=(lo+hi)/2;if(entries[mid].id<id)lo=mid+1;else hi=mid;}
        return lo<count&&entries[lo].id==id&&entries[lo].selected;
    }
    unsigned offscreen=0;                               // lights in the last update's candidates ranked off-screen (view given)
    Selection update(const std::vector<NorthlightLocalLights::Light>& lights,const float* camera,unsigned limit,float dtSeconds,bool continuous,const NorthlightLocalLightScissor::View* view=nullptr,const NorthlightLocalLights::Light* additional=nullptr,unsigned additionalCount=0,const std::uint64_t* immediate=nullptr,unsigned immediateCount=0,uint32_t sourceTime=0){
        offscreen=0;
        limit=std::min(limit,Capacity);
        if(!limit)reset();
        Selection out;fading=0;if(!limit||(!count&&lights.empty()&&(!additional||!additionalCount)))return out;
        const float dt=dtSeconds>0.f?std::min(dtSeconds,MaxGapSeconds):0.f,step=dt/FadeSeconds;
        struct Pick {float score,biased;const NorthlightLocalLights::Light* light;};
        const auto better=[](const Pick& a,const Pick& b){return a.biased!=b.biased?a.biased<b.biased:a.light->sourceId<b.light->sourceId;};
        std::array<Pick,Capacity+1> heap;unsigned n=0;const unsigned cap=limit+1; // best limit+1 by (biased,id); heap[0] is the worst
        double planes[4];bool planesReady=false;
        const auto consider=[&](const NorthlightLocalLights::Light& l){
            float d2=0;for(unsigned i=0;i<3;++i){const float d=l.position[i]-camera[i];d2+=d*d;}
            const float reach=VisibilityEnd+l.attenuationEnd;
            if(d2>=reach*reach*(1+1e-5f))return; // beyond VisibilityEnd with margin: no sqrt, no valid()
            if(!NorthlightLocalLights::valid(l)||l.attenuationEnd<=.11f)return;
            const float score=std::sqrt(d2)-l.attenuationEnd;if(!(score<VisibilityEnd))return;
            if(n==cap&&!better(Pick{score,score-StickyBias,&l},heap[0]))return; // not even as an incumbent in view
            const bool inc=count&&incumbent(l.sourceId);
            float biased=score-(inc?StickyBias:0.f);
            if(view&&view->valid){
              if(!planesReady){
                planes[0]=1/std::fabs(view->projX);planes[1]=1/std::fabs(view->projY);
                planes[2]=1/std::sqrt(1+planes[0]*planes[0]);planes[3]=1/std::sqrt(1+planes[1]*planes[1]);planesReady=true;
              }
              if(!inView(*view,l,inc?IncumbentMarginSin:ViewMarginSin,planes)){biased+=OffscreenPenalty;++offscreen;
                if(n==cap&&!better(Pick{score,biased,&l},heap[0]))return;}
            }
            const Pick p{score,biased,&l};
            if(n<cap){heap[n++]=p;std::push_heap(heap.begin(),heap.begin()+n,better);}
            else if(better(p,heap[0])){std::pop_heap(heap.begin(),heap.begin()+cap,better);heap[cap-1]=p;std::push_heap(heap.begin(),heap.begin()+cap,better);}
        };
        for(const auto& l:lights)consider(l);
        for(unsigned i=0;additional&&i<additionalCount;++i)consider(additional[i]);
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
            const float shown=e.shown-step;
            if(!continuous||!(shown>1e-6f))return;
            float d2=0;for(unsigned i=0;i<3;++i){const float d=e.light.position[i]-camera[i];d2+=d*d;}
            const float score=std::sqrt(d2)-e.light.attenuationEnd;
            if(score<VisibilityEnd)fade[fades++]={index,score,shown};}; // 1e-6: float residue of 1-k*step is gone, not fading
        unsigned old=0;
        for(unsigned i=0;i<selected;++i){
            while(old<count&&entries[old].id<sel[i].id)leaving(old++);
            const float target=sel[i].shown;
            const bool known=old<count&&entries[old].id==sel[i].id;const float prev=known?entries[old++].shown:0.f;
            sel[i].shown=!continuous||std::fabs(target-prev)<=step?target:target>prev?prev+step:prev-step;
            // Current particle colour already includes its authored alpha and
            // lifetime. A short burst must not wait through the lamp fade-in.
            if(immediate&&std::find(immediate,immediate+immediateCount,sel[i].id)!=immediate+immediateCount)sel[i].shown=target;
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
            gain*=NorthlightLocalLights::pulseGain(l,sourceTime);
            out.position[slot]={l.position[0],l.position[1],l.position[2],l.attenuationEnd};
            out.fogDistance=std::max(out.fogDistance,score+2*l.attenuationEnd);
            out.color[slot]={l.diffuse[0]*gain,l.diffuse[1]*gain,l.diffuse[2]*gain,1.f/std::max(l.attenuationEnd-std::min(l.attenuationStart,l.attenuationEnd*.9f),.05f)};
            out.sources[slot]=l;for(unsigned k=0;k<3;++k)out.sources[slot].diffuse[k]=out.color[slot][k];};
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
