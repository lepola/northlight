#define main distance_oracle_main
#include "composite_distance_gpu.cpp"
#undef main
#include <cstdarg>
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char* f,...){va_list a;va_start(a,f);std::vprintf(f,a);std::puts("");va_end(a);}
#include "saved_state.h"
#include "auto_bump_renderer.h"

#include "forwarders.h"
#include "fixtures/material_normal_cache_reference.h"
struct Device:ForwardIDirect3DDevice9{unsigned calls=0;bool fail=true;Device(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* c)override{++calls;if(fail){fail=false;return E_FAIL;}return real->GetDeviceCaps(c);}};
template<class Cache>static unsigned run(IDirect3DDevice9* d,const std::vector<IDirect3DTexture9*>& sources,std::vector<uint64_t>& hashes){Device wrapped(d);Cache cache(&wrapped);assert(!cache.resolve(sources[0]));assert(wrapped.calls==1);
 for(auto* source:sources){ok(d->BeginScene());auto* normal=cache.resolve(source);assert(normal);ok(d->EndScene());D3DSURFACE_DESC desc{};ok(normal->GetLevelDesc(0,&desc));IDirect3DSurface9 *surface=nullptr,*readback=nullptr;ok(normal->GetSurfaceLevel(0,&surface));ok(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&readback,nullptr));ok(d->GetRenderTargetData(surface,readback));D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));uint64_t hash=14695981039346656037ull;for(unsigned y=0;y<desc.Height;++y)for(unsigned x=0;x<desc.Width*4;++x)hash=(hash^static_cast<const unsigned char*>(lock.pBits)[size_t(y)*lock.Pitch+x])*1099511628211ull;ok(readback->UnlockRect());drop(surface);drop(readback);hashes.push_back(hash);cache.endFrame();}
 return wrapped.calls;
}
int main(int argc,char** argv){assert(argc==2);auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");auto* api=create(D3D_SDK_VERSION);HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferFormat=D3DFMT_X8R8G8B8;IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));std::vector<IDirect3DTexture9*> sources;
 for(unsigned i=0;i<20;++i){IDirect3DTexture9* source=nullptr;ok(d->CreateTexture(128,128,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&source,nullptr));D3DLOCKED_RECT lock{};ok(source->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<128;++y)std::memset(static_cast<char*>(lock.pBits)+size_t(y)*lock.Pitch,17+i*11,512);ok(source->UnlockRect(0));sources.push_back(source);}
 std::vector<uint64_t> a,b;const auto old=run<FrozenNormalCache>(d,sources,a),now=run<NorthlightAutoNormalCache>(d,sources,b);assert(old==21&&now==2&&a==b);for(auto*& p:sources)drop(p);drop(d);drop(api);FreeLibrary(module);DestroyWindow(window);printf("NORMAL_CAPABILITIES_PASS builds=20 oldQueries=%u newQueries=%u pixelHashesExact=1 initialFailureRetries=1\n",old,now);
}
