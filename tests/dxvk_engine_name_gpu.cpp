#undef NDEBUG
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>

static void ok(HRESULT result){if(FAILED(result)){std::fprintf(stderr,"D3D9 failure: %08lx\n",static_cast<unsigned long>(result));std::fflush(stderr);std::abort();}}
int main(){
    WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"NorthlightEngineNameGpuTest";
    assert(RegisterClassW(&cls));HWND window=CreateWindowW(cls.lpszClassName,L"",WS_OVERLAPPED,0,0,128,128,nullptr,nullptr,cls.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryW(L"d3d9.dll");assert(module);
    auto factory=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(factory);
    IDirect3D9* api=factory(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;
    pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.BackBufferCount=2;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED|D3DCREATE_FPU_PRESERVE,&pp,&device));
    for(unsigned run=0;run<2;++run){
        if(run){pp.BackBufferWidth=96;pp.BackBufferHeight=80;ok(device->Reset(&pp));}
        ok(device->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(51,102,153),1,0));
        IDirect3DSurface9 *back=nullptr,*staging=nullptr;ok(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back));
        D3DSURFACE_DESC desc{};ok(back->GetDesc(&desc));assert(desc.Format==D3DFMT_X8R8G8B8);
        ok(device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        ok(device->GetRenderTargetData(back,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        const auto* pixel=static_cast<const unsigned char*>(lock.pBits);assert(pixel[0]==153&&pixel[1]==102&&pixel[2]==51);
        ok(staging->UnlockRect());staging->Release();back->Release();
        ok(device->BeginScene());ok(device->EndScene());ok(device->Present(nullptr,nullptr,nullptr,nullptr));
        ok(device->TestCooperativeLevel());
    }
    device->Release();api->Release();DestroyWindow(window);
    std::puts("PASS GPU: original SDR backbuffer bytes, triple buffering, Present, Reset/resize, cooperative level");
}
