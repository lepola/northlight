// Frozen pre-capability-cache normal-map oracle.
#pragma once
#include "auto_normal_memory.h"
#include "quality_settings.h"
#include "material_height.h"
#include "material_profile.h"
#include <unordered_map>

// Generated maps and retained source references have separate bounded budgets.
// Source references prevent pointer reuse; they are not duplicate GPU copies.
class FrozenNormalCache {
    struct Entry {IDirect3DTexture9* source;IDirect3DTexture9* normal;std::uint64_t bytes,sourceBytes,used,version;unsigned frame;bool mutableSource,model;};
    IDirect3DDevice9* d;
    NorthlightMaterialProfile* profile=nullptr;
    NorthlightStateBlockPool states;
    std::vector<Entry> entries;
    std::unordered_map<IDirect3DBaseTexture9*,size_t> lookup[2];
    IDirect3DTexture9* leased[8]={};unsigned leaseCounts[8]={},leaseOverflow=0;
    size_t recent=0;
    IDirect3DPixelShader9* shader=nullptr,*modelShader=nullptr;
    std::uint64_t bytes=0,sourceBytes=0,clock=0,budget=32ull<<20;
    unsigned generated=0,builds=0,hits=0,evictions=0,allocations=0,reuses=0;
    unsigned polarity=0,details=75,balance=50,frame=0;bool memoryPressure=false;
    void erase(size_t index){
        auto& e=entries[index];lookup[e.model].erase(e.source);bytes-=e.bytes;sourceBytes-=e.sourceBytes;drop(e.source);drop(e.normal);
        if(index+1<entries.size()){e=entries.back();lookup[e.model].find(e.source)->second=index;}
        entries.pop_back();recent=entries.size();
    }
    bool reusable(IDirect3DTexture9* normal,UINT width,UINT height){
        // A fixed lease table avoids allocation on hits; overflow disables recycling.
        if(leaseOverflow)return false;for(auto* texture:leased)if(texture==normal)return false;
        D3DSURFACE_DESC desc{};if(FAILED(normal->GetLevelDesc(0,&desc))||desc.Width!=width||desc.Height!=height)return false;
        // A caller may have bound a borrowed map without taking a lease.
        for(DWORD stage=0;stage<21;++stage){IDirect3DBaseTexture9* bound=nullptr;
            const HRESULT hr=d->GetTexture(stage<16?stage:stage==20?D3DDMAPSAMPLER:D3DVERTEXTEXTURESAMPLER0+stage-16,&bound);
            const bool same=bound==normal;drop(bound);if(FAILED(hr)||same)return false;}
        for(unsigned i=0;i<4;++i){IDirect3DSurface9* target=nullptr;const HRESULT hr=d->GetRenderTarget(i,&target);
            if(FAILED(hr)){if(hr!=D3DERR_NOTFOUND)return false;continue;}
            IDirect3DTexture9* container=nullptr;if(target)target->GetContainer(IID_IDirect3DTexture9,reinterpret_cast<void**>(&container));
            const bool same=container==normal;drop(container);drop(target);if(same)return false;}
        return true;
    }
public:
    // Leases must end before the cache owner is destroyed.
    class Lease {
        friend class FrozenNormalCache;
        FrozenNormalCache* owner=nullptr;IDirect3DTexture9* texture=nullptr;unsigned slot=8;
        Lease(FrozenNormalCache& cache,IDirect3DTexture9* normal):owner(&cache),texture(normal){
            if(!texture)return;
            for(unsigned i=0;i<8;++i)if(owner->leased[i]==texture){++owner->leaseCounts[i];slot=i;return;}
            texture->AddRef();
            for(unsigned i=0;i<8;++i)if(!owner->leased[i]){owner->leased[i]=texture;owner->leaseCounts[i]=1;slot=i;return;}++owner->leaseOverflow;
        }
        void release(){if(texture){if(slot<8){if(--owner->leaseCounts[slot]){texture=nullptr;return;}owner->leased[slot]=nullptr;}else --owner->leaseOverflow;drop(texture);}}
    public:
        Lease()=default;Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
        Lease(Lease&& other)noexcept{std::swap(owner,other.owner);std::swap(texture,other.texture);std::swap(slot,other.slot);}
        Lease& operator=(Lease&& other)noexcept{if(this!=&other){release();owner=other.owner;texture=other.texture;slot=other.slot;other.texture=nullptr;}return *this;}
        ~Lease(){release();}
        IDirect3DTexture9* get()const{return texture;}
        explicit operator bool()const{return texture!=nullptr;}
    };
    explicit FrozenNormalCache(IDirect3DDevice9* device):d(device),states(device){refreshBudget();}
    void setProfiler(NorthlightMaterialProfile* p){profile=p;}
    ~FrozenNormalCache(){reset();drop(shader);drop(modelShader);}
    void reset(){states.clear();for(auto& e:entries){drop(e.source);drop(e.normal);}entries.clear();for(auto& index:lookup)index.clear();recent=0;bytes=sourceBytes=0;}
    void configure(const NorthlightQuality::Settings& s){if(polarity!=s.autoHeightPolarity||details!=s.autoHeightDetails||balance!=s.autoHeightBalance){reset();polarity=s.autoHeightPolarity;details=s.autoHeightDetails;balance=s.autoHeightBalance;}}
    void refreshBudget(){
        MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;
        budget=memoryPressure?32ull<<20:GlobalMemoryStatusEx(&memory)?NorthlightAutoNormalMemory::budget(memory.ullAvailVirtual,budget):32ull<<20;
        while(bytes>budget&&!entries.empty()){auto old=std::min_element(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.used<b.used;});erase(size_t(old-entries.begin()));++evictions;}
    }
    void setMemoryPressure(bool on){memoryPressure=on;refreshBudget();states.clear();}
    void trimMemory(){refreshBudget();states.clear();}
    void endFrame(){generated=0;if(memoryPressure)states.clear();if(++frame%60==0)refreshBudget();}
    void report(){logf("AUTONORMAL cached=%zu bytes=%llu generated=%u hits=%u evicted=%u sourceBytes=%llu budgetMiB=%llu allocations=%u reused=%u",entries.size(),(unsigned long long)bytes,builds,hits,evictions,(unsigned long long)sourceBytes,(unsigned long long)(budget>>20),allocations,reuses);}
    Lease acquire(IDirect3DBaseTexture9* source,bool model=false){return Lease(*this,resolve(source,model));}
    IDirect3DTexture9* resolve(IDirect3DBaseTexture9* base,bool model=false){
        using P=NorthlightMaterialProfile;P::Scope timing(profile,P::NormalCache);
        if(!base)return nullptr;
        auto it=entries.end();
        if(recent<entries.size()&&entries[recent].source==base&&entries[recent].model==model)it=entries.begin()+recent;
        else {auto found=lookup[model].find(base);if(found!=lookup[model].end())it=entries.begin()+found->second;}
        // Entries retain the proven 2D source. Only a new resource needs a
        // type query; version and mutable-source checks still run on every hit.
        IDirect3DTexture9* source=nullptr;
        if(it!=entries.end())source=it->source;
        else{if(base->GetType()!=D3DRTYPE_TEXTURE)return nullptr;source=static_cast<IDirect3DTexture9*>(base);}
        ++clock;const auto revision=NorthlightMaterialHeight::version(source);
        if(it!=entries.end()){
            if(revision!=it->version||(it->mutableSource&&!it->version&&frame-it->frame>=30))erase(size_t(it-entries.begin()));
            else{recent=size_t(it-entries.begin());it->used=clock;++hits;if(profile)profile->count(P::NormalHit);return it->normal;}
        }
        if(profile)profile->count(P::NormalMiss);
        if(generated>=2)return nullptr;
        timing.next(P::NormalBuild);
        D3DSURFACE_DESC desc{};if(FAILED(source->GetLevelDesc(0,&desc))||!desc.Width||!desc.Height||(desc.Usage&D3DUSAGE_DEPTHSTENCIL))return nullptr;
        const UINT width=std::min(desc.Width,512u),height=std::min(desc.Height,512u);
        timing.work(uint64_t(width)*height);
        const std::uint64_t cost=NorthlightAutoNormalMemory::mipBytes(D3DFMT_A8R8G8B8,width,height,0),sourceCost=NorthlightAutoNormalMemory::mipBytes(desc.Format,desc.Width,desc.Height,source->GetLevelCount());
        constexpr std::uint64_t sourceBudget=128ull<<20;if(cost>budget||sourceCost>sourceBudget)return nullptr;
        struct Recycled {IDirect3DTexture9* texture=nullptr;~Recycled(){drop(texture);}} recycled;
        while(!entries.empty()&&(bytes+cost>budget||sourceBytes+sourceCost>sourceBudget||entries.size()>=256)){
            auto oldest=std::min_element(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.used<b.used;});
            // Reuse only an evicted, unbound map that no pending draw borrows.
            if(!recycled.texture&&reusable(oldest->normal,width,height)){recycled.texture=oldest->normal;oldest->normal=nullptr;}
            erase(size_t(oldest-entries.begin()));++evictions;
        }
        // Native D3D9's fixed-function draw can clear programmable VS constants
        // beyond those restored by an ALL state block. Preserve the bank on builds.
        struct VertexConstants {IDirect3DDevice9* device;float values[256*4]={};UINT count=0;~VertexConstants(){if(count)device->SetVertexShaderConstantF(0,values,count);}} vertex{d};
        D3DCAPS9 caps{};if(FAILED(d->GetDeviceCaps(&caps)))return nullptr;
        const UINT count=std::min<UINT>(caps.MaxVertexShaderConst,256u);if(!count||FAILED(d->GetVertexShaderConstantF(0,vertex.values,count)))return nullptr;vertex.count=count;
        SavedState saved(d,&states);if(!saved.ok)return nullptr;
        auto*& selected=model?modelShader:shader;if(!selected&&FAILED(d->CreatePixelShader(model?kAutoNormalModelShader:kAutoNormalShader,&selected)))return nullptr;
        IDirect3DTexture9* normal=recycled.texture;recycled.texture=nullptr;IDirect3DSurface9* target=nullptr;
        if(normal)++reuses;
        else{if(FAILED(d->CreateTexture(width,height,0,D3DUSAGE_RENDERTARGET|D3DUSAGE_AUTOGENMIPMAP,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&normal,nullptr)))return nullptr;++allocations;}
        if(FAILED(normal->GetSurfaceLevel(0,&target))){drop(normal);return nullptr;}
        d->SetDepthStencilSurface(nullptr);for(unsigned i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
        const HRESULT bound=d->SetRenderTarget(0,target);drop(target);if(FAILED(bound)){drop(normal);return nullptr;}
        D3DVIEWPORT9 vp={0,0,width,height,0,1};d->SetViewport(&vp);
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE})d->SetRenderState(state,FALSE);
        d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
        d->SetTexture(0,source);d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetPixelShader(selected);
        for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(0,state,D3DTEXF_LINEAR);
        d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
        d->SetSamplerState(0,D3DSAMP_MAXMIPLEVEL,0);d->SetSamplerState(0,D3DSAMP_MIPMAPLODBIAS,0);
        d->SetSamplerState(0,D3DSAMP_ADDRESSU,model?D3DTADDRESS_CLAMP:D3DTADDRESS_WRAP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,model?D3DTADDRESS_CLAMP:D3DTADDRESS_WRAP);
        // Model relief uses only profile.z (polarity); its shader derives local detail from the source.
        NorthlightMaterialHeight::Image image;std::array<float,4> heightProfile;
        {P::Scope read(profile,P::HeightRead);heightProfile=model?NorthlightMaterialHeight::profile(image,polarity,details,balance):NorthlightMaterialHeight::textureProfile(source,polarity,details,balance);}
        const float texel[]={1.f/width,1.f/height,5.f,0};d->SetPixelShaderConstantF(0,texel,1);d->SetPixelShaderConstantF(1,heightProfile.data(),1);
        struct V {float x,y,z,w,u,v;};const V quad[]={{-.5f,-.5f,0,1,0,0},{width-.5f,-.5f,0,1,1,0},{-.5f,height-.5f,0,1,0,1},{width-.5f,height-.5f,0,1,1,1}};
        {P::Scope draw(profile,P::NormalBuild,0,true);draw.work(uint64_t(width)*height);if(FAILED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(V)))){drop(normal);return nullptr;}}
        {P::Scope mips(profile,P::Mips,0,true);normal->SetAutoGenFilterType(D3DTEXF_LINEAR);normal->GenerateMipSubLevels();}
        source->AddRef();try{entries.push_back({source,normal,cost,sourceCost,clock,revision,frame,(desc.Usage&(D3DUSAGE_DYNAMIC|D3DUSAGE_RENDERTARGET))!=0||(desc.Pool==D3DPOOL_DEFAULT&&source->GetLevelCount()==1),model});
            try{lookup[model].emplace(source,entries.size()-1);}catch(...){entries.pop_back();throw;}
        }catch(...){source->Release();drop(normal);return nullptr;}recent=entries.size()-1;bytes+=cost;sourceBytes+=sourceCost;++generated;++builds;return normal;
    }
};
