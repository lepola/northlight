#pragma once
#include "water_shader_patch.h"

// Private comparison for the two measured native programs. Only these programs
// establish the meanings of COLOR0, TEXCOORD0, FOG and c2 in the replacement.
class SpatialAlphaFog {
    static constexpr DWORD slots[3]={9,11,15};
    static constexpr D3DSAMPLERSTATETYPE states[4]={
        D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE};
    static constexpr DWORD values[4]={D3DTEXF_POINT,D3DTEXF_POINT,D3DTEXF_NONE,FALSE};
    IDirect3DVertexShader9* vertex=nullptr;
    IDirect3DPixelShader9* pixel=nullptr;
    bool attempted=false,active=false;
    IDirect3DBaseTexture9* savedTextures[3]={};
    DWORD savedSampler[3][4]={};
    float savedConstants[4][4]={};
    const NorthlightSpellLayer::SpatialFogMaterial* last=nullptr;
    IDirect3DDevice9* device=nullptr;
    const WorldRenderer::AlphaFogFrame* frame=nullptr;
    void releaseSaved(){
        for(auto& texture:savedTextures)if(texture){texture->Release();texture=nullptr;}
    }
    // Keep the game's registration copy until the option is first enabled.
    // This also covers a live settings change after the native shader loaded.
    std::vector<DWORD> nativeWords;
    bool save(){
        if(FAILED(device->GetPixelShaderConstantF(216,savedConstants[0],4)))return false;
        for(unsigned i=0;i<3;++i){
            if(FAILED(device->GetTexture(slots[i],&savedTextures[i])))return false;
            for(unsigned j=0;j<4;++j)
                if(FAILED(device->GetSamplerState(slots[i],states[j],&savedSampler[i][j])))return false;
        }
        return true;
    }
public:
    static constexpr uint64_t vertexHash=0x2e3dc5694f4a01afull,pixelHash=0xac008ac06cd451a9ull;
    void recordVertex(std::vector<DWORD>& words){
        if(!attempted && nativeWords.empty())nativeWords.swap(words);
    }
    void enablePrograms(IDirect3DDevice9* d,unsigned frameNumber){
        if(attempted || nativeWords.empty())return;
        attempted=true;
        LARGE_INTEGER begin{},end{},hz{};
        QueryPerformanceFrequency(&hz);QueryPerformanceCounter(&begin);
        std::vector<DWORD> source;
        source.swap(nativeWords);
        std::vector<uint32_t> patched;
        const char* why=NorthlightWaterShaderPatch::patch(
            reinterpret_cast<const uint32_t*>(source.data()),source.size(),patched);
        if(why){logf("SPATIALALPHA vertex rejected: %s",why);return;}
        HRESULT vs=d->CreateVertexShader(reinterpret_cast<const DWORD*>(patched.data()),&vertex);
        HRESULT ps=d->CreatePixelShader(kSpatialAlphaFogShader,&pixel);
        QueryPerformanceCounter(&end);
        logf("SPATIALALPHA programs vs=%08lx ps=%08lx frame=%u cpuMs=%.3f",vs,ps,frameNumber,
             hz.QuadPart?1000.*(end.QuadPart-begin.QuadPart)/hz.QuadPart:0.);
    }
    bool ready()const{return vertex && pixel;}
    unsigned draws=0;
    ~SpatialAlphaFog(){if(vertex)vertex->Release();if(pixel)pixel->Release();}
    void prepare(IDirect3DDevice9* d,const WorldRenderer::AlphaFogFrame& inputs){device=d;frame=&inputs;draws=0;}
    void restore(){
        if(!active)return;
        device->SetVertexShader(last->vertex);device->SetPixelShader(last->pixel);
        device->SetPixelShaderConstantF(216,savedConstants[0],4);
        for(unsigned i=0;i<3;++i){
            device->SetTexture(slots[i],savedTextures[i]);
            for(unsigned j=0;j<4;++j)device->SetSamplerState(slots[i],states[j],savedSampler[i][j]);
        }
        releaseSaved();active=false;last=nullptr;
    }
    void bind(const NorthlightSpellLayer::SpatialFogMaterial& material){
        if(active && last==&material){++draws;return;}
        last=&material;
        if(material.width!=frame->width || material.height!=frame->height){
            restore();return;
        }
        bool ok=true;
        if(!active){
            if(!save()){releaseSaved();return;}
            active=true;
            IDirect3DTexture9* textures[]={frame->fog,frame->liquid,frame->distance};
            for(unsigned i=0;i<3;++i){
                ok=SUCCEEDED(device->SetTexture(slots[i],textures[i]))&&ok;
                for(unsigned j=0;j<4;++j)ok=SUCCEEDED(device->SetSamplerState(slots[i],states[j],values[j]))&&ok;
            }
            ok=SUCCEEDED(device->SetPixelShaderConstantF(218,frame->raster,1))&&ok;
            ok=SUCCEEDED(device->SetPixelShaderConstantF(219,frame->grid,1))&&ok;
        }
        ok=SUCCEEDED(device->SetVertexShader(vertex))&&ok;
        ok=SUCCEEDED(device->SetPixelShader(pixel))&&ok;
        ok=SUCCEEDED(device->SetPixelShaderConstantF(216,material.ramp,1))&&ok;
        ok=SUCCEEDED(device->SetPixelShaderConstantF(217,material.colour,1))&&ok;
        if(ok)++draws;else restore();
    }
};
