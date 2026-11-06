#pragma once
#include "sky_composition.h"
#include "sky_alpha_patch.h"
#include <map>
#include <d3d9.h>
#include "saved_state.h"

// Replay the original sky shader and geometry: its animated alpha, second
// texture, alpha test and raster coverage remain authoritative. No depth is
// written. In particular a later skybox background replenishes transmission
// during a blend between skies; multiplying all cloud alphas cannot do that.
namespace NorthlightSkyComposition {
class Capture {
    template<class T> static void release(T*& p){if(p)p->Release();p=nullptr;}
    IDirect3DTexture9 *mask_=nullptr,*copy_=nullptr;
    IDirect3DSurface9 *surface_=nullptr,*copySurface_=nullptr,*msaa_=nullptr;
    IDirect3DDevice9* device_=nullptr;
    D3DMULTISAMPLE_TYPE samples_=D3DMULTISAMPLE_NONE;DWORD quality_=0;bool dirty_=false;
    IDirect3DSurface9* target_=nullptr; // identity only; owned by the caller
    unsigned frame_=~0u,width_=0,height_=0;
    D3DFORMAT sourceFormat_=D3DFMT_UNKNOWN;
    bool valid_=false;
    struct AlphaShader {IDirect3DPixelShader9 *original=nullptr,*patched=nullptr;unsigned control=0;};
    std::map<std::pair<IDirect3DPixelShader9*,unsigned>,AlphaShader> alphaShaders_;
    bool captureShader(IDirect3DDevice9* d,bool opaque){
        DWORD test=0,function=8,reference=0;
        if(FAILED(d->GetRenderState(D3DRS_ALPHATESTENABLE,&test))||FAILED(d->GetRenderState(D3DRS_ALPHAFUNC,&function))||FAILED(d->GetRenderState(D3DRS_ALPHAREF,&reference)))return false;
        if(!test)function=8;
        if(!opaque)function=0;
        IDirect3DPixelShader9* original=nullptr;if(FAILED(d->GetPixelShader(&original)))return false;
        if(!original){
            if(!opaque)return true;
            if(test&&function!=8)return false;
            unsigned last=8;
            for(unsigned stage=0;stage<8;++stage){
                DWORD op=0,result=0;if(FAILED(d->GetTextureStageState(stage,D3DTSS_COLOROP,&op)))return false;if(op==D3DTOP_DISABLE)break;
                if(FAILED(d->GetTextureStageState(stage,D3DTSS_RESULTARG,&result)))return false;if(result==D3DTA_CURRENT)last=stage;
            }
            if(last==8)return false;
            // Preserve TFACTOR and intermediate alpha: later colour stages can
            // read either. Only the final CURRENT alpha becomes a+(1-a)=1.
            return SUCCEEDED(d->SetTextureStageState(last,D3DTSS_ALPHAOP,D3DTOP_ADD))&&
                   SUCCEEDED(d->SetTextureStageState(last,D3DTSS_ALPHAARG1,D3DTA_TFACTOR))&&
                   SUCCEEDED(d->SetTextureStageState(last,D3DTSS_ALPHAARG2,D3DTA_TFACTOR|D3DTA_COMPLEMENT));
        }
        const auto key=std::make_pair(original,unsigned(function));auto entry=alphaShaders_.find(key);
        if(entry==alphaShaders_.end()){
            UINT bytes=0;std::vector<std::uint32_t> code,patched;unsigned control=0;IDirect3DPixelShader9* shader=nullptr;
            if(SUCCEEDED(original->GetFunction(nullptr,&bytes))&&bytes<=65536*4&&bytes%4==0){code.resize(bytes/4);
                if(SUCCEEDED(original->GetFunction(code.data(),&bytes))&&patchOutput(code.data(),code.size(),function,patched,control))d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&shader);}
            entry=alphaShaders_.emplace(key,AlphaShader{original,shader,control}).first;original=nullptr;
        }
        release(original);const auto& shader=entry->second;if(!shader.patched)return false;
        const float control[]={1,reference/255.f,0,.5f};
        return SUCCEEDED(d->SetPixelShader(shader.patched))&&(!opaque||(SUCCEEDED(d->SetPixelShaderConstantF(shader.control,control,1))&&SUCCEEDED(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE))));
    }
public:
    ~Capture(){reset();}
    void reset(){for(auto& entry:alphaShaders_){release(entry.second.original);release(entry.second.patched);}alphaShaders_.clear();release(surface_);release(mask_);release(copySurface_);release(copy_);release(msaa_);device_=nullptr;dirty_=false;target_=nullptr;width_=height_=0;sourceFormat_=D3DFMT_UNKNOWN;valid_=false;frame_=~0u;}
    void begin(unsigned frame){if(frame_!=frame){frame_=frame;valid_=false;target_=nullptr;}}
    void invalidate(){valid_=false;target_=nullptr;}
    void targetSet(std::uintptr_t){} // temporary offscreen passes do not erase main-sky coverage
    void colorWrite(std::uintptr_t target){if(reinterpret_cast<std::uintptr_t>(target_)==target)invalidate();}
    IDirect3DTexture9* texture(unsigned frame){
        if(!valid_||frame_!=frame)return nullptr;
        if(msaa_&&dirty_){if(FAILED(device_->StretchRect(msaa_,nullptr,surface_,nullptr,D3DTEXF_NONE))){invalidate();return nullptr;}dirty_=false;}
        return mask_;
    }
    bool prepare(IDirect3DDevice9* d,NorthlightStateBlockPool* pool=nullptr){
        if(valid_){IDirect3DSurface9* current=nullptr;const bool same=SUCCEEDED(d->GetRenderTarget(0,&current))&&current==target_;release(current);if(same)return true;}
        valid_=false;target_=nullptr;
        SavedState saved(d,pool);if(!saved.ok)return false;
        D3DSURFACE_DESC desc{};if(FAILED(saved.targets[0]->GetDesc(&desc)))return false;
        if(width_!=desc.Width||height_!=desc.Height||sourceFormat_!=desc.Format||samples_!=desc.MultiSampleType||quality_!=desc.MultiSampleQuality){
            release(surface_);release(mask_);release(copySurface_);release(copy_);release(msaa_);
            samples_=desc.MultiSampleType;quality_=desc.MultiSampleQuality;
            width_=desc.Width;height_=desc.Height;sourceFormat_=desc.Format;
        }
        if(!mask_&&(FAILED(d->CreateTexture(width_,height_,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&mask_,nullptr))||FAILED(mask_->GetSurfaceLevel(0,&surface_))))return false;
        if(samples_&&!msaa_&&FAILED(d->CreateRenderTarget(width_,height_,D3DFMT_A16B16G16R16F,samples_,quality_,FALSE,&msaa_,nullptr)))return false;
        if(!copy_&&(FAILED(d->CreateTexture(width_,height_,1,D3DUSAGE_RENDERTARGET,sourceFormat_,D3DPOOL_DEFAULT,&copy_,nullptr))||FAILED(copy_->GetSurfaceLevel(0,&copySurface_))))return false;
        if(FAILED(d->StretchRect(saved.targets[0],nullptr,copySurface_,nullptr,D3DTEXF_NONE)))return false;
        DWORD srgb=0;d->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb);
        d->SetDepthStencilSurface(nullptr);for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
        if(FAILED(d->SetRenderTarget(0,msaa_?msaa_:surface_)))return false;
        D3DVIEWPORT9 viewport={0,0,width_,height_,0,1};d->SetViewport(&viewport);
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})d->SetRenderState(state,FALSE);
        d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
        d->SetRenderState(D3DRS_TEXTUREFACTOR,0xffffffff);d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetStreamSourceFreq(0,1);
        d->SetTexture(0,copy_);d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
        d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TFACTOR);d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
        d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(0,state,D3DTEXF_POINT);
        d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,srgb);
        struct Vertex{float x,y,z,w,u,v;};const Vertex q[]={{-.5f,-.5f,0,1,0,0},{width_-.5f,-.5f,0,1,1,0},{-.5f,height_-.5f,0,1,0,1},{width_-.5f,height_-.5f,0,1,1,1}};
        valid_=SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex)));if(valid_){target_=saved.targets[0];device_=d;dirty_=true;}return valid_;
    }
    template<class Draw> bool draw(IDirect3DDevice9* d,Role role,Draw submit,NorthlightStateBlockPool* pool=nullptr){
        if(!valid_||role==Role::Unknown)return false;
        SavedState saved(d,pool);if(!saved.ok||saved.targets[0]!=target_)return false;
        DWORD blend=0,src=0,dst=0,op=0,writes=0;
        if(FAILED(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))||FAILED(d->GetRenderState(D3DRS_SRCBLEND,&src))||FAILED(d->GetRenderState(D3DRS_DESTBLEND,&dst))||FAILED(d->GetRenderState(D3DRS_BLENDOP,&op))||FAILED(d->GetRenderState(D3DRS_COLORWRITEENABLE,&writes)))return false;
        if(!(writes&7))return true;
        if(!blend){src=D3DBLEND_ONE;dst=D3DBLEND_ZERO;op=D3DBLENDOP_ADD;}
        if(op!=D3DBLENDOP_ADD||(dst!=D3DBLEND_ZERO&&dst!=D3DBLEND_ONE&&dst!=D3DBLEND_INVSRCALPHA)||(src!=D3DBLEND_ONE&&src!=D3DBLEND_SRCALPHA))return false;
        if(!captureShader(d,src==D3DBLEND_ONE&&dst==D3DBLEND_ZERO&&role==Role::Background))return false;
        for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
        if(FAILED(d->SetRenderTarget(0,msaa_?msaa_:surface_)))return false;
        d->SetViewport(&saved.viewport);d->SetRenderState(D3DRS_STENCILWRITEMASK,0);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
        d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
        d->SetRenderState(D3DRS_SRCBLEND,role==Role::Background?src:DWORD(D3DBLEND_ZERO));d->SetRenderState(D3DRS_DESTBLEND,dst);
        d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_BLENDOPALPHA,D3DBLENDOP_ADD);
        d->SetRenderState(D3DRS_SRCBLENDALPHA,role==Role::Background&&dst!=D3DBLEND_ONE?D3DBLEND_ONE:D3DBLEND_ZERO);d->SetRenderState(D3DRS_DESTBLENDALPHA,dst);
        const bool ok=SUCCEEDED(submit());if(!ok)invalidate();else dirty_=true;return ok;
    }
};
}
