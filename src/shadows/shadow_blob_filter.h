#pragma once
// Identifies the game's unit "blob" shadow decal by texture content and lets the
// device wrapper skip those draws while the extension's own shadow maps are on.
// Include after logf(const char*, ...). Never wraps or retains game textures:
// a verdict is cached per texture pointer, re-validated by level description on
// every claim and by content every 600 frames to survive pointer reuse.
#include "diagnostics_switch.h"
#include <d3d9.h>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#include <vector>
#include "actor_texture.h"
#include "shadow_blob_model.h"

class NorthlightShadowBlobFilter {
    struct Verdict {bool blob=false;UINT width=0,height=0;D3DFORMAT format=D3DFMT_UNKNOWN;unsigned checkedFrame=0;};
    IDirect3DDevice9* d;
    std::unordered_map<IDirect3DBaseTexture9*,Verdict> verdicts;
    std::vector<std::uint8_t> raw,decoded;
    const std::vector<std::uint8_t> reference=NorthlightShadowBlobModel::referencePixels();   // ~10 us once, at construction
    unsigned frame=0,skippedThisFrame=0,skippedTotal=0,identified=0,unsupported=0,reported=0;
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
        if(v.blob&&identified++<8)logf("SHADOWBLOB identified texture=%p %ux%u format=%u; native blob shadow draws are skipped while the extension is enabled",static_cast<void*>(base),desc.Width,desc.Height,unsigned(desc.Format));
        return v;
    }
public:
    // 0.3.192: false = the game's blob shadows are drawn again (the filter is kept, not called).
    // true = hide them while the mod draws actor shadows (0.3.154..0.3.191 behaviour).
    static constexpr bool HidesNativeBlobs=false;
    explicit NorthlightShadowBlobFilter(IDirect3DDevice9* device):d(device){}
    NorthlightShadowBlobFilter(const NorthlightShadowBlobFilter&)=delete;
    NorthlightShadowBlobFilter& operator=(const NorthlightShadowBlobFilter&)=delete;
    void reset(){verdicts.clear();}
    void endFrame(){++frame;if(frame%600==0&&NorthlightDiagnostics::enabled()&&(skippedTotal||reported++<3))logf("SHADOWBLOB skippedThisFrame=%u skippedTotal=%u verdicts=%zu",skippedThisFrame,skippedTotal,verdicts.size());skippedThisFrame=0;
        if(verdicts.size()>4096)verdicts.clear();}
    // True when the draw about to be issued uses the blob shadow texture with
    // alpha blending. The caller skips the native draw; nothing else changes.
    bool claim(UINT primitiveCount){
        if(!primitiveCount||primitiveCount>256)return false;
        IDirect3DBaseTexture9* bound=nullptr;
        if(FAILED(d->GetTexture(0,&bound))||!bound)return false;
        struct Release {IDirect3DBaseTexture9* p;~Release(){release(p);}} guard{bound};
        auto it=verdicts.find(bound);
        if(it==verdicts.end()){it=verdicts.emplace(bound,evaluate(bound)).first;}
        else{
            Verdict& v=it->second;
            if(v.blob||frame-v.checkedFrame>=600){
                D3DSURFACE_DESC desc={};bool same=bound->GetType()==D3DRTYPE_TEXTURE&&SUCCEEDED(static_cast<IDirect3DTexture9*>(bound)->GetLevelDesc(0,&desc))&&desc.Width==v.width&&desc.Height==v.height&&desc.Format==v.format;
                if(!same||frame-v.checkedFrame>=600)v=evaluate(bound);
            }
        }
        if(!it->second.blob)return false;
        DWORD blend=0;if(FAILED(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))||!blend)return false;
        ++skippedThisFrame;++skippedTotal;return true;
    }
};
