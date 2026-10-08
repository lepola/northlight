#pragma once
// Shared, serialized read-through state cache. Only the extension queries it;
// game-facing resource wrappers prevent raw GetDevice/Apply escape paths.
// Unknown interface/resource escapes permanently disable caching before return.
// State blocks always invalidate on Apply; recording never populates live state.
// 0.3.136: accepted setters write through only for slots whose backend has
// proven, by earlier exact Set->Get round trips, that it stores the value as given.
#ifndef NORTHLIGHT_DEVICE_MIRROR_TEST_API
#include <d3d9.h>
#include "forwarders.h"
#endif
#include <cstring>
#include <mutex>
#include <cstdint>
#include <new>
#include "captured_constant_epoch.h"
#include "mirror_guard.h"

static constexpr bool kMirrorWriteThrough=true,kMirrorBorrowedPeek=true;

// 0.3.180 (C3): known[0..count) all set. The flags hold only 0/1 (=true/false, memset 0), so a 4-byte
// word is all known iff it equals 0x01010101. Inline: a CRT memchr is an import-thunk byte loop under Wine.
static_assert(sizeof(bool)==1,"known flags are scanned as bytes");
inline bool allKnown(const bool* known,unsigned count){
    unsigned i=0;
    for(;i<count&&(reinterpret_cast<std::uintptr_t>(known+i)&3);++i)if(!known[i])return false;
    for(;count-i>=4;i+=4){std::uint32_t word;std::memcpy(&word,known+i,4);if(word!=0x01010101u)return false;}
    for(;i<count;++i)if(!known[i])return false;
    return true;
}

struct DeviceMirror {
    static constexpr unsigned Streams=16,Textures=16,RenderStates=256,SamplerTypes=16,VsFloat=256,PsFloat=224,Ints=16,Bools=16,Targets=4;
    MirrorGate gate; /* 0.3.180 (D0): the recursive mutex plus the owner thread and the census */
    NorthlightConstantEpoch::Clock constantEpoch;
    bool enabled=true,recording=false;
    std::size_t rawDepth=0; // Gate-protected; suspends caching across private extension work.
    const char* disableReason=nullptr;
    IDirect3DVertexShader9* vertexShader=nullptr;bool vertexShaderKnown=false;
    IDirect3DPixelShader9* pixelShader=nullptr;bool pixelShaderKnown=false;
    IDirect3DVertexDeclaration9* declaration=nullptr;bool declarationKnown=false;
    DWORD fvf=0;bool fvfKnown=false;
    struct Stream {IDirect3DVertexBuffer9* buffer=nullptr;UINT offset=0,stride=0;bool known=false;} streams[Streams];
    UINT frequency[Streams]={};bool frequencyKnown[Streams]={};
    IDirect3DIndexBuffer9* indices=nullptr;bool indicesKnown=false;
    IDirect3DBaseTexture9* textures[Textures]={};bool textureKnown[Textures]={};
    DWORD samplers[Textures][SamplerTypes]={};bool samplerKnown[Textures][SamplerTypes]={};
    DWORD renderStates[RenderStates]={};bool renderStateKnown[RenderStates]={};
    D3DVIEWPORT9 viewport={};bool viewportKnown=false;
    IDirect3DSurface9* targets[Targets]={};bool targetKnown[Targets]={};
    IDirect3DSurface9* depth=nullptr;bool depthKnown=false;
    float vsFloat[VsFloat][4]={};bool vsFloatKnown[VsFloat]={};
    int vsInt[Ints][4]={};bool vsIntKnown[Ints]={};
    BOOL vsBool[Bools]={};bool vsBoolKnown[Bools]={};
    float psFloat[PsFloat][4]={};bool psFloatKnown[PsFloat]={};
    std::uint64_t invalidations=0,answered=0,forwarded=0,checks=0,mismatches=0;
    std::uint64_t checkedFields=0,checkedRegisters=0,emptyChecks=0;
    std::uint64_t rawScopes=0,rawCalls=0;
    // 0.3.149 diagnostics: extension calls per D3D9 method inside raw scopes, counted
    // only while rawCounting (the renderer sets it for sampled frames; never with
    // Diagnostics=0). Indices: ExtensionDevice::rawMethodName(). Gate-protected.
    static constexpr unsigned RawMethodCapacity=48;
    bool rawCounting=false;std::uint32_t rawMethodCalls[RawMethodCapacity]={};
    void countRaw(unsigned i){if(rawCounting)++rawMethodCalls[i];}
    // Write-through learning. A slot is pending after an untrusted accepted
    // write whose value is stored in its field; the next authoritative read
    // compares. TrustAfter equal round trips trust the slot; one differing or
    // failed read distrusts it for the device lifetime (Reset included).
    enum Slot:unsigned {VertexShaderSlot,PixelShaderSlot,DeclarationSlot,IndicesSlot,StreamSlot,
        TextureSlot=StreamSlot+Streams,SamplerSlot=TextureSlot+Textures,RenderStateSlot=SamplerSlot+Textures*SamplerTypes,
        ViewportSlot=RenderStateSlot+RenderStates,TargetSlot,DepthSlot=TargetSlot+Targets,Slots};
    static constexpr std::uint8_t TrustAfter=8,Distrusted=0xff;
    std::uint8_t trust[Slots]={};bool pending[Slots]={};
    std::uint64_t writeThrough=0,learnedSlots=0,distrustedSlots=0,peeks=0;
    // Bumped by every accepted()/invalidate(). A setter writes through only if
    // nothing else mutated the mirror during its own backend call (re-entry).
    std::uint64_t mutations=0;
    bool active()const{return enabled&&!recording&&!rawDepth;}
    bool heldByThisThread()const{return MirrorGuard::heldByThisThread(gate);} /* 0.3.180 (C1): the in-place epoch source's precondition */
    // Accepted, active setter whose value is already in the slot's field.
    // Scalar slots (render/sampler states, viewport) additionally need this
    // exact value proven by an earlier round trip: a backend may normalize or
    // clamp only some values. Pointer bindings are identity-stored as a whole.
    bool written(unsigned slot,std::uint64_t serial,bool valueProven=true){
        if(mutations!=serial){pending[slot]=false;return false;}
        if(kMirrorWriteThrough&&trust[slot]==TrustAfter&&valueProven){pending[slot]=false;++writeThrough;return true;}
        pending[slot]=kMirrorWriteThrough&&trust[slot]!=Distrusted;return false;
    }
    // Proven (slot,value) pairs: exact 64-bit keys (never 0), open addressing,
    // insert-only and bounded; a full table just stops proving new pairs.
    static constexpr unsigned ProvenCapacity=4096,ProvenViewports=64;
    std::uint64_t proven[ProvenCapacity]={};unsigned provenCount=0;
    D3DVIEWPORT9 provenViewport[ProvenViewports]={};unsigned provenViewportCount=0;
    static std::uint64_t pair(unsigned slot,DWORD value){return (std::uint64_t(slot)+1)<<32|value;}
    static unsigned bucket(std::uint64_t key){return unsigned((key*0x9e3779b97f4a7c15ull)>>52)&(ProvenCapacity-1);}
    bool isProven(std::uint64_t key)const{
        for(unsigned i=bucket(key);;i=(i+1)&(ProvenCapacity-1)){if(proven[i]==key)return true;if(!proven[i])return false;}
    }
    void prove(std::uint64_t key){
        if(provenCount>=ProvenCapacity*3/4)return;
        for(unsigned i=bucket(key);;i=(i+1)&(ProvenCapacity-1)){if(proven[i]==key)return;if(!proven[i]){proven[i]=key;++provenCount;return;}}
    }
    bool isProven(const D3DVIEWPORT9& value)const{
        for(unsigned i=0;i<provenViewportCount;++i)if(!std::memcmp(&provenViewport[i],&value,sizeof value))return true;
        return false;
    }
    void prove(const D3DVIEWPORT9& value){if(provenViewportCount<ProvenViewports&&!isProven(value))provenViewport[provenViewportCount++]=value;}
    void forget(unsigned slot){pending[slot]=false;}
    // Authoritative backend read while active: same is false for a failed read.
    void observed(unsigned slot,bool same){
        if(!pending[slot])return;pending[slot]=false;
        if(!same){if(trust[slot]!=Distrusted){trust[slot]=Distrusted;++distrustedSlots;}return;}
        if(trust[slot]<TrustAfter&&++trust[slot]==TrustAfter)++learnedSlots;
    }
    // Callers hold gate, including ALL block Apply, Reset, and disable paths.
    void invalidate(){
        constantEpoch.invalidate();
        vertexShaderKnown=pixelShaderKnown=declarationKnown=fvfKnown=indicesKnown=viewportKnown=depthKnown=false;
        for(auto& s:streams)s.known=false;
        std::memset(frequencyKnown,0,sizeof frequencyKnown);std::memset(textureKnown,0,sizeof textureKnown);
        std::memset(samplerKnown,0,sizeof samplerKnown);std::memset(renderStateKnown,0,sizeof renderStateKnown);
        std::memset(targetKnown,0,sizeof targetKnown);std::memset(vsFloatKnown,0,sizeof vsFloatKnown);
        std::memset(vsIntKnown,0,sizeof vsIntKnown);std::memset(vsBoolKnown,0,sizeof vsBoolKnown);std::memset(psFloatKnown,0,sizeof psFloatKnown);
        std::memset(pending,0,sizeof pending);++mutations;++invalidations;
    }
    void disable(const char* reason){MirrorGuard lock(gate);if(enabled){enabled=false;disableReason=reason;}invalidate();}
};

class MirrorStateBlock final : public IDirect3DStateBlock9 {
    LONG refs=1;IDirect3DStateBlock9* real;IDirect3DDevice9* owner;DeviceMirror* mirror;
public:
    MirrorStateBlock(IDirect3DStateBlock9* block,IDirect3DDevice9* device,DeviceMirror* m)
        :real(block),owner(device),mirror(m){owner->AddRef();}
    ~MirrorStateBlock(){
        {MirrorGuard lock(mirror->gate,MirrorSite::StateBlock);real->Release();}
        owner->Release(); // May destroy the owner and gate: never under its lock.
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;
        if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3DStateBlock9)){*out=this;AddRef();return S_OK;}
        MirrorGuard lock(mirror->gate,MirrorSite::StateBlock);
        HRESULT hr=real->QueryInterface(id,out);if(SUCCEEDED(hr)&&*out)mirror->disable("state-block-interface");return hr;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** out) override{if(!out)return D3DERR_INVALIDCALL;*out=owner;owner->AddRef();return D3D_OK;}
    HRESULT STDMETHODCALLTYPE Capture() override{MirrorGuard lock(mirror->gate,MirrorSite::StateBlock);return real->Capture();}
    HRESULT STDMETHODCALLTYPE Apply() override{
        MirrorGuard lock(mirror->gate,MirrorSite::StateBlock);
        // Also invalidate failures: a backend may have changed part of the state.
        HRESULT hr=real->Apply();mirror->invalidate();return hr;
    }
};

class MirrorDevice : public ForwardIDirect3DDevice9 {
protected:
    DeviceMirror* m;
    using Guard=MirrorGuard;
    template<class T> static void giveRef(T* p,T** out){if(p)p->AddRef();*out=p;}
    bool accepted(HRESULT hr){++m->mutations;if(FAILED(hr)){m->invalidate();return false;}return m->active();}
    HRESULT wrapBlock(HRESULT hr,IDirect3DStateBlock9* block,IDirect3DStateBlock9** out){
        *out=block;
        if(SUCCEEDED(hr)&&block){
            try{*out=new MirrorStateBlock(block,this,m);}
            catch(...){m->disable("state-block-allocation");} // Preserve the successful original API result.
        }
        return hr;
    }
public:
    MirrorDevice(IDirect3DDevice9* device,DeviceMirror* mirror):ForwardIDirect3DDevice9(device),m(mirror){}
    // Setters: failure invalidates everything; an accepted write outside
    // recording/raw scopes stores its value and becomes known only for a
    // trusted slot (written()). Null resources never write through: backends
    // differ on null stream/target/depth/index reads (retention, NOTFOUND).
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* value) override{
        Guard lock(m->gate);const std::uint64_t serial=m->mutations+1;HRESULT hr=real->SetVertexShader(value);m->vertexShaderKnown=false;
        if(accepted(hr)){m->vertexShader=value;m->vertexShaderKnown=m->written(DeviceMirror::VertexShaderSlot,serial);}else m->forget(DeviceMirror::VertexShaderSlot);return hr;
    }
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** out) override{
        Guard lock(m->gate);if(out&&m->active()&&m->vertexShaderKnown){++m->answered;giveRef(m->vertexShader,out);return D3D_OK;}
        ++m->forwarded;HRESULT hr=real->GetVertexShader(out);
        if(out&&m->active()){m->observed(DeviceMirror::VertexShaderSlot,SUCCEEDED(hr)&&*out==m->vertexShader);if(SUCCEEDED(hr)){m->vertexShader=*out;m->vertexShaderKnown=true;}}return hr;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* value) override{
        Guard lock(m->gate);const std::uint64_t serial=m->mutations+1;HRESULT hr=real->SetPixelShader(value);m->pixelShaderKnown=false;
        if(accepted(hr)){m->pixelShader=value;m->pixelShaderKnown=m->written(DeviceMirror::PixelShaderSlot,serial);}else m->forget(DeviceMirror::PixelShaderSlot);return hr;
    }
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** out) override{
        Guard lock(m->gate);if(out&&m->active()&&m->pixelShaderKnown){++m->answered;giveRef(m->pixelShader,out);return D3D_OK;}
        ++m->forwarded;HRESULT hr=real->GetPixelShader(out);
        if(out&&m->active()){m->observed(DeviceMirror::PixelShaderSlot,SUCCEEDED(hr)&&*out==m->pixelShader);if(SUCCEEDED(hr)){m->pixelShader=*out;m->pixelShaderKnown=true;}}return hr;
    }
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* value) override{
        Guard lock(m->gate);const std::uint64_t serial=m->mutations+1;HRESULT hr=real->SetVertexDeclaration(value);m->declarationKnown=false;m->fvfKnown=false;
        if(accepted(hr)&&value){m->declaration=value;m->declarationKnown=m->written(DeviceMirror::DeclarationSlot,serial);}else m->forget(DeviceMirror::DeclarationSlot);return hr;
    }
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** out) override{
        Guard lock(m->gate);if(out&&m->active()&&m->declarationKnown){++m->answered;giveRef(m->declaration,out);return D3D_OK;}
        ++m->forwarded;HRESULT hr=real->GetVertexDeclaration(out);
        if(out&&m->active()){m->observed(DeviceMirror::DeclarationSlot,SUCCEEDED(hr)&&*out==m->declaration);if(SUCCEEDED(hr)){m->declaration=*out;m->declarationKnown=true;}}return hr;
    }
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* value) override{
        Guard lock(m->gate);const std::uint64_t serial=m->mutations+1;HRESULT hr=real->SetIndices(value);m->indicesKnown=false;
        if(accepted(hr)&&value){m->indices=value;m->indicesKnown=m->written(DeviceMirror::IndicesSlot,serial);}else m->forget(DeviceMirror::IndicesSlot);return hr;
    }
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** out) override{
        Guard lock(m->gate);if(out&&m->active()&&m->indicesKnown){++m->answered;giveRef(m->indices,out);return D3D_OK;}
        ++m->forwarded;HRESULT hr=real->GetIndices(out);
        if(out&&m->active()){m->observed(DeviceMirror::IndicesSlot,SUCCEEDED(hr)&&*out==m->indices);if(SUCCEEDED(hr)){m->indices=*out;m->indicesKnown=true;}}return hr;
    }
    // FVF aliases the declaration in both directions and never writes through.
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD value) override{Guard lock(m->gate);HRESULT hr=real->SetFVF(value);m->fvfKnown=m->declarationKnown=false;m->forget(DeviceMirror::DeclarationSlot);accepted(hr);return hr;}
    HRESULT STDMETHODCALLTYPE GetFVF(DWORD* out) override{
        Guard lock(m->gate);if(out&&m->active()&&m->fvfKnown){++m->answered;*out=m->fvf;return D3D_OK;}
        ++m->forwarded;HRESULT hr=real->GetFVF(out);if(out&&SUCCEEDED(hr)&&m->active()){m->fvf=*out;m->fvfKnown=true;}return hr;
    }
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT n,IDirect3DVertexBuffer9* b,UINT offset,UINT stride) override{
        Guard lock(m->gate);const std::uint64_t serial=m->mutations+1;HRESULT hr=real->SetStreamSource(n,b,offset,stride);const bool slot=n<DeviceMirror::Streams;if(slot)m->streams[n].known=false;
        if(accepted(hr)&&slot&&b){auto& s=m->streams[n];s.buffer=b;s.offset=offset;s.stride=stride;s.known=m->written(DeviceMirror::StreamSlot+n,serial);}
        else if(slot)m->forget(DeviceMirror::StreamSlot+n);return hr;
    }
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT n,IDirect3DVertexBuffer9** out,UINT* offset,UINT* stride) override{
        Guard lock(m->gate);bool valid=n<DeviceMirror::Streams&&out&&offset&&stride;
        if(valid&&m->active()&&m->streams[n].known){auto& s=m->streams[n];++m->answered;giveRef(s.buffer,out);*offset=s.offset;*stride=s.stride;return D3D_OK;}
        ++m->forwarded;HRESULT hr=real->GetStreamSource(n,out,offset,stride);
        if(valid&&m->active()){auto& s=m->streams[n];m->observed(DeviceMirror::StreamSlot+n,SUCCEEDED(hr)&&*out==s.buffer&&*offset==s.offset&&*stride==s.stride);
            if(SUCCEEDED(hr)){s.buffer=*out;s.offset=*offset;s.stride=*stride;s.known=true;}}return hr;
    }
    // Backends normalize/validate frequencies differently: read-through only.
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT n,UINT value) override{Guard lock(m->gate);HRESULT hr=real->SetStreamSourceFreq(n,value);if(n<DeviceMirror::Streams)m->frequencyKnown[n]=false;accepted(hr);return hr;}
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT n,UINT* out) override{
        Guard lock(m->gate);bool valid=n<DeviceMirror::Streams&&out;
        if(valid&&m->active()&&m->frequencyKnown[n]){++m->answered;*out=m->frequency[n];return D3D_OK;}
        ++m->forwarded;HRESULT hr=real->GetStreamSourceFreq(n,out);if(valid&&SUCCEEDED(hr)&&m->active()){m->frequency[n]=*out;m->frequencyKnown[n]=true;}return hr;
    }
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage,IDirect3DBaseTexture9* t) override{
        Guard lock(m->gate);const std::uint64_t serial=m->mutations+1;HRESULT hr=real->SetTexture(stage,t);const bool slot=stage<DeviceMirror::Textures;if(slot)m->textureKnown[stage]=false;
        if(accepted(hr)&&slot){m->textures[stage]=t;m->textureKnown[stage]=m->written(DeviceMirror::TextureSlot+stage,serial);}else if(slot)m->forget(DeviceMirror::TextureSlot+stage);return hr;
    }
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD stage,IDirect3DBaseTexture9** out) override{
        Guard lock(m->gate);bool valid=stage<DeviceMirror::Textures&&out;
        if(valid&&m->active()&&m->textureKnown[stage]){++m->answered;giveRef(m->textures[stage],out);return D3D_OK;}
        ++m->forwarded;HRESULT hr=real->GetTexture(stage,out);
        if(valid&&m->active()){m->observed(DeviceMirror::TextureSlot+stage,SUCCEEDED(hr)&&*out==m->textures[stage]);if(SUCCEEDED(hr)){m->textures[stage]=*out;m->textureKnown[stage]=true;}}return hr;
    }
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD n,D3DSAMPLERSTATETYPE type,DWORD value) override{
        Guard lock(m->gate);const std::uint64_t serial=m->mutations+1;HRESULT hr=real->SetSamplerState(n,type,value);const bool slot=n<DeviceMirror::Textures&&unsigned(type)<DeviceMirror::SamplerTypes;
        if(slot)m->samplerKnown[n][type]=false;const unsigned id=slot?DeviceMirror::SamplerSlot+n*DeviceMirror::SamplerTypes+unsigned(type):0;
        if(accepted(hr)&&slot){m->samplers[n][type]=value;m->samplerKnown[n][type]=m->written(id,serial,m->isProven(DeviceMirror::pair(id,value)));}else if(slot)m->forget(id);return hr;
    }
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD n,D3DSAMPLERSTATETYPE type,DWORD* out) override{
        Guard lock(m->gate);bool valid=n<DeviceMirror::Textures&&unsigned(type)<DeviceMirror::SamplerTypes&&out;
        if(valid&&m->active()&&m->samplerKnown[n][type]){++m->answered;*out=m->samplers[n][type];return D3D_OK;}
        ++m->forwarded;HRESULT hr=real->GetSamplerState(n,type,out);
        if(valid&&m->active()){const unsigned id=DeviceMirror::SamplerSlot+n*DeviceMirror::SamplerTypes+unsigned(type);const bool same=SUCCEEDED(hr)&&*out==m->samplers[n][type];
            if(same&&m->pending[id])m->prove(DeviceMirror::pair(id,*out));m->observed(id,same);
            if(SUCCEEDED(hr)){m->samplers[n][type]=*out;m->samplerKnown[n][type]=true;}}return hr;
    }
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE type,DWORD value) override{
        Guard lock(m->gate);const std::uint64_t serial=m->mutations+1;HRESULT hr=real->SetRenderState(type,value);const bool slot=unsigned(type)<DeviceMirror::RenderStates;
        if(slot)m->renderStateKnown[type]=false;
        // Vendor command states (RESZ/ATOC etc.) may have implicit side effects.
        const bool vendor=type==D3DRS_POINTSIZE||type==D3DRS_ADAPTIVETESS_Y;if(vendor)m->invalidate();
        if(accepted(hr)&&slot&&!vendor){m->renderStates[type]=value;const unsigned id=DeviceMirror::RenderStateSlot+unsigned(type);m->renderStateKnown[type]=m->written(id,serial,m->isProven(DeviceMirror::pair(id,value)));}
        else if(slot)m->forget(DeviceMirror::RenderStateSlot+unsigned(type));return hr;
    }
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE type,DWORD* out) override{
        Guard lock(m->gate);bool valid=unsigned(type)<DeviceMirror::RenderStates&&out;
        if(valid&&m->active()&&m->renderStateKnown[type]){++m->answered;*out=m->renderStates[type];return D3D_OK;}
        ++m->forwarded;HRESULT hr=real->GetRenderState(type,out);
        if(valid&&m->active()){const unsigned id=DeviceMirror::RenderStateSlot+unsigned(type);const bool same=SUCCEEDED(hr)&&*out==m->renderStates[type];
            if(same&&m->pending[id])m->prove(DeviceMirror::pair(id,*out));m->observed(id,same);
            if(SUCCEEDED(hr)){m->renderStates[type]=*out;m->renderStateKnown[type]=true;}}return hr;
    }
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9* value) override{
        Guard lock(m->gate);const std::uint64_t serial=m->mutations+1;HRESULT hr=real->SetViewport(value);m->viewportKnown=false;
        if(accepted(hr)&&value){m->viewport=*value;m->viewportKnown=m->written(DeviceMirror::ViewportSlot,serial,m->isProven(*value));}else m->forget(DeviceMirror::ViewportSlot);return hr;
    }
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* out) override{
        Guard lock(m->gate);if(out&&m->active()&&m->viewportKnown){++m->answered;*out=m->viewport;return D3D_OK;}
        ++m->forwarded;HRESULT hr=real->GetViewport(out);
        if(out&&m->active()){const bool same=SUCCEEDED(hr)&&!std::memcmp(out,&m->viewport,sizeof m->viewport);
            if(same&&m->pending[DeviceMirror::ViewportSlot])m->prove(*out);m->observed(DeviceMirror::ViewportSlot,same);if(SUCCEEDED(hr)){m->viewport=*out;m->viewportKnown=true;}}return hr;
    }
    // Target 0 implicitly resets viewport (and scissor): never computed here.
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD n,IDirect3DSurface9* value) override{
        Guard lock(m->gate);const std::uint64_t serial=m->mutations+1;HRESULT hr=real->SetRenderTarget(n,value);const bool slot=n<DeviceMirror::Targets;if(slot)m->targetKnown[n]=false;
        if(n==0){m->viewportKnown=false;m->forget(DeviceMirror::ViewportSlot);}
        if(accepted(hr)&&slot&&value){m->targets[n]=value;m->targetKnown[n]=m->written(DeviceMirror::TargetSlot+n,serial);}else if(slot)m->forget(DeviceMirror::TargetSlot+n);return hr;
    }
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD n,IDirect3DSurface9** out) override{
        Guard lock(m->gate);bool valid=n<DeviceMirror::Targets&&out;
        if(valid&&m->active()&&m->targetKnown[n]){++m->answered;giveRef(m->targets[n],out);return m->targets[n]?D3D_OK:D3DERR_NOTFOUND;}
        ++m->forwarded;HRESULT hr=real->GetRenderTarget(n,out);
        if(valid&&m->active()){m->observed(DeviceMirror::TargetSlot+n,SUCCEEDED(hr)&&*out==m->targets[n]);if(SUCCEEDED(hr)&&*out){m->targets[n]=*out;m->targetKnown[n]=true;}}return hr;
    }
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* value) override{
        Guard lock(m->gate);const std::uint64_t serial=m->mutations+1;HRESULT hr=real->SetDepthStencilSurface(value);m->depthKnown=false;
        if(accepted(hr)&&value){m->depth=value;m->depthKnown=m->written(DeviceMirror::DepthSlot,serial);}else m->forget(DeviceMirror::DepthSlot);return hr;
    }
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** out) override{
        Guard lock(m->gate);if(out&&m->active()&&m->depthKnown){++m->answered;giveRef(m->depth,out);return m->depth?D3D_OK:D3DERR_NOTFOUND;}
        ++m->forwarded;HRESULT hr=real->GetDepthStencilSurface(out);
        if(out&&m->active()){m->observed(DeviceMirror::DepthSlot,SUCCEEDED(hr)&&*out==m->depth);if(SUCCEEDED(hr)&&*out){m->depth=*out;m->depthKnown=true;}}return hr;
    }
    // Borrowed identities for immediate internal checks (no COM reference):
    // valid only while the binding is unchanged, so callers must finish using
    // the pointer before any device write. False means "use the Get* method".
    bool peekRenderTarget(DWORD n,IDirect3DSurface9*& out){
        Guard lock(m->gate);if(!kMirrorBorrowedPeek||n>=DeviceMirror::Targets||!m->active()||!m->targetKnown[n]||!m->targets[n])return false;
        ++m->answered;++m->peeks;out=m->targets[n];return true;
    }
    bool peekPixelShader(IDirect3DPixelShader9*& out){
        Guard lock(m->gate);if(!kMirrorBorrowedPeek||!m->active()||!m->pixelShaderKnown||!m->pixelShader)return false;
        ++m->answered;++m->peeks;out=m->pixelShader;return true;
    }
    // 0.3.196 (task 12): the mirror holds no reference (Set* stores raw pointers), so a borrowed shader/texture pointer is valid only while it stays bound.
    bool peekVertexShader(IDirect3DVertexShader9*& out){
        Guard lock(m->gate);if(!kMirrorBorrowedPeek||!m->active()||!m->vertexShaderKnown||!m->vertexShader)return false;
        ++m->answered;++m->peeks;out=m->vertexShader;return true;
    }
    bool peekTexture(DWORD stage,IDirect3DBaseTexture9*& out){
        Guard lock(m->gate);if(!kMirrorBorrowedPeek||stage>=DeviceMirror::Textures||!m->active()||!m->textureKnown[stage]||!m->textures[stage])return false;
        ++m->answered;++m->peeks;out=m->textures[stage];return true;
    }
    bool peekDepthStencilSurface(IDirect3DSurface9*& out){
        Guard lock(m->gate);if(!kMirrorBorrowedPeek||!m->active()||!m->depthKnown||!m->depth)return false;
        ++m->answered;++m->peeks;out=m->depth;return true;
    }
    // Writes refresh previously observed registers. They never establish Get*
    // support: pure-device/unsupported getter errors must still reach backend.
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT start,const float* data,UINT count) override{
        Guard lock(m->gate);HRESULT hr=real->SetVertexShaderConstantF(start,data,count);
        if(SUCCEEDED(hr)&&m->active())m->constantEpoch.writeFloat(start,count);
        if(accepted(hr)&&data&&start<DeviceMirror::VsFloat){const UINT copied=count<DeviceMirror::VsFloat-start?count:DeviceMirror::VsFloat-start;std::memcpy(m->vsFloat[start],data,size_t(copied)*4*sizeof(float));}return hr;
    }
    // 0.3.200 (frame trace): the backend's own registers, never the mirror (diagnostics only).
    HRESULT backendVertexShaderConstantF(UINT start,float* out,UINT count){Guard lock(m->gate);return real->GetVertexShaderConstantF(start,out,count);}
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT start,float* out,UINT count) override{
        Guard lock(m->gate);bool valid=out&&count&&start<DeviceMirror::VsFloat&&count<=DeviceMirror::VsFloat-start;
        const bool known=valid&&m->active()&&allKnown(m->vsFloatKnown+start,count);
        if(known){++m->answered;std::memcpy(out,m->vsFloat[start],size_t(count)*4*sizeof(float));return D3D_OK;}
        ++m->forwarded;HRESULT hr=real->GetVertexShaderConstantF(start,out,count);
        if(valid&&SUCCEEDED(hr)&&m->active()){std::memcpy(m->vsFloat[start],out,size_t(count)*4*sizeof(float));for(UINT i=0;i<count;++i)m->vsFloatKnown[start+i]=true;}return hr;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT start,const int* data,UINT count) override{
        Guard lock(m->gate);HRESULT hr=real->SetVertexShaderConstantI(start,data,count);
        if(SUCCEEDED(hr)&&m->active())m->constantEpoch.writeInt(count);
        if(accepted(hr)&&data&&start<DeviceMirror::Ints){const UINT copied=count<DeviceMirror::Ints-start?count:DeviceMirror::Ints-start;std::memcpy(m->vsInt[start],data,size_t(copied)*4*sizeof(int));}return hr;
    }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT start,int* out,UINT count) override{
        Guard lock(m->gate);bool valid=out&&count&&start<DeviceMirror::Ints&&count<=DeviceMirror::Ints-start;
        const bool known=valid&&m->active()&&allKnown(m->vsIntKnown+start,count);
        if(known){++m->answered;std::memcpy(out,m->vsInt[start],size_t(count)*4*sizeof(int));return D3D_OK;}
        ++m->forwarded;HRESULT hr=real->GetVertexShaderConstantI(start,out,count);
        if(valid&&SUCCEEDED(hr)&&m->active()){std::memcpy(m->vsInt[start],out,size_t(count)*4*sizeof(int));for(UINT i=0;i<count;++i)m->vsIntKnown[start+i]=true;}return hr;
    }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT start,const WINBOOL* data,UINT count) override{
        Guard lock(m->gate);HRESULT hr=real->SetVertexShaderConstantB(start,data,count);
        if(SUCCEEDED(hr)&&m->active())m->constantEpoch.writeBool(count);
        if(accepted(hr)&&data&&start<DeviceMirror::Bools){const UINT copied=count<DeviceMirror::Bools-start?count:DeviceMirror::Bools-start;for(UINT i=0;i<copied;++i)m->vsBoolKnown[start+i]=false;}return hr;
    }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT start,WINBOOL* out,UINT count) override{
        Guard lock(m->gate);bool valid=out&&count&&start<DeviceMirror::Bools&&count<=DeviceMirror::Bools-start;
        const bool known=valid&&m->active()&&allKnown(m->vsBoolKnown+start,count);
        if(known){++m->answered;std::memcpy(out,m->vsBool+start,size_t(count)*1*sizeof(WINBOOL));return D3D_OK;}
        ++m->forwarded;HRESULT hr=real->GetVertexShaderConstantB(start,out,count);
        if(valid&&SUCCEEDED(hr)&&m->active()){std::memcpy(m->vsBool+start,out,size_t(count)*1*sizeof(WINBOOL));for(UINT i=0;i<count;++i)m->vsBoolKnown[start+i]=true;}return hr;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT start,const float* data,UINT count) override{
        Guard lock(m->gate);HRESULT hr=real->SetPixelShaderConstantF(start,data,count);
        if(accepted(hr)&&data&&start<DeviceMirror::PsFloat){const UINT copied=count<DeviceMirror::PsFloat-start?count:DeviceMirror::PsFloat-start;std::memcpy(m->psFloat[start],data,size_t(copied)*4*sizeof(float));}return hr;
    }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT start,float* out,UINT count) override{
        Guard lock(m->gate);bool valid=out&&count&&start<DeviceMirror::PsFloat&&count<=DeviceMirror::PsFloat-start;
        const bool known=valid&&m->active()&&allKnown(m->psFloatKnown+start,count);
        if(known){++m->answered;std::memcpy(out,m->psFloat[start],size_t(count)*4*sizeof(float));return D3D_OK;}
        ++m->forwarded;HRESULT hr=real->GetPixelShaderConstantF(start,out,count);
        if(valid&&SUCCEEDED(hr)&&m->active()){std::memcpy(m->psFloat[start],out,size_t(count)*4*sizeof(float));for(UINT i=0;i<count;++i)m->psFloatKnown[start+i]=true;}return hr;
    }
    bool constantStamp(NorthlightConstantEpoch::Stamp& out){
        Guard lock(m->gate);out=m->constantEpoch.stamp(m->active());return out.valid;
    }
    // Diagnostic reads use the backend directly and do not warm the cache.
    // Called at a bounded cadence under the same gate as all intercepted writes.
    const char* audit(){
        Guard lock(m->gate);if(!m->active())return nullptr;++m->checks;
        struct Coverage {DeviceMirror* mirror;std::uint64_t before;
            ~Coverage(){if(mirror->checkedFields==before)++mirror->emptyChecks;}
        } coverage{m,m->checkedFields};
        auto mismatch=[&](const char* field){++m->mismatches;m->disable(field);return field;};
        if(m->vertexShaderKnown){++m->checkedFields;IDirect3DVertexShader9* p=nullptr;HRESULT hr=real->GetVertexShader(&p);bool same=SUCCEEDED(hr)&&p==m->vertexShader;if(p)p->Release();if(!same)return mismatch("vertexShader");}
        if(m->pixelShaderKnown){++m->checkedFields;IDirect3DPixelShader9* p=nullptr;HRESULT hr=real->GetPixelShader(&p);bool same=SUCCEEDED(hr)&&p==m->pixelShader;if(p)p->Release();if(!same)return mismatch("pixelShader");}
        if(m->declarationKnown){++m->checkedFields;IDirect3DVertexDeclaration9* p=nullptr;HRESULT hr=real->GetVertexDeclaration(&p);bool same=SUCCEEDED(hr)&&p==m->declaration;if(p)p->Release();if(!same)return mismatch("declaration");}
        if(m->indicesKnown){++m->checkedFields;IDirect3DIndexBuffer9* p=nullptr;HRESULT hr=real->GetIndices(&p);bool same=SUCCEEDED(hr)&&p==m->indices;if(p)p->Release();if(!same)return mismatch("indices");}
        if(m->fvfKnown){++m->checkedFields;DWORD value=0;if(FAILED(real->GetFVF(&value))||value!=m->fvf)return mismatch("fvf");}
        if(m->viewportKnown){++m->checkedFields;D3DVIEWPORT9 value={};if(FAILED(real->GetViewport(&value))||std::memcmp(&value,&m->viewport,sizeof value))return mismatch("viewport");}
        for(unsigned i=0;i<DeviceMirror::Streams;++i){
            const auto& s=m->streams[i];if(s.known){++m->checkedFields;IDirect3DVertexBuffer9* p=nullptr;UINT offset=0,stride=0;HRESULT hr=real->GetStreamSource(i,&p,&offset,&stride);bool same=SUCCEEDED(hr)&&p==s.buffer&&offset==s.offset&&stride==s.stride;if(p)p->Release();if(!same)return mismatch("stream");}
            if(m->frequencyKnown[i]){++m->checkedFields;UINT value=0;if(FAILED(real->GetStreamSourceFreq(i,&value))||value!=m->frequency[i])return mismatch("frequency");}
        }
        for(unsigned i=0;i<DeviceMirror::Textures;++i){
            if(m->textureKnown[i]){++m->checkedFields;IDirect3DBaseTexture9* p=nullptr;HRESULT hr=real->GetTexture(i,&p);bool same=SUCCEEDED(hr)&&p==m->textures[i];if(p)p->Release();if(!same)return mismatch("texture");}
            for(unsigned j=0;j<DeviceMirror::SamplerTypes;++j)if(m->samplerKnown[i][j]){++m->checkedFields;DWORD value=0;if(FAILED(real->GetSamplerState(i,D3DSAMPLERSTATETYPE(j),&value))||value!=m->samplers[i][j])return mismatch("sampler");}
        }
        for(unsigned i=0;i<DeviceMirror::RenderStates;++i)if(m->renderStateKnown[i]){++m->checkedFields;DWORD value=0;if(FAILED(real->GetRenderState(D3DRENDERSTATETYPE(i),&value))||value!=m->renderStates[i])return mismatch("renderState");}
        for(unsigned i=0;i<DeviceMirror::Targets;++i)if(m->targetKnown[i]){++m->checkedFields;IDirect3DSurface9* p=nullptr;HRESULT hr=real->GetRenderTarget(i,&p);bool same=SUCCEEDED(hr)&&p==m->targets[i];if(p)p->Release();if(!same)return mismatch("target");}
        if(m->depthKnown){++m->checkedFields;IDirect3DSurface9* p=nullptr;HRESULT hr=real->GetDepthStencilSurface(&p);bool same=SUCCEEDED(hr)&&p==m->depth;if(p)p->Release();if(!same)return mismatch("depth");}
        for(unsigned begin=0;begin<DeviceMirror::VsFloat;){
            if(!m->vsFloatKnown[begin]){++begin;continue;}unsigned end=begin+1;while(end<DeviceMirror::VsFloat&&m->vsFloatKnown[end])++end;
            ++m->checkedFields;m->checkedRegisters+=end-begin;float values[DeviceMirror::VsFloat*4];
            if(FAILED(real->GetVertexShaderConstantF(begin,values,end-begin))||std::memcmp(values,m->vsFloat[begin],(end-begin)*4*sizeof(float)))return mismatch("vsFloat");begin=end;
        }
        for(unsigned begin=0;begin<DeviceMirror::Ints;){
            if(!m->vsIntKnown[begin]){++begin;continue;}unsigned end=begin+1;while(end<DeviceMirror::Ints&&m->vsIntKnown[end])++end;
            ++m->checkedFields;m->checkedRegisters+=end-begin;int values[DeviceMirror::Ints*4];
            if(FAILED(real->GetVertexShaderConstantI(begin,values,end-begin))||std::memcmp(values,m->vsInt[begin],(end-begin)*4*sizeof(int)))return mismatch("vsInt");begin=end;
        }
        for(unsigned begin=0;begin<DeviceMirror::Bools;){
            if(!m->vsBoolKnown[begin]){++begin;continue;}unsigned end=begin+1;while(end<DeviceMirror::Bools&&m->vsBoolKnown[end])++end;
            ++m->checkedFields;m->checkedRegisters+=end-begin;WINBOOL values[DeviceMirror::Bools*1];
            if(FAILED(real->GetVertexShaderConstantB(begin,values,end-begin))||std::memcmp(values,m->vsBool+begin,(end-begin)*1*sizeof(WINBOOL)))return mismatch("vsBool");begin=end;
        }
        for(unsigned begin=0;begin<DeviceMirror::PsFloat;){
            if(!m->psFloatKnown[begin]){++begin;continue;}unsigned end=begin+1;while(end<DeviceMirror::PsFloat&&m->psFloatKnown[end])++end;
            ++m->checkedFields;m->checkedRegisters+=end-begin;float values[DeviceMirror::PsFloat*4];
            if(FAILED(real->GetPixelShaderConstantF(begin,values,end-begin))||std::memcmp(values,m->psFloat[begin],(end-begin)*4*sizeof(float)))return mismatch("psFloat");begin=end;
        }
        return nullptr;
    }
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override{
        Guard lock(m->gate);HRESULT hr=real->BeginStateBlock();m->invalidate();if(SUCCEEDED(hr))m->recording=true;else m->disable("state-block-begin-failed");return hr;
    }
    HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9** out) override{
        Guard lock(m->gate);if(!out){HRESULT hr=real->EndStateBlock(out);m->disable("state-block-end-null");return hr;}
        IDirect3DStateBlock9* block=nullptr;HRESULT hr=real->EndStateBlock(&block);m->invalidate();
        if(SUCCEEDED(hr))m->recording=false;else m->disable("state-block-end-failed");
        return wrapBlock(hr,block,out);
    }
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE type,IDirect3DStateBlock9** out) override{
        Guard lock(m->gate);if(!out)return real->CreateStateBlock(type,out);
        IDirect3DStateBlock9* block=nullptr;HRESULT hr=real->CreateStateBlock(type,&block);return wrapBlock(hr,block,out);
    }
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT count,const void* data,UINT stride) override{
        Guard lock(m->gate);HRESULT hr=real->DrawPrimitiveUP(type,count,data,stride);m->streams[0].known=false;m->forget(DeviceMirror::StreamSlot);accepted(hr);return hr;
    }
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type,UINT minimum,UINT vertices,UINT count,const void* indices,D3DFORMAT format,const void* data,UINT stride) override{
        Guard lock(m->gate);HRESULT hr=real->DrawIndexedPrimitiveUP(type,minimum,vertices,count,indices,format,data,stride);m->streams[0].known=false;m->indicesKnown=false;m->forget(DeviceMirror::StreamSlot);m->forget(DeviceMirror::IndicesSlot);accepted(hr);return hr;
    }
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* params) override{
        Guard lock(m->gate);m->invalidate();HRESULT hr=real->Reset(params);m->invalidate();if(SUCCEEDED(hr))m->recording=false;return hr;
    }
    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(WINBOOL value) override{
        Guard lock(m->gate);HRESULT hr=real->SetSoftwareVertexProcessing(value);m->invalidate();return hr;
    }
};

// Extension calls share cache + lock but do not re-enter game hooks or wrap
// resources. Lifetime is enclosed by Device and its internal state-block pools.
class ExtensionDevice final : public MirrorDevice {
    LONG refs=1;
public:
    class RawScope;
private:
    // Only the owning thread may bypass the per-call gate. Other callers take
    // the normal MirrorDevice path; shared rawDepth is never read lock-free.
    static inline thread_local RawScope* currentRaw=nullptr;
    bool rawActive()const;
public:
    // 0.3.192 (CS): "raw" callers (the renderer's own work) run on the thread that executes the Device: the game
    // thread on the direct path, the replay thread in stream mode. The gate's owner is that same thread either way.
    class RawScope {
        friend class ExtensionDevice;
        ExtensionDevice* device;
        MirrorGuard lock; // 0.3.180 (D0): a member, released after the destructor body as before
        RawScope* previous;
    public:
        explicit RawScope(ExtensionDevice& owner)
            :device(&owner),lock(owner.m->gate,MirrorSite::Raw),previous(currentRaw){
            owner.m->invalidate();++owner.m->rawDepth;++owner.m->rawScopes;
            currentRaw=this;
        }
        RawScope(const RawScope&)=delete;RawScope& operator=(const RawScope&)=delete;
        ~RawScope(){
            // Construct before SavedState so restoration runs while suspended.
            // Never restore enabled/recording: an escape or control failure may
            // have permanently disabled caching while this scope was active.
            device->m->invalidate();--device->m->rawDepth;currentRaw=previous;
        }
    };
#include "extension_raw_methods.inl"

    ExtensionDevice(IDirect3DDevice9* device,DeviceMirror* mirror):MirrorDevice(device,mirror){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override{
        if(!out)return E_POINTER;
        if(id==__uuidof(IUnknown)||id==__uuidof(IDirect3DDevice9)){*out=this;AddRef();return S_OK;}
        *out=nullptr;return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
};

inline bool ExtensionDevice::rawActive()const{return currentRaw&&currentRaw->device==this;}
