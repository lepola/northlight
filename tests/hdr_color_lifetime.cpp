// Production HDR cache with CPU-only COM objects; no graphics device or game.
#undef NDEBUG
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <memory>
#include "forwarders.h"
#include "hdr_color.h"

struct Shader final:IDirect3DPixelShader9 {
    ULONG refs=1;unsigned reads=0,adds=0;bool fail=false;
    std::vector<DWORD> code;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{assert(refs);++adds;return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{assert(refs);return --refs;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**)override{assert(false);return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetFunction(void* out,UINT* bytes)override{
        ++reads;if(fail)return E_FAIL;const auto size=UINT(code.size()*4);
        if(out){assert(*bytes>=size);std::memcpy(out,code.data(),size);}*bytes=size;return S_OK;
    }
};
struct Pool {
    std::vector<std::unique_ptr<Shader>> shaders;
    Shader* acquire(unsigned constant=0){
        Shader* result=nullptr;
        for(auto& p:shaders)if(!p->refs){result=p.get();break;}
        if(!result){shaders.push_back(std::make_unique<Shader>());result=shaders.back().get();}
        result->refs=1;result->reads=result->adds=0;result->fail=false;
        result->code={0xffff0300,0x02000001,0x800f0800,0xa0e40000u+constant,
                      0x02000001,0x800f0801,0xa0e40001u+constant,65535};return result;
    }
};
struct Device:ForwardIDirect3DDevice9 {
    Pool generated;unsigned creates=0;
    Device():ForwardIDirect3DDevice9(nullptr){}
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD*,IDirect3DPixelShader9** out)override{++creates;*out=generated.acquire();return S_OK;}
};
int main(){
    Device device;Pool native;
    {
        NorthlightHdr::Color cache;auto* old=native.acquire();
        auto* hdr=cache.replacement(&device,old,80,0);assert(hdr&&old->refs==2);
        const auto reads=old->reads,adds=old->adds,creates=device.creates;
        for(unsigned i=0;i<32;++i)assert(cache.replacement(&device,old,80,0)==hdr);
        assert(old->reads==reads&&old->adds==adds&&device.creates==creates);
        // Water re-registration releases its internal shader without forget().
        old->Release();assert(old->refs==1);
        auto* replacement=native.acquire(3);assert(replacement!=old);
        assert(cache.replacement(&device,replacement,80,0)!=hdr);
        cache.forget(old);assert(old->refs==0);
        auto* reused=native.acquire(7);assert(reused==old);
        const auto count=device.creates;
        assert(cache.replacement(&device,reused,80,0)&&device.creates==count+1);
        cache.reset();assert(reused->refs==1&&replacement->refs==1);
        reused->Release();replacement->Release();
    }
    {
        NorthlightHdr::Color cache;auto* source=native.acquire();auto* uiOnly=native.acquire(2);
        assert(cache.replacement(&device,source,80,0));
        assert(cache.replacement(&device,source,160,0)&&source->refs==3);
        assert(cache.replacement(&device,uiOnly,160,0)&&uiOnly->refs==2);
        // Changing UI white clears only that bank, then retains its new owner.
        assert(cache.replacement(&device,source,200,0)&&source->refs==3&&uiOnly->refs==1);
        cache.forget(source);assert(source->refs==1);
        assert(cache.replacement(&device,source,80,0)&&source->refs==2);
        cache.reset();cache.reset();assert(source->refs==1&&uiOnly->refs==1);
        source->Release();uiOnly->Release();
    }
    {
        NorthlightHdr::Color cache;std::vector<Shader*> sources;
        for(unsigned i=0;i<129;++i){auto* source=native.acquire();source->fail=true;sources.push_back(source);}
        for(unsigned bank=0;bank<2;++bank){
            for(unsigned i=0;i<128;++i)assert(!cache.replacement(&device,sources[i],bank?160:80,0));
            assert(!cache.replacement(&device,sources[128],bank?160:80,0)&&sources[128]->refs==1);
        }
        for(unsigned i=0;i<128;++i){assert(sources[i]->refs==3);const auto reads=sources[i]->reads;
            assert(!cache.replacement(&device,sources[i],80,0)&&sources[i]->reads==reads);}
        cache.forget(sources[0]);assert(sources[0]->refs==1);
        cache.reset();for(auto* source:sources){assert(source->refs==1);source->Release();}
    }
    for(const auto& shader:device.generated.shaders)assert(!shader->refs);
    std::puts("HDR_COLOR_LIFETIME_PASS retained_owner=1 pointer_reuse=1 cached_hits_no_work=1 forget=1 reset=1 ui_white=1 failed_entries=1 bounded_banks=128");
    return 0;
}
