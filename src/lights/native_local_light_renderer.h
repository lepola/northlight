#pragma once
#include "native_local_light_patch.h"
#include "local_light_compiled_shaders.h"
#include <memory>

struct NorthlightNativeLocalDraw {
    unsigned slot=0,rendererFrame=0;
    IDirect3DCubeTexture9* cube=nullptr; // Borrowed complete world-space cube.
    float position[3]{},range=0,gain=1,inverseView[16]{};
};
class NorthlightNativeLocalLighting {
    struct Key {IDirect3DVertexShader9* vs;IDirect3DPixelShader9* ps;unsigned slot;bool operator==(const Key& b)const{return vs==b.vs&&ps==b.ps&&slot==b.slot;}};
    struct Hash {size_t operator()(const Key& k)const{return (reinterpret_cast<size_t>(k.vs)*16777619u^reinterpret_cast<size_t>(k.ps))*16777619u^k.slot;}};
    template<class T> static void release(T*& p){if(p){p->Release();p=nullptr;}}
    struct Entry {
        IDirect3DVertexShader9 *originalVS=nullptr,*vs=nullptr;IDirect3DPixelShader9 *originalPS=nullptr,*ps=nullptr;
        NorthlightNativeLocalPatch::PixelInfo info;
        ~Entry(){release(vs);release(ps);release(originalVS);release(originalPS);}
    };
    std::unordered_map<Key,std::unique_ptr<Entry>,Hash> shaders;
    IDirect3DDevice9* d;
    unsigned warmFrame=~0u,coldAttempts=0;
    Entry* acquire(IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,unsigned slot,unsigned frame){
        const Key key{vs,ps,slot};auto old=shaders.find(key);if(old!=shaders.end())return old->second->ps?old->second.get():nullptr;
        if(!vs||!ps||shaders.size()>=128)return nullptr;
        if(warmFrame!=frame){warmFrame=frame;coldAttempts=0;}
        if(coldAttempts){++deferredPairs;return nullptr;}++coldAttempts; // One cold attempt/frame, including unsupported pairs.
        auto owned=std::make_unique<Entry>();auto* entry=owned.get();vs->AddRef();ps->AddRef();entry->originalVS=vs;entry->originalPS=ps;
        shaders.emplace(key,std::move(owned));++coldPairs;
        UINT vb=0,pb=0;if(FAILED(vs->GetFunction(nullptr,&vb))||FAILED(ps->GetFunction(nullptr,&pb))||vb<8||pb<8||vb>65536||pb>65536||vb%4||pb%4)return nullptr;
        std::vector<DWORD> vertex(vb/4),pixel(pb/4);std::vector<uint32_t> patchedVS,patchedPS;NorthlightNativeLocalPatch::VertexInfo vi;
        if(FAILED(vs->GetFunction(vertex.data(),&vb))||FAILED(ps->GetFunction(pixel.data(),&pb))||
           !NorthlightNativeLocalPatch::vertex(reinterpret_cast<const uint32_t*>(vertex.data()),vertex.size(),&slot,1,patchedVS,vi)||
           !NorthlightNativeLocalPatch::pixel(reinterpret_cast<const uint32_t*>(pixel.data()),pixel.size(),vi,reinterpret_cast<const uint32_t*>(kNativeLocalLighting1Shader),sizeof(kNativeLocalLighting1Shader)/4,patchedPS,entry->info))return nullptr;
        if(FAILED(d->CreateVertexShader(reinterpret_cast<const DWORD*>(patchedVS.data()),&entry->vs))||
           FAILED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(patchedPS.data()),&entry->ps))){release(entry->vs);release(entry->ps);return nullptr;}
        shaderSlots+=entry->info.addedSlots;return entry;
    }
public:
    unsigned coldPairs=0,deferredPairs=0,shaderSlots=0;uint64_t draws=0,fallbacks=0;
    explicit NorthlightNativeLocalLighting(IDirect3DDevice9* device):d(device){}
    void reset(){shaders.clear();} // A same-frame resource reset keeps the cold-attempt budget.
    template<class Draw> HRESULT draw(const NorthlightNativeLocalDraw& light,IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,Draw submit){
        Entry* shader=nullptr;try{shader=acquire(vs,ps,light.slot,light.rendererFrame);}catch(const std::bad_alloc&){++fallbacks;return submit();}
        if(!shader||!light.cube){++fallbacks;return submit();}
        const unsigned sampler=shader->info.samplers[0],first=shader->info.control+2;
        const D3DSAMPLERSTATETYPE keys[]={D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW,D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE};
        const DWORD wanted[]={D3DTADDRESS_CLAMP,D3DTADDRESS_CLAMP,D3DTADDRESS_CLAMP,D3DTEXF_POINT,D3DTEXF_POINT,D3DTEXF_NONE,FALSE};
        float saved[8][4];DWORD values[7];IDirect3DBaseTexture9* texture=nullptr;
        if(FAILED(d->GetPixelShaderConstantF(first,saved[0],8))||FAILED(d->GetTexture(sampler,&texture))){release(texture);++fallbacks;return submit();}
        for(unsigned i=0;i<7;++i)if(FAILED(d->GetSamplerState(sampler,keys[i],values+i))){release(texture);++fallbacks;return submit();}
        struct Restore {NorthlightNativeLocalLighting& r;Entry* e;IDirect3DBaseTexture9* texture;const D3DSAMPLERSTATETYPE* keys;const DWORD *values,*wanted;const float* constants;unsigned first,sampler;bool restored=false;
            bool restore(){if(restored)return true;auto retry=[](auto call){return SUCCEEDED(call())||SUCCEEDED(call());};bool ok=true;
                ok=retry([&]{return r.d->SetVertexShader(e->originalVS);})&&ok;ok=retry([&]{return r.d->SetPixelShader(e->originalPS);})&&ok;
                ok=retry([&]{return r.d->SetPixelShaderConstantF(first,constants,8);})&&ok;ok=retry([&]{return r.d->SetTexture(sampler,texture);})&&ok;
                for(unsigned i=0;i<7;++i)if(values[i]!=wanted[i])ok=retry([&]{return r.d->SetSamplerState(sampler,keys[i],values[i]);})&&ok;
                restored=ok;return ok;}
            ~Restore(){restore();release(texture);}
        } restore{*this,shader,texture,keys,values,wanted,saved[0],first,sampler};
        float constants[8][4]{};constants[0][3]=.025f;std::memcpy(constants[1],light.inverseView,64);std::memcpy(constants[5],light.position,12);
        constants[7][0]=.1f;constants[7][1]=light.range;constants[7][2]=1.f/256;constants[7][3]=light.gain;
        bool bound=SUCCEEDED(d->SetPixelShaderConstantF(first,constants[0],8))&&SUCCEEDED(d->SetTexture(sampler,light.cube));
        for(unsigned i=0;i<7&&bound;++i)if(values[i]!=wanted[i])bound=SUCCEEDED(d->SetSamplerState(sampler,keys[i],wanted[i]));
        bound=bound&&SUCCEEDED(d->SetVertexShader(shader->vs))&&SUCCEEDED(d->SetPixelShader(shader->ps));
        if(!bound){++fallbacks;if(!restore.restore())return E_FAIL;return submit();}
        ++draws;const HRESULT result=submit();return restore.restore()?result:E_FAIL;
    }
};
