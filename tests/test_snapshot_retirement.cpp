#include "draw_snapshot.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>

static bool refuseAllocation=false;
static std::atomic<void*> watchedIndex{nullptr};
static std::atomic<bool> indexFreed{false},indexFreedOnOwner{false};
[[maybe_unused]] static const auto ownerThread=std::this_thread::get_id();
#if !__has_feature(thread_sanitizer)
void* operator new(size_t n){if(refuseAllocation)throw std::bad_alloc();if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{if(p&&p==watchedIndex.load()){indexFreedOnOwner=std::this_thread::get_id()==ownerThread;indexFreed=true;}std::free(p);}
void operator delete(void* p,size_t)noexcept{::operator delete(p);}
#endif
namespace NorthlightDrawSnapshot {
struct SnapshotTestAccess {
    static size_t seedIndices(Frame& frame){
        assert(!frame.indexCacheEntries());
        for(size_t i=0;i<frame.decoded_.size();++i){
            auto entry=std::make_unique<Frame::DecodedEntry>();entry->slot=i;entry->fingerprint=i;
            entry->raw.assign(2048,static_cast<uint8_t>(i));entry->indices.assign(512,UINT(i));entry->runs.assign(32,{0,0,3});
            entry->bytes=sizeof(Frame::DecodedEntry)+128+entry->raw.capacity()+entry->indices.capacity()*sizeof(UINT)+entry->runs.capacity()*sizeof(Frame::CopyRun);
            if(!i)watchedIndex=entry->raw.data();
            frame.decodedBytes_+=entry->bytes;frame.decodedLookup_.emplace(i,entry.get());frame.decodedLRU_.append(*entry);frame.recent_=entry.get();frame.decoded_[i]=std::move(entry);
        }
        frame.decodedFreeCount_=0;assert(frame.indexCacheEntries()==1024&&frame.indexCacheBytes()<=Frame::IndexCacheLimit);
        return frame.indexCacheBytes();
    }
    static std::shared_ptr<const Mesh> seed(Frame& frame,IDirect3DVertexDeclaration9* declaration){
        std::shared_ptr<const Mesh> held;
        for(unsigned i=0;i<20;++i){
            Mesh mesh;mesh.streams[0].stride=16;mesh.streams[0].bytes.assign(4u<<20,static_cast<unsigned char>(i+1));
            mesh.vertexCount=3;mesh.primitiveCount=1;
            auto entry=std::make_unique<Frame::CacheEntry>();entry->key.vb[0]=i+1;entry->producer=&frame;
            entry->declaration.p=declaration;declaration->AddRef();entry->rawIndices.assign(512u<<10,1);
            entry->metadataBytes=sizeof(Frame::CacheEntry)+entry->rawIndices.capacity()+96;
            entry->bytes=entry->metadataBytes+NorthlightSourceIdentity::allocationBytes<Mesh>(mesh.capacityBytes());
            entry->mesh=frame.retain(std::move(mesh));entry->serial=frame.sources_->serial++;
            if(!held)held=entry->mesh;
            const auto bytes=entry->bytes,metadata=entry->metadataBytes;
            auto inserted=frame.sources_->entries.emplace(Frame::hashKey(entry->key),std::move(entry));assert(inserted.second);
            frame.sources_->lru.append(*inserted.first->second);frame.sources_->bytes+=bytes;frame.sources_->metadataBytes+=metadata;
            frame.predicted_=inserted.first->second.get();frame.predictionGeneration_=frame.sources_->generation;
        }
        assert(frame.sharedSourceBytes()<=frame.sharedSourceLimit());return held;
    }
    static bool invalidated(const Frame& frame){return !frame.predicted_||frame.predictionGeneration_!=frame.sources_->generation;}
    static bool indicesInvalidated(const Frame& frame){return !frame.recent_&&frame.activeRuns_==&frame.copyRuns_&&!frame.decodedLRU_.oldest()&&frame.decodedLookup_.empty()&&frame.decodedFreeCount_==frame.decoded_.size();}
};
}
struct Declaration:IDirect3DVertexDeclaration9 {
    unsigned refs=1,releases=0;std::thread::id owner=std::this_thread::get_id();
    unsigned AddRef()override{assert(std::this_thread::get_id()==owner);return ++refs;}
    unsigned Release()override{assert(std::this_thread::get_id()==owner&&"RELEASE_OWNER_THREAD");assert(refs);++releases;return --refs;}
    HRESULT GetDeclaration(D3DVERTEXELEMENT9*,UINT*)override{return D3DERR_INVALIDCALL;}
};
struct Blocker {
    std::atomic<bool>& entered;std::atomic<bool>& proceed;
    ~Blocker(){entered=true;while(!proceed)std::this_thread::yield();}
};
static void batchLimits(){
    NorthlightStreaming::CpuRetirement queue(false);auto held=std::make_shared<int>(42);
    std::atomic<bool> entered{false},proceed{false};auto blocker=std::shared_ptr<Blocker>(new Blocker{entered,proceed});
    while(!queue.retire(blocker,0))std::this_thread::yield();while(!entered)std::this_thread::yield();
    {
        NorthlightStreaming::CpuRetirementBatch batch(&queue);
        for(unsigned i=0;i<16;++i){auto owner=held;assert(batch.retire(owner,1)&&!owner);}
        auto extra=held;assert(!batch.retire(extra,1)&&extra==held&&batch.count()==16&&batch.bytes()==16);
        assert(batch.finish());assert(!batch.retire(extra,1)&&extra==held&&!batch.finish());proceed=true;assert(queue.waitIdle(10000));
    }
    {
        NorthlightStreaming::CpuRetirementBatch batch(&queue);auto owner=held;
        assert(batch.retire(owner,size_t(-1)));owner=held;assert(!batch.retire(owner,1)&&owner==held);
        const auto refused=queue.stats().refused;assert(!batch.finish()&&queue.stats().refused==refused+2&&!batch.queuedBytes()); // Bulk and individual attempts retain byte caps.
    }
#if !__has_feature(thread_sanitizer)
    {
        refuseAllocation=true;NorthlightStreaming::CpuRetirementBatch batch(&queue);refuseAllocation=false;
        auto owner=held;assert(!batch.retire(owner,1)&&owner==held&&!batch.finish()&&!batch.count());
    }
#endif
    {NorthlightStreaming::CpuRetirementBatch critical;auto owner=held;assert(!critical.retire(owner,1)&&owner==held&&!critical.finish());}
    assert(held.use_count()==1);
    std::puts("CPU_RETIRE_BATCH_PASS fixed_16_owners byte_overflow repeated_finish allocation_failure disabled_sync unchanged_queue_caps");
}
static void one(const char* name,bool defer,bool full,bool allocationFailure,bool indicesOnly=false,bool sourcePriority=false){
    const auto initial=NorthlightSourceIdentity::cpuLiveBytes.load();Declaration declaration;
    std::atomic<bool> entered{false},proceed{false};NorthlightStreaming::CpuRetirement queue(false);
    auto blocker=std::shared_ptr<Blocker>(new Blocker{entered,proceed});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!queue.retire(blocker,full?128u<<20:sourcePriority?36u<<20:1)){assert(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();}
    while(!entered)std::this_thread::yield();
    {
        NorthlightDrawSnapshot::Frame frame,peer;assert(peer.shareSnapshotPool(frame));
        auto held=indicesOnly?std::shared_ptr<const NorthlightDrawSnapshot::Mesh>{}:NorthlightDrawSnapshot::SnapshotTestAccess::seed(peer,&declaration);
        const auto before=frame.sharedSourceBytes();assert(indicesOnly?!before:before>90u<<20);
        indexFreed=false;indexFreedOnOwner=false;const auto indexBytes=NorthlightDrawSnapshot::SnapshotTestAccess::seedIndices(frame);
        refuseAllocation=allocationFailure;
        const auto start=std::chrono::steady_clock::now();
        const auto deferred=frame.clearIndexCache(defer?&queue:nullptr);
        const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
        refuseAllocation=false;
        assert(!frame.sharedSourceBytes()&&!peer.sharedSourceBytes()&&!frame.snapshotCacheEntries()&&!peer.indexCacheEntries());
        assert(frame.sharedSourceLimit()==128u<<20);
        assert(declaration.refs==1&&declaration.releases==(indicesOnly?0u:20u)&&"COM_RELEASE_BEFORE_ENQUEUE");
        assert(NorthlightDrawSnapshot::SnapshotTestAccess::invalidated(peer));
        assert(NorthlightDrawSnapshot::SnapshotTestAccess::indicesInvalidated(frame));
        const bool queued=defer&&!full&&!allocationFailure;
        const bool indicesQueued=queued&&!sourcePriority;
        if(queued){assert(deferred>=before&&"RETIREMENT_NOT_QUEUED");assert(deferred==before+(indicesQueued?indexBytes:0)&&"INDEX_RETIREMENT_NOT_QUEUED");assert(frame.sourceLiveObjects()==(indicesOnly?0u:20u));}
        else {assert(!deferred);assert(frame.sourceLiveObjects()==(indicesOnly?0u:1u));}
#if !__has_feature(thread_sanitizer)
        assert(indexFreed==!indicesQueued);if(!indicesQueued)assert(indexFreedOnOwner);
#endif
        if(held)assert(held->streams[0].bytes.size()==4u<<20&&held->streams[0].bytes.front()==1&&held->streams[0].bytes.back()==1);
        proceed=true;assert(queue.waitIdle(10000));assert(frame.sourceLiveObjects()==(indicesOnly?0u:1u));
#if !__has_feature(thread_sanitizer)
        assert(indexFreed&&indexFreedOnOwner==!indicesQueued);watchedIndex=nullptr;
#endif
        held.reset();assert(!frame.sourceLiveObjects());
        // A fresh populated pool after retirement still clears synchronously.
        held=NorthlightDrawSnapshot::SnapshotTestAccess::seed(frame,&declaration);
        assert(!frame.clearIndexCache());assert(frame.sourceLiveObjects()==1);
        held.reset();assert(!frame.sourceLiveObjects()&&declaration.refs==1&&declaration.releases==(indicesOnly?20u:40u));
        std::printf("SNAPSHOT_RETIRE_ROW case=%s bytes=%zu indexBytes=%zu deferred=%zu boundaryNs=%lld\n",name,before,indexBytes,deferred,static_cast<long long>(ns));
    }
    assert(NorthlightSourceIdentity::cpuLiveBytes.load()==initial);
}
int main(){
    batchLimits();
    one("demand",true,false,false);
    one("critical",false,false,false);
    one("queue-full",true,true,false);
    one("indices-only",true,false,false,true);
    one("source-priority",true,false,false,false,true);
#if !__has_feature(thread_sanitizer)
    one("allocation-failure",true,false,true); // TSan owns operator new; other modes inject failure.
#endif
    std::puts("SNAPSHOT_RETIRE_PASS owner_thread_COM bounded_queue critical_immediate shared_holders stale_prediction allocation_fallback ledger_drained rebuild");
}
