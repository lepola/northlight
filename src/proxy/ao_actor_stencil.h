#pragma once
#include <d3d9.h>
#include <cmath>
#include <new>
#include "saved_state.h"

// Reserve only bit 0x40 of a D24S8 scene depth. All other bits, including the
// name mask's 0x80, survive both private fullscreen passes. No shader patch,
// extra geometry replay or target change occurs around a native draw.
// beginDraw and freeze's materialization must run inside the caller's scene.
class NorthlightAOActorStencil {
    static constexpr DWORD Bit=0x40;
    static constexpr D3DRENDERSTATETYPE States[]={D3DRS_STENCILENABLE,D3DRS_STENCILFUNC,D3DRS_STENCILREF,D3DRS_STENCILMASK,
        D3DRS_STENCILWRITEMASK,D3DRS_STENCILFAIL,D3DRS_STENCILZFAIL,D3DRS_STENCILPASS,D3DRS_TWOSIDEDSTENCILMODE};
    IDirect3DDevice9* d;NorthlightStateBlockPool blocks;bool verifiedMsaaMrt;
    IDirect3DPixelShader9* white=nullptr;
    IDirect3DTexture9* texture=nullptr;
    IDirect3DSurface9 *surface=nullptr,*multisample=nullptr,*frameTarget=nullptr,*frameDepth=nullptr;
    D3DSURFACE_DESC shape{};DWORD previous[9]={};unsigned changed=0,draws=0;
    bool enabled=false,bad=false,started=false,frozen=false,resolved=false,active=false,tagged=false;
    template<class T> static void release(T*& p){if(p){p->Release();p=nullptr;}}
    static bool sameShape(const D3DSURFACE_DESC& a,const D3DSURFACE_DESC& b) {
        return a.Width==b.Width&&a.Height==b.Height&&a.MultiSampleType==b.MultiSampleType&&a.MultiSampleQuality==b.MultiSampleQuality;
    }
    bool shader() {
        if(white)return true;
        const DWORD code[]={0xffff0200,0x05000051,0xa00f0000,0x3f800000,0x3f800000,0x3f800000,0x3f800000,
            0x02000001,0x800f0800,0xa0e40000,0xffff};
        if(FAILED(d->CreatePixelShader(code,&white))||!white){release(white);return false;}return true;
    }
    bool state(D3DRENDERSTATETYPE key,DWORD value){return SUCCEEDED(d->SetRenderState(key,value));}
    bool stencil(DWORD comparison,DWORD reference,DWORD write,DWORD pass) {
        const DWORD values[]={TRUE,comparison,reference,Bit,write,D3DSTENCILOP_KEEP,D3DSTENCILOP_KEEP,
            pass,FALSE};
        for(unsigned i=0;i<9;++i)if(!state(States[i],values[i]))return false;
        return true;
    }
    static bool restoreSaved(SavedState& saved) {
        auto retry=[](auto call){return SUCCEEDED(call())||SUCCEEDED(call());};bool ok=true;
        ok=retry([&]{return saved.d->SetDepthStencilSurface(nullptr);})&&ok;
        for(unsigned i=1;i<4;++i)ok=retry([&]{return saved.d->SetRenderTarget(i,nullptr);})&&ok;
        ok=retry([&]{return saved.d->SetRenderTarget(0,saved.targets[0]);})&&ok;
        for(unsigned i=1;i<4;++i)ok=retry([&]{return saved.d->SetRenderTarget(i,saved.targets[i]);})&&ok;
        ok=retry([&]{return saved.d->SetDepthStencilSurface(saved.depth);})&&ok;
        ok=retry([&]{return saved.block->Apply();})&&ok;
        ok=retry([&]{return saved.d->SetViewport(&saved.viewport);})&&ok;
        saved.ok=false;return ok;
    }
    bool captureSaved(SavedState& saved) {
        // SavedState owns the references/lease; check every getter here rather
        // than accepting its permissive capture of absent extra targets/depth.
        saved.d=d;saved.pool=&blocks;saved.block=blocks.acquire();if(!saved.block)return false;
        for(unsigned i=0;i<4;++i) {
            const HRESULT hr=d->GetRenderTarget(i,&saved.targets[i]);
            if(FAILED(hr)&&(hr!=D3DERR_NOTFOUND||saved.targets[i]))return false;
        }
        const HRESULT depth=d->GetDepthStencilSurface(&saved.depth);
        if(FAILED(depth)&&(depth!=D3DERR_NOTFOUND||saved.depth))return false;
        return saved.ok=saved.targets[0]&&SUCCEEDED(d->GetViewport(&saved.viewport));
    }
    bool quad(bool clearBit) try {
        if(!shader())return false;
        // SavedState captures streams before DrawPrimitiveUP unbinds stream 0.
        bool drawn=false,restored=false;
        {SavedState saved(nullptr);if(!captureSaved(saved))return false;
        struct Restore {SavedState& saved;bool& ok;~Restore(){ok=restoreSaved(saved);}} restore{saved,restored};
        drawn=[&] {
        for(unsigned i=1;i<4;++i)if(FAILED(d->SetRenderTarget(i,nullptr)))return false;
        if(!clearBit) {
            for(unsigned i=0;i<20;++i) {
                IDirect3DBaseTexture9* bound=nullptr;const unsigned stage=i<16?i:D3DVERTEXTEXTURESAMPLER0+i-16;
                const HRESULT hr=d->GetTexture(stage,&bound);const bool alias=bound==texture;release(bound);
                if(FAILED(hr)||(alias&&FAILED(d->SetTexture(stage,nullptr))))return false;
            }
            if(FAILED(d->SetDepthStencilSurface(nullptr)))return false;
            if(FAILED(d->SetRenderTarget(0,multisample?multisample:surface))||FAILED(d->SetDepthStencilSurface(frameDepth)))return false;
        }else if(saved.targets[0]!=frameTarget||saved.depth!=frameDepth) {
            if(FAILED(d->SetDepthStencilSurface(nullptr))||FAILED(d->SetRenderTarget(0,frameTarget))||FAILED(d->SetDepthStencilSurface(frameDepth)))return false;
        }
        const D3DVIEWPORT9 viewport{0,0,shape.Width,shape.Height,0,1};
        if(FAILED(d->SetViewport(&viewport))||FAILED(d->SetStreamSourceFreq(0,1))||FAILED(d->SetVertexShader(nullptr))||FAILED(d->SetFVF(D3DFVF_XYZRHW))||FAILED(d->SetPixelShader(white)))return false;
        if(!state(D3DRS_ZENABLE,FALSE)||!state(D3DRS_ZWRITEENABLE,FALSE)||!state(D3DRS_ALPHATESTENABLE,FALSE)||
           !state(D3DRS_ALPHABLENDENABLE,FALSE)||!state(D3DRS_FOGENABLE,FALSE)||!state(D3DRS_SCISSORTESTENABLE,FALSE)||
           !state(D3DRS_CULLMODE,D3DCULL_NONE)||!state(D3DRS_FILLMODE,D3DFILL_SOLID)||!state(D3DRS_CLIPPLANEENABLE,0)||!state(D3DRS_MULTISAMPLEANTIALIAS,TRUE)||!state(D3DRS_MULTISAMPLEMASK,~DWORD(0))||
           !state(D3DRS_COLORWRITEENABLE,clearBit?0:15)||!stencil(clearBit?D3DCMP_ALWAYS:D3DCMP_EQUAL,clearBit?0:Bit,Bit,clearBit?D3DSTENCILOP_REPLACE:D3DSTENCILOP_ZERO))return false;
        if(!clearBit&&FAILED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0)))return false;
        struct Vertex {float x,y,z,w;};
        const Vertex vertices[]={{-.5f,-.5f,.5f,1},{float(shape.Width)-.5f,-.5f,.5f,1},
            {-.5f,float(shape.Height)-.5f,.5f,1},{float(shape.Width)-.5f,float(shape.Height)-.5f,.5f,1}};
        return SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)));
        }();}
        return drawn&&restored;
    }catch(const std::bad_alloc&){return false;}
    void releaseTargets(){release(multisample);release(surface);release(texture);}
    bool targets() {
        if(texture&&surface&&(shape.MultiSampleType==D3DMULTISAMPLE_NONE||multisample))return true;
        releaseTargets();
        if(FAILED(d->CreateTexture(shape.Width,shape.Height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture,nullptr))||!texture||
           FAILED(texture->GetSurfaceLevel(0,&surface))||!surface){releaseTargets();return false;}
        if(shape.MultiSampleType!=D3DMULTISAMPLE_NONE&&
           (FAILED(d->CreateRenderTarget(shape.Width,shape.Height,D3DFMT_A8R8G8B8,shape.MultiSampleType,shape.MultiSampleQuality,FALSE,&multisample,nullptr))||!multisample)) {
            releaseTargets();return false;
        }
        return true;
    }
    bool restore() {
        if(!active)return true;
        bool ok=true;
        for(unsigned i=0;i<9;++i)if(changed&(1u<<i)) {
            const HRESULT hr=d->SetRenderState(States[i],previous[i]);
            if(FAILED(hr)&&FAILED(d->SetRenderState(States[i],previous[i])))ok=false;
        }
        active=false;changed=0;if(!ok)invalidate();return ok;
    }
    static bool nameState(const DWORD* values) {
        return values[0]&&values[1]==D3DCMP_ALWAYS&&values[2]==0x80&&values[4]==0x80&&
            values[5]==D3DSTENCILOP_KEEP&&values[6]==D3DSTENCILOP_KEEP&&values[7]==D3DSTENCILOP_REPLACE&&!values[8];
    }
public:
    // Native D3D9 explicitly excludes antialiasing from MRT support:
    // learn.microsoft.com/windows/win32/direct3d9/multiple-render-targets
    // Ordinary MSAA is supported; an already occupied MRT needs backend proof.
    explicit NorthlightAOActorStencil(IDirect3DDevice9* device,bool verifiedMultisampleMrt=false):d(device),blocks(device),verifiedMsaaMrt(verifiedMultisampleMrt){}
    ~NorthlightAOActorStencil(){reset();}
    NorthlightAOActorStencil(const NorthlightAOActorStencil&)=delete;
    NorthlightAOActorStencil& operator=(const NorthlightAOActorStencil&)=delete;
    void beginFrame(bool on){const bool restored=restore();if(tagged)invalidate();release(frameTarget);release(frameDepth);enabled=on;bad=!restored;started=frozen=resolved=tagged=false;draws=0;}
    // When abandoning a live capture, clear the private bit before the next
    // native stencil consumer. Caller remains inside its scene. The ordinary
    // freeze clears this bit for free while materializing the mask.
    void invalidate(){bad=true;resolved=false;if(tagged){tagged=false;if(!quad(true)&&!quad(true))tagged=true;}}
    void resume(){frozen=false;invalidate();}
    bool beginDraw(bool actor,bool depthWriter,bool eligible,IDirect3DSurface9* sceneTarget,IDirect3DSurface9* sceneDepth) {
        if(!enabled||bad||frozen)return false;
        if(!eligible){invalidate();return false;}
        if(!depthWriter) {
            if(tagged) {
                DWORD values[9]={};bool ok=SUCCEEDED(d->GetRenderState(States[0],&values[0]));
                if(ok&&values[0]){for(unsigned i=1;ok&&i<9;++i)ok=SUCCEEDED(d->GetRenderState(States[i],&values[i]));ok=ok&&nameState(values);}
                if(!ok)invalidate();
            }
            return false;
        }
        if(!actor&&!started)return false;
        if(active){restore();invalidate();return false;}
        IDirect3DSurface9 *rt=nullptr,*ds=nullptr;D3DSURFACE_DESC rd{},dd{};D3DVIEWPORT9 viewport{};
        bool ok=sceneTarget&&sceneDepth&&SUCCEEDED(d->GetRenderTarget(0,&rt))&&rt==sceneTarget&&SUCCEEDED(d->GetDepthStencilSurface(&ds))&&ds==sceneDepth;
        if(ok&&started&&rt==frameTarget&&ds==frameDepth){rd=dd=shape;dd.Format=D3DFMT_D24S8;}
        else if(ok)ok=SUCCEEDED(rt->GetDesc(&rd))&&SUCCEEDED(ds->GetDesc(&dd));
        ok=ok&&
            dd.Format==D3DFMT_D24S8&&sameShape(rd,dd)&&SUCCEEDED(d->GetViewport(&viewport))&&!viewport.X&&!viewport.Y&&viewport.Width==rd.Width&&viewport.Height==rd.Height&&
            std::isfinite(viewport.MinZ)&&std::isfinite(viewport.MaxZ)&&viewport.MinZ>=0&&viewport.MinZ<viewport.MaxZ&&viewport.MaxZ<=1&&
            (!started||(rt==frameTarget&&ds==frameDepth&&sameShape(shape,rd)));
        if(ok&&rd.MultiSampleType!=D3DMULTISAMPLE_NONE&&!verifiedMsaaMrt) {
            IDirect3DSurface9* extra=nullptr;const HRESULT hr=d->GetRenderTarget(1,&extra);
            ok=hr==D3DERR_NOTFOUND&&!extra;release(extra);
        }
        for(unsigned i=0;ok&&i<9;++i)ok=SUCCEEDED(d->GetRenderState(States[i],&previous[i]));
        // The sole supported enabled-stencil state is NameMask's unconditional
        // mark. Extend its write/ref bits; preserve its complete native coverage.
        const bool name=ok&&nameState(previous);
        ok=ok&&(!previous[0]||name);
        if(ok&&!started) {
            if(!sameShape(shape,rd))releaseTargets();shape=rd;
            frameTarget=rt;rt=nullptr;frameDepth=ds;ds=nullptr;
            ok=quad(true);started=ok;
        }
        release(rt);release(ds);
        if(!ok){invalidate();return false;}
        // FUNC=ALWAYS ignores the read mask and unrelated reference bits.
        const DWORD values[]={TRUE,D3DCMP_ALWAYS,(previous[2]&~Bit)|(actor?Bit:0),previous[3],(name?0x80u:0u)|Bit,D3DSTENCILOP_KEEP,D3DSTENCILOP_KEEP,D3DSTENCILOP_REPLACE,FALSE};
        active=true;changed=0;
        for(unsigned i=0;i<9;++i)if(previous[i]!=values[i]) {
            changed|=1u<<i;
            if(FAILED(d->SetRenderState(States[i],values[i]))){restore();invalidate();return false;}
        }
        tagged=true;
        return true;
    }
    bool endDraw(HRESULT nativeResult){if(!active)return true;if(FAILED(nativeResult))invalidate();else ++draws;return restore();}
    bool freeze(IDirect3DSurface9* sampledDepth) {
        if(active){restore();invalidate();}frozen=true;
        if(!enabled||bad||!started||!draws)return false;
        if(sampledDepth!=frameDepth){invalidate();return false;}
        if(resolved)return true;
        if(!targets()||!quad(false)){invalidate();return false;}tagged=false;
        if(multisample&&FAILED(d->StretchRect(multisample,nullptr,surface,nullptr,D3DTEXF_NONE))){invalidate();return false;}
        resolved=true;return true;
    }
    IDirect3DTexture9* textureForDepth(IDirect3DSurface9* sampledDepth)const{return enabled&&frozen&&resolved&&!bad&&sampledDepth==frameDepth?texture:nullptr;}
    bool valid()const{return started&&!bad;}
    unsigned capturedDraws()const{return draws;}
    void reset(){restore();if(tagged)invalidate();blocks.clear();release(frameTarget);release(frameDepth);releaseTargets();release(white);enabled=false;bad=false;started=frozen=resolved=tagged=false;draws=0;shape={};}
};
