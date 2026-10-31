// CPU cache-state oracle; GPU image/lease coverage remains in auto_normal_reuse_gpu.cpp.
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <vector>
static long failAllocation=-1,allocationCount=0;
void* operator new(std::size_t n){if(failAllocation>=0&&allocationCount++==failAllocation)throw std::bad_alloc();if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
using UINT=unsigned;using DWORD=std::uint32_t;using HRESULT=int;using D3DFORMAT=unsigned;
constexpr int FALSE=0,TRUE=1,E_FAIL=-1,D3DERR_NOTFOUND=-2,IID_IDirect3DTexture9=0;
#define FAILED(x) ((x)<0)
enum {D3DFMT_DXT1=1,D3DFMT_DXT2,D3DFMT_DXT3,D3DFMT_DXT4,D3DFMT_DXT5,D3DFMT_A8,D3DFMT_L8,D3DFMT_P8,D3DFMT_R5G6B5,D3DFMT_X1R5G5B5,D3DFMT_A1R5G5B5,D3DFMT_A4R4G4B4,D3DFMT_A8L8,D3DFMT_V8U8,D3DFMT_L16,D3DFMT_R8G8B8,D3DFMT_A8R8G8B8,D3DFMT_X8R8G8B8,D3DFMT_A8B8G8R8,D3DFMT_X8B8G8R8,D3DFMT_G16R16,D3DFMT_R32F,D3DFMT_A16B16G16R16,D3DFMT_A16B16G16R16F,D3DFMT_G32R32F};
enum {D3DUSAGE_DYNAMIC=1,D3DUSAGE_RENDERTARGET=2,D3DUSAGE_DEPTHSTENCIL=4,D3DUSAGE_AUTOGENMIPMAP=8,D3DPOOL_DEFAULT=0,D3DPOOL_MANAGED=1,D3DRTYPE_TEXTURE=3,D3DDMAPSAMPLER=256,D3DVERTEXTEXTURESAMPLER0=257};
enum {D3DTEXF_LINEAR=1,D3DTEXF_NONE,D3DTADDRESS_CLAMP,D3DTADDRESS_WRAP,D3DCULL_NONE,D3DFVF_XYZRHW=4,D3DFVF_TEX1=256,D3DPT_TRIANGLESTRIP=5};
enum {D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_COLORWRITEENABLE,D3DRS_CULLMODE};
enum {D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_SRGBTEXTURE,D3DSAMP_MAXMIPLEVEL,D3DSAMP_MIPMAPLODBIAS,D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV};
struct D3DSURFACE_DESC {UINT Width=32,Height=32;DWORD Usage=0;D3DFORMAT Format=D3DFMT_A8R8G8B8;unsigned Pool=D3DPOOL_MANAGED;};
struct D3DVIEWPORT9 {DWORD X,Y,Width,Height;float MinZ,MaxZ;};struct D3DCAPS9 {UINT MaxVertexShaderConst=256;};
struct MEMORYSTATUSEX {DWORD dwLength=0;std::uint64_t ullAvailVirtual=0;};
static std::uint64_t availableMemory=715ull<<20;
static int GlobalMemoryStatusEx(MEMORYSTATUSEX* p){p->ullAvailVirtual=availableMemory;return TRUE;}
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static unsigned liveObjects=0;
struct Ref {unsigned refs=1;Ref(){++liveObjects;}virtual ~Ref(){--liveObjects;}void AddRef(){++refs;}void Release(){assert(refs);if(!--refs)delete this;}};
struct IDirect3DBaseTexture9:Ref {unsigned GetType(){return D3DRTYPE_TEXTURE;}};
struct IDirect3DSurface9;
struct IDirect3DTexture9:IDirect3DBaseTexture9 {
    D3DSURFACE_DESC desc;std::uint64_t identity=0,version=1,payload=0;unsigned levels=1;
    HRESULT GetLevelDesc(unsigned,D3DSURFACE_DESC* out){*out=desc;return 0;}unsigned GetLevelCount(){return levels;}
    HRESULT GetSurfaceLevel(unsigned,IDirect3DSurface9** out);void SetAutoGenFilterType(unsigned){}void GenerateMipSubLevels(){}
};
struct IDirect3DSurface9:Ref {
    IDirect3DTexture9* texture;explicit IDirect3DSurface9(IDirect3DTexture9* t):texture(t){t->AddRef();}~IDirect3DSurface9(){drop(texture);}
    HRESULT GetContainer(int,void** out){texture->AddRef();*out=texture;return 0;}
};
HRESULT IDirect3DTexture9::GetSurfaceLevel(unsigned,IDirect3DSurface9** out){try{*out=new IDirect3DSurface9(this);return 0;}catch(...){return E_FAIL;}}
struct IDirect3DPixelShader9:Ref {bool model;explicit IDirect3DPixelShader9(bool m):model(m){}};
static const DWORD kAutoNormalShader[]={0},kAutoNormalModelShader[]={1};
struct IDirect3DDevice9 {
    IDirect3DSurface9* target=nullptr;IDirect3DBaseTexture9* texture=nullptr;IDirect3DPixelShader9* shader=nullptr;
    float vertex[1024]={},pixel[8]={};bool failDraw=false;
    ~IDirect3DDevice9(){drop(target);drop(texture);drop(shader);}
    HRESULT GetTexture(DWORD stage,IDirect3DBaseTexture9** out){*out=stage==0?texture:nullptr;if(*out)(*out)->AddRef();return 0;}
    HRESULT GetRenderTarget(unsigned slot,IDirect3DSurface9** out){*out=slot==0?target:nullptr;if(*out)(*out)->AddRef();return *out?0:D3DERR_NOTFOUND;}
    HRESULT SetRenderTarget(unsigned slot,IDirect3DSurface9* value){if(!slot){if(value)value->AddRef();drop(target);target=value;}return 0;}
    HRESULT SetTexture(unsigned slot,IDirect3DBaseTexture9* value){if(!slot){if(value)value->AddRef();drop(texture);texture=value;}return 0;}
    HRESULT SetPixelShader(IDirect3DPixelShader9* value){if(value)value->AddRef();drop(shader);shader=value;return 0;}
    HRESULT GetDeviceCaps(D3DCAPS9* c){*c={};return 0;}
    HRESULT GetVertexShaderConstantF(unsigned first,float* p,unsigned count){std::memcpy(p,vertex+first*4,count*16);return 0;}
    HRESULT SetVertexShaderConstantF(unsigned first,const float* p,unsigned count){std::memcpy(vertex+first*4,p,count*16);return 0;}
    HRESULT SetPixelShaderConstantF(unsigned first,const float* p,unsigned count){std::memcpy(pixel+first*4,p,count*16);return 0;}
    HRESULT CreatePixelShader(const DWORD* p,IDirect3DPixelShader9** out){try{*out=new IDirect3DPixelShader9(p[0]!=0);return 0;}catch(...){return E_FAIL;}}
    HRESULT CreateTexture(UINT w,UINT h,UINT, DWORD usage,D3DFORMAT format,unsigned pool,IDirect3DTexture9** out,void*){
        try{*out=new IDirect3DTexture9;(*out)->desc={w,h,usage,format,pool};return 0;}catch(...){return E_FAIL;}}
    HRESULT SetDepthStencilSurface(void*){return 0;}HRESULT SetViewport(const D3DVIEWPORT9*){return 0;}HRESULT SetRenderState(unsigned,DWORD){return 0;}
    HRESULT SetSamplerState(unsigned,unsigned,DWORD){return 0;}HRESULT SetVertexShader(void*){return 0;}HRESULT SetFVF(DWORD){return 0;}
    HRESULT DrawPrimitiveUP(unsigned,unsigned,const void*,unsigned){
        if(failDraw){failDraw=false;return E_FAIL;}assert(target&&texture&&shader);auto* source=static_cast<IDirect3DTexture9*>(texture);
        std::uint64_t h=(source->identity*1099511628211ull)^source->payload^source->version^std::uint64_t(shader->model);
        for(float value:pixel){std::uint32_t bits;std::memcpy(&bits,&value,4);h=(h^bits)*1099511628211ull;}target->texture->payload=h;return 0;
    }
};
struct NorthlightStateBlockPool {explicit NorthlightStateBlockPool(IDirect3DDevice9*){}void clear(){}};
struct SavedState {
    IDirect3DDevice9* d;IDirect3DSurface9* target;IDirect3DBaseTexture9* texture;IDirect3DPixelShader9* shader;bool ok=true;
    SavedState(IDirect3DDevice9* device,NorthlightStateBlockPool*):d(device),target(d->target),texture(d->texture),shader(d->shader){if(target)target->AddRef();if(texture)texture->AddRef();if(shader)shader->AddRef();}
    ~SavedState(){d->SetRenderTarget(0,target);d->SetTexture(0,texture);d->SetPixelShader(shader);drop(target);drop(texture);drop(shader);}
};
static void logf(const char*,...){}
namespace NorthlightQuality {struct Settings {unsigned autoHeightPolarity=0,autoHeightDetails=75,autoHeightBalance=50;};}
namespace NorthlightMaterialHeight {
struct Image {};
static std::uint64_t version(IDirect3DTexture9* p){return p->version;}
static void read(IDirect3DTexture9*,Image&){}
static std::array<float,4> profile(const Image&,unsigned polarity,unsigned details,unsigned balance){return {{float(details),float(balance),float(polarity),0}};}
}
struct NorthlightMaterialProfile {
    enum Stage {NormalCache,NormalHit,NormalMiss,HeightRead,NormalBuild,Mips};
    struct Scope {Scope(NorthlightMaterialProfile*,Stage,unsigned=0,bool=false){}void work(std::uint64_t){}void next(Stage){}};void count(Stage){}
};
struct ZeroHash {size_t operator()(IDirect3DTexture9*)const{return 0;}};
#include "auto_normal_memory.h"
#include "cache_linear.h"
#include "cache_integrated.h"
#include <filesystem>
// This CPU oracle exercises the unchanged in-memory cache independently of D3D.
// The real persistent adapter runs in material_disk_gpu.cpp.
struct NorthlightMaterialMapPersistence {
    enum class Status {Skip,Pending,Miss,Hit};struct Probe {Status status=Status::Skip;std::array<float,4> profile{};};
    NorthlightMaterialMapPersistence(IDirect3DDevice9*,const std::filesystem::path&,const char*,const char* =nullptr,bool=false){}
    void reset(){}void endFrame(){}void report(){}
    Probe probe(IDirect3DTexture9*,const D3DSURFACE_DESC&,bool,unsigned,unsigned,unsigned){return {};}
    bool upload(const Probe&,IDirect3DTexture9*,unsigned,unsigned){return false;}
    void capture(const Probe&,IDirect3DTexture9*,unsigned,unsigned){}
};
#include "cache_current.h"
template<class T,class=void>struct Indexed:std::false_type{};
template<class T>struct Indexed<T,std::void_t<decltype(std::declval<T>().lookup)>>:std::true_type{};
using Row=std::tuple<std::uint64_t,bool,std::uint64_t,unsigned,std::uint64_t,std::uint64_t>;
template<class Cache>static std::vector<Row> state(Cache& c){
    std::vector<Row> out;std::uint64_t bytes=0,sources=0;
    for(size_t i=0;i<c.entries.size();++i){const auto& e=c.entries[i];assert(e.source&&e.normal);bytes+=e.bytes;sources+=e.sourceBytes;
        out.emplace_back(e.source->identity,e.model,e.version,e.frame,e.used,e.normal->payload);
        if constexpr(Indexed<Cache>::value){const auto f=c.lookup[e.model].find(e.source);assert(f!=c.lookup[e.model].end()&&f->second==i);}}
    assert(bytes==c.bytes&&sources==c.sourceBytes&&c.entries.size()<=256&&bytes<=c.budget&&sources<=(128ull<<20));
    if constexpr(Indexed<Cache>::value)assert(c.lookup[0].size()+c.lookup[1].size()==c.entries.size());
    std::sort(out.begin(),out.end());return out;
}
template<class Cache>struct Owner {IDirect3DDevice9 device;Cache cache{&device};};
static std::uint32_t seed=0x12345678;static unsigned randomValue(){seed^=seed<<13;seed^=seed>>17;return seed^=seed<<5;}
template<class Cache>static void faults(IDirect3DTexture9* source,unsigned& sites){
    for(long site=0;site<32;++site){Owner<Cache> o;const auto before=source->refs;
        failAllocation=site;allocationCount=0;try{o.cache.resolve(source,true);}catch(const std::bad_alloc&){assert(false&&"publication failure escaped");}
        if(allocationCount>site)++sites;failAllocation=-1;state(o.cache);o.cache.endFrame();assert(o.cache.resolve(source,true));state(o.cache);
        o.cache.reset();assert(source->refs==before);assert(o.cache.entries.empty());state(o.cache);
    }
}
// Exercise a visible set larger than the unchanged 32 MiB map budget.
// Presence is the contract: a cache hit supplies the same relief every frame.
template<class Cache>static unsigned overBudgetStability(bool stable,unsigned count=32,unsigned width=512){
    const auto before=liveObjects;unsigned transitions=0;
    {
        Owner<Cache> o;o.cache.frame=~0u-16;std::vector<IDirect3DTexture9*> sources;
        for(unsigned i=0;i<count;++i){auto* t=new IDirect3DTexture9;t->identity=1000+i;t->payload=i*31+7;t->desc.Width=t->desc.Height=width;sources.push_back(t);}
        std::vector<bool> previous(count);unsigned present=0,builds=0;
        for(unsigned f=0;f<count+64;++f){
            std::vector<bool> now(count);
            for(unsigned j=0;j<count;++j){const unsigned i=f<count?j:(f&1?count-1-j:(j+f)%count);auto* map=o.cache.resolve(sources[i],i%2!=0);now[i]=map!=nullptr;}
            state(o.cache);assert(o.cache.budget==(32ull<<20));
            if(f==count-1){present=unsigned(std::count(now.begin(),now.end(),true));builds=o.cache.builds;assert(present>0&&present<count);}
            if(f>=count)for(unsigned i=0;i<count;++i)transitions+=now[i]!=previous[i];
            previous=now;o.cache.endFrame();
        }
        if(stable){assert(!transitions&&o.cache.builds==builds);assert(o.cache.entries.size()==present);}
        // Leave the previous set behind: a new view can reclaim its cold entries.
        o.cache.endFrame();o.cache.endFrame();auto* next=new IDirect3DTexture9;next->identity=9999;next->desc.Width=next->desc.Height=512;
        assert(o.cache.resolve(next,true));state(o.cache);drop(next);
        // A real source mutation must still invalidate a hot map.
        o.cache.endFrame();auto* hot=o.cache.resolve(sources[0],false);
        if(hot){const auto payload=hot->payload;++sources[0]->version;auto* changed=o.cache.resolve(sources[0],false);assert(changed&&changed->payload!=payload);}
        o.cache.reset();for(auto*& t:sources){assert(t->refs==1);drop(t);}
    }
    assert(liveObjects==before);return transitions;
}
int main(int argc,char**){
    std::vector<IDirect3DTexture9*> source;for(unsigned i=0;i<320;++i){auto* t=new IDirect3DTexture9;t->identity=i+1;t->payload=i*17+3;source.push_back(t);}
    unsigned compared=0,sites=0;
    {
        Owner<CacheLinear> a;Owner<CacheIntegrated> b;Owner<CacheCurrent> c;
        auto same=[&](unsigned i,bool model){auto* x=a.cache.resolve(source[i],model);auto* y=b.cache.resolve(source[i],model);auto* z=c.cache.resolve(source[i],model);
            assert(bool(x)==bool(y)&&bool(x)==bool(z));if(x)assert(x->payload==y->payload&&x->payload==z->payload);
            assert(state(a.cache)==state(b.cache)&&state(a.cache)==state(c.cache));assert(a.cache.bytes==b.cache.bytes&&a.cache.bytes==c.cache.bytes);++compared;};
        auto frame=[&]{a.cache.endFrame();b.cache.endFrame();c.cache.endFrame();};
        for(unsigned i=0;i<320;++i){same(i,i%3==0);frame();} // Count eviction and non-adjacent/model-specific hits.
        for(unsigned i=0;i<12000;++i){const unsigned op=randomValue()%31,index=randomValue()%320;
            if(op==0){++source[index]->version;source[index]->payload^=randomValue();}
            if(op==1){a.cache.reset();b.cache.reset();c.cache.reset();}
            if(op==2){NorthlightQuality::Settings s;s.autoHeightPolarity=randomValue()%2;a.cache.configure(s);b.cache.configure(s);c.cache.configure(s);}
            if(op==3){const bool pressure=(randomValue()&1)!=0;a.cache.setMemoryPressure(pressure);b.cache.setMemoryPressure(pressure);c.cache.setMemoryPressure(pressure);}
            if(op==4)a.device.failDraw=b.device.failDraw=c.device.failDraw=true;
            same(index,(randomValue()&1)!=0);if(op<20)frame();
        }
        a.cache.reset();b.cache.reset();c.cache.reset();frame();
        for(auto* t:source){t->desc.Width=t->desc.Height=512;t->desc.Pool=D3DPOOL_DEFAULT;t->version=0;}
        for(unsigned i=0;i<180;++i){same(i%30,(i/30)%2);frame();} // Byte pressure and unknown mutable revision.
        for(unsigned i=0;i<31;++i)frame();source[0]->payload^=0x7343;same(0,true);frame();
        availableMemory=1200ull<<20;a.cache.refreshBudget();b.cache.refreshBudget();c.cache.refreshBudget();
        for(unsigned i=0;i<65;++i){same(i,true);frame();}
        availableMemory=715ull<<20;a.cache.refreshBudget();b.cache.refreshBudget();c.cache.refreshBudget();assert(state(a.cache)==state(c.cache));
    }
    // Nine simultaneous leases cover both fixed slots and overflow; pressure cannot recycle them.
    {
        Owner<CacheCurrent> o;std::vector<CacheCurrent::Lease> held;std::vector<std::uint64_t> payload;
        for(unsigned i=0;i<9;++i){held.push_back(o.cache.acquire(source[i],true));assert(held.back());payload.push_back(held.back().get()->payload);o.cache.endFrame();}
        for(unsigned i=9;i<65;++i){assert(o.cache.resolve(source[i],true));o.cache.endFrame();}
        for(unsigned i=0;i<held.size();++i)assert(held[i].get()->payload==payload[i]);
        o.cache.reset();state(o.cache);for(unsigned i=0;i<held.size();++i)assert(held[i].get()->payload==payload[i]);
    }
    const auto oldTransitions=overBudgetStability<CacheIntegrated>(false);
    const auto newTransitions=overBudgetStability<CacheCurrent>(true);assert(oldTransitions>0&&!newTransitions);
    std::printf("NORMAL_CACHE_STABILITY_PASS visible=32 budgetMiB=32 frames=96 oldTransitions=%u newTransitions=%u mixedModelWorld=1 viewChange=1 revision=1\n",oldTransitions,newTransitions);
    for(const auto scenario:{std::pair<unsigned,unsigned>{320,32},{32,4096}}){
        const auto old=overBudgetStability<CacheIntegrated>(false,scenario.first,scenario.second);
        assert(old>0&&!overBudgetStability<CacheCurrent>(true,scenario.first,scenario.second));
        std::printf("NORMAL_CACHE_LIMIT_PASS visible=%u width=%u oldTransitions=%u newTransitions=0 frameWrap=1\n",scenario.first,scenario.second,old);
    }
    faults<CacheCurrent>(source[0],sites);assert(sites>=5);
    for(auto*& t:source){assert(t->refs==1);drop(t);}assert(liveObjects==0);
    std::printf("AUTO_NORMAL_INDEX_PASS compared=%u allocationSites=%u collisionMode=%u objects=0\n",compared,sites,unsigned(NORMAL_INDEX_COLLISIONS));
    (void)argc;
}
