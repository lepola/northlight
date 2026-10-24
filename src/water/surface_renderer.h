#pragma once
#include "surface_compiled_shaders.h"
#include "quality_settings.h"
#include "surface_frame.h"

// Included after SavedState, drop, logf and NameMaskCompositeBind.
class NorthlightSurfaceRenderer {
    IDirect3DDevice9* d;NorthlightStateBlockPool stateBlocks;
    IDirect3DPixelShader9* weatherPS[2]={};bool failed=false,weatherUsed=false;
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
    ~NorthlightSurfaceRenderer(){reset();for(auto& p:weatherPS)drop(p);}
    void reset(){stateBlocks.clear();failed=weatherUsed=false;}
    void endFrame(){if(!weatherUsed||failed)stateBlocks.clear();weatherUsed=false;}
    void precipitation(IDirect3DSurface9* targetSurface,IDirect3DTexture9* finalDepth,UINT w,UINT h,const NorthlightSurfaceFrame& context,NorthlightQuality::Preset preset,IDirect3DSurface9* nameDepth){
        if(failed||!finalDepth||context.c[3][0]<=0)return;SavedState saved(d,&stateBlocks);if(!saved.ok)return;state();unsigned i=preset==NorthlightQuality::Preset::Performance?1:0;
        weatherUsed=true;
        if(!weatherPS[i]&&!check(d->CreatePixelShader(i?kPrecipitationPerformanceShader:kPrecipitationQualityShader,&weatherPS[i]),"precipitation shader"))return;
        if(!check(d->SetRenderTarget(0,targetSurface),"precipitation target"))return;blend();d->SetRenderState(D3DRS_COLORWRITEENABLE,7);d->SetPixelShader(weatherPS[i]);d->SetTexture(3,finalDepth);d->SetPixelShaderConstantF(0,&context.c[0][0],10);
        NameMaskCompositeBind protect(d,nameDepth);check(quad(w,h),"supplemental precipitation");
    }
};
