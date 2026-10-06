// StreamDevice tests against the fake Target: targeted cases (lifetime, StreamState, locks, queries, Reset, backpressure,
// nested sync, shutdown) and the equivalence run. Included by test_command_stream.cpp after command_stream_targets.h.
#pragma once
#include <unordered_map>
struct FakeD3D:IDirect3D9 {};
static void checkClean(){if(gLiveTargets.load()||liveProxyObjects.load()){std::fprintf(stderr,"leak: targets=%d proxies=%ld\n",gLiveTargets.load(),liveProxyObjects.load());std::abort();}}
struct Rig {
    TargetDevice* target=nullptr;IDirect3DDevice9* dev=nullptr;StreamDevice* sd=nullptr;FakeD3D parent;const char* reason=nullptr;
    explicit Rig(bool streamed,StreamDevice::Options opt={}){
        target=new TargetDevice;
        if(streamed){
            D3DPRESENT_PARAMETERS pp{};pp.BackBufferWidth=640;pp.BackBufferHeight=480;pp.BackBufferFormat=(D3DFORMAT)22;pp.BackBufferCount=2;
            sd=StreamDevice::make(target,&parent,&pp,std::move(opt),&reason);CHECK(sd);dev=sd;
        }else dev=target;
    }
    StreamCore& core(){return sd->streamCore();}
    void sync(){CHECK(dev->TestCooperativeLevel()==5);}
    void finish(){dev->Release();dev=nullptr;}
};
static std::vector<std::string> filtered(const std::vector<std::string>& t){std::vector<std::string> r;for(auto& s:t){if(isGetName(s))continue;if(s.size()>3&&s.compare(s.size()-3,3," ro")==0&&s.compare(2,9,"::Unlock ")==0)continue;r.push_back(s);}return r;}   // a READONLY buffer lock is served from the shadow: no Target event

static void lifetimeAndIdentity(){
    gTrace.clear();Rig rig(true);auto& core=rig.core();const auto base=get(core.q.stats.syncCalls);
    IDirect3DTexture9* tex=nullptr;CHECK(rig.dev->CreateTexture(64,64,0,0,(D3DFORMAT)22,(D3DPOOL)1,&tex,nullptr)==D3D_OK&&tex);
    CHECK(tex->GetLevelCount()==7);   // Levels=0: the full chain, answered locally
    D3DSURFACE_DESC d{};CHECK(tex->GetLevelDesc(3,&d)==D3D_OK&&d.Width==8&&d.Height==8&&d.Format==(D3DFORMAT)22);
    IDirect3DSurface9 *a=nullptr,*b=nullptr;CHECK(tex->GetSurfaceLevel(2,&a)==D3D_OK&&tex->GetSurfaceLevel(2,&b)==D3D_OK&&a==b);   // children are cached: one identity
    void* c=nullptr;CHECK(a->GetContainer(__uuidof(IDirect3DTexture9),&c)==S_OK&&c==tex);static_cast<IUnknown*>(c)->Release();
    IDirect3DBaseTexture9* base9=nullptr;CHECK(tex->QueryInterface(__uuidof(IDirect3DBaseTexture9),reinterpret_cast<void**>(&base9))==S_OK&&base9==tex);base9->Release();
    void* none=nullptr;CHECK(tex->QueryInterface(__uuidof(IDirect3DDevice9),&none)==E_NOINTERFACE&&get(core.q.stats.qiMisses)==1);
    // a texture released while it is still bound stays alive; Get returns the same object
    gKnobs.hold.store(true);rig.dev->BeginScene();   // the replay thread is stuck: everything below is only queued
    CHECK(rig.dev->SetTexture(0,tex)==D3D_OK);tex->Release();a->Release();a->Release();   // both game references of the level and the texture's own go
    IDirect3DBaseTexture9* got=nullptr;CHECK(rig.dev->GetTexture(0,&got)==D3D_OK&&got==static_cast<IDirect3DBaseTexture9*>(tex));
    CHECK(get(core.q.stats.syncCalls)==base);   // answered from StreamState while the replay thread is busy
    got->Release();
    CHECK(rig.dev->SetTexture(0,nullptr)==D3D_OK);   // the last bind goes: one Destroy, recorded after every use
    gKnobs.hold.store(false);rig.sync();
    // a dead create: the proxy exists, its commands are dropped, the failure is counted once
    IDirect3DCubeTexture9* cube=nullptr;CHECK(rig.dev->CreateCubeTexture(16,1,0,(D3DFORMAT)22,(D3DPOOL)1,&cube,nullptr)==D3D_OK&&cube);
    CHECK(rig.dev->SetTexture(1,cube)==D3D_OK);cube->PreLoad();rig.sync();
    CHECK(get(core.q.stats.deadCreates)>=1);rig.dev->SetTexture(1,nullptr);cube->Release();rig.sync();
    rig.finish();checkClean();
}

static void stateKnownUnknown(){
    gTrace.clear();Rig rig(true);auto& core=rig.core();auto& s=core.q.stats;
    rig.sync();const auto syncs=get(s.syncCalls);
    DWORD v=0;
    CHECK(rig.dev->GetRenderState((D3DRENDERSTATETYPE)8,&v)==D3D_OK&&v==1008&&get(s.syncCalls)==syncs);   // a default read by the defaults batch
    CHECK(rig.dev->SetRenderState((D3DRENDERSTATETYPE)7,99)==D3D_OK&&rig.dev->GetRenderState((D3DRENDERSTATETYPE)7,&v)==D3D_OK&&v==99&&get(s.syncCalls)==syncs);
    DWORD tv=0;CHECK(rig.dev->GetSamplerState(3,(D3DSAMPLERSTATETYPE)2,&tv)==D3D_OK&&tv==2000+3*16+2&&rig.dev->GetSamplerState(257,(D3DSAMPLERSTATETYPE)2,&tv)==D3D_OK&&tv==2000+17*16+2);
    D3DLIGHT9 light{};CHECK(rig.dev->GetLight(3,&light)==5&&get(s.syncCalls)==syncs+1);   // never set and never read: a sync call (the fake answers 5)
    D3DVIEWPORT9 vp{};CHECK(rig.dev->GetViewport(&vp)==D3D_OK&&vp.Width==640&&get(s.syncCalls)==syncs+1);
    // implicit objects: the default render target is the swap chain's back buffer proxy
    IDirect3DSurface9 *rt=nullptr,*rt2=nullptr,*bb=nullptr;
    CHECK(rig.dev->GetRenderTarget(0,&rt)==D3D_OK&&rig.dev->GetRenderTarget(0,&rt2)==D3D_OK&&rt==rt2&&rig.dev->GetBackBuffer(0,0,(D3DBACKBUFFER_TYPE)0,&bb)==D3D_OK&&bb==rt);
    D3DSURFACE_DESC d{};CHECK(bb->GetDesc(&d)==D3D_OK&&d.Width==640&&d.Height==480);rt->Release();rt2->Release();bb->Release();
    IDirect3DSurface9* ds=nullptr;CHECK(rig.dev->GetDepthStencilSurface(&ds)==D3D_OK&&ds);ds->Release();
    // a state block's Apply makes StreamState unknown; the Get then reads the Target
    IDirect3DStateBlock9* sb=nullptr;CHECK(rig.dev->CreateStateBlock((D3DSTATEBLOCKTYPE)1,&sb)==D3D_OK);
    const auto before=get(s.syncCalls);CHECK(sb->Apply()==D3D_OK);CHECK(rig.dev->GetRenderState((D3DRENDERSTATETYPE)7,&v)==D3D_OK&&v==99&&get(s.syncCalls)==before+1);
    // recording: setters are recorded but StreamState does not follow (as DeviceMirror::recording)
    CHECK(rig.dev->BeginStateBlock()==D3D_OK&&rig.dev->BeginStateBlock()==D3DERR_INVALIDCALL);
    rig.dev->SetRenderState((D3DRENDERSTATETYPE)9,4242);IDirect3DStateBlock9* made=nullptr;CHECK(rig.dev->EndStateBlock(&made)==D3D_OK&&made);
    rig.sync();CHECK(rig.dev->GetRenderState((D3DRENDERSTATETYPE)9,&v)==D3D_OK&&v==1009);bool sawBegin=false,sawSet=false,sawEnd=false;for(auto& t:gTrace){sawBegin|=t=="Device::BeginStateBlock";sawSet|=t.rfind("Device::SetRenderState 9.000000",0)==0;sawEnd|=t=="Device::EndStateBlock";}
    CHECK(sawBegin&&sawSet&&sawEnd);
    sb->Release();made->Release();
    // a slot the audit declared sync-only always reads the Target
    CHECK(rig.dev->SetRenderState((D3DRENDERSTATETYPE)20,5)==D3D_OK);core.syncOnly[0].fetch_or(1ull<<20);
    const auto s0=get(s.syncCalls);CHECK(rig.dev->GetRenderState((D3DRENDERSTATETYPE)20,&v)==D3D_OK&&v==5&&get(s.syncCalls)==s0+1);
    // a failed replayed setter invalidates StreamState at the next Get
    core.replayFailure.store(true);const auto s1=get(s.syncCalls);CHECK(rig.dev->GetRenderState((D3DRENDERSTATETYPE)7,&v)==D3D_OK&&get(s.syncCalls)==s1+1);
    rig.finish();checkClean();
}

static void locksPreserveBytes(){
    gTrace.clear();Rig rig(true);auto& s=rig.core().q.stats;
    IDirect3DVertexBuffer9* vb=nullptr;CHECK(rig.dev->CreateVertexBuffer(1024,D3::kUsageDynamic,0,(D3DPOOL)1,&vb,nullptr)==D3D_OK);
    auto lockWrite=[&](UINT off,UINT size,DWORD flags,unsigned char fill){void* p=nullptr;CHECK(vb->Lock(off,size,&p,flags)==D3D_OK);std::memset(p,fill,size?size:1024-off);CHECK(vb->Unlock()==D3D_OK);};
    auto readAll=[&](std::vector<unsigned char>& out){void* p=nullptr;CHECK(vb->Lock(0,0,&p,D3::kLockReadOnly)==D3D_OK);out.assign((unsigned char*)p,(unsigned char*)p+1024);CHECK(vb->Unlock()==D3D_OK);};
    lockWrite(0,0,0,0xA1);lockWrite(100,50,0,0xB2);lockWrite(1000,24,D3::kLockNoOverwrite,0xC3);   // flags 0, a partial range, a NOOVERWRITE ring step
    std::vector<unsigned char> got;readAll(got);
    for(int i=0;i<1024;++i){const unsigned char want=(i>=100&&i<150)?0xB2:(i>=1000)?0xC3:0xA1;CHECK(got[i]==want);}   // untouched bytes keep their content
    lockWrite(0,0,D3::kLockDiscard,0xD4);readAll(got);for(int i=0;i<1024;++i)CHECK(got[i]==0xD4);
    CHECK(get(s.passThrough[unsigned(PassReason::ReadOnly)])==0);   // READONLY on a shadowed buffer is served from the shadow
    rig.sync();
    // the Target saw the same bytes: its memory equals what the game last saw
    unsigned unlocks=0;for(auto& t:gTrace)if(t.rfind("VB::Unlock",0)==0)++unlocks;CHECK(unlocks==4);
    CHECK(get(s.wholeLockBytes)>=2048&&get(s.lockRecordedBytes)>=2048+50+24);   // whole-buffer locks (size 0) record the whole buffer each Unlock, and are counted
    {   // DXVK range rules: a size past the end is clamped, only an offset beyond the end fails
        void* q=nullptr;CHECK(vb->Lock(1000,5000,&q,0)==D3D_OK&&vb->Unlock()==D3D_OK);CHECK(vb->Lock(1025,4,&q,0)==D3DERR_INVALIDCALL);CHECK(vb->Lock(1024,0,&q,0)==D3D_OK&&vb->Unlock()==D3D_OK);
    }
    vb->Release();
    {   // a static buffer has no shadow: the first write lock is asynchronous, the second passes through
        IDirect3DVertexBuffer9* st=nullptr;CHECK(rig.dev->CreateVertexBuffer(256,0,0,(D3DPOOL)1,&st,nullptr)==D3D_OK);void* q=nullptr;
        const auto pass=get(s.passThrough[unsigned(PassReason::NoShadow)]);
        CHECK(st->Lock(0,0,&q,0)==D3D_OK&&st->Unlock()==D3D_OK&&get(s.passThrough[unsigned(PassReason::NoShadow)])==pass);
        CHECK(st->Lock(0,0,&q,0)==D3D_OK&&st->Unlock()==D3D_OK&&get(s.passThrough[unsigned(PassReason::NoShadow)])==pass+1);
        st->Release();
    }
    // a big static buffer has no shadow: the first write lock is asynchronous, later ones pass through
    IDirect3DVertexBuffer9* big=nullptr;CHECK(rig.dev->CreateVertexBuffer(5u<<20,0,0,(D3DPOOL)1,&big,nullptr)==D3D_OK);
    void* p=nullptr;const auto np0=get(s.passThrough[unsigned(PassReason::NoShadow)]);CHECK(big->Lock(0,4096,&p,0)==D3D_OK);std::memset(p,0x5A,4096);CHECK(big->Unlock()==D3D_OK&&get(s.passThrough[unsigned(PassReason::NoShadow)])==np0);
    CHECK(big->Lock(0,4096,&p,0)==D3D_OK&&p&&get(s.passThrough[unsigned(PassReason::NoShadow)])==np0+1);CHECK(((unsigned char*)p)[7]==0x5A);CHECK(big->Unlock()==D3D_OK);   // real memory, first write already there
    CHECK(big->Lock(0,16,&p,D3::kLockReadOnly)==D3D_OK&&get(s.passThrough[unsigned(PassReason::ReadOnly)])==1&&((unsigned char*)p)[0]==0x5A);CHECK(big->Unlock()==D3D_OK);
    {   // MANAGED pool: DXVK ignores DISCARD there and keeps the old bytes, so it is a plain lock, never the zero-filled staging path
        const auto async=get(s.lockAsync),pass=get(s.passThrough[unsigned(PassReason::NoShadow)]);
        CHECK(big->Lock(0,16,&p,D3::kLockDiscard)==D3D_OK&&((unsigned char*)p)[7]==0x5A&&get(s.lockAsync)==async&&get(s.passThrough[unsigned(PassReason::NoShadow)])==pass+1);CHECK(big->Unlock()==D3D_OK);
    }
    big->Release();
    {   // DEFAULT pool: DISCARD needs no old bytes, so it is asynchronous; a small overrun of the staged range stays inside the allocation (ASan)
        IDirect3DVertexBuffer9* d=nullptr;CHECK(rig.dev->CreateVertexBuffer(256,0,0,(D3DPOOL)0,&d,nullptr)==D3D_OK);void* q=nullptr;
        CHECK(d->Lock(0,0,&q,0)==D3D_OK&&d->Unlock()==D3D_OK);const auto async=get(s.lockAsync);
        CHECK(d->Lock(0,64,&q,D3::kLockDiscard)==D3D_OK&&get(s.lockAsync)==async+1);std::memset(q,0x33,64+100);CHECK(d->Unlock()==D3D_OK);d->Release();
        IDirect3DVertexBuffer9* f=nullptr;CHECK(rig.dev->CreateVertexBuffer(100,D3::kUsageDynamic,0,(D3DPOOL)0,&f,nullptr)==D3D_OK);   // shadowed: the same tolerance
        CHECK(f->Lock(0,0,&q,0)==D3D_OK);std::memset(q,0x44,100+100);CHECK(f->Unlock()==D3D_OK);f->Release();
    }
    // textures: first write lock staged with our pitch (DXT block rows), later write locks and READONLY pass through
    IDirect3DTexture9* dxt=nullptr;CHECK(rig.dev->CreateTexture(16,16,1,0,(D3DFORMAT)0x31545844,(D3DPOOL)1,&dxt,nullptr)==D3D_OK);
    D3DLOCKED_RECT lr{};CHECK(dxt->LockRect(0,&lr,nullptr,0)==D3D_OK&&lr.Pitch==32);   // 4 blocks of 8 bytes per block row
    for(int r=0;r<4;++r)std::memset((unsigned char*)lr.pBits+r*lr.Pitch,0x10+r,32);CHECK(dxt->UnlockRect(0)==D3D_OK);rig.sync();
    CHECK(dxt->LockRect(0,&lr,nullptr,0)==D3D_OK&&get(s.passThrough[unsigned(PassReason::Written)])==1);CHECK(lr.Pitch==16*4+16);CHECK(dxt->UnlockRect(0)==D3D_OK);   // the Target's own pitch
    CHECK(dxt->LockRect(0,&lr,nullptr,D3::kLockReadOnly)==D3D_OK&&get(s.passThrough[unsigned(PassReason::ReadOnly)])==2);
    unsigned char row0[32];std::memcpy(row0,lr.pBits,32);for(int i=0;i<32;++i)CHECK(row0[i]==0x10);CHECK(dxt->UnlockRect(0)==D3D_OK);
    dxt->Release();
    IDirect3DTexture9* unk=nullptr;CHECK(rig.dev->CreateTexture(8,8,1,0,(D3DFORMAT)999,(D3DPOOL)1,&unk,nullptr)==D3D_OK);   // unknown format: pass-through, counted
    CHECK(unk->LockRect(0,&lr,nullptr,0)==D3D_OK&&get(s.passThrough[unsigned(PassReason::Format)])==1);CHECK(unk->UnlockRect(0)==D3D_OK);unk->Release();
    rig.finish();checkClean();
}

static void shadowCap(){
    gTrace.clear();Rig rig(true);auto& q=rig.core().q;auto& s=q.stats;
    std::vector<IDirect3DVertexBuffer9*> v;
    for(int i=0;i<14;++i){IDirect3DVertexBuffer9* b=nullptr;CHECK(rig.dev->CreateVertexBuffer(2u<<20,D3::kUsageDynamic,0,(D3DPOOL)0,&b,nullptr)==D3D_OK);v.push_back(b);}
    CHECK(s.shadowBytes.load()==std::int64_t(12)*(2<<20));   // 24 MiB: the 13th and 14th are refused (never evicting a live one) but still work
    CHECK(get(s.shadowRefused)==2&&get(s.shadowRefusedBytes)==2u*(2u<<20));
    void* p=nullptr;CHECK(v[13]->Lock(0,64,&p,D3::kLockDiscard)==D3D_OK&&v[13]->Unlock()==D3D_OK&&get(s.shadowLate)==0);   // still no room
    v[0]->Release();v[1]->Release();rig.sync();   // room again: a refused buffer takes its shadow at its next DISCARD lock, and only there
    CHECK(v[12]->Lock(0,64,&p,0)==D3D_OK&&v[12]->Unlock()==D3D_OK&&get(s.shadowLate)==0);
    CHECK(v[12]->Lock(0,64,&p,D3::kLockDiscard)==D3D_OK&&v[12]->Unlock()==D3D_OK&&get(s.shadowLate)==1&&s.shadowBytes.load()==std::int64_t(11)*(2<<20));
    v[0]=v[1]=nullptr;
    rig.sync();q.setPressure(true);CHECK(q.shadowCap()==ShadowBudgetBytes/2&&!q.shadowAdmit(1<<20)&&s.shadowBytes.load()==std::int64_t(11)*(2<<20));
    q.setPressure(false);for(auto* b:v)if(b)b->Release();rig.sync();CHECK(s.shadowBytes.load()==0);   // freed with their proxies
    rig.finish();checkClean();
}
static void queriesAndSyncCensus(){
    gTrace.clear();Rig rig(true);auto& s=rig.core().q.stats;
    {   // a query never issued: the real GetData, not S_FALSE forever
        IDirect3DQuery9* nq=nullptr;CHECK(rig.dev->CreateQuery((D3DQUERYTYPE)9,&nq)==D3D_OK);DWORD d=0;const auto syncs=get(s.syncCalls);
        nq->GetData(&d,4,0);CHECK(get(s.syncCalls)==syncs+1);nq->Release();
    }
    IDirect3DQuery9* q=nullptr;CHECK(rig.dev->CreateQuery((D3DQUERYTYPE)9,&q)==D3D_OK&&q->GetDataSize()==4&&q->GetType()==(D3DQUERYTYPE)9);
    DWORD data=0;CHECK(q->Issue(D3::kIssueEnd)==D3D_OK);
    int spins=0;HRESULT hr;while((hr=q->GetData(&data,4,0))==S_FALSE&&++spins<2000000)std::this_thread::yield();   // terminates: every GetData publishes
    CHECK(hr==D3D_OK&&data==0xABCD0000u);(void)spins;
    data=0;CHECK(q->Issue(D3::kIssueEnd)==D3D_OK&&q->GetData(&data,4,D3::kGetDataFlush)==D3D_OK&&data==0xABCD0000u);   // FLUSH: the real GetData after everything recorded
    CHECK(get(s.census[(std::size_t)Cmd::SyncGetData])==2&&std::string(cmdName(Cmd::SyncGetData))=="SyncGetData");   // the flush wait and the never-issued query show by name
    q->Release();
    // every sync class drains and is counted by name
    rig.dev->SetRenderState((D3DRENDERSTATETYPE)7,1);DWORD n=0;D3DDISPLAYMODE dm{};D3DRASTER_STATUS rs{};D3DGAMMARAMP ramp{};D3DCLIPSTATUS9 cs{};
    CHECK(rig.dev->TestCooperativeLevel()==5&&rig.core().q.depth()==0);
    CHECK(rig.dev->ValidateDevice(&n)==5&&rig.dev->GetDisplayMode(0,&dm)==5&&rig.dev->GetRasterStatus(0,&rs)==5&&rig.dev->GetClipStatus(&cs)==5);rig.dev->GetGammaRamp(0,&ramp);
    for(Cmd c:{Cmd::Device_TestCooperativeLevel,Cmd::Device_ValidateDevice,Cmd::Device_GetDisplayMode,Cmd::Device_GetRasterStatus,Cmd::Device_GetClipStatus,Cmd::Device_GetGammaRamp})CHECK(get(s.census[(std::size_t)c])==1);
    IDirect3DSurface9* off=nullptr;CHECK(rig.dev->CreateOffscreenPlainSurface(8,8,(D3DFORMAT)22,(D3DPOOL)2,&off,nullptr)==D3D_OK);
    CHECK(rig.dev->GetRenderTargetData(off,off)==5&&rig.dev->GetFrontBufferData(0,off)==5);off->Release();
    rig.finish();checkClean();
}

static void resetAndShutdown(){
    gTrace.clear();Rig rig(true);
    IDirect3DSurface9* bb=nullptr;CHECK(rig.dev->GetBackBuffer(0,0,(D3DBACKBUFFER_TYPE)0,&bb)==D3D_OK);bb->Release();   // the game released it: required before Reset
    IDirect3DTexture9* t=nullptr;rig.dev->CreateTexture(32,32,1,0,(D3DFORMAT)22,(D3DPOOL)1,&t,nullptr);rig.dev->SetTexture(0,t);rig.dev->SetRenderState((D3DRENDERSTATETYPE)7,77);
    D3DPRESENT_PARAMETERS pp{};pp.BackBufferWidth=800;pp.BackBufferHeight=600;pp.BackBufferFormat=(D3DFORMAT)22;pp.BackBufferCount=2;
    CHECK(rig.dev->Reset(&pp)==D3D_OK);
    DWORD v=0;CHECK(rig.dev->GetRenderState((D3DRENDERSTATETYPE)7,&v)==D3D_OK&&v==1007);   // the defaults batch ran again
    IDirect3DBaseTexture9* g=nullptr;CHECK(rig.dev->GetTexture(0,&g)==D3D_OK&&g==nullptr);   // Reset unbinds
    D3DSURFACE_DESC d{};CHECK(rig.dev->GetBackBuffer(0,0,(D3DBACKBUFFER_TYPE)0,&bb)==D3D_OK&&bb->GetDesc(&d)==D3D_OK&&d.Width==800&&d.Height==600);   // re-derived
    IDirect3DSurface9* rt=nullptr;CHECK(rig.dev->GetRenderTarget(0,&rt)==D3D_OK&&rt==bb);rt->Release();
    CHECK(rig.dev->Reset(&pp)==D3DERR_INVALIDCALL);   // the game still holds a back buffer: the Target refuses, the stream reports it
    bb->Release();CHECK(rig.dev->Reset(&pp)==D3D_OK);
    t->Release();
    rig.finish();CHECK(gLiveTargets.load()==0&&liveProxyObjects.load()==0&&!gTrace.empty());   // the final Release joined the replay thread
}

static void cursorAndForeignThread(){
    gTrace.clear();Rig rig(true);auto& s=rig.core().q.stats;
    CHECK(rig.dev->ShowCursor(1)==0&&rig.dev->ShowCursor(0)==1);   // the previous visibility, tracked locally
    std::thread other([&]{rig.dev->SetCursorPosition(5,6,7);});other.join();   // a foreign thread never touches the queue
    CHECK(get(s.foreignEntries)==1);
    rig.dev->SetCursorPosition(1,2,3);rig.sync();   // the game thread's next entry drains the foreign one first
    std::vector<std::string> cursor;for(auto& t:gTrace)if(t.rfind("Device::SetCursorPosition",0)==0||t.rfind("Device::ShowCursor",0)==0)cursor.push_back(t);
    CHECK(cursor.size()==4&&cursor[0].rfind("Device::ShowCursor",0)==0&&cursor[2].find("5.000000 6.000000 7.000000")!=std::string::npos&&cursor[3].find("1.000000 2.000000 3.000000")!=std::string::npos);
    rig.finish();checkClean();
}
// D3D9/DXVK keep the device alive while any child is publicly referenced: releasing the device first must not free the queue
// under a later Release of a texture, a buffer or the swap chain (ASan), and the Target goes away exactly once, at the last one.
static void childrenOutliveTheDevice(){
    gTrace.clear();const int deletes=gDeviceDeletes.load();Rig rig(true);
    IDirect3DTexture9* tex=nullptr;IDirect3DVertexBuffer9* vb=nullptr;IDirect3DSwapChain9* sc=nullptr;IDirect3DSurface9 *lvl=nullptr,*bb=nullptr;IDirect3DDevice9* back=nullptr;
    CHECK(rig.dev->CreateTexture(32,32,0,0,(D3DFORMAT)22,(D3DPOOL)1,&tex,nullptr)==D3D_OK&&rig.dev->CreateVertexBuffer(256,D3::kUsageDynamic,0,(D3DPOOL)0,&vb,nullptr)==D3D_OK);
    CHECK(rig.dev->GetSwapChain(0,&sc)==D3D_OK&&tex->GetSurfaceLevel(1,&lvl)==D3D_OK&&rig.dev->GetBackBuffer(0,0,(D3DBACKBUFFER_TYPE)0,&bb)==D3D_OK);
    rig.dev->SetTexture(0,tex);   // a bind pins nothing
    void* p=nullptr;CHECK(vb->Lock(0,0,&p,0)==D3D_OK&&vb->Unlock()==D3D_OK);
    rig.dev->Release();   // the game's reference: the device lives on for the objects it handed out
    CHECK(gDeviceDeletes.load()==deletes&&tex->GetDevice(&back)==D3D_OK&&back==rig.sd);back->Release();
    D3DSURFACE_DESC d{};CHECK(lvl->GetDesc(&d)==D3D_OK&&d.Width==16);
    tex->Release();vb->Release();sc->Release();CHECK(gDeviceDeletes.load()==deletes);   // the level and the back buffer still hold it
    lvl->Release();CHECK(gDeviceDeletes.load()==deletes);
    bb->Release();   // the last public reference: the device, the queue and the replay thread go
    CHECK(gDeviceDeletes.load()==deletes+1);checkClean();
    // and without the game keeping a bound proxy alive: a bound texture does not keep the device
    Rig again(true);IDirect3DTexture9* t2=nullptr;CHECK(again.dev->CreateTexture(8,8,1,0,(D3DFORMAT)22,(D3DPOOL)1,&t2,nullptr)==D3D_OK);
    again.dev->SetTexture(0,t2);t2->Release();again.dev->Release();CHECK(gDeviceDeletes.load()==deletes+2);checkClean();
}
static void queryProbeAndDeadQuery(){
    gTrace.clear();Rig rig(true);
    CHECK(rig.dev->CreateQuery((D3DQUERYTYPE)9,nullptr)==D3D_OK&&rig.dev->CreateQuery((D3DQUERYTYPE)77,nullptr)==D3DERR_NOTAVAILABLE);   // the Target's own answer
    gKnobs.failQueries.store(true);IDirect3DQuery9* q=nullptr;CHECK(rig.dev->CreateQuery((D3DQUERYTYPE)9,&q)==D3D_OK&&q);   // asynchronous: the proxy is returned, the real create fails
    rig.sync();DWORD v=0;CHECK(q->Issue(D3::kIssueEnd)==D3D_OK);CHECK(q->GetData(&v,4,0)==D3DERR_INVALIDCALL&&get(rig.core().q.stats.deadCreates)>=1);
    q->Release();gKnobs.failQueries.store(false);rig.finish();checkClean();
}
// An init failure after the replay thread ran the owner callback: the implicit objects bound so far are released, no proxy or
// Target object leaks, the callback runs again on this thread, and the caller keeps its device.
static void initFailureFallback(){
    gTrace.clear();gKnobs.failSwapChain.store(true);auto* t=new TargetDevice;FakeD3D parent;
    std::vector<std::thread::id> ids;StreamDevice::Options opt;opt.threadStart=[&]{ids.push_back(std::this_thread::get_id());};
    D3DPRESENT_PARAMETERS pp{};pp.BackBufferWidth=640;pp.BackBufferHeight=480;pp.BackBufferFormat=(D3DFORMAT)22;pp.BackBufferCount=2;const char* reason=nullptr;
    StreamDevice* d=StreamDevice::make(t,&parent,&pp,std::move(opt),&reason);
    CHECK(!d&&reason&&std::string(reason)=="init");
    CHECK(ids.size()==2&&ids[1]==std::this_thread::get_id()&&ids[0]!=ids[1]);
    CHECK(t->refs.load()==1&&liveProxyObjects.load()==0);   // the device is the caller's alone again; the default render target and depth buffer proxies are gone
    gKnobs.failSwapChain.store(false);t->Release();checkClean();
}
static void nestedSyncInPump(){
    gTrace.clear();Rig rig(true);auto& s=rig.core().q.stats;
    static Rig* r;static HRESULT nested;static int calls;r=&rig;nested=12345;calls=0;
    gKnobs.hold.store(true);rig.dev->BeginScene();   // the replay thread is busy: the next sync really waits
    pumpHook=[]{if(calls++==0){nested=r->dev->TestCooperativeLevel();gKnobs.hold.store(false);}};
    CHECK(rig.dev->TestCooperativeLevel()==5);pumpHook=nullptr;
    CHECK(nested==D3DERR_INVALIDCALL&&get(s.nestedSyncs)==1);
    rig.finish();
}

static void upDrawsAndBackpressure(){
    gTrace.clear();StreamDevice::Options opt;opt.budget=2u<<20;Rig rig(true,opt);auto& s=rig.core().q.stats;
    std::vector<unsigned char> data(4000);for(std::size_t i=0;i<data.size();++i)data[i]=(unsigned char)(i*7);
    CHECK(rig.dev->DrawPrimitiveUP((D3DPRIMITIVETYPE)4,10,data.data(),20)==D3D_OK);rig.sync();   // copied: the replay sees the original bytes, keyed by the game's pointer
    CHECK(rig.target->lastUpData!=data.data()&&rig.target->lastUpIdentity==data.data());
    std::memset(data.data(),0xEE,data.size());
    // an UP draw above the inline limit travels in a Block; above the budget it passes through with the game's own pointer
    std::vector<unsigned char> big(600u<<10,3),huge(5u<<19,4);
    CHECK(rig.dev->DrawPrimitiveUP((D3DPRIMITIVETYPE)4,100,big.data(),2000)==D3D_OK);rig.sync();CHECK(rig.target->lastUpData!=big.data()&&rig.target->lastUpIdentity==big.data());
    CHECK(rig.dev->DrawPrimitiveUP((D3DPRIMITIVETYPE)4,240,huge.data(),3500)==D3D_OK);rig.sync();CHECK(rig.target->lastUpData==huge.data()&&rig.target->lastUpIdentity==huge.data());
    CHECK(get(s.passThrough[unsigned(PassReason::Budget)])==1&&get(s.blockRefused)>=1);
    // the producer blocks at the budget until the replay thread retires
    gKnobs.hold.store(true);rig.dev->BeginScene();std::atomic<bool> done{false};
    std::thread game([&]{std::vector<unsigned char> chunk(200000,1);for(int i=0;i<40;++i)rig.dev->DrawPrimitiveUP((D3DPRIMITIVETYPE)4,600,chunk.data(),100);done.store(true);});
    CHECK(waitFor([&]{return get(s.backpressureWaits)>=1;}));std::this_thread::sleep_for(std::chrono::milliseconds(30));CHECK(!done.load());
    gKnobs.hold.store(false);game.join();CHECK(done.load()&&get(s.highWaterBytes)<=2u<<20);rig.sync();
    unsigned draws=0;for(auto& t:gTrace)if(t.rfind("Device::DrawPrimitiveUP",0)==0)++draws;CHECK(draws>=43);
    rig.finish();CHECK(gLiveTargets.load()==0);
}

static std::atomic<int> gCaptures{0};
static bool fakeCapture(GameSnapshot& s,Trigger,std::uint64_t draw){const unsigned v=7;s.record(0x1000,&v,4,true);s.triggerDraw=draw;gCaptures.fetch_add(1);return true;}
static void snapshotTriggers(){
    gTrace.clear();gCaptures=0;StreamDevice::Options opt;opt.capture=&fakeCapture;Rig rig(true,opt);
    for(int frame=0;frame<3;++frame){
        for(int i=0;i<12;++i)rig.dev->DrawPrimitive((D3DPRIMITIVETYPE)4,0,2);   // untagged draws: only the first of the frame takes a snapshot
        rig.dev->Present(nullptr,nullptr,nullptr,nullptr);
    }
    rig.sync();CHECK(gCaptures.load()==3);   // one FrameStart capture per frame, however many draws
    auto& rp=rig.sd->replayerOf();CHECK(rp.snapshots.allocated()>=1);
    rig.finish();   // every snapshot went back to the pool
}

// ---- the equivalence run ----
struct Game {
    IDirect3DDevice9* dev;std::uint64_t rng;std::vector<std::string>& out;std::vector<HRESULT>& presents;
    std::vector<IDirect3DTexture9*> tex;std::vector<IDirect3DVertexBuffer9*> vb;std::vector<IDirect3DIndexBuffer9*> ib;std::vector<IDirect3DSurface9*> surf;
    std::vector<IDirect3DVertexShader9*> vs;std::vector<IDirect3DPixelShader9*> ps;std::vector<IDirect3DVertexDeclaration9*> decl;std::vector<IDirect3DQuery9*> queries;
    std::vector<IDirect3DStateBlock9*> blocks;std::vector<std::pair<IDirect3DSurface9*,IDirect3DTexture9*>> levels;
    std::unordered_map<const void*,int> ids;int nextId=1;
    Game(IDirect3DDevice9* d,std::uint64_t seed,std::vector<std::string>& o,std::vector<HRESULT>& p):dev(d),rng(seed),out(o),presents(p){}
    unsigned r(unsigned n){rng=mix(rng);return unsigned((rng>>11)%n);}
    template<class T> int reg(T* p){ids[p]=nextId;return nextId++;}
    int idOf(const void* p){auto i=ids.find(p);return i==ids.end()?(p?-1:0):i->second;}
    void log(const std::string& s){out.push_back(s);}
    static std::string hx(const unsigned char* p,std::size_t n){return fb(p,n);}
    template<class T> T* any(std::vector<T*>& v){return v.empty()?nullptr:v[r(unsigned(v.size()))];}
    void dropOne(){
        switch(r(7)){
        // once the game let go of an object its pointer means nothing: the device may still hold it, and a Get then hands out a pointer the game never saw
        case 0:if(!tex.empty()){auto i=r(unsigned(tex.size()));for(auto it=levels.begin();it!=levels.end();){if(it->second==tex[i]){ids.erase(it->first);it->first->Release();it->second->Release();it=levels.erase(it);}else ++it;}ids.erase(tex[i]);tex[i]->Release();tex.erase(tex.begin()+i);}break;
        case 1:if(!vb.empty()){auto i=r(unsigned(vb.size()));ids.erase(vb[i]);vb[i]->Release();vb.erase(vb.begin()+i);}break;
        case 2:if(!ib.empty()){auto i=r(unsigned(ib.size()));ids.erase(ib[i]);ib[i]->Release();ib.erase(ib.begin()+i);}break;
        case 3:if(!surf.empty()){auto i=r(unsigned(surf.size()));ids.erase(surf[i]);surf[i]->Release();surf.erase(surf.begin()+i);}break;
        case 4:if(!vs.empty()){auto i=r(unsigned(vs.size()));ids.erase(vs[i]);vs[i]->Release();vs.erase(vs.begin()+i);}break;
        case 5:if(!ps.empty()){auto i=r(unsigned(ps.size()));ids.erase(ps[i]);ps[i]->Release();ps.erase(ps.begin()+i);}break;
        default:if(!queries.empty()){auto i=r(unsigned(queries.size()));queries[i]->Release();queries.erase(queries.begin()+i);}break;
        }
    }
    void step(int n){
        if(n%40==39)presents.push_back(dev->Present(nullptr,nullptr,nullptr,nullptr));
        switch(r(34)){
        case 0:{const unsigned w=1u<<(2+r(5)),h=1u<<(2+r(5));static const unsigned fm[]={21,22,23,0x31545844,0x35545844,28};
            const unsigned f=fm[r(6)];const DWORD usage=r(4)==0?D3::kUsageDynamic:0;IDirect3DTexture9* t=nullptr;
            if(dev->CreateTexture(w,h,r(3)==0?0:1+r(4),usage,(D3DFORMAT)f,(D3DPOOL)(1+r(2)),&t,nullptr)==D3D_OK&&t&&tex.size()<24){reg(t);tex.push_back(t);
                D3DSURFACE_DESC d{};t->GetLevelDesc(0,&d);log("tex "+std::to_string(t->GetLevelCount())+" "+std::to_string(d.Width)+"x"+std::to_string(d.Height)+" "+std::to_string(d.Usage));}
            else if(t)t->Release();break;}
        case 1:{IDirect3DVertexBuffer9* b=nullptr;const UINT len=64+r(4000);if(dev->CreateVertexBuffer(len,r(3)==0?D3::kUsageDynamic:0,r(2)?0x112:0,(D3DPOOL)(r(2)?0:1),&b,nullptr)==D3D_OK&&b&&vb.size()<16){reg(b);vb.push_back(b);
                D3DVERTEXBUFFER_DESC d{};b->GetDesc(&d);log("vb "+std::to_string(d.Size)+" "+std::to_string(d.FVF));}else if(b)b->Release();break;}
        case 2:{IDirect3DIndexBuffer9* b=nullptr;if(dev->CreateIndexBuffer(64+r(2000),0,(D3DFORMAT)(r(2)?101:102),(D3DPOOL)1,&b,nullptr)==D3D_OK&&b&&ib.size()<12){reg(b);ib.push_back(b);}else if(b)b->Release();break;}
        case 3:{IDirect3DSurface9* s=nullptr;HRESULT hr=r(2)?dev->CreateOffscreenPlainSurface(8+r(30),8+r(30),(D3DFORMAT)22,(D3DPOOL)2,&s,nullptr):dev->CreateRenderTarget(16,16,(D3DFORMAT)22,(D3DMULTISAMPLE_TYPE)0,0,0,&s,nullptr);
            if(hr==D3D_OK&&s&&surf.size()<12){reg(s);surf.push_back(s);D3DSURFACE_DESC d{};s->GetDesc(&d);log("surf "+std::to_string(d.Width)+" "+std::to_string(d.Usage));}else if(s)s->Release();break;}
        case 4:{DWORD code[]={0xFFFE0200u+r(2),0x02000001u,r(1000),r(1000),0x00000001u,0x0000FFFFu};
            if(r(2)){IDirect3DVertexShader9* v=nullptr;if(dev->CreateVertexShader(code,&v)==D3D_OK&&v&&vs.size()<8){reg(v);vs.push_back(v);UINT n2=0;v->GetFunction(nullptr,&n2);DWORD back[8]={};v->GetFunction(back,&n2);log("vs "+std::to_string(n2)+" "+hx((unsigned char*)back,n2));}else if(v)v->Release();}
            else{IDirect3DPixelShader9* p=nullptr;if(dev->CreatePixelShader(code,&p)==D3D_OK&&p&&ps.size()<8){reg(p);ps.push_back(p);}else if(p)p->Release();}break;}
        case 5:{D3DVERTEXELEMENT9 el[3]={{0,0,2,0,0,0},{0,12,1,0,5,0},{0xFF,0,17,0,0,0}};IDirect3DVertexDeclaration9* d=nullptr;
            if(dev->CreateVertexDeclaration(el,&d)==D3D_OK&&d&&decl.size()<6){reg(d);decl.push_back(d);D3DVERTEXELEMENT9 back[4]={};UINT c=0;d->GetDeclaration(back,&c);log("decl "+std::to_string(c));}else if(d)d->Release();break;}
        case 6:dev->SetRenderState((D3DRENDERSTATETYPE)(1+r(250)),r(100000));break;
        case 7:dev->SetSamplerState(r(2)?r(16):257+r(4),(D3DSAMPLERSTATETYPE)(1+r(13)),r(100));break;
        case 8:dev->SetTextureStageState(r(8),(D3DTEXTURESTAGESTATETYPE)(1+r(32)),r(100));break;
        case 9:{DWORD s=r(250)+1;DWORD v=0;HRESULT hr=dev->GetRenderState((D3DRENDERSTATETYPE)s,&v);log("rs "+std::to_string(hr)+" "+std::to_string(v));
            DWORD sv=0;hr=dev->GetSamplerState(r(16),(D3DSAMPLERSTATETYPE)(1+r(13)),&sv);log("ss "+std::to_string(hr)+" "+std::to_string(sv));
            hr=dev->GetTextureStageState(r(8),(D3DTEXTURESTAGESTATETYPE)(1+r(32)),&sv);log("tss "+std::to_string(hr)+" "+std::to_string(sv));break;}
        case 10:{D3DMATRIX m{};for(int i=0;i<16;++i)m.m[i]=float(r(1000));dev->SetTransform((D3DTRANSFORMSTATETYPE)(r(2)?2:3),&m);D3DMATRIX g{};dev->GetTransform((D3DTRANSFORMSTATETYPE)(r(2)?2:3),&g);log("xf "+hx((unsigned char*)&g,64));break;}
        case 11:{D3DVIEWPORT9 v{};v.X=r(10);v.Y=r(10);v.Width=64+r(500);v.Height=64+r(400);v.MaxZ=1;dev->SetViewport(&v);D3DVIEWPORT9 g{};dev->GetViewport(&g);log("vp "+hx((unsigned char*)&g,sizeof g));
            RECT sc{LONG(r(5)),LONG(r(5)),LONG(50+r(100)),LONG(50+r(100))};dev->SetScissorRect(&sc);RECT g2{};dev->GetScissorRect(&g2);log("sc "+hx((unsigned char*)&g2,sizeof g2));break;}
        case 12:{float c[16];for(float& x:c)x=float(r(500));const UINT reg0=r(200),cnt=1+r(4);dev->SetVertexShaderConstantF(reg0,c,cnt);float g[16]={};HRESULT hr=dev->GetVertexShaderConstantF(reg0+r(cnt),g,1);log("vsf "+std::to_string(hr)+" "+hx((unsigned char*)g,16));break;}
        case 13:{if(auto* t=any(tex)){const DWORD st=r(2)?r(16):257+r(4);dev->SetTexture(st,t);IDirect3DBaseTexture9* g=nullptr;dev->GetTexture(st,&g);log("gtex "+std::to_string(idOf(g)));if(g)g->Release();}break;}
        case 14:{if(auto* b=any(vb)){const UINT i=r(4);dev->SetStreamSource(i,b,r(8),20);IDirect3DVertexBuffer9* g=nullptr;UINT o=0,st=0;dev->GetStreamSource(i,&g,&o,&st);log("gsv "+std::to_string(idOf(g)));if(g)g->Release();}
            if(auto* b=any(ib)){dev->SetIndices(b);IDirect3DIndexBuffer9* g=nullptr;dev->GetIndices(&g);log("gib "+std::to_string(idOf(g)));if(g)g->Release();}break;}
        case 15:{if(auto* v=any(vs)){dev->SetVertexShader(v);IDirect3DVertexShader9* g=nullptr;dev->GetVertexShader(&g);log("gvs "+std::to_string(idOf(g)));if(g)g->Release();}
            if(auto* p=any(ps)){dev->SetPixelShader(p);IDirect3DPixelShader9* g=nullptr;dev->GetPixelShader(&g);log("gps "+std::to_string(idOf(g)));if(g)g->Release();}
            if(auto* d=any(decl)){dev->SetVertexDeclaration(d);}else dev->SetFVF(0x112);DWORD f=0;dev->GetFVF(&f);log("fvf "+std::to_string(f));break;}
        case 16:{if(auto* b=any(vb)){D3DVERTEXBUFFER_DESC d{};b->GetDesc(&d);const UINT off=r(d.Size/2),size=r(3)==0?0:1+r(d.Size-off-1>0?d.Size-off-1:1);
                static const DWORD fl[]={0,D3::kLockDiscard,D3::kLockNoOverwrite,D3::kLockReadOnly};const DWORD flags=fl[r(4)];void* p=nullptr;HRESULT hr=b->Lock(off,size,&p,flags);log("vlock "+std::to_string(hr));
                if(hr==D3D_OK){const UINT n2=size?size:d.Size-off;if(flags==D3::kLockReadOnly)log("vread "+hx((unsigned char*)p,n2));else for(UINT i=0;i<n2;++i)static_cast<unsigned char*>(p)[i]=(unsigned char)r(256);b->Unlock();}}break;}
        case 17:{if(auto* b=any(ib)){D3DINDEXBUFFER_DESC d{};b->GetDesc(&d);void* p=nullptr;const DWORD flags=r(2)?0:D3::kLockDiscard;if(b->Lock(0,0,&p,flags)==D3D_OK){for(UINT i=0;i<d.Size;++i)static_cast<unsigned char*>(p)[i]=(unsigned char)r(256);b->Unlock();}}break;}
        case 18:case 19:{if(auto* t=any(tex)){D3DSURFACE_DESC d{};const UINT lvl=r(t->GetLevelCount());t->GetLevelDesc(lvl,&d);const bool viaSurface=r(3)==0;
                static const DWORD fl[]={0,0,D3::kLockDiscard,D3::kLockReadOnly};const DWORD flags=fl[r(4)];RECT rc{0,0,LONG(d.Width),LONG(d.Height)};const bool useRect=r(2);
                if(useRect&&d.Width>=8&&d.Height>=8){rc.left=LONG(r(d.Width/2));rc.top=LONG(r(d.Height/2));rc.right=rc.left+1+LONG(r(d.Width-rc.left-1));rc.bottom=rc.top+1+LONG(r(d.Height-rc.top-1));}
                D3DLOCKED_RECT lr{};IDirect3DSurface9* sf=nullptr;HRESULT hr;
                if(viaSurface){t->GetSurfaceLevel(lvl,&sf);hr=sf->LockRect(&lr,useRect?&rc:nullptr,flags);}else hr=t->LockRect(lvl,&lr,useRect?&rc:nullptr,flags);
                log("tlock "+std::to_string(hr)+" "+std::to_string(viaSurface));
                if(hr==D3D_OK){
                    const bool fourcc=d.Format==(D3DFORMAT)0x31545844||d.Format==(D3DFORMAT)0x35545844;const unsigned bpp=d.Format==(D3DFORMAT)21||d.Format==(D3DFORMAT)22?4:(d.Format==(D3DFORMAT)23?2:1);
                    const unsigned rows=fourcc?(rc.bottom-rc.top+3)/4:rc.bottom-rc.top,rowBytes=fourcc?((rc.right-rc.left+3)/4)*(d.Format==(D3DFORMAT)0x31545844?8:16):(rc.right-rc.left)*bpp;
                    for(unsigned y=0;y<rows;++y){auto* row=static_cast<unsigned char*>(lr.pBits)+std::size_t(y)*lr.Pitch;
                        if(flags==D3::kLockReadOnly)log("tread "+hx(row,rowBytes));else for(unsigned x=0;x<rowBytes;++x)row[x]=(unsigned char)r(256);}
                    if(viaSurface)sf->UnlockRect();else t->UnlockRect(lvl);}
                if(sf)sf->Release();}break;}
        case 20:{if(auto* t=any(tex)){IDirect3DSurface9* sf=nullptr;if(t->GetSurfaceLevel(r(t->GetLevelCount()),&sf)==D3D_OK){if(levels.size()<8){reg(sf);levels.push_back({sf,t});t->AddRef();}else sf->Release();}}break;}
        case 21:dev->DrawPrimitive((D3DPRIMITIVETYPE)(1+r(6)),r(100),1+r(100));break;
        case 22:dev->DrawIndexedPrimitive((D3DPRIMITIVETYPE)4,r(10),r(10),1+r(50),r(100),1+r(50));break;
        case 23:{unsigned char v[20*30];for(auto& x:v)x=(unsigned char)r(256);const UINT n3=1+r(8);dev->DrawPrimitiveUP((D3DPRIMITIVETYPE)4,n3,v,20);
            unsigned short idx[64];for(auto& x:idx)x=(unsigned short)r(20);dev->DrawIndexedPrimitiveUP((D3DPRIMITIVETYPE)4,0,20,1+r(10),idx,(D3DFORMAT)101,v,20);break;}
        case 24:dev->Clear(0,nullptr,7,r(1000),0.5f,0);{D3DRECT rc[2]={{0,0,10,10},{5,5,20,20}};dev->Clear(1+r(2),rc,1,r(100),1.f,0);}break;
        case 25:{if(auto* s=any(surf)){dev->SetRenderTarget(r(2)?0:1,s);IDirect3DSurface9* g=nullptr;dev->GetRenderTarget(0,&g);log("grt "+std::to_string(idOf(g)));if(g)g->Release();}break;}
        case 26:{if(auto* a=any(surf))if(auto* b=any(surf)){RECT rc{0,0,8,8};dev->StretchRect(a,&rc,b,nullptr,(D3DTEXTUREFILTERTYPE)1);dev->ColorFill(b,&rc,r(1000));}break;}
        case 27:{IDirect3DQuery9* q=nullptr;if(r(3)==0&&queries.size()<4&&dev->CreateQuery((D3DQUERYTYPE)9,&q)==D3D_OK)queries.push_back(q);
            if(auto* qq=any(queries)){qq->Issue(D3::kIssueEnd);DWORD v=0;HRESULT hr;int spins=0;while((hr=qq->GetData(&v,4,0))==S_FALSE&&++spins<100000)std::this_thread::yield();log("q "+std::to_string(hr)+" "+std::to_string(v));}break;}
        case 28:{if(r(4)==0&&blocks.size()<4){if(r(2)){IDirect3DStateBlock9* sb=nullptr;if(dev->CreateStateBlock((D3DSTATEBLOCKTYPE)1,&sb)==D3D_OK)blocks.push_back(sb);}
                else{dev->BeginStateBlock();dev->SetRenderState((D3DRENDERSTATETYPE)(1+r(250)),r(100));IDirect3DStateBlock9* sb=nullptr;if(dev->EndStateBlock(&sb)==D3D_OK)blocks.push_back(sb);}}
            else if(!blocks.empty()){auto* sb=blocks[r(unsigned(blocks.size()))];if(r(2))sb->Apply();else sb->Capture();DWORD v=0;dev->GetRenderState((D3DRENDERSTATETYPE)(1+r(250)),&v);log("ap "+std::to_string(v));}break;}
        case 29:{log("tcl "+std::to_string(dev->TestCooperativeLevel()));DWORD np=0;log("val "+std::to_string(dev->ValidateDevice(&np)));break;}
        case 30:{IDirect3DSurface9* g=nullptr;dev->GetDepthStencilSurface(&g);D3DSURFACE_DESC d{};if(g){g->GetDesc(&d);g->Release();}log("ds "+std::to_string(d.Width));dev->GetRenderTarget(0,&g);if(g){g->GetDesc(&d);log("rt0 "+std::to_string(d.Width)+" "+std::to_string(idOf(g)));g->Release();}break;}
        case 31:dev->SetViewport(nullptr);break;
        case 32:{if(r(4)==0)dropOne();break;}
        default:break;
        }
    }
    void releaseAll(){
        for(auto& l:levels){ids.erase(l.first);l.first->Release();l.second->Release();}levels.clear();
        ids.clear();
        for(auto* x:tex)x->Release();for(auto* x:vb)x->Release();for(auto* x:ib)x->Release();for(auto* x:surf)x->Release();for(auto* x:vs)x->Release();for(auto* x:ps)x->Release();
        for(auto* x:decl)x->Release();for(auto* x:queries)x->Release();for(auto* x:blocks)x->Release();
        tex.clear();vb.clear();ib.clear();surf.clear();vs.clear();ps.clear();decl.clear();queries.clear();blocks.clear();
    }
};
static void equivalence(int steps,std::uint64_t seed){
    std::vector<std::string> outA,outB,traceA,traceB;std::vector<HRESULT> presA,presB;
    for(int mode=0;mode<2;++mode){
        gTrace.clear();gKnobs.presents.store(0);gNormalize=true;gPtrIds.clear();gNextPtrId=0;Rig rig(mode==1);auto& out=mode?outB:outA;auto& pres=mode?presB:presA;
        Game g(rig.dev,seed,out,pres);
        for(int i=0;i<steps;++i){
            g.step(i);
            if(i==steps/2){   // a Reset in the middle: the game released nothing the Target holds
                g.releaseAll();D3DPRESENT_PARAMETERS pp{};pp.BackBufferWidth=700;pp.BackBufferHeight=500;pp.BackBufferFormat=(D3DFORMAT)22;pp.BackBufferCount=2;
                out.push_back("reset "+std::to_string(rig.dev->Reset(&pp)));
            }
        }
        g.releaseAll();rig.sync();
        (mode?traceB:traceA)=filtered(gTrace);
        rig.finish();checkClean();gNormalize=false;
    }
    CHECK(outA.size()==outB.size());for(std::size_t i=0;i<outA.size();++i)if(outA[i]!=outB[i]){std::fprintf(stderr,"result %zu differs:\n direct: %s\n stream: %s\n",i,outA[i].c_str(),outB[i].c_str());std::abort();}
    {std::size_t i=0;while(i<traceA.size()&&i<traceB.size()&&traceA[i]==traceB[i])++i;
     if(i<traceA.size()||i<traceB.size()){std::fprintf(stderr,"trace differs at %zu (sizes %zu vs %zu)\n direct: %s\n stream: %s\n",i,traceA.size(),traceB.size(),i<traceA.size()?traceA[i].substr(0,300).c_str():"-",i<traceB.size()?traceB[i].substr(0,300).c_str():"-");std::abort();}}
    for(std::size_t i=0;i<traceA.size();++i)if(traceA[i]!=traceB[i]){std::fprintf(stderr,"trace %zu differs:\n direct: %s\n stream: %s\n",i,traceA[i].substr(0,300).c_str(),traceB[i].substr(0,300).c_str());std::abort();}
    CHECK(presA.size()==presB.size()&&!presA.empty());CHECK(presB[0]==D3D_OK);for(std::size_t i=1;i<presB.size();++i)CHECK(presB[i]==presA[i-1]);   // Present returns the previous frame's real result
    std::printf("equivalence seed=%llu steps=%d results=%zu trace=%zu presents=%zu\n",(unsigned long long)seed,steps,outA.size(),traceA.size(),presA.size());
}
static void streamTests(bool threadsOnly){
    lifetimeAndIdentity();stateKnownUnknown();locksPreserveBytes();shadowCap();queriesAndSyncCensus();resetAndShutdown();childrenOutliveTheDevice();queryProbeAndDeadQuery();initFailureFallback();cursorAndForeignThread();nestedSyncInPump();upDrawsAndBackpressure();snapshotTriggers();
    equivalence(20000,12345);equivalence(20000,987654321);
    (void)threadsOnly;
}
