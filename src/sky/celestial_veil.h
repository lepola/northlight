#pragma once
// veil draw (CelestialVeilPS): the D3D call sequence only, templated on the
// device so the tests can record it. Include after d3d9.h and drop().
// Called inside renderEffects after water, before UI; the caller's SavedState
// restores every state set here, so nothing is saved or restored per draw.
namespace NorthlightCelestialVeil {
// Constant registers uploaded: c0..c12 (image, projection, view, basis, tint,
// policy, repair, emission) and c46..c48 (terrain policy, hue, core+wrap).
constexpr unsigned LowRegisters=13,HighRegister=46,HighRegisters=3,Registers=49;
// Samplers: s0 depth, s1 unused, s2 water, s3 halo, s4 terrain mask, s5 ring.
constexpr unsigned Samplers=6;
struct Stats {unsigned draws=0,targetSwitches=0,skipped=0;};
// Screen blend (INVDESTCOLOR/ONE) of the shader's 1-exp(-x) output, like the glare.
// No draw at strength 0 (effects off, world debug and a missing sun glare are
// gated by the caller). The target is the one water just composited into:
// when it is still bound, no SetRenderTarget is issued (no new render pass).
template<class Device,class Surface,class Shader,class Texture>
bool draw(Device* d,Surface* target,Shader* ps,Texture* const (&textures)[Samplers],const float (&c)[Registers][4],
          const int (&bounds)[4],unsigned width,unsigned height,Stats& stats){
    if(!d||!target||!ps||!width||!height||!(c[12][2]+c[47][3]>0)||bounds[0]>=bounds[2]||bounds[1]>=bounds[3]){++stats.skipped;return false;}
    Surface* current=nullptr;
    const bool bound=SUCCEEDED(d->GetRenderTarget(0,&current))&&current==target;drop(current);
    if(!bound){if(FAILED(d->SetRenderTarget(0,target)))return false;++stats.targetSwitches;}
    D3DVIEWPORT9 viewport={0,0,width,height,0,1};d->SetViewport(&viewport);
    d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetPixelShader(ps);
    const struct{D3DRENDERSTATETYPE s;DWORD v;} states[]={{D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},
        {D3DRS_ALPHABLENDENABLE,TRUE},{D3DRS_SRCBLEND,D3DBLEND_INVDESTCOLOR},{D3DRS_DESTBLEND,D3DBLEND_ONE},{D3DRS_BLENDOP,D3DBLENDOP_ADD},
        {D3DRS_SEPARATEALPHABLENDENABLE,FALSE},{D3DRS_COLORWRITEENABLE,7},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,TRUE},
        {D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_FOGENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0}};
    for(const auto& s:states)d->SetRenderState(s.s,s.v);
    if(c[10][0]>1.5f)d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);
    for(unsigned k=0;k<Samplers;++k){
        d->SetTexture(k,textures[k]);
        d->SetSamplerState(k,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(k,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        const DWORD filter=k==4?D3DTEXF_LINEAR:D3DTEXF_POINT;
        d->SetSamplerState(k,D3DSAMP_MINFILTER,filter);d->SetSamplerState(k,D3DSAMP_MAGFILTER,filter);
        d->SetSamplerState(k,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(k,D3DSAMP_SRGBTEXTURE,FALSE);
    }
    RECT scissor={bounds[0],bounds[1],bounds[2],bounds[3]};d->SetScissorRect(&scissor);
    if(FAILED(d->SetPixelShaderConstantF(0,c[0],LowRegisters))||FAILED(d->SetPixelShaderConstantF(HighRegister,c[HighRegister],HighRegisters)))return false;
    struct V{float x,y,z,w,u,v;} vertices[]={{-.5f,-.5f,0,1,0,0},{float(width)-.5f,-.5f,0,1,1,0},{-.5f,float(height)-.5f,0,1,0,1},{float(width)-.5f,float(height)-.5f,0,1,1,1}};
    if(FAILED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(V))))return false;
    ++stats.draws;return true;
}
} // namespace NorthlightCelestialVeil
