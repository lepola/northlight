#pragma once
#include "auto_parallax_patch.h"

class NorthlightAutoParallax {
    IDirect3DDevice9* d;NorthlightAutoNormalCache& normals;
    struct Pixel {IDirect3DPixelShader9* source=nullptr,*shader=nullptr;unsigned constants=0;};
    struct Vertex {IDirect3DVertexShader9* source=nullptr,*shader=nullptr;};
    std::unordered_map<IDirect3DPixelShader9*,Pixel> pixels;
    std::unordered_map<IDirect3DVertexShader9*,Vertex> vertices;
    unsigned draws=0,skipped=0,frames=0;
    static constexpr D3DSAMPLERSTATETYPE keys[]={D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_SRGBTEXTURE,D3DSAMP_MAXMIPLEVEL,D3DSAMP_MIPMAPLODBIAS};
    struct State {
        IDirect3DDevice9* d;IDirect3DPixelShader9* pixel=nullptr;IDirect3DVertexShader9* vertex=nullptr;
        IDirect3DBaseTexture9* textures[2]={};DWORD samplers[2][8]={};float constants[8]={};unsigned layers=0,constantBase=0,samplerChanged[2]={};bool changed=false,borrowedVertex=false,borrowedPixel=false;
        explicit State(IDirect3DDevice9* device):d(device){}
        bool saveShaders(IDirect3DVertexShader9* boundVertex,IDirect3DPixelShader9* boundPixel){
            // The caller retains these authoritative bindings until this draw returns.
            vertex=boundVertex;pixel=boundPixel;borrowedVertex=vertex!=nullptr;borrowedPixel=pixel!=nullptr;
            if((!borrowedVertex&&FAILED(d->GetVertexShader(&vertex)))||!vertex||(!borrowedPixel&&FAILED(d->GetPixelShader(&pixel)))||!pixel)return false;return true;
        }
        bool saveSamplers(unsigned count){
            for(unsigned n=0;n<count;++n){layers=n+1;if(FAILED(d->GetTexture(14+n,&textures[n])))return false;for(unsigned i=0;i<8;++i)if(FAILED(d->GetSamplerState(14+n,keys[i],&samplers[n][i])))return false;}return true;
        }
        void restore(){if(changed){d->SetVertexShader(vertex);d->SetPixelShader(pixel);d->SetPixelShaderConstantF(constantBase,constants,2);
            for(unsigned n=0;n<layers;++n){d->SetTexture(14+n,textures[n]);for(unsigned i=0;i<8;++i)if(samplerChanged[n]&(1u<<i))d->SetSamplerState(14+n,keys[i],samplers[n][i]);}}
            changed=false;}
        ~State(){restore();
            if(!borrowedVertex)drop(vertex);if(!borrowedPixel)drop(pixel);for(auto*& t:textures)drop(t);}
    };
public:
    NorthlightAutoParallax(IDirect3DDevice9* device,NorthlightAutoNormalCache& cache):d(device),normals(cache){}
    ~NorthlightAutoParallax(){for(auto& p:pixels){drop(p.second.shader);drop(p.second.source);}for(auto& v:vertices){drop(v.second.shader);drop(v.second.source);}}
    bool supports(IDirect3DPixelShader9* pixel,unsigned material){
        auto it=pixels.find(pixel);if(it!=pixels.end())return it->second.shader!=nullptr;
        if(pixels.size()>=512)return false;UINT size=0;std::vector<DWORD> code;std::vector<std::uint32_t> patched;
        if(FAILED(pixel->GetFunction(nullptr,&size))||size>65536||size%4)return false;code.resize(size/4);
        Pixel entry;entry.source=pixel;entry.source->AddRef();
        HRESULT hr=E_FAIL;bool patchedOK=false;
        if(SUCCEEDED(pixel->GetFunction(code.data(),&size))&&(patchedOK=NorthlightParallaxPatch::pixel(reinterpret_cast<std::uint32_t*>(code.data()),code.size(),material==6,patched,&entry.constants)))hr=d->CreatePixelShader(reinterpret_cast<DWORD*>(patched.data()),&entry.shader);
        pixels.emplace(pixel,entry);if(!entry.shader)logf("AUTOPARALLAX native shader retained hash=%016llx patch=%d hr=%08lx",(unsigned long long)NorthlightWaterShaderPatch::fnv1a64(reinterpret_cast<std::uint32_t*>(code.data()),code.size()),int(patchedOK),(unsigned long)hr);
        return entry.shader!=nullptr;
    }
    void endFrame(){if(draws&&frames++%600==0)logf("AUTOPARALLAX draws=%u skipped=%u pixel_variants=%zu vertex_variants=%zu",draws,skipped,pixels.size(),vertices.size());draws=skipped=0;}
    template<class Draw> HRESULT draw(IDirect3DVertexShader9* knownVertex,IDirect3DVertexShader9* forwarded,unsigned material,const float* projection,const float* control,Draw nativeDraw,bool model=false,IDirect3DVertexShader9* boundVertex=nullptr,IDirect3DPixelShader9* boundPixel=nullptr){
        const bool dual=material==6;State saved(d);if(!saved.saveShaders(boundVertex,boundPixel))return nativeDraw();
        DWORD z=0,zwrite=0,zfunc=0,stencil=0;
        if(FAILED(d->GetRenderState(D3DRS_ZENABLE,&z))||FAILED(d->GetRenderState(D3DRS_ZWRITEENABLE,&zwrite))||FAILED(d->GetRenderState(D3DRS_ZFUNC,&zfunc))||FAILED(d->GetRenderState(D3DRS_STENCILENABLE,&stencil))||!z||zfunc==D3DCMP_ALWAYS||stencil||(!zwrite&&material>2))return nativeDraw();
        IDirect3DVertexShader9* vertex=forwarded;
        if(saved.vertex!=knownVertex){auto it=vertices.find(saved.vertex);if(it==vertices.end()){
            if(vertices.size()>=256)return nativeDraw();UINT size=0;std::vector<DWORD> code;std::vector<std::uint32_t> patched;
            if(FAILED(saved.vertex->GetFunction(nullptr,&size))||size>65536)return nativeDraw();code.resize(size/4);
            if(FAILED(saved.vertex->GetFunction(code.data(),&size))||NorthlightWaterShaderPatch::patch(reinterpret_cast<std::uint32_t*>(code.data()),code.size(),patched))return nativeDraw();
            Vertex entry;entry.source=saved.vertex;entry.source->AddRef();d->CreateVertexShader(reinterpret_cast<DWORD*>(patched.data()),&entry.shader);it=vertices.emplace(saved.vertex,entry).first;
        }vertex=it->second.shader;}
        if(!vertex)return nativeDraw();
        if(!supports(saved.pixel,material)){++skipped;return nativeDraw();}
        auto it=pixels.find(saved.pixel);
        saved.constantBase=it->second.constants;if(FAILED(d->GetPixelShaderConstantF(saved.constantBase,saved.constants,2)))return nativeDraw();
        NorthlightAutoNormalCache::Lease maps[2];
        for(unsigned n=0;n<(dual?2u:1u);++n){IDirect3DBaseTexture9* source=nullptr;if(FAILED(d->GetTexture(n,&source)))return nativeDraw();maps[n]=normals.acquire(source,model);drop(source);if(!maps[n])return nativeDraw();}
        if(!saved.saveSamplers(dual?2:1))return nativeDraw();
        saved.changed=true;
        for(unsigned n=0;n<(dual?2u:1u);++n){DWORD u=D3DTADDRESS_WRAP,v=D3DTADDRESS_WRAP;d->GetSamplerState(n,D3DSAMP_ADDRESSU,&u);d->GetSamplerState(n,D3DSAMP_ADDRESSV,&v);
            const DWORD values[]={D3DTEXF_LINEAR,D3DTEXF_LINEAR,D3DTEXF_LINEAR,u,v,FALSE,0,0};d->SetTexture(14+n,maps[n].get());for(unsigned i=0;i<8;++i)if(saved.samplers[n][i]!=values[i]){saved.samplerChanged[n]|=1u<<i;d->SetSamplerState(14+n,keys[i],values[i]);}}
        float bank[8];std::copy(projection,projection+4,bank);std::copy(control,control+4,bank+4);
        if(FAILED(d->SetVertexShader(vertex))||FAILED(d->SetPixelShader(it->second.shader))||FAILED(d->SetPixelShaderConstantF(saved.constantBase,bank,2))){saved.restore();return nativeDraw();}
        ++draws;return nativeDraw();
    }
};
