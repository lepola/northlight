#pragma once
#include <d3d9.h>
namespace NorthlightHdr {
// Subsequent passes in an uninterrupted quad batch retain shader/sampler state.
// Unbind inputs before changing the output to avoid read/write texture hazards.
inline HRESULT quadTarget(IDirect3DDevice9* device,IDirect3DSurface9* target,unsigned width,unsigned height,unsigned samplers=1){
    for(unsigned i=0;i<samplers;++i)device->SetTexture(i,nullptr);
    HRESULT hr=device->SetRenderTarget(0,target);if(FAILED(hr))return hr;
    D3DVIEWPORT9 viewport={0,0,width,height,0,1};return device->SetViewport(&viewport);
}
inline HRESULT quadState(IDirect3DDevice9* device,IDirect3DSurface9* target,unsigned width,unsigned height,unsigned samplers=1){
    device->SetDepthStencilSurface(nullptr);for(unsigned i=1;i<4;++i)device->SetRenderTarget(i,nullptr);
    HRESULT hr=quadTarget(device,target,width,height,samplers);if(FAILED(hr))return hr;
    device->SetVertexShader(nullptr);device->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);device->SetStreamSourceFreq(0,1);
    const struct{D3DRENDERSTATETYPE state;DWORD value;} states[]={
        {D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},
        {D3DRS_SEPARATEALPHABLENDENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},
        {D3DRS_FOGENABLE,FALSE},{D3DRS_LIGHTING,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_FILLMODE,D3DFILL_SOLID},
        {D3DRS_COLORWRITEENABLE,15},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0},{D3DRS_MULTISAMPLEMASK,0xffffffff}};
    for(const auto& state:states){hr=device->SetRenderState(state.state,state.value);if(FAILED(hr))return hr;}
    for(unsigned i=0;i<samplers;++i){
        device->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT);device->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT);device->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        device->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);device->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);device->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE);
    }
    return S_OK;
}
inline HRESULT drawQuad(IDirect3DDevice9* device,unsigned width,unsigned height){
    struct Vertex{float x,y,z,w,u,v;};
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{float(width)-.5f,-.5f,0,1,1,0},{-.5f,float(height)-.5f,0,1,0,1},{float(width)-.5f,float(height)-.5f,0,1,1,1}};
    return device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex));
}
}
