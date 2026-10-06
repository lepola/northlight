#pragma once
#include "static_shadow_scene.h"
#ifndef STATIC_SHADOW_GPU_TEST
#include "static_shadow_compiled_shaders.h"
#include <d3d9.h>
#endif
#include "static_shadow_draw_state.h"
#include "shadow_bounds.h"
#include "streaming_budget.h"
#include "cpu_retirement.h"
#include "static_shadow_owners.h"
#include "static_plan_job.h"
#include "stream_hooks.h"
#include <deque>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <climits>
#include <functional>
#include <set>
#include <unordered_map>
#include <tuple>
#include <memory>
#include <new>
#include <thread>

namespace StaticShadow {
// World AABB of every instance-batch a command/slice draws; exact per-pixel
// partial cache redraws skip only commands whose footprint misses the rect.
struct CasterBounds {Vec3 low{INFINITY,INFINITY,INFINITY},high{-INFINITY,-INFINITY,-INFINITY};bool bounded=true;
    void add(Vec3 a,Vec3 b){if(!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(a.z)||!std::isfinite(b.x)||!std::isfinite(b.y)||!std::isfinite(b.z))bounded=false;low={std::min(low.x,a.x),std::min(low.y,a.y),std::min(low.z,a.z)};high={std::max(high.x,b.x),std::max(high.y,b.y),std::max(high.z,b.z)};}};
// Device resources are confined to the render thread. CPU snapshots never own
// D3D objects; failed staging never overwrites an already drawable resource.
class GpuCache {
public:
    struct Limits { size_t residentBytes=128u<<20,uploadBytes=2u<<20;double uploadMs=1.0;unsigned retainFrames=1800;size_t planMetadataBytes=1024u<<10; };
    struct Statistics {
        uint64_t residentBytes=0,peakBytes=0,uploadedBytes=0,frameBytes=0,uploads=0,evictions=0,failures=0;
        uint64_t drawCalls=0,instances=0,culled=0,maintenanceVisits=0,publications=0;unsigned readyModels=0,pendingModels=0;
        uint64_t instanceLocks=0,instanceDiscards=0,instanceBytes=0;
        uint64_t planBuilds=0,planHits=0,planInvalidations=0,placementTests=0,batchTests=0;
        uint64_t modelPlanBuilds=0,modelPlanReuses=0,modelPlanFallbacks=0,rectSkipped=0;
        uint64_t coveredMemoFills=0,planEvictions=0,planSelfChecks=0,planSelfCheckMismatches=0,planDiscards=0,planIdleReleases=0,publishDiffs=0,publishDiffFallbacks=0,ownerTests=0;double publishMs=0,publishPeakMs=0;double planMs=0,planPeakMs=0;
        double planReuseMs=0,planRebuildMs=0,planWalkMs=0; /* 0.3.152: build split, cumulative: reused-slice copies, rebuilt slices, the rest (group walk, lookups, metadata) */
        uint64_t asyncKicks=0,asyncBuilds=0,asyncStolen=0,asyncFailures=0;double asyncWaitMs=0,asyncWorkerMs=0; /* 0.3.152 plan worker: installed builds (not in planMs), stolen back, failed (rebuilt synchronously), render-thread wait, worker build time */
        double uploadMs=0,publishFrameMs=0;bool instancing=false; /* publishFrameMs: this frame (hitch); publishMs/publishPeakMs: cumulative (staticpub) */
    };
private:
    template<class T> static void release(T*& p){if(p){p->Release();p=nullptr;}}
    struct Texture {
        IDirect3DTexture9* gpu=nullptr;uint64_t key=0;size_t bytes=0;unsigned nextRow=0;
        unsigned width=0,height=0;bool ready=false;std::shared_ptr<const Model> owner;size_t material=0;std::vector<uint8_t> alpha;std::shared_ptr<const std::vector<uint8_t>> preparedAlpha;
        ~Texture(){release(gpu);if(preparedAlpha)NorthlightStreaming::cpuRetirement().retire(preparedAlpha,preparedAlpha->capacity());if(accounted)*accounted-=bytes;}size_t* accounted=nullptr;
    };
    struct Resource {
        IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
        Vec3 low{},high{};bool boundsValid=false;
        std::shared_ptr<const Model> model;std::vector<std::shared_ptr<Texture>> textures;std::vector<bool> opaque;
        size_t vertexDone=0,indexDone=0,bytes=0;UINT vertexCount=0;uint64_t lastUsed=0,expires=0;bool ready=false,wanted=false;
        ~Resource(){release(vb);release(ib);if(accounted)*accounted-=bytes;}size_t* accounted=nullptr;
    };
    struct Instance { float row[12]; };
    Limits limits_;mutable Statistics stats_;size_t geometryBytes_=0,textureBytes_=0;
    std::function<bool(size_t)> admit_;
    std::map<std::string,std::unique_ptr<Resource>> resources_;
    std::multimap<uint64_t,std::weak_ptr<Texture>> textures_;
    decltype(textures_)::iterator textureSweep_=textures_.end();
    std::shared_ptr<const Snapshot> scene_;
    uint64_t generation_=0;std::string map_;
    IDirect3DDevice9* device_=nullptr;
    IDirect3DVertexShader9 *vs_=nullptr,*instancedVS_=nullptr;
    IDirect3DPixelShader9 *ps_=nullptr,*fastPS_=nullptr,*opaqueFastPS_=nullptr,*opaquePS_=nullptr;
    IDirect3DVertexDeclaration9 *decl_=nullptr,*instancedDecl_=nullptr;
    IDirect3DVertexBuffer9* instances_=nullptr;
    unsigned instanceCapacity_=0,instanceCursor_=0;
    bool canInstance_=false;
    std::string staging_;
    std::multimap<uint64_t,std::string> missing_,expiry_;
    std::map<std::string,uint64_t> deferred_;
    std::map<std::string,Resource*> readyByKey_;
    uint64_t lastUpdateFrame_=0;
    std::map<std::string,std::vector<size_t>> fallbackGroups_;
    const std::map<std::string,std::vector<size_t>>* groups_=nullptr;
    std::deque<std::unique_ptr<Resource>> retired_;
    NorthlightStreaming::Budget* sharedBudget_=nullptr;
    unsigned maintenanceLeft_=0;
    bool maintenanceAvailable()const{return !sharedBudget_||(maintenanceLeft_&&sharedBudget_->available());}
    void maintenanceStep(){if(sharedBudget_&&maintenanceLeft_)--maintenanceLeft_;}
    void retireCpu(std::shared_ptr<const Model>& owner){if(owner)NorthlightStreaming::cpuRetirement().retire(owner,owner->bytes);}
    void drainRetired(bool force=false){
        while(!retired_.empty()&&(force||maintenanceAvailable())){auto& r=*retired_.front();
            if(!r.textures.empty()){r.textures.pop_back();}
            else if(r.vb)release(r.vb);else if(r.ib)release(r.ib);
            else{retireCpu(r.model);retired_.pop_front();}
            if(!force)maintenanceStep();
        }
    }
    using OwnerKey=StaticShadow::OwnerKey;
    std::shared_ptr<const OwnerMap> covered_; /* immutable, usually prepared by the GI worker */
    static bool samePlacement(const Placement& a,const Placement& b){return StaticShadow::sameOwner(a,b);}
    bool covered(const Placement& p)const {if(!covered_)return false;auto found=covered_->find(OwnerKey{p.category,p.uid,p.modelKey});if(found==covered_->end())return false;for(const auto& owner:found->second)if(samePlacement(owner,p))return true;return false;}
    // first: the instance index in the plan's (virtual) concatenated instance
    // array; data: the same instances in the owning slice's chunk.
    struct Command {Resource* resource;size_t batch,first;unsigned count,path;CasterBounds bounds;const Instance* data;};
    struct ModelSlice {
        size_t commandFirst=0,commandCount=0,instanceFirst=0,instanceCount=0;
        uint64_t signature=0,culled=0;CasterBounds bounds;uint32_t chunk=0; /* in the tail padding: metadata caps (sizeof) unchanged */
    };
    struct Plan {
        float matrix[16]{};bool occupied=false,valid=false,fullRebuild=true,pinned=false;uint64_t signature=0,culled=0,used=0; /* pinned: reserved for the plan worker */
        // 0.3.152: instances live in one chunk per non-empty model slice, in
        // command order; a reused slice moves its chunk (no copy, no big block).
        // Commands stay one ordered vector. Metadata is independently capped per slot.
        std::vector<Command> commands;std::vector<std::vector<Instance>> chunks;size_t instanceCount=0;
        std::map<std::string,ModelSlice> models;
        std::set<std::string> dirtyModels;
        size_t metadataBytes=0,dirtyBytes=0;
    };
    mutable Plan plans_[8];mutable unsigned planCursor_=0;mutable uint64_t planClock_=0;
public:
    // 0.3.137: plan builds walk the sorted ready/previous-slice maps in lockstep
    // with the sorted groups, memoize exact covered() answers per placement and
    // recycle slice nodes: the same algorithm and results without per-group
    // string searches or per-placement key copies. false = 0.3.136 lookups.
    static constexpr bool FastPlanLookups=true;
    // Evict the least recently used plan instead of round robin, so plans
    // prepared ahead of time never push out the ones drawn every frame.
    static constexpr bool LruPlanSlots=true;
    void referencePlans(){mutate();fastPlans_=false;lruPlans_=false;idleRelease_=false;} /* tests: exact 0.3.136 path */
    void roundRobinPlans(){mutate();lruPlans_=false;idleRelease_=false;} /* tests: fast lookups with 0.3.136 eviction */
    // 0.3.138 publication/ownership invalidation (render-thread staticCache hitch):
    // ExactPublishDiff: only placements that left/entered/moved are tested, not
    // whole runs; kept plans are the identical plans a rebuild would make.
    // GroupedOwnerInvalidation: one ready lookup per model, first visible hit per plan.
    // IdlePlanRelease: forget plans not prepared for IdlePlanTicks calls (a
    // cache: a later prepare rebuilds), so invalidation scans fewer plans.
    static constexpr bool ExactPublishDiff=true,GroupedOwnerInvalidation=true,IdlePlanRelease=true;static constexpr uint64_t IdlePlanTicks=4096;
    void referencePublish(){mutate();exactDiff_=false;groupedOwners_=false;idleRelease_=false;} /* tests: exact 0.3.137 publication path */
    void exactPublishOnly(bool exact,bool grouped,bool idle,uint64_t idleTicks=IdlePlanTicks){mutate();exactDiff_=exact;groupedOwners_=grouped;idleRelease_=idle;idleTicks_=idleTicks;} /* tests */
private:
    // Debug: also build every plan the 0.3.136 way and count strict mismatches
    // (stats planSelfChecks/planSelfCheckMismatches). Doubles plan CPU; off.
    static constexpr bool PlanSelfCheck=false;
    bool fastPlans_=FastPlanLookups,lruPlans_=LruPlanSlots,selfCheck_=PlanSelfCheck;
    bool exactDiff_=ExactPublishDiff,groupedOwners_=GroupedOwnerInvalidation,idleRelease_=IdlePlanRelease;std::string keyScratch_;uint64_t idleTicks_=IdlePlanTicks;
    struct Candidate {const Placement* placement;Instance instance;};
    // 0.3.152: everything one plan build writes besides the plan: scratch, the
    // covered() memo and counters. The render thread and the plan worker each own one.
    struct BuildCounters {uint64_t placementTests=0,batchTests=0,modelPlanBuilds=0,modelPlanReuses=0,modelPlanFallbacks=0,coveredMemoFills=0;double reuseMs=0,rebuildMs=0,walkMs=0;};
    struct BuildContext {
        std::vector<Candidate> candidates;std::vector<Instance> transforms[2];std::vector<std::pair<Vec3,Vec3>> boxes[2];
        // covered() per placement index for the current snapshot and owner set:
        // 0 unknown, 1 drawn, 2 covered. Any scene/owner change bumps the epoch.
        std::vector<uint8_t> memo;uint64_t memoEpoch=0;BuildCounters counters;
        // Undo log of what a build takes from `previous` (moved chunks, slice
        // nodes with their old values): a failed build leaves it unchanged.
        struct ChunkMove {size_t from,to;};struct NodeMove {std::map<std::string,ModelSlice>::iterator at;ModelSlice old;};
        std::vector<ChunkMove> chunkMoves;std::vector<NodeMove> nodeMoves;
        std::vector<std::vector<Instance>> spare; /* storage of dropped slices, for rebuilt ones */
#ifdef STATIC_SHADOW_GPU_TEST
        size_t failAfter=SIZE_MAX; /* tests: bad_alloc after this many ready models */
#endif
    };
    mutable BuildContext sync_;uint64_t coveredEpoch_=1;
    void coveredChanged(){++coveredEpoch_;}
    bool coveredAt(BuildContext& ctx,size_t index)const{
        if(ctx.memoEpoch!=coveredEpoch_){ctx.memo.assign(scene_->placements.size(),0);ctx.memoEpoch=coveredEpoch_;++ctx.counters.coveredMemoFills;}
        if(index>=ctx.memo.size())return covered(scene_->placements[index]);
        auto& known=ctx.memo[index];if(!known)known=covered(scene_->placements[index])?2:1;return known==2;
    }
    void addCounters(BuildCounters& c)const{auto& s=stats_;s.placementTests+=c.placementTests;s.batchTests+=c.batchTests;s.modelPlanBuilds+=c.modelPlanBuilds;s.modelPlanReuses+=c.modelPlanReuses;s.modelPlanFallbacks+=c.modelPlanFallbacks;s.coveredMemoFills+=c.coveredMemoFills;s.planReuseMs+=c.reuseMs;s.planRebuildMs+=c.rebuildMs;s.planWalkMs+=c.walkMs;c={};}
    Plan& planSlot()const{ /* pinned slots belong to the plan worker */
        if(!lruPlans_){for(;;){auto& plan=plans_[planCursor_++%8];if(!plan.pinned)return plan;}}
        Plan* oldest=nullptr;for(auto& plan:plans_){if(plan.pinned)continue;if(!plan.occupied)return plan;if(!oldest||plan.used<oldest->used)oldest=&plan;}
        ++stats_.planEvictions;return *oldest;
    }
    std::vector<const NorthlightShadowBounds::TexelRect*> hits_;
    uint64_t epoch_=1;
    void invalidate(Plan& plan){if(plan.valid){plan.valid=false;++stats_.planInvalidations;}}
    size_t planMetadataLimit()const{return std::min(limits_.planMetadataBytes,size_t(1024u<<10));}
    static size_t modelMetadataBytes(const std::string& key){return sizeof(ModelSlice)+key.size()+1+192;}
    void invalidateAll(Plan& plan){invalidate(plan);plan.fullRebuild=true;plan.models.clear();plan.dirtyModels.clear();plan.metadataBytes=plan.dirtyBytes=0;}
    void invalidateModel(Plan& plan,const std::string& key){
        invalidate(plan);if(plan.fullRebuild||plan.dirtyModels.count(key))return;
        const size_t bytes=key.size()+1+128;
        if(bytes>planMetadataLimit()||plan.metadataBytes+plan.dirtyBytes>planMetadataLimit()-bytes){invalidateAll(plan);++stats_.modelPlanFallbacks;return;}
        plan.dirtyModels.insert(key);plan.dirtyBytes+=bytes;
    }
    void changed(){for(auto& plan:plans_)invalidateAll(plan);}
    // Bounds come from the actual drawable batches, not asset placement boxes.
    // Invalid metadata fails open. Union-transform roundoff is expanded before
    // the light-space test so coarse rejection cannot trim a visible child.
    static bool coarseVisible(const Placement& p,const Resource& r,const float* matrix,bool affine=false){
        if(!r.boundsValid)return true;
        const float low[]={r.low.x,r.low.y,r.low.z},high[]={r.high.x,r.high.y,r.high.z};
        const float translation[]={p.translation.x,p.translation.y,p.translation.z};
        Vec3 lo,hi;float* lows[]={&lo.x,&lo.y,&lo.z};float* highs[]={&hi.x,&hi.y,&hi.z};
        for(unsigned axis=0;axis<3;++axis){
            double l=translation[axis],h=l,mag=std::fabs(l);
            if(!std::isfinite(l))return true;
            for(unsigned j=0;j<3;++j){const double c=p.matrix[axis*3+j];if(!std::isfinite(c))return true;
                const double a=c*low[j],b=c*high[j];l+=std::min(a,b);h+=std::max(a,b);mag+=std::max(std::fabs(a),std::fabs(b));}
            const double margin=1e-4+mag*(32.0*std::numeric_limits<float>::epsilon());
            *lows[axis]=float(l-margin);*highs[axis]=float(h+margin);
        }
        return !(affine?NorthlightShadowBounds::directionalClipRejectAffine(lo,hi,matrix):NorthlightShadowBounds::directionalClipReject(lo,hi,matrix));
    }
    void modelChanged(const Resource& resource){
        for(auto& plan:plans_)if(plan.occupied&&(plan.valid||!plan.fullRebuild)){
            if(plan.fullRebuild){invalidate(plan);continue;}
            // Metadata lookup is safe even if an earlier invalidation retired
            // another model's Resource; never dereference old command pointers.
            auto old=plan.models.find(resource.model->key);
            bool affects=old!=plan.models.end()&&old->second.commandCount;
            if(!affects&&scene_&&groups_){auto group=groups_->find(resource.model->key);
                if(group!=groups_->end())for(size_t i:group->second)if(!covered(scene_->placements[i])&&coarseVisible(scene_->placements[i],resource,plan.matrix)){affects=true;break;}}
            if(affects)invalidateModel(plan,resource.model->key);
        }
    }
    void ownerChanged(const Placement& placement){
        auto* resource=readyResource(placement.modelKey);if(!resource)return;
        for(auto& plan:plans_)if(plan.occupied&&(plan.valid||!plan.fullRebuild)){++stats_.ownerTests;if(coarseVisible(placement,*resource,plan.matrix))invalidateModel(plan,placement.modelKey);}
    }
    // Same final state as ownerChanged() per placement of one model: one ready
    // lookup; per plan the first coarse-visible placement dirties the model and
    // every later call for that plan/key would be a no-op (dirty key already
    // present, or the plan fell back to a full rebuild and is skipped).
    void ownersChanged(const std::string& key,const Placement* const* first,const Placement* const* last){
        if(first==last)return;
        if(!groupedOwners_){for(auto p=first;p!=last;++p)ownerChanged(**p);return;}
        auto* resource=readyResource(key);if(!resource)return;
        for(auto& plan:plans_){const bool affine=NorthlightShadowBounds::affineLightMatrix(plan.matrix);
            for(auto p=first;p!=last&&plan.occupied&&(plan.valid||!plan.fullRebuild);++p){++stats_.ownerTests;if(coarseVisible(**p,*resource,plan.matrix,affine)){invalidateModel(plan,key);break;}}}
    }
    // Exact diff of the changed middle of one model's run: pairs equal
    // (samePlacement) placements by (uid,category). Unpaired placements are the
    // changed ones, in run order. Any duplicate identity or reordering of the
    // paired ones returns false (caller treats the whole middle as changed).
    struct DiffEntry {uint64_t uid;uint32_t category;uint32_t position;bool operator<(const DiffEntry& o)const{return uid!=o.uid?uid<o.uid:category!=o.category?category<o.category:position<o.position;}};
    std::vector<DiffEntry> diffNew_;std::vector<uint32_t> diffPair_;std::vector<uint8_t> diffUsed_;std::vector<const Placement*> changedOwners_;
    bool exactMiddle(const Snapshot& previous,const std::vector<size_t>& old,size_t oldBegin,size_t oldEnd,const Snapshot& next,const std::vector<size_t>& current,size_t newBegin,size_t newEnd){
        diffNew_.clear();for(size_t j=newBegin;j<newEnd;++j){const auto& p=next.placements[current[j]];diffNew_.push_back({p.uid,p.category,uint32_t(j)});}
        std::sort(diffNew_.begin(),diffNew_.end());
        for(size_t k=1;k<diffNew_.size();++k)if(diffNew_[k].uid==diffNew_[k-1].uid&&diffNew_[k].category==diffNew_[k-1].category)return false;
        diffPair_.assign(oldEnd-oldBegin,UINT32_MAX);diffUsed_.assign(newEnd-newBegin,0);uint32_t last=0;bool any=false;
        for(size_t i=oldBegin;i<oldEnd;++i){const auto& p=previous.placements[old[i]];
            auto found=std::lower_bound(diffNew_.begin(),diffNew_.end(),DiffEntry{p.uid,p.category,0});
            if(found==diffNew_.end()||found->uid!=p.uid||found->category!=p.category||!samePlacement(p,next.placements[current[found->position]]))continue;
            auto& used=diffUsed_[found->position-newBegin];if(used||(any&&found->position<=last))return false; /* duplicate old identity or reordered */
            used=1;diffPair_[i-oldBegin]=found->position;last=found->position;any=true;
        }
        for(size_t i=oldBegin;i<oldEnd;++i)if(diffPair_[i-oldBegin]==UINT32_MAX)changedOwners_.push_back(&previous.placements[old[i]]);
        for(size_t j=newBegin;j<newEnd;++j)if(!diffUsed_[j-newBegin])changedOwners_.push_back(&next.placements[current[j]]);
        return true;
    }
    void snapshotChanged(const Snapshot& previous,const Snapshot& next){
        // Compare ordered per-model placement runs. A common prefix/suffix is
        // untouched; only the changed middle can dirty a cascade. This is linear
        // in placements, without building per-placement maps on publication.
        // 0.3.138: inside the middle only placements that really left, entered
        // or moved are tested (a moving window removes at the front and adds at
        // the end of every run, which made the whole run "changed").
        std::map<std::string,std::vector<size_t>> oldFallback,newFallback;
        auto groups=[](const Snapshot& s,auto& fallback)->const auto&{
            if(s.preparedIdentity==&s)return s.placementGroups;
            for(size_t i=0;i<s.placements.size();++i)fallback[s.placements[i].modelKey].push_back(i);return fallback;
        };
        const auto& before=groups(previous,oldFallback);const auto& after=groups(next,newFallback);
        auto run=[&](const std::string& key){ownersChanged(key,changedOwners_.data(),changedOwners_.data()+changedOwners_.size());};
        auto a=before.begin(),b=after.begin();
        while(a!=before.end()||b!=after.end()){changedOwners_.clear();
            if(b==after.end()||(a!=before.end()&&a->first<b->first)){for(size_t i:a->second)changedOwners_.push_back(&previous.placements[i]);run(a->first);++a;continue;}
            if(a==before.end()||b->first<a->first){for(size_t i:b->second)changedOwners_.push_back(&next.placements[i]);run(b->first);++b;continue;}
            const auto& old=a->second;const auto& current=b->second;size_t prefix=0,oldEnd=old.size(),newEnd=current.size();
            while(prefix<oldEnd&&prefix<newEnd&&samePlacement(previous.placements[old[prefix]],next.placements[current[prefix]]))++prefix;
            while(oldEnd>prefix&&newEnd>prefix&&samePlacement(previous.placements[old[oldEnd-1]],next.placements[current[newEnd-1]])){--oldEnd;--newEnd;}
            const bool exact=exactDiff_&&prefix<oldEnd&&prefix<newEnd;
            if(exact&&exactMiddle(previous,old,prefix,oldEnd,next,current,prefix,newEnd))++stats_.publishDiffs;
            else{if(exact){++stats_.publishDiffFallbacks;changedOwners_.clear();}
                for(size_t i=prefix;i<oldEnd;++i)changedOwners_.push_back(&previous.placements[old[i]]);
                for(size_t i=prefix;i<newEnd;++i)changedOwners_.push_back(&next.placements[current[i]]);}
            run(a->first);++a;++b;
        }
    }
    static constexpr unsigned InstanceCapacity=256;
    static constexpr unsigned InstanceStreamCapacity=8192;
    static std::string resourceKey(const Model& m){return m.key+"#"+std::to_string(m.contentRevision);}
    Resource* readyResource(const std::string& key)const {auto found=readyByKey_.find(key);return found==readyByKey_.end()?nullptr:found->second;}
    void refreshReady(const std::string& key){
        Resource* chosen=nullptr;auto desired=scene_?scene_->models.find(key):std::map<std::string,std::shared_ptr<const Model>>::const_iterator{};
        const std::string prefix=key+"#";
        for(auto i=resources_.lower_bound(prefix);i!=resources_.end()&&i->first.compare(0,prefix.size(),prefix)==0;++i){auto* r=i->second.get();if(!r->ready)continue;if(!chosen)chosen=r;if(scene_&&desired!=scene_->models.end()&&r->model->contentRevision==desired->second->contentRevision){chosen=r;break;}}
        auto* previous=readyResource(key);if(previous!=chosen){if(previous)modelChanged(*previous);if(chosen)modelChanged(*chosen);}
        if(chosen)readyByKey_[key]=chosen;else readyByKey_.erase(key);
    }
    void eraseResource(std::map<std::string,std::unique_ptr<Resource>>::iterator victim){
        const auto key=victim->second->model->key;modelChanged(*victim->second);if(victim->second->ready)--stats_.readyModels;
        if(victim->first==staging_)staging_.clear();retired_.push_back(std::move(victim->second));resources_.erase(victim);++stats_.evictions;refreshReady(key);if(!sharedBudget_)drainRetired();
    }
    void pruneTextures(){
        if(textureSweep_==textures_.end())textureSweep_=textures_.begin();
        unsigned visited=0;while(textureSweep_!=textures_.end()&&maintenanceAvailable()&&(!sharedBudget_||visited<16)){auto current=textureSweep_++;if(current->second.expired())textures_.erase(current);++visited;maintenanceStep();}
    }
    void expire(uint64_t frame){
        bool removed=false;
        while(!expiry_.empty()&&expiry_.begin()->first<=frame&&maintenanceAvailable()){const auto event=*expiry_.begin();expiry_.erase(expiry_.begin());auto found=resources_.find(event.second);++stats_.maintenanceVisits;maintenanceStep();
            if(found!=resources_.end()&&!found->second->wanted&&found->second->expires==event.first){eraseResource(found);removed=true;}}
        if(removed)pruneTextures();
    }
    void makeRoom(size_t bytes){
        drainRetired();
        // Expiry order is also last-use order. Avoid rescanning the complete
        // resident pool for every victim when admission reaches the cap.
        auto event=expiry_.begin();
        while(memory()+bytes>limits_.residentBytes&&event!=expiry_.end()&&maintenanceAvailable()){
            const auto identity=event->second;const auto expires=event->first;auto current=event++;++stats_.maintenanceVisits;maintenanceStep();
            auto victim=resources_.find(identity);if(victim==resources_.end()){expiry_.erase(current);continue;}
            auto& r=*victim->second;if(identity==staging_||r.wanted||r.expires!=expires||scene_->models.count(r.model->key))continue;
            expiry_.erase(current);eraseResource(victim);drainRetired();
        }
    }
    const std::string& resourceKeyOf(const Model& m){keyScratch_.assign(m.key);keyScratch_+='#';keyScratch_+=std::to_string(m.contentRevision);return keyScratch_;}
    void publish(std::shared_ptr<const Snapshot> scene,uint64_t frame){
        const auto start=std::chrono::steady_clock::now();
        struct Timer {Statistics& s;std::chrono::steady_clock::time_point t;~Timer(){const double e=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();s.publishMs+=e;s.publishPeakMs=std::max(s.publishPeakMs,e);}} timer{stats_,start};
        if(idleRelease_)for(auto& plan:plans_)if(plan.occupied&&planClock_-plan.used>idleTicks_){plan=Plan{};++stats_.planIdleReleases;}
        ++stats_.publications;auto previous=std::move(scene_);scene_=std::move(scene);coveredChanged();
        const bool unchangedGeometry=previous&&previous->preparedIdentity==previous.get()&&scene_->preparedIdentity==scene_.get()&&previous->geometryRevision&&previous->geometryRevision==scene_->geometryRevision;
        if(!unchangedGeometry){if(previous)snapshotChanged(*previous,*scene_);else changed();}
        if(scene_->preparedIdentity==scene_.get())groups_=&scene_->placementGroups;
        else {fallbackGroups_.clear();for(size_t i=0;i<scene_->placements.size();++i)fallbackGroups_[scene_->placements[i].modelKey].push_back(i);groups_=&fallbackGroups_;}
        if(unchangedGeometry){NorthlightStreaming::cpuRetirement().retire(previous,previous->retirementBytes);expire(frame);drainRetired();return;}
        // Reconcile desired model revisions, preserving all unchanged queues and
        // ready lookups. Never scan the retained-resource pool per publication.
        // Both model maps share key order: forward cursors find what find() would.
        if(previous){auto wanted=scene_->models.begin();for(const auto& kv:previous->models){
            if(exactDiff_){while(wanted!=scene_->models.end()&&wanted->first<kv.first)++wanted;}else wanted=scene_->models.find(kv.first);
            if(wanted!=scene_->models.end()&&wanted->first==kv.first&&wanted->second->contentRevision==kv.second->contentRevision)continue;
            auto found=resources_.find(exactDiff_?resourceKeyOf(*kv.second):resourceKey(*kv.second));if(found!=resources_.end()){auto& r=*found->second;r.wanted=false;r.lastUsed=lastUpdateFrame_;r.expires=r.lastUsed+limits_.retainFrames+1;expiry_.emplace(r.expires,found->first);if(found->first==staging_)eraseResource(found);}
            deferred_.erase(kv.first);
        }}
        // Remove canceled queue items (including revisions superseded in place).
        for(auto i=missing_.begin();i!=missing_.end();) {auto next=scene_->models.find(i->second);auto old=previous?previous->models.find(i->second):scene_->models.end();
            if(next==scene_->models.end()||(previous&&old!=previous->models.end()&&old->second->contentRevision!=next->second->contentRevision))i=missing_.erase(i);else ++i;}
        stats_.pendingModels=0;
        auto cursor=previous?previous->models.begin():scene_->models.end();
        for(const auto& kv:scene_->models){auto old=scene_->models.end();
            if(previous&&exactDiff_){while(cursor!=previous->models.end()&&cursor->first<kv.first)++cursor;if(cursor!=previous->models.end()&&cursor->first==kv.first)old=cursor;else old=previous->models.end();}
            else if(previous)old=previous->models.find(kv.first);
            const bool unchanged=previous&&old!=previous->models.end()&&old->second->contentRevision==kv.second->contentRevision;
            auto found=resources_.find(exactDiff_?resourceKeyOf(*kv.second):resourceKey(*kv.second));
            if(found!=resources_.end()){found->second->wanted=true;found->second->expires=0;if(!unchanged){found->second->lastUsed=frame;refreshReady(kv.first);}if(found->second->ready)continue;}
            ++stats_.pendingModels;if(unchanged)continue;++stats_.maintenanceVisits;
            if(found==resources_.end()){auto retry=deferred_.find(kv.first);missing_.emplace(retry==deferred_.end()?frame:std::max(frame,retry->second),kv.first);}
        }
        if(previous){size_t bytes=previous->retirementBytes;if(!bytes){bytes=previous->placements.capacity()*sizeof(Placement);for(const auto& kv:previous->models)bytes+=kv.second->bytes;}NorthlightStreaming::cpuRetirement().retire(previous,bytes);}
        expire(frame);drainRetired();
    }
    static bool sameAlpha(const Texture& t,const NorthlightGI::WorldMaterial& m){
        if(std::max(1u,m.width)!=t.width||std::max(1u,m.height)!=t.height)return false;
        for(size_t i=0;i<size_t(t.width)*t.height;++i){size_t j=i*4+3;const auto* old=t.owner?&t.owner->materials[t.material]:nullptr;unsigned a=old?(j<old->rgba.size()?old->rgba[j]:255):(t.preparedAlpha?(*t.preparedAlpha)[i]:t.alpha[i]),b=j<m.rgba.size()?m.rgba[j]:255;if(a!=b)return false;}return true;
    }
    static uint64_t mix(uint64_t h,uint64_t v){return (h^v)*1099511628211ull;}
    static Instance transform(const Placement& p){Instance x{};for(unsigned r=0;r<3;++r){for(unsigned c=0;c<3;++c)x.row[r*4+c]=p.matrix[r*3+c];x.row[r*4+3]=r==0?p.translation.x:r==1?p.translation.y:p.translation.z;}return x;}
    static bool visible(const Placement& p,const Batch& b,const float* matrix){Vec3 lo,hi;transformBounds(p,b.low,b.high,lo,hi);return !NorthlightShadowBounds::directionalClipReject(lo,hi,matrix);}
    const Plan& prepare(const float* matrix)const {
        const bool installed=asyncCount_&&joinMatrix(matrix); /* 0.3.152: this matrix's worker build (counted as a build, not a hit) */
        Plan* result=nullptr;
        for(auto& plan:plans_)if(plan.occupied&&std::memcmp(plan.matrix,matrix,sizeof(plan.matrix))==0){
            plan.used=++planClock_;if(plan.valid){if(!installed)++stats_.planHits;return plan;}result=&plan;break;}
        bool reuseMatrix=result!=nullptr;
        if(!result){result=&planSlot();result->used=++planClock_;}
        const auto buildStart=std::chrono::steady_clock::now();
        auto& previous=*result;previous.valid=false;++stats_.planBuilds;
        // Debug self-check: the 0.3.136 build from a copy of the same previous
        // plan, compared field by field (strict) with the fast build.
        Plan reference;const bool check=selfCheck_&&fastPlans_;
        if(check){Plan copy=previous;build(sync_,copy,reuseMatrix,matrix,false,reference);sync_.counters={};}
        const double splitBefore=stats_.planReuseMs+stats_.planRebuildMs;
        Plan next;build(sync_,previous,reuseMatrix,matrix,fastPlans_,next);addCounters(sync_.counters);
        if(check){++stats_.planSelfChecks;if(planBytes(next,true)!=planBytes(reference,true))++stats_.planSelfCheckMismatches;}
        previous=std::move(next);
        const double spent=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-buildStart).count();stats_.planMs+=spent;stats_.planPeakMs=std::max(stats_.planPeakMs,spent);
        stats_.planWalkMs+=std::max(0.,spent-(stats_.planReuseMs+stats_.planRebuildMs-splitBefore));
        return previous;
    }
    static constexpr size_t SpareChunks=64,SpareChunkInstances=256;
    // Nodes taken from `previous` go back to it with their old values.
    static void returnNodes(BuildContext& ctx,Plan& previous,Plan& next){
        for(auto e=ctx.nodeMoves.rbegin();e!=ctx.nodeMoves.rend();++e){auto node=next.models.extract(e->at);node.mapped()=e->old;previous.models.insert(std::move(node));}
        ctx.nodeMoves.clear();
    }
    // One plan build from `previous` (the slot's old contents). On success
    // previous is consumed: its chunks move (fast path: also its slice
    // nodes). A throwing build undoes both (0.3.152: the plan worker's failed
    // item gets its exact old entry back). The result is identical either way.
    void build(BuildContext& ctx,Plan& previous,bool reuseMatrix,const float* matrix,bool fast,Plan& next)const {
        next.occupied=true;next.fullRebuild=false;next.used=previous.used;std::memcpy(next.matrix,matrix,sizeof(next.matrix));
        // Reserve aggregate storage once; unchanged slices need only linear
        // copies, not placement bounds, per-batch tests or transform hashing.
        // Chunk and undo capacity for every group: moves out of previous never throw.
        const size_t groupCount=scene_&&groups_?groups_->size():0;
        next.commands.reserve(previous.commands.size());next.chunks.reserve(groupCount);
        ctx.chunkMoves.clear();ctx.nodeMoves.clear();ctx.chunkMoves.reserve(groupCount);ctx.nodeMoves.reserve(groupCount);ctx.spare.reserve(SpareChunks);
        try{buildGroups(ctx,previous,reuseMatrix,matrix,fast,next);}
        catch(...){returnNodes(ctx,previous,next);for(const auto& move:ctx.chunkMoves)previous.chunks[move.from]=std::move(next.chunks[move.to]);ctx.chunkMoves.clear();throw;}
        ctx.chunkMoves.clear();ctx.nodeMoves.clear();
        for(auto& chunk:previous.chunks)if(chunk.capacity()&&chunk.capacity()<=SpareChunkInstances&&ctx.spare.size()<SpareChunks)ctx.spare.push_back(std::move(chunk));
    }
    void buildGroups(BuildContext& ctx,Plan& previous,bool reuseMatrix,const float* matrix,bool fast,Plan& next)const {
        auto& counters=ctx.counters;auto& candidates=ctx.candidates;auto& transforms=ctx.transforms;auto& boxes=ctx.boxes;
        uint64_t h=1469598103934665603ull;if(scene_)h=mix(h,generation_);
        // groups_, readyByKey_ and plan.models share std::less<std::string>
        // order, so a forward cursor finds exactly what find() would.
        // affine: the matrix precondition of the bounds tests, checked once.
        const bool affine=fast&&NorthlightShadowBounds::affineLightMatrix(matrix);auto ready=readyByKey_.begin();auto cursor=previous.models.begin();
        if(scene_&&groups_)for(const auto& group:*groups_){
            Resource* r=nullptr;
            if(fast){int order=1;while(ready!=readyByKey_.end()&&(order=ready->first.compare(group.first))<0)++ready;if(ready!=readyByKey_.end()&&!order)r=ready->second;}
            else r=readyResource(group.first);
            if(!r)continue;const auto& m=*r->model;
#ifdef STATIC_SHADOW_GPU_TEST
            if(ctx.failAfter!=SIZE_MAX&&!ctx.failAfter--)throw std::bad_alloc();
#endif
            ModelSlice slice;slice.commandFirst=next.commands.size();slice.instanceFirst=next.instanceCount;std::vector<Instance> chunk;
            auto old=previous.models.end();
            if(fast){int order=1;while(cursor!=previous.models.end()&&(order=cursor->first.compare(group.first))<0)++cursor;if(cursor!=previous.models.end()&&!order)old=cursor;}
            else old=previous.models.find(group.first);
            const bool reused=reuseMatrix&&!previous.fullRebuild&&!previous.dirtyModels.count(group.first)&&old!=previous.models.end();
            const auto splitStart=std::chrono::steady_clock::now(); /* 0.3.152 split: two clock reads per ready model */
            if(reused){
                const auto& cached=old->second;
                for(size_t c=cached.commandFirst;c<cached.commandFirst+cached.commandCount;++c){auto command=previous.commands[c];command.first=slice.instanceFirst+(command.first-cached.instanceFirst);next.commands.push_back(command);}
                if(cached.instanceCount){ctx.chunkMoves.push_back({cached.chunk,next.chunks.size()});next.chunks.push_back(std::move(previous.chunks[cached.chunk]));} /* reserved: no throw */
                slice.signature=cached.signature;slice.culled=cached.culled;slice.bounds=cached.bounds;++counters.modelPlanReuses;
            }else{
                ++counters.modelPlanBuilds;if(!ctx.spare.empty()){chunk=std::move(ctx.spare.back());ctx.spare.pop_back();chunk.clear();}
                uint64_t keyHash=1469598103934665603ull;for(unsigned char c:group.first)keyHash=mix(keyHash,c);
                uint64_t modelHash=1469598103934665603ull;
                candidates.clear();
                for(size_t index:group.second){const auto& p=scene_->placements[index];if(fast?coveredAt(ctx,index):covered(p))continue;++counters.placementTests;
                    if(!coarseVisible(p,*r,matrix,affine)){slice.culled+=m.batches.size();continue;}
                    candidates.push_back({&p,transform(p)});
                }
                if(!candidates.empty())chunk.reserve(candidates.size()*m.batches.size()); /* one allocation per rebuilt slice at most */
                if(!candidates.empty())for(size_t b=0;b<m.batches.size();++b){const auto& batch=m.batches[b];transforms[0].clear();transforms[1].clear();boxes[0].clear();boxes[1].clear();
                    for(const auto& candidate:candidates){const auto& p=*candidate.placement;Vec3 lo,hi;transformBounds(p,batch.low,batch.high,lo,hi);++counters.batchTests;
                        if(affine?NorthlightShadowBounds::directionalClipRejectAffine(lo,hi,matrix):NorthlightShadowBounds::directionalClipReject(lo,hi,matrix)){++slice.culled;continue;}
                        const unsigned path=(affine?NorthlightShadowBounds::depthFullyInsideAffine(lo,hi,matrix):NorthlightShadowBounds::depthFullyInside(lo,hi,matrix))?1:0;transforms[path].push_back(candidate.instance);boxes[path].push_back({lo,hi});slice.bounds.add(lo,hi);
                        modelHash=mix(modelHash,p.uid);modelHash=mix(modelHash,p.category);modelHash=mix(modelHash,keyHash);modelHash=mix(modelHash,m.contentRevision);modelHash=mix(modelHash,b);
                        for(float f:candidate.instance.row){uint32_t bits;std::memcpy(&bits,&f,4);modelHash=mix(modelHash,bits);}
                    }
                    for(unsigned path=0;path<2;++path){const auto& values=transforms[path];const size_t first=slice.instanceFirst+chunk.size();
                        chunk.insert(chunk.end(),values.begin(),values.end());
                        for(size_t offset=0;offset<values.size();offset+=InstanceCapacity){const unsigned count=unsigned(std::min<size_t>(InstanceCapacity,values.size()-offset));CasterBounds box;for(unsigned n=0;n<count;++n)box.add(boxes[path][offset+n].first,boxes[path][offset+n].second);next.commands.push_back({r,b,first+offset,count,path,box,nullptr});}
                    }
                }
                slice.signature=modelHash;
            }
            (reused?counters.reuseMs:counters.rebuildMs)+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-splitStart).count();
            slice.commandCount=next.commands.size()-slice.commandFirst;
            if(slice.commandCount){if(!reused)next.chunks.push_back(std::move(chunk)); /* reserved: no throw; moving keeps the data pointers */
                const auto& stored=next.chunks.back();slice.chunk=uint32_t(next.chunks.size()-1);slice.instanceCount=stored.size();next.instanceCount+=slice.instanceCount;
                for(size_t c=slice.commandFirst;c<next.commands.size();++c)next.commands[c].data=stored.data()+(next.commands[c].first-slice.instanceFirst);}
            else if(chunk.capacity()&&ctx.spare.size()<SpareChunks)ctx.spare.push_back(std::move(chunk));
            next.culled+=slice.culled;
            // Empty/invisible groups do not contribute to the shadow image.
            // Canonical model order and exact per-instance data remain stable.
            if(slice.commandCount)h=mix(h,slice.signature);
            if(!next.fullRebuild){const size_t bytes=modelMetadataBytes(group.first);
                if(bytes>planMetadataLimit()||next.metadataBytes>planMetadataLimit()-bytes){returnNodes(ctx,previous,next);next.models.clear();next.metadataBytes=0;next.fullRebuild=true;++counters.modelPlanFallbacks;}
                else if(fast&&old!=previous.models.end()){cursor=std::next(old);auto node=previous.models.extract(old);ctx.nodeMoves.push_back({{},node.mapped()});node.mapped()=slice;ctx.nodeMoves.back().at=next.models.insert(next.models.end(),std::move(node));next.metadataBytes+=bytes;}
                else{if(fast)next.models.emplace_hint(next.models.end(),group.first,slice);else next.models.emplace(group.first,slice);next.metadataBytes+=bytes;}
            }
        }
        next.signature=h;next.valid=true;
    }
    // Field-exact plan serialization; resources by key/revision plus whether
    // each is this cache's current ready resource for its key.
    // Field-exact plan serialization; resources by key/revision plus whether
    // each is this cache's current ready resource for its key. strict adds the
    // bookkeeping that never reaches a draw (culled count, empty slices, caps).
    // Field-exact plan serialization; resources by key/revision plus whether
    // each is this cache's current ready resource for its key. strict adds the
    // bookkeeping that never reaches a draw (culled count, empty slices, caps).
    std::string planBytes(const Plan& p,bool strict)const {
        std::string out;auto put=[&](const void* v,size_t n){out.append(static_cast<const char*>(v),n);};
        auto bounds=[&](const CasterBounds& b){put(&b.low,sizeof b.low);put(&b.high,sizeof b.high);put(&b.bounded,1);};
        put(&p.signature,8);put(&p.valid,1);const uint64_t sizes[]={p.commands.size(),p.instanceCount};put(sizes,sizeof sizes);
        if(strict){put(&p.culled,8);put(&p.fullRebuild,1);const uint64_t more[]={p.models.size(),p.dirtyModels.size(),p.metadataBytes,p.dirtyBytes};put(more,sizeof more);}
        for(const auto& c:p.commands){const auto& m=*c.resource->model;put(m.key.data(),m.key.size()+1);put(&m.contentRevision,8);const bool current=readyResource(m.key)==c.resource;put(&current,1);const uint64_t f[]={c.batch,c.first,c.count,c.path};put(f,sizeof f);bounds(c.bounds);}
        for(const auto& c:p.commands)put(c.data,c.count*sizeof(Instance)); /* commands tile the instances in order */
        for(const auto& kv:p.models)if(strict||kv.second.commandCount){put(kv.first.data(),kv.first.size()+1);const auto& x=kv.second;const uint64_t f[]={x.commandFirst,x.commandCount,x.instanceFirst,x.instanceCount,x.signature,strict?x.culled:0};put(f,sizeof f);bounds(x.bounds);}
        return out;
    }
    size_t memory()const {return geometryBytes_+textureBytes_;}
    bool admission(size_t n)const {const size_t used=memory();return n<=limits_.residentBytes&&used<=limits_.residentBytes-n&&(!admit_||admit_(n*2));} // managed resources have a host copy
    bool shaders(IDirect3DDevice9* d){
        if(vs_&&ps_&&decl_)return true;
        D3DCAPS9 caps{};if(FAILED(d->GetDeviceCaps(&caps))||caps.VertexShaderVersion<D3DVS_VERSION(3,0))return false;
        const D3DVERTEXELEMENT9 decl[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
        if(FAILED(d->CreateVertexShader(kStaticCasterVSShader,&vs_))||FAILED(d->CreatePixelShader(kStaticCasterPSShader,&ps_))||FAILED(d->CreateVertexDeclaration(decl,&decl_))){release(vs_);release(ps_);release(decl_);return false;}
        // Optional performance variants: any creation failure keeps .95 path.
        if(FAILED(d->CreatePixelShader(kStaticCasterFastPSShader,&fastPS_)))release(fastPS_);
        if(FAILED(d->CreatePixelShader(kStaticCasterOpaqueFastPSShader,&opaqueFastPS_)))release(opaqueFastPS_);
        if(FAILED(d->CreatePixelShader(kStaticCasterOpaquePSShader,&opaquePS_)))release(opaquePS_);
        const D3DVERTEXELEMENT9 idecl[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},{1,0,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_TEXCOORD,1},{1,16,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_TEXCOORD,2},{1,32,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_TEXCOORD,3},D3DDECL_END()};
        canInstance_=caps.MaxStreams>=2&&SUCCEEDED(d->CreateVertexShader(kStaticCasterInstancedVSShader,&instancedVS_))&&SUCCEEDED(d->CreateVertexDeclaration(idecl,&instancedDecl_));
        if(canInstance_){
            // Append across model/material groups and cascades. DISCARD on wrap
            // gives queued draws their own backing store; NOOVERWRITE only ever
            // touches previously unused ranges, so no fence or GPU wait is needed.
            instanceCapacity_=(caps.DevCaps2&D3DDEVCAPS2_STREAMOFFSET)?InstanceStreamCapacity:InstanceCapacity;
            if(FAILED(d->CreateVertexBuffer(sizeof(Instance)*instanceCapacity_,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&instances_,nullptr))){
                release(instances_);instanceCapacity_=InstanceCapacity;
                canInstance_=SUCCEEDED(d->CreateVertexBuffer(sizeof(Instance)*instanceCapacity_,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&instances_,nullptr));
            }
            instanceCursor_=0;
        }
        if(!canInstance_){release(instancedVS_);release(instancedDecl_);release(instances_);}return true;
    }
    bool begin(IDirect3DDevice9* d,const std::shared_ptr<const Model>& model,uint64_t frame){
        const size_t vb=model->vertices.size()*sizeof(Vertex),ib=model->indexCount()*(model->index16?2:4);
        makeRoom(vb+ib);if((sharedBudget_&&!sharedBudget_->available())||!vb||!ib||vb>UINT_MAX||ib>UINT_MAX||!admission(vb+ib))return false;
        auto r=std::make_unique<Resource>();r->model=model;r->vertexCount=UINT(model->vertices.size());r->bytes=vb+ib;r->lastUsed=frame;r->wanted=true;
        r->boundsValid=!model->batches.empty();
        r->low={INFINITY,INFINITY,INFINITY};r->high={-INFINITY,-INFINITY,-INFINITY};
        for(const auto& b:model->batches){
            if(!std::isfinite(b.low.x)||!std::isfinite(b.low.y)||!std::isfinite(b.low.z)||!std::isfinite(b.high.x)||!std::isfinite(b.high.y)||!std::isfinite(b.high.z)||b.low.x>b.high.x||b.low.y>b.high.y||b.low.z>b.high.z)r->boundsValid=false;
            r->low={std::min(r->low.x,b.low.x),std::min(r->low.y,b.low.y),std::min(r->low.z,b.low.z)};
            r->high={std::max(r->high.x,b.high.x),std::max(r->high.y,b.high.y),std::max(r->high.z,b.high.z)};
        }
        if(FAILED(d->CreateVertexBuffer(UINT(vb),D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,&r->vb,nullptr))||FAILED(d->CreateIndexBuffer(UINT(ib),D3DUSAGE_WRITEONLY,model->index16?D3DFMT_INDEX16:D3DFMT_INDEX32,D3DPOOL_MANAGED,&r->ib,nullptr)))return false;
        r->accounted=&geometryBytes_;geometryBytes_+=r->bytes;
        // Add resource before material admission so all allocated bytes count.
        const std::string identity=resourceKey(*model);resources_[identity]=std::move(r);Resource& dest=*resources_.at(identity);
        dest.textures.resize(model->materials.size());
        for(const auto& mat:model->materials)dest.opaque.push_back(mat.alphaCutoff<=0||((!mat.width||!mat.height||mat.rgba.empty())&&mat.alphaCutoff<=1));
        staging_=identity;return true;
    }
    bool advance(IDirect3DDevice9* d,Resource& r,size_t& budget,const std::chrono::steady_clock::time_point& start){
        auto timedOut=[&](){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()>=limits_.uploadMs||(sharedBudget_&&!sharedBudget_->available());};
        const Model& m=*r.model;
        while(budget&&!timedOut()){
            size_t total=m.vertices.size()*sizeof(Vertex);
            if(r.vertexDone<total){size_t n=std::min({budget,total-r.vertexDone,size_t(64u<<10)});void* out=nullptr;if(FAILED(r.vb->Lock(UINT(r.vertexDone),UINT(n),&out,0))||!out)return false;std::memcpy(out,reinterpret_cast<const char*>(m.vertices.data())+r.vertexDone,n);if(FAILED(r.vb->Unlock()))return false;r.vertexDone+=n;budget-=n;if(sharedBudget_)sharedBudget_->consume(n);continue;}
            total=m.indexCount()*(m.index16?2:4);
            if(r.indexDone<total){const size_t stride=m.index16?2:4;size_t n=std::min({budget,total-r.indexDone,size_t(64u<<10)})/stride*stride;if(!n)break;void* out=nullptr;if(FAILED(r.ib->Lock(UINT(r.indexDone),UINT(n),&out,0))||!out)return false;if(m.uploadPrepared&&m.index16)std::memcpy(out,reinterpret_cast<const char*>(m.uploadIndices16.data())+r.indexDone,n);else if(m.index16){auto* dst=static_cast<uint16_t*>(out);for(size_t i=0;i<n/2;++i)dst[i]=uint16_t(m.indices[r.indexDone/2+i]);}else std::memcpy(out,m.indices.data()+r.indexDone/4,n);if(FAILED(r.ib->Unlock()))return false;r.indexDone+=n;budget-=n;if(sharedBudget_)sharedBudget_->consume(n);continue;}
            bool all=true;
            for(size_t i=0;i<r.textures.size();++i){
                if(timedOut()){all=false;break;}
                if(!r.textures[i]){const auto& mat=m.materials[i];uint64_t key=m.materialKeys[i];auto range=textures_.equal_range(key);for(auto it=range.first;it!=range.second;++it){auto candidate=it->second.lock();if(candidate&&sameAlpha(*candidate,mat)){r.textures[i]=std::move(candidate);break;}}
                    if(!r.textures[i]){auto texture=std::make_shared<Texture>();texture->key=key;texture->owner=r.model;texture->material=i;texture->width=std::max(1u,mat.width);texture->height=std::max(1u,mat.height);texture->bytes=size_t(texture->width)*texture->height*4;makeRoom(texture->bytes);
                        if(sharedBudget_&&!sharedBudget_->available())return true;
                        const bool admitted=admission(texture->bytes);
                        if(!admitted&&sharedBudget_&&!maintenanceLeft_)return true;
                        if(!admitted||FAILED(d->CreateTexture(texture->width,texture->height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture->gpu,nullptr)))return false;
                        texture->accounted=&textureBytes_;textureBytes_+=texture->bytes;if(m.uploadPrepared){texture->preparedAlpha=m.uploadAlpha[i];texture->owner.reset();}else texture->alpha.resize(size_t(texture->width)*texture->height);textures_.emplace(key,texture);r.textures[i]=std::move(texture);
                    }
                }
                auto& t=*r.textures[i];if(t.ready)continue;all=false;const size_t rowBytes=size_t(t.width)*4;if(rowBytes>budget)break;const unsigned rows=unsigned(std::min({size_t(t.height-t.nextRow),budget/rowBytes,size_t(64)}));
                RECT rect{0,LONG(t.nextRow),LONG(t.width),LONG(t.nextRow+rows)};D3DLOCKED_RECT lock{};if(FAILED(t.gpu->LockRect(0,&lock,&rect,0))||!lock.pBits)return false;const auto& mat=m.materials[i];
                for(unsigned y=0;y<rows;++y){auto* dst=reinterpret_cast<uint32_t*>(static_cast<char*>(lock.pBits)+y*lock.Pitch);const size_t offset=size_t(t.nextRow+y)*rowBytes;
                    if(m.uploadPrepared&&offset+rowBytes<=mat.rgba.size())std::memcpy(dst,mat.rgba.data()+offset,rowBytes);
                    else if(m.uploadPrepared)std::fill(dst,dst+t.width,0xffffffffu);
                    else for(unsigned x=0;x<t.width;++x){const size_t j=offset+x*4;const unsigned a=j+3<mat.rgba.size()?mat.rgba[j+3]:255;dst[x]=(a<<24)|0x00ffffff;t.alpha[size_t(t.nextRow+y)*t.width+x]=uint8_t(a);}}
                if(FAILED(t.gpu->UnlockRect(0)))return false;t.nextRow+=rows;t.ready=t.nextRow==t.height;if(t.ready)t.owner.reset();budget-=rows*rowBytes;if(sharedBudget_)sharedBudget_->consume(rows*rowBytes);break;
            }
            if(all){
                if(m.drawModel){auto owner=r.model;r.model=m.drawModel;retireCpu(owner);r.ready=true;break;}
                auto compact=std::make_shared<Model>();compact->key=m.key;compact->contentRevision=m.contentRevision;compact->batches=m.batches;compact->index16=m.index16;
                for(const auto& original:m.materials){NorthlightGI::WorldMaterial mat;mat.alphaCutoff=original.alphaCutoff;mat.addressU=original.addressU;mat.addressV=original.addressV;compact->materials.push_back(std::move(mat));}
                r.model=std::move(compact);r.ready=true;break;}
        }return true;
    }
public:
    GpuCache()=default;
    explicit GpuCache(Limits l):limits_(l){}
    ~GpuCache(){reset();}
    GpuCache(const GpuCache&)=delete;GpuCache& operator=(const GpuCache&)=delete;
    // Existing local geometry owns only exact placements, never bounds/name guesses.
    // Idempotent across frames: a repeated owner set does not invalidate caches.
    void setCoveredPlacements(const std::vector<Placement>& placements){
        mutate();auto next=std::make_shared<OwnerMap>();addOwners(*next,placements);setCoveredOwners(std::move(next));
    }
    // Exact ownership changes only invalidate cascades the placement can
    // touch; unchanged owners need no bounds work. Same calls and order as the
    // 0.3.136 per-element search: removed owners first, then added owners.
    size_t lastOwnerChanges_=0; /* ownerChanged calls of the last diff (log) */
    size_t lastOwnerChanges()const{return lastOwnerChanges_;}
    void setCoveredOwners(std::shared_ptr<const OwnerMap> next){
        mutate();if(next==covered_)return;
        static const OwnerMap none;
        const OwnerMap& from=covered_?*covered_:none;const OwnerMap& to=next?*next:none;
        // Grouped: collect, stable-sort by model, then one pass per model. The
        // final plan state does not depend on the order of different models
        // (per plan, dirty keys and the metadata-cap fallback are order-free).
        size_t changes=0;
        if(groupedOwners_){changedOwners_.clear();auto collect=[&](const Placement& p){changedOwners_.push_back(&p);};
            changes=ownerDifferences(from,to,collect)+ownerDifferences(to,from,collect);
            std::stable_sort(changedOwners_.begin(),changedOwners_.end(),[](const Placement* x,const Placement* y){return x->modelKey<y->modelKey;});
            for(size_t i=0;i<changedOwners_.size();){size_t j=i+1;while(j<changedOwners_.size()&&changedOwners_[j]->modelKey==changedOwners_[i]->modelKey)++j;ownersChanged(changedOwners_[i]->modelKey,changedOwners_.data()+i,changedOwners_.data()+j);i=j;}}
        else changes=ownerDifferences(from,to,[&](const Placement& p){ownerChanged(p);})+ownerDifferences(to,from,[&](const Placement& p){ownerChanged(p);});
        lastOwnerChanges_=changes; /* hitch: ownerChanged count of the last diff (log); grouped mode counts the same differences */
        if(!changes&&covered_)return;
        // The replaced map may be the last owner of ~3000 nodes: free it on
        // the CPU reaper; refusal destroys it here exactly as before.
        auto old=std::move(covered_);covered_=std::move(next);coveredChanged();
        if(old)NorthlightStreaming::cpuRetirement().retire(old,old->size()*(sizeof(Placement)+128));
    }
    void setAdmission(std::function<bool(size_t)> f){mutate();admit_=std::move(f);}
    const Statistics& stats()const{return stats_;}
    void reset(){mutate(true);++epoch_;coveredChanged();changed();groups_=nullptr;fallbackGroups_.clear();covered_.reset();deferred_.clear();missing_.clear();expiry_.clear();readyByKey_.clear();lastUpdateFrame_=0;resources_.clear();drainRetired(true);textures_.clear();textureSweep_=textures_.end();scene_.reset();staging_.clear();release(vs_);release(instancedVS_);release(ps_);release(fastPS_);release(opaqueFastPS_);release(opaquePS_);release(decl_);release(instancedDecl_);release(instances_);device_=nullptr;map_.clear();generation_=0;canInstance_=false;instanceCapacity_=instanceCursor_=0;stats_.residentBytes=0;stats_.readyModels=0;stats_.pendingModels=0;}
    bool update(IDirect3DDevice9* d,std::shared_ptr<const Snapshot> scene,uint64_t frame,bool allowUploads=true,NorthlightStreaming::Budget* sharedBudget=nullptr){
        mutate();sharedBudget_=sharedBudget;maintenanceLeft_=64;
        const auto start=std::chrono::steady_clock::now();stats_.frameBytes=0;stats_.uploadMs=0;if(!scene||!d)return true;
        if((device_&&device_!=d)||(scene_&&(scene->map!=map_||scene->mapGeneration!=generation_))){auto owners=std::move(covered_);reset();covered_=std::move(owners);coveredChanged();}
        device_=d;map_=scene->map;generation_=scene->mapGeneration;
        stats_.publishFrameMs=0;
        if(scene_.get()!=scene.get()){publish(std::move(scene),frame);stats_.publishFrameMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}else {expire(frame);drainRetired();if(textureSweep_!=textures_.end())pruneTextures();}
        lastUpdateFrame_=frame;
        // The warm path performs no model/material map walk and no allocations.
        if(!allowUploads||(sharedBudget_&&!sharedBudget_->available())||(staging_.empty()&&(missing_.empty()||missing_.begin()->first>frame))){stats_.residentBytes=memory();stats_.instancing=canInstance_;return true;}
        size_t budget=sharedBudget_?std::min(limits_.uploadBytes,sharedBudget_->remainingBytes()):limits_.uploadBytes;const size_t initialBudget=budget;bool ok=true;
        if(!shaders(d)){++stats_.failures;return false;}
        while(budget&&(!sharedBudget_||sharedBudget_->available())&&std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<limits_.uploadMs){
            if(staging_.empty()){
                if(missing_.empty()||missing_.begin()->first>frame)break;const auto name=missing_.begin()->second;missing_.erase(missing_.begin());auto next=scene_->models.find(name);if(next==scene_->models.end())continue;
                if(!begin(d,next->second,frame)){if(sharedBudget_&&(!sharedBudget_->available()||!maintenanceLeft_)){missing_.emplace(frame+1,name);break;}deferred_[name]=frame+60;missing_.emplace(frame+60,name);++stats_.failures;ok=false;break;}
            }
            auto found=resources_.find(staging_);if(found==resources_.end()){staging_.clear();break;}
            const size_t before=budget;if(!advance(d,*found->second,budget,start)){const auto name=found->second->model->key;deferred_[name]=frame+60;missing_.emplace(frame+60,name);eraseResource(found);++stats_.failures;ok=false;break;}
            if(found->second->ready){const auto name=found->second->model->key;modelChanged(*found->second);++stats_.uploads;++stats_.readyModels;--stats_.pendingModels;deferred_.erase(name);staging_.clear();refreshReady(name);
                // A successfully uploaded revision replaces its old ready fallback.
                const std::string prefix=name+"#";for(auto old=resources_.lower_bound(prefix);old!=resources_.end()&&old->first.compare(0,prefix.size(),prefix)==0;){if(old!=found&&!old->second->wanted){auto victim=old++;eraseResource(victim);}else ++old;}pruneTextures();
            }else if(before==budget)break;
        }
        stats_.frameBytes=initialBudget-budget;stats_.uploadedBytes+=stats_.frameBytes;stats_.residentBytes=memory();stats_.peakBytes=std::max(stats_.peakBytes,stats_.residentBytes);
        stats_.uploadMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();stats_.instancing=canInstance_;return ok;
    }
    bool coverage(uint64_t uid,uint32_t category,const std::string& modelKey,uint64_t expectedRevision=0)const {if(!scene_)return false;auto* r=readyResource(modelKey);if(!r)return false;auto expected=scene_->models.find(modelKey);if(expected==scene_->models.end()||r->model->contentRevision!=expected->second->contentRevision||(expectedRevision&&r->model->contentRevision!=expectedRevision))return false;for(const auto& p:scene_->placements)if(p.uid==uid&&p.category==category&&p.modelKey==modelKey&&!covered(p))return true;return false;}
    bool coverage(const Placement& placement,uint64_t revision)const {
        if(!coverage(placement.uid,placement.category,placement.modelKey,revision))return false;
        for(const auto& current:scene_->placements)if(current.uid==placement.uid&&current.category==placement.category&&current.modelKey==placement.modelKey)
            return std::memcmp(current.matrix,placement.matrix,sizeof(current.matrix))==0&&current.translation.x==placement.translation.x&&current.translation.y==placement.translation.y&&current.translation.z==placement.translation.z;
        return false;
    }
    uint64_t signature(const float* matrix)const {return prepare(matrix).signature;}
    // Frees an early prepared plan that was never used (fewer slots to scan on
    // invalidation). Only cache state: a later prepare(matrix) builds as usual.
    void discardPlan(const float* matrix){mutate();for(auto& plan:plans_)if(plan.occupied&&std::memcmp(plan.matrix,matrix,sizeof(plan.matrix))==0){plan=Plan{};++stats_.planDiscards;}}
    // True when prepare(matrix) would return an already valid plan (no build).
    // 0.3.152: joins a kicked item first, so a finished worker build reads as ready here (the
    // synchronous path would say false until its own build). Only NorthlightStaticPrebuild uses
    // this (Enabled=false): if re-enabled, its hit/miss counts would include worker installs.
    bool planReady(const float* matrix)const {joinPlan(matrix);return validPlan(matrix);}
    // 0.3.152: joins the plan worker's item for matrix, if any (installs or steals it back).
    void joinPlan(const float* matrix)const {if(asyncCount_)joinMatrix(matrix);}
#ifdef STATIC_SHADOW_GPU_TEST
    std::string planDigest(const float* matrix,bool strict)const {return planBytes(prepare(matrix),strict);}
    // Complete invalidation state of every slot without preparing anything.
    std::string planStateDigest(bool used=true)const {std::string out; /* used=false: LRU stamps left out */
        for(const auto& p:plans_){out.push_back(char(p.occupied));out.push_back(char(p.valid));out.push_back(char(p.fullRebuild));out.append(reinterpret_cast<const char*>(p.matrix),64);
            const uint64_t n[]={p.metadataBytes,p.dirtyBytes,used?p.used:0};out.append(reinterpret_cast<const char*>(n),sizeof n);for(const auto& k:p.dirtyModels){out+=k;out.push_back(0);}out+="|";if(p.occupied&&p.valid)out+=planBytes(p,true);}
        const uint64_t st[]={stats_.planInvalidations,stats_.modelPlanFallbacks};out.append(reinterpret_cast<const char*>(st),sizeof st);return out;}
    void selfCheckPlans(){mutate();selfCheck_=true;} /* tests: always compare with the reference build */
    // Tests: the plan worker on/off regardless of cores, a worker delay per item
    // (steal-back), and a bad_alloc after failAfter ready models in items of failMask.
    void testAsync(bool on,unsigned delayUs=0,unsigned failMask=0,size_t failAfter=0,unsigned dequeueDelayUs=0){mutate();asyncPlans_=on;testDelayUs_=delayUs;testFailMask_=failMask;testFailAfter_=failAfter;testDequeueDelayUs_=dequeueDelayUs;}
    unsigned testRetractions()const{return worker_?worker_->retractions():0;}
    unsigned asyncPending()const{return asyncCount_;}
#endif
    // Per-model content of a rendered cache: slice signature plus the world AABB
    // of everything the slice drew. Valid only for the same reset epoch and
    // instancing mode, and only when the plan kept per-model metadata.
    struct ContentRecord {
        struct Slice {std::string key;uint64_t signature=0,revision=0;CasterBounds bounds;};
        bool valid=false,instancing=false;uint64_t epoch=0;float matrix[16]{};std::vector<Slice> slices;
        bool contains(const std::string& key,uint64_t revision)const{auto found=std::lower_bound(slices.begin(),slices.end(),key,[](const Slice& s,const std::string& k){return s.key<k;});return found!=slices.end()&&found->key==key&&found->revision==revision;}
    };
    void record(const float* matrix,ContentRecord& out)const {
        // Assign in place: unchanged keys reuse their string storage, no allocation.
        out.valid=false;const auto& plan=prepare(matrix);if(plan.fullRebuild){out.slices.clear();return;}
        size_t n=0;for(const auto& kv:plan.models)if(kv.second.commandCount){if(n==out.slices.size())out.slices.emplace_back();auto& slice=out.slices[n++];if(slice.key!=kv.first)slice.key=kv.first;slice.signature=kv.second.signature;slice.revision=plan.commands[kv.second.commandFirst].resource->model->contentRevision;slice.bounds=kv.second.bounds;}
        out.slices.resize(n);
        out.epoch=epoch_;out.instancing=canInstance_;std::memcpy(out.matrix,matrix,sizeof(out.matrix));out.valid=true;
    }
    // Boxes covering the old and new versions of every added, removed or
    // changed non-empty model slice: the recorded union for the old version,
    // every drawn instance-batch of the new one. False: no bound, draw all.
    bool changedBounds(const float* matrix,const ContentRecord& old,std::vector<CasterBounds>& changed,size_t& models)const {
        changed.clear();models=0;if(!old.valid||old.epoch!=epoch_||old.instancing!=canInstance_||std::memcmp(old.matrix,matrix,sizeof(old.matrix))!=0)return false;
        return changedIn(prepare(matrix),old,changed,models);
    }
    static bool changedIn(const Plan& plan,const ContentRecord& old,std::vector<CasterBounds>& changed,size_t& models){
        if(plan.fullRebuild)return false;
        auto current=[&](const ModelSlice& slice){if(!slice.bounds.bounded)return false;
            for(size_t c=slice.commandFirst;c<slice.commandFirst+slice.commandCount;++c){const auto& command=plan.commands[c];const auto& batch=command.resource->model->batches[command.batch];
                for(unsigned n=0;n<command.count;++n){const auto& row=command.data[n].row;Placement p;for(unsigned r=0;r<3;++r)for(unsigned k=0;k<3;++k)p.matrix[r*3+k]=row[r*4+k];p.translation={row[3],row[7],row[11]};
                    Vec3 lo,hi;transformBounds(p,batch.low,batch.high,lo,hi);CasterBounds box;box.add(lo,hi);if(!box.bounded)return false;changed.push_back(box);}}
            return true;};
        auto a=old.slices.begin();auto b=plan.models.begin();
        auto skipEmpty=[&](){while(b!=plan.models.end()&&!b->second.commandCount)++b;};skipEmpty();
        while(a!=old.slices.end()||b!=plan.models.end()){
            if(b==plan.models.end()||(a!=old.slices.end()&&a->key<b->first)){if(!a->bounds.bounded)return false;changed.push_back(a->bounds);++models;++a;continue;}
            if(a==old.slices.end()||b->first<a->key){if(!current(b->second))return false;++models;++b;skipEmpty();continue;}
            if(a->signature!=b->second.signature){if(!a->bounds.bounded||!current(b->second))return false;changed.push_back(a->bounds);++models;}
            ++a;++b;skipEmpty();
        }
        return true;
    }
    // Draw calls draw() would issue for each candidate rect set (null: full).
    void drawCalls(const float* matrix,const std::vector<const std::vector<NorthlightShadowBounds::TexelRect>*>& candidates,long size,long margin,std::vector<size_t>& out)const {
        out.assign(candidates.size(),0);if(!scene_||!stats_.readyModels||!groups_||groups_->empty())return;callsIn(prepare(matrix),canInstance_,matrix,candidates,size,margin,out);
    }
    static void callsIn(const Plan& plan,bool instancing,const float* matrix,const std::vector<const std::vector<NorthlightShadowBounds::TexelRect>*>& candidates,long size,long margin,std::vector<size_t>& out){
        for(const auto& command:plan.commands){const size_t calls=instancing&&command.count>1?1:command.count;NorthlightShadowBounds::TexelRect footprint;
            const bool known=command.bounds.bounded&&NorthlightShadowBounds::texelFootprint(command.bounds.low,command.bounds.high,matrix,size,margin,footprint);
            for(size_t k=0;k<candidates.size();++k){if(!candidates[k]){out[k]+=calls;continue;}for(const auto& rect:*candidates[k])if(!known||NorthlightShadowBounds::intersects(footprint,rect))out[k]+=calls;}}
    }
    // 0.3.152 plan worker. The worker's detached plan for kickPlans' after():
    // the answers changedBounds()/drawCalls() give for its matrix, with the
    // kick's instancing mode and epoch (kickedModeCurrent() re-checks both).
    class DetachedView {
        const GpuCache& cache_;const Plan& plan_;
    public:
        DetachedView(const GpuCache& cache,const Plan& plan):cache_(cache),plan_(plan){}
        const float* matrix()const{return plan_.matrix;}uint64_t signature()const{return plan_.signature;}
        bool changedBounds(const float* matrix,const ContentRecord& old,std::vector<CasterBounds>& changed,size_t& models)const{
            changed.clear();models=0;if(!old.valid||old.epoch!=cache_.asyncEpoch_||old.instancing!=cache_.asyncInstancing_||std::memcmp(old.matrix,matrix,sizeof(old.matrix))!=0)return false;
            return changedIn(plan_,old,changed,models);}
        void drawCalls(const float* matrix,const std::vector<const std::vector<NorthlightShadowBounds::TexelRect>*>& candidates,long size,long margin,std::vector<size_t>& out)const{
            out.assign(candidates.size(),0);callsIn(plan_,cache_.asyncInstancing_,matrix,candidates,size,margin,out);}
    };
    // after() results still describe this cache: same instancing mode and epoch, no mutator since the kick.
    bool kickedModeCurrent()const{return canInstance_==asyncInstancing_&&epoch_==asyncEpoch_&&mutations_==asyncMutations_;}
    // Render thread, with every plan input final for the frame: prepares
    // `first` synchronously (needed at once), then builds the plans of `later`
    // (loop order) that are not ready on the plan worker. Their entries are
    // reserved (pinned) now, in order, exactly as their prepare() would pick
    // them; the old contents move into the item the worker builds from. The
    // first prepare() of a kicked matrix installs its build or steals it back
    // (not started: built inline), any failure is rebuilt there synchronously,
    // and every mutator settles all items first. after(i,view) runs on the
    // worker after item i's build (no D3D, job-owned outputs only).
    // Returns the items kicked; 0: all plans stay synchronous.
    unsigned kickPlans(const float* first,const float* const* later,unsigned count,std::function<void(unsigned,const DetachedView&)> after={}){
        settle();
        if(!asyncPlans_||!lruPlans_||!scene_||!stats_.readyModels||!groups_||groups_->empty())return 0; /* LRU: a stolen item gets its reserved entry again */
        // Loop order: a ready plan is stamped as its hit would be, an invalid one
        // reserved; each reservation then sees the LRU order its prepare() would.
        auto repeated=[&](unsigned i){if(first&&!std::memcmp(first,later[i],sizeof(Plan::matrix)))return true;
            for(unsigned k=0;k<i;++k)if(!std::memcmp(later[k],later[i],sizeof(Plan::matrix)))return true;return false;};
        bool any=false;for(unsigned i=0;i<count&&!any;++i)any=!repeated(i)&&!validPlan(later[i]);
        if(!any)return 0; /* all later plans ready: nothing else */
        if(first)prepare(first);
        unsigned n=0;
        for(unsigned i=0;i<count&&n<NorthlightStaticPlanJob::MaxItems;++i){if(repeated(i))continue;const float* m=later[i];Plan* slot=nullptr;
            for(auto& plan:plans_)if(plan.occupied&&!std::memcmp(plan.matrix,m,sizeof(plan.matrix))){slot=&plan;break;}
            if(slot&&slot->valid){slot->used=++planClock_;continue;}
            auto& item=async_[n++];item.reuseMatrix=slot!=nullptr;item.evicted=false;
            if(!slot){const auto evictions=stats_.planEvictions;slot=&planSlot();item.evicted=stats_.planEvictions!=evictions;}
            item.savedUsed=slot->used;item.savedOccupied=slot->occupied;item.savedValid=slot->valid;std::memcpy(item.savedMatrix,slot->matrix,sizeof(item.savedMatrix));item.saved=std::move(*slot);
            *slot=Plan{};slot->occupied=slot->pinned=true;std::memcpy(slot->matrix,m,sizeof(slot->matrix));slot->used=item.savedUsed;
            item.slot=slot;std::memcpy(item.matrix,m,sizeof(item.matrix));item.failed=false;item.ms=0;item.counters={};
        }
        if(!n)return 0;
        asyncCount_=n;asyncInstancing_=canInstance_;asyncEpoch_=epoch_;asyncMutations_=mutations_;after_=std::move(after);++stats_.asyncKicks;
        bool started=false;try{if(!worker_)worker_=std::make_unique<NorthlightStaticPlanJob::Worker>();
#ifdef STATIC_SHADOW_GPU_TEST
            worker_->testDequeueDelay(testDequeueDelayUs_);
#endif
            started=worker_->start(n,[this](unsigned i){runItem(i);});}catch(...){}
        if(!started){for(unsigned i=0;i<n;++i){auto& item=async_[i];*item.slot=std::move(item.saved);if(item.evicted)--stats_.planEvictions;item.slot=nullptr;item.saved=Plan{};}
            asyncCount_=0;after_=nullptr;--stats_.asyncKicks;return 0;}
        return n;
    }
    // Joins the plan worker: pending items are stolen back (their entries get
    // the old contents again), running ones awaited, finished ones installed.
    // RAII at the end of render(); first in every mutator. discard (reset):
    // finished builds are dropped and their entries get the old identity
    // (occupied/valid/matrix/used), which reset() invalidates like the old plan.
    void settle(bool discard=false)const{
        if(!asyncCount_)return;
        const auto start=std::chrono::steady_clock::now();worker_->finish(true);stats_.asyncWaitMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        for(unsigned i=0;i<asyncCount_;++i)if(async_[i].slot)settleItem(i,discard);
    }
    // Null rects draw the complete plan. With disjoint rects each command is
    // drawn once per rect it can touch, scissored to it, in plan order: every
    // pixel inside a rect sees exactly the full plan's fragment sequence.
    bool draw(IDirect3DDevice9* d,const float* matrix,const std::vector<NorthlightShadowBounds::TexelRect>* rects=nullptr,long size=0,long margin=0){
        stats_.drawCalls=stats_.instances=stats_.culled=stats_.rectSkipped=0;if(!scene_||!stats_.readyModels||!groups_||groups_->empty())return true;if(!vs_||!ps_||!decl_)return false;
        struct RestoreFrequency {IDirect3DDevice9* d;~RestoreFrequency(){d->SetStreamSourceFreq(0,1);d->SetStreamSourceFreq(1,1);d->SetStreamSource(1,nullptr,0,0);}} guard{d};
        d->SetPixelShader(ps_);d->SetVertexShaderConstantF(0,matrix,4);
        d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);d->SetRenderState(D3DRS_FOGENABLE,FALSE);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,rects?TRUE:FALSE);d->SetRenderState(D3DRS_CLIPPLANEENABLE,0);d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);d->SetRenderState(D3DRS_DEPTHBIAS,0);d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0);
        d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
        const auto& plan=prepare(matrix);stats_.culled=plan.culled;
        Resource* bound=nullptr;IDirect3DPixelShader9* boundPixel=nullptr;IDirect3DTexture9* boundTexture=nullptr;
        bool materialBound=false,textureBound=false;DWORD addressU=0,addressV=0;float cutoff=0;
        size_t uploadedEnd=0,uploadedFirst=0;unsigned uploadedCursor=0;
        DrawMode drawMode;const NorthlightShadowBounds::TexelRect* scissor=nullptr;hits_.clear();
        for(size_t c=0;c<plan.commands.size();++c){const auto& command=plan.commands[c];
            if(rects){hits_.clear();NorthlightShadowBounds::TexelRect footprint;const bool known=command.bounds.bounded&&NorthlightShadowBounds::texelFootprint(command.bounds.low,command.bounds.high,matrix,size,margin,footprint);
                for(const auto& rect:*rects)if(!known||NorthlightShadowBounds::intersects(footprint,rect))hits_.push_back(&rect);
                if(hits_.empty()){++stats_.rectSkipped;continue;}}
            auto pass=[&](size_t k){if(!rects)return;if(scissor!=hits_[k]){const RECT r={LONG(hits_[k]->left),LONG(hits_[k]->top),LONG(hits_[k]->right),LONG(hits_[k]->bottom)};d->SetScissorRect(&r);scissor=hits_[k];}};
            const size_t passes=rects?hits_.size():1;
            auto& r=*command.resource;const auto& m=*r.model;const auto& b=m.batches[command.batch];const unsigned count=command.count;
            if(bound!=&r){d->SetStreamSource(0,r.vb,0,sizeof(Vertex));d->SetIndices(r.ib);bound=&r;}
            const auto& material=m.materials[b.material];
            if(!materialBound||addressU!=material.addressU){d->SetSamplerState(0,D3DSAMP_ADDRESSU,material.addressU);addressU=material.addressU;}
            if(!materialBound||addressV!=material.addressV){d->SetSamplerState(0,D3DSAMP_ADDRESSV,material.addressV);addressV=material.addressV;}
            if(!materialBound||cutoff!=material.alphaCutoff){float cut[]={0,0,0,material.alphaCutoff};d->SetPixelShaderConstantF(0,cut,1);cutoff=material.alphaCutoff;}materialBound=true;
            const bool opaque=r.opaque[b.material];IDirect3DPixelShader9* pixel=command.path?(opaque&&opaqueFastPS_?opaqueFastPS_:fastPS_):(opaque?opaquePS_:ps_);if(!pixel)pixel=ps_;
            if(boundPixel!=pixel){d->SetPixelShader(pixel);boundPixel=pixel;}
            auto* texture=(pixel==opaqueFastPS_||pixel==opaquePS_)?nullptr:r.textures[b.material]->gpu;
            if(!textureBound||boundTexture!=texture){d->SetTexture(0,texture);boundTexture=texture;textureBound=true;}
            bool instanced=canInstance_&&count>1;
            if(instanced&&c>=uploadedEnd){
                if(instanceCapacity_==InstanceCapacity||instanceCursor_+count>instanceCapacity_)instanceCursor_=0;
                uploadedCursor=instanceCursor_;uploadedFirst=command.first;uploadedEnd=c+1;
                // Gather whole existing draw groups into one lock. Never split a
                // draw or enlarge geometry buffers. Offset-less devices retain
                // the original single-group DISCARD path.
                if(instanceCapacity_>InstanceCapacity)while(uploadedEnd<plan.commands.size()){
                    const auto& next=plan.commands[uploadedEnd];if(next.first+next.count-uploadedFirst>instanceCapacity_-instanceCursor_)break;++uploadedEnd;}
                const auto& last=plan.commands[uploadedEnd-1];const unsigned n=unsigned(last.first+last.count-uploadedFirst);
                const DWORD flags=instanceCursor_?D3DLOCK_NOOVERWRITE:D3DLOCK_DISCARD;void* data=nullptr;
                if(FAILED(instances_->Lock(instanceCursor_*sizeof(Instance),n*sizeof(Instance),&data,flags))||!data){canInstance_=false;instanced=false;}
                else{++stats_.instanceLocks;stats_.instanceDiscards+=flags==D3DLOCK_DISCARD;stats_.instanceBytes+=n*sizeof(Instance);
                    // Commands tile the instances in order: one copy per run of adjacent chunk data.
                    for(size_t k=c;k<uploadedEnd;){const auto& from=plan.commands[k];size_t run=from.count;while(++k<uploadedEnd&&plan.commands[k].data==from.data+run)run+=plan.commands[k].count;
                        std::memcpy(static_cast<Instance*>(data)+(from.first-uploadedFirst),from.data,run*sizeof(Instance));}
                    if(FAILED(instances_->Unlock())){canInstance_=false;instanced=false;}else instanceCursor_+=n;
                }
            }
            const UINT instanceByteOffset=UINT((uploadedCursor+command.first-uploadedFirst)*sizeof(Instance));
            if(instanced&&!drawMode.apply(d,true,count,instanceByteOffset,instancedVS_,instancedDecl_,instances_,sizeof(Instance))){canInstance_=false;instanced=false;}
            size_t done=0;
            if(instanced)for(;done<passes;++done){pass(done);if(FAILED(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,r.vertexCount,b.firstIndex,b.indexCount/3))){drawMode.invalidate();canInstance_=false;instanced=false;break;}++stats_.drawCalls;stats_.instances+=count;}
            // A failed instanced pass is retried per instance, like the full path.
            if(!instanced){if(!drawMode.apply(d,false,1,0,vs_,decl_,instances_,sizeof(Instance)))return false;
                for(;done<passes;++done){pass(done);for(unsigned n=0;n<count;++n){d->SetVertexShaderConstantF(4,command.data[n].row,3);if(FAILED(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,r.vertexCount,b.firstIndex,b.indexCount/3)))return false;++stats_.drawCalls;++stats_.instances;}}}
        }
        stats_.instancing=canInstance_;return true;
    }
private:
    bool validPlan(const float* matrix)const{for(const auto& plan:plans_)if(plan.occupied&&plan.valid&&std::memcmp(plan.matrix,matrix,sizeof(plan.matrix))==0)return true;return false;}
    struct AsyncItem {Plan* slot=nullptr;Plan saved,built;float matrix[16]{},savedMatrix[16]{};uint64_t savedUsed=0;bool savedOccupied=false,savedValid=false,reuseMatrix=false,evicted=false,failed=false;double ms=0;BuildCounters counters;};
    mutable AsyncItem async_[NorthlightStaticPlanJob::MaxItems];mutable unsigned asyncCount_=0;mutable BuildContext workerContext_;
    bool asyncPlans_=NorthlightStaticPlanJob::Async&&NorthlightStream::cores()>2; /* 2 cores or fewer: always synchronous (0.3.192: the replay thread takes one while the stream runs) */
    bool asyncInstancing_=false;uint64_t asyncEpoch_=0,mutations_=0,asyncMutations_=0;
    mutable std::function<void(unsigned,const DetachedView&)> after_;
    void mutate(bool discard=false){settle(discard);++mutations_;} /* first in every mutator */
#ifdef STATIC_SHADOW_GPU_TEST
    unsigned testDelayUs_=0,testFailMask_=0,testDequeueDelayUs_=0;size_t testFailAfter_=0;
#endif
    // Worker thread: item i only (its saved/built plans, workerContext_) and
    // the frozen inputs. Never plans_, stats_, D3D or the render thread's context.
    void runItem(unsigned i)const{
        auto& item=async_[i];const auto start=std::chrono::steady_clock::now();
        try{
#ifdef STATIC_SHADOW_GPU_TEST
            if(testDelayUs_)std::this_thread::sleep_for(std::chrono::microseconds(testDelayUs_));
            workerContext_.failAfter=(testFailMask_>>i&1)?testFailAfter_:SIZE_MAX;
#endif
            build(workerContext_,item.saved,item.reuseMatrix,item.matrix,fastPlans_,item.built);item.counters=workerContext_.counters;
            item.counters.walkMs=std::max(0.,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()-item.counters.reuseMs-item.counters.rebuildMs);
        }catch(...){item.failed=true;}
        workerContext_.counters={};
        if(!item.failed&&after_)try{after_(i,DetachedView(*this,item.built));}catch(...){} /* after() reports its own failures */
        item.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
    bool joinMatrix(const float* matrix)const{
        for(unsigned i=0;i<asyncCount_;++i)if(async_[i].slot&&std::memcmp(async_[i].matrix,matrix,sizeof(async_[i].matrix))==0)return settleItem(i);
        return false;
    }
    // true: item i's build is now installed in its entry.
    bool settleItem(unsigned i,bool discard=false)const{
        auto& item=async_[i];auto& slot=*item.slot;item.slot=nullptr;
        const auto start=std::chrono::steady_clock::now();const auto state=worker_->claim(i);stats_.asyncWaitMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        bool installed=false;
        // Stolen, or failed (the build undid itself): as if never reserved; the next prepare() builds as the synchronous path would.
        if(state==NorthlightStaticPlanJob::State::Stolen||item.failed){slot=std::move(item.saved);if(item.evicted)--stats_.planEvictions;++(item.failed?stats_.asyncFailures:stats_.asyncStolen);}
        else if(discard){slot=Plan{};slot.occupied=item.savedOccupied;slot.valid=item.savedValid;std::memcpy(slot.matrix,item.savedMatrix,sizeof(slot.matrix));slot.used=item.savedUsed;if(item.evicted)--stats_.planEvictions;}
        else{slot=std::move(item.built);addCounters(item.counters);++stats_.planBuilds;++stats_.asyncBuilds;stats_.asyncWorkerMs+=item.ms;installed=true;}
        item.saved=Plan{};item.built=Plan{};
        bool open=false;for(unsigned k=0;k<asyncCount_;++k)open|=async_[k].slot!=nullptr;
        if(!open){worker_->finish(false);asyncCount_=0;after_=nullptr;}
        return installed;
    }
    std::unique_ptr<NorthlightStaticPlanJob::Worker> worker_; /* last: its thread stops first */
};
} // namespace StaticShadow
