    // Memory guard (memory_guard.h): render-thread side. Called only at the frame
    // boundary, after endFrame() recycled every replay: no capture, draw or GPU
    // binding of the finished frame still refers to a cache entry. Every cache
    // here is optional and keyed by identity/version/contents, so a later lookup
    // misses and rebuilds from the live buffers. Shared immutable snapshots stay
    // valid for any holder; mesh-keyed caches use weak owners (no ABA reuse).
    struct MemoryTrim {size_t terrain=0,snapshots=0,indices=0,pool=0,gpu=0;
        double quiesceMs=0,terrainMs=0,snapshotsMs=0,poolMs=0,replayGpuMs=0,meshPoolsMs=0,rigidMs=0;
        size_t total()const{return terrain+snapshots+indices+pool+gpu;}};
    bool memoryPressure=false;
    std::atomic<bool> workerMemoryTrim{false}; /* 0.3.153: the geometry builder thread drops its incremental local-geometry cache on its next request */
    std::atomic<bool> giMemoryTrim{false}; /* the GI worker frees its relight cache (ProbeRelightCache) on its next request */
    std::atomic<uint64_t> geometryMemoryDemand{0}; /* the geometry builder's refused aggregate floor while it stalls for memory (0: none); the guard's demand */
    bool staticRecoveryPaused=false;
    void reclaimInactiveCasters(){
        if(staticRecoveryPaused||!staticStream||(active&&active->bvh)||!geometryMemoryDemand.load(std::memory_order_relaxed))return;
        // The last selected models are pinned indefinitely by lastNeeded and
        // the published snapshot even while no world can use them. Give the
        // geometry builder priority; do this once, after all frame draws end.
        const auto cpu=staticScene?staticScene->stats.cpuBytes:0,metadata=staticScene?staticScene->stats.metadataBytes:0;
        staticRecoveryPaused=true;staticStream->setPaused(true);
        staticCasters.reset();staticMatcher.clear();staticM2Memory.clear();staticScene.reset();staticOwnerGeneration=UINT64_MAX;
        workerMemoryTrim.store(true,std::memory_order_relaxed);
        logf("STATIC SHADOW recovery paused=1 reason=geometry-demand scheduledCpuMiB=%.2f scheduledMetadataMiB=%.2f workerRelease=async",
             double(cpu)/1048576,double(metadata)/1048576);
    }
    static constexpr size_t ReplayPoolBytes=48u*1024u*1024u;
    size_t replayPoolLimit()const{return memoryPressure?ReplayPoolBytes/2:ReplayPoolBytes;}
    MemoryTrim trimMemory(){
        MemoryTrim t;auto phaseStart=std::chrono::steady_clock::now();
        const auto elapsed=[&](){const auto now=std::chrono::steady_clock::now();const double ms=std::chrono::duration<double,std::milli>(now-phaseStart).count();phaseStart=now;return ms;};
        prepareQuiesce();stateBlocks.clear();t.quiesceMs=elapsed(); /* 0.3.177 */
        {const auto before=terrainBoundsCache.sourceLiveBytes()+terrainBoundsCache.sourceMetadataBytes();terrainBoundsCache.clearPersistent();const auto after=terrainBoundsCache.sourceLiveBytes()+terrainBoundsCache.sourceMetadataBytes();t.terrain=size_t(before-std::min(before,after));}
        t.terrainMs=elapsed();
        const auto sourceBefore=replaySnapshots.sourceLiveBytes()+replaySnapshots.sharedSourceMetadataBytes();
        {const auto before=replaySnapshots.indexCacheBytes();replaySnapshots.clearIndexCache();t.indices=before-std::min(before,replaySnapshots.indexCacheBytes());} /* clears snapshots too */
        const auto sourceAfter=replaySnapshots.sourceLiveBytes()+replaySnapshots.sharedSourceMetadataBytes();t.snapshots=sourceBefore-std::min(sourceBefore,sourceAfter);
        t.snapshotsMs=elapsed();
        t.pool=pooledSnapshotBytes+freeReplays.size()*sizeof(Replay);freeReplays.clear();freeReplays.shrink_to_fit();pooledSnapshotBytes=0;
        t.poolMs=elapsed();
        t.gpu=replayGpuCache.bytes();replayGpuCache.clear();t.replayGpuMs=elapsed();
        t.gpu+=trimMeshPools();t.meshPoolsMs=elapsed(); /* 0.3.216: idle page buffers (never the active table), unless a staged upload is in progress */
        rigidMemoryClear(); /* 0.3.172: remembered rigid replays (their shared meshes and COM references) */
        t.rigidMs=elapsed();
        workerMemoryTrim.store(true,std::memory_order_relaxed);giMemoryTrim.store(true,std::memory_order_relaxed);
        return t;
    }
    // 0.3.172 near capture reserve (near_reserve.h): off under pressure and with ActorShadows=0.
    void setNearReserve(){replaySnapshots.setNearReserve(memoryPressure||!quality.actorShadows?0:NorthlightNearReserve::ReserveBytes);}
    // Demand keeps the existing world savings and one-shot trim, but terrain
    // retention is halved only after an actual low-address-space sample.
    void setMemoryPressure(bool on,bool halveTerrain){
        memoryPressure=on;setNearReserve();
        replaySnapshots.setSnapshotCacheLimit(on?NorthlightDrawSnapshot::Frame::SnapshotCacheLimit/2:NorthlightDrawSnapshot::Frame::SnapshotCacheLimit);
        terrainBoundsCache.setPersistentByteLimit(halveTerrain?16u*1024u*1024u:32u*1024u*1024u);
        replayGpuCache.setLimit(on?32u*1024u*1024u:64u*1024u*1024u);
    }
