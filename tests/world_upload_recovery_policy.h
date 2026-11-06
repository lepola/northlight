#pragma once
inline void WorldColdUploadTest::recoveryPolicy(UploadFaultDevice& d,D3DPRESENT_PARAMETERS& parameters){
    (void)parameters;const bool wasSpooling=Spooling;Spooling=true;
    auto renderer=std::make_unique<WorldRenderer>(&d);auto& w=*renderer;w.valid=true;assert(w.resources(128,128,D3DFMT_A16B16G16R16F));
    auto old=scene(0);complete(w,old);assert(!w.pressureReplayAllocation()&&w.uploadedMeshPlan.lock()==old->meshPlan);
    auto changedBeforeReset=scene(9);changedBeforeReset->bvh=old->bvh;w.active=changedBeforeReset;w.reset();
    assert(!w.pressureReplayAllocation()&&!w.resetReplayScoped); // A new plan cannot borrow a previous BVH commit.
    w.active=old;w.valid=true;assert(w.resources(128,128,D3DFMT_A16B16G16R16F));complete(w,old);w.reset();assert(w.pressureReplayAllocation()&&w.resetReplayScoped&&old->meshPlan->payload.live());
    auto arm=[&]{w.active=old;w.resetReplaySource=old->bvh;w.resetReplayPlan=old->meshPlan;w.resetReplayScoped=true;w.resetReplayPending=true;w.meshRetry.clear();};
    auto cancelled=[&]{assert(!w.pressureReplayAllocation()&&!w.resetReplayScoped&&!w.resetReplayPending&&!w.resetReplayQuery&&!d.queryAlive);};
    d.fakeQueries=true;d.queryCreateResult=d.queryIssueResult=S_OK;d.queryDataResult=S_FALSE;
    assert(!w.pressureReplayReady()&&d.queryAlive==1);auto replacement=scene(1);w.active=replacement;cancelled();
    arm();assert(!w.pressureReplayReady());replacement->bvh=old->bvh;w.active=replacement;cancelled(); // Same BVH, different plan is still a replacement.
    arm();w.resetReplayPending=false;const auto& key=old->meshPlan->payload.share()->pages->pages.front().key;
    assert(w.meshStore.beginStaging({key}));w.syncMeshBuffers();const auto id=w.meshStore.staging().uploadBuffer.front();
    {AddressBallast pressure;pressure.fill(240);assert(!w.acquireMeshPage(id,true,512u<<10,D3DPOOL_DEFAULT));assert(!w.meshBuffers[id].vb&&!w.meshStore.entries()[id].vbCapacity);}
    auto differentBvh=scene(2);
    {AddressBallast pressure;pressure.fill(248);const auto before=pressure.available();assert(before<NorthlightGeometryMemory::ProcessReserve);
     assert(w.pressureReplayAllocation()&&w.acquireMeshPage(id,true,64u<<10,D3DPOOL_DEFAULT));assert(w.meshBuffers[id].vb);
     const auto after=pressure.available();w.active=differentBvh;assert(!w.acquireMeshPage(id,false,64u<<10,D3DPOOL_DEFAULT)&&!w.resetReplayScoped&&!w.meshBuffers[id].ib);
     arm();w.resetReplayPending=false;w.active=replacement;assert(!w.acquireMeshPage(id,false,64u<<10,D3DPOOL_DEFAULT)&&!w.resetReplayScoped&&!w.meshBuffers[id].ib);
     std::printf("COLD_RECOVERY_POLICY_ALLOCATION entryMiB=%.6f afterCreateMiB=%.6f sameCommittedSource=1 oneBoundedResource=1 newBvhRefused=1 newPlanRefused=1\n",double(before)/1048576,double(after)/1048576);}
    arm();w.resetReplayPending=false;w.meshStore.cancel();w.releaseMeshPool();assert(w.pressureReplayAllocation());
    d.fakeQueries=false;w.valid=true;assert(w.resources(128,128,D3DFMT_A16B16G16R16F));d.failAt=d.indices+3;const auto before=d.failAt;
    for(unsigned i=0;i<2000&&!w.meshRetry.waiting;++i){NorthlightStreaming::Budget budget(4u<<20,2);w.upload(budget);assert(SUCCEEDED(d.Present(nullptr,nullptr,nullptr,nullptr)));}
    assert(d.indices==before&&w.meshRetry.waiting&&!w.pendingMesh&&w.meshStore.table().empty()&&old->meshPlan->payload.live()&&w.pressureReplayAllocation());
    d.failAt=0;w.meshRetry.failedAt-=1000;complete(w,old);assert(!w.resetReplayScoped&&!w.pressureReplayAllocation());
    w.reset();assert(w.pressureReplayAllocation());w.releaseGPU();assert(!w.resetReplayScoped&&!w.resetReplayPending&&!w.pressureReplayAllocation());
    assert(!d.queryAlive&&!d.queryReferences&&!d.queryFlushed);Spooling=wasSpooling;
    std::puts("COLD_RECOVERY_POLICY_PASS exactBvhAndPlan=1 replacementCancelsQuery=1 belowFloorRefused=1 actual248Admission=1 replacementsCannotBorrow=1 failureRetainsSource=1 commitClearsAllowance=1 fullReleaseCancels=1 initialFloorMiB=256 replayFloorMiB=240");
}
