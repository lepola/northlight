#pragma once
#include "world_probe_cache.h"
#include <type_traits>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace NorthlightGI {

inline bool staticVectorWithin(Vec3 a,Vec3 b,float tolerance) {
    if(!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(a.z)||
       !std::isfinite(b.x)||!std::isfinite(b.y)||!std::isfinite(b.z))return false;
    const Vec3 delta={a.x-b.x,a.y-b.y,a.z-b.z};return delta.x*delta.x+delta.y*delta.y+delta.z*delta.z<=tolerance*tolerance;
}
// The four global lighting vectors benefit from a packed three-lane compare;
// local lights retain the scalar helper to avoid extra packing overhead.
inline bool staticGlobalVectorWithin(Vec3 a,Vec3 b,float tolerance) {
#if defined(__SSE2__)
    if(!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(a.z)||
       !std::isfinite(b.x)||!std::isfinite(b.y)||!std::isfinite(b.z))return false;
    const __m128 aa=_mm_set_ps(0.f,a.z,a.y,a.x),bb=_mm_set_ps(0.f,b.z,b.y,b.x);
    const __m128 delta=_mm_sub_ps(aa,bb);float squared[4];
    _mm_storeu_ps(squared,_mm_mul_ps(delta,delta));
    return squared[0]+squared[1]+squared[2]<=tolerance*tolerance;
#else
    return staticVectorWithin(a,b,tolerance);
#endif
}
// This is also the cache invalidation policy. Check every source, including a
// removed secondary light; a request's reason bits do not identify generation.
inline bool sameStaticLighting(const Lighting& a,const Lighting& b) {
    if(!staticGlobalVectorWithin(a.sunDirection,b.sunDirection,.02f)||
       !staticGlobalVectorWithin(a.sunIrradiance,b.sunIrradiance,.03f)||
       !staticGlobalVectorWithin(a.skyRadiance,b.skyRadiance,.01f)||
       !staticGlobalVectorWithin(a.worldUp,b.worldUp,0)||
       !std::isfinite(a.maxDistance)||a.maxDistance!=b.maxDistance||
       !std::isfinite(a.rayBias)||a.rayBias!=b.rayBias||a.maxBounces!=b.maxBounces||
       a.additionalDirections.size()!=b.additionalDirections.size()||a.points.size()!=b.points.size())return false;
    for(size_t i=0;i<a.additionalDirections.size();++i)
        if(!staticVectorWithin(a.additionalDirections[i].direction,b.additionalDirections[i].direction,.02f)||
           !staticVectorWithin(a.additionalDirections[i].irradiance,b.additionalDirections[i].irradiance,.03f))return false;
    // Authored point lights belong to the BVH region and are stable. No camera
    // tolerance is appropriate for a changed local-light asset/list.
    for(size_t i=0;i<a.points.size();++i) {
        const auto& p=a.points[i];const auto& q=b.points[i];
        if(!staticVectorWithin(p.position,q.position,0)||!staticVectorWithin(p.irradiance,q.irradiance,0)||
           !std::isfinite(p.attenuationStart)||p.attenuationStart!=q.attenuationStart||
           !std::isfinite(p.attenuationEnd)||p.attenuationEnd!=q.attenuationEnd)return false;
    }
    return true;
}
inline bool staticFallbackCompatible(const std::string& solvedMap,const std::string& sceneMap,
                                     const std::string& latestMap,Vec3 solvedCamera,
                                     Vec3 sceneCenter,Vec3 latestCamera,
                                     const Lighting& solvedLight,const Lighting& latestLight) {
    return !solvedMap.empty()&&solvedMap==sceneMap&&solvedMap==latestMap&&
           staticVectorWithin(solvedCamera,latestCamera,96.f)&&
           staticVectorWithin(sceneCenter,latestCamera,64.f)&&sameStaticLighting(solvedLight,latestLight);
}

// Moving draw packets are only a partial observation of the world. They must
// never own static probe validity, visibility moments, or atlas residency.
// Keep their radiance correction separate from the world/light generation.
//
// Cost model (0.3.35): every actor capture used to discard all corrections and
// re-solve every probe near any actor (~225 solves of 64 rays x 3 bounces per
// 250 ms capture), which saturated the worker and starved the game thread.
// A correction now stays valid while the quantized bounds of the actors near
// that probe are unchanged, at most DynamicSolveBudget probes are solved per
// apply (nearest to an actor first), and the rest keep their previous
// correction until their turn. The weight always comes from current bounds,
// so a retained correction of a departed actor has zero effect.
inline constexpr float DynamicFullDistance=12.f;
inline constexpr float DynamicZeroDistance=24.f;
// Actors farther than the weight radius still bounce light into a solved
// correction (the solve traces the whole actor BVH), so identity reaches wider.
inline constexpr float DynamicSignatureDistance=48.f;
inline constexpr size_t DynamicCandidateCeiling=512;
inline constexpr unsigned DynamicSolveBudget=128;
// Part of the budget always goes to the probes nearest an actor (the ones the
// player stands on); the rest refreshes the oldest corrections round-robin.
inline constexpr unsigned DynamicNearestTier=32;
// A correction solved under a different actor set is applied at most this
// many captures before the probe falls back to the static solution.
inline constexpr uint32_t DynamicStaleLimit=8;
inline constexpr float DynamicAnchorHysteresis=.5f;
// 0.3.138: a probe's static-only ray paths are recorded once per static
// generation; a moving re-solve replays their moving queries (any-hit) and
// retraces only rays an actor touches. Bit-identical; saves ~90% of a
// re-solve. Records (~6-10 KiB per probe) are bounded by this byte budget;
// over budget a probe is solved directly, exactly as before.
inline constexpr bool DynamicPathReplay=true;
inline constexpr size_t DynamicPathRecordBudget=4u*1024u*1024u;
class DynamicProbeLayer {
    struct Bounds { Vec3 low,high;Vec3 anchorLow,anchorHigh;bool valid=false;uint32_t hash=0; };
    struct Solved { Probe probe;uint32_t signature=0;uint32_t solvedGeneration=0,visitedGeneration=0; };
    struct Hash { size_t operator()(ProbeGridKey key)const noexcept{return probeSeed(key);} };
    std::vector<Bounds> bounds_;
    std::unordered_map<ProbeGridKey,Solved,Hash> solved_;
    struct Path { StaticPathRecord record;uint32_t usedGeneration=0; };
    std::unordered_map<ProbeGridKey,Path,Hash> paths_;
    size_t pathBytes_=0;
    uint32_t generation_=0;
    static uint32_t mix(uint32_t h){h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;return h;}
    static uint32_t cell(float v){return uint32_t(int32_t(std::floor(std::max(-1e9f,std::min(1e9f,v)))));}
    static float span(const Bounds& a,Vec3 low,Vec3 high) {
        return std::max({std::fabs(a.anchorLow.x-low.x),std::fabs(a.anchorLow.y-low.y),std::fabs(a.anchorLow.z-low.z),
                         std::fabs(a.anchorHigh.x-high.x),std::fabs(a.anchorHigh.y-high.y),std::fabs(a.anchorHigh.z-high.z)});
    }
    void rebuild(const WorldScene* actors) {
        std::vector<Bounds> previous;previous.swap(bounds_);
        if(!actors)return;
        bounds_.resize(actors->materials.size());
        // ActorJob assigns one material per captured draw. Ignore unreferenced
        // vertices: a draw's snapshot can include vertices outside its triangles.
        for(const auto& t:actors->triangles) {
            if(t.material>=bounds_.size())continue;
            auto& b=bounds_[t.material];
            for(auto index:{t.v0,t.v1,t.v2}) {
                if(index>=actors->vertices.size())continue;
                const Vec3 p=actors->vertices[index].position;
                if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))continue;
                if(!b.valid){b.low=b.high=p;b.valid=true;}
                else {b.low={std::min(b.low.x,p.x),std::min(b.low.y,p.y),std::min(b.low.z,p.z)};
                      b.high={std::max(b.high.x,p.x),std::max(b.high.y,p.y),std::max(b.high.z,p.z)};}
            }
        }
        // Anchored, one-unit quantized bounds with hysteresis: a box that stays
        // within half a unit of a previous capture's anchor (idle animation,
        // a cloak, a weapon) inherits that anchor, so its identity does not
        // flap at integer boundaries. A real move re-anchors once.
        for(auto& b:bounds_)if(b.valid) {
            b.anchorLow=b.low;b.anchorHigh=b.high;float best=DynamicAnchorHysteresis;
            for(const auto& old:previous)if(old.valid){float d=span(old,b.low,b.high);if(d<=best){best=d;b.anchorLow=old.anchorLow;b.anchorHigh=old.anchorHigh;}}
            b.hash=mix(cell(b.anchorLow.x)*73856093u^cell(b.anchorLow.y)*19349663u^cell(b.anchorLow.z)*83492791u^
                       cell(b.anchorHigh.x)*2654435761u^cell(b.anchorHigh.y)*2246822519u^cell(b.anchorHigh.z)*3266489917u);
        }
    }
    static float boxDistanceSquared(const Bounds& b,Vec3 p) {
#if defined(__SSE2__)
        const __m128 q=_mm_set_ps(0.f,p.z,p.y,p.x),lo=_mm_set_ps(0.f,b.low.z,b.low.y,b.low.x);
        const __m128 hi=_mm_set_ps(0.f,b.high.z,b.high.y,b.high.x),zero=_mm_setzero_ps();
        const __m128 a=_mm_sub_ps(lo,q),c=_mm_sub_ps(q,hi);
        // Ordered selects preserve std::max's first-operand NaN behavior.
        const __m128 m0=_mm_cmplt_ps(a,zero),first=_mm_or_ps(_mm_and_ps(m0,zero),_mm_andnot_ps(m0,a));
        const __m128 m1=_mm_cmplt_ps(first,c),d=_mm_or_ps(_mm_and_ps(m1,c),_mm_andnot_ps(m1,first));
        float squared[4];_mm_storeu_ps(squared,_mm_mul_ps(d,d));
        return squared[0]+squared[1]+squared[2];
#else
        float dx=std::max({b.low.x-p.x,0.f,p.x-b.high.x});
        float dy=std::max({b.low.y-p.y,0.f,p.y-b.high.y});
        float dz=std::max({b.low.z-p.z,0.f,p.z-b.high.z});
        return dx*dx+dy*dy+dz*dz;
#endif
    }
    static float weightDistance(float distance) {
        float t=std::max(0.f,std::min(1.f,(DynamicZeroDistance-std::sqrt(distance))/(DynamicZeroDistance-DynamicFullDistance)));
        return t*t*(3.f-2.f*t);
    }
public:
    // Static world/light generation changed: every correction is stale.
    void reset(const WorldScene* actors) {
        solved_.clear();paths_.clear();pathBytes_=0;generation_=0;bounds_.clear();rebuild(actors);
    }
    // Only the observed actor packet changed. Corrections whose nearby actor
    // bounds are unchanged stay valid; the others are refreshed by budget.
    void observe(const WorldScene* actors) {
        rebuild(actors);++generation_;
        // Bound retention: drop corrections neither solved nor reused for 16
        // captures (a too-old stale correction is not reused, so it ages out).
        for(auto it=solved_.begin();it!=solved_.end();)
            if(generation_-it->second.visitedGeneration>16)it=solved_.erase(it);else ++it;
        for(auto it=paths_.begin();it!=paths_.end();)
            if(generation_-it->second.usedGeneration>16){pathBytes_-=it->second.record.retainedBytes();it=paths_.erase(it);}else ++it;
    }
    size_t pathRecords()const{return paths_.size();}
    size_t pathBytes()const{return pathBytes_;}
    void accountMemory(NorthlightMemoryFootprint::Counter& memory)const{
        using namespace NorthlightMemoryFootprint;
        if(!memory.visit(this))return;
        memory.add(Metadata,sizeof(*this));memory.vector(Metadata,bounds_);
        for(const auto& entry:solved_)memory.allocation(Probes,&entry,sizeof(entry));
        for(const auto& entry:paths_){memory.allocation(Metadata,&entry,sizeof(entry));memory.vector(Probes,entry.second.record.rays);memory.vector(Probes,entry.second.record.queries);}
    }
    size_t retained()const{return solved_.size();}
    uint32_t generation()const{return generation_;}
    float distanceSquared(Vec3 p)const {
        // Explicit near-field approximation: full moving-object transport in
        // 12 units, smoothly returning to the static world solution by 24.
        // This support is tied to observed world geometry, never to camera/grid.
        float closest=DynamicZeroDistance*DynamicZeroDistance;
        for(const auto& b:bounds_)if(b.valid)closest=std::min(closest,boxDistanceSquared(b,p));
        return closest;
    }
    float weight(Vec3 p)const {return weightDistance(distanceSquared(p));}
    // Order-independent identity of every actor box that can influence p.
    uint32_t signature(Vec3 p)const {
        uint32_t h=0x9e3779b9u;
        for(const auto& b:bounds_)if(b.valid&&boxDistanceSquared(b,p)<DynamicSignatureDistance*DynamicSignatureDistance)h+=b.hash;
        return mix(h);
    }
    // Modify a private export only; cancellation never changes a published
    // snapshot. Full-key cache lookup prevents modulo aliases crossing regions.
    // Retained solved corrections remain reusable after a camera-only cancel.
    template<class Solve,class Cancel>
    bool apply(std::vector<ProbeAtlasEntry>& atlas,Solve solve,Cancel cancel,
               unsigned& reused,unsigned& computed) {
        return applyBudget<true>(atlas,solve,cancel,reused,computed,DynamicSolveBudget);
    }
    // Progressive static publication must not erase previously solved moving
    // contributions or spend another dynamic solve budget on every batch.
    // Use the same full-key, signature, age, selection and blending policy;
    // only the final request publication computes new moving contributions.
    template<class Cancel>
    bool applyCached(std::vector<ProbeAtlasEntry>& atlas,Cancel cancel,unsigned& reused) {
        unsigned computed=0;
        return applyBudget<false>(atlas,[](ProbeGridKey,Vec3){return Probe{};},cancel,reused,computed,0);
    }
private:
    // A solve receives this probe's path record, or null when the budget is
    // spent (then it must solve directly). Accounting follows the record size.
    template<class Solve>
    Probe solvePath(Solve& solve,ProbeGridKey key,Vec3 position) {
        auto found=paths_.find(key);
        if(found==paths_.end()) {
            if(!DynamicPathReplay||pathBytes_>=DynamicPathRecordBudget)return solve(key,position,static_cast<StaticPathRecord*>(nullptr));
            found=paths_.emplace(key,Path{}).first;
        }
        auto& path=found->second;path.usedGeneration=generation_;
        pathBytes_-=path.record.retainedBytes();
        struct Account { size_t& bytes;const StaticPathRecord& record;~Account(){bytes+=record.retainedBytes();} } account{pathBytes_,path.record};
        return solve(key,position,&path.record);
    }
    template<bool WithBudget,class Solve,class Cancel>
    bool applyBudget(std::vector<ProbeAtlasEntry>& atlas,Solve solve,Cancel cancel,
                     unsigned& reused,unsigned& computed,unsigned budget) {
        // Without valid actor bounds, every probe has zero dynamic weight.
        if(std::none_of(bounds_.begin(),bounds_.end(),[](const Bounds& b){return b.valid;}))return !cancel();
        struct Candidate {ProbeAtlasEntry* entry;float distance,amount;uint32_t signature,solvedGeneration;bool fresh,chosen;};
        std::vector<Candidate> candidates;
        for(auto& entry:atlas) {
            if(!entry.occupied||!entry.probe.valid)continue;
            float distance=distanceSquared(entry.probe.position);
            if(distance>=DynamicZeroDistance*DynamicZeroDistance)continue;
            // The outer shell blends at under 1/64: not worth a solve.
            float amount=weightDistance(distance);if(amount<1.f/64)continue;
            candidates.push_back({&entry,distance,amount,0,0,false,false});
        }
        // Hard ceiling of moving-object probes per publication. Selection is
        // actor-distance/world-key ordered, never camera ordered, so the
        // probes an actor stands on are refreshed first.
        std::sort(candidates.begin(),candidates.end(),[](const Candidate& a,const Candidate& b){
            if(a.distance!=b.distance)return a.distance<b.distance;
            const auto x=a.entry->key,y=b.entry->key;
            if(x.x!=y.x)return x.x<y.x;if(x.y!=y.y)return x.y<y.y;return x.z<y.z;
        });
        if(candidates.size()>DynamicCandidateCeiling)candidates.resize(DynamicCandidateCeiling);
        // Classify, then spend the budget: nearest tier first, then the oldest
        // corrections (never solved counts as oldest) so every mismatched
        // candidate is refreshed within a few captures instead of starving.
        std::vector<Candidate*> mismatched;
        for(auto& c:candidates) {
            c.signature=signature(c.entry->probe.position);
            auto found=solved_.find(c.entry->key);
            if(found!=solved_.end()){c.solvedGeneration=found->second.solvedGeneration+1;c.fresh=found->second.signature==c.signature;}
            if(!c.fresh)mismatched.push_back(&c);
        }
        for(size_t i=0;i<mismatched.size()&&i<DynamicNearestTier&&budget;++i){mismatched[i]->chosen=true;--budget;}
        // Cached-only publication cannot choose a solve, so ordering is unused.
        if constexpr(WithBudget)std::stable_sort(mismatched.begin(),mismatched.end(),[](const Candidate* a,const Candidate* b){return a->solvedGeneration<b->solvedGeneration;});
        for(auto* c:mismatched){if(!budget)break;if(!c->chosen){c->chosen=true;--budget;}}
        unsigned visited=0;
        for(const auto& candidate:candidates) {
            auto& entry=*candidate.entry;
            if((visited++%8)==0&&cancel())return false;
            auto found=solved_.find(entry.key);
            Probe moving;
            if(candidate.fresh){found->second.visitedGeneration=generation_;moving=found->second.probe;++reused;}
            else if(candidate.chosen){
                if constexpr(std::is_invocable_v<Solve,ProbeGridKey,Vec3,StaticPathRecord*>)moving=solvePath(solve,entry.key,entry.probe.position);
                else moving=solve(entry.key,entry.probe.position);
                ++computed;
                solved_[entry.key]={moving,candidate.signature,generation_,generation_};
            }
            else if(found!=solved_.end()&&generation_-found->second.solvedGeneration<=DynamicStaleLimit){found->second.visitedGeneration=generation_;moving=found->second.probe;++reused;}
            else continue;
            // A moving actor enclosing a probe cannot turn that static world
            // probe into a coverage hole. Ignore that undefined correction.
            if(!moving.valid)continue;
            bool finite=true;
            for(const auto& sh:moving.sh)finite=finite&&std::isfinite(sh.x)&&std::isfinite(sh.y)&&std::isfinite(sh.z);
            if(!finite)continue;
            // A moving actor may add bounce freely, but its occlusion of a probe
            // (the player's own body standing on the probe, dark armour) must
            // not remove more than a quarter of the static irradiance: contact
            // darkening is AO's job, and a black patch following the player is
            // not what an 8-unit probe lattice can represent.
            {
                const auto& b0=entry.probe.sh[0];const auto& m0=moving.sh[0];
                const float baseL=b0.x+b0.y+b0.z,movingL=m0.x+m0.y+m0.z;
                if(baseL>1e-6f&&movingL<baseL*.75f){
                    const float k=(baseL*.25f)/std::max(baseL-movingL,1e-6f);
                    for(auto& sh:moving.sh){const auto& base=entry.probe.sh[&sh-moving.sh];sh=base+(sh-base)*k;}
                }
            }
            const float amount=candidate.amount;
            for(unsigned coefficient=0;coefficient<4;++coefficient) {
                auto& base=entry.probe.sh[coefficient];
                base=base+(moving.sh[coefficient]-base)*amount;
            }
        }
        return !cancel();
    }
};
} // namespace NorthlightGI
