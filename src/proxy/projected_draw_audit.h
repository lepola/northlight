#pragma once
#include <d3d9.h>
#include <array>
#include <cstdint>

// Sampled diagnostics only. Reference draws change neither colour, depth nor stencil.
// Results are polled without FLUSH; a full query pool simply skips the probe.
class NorthlightProjectedDrawAudit {
public:
    struct Result {
        unsigned frame=0,phase=0,primitives=0;
        DWORD visible=0,potential=0,source=0,destination=0,depthFunction=0;
        DWORD depthBias=0,slopeBias=0,fvf=0,alphaTest=0;
        std::uintptr_t vertex=0,pixel=0,texture=0;
        HRESULT status=S_OK;
    };
private:
    struct Slot {IDirect3DQuery9* visible=nullptr,*potential=nullptr;Result result;bool pending=false;};
    std::array<Slot,32> slots{};
    unsigned sampleFrame=~0u,samples=0;
    bool unsupported=false;
public:
    ~NorthlightProjectedDrawAudit(){reset();}
    bool canSample(unsigned frame)const{return !unsupported&&(frame!=sampleFrame||samples<16);}
    unsigned probes(unsigned frame)const{return sampleFrame==frame?samples:0;}
    unsigned pending()const{unsigned n=0;for(const auto& s:slots)n+=s.pending;return n;}
    bool disabled()const{return unsupported;}
    void reset(){for(auto& s:slots){if(s.visible)s.visible->Release();if(s.potential)s.potential->Release();s={};}unsupported=false;sampleFrame=~0u;samples=0;}
    template<class Emit> void poll(Emit emit){
        for(auto& s:slots)if(s.pending){
            const HRESULT a=s.visible->GetData(&s.result.visible,sizeof(DWORD),0);
            const HRESULT b=s.potential->GetData(&s.result.potential,sizeof(DWORD),0);
            if(a==S_FALSE||b==S_FALSE)continue;
            if(FAILED(a)||FAILED(b))s.result.status=FAILED(a)?a:b;
            emit(s.result);s.pending=false;
        }
    }
    template<class Draw> HRESULT draw(IDirect3DDevice9* d,unsigned frame,unsigned phase,unsigned primitives,Draw native){
        if(unsupported||!primitives||primitives>256)return native();
        if(sampleFrame!=frame){sampleFrame=frame;samples=0;}
        if(samples>=16)return native();
        DWORD write=1,blend=0,z=0,stencil=1,color=0,src=0,dst=0,op=0,zfunc=0;
        auto read=[&](D3DRENDERSTATETYPE k,DWORD& v){return SUCCEEDED(d->GetRenderState(k,&v));};
        if(!read(D3DRS_ZWRITEENABLE,write)||write||!read(D3DRS_ALPHABLENDENABLE,blend)||!blend||
           !read(D3DRS_ZENABLE,z)||z!=D3DZB_TRUE||!read(D3DRS_STENCILENABLE,stencil)||stencil||
           !read(D3DRS_COLORWRITEENABLE,color)||!(color&7)||!read(D3DRS_SRCBLEND,src)||
           !read(D3DRS_DESTBLEND,dst)||!read(D3DRS_BLENDOP,op)||op!=D3DBLENDOP_ADD||
           !read(D3DRS_ZFUNC,zfunc)||zfunc==D3DCMP_ALWAYS||zfunc==D3DCMP_NEVER)return native();
        IDirect3DSurface9* extra=nullptr;d->GetRenderTarget(1,&extra);
        if(extra){extra->Release();return native();}
        Slot* slot=nullptr;for(auto& s:slots)if(!s.pending){slot=&s;break;}if(!slot)return native();
        auto& s=*slot;
        if((!s.visible&&FAILED(d->CreateQuery(D3DQUERYTYPE_OCCLUSION,&s.visible)))||
           (!s.potential&&FAILED(d->CreateQuery(D3DQUERYTYPE_OCCLUSION,&s.potential)))){unsupported=true;return native();}
        s.result={};auto& r=s.result;r.frame=frame;r.phase=phase;r.primitives=primitives;r.source=src;r.destination=dst;r.depthFunction=zfunc;
        read(D3DRS_DEPTHBIAS,r.depthBias);read(D3DRS_SLOPESCALEDEPTHBIAS,r.slopeBias);read(D3DRS_ALPHATESTENABLE,r.alphaTest);d->GetFVF(&r.fvf);
        IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;IDirect3DBaseTexture9* t=nullptr;
        d->GetVertexShader(&vs);d->GetPixelShader(&ps);d->GetTexture(0,&t);
        r.vertex=reinterpret_cast<std::uintptr_t>(vs);r.pixel=reinterpret_cast<std::uintptr_t>(ps);r.texture=reinterpret_cast<std::uintptr_t>(t);
        if(vs)vs->Release();if(ps)ps->Release();if(t)t->Release();
        ++samples;
        // Probe BEFORE the real call: UP calls unbind stream 0/indices. The real
        // call retains those API side effects and is issued exactly once.
        HRESULT probe=d->SetRenderState(D3DRS_COLORWRITEENABLE,0);
        if(SUCCEEDED(probe))probe=d->SetRenderState(D3DRS_ZFUNC,D3DCMP_ALWAYS);
        bool potentialBegun=false;
        if(SUCCEEDED(probe)){probe=s.potential->Issue(D3DISSUE_BEGIN);potentialBegun=SUCCEEDED(probe);}
        if(SUCCEEDED(probe))probe=native();
        if(potentialBegun){const auto end=s.potential->Issue(D3DISSUE_END);if(SUCCEEDED(probe))probe=end;}
        const HRESULT restoreZ=d->SetRenderState(D3DRS_ZFUNC,zfunc),restoreColor=d->SetRenderState(D3DRS_COLORWRITEENABLE,color);
        if(FAILED(probe)||FAILED(restoreZ)||FAILED(restoreColor)){unsupported=true;return native();}
        const bool begun=SUCCEEDED(s.visible->Issue(D3DISSUE_BEGIN));
        const HRESULT hr=native();
        const HRESULT end=begun?s.visible->Issue(D3DISSUE_END):E_FAIL;
        if(begun&&SUCCEEDED(end)){s.pending=true;r.status=hr;}else unsupported=true;
        return hr;
    }
};
