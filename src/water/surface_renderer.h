#pragma once
#include "surface_compiled_shaders.h"
#include "quality_settings.h"
#include "surface_frame.h"
#include "weather_lens.h"

// Included after SavedState, drop, logf and NameMaskCompositeBind.
class NorthlightSurfaceRenderer {
    friend struct NorthlightWeatherLensTestAccess;
    IDirect3DDevice9* d;NorthlightStateBlockPool stateBlocks;
    IDirect3DPixelShader9* weatherPS[4]={};IDirect3DPixelShader9* lensPS=nullptr;bool failed=false,lensFailed=false,weatherUsed=false;
    NorthlightWeatherLens::State lensState;
    std::array<NorthlightWeatherLens::Vertex,NorthlightWeatherLens::Capacity*6> lensVertices{};
    bool check(HRESULT hr,const char* stage){if(SUCCEEDED(hr))return true;if(!failed)logf("SURFACE disabled: %s hr=0x%08lx (native weather retained)",stage,(unsigned long)hr);failed=true;return false;}
    HRESULT quad(UINT w,UINT h){struct V{float x,y,z,w,u,v;} v[]={{-.5f,-.5f,0,1,0,0},{float(w)-.5f,-.5f,0,1,1,0},{-.5f,float(h)-.5f,0,1,0,1},{float(w)-.5f,float(h)-.5f,0,1,1,1}};D3DVIEWPORT9 vp={0,0,w,h,0,1};d->SetViewport(&vp);return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V));}
    void state(){
        d->SetDepthStencilSurface(nullptr);for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetStreamSourceFreq(0,1);
        for(auto s:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE,D3DRS_LIGHTING,D3DRS_SRGBWRITEENABLE,D3DRS_CLIPPLANEENABLE,D3DRS_WRAP0})d->SetRenderState(s,0);
        d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff);d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE);
        d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        DWORD point=0;d->GetRenderState(D3DRS_POINTSIZE,&point);if(point==MAKEFOURCC('A','2','M','1'))d->SetRenderState(D3DRS_POINTSIZE,MAKEFOURCC('A','2','M','0'));
        for(unsigned i=0;i<8;++i){d->SetTexture(i,nullptr);d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_MINFILTER,i==0||i==4?D3DTEXF_LINEAR:D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MAGFILTER,i==0||i==4?D3DTEXF_LINEAR:D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(i,D3DSAMP_MAXMIPLEVEL,0);d->SetSamplerState(i,D3DSAMP_MIPMAPLODBIAS,0);d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE);}
    }
    void blend(){d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);}
public:
    explicit NorthlightSurfaceRenderer(IDirect3DDevice9* device):d(device),stateBlocks(device){}
    ~NorthlightSurfaceRenderer(){reset();for(auto& p:weatherPS)drop(p);drop(lensPS);}
    void reset(){stateBlocks.clear();failed=lensFailed=weatherUsed=false;lensState={};}
    void endFrame(){if(!weatherUsed||failed)stateBlocks.clear();weatherUsed=false;}
    void precipitation(IDirect3DSurface9* targetSurface,IDirect3DTexture9* finalDepth,UINT w,UINT h,const NorthlightSurfaceFrame& context,NorthlightQuality::Preset preset,IDirect3DSurface9* nameDepth){
        if(failed||!finalDepth||context.c[3][0]<=0)return;SavedState saved(d,&stateBlocks);if(!saved.ok)return;state();unsigned i=(preset==NorthlightQuality::Preset::Performance?2:0)+(context.c[3][1]>.5f?1:0);
        weatherUsed=true;
        const DWORD* shaders[]={kRainQualityShader,kSnowQualityShader,kRainPerformanceShader,kSnowPerformanceShader};
        if(!weatherPS[i]&&!check(d->CreatePixelShader(shaders[i],&weatherPS[i]),"precipitation shader"))return;
        if(!check(d->SetRenderTarget(0,targetSurface),"precipitation target"))return;blend();d->SetRenderState(D3DRS_COLORWRITEENABLE,7);d->SetPixelShader(weatherPS[i]);d->SetTexture(3,finalDepth);d->SetPixelShaderConstantF(0,&context.c[0][0],10);
        NameMaskCompositeBind protect(d,nameDepth);check(quad(w,h),"supplemental precipitation");
    }
    void lens(IDirect3DSurface9* target,IDirect3DTexture9* scene,IDirect3DSurface9* sceneSurface,UINT w,UINT h,bool snow,double time,float visibility,IDirect3DSurface9* nameDepth,float intensity=1.f,float heat=0.f){
        if(lensFailed||!target||!scene||!sceneSurface||target==sceneSurface||!w||!h||!(visibility>0))return;
        lensState.advance(time,intensity,snow,heat,float(w)/float(h));
        const unsigned vertices=lensState.vertices(lensVertices.data(),h);if(!vertices)return;
        SavedState saved(d,&stateBlocks);if(!saved.ok)return;state();
        // The earlier world copy is no longer current after world and spell composition.
        // Reuse its texture, copying only on frames that actually draw lens optics.
        if(!lensPS&&FAILED(d->CreatePixelShader(kWeatherLensParticleShader,&lensPS))){lensFailed=true;logf("SURFACE lens shader unavailable");return;}
        if(FAILED(d->StretchRect(target,nullptr,sceneSurface,nullptr,D3DTEXF_NONE))||FAILED(d->SetRenderTarget(0,target))){lensFailed=true;logf("SURFACE lens scene copy unavailable");return;}
        weatherUsed=true;const float c[4]={0,visibility,float(w)/float(h),
                                         std::isfinite(intensity)?std::clamp(intensity,0.f,1.f):0.f};d->SetRenderState(D3DRS_COLORWRITEENABLE,7);
        d->SetPixelShader(lensPS);d->SetTexture(0,scene);d->SetPixelShaderConstantF(10,c,1);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
        d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
        d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX4|D3DFVF_TEXCOORDSIZE4(2)|D3DFVF_TEXCOORDSIZE4(3));
        const D3DVIEWPORT9 viewport={0,0,w,h,0,1};d->SetViewport(&viewport);
        NameMaskCompositeBind protect(d,nameDepth);
        if(FAILED(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,vertices/3,lensVertices.data(),sizeof(lensVertices[0])))){lensFailed=true;logf("SURFACE lens draw unavailable");}
    }
};
