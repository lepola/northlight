#pragma once
#include "auto_parallax_patch.h"
#include "shared_pom.h"

class NorthlightAutoParallax {
    IDirect3DDevice9* d;NorthlightAutoNormalCache& normals;
    NorthlightMaterialProfile* profile=nullptr;
    struct Pixel {IDirect3DPixelShader9* source=nullptr,*shader=nullptr,*shared=nullptr;unsigned constants=0,sharedConstants=0;bool sharedChecked=false;};
    struct Vertex {IDirect3DVertexShader9* source=nullptr,*shader=nullptr,*shared=nullptr;bool sharedChecked=false;};
    std::unordered_map<IDirect3DPixelShader9*,Pixel> pixels;
    std::unordered_map<IDirect3DVertexShader9*,Vertex> vertices;
    unsigned draws=0,skipped=0,frames=0;bool sharedActive=false;
    static constexpr D3DSAMPLERSTATETYPE keys[]={D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_SRGBTEXTURE,D3DSAMP_MAXMIPLEVEL,D3DSAMP_MIPMAPLODBIAS};
    struct State {
        IDirect3DDevice9* d;IDirect3DPixelShader9* pixel=nullptr;IDirect3DVertexShader9* vertex=nullptr;
        IDirect3DBaseTexture9* textures[4]={};DWORD samplers[4][8]={};float constants[12]={};unsigned stages[4]={},layers=0,constantBase=0,constantCount=2,samplerChanged[4]={};bool changed=false,borrowedVertex=false,borrowedPixel=false;
        explicit State(IDirect3DDevice9* device):d(device){}
        bool saveShaders(IDirect3DVertexShader9* boundVertex,IDirect3DPixelShader9* boundPixel){
            // The caller retains these authoritative bindings until this draw returns.
            vertex=boundVertex;pixel=boundPixel;borrowedVertex=vertex!=nullptr;borrowedPixel=pixel!=nullptr;
            if((!borrowedVertex&&FAILED(d->GetVertexShader(&vertex)))||!vertex||(!borrowedPixel&&FAILED(d->GetPixelShader(&pixel)))||!pixel)return false;return true;
        }
        bool saveSamplers(unsigned count,bool shared=false){
            for(unsigned n=0;n<count*(shared?2u:1u);++n){layers=n+1;stages[n]=n<count?14+n:12+n-count;if(FAILED(d->GetTexture(stages[n],&textures[n])))return false;for(unsigned i=0;i<8;++i)if(FAILED(d->GetSamplerState(stages[n],keys[i],&samplers[n][i])))return false;}return true;
        }
        void restore(){if(changed){d->SetVertexShader(vertex);d->SetPixelShader(pixel);d->SetPixelShaderConstantF(constantBase,constants,constantCount);
            for(unsigned n=0;n<layers;++n){d->SetTexture(stages[n],textures[n]);for(unsigned i=0;i<8;++i)if(samplerChanged[n]&(1u<<i))d->SetSamplerState(stages[n],keys[i],samplers[n][i]);}}
            changed=false;}
        ~State(){restore();
            if(!borrowedVertex)drop(vertex);if(!borrowedPixel)drop(pixel);for(auto*& t:textures)drop(t);}
    };
public:
    NorthlightAutoParallax(IDirect3DDevice9* device,NorthlightAutoNormalCache& cache):d(device),normals(cache){}
    void setProfiler(NorthlightMaterialProfile* p){profile=p;}
    ~NorthlightAutoParallax(){for(auto& p:pixels){drop(p.second.shader);drop(p.second.shared);drop(p.second.source);}for(auto& v:vertices){drop(v.second.shader);drop(v.second.shared);drop(v.second.source);}}
    bool shared()const{return sharedActive;}
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
    bool supportsShared(IDirect3DPixelShader9* pixel,unsigned material){
        try {
        if(!supports(pixel,material))return false;auto& entry=pixels.find(pixel)->second;if(entry.sharedChecked)return entry.shared!=nullptr;entry.sharedChecked=true;
        UINT bytes=0;
        if(FAILED(pixel->GetFunction(nullptr,&bytes))||bytes>65536||bytes%4)return false;
        std::vector<DWORD> code(bytes/4);std::vector<uint32_t> patched;
        if(SUCCEEDED(pixel->GetFunction(code.data(),&bytes))&&NorthlightParallaxPatch::pixel(reinterpret_cast<uint32_t*>(code.data()),code.size(),material==6,patched,&entry.sharedConstants,true))d->CreatePixelShader(reinterpret_cast<DWORD*>(patched.data()),&entry.shared);
        return entry.shared!=nullptr;
        }catch(...){return false;}
    }
    template<class Draw> HRESULT draw(IDirect3DVertexShader9* knownVertex,IDirect3DVertexShader9* forwarded,unsigned material,const float* projection,const float* control,Draw nativeDraw,bool model=false,IDirect3DVertexShader9* boundVertex=nullptr,IDirect3DPixelShader9* boundPixel=nullptr,NorthlightSharedPOM* sharedPOM=nullptr,IDirect3DVertexShader9* sharedVertex=nullptr,bool equivalentVertex=false){
        NorthlightMaterialProfile::Scope timing(profile,NorthlightMaterialProfile::PomSetup);
        const bool dual=material==6;State saved(d);if(!saved.saveShaders(boundVertex,boundPixel))return nativeDraw();
        DWORD z=0,zwrite=0,zfunc=0,stencil=0;
        if(FAILED(d->GetRenderState(D3DRS_ZENABLE,&z))||FAILED(d->GetRenderState(D3DRS_ZWRITEENABLE,&zwrite))||FAILED(d->GetRenderState(D3DRS_ZFUNC,&zfunc))||FAILED(d->GetRenderState(D3DRS_STENCILENABLE,&stencil))||!z||zfunc==D3DCMP_ALWAYS||zfunc==D3DCMP_NEVER||stencil||(!zwrite&&material>2))return nativeDraw();
        IDirect3DVertexShader9* vertex=forwarded;
        if(saved.vertex!=knownVertex){auto it=vertices.find(saved.vertex);if(it==vertices.end()){
            if(vertices.size()>=256)return nativeDraw();UINT size=0;std::vector<DWORD> code;std::vector<std::uint32_t> patched;
            if(FAILED(saved.vertex->GetFunction(nullptr,&size))||size>65536)return nativeDraw();code.resize(size/4);
            if(FAILED(saved.vertex->GetFunction(code.data(),&size))||NorthlightWaterShaderPatch::patch(reinterpret_cast<std::uint32_t*>(code.data()),code.size(),patched))return nativeDraw();
            Vertex entry;entry.source=saved.vertex;entry.source->AddRef();d->CreateVertexShader(reinterpret_cast<DWORD*>(patched.data()),&entry.shader);
            it=vertices.emplace(saved.vertex,entry).first;
        }vertex=it->second.shader;if(equivalentVertex&&sharedPOM&&sharedPOM->available()&&!it->second.sharedChecked){it->second.sharedChecked=true;it->second.shared=NorthlightSharedPOM::vertexShader(d,it->second.shader);}sharedVertex=equivalentVertex?it->second.shared:nullptr;}
        if(!vertex)return nativeDraw();
        if(!supports(saved.pixel,material)){++skipped;return nativeDraw();}
        auto it=pixels.find(saved.pixel);
        NorthlightAutoNormalCache::Lease maps[2];
        for(unsigned n=0;n<(dual?2u:1u);++n){IDirect3DBaseTexture9* source=nullptr;if(FAILED(d->GetTexture(n,&source)))return nativeDraw();maps[n]=normals.acquire(source,model);drop(source);if(!maps[n])return nativeDraw();}
        DWORD addressU[2]={D3DTADDRESS_WRAP,D3DTADDRESS_WRAP},addressV[2]={D3DTADDRESS_WRAP,D3DTADDRESS_WRAP};bool matching=sharedPOM&&sharedPOM->available()&&sharedVertex;
        for(unsigned n=0;n<(dual?2u:1u);++n){const bool u=SUCCEEDED(d->GetSamplerState(n,D3DSAMP_ADDRESSU,&addressU[n])),v=SUCCEEDED(d->GetSamplerState(n,D3DSAMP_ADDRESSV,&addressV[n]));matching=matching&&u&&v&&maps[n].get()==sharedPOM->maps[n]&&addressU[n]==sharedPOM->addressU[n]&&addressV[n]==sharedPOM->addressV[n];}
        const bool reuse=matching&&supportsShared(saved.pixel,material);
        saved.constantBase=reuse?it->second.sharedConstants:it->second.constants;saved.constantCount=reuse?3:2;
        if(FAILED(d->GetPixelShaderConstantF(saved.constantBase,saved.constants,saved.constantCount))||!saved.saveSamplers(dual?2:1,reuse))return nativeDraw();
        NorthlightSharedPOM::Binding binding(d);
        const auto fallback=[&]{saved.restore();binding.restore();return this->draw(knownVertex,forwarded,material,projection,control,nativeDraw,model,boundVertex,boundPixel);};
        if(reuse&&!binding.bind(*sharedPOM,true))return fallback();
        saved.changed=true;
        for(unsigned n=0;n<(dual?2u:1u);++n){const DWORD values[]={D3DTEXF_LINEAR,D3DTEXF_LINEAR,D3DTEXF_LINEAR,addressU[n],addressV[n],FALSE,0,0};if(FAILED(d->SetTexture(14+n,maps[n].get()))&&reuse)return fallback();for(unsigned i=0;i<8;++i)if(saved.samplers[n][i]!=values[i]){saved.samplerChanged[n]|=1u<<i;if(FAILED(d->SetSamplerState(14+n,keys[i],values[i]))&&reuse)return fallback();}}
        if(reuse)for(unsigned n=0;n<(dual?2u:1u);++n){const unsigned slot=(dual?2:1)+n;const DWORD values[]={D3DTEXF_POINT,D3DTEXF_POINT,D3DTEXF_NONE,D3DTADDRESS_CLAMP,D3DTADDRESS_CLAMP,FALSE,0,0};
            if(FAILED(d->SetTexture(12+n,sharedPOM->texture(n))))return fallback();for(unsigned i=0;i<8;++i)if(saved.samplers[slot][i]!=values[i]){saved.samplerChanged[slot]|=1u<<i;if(FAILED(d->SetSamplerState(12+n,keys[i],values[i])))return fallback();}}
        float bank[12];std::copy(projection,projection+4,bank);std::copy(control,control+4,bank+4);if(reuse)std::copy(sharedPOM->control,sharedPOM->control+4,bank+8);
        if(FAILED(d->SetVertexShader(reuse?sharedVertex:vertex))||FAILED(d->SetPixelShader(reuse?it->second.shared:it->second.shader))||FAILED(d->SetPixelShaderConstantF(saved.constantBase,bank,saved.constantCount))){if(reuse)return fallback();saved.restore();return nativeDraw();}
        struct Active {bool& flag;Active(bool& value,bool on):flag(value){flag=on;}~Active(){flag=false;}} active(sharedActive,reuse);
        ++draws;return nativeDraw();
    }
};
