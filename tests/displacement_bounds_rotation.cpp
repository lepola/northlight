#define NOMINMAX
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <random>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <unordered_set>
static int failAllocationAfter=-1;
void* operator new(std::size_t n){if(failAllocationAfter==0){failAllocationAfter=-1;throw std::bad_alloc();}if(failAllocationAfter>0)--failAllocationAfter;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
#include "forwarders.h"
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char*,...){}
#include "quality_settings.h"
#include "auto_displacement_renderer.h"
#define NorthlightAutoDisplacement NorthlightAutoDisplacementFrozen
#include "bounds_rotation_baseline.h"
#undef NorthlightAutoDisplacement

struct DisplacementStateTestAccess {
    using Mesh=NorthlightDrawSnapshot::Mesh;
    using Owner=std::shared_ptr<const Mesh>;
    template<class Cache>static void touch(Cache& cache,typename Cache::Bounds& value){
        if constexpr(std::is_same<Cache,NorthlightAutoDisplacement>::value)cache.touchBounds(value);
        else value.used=++cache.clock;
    }
    template<class Cache>static void insert(Cache& cache,const Owner& mesh,float value,bool skin=false){
        typename Cache::Bounds bound{};bound.low[0]=value;bound.high[2]=value+1;
        if(skin)bound.skin=std::make_unique<NorthlightReplayBounds::SkinEnvelope>();
        cache.retainBounds(mesh,std::move(bound));
    }
    static void structure(const NorthlightAutoDisplacement& cache){
        std::unordered_set<const void*> seen;const NorthlightAutoDisplacement::Bounds* previous=nullptr;size_t bytes=0;
        for(auto* node=cache.boundsLRU.oldest();node;node=node->lru.newer){
            assert(seen.insert(node).second&&node->lru.older==previous&&cache.bounds.at(node->key).key==node->key);
            if(previous)assert(previous->used<node->used);
            bytes+=node->bytes;previous=node;
        }
        assert(seen.size()==cache.bounds.size()&&bytes==cache.boundsBytes&&bytes<=(8u<<20)&&seen.size()<=4096);
        for(const auto& entry:cache.bounds)assert(entry.first==entry.second.key&&seen.count(&entry.second));
    }
    static void equivalent(const NorthlightAutoDisplacement& current,const NorthlightAutoDisplacementFrozen& frozen){
        assert(current.bounds.size()==frozen.bounds.size());
        for(const auto& entry:current.bounds){auto old=frozen.bounds.find(entry.first);assert(old!=frozen.bounds.end());
            assert(entry.second.used==old->second.used&&!std::memcmp(entry.second.low,old->second.low,sizeof entry.second.low)&&!std::memcmp(entry.second.high,old->second.high,sizeof entry.second.high));}
        if(!current.bounds.empty()){auto old=std::min_element(frozen.bounds.begin(),frozen.bounds.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});assert(old->first==current.boundsLRU.oldest()->key);}
        structure(current);
    }
    static void differential(){
        NorthlightAutoDisplacement current(nullptr);NorthlightAutoDisplacementFrozen frozen(nullptr);
        std::vector<Owner> owners;for(unsigned i=0;i<7000;++i)owners.push_back(std::make_shared<Mesh>());
        std::mt19937 random(9134);size_t checked=0,misses=0;
        for(unsigned frame=0;frame<50;++frame){
            if(frame==21||frame==39){current.reset();frozen.reset();}
            for(unsigned j=0;j<1200;++j){unsigned i=(frame*341+j)%owners.size();if(j%31==0)i=random()%owners.size();
                if(j%127==0)owners[i]=std::make_shared<Mesh>(); // new immutable generations
                auto now=current.cachedBounds(owners[i]);auto old=frozen.cachedBounds(owners[i]);
                assert((now==current.bounds.end())==(old==frozen.bounds.end()));
                if(now==current.bounds.end()){insert(current,owners[i],float(i));insert(frozen,owners[i],float(i));++misses;}
                else {touch(current,now->second);touch(frozen,old->second);}
                ++checked;if(j%127==0)equivalent(current,frozen);
            }
            equivalent(current,frozen);
        }
        // Expired owners are never retained. Same address must unlink its old node,
        // whether it is oldest, in the middle, or newest in the queue.
        for(unsigned target:{0u,2u,4u}){
            current.reset();frozen.reset();alignas(Mesh) unsigned char storage[sizeof(Mesh)];
            auto make=[&]{return Owner(new(storage) Mesh,[](const Mesh* mesh){mesh->~Mesh();});};
            Owner first=make();std::weak_ptr<const Mesh> weak=first;std::vector<Owner> live;
            for(unsigned i=0;i<5;++i){auto owner=i==target?first:Owner(std::make_shared<Mesh>());live.push_back(owner);insert(current,owner,float(i));insert(frozen,owner,float(i));}
            first.reset();live[target].reset();assert(weak.expired());Owner replacement=make();
            assert(current.cachedBounds(replacement)==current.bounds.end()&&frozen.cachedBounds(replacement)==frozen.bounds.end());
            equivalent(current,frozen);insert(current,replacement,90);insert(frozen,replacement,90);equivalent(current,frozen);
            current.reset();frozen.reset();structure(current);
        }
        // The accounted 8 MiB cap still applies when individual skin envelopes
        // hit it before 4096 entries. New list metadata is included in bytes.
        current.reset();owners.clear();for(unsigned i=0;i<5000;++i){owners.push_back(std::make_shared<Mesh>());insert(current,owners.back(),float(i),true);if(i%127==0)structure(current);}
        structure(current);assert(current.boundsBytes<=(8u<<20));
        current.setMemoryPressure(true);structure(current);assert(current.bounds.empty()&&current.boundsBytes==0);current.reset();structure(current);
        // Node/bucket allocation failures may happen after eviction. Neither
        // failure can leave a linked freed node or an uncharged live node.
        for(unsigned count:{0u,4096u})for(int failure=0;failure<3;++failure){
            current.reset();owners.clear();for(unsigned i=0;i<count;++i){owners.push_back(std::make_shared<Mesh>());insert(current,owners.back(),float(i));}
            Owner next=std::make_shared<Mesh>();failAllocationAfter=failure;
            try{insert(current,next,90);}catch(const std::bad_alloc&){}
            failAllocationAfter=-1;structure(current);current.reset();structure(current);
        }
        std::printf("BOUNDS_ROTATION_METADATA currentEntryBytes=%zu frozenEntryBytes=%zu capEntries=4096 capBytes=%u allocationFailureCases=6\n",sizeof(NorthlightAutoDisplacement::Bounds)+4*sizeof(void*),sizeof(NorthlightAutoDisplacementFrozen::Bounds)+4*sizeof(void*),8u<<20);
        std::printf("BOUNDS_ROTATION_DIFFERENTIAL_PASS draws=%zu misses=%zu exact_victims exact_payload weak_owner address_reuse rehash reset pressure byte_cap\n",checked,misses);
    }
    template<class Cache>static double benchmark(bool rotating){
        Cache cache(nullptr);std::vector<Owner> owners;for(unsigned i=0;i<(rotating?8192u:1200u);++i)owners.push_back(std::make_shared<Mesh>());
        double total=0;const unsigned frames=64,warm=24;
        for(unsigned frame=0;frame<frames;++frame){const auto start=std::chrono::steady_clock::now();
            for(unsigned j=0;j<1200;++j){const unsigned i=rotating?(frame*400+j)%owners.size():j;auto found=cache.cachedBounds(owners[i]);if(found==cache.bounds.end())insert(cache,owners[i],float(i));else touch(cache,found->second);}
            if(frame>=warm)total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        }
        return total/(frames-warm);
    }
};
int main(){
    DisplacementStateTestAccess::differential();
    for(bool rotation:{false,true})for(unsigned round=0;round<3;++round){double oldMs,newMs;
        if(round%2){newMs=DisplacementStateTestAccess::benchmark<NorthlightAutoDisplacement>(rotation);oldMs=DisplacementStateTestAccess::benchmark<NorthlightAutoDisplacementFrozen>(rotation);}
        else {oldMs=DisplacementStateTestAccess::benchmark<NorthlightAutoDisplacementFrozen>(rotation);newMs=DisplacementStateTestAccess::benchmark<NorthlightAutoDisplacement>(rotation);}
        std::printf("{\"workload\":\"%s\",\"draws\":1200,\"new_per_frame\":%u,\"old_ms\":%.6f,\"new_ms\":%.6f}\n",rotation?"rotating":"warm",rotation?400:0,oldMs,newMs);
    }
}
