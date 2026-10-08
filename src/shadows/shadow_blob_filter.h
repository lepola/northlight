#pragma once
// Identifies the game's unit "blob" shadow decal by texture content and lets the
// device wrapper skip those draws (BlobShadowStrength=0) or draw them with a lighter
// texture (1..99; skipped when the blend cannot be lightened) while the extension's own shadow maps are on.
// Include after logf(const char*, ...). Never wraps or retains game textures:
// a verdict is cached per texture pointer, re-validated by level description on
// every claim and by content every 600 frames to survive pointer reuse.
#include "diagnostics_switch.h"
#include <d3d9.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>
#include "actor_texture.h"
#include "shadow_blob_model.h"

class NorthlightShadowBlobFilter {
    struct Verdict {bool blob=false;UINT width=0,height=0;D3DFORMAT format=D3DFMT_UNKNOWN;unsigned checkedFrame=0;};
    IDirect3DDevice9* d;
    // 0.3.196 (task 12): optional borrowed-texture peek (the renderer's mirror); null or false = the GetTexture path.
    bool (*peekTexture)(void*,DWORD,IDirect3DBaseTexture9*&)=nullptr;void* peekContext=nullptr;
    std::unordered_map<IDirect3DBaseTexture9*,Verdict> verdicts;
    std::vector<std::uint8_t> raw,decoded;
    const std::vector<std::uint8_t> reference=NorthlightShadowBlobModel::referencePixels();   // ~10 us once, at construction
    unsigned frame=0,skippedThisFrame=0,skippedTotal=0,identified=0,unsupported=0,reported=0;
    const unsigned strength;                 // 0.3.193 BlobShadowStrength 0..100 (clamped by the quality parser), read once at construction; the Device asks strength()/active()
    IDirect3DTexture9* faint=nullptr;unsigned faintFailures=0,faintFailedFrame=0;
    unsigned faintThisFrame=0,faintTotal=0,statesLogged=0;
    static void release(IUnknown* p){if(p)p->Release();}
    bool contentMatches(IDirect3DTexture9* texture,const D3DSURFACE_DESC& desc){
        using Format=NorthlightActorTexture::Format;Format format;
        switch(desc.Format){case D3DFMT_A8R8G8B8:format=Format::BGRA8;break;case D3DFMT_X8R8G8B8:format=Format::BGRX8;break;case D3DFMT_R5G6B5:format=Format::RGB565;break;
        case D3DFMT_DXT1:format=Format::BC1;break;case D3DFMT_DXT3:format=Format::BC2;break;case D3DFMT_DXT5:format=Format::BC3;break;
        // 0.3.171: shadowblob.blp has 1-bit alpha; the game uploads it as A1R5G5B5. Not
        // A4R4G4B4: 4-bit colour steps can bring a non-blob grey within the colour threshold.
        case D3DFMT_A1R5G5B5:format=Format::ARGB1555;break;case D3DFMT_X1R5G5B5:format=Format::XRGB1555;break;
        default:if(unsupported++<4)logf("SHADOWBLOB candidate %ux%u format=%u unsupported by decoder",desc.Width,desc.Height,unsigned(desc.Format));return false;}
        if(desc.Usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL|D3DUSAGE_DYNAMIC))return false;
        const UINT rows=NorthlightActorTexture::rowCount(desc.Height,format);
        const UINT rowBytes=UINT(NorthlightActorTexture::rowBytes(desc.Width,format));
        raw.assign(std::size_t(rows)*rowBytes,0);
        D3DLOCKED_RECT locked={};if(FAILED(texture->LockRect(0,&locked,nullptr,D3DLOCK_READONLY)))return false;
        const bool readable=locked.pBits&&locked.Pitch>=INT(rowBytes);
        if(readable)for(UINT row=0;row<rows;++row)std::memcpy(raw.data()+std::size_t(row)*rowBytes,static_cast<const std::uint8_t*>(locked.pBits)+std::size_t(row)*unsigned(locked.Pitch),rowBytes);
        if(FAILED(texture->UnlockRect(0))||!readable)return false;
        if(!NorthlightActorTexture::decode(raw.data(),raw.size(),desc.Width,desc.Height,rowBytes,format,decoded))return false;
        if(decoded.size()!=reference.size())return false;
        return NorthlightShadowBlobModel::matches(decoded.data(),reference.data(),reference.size());
    }
    Verdict evaluate(IDirect3DBaseTexture9* base){
        Verdict v;v.checkedFrame=frame;
        if(!base||base->GetType()!=D3DRTYPE_TEXTURE)return v;
        auto* texture=static_cast<IDirect3DTexture9*>(base);
        D3DSURFACE_DESC desc={};if(FAILED(texture->GetLevelDesc(0,&desc)))return v;
        v.width=desc.Width;v.height=desc.Height;v.format=desc.Format;
        if(desc.Width!=NorthlightShadowBlobModel::Width||desc.Height!=NorthlightShadowBlobModel::Height)return v;
        v.blob=contentMatches(texture,desc);
        if(v.blob&&identified++<8)logf("SHADOWBLOB identified texture=%p %ux%u format=%u; native blob shadow draws follow BlobShadowStrength=%u while the extension draws actor shadows (0 skipped, 1..99 lighter texture under modulate blend, 100 unchanged)",static_cast<void*>(base),desc.Width,desc.Height,unsigned(desc.Format),strength);
        return v;
    }
    // 0.3.193: one lazily created A8R8G8B8 MANAGED texture (survives Reset) with the faint disc's mip chain.
    // Null on failure: logged for the first few failures, retried every 600 frames (a transient CreateTexture/LockRect failure
    // must not last the session); claim() hides the blob meanwhile.
    IDirect3DTexture9* ensureFaint(){
        if(faint)return faint;
        if(faintFailures&&frame-faintFailedFrame<600)return nullptr;
        const auto levels=NorthlightShadowBlobModel::faintMipChain(strength);
        IDirect3DTexture9* texture=nullptr;
        HRESULT hr=d->CreateTexture(NorthlightShadowBlobModel::Width,NorthlightShadowBlobModel::Height,UINT(levels.size()),0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr);
        for(UINT level=0;SUCCEEDED(hr)&&texture&&level<levels.size();++level){
            const UINT size=NorthlightShadowBlobModel::Width>>level;D3DLOCKED_RECT locked={};
            hr=texture->LockRect(level,&locked,nullptr,0);if(FAILED(hr))break;
            if(!locked.pBits||locked.Pitch<INT(size*4)){texture->UnlockRect(level);hr=E_FAIL;break;}
            for(UINT row=0;row<size;++row)std::memcpy(static_cast<std::uint8_t*>(locked.pBits)+std::size_t(row)*unsigned(locked.Pitch),levels[level].data()+std::size_t(row)*size*4,size*4);
            texture->UnlockRect(level);
        }
        if(FAILED(hr)||!texture){release(texture);faintFailedFrame=frame;if(faintFailures++<3)logf("SHADOWBLOB faint texture unavailable hr=%08lx; native blob shadows are hidden, retrying in 600 frames",static_cast<unsigned long>(hr));return nullptr;}
        faint=texture;return faint;
    }
    // The first identified draws log the states the faint swap depends on (confirms the modulate blend).
    void logStates(UINT primitiveCount,bool modulate,const char* mode){
        DWORD v[8]={};D3DRENDERSTATETYPE rs[7]={D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAREF,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHAFUNC};
        for(int i=0;i<7;++i)d->GetRenderState(rs[i],&v[i]);
        DWORD c0=0,a1=0,a2=0,ao=0,c1=0,fvf=0;
        d->GetTextureStageState(0,D3DTSS_COLOROP,&c0);d->GetTextureStageState(0,D3DTSS_COLORARG1,&a1);d->GetTextureStageState(0,D3DTSS_COLORARG2,&a2);d->GetTextureStageState(0,D3DTSS_ALPHAOP,&ao);d->GetTextureStageState(1,D3DTSS_COLOROP,&c1);d->GetFVF(&fvf);
        logf("SHADOWBLOB draw states srcBlend=%u destBlend=%u blendOp=%u alphaBlend=%u alphaTest=%u alphaRef=%u alphaFunc=%u stage0 colorOp=%u arg1=%u arg2=%u alphaOp=%u stage1 colorOp=%u fvf=0x%x primitives=%u strength=%u modulate=%d mode=%s",
            unsigned(v[0]),unsigned(v[1]),unsigned(v[2]),unsigned(v[5]),unsigned(v[3]),unsigned(v[4]),unsigned(v[6]),unsigned(c0),unsigned(a1),unsigned(a2),unsigned(ao),unsigned(c1),unsigned(fvf),unsigned(primitiveCount),strength,modulate?1:0,mode);
    }
public:
    enum class Claim {None,Skip,Faint};   // draw normally / skip the game's draw (strength 0, or 1..99 without a liftable blend) / draw with faintTexture()
    // 0.3.193: Faint hands back the AddRef'd texture bound to stage 0 (the caller restores and releases it); Skip/None hold nothing.
    struct Result {Claim claim=Claim::None;IDirect3DBaseTexture9* original=nullptr;};
    NorthlightShadowBlobFilter(IDirect3DDevice9* device,unsigned blobShadowStrength):d(device),strength(blobShadowStrength){}
    ~NorthlightShadowBlobFilter(){release(faint);}
    NorthlightShadowBlobFilter(const NorthlightShadowBlobFilter&)=delete;
    NorthlightShadowBlobFilter& operator=(const NorthlightShadowBlobFilter&)=delete;
    void reset(){verdicts.clear();}
    void setTexturePeek(bool(*fn)(void*,DWORD,IDirect3DBaseTexture9*&),void* context){peekTexture=fn;peekContext=context;}
    void endFrame(){++frame;if(frame%600==0&&NorthlightDiagnostics::enabled()&&(skippedTotal||faintTotal||reported++<3))logf("SHADOWBLOB skippedThisFrame=%u skippedTotal=%u faintThisFrame=%u faintTotal=%u verdicts=%zu",skippedThisFrame,skippedTotal,faintThisFrame,faintTotal,verdicts.size());skippedThisFrame=0;faintThisFrame=0;
        if(verdicts.size()>4096)verdicts.clear();}
    IDirect3DTexture9* faintTexture()const{return faint;}
    bool active()const{return strength<100;}   // 100 = the filter does no work and the game's blobs are drawn as they are
    // Skip: the draw about to be issued uses the blob shadow texture with alpha blending and the
    // strength is 0, or 1..99 and the faint swap is not possible (the blend is not a modulate or the faint
    // texture is unavailable: the 0.3.192 behaviour, the blob is hidden); the caller skips the native draw.
    // Faint (strength 1..99): same draw, with a modulate blend (ALPHABLENDENABLE, BLENDOP ADD,
    // DESTCOLOR*ZERO or ZERO*SRCCOLOR); the caller binds faintTexture() to stage 0 around the draw and
    // releases Result::original. Not a blob draw, or strength 100: None.
    Result claim(UINT primitiveCount){
        if(strength>=100||!primitiveCount||primitiveCount>256)return {};
        IDirect3DBaseTexture9* bound=nullptr;
        // 0.3.196 (task 12): a borrowed identity (no device write before the verdict); a Faint verdict AddRefs it for the caller below.
        const bool borrowed=peekTexture&&peekTexture(peekContext,0,bound);
        if(!borrowed&&(FAILED(d->GetTexture(0,&bound))||!bound))return {};
        struct Release {IDirect3DBaseTexture9* p;~Release(){release(p);}} guard{borrowed?nullptr:bound};   // Faint takes the reference over
        auto it=verdicts.find(bound);
        if(it==verdicts.end()){it=verdicts.emplace(bound,evaluate(bound)).first;}
        else{
            Verdict& v=it->second;
            if(v.blob||frame-v.checkedFrame>=600){
                D3DSURFACE_DESC desc={};bool same=bound->GetType()==D3DRTYPE_TEXTURE&&SUCCEEDED(static_cast<IDirect3DTexture9*>(bound)->GetLevelDesc(0,&desc))&&desc.Width==v.width&&desc.Height==v.height&&desc.Format==v.format;
                if(!same||frame-v.checkedFrame>=600)v=evaluate(bound);
            }
        }
        if(!it->second.blob)return {};
        DWORD blend=0;if(FAILED(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))||!blend)return {};
        if(!strength){if(statesLogged<2){++statesLogged;logStates(primitiveCount,false,"skip");}++skippedThisFrame;++skippedTotal;return {Claim::Skip};}
        DWORD op=0,src=0,dst=0;
        const bool modulate=SUCCEEDED(d->GetRenderState(D3DRS_BLENDOP,&op))&&SUCCEEDED(d->GetRenderState(D3DRS_SRCBLEND,&src))&&SUCCEEDED(d->GetRenderState(D3DRS_DESTBLEND,&dst))
            &&op==D3DBLENDOP_ADD&&((src==D3DBLEND_DESTCOLOR&&dst==D3DBLEND_ZERO)||(src==D3DBLEND_ZERO&&dst==D3DBLEND_SRCCOLOR));
        const bool accepted=modulate&&ensureFaint()!=nullptr;
        if(statesLogged<2){++statesLogged;logStates(primitiveCount,modulate,accepted?"faint":"skip");}
        if(!accepted){++skippedThisFrame;++skippedTotal;return {Claim::Skip};}   // 0.3.193: cannot lighten it, hide it as 0.3.192 did
        ++faintThisFrame;++faintTotal;if(borrowed)bound->AddRef();guard.p=nullptr;return {Claim::Faint,bound};
    }
};
