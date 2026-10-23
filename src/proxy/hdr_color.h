#pragma once
#include <d3d9.h>
#include <cmath>
#include <unordered_map>
#include "hdr_ui_patch.h"
#include "hdr_compiled_shaders.h"
#include "saved_state.h"

namespace NorthlightHdr {
class Color {
    struct Entry {IDirect3DPixelShader9* shader[3]{};bool attempted[3]{};};
    std::unordered_map<IDirect3DPixelShader9*,Entry> shaders;
    IDirect3DPixelShader9* world=nullptr;
    float white=0;
    template<class T> static void release(T*& p){if(p){p->Release();p=nullptr;}}
public:
    unsigned patched=0,rejected=0;
    ~Color(){reset();}
    void forget(IDirect3DPixelShader9* original){auto it=shaders.find(original);if(it!=shaders.end()){for(auto*& p:it->second.shader)release(p);shaders.erase(it);}}
    void reset(){for(auto& e:shaders)for(auto*& p:e.second.shader)release(p);shaders.clear();release(world);white=0;}
    IDirect3DPixelShader9* replacement(IDirect3DDevice9* d,IDirect3DPixelShader9* original,float uiWhite,unsigned variant){
        if(!original||variant>2)return nullptr;
        if(white!=uiWhite){reset();white=uiWhite;}
        auto it=shaders.find(original);if(it==shaders.end()){if(shaders.size()>=128){++rejected;return nullptr;}it=shaders.emplace(original,Entry{}).first;}
        auto& entry=it->second;if(entry.attempted[variant])return entry.shader[variant];entry.attempted[variant]=true;
        UINT bytes=0;HRESULT hr=original->GetFunction(nullptr,&bytes);
        if(FAILED(hr)||bytes<12||bytes%4||bytes>65536*4){++rejected;return nullptr;}
        std::vector<std::uint32_t> code(bytes/4),result;
        if(FAILED(original->GetFunction(code.data(),&bytes))){++rejected;return nullptr;}
        const bool sm2=code[0]==0xffff0200;
        const auto* transfer=sm2?kHdrUiPs2:kHdrUiPs3;
        const auto count=sm2?sizeof(kHdrUiPs2)/4:sizeof(kHdrUiPs3)/4;
        if(!NorthlightHdrUi::patch(code.data(),code.size(),transfer,count,variant==2?80.f:uiWhite,variant==1,result)||
           FAILED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(result.data()),&entry.shader[variant]))){++rejected;return nullptr;}
        ++patched;return entry.shader[variant];
    }
    template<class Draw> HRESULT draw(IDirect3DDevice9* d,float uiWhite,Draw submit){
        IDirect3DPixelShader9* original=nullptr;
        if(FAILED(d->GetPixelShader(&original))||!original){++rejected;return submit();}
        DWORD src=0,dst=0,srgb=0,blend=0;d->GetRenderState(D3DRS_SRCBLEND,&src);d->GetRenderState(D3DRS_DESTBLEND,&dst);d->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb);d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);
        struct Restore {IDirect3DDevice9* d;IDirect3DPixelShader9* ps;DWORD srgb;~Restore(){d->SetPixelShader(ps);if(srgb)d->SetRenderState(D3DRS_SRGBWRITEENABLE,srgb);ps->Release();}} restore{d,original,srgb};
        const unsigned variant=blend&&src==D3DBLEND_ONE&&dst==D3DBLEND_INVSRCALPHA?1:
            (blend&&((src==D3DBLEND_DESTCOLOR&&dst==D3DBLEND_ZERO)||(src==D3DBLEND_ZERO&&dst==D3DBLEND_SRCCOLOR))?2:0);
        auto* shader=replacement(d,original,uiWhite,variant);if(!shader)return submit();
        HRESULT hr=d->SetPixelShader(shader);if(SUCCEEDED(hr))hr=d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
        if(SUCCEEDED(hr))hr=submit();return hr;
    }
    HRESULT convertWorld(IDirect3DDevice9* d,IDirect3DSurface9* source,IDirect3DSurface9* target,IDirect3DTexture9* copy,IDirect3DSurface9* copySurface,
                         unsigned width,unsigned height,float paperWhite,float maxNits,NorthlightStateBlockPool* pool=nullptr,float contrast=1,float exposure=0){
        if(!world){HRESULT hr=d->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrWorld),&world);if(FAILED(hr))return hr;}
        SavedState saved(d,pool);if(!saved.ok)return E_FAIL;
        HRESULT hr=d->SetTexture(0,nullptr);if(SUCCEEDED(hr))hr=d->StretchRect(source,nullptr,copySurface,nullptr,D3DTEXF_NONE);if(FAILED(hr))return hr;
        d->SetDepthStencilSurface(nullptr);for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
        hr=d->SetRenderTarget(0,target);if(FAILED(hr))return hr;
        d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetStreamSourceFreq(0,1);
        const struct {D3DRENDERSTATETYPE name;DWORD value;} states[]={
            {D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},
            {D3DRS_SEPARATEALPHABLENDENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},
            {D3DRS_FOGENABLE,FALSE},{D3DRS_LIGHTING,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_FILLMODE,D3DFILL_SOLID},
            {D3DRS_COLORWRITEENABLE,15},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0},{D3DRS_MULTISAMPLEMASK,0xffffffff}};
        for(auto s:states){hr=d->SetRenderState(s.name,s.value);if(FAILED(hr))return hr;}
        d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
        D3DVIEWPORT9 vp={0,0,width,height,0,1};d->SetViewport(&vp);
        float scale[]={paperWhite/80,paperWhite/80,paperWhite/80,maxNits/80},grade[]={contrast,std::exp2(exposure),0,0};d->SetPixelShaderConstantF(0,scale,1);d->SetPixelShaderConstantF(1,grade,1);d->SetPixelShader(world);d->SetTexture(0,copy);
        struct Vertex {float x,y,z,w,u,v;};
        const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{float(width)-.5f,-.5f,0,1,1,0},{-.5f,float(height)-.5f,0,1,0,1},{float(width)-.5f,float(height)-.5f,0,1,1,1}};
        return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex));
    }
};
}
