#pragma once
#include <atomic>
#include <cstring>

// Valid only during one uninterrupted model-replay loop on the real device.
// Never shadows game state or survives a StateBlock/other rendering pass.
// DrawPrimitive/DrawIndexedPrimitive do not mutate these bindings.
namespace NorthlightReplayDrawState {
// A negative cutoff bypasses alpha testing in both replay shaders. The first
// material still binds; later opaque draws may retain its texture/addressing.
// Native shader cutouts have a nonnegative cutoff and never take this shortcut.
constexpr bool SkipOpaqueMaterial=true;
// Guard for the proof: the only formats whose alpha can fall below -1. Set by the proxy's
// Create*Texture wrappers when the game creates one; then every draw binds again.
inline std::atomic<bool> floatAlphaTextures{false};
inline bool floatAlphaFormat(unsigned format){return format==113||format==116;} /* D3DFMT_A16B16G16R16F, D3DFMT_A32B32G32R32F */
inline void noteTextureFormat(unsigned format){if(floatAlphaFormat(format))floatAlphaTextures.store(true,std::memory_order_relaxed);}
inline bool skipOpaque(){return SkipOpaqueMaterial&&!floatAlphaTextures.load(std::memory_order_relaxed);}
template<bool Skip> class BasicCache {
    IDirect3DDevice9* d_;
    bool geometry_=false,material_=false,skip_;
    IDirect3DVertexDeclaration9* declaration_=nullptr;
    IDirect3DVertexShader9* shader_=nullptr;
    IDirect3DIndexBuffer9* index_=nullptr;
    IDirect3DVertexBuffer9* streams_[4]={};UINT offsets_[4]={},strides_[4]={};
    IDirect3DBaseTexture9* texture_=nullptr;DWORD u_=0,v_=0;float cutoff_=0;unsigned alphaMode_=0;
    bool legacy_=false;IDirect3DBaseTexture9* legacyTexture_=nullptr;DWORD legacyU_=0,legacyV_=0; /* diagnostic: what the binding-every-draw path would hold */
public:
    /* Diagnostic only: successful setter calls issued by this pass; legacy*: the
       texture/address calls the 0.3.142 path would have issued for the same draws. */
    struct Counts {unsigned declarations=0,streams=0,indices=0,shaders=0,textures=0,samplers=0,cutoffs=0,legacyTextures=0,legacySamplers=0,opaqueKept=0;} counts;
    explicit BasicCache(IDirect3DDevice9* device):d_(device),skip_(Skip&&skipOpaque()){}
    bool skipping()const{return skip_;}
    template<class Replay> HRESULT geometry(const Replay& p){
        HRESULT hr;
        if(!geometry_||declaration_!=p.decl){hr=d_->SetVertexDeclaration(p.decl);if(FAILED(hr)){geometry_=false;return hr;}declaration_=p.decl;++counts.declarations;}
        for(unsigned s=0;s<4;++s)if(!geometry_||streams_[s]!=p.stream[s]||offsets_[s]!=p.offset[s]||strides_[s]!=p.stride[s]){
            hr=d_->SetStreamSource(s,p.stream[s],p.offset[s],p.stride[s]);if(FAILED(hr)){geometry_=false;return hr;}
            streams_[s]=p.stream[s];offsets_[s]=p.offset[s];strides_[s]=p.stride[s];++counts.streams;
        }
        if(!geometry_||index_!=p.index){hr=d_->SetIndices(p.index);if(FAILED(hr)){geometry_=false;return hr;}index_=p.index;++counts.indices;}
        if(!geometry_||shader_!=p.shader){hr=d_->SetVertexShader(p.shader);if(FAILED(hr)){geometry_=false;return hr;}shader_=p.shader;++counts.shaders;}
        geometry_=true;return D3D_OK;
    }
    template<class Replay> HRESULT material(const Replay& p){
        HRESULT hr;
        if(!legacy_||legacyTexture_!=p.texture)++counts.legacyTextures;
        counts.legacySamplers+=unsigned(!legacy_||legacyU_!=p.addressU)+unsigned(!legacy_||legacyV_!=p.addressV);
        legacy_=true;legacyTexture_=p.texture;legacyU_=p.addressU;legacyV_=p.addressV;
        if(skip_&&material_&&p.cutoff<=-1.f){counts.opaqueKept+=texture_!=p.texture||u_!=p.addressU||v_!=p.addressV;}
        else {
            if(!material_||texture_!=p.texture){hr=d_->SetTexture(0,p.texture);if(FAILED(hr)){material_=false;return hr;}texture_=p.texture;++counts.textures;}
            if(!material_||u_!=p.addressU){hr=d_->SetSamplerState(0,D3DSAMP_ADDRESSU,p.addressU);if(FAILED(hr)){material_=false;return hr;}u_=p.addressU;++counts.samplers;}
            if(!material_||v_!=p.addressV){hr=d_->SetSamplerState(0,D3DSAMP_ADDRESSV,p.addressV);if(FAILED(hr)){material_=false;return hr;}v_=p.addressV;++counts.samplers;}
        }
        if(!material_||std::memcmp(&cutoff_,&p.cutoff,sizeof(float))||alphaMode_!=p.alphaMode){float c[]={float(p.alphaMode),0,0,p.cutoff};hr=d_->SetPixelShaderConstantF(0,c,1);if(FAILED(hr)){material_=false;return hr;}cutoff_=p.cutoff;alphaMode_=p.alphaMode;++counts.cutoffs;}
        material_=true;return D3D_OK;
    }
};
using Cache=BasicCache<SkipOpaqueMaterial>;
}
