#pragma once
inline void WorldColdUploadTest::fenceFailures(UploadFaultDevice& d,D3DPRESENT_PARAMETERS& parameters){
    (void)parameters;const bool wasSpooling=Spooling;Spooling=false;
    {
        auto renderer=std::make_unique<WorldRenderer>(&d);auto& w=*renderer;w.valid=true;assert(w.resources(128,128,D3DFMT_A16B16G16R16F));
        auto old=scene(0),next=scene(1);complete(w,old);w.fence().drop();
        const auto table=w.meshStore.table();assert(!table.empty());auto* first=w.vertices;
        for(auto id:table)assert(w.meshBuffers[id].vbPool==D3DPOOL_MANAGED);
        d.fakeQueries=true;
        auto intact=[&]{assert(!w.failed&&w.meshStore.table()==table&&w.vertices==first&&w.uploaded.lock()==old->bvh&&!d.queryFlushed);};
        auto fresh=[&]{drop(w.resetReplayQuery);w.resetReplayFence.drop();assert(!d.queryAlive&&!d.queryReferences);w.active=old;w.meshRetry.clear();w.resetReplayPending=true;w.resetReplayFailures=0;d.queryCreateResult=d.queryIssueResult=S_OK;d.queryDataResult=S_FALSE;intact();};
        auto retry=[&]{assert(w.resetReplayPending&&!w.resetReplayQuery&&w.resetReplayFailures==1&&w.meshRetry.waiting&&!d.queryAlive&&!d.queryReferences);intact();
            const auto creates=d.queryCreates,indices=d.indices;w.active=next;NorthlightStreaming::Budget budget(4u<<20,2);w.upload(budget);assert(d.queryCreates==creates&&d.indices==indices&&!w.pendingMesh);w.active=old;intact();};
        // Every query fault must leave the committed normal generation usable.
        fresh();d.queryCreateResult=E_OUTOFMEMORY;assert(!w.pressureReplayReady());retry();
        fresh();d.queryIssueResult=D3DERR_DEVICELOST;assert(!w.pressureReplayReady());retry();
        fresh();assert(!w.pressureReplayReady());assert(d.queryAlive==1&&d.queryReferences==1);d.queryDataResult=E_FAIL;assert(!w.pressureReplayReady());retry();
        fresh();assert(!w.pressureReplayReady());w.resetReplayStarted=GetTickCount()-5001;assert(!w.pressureReplayReady());retry();
        // A pending query is polled without FLUSH, extra query allocation, or a
        // retry delay; successful completion releases it before replay starts.
        fresh();const auto reads=d.queryReads,creates=d.queryCreates;assert(!w.pressureReplayReady());assert(d.queryReads==reads&&d.queryCreates==creates+1);
        for(unsigned i=0;i<8;++i){assert(!w.pressureReplayReady());assert(!w.meshRetry.waiting&&d.queryCreates==creates+1);intact();}
        d.queryDataResult=S_OK;assert(w.pressureReplayReady()&&!w.resetReplayPending&&!w.resetReplayQuery&&!d.queryAlive);intact();
        const auto noPendingCreates=d.queryCreates;assert(w.pressureReplayReady()&&d.queryCreates==noPendingCreates);
        // Its dedicated fence must never signal the live ring's current frame.
        fresh();auto* ring=w.fence().issue();assert(ring&&w.frameFence.current==ring);const auto issues=d.queryIssues;
        assert(!w.pressureReplayReady());assert(w.frameFence.current==ring&&d.queryIssues==issues+1);ring->Release();w.fence().drop();
        d.queryDataResult=S_OK;assert(w.pressureReplayReady()&&!d.queryAlive);intact();
        // A target failure during this wait is retryable and retains the mesh.
        fresh();assert(!w.pressureReplayReady());w.releaseGeometryTargets();d.failTextureAt=d.textures+1;
        assert(!w.geometryResources(128,128));d.failTextureAt=0;assert(w.resetReplayQuery&&w.resetReplayPending);intact();
        // Reset and release cancel query ownership without forgetting the need
        // to fence the next replay. The unrelated managed generation survives.
        w.reset();assert(!w.resetReplayQuery&&!w.resetReplayFence.current&&w.resetReplayPending&&!d.queryAlive&&!d.queryReferences);intact();
        w.valid=true;assert(w.resources(128,128,D3DFMT_A16B16G16R16F));w.meshRetry.clear();assert(!w.pressureReplayReady());
        w.releaseGPU(true);assert(!w.resetReplayQuery&&!w.resetReplayFence.current&&w.resetReplayPending&&!d.queryAlive&&!d.queryReferences);intact();
        w.valid=true;assert(w.resources(128,128,D3DFMT_A16B16G16R16F));w.meshRetry.clear();assert(!w.pressureReplayReady());d.queryDataResult=S_OK;
        assert(w.pressureReplayReady()&&!w.resetReplayPending&&!d.queryAlive);intact();
        d.fakeQueries=false;complete(w,next);assert(!w.failed&&w.uploaded.lock()==next->bvh);
        std::puts("COLD_FENCE_FAILURE_PASS create=1 issue=1 getData=1 timeout=1 pending=8 noFlush=1 independentRing=1 targetRollback=1 resetCancel=1 releaseCancel=1 recovery=1 balancedReferences=1");
    }
    assert(!d.queryAlive&&!d.queryReferences&&!d.queryFlushed);d.fakeQueries=false;Spooling=wasSpooling;
}
