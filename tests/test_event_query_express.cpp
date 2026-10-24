// Reuse the actual stream, SDK stub, fake resources and lifetime checks.
#define main unused_stream_main
#include "test_command_stream.cpp"
#undef main

struct QueryDrawTarget;
enum class BoundaryFault {None,Lost,Dead,MissingInner,MissingEnd};
struct BoundaryQuery:TQuery {
    QueryDrawTarget* owner=nullptr;
    HRESULT Issue(DWORD flags) override{if(flags&D3::kIssueEnd)++issued;return D3D_OK;}
    HRESULT GetData(void* data,DWORD size,DWORD flags) override;
};
struct QueryDrawTarget:TargetDevice {
    std::atomic<bool> firstEntered{false},releaseFirst{false},secondEntered{false},releaseSecond{false},secondRetired{false};
    std::atomic<bool> backgroundReady{false};
    unsigned draws=0,driverFlushCalls=0;bool driverAfterSecond=false,falseValue=false;
    HRESULT status=D3D_OK,publishStatus=D3D_OK;bool publishOnExit=false;BoundaryFault fault=BoundaryFault::None;StreamCore* core=nullptr;
    StreamQuery* proxy=nullptr;BoundaryQuery* query=nullptr;
    HRESULT CreateQuery(D3DQUERYTYPE type,IDirect3DQuery9** out) override{
        if(type!=8)return TargetDevice::CreateQuery(type,out);
        query=new BoundaryQuery;query->type=8;query->owner=this;*out=query;return D3D_OK;
    }
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT) override{
        const unsigned draw=draws++;
        if(draw==0){
            firstEntered.store(true);
            while(!releaseFirst.load())std::this_thread::yield();
            if(publishOnExit){unsigned char bytes[64]={};const DWORD value=falseValue?0:0xABCD0000u+query->issued;std::memcpy(bytes,&value,4);proxy->completeEvent(publishStatus,bytes);}
            if(fault==BoundaryFault::Lost)core->coopState.store(D3DERR_DEVICELOST);
            if(fault==BoundaryFault::Dead)proxy->dead.store(true);
            if(fault==BoundaryFault::MissingInner)proxy->inner=nullptr;
        }else{
            CHECK(draw==1);secondEntered.store(true);
            while(!releaseSecond.load())std::this_thread::yield();
            secondRetired.store(true);
        }
        return D3D_OK;
    }
};
HRESULT BoundaryQuery::GetData(void* data,DWORD size,DWORD flags){
    if(!flags){
        if(!owner->backgroundReady.load())return S_FALSE;
        if(data){const DWORD value=0xABCD0000u+issued;std::memcpy(data,&value,4);}
        return D3D_OK;
    }
    ++owner->driverFlushCalls;owner->driverAfterSecond=owner->secondRetired.load();
    if(flags!=D3::kGetDataFlush||(!data&&size)||(data&&size!=4))return D3DERR_INVALIDCALL;
    if(owner->status==D3D_OK&&data){const DWORD value=owner->falseValue?0:0xABCD0000u+issued;std::memcpy(data,&value,4);}
    return owner->status;
}
struct QueryRig {
    QueryDrawTarget* target=new QueryDrawTarget;StreamDevice* device=nullptr;FakeD3D parent;
    QueryRig(){
        StreamDevice::Options options;options.extension=&target->ext;options.directReplay=true;
        options.rawOf=[](IUnknown* p,unsigned){return unwrapFake(p);};
        D3DPRESENT_PARAMETERS pp{};pp.BackBufferWidth=640;pp.BackBufferHeight=480;pp.BackBufferFormat=22;pp.BackBufferCount=2;
        const char* reason=nullptr;device=StreamDevice::make(target,&parent,&pp,std::move(options),&reason);CHECK(device);
    }
    void sync(){DWORD value=0;CHECK(device->ValidateDevice(&value)==5);}
    void draw(){CHECK(device->DrawIndexedPrimitive(4,0,0,3,0,1)==D3D_OK);}
};
static QueryRig* pumping=nullptr;static IDirect3DQuery9* pumpingQuery=nullptr;static bool nestedActions=false,pumpedOnce=false,finalRelease=false,futureBeforeDraw=false;
static HRESULT nestedResult=0,nestedReset=0;
static void queryPump(){
    CHECK(inPumpedWait&&pumping);
    if(!pumpedOnce){
        pumpedOnce=true;
        if(nestedActions){
            nestedResult=pumpingQuery->GetData(nullptr,0,D3::kGetDataFlush);
            D3DPRESENT_PARAMETERS pp{};nestedReset=pumping->device->Reset(&pp);
            CHECK(pumpingQuery->Issue(D3::kIssueEnd)==D3D_OK);
            if(futureBeforeDraw)pumping->draw();
            pumping->device->streamCore().q.publish();
            CHECK(pumpingQuery->Release()==unsigned(finalRelease?1:2)); // The pumped call owns its own temporary reference.
        }
        pumping->target->releaseFirst.store(true);
    }else if(pumping->target->secondEntered.load())pumping->target->releaseSecond.store(true);
}
static void scenario(bool baseline,bool gated,const char* name,bool endSeen,bool reset,bool cached,HRESULT status,bool nested,bool nullPayload=false,bool invalidPayload=false,bool falsePayload=false,bool dropFinal=false,BoundaryFault fault=BoundaryFault::None,bool noIssue=false,DWORD flags=D3::kGetDataFlush){
    gTrace.clear();QueryRig rig;auto* t=rig.target;auto& core=rig.device->streamCore();
    IDirect3DQuery9* query=nullptr;CHECK(rig.device->CreateQuery(8,&query)==D3D_OK);rig.sync();
    auto* proxy=static_cast<StreamQuery*>(query);t->proxy=proxy;t->status=status;t->publishStatus=status;t->publishOnExit=cached;t->falseValue=falsePayload;t->fault=fault;t->core=&core;
    if(endSeen&&!noIssue){CHECK(query->Issue(D3::kIssueEnd)==D3D_OK);rig.sync();}
    if(reset){D3DPRESENT_PARAMETERS pp{};CHECK(rig.device->Reset(&pp)==D3D_OK);}
    rig.draw();core.q.publish();CHECK(waitFor([&]{return t->firstEntered.load();}));
    if(!endSeen&&!noIssue){
        if(fault==BoundaryFault::MissingEnd){proxy->gen.store(1);proxy->eventEpoch=core.queryEpoch.load();} // Impossible stream state must terminate instead of spinning.
        else CHECK(query->Issue(D3::kIssueEnd)==D3D_OK);
    }
    futureBeforeDraw=nested&&!endSeen;
    if(!futureBeforeDraw)rig.draw();
    core.q.publish();
    core.timing=true;proxy->eventLocalSince=nowNs()-3000000;
    if(nested&&!dropFinal)query->AddRef();
    pumping=&rig;pumpingQuery=query;nestedActions=nested;finalRelease=dropFinal;pumpedOnce=false;nestedResult=0;nestedReset=0;
    pumpHook=&queryPump;
    std::atomic<bool> cancel{false},watchdogFired{false};
    std::thread watchdog([&]{for(unsigned i=0;i<10000&&!cancel.load();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(!cancel.load()){watchdogFired.store(true);t->releaseFirst.store(true);t->releaseSecond.store(true);}});
    DWORD value=0x87654321u;
    const auto beforeSync=get(core.q.stats.syncCalls),beforeReadback=get(core.q.stats.expressReadbacks),beforeExpress=get(core.q.stats.expressQueries);
    const auto beforeHits=get(core.q.stats.expressQueryHits),beforeFallbacks=get(core.q.stats.expressQueryFallbacks);
    const HRESULT result=query->GetData(nullPayload?nullptr:&value,nullPayload?0u:invalidPayload?3u:4u,flags);
    cancel.store(true);watchdog.join();pumpHook=nullptr;pumping=nullptr;
    CHECK(!watchdogFired.load()&&pumpedOnce);
    const bool hit=!baseline&&(endSeen||gated)&&!reset&&!invalidPayload&&fault==BoundaryFault::None&&!noIssue&&flags==D3::kGetDataFlush;
    // The nested-future-END case records its later draw from the pump: the
    // original ordered task precedes that draw, whereas an ungated fallback
    // is appended after it. This case proves generation ordering, not uplift.
    const bool bypass=hit||(baseline&&futureBeforeDraw);
    CHECK(t->releaseSecond.load()!=bypass&&t->secondRetired.load()!=bypass);
    CHECK(get(core.q.stats.syncCalls)==beforeSync+1&&get(core.q.stats.expressReadbacks)==beforeReadback);
    const bool attempted=!baseline&&!reset&&!invalidPayload&&!noIssue&&flags==D3::kGetDataFlush;
    CHECK(get(core.q.stats.expressQueries)==beforeExpress+unsigned(attempted));
    CHECK(get(core.q.stats.expressQueryHits)==beforeHits+unsigned(hit));
    CHECK(get(core.q.stats.expressQueryFallbacks)==beforeFallbacks+unsigned(attempted&&!hit));
    CHECK(get(core.q.stats.expressNs)==get(core.q.stats.readbackExpressNs)+get(core.q.stats.queryExpressNs));
    const HRESULT expected=invalidPayload||fault==BoundaryFault::MissingInner||flags!=D3::kGetDataFlush?D3DERR_INVALIDCALL:status;
    CHECK(result==expected);
    const unsigned returnedGeneration=fault==BoundaryFault::MissingEnd||fault==BoundaryFault::Dead||noIssue?0u:!baseline&&!gated&&nested&&!endSeen?2u:1u;
    if(result==D3D_OK&&!nullPayload)CHECK(value==(falsePayload?0:0xABCD0000u+returnedGeneration));
    else CHECK(value==0x87654321u);
    if((bypass&&cached)||fault==BoundaryFault::MissingInner)CHECK(t->driverFlushCalls==0);
    else CHECK(t->driverFlushCalls>0&&t->driverAfterSecond==!bypass);
    const auto profile=core.queryFrame[8];
    CHECK(profile.calls==1&&profile.errors==unsigned(expected<0)&&profile.pending==unsigned(expected==S_FALSE));
    CHECK(profile.beforeNs+profile.replayNs+profile.afterNs==profile.tripNs);
    if(nested)CHECK(nestedResult==D3DERR_INVALIDCALL&&nestedReset==D3DERR_INVALIDCALL);
    std::printf("EVENT_BOUNDARY case=%s baseline=%u bypassedLaterDraw=%u driverFlushCalls=%u result=%d beforeReplayMs=%.4f replayMs=%.4f afterReplayMs=%.4f\n",
        name,unsigned(baseline),unsigned(bypass),t->driverFlushCalls,int(result),double(profile.beforeNs)/1e6,double(profile.replayNs)/1e6,double(profile.afterNs)/1e6);
    if(fault==BoundaryFault::MissingInner)core.q.runExpress([](void* p){auto* target=static_cast<QueryDrawTarget*>(p);target->proxy->inner=target->query;},t);
    if(fault==BoundaryFault::Dead)proxy->dead.store(false);
    t->releaseSecond.store(true);rig.sync();
    if(nested&&!dropFinal){
        proxy->eventLocalSince=nowNs()-3000000;value=0;
        CHECK(query->GetData(&value,4,D3::kGetDataFlush)==D3D_OK&&value==0xABCD0002u);
    }
    if(!dropFinal)query->Release();rig.sync();rig.device->Release();checkClean();
}
static void idleScenario(bool baseline,bool noPending){
    gTrace.clear();QueryRig rig;auto* t=rig.target;auto& core=rig.device->streamCore();
    IDirect3DQuery9* query=nullptr;CHECK(rig.device->CreateQuery(8,&query)==D3D_OK);rig.sync();
    auto* proxy=static_cast<StreamQuery*>(query);t->proxy=proxy;t->backgroundReady.store(noPending);
    CHECK(query->Issue(D3::kIssueEnd)==D3D_OK);rig.sync();
    if(noPending)CHECK(waitFor([&]{return proxy->eventDoneGen.load(std::memory_order_acquire)==1;}));
    const auto sleeps=get(core.q.stats.consumerSleeps);
    // Completion can leave one final nextTimed wait. Two new sleeps prove the
    // subsequent empty/no-pending next(true) path was also reached.
    CHECK(waitFor([&]{return get(core.q.stats.consumerSleeps)>=sleeps+(noPending?2u:1u);}));
    CHECK(core.q.depth()==0);
    if(noPending)proxy->eventDoneGen.store(0,std::memory_order_release); // Exercise the cold-cache pumped path after background removal.
    core.timing=true;proxy->eventLocalSince=nowNs()-3000000;
    const auto posts=get(core.q.stats.expressQueries),hits=get(core.q.stats.expressQueryHits),fallbacks=get(core.q.stats.expressQueryFallbacks);
    std::atomic<bool> done{false};
    std::thread watchdog([&]{for(unsigned i=0;i<10000&&!done.load();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));CHECK(done.load());});
    DWORD value=0;CHECK(query->GetData(&value,4,D3::kGetDataFlush)==D3D_OK&&value==0xABCD0001u);
    done.store(true);watchdog.join();
    CHECK(t->driverFlushCalls==1&&get(core.q.stats.expressQueries)==posts+unsigned(!baseline));
    CHECK(get(core.q.stats.expressQueryHits)==hits+unsigned(!baseline)&&get(core.q.stats.expressQueryFallbacks)==fallbacks);
    std::printf("EVENT_IDLE noPending=%u baseline=%u driverFlushCalls=%u\n",unsigned(noPending),unsigned(baseline),t->driverFlushCalls);
    query->Release();rig.sync();rig.device->Release();checkClean();
}
int main(int argc,char** argv){
    const bool baseline=argc>1&&std::string(argv[1])=="baseline";
    const bool gated=!(argc>1&&std::string(argv[1])=="ungated");
    scenario(baseline,gated,"seen-end",true,false,false,D3D_OK,false);
    scenario(baseline,gated,"cached-at-boundary",true,false,true,D3D_OK,false);
    scenario(baseline,gated,"cached-FALSE",true,false,true,D3D_OK,false,false,false,true);
    scenario(baseline,gated,"first-end-not-seen",false,false,false,D3D_OK,false);
    scenario(baseline,gated,"driver-error",true,false,false,D3DERR_DEVICELOST,false);
    scenario(baseline,gated,"coop-loss-at-boundary",true,false,false,D3DERR_DEVICELOST,false,false,false,false,false,BoundaryFault::Lost);
    scenario(baseline,gated,"cached-error",true,false,true,D3DERR_DEVICELOST,false);
    scenario(baseline,gated,"pending-budget",true,false,false,S_FALSE,false);
    scenario(baseline,gated,"reset-epoch",true,true,false,D3D_OK,false);
    scenario(baseline,gated,"nested-reissue-reset-release",true,false,false,D3D_OK,true);
    scenario(baseline,gated,"nested-final-release",true,false,false,D3D_OK,true,false,false,false,true);
    scenario(baseline,gated,"null-payload",true,false,false,D3D_OK,false,true);
    scenario(baseline,gated,"invalid-payload",true,false,false,D3D_OK,false,false,true);
    scenario(baseline,gated,"never-issued",true,false,false,D3D_OK,false,false,false,false,false,BoundaryFault::None,true);
    scenario(baseline,gated,"nested-future-end",false,false,false,D3D_OK,true);
    scenario(baseline,gated,"missing-end-drained",false,false,false,D3D_OK,false,false,false,false,false,BoundaryFault::MissingEnd);
    scenario(baseline,gated,"dead-at-boundary",false,false,false,D3D_OK,false,false,false,false,false,BoundaryFault::Dead);
    scenario(baseline,gated,"missing-inner-at-boundary",false,false,false,D3D_OK,false,false,false,false,false,BoundaryFault::MissingInner);
    scenario(baseline,gated,"invalid-flags",true,false,false,D3D_OK,false,false,false,false,false,BoundaryFault::None,false,D3::kGetDataFlush|4);
    idleScenario(baseline,false);idleScenario(baseline,true);
    textureExpressReadback();checkClean();
    std::puts("PASS exact EVENT boundary wait: deterministic later-draw dependency, ordered fallback, payload/error/pending, reset, nested reissue and extra-reference release; no game/GPU");
}
