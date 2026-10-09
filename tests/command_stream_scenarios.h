// StreamDevice tests against the fake Target: targeted cases (lifetime, StreamState, locks, queries, Reset, backpressure,
// nested sync, shutdown) and the equivalence run. Included by test_command_stream.cpp after command_stream_targets.h.
#pragma once
#include <unordered_map>
struct FakeD3D:IDirect3D9 {};
static void checkClean(){if(gLiveTargets.load()||liveProxyObjects.load()){std::fprintf(stderr,"leak: targets=%d proxies=%ld\n",gLiveTargets.load(),liveProxyObjects.load());std::abort();}}
struct Rig {
    TargetDevice* target=nullptr;IDirect3DDevice9* dev=nullptr;StreamDevice* sd=nullptr;FakeD3D parent;const char* reason=nullptr;
    // direct: replay the DIRECT methods on the fake extension device with raw pointers (the fake's raw = its exposed object shifted, see unwrapFake)
    explicit Rig(bool streamed,StreamDevice::Options opt={},bool direct=true){
        target=new TargetDevice;
        if(streamed){
            opt.extension=&target->ext;opt.directReplay=direct;
            opt.rawOf=[](IUnknown* e,unsigned){return gKnobs.noRaw.load()?nullptr:unwrapFake(e);};
            D3DPRESENT_PARAMETERS pp{};pp.BackBufferWidth=640;pp.BackBufferHeight=480;pp.BackBufferFormat=(D3DFORMAT)22;pp.BackBufferCount=2;
            sd=StreamDevice::make(target,&parent,&pp,std::move(opt),&reason);CHECK(sd);dev=sd;
        }else dev=target;
    }
    StreamCore& core(){return sd->streamCore();}
    void sync(){DWORD n=0;CHECK(dev->ValidateDevice(&n)==5);}   // 0.3.204 (task 21): a sync call that always drains (TestCooperativeLevel is answered locally while the device is OK)
    // 0.3.196 (task 12): every wait of the real StreamDevice paths (generated runSync, runTask, Present/SwapPresent) targeted a kFlagWaitTarget command.
    void checkFlagged(){if(sd)CHECK(core().q.unflaggedWaits()==0);}
    void finish(){checkFlagged();dev->Release();dev=nullptr;}
};
// With filtering on the Target sees fewer Sets: the filterable ones are dropped from both traces and the STATE digests (logged at every draw,
// clear, copy, present and write unlock) carry the effective device state instead. With it compiled out the traces must be identical.
static std::vector<std::string> filtered(const std::vector<std::string>& t){std::vector<std::string> r;for(auto& s:t){if(isGetName(s))continue;if(kFilterRedundantState&&isFilterableName(s))continue;if(s.size()>3&&s.compare(s.size()-3,3," ro")==0&&s.find("::Unlock ")!=std::string::npos)continue;r.push_back(s);}return r;}   // READONLY locks served from a shadow (or read back once by the stream) are no Target events worth comparing: the bytes the game reads are compared as results

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
    {   // a small static buffer keeps its first write as an evictable shadow (0.3.192 CS): the second lock is served from it, no pass-through
        IDirect3DVertexBuffer9* st=nullptr;CHECK(rig.dev->CreateVertexBuffer(256,0,0,(D3DPOOL)1,&st,nullptr)==D3D_OK);void* q=nullptr;
        const auto pass=get(s.passThrough[unsigned(PassReason::NoShadow)]);
        CHECK(st->Lock(0,0,&q,0)==D3D_OK&&st->Unlock()==D3D_OK&&get(s.passThrough[unsigned(PassReason::NoShadow)])==pass);
        CHECK(st->Lock(0,0,&q,0)==D3D_OK&&st->Unlock()==D3D_OK&&get(s.passThrough[unsigned(PassReason::NoShadow)])==pass);
        st->Release();
    }
    // a static buffer above kMaxStaticShadow has no shadow: the first write lock is asynchronous, later ones pass through
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
    CHECK(dxt->LockRect(0,&lr,nullptr,0)==D3D_OK&&get(s.passThrough[unsigned(PassReason::Written)])==0);CHECK(lr.Pitch==32);CHECK(dxt->UnlockRect(0)==D3D_OK);   // the level's shadow: our pitch, no sync
    CHECK(dxt->LockRect(0,&lr,nullptr,D3::kLockReadOnly)==D3D_OK&&get(s.passThrough[unsigned(PassReason::ReadOnly)])==1);   // (the one above is the big buffer's)
    unsigned char row0[32];std::memcpy(row0,lr.pBits,32);for(int i=0;i<32;++i)CHECK(row0[i]==0x10);CHECK(dxt->UnlockRect(0)==D3D_OK);
    dxt->Release();
    IDirect3DTexture9* unk=nullptr;CHECK(rig.dev->CreateTexture(8,8,1,0,(D3DFORMAT)999,(D3DPOOL)1,&unk,nullptr)==D3D_OK);   // unknown format: pass-through, counted
    CHECK(unk->LockRect(0,&lr,nullptr,0)==D3D_OK&&get(s.passThrough[unsigned(PassReason::Format)])==1);CHECK(unk->UnlockRect(0)==D3D_OK);unk->Release();
    rig.finish();checkClean();
}

// 0.3.192 (CS): re-locked NON-DYNAMIC buffers. There is no fresh keep: the first write (and any DISCARD) lock is staged as before; a written buffer is read back
// ONCE (whole buffer, READONLY|NOOVERWRITE through Options::readBackLock) at its first write re-lock; every later lock is served from the shadow and
// its Unlock records only the locked range. These shadows share the buffer cap and ONE LRU with DYNAMIC ones; a GPU write (ProcessVertices) drops them for good.
static unsigned char* targetBytes(IDirect3DVertexBuffer9* b){return static_cast<TVertexBuffer*>(static_cast<IDirect3DVertexBuffer9*>(ProxyBase::of(b)->inner))->mem.data();}
static bool lastReadBackUnlock(const char* tail){for(std::size_t i=gTrace.size();i-->0;)if(gTrace[i].rfind("VB::Unlock ",0)==0&&gTrace[i].size()>=3&&gTrace[i].compare(gTrace[i].size()-3,3," ro")==0)return gTrace[i].find(tail)!=std::string::npos;return false;}
static DWORD dxvk3ReadBack(){return D3::kLockReadOnly|D3::kLockNoOverwrite;}
static void frames(IDirect3DDevice9* d,unsigned n){for(unsigned i=0;i<n;++i)d->Present(nullptr,nullptr,nullptr,nullptr);}
static bool shadowOn(IDirect3DVertexBuffer9* b){return static_cast<StreamVertexBuffer*>(ProxyBase::of(b))->buf.shadowOn;}
static void staticBufferShadows(){
    gTrace.clear();StreamDevice::Options opt;opt.readBackLock=&dxvk3ReadBack;
    Rig rig(true,opt);auto& q=rig.core().q;auto& s=q.stats;IDirect3DDevice9* d=rig.dev;
    const auto noshadow=[&]{return get(s.passThrough[unsigned(PassReason::NoShadow)]);};
    {   // N re-locks of a written 1 MiB MANAGED buffer: exactly one synchronous readback, then none; partial writes keep the untouched bytes
        IDirect3DVertexBuffer9* vb=nullptr;CHECK(d->CreateVertexBuffer(1u<<20,0,0,(D3DPOOL)1,&vb,nullptr)==D3D_OK);void* p=nullptr;
        CHECK(vb->Lock(100,4096,&p,0)==D3D_OK);std::memset(p,0xA5,4096);CHECK(vb->Unlock()==D3D_OK);   // first write: staged, no shadow
        auto& st=static_cast<StreamVertexBuffer*>(ProxyBase::of(vb))->buf;CHECK(!st.shadowOn);
        rig.sync();const auto sync0=get(s.syncCalls),pass0=noshadow();
        for(int i=0;i<6;++i){
            CHECK(vb->Lock(8192+i*64,64,&p,0)==D3D_OK);std::memset(p,0xB0+i,64);CHECK(vb->Unlock()==D3D_OK);
            if(i==0)CHECK(get(s.syncCalls)==sync0+1&&get(s.stShadowReadbacks)==1&&st.shadowOn);   // the one readback
        }
        CHECK(get(s.syncCalls)==sync0+1&&get(s.stShadowReadbacks)==1&&get(s.dynShadowReadbacks)==0&&noshadow()==pass0);   // then zero syncs
        CHECK(lastReadBackUnlock(" 1048576 4096 ro"));   // whole buffer, READONLY|NOOVERWRITE
        CHECK(vb->Lock(0,0,&p,D3::kLockReadOnly)==D3D_OK);   // READONLY: served from the shadow, no sync
        {const unsigned char* c=(const unsigned char*)p;for(unsigned i=0;i<(1u<<20);++i){unsigned char want=0;if(i>=100&&i<4196)want=0xA5;else if(i>=8192&&i<8192+6*64)want=(unsigned char)(0xB0+(i-8192)/64);CHECK(c[i]==want);}}
        CHECK(vb->Unlock()==D3D_OK&&get(s.syncCalls)==sync0+1&&get(s.passThrough[unsigned(PassReason::ReadOnly)])==0);
        rig.sync();   // the Target holds exactly what the game saw
        {const unsigned char* c=targetBytes(vb);for(unsigned i=0;i<(1u<<20);++i){unsigned char want=0;if(i>=100&&i<4196)want=0xA5;else if(i>=8192&&i<8192+6*64)want=(unsigned char)(0xB0+(i-8192)/64);CHECK(c[i]==want);}}
        // a GPU write: the shadow goes for good, the buffer is real memory again
        CHECK(d->ProcessVertices(0,0,1,vb,nullptr,0)==D3D_OK);CHECK(!st.shadowOn&&st.shadowDead&&s.shadowBytes.load()==0);
        const auto rb=get(s.stShadowReadbacks);CHECK(vb->Lock(0,16,&p,0)==D3D_OK&&noshadow()==pass0+1&&((unsigned char*)p)[100]==0xA5);CHECK(vb->Unlock()==D3D_OK);
        CHECK(get(s.stShadowReadbacks)==rb);
        vb->Release();rig.sync();
    }
    {   // a SMALL non-DYNAMIC buffer no longer keeps its first write: that lock is staged, the next write lock reads it back; an index buffer too
        IDirect3DVertexBuffer9* vb=nullptr;CHECK(d->CreateVertexBuffer(1000,0,0,(D3DPOOL)0,&vb,nullptr)==D3D_OK);void* p=nullptr;
        const auto sync0=get(s.syncCalls),rb=get(s.stShadowReadbacks);
        CHECK(vb->Lock(10,20,&p,0)==D3D_OK);std::memset(p,0x61,20);CHECK(vb->Unlock()==D3D_OK);CHECK(!shadowOn(vb)&&get(s.syncCalls)==sync0&&s.shadowBytes.load()==0);
        CHECK(vb->Lock(30,10,&p,D3::kLockNoOverwrite)==D3D_OK&&get(s.stShadowReadbacks)==rb+1&&shadowOn(vb));std::memset(p,0x62,10);CHECK(vb->Unlock()==D3D_OK);
        CHECK(vb->Lock(0,0,&p,D3::kLockReadOnly)==D3D_OK);CHECK(((unsigned char*)p)[9]==0&&((unsigned char*)p)[10]==0x61&&((unsigned char*)p)[29]==0x61&&((unsigned char*)p)[30]==0x62&&((unsigned char*)p)[40]==0);CHECK(vb->Unlock()==D3D_OK);
        CHECK(get(s.syncCalls)==sync0+1&&get(s.stShadowReadbacks)==rb+1);
        rig.sync();{const unsigned char* c=targetBytes(vb);CHECK(c[9]==0&&c[10]==0x61&&c[29]==0x61&&c[30]==0x62&&c[40]==0);}
        vb->Release();
        IDirect3DIndexBuffer9* ib=nullptr;CHECK(d->CreateIndexBuffer(2u<<20,0,(D3DFORMAT)101,(D3DPOOL)0,&ib,nullptr)==D3D_OK);
        CHECK(ib->Lock(0,64,&p,0)==D3D_OK&&ib->Unlock()==D3D_OK);const auto r0=get(s.stShadowReadbacks);   // 2 MiB: staged first
        CHECK(ib->Lock(0,64,&p,0)==D3D_OK&&ib->Unlock()==D3D_OK&&get(s.stShadowReadbacks)==r0+1);
        CHECK(ib->Lock(64,64,&p,0)==D3D_OK&&ib->Unlock()==D3D_OK&&get(s.stShadowReadbacks)==r0+1);ib->Release();rig.sync();
        // a DISCARD lock of a non-DYNAMIC buffer never makes a shadow (contents undefined, but only DYNAMIC buffers take a late one)
        IDirect3DVertexBuffer9* nd=nullptr;CHECK(d->CreateVertexBuffer(1000,0,0,(D3DPOOL)0,&nd,nullptr)==D3D_OK);
        CHECK(nd->Lock(0,0,&p,0)==D3D_OK&&nd->Unlock()==D3D_OK&&nd->Lock(0,0,&p,D3::kLockDiscard)==D3D_OK&&nd->Unlock()==D3D_OK&&!shadowOn(nd)&&get(s.shadowLate)==0);nd->Release();rig.sync();
    }
    {   // the cap: 4 MiB shadows share the 16 MiB buffer cap; the LRU one goes, a locked one never; the evicted buffer reads back again; DYNAMIC takes part
        const UINT sz=4u<<20;const int n=int(ShadowBudgetBytes/sz);std::vector<IDirect3DVertexBuffer9*> v;void* p=nullptr;
        auto prime=[&](IDirect3DVertexBuffer9* b,unsigned char fill){CHECK(b->Lock(0,64,&p,0)==D3D_OK);std::memset(p,fill,64);CHECK(b->Unlock()==D3D_OK);CHECK(b->Lock(64,64,&p,0)==D3D_OK);std::memset(p,fill+1,64);CHECK(b->Unlock()==D3D_OK);};   // first staged, second the readback
        auto shadowed=[&](int i){return shadowOn(v[i]);};
        const auto ev0=get(s.stShadowEvicted);
        for(int i=0;i<n;++i){IDirect3DVertexBuffer9* b=nullptr;CHECK(d->CreateVertexBuffer(sz,0,0,(D3DPOOL)1,&b,nullptr)==D3D_OK);v.push_back(b);prime(b,0x10*(i+1));}
        CHECK(s.shadowBytes.load()==std::int64_t(n)*sz&&get(s.stShadowEvicted)==ev0);
        frames(d,1);
        void* held=nullptr;CHECK(v[0]->Lock(0,64,&held,0)==D3D_OK);   // v[0] locked...
        for(int i=1;i<n;++i){CHECK(v[i]->Lock(0,16,&p,0)==D3D_OK&&v[i]->Unlock()==D3D_OK);}   // ...and the oldest: the others are all newer
        frames(d,1);
        {IDirect3DVertexBuffer9* b=nullptr;CHECK(d->CreateVertexBuffer(sz,0,0,(D3DPOOL)1,&b,nullptr)==D3D_OK);v.push_back(b);prime(b,0x70);}
        CHECK(get(s.stShadowEvicted)==ev0+1&&shadowed(0)&&!shadowed(1)&&shadowed(n)&&s.shadowBytes.load()<=std::int64_t(ShadowBudgetBytes));   // v[1] (LRU unlocked) went; the locked v[0] stayed
        std::memset(held,0x7E,64);CHECK(v[0]->Unlock()==D3D_OK);
        frames(d,1);
        const auto rb=get(s.stShadowReadbacks);   // the evicted buffer: readback again, bytes intact
        CHECK(v[1]->Lock(0,0,&p,0)==D3D_OK&&get(s.stShadowReadbacks)==rb+1&&((unsigned char*)p)[0]==0x20&&((unsigned char*)p)[63]==0x20&&((unsigned char*)p)[64]==0x21&&((unsigned char*)p)[127]==0x21&&((unsigned char*)p)[128]==0);CHECK(v[1]->Unlock()==D3D_OK);
        // memory pressure: the cap halves; shadows are evicted down to it, the locked one is never touched
        void* heldAgain=nullptr;CHECK(v[0]->Lock(0,64,&heldAgain,0)==D3D_OK);
        rig.sync();rig.core().memoryPressure.store(true);d->Present(nullptr,nullptr,nullptr,nullptr);rig.sync();
        CHECK(q.pressure()&&s.shadowBytes.load()<=std::int64_t(q.shadowCap())&&shadowed(0)&&((unsigned char*)heldAgain)[0]==0x7E);
        CHECK(v[0]->Unlock()==D3D_OK);CHECK(v[0]->Lock(0,0,&p,D3::kLockReadOnly)==D3D_OK&&((unsigned char*)p)[0]==0x7E&&((unsigned char*)p)[64]==0x11);CHECK(v[0]->Unlock()==D3D_OK);
        rig.core().memoryPressure.store(false);d->Present(nullptr,nullptr,nullptr,nullptr);rig.sync();CHECK(!q.pressure());
        // a new DYNAMIC buffer evicts the LRU shadow of either kind instead of being refused
        for(int i=0;i<=n;++i){if(!shadowed(i)){frames(d,1);CHECK(v[i]->Lock(0,16,&p,0)==D3D_OK&&v[i]->Unlock()==D3D_OK);}}   // refill (readbacks) up to the cap
        frames(d,1);
        const auto ev=get(s.stShadowEvicted),refused=get(s.shadowRefused);IDirect3DVertexBuffer9* dyn=nullptr;
        CHECK(d->CreateVertexBuffer(sz,D3::kUsageDynamic,0,(D3DPOOL)0,&dyn,nullptr)==D3D_OK);
        CHECK(shadowOn(dyn)&&get(s.shadowRefused)==refused&&get(s.stShadowEvicted)>ev&&s.shadowBytes.load()<=std::int64_t(ShadowBudgetBytes));
        dyn->Release();for(auto* b:v)b->Release();rig.sync();CHECK(s.shadowBytes.load()==0);
    }
    rig.finish();checkClean();
    {   // without the DXVK 3 hook the readback is plain READONLY
        gTrace.clear();Rig r2(true);IDirect3DVertexBuffer9* vb=nullptr;void* p=nullptr;
        CHECK(r2.dev->CreateVertexBuffer(1u<<20,0,0,(D3DPOOL)0,&vb,nullptr)==D3D_OK);CHECK(vb->Lock(0,64,&p,0)==D3D_OK&&vb->Unlock()==D3D_OK&&vb->Lock(64,64,&p,0)==D3D_OK&&vb->Unlock()==D3D_OK);
        r2.sync();CHECK(get(r2.core().q.stats.stShadowReadbacks)==1&&lastReadBackUnlock(" 1048576 0 ro"));vb->Release();r2.finish();checkClean();
    }
}

// A model of one buffer's bytes: what the game wrote, to compare with a read through the stream and with the Target's memory.
struct BufModel {
    std::vector<unsigned char> bytes;explicit BufModel(std::size_t n):bytes(n,0){}
    void write(IDirect3DVertexBuffer9* b,UINT off,UINT len,unsigned char fill,DWORD flags=0){void* p=nullptr;CHECK(b->Lock(off,len,&p,flags)==D3D_OK);std::memset(p,fill,len);std::memset(bytes.data()+off,fill,len);CHECK(b->Unlock()==D3D_OK);}
    bool same(const unsigned char* got)const{return std::memcmp(bytes.data(),got,bytes.size())==0;}
};
// 0.3.192 (CS): DYNAMIC buffers without a shadow (refused at creation, or evicted) get one at their first write re-lock by ONE readback; one LRU over all buffer shadows;
// a shadow locked in the current frame is never evicted; a locked one never under pressure; the buffer's bytes stay equal to the Target's throughout.
static void dynamicBufferShadows(){
    gTrace.clear();StreamDevice::Options opt;opt.readBackLock=&dxvk3ReadBack;
    Rig rig(true,opt);auto& q=rig.core().q;auto& s=q.stats;IDirect3DDevice9* d=rig.dev;
    const UINT sz=2u<<20;const int n=int(ShadowBudgetBytes/sz);   // n shadows fill the cap
    std::vector<IDirect3DVertexBuffer9*> v;std::vector<BufModel> m;
    for(int i=0;i<n+2;++i){IDirect3DVertexBuffer9* b=nullptr;CHECK(d->CreateVertexBuffer(sz,D3::kUsageDynamic,0,(D3DPOOL)0,&b,nullptr)==D3D_OK);v.push_back(b);m.emplace_back(sz);}
    CHECK(get(s.shadowRefused)==2&&!shadowOn(v[n])&&!shadowOn(v[n+1]));
    for(int i=0;i<n+2;++i)m[i].write(v[i],0,64,0xA0+i);   // first writes (the refused ones: staged)
    const auto noshadow=[&]{return get(s.passThrough[unsigned(PassReason::NoShadow)]);};
    // same frame, cap full of shadows locked in this frame: the re-lock of v[n] is refused (pass-through, counted), its bytes still right
    {const auto pass0=noshadow(),rf=get(s.relockRefused);m[n].write(v[n],100,50,0xC1);CHECK(!shadowOn(v[n])&&noshadow()==pass0+1&&get(s.relockRefused)==rf+1&&get(s.dynShadowEvicted)==0);}
    for(int i=0;i<n;++i)CHECK(shadowOn(v[i]));   // none of the same-frame shadows was evicted
    frames(d,2);
    // room can be made now: exactly one synchronous readback, then none; the pass-through write made while it had no shadow is in the shadow
    {const auto sync0=get(s.syncCalls),rb=get(s.dynShadowReadbacks);
     m[n].write(v[n],200,32,0xC2);CHECK(shadowOn(v[n])&&get(s.syncCalls)==sync0+1&&get(s.dynShadowReadbacks)==rb+1&&get(s.dynShadowEvicted)==1&&!shadowOn(v[0])&&get(s.stShadowEvicted)==0);   // v[0]: least recently locked
     for(int i=0;i<5;++i)m[n].write(v[n],4096+i*16,16,0xD0+i);
     CHECK(get(s.syncCalls)==sync0+1&&get(s.dynShadowReadbacks)==rb+1);
     void* p=nullptr;CHECK(v[n]->Lock(0,0,&p,D3::kLockReadOnly)==D3D_OK&&m[n].same((unsigned char*)p)&&get(s.syncCalls)==sync0+1);CHECK(v[n]->Unlock()==D3D_OK);
     rig.sync();CHECK(m[n].same(targetBytes(v[n]))&&lastReadBackUnlock(" 2097152 4096 ro"));}
    // LRU: a frequently locked buffer keeps its shadow, the least recently locked one is evicted, and it recovers with the right bytes
    frames(d,1);
    m[1].write(v[1],64,8,0xE1);   // v[1] is now the most recently locked (v[0] has no shadow)
    frames(d,1);
    {const auto ev=get(s.dynShadowEvicted);m[n+1].write(v[n+1],300,20,0xE2);   // re-lock of v[n+1]: needs room, the LRU unlocked one is v[2]
     CHECK(shadowOn(v[n+1])&&shadowOn(v[1])&&!shadowOn(v[2])&&get(s.dynShadowEvicted)==ev+1);}
    // a pass-through write while v[2] has no shadow (every other shadow was locked this frame: no victim), then the recovery
    {frames(d,1);for(int i=0;i<=n+1;++i)if(shadowOn(v[i]))m[i].write(v[i],8192,4,0xF0);   // lock every shadowed buffer now
     const auto pass0=noshadow(),rf=get(s.relockRefused),ev=get(s.dynShadowEvicted);
     m[2].write(v[2],500,10,0xF1);CHECK(!shadowOn(v[2])&&noshadow()==pass0+1&&get(s.relockRefused)==rf+1&&get(s.dynShadowEvicted)==ev);   // refused, nothing evicted
     frames(d,1);const auto rb=get(s.dynShadowReadbacks);
     m[2].write(v[2],600,10,0xF2);CHECK(shadowOn(v[2])&&get(s.dynShadowReadbacks)==rb+1);
     void* p=nullptr;CHECK(v[2]->Lock(0,0,&p,D3::kLockReadOnly)==D3D_OK&&m[2].same((unsigned char*)p));CHECK(v[2]->Unlock()==D3D_OK);
     rig.sync();for(int i=0;i<n+2;++i)CHECK(m[i].same(targetBytes(v[i])));}
    // a DISCARD lock keeps the late path (no readback)
    {frames(d,1);int cold=-1;for(int i=0;i<n+2;++i)if(!shadowOn(v[i])){cold=i;break;}
     CHECK(cold>=0);const auto late=get(s.shadowLate),rb=get(s.dynShadowReadbacks);void* p=nullptr;CHECK(v[cold]->Lock(0,0,&p,D3::kLockDiscard)==D3D_OK);std::memset(p,0x11,sz);std::memset(m[cold].bytes.data(),0x11,sz);CHECK(v[cold]->Unlock()==D3D_OK);
     CHECK(shadowOn(v[cold])&&get(s.shadowLate)==late+1&&get(s.dynShadowReadbacks)==rb);rig.sync();CHECK(m[cold].same(targetBytes(v[cold])));}
    // memory pressure: a locked shadow is never evicted, the rest go LRU down to the halved cap
    {frames(d,1);int held=-1;for(int i=0;i<n+2;++i)if(shadowOn(v[i])){held=i;break;}
     void* hp=nullptr;CHECK(v[held]->Lock(0,64,&hp,0)==D3D_OK);std::memset(hp,0x5A,64);
     rig.sync();rig.core().memoryPressure.store(true);frames(d,1);rig.sync();
     CHECK(q.pressure()&&s.shadowBytes.load()<=std::int64_t(q.shadowCap())&&shadowOn(v[held]));
     CHECK(v[held]->Unlock()==D3D_OK);std::memset(m[held].bytes.data(),0x5A,64);rig.sync();CHECK(m[held].same(targetBytes(v[held])));
     rig.core().memoryPressure.store(false);frames(d,1);rig.sync();CHECK(!q.pressure());}
    for(auto* b:v)b->Release();rig.sync();CHECK(s.shadowBytes.load()==0);
    rig.finish();checkClean();
}
// 0.3.192 (CS): the adaptive buffer-shadow cap. It grows (4 MiB steps, at most one per kShadowGrowFrames Presents, up to 32 MiB) only on a thrash signal
// without memory pressure: evicting a HOT shadow. A cold working set never grows it. Pressure halves it, forgets the growth and never lets it grow.
static void adaptiveShadowCap(){
    gTrace.clear();StreamDevice::Options opt;opt.readBackLock=&dxvk3ReadBack;
    Rig rig(true,opt);auto& q=rig.core().q;auto& s=q.stats;IDirect3DDevice9* d=rig.dev;
    const UINT sz=2u<<20;std::vector<IDirect3DVertexBuffer9*> v;
    for(int i=0;i<20;++i){IDirect3DVertexBuffer9* b=nullptr;CHECK(d->CreateVertexBuffer(sz,D3::kUsageDynamic,0,(D3DPOOL)0,&b,nullptr)==D3D_OK);v.push_back(b);void* p=nullptr;CHECK(b->Lock(0,16,&p,0)==D3D_OK&&b->Unlock()==D3D_OK);}
    CHECK(q.shadowCap()==ShadowBudgetBytes&&get(s.shadowRefused)==12);
    auto request=[&]{for(auto* b:v)if(!shadowOn(b)){void* p=nullptr;CHECK(b->Lock(0,16,&p,0)==D3D_OK&&b->Unlock()==D3D_OK);return;}};   // a write re-lock of a buffer without a shadow
    auto touchAll=[&]{for(auto* b:v)if(shadowOn(b)){void* p=nullptr;CHECK(b->Lock(0,16,&p,0)==D3D_OK&&b->Unlock()==D3D_OK);}};
    // a cold working set: everything idle for far more than kShadowHotFrames; the eviction is not a thrash signal
    frames(d,kShadowHotFrames*2);request();
    CHECK(get(s.dynShadowEvicted)==1&&get(s.hotShadowEvicted)==0&&get(s.shadowCapGrows)==0&&q.shadowCap()==ShadowBudgetBytes&&s.shadowBytes.load()<=std::int64_t(ShadowBudgetBytes));
    // thrash: hot shadows are the only victims; one step per interval, up to the maximum
    std::size_t cap=ShadowBudgetBytes;
    for(int it=0;it<10;++it){
        frames(d,kShadowGrowFrames);touchAll();frames(d,1);
        const auto grows=get(s.shadowCapGrows);
        for(int k=0;k<6;++k)request();   // many requests in one interval: at most ONE step
        const std::size_t want=cap+kShadowGrowStep<=kShadowBudgetMaxBytes?cap+kShadowGrowStep:cap;
        CHECK(q.shadowCap()==want&&get(s.shadowCapGrows)==grows+(want!=cap)&&q.shadowCap()<=kShadowBudgetMaxBytes&&s.shadowBytes.load()<=std::int64_t(q.shadowCap()));
        cap=want;
    }
    CHECK(cap==kShadowBudgetMaxBytes&&get(s.shadowCapGrows)==4&&get(s.hotShadowEvicted)>0);
    // pressure: the cap halves (of the base: the growth is forgotten), and thrash under pressure never grows it
    rig.sync();rig.core().memoryPressure.store(true);frames(d,1);rig.sync();
    CHECK(q.pressure()&&q.shadowCap()==ShadowBudgetBytes/2&&s.shadowBytes.load()<=std::int64_t(q.shadowCap()));
    {const auto grows=get(s.shadowCapGrows);for(int it=0;it<3;++it){frames(d,kShadowGrowFrames);touchAll();frames(d,1);for(int k=0;k<4;++k)request();}
     CHECK(get(s.shadowCapGrows)==grows&&q.shadowCap()==ShadowBudgetBytes/2&&s.shadowBytes.load()<=std::int64_t(q.shadowCap()));}
    // after the pressure the cap is the base again and grows only by the thrash rule
    rig.core().memoryPressure.store(false);frames(d,1);rig.sync();
    CHECK(!q.pressure()&&q.shadowCap()==ShadowBudgetBytes);
    {const auto grows=get(s.shadowCapGrows);frames(d,kShadowGrowFrames);touchAll();frames(d,1);for(int k=0;k<8;++k)request();CHECK(get(s.shadowCapGrows)==grows+1&&q.shadowCap()==ShadowBudgetBytes+kShadowGrowStep);}
    for(auto* b:v)b->Release();rig.sync();CHECK(s.shadowBytes.load()==0);
    rig.finish();checkClean();
}

// 0.3.192 (CS): the LARGE-buffer allowance: a ~15.8 MB DYNAMIC buffer (above a quarter of the regular cap) keeps a shadow in its own large budget (36 MiB since 0.3.204; not in
// shadowBytes): granted at creation when free, so its locks never sync; a second large buffer is refused while the holder is in use and takes it by LRU only
// when the holder is idle (L=20 MB: one fits the 36 MiB budget, two do not); pressure drops it (never while locked: at the unlock) and blocks a re-grant until it ends.
static void largeBufferAllowance(){
    gTrace.clear();StreamDevice::Options opt;opt.readBackLock=&dxvk3ReadBack;
    Rig rig(true,opt);auto& q=rig.core().q;auto& s=q.stats;IDirect3DDevice9* d=rig.dev;
    const UINT L=20000000;static_assert(L<=kMaxLargeShadow&&2*std::size_t(L)>LargeShadowBudgetBytes,"one fits, two do not");
    IDirect3DVertexBuffer9 *A=nullptr,*B=nullptr;
    CHECK(d->CreateVertexBuffer(L,D3::kUsageDynamic,0,(D3DPOOL)0,&A,nullptr)==D3D_OK);
    CHECK(shadowOn(A)&&q.largeBytes()==L&&s.shadowBytes.load()==0&&get(s.largeShadowGrants)==1&&get(s.shadowRefused)==0);
    CHECK(d->CreateVertexBuffer(L,D3::kUsageDynamic,0,(D3DPOOL)0,&B,nullptr)==D3D_OK);
    CHECK(!shadowOn(B)&&q.largeBytes()==L&&get(s.shadowRefused)==1);   // the allowance is taken: no eviction at creation
    BufModel ma(L),mb(L);const auto noshadow=[&]{return get(s.passThrough[unsigned(PassReason::NoShadow)]);};
    rig.sync();const auto sync0=get(s.syncCalls),pass0=noshadow();
    ma.write(A,0,4096,0x11);ma.write(A,5000,300,0x12,D3::kLockNoOverwrite);ma.write(A,9000,64,0x13,D3::kLockDiscard);ma.write(A,70000,64,0x14);
    CHECK(get(s.syncCalls)==sync0&&noshadow()==pass0);   // the whole point: no pass-through syncs
    rig.sync();CHECK(ma.same(targetBytes(A)));
    // B: first write staged, then its re-lock is refused (the holder was locked in this frame) and passes through
    mb.write(B,0,64,0x21);{const auto rf=get(s.relockRefused);mb.write(B,100,64,0x22);CHECK(!shadowOn(B)&&get(s.relockRefused)==rf+1&&q.largeBytes()==L&&shadowOn(A));}
    // the holder idle for kLargeIdleFrames: B takes the allowance by LRU (one readback), A goes
    frames(d,kLargeIdleFrames+1);
    {const auto rb=get(s.dynShadowReadbacks),dr=get(s.largeShadowDrops);mb.write(B,200,64,0x23);
     CHECK(shadowOn(B)&&!shadowOn(A)&&get(s.dynShadowReadbacks)==rb+1&&get(s.largeShadowDrops)==dr+1&&q.largeBytes()==L&&get(s.largeShadowGrants)==2);
     ma.write(A,300,16,0x15);CHECK(!shadowOn(A)&&shadowOn(B));}   // A: B is in use this frame, A passes through (bytes still right)
    rig.sync();CHECK(ma.same(targetBytes(A))&&mb.same(targetBytes(B)));
    // pressure while B is locked: it stays until its unlock, then goes; no re-grant under pressure
    frames(d,1);
    {void* p=nullptr;CHECK(B->Lock(400,32,&p,0)==D3D_OK);std::memset(p,0x31,32);std::memset(mb.bytes.data()+400,0x31,32);
     rig.sync();rig.core().memoryPressure.store(true);frames(d,1);rig.sync();
     CHECK(q.pressure()&&shadowOn(B)&&q.largeBytes()==L);   // locked: kept
     CHECK(B->Unlock()==D3D_OK&&!shadowOn(B)&&q.largeBytes()==0);}   // the unlock under pressure drops it
    rig.sync();CHECK(mb.same(targetBytes(B)));
    {const auto rf=get(s.relockRefused),g=get(s.largeShadowGrants);mb.write(B,500,16,0x32);mb.write(B,600,16,0x33);ma.write(A,700,16,0x16);
     CHECK(!shadowOn(B)&&!shadowOn(A)&&q.largeBytes()==0&&get(s.largeShadowGrants)==g&&get(s.relockRefused)>=rf+2);}
    rig.sync();CHECK(mb.same(targetBytes(B))&&ma.same(targetBytes(A)));
    // pressure over: the next write re-lock grants it again
    rig.core().memoryPressure.store(false);frames(d,1);rig.sync();CHECK(!q.pressure());
    {const auto g=get(s.largeShadowGrants);mb.write(B,800,16,0x34);CHECK(shadowOn(B)&&q.largeBytes()==L&&get(s.largeShadowGrants)==g+1);}
    rig.sync();CHECK(mb.same(targetBytes(B)));
    A->Release();B->Release();rig.sync();CHECK(q.largeBytes()==0&&s.shadowBytes.load()==0);
    rig.finish();checkClean();
}

// 0.3.204: the allowance holds two large buffers (a player's client: the game's ~15.8 MB one plus an 18 MB one): both shadowed, no pass-through; a third that
// does not fit is refused; one above kMaxLargeShadow is never a candidate; pressure drops both (a locked one at its unlock).
static void twoLargeBuffers(){
    gTrace.clear();StreamDevice::Options opt;opt.readBackLock=&dxvk3ReadBack;
    Rig rig(true,opt);auto& q=rig.core().q;auto& s=q.stats;IDirect3DDevice9* d=rig.dev;
    const UINT L1=15800000,L2=18000000,L3=6000000,Big=26000000;
    static_assert(std::size_t(L1)+L2<=LargeShadowBudgetBytes&&std::size_t(L1)+L2+L3>LargeShadowBudgetBytes&&Big>kMaxLargeShadow,"sizes");
    IDirect3DVertexBuffer9 *A=nullptr,*B=nullptr,*C=nullptr,*G=nullptr;
    CHECK(d->CreateVertexBuffer(L1,D3::kUsageDynamic,0,(D3DPOOL)0,&A,nullptr)==D3D_OK&&d->CreateVertexBuffer(L2,D3::kUsageDynamic,0,(D3DPOOL)0,&B,nullptr)==D3D_OK);
    CHECK(shadowOn(A)&&shadowOn(B)&&q.largeBytes()==std::size_t(L1)+L2&&s.shadowBytes.load()==0&&get(s.largeShadowGrants)==2&&get(s.shadowRefused)==0);
    BufModel ma(L1),mb(L2);const auto noshadow=[&]{return get(s.passThrough[unsigned(PassReason::NoShadow)]);};
    rig.sync();const auto sync0=get(s.syncCalls),pass0=noshadow();
    ma.write(A,0,4096,0x11);ma.write(A,5000,300,0x12,D3::kLockNoOverwrite);ma.write(A,9000,64,0x13,D3::kLockDiscard);
    mb.write(B,0,4096,0x21);mb.write(B,17000000,300,0x22,D3::kLockNoOverwrite);mb.write(B,9000,64,0x23,D3::kLockDiscard);mb.write(B,70000,64,0x24);
    CHECK(get(s.syncCalls)==sync0&&noshadow()==pass0&&get(s.shadowRefused)==0);
    rig.sync();CHECK(ma.same(targetBytes(A))&&mb.same(targetBytes(B)));
    // a third large buffer does not fit (both holders in use): refused, nothing dropped
    CHECK(d->CreateVertexBuffer(L3,D3::kUsageDynamic,0,(D3DPOOL)0,&C,nullptr)==D3D_OK);
    CHECK(!shadowOn(C)&&shadowOn(A)&&shadowOn(B)&&q.largeBytes()==std::size_t(L1)+L2&&get(s.shadowRefused)==1&&get(s.largeShadowDrops)==0);
    // above kMaxLargeShadow: never a large candidate
    CHECK(d->CreateVertexBuffer(Big,D3::kUsageDynamic,0,(D3DPOOL)0,&G,nullptr)==D3D_OK&&!shadowOn(G)&&q.largeBytes()==std::size_t(L1)+L2&&get(s.largeShadowGrants)==2);
    // pressure while B is locked: A goes at once, B at its unlock; largeBytes returns to 0
    frames(d,1);
    {void* p=nullptr;CHECK(B->Lock(400,32,&p,0)==D3D_OK);std::memset(p,0x31,32);std::memset(mb.bytes.data()+400,0x31,32);
     rig.sync();rig.core().memoryPressure.store(true);frames(d,1);rig.sync();
     CHECK(q.pressure()&&!shadowOn(A)&&shadowOn(B)&&q.largeBytes()==L2);
     CHECK(B->Unlock()==D3D_OK&&!shadowOn(B)&&q.largeBytes()==0);}
    rig.sync();CHECK(mb.same(targetBytes(B))&&ma.same(targetBytes(A)));
    rig.core().memoryPressure.store(false);frames(d,1);rig.sync();
    A->Release();B->Release();C->Release();G->Release();rig.sync();CHECK(q.largeBytes()==0&&s.shadowBytes.load()==0);
    rig.finish();checkClean();
}

static void shadowCap(){
    gTrace.clear();Rig rig(true);auto& q=rig.core().q;auto& s=q.stats;
    const int n=int(ShadowBudgetBytes/(2u<<20));   // how many 2 MiB shadows fit the cap
    std::vector<IDirect3DVertexBuffer9*> v;
    for(int i=0;i<n+2;++i){IDirect3DVertexBuffer9* b=nullptr;CHECK(rig.dev->CreateVertexBuffer(2u<<20,D3::kUsageDynamic,0,(D3DPOOL)0,&b,nullptr)==D3D_OK);v.push_back(b);}
    CHECK(s.shadowBytes.load()==std::int64_t(n)*(2<<20));   // the cap: the last two are refused (never evicting a live one) but still work
    CHECK(get(s.shadowRefused)==2&&get(s.shadowRefusedBytes)==2u*(2u<<20));
    void* p=nullptr;CHECK(v[n+1]->Lock(0,64,&p,D3::kLockDiscard)==D3D_OK&&v[n+1]->Unlock()==D3D_OK&&get(s.shadowLate)==0);   // still no room
    v[0]->Release();v[1]->Release();rig.sync();   // room again: a refused buffer takes its shadow at its next DISCARD lock, and only there
    CHECK(v[n]->Lock(0,64,&p,0)==D3D_OK&&v[n]->Unlock()==D3D_OK&&get(s.shadowLate)==0);
    CHECK(v[n]->Lock(0,64,&p,D3::kLockDiscard)==D3D_OK&&v[n]->Unlock()==D3D_OK&&get(s.shadowLate)==1&&s.shadowBytes.load()==std::int64_t(n-1)*(2<<20));
    v[0]=v[1]=nullptr;
    rig.sync();q.setPressure(true);CHECK(q.shadowCap()==ShadowBudgetBytes/2&&!q.shadowAdmit(1<<20)&&s.shadowBytes.load()==std::int64_t(n-1)*(2<<20));
    q.setPressure(false);for(auto* b:v)if(b)b->Release();rig.sync();CHECK(s.shadowBytes.load()==0);   // freed with their proxies
    rig.finish();checkClean();
}
// Per-level texture shadows: fresh small levels keep their first-write bytes, a written level is read back once, partial rects and DXT
// block rows land in the right place, DISCARD on a dynamic texture needs nothing, the cap refuses without breaking anything, and a GPU
// write drops the copy.
static void textureShadows(){
    gTrace.clear();Rig rig(true);auto& q=rig.core().q;auto& s=q.stats;
    auto fill=[&](unsigned char* base,INT pitch,unsigned rows,unsigned rowBytes,unsigned seed){for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<rowBytes;++x)base[std::size_t(y)*pitch+x]=(unsigned char)(seed+y*7+x);};
    {   // fresh, small: the first write lock is the shadow; later write locks, partial, never read back and never pass through
        IDirect3DTexture9* t=nullptr;CHECK(rig.dev->CreateTexture(16,16,1,0,(D3DFORMAT)22,(D3DPOOL)1,&t,nullptr)==D3D_OK);rig.sync();TTexture* tt=gLastTexture;
        D3DLOCKED_RECT lr{};CHECK(t->LockRect(0,&lr,nullptr,0)==D3D_OK&&lr.Pitch==64);fill((unsigned char*)lr.pBits,lr.Pitch,16,64,1);CHECK(t->UnlockRect(0)==D3D_OK&&get(s.texShadowFresh)==1);
        const auto pass0=get(s.passThrough[unsigned(PassReason::Written)]),hits0=get(s.texShadowHits);RECT rc{4,2,8,6};
        CHECK(t->LockRect(0,&lr,&rc,0)==D3D_OK&&lr.Pitch==64&&get(s.passThrough[unsigned(PassReason::Written)])==pass0&&get(s.texShadowReadbacks)==0&&get(s.texShadowHits)==hits0+1);
        fill((unsigned char*)lr.pBits,lr.Pitch,4,16,100);CHECK(t->UnlockRect(0)==D3D_OK);
        CHECK(t->LockRect(0,&lr,nullptr,D3::kLockReadOnly)==D3D_OK);   // read back through the shadow: the untouched bytes kept their content
        const unsigned char* b=(const unsigned char*)lr.pBits;
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<64;++x){const bool in=y>=2&&y<6&&x>=16&&x<32;const unsigned char want=in?(unsigned char)(100+(y-2)*7+(x-16)):(unsigned char)(1+y*7+x);CHECK(b[std::size_t(y)*lr.Pitch+x]==want);}
        CHECK(t->UnlockRect(0)==D3D_OK);rig.sync();
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<64;++x){const bool in=y>=2&&y<6&&x>=16&&x<32;const unsigned char want=in?(unsigned char)(100+(y-2)*7+(x-16)):(unsigned char)(1+y*7+x);CHECK(tt->surf[0]->mem[std::size_t(y)*tt->surf[0]->pitch+x]==want);}   // and the real level got the same bytes, at its own pitch
        t->Release();
    }
    {   // DXT block rows: a partial, block-aligned rect lands on the right block rows
        IDirect3DTexture9* t=nullptr;CHECK(rig.dev->CreateTexture(64,64,1,0,(D3DFORMAT)0x31545844,(D3DPOOL)1,&t,nullptr)==D3D_OK);rig.sync();TTexture* tt=gLastTexture;
        D3DLOCKED_RECT lr{};CHECK(t->LockRect(0,&lr,nullptr,0)==D3D_OK&&lr.Pitch==128);std::memset(lr.pBits,0x11,128*16);CHECK(t->UnlockRect(0)==D3D_OK);
        RECT rc{16,16,48,32};CHECK(t->LockRect(0,&lr,&rc,0)==D3D_OK&&lr.Pitch==128);   // 4 block rows of 4 blocks, inside a 16-block-wide level
        for(unsigned y=0;y<4;++y)std::memset((unsigned char*)lr.pBits+y*lr.Pitch,0xA0+y,32);CHECK(t->UnlockRect(0)==D3D_OK);rig.sync();
        CHECK(t->LockRect(0,&lr,nullptr,D3::kLockReadOnly)==D3D_OK);   // the shadow holds the block rows where the game put them
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<128;++x){const bool in=y>=4&&y<8&&x>=32&&x<64;CHECK(((unsigned char*)lr.pBits)[std::size_t(y)*lr.Pitch+x]==(in?0xA0+(y-4):0x11));}
        CHECK(t->UnlockRect(0)==D3D_OK);
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<128;++x){const bool in=y>=4&&y<8&&x>=32&&x<64;CHECK(tt->surf[0]->mem[std::size_t(y)*tt->surf[0]->pitch+x]==(in?0xA0+(y-4):0x11));}   // the replay put the same block rows into the real level
        t->Release();
    }
    {   // a written level above the fresh limit: ONE readback, then everything from the shadow
        IDirect3DTexture9* t=nullptr;CHECK(rig.dev->CreateTexture(1024,512,1,0,(D3DFORMAT)22,(D3DPOOL)1,&t,nullptr)==D3D_OK);
        D3DLOCKED_RECT lr{};RECT rc{0,0,64,4};CHECK(t->LockRect(0,&lr,&rc,0)==D3D_OK);fill((unsigned char*)lr.pBits,lr.Pitch,4,256,9);CHECK(t->UnlockRect(0)==D3D_OK&&get(s.texShadowFresh)==2);   // (the two small ones above); this one staged
        const auto syncs=get(s.census[(std::size_t)Cmd::SyncLock]);
        RECT r2{10,1,20,3};CHECK(t->LockRect(0,&lr,&r2,0)==D3D_OK&&get(s.texShadowReadbacks)==1&&get(s.census[(std::size_t)Cmd::SyncLock])==syncs+1);
        CHECK(lr.Pitch==4096&&((unsigned char*)lr.pBits)[0]==(unsigned char)(9+1*7+0*0+40-0));   // the readback returned what was staged: row 1, x=40 (the shadow of 10 px in) 
        fill((unsigned char*)lr.pBits,lr.Pitch,2,40,200);CHECK(t->UnlockRect(0)==D3D_OK);
        CHECK(t->LockRect(0,&lr,&rc,0)==D3D_OK&&get(s.texShadowReadbacks)==1&&get(s.census[(std::size_t)Cmd::SyncLock])==syncs+1);CHECK(t->UnlockRect(0)==D3D_OK);   // no second readback, no sync
        t->Release();
    }
    {   // DISCARD on a dynamic texture: the old bytes are undefined, so no readback is ever needed
        IDirect3DTexture9* t=nullptr;CHECK(rig.dev->CreateTexture(256,256,1,D3::kUsageDynamic,(D3DFORMAT)22,(D3DPOOL)0,&t,nullptr)==D3D_OK);D3DLOCKED_RECT lr{};
        const auto rb=get(s.texShadowReadbacks);
        for(int i=0;i<3;++i){CHECK(t->LockRect(0,&lr,nullptr,D3::kLockDiscard)==D3D_OK);fill((unsigned char*)lr.pBits,lr.Pitch,256,1024,unsigned(i));CHECK(t->UnlockRect(0)==D3D_OK);}
        CHECK(get(s.texShadowReadbacks)==rb);t->Release();
    }
    {   // a GPU-side write drops the copy for good: the next write lock passes through
        IDirect3DTexture9 *a=nullptr,*b=nullptr;CHECK(rig.dev->CreateTexture(16,16,1,0,(D3DFORMAT)22,(D3DPOOL)1,&a,nullptr)==D3D_OK&&rig.dev->CreateTexture(16,16,1,0,(D3DFORMAT)22,(D3DPOOL)1,&b,nullptr)==D3D_OK);
        D3DLOCKED_RECT lr{};CHECK(b->LockRect(0,&lr,nullptr,0)==D3D_OK&&b->UnlockRect(0)==D3D_OK);const auto bytes=s.texShadowBytes.load();
        CHECK(rig.dev->UpdateTexture(a,b)==D3D_OK&&s.texShadowBytes.load()==bytes-16*16*4);const auto w=get(s.passThrough[unsigned(PassReason::Written)]);
        CHECK(b->LockRect(0,&lr,nullptr,0)==D3D_OK&&get(s.passThrough[unsigned(PassReason::Written)])==w+1&&b->UnlockRect(0)==D3D_OK);a->Release();b->Release();
    }
    {   // 0.3.196 (task 12): a fresh keep takes free room or evicts only fresh keeps that are >= kFreshEvictAgeFrames old, never a re-locked shadow; with none it is skipped, the lock stages (no sync) and the real level still gets the bytes.
        // The readbacks that follow are split by cause: a fresh keep that was evicted, a re-locked shadow that was evicted, a level that never had a shadow.
        std::vector<IDirect3DTexture9*> v;const auto skip0=get(s.texShadowFreshSkipped),ev0=get(s.texShadowEvicted),sl0=get(s.census[(std::size_t)Cmd::SyncLock]),rf0=get(s.texShadowRefused);
        TTexture* last=nullptr;
        for(int i=0;i<100&&get(s.texShadowFreshSkipped)==skip0;++i){
            IDirect3DTexture9* t=nullptr;CHECK(rig.dev->CreateTexture(256,256,1,0,(D3DFORMAT)22,(D3DPOOL)1,&t,nullptr)==D3D_OK);rig.sync();last=gLastTexture;
            D3DLOCKED_RECT lr{};CHECK(t->LockRect(0,&lr,nullptr,0)==D3D_OK);fill((unsigned char*)lr.pBits,lr.Pitch,256,1024,77);CHECK(t->UnlockRect(0)==D3D_OK);v.push_back(t);}
        CHECK(get(s.texShadowFreshSkipped)==skip0+1&&v.size()>=2&&s.texShadowBytes.load()<=std::int64_t(q.texShadowCap()));   // the cap filled with fresh keeps, the next one was skipped
        CHECK(get(s.texShadowEvicted)==ev0&&get(s.census[(std::size_t)Cmd::SyncLock])==sl0&&get(s.texShadowRefused)==rf0);   // ... without evicting, without a synchronous lock, and not as a refusal
        rig.sync();for(unsigned y=0;y<256;++y)for(unsigned x=0;x<1024;++x)CHECK(last->surf[0]->mem[std::size_t(y)*last->surf[0]->pitch+x]==(unsigned char)(77+y*7+x));   // the staged Block reached the device
        const auto rb0=get(s.texShadowReadbacks),nv0=get(s.readbackNeverShadowed),fd0=get(s.readbackAfterFreshDrop),re0=get(s.readbackAfterRelockedEvict),fs0=get(s.readbackAfterFreshSkip);
        auto causes=[&]{return get(s.readbackAfterFreshSkip)-fs0+get(s.readbackNeverShadowed)-nv0+get(s.readbackAfterFreshDrop)-fd0+get(s.readbackAfterRelockedEvict)-re0;};
        {D3DLOCKED_RECT lr{};IDirect3DTexture9* t=v.back();CHECK(t->LockRect(0,&lr,nullptr,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK);}   // the skipped level, written now: one readback, its fresh keep was skipped
        CHECK(get(s.texShadowReadbacks)==rb0+1&&get(s.readbackAfterFreshSkip)==fs0+1&&causes()==1&&get(s.texShadowEvicted)>ev0);
        for(auto* t:v){D3DLOCKED_RECT lr{};CHECK(t->LockRect(0,&lr,nullptr,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK);}   // the fresh keeps the readback evicted come back through a readback each
        CHECK(get(s.readbackAfterFreshDrop)>fd0&&causes()==get(s.texShadowReadbacks)-rb0);
        std::vector<IDirect3DTexture9*> hot;   // re-locked 2 MiB levels push the re-locked shadows out too
        for(int i=0;i<10;++i){IDirect3DTexture9* t=nullptr;CHECK(rig.dev->CreateTexture(1024,512,1,0,(D3DFORMAT)22,(D3DPOOL)1,&t,nullptr)==D3D_OK);D3DLOCKED_RECT lr{};RECT rc{0,0,8,2};
            CHECK(t->LockRect(0,&lr,&rc,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK&&t->LockRect(0,&lr,&rc,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK);hot.push_back(t);}
        for(auto* t:v){D3DLOCKED_RECT lr{};CHECK(t->LockRect(0,&lr,nullptr,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK);}
        CHECK(get(s.readbackAfterRelockedEvict)>re0&&causes()==get(s.texShadowReadbacks)-rb0&&s.texShadowBytes.load()<=std::int64_t(q.texShadowCap()));   // every readback has exactly one cause
        for(auto* t:v)t->Release();for(auto* t:hot)t->Release();rig.sync();CHECK(s.texShadowBytes.load()==0);
    }
    {   // 0.3.196 (task 12) cooldown: 80 write-once levels fill the cap; a new level written and re-locked at once is skipped (young keeps) and costs one readback, counted as freshSkip;
        // after kFreshEvictAgeFrames Presents the stale keeps make room, and new write-then-re-lock levels are served from their shadow without any SyncLock.
        std::vector<IDirect3DTexture9*> v;const auto skip0=get(s.texShadowFreshSkipped);
        auto writeOnce=[&](bool relock){IDirect3DTexture9* t=nullptr;CHECK(rig.dev->CreateTexture(256,256,1,0,(D3DFORMAT)22,(D3DPOOL)1,&t,nullptr)==D3D_OK);D3DLOCKED_RECT lr{};
            CHECK(t->LockRect(0,&lr,nullptr,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK);
            if(relock){RECT rc{0,0,8,2};CHECK(t->LockRect(0,&lr,&rc,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK);}
            return t;};
        for(int i=0;i<80;++i)v.push_back(writeOnce(false));
        CHECK(get(s.texShadowFreshSkipped)>skip0);
        const auto fs0=get(s.readbackAfterFreshSkip),sl0=get(s.census[(std::size_t)Cmd::SyncLock]),ev0=get(s.texShadowEvicted);
        v.push_back(writeOnce(true));
        CHECK(get(s.readbackAfterFreshSkip)==fs0+1&&get(s.census[(std::size_t)Cmd::SyncLock])==sl0+1);   // before the cooldown
        for(unsigned i=0;i<kFreshEvictAgeFrames+1;++i)rig.dev->Present(nullptr,nullptr,nullptr,nullptr);
        const auto sl1=get(s.census[(std::size_t)Cmd::SyncLock]),rb1=get(s.texShadowReadbacks),hits1=get(s.texShadowHits);
        for(int i=0;i<10;++i)v.push_back(writeOnce(true));
        CHECK(get(s.census[(std::size_t)Cmd::SyncLock])==sl1&&get(s.texShadowReadbacks)==rb1&&get(s.texShadowHits)>=hits1+10&&get(s.texShadowEvicted)>ev0);   // after it: no sync, stale keeps were evicted
        CHECK(s.texShadowBytes.load()<=std::int64_t(q.texShadowCap()));
        for(auto* t:v)t->Release();rig.sync();CHECK(s.texShadowBytes.load()==0);
    }
    rig.sync();CHECK(s.texShadowBytes.load()>=0);
    // The cap fills with fresh keeps (levels WoW loads once and never locks again); the levels that ARE re-locked must still end up shadowed:
    // shadows are evictable, never-re-locked ones first, and each hot level costs one readback and then no sync at all.
    std::vector<IDirect3DTexture9*> fresh;const auto evicted0=get(s.texShadowEvicted);
    for(int i=0;i<140;++i){IDirect3DTexture9* t=nullptr;CHECK(rig.dev->CreateTexture(256,256,1,0,(D3DFORMAT)22,(D3DPOOL)1,&t,nullptr)==D3D_OK);D3DLOCKED_RECT lr{};   // 256 KiB: the largest fresh keep
        CHECK(t->LockRect(0,&lr,nullptr,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK);fresh.push_back(t);}
    const auto skipped0=get(s.texShadowFreshSkipped);
    CHECK(s.texShadowBytes.load()<=std::int64_t(q.texShadowCap())&&get(s.texShadowEvicted)==evicted0&&skipped0>0);   // 140 x 256 KiB > 16 MiB: the keeps that did not fit were skipped (0.3.196: a fresh keep evicts nothing), nothing refused
    std::vector<IDirect3DTexture9*> hot;
    for(int i=0;i<4;++i){IDirect3DTexture9* t=nullptr;CHECK(rig.dev->CreateTexture(1024,512,1,0,(D3DFORMAT)22,(D3DPOOL)1,&t,nullptr)==D3D_OK);D3DLOCKED_RECT lr{};RECT rc{0,0,8,2};
        CHECK(t->LockRect(0,&lr,&rc,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK);hot.push_back(t);}   // first write: staged (the level is above the fresh limit)
    const auto sl0=get(s.census[(std::size_t)Cmd::SyncLock]),rb0=get(s.texShadowReadbacks);
    for(int round=0;round<30;++round)for(auto* t:hot){D3DLOCKED_RECT lr{};RECT rc{0,0,8,2};CHECK(t->LockRect(0,&lr,&rc,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK);}
    CHECK(get(s.texShadowReadbacks)==rb0+4&&get(s.census[(std::size_t)Cmd::SyncLock])==sl0+4);   // one readback per hot level, then nothing synchronous
    CHECK(s.texShadowBytes.load()<=std::int64_t(q.texShadowCap()));
    // a kept first write that is locked again was worth keeping (counted once), and a re-locked shadow outlives never-re-locked ones
    const auto useful0=get(s.texShadowFreshUseful);{D3DLOCKED_RECT lr{};IDirect3DTexture9* t=fresh[48];   // (a kept one: the first ~64 fit, the readbacks above evicted the oldest 32)
       CHECK(t->LockRect(0,&lr,nullptr,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK&&t->LockRect(0,&lr,nullptr,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK);}
    CHECK(get(s.texShadowFreshUseful)==useful0+1);
    for(int i=0;i<140;++i){IDirect3DTexture9* t=nullptr;CHECK(rig.dev->CreateTexture(256,256,1,0,(D3DFORMAT)22,(D3DPOOL)1,&t,nullptr)==D3D_OK);D3DLOCKED_RECT lr{};CHECK(t->LockRect(0,&lr,nullptr,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK);fresh.push_back(t);}   // more fresh keeps churn
    const auto sl1=get(s.census[(std::size_t)Cmd::SyncLock]);
    for(auto* t:hot){D3DLOCKED_RECT lr{};RECT rc{0,0,8,2};CHECK(t->LockRect(0,&lr,&rc,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK);}
    CHECK(get(s.census[(std::size_t)Cmd::SyncLock])==sl1);   // the hot set survived the churn: still shadowed
    // a fresh keep never evicts a re-locked shadow: with only re-locked ones left it is refused, and still works
    for(auto* t:fresh)t->Release();fresh.clear();rig.sync();
    std::vector<IDirect3DTexture9*> warm;
    for(int i=0;i<16;++i){IDirect3DTexture9* t=nullptr;CHECK(rig.dev->CreateTexture(1024,512,1,0,(D3DFORMAT)22,(D3DPOOL)1,&t,nullptr)==D3D_OK);D3DLOCKED_RECT lr{};RECT rc{0,0,8,2};
        CHECK(t->LockRect(0,&lr,&rc,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK&&t->LockRect(0,&lr,&rc,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK);warm.push_back(t);}   // 20 re-locked 2 MiB levels > the 16 MiB cap: the oldest were evicted
    CHECK(s.texShadowBytes.load()<=std::int64_t(q.texShadowCap()));
    const auto skipped1=get(s.texShadowFreshSkipped),refused0=get(s.texShadowRefused);{IDirect3DTexture9* t=nullptr;CHECK(rig.dev->CreateTexture(64,64,1,0,(D3DFORMAT)22,(D3DPOOL)1,&t,nullptr)==D3D_OK);D3DLOCKED_RECT lr{};
        const auto size0=s.texShadowBytes.load();CHECK(size0+64*64*4>std::int64_t(q.texShadowCap()));   // the cap is full of re-locked levels
        CHECK(t->LockRect(0,&lr,nullptr,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK&&get(s.texShadowFreshSkipped)==skipped1+1&&get(s.texShadowRefused)==refused0&&s.texShadowBytes.load()==size0);t->Release();}   // skipped (it stages), not refused
    for(auto* t:hot)t->Release();for(auto* t:warm)t->Release();rig.sync();CHECK(s.texShadowBytes.load()==0);
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
    CHECK(rig.dev->ValidateDevice(&n)==5&&rig.core().q.depth()==0);
    CHECK(rig.dev->TestCooperativeLevel()==D3D_OK&&get(s.coopAnswered)==1);   // 0.3.204 (task 21): answered on the game thread (no census, see testCooperativeLevelLocal)
    CHECK(rig.dev->GetDisplayMode(0,&dm)==5&&rig.dev->GetRasterStatus(0,&rs)==5&&rig.dev->GetClipStatus(&cs)==5);rig.dev->GetGammaRamp(0,&ramp);
    CHECK(get(s.census[(std::size_t)Cmd::Device_TestCooperativeLevel])==0);
    for(Cmd c:{Cmd::Device_ValidateDevice,Cmd::Device_GetDisplayMode,Cmd::Device_GetRasterStatus,Cmd::Device_GetClipStatus,Cmd::Device_GetGammaRamp})CHECK(get(s.census[(std::size_t)c])==1);
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

// The cursor: Win32 state of the calling thread, done at the call and never recorded (a late replay warped the mouse back and fought WM_SETCURSOR).
static std::vector<std::string> gCursorLog;static POINT gCursorPos{0,0};static unsigned char gCursorBits[32*32*4];static int gCursorHandles=0;
static CursorApi cursorFake(){
    CursorApi a;
    a.getPos=[](POINT* p){*p=gCursorPos;return BOOL(1);};
    a.setPos=[](int x,int y){gCursorLog.push_back("setPos "+std::to_string(x)+" "+std::to_string(y));gCursorPos=POINT{x,y};return BOOL(1);};
    a.create=[](UINT hx,UINT hy,const unsigned char* bits)->void*{std::memcpy(gCursorBits,bits,sizeof gCursorBits);gCursorLog.push_back("create "+std::to_string(hx)+" "+std::to_string(hy));return reinterpret_cast<void*>(std::uintptr_t(0x100+ ++gCursorHandles));};
    a.destroy=[](void* h){gCursorLog.push_back("destroy "+std::to_string(reinterpret_cast<std::uintptr_t>(h)));};
    a.set=[](void* h){gCursorLog.push_back(h?"set "+std::to_string(reinterpret_cast<std::uintptr_t>(h)):std::string("set null"));};
    return a;
}
static void cursorHandling(){
    gTrace.clear();gCursorLog.clear();gCursorPos=POINT{0,0};gCursorHandles=0;const CursorApi api=cursorFake();
    StreamDevice::Options opt;opt.cursorApi=&api;Rig rig(true,opt);IDirect3DDevice9* d=rig.dev;auto& q=rig.core().q;auto& s=q.stats;
    const auto seq=q.recordedSeq();
    // SetCursorPosition: applied now, on the caller's thread, only when it differs, and never recorded
    d->SetCursorPosition(5,6,0);d->SetCursorPosition(5,6,0);CHECK(gCursorLog==std::vector<std::string>{"setPos 5 6"});
    std::thread other([&]{d->SetCursorPosition(9,9,0);});other.join();CHECK(gCursorLog.size()==2&&gCursorLog[1]=="setPos 9 9"&&get(s.foreignEntries)==1);
    // ShowCursor: the previous visibility; no handle yet, so no ::SetCursor
    CHECK(d->ShowCursor(1)==0&&d->ShowCursor(0)==1&&gCursorLog.size()==2);
    // a hardware cursor from the surface's shadow: 32x32, pitch 128, the surface's own pixels, hidden because visibility is off
    IDirect3DSurface9* sf=nullptr;CHECK(d->CreateOffscreenPlainSurface(16,12,(D3DFORMAT)21,(D3DPOOL)2,&sf,nullptr)==D3D_OK);
    D3DLOCKED_RECT lr{};CHECK(sf->LockRect(&lr,nullptr,0)==D3D_OK);for(unsigned y=0;y<12;++y)for(unsigned x=0;x<64;++x)((unsigned char*)lr.pBits)[y*lr.Pitch+x]=(unsigned char)(y*5+x+1);CHECK(sf->UnlockRect()==D3D_OK);
    const auto sync0=get(s.census[(std::size_t)Cmd::SyncLock]);
    CHECK(d->SetCursorProperties(3,4,sf)==D3D_OK&&get(s.census[(std::size_t)Cmd::SyncLock])==sync0);   // no readback: the shadow is current
    CHECK(gCursorLog.size()==4&&gCursorLog[2]=="create 3 4"&&gCursorLog[3]=="set null");
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<128;++x)CHECK(gCursorBits[y*128+x]==(y<12&&x<64?(unsigned char)(y*5+x+1):0));   // only the surface's 16x12 pixels
    CHECK(d->ShowCursor(1)==0&&gCursorLog.back()=="set 257");
    // no valid CPU copy (a GPU write dropped the shadow): one synchronous readback, the same bytes; the old cursor goes before the new one is made
    CHECK(d->ColorFill(sf,nullptr,0)==D3D_OK);rig.sync();
    CHECK(d->SetCursorProperties(1,2,sf)==D3D_OK&&get(s.census[(std::size_t)Cmd::SyncLock])==sync0+1);
    CHECK(gCursorLog[gCursorLog.size()-3]=="destroy 257"&&gCursorLog[gCursorLog.size()-2]=="create 1 2"&&gCursorLog.back()=="set 258");   // visible now: shown at once
    for(unsigned y=0;y<12;++y)for(unsigned x=0;x<64;++x)CHECK(gCursorBits[y*128+x]==(unsigned char)(y*5+x+1));
    // invalid: null, and a surface that is not A8R8G8B8
    IDirect3DSurface9* x8=nullptr;CHECK(d->CreateOffscreenPlainSurface(16,16,(D3DFORMAT)22,(D3DPOOL)2,&x8,nullptr)==D3D_OK);
    CHECK(d->SetCursorProperties(0,0,nullptr)==D3DERR_INVALIDCALL&&d->SetCursorProperties(0,0,x8)==D3DERR_INVALIDCALL);
    // a large cursor in fullscreen is a software cursor: forwarded through the queue (the backend does nothing with it), ours untouched
    IDirect3DSurface9* big=nullptr;CHECK(d->CreateOffscreenPlainSurface(64,64,(D3DFORMAT)21,(D3DPOOL)2,&big,nullptr)==D3D_OK);
    const std::size_t logSize=gCursorLog.size();const auto before=q.recordedSeq();
    CHECK(d->SetCursorProperties(0,0,big)==D3D_OK&&q.recordedSeq()==before+1&&gCursorLog.size()==logSize);rig.sync();
    unsigned forwarded=0;for(auto& t:gTrace)if(t.rfind("Device::SetCursorProperties",0)==0)++forwarded;CHECK(forwarded==1);
    // windowed, the same surface is a hardware cursor; Reset keeps the handle
    D3DPRESENT_PARAMETERS pp{};pp.BackBufferWidth=640;pp.BackBufferHeight=480;pp.BackBufferFormat=(D3DFORMAT)22;pp.BackBufferCount=2;pp.Windowed=1;
    CHECK(d->Reset(&pp)==D3D_OK);const auto afterReset=gCursorLog.size();
    CHECK(d->SetCursorProperties(0,0,big)==D3D_OK&&gCursorLog[afterReset]=="destroy 258"&&gCursorLog[afterReset+1]=="create 0 0");   // (no destroy happened at Reset)
    // nothing of the cursor was ever recorded
    for(auto& t:gTrace)CHECK(t.rfind("Device::ShowCursor",0)!=0&&t.rfind("Device::SetCursorPosition",0)!=0);
    CHECK(q.recordedSeq()>seq);
    sf->Release();x8->Release();big->Release();
    rig.finish();checkClean();
    CHECK(gCursorLog.back()=="destroy 259");   // the cursor handle goes with the device
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
    rig.checkFlagged();rig.dev->Release();   // the game's reference: the device lives on for the objects it handed out
    CHECK(gDeviceDeletes.load()==deletes&&tex->GetDevice(&back)==D3D_OK&&back==rig.sd);back->Release();
    D3DSURFACE_DESC d{};CHECK(lvl->GetDesc(&d)==D3D_OK&&d.Width==16);
    tex->Release();vb->Release();sc->Release();CHECK(gDeviceDeletes.load()==deletes);   // the level and the back buffer still hold it
    lvl->Release();CHECK(gDeviceDeletes.load()==deletes);
    bb->Release();   // the last public reference: the device, the queue and the replay thread go
    CHECK(gDeviceDeletes.load()==deletes+1);checkClean();
    // and without the game keeping a bound proxy alive: a bound texture does not keep the device
    Rig again(true);IDirect3DTexture9* t2=nullptr;CHECK(again.dev->CreateTexture(8,8,1,0,(D3DFORMAT)22,(D3DPOOL)1,&t2,nullptr)==D3D_OK);
    again.dev->SetTexture(0,t2);t2->Release();again.checkFlagged();again.dev->Release();CHECK(gDeviceDeletes.load()==deletes+2);checkClean();
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
// The periodic stats line: >600 presents with Diagnostics on give exactly one line, and it carries the CSTREAM tag the log is searched by.
static std::vector<std::string> gStatLines;
static void statsLine(){
    gTrace.clear();gStatLines.clear();StreamDevice::Options opt;
    opt.log=[](const char* l){gStatLines.push_back(l);};opt.diagnostics=[]{return true;};
    Rig rig(true,opt);
    for(int i=0;i<610;++i){rig.dev->DrawPrimitive((D3DPRIMITIVETYPE)4,0,2);rig.dev->Present(nullptr,nullptr,nullptr,nullptr);}
    rig.sync();
    std::vector<std::string> lines;for(auto& l:gStatLines)if(l.find(" frames=600 ")!=std::string::npos)lines.push_back(l);   // the 600th replayed frame
    CHECK(lines.size()==1&&lines[0].rfind("CSTREAM cmds=",0)==0&&lines[0].find("passPerFrame=")!=std::string::npos&&lines[0].find("census[")!=std::string::npos&&lines[0].back()==']');
    for(const char* field:{"game[per frame]: ms=","syncMs=","presentWaitMs=","bpMs=","sleeps=","publishes=","replayBusyMs/frame=","recorded=","answered=","texShadow=","readbacks=","bufShadow=","readbacks/frame=","evicted/frame=","(hot ","refused/frame=","grows=","large=","memMB=","texFreshSkipped=","texReadbackCause[freshDrop=","relockedEvict=","neverShadowed=","freshSkip="," skipped="})CHECK(lines[0].find(field)!=std::string::npos);   // per-window numbers
    CHECK(lines[0].size()<2000);
    rig.finish();checkClean();
}
// Redundant-state filtering: a repeated Set of the value the game last set is not recorded; anything else is.
static void redundantFiltering(){
    gTrace.clear();Rig rig(true);auto& q=rig.core().q;auto& st=rig.sd->stateOf();
    // recorded(f): did f put a command in the queue?
    auto recorded=[&](auto f){const auto before=q.recordedSeq();f();return q.recordedSeq()!=before;};
    // twice(f): the first call is always recorded; the second only when filtering is off
    auto twice=[&](auto f,const char* what){const bool a=recorded(f),b=recorded(f);if(!a||b==kFilterRedundantState){std::fprintf(stderr,"redundancy: %s first=%d second=%d\n",what,a,b);std::abort();}};
    auto differs=[&](auto f,const char* what){if(!recorded(f)){std::fprintf(stderr,"redundancy: %s with a new value was not recorded\n",what);std::abort();}};
    auto never=[&](auto f,const char* what){if(!recorded(f)||!recorded(f)){std::fprintf(stderr,"redundancy: %s must always be recorded\n",what);std::abort();}};
    IDirect3DDevice9* d=rig.dev;
    // a default read by the defaults batch is not a game Set: the first Set of the same value is recorded, only then it is filtered
    twice([&]{d->SetRenderState((D3DRENDERSTATETYPE)8,1008);},"render state at its default");
    differs([&]{d->SetRenderState((D3DRENDERSTATETYPE)8,1);},"render state");
    never([&]{d->SetRenderState((D3DRENDERSTATETYPE)154,0x7fa05000);},"D3DRS_POINTSIZE (a vendor trigger)");
    never([&]{d->SetRenderState((D3DRENDERSTATETYPE)181,7);},"D3DRS_ADAPTIVETESS_Y");
    twice([&]{d->SetSamplerState(2,(D3DSAMPLERSTATETYPE)5,3);},"sampler state");twice([&]{d->SetSamplerState(258,(D3DSAMPLERSTATETYPE)5,3);},"vertex sampler state");
    differs([&]{d->SetSamplerState(2,(D3DSAMPLERSTATETYPE)5,4);},"sampler state");
    twice([&]{d->SetTextureStageState(1,(D3DTEXTURESTAGESTATETYPE)4,9);},"texture stage state");
    IDirect3DTexture9 *ta=nullptr,*tb=nullptr;CHECK(d->CreateTexture(8,8,1,0,(D3DFORMAT)22,(D3DPOOL)1,&ta,nullptr)==D3D_OK&&d->CreateTexture(8,8,1,0,(D3DFORMAT)22,(D3DPOOL)1,&tb,nullptr)==D3D_OK);
    twice([&]{d->SetTexture(3,ta);},"texture");differs([&]{d->SetTexture(3,tb);},"texture");twice([&]{d->SetTexture(3,nullptr);},"texture null");
    {   // a redundant bind changes no use count (no atomic), and a different one does
        ProxyBase* pa=ProxyBase::of(ta);d->SetTexture(4,ta);const auto use=pa->use.load();recorded([&]{d->SetTexture(4,ta);});CHECK(pa->use.load()==use);
        d->SetTexture(4,tb);CHECK(pa->use.load()==use-1);
        if(!kFilterRedundantState){d->SetTexture(4,tb);const auto u2=ProxyBase::of(tb)->use.load();d->SetTexture(4,tb);CHECK(ProxyBase::of(tb)->use.load()==u2);}   // unfiltered, the same proxy again still binds nothing twice
    }
    IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;CHECK(d->CreateVertexBuffer(256,0,0,(D3DPOOL)0,&vb,nullptr)==D3D_OK&&d->CreateIndexBuffer(64,0,(D3DFORMAT)101,(D3DPOOL)0,&ib,nullptr)==D3D_OK);
    twice([&]{d->SetStreamSource(0,vb,0,20);},"stream source");differs([&]{d->SetStreamSource(0,vb,4,20);},"stream source offset");differs([&]{d->SetStreamSource(0,vb,4,24);},"stream source stride");
    twice([&]{d->SetStreamSourceFreq(1,1);},"stream frequency");twice([&]{d->SetIndices(ib);},"indices");
    {   // DrawPrimitiveUP unbinds stream 0, DrawIndexedPrimitiveUP also the indices: the same Set after them is not redundant
        unsigned char v[200]={};d->SetStreamSource(0,vb,4,24);d->SetIndices(ib);
        CHECK(d->DrawPrimitiveUP((D3DPRIMITIVETYPE)4,1,v,20)==D3D_OK);CHECK(recorded([&]{d->SetStreamSource(0,vb,4,24);})&&(!recorded([&]{d->SetIndices(ib);}))==kFilterRedundantState);   // stream 0 was unbound; the indices were not
        unsigned short ix[8]={};CHECK(d->DrawIndexedPrimitiveUP((D3DPRIMITIVETYPE)4,0,3,1,ix,(D3DFORMAT)101,v,20)==D3D_OK);CHECK(recorded([&]{d->SetIndices(ib);})&&recorded([&]{d->SetStreamSource(0,vb,4,24);})&&(!recorded([&]{d->SetIndices(ib);}))==kFilterRedundantState);
    }
    DWORD code[]={0xFFFE0200u,0x0000FFFFu};IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;D3DVERTEXELEMENT9 el[2]={{0,0,2,0,0,0},{0xFF,0,17,0,0,0}};IDirect3DVertexDeclaration9* dc=nullptr;
    CHECK(d->CreateVertexShader(code,&vs)==D3D_OK&&d->CreatePixelShader(code,&ps)==D3D_OK&&d->CreateVertexDeclaration(el,&dc)==D3D_OK);
    twice([&]{d->SetVertexShader(vs);},"vertex shader");twice([&]{d->SetPixelShader(ps);},"pixel shader");twice([&]{d->SetVertexDeclaration(dc);},"vertex declaration");
    twice([&]{d->SetFVF(0x112);},"FVF");differs([&]{d->SetVertexDeclaration(dc);},"declaration after an FVF");
    {   // constants: filtered only when EVERY register in range is known from a game Set and equal; otherwise the whole call is recorded
        float c[16];for(int i=0;i<16;++i)c[i]=float(i);
        twice([&]{d->SetVertexShaderConstantF(10,c,2);},"vs float constants");
        CHECK(!recorded([&]{d->SetVertexShaderConstantF(10,c,1);})==kFilterRedundantState);   // a subset of what was set
        CHECK(recorded([&]{d->SetVertexShaderConstantF(11,c+4,2);}));                         // overlap: register 12 was never set
        CHECK(recorded([&]{d->SetVertexShaderConstantF(10,c+4,1);}));                         // a different value
        twice([&]{d->SetPixelShaderConstantF(0,c,4);},"ps float constants");
        int ic[8]={1,2,3,4,5,6,7,8};twice([&]{d->SetVertexShaderConstantI(1,ic,2);},"vs int constants");twice([&]{d->SetPixelShaderConstantI(1,ic,1);},"ps int constants");
        WINBOOL bc[3]={1,0,1};twice([&]{d->SetVertexShaderConstantB(0,bc,3);},"vs bool constants");twice([&]{d->SetPixelShaderConstantB(2,bc,1);},"ps bool constants");
        CHECK(recorded([&]{d->SetVertexShaderConstantF(300,c,1);}));                          // out of range: always recorded
    }
    D3DMATRIX m{};m.m[0]=2;D3DMATERIAL9 mt{};mt.b[0]=1;RECT rc{0,0,10,10};float plane[4]={1,0,0,0};
    twice([&]{d->SetTransform((D3DTRANSFORMSTATETYPE)2,&m);},"transform");twice([&]{d->SetMaterial(&mt);},"material");twice([&]{d->LightEnable(1,1);},"light enable");
    twice([&]{d->SetScissorRect(&rc);},"scissor");twice([&]{d->SetClipPlane(0,plane);},"clip plane");twice([&]{d->SetNPatchMode(1.f);},"npatch");
    twice([&]{d->SetSoftwareVertexProcessing(1);},"software vertex processing");twice([&]{d->SetCurrentTexturePalette(3);},"texture palette");
    // never filtered
    IDirect3DSurface9* rt=nullptr;CHECK(d->CreateRenderTarget(64,64,(D3DFORMAT)22,(D3DMULTISAMPLE_TYPE)0,0,0,&rt,nullptr)==D3D_OK);
    never([&]{d->SetRenderTarget(1,rt);},"SetRenderTarget");never([&]{d->SetDepthStencilSurface(nullptr);},"SetDepthStencilSurface");
    D3DVIEWPORT9 vpt{0,0,64,64,0,1};never([&]{d->SetViewport(&vpt);},"SetViewport");D3DLIGHT9 lt{};never([&]{d->SetLight(0,&lt);},"SetLight");
    never([&]{d->DrawPrimitive((D3DPRIMITIVETYPE)4,0,1);},"DrawPrimitive");never([&]{d->Clear(0,nullptr,1,0,1.f,0);},"Clear");
    // a state block's Apply makes everything unknown; Reset too; a replay failure invalidates at the next Get
    IDirect3DStateBlock9* sb=nullptr;CHECK(d->CreateStateBlock((D3DSTATEBLOCKTYPE)1,&sb)==D3D_OK);
    d->SetRenderState((D3DRENDERSTATETYPE)20,2);sb->Apply();CHECK(recorded([&]{d->SetRenderState((D3DRENDERSTATETYPE)20,2);}));
    d->SetRenderState((D3DRENDERSTATETYPE)21,2);rig.core().replayFailure.store(true);DWORD gv=0;d->GetRenderState((D3DRENDERSTATETYPE)22,&gv);CHECK(recorded([&]{d->SetRenderState((D3DRENDERSTATETYPE)21,2);}));
    // the same without any Get in between: the failure flag is consumed where filtering is decided
    d->SetRenderState((D3DRENDERSTATETYPE)23,2);CHECK(!recorded([&]{d->SetRenderState((D3DRENDERSTATETYPE)23,2);})==kFilterRedundantState);
    {const auto f0=get(q.stats.replayFailures);rig.core().replayFailure.store(true);CHECK(recorded([&]{d->SetRenderState((D3DRENDERSTATETYPE)23,2);})&&!rig.core().replayFailure.load()&&get(q.stats.replayFailures)==f0+1);}
    d->SetRenderState((D3DRENDERSTATETYPE)24,2);rig.core().replayFailure.store(true);d->Present(nullptr,nullptr,nullptr,nullptr);CHECK(!rig.core().replayFailure.load()&&recorded([&]{d->SetRenderState((D3DRENDERSTATETYPE)24,2);}));   // Present consumes it too
    // a value learned by a sync Get is not a game Set
    d->GetRenderState((D3DRENDERSTATETYPE)30,&gv);CHECK(recorded([&]{d->SetRenderState((D3DRENDERSTATETYPE)30,gv);}));
    // a slot the audit declared sync-only is never filtered
    d->SetRenderState((D3DRENDERSTATETYPE)40,1);rig.core().syncOnly[0].fetch_or(1ull<<40);CHECK(recorded([&]{d->SetRenderState((D3DRENDERSTATETYPE)40,1);}));
    // recording a state block records every Set and leaves StreamState alone
    d->SetRenderState((D3DRENDERSTATETYPE)50,7);CHECK(d->BeginStateBlock()==D3D_OK);
    CHECK(recorded([&]{d->SetRenderState((D3DRENDERSTATETYPE)50,7);})&&recorded([&]{d->SetRenderState((D3DRENDERSTATETYPE)50,7);}));
    IDirect3DStateBlock9* made=nullptr;CHECK(d->EndStateBlock(&made)==D3D_OK);CHECK(!recorded([&]{d->SetRenderState((D3DRENDERSTATETYPE)50,7);})==kFilterRedundantState);
    D3DPRESENT_PARAMETERS pp{};pp.BackBufferWidth=640;pp.BackBufferHeight=480;pp.BackBufferFormat=(D3DFORMAT)22;pp.BackBufferCount=2;
    IDirect3DSurface9* bb=nullptr;d->GetBackBuffer(0,0,(D3DBACKBUFFER_TYPE)0,&bb);bb->Release();
    CHECK(d->Reset(&pp)==D3D_OK);CHECK(recorded([&]{d->SetRenderState((D3DRENDERSTATETYPE)50,7);}));
    if(kFilterRedundantState)CHECK(get(q.stats.filteredCalls)>30);else CHECK(get(q.stats.filteredCalls)==0);
    (void)st;
    sb->Release();made->Release();rt->Release();dc->Release();vs->Release();ps->Release();vb->Release();ib->Release();ta->Release();tb->Release();
    rig.finish();checkClean();
}
// D3D9 side effects the stream mirrors: SetRenderTarget resets the viewport and scissor (a Get must not answer the old one).
static void renderTargetResetsViewport(){
    gTrace.clear();Rig rig(true);IDirect3DDevice9* d=rig.dev;
    D3DVIEWPORT9 v{0,0,100,100,0,1};d->SetViewport(&v);D3DVIEWPORT9 g{};const auto syncs=get(rig.core().q.stats.syncCalls);
    CHECK(d->GetViewport(&g)==D3D_OK&&g.Width==100&&get(rig.core().q.stats.syncCalls)==syncs);
    IDirect3DSurface9* rt=nullptr;CHECK(d->CreateRenderTarget(64,64,(D3DFORMAT)22,(D3DMULTISAMPLE_TYPE)0,0,0,&rt,nullptr)==D3D_OK);d->SetRenderTarget(0,rt);
    CHECK(d->GetViewport(&g)==D3D_OK&&get(rig.core().q.stats.syncCalls)==syncs+1);   // unknown again: the Target answers
    rt->Release();rig.finish();checkClean();
}
// Memory pressure published by the memory guard: at the next Present the stream gives memory back (idle pools, texture shadows down to the
// halved cap, idle buffer shadows) without ever touching what is locked, and what the game reads stays what the real resource holds.
static void memoryPressureRelease(){
    gTrace.clear();Rig rig(true);auto& core=rig.core();auto& q=core.q;auto& s=q.stats;IDirect3DDevice9* d=rig.dev;
    // buffer shadows: three aged, one fresh, one locked across the Present
    std::vector<IDirect3DVertexBuffer9*> vb;
    for(int i=0;i<5;++i){IDirect3DVertexBuffer9* b=nullptr;CHECK(d->CreateVertexBuffer(1u<<20,D3::kUsageDynamic,0,(D3DPOOL)0,&b,nullptr)==D3D_OK);vb.push_back(b);void* p=nullptr;CHECK(b->Lock(0,0,&p,0)==D3D_OK);std::memset(p,0x40+i,1u<<20);CHECK(b->Unlock()==D3D_OK);}
    for(int i=0;i<130;++i)d->Present(nullptr,nullptr,nullptr,nullptr);   // the first three go idle: their last lock is 130 frames old
    {void* p=nullptr;CHECK(vb[3]->Lock(0,64,&p,0)==D3D_OK&&vb[3]->Unlock()==D3D_OK);}   // fresh
    void* held=nullptr;CHECK(vb[4]->Lock(0,0,&held,0)==D3D_OK);std::memset(held,0x77,1u<<20);   // locked now, across the Present
    auto shadowOf=[&](int i){return static_cast<StreamVertexBuffer*>(ProxyBase::of(vb[i]))->buf.shadowOn;};
    // texture shadows up to the cap, one level locked across the Present
    std::vector<IDirect3DTexture9*> tex;
    for(int i=0;i<70;++i){IDirect3DTexture9* t=nullptr;CHECK(d->CreateTexture(256,256,1,0,(D3DFORMAT)22,(D3DPOOL)1,&t,nullptr)==D3D_OK);D3DLOCKED_RECT lr{};CHECK(t->LockRect(0,&lr,nullptr,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK);tex.push_back(t);}
    D3DLOCKED_RECT tl{};CHECK(tex[0]->LockRect(0,&tl,nullptr,0)==D3D_OK);std::memset(tl.pBits,0x5C,256*4);
    CHECK(s.texShadowBytes.load()>std::int64_t(TextureShadowBudgetBytes/2)&&shadowOf(0)&&shadowOf(1)&&shadowOf(2)&&shadowOf(3)&&shadowOf(4));
    rig.sync();const auto before=rig.sd->replayerOf().memory();
    core.memoryPressure.store(true);d->Present(nullptr,nullptr,nullptr,nullptr);rig.sync();
    CHECK(q.pressure()&&s.texShadowBytes.load()<=std::int64_t(q.texShadowCap())&&s.shadowBytes.load()<=std::int64_t(q.shadowCap()));
    CHECK(!shadowOf(0)&&!shadowOf(1)&&!shadowOf(2));   // idle for 130 frames: dropped
    CHECK(shadowOf(4));                                  // locked: never touched (the cap allows it)
    CHECK(rig.sd->replayerOf().memory().total()<before.total());
    CHECK(get(s.texShadowEvicted)>0);
    // the locked ones keep working and the game reads what the real resource holds
    CHECK(vb[4]->Unlock()==D3D_OK&&tex[0]->UnlockRect(0)==D3D_OK);
    void* p=nullptr;CHECK(vb[4]->Lock(0,16,&p,D3::kLockReadOnly)==D3D_OK&&((unsigned char*)p)[0]==0x77&&((unsigned char*)p)[15]==0x77);CHECK(vb[4]->Unlock()==D3D_OK);
    CHECK(vb[0]->Lock(0,16,&p,D3::kLockReadOnly)==D3D_OK&&((unsigned char*)p)[0]==0x40&&((unsigned char*)p)[15]==0x40);CHECK(vb[0]->Unlock()==D3D_OK);   // dropped: read from the real buffer
    D3DLOCKED_RECT r2{};CHECK(tex[0]->LockRect(0,&r2,nullptr,D3::kLockReadOnly)==D3D_OK&&((unsigned char*)r2.pBits)[3]==0x5C&&((unsigned char*)r2.pBits)[1023]==0x5C);CHECK(tex[0]->UnlockRect(0)==D3D_OK);
    // pressure over: caps back, and a DISCARD lock brings a dropped buffer's shadow back
    core.memoryPressure.store(false);d->Present(nullptr,nullptr,nullptr,nullptr);CHECK(!q.pressure());
    const auto late=get(s.shadowLate);CHECK(vb[1]->Lock(0,0,&p,D3::kLockDiscard)==D3D_OK&&vb[1]->Unlock()==D3D_OK&&get(s.shadowLate)==late+1&&shadowOf(1));
    for(auto* t:tex)t->Release();for(auto* b:vb)b->Release();
    rig.finish();checkClean();
}
// 0.3.204 (task 21): TestCooperativeLevel is answered on the game thread from StreamCore::coopState (written by the replay thread) while it is D3D_OK; lost / not reset
// goes synchronous again (exact answers), and a successful Reset publishes D3D_OK. Observed StreamFramesAhead frames late, so the test Presents and drains first.
static void testCooperativeLevelLocal(unsigned framesAhead){
    gTrace.clear();gKnobs.coop.store(D3D_OK);StreamDevice::Options opt;opt.framesAhead=framesAhead;Rig rig(true,opt);auto& core=rig.core();auto& s=core.q.stats;
    // (a) device OK: nothing is queued, nothing waits, the replay thread is not involved (it is busy in BeginScene)
    gKnobs.hold.store(true);rig.dev->BeginScene();rig.dev->SetRenderState((D3DRENDERSTATETYPE)7,5);
    const auto syncs=get(s.syncCalls),sg=get(s.stateSynced);const std::size_t depth=core.q.depth();
    for(int i=0;i<50;++i)CHECK(rig.dev->TestCooperativeLevel()==D3D_OK);
    CHECK(get(s.syncCalls)==syncs&&get(s.stateSynced)==sg&&get(s.coopAnswered)==50&&get(s.census[(std::size_t)Cmd::Device_TestCooperativeLevel])==0&&core.q.depth()>=depth&&core.q.depth()>0);
    gKnobs.hold.store(false);rig.sync();
    // Presents of an OK device keep it OK
    for(unsigned i=0;i<4;++i)rig.dev->Present(nullptr,nullptr,nullptr,nullptr);rig.sync();
    const auto base=get(s.syncCalls);CHECK(rig.dev->TestCooperativeLevel()==D3D_OK&&get(s.syncCalls)==base);
    // (b) lost: the replay thread sees DEVICELOST at a Present; every call is then synchronous and exact
    gKnobs.coop.store(D3DERR_DEVICELOST);
    for(unsigned i=0;i<framesAhead+3;++i)rig.dev->Present(nullptr,nullptr,nullptr,nullptr);rig.sync();
    CHECK(core.coopState.load()==D3DERR_DEVICELOST);
    const auto s0=get(s.syncCalls),c0=get(s.census[(std::size_t)Cmd::Device_TestCooperativeLevel]),a0=get(s.coopAnswered);
    CHECK(rig.dev->TestCooperativeLevel()==D3DERR_DEVICELOST&&rig.dev->TestCooperativeLevel()==D3DERR_DEVICELOST);
    CHECK(get(s.syncCalls)==s0+2&&get(s.census[(std::size_t)Cmd::Device_TestCooperativeLevel])==c0+2&&get(s.coopAnswered)==a0);
    // a Reset that fails (still lost) leaves the cache at what the Target reports: the next call stays synchronous, never a local D3D_OK
    IDirect3DSurface9* bb=nullptr;CHECK(rig.dev->GetBackBuffer(0,0,(D3DBACKBUFFER_TYPE)0,&bb)==D3D_OK);bb->Release();
    D3DPRESENT_PARAMETERS pp{};pp.BackBufferWidth=640;pp.BackBufferHeight=480;pp.BackBufferFormat=(D3DFORMAT)22;pp.BackBufferCount=2;
    CHECK(rig.dev->Reset(&pp)==D3DERR_DEVICELOST&&core.coopState.load()==D3DERR_DEVICELOST);
    {const auto s2=get(s.syncCalls),a2=get(s.coopAnswered);CHECK(rig.dev->TestCooperativeLevel()==D3DERR_DEVICELOST&&get(s.syncCalls)==s2+1&&get(s.coopAnswered)==a2);}
    // not reset yet: the Target says so and the cache follows it
    gKnobs.coop.store(D3DERR_DEVICENOTRESET);
    {const auto s3=get(s.syncCalls);
     CHECK(rig.dev->TestCooperativeLevel()==D3DERR_DEVICENOTRESET&&get(s.syncCalls)==s3+1&&core.coopState.load()==D3DERR_DEVICENOTRESET);
     CHECK(rig.dev->TestCooperativeLevel()==D3DERR_DEVICENOTRESET&&get(s.syncCalls)==s3+2);}
    // Reset (the game released its back buffer): the Target is cooperative again, the next call is local
    CHECK(rig.dev->Reset(&pp)==D3D_OK&&core.coopState.load()==D3D_OK);
    const auto s1=get(s.syncCalls),a1=get(s.coopAnswered);
    CHECK(rig.dev->TestCooperativeLevel()==D3D_OK&&get(s.syncCalls)==s1&&get(s.coopAnswered)==a1+1);
    for(unsigned i=0;i<4;++i)rig.dev->Present(nullptr,nullptr,nullptr,nullptr);rig.sync();
    CHECK(rig.dev->TestCooperativeLevel()==D3D_OK&&core.coopState.load()==D3D_OK);
    rig.finish();gKnobs.coop.store(D3D_OK);
}
static void nestedSyncInPump(){
    gTrace.clear();Rig rig(true);auto& s=rig.core().q.stats;
    static Rig* r;static HRESULT nested;static int calls;r=&rig;nested=12345;calls=0;
    gKnobs.hold.store(true);rig.dev->BeginScene();   // the replay thread is busy: the next sync really waits
    pumpHook=[]{if(calls++==0){DWORD np=0;nested=r->dev->ValidateDevice(&np);gKnobs.hold.store(false);}};
    DWORD np=0;CHECK(rig.dev->ValidateDevice(&np)==5);pumpHook=nullptr;
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

// At most three snapshots per frame and one frame of lag: the pool (8) is never exhausted by the trigger policy.
static void snapshotPoolNotExhausted(){
    gTrace.clear();gCaptures=0;StreamDevice::Options opt;opt.capture=&fakeCapture;Rig rig(true,opt);
    for(int frame=0;frame<300;++frame){for(int i=0;i<6;++i)rig.dev->DrawPrimitive((D3DPRIMITIVETYPE)4,0,2);rig.dev->Present(nullptr,nullptr,nullptr,nullptr);}
    rig.sync();CHECK(gCaptures.load()==300&&rig.sd->replayerOf().snapshots.allocated()<=SnapshotPool::DefaultCap);
    rig.finish();
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
        case 6:dev->SetRenderState((D3DRENDERSTATETYPE)(1+r(250)),r(3));break;
        case 7:dev->SetSamplerState(r(2)?r(16):257+r(4),(D3DSAMPLERSTATETYPE)(1+r(13)),r(3));break;
        case 8:dev->SetTextureStageState(r(8),(D3DTEXTURESTAGESTATETYPE)(1+r(32)),r(3));break;
        case 9:{DWORD s=r(250)+1;DWORD v=0;HRESULT hr=dev->GetRenderState((D3DRENDERSTATETYPE)s,&v);log("rs "+std::to_string(hr)+" "+std::to_string(v));
            DWORD sv=0;hr=dev->GetSamplerState(r(16),(D3DSAMPLERSTATETYPE)(1+r(13)),&sv);log("ss "+std::to_string(hr)+" "+std::to_string(sv));
            hr=dev->GetTextureStageState(r(8),(D3DTEXTURESTAGESTATETYPE)(1+r(32)),&sv);log("tss "+std::to_string(hr)+" "+std::to_string(sv));break;}
        case 10:{D3DMATRIX m{};{const float mv=float(r(3));for(int i=0;i<16;++i)m.m[i]=mv;}dev->SetTransform((D3DTRANSFORMSTATETYPE)(r(2)?2:3),&m);D3DMATRIX g{};dev->GetTransform((D3DTRANSFORMSTATETYPE)(r(2)?2:3),&g);log("xf "+hx((unsigned char*)&g,64));break;}
        case 11:{D3DVIEWPORT9 v{};v.X=r(10);v.Y=r(10);v.Width=64+r(500);v.Height=64+r(400);v.MaxZ=1;dev->SetViewport(&v);D3DVIEWPORT9 g{};dev->GetViewport(&g);log("vp "+hx((unsigned char*)&g,sizeof g));
            RECT sc{LONG(r(2)),LONG(r(2)),LONG(50+r(2)),LONG(50+r(2))};dev->SetScissorRect(&sc);RECT g2{};dev->GetScissorRect(&g2);log("sc "+hx((unsigned char*)&g2,sizeof g2));break;}
        case 12:{float c[16];{const float cv=float(r(2));for(float& x:c)x=cv;}const UINT reg0=r(6),cnt=1+r(4);dev->SetVertexShaderConstantF(reg0,c,cnt);float g[16]={};HRESULT hr=dev->GetVertexShaderConstantF(reg0+r(cnt),g,1);log("vsf "+std::to_string(hr)+" "+hx((unsigned char*)g,16));break;}
        case 13:{if(auto* t=any(tex)){const DWORD st=r(2)?r(16):257+r(4);dev->SetTexture(st,t);IDirect3DBaseTexture9* g=nullptr;dev->GetTexture(st,&g);log("gtex "+std::to_string(idOf(g)));if(g)g->Release();}break;}
        case 14:{if(auto* b=any(vb)){const UINT i=r(4);dev->SetStreamSource(i,b,r(2),20);IDirect3DVertexBuffer9* g=nullptr;UINT o=0,st=0;dev->GetStreamSource(i,&g,&o,&st);log("gsv "+std::to_string(idOf(g)));if(g)g->Release();}
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
        case 31:{if(r(2)){D3DVIEWPORT9 v{};v.X=r(2);v.Width=100;v.Height=100;v.MaxZ=1;dev->SetViewport(&v);}else dev->SetViewport(nullptr);break;}
        case 32:{if(r(4)==0)dropOne();break;}
        default:{   // the less common filterable Sets, with values from a tiny set
            switch(r(6)){
            case 0:{D3DMATERIAL9 m{};std::memset(&m,int(r(2)),sizeof m);dev->SetMaterial(&m);break;}
            case 1:dev->LightEnable(r(3),r(2));break;
            case 2:{float p[4]={float(r(2)),0,0,0};dev->SetClipPlane(r(2),p);break;}
            case 3:dev->SetNPatchMode(float(r(2)));break;
            case 4:dev->SetSoftwareVertexProcessing(r(2));break;
            default:dev->SetCurrentTexturePalette(r(2));break;
            }
            break;}
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
static void equivalence(int steps,std::uint64_t seed,unsigned framesAhead=1){
    // mode 0: the game on the Device directly; 1: through the stream, every call replayed on the Device; 2: through the stream with the DIRECT
    // methods replayed on the extension device with raw pointers. All three must leave identical Target traces and game-visible results.
    std::vector<std::string> out[3],trace[3];std::vector<HRESULT> pres[3];
    for(int mode=0;mode<3;++mode){
        gTrace.clear();gKnobs.presents.store(0);gNormalize=true;gPtrIds.clear();gNextPtrId=0;StreamDevice::Options opt;opt.framesAhead=framesAhead;Rig rig(mode>0,opt,mode==2);
        Game g(rig.dev,seed,out[mode],pres[mode]);
        for(int i=0;i<steps;++i){
            g.step(i);
            if(i==steps/2){   // a Reset in the middle: the game released nothing the Target holds
                g.releaseAll();D3DPRESENT_PARAMETERS pp{};pp.BackBufferWidth=700;pp.BackBufferHeight=500;pp.BackBufferFormat=(D3DFORMAT)22;pp.BackBufferCount=2;
                out[mode].push_back("reset "+std::to_string(rig.dev->Reset(&pp)));
            }
        }
        g.releaseAll();rig.sync();
        if(mode&&kFilterRedundantState)CHECK(get(rig.core().q.stats.filteredCalls)>100);   // the scenario repeats Sets on purpose
        if(mode==2&&kDirectReplay)CHECK(get(rig.core().q.stats.directCalls)>1000);
        if(mode==1||(mode==2&&!kDirectReplay))CHECK(get(rig.core().q.stats.directCalls)==0);
        trace[mode]=filtered(gTrace);
        rig.finish();checkClean();gNormalize=false;
    }
    for(int mode=1;mode<3;++mode){
        CHECK(out[0].size()==out[mode].size());for(std::size_t i=0;i<out[0].size();++i)if(out[0][i]!=out[mode][i]){std::fprintf(stderr,"result %zu differs (mode %d):\n direct: %s\n stream: %s\n",i,mode,out[0][i].c_str(),out[mode][i].c_str());std::abort();}
        {std::size_t i=0;auto& A=trace[0];auto& B=trace[mode];while(i<A.size()&&i<B.size()&&A[i]==B[i])++i;
         if(i<A.size()||i<B.size()){std::fprintf(stderr,"trace differs at %zu (mode %d, sizes %zu vs %zu)\n direct: %s\n stream: %s\n",i,mode,A.size(),B.size(),i<A.size()?A[i].substr(0,300).c_str():"-",i<B.size()?B[i].substr(0,300).c_str():"-");
             for(std::size_t k=i>12?i-12:0;k<i;++k)std::fprintf(stderr,"  before[%zu]: %s | %s\n",k,A[k].substr(0,110).c_str(),B[k].substr(0,110).c_str());std::abort();}}
        // Present returns the real result of the Present framesAhead frames back (0.3.200 (pipeline); 1 = the previous frame's), D3D_OK before there is one
        CHECK(pres[0].size()==pres[mode].size()&&!pres[0].empty());for(std::size_t i=0;i<pres[mode].size();++i)CHECK(pres[mode][i]==(i>=framesAhead?pres[0][i-framesAhead]:D3D_OK));
    }
    std::printf("equivalence seed=%llu steps=%d framesAhead=%u results=%zu trace=%zu presents=%zu\n",(unsigned long long)seed,steps,framesAhead,out[0].size(),trace[0].size(),pres[0].size());
}
// Direct replay: a proxy caches the backend object behind its inner right when the create/derive/first-sight bound it, a proxy without one
// replays through the Device, and the cache follows Reset.
static void directReplayRaw(){
    if(!kDirectReplay)return;
    gTrace.clear();Rig rig(true);auto& s=rig.core().q.stats;IDirect3DDevice9* d=rig.dev;
    auto rawOk=[&](ProxyBase* p){return p&&p->inner&&p->raw&&p->raw==static_cast<IUnknown*>(unwrapFake(p->inner));};
    IDirect3DTexture9* tex=nullptr;IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;IDirect3DVertexDeclaration9* dc=nullptr;
    CHECK(d->CreateTexture(32,32,2,0,(D3DFORMAT)22,(D3DPOOL)1,&tex,nullptr)==D3D_OK&&d->CreateVertexBuffer(64,0,0,(D3DPOOL)0,&vb,nullptr)==D3D_OK&&d->CreateIndexBuffer(64,0,(D3DFORMAT)101,(D3DPOOL)0,&ib,nullptr)==D3D_OK);
    DWORD code[]={0xFFFE0200u,0x0000FFFFu};D3DVERTEXELEMENT9 el[2]={{0,0,2,0,0,0},{0xFF,0,17,0,0,0}};
    CHECK(d->CreateVertexShader(code,&vs)==D3D_OK&&d->CreatePixelShader(code,&ps)==D3D_OK&&d->CreateVertexDeclaration(el,&dc)==D3D_OK);
    IDirect3DSurface9* lvl=nullptr;CHECK(tex->GetSurfaceLevel(1,&lvl)==D3D_OK);rig.sync();
    for(IUnknown* o:{(IUnknown*)tex,(IUnknown*)vb,(IUnknown*)ib,(IUnknown*)vs,(IUnknown*)ps,(IUnknown*)dc,(IUnknown*)lvl})CHECK(rawOk(ProxyBase::of(o)));   // create and derive
    IDirect3DSurface9 *rt=nullptr,*ds=nullptr;CHECK(d->GetRenderTarget(0,&rt)==D3D_OK&&d->GetDepthStencilSurface(&ds)==D3D_OK);CHECK(rawOk(ProxyBase::of(rt))&&rawOk(ProxyBase::of(ds)));   // implicit objects
    // the direct calls count, and the Target saw what it would have through the Device
    const auto d0=get(s.directCalls);
    d->SetRenderState((D3DRENDERSTATETYPE)7,3);d->SetTexture(0,tex);d->SetStreamSource(0,vb,0,20);d->SetIndices(ib);d->SetVertexShader(vs);d->SetPixelShader(ps);d->SetVertexDeclaration(dc);d->SetRenderTarget(1,lvl);rig.sync();
    CHECK(get(s.directCalls)==d0+8);
    // a dead create has no raw and its calls are not direct (they go through the Device, which drops them)
    IDirect3DCubeTexture9* cube=nullptr;CHECK(d->CreateCubeTexture(8,1,0,(D3DFORMAT)22,(D3DPOOL)1,&cube,nullptr)==D3D_OK);rig.sync();CHECK(!ProxyBase::of(cube)->raw);
    const auto d1=get(s.directCalls);d->SetTexture(5,cube);rig.sync();CHECK(get(s.directCalls)==d1);d->SetTexture(5,nullptr);
    // a resolver that proves nothing: that proxy's Sets replay through the Device, the rest stay direct
    gKnobs.noRaw.store(true);IDirect3DTexture9* plain=nullptr;CHECK(d->CreateTexture(8,8,1,0,(D3DFORMAT)22,(D3DPOOL)1,&plain,nullptr)==D3D_OK);rig.sync();gKnobs.noRaw.store(false);
    CHECK(!ProxyBase::of(plain)->raw);const auto d2=get(s.directCalls);d->SetTexture(6,plain);d->SetRenderState((D3DRENDERSTATETYPE)8,3);rig.sync();CHECK(get(s.directCalls)==d2+1);
    // Reset: the back buffers are derived again and their raw follows
    d->SetTexture(0,nullptr);d->SetTexture(6,nullptr);d->SetRenderTarget(1,nullptr);rt->Release();ds->Release();
    IDirect3DSurface9* bb=nullptr;CHECK(d->GetBackBuffer(0,0,(D3DBACKBUFFER_TYPE)0,&bb)==D3D_OK);bb->Release();
    ProxyBase* kid=ProxyBase::of(bb);CHECK(rawOk(kid));
    D3DPRESENT_PARAMETERS pp{};pp.BackBufferWidth=800;pp.BackBufferHeight=600;pp.BackBufferFormat=(D3DFORMAT)22;pp.BackBufferCount=2;lvl->Release();
    CHECK(d->Reset(&pp)==D3D_OK);CHECK(rawOk(kid));   // raw follows the re-derived back buffer (an address compare with the old raw is not valid: the allocator may reuse it)
    d->SetTexture(0,tex);d->SetRenderTarget(0,bb);rig.sync();   // calls on the re-derived back buffer are direct again
    d->SetRenderTarget(0,nullptr);
    plain->Release();cube->Release();tex->Release();vb->Release();ib->Release();vs->Release();ps->Release();dc->Release();
    rig.finish();checkClean();
}
// 0.3.192 (CS): queue/counter layout. The producer's, the consumer's and the shared groups sit on distinct cache lines; a heap-allocated
// core (StreamDevice::coreOwner) must come back aligned (C++17 aligned new).
static void layoutIsolation(){
    static_assert(Queue::layoutIsolated(),"queue groups");
    static_assert(offsetof(Counters,commands)/kLine!=offsetof(Counters,directCalls)/kLine&&offsetof(Counters,directCalls)/kLine!=offsetof(Counters,chunksLive)/kLine,"counters");
    static_assert(offsetof(Counters,consumerSleeps)/kLine==offsetof(Counters,replayFailures)/kLine&&offsetof(Counters,queryPolls)/kLine==offsetof(Counters,deadCreates)/kLine,"consumer counters together");
    auto* c=new StreamCore;CHECK(reinterpret_cast<std::uintptr_t>(c)%kLine==0&&reinterpret_cast<std::uintptr_t>(&c->q.stats)%kLine==0);delete c;
    std::unique_ptr<Queue> q(new Queue);CHECK(reinterpret_cast<std::uintptr_t>(q.get())%kLine==0);
}
// Replay accounting: idle grows while the queue is empty and busy = wall - idle never exceeds wall; the line carries sleeps= and publishes=.
static void replayTimingAccounting(){
    gTrace.clear();Rig rig(true);Replayer& rp=rig.sd->replayerOf();
    for(int i=0;i<200;++i){rig.dev->SetRenderState((D3DRENDERSTATETYPE)7,i);rig.dev->DrawPrimitive((D3DPRIMITIVETYPE)4,0,2);}
    rig.dev->Present(nullptr,nullptr,nullptr,nullptr);rig.sync();
    const auto idle0=rp.idleNs.load(),wall0=rp.wallNs();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));rig.sync();   // the replay thread waits for commands; a wait is accounted when it ends (the sync wakes it)
    const auto idle1=rp.idleNs.load(),wall1=rp.wallNs();
    CHECK(idle1-idle0>=40000000ull&&idle1-idle0<=wall1-wall0+2000000ull);   // grew by about the empty time, never more than elapsed (+2 ms: 0.3.196 (task 12): the replay thread may already have begun this wait when wall0 was sampled, a few us of slack seen in ~1 run of 20)
    const auto idle=rp.idleNs.load();const auto wall=rp.wallNs();CHECK(idle<=wall&&rp.busyNsTotal()<=wall&&rp.busyNsTotal()>0);
    rig.finish();checkClean();
}
// Diagnostics off: the replay thread keeps no audit state; switched on mid-session it starts clean at the next frame.
static std::atomic<bool> gDiagOn{false};
static int frameSalt(){static int n=0;return n+=1000;}
static void diagnosticsOffSkipsAudit(){
    gTrace.clear();gDiagOn.store(false);StreamDevice::Options opt;opt.diagnostics=[]{return gDiagOn.load();};opt.log=[](const char*){};
    Rig rig(true,opt);Replayer& rp=rig.sd->replayerOf();
    auto frame=[&]{for(int i=0;i<300;++i)rig.dev->SetRenderState((D3DRENDERSTATETYPE)(7+i%5),DWORD(i+frameSalt()));rig.dev->Present(nullptr,nullptr,nullptr,nullptr);rig.sync();};
    frame();frame();CHECK(rp.auditSets()==0);
    gDiagOn.store(true);frame();CHECK(rp.auditSets()==0);   // the frame that was running when it turned on: not recorded
    frame();CHECK(rp.auditSets()>0);
    gDiagOn.store(false);const auto n=rp.auditSets();frame();frame();CHECK(rp.auditSets()<=n+300);   // one more recorded frame at most (the flip is seen at the next boundary)
    rig.finish();checkClean();
}
// A pending query makes the replay thread poll; publish() must still wake it at once (it used to sleep 50 us blind, which Windows rounds up to 1 ms+).
static void idlePollWakes(){
    {Queue q;std::atomic<int> st{0};std::atomic<std::uint64_t> wokeNs{0};
     std::thread t([&]{st.store(1);auto* h=q.nextTimed(5000);if(h){wokeNs.store(nowNs());q.retire(h);}st.store(2);});
     while(st.load()!=1)std::this_thread::yield();std::this_thread::sleep_for(std::chrono::milliseconds(30));   // asleep in the timed wait
     const auto t0=nowNs();record(q,1,8,0);q.publish();
     CHECK(waitFor([&]{return st.load()==2;},3000)&&wokeNs.load()>=t0&&wokeNs.load()-t0<1000000000ull);t.join();   // far below the 5 s timeout
     Queue r;CHECK(r.nextTimed(2)==nullptr&&get(r.stats.consumerSleeps)==1);}   // no data: returns after the timeout
    gTrace.clear();gKnobs.holdQueries.store(true);Rig rig(true);auto& q=rig.core().q;
    IDirect3DQuery9* qy=nullptr;CHECK(rig.dev->CreateQuery((D3DQUERYTYPE)9,&qy)==D3D_OK);CHECK(qy->Issue(D3::kIssueEnd)==D3D_OK);rig.dev->BeginScene();   // publishes; the query stays pending
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const auto sleeps0=get(q.stats.consumerSleeps);CHECK(sleeps0>=1);   // idle-polling on the timed wait
    std::vector<std::uint64_t> lat;
    for(int i=0;i<100;++i){
        std::this_thread::sleep_for(std::chrono::microseconds(300));   // let it fall back into the timed wait
        const auto t0=nowNs();rig.dev->EndScene();const auto seq=q.recordedSeq();q.publish();   // EndScene publishes by itself
        while(q.replayedSeq()<seq)std::this_thread::yield();lat.push_back(nowNs()-t0);
    }
    std::sort(lat.begin(),lat.end());CHECK(lat[50]<700000ull&&lat[99]<500000000ull);   // median under the 1 ms poll period: woken, not polled
    gKnobs.holdQueries.store(false);DWORD d=0;int spins=0;while(qy->GetData(&d,4,0)==S_FALSE&&++spins<1000000)std::this_thread::yield();CHECK(spins<1000000);
    qy->Release();rig.finish();checkClean();
}

// 0.3.192 (CS) review fixes ------------------------------------------------------------------------------------------------------------------
// A Block that cannot fit beside the open write chunk is refused up front: a 10 MiB unlock with the 16 MiB budget neither waits for a drain nor stalls the game thread.
static void impossibleBlockIsRefusedAtOnce(){
    gTrace.clear();Rig rig(true);auto& q=rig.core().q;auto& s=q.stats;IDirect3DDevice9* d=rig.dev;
    const UINT len=10u<<20;IDirect3DVertexBuffer9* vb=nullptr;CHECK(d->CreateVertexBuffer(len,D3::kUsageDynamic,0,(D3DPOOL)0,&vb,nullptr)==D3D_OK&&vb);CHECK(shadowOn(vb));   // the large allowance
    gKnobs.hold.store(true);d->BeginScene();for(int i=0;i<10;++i)d->SetRenderState((D3DRENDERSTATETYPE)(60+i),unsigned(i+1));   // the replay thread is stuck: the queue is not drained
    const auto refused0=get(s.blockRefused),bp0=get(s.backpressureWaits);
    CHECK(q.tryAllocBlock(len)==nullptr&&get(s.blockRefused)==refused0+1&&get(s.backpressureWaits)==bp0);   // would have waited for a drain that cannot come
    gKnobs.hold.store(false);
    void* p=nullptr;CHECK(vb->Lock(0,0,&p,D3::kLockDiscard)==D3D_OK);std::memset(p,0x6B,len);((unsigned char*)p)[len-1]=0x11;
    CHECK(vb->Unlock()==D3D_OK);   // the 128 KiB inline pieces: no Block, no stall
    CHECK(get(s.blockRefused)>=refused0+2&&get(s.backpressureWaits)==bp0);
    rig.sync();{const unsigned char* c=targetBytes(vb);CHECK(c[0]==0x6B&&c[len/2]==0x6B&&c[len-1]==0x11);}
    // the same for a big staged lock (a 9 MiB first write without a shadow): the budget refusal sends it to the synchronous pass-through, still without a backpressure wait
    IDirect3DVertexBuffer9* st=nullptr;CHECK(d->CreateVertexBuffer(9u<<20,0,0,(D3DPOOL)0,&st,nullptr)==D3D_OK&&st);
    CHECK(st->Lock(0,0,&p,0)==D3D_OK&&p&&get(s.passThrough[unsigned(PassReason::Budget)])>=1);std::memset(p,0x22,9u<<20);CHECK(st->Unlock()==D3D_OK);
    CHECK(get(s.backpressureWaits)==bp0);rig.sync();CHECK(targetBytes(st)[(9u<<20)-1]==0x22);
    vb->Release();st->Release();rig.finish();checkClean();
}
// Small staged locks (first write / DISCARD without a shadow) use the game-side scratch: no pooled Block (no cross-thread pool mutex), the bytes still arrive, an overrun
// stays inside the allocation (ASan), and when every slot is taken (or the lock is large) the Block path takes over.
static void smallStagedLocksUseScratch(){
    gTrace.clear();Rig rig(true);auto& s=rig.core().q.stats;IDirect3DDevice9* d=rig.dev;
    const auto blocks=[&]{return get(s.blockAllocs)+get(s.blockReuses);};
    IDirect3DVertexBuffer9* a=nullptr;CHECK(d->CreateVertexBuffer(1u<<20,0,0,(D3DPOOL)0,&a,nullptr)==D3D_OK);   // no shadow: static, DEFAULT pool
    const auto b0=blocks();void* p=nullptr;
    CHECK(a->Lock(100,64,&p,0)==D3D_OK);CHECK(((unsigned char*)p)[0]==0&&((unsigned char*)p)[63]==0);std::memset(p,0x77,64+300);CHECK(a->Unlock()==D3D_OK);   // zeroed range, overrun tolerated
    CHECK(blocks()==b0&&get(s.lockAsync)>=1);rig.sync();{const unsigned char* c=targetBytes(a);CHECK(c[99]==0&&c[100]==0x77&&c[163]==0x77&&c[164]==0);}
    CHECK(a->Lock(0,64u<<10,&p,D3::kLockDiscard)==D3D_OK);std::memset(p,0x33,64u<<10);CHECK(a->Unlock()==D3D_OK);CHECK(blocks()==b0);   // the largest scratch lock: 64 KiB
    CHECK(a->Lock(0,(64u<<10)+1,&p,D3::kLockDiscard)==D3D_OK);std::memset(p,0x44,(64u<<10)+1);CHECK(a->Unlock()==D3D_OK);CHECK(blocks()==b0+1);   // above: a pooled Block
    rig.sync();{const unsigned char* c=targetBytes(a);CHECK(c[0]==0x44&&c[(64u<<10)]==0x44&&c[(64u<<10)+1]==0);}
    // five buffers locked at the same time: four scratch slots, the fifth takes a Block
    std::vector<IDirect3DVertexBuffer9*> v;const auto b1=blocks();
    for(int i=0;i<5;++i){IDirect3DVertexBuffer9* b=nullptr;CHECK(d->CreateVertexBuffer(4096,0,0,(D3DPOOL)0,&b,nullptr)==D3D_OK);v.push_back(b);CHECK(b->Lock(0,32,&p,0)==D3D_OK);std::memset(p,0xA0+i,32);}
    CHECK(blocks()==b1+1);for(auto* b:v)CHECK(b->Unlock()==D3D_OK);rig.sync();
    for(int i=0;i<5;++i)CHECK(targetBytes(v[i])[31]==0xA0+i&&targetBytes(v[i])[32]==0);
    // the slots came back: the next small lock needs no Block
    CHECK(v[0]->Lock(0,32,&p,D3::kLockDiscard)==D3D_OK&&blocks()==b1+1);CHECK(v[0]->Unlock()==D3D_OK);
    // a buffer released while locked gives its slot back
    {IDirect3DVertexBuffer9* r=nullptr;CHECK(d->CreateVertexBuffer(256,0,0,(D3DPOOL)0,&r,nullptr)==D3D_OK);CHECK(r->Lock(0,16,&p,0)==D3D_OK);r->Release();rig.sync();}
    for(int i=0;i<4;++i){CHECK(v[i]->Lock(0,16,&p,D3::kLockDiscard)==D3D_OK);}
    CHECK(blocks()==b1+1);for(int i=0;i<4;++i)CHECK(v[i]->Unlock()==D3D_OK);
    for(auto* b:v)b->Release();a->Release();rig.finish();checkClean();
}
// 0.3.200 (pipeline): StreamFramesAhead n: with the replay thread stuck, n Presents return at once and the next one waits (n=1: the 0.3.199 pacing, the
// second Present waits); the snapshot pool and the clamp follow n.
static void framesAheadPacing(){
    static_assert(SnapshotPool::capFor(1)==SnapshotPool::DefaultCap&&SnapshotPool::capFor(3)*sizeof(GameSnapshot)<=(1u<<20),"pool per framesAhead, under 1 MiB");
    static_assert(budgetForFramesAhead(1)==BudgetBytes&&budgetForFramesAhead(2)==(32u<<20)&&budgetForFramesAhead(3)==(32u<<20)&&budgetForFramesAhead(0)==BudgetBytes&&budgetForFramesAhead(9)==kMaxBudgetBytes,"budget: +16 MiB per extra frame, 32 MiB at most");
    for(unsigned n:{1u,2u,3u,7u}){
        const unsigned want=clampFramesAhead(n);
        gTrace.clear();StreamDevice::Options opt;opt.framesAhead=n;Rig rig(true,opt);auto& q=rig.core().q;
        CHECK(rig.sd->framesAhead()==want&&rig.sd->replayerOf().snapshots.allocated()==0);
        rig.dev->Present(nullptr,nullptr,nullptr,nullptr);rig.sync();   // P0 has run
        gKnobs.hold.store(true);rig.dev->BeginScene();   // the replay thread is stuck: nothing after this runs
        std::atomic<unsigned> returned{0};const auto waits0=get(q.stats.presentWaits);
        std::thread game([&]{for(unsigned i=0;i<want+1;++i){rig.dev->Present(nullptr,nullptr,nullptr,nullptr);returned.fetch_add(1);}});   // (the producer for now; joined before the main thread records again)
        CHECK(waitFor([&]{return returned.load()==want;}));
        std::this_thread::sleep_for(std::chrono::milliseconds(40));CHECK(returned.load()==want);   // Present want+1 waits for the stuck one want frames back
        gKnobs.hold.store(false);game.join();CHECK(returned.load()==want+1&&get(q.stats.presentWaits)>=waits0+1);
        rig.dev->EndScene();rig.sync();rig.finish();checkClean();
    }
}
// 0.3.200 (pipeline): an evicted level shadow's allocation serves the next one (no free + malloc pair); the reused shadow is zero-filled like a new one; pressure drops the spares.
static void textureShadowSpares(){
    gTrace.clear();Rig rig(true);auto& core=rig.core();auto& s=core.q.stats;IDirect3DDevice9* d=rig.dev;
    std::vector<IDirect3DTexture9*> v;
    auto writeOnce=[&](unsigned char fill){IDirect3DTexture9* t=nullptr;CHECK(d->CreateTexture(256,256,1,0,(D3DFORMAT)22,(D3DPOOL)1,&t,nullptr)==D3D_OK);D3DLOCKED_RECT lr{};RECT rc{0,0,16,16};
        CHECK(t->LockRect(0,&lr,&rc,0)==D3D_OK);for(int y=0;y<16;++y)std::memset(static_cast<unsigned char*>(lr.pBits)+std::ptrdiff_t(y)*lr.Pitch,fill,64);CHECK(t->UnlockRect(0)==D3D_OK);return t;};
    for(int i=0;i<64;++i)v.push_back(writeOnce(0xAB));   // 64 x 256 KiB fresh keeps fill the 16 MiB cap
    CHECK(get(s.texShadowSpared)==0&&get(s.texShadowSpareReuses)==0);
    for(unsigned i=0;i<kFreshEvictAgeFrames+1;++i)d->Present(nullptr,nullptr,nullptr,nullptr);   // the keeps are stale now
    for(int i=0;i<8;++i)v.push_back(writeOnce(0xCD));
    CHECK(get(s.texShadowEvicted)>=8&&get(s.texShadowSpared)>=8&&get(s.texShadowSpareReuses)>=8);   // each new keep evicted a stale one and took its allocation
    {D3DLOCKED_RECT lr{};IDirect3DTexture9* t=v.back();CHECK(t->LockRect(0,&lr,nullptr,D3::kLockReadOnly)==D3D_OK);   // served from the reused shadow
     const auto* p=static_cast<const unsigned char*>(lr.pBits);bool ok=true;
     for(unsigned y=0;y<256;++y)for(unsigned x=0;x<1024;++x)ok&=p[std::size_t(y)*lr.Pitch+x]==((y<16&&x<64)?0xCD:0);   // the stale level's bytes are gone: zero where nothing was written
     CHECK(ok&&t->UnlockRect(0)==D3D_OK);}
    {IDirect3DTexture9* t=nullptr;CHECK(d->CreateTexture(1024,512,1,0,(D3DFORMAT)22,(D3DPOOL)1,&t,nullptr)==D3D_OK);D3DLOCKED_RECT lr{};RECT rc{0,0,8,2};
     CHECK(t->LockRect(0,&lr,&rc,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK&&t->LockRect(0,&lr,&rc,0)==D3D_OK&&t->UnlockRect(0)==D3D_OK);v.push_back(t);}   // a 2 MiB readback evicts eight keeps: the spares fill, none fits it
    {std::size_t kept=0;for(const auto& x:core.texSpare)kept+=x.capacity()?1:0;CHECK(kept==StreamCore::kTexSpares);}
    core.memoryPressure.store(true);d->Present(nullptr,nullptr,nullptr,nullptr);rig.sync();
    for(const auto& x:core.texSpare)CHECK(x.capacity()==0);   // pressure: no spare kept
    core.memoryPressure.store(false);d->Present(nullptr,nullptr,nullptr,nullptr);rig.sync();
    for(auto* t:v)t->Release();rig.sync();CHECK(s.texShadowBytes.load()==0);rig.finish();checkClean();
}
// 0.3.200 (frame skip): StreamFrameSkip. A lagging replay (stuck in frame 0 while the game records frames 1 and 2, three frames ahead) skips frame 1:
// its draws, Clears and real Present are dropped, everything else runs. The same script with StreamFrameSkip=0 is the reference: the trace without draws,
// Clears and Presents, the Target's resource bytes, its final state and the Present results must be equal. Frames that issue an occlusion query, draw into
// a texture (set in the frame or carried over from the previous one) or copy the back buffer are drawn; an event query's frame is skipped and still completes.
enum class SkipCase{Plain,Event,Occlusion,RenderToTexture,CarriedTarget,BackBufferCopy};
struct SkipRun {std::vector<std::string> trace;std::vector<unsigned char> tex,vb;std::string state;std::vector<HRESULT> presents;std::uint64_t skipped=0,dropped=0;unsigned draws=0,realPresents=0;};
static bool droppedBySkip(const std::string& t){return t.rfind("Device::Draw",0)==0||t.rfind("Device::Clear",0)==0||t.rfind("Device::Present",0)==0;}
static SkipRun skipScript(unsigned frameSkip,SkipCase c,bool lag=true){
    gTrace.clear();gCaptures=0;gNormalize=true;gPtrIds.clear();gNextPtrId=0;gKnobs.presents.store(0);gKnobs.noDigest.store(true);
    StreamDevice::Options opt;opt.framesAhead=3;opt.frameSkip=frameSkip;opt.capture=&fakeCapture;Rig rig(true,opt);IDirect3DDevice9* d=rig.dev;SkipRun r;
    IDirect3DTexture9* tex=nullptr;CHECK(d->CreateTexture(16,16,1,0,(D3DFORMAT)22,(D3DPOOL)1,&tex,nullptr)==D3D_OK);
    IDirect3DVertexBuffer9* vb=nullptr;CHECK(d->CreateVertexBuffer(256,D3::kUsageDynamic,0,(D3DPOOL)0,&vb,nullptr)==D3D_OK);
    IDirect3DTexture9* rtt=nullptr;CHECK(d->CreateTexture(32,32,1,D3::kUsageRT,(D3DFORMAT)22,(D3DPOOL)0,&rtt,nullptr)==D3D_OK);
    IDirect3DSurface9 *rts=nullptr,*bb=nullptr;CHECK(rtt->GetSurfaceLevel(0,&rts)==D3D_OK&&d->GetBackBuffer(0,0,(D3DBACKBUFFER_TYPE)0,&bb)==D3D_OK);
    IDirect3DQuery9* q=nullptr;CHECK(d->CreateQuery((D3DQUERYTYPE)(c==SkipCase::Event?8:9),&q)==D3D_OK);
    const unsigned char up[3*16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48};
    auto frame=[&](int f){
        d->SetRenderState((D3DRENDERSTATETYPE)7,100+f);d->SetTexture(0,tex);const float k[4]={float(f),1,2,3};d->SetVertexShaderConstantF(4,k,1);
        D3DLOCKED_RECT lr{};RECT rc{0,f,16,f+1};CHECK(tex->LockRect(0,&lr,&rc,0)==D3D_OK);std::memset(lr.pBits,0x10+f,64);CHECK(tex->UnlockRect(0)==D3D_OK);   // one row per frame
        void* p=nullptr;CHECK(vb->Lock(UINT(f)*16,16,&p,D3::kLockNoOverwrite)==D3D_OK);std::memset(p,0x40+f,16);CHECK(vb->Unlock()==D3D_OK);
        d->BeginScene();d->Clear(0,nullptr,1,0,1.f,0);d->DrawPrimitive((D3DPRIMITIVETYPE)4,0,2);d->DrawPrimitiveUP((D3DPRIMITIVETYPE)4,1,up,16);
        if(f==1)switch(c){
            case SkipCase::Event:case SkipCase::Occlusion:q->Issue(D3::kIssueBegin);d->DrawPrimitive((D3DPRIMITIVETYPE)4,0,2);q->Issue(D3::kIssueEnd);break;
            case SkipCase::RenderToTexture:d->SetRenderTarget(0,rts);d->DrawPrimitive((D3DPRIMITIVETYPE)4,0,2);d->SetRenderTarget(0,bb);break;
            case SkipCase::CarriedTarget:d->DrawPrimitive((D3DPRIMITIVETYPE)4,0,2);d->SetRenderTarget(0,bb);break;   // frame 0 left the texture bound
            case SkipCase::BackBufferCopy:d->StretchRect(bb,nullptr,rts,nullptr,(D3DTEXTUREFILTERTYPE)0);break;
            default:break;}
        if(f==0&&c==SkipCase::CarriedTarget)d->SetRenderTarget(0,rts);
        d->EndScene();r.presents.push_back(d->Present(nullptr,nullptr,nullptr,nullptr));
    };
    rig.sync();   // the replay has opened frame 0 (nothing recorded ahead of it: drawn)
    if(lag)gKnobs.hold.store(true);   // frame 0's BeginScene keeps the replay thread there while frames 1 and 2 are recorded (three frames ahead: no Present waits)
    for(int f=0;f<3;++f){frame(f);if(!lag)rig.sync();}
    gKnobs.hold.store(false);rig.sync();   // frame 1 opened with frames 1 and 2 complete: skipped if allowed; frame 2 had no later frame: drawn
    for(int f=3;f<6;++f){frame(f);rig.sync();}   // drained after each: no later frame is complete when one opens; Present 4 waits for Present 1 (the skipped one when it was skipped)
    rig.sync();
    if(c==SkipCase::Event){HRESULT hr=S_FALSE;CHECK(waitFor([&]{hr=q->GetData(nullptr,0,0);return hr!=S_FALSE;})&&hr==D3D_OK);}   // the skipped frame's event query still completes
    rig.sync();
    auto& rp=rig.sd->replayerOf();CHECK(rp.snapshots.allocated()>=1&&rp.snapshots.idle()+1==rp.snapshots.allocated());   // every snapshot but the active one is back in the pool
    r.skipped=rp.skippedFrames();r.dropped=get(rig.core().q.stats.skippedCommands);r.trace=gTrace;
    for(auto& t:gTrace){if(t.rfind("Device::Draw",0)==0)++r.draws;if(t.rfind("Device::Present",0)==0)++r.realPresents;}
    {auto* t=static_cast<TTexture*>(static_cast<IDirect3DTexture9*>(ProxyBase::of(tex)->inner));r.tex.assign(t->surf[0]->mem,t->surf[0]->mem+t->surf[0]->own.size());}
    r.vb.assign(targetBytes(vb),targetBytes(vb)+256);
    gKnobs.noDigest.store(false);r.state=rig.target->digest();
    CHECK(rig.target->ext.tex[0]==ProxyBase::of(tex)->inner&&rig.target->ext.rt0==static_cast<TSurface*>(static_cast<IDirect3DSurface9*>(ProxyBase::of(bb)->inner)));   // the Target binds the game's objects
    q->Release();rts->Release();bb->Release();rtt->Release();vb->Release();tex->Release();rig.finish();checkClean();gNormalize=false;
    return r;
}
static std::vector<std::string> withoutDropped(const std::vector<std::string>& t){std::vector<std::string> r;for(auto& s:t)if(!droppedBySkip(s)&&s!="STATE")r.push_back(s);return r;}
static void frameSkipping(){
    for(SkipCase c:{SkipCase::Plain,SkipCase::Event,SkipCase::Occlusion,SkipCase::RenderToTexture,SkipCase::CarriedTarget,SkipCase::BackBufferCopy}){
        const SkipRun ref=skipScript(0,c),run=skipScript(1,c);
        const bool skippable=c==SkipCase::Plain||c==SkipCase::Event;
        CHECK(ref.skipped==0&&ref.realPresents==6);   // StreamFrameSkip=0: every frame drawn and presented
        CHECK(run.skipped==(skippable?1u:0u)&&run.realPresents==(skippable?5u:6u));
        if(skippable){
            const unsigned frameDraws=c==SkipCase::Event?3u:2u;   // frame 1: the indexed draw, the UP draw (and the query's draw)
            CHECK(run.draws+frameDraws==ref.draws&&run.dropped==frameDraws+1);   // + its Clear
            CHECK(withoutDropped(run.trace)==withoutDropped(ref.trace));   // every other call: the same, in the same order, with the same bytes
        }else CHECK(run.trace==ref.trace&&run.dropped==0);   // drawn: exactly the reference
        CHECK(run.tex==ref.tex&&run.vb==ref.vb&&run.state==ref.state&&run.presents==ref.presents);   // resources, final state and Present results as the full replay
        for(int f=0;f<6;++f){CHECK(run.tex[std::size_t(f)*(16*4+16)]==0x10+f);CHECK(run.vb[std::size_t(f)*16]==0x40+f);}   // the skipped frame's uploads are there
        std::printf("frame skip case=%d skipped=%llu dropped=%llu draws=%u/%u presents=%u/%u\n",int(c),(unsigned long long)run.skipped,(unsigned long long)run.dropped,run.draws,ref.draws,run.realPresents,ref.realPresents);
    }
    {const SkipRun idle=skipScript(1,SkipCase::Plain,false);CHECK(idle.skipped==0&&idle.realPresents==6);}   // a replay that keeps up never skips
}
// The game thread's Present wait is released by a skipped Present: with the replay stuck in frame 0, a game thread records frames 1..4 (Present 3 waits
// for Present 0, Present 4 for Present 1); released, the replay skips frame 1 and the game finishes. The CSTREAM line counts it; the first skip is logged once.
static std::vector<std::string> gSkipLog;static std::mutex gSkipLogMutex;
static void frameSkipReleasesPresentWait(){
    gTrace.clear();gSkipLog.clear();StreamDevice::Options opt;opt.framesAhead=3;opt.frameSkip=1;opt.log=[](const char* l){std::lock_guard<std::mutex> g(gSkipLogMutex);gSkipLog.push_back(l);};
    Rig rig(true,opt);IDirect3DDevice9* d=rig.dev;d->SetRenderState((D3DRENDERSTATETYPE)7,1);rig.sync();   // frame 0 opened with nothing ahead of it: drawn
    gKnobs.hold.store(true);std::atomic<unsigned> returned{0};
    std::thread game([&]{for(int f=0;f<5;++f){d->BeginScene();d->DrawPrimitive((D3DPRIMITIVETYPE)4,0,2);d->EndScene();d->Present(nullptr,nullptr,nullptr,nullptr);returned.fetch_add(1);}});
    CHECK(waitFor([&]{return returned.load()==3;}));std::this_thread::sleep_for(std::chrono::milliseconds(30));CHECK(returned.load()==3);   // Present 3 waits for the stuck frame 0
    gKnobs.hold.store(false);game.join();rig.sync();
    auto& rp=rig.sd->replayerOf();CHECK(rp.skippedFrames()>=1&&rp.skippedFrames()<=2);   // frame 1 always (frames 1 and 2 were complete), frame 2 if Present 3 came first
    unsigned presents=0;for(auto& t:gTrace)if(t.rfind("Device::Present",0)==0)++presents;CHECK(presents+rp.skippedFrames()==5);
    {std::lock_guard<std::mutex> g(gSkipLogMutex);unsigned first=0;for(auto& l:gSkipLog)if(l.rfind("CSTREAM frame skip: first skipped frame=1 ",0)==0)++first;CHECK(first==1);}
    rig.finish();checkClean();
}
static void streamTests(bool threadsOnly){
    layoutIsolation();replayTimingAccounting();diagnosticsOffSkipsAudit();idlePollWakes();
    lifetimeAndIdentity();stateKnownUnknown();locksPreserveBytes();staticBufferShadows();dynamicBufferShadows();largeBufferAllowance();twoLargeBuffers();adaptiveShadowCap();shadowCap();queriesAndSyncCensus();resetAndShutdown();directReplayRaw();redundantFiltering();renderTargetResetsViewport();textureShadows();statsLine();childrenOutliveTheDevice();queryProbeAndDeadQuery();initFailureFallback();cursorHandling();nestedSyncInPump();testCooperativeLevelLocal(1);testCooperativeLevelLocal(3);upDrawsAndBackpressure();snapshotTriggers();snapshotPoolNotExhausted();memoryPressureRelease();impossibleBlockIsRefusedAtOnce();smallStagedLocksUseScratch();
    framesAheadPacing();textureShadowSpares();frameSkipping();frameSkipReleasesPresentWait();   // 0.3.200 (frame skip)
    equivalence(20000,12345);equivalence(20000,987654321);equivalence(20000,24680,2);equivalence(20000,13579,3);   // 0.3.200 (pipeline): 2 and 3 frames ahead
    (void)threadsOnly;
}
