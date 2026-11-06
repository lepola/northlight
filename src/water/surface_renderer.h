#pragma once
#include "surface_compiled_shaders.h"
#include "quality_settings.h"
#include "surface_frame.h"
#include "weather_lens.h"
#include "weather_sand_lens.h"
#include "weather_texture_data.h"
#include <cstring>

// Included after SavedState, drop, logf and NameMaskCompositeBind.
class NorthlightSurfaceRenderer {
    friend struct NorthlightWeatherLensTestAccess;
    friend struct NorthlightWeatherArtTestAccess;
    IDirect3DDevice9* d;NorthlightStateBlockPool stateBlocks;
    IDirect3DPixelShader9* weatherPS[4]={};IDirect3DPixelShader9* lensPS=nullptr;bool failed=false,lensFailed=false,weatherUsed=false;
    IDirect3DPixelShader9* coatingPS[2]={};bool coatingFailed[2]={};NorthlightWeatherSand::State sandState;
    IDirect3DTexture9* art[3]={};bool artFailed[3]={};
    NorthlightWeatherMotion::Tracker motion;
    NorthlightWeatherLens::State lensState;
    std::array<NorthlightWeatherLens::Vertex,NorthlightWeatherLens::MaxVertices> lensVertices{};
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
    bool ensureArt(unsigned index){
        if(art[index])return true;if(artFailed[index])return false;
        using namespace NorthlightWeatherArt;
        UINT w=index==2?kNoiseWidth:index?kSnowWidth:kFrostWidth,h=index==2?kNoiseHeight:index?kSnowHeight:kFrostHeight;
        const unsigned char* bytes=index==2?kNoiseTexels:index?kSnowTexels:kFrostTexels;
        HRESULT hr=d->CreateTexture(w,h,0,0,D3DFMT_L8,D3DPOOL_MANAGED,&art[index],nullptr);
        if(SUCCEEDED(hr))for(UINT level=0,levels=art[index]->GetLevelCount();level<levels;++level){
            D3DLOCKED_RECT lock{};hr=art[index]->LockRect(level,&lock,nullptr,0);if(FAILED(hr))break;
            for(UINT y=0;y<h;++y)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,bytes+y*w,w);
            hr=art[index]->UnlockRect(level);if(FAILED(hr))break;
            bytes+=w*h;w=std::max(1u,w/2);h=std::max(1u,h/2);
        }
        if(FAILED(hr)){drop(art[index]);artFailed[index]=true;logf("SURFACE weather material unavailable: %u",index);return false;}return true;
    }
    void bindArt(unsigned index,unsigned sampler){
        d->SetTexture(sampler,art[index]);d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_LINEAR);
    }
public:
    explicit NorthlightSurfaceRenderer(IDirect3DDevice9* device):d(device),stateBlocks(device){}
    ~NorthlightSurfaceRenderer(){reset();for(auto& p:weatherPS)drop(p);for(auto& p:coatingPS)drop(p);for(auto& p:art)drop(p);drop(lensPS);}
    void reset(){stateBlocks.clear();failed=lensFailed=weatherUsed=false;lensState={};sandState={};motion={};coatingFailed[0]=coatingFailed[1]=false;for(auto& f:artFailed)f=false;}
    void setMotion(const NorthlightSurfaceFrame& context,unsigned kind,const NorthlightQuality::Settings& q,double time){motion.update(context,kind,q.weatherWindDirection,q.weatherWindStrength,q.weatherCameraMotion!=0,time);}
    const NorthlightWeatherMotion::Flow& weatherFlow()const{return motion.flow;}
    void endFrame(){if(!weatherUsed||failed)stateBlocks.clear();weatherUsed=false;}
    unsigned lensParticleCount()const{return lensState.count;}
    unsigned lensMergeCount()const{return lensState.merges;}
    float frostCoverage()const{return lensState.frost;}
    void precipitation(IDirect3DSurface9* targetSurface,IDirect3DTexture9* finalDepth,UINT w,UINT h,const NorthlightSurfaceFrame& context,NorthlightQuality::Preset preset,IDirect3DSurface9* nameDepth){
        if(failed||!finalDepth||context.c[3][0]<=0)return;const bool snow=context.c[3][1]>.5f;if(snow&&!ensureArt(1))return;
        SavedState saved(d,&stateBlocks);if(!saved.ok)return;state();unsigned i=(preset==NorthlightQuality::Preset::Performance?2:0)+(snow?1:0);
        weatherUsed=true;
        const DWORD* shaders[]={kRainQualityShader,kSnowQualityShader,kRainPerformanceShader,kSnowPerformanceShader};
        if(!weatherPS[i]&&!check(d->CreatePixelShader(shaders[i],&weatherPS[i]),"precipitation shader"))return;
        if(!check(d->SetRenderTarget(0,targetSurface),"precipitation target"))return;blend();d->SetRenderState(D3DRS_COLORWRITEENABLE,7);d->SetPixelShader(weatherPS[i]);d->SetTexture(3,finalDepth);d->SetPixelShaderConstantF(0,&context.c[0][0],10);
        if(snow)bindArt(1,2);
        NameMaskCompositeBind protect(d,nameDepth);check(quad(w,h),"supplemental precipitation");
    }
    void lens(IDirect3DSurface9* target,IDirect3DTexture9* scene,IDirect3DSurface9* sceneSurface,UINT w,UINT h,bool snow,double time,float visibility,IDirect3DSurface9* nameDepth,float intensity=1.f,float heat=0.f,float size=1.f,bool rivulets=true,float frostVisibility=0.f){
        if(lensFailed||!target||!scene||!sceneSurface||target==sceneSurface||!w||!h||(!(visibility>0)&&!(frostVisibility>0)))return;
        lensState.advance(time,intensity,snow,heat,float(w)/float(h),size,rivulets,motion.flow);
        unsigned vertices=visibility>0?lensState.vertices(lensVertices.data(),h,rivulets):0;
        float frost=snow?lensState.frost*std::clamp(frostVisibility,0.f,1.f):0;
        bool snowParticles=snow||lensState.fadeCount>0;if(vertices&&!snowParticles)for(unsigned i=0;i<lensState.count;++i)snowParticles=snowParticles||lensState.drops[i].visualSnow>0;
        if(snowParticles&&vertices&&!ensureArt(1))vertices=0;if(frost>0&&!ensureArt(0))frost=0;
        if(!vertices&&!(frost>0))return;
        SavedState saved(d,&stateBlocks);if(!saved.ok)return;state();
        // The earlier world copy is no longer current after world and spell composition.
        // Reuse its texture, copying only on frames that actually draw lens optics.
        if(vertices&&!lensPS&&FAILED(d->CreatePixelShader(kWeatherLensParticleShader,&lensPS))){lensFailed=true;logf("SURFACE lens shader unavailable");return;}
        if(FAILED(d->StretchRect(target,nullptr,sceneSurface,nullptr,D3DTEXF_NONE))||FAILED(d->SetRenderTarget(0,target))){lensFailed=true;logf("SURFACE lens scene copy unavailable");return;}
        weatherUsed=true;const float c[4]={0,visibility,float(w)/float(h),
                                         std::isfinite(intensity)?std::clamp(intensity,0.f,1.f):0.f};d->SetRenderState(D3DRS_COLORWRITEENABLE,7);
        d->SetTexture(0,scene);d->SetPixelShaderConstantF(10,c,1);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
        d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
        const D3DVIEWPORT9 viewport={0,0,w,h,0,1};d->SetViewport(&viewport);
        NameMaskCompositeBind protect(d,nameDepth);
        if(vertices){if(snowParticles)bindArt(1,2);d->SetPixelShader(lensPS);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX4|D3DFVF_TEXCOORDSIZE4(2)|D3DFVF_TEXCOORDSIZE4(3));
            if(FAILED(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,vertices/3,lensVertices.data(),sizeof(lensVertices[0])))){lensFailed=true;logf("SURFACE lens draw unavailable");}}
        if(frost>0&&!coatingFailed[0]){
            if(!coatingPS[0]&&FAILED(d->CreatePixelShader(kFrostVignetteShader,&coatingPS[0]))){coatingFailed[0]=true;logf("SURFACE frost shader unavailable");return;}
            const float coating[4]={frost,0,float(lensState.clock.elapsed),1};d->SetPixelShaderConstantF(11,coating,1);
            bindArt(0,1);
            d->SetPixelShader(coatingPS[0]);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);
            if(FAILED(quad(w,h))){coatingFailed[0]=true;logf("SURFACE frost draw unavailable");}
        }
    }
    void sandLens(IDirect3DSurface9* target,UINT w,UINT h,double time,float visibility,IDirect3DSurface9* nameDepth,float intensity=1.f,float size=1.f){
        if(coatingFailed[1]||!target||!w||!h||!(visibility>0))return;
        sandState.advance(time,intensity,float(w)/h,std::clamp(size,.25f,3.f),motion.flow);
        const unsigned vertices=sandState.vertices(lensVertices.data(),h);
        if(!ensureArt(2))return;
        SavedState saved(d,&stateBlocks);if(!saved.ok)return;state();
        if(!coatingPS[1]&&FAILED(d->CreatePixelShader(kSandLensShader,&coatingPS[1]))){coatingFailed[1]=true;logf("SURFACE sand shader unavailable");return;}
        if(FAILED(d->SetRenderTarget(0,target)))return;weatherUsed=true;
        const float lens[4]={2,visibility,float(w)/h,std::clamp(intensity,0.f,1.f)};
        const float coating[4]={0,std::clamp(visibility,0.f,1.f),float(sandState.clock.elapsed),std::clamp(size,.25f,3.f)};
        d->SetPixelShaderConstantF(10,lens,1);d->SetPixelShaderConstantF(11,coating,1);d->SetPixelShader(coatingPS[1]);
        bindArt(2,4);d->SetSamplerState(4,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(4,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
        d->SetRenderState(D3DRS_COLORWRITEENABLE,7);d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
        d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
        NameMaskCompositeBind protect(d,nameDepth);
        if(FAILED(quad(w,h))){coatingFailed[1]=true;logf("SURFACE sand draw unavailable");}
        if(vertices&&!coatingFailed[1]){
            if(!lensPS&&FAILED(d->CreatePixelShader(kWeatherLensParticleShader,&lensPS))){coatingFailed[1]=true;return;}
            d->SetPixelShader(lensPS);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX4|D3DFVF_TEXCOORDSIZE4(2)|D3DFVF_TEXCOORDSIZE4(3));
            if(FAILED(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,vertices/3,lensVertices.data(),sizeof(lensVertices[0])))){coatingFailed[1]=true;logf("SURFACE sand lens particles unavailable");}
        }
    }
};
