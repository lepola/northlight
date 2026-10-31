#define NOMINMAX
#include <d3d9.h>
#include <cassert>
static unsigned pressureMemoryReads=0;
static BOOL WINAPI pressureMemoryStatus(MEMORYSTATUSEX* memory){
    ++pressureMemoryReads;assert(memory&&memory->dwLength==sizeof(*memory));memory->ullAvailVirtual=2048ull<<20;return TRUE;
}
#define GlobalMemoryStatusEx pressureMemoryStatus
#include "material_pressure_reference.h"
#undef GlobalMemoryStatusEx

namespace NorthlightDrawSnapshot {
struct SnapshotTestAccess {
    static void seedIndices(Frame& frame,size_t rawBytes=2048){
        for(size_t i=0;i<frame.decoded_.size();++i){
            auto entry=std::make_unique<Frame::DecodedEntry>();entry->slot=i;entry->fingerprint=i;
            entry->raw.assign(rawBytes,static_cast<uint8_t>(i));entry->indices.assign(512,UINT(i));entry->runs.assign(32,{0,0,3});
            entry->bytes=sizeof(Frame::DecodedEntry)+128+entry->raw.capacity()+entry->indices.capacity()*sizeof(UINT)+entry->runs.capacity()*sizeof(Frame::CopyRun);
            frame.decodedBytes_+=entry->bytes;frame.decodedLookup_.emplace(i,entry.get());frame.decodedLRU_.append(*entry);frame.recent_=entry.get();frame.decoded_[i]=std::move(entry);
        }
        frame.decodedFreeCount_=0;assert(frame.indexCacheEntries()==1024&&frame.indexCacheBytes()<=Frame::IndexCacheLimit);
    }
    static std::shared_ptr<const Mesh> seed(Frame& frame,IDirect3DVertexDeclaration9* declaration,unsigned count=26,size_t payloadBytes=4u<<20){
        std::shared_ptr<const Mesh> held;
        for(unsigned i=0;i<count;++i){
            Mesh mesh;mesh.streams[0].stride=16;mesh.streams[0].bytes.assign(payloadBytes,static_cast<unsigned char>(i+1));mesh.vertexCount=3;mesh.primitiveCount=1;
            auto entry=std::make_unique<Frame::CacheEntry>();entry->key.vb[0]=i+1;entry->key.declaration=declaration;entry->producer=&frame;
            entry->declaration.p=declaration;declaration->AddRef();
            entry->metadataBytes=sizeof(Frame::CacheEntry)+96;
            entry->bytes=entry->metadataBytes+NorthlightSourceIdentity::allocationBytes<Mesh>(mesh.capacityBytes());
            entry->mesh=frame.retain(std::move(mesh));entry->serial=frame.sources_->serial++;
            if(!held)held=entry->mesh;
            const auto bytes=entry->bytes,metadata=entry->metadataBytes;
            const auto hash=Frame::hashKey(entry->key);auto added=frame.sources_->entries.emplace(hash,std::move(entry));assert(added.second);
            frame.sources_->lru.append(*added.first->second);frame.sources_->bytes+=bytes;frame.sources_->metadataBytes+=metadata;
            frame.predicted_=added.first->second.get();frame.predictionGeneration_=frame.sources_->generation;
        }
        assert(frame.sharedSourceBytes()<=frame.sharedSourceLimit());return held;
    }
    static bool predictionCleared(const Frame& frame){return frame.predicted_==nullptr;}
};
}
struct PressureDeclaration:Declaration {
    DWORD thread=GetCurrentThreadId();unsigned releases=0;
    ULONG STDMETHODCALLTYPE Release()override{assert(GetCurrentThreadId()==thread);++releases;return Declaration::Release();}
};
#ifdef NORTHLIGHT_TEST_MATERIAL_PRESSURE_POLICY
struct PressureBlocker {
    std::atomic<bool>& entered;std::atomic<bool>& proceed;
    ~PressureBlocker(){entered=true;while(!proceed)std::this_thread::yield();}
};
#endif
void MaterialCaptureStateTestAccess::pressureTransition(){
    CaptureBackend device;PressureDeclaration declaration;
    const auto initialCpu=NorthlightSourceIdentity::cpuLiveBytes.load();
    unsigned pressureReads=0,trimReads=0,recoveryReads=0,evictions=0,geometryEvictions=0;size_t before=0,after=0;
    {
        NorthlightAutoBump bump(&device);NorthlightDrawSnapshot::Frame worldPeer(2u<<20,0);
        assert(worldPeer.shareSnapshotPool(bump.deferredSnapshots));
        auto held=NorthlightDrawSnapshot::SnapshotTestAccess::seed(bump.deferredSnapshots,&declaration);
        assert(bump.deferredSnapshots.sharedSourceParticipants()==3&&bump.deferredSnapshots.sharedSourceLimit()==(112u<<20));
        // Match the real guard: world caps shrink first, then material caps.
        worldPeer.setSnapshotCacheLimit(32u<<20);bump.deferredSnapshots.clearFrame();before=bump.deferredSnapshots.sharedSourceBytes();
        assert(before>(60u<<20)&&before<=(80u<<20));
        const auto queries=pressureMemoryReads;failAllocationAfter=0;bump.setMemoryPressure(true);assert(failAllocationAfter==0);failAllocationAfter=-1;
        pressureReads=pressureMemoryReads-queries;evictions=bump.deferredSnapshots.maintenance().snapshotEvictions;after=bump.deferredSnapshots.sharedSourceBytes();
        assert(bump.memoryPressure&&!after&&!bump.deferredSnapshots.snapshotCacheEntries());
        assert(bump.deferredSnapshots.snapshotCacheLimit()==(4u<<20)&&DisplacementStateTestAccess::pressureLimit(bump.displacement)==(6u<<20));
        assert(bump.deferredSnapshots.sharedSourceLimit()==(42u<<20)&&!bump.deferredGPU.limit()&&!bump.deferredGPU.chargedBytes());
        assert(NorthlightDrawSnapshot::SnapshotTestAccess::predictionCleared(bump.deferredSnapshots));
        assert(declaration.refs==1&&declaration.releases==26);
        assert(held->streams[0].bytes.size()==(4u<<20)&&std::all_of(held->streams[0].bytes.begin(),held->streams[0].bytes.end(),[](unsigned char x){return x==1;}));
        assert(bump.deferredSnapshots.sourceLiveObjects()==1); // escaped immutable ownership stays valid
        const auto repeatQueries=pressureMemoryReads;bump.setMemoryPressure(true);assert(pressureMemoryReads==repeatQueries);
        const auto trimQueries=pressureMemoryReads;failAllocationAfter=0;bump.trimMemory();assert(failAllocationAfter==0);failAllocationAfter=-1;trimReads=pressureMemoryReads-trimQueries;
        assert(!bump.deferredSnapshots.sharedSourceBytes()&&bump.deferredSnapshots.sharedSourceLimit()==(42u<<20)&&declaration.refs==1);
        const auto recoveryQueries=pressureMemoryReads;bump.setMemoryPressure(false);recoveryReads=pressureMemoryReads-recoveryQueries;
        assert(!bump.memoryPressure&&bump.deferredSnapshots.snapshotCacheLimit()==(24u<<20)&&bump.deferredGPU.limit()==(32u<<20));
        assert(DisplacementStateTestAccess::pressureLimit(bump.displacement)==(24u<<20)&&bump.deferredSnapshots.sharedSourceLimit()==(80u<<20));
        assert(!bump.deferredSnapshots.sharedSourceBytes()&&declaration.refs==1&&held->streams[0].bytes.back()==1);
        held.reset();assert(!bump.deferredSnapshots.sourceLiveObjects());
    }
    assert(NorthlightSourceIdentity::cpuLiveBytes.load()==initialCpu&&declaration.refs==1);
    {
        NorthlightAutoDisplacement geometry(&device);PressureDeclaration ownDeclaration;
        std::shared_ptr<const NorthlightDrawSnapshot::Mesh> held[3];
        for(unsigned i=0;i<3;++i){auto& pool=DisplacementStateTestAccess::pressureSources(geometry,i);held[i]=NorthlightDrawSnapshot::SnapshotTestAccess::seed(pool,&ownDeclaration,i?3:5,i?1u<<20:4u<<20);pool.clearFrame();}
        failAllocationAfter=0;geometry.setMemoryPressure(true);assert(failAllocationAfter==0);failAllocationAfter=-1;
        for(unsigned i=0;i<3;++i){auto& pool=DisplacementStateTestAccess::pressureSources(geometry,i);geometryEvictions+=pool.maintenance().snapshotEvictions;
            assert(!pool.sharedSourceBytes()&&!pool.snapshotCacheEntries()&&pool.snapshotCacheLimit()==(i?1u<<20:6u<<20));
            assert(pool.sourceLiveObjects()==1&&held[i]->streams[0].bytes.front()==1&&held[i]->streams[0].bytes.back()==1);}
        assert(ownDeclaration.refs==1&&ownDeclaration.releases==11);
        geometry.setMemoryPressure(false);for(unsigned i=0;i<3;++i){auto& pool=DisplacementStateTestAccess::pressureSources(geometry,i);assert(pool.snapshotCacheLimit()==(i?4u<<20:24u<<20));held[i].reset();assert(!pool.sourceLiveObjects());}
    }
    assert(NorthlightSourceIdentity::cpuLiveBytes.load()==initialCpu);
#ifdef NORTHLIGHT_TEST_MATERIAL_PRESSURE_POLICY
    // Reproduce the real guard: a >100 MiB shared world/source retirement,
    // followed immediately by four material index pools and two range pools.
    // Independent enqueues exceed 128 MiB; one idle oversized batch can carry
    // the complete, already-resident clear without changing the queue's limit.
    for(unsigned scenario=0;scenario<4;++scenario){
        const bool batched=scenario!=0,oversized=scenario!=2,busy=scenario==3;
        NorthlightStreaming::CpuRetirement retirement(oversized);
        std::atomic<bool> entered{false},proceed{false};
        {auto blocker=std::shared_ptr<PressureBlocker>(new PressureBlocker{entered,proceed});assert(retirement.retire(blocker,busy?128u<<20:0));while(!entered)std::this_thread::yield();}
        {
            NorthlightAutoBump bump(&device);NorthlightDrawSnapshot::Frame worldPeer;PressureDeclaration ownDeclaration;
            assert(worldPeer.shareSnapshotPool(bump.deferredSnapshots));
            NorthlightDrawSnapshot::Frame* pools[]={&worldPeer,&bump.deferredSnapshots,&DisplacementStateTestAccess::pressureSources(bump.displacement,0),&DisplacementStateTestAccess::pressureSources(bump.displacement,1),&DisplacementStateTestAccess::pressureSources(bump.displacement,2)};
            std::shared_ptr<const NorthlightDrawSnapshot::Mesh> held[3];
            held[0]=NorthlightDrawSnapshot::SnapshotTestAccess::seed(worldPeer,&ownDeclaration);
            for(unsigned i=0;i<5;++i)NorthlightDrawSnapshot::SnapshotTestAccess::seedIndices(*pools[i],8192);
            for(unsigned i=1;i<3;++i)held[i]=NorthlightDrawSnapshot::SnapshotTestAccess::seed(*pools[i+2],&ownDeclaration,3,1u<<20);
            NorthlightStreaming::CpuRetirementBatch batch(batched?&retirement:nullptr);
            auto* sink=batched?&batch:nullptr;
#ifdef NORTHLIGHT_TEST_DISABLE_PRESSURE_BATCH
            sink=nullptr;
#endif
            const auto accepted=retirement.stats().accepted;
            const auto start=std::chrono::steady_clock::now();
            const auto worldStaged=worldPeer.clearIndexCache(&retirement,sink);
            worldPeer.setSnapshotCacheLimit(32u<<20);
            bump.setMemoryPressure(true,32u<<20,&retirement,sink);
            // Shared source storage was already emptied by the world clear.
            // Every declaration was released on this thread, before finish().
            assert(ownDeclaration.refs==1&&ownDeclaration.releases==32);
            for(auto* pool:pools)assert(!pool->sharedSourceBytes()&&!pool->indexCacheEntries()&&!pool->snapshotCacheEntries());
            if(batched){
                assert(worldStaged>(100u<<20));assert(batch.bytes()>(128u<<20)&&batch.count()==8&&"ALL_PRESSURE_POOLS_MUST_SHARE_ONE_BATCH");
                assert(retirement.stats().accepted==accepted);
                assert(pools[0]->sourceLiveObjects()==26&&pools[3]->sourceLiveObjects()==3&&pools[4]->sourceLiveObjects()==3);
            }else assert(retirement.stats().refused>0&&"INDIVIDUAL_ENQUEUES_MUST_REPRODUCE_SATURATION");
            const bool queued=batch.finish();assert(queued==(batched&&oversized&&!busy)&&"IDLE_BATCH_NOT_QUEUED");
            const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            if(batched){
                if(queued)assert(retirement.stats().accepted==accepted+1&&batch.queuedBytes()==batch.bytes());
                else if(busy)assert(retirement.stats().accepted==accepted&&!batch.queuedBytes());
                else assert(retirement.stats().accepted>accepted&&batch.queuedBytes()>0&&batch.queuedBytes()<=(128u<<20)&&batch.queuedBytes()<batch.bytes());
            }
            assert(!batch.finish()); // No second submission or lifetime extension.
            proceed=true;assert(retirement.waitIdle(10000));
            for(unsigned i=0;i<3;++i){auto* pool=pools[i?i+2:0];assert(pool->sourceLiveObjects()==1);assert(held[i]->streams[0].bytes.front()==1&&held[i]->streams[0].bytes.back()==1);held[i].reset();assert(!pool->sourceLiveObjects());}
            bump.setMemoryPressure(false);
            std::printf("MATERIAL_PRESSURE_BATCH_PASS scenario=%u batched=%u idleOversized=%u busy=%u queued=%u stagedMiB=%.3f queuedMiB=%.3f boundaryNs=%lld ownerCOM=1 ledgerDrained=1\n",scenario,unsigned(batched),unsigned(oversized),unsigned(busy),unsigned(queued),double(batch.bytes())/(1u<<20),double(batch.queuedBytes())/(1u<<20),static_cast<long long>(ns));
        }
        assert(NorthlightSourceIdentity::cpuLiveBytes.load()==initialCpu);
    }
    for(bool critical:{false,true}){
        auto& retirement=NorthlightStreaming::cpuRetirement();assert(retirement.waitIdle(10000));
        std::atomic<bool> entered{false},proceed{false};auto blocker=std::shared_ptr<PressureBlocker>(new PressureBlocker{entered,proceed});
        assert(retirement.retire(blocker,1));while(!entered)std::this_thread::yield();
        {
            NorthlightAutoBump bump(&device);PressureDeclaration ownDeclaration;
            std::shared_ptr<const NorthlightDrawSnapshot::Mesh> held[3];
            NorthlightDrawSnapshot::Frame* pools[]={&bump.deferredSnapshots,&DisplacementStateTestAccess::pressureSources(bump.displacement,0),&DisplacementStateTestAccess::pressureSources(bump.displacement,1),&DisplacementStateTestAccess::pressureSources(bump.displacement,2)};
            for(unsigned i=0;i<4;++i){NorthlightDrawSnapshot::SnapshotTestAccess::seedIndices(*pools[i]);
                if(i!=1)held[i?i-1:0]=NorthlightDrawSnapshot::SnapshotTestAccess::seed(*pools[i],&ownDeclaration,3,1u<<20);}
            const auto accepted=retirement.stats().accepted;
            const auto start=std::chrono::steady_clock::now();
            bump.setMemoryPressurePolicy(true,critical);
            const auto boundaryNs=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            std::printf("MATERIAL_PRESSURE_BOUNDARY critical=%u ns=%lld\n",unsigned(critical),static_cast<long long>(boundaryNs));
            for(unsigned i=0;i<4;++i)assert(!pools[i]->snapshotCacheEntries()&&!pools[i]->indexCacheEntries()&&NorthlightDrawSnapshot::SnapshotTestAccess::predictionCleared(*pools[i]));
            assert(ownDeclaration.refs==1&&ownDeclaration.releases==9);
            for(unsigned i=0;i<3;++i){auto* pool=pools[i?i+1:0];assert(pool->sourceLiveObjects()==(critical?1u:3u));assert(held[i]->streams[0].bytes.front()==1&&held[i]->streams[0].bytes.back()==1);}
            assert(retirement.stats().accepted==accepted+(critical?0u:7u));
            proceed=true;assert(retirement.waitIdle(10000));
            for(unsigned i=0;i<3;++i){auto* pool=pools[i?i+1:0];assert(pool->sourceLiveObjects()==1);held[i].reset();assert(!pool->sourceLiveObjects());}
            bump.setMemoryPressurePolicy(false,false);
        }
        assert(NorthlightSourceIdentity::cpuLiveBytes.load()==initialCpu);
        std::printf("MATERIAL_PRESSURE_RETIRE_PASS critical=%u decodedPools=4 sourcePools=3 ownerCOM=1 boundedCPU=1 heldBytesExact=1 ledgerDrained=1\n",unsigned(critical));
    }
    {
        NorthlightAutoBump bump(&device);
        bump.setMemoryPressurePolicy(true,false);
        assert(bump.memoryPressure&&bump.deferredGPU.limit()==(32u<<20));
        assert(bump.deferredPacketLimit==512&&bump.deferredByteLimit==(8u<<20));
        assert(bump.deferredSnapshots.snapshotCacheLimit()==(4u<<20)&&DisplacementStateTestAccess::pressureLimit(bump.displacement)==(6u<<20));
        bump.setMemoryPressurePolicy(true,false);assert(bump.deferredGPU.limit()==(32u<<20));
        bump.setMemoryPressurePolicy(true,true);assert(bump.memoryPressure&&!bump.deferredGPU.limit()&&!bump.deferredGPU.chargedBytes());
        assert(bump.deferredPacketLimit==64&&bump.deferredByteLimit==(1u<<20));
        bump.setMemoryPressurePolicy(true,true);assert(bump.deferredPacketLimit==64&&bump.deferredByteLimit==(1u<<20));
        bump.setMemoryPressurePolicy(true,false);assert(bump.deferredGPU.limit()==(32u<<20));
        assert(bump.deferredPacketLimit==512&&bump.deferredByteLimit==(8u<<20));
        bump.trimMemory();assert(!bump.deferredGPU.chargedBytes()&&bump.deferredGPU.limit()==(32u<<20));
        bump.setMemoryPressurePolicy(false,false);assert(!bump.memoryPressure&&bump.deferredGPU.limit()==(32u<<20));
        assert(bump.deferredPacketLimit==512&&bump.deferredByteLimit==(8u<<20));
        std::puts("MATERIAL_PRESSURE_POLICY_PASS demand_32 critical_0 recovery_32 reduced_cpu_pools repeated trim");
    }
#endif
    std::printf("MATERIAL_PRESSURE_PASS before=%zu after=%zu shrinkEvictions=%u geometryShrinkEvictions=%u pressureMemoryReads=%u trimMemoryReads=%u recoveryMemoryReads=%u capMiB=42 allocationFree=1 heldBytesExact=1 declarationThread=1 predictionCleared=1 recovery=1\n",before,after,evictions,geometryEvictions,pressureReads,trimReads,recoveryReads);
}
int main(){MaterialCaptureStateTestAccess::pressureTransition();}
