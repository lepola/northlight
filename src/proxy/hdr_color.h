#pragma once
#include <d3d9.h>
#include <cmath>
#include <unordered_map>
#include "hdr_ui_patch.h"
#include "hdr_compiled_shaders.h"
#include "saved_state.h"
#include "hdr_quad.h"

namespace NorthlightHdr {
class Color {
    struct Entry {IDirect3DPixelShader9* shader[3]{};bool attempted[3]{};};
    std::unordered_map<IDirect3DPixelShader9*,Entry> shaderBanks[2];
    IDirect3DPixelShader9* world=nullptr;
    IDirect3DPixelShader9* fixedUi=nullptr;IDirect3DPixelShader9* multiFixedUi=nullptr;
    IDirect3DPixelShader9* layerShader=nullptr;IDirect3DTexture9* layerTexture=nullptr;
    IDirect3DSurface9* layerSurface=nullptr;IDirect3DSurface9* layerMsaa=nullptr;D3DSURFACE_DESC layerDesc{};
    float uiScale=0;
    template<class T> static void release(T*& p){if(p){p->Release();p=nullptr;}}
    static bool disabledStage(IDirect3DDevice9* d,unsigned stage){
        DWORD op=0,arg=0;if(FAILED(d->GetTextureStageState(stage,D3DTSS_COLOROP,&op)))return false;
        if(op==D3DTOP_DISABLE)return true;
        IDirect3DBaseTexture9* texture=nullptr;
        const bool disabled=SUCCEEDED(d->GetTextureStageState(stage,D3DTSS_COLORARG1,&arg))&&arg==D3DTA_TEXTURE&&
            SUCCEEDED(d->GetTexture(stage,&texture))&&!texture;
        release(texture);return disabled;
    }
public:
    unsigned patched=0,rejected=0;
    std::uint64_t layers=0,layerPixels=0,screenDirect=0,attenuationDirect=0,multiTextureDirect=0;
    ~Color(){reset();}
    std::uint64_t payloadBytes()const{return std::uint64_t(layerDesc.Width)*layerDesc.Height*4*((layerTexture?1:0)+(layerMsaa?unsigned(layerDesc.MultiSampleType):0));}
    void forget(IDirect3DPixelShader9* original){for(auto& shaders:shaderBanks){auto it=shaders.find(original);if(it!=shaders.end()){for(auto*& p:it->second.shader)release(p);shaders.erase(it);}}}
    void reset(){for(auto& shaders:shaderBanks){for(auto& e:shaders)for(auto*& p:e.second.shader)release(p);shaders.clear();}release(world);release(fixedUi);release(multiFixedUi);release(layerShader);release(layerSurface);release(layerTexture);release(layerMsaa);layerDesc={};uiScale=0;}
    template<class Draw> HRESULT legacyLayer(IDirect3DDevice9* d,float uiWhite,Draw submit){
        SavedState saved(d);if(!saved.ok)return E_FAIL;
        D3DSURFACE_DESC desc{};HRESULT hr=saved.targets[0]->GetDesc(&desc);if(FAILED(hr))return hr;
        ++layers;layerPixels+=std::uint64_t(desc.Width)*desc.Height;
        DWORD enabled=0,src=0,dst=0,writes=15;d->GetRenderState(D3DRS_ALPHABLENDENABLE,&enabled);d->GetRenderState(D3DRS_SRCBLEND,&src);d->GetRenderState(D3DRS_DESTBLEND,&dst);d->GetRenderState(D3DRS_COLORWRITEENABLE,&writes);
        const bool factor=enabled&&((src==D3DBLEND_DESTCOLOR&&dst==D3DBLEND_ZERO)||(src==D3DBLEND_ZERO&&(dst==D3DBLEND_SRCCOLOR||dst==D3DBLEND_INVSRCALPHA)));
        const bool premultiplied=enabled&&!factor&&(src==D3DBLEND_SRCALPHA||dst==D3DBLEND_INVSRCALPHA);
        if(desc.Width!=layerDesc.Width||desc.Height!=layerDesc.Height||desc.MultiSampleType!=layerDesc.MultiSampleType||desc.MultiSampleQuality!=layerDesc.MultiSampleQuality){release(layerSurface);release(layerTexture);release(layerMsaa);layerDesc=desc;}
        if(!layerTexture){hr=d->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&layerTexture,nullptr);if(SUCCEEDED(hr))hr=layerTexture->GetSurfaceLevel(0,&layerSurface);if(FAILED(hr))return hr;}
        if(desc.MultiSampleType&&!layerMsaa){hr=d->CreateRenderTarget(desc.Width,desc.Height,D3DFMT_A8R8G8B8,desc.MultiSampleType,desc.MultiSampleQuality,FALSE,&layerMsaa,nullptr);if(FAILED(hr))return hr;}
        if(!layerShader){hr=d->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrLayer),&layerShader);if(FAILED(hr))return hr;}
        for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
        hr=d->SetRenderTarget(0,layerMsaa?layerMsaa:layerSurface);if(FAILED(hr))return hr;
        const D3DVIEWPORT9 full={0,0,desc.Width,desc.Height,0,1};DWORD scissor=0;d->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor);
        d->SetViewport(&full);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->Clear(0,nullptr,D3DCLEAR_TARGET,factor?0xffffffff:0,1,0);
        d->SetViewport(&saved.viewport);d->SetRenderState(D3DRS_SCISSORTESTENABLE,scissor);
        d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLENDALPHA,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLENDALPHA,enabled?D3DBLEND_INVSRCALPHA:D3DBLEND_ZERO);d->SetRenderState(D3DRS_BLENDOPALPHA,D3DBLENDOP_ADD);
        hr=submit();if(FAILED(hr))return hr;
        if(layerMsaa){hr=d->StretchRect(layerMsaa,nullptr,layerSurface,nullptr,D3DTEXF_NONE);if(FAILED(hr))return hr;}
        hr=quadState(d,saved.targets[0],desc.Width,desc.Height);if(FAILED(hr))return hr;
        const float scale[]={uiWhite/80,uiWhite/80,uiWhite/80,1},parameters[]={premultiplied?1.f:0.f,factor&&src==D3DBLEND_ZERO&&dst==D3DBLEND_INVSRCALPHA?1.f:0.f,0,0};
        d->SetPixelShaderConstantF(0,scale,1);d->SetPixelShaderConstantF(2,parameters,1);d->SetPixelShader(layerShader);d->SetTexture(0,layerTexture);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,factor?D3DBLEND_DESTCOLOR:D3DBLEND_ONE);
        d->SetRenderState(D3DRS_DESTBLEND,factor?D3DBLEND_ZERO:!enabled||dst==D3DBLEND_INVSRCALPHA?D3DBLEND_INVSRCALPHA:D3DBLEND_ONE);
        d->SetRenderState(D3DRS_COLORWRITEENABLE,writes);
        return drawQuad(d,desc.Width,desc.Height);
    }
    IDirect3DPixelShader9* replacement(IDirect3DDevice9* d,IDirect3DPixelShader9* original,float uiWhite,unsigned variant){
        if(!original||variant>2)return nullptr;
        // World/sky radiance and UI white alternate every frame. Retain both
        // scales; a live UI adjustment invalidates only its own bounded bank.
        auto& shaders=shaderBanks[uiWhite==80?0:1];
        if(uiWhite!=80&&uiScale!=uiWhite){for(auto& e:shaders)for(auto*& p:e.second.shader)release(p);shaders.clear();uiScale=uiWhite;}
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
    template<class Draw> HRESULT fixedDraw(IDirect3DDevice9* d,float uiWhite,Draw submit){
        DWORD fog=0,coordinates=0,transform=0,blend=0,src=0,dst=0;
        d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);if(blend){d->GetRenderState(D3DRS_SRCBLEND,&src);d->GetRenderState(D3DRS_DESTBLEND,&dst);}
        if(blend&&src==D3DBLEND_ZERO&&dst==D3DBLEND_INVSRCALPHA){
            DWORD op=0,writes=0,srgb=0;
            if(SUCCEEDED(d->GetRenderState(D3DRS_BLENDOP,&op))&&op==D3DBLENDOP_ADD&&
               SUCCEEDED(d->GetRenderState(D3DRS_COLORWRITEENABLE,&writes))&&SUCCEEDED(d->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb))){
                // Cdst*(1-alpha) is independent of the color transfer. Keep
                // the legacy factor layer's destination-alpha policy as well.
                struct Restore{IDirect3DDevice9* d;DWORD writes,srgb;~Restore(){d->SetRenderState(D3DRS_COLORWRITEENABLE,writes);d->SetRenderState(D3DRS_SRGBWRITEENABLE,srgb);}}restore{d,writes,srgb};
                HRESULT hr=d->SetRenderState(D3DRS_COLORWRITEENABLE,writes&~D3DCOLORWRITEENABLE_ALPHA);
                if(SUCCEEDED(hr))hr=d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
                if(SUCCEEDED(hr)){++attenuationDirect;hr=submit();}return hr;
            }
        }
        float second[8]={0,1,0,0,0,1,0,0};const bool multi=!disabledStage(d,1);
        if(multi){
            DWORD result=0;
            if(!disabledStage(d,2)||FAILED(d->GetTextureStageState(0,D3DTSS_RESULTARG,&result))||result!=D3DTA_CURRENT||
               FAILED(d->GetTextureStageState(1,D3DTSS_RESULTARG,&result))||result!=D3DTA_CURRENT||
               FAILED(d->GetTextureStageState(1,D3DTSS_TEXCOORDINDEX,&coordinates))||coordinates>1||
               FAILED(d->GetTextureStageState(1,D3DTSS_TEXTURETRANSFORMFLAGS,&transform))||transform!=D3DTTFF_DISABLE)return legacyLayer(d,uiWhite,submit);
            second[3]=float(coordinates);
            for(unsigned channel=0;channel<2;++channel){
                float* flags=second+channel*4;flags[0]=flags[1]=flags[2]=0;DWORD op=0;
                if(FAILED(d->GetTextureStageState(1,channel?D3DTSS_ALPHAOP:D3DTSS_COLOROP,&op)))return E_FAIL;
                if(op!=D3DTOP_SELECTARG1&&op!=D3DTOP_SELECTARG2&&op!=D3DTOP_MODULATE)return legacyLayer(d,uiWhite,submit);
                for(unsigned arg=1;arg<=2;++arg){
                    if((op==D3DTOP_SELECTARG1&&arg==2)||(op==D3DTOP_SELECTARG2&&arg==1))continue;
                    DWORD value=0;const auto state=channel?(arg==1?D3DTSS_ALPHAARG1:D3DTSS_ALPHAARG2):(arg==1?D3DTSS_COLORARG1:D3DTSS_COLORARG2);
                    if(FAILED(d->GetTextureStageState(1,state,&value)))return E_FAIL;
                    const unsigned index=value==D3DTA_TEXTURE?0:value==D3DTA_CURRENT?1:value==D3DTA_DIFFUSE?2:3;
                    if(index==3||flags[index])return legacyLayer(d,uiWhite,submit);flags[index]=1;
                }
            }
            if(second[0]||second[4]){IDirect3DBaseTexture9* texture=nullptr;const HRESULT hr=d->GetTexture(1,&texture);const bool present=texture&&texture->GetType()==D3DRTYPE_TEXTURE;release(texture);if(FAILED(hr))return hr;if(!present)return legacyLayer(d,uiWhite,submit);}
        }
        if(FAILED(d->GetRenderState(D3DRS_FOGENABLE,&fog))||fog||
           FAILED(d->GetTextureStageState(0,D3DTSS_TEXCOORDINDEX,&coordinates))||coordinates||
           FAILED(d->GetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,&transform))||transform!=D3DTTFF_DISABLE)return legacyLayer(d,uiWhite,submit);
        float flags[4]{};
        for(unsigned channel=0;channel<2;++channel){
            DWORD op=0;if(FAILED(d->GetTextureStageState(0,channel?D3DTSS_ALPHAOP:D3DTSS_COLOROP,&op)))return E_FAIL;
            if(channel&&op==D3DTOP_DISABLE){flags[3]=1;continue;}
            if(op!=D3DTOP_SELECTARG1&&op!=D3DTOP_SELECTARG2&&op!=D3DTOP_MODULATE)return legacyLayer(d,uiWhite,submit);
            for(unsigned arg=1;arg<=2;++arg){
                if((op==D3DTOP_SELECTARG1&&arg==2)||(op==D3DTOP_SELECTARG2&&arg==1))continue;
                DWORD value=0;const auto state=channel?(arg==1?D3DTSS_ALPHAARG1:D3DTSS_ALPHAARG2):(arg==1?D3DTSS_COLORARG1:D3DTSS_COLORARG2);
                if(FAILED(d->GetTextureStageState(0,state,&value)))return E_FAIL;
                if(value==D3DTA_TEXTURE){if(flags[channel*2])return legacyLayer(d,uiWhite,submit);flags[channel*2]=1;}
                else if(value==D3DTA_DIFFUSE||value==D3DTA_CURRENT){if(flags[channel*2+1])return legacyLayer(d,uiWhite,submit);flags[channel*2+1]=1;}
                else return legacyLayer(d,uiWhite,submit);
            }
        }
        // Sampling an absent texture has special fixed-function semantics.
        if(flags[0]||flags[2]){IDirect3DBaseTexture9* texture=nullptr;const HRESULT hr=d->GetTexture(0,&texture);const bool present=texture!=nullptr;release(texture);if(FAILED(hr))return hr;if(!present)return legacyLayer(d,uiWhite,submit);}
        if(blend&&(src==D3DBLEND_DESTCOLOR||src==D3DBLEND_ZERO))return legacyLayer(d,uiWhite,submit);
        auto*& shader=multi?multiFixedUi:fixedUi;
        if(!shader){const HRESULT hr=d->CreatePixelShader(reinterpret_cast<const DWORD*>(multi?kHdrFixedUi2:kHdrFixedUi),&shader);if(FAILED(hr))return legacyLayer(d,uiWhite,submit);}
        const unsigned registers=multi?4:2;
        float constants[16];DWORD srgb=0;HRESULT hr=d->GetPixelShaderConstantF(0,constants,registers);if(FAILED(hr))return hr;
        hr=d->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb);if(FAILED(hr))return hr;
        struct Restore{IDirect3DDevice9* d;float* constants;unsigned registers;DWORD srgb,src;bool screen;~Restore(){d->SetPixelShader(nullptr);d->SetPixelShaderConstantF(0,constants,registers);d->SetRenderState(D3DRS_SRGBWRITEENABLE,srgb);if(screen)d->SetRenderState(D3DRS_SRCBLEND,src);}}restore{d,constants,registers,srgb,src,blend&&src==D3DBLEND_INVDESTCOLOR&&dst==D3DBLEND_ONE};
        const float scale[]={uiWhite/80,uiWhite/80,uiWhite/80,blend&&src==D3DBLEND_ONE&&dst==D3DBLEND_INVSRCALPHA?1.f:0.f};
        hr=d->SetPixelShader(shader);if(SUCCEEDED(hr))hr=d->SetPixelShaderConstantF(0,scale,1);
        if(SUCCEEDED(hr))hr=d->SetPixelShaderConstantF(1,flags,1);
        if(SUCCEEDED(hr)&&multi)hr=d->SetPixelShaderConstantF(2,second,2);
        if(SUCCEEDED(hr))hr=d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
        if(SUCCEEDED(hr)&&restore.screen){++screenDirect;hr=d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);}
        if(SUCCEEDED(hr)){if(multi)++multiTextureDirect;hr=submit();}return hr;
    }
    template<class Draw> HRESULT draw(IDirect3DDevice9* d,float uiWhite,Draw submit){
        IDirect3DPixelShader9* original=nullptr;
        if(FAILED(d->GetPixelShader(&original)))return E_FAIL;
        if(!original)return fixedDraw(d,uiWhite,submit);
        DWORD src=0,dst=0,srgb=0,blend=0;d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);
        if(blend){d->GetRenderState(D3DRS_SRCBLEND,&src);d->GetRenderState(D3DRS_DESTBLEND,&dst);}
        const HRESULT srgbStatus=d->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb);if(FAILED(srgbStatus)){original->Release();return srgbStatus;}
        const bool screen=blend&&src==D3DBLEND_INVDESTCOLOR&&dst==D3DBLEND_ONE;
        struct Restore {IDirect3DDevice9* d;IDirect3DPixelShader9* ps;DWORD srgb,src;bool screen;~Restore(){d->SetPixelShader(ps);if(srgb)d->SetRenderState(D3DRS_SRGBWRITEENABLE,srgb);if(screen)d->SetRenderState(D3DRS_SRCBLEND,src);ps->Release();}} restore{d,original,srgb,src,screen};
        // The compatibility layer converted screen sources to linear additive
        // light. A patched shader can do the same on its original geometry.
        const unsigned variant=blend&&src==D3DBLEND_ONE&&dst==D3DBLEND_INVSRCALPHA?1:
            (blend&&((src==D3DBLEND_DESTCOLOR&&dst==D3DBLEND_ZERO)||(src==D3DBLEND_ZERO&&dst==D3DBLEND_SRCCOLOR))?2:0);
        auto* shader=replacement(d,original,uiWhite,variant);if(!shader)return legacyLayer(d,uiWhite,submit);
        HRESULT hr=d->SetPixelShader(shader);if(SUCCEEDED(hr))hr=d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
        if(SUCCEEDED(hr)&&screen){++screenDirect;hr=d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);}
        if(SUCCEEDED(hr))hr=submit();return hr;
    }
    HRESULT convertWorld(IDirect3DDevice9* d,IDirect3DSurface9* source,IDirect3DSurface9* target,IDirect3DTexture9* copy,IDirect3DSurface9* copySurface,
                         unsigned width,unsigned height,float paperWhite,float maxNits,NorthlightStateBlockPool* pool=nullptr,float contrast=1,float exposure=0){
        if(!world){HRESULT hr=d->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrWorld),&world);if(FAILED(hr))return hr;}
        SavedState saved(d,pool);if(!saved.ok)return E_FAIL;
        HRESULT hr=d->SetTexture(0,nullptr);if(SUCCEEDED(hr))hr=d->StretchRect(source,nullptr,copySurface,nullptr,D3DTEXF_NONE);if(FAILED(hr))return hr;
        hr=quadState(d,target,width,height);if(FAILED(hr))return hr;
        float scale[]={paperWhite/80,paperWhite/80,paperWhite/80,maxNits/80},grade[]={contrast,std::exp2(exposure),0,0};d->SetPixelShaderConstantF(0,scale,1);d->SetPixelShaderConstantF(1,grade,1);d->SetPixelShader(world);d->SetTexture(0,copy);
        return drawQuad(d,width,height);
    }
};
}
