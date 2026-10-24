#define main distance_oracle_main
#include "composite_distance_gpu.cpp"
#undef main
#include <cstdarg>
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char* f,...){va_list a;va_start(a,f);std::vprintf(f,a);std::puts("");va_end(a);}
#include "saved_state.h"
#include "auto_bump_renderer.h"
int main(int argc,char** argv){
 std::setvbuf(stdout,nullptr,_IONBF,0);assert(argc==2);auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");auto api=create(D3D_SDK_VERSION);
 HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferFormat=D3DFMT_X8R8G8B8;IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 NorthlightAutoNormalCache cache(d);std::vector<IDirect3DTexture9*> textures;
 for(unsigned i=0;i<20;++i){IDirect3DTexture9* source=nullptr;ok(d->CreateTexture(1024,1024,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&source,nullptr));D3DLOCKED_RECT lock{};ok(source->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<1024;++y)std::memset((char*)lock.pBits+y*lock.Pitch,128,1024*4);ok(source->UnlockRect(0));textures.push_back(source);}
 for(auto* source:textures){ok(d->BeginScene());assert(cache.resolve(source));ok(d->EndScene());cache.endFrame();}
 std::puts("CACHE_FIRST_PASS");cache.report();
 for(unsigned i=0;i<35;++i)cache.endFrame();
 for(auto* source:textures){ok(d->BeginScene());assert(cache.resolve(source));ok(d->EndScene());cache.endFrame();}
 std::puts("CACHE_SECOND_PASS");cache.report();
 for(auto*& source:textures)drop(source);std::puts("MATERIAL_CACHE_WORKING_PASS");
}
