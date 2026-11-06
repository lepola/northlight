#pragma once
#include <d3d9.h>
#include <unordered_map>
#include <memory>
#include "hdr_ui_patch.h"
#include "saved_state.h"

// Coverage of proven native overlays already drawn into Scene before composition.
// Depth still belongs to the surface behind them. Accumulate A = a+(1-a)*A
// in a free MRT using the native shader's final alpha and unchanged blend/test state.
// A second MRT preserves early additive RGB; world receivers erase both planes
// using the native blend. No geometry replay or change to native colour/depth;
// ordinary translucent models still receive lighting/fog.
namespace NorthlightEffectCoverage {
enum class Write { None, Alpha, Premultiplied, Replace, Clear, Additive, Modulate };
inline Write classify(bool active,bool zWrite,bool blend,unsigned src,unsigned dst,unsigned op,unsigned rgb,bool nativeOverlay){
    if((rgb&7)!=7)return Write::None;
    const bool protect=nativeOverlay&&!zWrite;
    if(!blend||(src==D3DBLEND_ONE&&dst==D3DBLEND_ZERO&&op==D3DBLENDOP_ADD))
        return protect?Write::Replace:(active?Write::Clear:Write::None);
    if(op==D3DBLENDOP_ADD&&dst==D3DBLEND_INVSRCALPHA){
        if(src==D3DBLEND_SRCALPHA)return protect?Write::Alpha:(active?Write::Clear:Write::None);
        if(src==D3DBLEND_ONE)return protect?Write::Premultiplied:(active?Write::Clear:Write::None);
    }
    if(op==D3DBLENDOP_ADD&&dst==D3DBLEND_ONE&&(src==D3DBLEND_ONE||src==D3DBLEND_SRCALPHA))
        return protect?Write::Additive:Write::None;
    if(active&&op==D3DBLENDOP_ADD&&((src==D3DBLEND_DESTCOLOR&&dst==D3DBLEND_ZERO)||
       (src==D3DBLEND_ZERO&&dst==D3DBLEND_SRCCOLOR)||(src==D3DBLEND_DESTCOLOR&&dst==D3DBLEND_SRCCOLOR)))return Write::Modulate;
    return Write::None;
}
inline bool patch(const std::uint32_t* words,size_t count,Write write,std::vector<std::uint32_t>& out,unsigned slot=1){
    using namespace NorthlightHdrUi;
    std::vector<Instruction> ops;unsigned cost=0;out.clear();
    if(write==Write::None||!parse(words,count,ops,cost))return false;
    const unsigned major=(words[0]>>8)&255,temps=major==2?12:32,constants=major==2?32:224;
    bool usedTemps[32]={},usedConstants[224]={};unsigned mask=0,outputs=0;
    for(const auto& ins:ops){
        unsigned first=ins.op==31?2:1,last=(ins.op==47||ins.op==48||ins.op==81)?1:unsigned(ins.count-1);
        for(unsigned j=first;j<=last;++j){const Word w=words[ins.at+j];const unsigned t=type(w),r=index(w);
            if(t==0){if(r>=temps)return false;usedTemps[r]=true;}
            if(t==2){if(r>=constants)return false;usedConstants[r]=true;}
            if(t==8){if(r>=4||j!=1)return false;if(!r)mask|=(w>>16)&15;outputs|=1u<<r;}
            if(t==9)return false;
        }
    }
    unsigned tmp=0,c=0;while(tmp<temps&&usedTemps[tmp])++tmp;while(c<constants&&usedConstants[c])++c;
    if(mask!=15||tmp==temps||c==constants||slot<1||slot>3||(outputs&(1u<<slot))||cost+3>(major==2?64u:512u))return false;
    out.assign(words,words+count-1);
    // def cN = (0,1,0,0), local to this shader; no game constants are changed.
    out.insert(out.begin()+1,{0x05000051u,reg(0xa00f0000u,2,c),0,0x3f800000u,0,0});
    for(const auto& ins:ops)if(ins.op!=31&&ins.op!=47&&ins.op!=48&&ins.op!=81&&ins.count>1){
        const Word dst=words[ins.at+1];if(type(dst)==8&&index(dst)==0)out[ins.at+7]=reg(dst,0,tmp);
    }
    const Word value=write==Write::Premultiplied?reg(0x80ff0000u,0,tmp):reg(write==Write::Clear?0xa0000000u:0xa0550000u,2,c);
    out.insert(out.end(),{0x02000001u,0x800f0800u,reg(0x80e40000u,0,tmp)});
    if(write!=Write::Additive&&write!=Write::Modulate)
        out.insert(out.end(),{0x02000001u,reg(0x80070000u,0,tmp),value});
    out.insert(out.end(),{0x02000001u,reg(0x800f0000u,8,slot),reg(0x80e40000u,0,tmp),0x0000ffffu});
    return true;
}
class Plane {
    struct Shaders {IDirect3DPixelShader9* shader[18]={};bool tried[18]={};};
    std::unordered_map<IDirect3DPixelShader9*,Shaders> shaders;
    IDirect3DTexture9* mask=nullptr;IDirect3DSurface9 *surface=nullptr,*msaa=nullptr,*target=nullptr;
    D3DSURFACE_DESC desc{};bool capsChecked=false,capsOk=false,valid=true,marked=false,multisample=false;unsigned targets=0,boundSlot=0;
    IDirect3DDevice9* boundDevice=nullptr;
    template<class T> static void drop(T*& p){if(p){p->Release();p=nullptr;}}
    void releaseImage(){drop(msaa);drop(surface);drop(mask);desc={};}
    IDirect3DPixelShader9* replacement(IDirect3DDevice9* d,IDirect3DPixelShader9* original,Write write,unsigned slot)try{
        auto it=shaders.find(original);if(it==shaders.end()){if(shaders.size()>=256)return nullptr;it=shaders.emplace(original,Shaders{}).first;}
        auto& bank=it->second;const unsigned index=(slot-1)*6+unsigned(write)-1;if(bank.tried[index])return bank.shader[index];bank.tried[index]=true;
        UINT bytes=0;if(FAILED(original->GetFunction(nullptr,&bytes))||bytes<12||bytes>262144||bytes%4)return nullptr;
        std::vector<std::uint32_t> words(bytes/4),patched;
        if(FAILED(original->GetFunction(words.data(),&bytes))||!patch(words.data(),words.size(),write,patched,slot))return nullptr;
        if(FAILED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&bank.shader[index])))return nullptr;
        return bank.shader[index];
    }catch(const std::bad_alloc&){return nullptr;}
public:
    unsigned draws=0,bindings=0;
    ~Plane(){reset();}
    bool active()const{return marked&&valid;}
    // Native D3D9 does not guarantee MRT+MSAA. Enable only on validated backends.
    void allowMultisample(bool allow){multisample=allow;}
    void endBatch(){if(boundDevice){IDirect3DSurface9* current=nullptr;if(SUCCEEDED(boundDevice->GetRenderTarget(boundSlot,&current))&&current==(msaa?msaa:surface))boundDevice->SetRenderTarget(boundSlot,nullptr);drop(current);boundDevice=nullptr;boundSlot=0;}}
    void endFrame(){endBatch();drop(target);marked=false;valid=true;draws=bindings=0;}
    void colorWrite(IDirect3DSurface9* destination){if(destination&&destination==target)endFrame();}
    template<class Forget> void forget(IDirect3DPixelShader9* original,Forget derived){auto it=shaders.find(original);if(it!=shaders.end()){for(auto*& p:it->second.shader){if(p)derived(p);drop(p);}shaders.erase(it);}}
    void forget(IDirect3DPixelShader9* original){forget(original,[](IDirect3DPixelShader9*){});}
    void reset(){endFrame();for(auto& entry:shaders)for(auto*& p:entry.second.shader)drop(p);shaders.clear();releaseImage();capsChecked=capsOk=false;}
    template<class Draw> HRESULT draw(IDirect3DDevice9* d,Write write,Draw submit,bool batch=false,bool rgb=false){
        auto fallback=[&]{endBatch();return submit();};
        if(write==Write::None||!valid)return fallback();
        if(!capsChecked){D3DCAPS9 caps{};capsChecked=true;capsOk=SUCCEEDED(d->GetDeviceCaps(&caps))&&caps.NumSimultaneousRTs>=2&&
            (caps.PrimitiveMiscCaps&D3DPMISCCAPS_INDEPENDENTWRITEMASKS)&&(caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING);targets=caps.NumSimultaneousRTs;}
        if(!capsOk)return fallback();
        IDirect3DSurface9 *rt=nullptr,*aux=nullptr;IDirect3DPixelShader9* ps=nullptr;DWORD color=0;D3DSURFACE_DESC current{};
        unsigned slot=1;D3DRENDERSTATETYPE colorState=D3DRS_COLORWRITEENABLE1;
        struct Restore {IDirect3DDevice9* d;IDirect3DSurface9*& rt;IDirect3DSurface9*& aux;IDirect3DPixelShader9*& ps;DWORD& color;unsigned& slot;D3DRENDERSTATETYPE& colorState;bool keep=false,changed=false;
            ~Restore(){if(changed){d->SetPixelShader(ps);d->SetRenderState(colorState,color);if(!keep)d->SetRenderTarget(slot,nullptr);}drop(rt);drop(aux);drop(ps);}} restore{d,rt,aux,ps,color,slot,colorState};
        HRESULT got=d->GetRenderTarget(1,&aux);
        while(SUCCEEDED(got)&&aux&&!(boundDevice==d&&boundSlot==slot&&aux==(msaa?msaa:surface))&&slot+1<targets){drop(aux);++slot;got=d->GetRenderTarget(slot,&aux);}
        colorState=slot==1?D3DRS_COLORWRITEENABLE1:slot==2?D3DRS_COLORWRITEENABLE2:D3DRS_COLORWRITEENABLE3;
        const bool already=boundDevice==d&&boundSlot==slot&&aux==(msaa?msaa:surface);
        if(already)drop(aux);
        if(slot>=targets||(FAILED(got)&&got!=D3DERR_NOTFOUND)||aux||FAILED(d->GetRenderTarget(0,&rt))||!rt||
           FAILED(rt->GetDesc(&current))||FAILED(d->GetPixelShader(&ps))||!ps||FAILED(d->GetRenderState(colorState,&color))){if(write==Write::Clear)valid=false;return fallback();}
        if((current.MultiSampleType&&!multisample)||(target&&rt!=target))return fallback();
        auto* patched=replacement(d,ps,write,slot);if(!patched){if(write==Write::Clear)valid=false;return fallback();}
        if(!target){
            if(desc.Width!=current.Width||desc.Height!=current.Height||desc.Format!=current.Format||desc.MultiSampleType!=current.MultiSampleType||desc.MultiSampleQuality!=current.MultiSampleQuality)releaseImage();
            // Match RT0's bit depth, including MSAA. This also supports devices
            // without MRTINDEPENDENTBITDEPTH; coverage consumes red, emission RGB.
            if(!mask){
                desc=current;const auto format=rgb&&desc.Format==D3DFMT_X8R8G8B8?D3DFMT_A8R8G8B8:desc.Format;
                HRESULT hr=d->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&mask,nullptr);
                if(SUCCEEDED(hr))hr=mask->GetSurfaceLevel(0,&surface);
                if(SUCCEEDED(hr)&&desc.MultiSampleType)hr=d->CreateRenderTarget(desc.Width,desc.Height,format,desc.MultiSampleType,desc.MultiSampleQuality,FALSE,&msaa,nullptr);
                if(FAILED(hr)){releaseImage();valid=false;return fallback();}
            }
            if(FAILED(d->ColorFill(msaa?msaa:surface,nullptr,0))){valid=false;return fallback();}
            target=rt;target->AddRef();
        }
        restore.changed=true;
        if(!already)++bindings;
        if((!already&&FAILED(d->SetRenderTarget(slot,msaa?msaa:surface)))||FAILED(d->SetRenderState(colorState,rgb?7:D3DCOLORWRITEENABLE_RED))||FAILED(d->SetPixelShader(patched))){
            d->SetPixelShader(ps);d->SetRenderState(colorState,color);d->SetRenderTarget(slot,nullptr);restore.changed=false;valid=false;return fallback();}
        const HRESULT hr=submit();if(SUCCEEDED(hr)&&write!=Write::Clear){marked=true;++draws;}
        if(batch){restore.keep=true;boundDevice=d;boundSlot=slot;}else{boundDevice=nullptr;boundSlot=0;}return hr;
    }
    IDirect3DTexture9* texture(IDirect3DDevice9* d,IDirect3DSurface9* scene){
        if(!active()||scene!=target)return nullptr;
        if(msaa&&FAILED(d->StretchRect(msaa,nullptr,surface,nullptr,D3DTEXF_NONE)))return nullptr;
        return mask;
    }
};

// Keep additive radiance separate from alpha coverage: adding opacity for a
// glow would also exempt the opaque wall behind it. Both planes use the native
// blend and depth tests in the same submission. Alpha draws/opaque receivers
// attenuate/erase old emission; modulate shadows multiply it in native order.
class Capture {
    Plane coverage,emission;
    IDirect3DPixelShader9* pack=nullptr;
    std::unique_ptr<NorthlightStateBlockPool> states;
    bool packed=false;
    Write batchWrite=Write::None;
public:
    unsigned draws=0,bindings=0;
    ~Capture(){reset();}
    bool active()const{return coverage.active()||emission.active();}
    bool hasEmission()const{return packed;}
    void allowMultisample(bool allow){coverage.allowMultisample(allow);emission.allowMultisample(allow);}
    void endBatch(){emission.endBatch();coverage.endBatch();batchWrite=Write::None;}
    void endFrame(){endBatch();coverage.endFrame();emission.endFrame();packed=false;draws=bindings=0;}
    void colorWrite(IDirect3DSurface9* dst){coverage.colorWrite(dst);emission.colorWrite(dst);if(!active())packed=false;}
    void forget(IDirect3DPixelShader9* ps){coverage.forget(ps,[&](auto* derived){emission.forget(derived);});emission.forget(ps);}
    void reset(){endFrame();coverage.reset();emission.reset();if(pack){pack->Release();pack=nullptr;}states.reset();}
    template<class Draw> HRESULT draw(IDirect3DDevice9* d,Write write,Draw submit,bool batch=false){
        // A glow-only batch owns RT1; an alpha+glow batch needs RT1/RT2.
        // Rebind on the transition so SM2 never sees a hole in COLOR outputs
        // or the same emission surface bound to two targets simultaneously.
        if(write!=batchWrite)endBatch();batchWrite=write;
        const auto ew=write==Write::Additive?write:emission.active()?(write==Write::Modulate?write:write!=Write::None?Write::Clear:Write::None):Write::None;
        const auto cw=write==Write::Additive||write==Write::Modulate?Write::None:write;
        const HRESULT hr=coverage.draw(d,cw,[&]{return emission.draw(d,ew,submit,batch,true);},batch);
        draws=coverage.draws+emission.draws;bindings=coverage.bindings+emission.bindings;return hr;
    }
    IDirect3DTexture9* texture(IDirect3DDevice9* d,IDirect3DSurface9* scene){
        endBatch();packed=false;
        auto* alpha=coverage.texture(d,scene);auto* energy=emission.texture(d,scene);
        if(!energy)return alpha;
        // Pack coverage in the otherwise unused alpha of the emission image.
        // One alpha-only fullscreen pass, only for frames with early glow draws;
        // the final shaders keep their existing sampler and texture fetch.
        if(!pack){
            static const DWORD code[]={0xffff0200,0x0200001f,0x80000000,0xb0030000,
                0x0200001f,0x90000000,0xa00f0800,0x03000042,0x800f0000,0xb0e40000,0xa0e40800,
                0x03000005,0x800f0000,0x80000000,0xa0000000,0x02000001,0x800f0800,0x80e40000,0x0000ffff};
            if(FAILED(d->CreatePixelShader(code,&pack)))return alpha;
        }
        if(!states)states=std::make_unique<NorthlightStateBlockPool>(d);
        SavedState saved(d,states.get());if(!saved.ok)return alpha;
        IDirect3DSurface9* surface=nullptr;if(FAILED(energy->GetSurfaceLevel(0,&surface)))return alpha;
        D3DSURFACE_DESC desc{};surface->GetDesc(&desc);
        for(unsigned s=0;s<16;++s)d->SetTexture(s,nullptr);
        d->SetDepthStencilSurface(nullptr);for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
        HRESULT hr=d->SetRenderTarget(0,surface);surface->Release();if(FAILED(hr))return alpha;
        const D3DVIEWPORT9 vp={0,0,desc.Width,desc.Height,0,1};d->SetViewport(&vp);
        for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE})d->SetRenderState(key,FALSE);
        d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_ALPHA);
        d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetPixelShader(pack);d->SetTexture(0,alpha);
        const float scale[]={alpha?1.f:0.f,0,0,0};d->SetPixelShaderConstantF(0,scale,1);
        for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(0,key,D3DTEXF_POINT);
        d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
        struct V{float x,y,z,w,u,v;};const float w=float(desc.Width)-.5f,h=float(desc.Height)-.5f;
        const V q[]={{-.5f,-.5f,0,1,0,0},{w,-.5f,0,1,1,0},{-.5f,h,0,1,0,1},{w,h,0,1,1,1}};
        const bool ownScene=SUCCEEDED(d->BeginScene());
        hr=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V));if(ownScene)d->EndScene();
        packed=SUCCEEDED(hr);return packed?energy:alpha;
    }
    static void bind(IDirect3DDevice9* d,IDirect3DTexture9* texture,bool emission=false){
        const float info[]={texture?1.f:0.f,emission?1.f:0.f,0,0};d->SetPixelShaderConstantF(79,info,1);d->SetTexture(7,texture);
        for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(7,key,D3DTEXF_POINT);
        d->SetSamplerState(7,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(7,D3DSAMP_SRGBTEXTURE,FALSE);
        for(auto key:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})d->SetSamplerState(7,key,D3DTADDRESS_CLAMP);
    }
};
}
