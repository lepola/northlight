#pragma once
// 0.3.192 (CS): StreamDevice, the IDirect3DDevice9 the game sees while the command stream runs. Game thread only:
// every call is recorded (record/state), answered from StreamState or the creation arguments (get/local), or made
// synchronously (sync). The replay thread executes the queue against the Target (the unchanged Device). The generated
// NORTHLIGHT_STREAM_DEVICE_METHODS cover every method of those classes; the `custom` ones are written below.
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "replay_thread.h"
#include "game_snapshot.h"
#include "shader_tags.h"

namespace NorthlightStream {
// The stream the process runs, for the opaque-pointer hook (celestial identities the game stored in its own memory).
inline std::atomic<StreamCore*> activeCore{nullptr};
inline const void* innerOfActive(const void* p){StreamCore* c=activeCore.load(std::memory_order_acquire);return c?c->reg.innerOf(p):p;}

class StreamDevice final:public IDirect3DDevice9 {
public:
    struct Options {
        std::size_t budget=BudgetBytes;
        bool (*capture)(GameSnapshot&,Trigger,std::uint64_t)=nullptr;   // game_snapshot.h capture() in the DLL; null = no snapshots
        std::function<void()> threadStart;                              // runs on the replay thread first (owner handoff)
        std::function<void(const char*)> log;
        bool (*diagnostics)()=nullptr;                                  // NorthlightDiagnostics::enabled in the DLL
    };
    // Builds the stream around `target` (the Device). On success the replay thread owns the target; on failure nothing
    // was handed over (nullptr, reason filled) and the caller keeps using the target directly.
    static StreamDevice* make(IDirect3DDevice9* target,IDirect3D9* parent,const D3DPRESENT_PARAMETERS* pp,Options opt,const char** reason){
        std::unique_ptr<StreamDevice> d;
        try{d.reset(new StreamDevice(target,parent,pp,std::move(opt)));}catch(...){if(reason)*reason="allocation";return nullptr;}
        if(!d->replayer.start()){if(reason)*reason="thread";return nullptr;}
        bool ok=false;
        const bool ran=runTask(d->core,[&](StreamCore& c){
            D3DDEVICE_CREATION_PARAMETERS cp{};c.target->GetCreationParameters(&cp);d->creation=cp;c.target->GetDeviceCaps(&d->caps);
            c.availableTextureMem.store(c.target->GetAvailableTextureMem());
            IDirect3DSwapChain9* sc=nullptr;
            if(SUCCEEDED(c.target->GetSwapChain(0,&sc))&&sc){c.reg.bindInner(d->sc0,sc);sc->GetPresentParameters(&d->sc0->pp);d->pp=d->sc0->pp;d->ensureBackBuffers(c);}
            else d->sc0->dead.store(true);
            d->st.loadDefaults(c.target);ok=!d->sc0->dead.load();},Cmd::SyncInit);
        if(!ran||!ok){d->replayer.stop();if(reason)*reason="init";d->replayer.join();d->abandon();return nullptr;}
        gameTid.store(Replayer::currentTid());activeCore.store(&d->core,std::memory_order_release);innerOf=&innerOfActive;
        return d.release();
    }
    ~StreamDevice(){}

    NORTHLIGHT_STREAM_DEVICE_METHODS

    // ---- hooks the generated bodies call ----
    Queue& streamQueue(){if(foreignPending.load(std::memory_order_relaxed))drainForeign();return core.q;}
    template<Cmd C,class... A> void observe(CmdTag<C>,A&&...){}
    template<Cmd C,class... A> typename MethodTraits<C>::Ret syncCall(CmdTag<C> t,A... a){noteSync(t,a...);return runSync(streamQueue(),t,a...);}
    template<Cmd C,class... A> typename MethodTraits<C>::Ret syncGet(CmdTag<C> t,A... a){add(core.q.stats.stateSynced);return runSync(streamQueue(),t,a...);}

    // state class: StreamState follows the game's calls (not while a state block records)
    void observe(CmdTag<Cmd::Device_SetRenderTarget>,DWORD i,IDirect3DSurface9* s){if(recording||i>=StreamState::kRTs)return;StreamState::bind(st.rt[i],ProxyBase::of(s));st.rtKnown[i]=true;}
    void observe(CmdTag<Cmd::Device_SetDepthStencilSurface>,IDirect3DSurface9* s){if(recording)return;StreamState::bind(st.ds,ProxyBase::of(s));st.dsKnown=true;}
    void observe(CmdTag<Cmd::Device_SetTransform>,D3DTRANSFORMSTATETYPE s,const D3DMATRIX* m){if(recording||unsigned(s)>=StreamState::kXforms)return;if(m)st.xf[s].set(*m);else st.xf[s].known=false;}
    void observe(CmdTag<Cmd::Device_MultiplyTransform>,D3DTRANSFORMSTATETYPE s,const D3DMATRIX*){if(!recording&&unsigned(s)<StreamState::kXforms)st.xf[s].known=false;}
    void observe(CmdTag<Cmd::Device_SetViewport>,const D3DVIEWPORT9* v){if(recording)return;if(v)st.viewport.set(*v);else st.viewport.known=false;}
    void observe(CmdTag<Cmd::Device_SetMaterial>,const D3DMATERIAL9* m){if(recording)return;if(m)st.material.set(*m);else st.material.known=false;}
    void observe(CmdTag<Cmd::Device_SetLight>,DWORD i,const D3DLIGHT9* l){if(!recording&&l)st.setLight(i,*l);}
    void observe(CmdTag<Cmd::Device_LightEnable>,DWORD i,WINBOOL e){if(!recording)st.enableLight(i,e);}
    void observe(CmdTag<Cmd::Device_SetClipPlane>,DWORD i,const float* p){if(recording||i>=StreamState::kClip||!p)return;st.clip[i].known=true;std::memcpy(st.clip[i].p,p,16);}
    void observe(CmdTag<Cmd::Device_SetRenderState>,D3DRENDERSTATETYPE s,DWORD v){if(!recording&&unsigned(s)<StreamState::kRS)st.rs[s].set(v);}
    void observe(CmdTag<Cmd::Device_SetTexture>,DWORD stage,IDirect3DBaseTexture9* t){
        unsigned idx;if(recording||!StreamState::sampIndex(stage,idx))return;StreamState::bind(st.tex[idx],ProxyBase::of(t));st.texKnown[idx]=true;}
    void observe(CmdTag<Cmd::Device_SetTextureStageState>,DWORD stage,D3DTEXTURESTAGESTATETYPE t,DWORD v){if(!recording&&stage<StreamState::kTSStages&&unsigned(t)<StreamState::kTSTypes)st.tss[stage][t].set(v);}
    void observe(CmdTag<Cmd::Device_SetSamplerState>,DWORD s,D3DSAMPLERSTATETYPE t,DWORD v){unsigned idx;if(!recording&&StreamState::sampIndex(s,idx)&&unsigned(t)<StreamState::kSampTypes)st.samp[idx][t].set(v);}
    void observe(CmdTag<Cmd::Device_SetCurrentTexturePalette>,UINT n){if(!recording)st.palette.set(n);}
    void observe(CmdTag<Cmd::Device_SetScissorRect>,const RECT* r){if(recording)return;if(r)st.scissor.set(*r);else st.scissor.known=false;}
    void observe(CmdTag<Cmd::Device_SetSoftwareVertexProcessing>,WINBOOL b){if(!recording)st.swvp.set(b);}
    void observe(CmdTag<Cmd::Device_SetNPatchMode>,float n){if(!recording)st.npatch.set(n);}
    void observe(CmdTag<Cmd::Device_SetVertexDeclaration>,IDirect3DVertexDeclaration9* d){if(recording)return;StreamState::bind(st.decl,ProxyBase::of(d));st.declKnown=true;st.fvf.known=false;}
    void observe(CmdTag<Cmd::Device_SetFVF>,DWORD f){if(recording)return;st.fvf.set(f);st.declKnown=false;}   // the Target builds an implicit declaration: a Get of it syncs
    void observe(CmdTag<Cmd::Device_SetVertexShader>,IDirect3DVertexShader9* s){if(recording)return;StreamState::bind(st.vs,ProxyBase::of(s));st.vsKnown=true;}
    void observe(CmdTag<Cmd::Device_SetPixelShader>,IDirect3DPixelShader9* s){if(recording)return;StreamState::bind(st.ps,ProxyBase::of(s));st.psKnown=true;}
    void observe(CmdTag<Cmd::Device_SetVertexShaderConstantF>,UINT r,const float* d,UINT n){constF(st.vsF,r,d,n);}
    void observe(CmdTag<Cmd::Device_SetPixelShaderConstantF>,UINT r,const float* d,UINT n){constF(st.psF,r,d,n);}
    void observe(CmdTag<Cmd::Device_SetVertexShaderConstantI>,UINT r,const int* d,UINT n){constI(st.vsI,r,d,n);}
    void observe(CmdTag<Cmd::Device_SetPixelShaderConstantI>,UINT r,const int* d,UINT n){constI(st.psI,r,d,n);}
    void observe(CmdTag<Cmd::Device_SetVertexShaderConstantB>,UINT r,const WINBOOL* d,UINT n){constB(st.vsB,r,d,n);}
    void observe(CmdTag<Cmd::Device_SetPixelShaderConstantB>,UINT r,const WINBOOL* d,UINT n){constB(st.psB,r,d,n);}
    void observe(CmdTag<Cmd::Device_SetStreamSource>,UINT i,IDirect3DVertexBuffer9* vb,UINT off,UINT stride){
        if(recording||i>=StreamState::kStreams)return;auto& s=st.streams[i];StreamState::bind(s.vb,ProxyBase::of(vb));s.offset=off;s.stride=stride;s.known=true;}
    void observe(CmdTag<Cmd::Device_SetStreamSourceFreq>,UINT i,UINT f){if(!recording&&i<StreamState::kStreams)st.streams[i].freq.set(f);}
    void observe(CmdTag<Cmd::Device_SetIndices>,IDirect3DIndexBuffer9* ib){if(recording)return;StreamState::bind(st.indices,ProxyBase::of(ib));st.indicesKnown=true;}
    void observe(CmdTag<Cmd::Device_DrawPrimitive>,D3DPRIMITIVETYPE,UINT,UINT n){onDraw(n);}
    void observe(CmdTag<Cmd::Device_DrawIndexedPrimitive>,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT n){onDraw(n);}
    // GPU-side writes: the destination's CPU-side lock knowledge no longer holds
    void observe(CmdTag<Cmd::Device_UpdateSurface>,IDirect3DSurface9*,const RECT*,IDirect3DSurface9* dst,const POINT*){written(ProxyBase::of(dst));}
    void observe(CmdTag<Cmd::Device_UpdateTexture>,IDirect3DBaseTexture9*,IDirect3DBaseTexture9* dst){written(ProxyBase::of(dst));}
    void observe(CmdTag<Cmd::Device_StretchRect>,IDirect3DSurface9*,const RECT*,IDirect3DSurface9* dst,const RECT*,D3DTEXTUREFILTERTYPE){written(ProxyBase::of(dst));}
    void observe(CmdTag<Cmd::Device_ColorFill>,IDirect3DSurface9* s,const RECT*,D3DCOLOR){written(ProxyBase::of(s));}
    void observe(CmdTag<Cmd::Device_ProcessVertices>,UINT,UINT,UINT,IDirect3DVertexBuffer9* dst,IDirect3DVertexDeclaration9*,DWORD){written(ProxyBase::of(dst));}
    template<class... A> void noteSync(A&&...){}
    void noteSync(CmdTag<Cmd::Device_GetRenderTargetData>,IDirect3DSurface9*,IDirect3DSurface9* dst){written(ProxyBase::of(dst));}
    void noteSync(CmdTag<Cmd::Device_GetFrontBufferData>,UINT,IDirect3DSurface9* dst){written(ProxyBase::of(dst));}

    // get class: answered from StreamState when the slot is known (and not sync-only); false = sync call
    bool begin(){streamQueue().publish();if(core.replayFailure.exchange(false)){st.invalidate();add(core.q.stats.replayFailures);}return true;}
    bool hit(){add(core.q.stats.stateAnswered);return true;}
    template<class I> static void give(ProxyBase* p,I** out){if(!p){*out=nullptr;return;}p->comAddRef();*out=static_cast<I*>(p->unk);}
    bool syncOnly(unsigned bit){return core.syncOnly[bit/64].load(std::memory_order_relaxed)&(1ull<<(bit%64));}
    bool answer(CmdTag<Cmd::Device_GetRenderState>,D3DRENDERSTATETYPE s,DWORD* v,HRESULT& hr){
        begin();if(!v||unsigned(s)>=StreamState::kRS||!st.rs[s].known||syncOnly(StreamState::bitRS(s)))return false;*v=st.rs[s].v;hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetSamplerState>,DWORD s,D3DSAMPLERSTATETYPE t,DWORD* v,HRESULT& hr){
        begin();unsigned idx;if(!v||!StreamState::sampIndex(s,idx)||unsigned(t)>=StreamState::kSampTypes||!st.samp[idx][t].known||syncOnly(StreamState::bitSamp(idx,unsigned(t))))return false;
        *v=st.samp[idx][t].v;hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetTextureStageState>,DWORD s,D3DTEXTURESTAGESTATETYPE t,DWORD* v,HRESULT& hr){
        begin();if(!v||s>=StreamState::kTSStages||unsigned(t)>=StreamState::kTSTypes||!st.tss[s][t].known||syncOnly(StreamState::bitTss(s,unsigned(t))))return false;
        *v=st.tss[s][t].v;hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetTransform>,D3DTRANSFORMSTATETYPE s,D3DMATRIX* m,HRESULT& hr){
        begin();if(!m||unsigned(s)>=StreamState::kXforms||!st.xf[s].known)return false;*m=st.xf[s].v;hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetViewport>,D3DVIEWPORT9* v,HRESULT& hr){begin();if(!v||!st.viewport.known)return false;*v=st.viewport.v;hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetMaterial>,D3DMATERIAL9* m,HRESULT& hr){begin();if(!m||!st.material.known)return false;*m=st.material.v;hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetLight>,DWORD i,D3DLIGHT9* l,HRESULT& hr){begin();if(!l||i>=st.lights.size()||!st.lights[i].l.known)return false;*l=st.lights[i].l.v;hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetLightEnable>,DWORD i,WINBOOL* e,HRESULT& hr){begin();if(!e||i>=st.lights.size()||!st.lights[i].enabled.known)return false;*e=st.lights[i].enabled.v;hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetClipPlane>,DWORD i,float* p,HRESULT& hr){begin();if(!p||i>=StreamState::kClip||!st.clip[i].known)return false;std::memcpy(p,st.clip[i].p,16);hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetScissorRect>,RECT* r,HRESULT& hr){begin();if(!r||!st.scissor.known)return false;*r=st.scissor.v;hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetSoftwareVertexProcessing>,WINBOOL& r){begin();if(!st.swvp.known)return false;r=st.swvp.v;return hit();}
    bool answer(CmdTag<Cmd::Device_GetNPatchMode>,float& r){begin();if(!st.npatch.known)return false;r=st.npatch.v;return hit();}
    bool answer(CmdTag<Cmd::Device_GetCurrentTexturePalette>,UINT* n,HRESULT& hr){begin();if(!n||!st.palette.known)return false;*n=st.palette.v;hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetFVF>,DWORD* f,HRESULT& hr){begin();if(!f||!st.fvf.known)return false;*f=st.fvf.v;hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetPaletteEntries>,UINT,PALETTEENTRY*,HRESULT&){begin();return false;}   // palettes are not shadowed
    bool answer(CmdTag<Cmd::Device_GetVertexShaderConstantF>,UINT r,float* d,UINT n,HRESULT& hr){begin();return getF(st.vsF,r,d,n,hr);}
    bool answer(CmdTag<Cmd::Device_GetPixelShaderConstantF>,UINT r,float* d,UINT n,HRESULT& hr){begin();return getF(st.psF,r,d,n,hr);}
    bool answer(CmdTag<Cmd::Device_GetVertexShaderConstantI>,UINT r,int* d,UINT n,HRESULT& hr){begin();return getI(st.vsI,r,d,n,hr);}
    bool answer(CmdTag<Cmd::Device_GetPixelShaderConstantI>,UINT r,int* d,UINT n,HRESULT& hr){begin();return getI(st.psI,r,d,n,hr);}
    bool answer(CmdTag<Cmd::Device_GetVertexShaderConstantB>,UINT r,WINBOOL* d,UINT n,HRESULT& hr){begin();return getB(st.vsB,r,d,n,hr);}
    bool answer(CmdTag<Cmd::Device_GetPixelShaderConstantB>,UINT r,WINBOOL* d,UINT n,HRESULT& hr){begin();return getB(st.psB,r,d,n,hr);}
    bool answer(CmdTag<Cmd::Device_GetRenderTarget>,DWORD i,IDirect3DSurface9** pp,HRESULT& hr){
        begin();if(!pp||i>=StreamState::kRTs||!st.rtKnown[i]||!st.rt[i])return false;give(st.rt[i],pp);hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetDepthStencilSurface>,IDirect3DSurface9** pp,HRESULT& hr){begin();if(!pp||!st.dsKnown||!st.ds)return false;give(st.ds,pp);hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetTexture>,DWORD s,IDirect3DBaseTexture9** pp,HRESULT& hr){
        begin();unsigned idx;if(!pp||!StreamState::sampIndex(s,idx)||!st.texKnown[idx])return false;give(st.tex[idx],pp);hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetVertexDeclaration>,IDirect3DVertexDeclaration9** pp,HRESULT& hr){begin();if(!pp||!st.declKnown)return false;give(st.decl,pp);hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetVertexShader>,IDirect3DVertexShader9** pp,HRESULT& hr){begin();if(!pp||!st.vsKnown)return false;give(st.vs,pp);hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetPixelShader>,IDirect3DPixelShader9** pp,HRESULT& hr){begin();if(!pp||!st.psKnown)return false;give(st.ps,pp);hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetStreamSource>,UINT i,IDirect3DVertexBuffer9** pp,UINT* off,UINT* stride,HRESULT& hr){
        begin();if(!pp||!off||!stride||i>=StreamState::kStreams||!st.streams[i].known)return false;auto& s=st.streams[i];give(s.vb,pp);*off=s.offset;*stride=s.stride;hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetStreamSourceFreq>,UINT i,UINT* f,HRESULT& hr){begin();if(!f||i>=StreamState::kStreams||!st.streams[i].freq.known)return false;*f=st.streams[i].freq.v;hr=D3D_OK;return hit();}
    bool answer(CmdTag<Cmd::Device_GetIndices>,IDirect3DIndexBuffer9** pp,HRESULT& hr){begin();if(!pp||!st.indicesKnown)return false;give(st.indices,pp);hr=D3D_OK;return hit();}

    // local class
    UINT local(CmdTag<Cmd::Device_GetAvailableTextureMem>){return core.availableTextureMem.load();}
    HRESULT local(CmdTag<Cmd::Device_GetDirect3D>,IDirect3D9** pp){if(!pp)return D3DERR_INVALIDCALL;*pp=parent;parent->AddRef();return D3D_OK;}
    HRESULT local(CmdTag<Cmd::Device_GetDeviceCaps>,D3DCAPS9* c){if(!c)return D3DERR_INVALIDCALL;*c=caps;return D3D_OK;}
    HRESULT local(CmdTag<Cmd::Device_GetCreationParameters>,D3DDEVICE_CREATION_PARAMETERS* c){if(!c)return D3DERR_INVALIDCALL;*c=creation;return D3D_OK;}
    HRESULT local(CmdTag<Cmd::Device_GetSwapChain>,UINT i,IDirect3DSwapChain9** pp){if(!pp)return D3DERR_INVALIDCALL;*pp=nullptr;if(i!=0)return D3DERR_INVALIDCALL;sc0->comAddRef();*pp=sc0;return D3D_OK;}
    UINT local(CmdTag<Cmd::Device_GetNumberOfSwapChains>){return 1;}

    // ---- custom methods ----
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override{
        if(!out)return E_POINTER;*out=nullptr;
        if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3DDevice9)){*out=static_cast<IDirect3DDevice9*>(this);AddRef();return S_OK;}
        add(core.q.stats.qiMisses);return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ULONG(refs.fetch_add(1)+1);}
    ULONG STDMETHODCALLTYPE Release() override{
        const LONG n=refs.fetch_sub(1)-1;
        if(n==0)finalRelease();
        return n<0?0:ULONG(n);}
    BOOL STDMETHODCALLTYPE ShowCursor(BOOL show) override{const BOOL prev=cursorVisible;cursorVisible=show;record_Device_ShowCursor(streamQueue(),show);return prev;}
    void STDMETHODCALLTYPE SetCursorPosition(int x,int y,DWORD flags) override{
        if(std::this_thread::get_id()!=gameThread){   // a foreign thread must not touch the single-producer queue: hand it to the game thread
            add(core.q.stats.foreignEntries);{std::lock_guard<std::mutex> l(foreignMutex);foreign.push_back({x,y,flags});}foreignPending.fetch_add(1);return;}
        record_Device_SetCursorPosition(streamQueue(),x,y,flags);}

    HRESULT STDMETHODCALLTYPE Present(const RECT* src,const RECT* dst,HWND window,const RGNDATA* dirty) override{return presentCommon(nullptr,src,dst,window,dirty,0);}
    HRESULT presentCommon(StreamSwapChain* swap,const RECT* src,const RECT* dst,HWND window,const RGNDATA* dirty,DWORD flags){
        Queue& q=streamQueue();
        policy.onPresent();
        UINT dirtyBytes=0;
        if(dirty){dirtyBytes=dirty->rdh.dwSize+dirty->rdh.nCount*UINT(sizeof(RECT));if(sizeof(PresentArgs)+dirtyBytes>MaxInlinePayload)dirtyBytes=0;}
        auto* a=static_cast<PresentArgs*>(q.reserve((std::uint16_t)(swap?Cmd::SwapPresent:Cmd::Present),std::uint32_t(sizeof(PresentArgs)+dirtyBytes)));
        a->swapChain=swap;a->window=window;a->flags=flags;a->hasSrc=src!=nullptr;a->hasDst=dst!=nullptr;a->dirtyBytes=dirtyBytes;
        if(src)a->src=*src;else a->src=RECT{};
        if(dst)a->dst=*dst;else a->dst=RECT{};
        if(dirtyBytes)std::memcpy(a+1,dirty,dirtyBytes);
        q.commit();q.publish();
        const std::uint64_t seq=q.recordedSeq(),prev=prevPresent;prevPresent=seq;
        HRESULT result=D3D_OK;
        if(prev){q.waitReplayed(prev,WaitKind::Present);result=presentResult(prev);}   // one frame ahead: the previous frame's real HRESULT
        q.setPressure(core.memoryPressure.load(std::memory_order_relaxed)||NorthlightStream::memoryPressure.load(std::memory_order_relaxed));
        if(pressureApplied!=q.pressure()){pressureApplied=q.pressure();if(pressureApplied)q.trim();}
        return result;
    }
    // The real result of the Present command with sequence number `seq`, recorded by the replay thread.
    HRESULT presentResult(std::uint64_t seq){const auto& e=core.presentRing[seq%core.kRing];return e.seq.load()==seq?e.hr.load():D3D_OK;}

    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* p) override{
        if(!p)return D3DERR_INVALIDCALL;
        st.clear();recording=false;
        HRESULT hr=D3DERR_INVALIDCALL;
        const bool ran=runTask(core,[&](StreamCore& c){   // everything recorded so far has run; the game waits, pumped
            for(ProxyBase* k:sc0->kids)if(k&&k->inner&&k->refs.load()==0){c.reg.unbindInner(k);k->inner->Release();k->inner=nullptr;}   // one the game still holds keeps its object, as in D3D9   // back buffers must not outlive the swap chain's reset
            hr=c.target->Reset(p);
            if(SUCCEEDED(hr)){IDirect3DSwapChain9* sc=nullptr;if(SUCCEEDED(c.target->GetSwapChain(0,&sc))&&sc){sc->GetPresentParameters(&sc0->pp);sc->Release();}}
            ensureBackBuffers(c);
            st.loadDefaults(c.target);c.replayFailure.store(false);},Cmd::SyncReset);
        if(!ran)return D3DERR_INVALIDCALL;
        if(SUCCEEDED(hr))pp=sc0->pp;
        return hr;
    }
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT swapChain,UINT index,D3DBACKBUFFER_TYPE type,IDirect3DSurface9** out) override{
        if(swapChain!=0)return D3DERR_INVALIDCALL;return sc0->GetBackBuffer(index,type,out);}
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS*,IDirect3DSwapChain9** out) override{
        if(out)*out=nullptr;add(core.q.stats.createFailures);return D3DERR_NOTAVAILABLE;}   // not supported while streaming (the game never uses it)

    // ---- creates: asynchronous, the proxy is returned at once; synchronous under memory pressure or with a shared handle ----
    template<class P> HRESULT finishCreate(P* proxy,Cmd id,CreateArgs& a,const void* extra,UINT extraBytes,bool forceSync,void** out){
        Queue& q=streamQueue();
        if(forceSync||q.pressure()){
            HRESULT hr=D3DERR_INVALIDCALL;
            const bool ran=runTask(core,[&](StreamCore&){hr=replayer.createNow(a,id,extra);if(SUCCEEDED(hr))afterSyncCreate(proxy);},Cmd::SyncCreate);
            if(!ran||FAILED(hr)){discard(proxy);*out=nullptr;return ran?hr:D3DERR_INVALIDCALL;}
            *out=static_cast<typename std::remove_pointer<decltype(firstIface(proxy))>::type*>(proxy);return D3D_OK;
        }
        a.extraBytes=extraBytes;
        auto* p=static_cast<CreateArgs*>(q.reserve((std::uint16_t)id,std::uint32_t(sizeof(CreateArgs)+((extraBytes+7u)&~7u))));
        *p=a;if(extraBytes)std::memcpy(p+1,extra,extraBytes);q.commit();
        *out=static_cast<typename std::remove_pointer<decltype(firstIface(proxy))>::type*>(proxy);return D3D_OK;
    }
    static IDirect3DTexture9* firstIface(StreamTexture*);static IDirect3DCubeTexture9* firstIface(StreamCubeTexture*);static IDirect3DVolumeTexture9* firstIface(StreamVolumeTexture*);
    static IDirect3DVertexBuffer9* firstIface(StreamVertexBuffer*);static IDirect3DIndexBuffer9* firstIface(StreamIndexBuffer*);static IDirect3DSurface9* firstIface(StreamSurface*);
    static IDirect3DVertexDeclaration9* firstIface(StreamVertexDeclaration*);static IDirect3DVertexShader9* firstIface(StreamVertexShader*);static IDirect3DPixelShader9* firstIface(StreamPixelShader*);
    static IDirect3DQuery9* firstIface(StreamQuery*);static IDirect3DStateBlock9* firstIface(StreamStateBlock*);
    template<class P> void afterSyncCreate(P*){}
    void afterSyncCreate(StreamQuery* p){if(p->inner&&!p->dataSize)p->dataSize=static_cast<IDirect3DQuery9*>(p->inner)->GetDataSize();}
    template<class P> void discard(P* p){core.reg.erase(p);delete p;}

    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT fmt,D3DPOOL pool,IDirect3DTexture9** out,HANDLE* shared) override{
        if(!out)return D3DERR_INVALIDCALL;*out=nullptr;if(!w||!h)return D3DERR_INVALIDCALL;
        StreamTexture* p;try{p=new StreamTexture(&core,w,h,levels,usage,unsigned(fmt),DWORD(pool));}catch(...){return E_OUTOFMEMORY;}
        CreateArgs a{p,{w,h,levels,usage,unsigned(fmt),unsigned(pool),0,0},0};void* o=nullptr;
        const HRESULT hr=finishCreate(p,Cmd::CreateTexture,a,nullptr,0,shared&&*shared,&o);*out=static_cast<IDirect3DTexture9*>(o);return hr;}
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT w,UINT h,UINT d,UINT levels,DWORD usage,D3DFORMAT fmt,D3DPOOL pool,IDirect3DVolumeTexture9** out,HANDLE* shared) override{
        if(!out)return D3DERR_INVALIDCALL;*out=nullptr;if(!w||!h||!d)return D3DERR_INVALIDCALL;
        StreamVolumeTexture* p;try{p=new StreamVolumeTexture(&core,w,h,d,levels,usage,unsigned(fmt),DWORD(pool));}catch(...){return E_OUTOFMEMORY;}
        CreateArgs a{p,{w,h,d,levels,usage,unsigned(fmt),unsigned(pool),0},0};void* o=nullptr;
        const HRESULT hr=finishCreate(p,Cmd::CreateVolumeTexture,a,nullptr,0,shared&&*shared,&o);*out=static_cast<IDirect3DVolumeTexture9*>(o);return hr;}
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT edge,UINT levels,DWORD usage,D3DFORMAT fmt,D3DPOOL pool,IDirect3DCubeTexture9** out,HANDLE* shared) override{
        if(!out)return D3DERR_INVALIDCALL;*out=nullptr;if(!edge)return D3DERR_INVALIDCALL;
        StreamCubeTexture* p;try{p=new StreamCubeTexture(&core,edge,levels,usage,unsigned(fmt),DWORD(pool));}catch(...){return E_OUTOFMEMORY;}
        CreateArgs a{p,{edge,levels,usage,unsigned(fmt),unsigned(pool),0,0,0},0};void* o=nullptr;
        const HRESULT hr=finishCreate(p,Cmd::CreateCubeTexture,a,nullptr,0,shared&&*shared,&o);*out=static_cast<IDirect3DCubeTexture9*>(o);return hr;}
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT length,DWORD usage,DWORD fvf,D3DPOOL pool,IDirect3DVertexBuffer9** out,HANDLE* shared) override{
        if(!out)return D3DERR_INVALIDCALL;*out=nullptr;if(!length)return D3DERR_INVALIDCALL;
        StreamVertexBuffer* p;try{p=new StreamVertexBuffer(&core,length,usage,fvf,DWORD(pool));}catch(...){return E_OUTOFMEMORY;}
        CreateArgs a{p,{length,usage,fvf,unsigned(pool),0,0,0,0},0};void* o=nullptr;
        const HRESULT hr=finishCreate(p,Cmd::CreateVertexBuffer,a,nullptr,0,shared&&*shared,&o);*out=static_cast<IDirect3DVertexBuffer9*>(o);return hr;}
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT length,DWORD usage,D3DFORMAT fmt,D3DPOOL pool,IDirect3DIndexBuffer9** out,HANDLE* shared) override{
        if(!out)return D3DERR_INVALIDCALL;*out=nullptr;if(!length)return D3DERR_INVALIDCALL;
        StreamIndexBuffer* p;try{p=new StreamIndexBuffer(&core,length,usage,unsigned(fmt),DWORD(pool));}catch(...){return E_OUTOFMEMORY;}
        CreateArgs a{p,{length,usage,unsigned(fmt),unsigned(pool),0,0,0,0},0};void* o=nullptr;
        const HRESULT hr=finishCreate(p,Cmd::CreateIndexBuffer,a,nullptr,0,shared&&*shared,&o);*out=static_cast<IDirect3DIndexBuffer9*>(o);return hr;}
    HRESULT createSurface(Cmd id,UINT w,UINT h,D3DFORMAT fmt,UINT ms,DWORD msq,UINT sixth,DWORD usage,DWORD pool,IDirect3DSurface9** out,HANDLE* shared){
        if(!out)return D3DERR_INVALIDCALL;*out=nullptr;if(!w||!h)return D3DERR_INVALIDCALL;
        Info i;i.w=w;i.h=h;i.fmt=unsigned(fmt);i.usage=usage;i.pool=pool;i.ms=ms;i.msq=msq;
        StreamSurface* p;try{p=new StreamSurface(&core,i);}catch(...){return E_OUTOFMEMORY;}
        CreateArgs a{p,{w,h,id==Cmd::CreateOffscreenPlainSurface?unsigned(fmt):unsigned(fmt),id==Cmd::CreateOffscreenPlainSurface?unsigned(pool):ms,msq,sixth,0,0},0};
        if(id==Cmd::CreateOffscreenPlainSurface){a.v[2]=unsigned(fmt);a.v[3]=pool;}
        void* o=nullptr;const HRESULT hr=finishCreate(p,id,a,nullptr,0,shared&&*shared,&o);*out=static_cast<IDirect3DSurface9*>(o);return hr;}
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT w,UINT h,D3DFORMAT fmt,D3DMULTISAMPLE_TYPE ms,DWORD msq,WINBOOL lockable,IDirect3DSurface9** out,HANDLE* shared) override{
        return createSurface(Cmd::CreateRenderTarget,w,h,fmt,unsigned(ms),msq,lockable?1u:0u,D3::kUsageRT,D3::kPoolDefault,out,shared);}
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT w,UINT h,D3DFORMAT fmt,D3DMULTISAMPLE_TYPE ms,DWORD msq,WINBOOL discard,IDirect3DSurface9** out,HANDLE* shared) override{
        return createSurface(Cmd::CreateDepthStencilSurface,w,h,fmt,unsigned(ms),msq,discard?1u:0u,D3::kUsageDS,D3::kPoolDefault,out,shared);}
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT w,UINT h,D3DFORMAT fmt,D3DPOOL pool,IDirect3DSurface9** out,HANDLE* shared) override{
        return createSurface(Cmd::CreateOffscreenPlainSurface,w,h,fmt,0,0,0,0,DWORD(pool),out,shared);}
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9* elements,IDirect3DVertexDeclaration9** out) override{
        if(!out)return D3DERR_INVALIDCALL;*out=nullptr;if(!elements)return D3DERR_INVALIDCALL;
        UINT n=0;while(n<256&&elements[n].Stream!=0xFF)++n;if(n>=256)return D3DERR_INVALIDCALL;++n;   // including the end element
        StreamVertexDeclaration* p;try{p=new StreamVertexDeclaration(&core);p->elements.assign(elements,elements+n);}catch(...){return E_OUTOFMEMORY;}
        CreateArgs a{p,{},0};void* o=nullptr;const HRESULT hr=finishCreate(p,Cmd::CreateVertexDeclaration,a,elements,n*UINT(sizeof(D3DVERTEXELEMENT9)),false,&o);*out=static_cast<IDirect3DVertexDeclaration9*>(o);return hr;}
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code,IDirect3DVertexShader9** out) override{
        if(!out)return D3DERR_INVALIDCALL;*out=nullptr;const std::size_t tokens=shaderTokens(code);if(!tokens)return D3DERR_INVALIDCALL;
        StreamVertexShader* p;try{p=new StreamVertexShader(&core);p->code.assign(code,code+tokens);}catch(...){return E_OUTOFMEMORY;}
        p->tags=NorthlightShaderTags::triggerTagsOfBytecode(code,tokens*4);   // from the bytes the game passed: the same ones GetFunction returns
        CreateArgs a{p,{},0};void* o=nullptr;const HRESULT hr=finishCreate(p,Cmd::CreateVertexShader,a,code,UINT(tokens*4),false,&o);*out=static_cast<IDirect3DVertexShader9*>(o);return hr;}
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** out) override{
        if(!out)return D3DERR_INVALIDCALL;*out=nullptr;const std::size_t tokens=shaderTokens(code);if(!tokens)return D3DERR_INVALIDCALL;
        StreamPixelShader* p;try{p=new StreamPixelShader(&core);p->code.assign(code,code+tokens);}catch(...){return E_OUTOFMEMORY;}
        CreateArgs a{p,{},0};void* o=nullptr;const HRESULT hr=finishCreate(p,Cmd::CreatePixelShader,a,code,UINT(tokens*4),false,&o);*out=static_cast<IDirect3DPixelShader9*>(o);return hr;}
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE type,IDirect3DQuery9** out) override{
        if(!out){return D3DERR_INVALIDCALL;}*out=nullptr;
        StreamQuery* p;try{p=new StreamQuery(&core,unsigned(type));}catch(...){return E_OUTOFMEMORY;}
        CreateArgs a{p,{unsigned(type),0,0,0,0,0,0,0},0};void* o=nullptr;
        const HRESULT hr=finishCreate(p,Cmd::CreateQuery,a,nullptr,0,p->dataSize==0,&o);*out=static_cast<IDirect3DQuery9*>(o);return hr;}   // unknown query types create synchronously (the data size is the real one)
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE type,IDirect3DStateBlock9** out) override{
        if(!out)return D3DERR_INVALIDCALL;*out=nullptr;
        StreamStateBlock* p;try{p=new StreamStateBlock(&core);}catch(...){return E_OUTOFMEMORY;}
        hookBlock(p);CreateArgs a{p,{unsigned(type),0,0,0,0,0,0,0},0};void* o=nullptr;
        const HRESULT hr=finishCreate(p,Cmd::CreateStateBlock,a,nullptr,0,false,&o);*out=static_cast<IDirect3DStateBlock9*>(o);return hr;}
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override{
        if(recording)return D3DERR_INVALIDCALL;recording=true;Queue& q=streamQueue();q.reserve((std::uint16_t)Cmd::BeginStateBlock,0);q.commit();return D3D_OK;}
    HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9** out) override{
        if(!out)return D3DERR_INVALIDCALL;*out=nullptr;if(!recording)return D3DERR_INVALIDCALL;recording=false;
        StreamStateBlock* p;try{p=new StreamStateBlock(&core);}catch(...){return E_OUTOFMEMORY;}
        hookBlock(p);CreateArgs a{p,{},0};void* o=nullptr;
        const HRESULT hr=finishCreate(p,Cmd::EndStateBlock,a,nullptr,0,false,&o);*out=static_cast<IDirect3DStateBlock9*>(o);return hr;}
    void hookBlock(StreamStateBlock* p){p->hook=[this](ProxyBase&,bool apply){if(apply)st.invalidate();};}

    // ---- user-pointer draws: the data is copied (the game's memory is reused at once); the replay sets upIdentity to the game's pointer ----
    // Readers in the mod index UP data from the base pointer and may overread a little: whole vertices (count*stride) are copied
    // and kUpSlack zeroed bytes follow each payload.
    static constexpr std::size_t kUpSlack=64;
    static std::size_t vertexBytes(unsigned count,unsigned stride){return std::size_t(count)*stride;}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT primCount,const void* data,UINT stride) override{
        if(!data||!stride||!primCount)return D3DERR_INVALIDCALL;
        onDraw(primCount);Queue& q=streamQueue();
        const std::size_t bytes=vertexBytes(primVerts(unsigned(type),primCount),stride);
        if(sizeof(DrawUPArgs)+bytes+kUpSlack<=MaxInlinePayload){
            auto* a=static_cast<DrawUPArgs*>(q.reserve((std::uint16_t)Cmd::DrawPrimitiveUP,std::uint32_t(sizeof(DrawUPArgs)+bytes+kUpSlack)));
            *a=DrawUPArgs{unsigned(type),primCount,stride,UINT(bytes),1,data};std::memcpy(a+1,data,bytes);std::memset(reinterpret_cast<unsigned char*>(a+1)+bytes,0,kUpSlack);q.commit();return D3D_OK;}
        if(Block* b=q.tryAllocBlock(bytes+kUpSlack)){
            auto* a=static_cast<DrawUPArgs*>(q.reserveWithBlock((std::uint16_t)Cmd::DrawPrimitiveUP,sizeof(DrawUPArgs),b));
            *a=DrawUPArgs{unsigned(type),primCount,stride,UINT(bytes),0,data};b->used=std::uint32_t(bytes);std::memcpy(b->data(),data,bytes);std::memset(b->data()+bytes,0,kUpSlack);q.commit();return D3D_OK;}
        add(q.stats.passThrough[unsigned(PassReason::Budget)]);
        HRESULT hr=D3DERR_INVALIDCALL;
        runTask(core,[&](StreamCore& c){upIdentity=data;hr=c.target->DrawPrimitiveUP(type,primCount,data,stride);upIdentity=nullptr;},Cmd::SyncUpDraw);
        return hr;}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type,UINT minIndex,UINT numVertices,UINT primCount,const void* indices,D3DFORMAT indexFormat,const void* data,UINT stride) override{
        if(!data||!indices||!stride||!primCount)return D3DERR_INVALIDCALL;
        onDraw(primCount);Queue& q=streamQueue();
        const std::size_t indexBytes=std::size_t(primVerts(unsigned(type),primCount))*(unsigned(indexFormat)==D3::kFmtIndex32?4:2),
                          vbytes=vertexBytes(minIndex+numVertices,stride),total=((indexBytes+7u)&~std::size_t(7))+vbytes+kUpSlack;
        auto fill=[&](unsigned char* base){std::memcpy(base,indices,indexBytes);std::memcpy(base+((indexBytes+7u)&~std::size_t(7)),data,vbytes);std::memset(base+((indexBytes+7u)&~std::size_t(7))+vbytes,0,kUpSlack);};
        DrawIUPArgs args{unsigned(type),minIndex,numVertices,primCount,unsigned(indexFormat),stride,UINT(indexBytes),UINT(vbytes),data};
        if(sizeof(DrawIUPArgs)+total<=MaxInlinePayload){
            auto* a=static_cast<DrawIUPArgs*>(q.reserve((std::uint16_t)Cmd::DrawIndexedPrimitiveUP,std::uint32_t(sizeof(DrawIUPArgs)+total)));
            *a=args;fill(reinterpret_cast<unsigned char*>(a+1));q.commit();return D3D_OK;}
        if(Block* b=q.tryAllocBlock(total)){
            auto* a=static_cast<DrawIUPArgs*>(q.reserveWithBlock((std::uint16_t)Cmd::DrawIndexedPrimitiveUP,sizeof(DrawIUPArgs),b));
            *a=args;b->used=std::uint32_t(total);fill(b->data());q.commit();return D3D_OK;}
        add(q.stats.passThrough[unsigned(PassReason::Budget)]);
        HRESULT hr=D3DERR_INVALIDCALL;
        runTask(core,[&](StreamCore& c){upIdentity=data;hr=c.target->DrawIndexedPrimitiveUP(type,minIndex,numVertices,primCount,indices,indexFormat,data,stride);upIdentity=nullptr;},Cmd::SyncUpDraw);
        return hr;}
    static unsigned primVerts(unsigned type,unsigned n){switch(type){case 1:return n;case 2:return n*2;case 3:return n+1;case 4:return n*3;default:return n+2;}}

    // ---- accessors for the DLL wiring and the tests ----
    StreamCore& streamCore(){return core;}
    Replayer& replayerOf(){return replayer;}
    StreamState& stateOf(){return st;}
    StreamSwapChain* swapChain0(){return sc0;}
    std::uint64_t draws()const{return drawOrdinal;}

private:
    struct Foreign {int x,y;DWORD flags;};
    std::unique_ptr<StreamCore> coreOwner;StreamCore& core;Replayer replayer;StreamState st;
    IDirect3D9* parent;D3DCAPS9 caps{};D3DDEVICE_CREATION_PARAMETERS creation{};D3DPRESENT_PARAMETERS pp{};
    StreamSwapChain* sc0=nullptr;std::atomic<LONG> refs{1};
    bool recording=false,cursorVisible=false,pressureApplied=false;std::uint64_t prevPresent=0,drawOrdinal=0;
    std::thread::id gameThread=std::this_thread::get_id();
    std::mutex foreignMutex;std::vector<Foreign> foreign;std::atomic<unsigned> foreignPending{0};
    TriggerPolicy policy;bool (*capture)(GameSnapshot&,Trigger,std::uint64_t)=nullptr;

    StreamDevice(IDirect3DDevice9* target,IDirect3D9* par,const D3DPRESENT_PARAMETERS* p,Options opt)
        :coreOwner(new StreamCore(opt.budget)),core(*coreOwner),replayer(core),parent(par),capture(opt.capture){
        core.target=target;core.game=this;core.logLine=nullptr;st.core=&core;
        replayer.threadStart=std::move(opt.threadStart);replayer.log=opt.log;replayer.diagnostics=opt.diagnostics;
        if(p)pp=*p;
        sc0=new StreamSwapChain(&core);sc0->pp=pp;
        const UINT n=pp.BackBufferCount?pp.BackBufferCount:1;sc0->kids.assign(n,nullptr);
        parent->AddRef();
    }
    void abandon(){   // init failed after the thread ran: tear down without the Target
        st.clear();delete sc0;sc0=nullptr;if(parent)parent->Release();parent=nullptr;
    }
    // Replay thread: the swap chain's back-buffer proxies exist from the start (so a Get of the default render target returns
    // the same object GetBackBuffer does) and follow every Reset: cached, unreferenced (use 0) until the game or a bind takes one.
    void ensureBackBuffers(StreamCore& c){
        auto* sc=static_cast<IDirect3DSwapChain9*>(sc0->inner);if(!sc)return;
        const UINT n=sc0->pp.BackBufferCount?sc0->pp.BackBufferCount:1;
        if(sc0->kids.size()<n)sc0->kids.resize(n,nullptr);
        for(UINT i=0;i<UINT(sc0->kids.size());++i){
            ProxyBase* k=sc0->kids[i];
            if(!k){Info in;in.w=sc0->pp.BackBufferWidth;in.h=sc0->pp.BackBufferHeight;in.fmt=unsigned(sc0->pp.BackBufferFormat);in.usage=D3::kUsageRT;in.pool=D3::kPoolDefault;in.ms=unsigned(sc0->pp.MultiSampleType);in.msq=sc0->pp.MultiSampleQuality;
                   k=new StreamSurface(&c,in,sc0,nullptr);k->refs.store(0);k->use.store(0);sc0->kids[i]=k;}
            k->info.w=sc0->pp.BackBufferWidth;k->info.h=sc0->pp.BackBufferHeight;k->info.fmt=unsigned(sc0->pp.BackBufferFormat);
            if(k->inner)continue;
            IDirect3DSurface9* b=nullptr;
            if(i<n&&SUCCEEDED(sc->GetBackBuffer(i,(D3DBACKBUFFER_TYPE)0,&b))&&b){c.reg.bindInner(k,b);k->dead.store(false);}else k->dead.store(true);
        }
    }
    void drainForeign(){
        std::vector<Foreign> take;{std::lock_guard<std::mutex> l(foreignMutex);take.swap(foreign);foreignPending.store(0);}
        for(const auto& f:take)record_Device_SetCursorPosition(core.q,f.x,f.y,f.flags);
    }
    void finalRelease(){
        st.clear();sc0->comRelease();   // binds and the swap chain's own reference go; the Destroys run before the Target's release
        runTask(core,[&](StreamCore& c){c.target->Release();c.target=nullptr;},Cmd::SyncRelease);   // the Device's final release happens on the replay thread
        replayer.stop();
        innerOf=nullptr;activeCore.store(nullptr,std::memory_order_release);
        IDirect3D9* p=parent;
        delete this;
        if(p)p->Release();
    }
    void onDraw(unsigned){
        ++drawOrdinal;if(!capture)return;
        const unsigned tags=st.vs?static_cast<StreamVertexShader*>(st.vs)->tags:0;
        takeSnapshot(policy.onDraw(tags));
    }
    void takeSnapshot(Trigger t){
        if(t==Trigger::None||!capture)return;
        GameSnapshot* s=replayer.snapshots.acquire();if(!s)return;
        if(!capture(*s,t,drawOrdinal)){replayer.snapshots.release(s);return;}
        Queue& q=core.q;auto* p=static_cast<GameSnapshot**>(q.reserve((std::uint16_t)Cmd::Snapshot,sizeof(GameSnapshot*)));*p=s;q.commit();
    }
    void written(ProxyBase* p){
        if(!p)return;
        switch(p->kind){
        case Kind::Surface:{auto* s=static_cast<StreamSurface*>(p);s->subp->written=true;break;}
        case Kind::Texture:for(auto& s:static_cast<StreamTexture*>(p)->subs)s.written=true;break;
        case Kind::CubeTexture:for(auto& s:static_cast<StreamCubeTexture*>(p)->subs)s.written=true;break;
        case Kind::VolumeTexture:for(auto& s:static_cast<StreamVolumeTexture*>(p)->subs)s.written=true;break;
        case Kind::VertexBuffer:dropShadow(*p,static_cast<StreamVertexBuffer*>(p)->buf);static_cast<StreamVertexBuffer*>(p)->buf.written=true;break;
        default:break;
        }
    }
    void constF(StreamState::VsF* regs,UINT r,const float* d,UINT n){if(recording||!d)return;for(UINT i=0;i<n&&r+i<256;++i){regs[r+i].known=true;std::memcpy(regs[r+i].v,d+4*i,16);}}
    void constI(StreamState::VsI* regs,UINT r,const int* d,UINT n){if(recording||!d)return;for(UINT i=0;i<n&&r+i<16;++i){regs[r+i].known=true;std::memcpy(regs[r+i].v,d+4*i,16);}}
    void constB(Slot<BOOL>* regs,UINT r,const WINBOOL* d,UINT n){if(recording||!d)return;for(UINT i=0;i<n&&r+i<16;++i)regs[r+i].set(d[i]);}
    bool getF(StreamState::VsF* regs,UINT r,float* d,UINT n,HRESULT& hr){
        if(!d||r+n>256)return false;for(UINT i=0;i<n;++i)if(!regs[r+i].known)return false;
        for(UINT i=0;i<n;++i)std::memcpy(d+4*i,regs[r+i].v,16);hr=D3D_OK;return hit();}
    bool getI(StreamState::VsI* regs,UINT r,int* d,UINT n,HRESULT& hr){
        if(!d||r+n>16)return false;for(UINT i=0;i<n;++i)if(!regs[r+i].known)return false;
        for(UINT i=0;i<n;++i)std::memcpy(d+4*i,regs[r+i].v,16);hr=D3D_OK;return hit();}
    bool getB(Slot<BOOL>* regs,UINT r,WINBOOL* d,UINT n,HRESULT& hr){
        if(!d||r+n>16)return false;for(UINT i=0;i<n;++i)if(!regs[r+i].known)return false;
        for(UINT i=0;i<n;++i)d[i]=regs[r+i].v;hr=D3D_OK;return hit();}
};

inline HRESULT STDMETHODCALLTYPE StreamSwapChain::Present(const RECT* src,const RECT* dst,HWND window,const RGNDATA* dirty,DWORD flags){
    return static_cast<StreamDevice*>(core->game)->presentCommon(this,src,dst,window,dirty,flags);}
inline HRESULT STDMETHODCALLTYPE StreamSwapChain::GetBackBuffer(UINT index,D3DBACKBUFFER_TYPE type,IDirect3DSurface9** out){
    if(!out)return D3DERR_INVALIDCALL;*out=nullptr;
    if(index>=kids.size()||unsigned(type)!=0)return D3DERR_INVALIDCALL;
    auto* kid=static_cast<StreamSurface*>(kids[index]);
    if(!kid){
        Info i;i.w=pp.BackBufferWidth;i.h=pp.BackBufferHeight;i.fmt=unsigned(pp.BackBufferFormat);i.usage=D3::kUsageRT;i.pool=D3::kPoolDefault;i.ms=unsigned(pp.MultiSampleType);i.msq=pp.MultiSampleQuality;
        try{kid=new StreamSurface(core,i,this,nullptr);}catch(...){return E_OUTOFMEMORY;}
        kids[index]=kid;comAddRef();recordDerive(*core,this,kid,DeriveBackBuffer,index,unsigned(type));
    }else kid->comAddRef();
    *out=kid;return D3D_OK;
}
}
