#pragma once
// 0.3.192 (CS): the replay thread. It owns the Target (the real Device) after the handoff: it executes the queue in
// order (generated dispatch plus the hand-written commands below), runs the game thread's tasks and sync calls,
// polls pending queries, keeps the Present bookkeeping and stops on a Stop command. Every Target call the game made
// happens here, at the stream position the game made it, with every stream proxy translated to its inner object.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <thread>
#include <vector>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#include "stream_proxies.h"
#include "stream_state.h"
#include "snapshot_store.h"
#include "stream_hooks.h"

namespace NorthlightStream {
// ---- implicit proxies: objects the game never created (back buffers, the default depth buffer, objects the mod bound) ----
template<class T> inline T* StreamCore::toProxy(T* r){
    if(!r)return nullptr;
    return static_cast<T*>(static_cast<IUnknown*>(proxyFor(r,false)->unk));
}
template<class T> inline ProxyBase* StreamCore::proxyFor(T* r,bool bound){
    IUnknown* u=r;
    if(ProxyBase* b=reg.findInner(u)){   // already known: the proxy holds its own reference, drop the one the Target just added
        u->Release();
        if(!bound)b->comAddRef();   // bound: StreamState::bind adds the use itself
        return b;
    }
    ProxyBase* p=makeImplicit(u);
    if(!bound)return p;
    p->refs.store(0);p->use.store(0);   // no game reference and no use yet: StreamState::bind adds the bind
    return p;
}
inline ProxyBase* StreamCore::makeImplicit(IUnknown* u){
    ProxyBase* p=nullptr;
    // The kind comes from the object itself: QueryInterface on the Target-level pointer (no reference kept).
    auto has=[&](auto* tag)->bool{using I=std::remove_pointer_t<decltype(tag)>;void* out=nullptr;if(u->QueryInterface(__uuidof(I),&out)==S_OK&&out){static_cast<IUnknown*>(out)->Release();return true;}return false;};
    if(has(static_cast<IDirect3DSurface9*>(nullptr))){
        auto* s=static_cast<IDirect3DSurface9*>(u);D3DSURFACE_DESC d{};s->GetDesc(&d);
        Info i;i.w=d.Width;i.h=d.Height;i.fmt=unsigned(d.Format);i.usage=d.Usage;i.pool=unsigned(d.Pool);i.ms=unsigned(d.MultiSampleType);i.msq=d.MultiSampleQuality;
        p=new StreamSurface(this,i);static_cast<StreamSurface*>(p)->own.written=true;
    }else if(has(static_cast<IDirect3DVertexBuffer9*>(nullptr))){
        D3DVERTEXBUFFER_DESC d{};static_cast<IDirect3DVertexBuffer9*>(u)->GetDesc(&d);
        auto* v=new StreamVertexBuffer(this,d.Size,d.Usage,d.FVF,unsigned(d.Pool),false);v->buf.written=true;p=v;
    }else if(has(static_cast<IDirect3DIndexBuffer9*>(nullptr))){
        D3DINDEXBUFFER_DESC d{};static_cast<IDirect3DIndexBuffer9*>(u)->GetDesc(&d);
        auto* v=new StreamIndexBuffer(this,d.Size,d.Usage,unsigned(d.Format),unsigned(d.Pool),false);v->buf.written=true;p=v;
    }else if(has(static_cast<IDirect3DTexture9*>(nullptr))){
        auto* t=static_cast<IDirect3DTexture9*>(u);D3DSURFACE_DESC d{};t->GetLevelDesc(0,&d);
        auto* v=new StreamTexture(this,d.Width,d.Height,t->GetLevelCount(),d.Usage,unsigned(d.Format),unsigned(d.Pool));for(auto& s:v->subs)s.written=true;p=v;
    }else if(has(static_cast<IDirect3DCubeTexture9*>(nullptr))){
        auto* t=static_cast<IDirect3DCubeTexture9*>(u);D3DSURFACE_DESC d{};t->GetLevelDesc(0,&d);
        auto* v=new StreamCubeTexture(this,d.Width,t->GetLevelCount(),d.Usage,unsigned(d.Format),unsigned(d.Pool));for(auto& s:v->subs)s.written=true;p=v;
    }else if(has(static_cast<IDirect3DVolumeTexture9*>(nullptr))){
        auto* t=static_cast<IDirect3DVolumeTexture9*>(u);D3DVOLUME_DESC d{};t->GetLevelDesc(0,&d);
        auto* v=new StreamVolumeTexture(this,d.Width,d.Height,d.Depth,t->GetLevelCount(),d.Usage,unsigned(d.Format),unsigned(d.Pool));for(auto& s:v->subs)s.written=true;p=v;
    }else if(has(static_cast<IDirect3DVertexShader9*>(nullptr))){
        auto* v=new StreamVertexShader(this);UINT n=0;auto* s=static_cast<IDirect3DVertexShader9*>(u);
        if(SUCCEEDED(s->GetFunction(nullptr,&n))&&n){v->code.resize((n+3)/4);s->GetFunction(v->code.data(),&n);}p=v;
    }else if(has(static_cast<IDirect3DPixelShader9*>(nullptr))){
        auto* v=new StreamPixelShader(this);UINT n=0;auto* s=static_cast<IDirect3DPixelShader9*>(u);
        if(SUCCEEDED(s->GetFunction(nullptr,&n))&&n){v->code.resize((n+3)/4);s->GetFunction(v->code.data(),&n);}p=v;
    }else if(has(static_cast<IDirect3DVertexDeclaration9*>(nullptr))){
        auto* v=new StreamVertexDeclaration(this);UINT n=0;auto* s=static_cast<IDirect3DVertexDeclaration9*>(u);
        if(SUCCEEDED(s->GetDeclaration(nullptr,&n))&&n){v->elements.resize(n);s->GetDeclaration(v->elements.data(),&n);}p=v;
    }else{   // a swap chain (the only other object a Get returns)
        auto* v=new StreamSwapChain(this);static_cast<IDirect3DSwapChain9*>(u)->GetPresentParameters(&v->pp);p=v;
    }
    reg.bindInner(p,u);
    return p;
}

struct Translator {
    StreamCore& c;
    IDirect3DDevice9* device(){return c.target;}
    template<class T> T* inner(T* p){return c.inner(p);}
    template<class T> T* toProxy(T* p){return c.toProxy(p);}
    void result(Cmd id,HRESULT hr){if(FAILED(hr)&&cmdSetsState(id)){add(c.q.stats.replayFailures);c.replayFailure.store(true);}}
    void skipped(Cmd){add(c.q.stats.deadCreates);}
};

inline void StreamState::loadDefaults(IDirect3DDevice9* dev){
    StreamCore& c=*core;
    for(unsigned s=1;s<kRS;++s){DWORD v=0;if(SUCCEEDED(dev->GetRenderState((D3DRENDERSTATETYPE)s,&v)))rs[s].set(v);}
    for(unsigned idx=0;idx<kSamplers;++idx){
        const DWORD number=idx<16?idx:DWORD(256+(idx-16));
        for(unsigned t=1;t<14;++t){DWORD v=0;if(SUCCEEDED(dev->GetSamplerState(number,(D3DSAMPLERSTATETYPE)t,&v)))samp[idx][t].set(v);}
        bind(tex[idx],nullptr);texKnown[idx]=true;   // a fresh or reset device has nothing bound
    }
    for(unsigned st=0;st<kTSStages;++st)for(unsigned t=1;t<kTSTypes;++t){DWORD v=0;if(SUCCEEDED(dev->GetTextureStageState(st,(D3DTEXTURESTAGESTATETYPE)t,&v)))tss[st][t].set(v);}
    D3DVIEWPORT9 vp{};if(SUCCEEDED(dev->GetViewport(&vp)))viewport.set(vp);
    RECT sc{};if(SUCCEEDED(dev->GetScissorRect(&sc)))scissor.set(sc);
    DWORD f=0;if(SUCCEEDED(dev->GetFVF(&f)))fvf.set(f);
    swvp.set(dev->GetSoftwareVertexProcessing());npatch.set(dev->GetNPatchMode());
    for(unsigned t:{2u,3u,256u}){D3DMATRIX m{};if(SUCCEEDED(dev->GetTransform((D3DTRANSFORMSTATETYPE)t,&m)))xf[t].set(m);}
    for(auto& st:streams){bind(st.vb,nullptr);st.known=true;}
    bind(indices,nullptr);indicesKnown=true;bind(vs,nullptr);vsKnown=true;bind(ps,nullptr);psKnown=true;bind(decl,nullptr);declKnown=true;
    for(unsigned i=0;i<kRTs;++i){
        IDirect3DSurface9* s=nullptr;
        if(SUCCEEDED(dev->GetRenderTarget(i,&s))&&s){bind(rt[i],c.toProxyBound(s));}
        else bind(rt[i],nullptr);
        rtKnown[i]=true;
    }
    IDirect3DSurface9* z=nullptr;
    if(SUCCEEDED(dev->GetDepthStencilSurface(&z))&&z)bind(ds,c.toProxyBound(z));else bind(ds,nullptr);
    dsKnown=true;
}

// ---- hand-written commands ----
struct CreateArgs {ProxyBase* p;UINT v[8];UINT extraBytes;};   // v: the create call's scalar arguments in order; extra follows (bytecode, elements)
struct PresentArgs {ProxyBase* swapChain;HWND window;DWORD flags;UINT hasSrc,hasDst,dirtyBytes;RECT src,dst;};   // dirty region bytes follow
struct DrawUPArgs {UINT type,primCount,stride,vertexBytes,flat;const void* gameVertices;};                   // vertices follow (flat) or in the Block
struct DrawIUPArgs {UINT type,minIndex,numVertices,primCount,indexFormat,stride,indexBytes,vertexBytes;const void* gameVertices;};   // indices then vertices
struct StateBlockArgs {ProxyBase* p;UINT type;};

class Replayer {
public:
    StreamCore& core;
    SnapshotPool snapshots;
    std::function<void()> threadStart;                  // runs on the replay thread before the first command (owner handoff)
    std::function<void(const char*)> log;               // diagnostics line sink; may be empty
    bool (*diagnostics)()=nullptr;                      // Diagnostics on: the CSTREAM line and the sync-only audit (asked per frame)
    unsigned sampleEvery=600;
    std::atomic<std::uint64_t> busyNs{0},frameBusyNs{0};
    explicit Replayer(StreamCore& c):core(c){}
    ~Replayer(){join();}
    Replayer(const Replayer&)=delete;Replayer& operator=(const Replayer&)=delete;

    // Starts the thread; false (and no thread) when it cannot be created. The creating thread's x87/MXCSR control
    // state is copied: DXVK sets single precision on the CreateDevice thread, and hook float math must match.
    bool start(){
        try{
            unsigned short cw=0;unsigned csr=0;captureFpu(cw,csr);
            std::atomic<int> ready{0};const unsigned long creator=currentTid();
            th_=std::thread([this,cw,csr,creator,&ready]{
                applyFpu(cw,csr);if(threadStart)threadStart();replayTid.store(currentTid());
                const bool attached=attachInput(creator,true);   // see attachInput
                ready.store(1);loop();
                if(attached)attachInput(creator,false);});
            while(!ready.load())std::this_thread::yield();   // the handoff is complete before CreateDevice returns to the game
            return true;
        }catch(...){return false;}
    }
    // Stops after everything recorded so far has run. Game thread; call once.
    void stop(){
        if(!th_.joinable())return;
        core.q.reserve((std::uint16_t)Cmd::Stop,0);core.q.commit();core.q.publish();
        th_.join();
        if(current_){snapshots.release(current_);current_=nullptr;}
    }
    void join(){if(th_.joinable()){core.q.interrupt();stopNow_.store(true);th_.join();}}
    bool running()const{return th_.joinable();}
    // DXVK applies the hardware cursor (ShowCursor / SetCursorProperties) with ::SetCursor, which acts on the calling thread's
    // input queue. Attaching the replay thread to the game thread's queue makes it the game's cursor. Win32 only; logged if it fails.
    bool attachInput(unsigned long gameThread,bool on){
#ifdef _WIN32
        const BOOL ok=AttachThreadInput(DWORD(currentTid()),DWORD(gameThread),on?TRUE:FALSE);
        if(!ok&&log){char b[96];std::snprintf(b,sizeof b,"CSTREAM AttachThreadInput(%s) failed error=%lu",on?"attach":"detach",(unsigned long)GetLastError());log(b);}
        return ok!=FALSE;
#else
        (void)gameThread;(void)on;return false;
#endif
    }
    static unsigned long currentTid(){
#ifdef _WIN32
        return GetCurrentThreadId();
#else
        return (unsigned long)std::hash<std::thread::id>()(std::this_thread::get_id());
#endif
    }

private:
    std::thread th_;std::atomic<bool> stopNow_{false};
    std::vector<StreamQuery*> pending_;
    GameSnapshot* current_=nullptr;std::optional<ScopedPlayback> playback_;
    std::uint64_t draws_=0;
    // audit: the values the game last set this frame, replay side
    DWORD auditRS_[StreamState::kRS]={},auditSamp_[StreamState::kSamplers][StreamState::kSampTypes]={},auditTss_[StreamState::kTSStages][StreamState::kTSTypes]={};
    std::vector<unsigned> touched_;std::vector<bool> touchedFlag_=std::vector<bool>(StreamState::kBits,false);
    struct Avg {double depth=0,bytes=0;unsigned n=0;std::uint64_t maxDepth=0,maxBytes=0;} avg_;
    std::uint64_t lastBusy_=0,lastPass_=0;unsigned deadLogged_=0;

    static void captureFpu(unsigned short& cw,unsigned& csr){
        cw=0;csr=0;
#if defined(__i386__)||defined(__x86_64__)
        asm volatile("fnstcw %0":"=m"(cw));
#endif
#if defined(__SSE__)
        csr=_mm_getcsr();
#endif
    }
    static void applyFpu(unsigned short cw,unsigned csr){
#if defined(__i386__)||defined(__x86_64__)
        asm volatile("fldcw %0"::"m"(cw));
#endif
#if defined(__SSE__)
        _mm_setcsr(csr);
#endif
        (void)cw;(void)csr;
    }

    void note(unsigned bit){if(!touchedFlag_[bit]){touchedFlag_[bit]=true;touched_.push_back(bit);}}
    void pollQueries(){
        for(std::size_t i=0;i<pending_.size();){
            StreamQuery* q=pending_[i];bool done=false;
            if(q->dead.load()||!q->inner)done=true;
            else{
                unsigned char buf[64]={};add(core.q.stats.queryPolls);
                if(static_cast<IDirect3DQuery9*>(q->inner)->GetData(buf,q->dataSize,0)==S_OK){std::memcpy(q->result,buf,sizeof buf);q->resultSize=q->dataSize;q->readyGen.store(q->issueSeen,std::memory_order_release);done=true;}
            }
            if(done){pending_[i]=pending_.back();pending_.pop_back();}else ++i;
        }
    }
    void verify(ProxyBase* p){
        bool bad=false;
        switch(p->kind){
        case Kind::Texture:{auto* t=static_cast<IDirect3DTexture9*>(p->inner);D3DSURFACE_DESC d{};t->GetLevelDesc(0,&d);
            bad=d.Width!=p->info.w||d.Height!=p->info.h||unsigned(d.Format)!=p->info.fmt||(!(p->info.usage&D3::kUsageAutoGen)&&t->GetLevelCount()!=p->info.levels);break;}
        case Kind::Surface:{D3DSURFACE_DESC d{};static_cast<IDirect3DSurface9*>(p->inner)->GetDesc(&d);bad=d.Width!=p->info.w||d.Height!=p->info.h||unsigned(d.Format)!=p->info.fmt;break;}
        case Kind::VertexBuffer:{D3DVERTEXBUFFER_DESC d{};static_cast<IDirect3DVertexBuffer9*>(p->inner)->GetDesc(&d);bad=d.Size!=p->info.length||d.FVF!=p->info.fvf;break;}
        case Kind::IndexBuffer:{D3DINDEXBUFFER_DESC d{};static_cast<IDirect3DIndexBuffer9*>(p->inner)->GetDesc(&d);bad=d.Size!=p->info.length;break;}
        default:break;
        }
        if(bad){add(core.q.stats.proxyMismatch);if(log&&deadLogged_<8){++deadLogged_;log("CSTREAM proxy answer differs from the real object (creation arguments)");}}
    }
    HRESULT create(const CreateArgs& a,Cmd id,const void* extra){
        StreamCore& c=core;IDirect3DDevice9* dev=c.target;ProxyBase* p=a.p;IUnknown* made=nullptr;HRESULT hr=D3DERR_INVALIDCALL;const UINT* v=a.v;
        switch(id){
        case Cmd::CreateTexture:{IDirect3DTexture9* o=nullptr;hr=dev->CreateTexture(v[0],v[1],v[2],v[3],(D3DFORMAT)v[4],(D3DPOOL)v[5],&o,nullptr);made=o;break;}
        case Cmd::CreateVolumeTexture:{IDirect3DVolumeTexture9* o=nullptr;hr=dev->CreateVolumeTexture(v[0],v[1],v[2],v[3],v[4],(D3DFORMAT)v[5],(D3DPOOL)v[6],&o,nullptr);made=o;break;}
        case Cmd::CreateCubeTexture:{IDirect3DCubeTexture9* o=nullptr;hr=dev->CreateCubeTexture(v[0],v[1],v[2],(D3DFORMAT)v[3],(D3DPOOL)v[4],&o,nullptr);made=o;break;}
        case Cmd::CreateVertexBuffer:{IDirect3DVertexBuffer9* o=nullptr;hr=dev->CreateVertexBuffer(v[0],v[1],v[2],(D3DPOOL)v[3],&o,nullptr);made=o;break;}
        case Cmd::CreateIndexBuffer:{IDirect3DIndexBuffer9* o=nullptr;hr=dev->CreateIndexBuffer(v[0],v[1],(D3DFORMAT)v[2],(D3DPOOL)v[3],&o,nullptr);made=o;break;}
        case Cmd::CreateRenderTarget:{IDirect3DSurface9* o=nullptr;hr=dev->CreateRenderTarget(v[0],v[1],(D3DFORMAT)v[2],(D3DMULTISAMPLE_TYPE)v[3],v[4],v[5],&o,nullptr);made=o;break;}
        case Cmd::CreateDepthStencilSurface:{IDirect3DSurface9* o=nullptr;hr=dev->CreateDepthStencilSurface(v[0],v[1],(D3DFORMAT)v[2],(D3DMULTISAMPLE_TYPE)v[3],v[4],v[5],&o,nullptr);made=o;break;}
        case Cmd::CreateOffscreenPlainSurface:{IDirect3DSurface9* o=nullptr;hr=dev->CreateOffscreenPlainSurface(v[0],v[1],(D3DFORMAT)v[2],(D3DPOOL)v[3],&o,nullptr);made=o;break;}
        case Cmd::CreateVertexDeclaration:{IDirect3DVertexDeclaration9* o=nullptr;hr=dev->CreateVertexDeclaration(static_cast<const D3DVERTEXELEMENT9*>(extra),&o);made=o;break;}
        case Cmd::CreateVertexShader:{IDirect3DVertexShader9* o=nullptr;hr=dev->CreateVertexShader(static_cast<const DWORD*>(extra),&o);made=o;break;}
        case Cmd::CreatePixelShader:{IDirect3DPixelShader9* o=nullptr;hr=dev->CreatePixelShader(static_cast<const DWORD*>(extra),&o);made=o;break;}
        case Cmd::CreateQuery:{IDirect3DQuery9* o=nullptr;hr=dev->CreateQuery((D3DQUERYTYPE)v[0],&o);made=o;break;}
        case Cmd::CreateStateBlock:{IDirect3DStateBlock9* o=nullptr;hr=dev->CreateStateBlock((D3DSTATEBLOCKTYPE)v[0],&o);made=o;break;}
        case Cmd::EndStateBlock:{IDirect3DStateBlock9* o=nullptr;hr=dev->EndStateBlock(&o);made=o;break;}
        default:break;
        }
        if(SUCCEEDED(hr)&&made){c.reg.bindInner(p,made);verify(p);}
        else{
            p->dead.store(true);add(c.q.stats.deadCreates);add(c.q.stats.createFailures);
            if(log&&deadLogged_<8){++deadLogged_;char b[128];std::snprintf(b,sizeof b,"CSTREAM dead create %s hr=0x%08lx",cmdName(id),(unsigned long)hr);log(b);}
        }
        return hr;
    }
    void unlockImage(const CommandHeader* h){
        const auto* a=reinterpret_cast<const UnlockImageArgs*>(Queue::payload(h));Block* b=Queue::blockOf(h);ProxyBase* p=a->proxy;
        if(!p->inner||p->dead.load()||!b){add(core.q.stats.replayFailures);return;}
        D3DLOCKED_RECT lr{};D3DLOCKED_BOX lb{};RECT rect=RECT{a->l,a->t,a->r,a->b};D3DBOX box{};box.Left=UINT(a->l);box.Top=UINT(a->t);box.Right=UINT(a->r);box.Bottom=UINT(a->b);box.Front=a->bf;box.Back=a->bk;
        const bool volume=a->route==RouteVolume||a->route==RouteVolumeTexture;
        if(FAILED(callLock(a->route,p->inner,a->level,a->face,&lr,&lb,a->hasRect&&!volume?&rect:nullptr,a->hasRect&&volume?&box:nullptr,a->flags))){add(core.q.stats.replayFailures);return;}
        auto* dst=static_cast<unsigned char*>(volume?lb.pBits:lr.pBits);const INT rowPitch=volume?lb.RowPitch:lr.Pitch;const INT slicePitch=volume?lb.SlicePitch:0;
        const unsigned char* src=b->data();
        if(dst)for(UINT s=0;s<a->slices;++s)for(UINT r=0;r<a->rows;++r)
            std::memcpy(dst+std::ptrdiff_t(s)*slicePitch+std::ptrdiff_t(r)*rowPitch,src+std::size_t(s)*a->slicePitch+std::size_t(r)*a->pitch,a->rowBytes);
        callUnlock(a->route,p->inner,a->level,a->face);
    }
    void unlockBuffer(const CommandHeader* h){
        const auto* a=reinterpret_cast<const UnlockBufferArgs*>(Queue::payload(h));ProxyBase* p=a->proxy;
        const unsigned char* data=a->inlineData?reinterpret_cast<const unsigned char*>(a+1):Queue::blockOf(h)->data();
        if(!p->inner||p->dead.load()){add(core.q.stats.replayFailures);return;}
        void* dst=nullptr;const DWORD flags=a->flags&~D3::kLockReadOnly;
        const HRESULT hr=p->kind==Kind::VertexBuffer?static_cast<IDirect3DVertexBuffer9*>(p->inner)->Lock(a->off,a->size,&dst,flags):static_cast<IDirect3DIndexBuffer9*>(p->inner)->Lock(a->off,a->size,&dst,flags);
        if(FAILED(hr)||!dst){add(core.q.stats.replayFailures);return;}
        std::memcpy(dst,data,a->size);
        if(p->kind==Kind::VertexBuffer)static_cast<IDirect3DVertexBuffer9*>(p->inner)->Unlock();else static_cast<IDirect3DIndexBuffer9*>(p->inner)->Unlock();
    }
    void derive(const DeriveArgs& a){
        IUnknown* got=nullptr;HRESULT hr=D3DERR_INVALIDCALL;IUnknown* in=a.parent->inner;
        if(in&&!a.parent->dead.load())switch(a.op){
        case DeriveSurfaceLevel:{IDirect3DSurface9* o=nullptr;hr=static_cast<IDirect3DTexture9*>(in)->GetSurfaceLevel(a.a,&o);got=o;break;}
        case DeriveCubeFace:{IDirect3DSurface9* o=nullptr;hr=static_cast<IDirect3DCubeTexture9*>(in)->GetCubeMapSurface((D3DCUBEMAP_FACES)a.a,a.b,&o);got=o;break;}
        case DeriveVolumeLevel:{IDirect3DVolume9* o=nullptr;hr=static_cast<IDirect3DVolumeTexture9*>(in)->GetVolumeLevel(a.a,&o);got=o;break;}
        default:{IDirect3DSurface9* o=nullptr;hr=static_cast<IDirect3DSwapChain9*>(in)->GetBackBuffer(a.a,(D3DBACKBUFFER_TYPE)a.b,&o);got=o;break;}
        }
        if(SUCCEEDED(hr)&&got)core.reg.bindInner(a.child,got);
        else{a.child->dead.store(true);add(core.q.stats.deadCreates);}
    }
    void destroy(ProxyBase* p){
        if(p->pendingDestroy.fetch_sub(1)!=1||p->use.load()>0)return;   // a later Destroy is queued, or the proxy was handed out again
        core.reg.erase(p);
        for(ProxyBase* k:p->kids)if(k){core.reg.erase(k);if(k->inner)k->inner->Release();delete k;}
        if(p->inner)p->inner->Release();
        if(p->kind==Kind::Query){auto it=std::find(pending_.begin(),pending_.end(),static_cast<StreamQuery*>(static_cast<ProxyBase*>(p)));if(it!=pending_.end())pending_.erase(it);}
        delete p;
    }
    static unsigned primVertices(unsigned type,unsigned n){switch(type){case 1:return n;case 2:return n*2;case 3:return n+1;case 4:return n*3;default:return n+2;}}
    void present(const CommandHeader* h,bool swap){
        const auto* a=reinterpret_cast<const PresentArgs*>(Queue::payload(h));
        const RGNDATA* dirty=a->dirtyBytes?reinterpret_cast<const RGNDATA*>(a+1):nullptr;
        HRESULT hr;
        if(swap){IDirect3DSwapChain9* s=a->swapChain->dead.load()?nullptr:static_cast<IDirect3DSwapChain9*>(a->swapChain->inner);
                 hr=s?s->Present(a->hasSrc?&a->src:nullptr,a->hasDst?&a->dst:nullptr,a->window,dirty,a->flags):D3DERR_INVALIDCALL;}
        else hr=core.target->Present(a->hasSrc?&a->src:nullptr,a->hasDst?&a->dst:nullptr,a->window,dirty);
        core.presentResult.store(hr);{const std::uint64_t seq=core.q.replayedSeq()+1;auto& e=core.presentRing[seq%StreamCore::kRing];e.hr.store(hr);e.seq.store(seq);}
        endFrame();
    }
    void endFrame(){
        core.availableTextureMem.store(core.target->GetAvailableTextureMem());
        pollQueries();
        const std::uint64_t frames=core.framesReplayed.fetch_add(1)+1;
        const std::uint64_t depth=core.q.depth(),bytes=get(core.q.stats.highWaterBytes);
        avg_.depth+=double(depth);avg_.bytes+=double(bytes);++avg_.n;if(depth>avg_.maxDepth)avg_.maxDepth=depth;if(bytes>avg_.maxBytes)avg_.maxBytes=bytes;
        const bool diag=diagnostics&&diagnostics();
        if(diag&&frames%sampleEvery==1)runAudit();
        if(diag&&log&&frames%sampleEvery==0)cstreamLine(frames);
        touched_.clear();std::fill(touchedFlag_.begin(),touchedFlag_.end(),false);
    }
    // Scalar slots set this frame must read back from the Target as set; a mismatch (a backend that normalizes) makes the slot sync-only for good.
    void runAudit(){
        IDirect3DDevice9* dev=core.target;
        for(unsigned bit:touched_){
            DWORD got=0,want=0;bool ok=false;
            if(bit<StreamState::kRS){ok=SUCCEEDED(dev->GetRenderState((D3DRENDERSTATETYPE)bit,&got));want=auditRS_[bit];}
            else if(bit<StreamState::kRS+StreamState::kSamplers*StreamState::kSampTypes){
                const unsigned k=bit-StreamState::kRS,idx=k/StreamState::kSampTypes,t=k%StreamState::kSampTypes;
                ok=SUCCEEDED(dev->GetSamplerState(idx<16?idx:256+(idx-16),(D3DSAMPLERSTATETYPE)t,&got));want=auditSamp_[idx][t];}
            else{const unsigned k=bit-StreamState::kRS-StreamState::kSamplers*StreamState::kSampTypes,st=k/StreamState::kTSTypes,t=k%StreamState::kTSTypes;
                ok=SUCCEEDED(dev->GetTextureStageState(st,(D3DTEXTURESTAGESTATETYPE)t,&got));want=auditTss_[st][t];}
            if(ok&&got!=want&&!(core.syncOnly[bit/64].load()&(1ull<<(bit%64)))){
                core.syncOnly[bit/64].fetch_or(1ull<<(bit%64));add(core.q.stats.syncOnlySlots);
                if(log){char b[128];std::snprintf(b,sizeof b,"CSTREAM sync-only slot=%u set=%lu read=%lu",bit,(unsigned long)want,(unsigned long)got);log(b);}
            }
        }
    }
    void cstreamLine(std::uint64_t frames){
        const Counters& s=core.q.stats;char buf[1400];int n=formatCounters(buf,sizeof buf,s);
        const double inv=avg_.n?1.0/avg_.n:0.0;
        const std::uint64_t busy=busyNs.load(),dBusy=busy-lastBusy_;lastBusy_=busy;
        n+=std::snprintf(buf+n,sizeof buf-size_t(n)," frames=%llu depthAvg=%.0f depthMax=%llu bytesAvg=%.0f bytesMax=%llu replayBusyMs/frame=%.3f dead=%llu answered=%llu synced=%llu syncOnly=%llu snap=%llu/%llu/%llu/%llu",
            (unsigned long long)frames,avg_.depth*inv,(unsigned long long)avg_.maxDepth,avg_.bytes*inv,(unsigned long long)avg_.maxBytes,sampleEvery?dBusy/1e6/sampleEvery:0.0,
            (unsigned long long)get(s.deadCreates),(unsigned long long)get(s.stateAnswered),(unsigned long long)get(s.stateSynced),(unsigned long long)get(s.syncOnlySlots),
            SnapshotStats::hits.load(),SnapshotStats::misses.load(),SnapshotStats::triggers.load(),SnapshotStats::overflow.load());
        std::uint64_t pass=0;for(unsigned r=0;r<Counters::kPassReasons;++r)pass+=get(s.passThrough[r]);
        const std::uint64_t dPass=pass-lastPass_;lastPass_=pass;
        n+=std::snprintf(buf+n,sizeof buf-size_t(n)," shadowRefused=%llu/%.1fMB shadowLate=%llu passPerFrame=%.2f",(unsigned long long)get(s.shadowRefused),get(s.shadowRefusedBytes)/1048576.0,(unsigned long long)get(s.shadowLate),sampleEvery?double(dPass)/sampleEvery:0.0);
        n+=std::snprintf(buf+n,sizeof buf-size_t(n)," pass[");
        for(unsigned r=0;r<Counters::kPassReasons;++r)n+=std::snprintf(buf+n,sizeof buf-size_t(n),"%s%s=%llu",r?",":"",passReasonName(r),(unsigned long long)get(s.passThrough[r]));
        n+=std::snprintf(buf+n,sizeof buf-size_t(n),"] census[");
        std::vector<std::pair<std::uint64_t,unsigned>> top;
        for(unsigned i=0;i<(unsigned)Cmd::Count;++i)if(get(s.census[i]))top.push_back({get(s.census[i]),i});
        std::sort(top.rbegin(),top.rend());
        for(std::size_t i=0;i<top.size()&&i<8;++i)n+=std::snprintf(buf+n,sizeof buf-size_t(n),"%s%s=%llu",i?",":"",cmdName((Cmd)top[i].second),(unsigned long long)top[i].first);
        std::snprintf(buf+n,sizeof buf-size_t(n),"]");
        log(buf);avg_=Avg();
    }
    void activateSnapshot(GameSnapshot* s){
        playback_.reset();
        if(current_)snapshots.release(current_);
        current_=s;if(s)playback_.emplace(*s);
    }
    void execute(const CommandHeader* h){
        Translator tr{core};
        if(dispatchGenerated(h,tr)){
            if(h->id==(std::uint16_t)Cmd::Device_SetRenderState){const auto* a=reinterpret_cast<const Args_Device_SetRenderState*>(h+1);if(unsigned(a->State)<StreamState::kRS){auditRS_[a->State]=a->Value;note(unsigned(a->State));}}
            else if(h->id==(std::uint16_t)Cmd::Device_SetSamplerState){const auto* a=reinterpret_cast<const Args_Device_SetSamplerState*>(h+1);unsigned idx;
                if(StreamState::sampIndex(a->Sampler,idx)&&unsigned(a->Type)<StreamState::kSampTypes){auditSamp_[idx][a->Type]=a->Value;note(StreamState::bitSamp(idx,unsigned(a->Type)));}}
            else if(h->id==(std::uint16_t)Cmd::Device_SetTextureStageState){const auto* a=reinterpret_cast<const Args_Device_SetTextureStageState*>(h+1);
                if(a->Stage<StreamState::kTSStages&&unsigned(a->Type)<StreamState::kTSTypes){auditTss_[a->Stage][a->Type]=a->Value;note(StreamState::bitTss(a->Stage,unsigned(a->Type)));}}
            else if(h->id==(std::uint16_t)Cmd::Query_Issue){const auto* a=reinterpret_cast<const Args_Query_Issue*>(h+1);
                auto* q=static_cast<StreamQuery*>(a->self);
                if((a->dwIssueFlags&D3::kIssueEnd)&&!q->dead.load()){++q->issueSeen;if(std::find(pending_.begin(),pending_.end(),q)==pending_.end())pending_.push_back(q);}}
            else if(h->id==(std::uint16_t)Cmd::Device_DrawPrimitive||h->id==(std::uint16_t)Cmd::Device_DrawIndexedPrimitive){
                ++draws_;if(current_&&(draws_&31)==0){SnapshotStats::ageDraws.fetch_add(draws_>=current_->triggerDraw?draws_-current_->triggerDraw:0,std::memory_order_relaxed);SnapshotStats::ageSamples.fetch_add(1,std::memory_order_relaxed);}}   // game_snapshot.h noteAge, sampled
            return;
        }
        switch((Cmd)h->id){
        case Cmd::Sync:{SyncCall* sc;std::memcpy(&sc,h+1,sizeof sc);if(!executeSync(*sc,tr))sc->done.store(2);return;}
        case Cmd::Quiesce:{Task* t;std::memcpy(&t,h+1,sizeof t);t->fn(t->arg,core);return;}
        case Cmd::Destroy:{ProxyBase* p;std::memcpy(&p,h+1,sizeof p);destroy(p);return;}
        case Cmd::Derive:derive(*reinterpret_cast<const DeriveArgs*>(h+1));return;
        case Cmd::Snapshot:{GameSnapshot* s;std::memcpy(&s,h+1,sizeof s);activateSnapshot(s);return;}
        case Cmd::Present:present(h,false);return;
        case Cmd::SwapPresent:present(h,true);return;
        case Cmd::BeginStateBlock:core.replayFailureOnFail(core.target->BeginStateBlock());return;
        case Cmd::UnlockBuffer:unlockBuffer(h);return;
        case Cmd::UnlockRect:case Cmd::UnlockBox:unlockImage(h);return;
        case Cmd::DrawPrimitiveUP:{
            const auto* a=reinterpret_cast<const DrawUPArgs*>(Queue::payload(h));const void* data=a->flat?static_cast<const void*>(a+1):Queue::blockOf(h)->data();
            ++draws_;upIdentity=a->gameVertices;core.target->DrawPrimitiveUP((D3DPRIMITIVETYPE)a->type,a->primCount,data,a->stride);upIdentity=nullptr;return;}
        case Cmd::DrawIndexedPrimitiveUP:{
            const auto* a=reinterpret_cast<const DrawIUPArgs*>(Queue::payload(h));const unsigned char* base=(h->flags&kFlagBlock)?Queue::blockOf(h)->data():reinterpret_cast<const unsigned char*>(a+1);
            ++draws_;upIdentity=a->gameVertices;core.target->DrawIndexedPrimitiveUP((D3DPRIMITIVETYPE)a->type,a->minIndex,a->numVertices,a->primCount,base,(D3DFORMAT)a->indexFormat,base+((a->indexBytes+7u)&~7u),a->stride);upIdentity=nullptr;return;}
        case Cmd::CreateTexture:case Cmd::CreateVolumeTexture:case Cmd::CreateCubeTexture:case Cmd::CreateVertexBuffer:case Cmd::CreateIndexBuffer:
        case Cmd::CreateRenderTarget:case Cmd::CreateDepthStencilSurface:case Cmd::CreateOffscreenPlainSurface:case Cmd::CreateVertexDeclaration:
        case Cmd::CreateVertexShader:case Cmd::CreatePixelShader:case Cmd::CreateQuery:case Cmd::CreateStateBlock:case Cmd::EndStateBlock:{
            const auto* a=reinterpret_cast<const CreateArgs*>(h+1);create(*a,(Cmd)h->id,a+1);return;}
        case Cmd::Nop:return;
        default:add(core.q.stats.replayFailures);return;
        }
    }
    void loop(){
        Queue& q=core.q;
        for(;;){
            const CommandHeader* h=q.next(false);
            if(!h){
                if(!pending_.empty()){pollQueries();std::this_thread::sleep_for(std::chrono::microseconds(50));continue;}
                h=q.next(true);if(!h){if(stopNow_.load())return;continue;}
            }
            const std::uint64_t t0=nowNs();
            if(h->id==(std::uint16_t)Cmd::Stop){q.retire(h);return;}
            execute(h);
            q.retire(h);
            busyNs.fetch_add(nowNs()-t0,std::memory_order_relaxed);
        }
    }
public:
    // Game thread, creating a proxy synchronously (memory pressure, shared handles): the same create the queue would run.
    HRESULT createNow(const CreateArgs& a,Cmd id,const void* extra){return create(a,id,extra);}
};
}
