// Exercise EVENT completion on real D3D9 backends, without the game.
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <thread>
#include "query_poll.h"
static void ok(HRESULT hr){assert(SUCCEEDED(hr));}
static std::uint64_t now(){LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return std::uint64_t(double(t.QuadPart)*1e9/double(f.QuadPart));}
struct Vertex{float x,y,z,w;DWORD color;};
int main(int argc,char** argv){
    assert(argc==2);
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightEventFlushTest";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);IDirect3DDevice9* device=nullptr;
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=2560;pp.BackBufferHeight=1440;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device));
    ok(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));ok(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    ok(device->SetRenderState(D3DRS_ZENABLE,FALSE));ok(device->SetRenderState(D3DRS_LIGHTING,FALSE));
    ok(device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
    Vertex quad[]={{0,0,0,1,0x7f4080ff},{2560,0,0,1,0x7f4080ff},{0,1440,0,1,0x7f4080ff},{2560,1440,0,1,0x7f4080ff}};
    unsigned polls[2]{},flushes[2]{},pending[2]{},trips[2]{};
    IDirect3DQuery9* query=nullptr;ok(device->CreateQuery(D3DQUERYTYPE_EVENT,&query));
    for(unsigned frame=0;frame<64;++frame){
        const unsigned mode=frame%2;ok(device->BeginScene());
        for(unsigned draw=0;draw<32+frame;++draw)ok(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));
        ok(query->Issue(D3DISSUE_END));ok(device->EndScene());
        const auto start=now();HRESULT hr=S_FALSE;BOOL value=FALSE;
        while(hr==S_FALSE){
            NorthlightStream::EventFlushCadence cadence;unsigned calls=0;++trips[mode];
            auto poll=[&]{const DWORD flags=mode?cadence.flags(now()):D3DGETDATA_FLUSH;++polls[mode];flushes[mode]+=flags!=0;
                const auto result=query->GetData(frame%4<2?&value:nullptr,frame%4<2?sizeof value:0,flags);pending[mode]+=result==S_FALSE;return result;};
            hr=NorthlightStream::waitEventQuery(poll,now,[]{std::this_thread::yield();},calls,NorthlightStream::kEventFlushWaitNs);
            assert(now()-start<10000000000ull);
        }
        assert(hr==S_OK);ok(query->GetData(&value,sizeof value,0));assert(value==TRUE);
    }
    assert(flushes[0]==polls[0]&&flushes[1]<=polls[1]);
    if(pending[1]>64)assert(flushes[1]<polls[1]);
    std::printf("EVENT_FLUSH_GPU_PASS generations=64 real_payload=TRUE legacy_polls=%u legacy_flushes=%u cadence_polls=%u cadence_flushes=%u cadence_pending=%u cadence_trips=%u\n",polls[0],flushes[0],polls[1],flushes[1],pending[1],trips[1]);
    query->Release();device->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
