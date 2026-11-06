#pragma once
#include <d3d9.h>
#include <algorithm>
#include <unordered_map>
#include "hdr_ui_patch.h"
#include "hdr_compiled_shaders.h"

namespace NorthlightHdr {
// Add the linear foreground to the existing sky in the native draw itself.
// No geometry replay, new image, depth/stencil write or occlusion-query samples.
class SkyForeground {
    struct Entry {IDirect3DPixelShader9* original=nullptr;IDirect3DPixelShader9* shader[6]{};bool tried[6]{};};
    std::unordered_map<IDirect3DPixelShader9*,Entry> shaders;
    bool checked=false,capable=false;unsigned targets=0;
    template<class T> static void drop(T*& p){if(p)p->Release();p=nullptr;}
    IDirect3DPixelShader9* replacement(IDirect3DDevice9* d,IDirect3DPixelShader9* original,unsigned slot,bool premultiplied)try{
        auto it=shaders.find(original);
        if(it==shaders.end()){
            if(shaders.size()>=256)return nullptr;
            it=shaders.emplace(original,Entry{}).first;original->AddRef();it->second.original=original;
        }
        auto& entry=it->second;const unsigned index=(slot-1)*2+unsigned(premultiplied);
        if(entry.tried[index])return entry.shader[index];entry.tried[index]=true;
        UINT bytes=0;if(FAILED(original->GetFunction(nullptr,&bytes))||bytes<12||bytes>262144||bytes%4)return nullptr;
        std::vector<std::uint32_t> words(bytes/4),patched;
        if(FAILED(original->GetFunction(words.data(),&bytes)))return nullptr;
        const bool sm2=words[0]==0xffff0200;
        const auto* transfer=sm2?kHdrUiPs2:kHdrUiPs3;
        const auto count=sm2?sizeof(kHdrUiPs2)/4:sizeof(kHdrUiPs3)/4;
        if(!NorthlightHdrUi::patch(words.data(),words.size(),transfer,count,80,premultiplied,patched,nullptr,false,slot)||
           FAILED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&entry.shader[index])))return nullptr;
        return entry.shader[index];
    }catch(const std::bad_alloc&){return nullptr;}
public:
    std::uint64_t draws=0;
    ~SkyForeground(){reset();}
    void reset(){for(auto& item:shaders){drop(item.second.original);for(auto*& p:item.second.shader)drop(p);}shaders.clear();checked=capable=false;targets=0;draws=0;}
    template<class Draw> bool draw(IDirect3DDevice9* d,IDirect3DSurface9* sky,bool allowMultisample,Draw submit,HRESULT& result){
        if(!sky)return false;
        if(!checked){D3DCAPS9 caps{};checked=true;capable=SUCCEEDED(d->GetDeviceCaps(&caps))&&caps.NumSimultaneousRTs>=2&&
            (caps.PrimitiveMiscCaps&D3DPMISCCAPS_INDEPENDENTWRITEMASKS)&&(caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS)&&
            (caps.PrimitiveMiscCaps&D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING);targets=std::min(4u,unsigned(caps.NumSimultaneousRTs));}
        if(!capable)return false;
        DWORD srgb=0,blend=0,src=0,dst=0,op=0,writes=0;
        if(FAILED(d->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb))||srgb||FAILED(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))||
           FAILED(d->GetRenderState(D3DRS_COLORWRITEENABLE,&writes)))return false;
        if(blend&&(FAILED(d->GetRenderState(D3DRS_SRCBLEND,&src))||FAILED(d->GetRenderState(D3DRS_DESTBLEND,&dst))||
           FAILED(d->GetRenderState(D3DRS_BLENDOP,&op))||op!=D3DBLENDOP_ADD||
           (src!=D3DBLEND_ONE&&src!=D3DBLEND_SRCALPHA)||(dst!=D3DBLEND_ONE&&dst!=D3DBLEND_INVSRCALPHA)))return false;
        IDirect3DSurface9 *native=nullptr,*aux=nullptr,*depth=nullptr;IDirect3DPixelShader9* original=nullptr;unsigned slot=1;DWORD oldWrites=0;
        struct Restore {IDirect3DDevice9* d;IDirect3DSurface9*& native;IDirect3DSurface9*& aux;IDirect3DSurface9*& depth;IDirect3DPixelShader9*& original;unsigned& slot;DWORD& writes;bool changed=false;
            ~Restore(){if(changed){d->SetPixelShader(original);d->SetRenderTarget(slot,nullptr);d->SetRenderState(D3DRENDERSTATETYPE(D3DRS_COLORWRITEENABLE1+slot-1),writes);}drop(native);drop(aux);drop(depth);drop(original);}} restore{d,native,aux,depth,original,slot,oldWrites};
        auto colorState=[](unsigned n){return n==1?D3DRS_COLORWRITEENABLE1:n==2?D3DRS_COLORWRITEENABLE2:D3DRS_COLORWRITEENABLE3;};
        for(;slot<targets;++slot){const HRESULT hr=d->GetRenderTarget(slot,&aux);if(hr==D3DERR_NOTFOUND||SUCCEEDED(hr)&&!aux)break;if(FAILED(hr))return false;drop(aux);}
        if(slot>=targets||FAILED(d->GetRenderTarget(0,&native))||!native||FAILED(d->GetPixelShader(&original))||!original)return false;
        D3DSURFACE_DESC a{},b{},z{};
        if(FAILED(native->GetDesc(&a))||FAILED(sky->GetDesc(&b))||a.Width!=b.Width||a.Height!=b.Height||a.MultiSampleType!=b.MultiSampleType||
           a.MultiSampleQuality!=b.MultiSampleQuality||(a.MultiSampleType&&!allowMultisample)||
           FAILED(d->GetDepthStencilSurface(&depth))||!depth||FAILED(depth->GetDesc(&z))||z.Width<a.Width||z.Height<a.Height||
           z.MultiSampleType!=a.MultiSampleType||z.MultiSampleQuality!=a.MultiSampleQuality)return false;
        auto* patched=replacement(d,original,slot,blend&&src==D3DBLEND_ONE&&dst==D3DBLEND_INVSRCALPHA);if(!patched)return false;
        const auto state=colorState(slot);if(FAILED(d->GetRenderState(state,&oldWrites)))return false;
        // The three COLORWRITEENABLE enums are consecutive in D3D9.
        restore.changed=true;
        if(FAILED(d->SetRenderTarget(slot,sky))||FAILED(d->SetRenderState(state,writes))||FAILED(d->SetPixelShader(patched)))return false;
        ++draws;result=submit();return true;
    }
};
}
