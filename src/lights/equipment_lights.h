#pragma once
#include "world_local_lights.h"
#include "local_light_selection.h"
#include "native_light_snapshot.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NorthlightEquipmentLights {
inline constexpr unsigned Capacity=NorthlightLocalLightSelection::Capacity,MemoBanks=64,MaxReadsPerFrame=64,BackgroundReads=32;
inline constexpr float LifeSeconds=.25f,FadeSeconds=.10f,MaxRadius=NorthlightLocalLights::NativeMaxRadius,MaxDistance=NorthlightLocalLightSelection::VisibilityEnd+MaxRadius;
// Unknown state retains a bounded backend-read budget. Proven mirror state
// costs no backend calls, so early draws cannot starve later sources.
struct CaptureBudget {
    unsigned candidates=0,actorCandidates=0,reads=0,actorReads=0,backgroundReads=0,cached=0,readLimited=0,domainRejected=0,decoded=0,rejected=0;
    void nextFrame(){*this={};}
    bool take(bool actor,bool known){
        ++candidates;if(actor)++actorCandidates;
        if(known){++cached;return true;}
        if(reads>=MaxReadsPerFrame||(!actor&&backgroundReads>=BackgroundReads)){++readLimited;return false;}
        ++reads;if(actor)++actorReads;else ++backgroundReads;return true;
    }
};
// Borrow only getter-proven constants under the draw's existing gate.
template<class Mirror> inline bool cachedBank(const Mirror* mirror){
    if(!mirror||!mirror->heldByThisThread()||!mirror->active()||!mirror->depthKnown)return false;
    for(unsigned i=2;i<6;++i)if(!mirror->vsFloatKnown[i])return false;
    for(unsigned i=17;i<28;++i)if(!mirror->vsFloatKnown[i])return false;
    return true;
}
struct Native {float position[3]{},color[3]{},attenuation[3]{};};
// c17..20 = diffuse, c21..24 = VIEW-space position, c25..27 = the
// constant/linear/quadratic denominator. Layout is admitted only by the proof.
inline bool decode(const float* bank,unsigned slot,const float* inverseView,const float* camera,Native& out){
    if(!bank||!inverseView||!camera||slot>=4)return false;Native n;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(inverseView[i]))return false;
    for(unsigned i=0;i<3;++i){const float p=bank[4*(4+slot)+i];if(!std::isfinite(p)||std::fabs(p)>100000)return false;
        n.color[i]=bank[4*slot+i];n.attenuation[i]=bank[4*(8+i)+slot];
        if(!std::isfinite(n.color[i])||n.color[i]<0||n.color[i]>16||!std::isfinite(n.attenuation[i])||n.attenuation[i]<0||n.attenuation[i]>10000)return false;}
    if(std::max({n.color[0],n.color[1],n.color[2]})<.01f||n.attenuation[0]+n.attenuation[1]+n.attenuation[2]<.001f)return false;
    float d2=0;for(unsigned i=0;i<3;++i){n.position[i]=inverseView[12+i];for(unsigned k=0;k<3;++k)n.position[i]+=bank[4*(4+slot)+k]*inverseView[4*k+i];
        if(!std::isfinite(camera[i])||!std::isfinite(n.position[i])||std::fabs(n.position[i])>=100000)return false;const float d=n.position[i]-camera[i];d2+=d*d;}
    if(d2>MaxDistance*MaxDistance)return false;out=n;return true;
}
inline float distance2(const float* a,const float* b){float d=0;for(unsigned k=0;k<3;++k){const float v=a[k]-b[k];d+=v*v;}return d;}
inline bool hueClose(const float* a,const float* b){const float ap=std::max({a[0],a[1],a[2]}),bp=std::max({b[0],b[1],b[2]});
    if(!(ap>0&&bp>0))return false;for(unsigned k=0;k<3;++k)if(std::fabs(a[k]/ap-b[k]/bp)>.08f)return false;return true;}
inline bool positionAndAttenuation(const Native& source,const float* position,const float* denominator){
    if(distance2(source.position,position)>=.25f*.25f)return false;
    for(unsigned k=0;k<3;++k)if(std::fabs(source.attenuation[k]-denominator[k])>1e-4f*std::max(1.f,denominator[k]))return false;
    return true;
}
inline bool staticDuplicate(const Native& n,const std::vector<NorthlightLocalLights::Light>& lights){
    for(const auto& l:lights)if(distance2(n.position,l.position)<.25f*.25f&&NorthlightLocalLights::valid(l)&&hueClose(n.color,l.diffuse))return true;
    return false; // Conservative suppression: a lamp already represented by FGL is never added twice, even with a different falloff fit.
}
// Fit the native denominator to the existing compact linear-light batch.
// A range of at most 12 yards and peak <= 1 bound this experimental addition;
// this is an artistic local-light approximation, not an exact native falloff.
inline NorthlightLocalLights::Light light(const Native& n,std::uint64_t id,float gain){
    NorthlightLocalLights::Light l{};std::copy_n(n.position,3,l.position);std::copy_n(n.color,3,l.diffuse);
    l.sourceId=0xfec0000000000000ull|id;l.kind=2;return NorthlightLocalLights::nativeTransport(l,n.attenuation,gain);
}
struct Tracker {
    struct Entry {Native source;std::uint64_t id=0;std::uint32_t seen=0;float gain=0;std::uint64_t nativeId=0;std::uint32_t flags=0,fixtureTag=0;};
    std::array<Entry,Capacity> entries{};unsigned count=0,banks=0,backgroundBanks=0,bankDuplicates=0,observed=0,duplicates=0,dropped=0;std::uint64_t serial=0;std::uint32_t updated=0;
    struct Bank {std::array<std::array<float,9>,4> sources{};unsigned count=0;};
    std::array<Bank,MemoBanks> frameBanks{};
    std::array<NorthlightLocalLights::Light,Capacity> output{};unsigned outputCount=0;
    std::array<std::uint64_t,Capacity> suppressed{};unsigned suppressedCount=0;
    void reset(){count=outputCount=suppressedCount=0;updated=0;nextFrame();}
    void nextFrame(){banks=backgroundBanks=bankDuplicates=observed=duplicates=dropped=0;}
    bool admit(const float* bank,unsigned points=4,bool actor=true){
        if(!bank||!points||points>4)return false;Bank key;key.count=points;
        // Unused point slots and colour/position .w components are not inputs
        // to the proven native lighting expression. Slot order is irrelevant.
        for(unsigned i=0;i<points;++i)for(unsigned k=0;k<3;++k){auto& s=key.sources[i];s[k]=bank[4*i+k];s[3+k]=bank[4*(4+i)+k];s[6+k]=bank[4*(8+k)+i];}
        for(unsigned i=0;i<points;++i)for(float v:key.sources[i])if(!std::isfinite(v))return false;
        std::sort(key.sources.begin(),key.sources.begin()+points);
        for(unsigned i=0;i<std::min(banks,MemoBanks);++i)if(frameBanks[i].count==points&&frameBanks[i].sources==key.sources){++bankDuplicates;return false;}
        frameBanks[banks++%MemoBanks]=key;if(!actor)++backgroundBanks;return true;
    }
    void observe(const Native& n,std::uint32_t now,const std::vector<NorthlightLocalLights::Light>& fixed,const float* camera=nullptr,std::uint64_t nativeId=0,std::uint32_t flags=0,std::uint32_t fixtureTag=0){
        ++observed;if(staticDuplicate(n,fixed)){++duplicates;return;}
        if(!nativeId){Entry* match=nullptr;bool ambiguous=false;
            for(unsigned i=0;i<count;++i)if(entries[i].nativeId&&positionAndAttenuation(n,entries[i].source.position,entries[i].source.attenuation)){
                if(match){ambiguous=true;break;}match=&entries[i];}
            if(match&&!ambiguous){match->source=n;match->seen=now;return;}
        }
        unsigned at=count;float best=1.f;
        for(unsigned i=0;i<count;++i){
            if(nativeId&&entries[i].nativeId==nativeId){at=i;break;}
            const float d=distance2(n.position,entries[i].source.position);
            if((!nativeId||!entries[i].nativeId)&&d<best&&hueClose(n.color,entries[i].source.color)){at=i;best=d;}}
        if(at==count){
            if(count==Capacity){
                unsigned worst=0;bool stale=false;float farthest=-1;
                for(unsigned i=0;i<count;++i){if(std::uint32_t(now-entries[i].seen)>=unsigned(LifeSeconds*1000)){worst=i;stale=true;break;}
                    if(camera){const float d=distance2(camera,entries[i].source.position);
                        if(d>farthest||(d==farthest&&std::lexicographical_compare(entries[worst].source.position,entries[worst].source.position+3,entries[i].source.position,entries[i].source.position+3))){farthest=d;worst=i;}}}
                // New candidates rank by distance, regardless of draw order;
                // only an already displayed incumbent receives the churn bias.
                const float d2=camera?distance2(camera,n.position):0.f;
                const float reach=std::sqrt(d2)+1.f;
                const bool closer=entries[worst].gain>0?reach*reach<farthest:
                    d2<farthest||(d2==farthest&&std::lexicographical_compare(n.position,n.position+3,entries[worst].source.position,entries[worst].source.position+3));
                if(!stale&&(!camera||!closer)){++dropped;return;}
                entries[worst]={n,++serial,now,0,nativeId,flags,fixtureTag};
            }else entries[count++]={n,++serial,now,0,nativeId,flags,fixtureTag};
        }
        else{entries[at].source=n;entries[at].seen=now;if(nativeId){entries[at].nativeId=nativeId;entries[at].flags=flags;entries[at].fixtureTag=fixtureTag;}}
    }
    void observeScene(const NorthlightNativeLights::Snapshot& scene,std::uint32_t now,const std::vector<NorthlightLocalLights::Light>& fixed,const float* camera,const uint64_t* claimed=nullptr,unsigned claimedCount=0){
        if(!scene.valid||!NorthlightNativeLights::wellFormed(scene))return;
        const auto owned=[&](uint64_t id){return claimed&&std::binary_search(claimed,claimed+claimedCount,id);};
        // Draw banks have no identity and may contain the previous animated
        // hue. Adopt only an unambiguous scene source with the same position
        // and denominator; colour must not leave a second 250 ms light behind.
        for(unsigned i=0;i<count;++i)if(!entries[i].nativeId){
            const auto& source=entries[i].source;const NorthlightNativeLights::Point* match=nullptr;bool ambiguous=false;
            for(unsigned j=0;j<scene.count;++j){const auto& point=scene.lights[j];
                if(!positionAndAttenuation(source,point.position,point.attenuation))continue;
                if(match){ambiguous=true;break;}match=&point;
            }
            if(!match||ambiguous)continue;
            const auto end=entries.begin()+count;
            if(std::find_if(entries.begin(),end,[&](const Entry& e){return e.nativeId==match->id;})==end)entries[i].nativeId=match->id;
            else{entries[i].seen=now-unsigned(LifeSeconds*1000);entries[i].gain=0;}
        }
        for(unsigned i=0;i<count;++i)if(entries[i].nativeId){
            const auto end=scene.lights.begin()+scene.count;
            const auto p=std::find_if(scene.lights.begin(),end,[&](const auto& light){return light.id==entries[i].nativeId;});
            if(p==end||owned(entries[i].nativeId)||std::max({p->color[0],p->color[1],p->color[2]})<.01f){
                entries[i].seen=now-unsigned(LifeSeconds*1000);entries[i].gain=0;
            }
        }
        for(unsigned i=0;i<scene.count;++i){const auto& p=scene.lights[i];
            if(owned(p.id))continue;
            if(std::max({p.color[0],p.color[1],p.color[2]})<.01f||p.attenuation[0]+p.attenuation[1]+p.attenuation[2]<.001f||distance2(p.position,camera)>MaxDistance*MaxDistance)continue;
            Native n;std::copy_n(p.position,3,n.position);std::copy_n(p.color,3,n.color);std::copy_n(p.attenuation,3,n.attenuation);
            observe(n,now,fixed,camera,p.id,(p.flags&NorthlightNativeLights::WorldFixture)?NorthlightLocalLights::CastShadow:0,p.reserved);
        }
        // A draw can have reported the new hue before the scene snapshot
        // updates its existing identity. Retire that anonymous duplicate.
        for(unsigned i=0;i<count;++i)if(!entries[i].nativeId)
            for(unsigned j=0;j<count;++j)if(entries[j].nativeId&&entries[j].seen==now&&
               distance2(entries[i].source.position,entries[j].source.position)<.25f*.25f&&hueClose(entries[i].source.color,entries[j].source.color)){
                entries[i].seen=now-unsigned(LifeSeconds*1000);entries[i].gain=0;break;
            }
        // A placed source may publish at its luminous-volume centroid, while
        // native draw constants still contain the original bulb/nozzle point.
        for(unsigned i=0;i<count&&claimedCount;++i)if(!entries[i].nativeId)
            for(unsigned j=0;j<scene.count;++j)if(owned(scene.lights[j].id)&&positionAndAttenuation(entries[i].source,scene.lights[j].position,scene.lights[j].attenuation)){
                entries[i].seen=now-unsigned(LifeSeconds*1000);entries[i].gain=0;break;}
    }
    const Entry* fixture(std::uint64_t source)const{
        if((source>>52)!=0xfec)return nullptr;
        for(unsigned i=0;i<count;++i)if((0xfec0000000000000ull|entries[i].id)==source&&entries[i].fixtureTag&&entries[i].flags==NorthlightLocalLights::CastShadow)return &entries[i];
        return nullptr;
    }
    void publish(std::uint32_t now,const std::vector<NorthlightLocalLights::Light>* fixed=nullptr){
        const float dt=updated?std::min(float(std::uint32_t(now-updated))*.001f,.1f):0;updated=now;outputCount=suppressedCount=0;
        for(unsigned i=0;i<count;){auto& e=entries[i];const auto age=std::uint32_t(now-e.seen);const float target=age<100?1.f:std::clamp((LifeSeconds-float(age)*.001f)/(LifeSeconds-.1f),0.f,1.f);
            // Scene publication can add a static lamp after this entry was
            // observed. Drop it now, including the local selection's old tail.
            if(fixed&&staticDuplicate(e.source,*fixed)){suppressed[suppressedCount++]=0xfec0000000000000ull|e.id;++duplicates;entries[i]=entries[--count];continue;}
            const float step=dt/FadeSeconds;e.gain+=std::clamp(target-e.gain,-step,step);
            if(age>=unsigned(LifeSeconds*1000)&&e.gain<=.0001f){suppressed[suppressedCount++]=0xfec0000000000000ull|e.id;entries[i]=entries[--count];continue;}
            if(e.gain>.0001f){auto l=light(e.source,e.id,e.gain);l.flags=e.flags;output[outputCount++]=l;}++i;}
    }
};
// The opposite publication transition: an FGL lamp disappears, but its former
// local-selection entry is fading. Its native replacement must not overlap it.
inline void replaceFadingDuplicates(NorthlightLocalLightSelection::Tracker& selection,const NorthlightLocalLights::Light* lights,unsigned count){
    for(unsigned n=0;n<count;++n)for(unsigned i=0;i<selection.count;){const auto& old=selection.entries[i];
        if(old.id!=lights[n].sourceId&&distance2(old.light.position,lights[n].position)<.25f*.25f&&hueClose(old.light.diffuse,lights[n].diffuse))selection.forget(old.id);
        else ++i;}
}
} // namespace NorthlightEquipmentLights
