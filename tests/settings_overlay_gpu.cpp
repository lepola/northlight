// Render the actual settings window into a private SDR target.
#define main unused_composite_distance_main
#include "composite_distance_gpu.cpp"
#undef main
#include "forwarders.h"
#include "saved_state.h"
#include "settings_service.h"
#include "settings_overlay.h"
#include "memory_profile_snapshot.h"
#include "imgui.h"
#include "imgui_internal.h"

struct OverlayDevice:ForwardIDirect3DDevice9{
    IDirect3DSurface9* target=nullptr;unsigned stateBlockCreates=0;
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE type,IDirect3DStateBlock9** out)override{
        if(++stateBlockCreates>1){*out=nullptr;return E_OUTOFMEMORY;}
        return real->CreateStateBlock(type,out);
    }
    explicit OverlayDevice(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT chain,UINT index,D3DBACKBUFFER_TYPE type,IDirect3DSurface9** out)override{
        if(target&&chain==0&&index==0){*out=target;target->AddRef();return S_OK;}
        return real->GetBackBuffer(chain,index,type,out);
    }
};
int main(int argc,char** argv){
    assert(argc==3);constexpr unsigned W=1280,H=800;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightSettingsOverlayOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,W,H,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* real=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&real));
    OverlayDevice device(real);ok(real->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&device.target,nullptr));
    IDirect3DSurface9* readback=nullptr;ok(real->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    {std::ofstream config(std::string(argv[2])+"/northlight-renderer.ini");config<<"[Renderer]\nSettingsOverlay=1\n";}
    auto service=std::make_shared<NorthlightSettings::Service>(argv[2]);
    assert(service->requestSnapshot());
    for(unsigned attempt=0;attempt<500&&!service->snapshot();++attempt)Sleep(10);
    assert(service->snapshot());
    auto overlay=NorthlightOverlay::create(&device,true,"F11",service,[](const char* text){std::puts(text);});assert(overlay&&!overlay->visible());
    SendMessageW(window,WM_KEYDOWN,VK_F11,0);SendMessageW(window,WM_KEYUP,VK_F11,0);assert(overlay->visible());
    auto draw=[&](float){
        ok(real->ColorFill(device.target,nullptr,0xff000000));device.stateBlockCreates=0;
        ok(real->SetRenderState(D3DRS_CULLMODE,D3DCULL_CW));ok(real->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
        D3DMATRIX transform{};for(unsigned i=0;i<4;++i)transform.m[i][i]=float(i+1);transform.m[3][0]=3.25f;
        for(auto mode:{D3DTS_WORLD,D3DTS_VIEW,D3DTS_PROJECTION})ok(real->SetTransform(mode,&transform));
        {SavedState saved(&device);assert(saved.ok);overlay->draw();}
        assert(device.stateBlockCreates==1);DWORD value=0;ok(real->GetRenderState(D3DRS_CULLMODE,&value));assert(value==D3DCULL_CW);ok(real->GetRenderState(D3DRS_ALPHABLENDENABLE,&value));assert(value==FALSE);
        for(auto mode:{D3DTS_WORLD,D3DTS_VIEW,D3DTS_PROJECTION}){D3DMATRIX restored{};ok(real->GetTransform(mode,&restored));assert(!std::memcmp(&transform,&restored,sizeof transform));}
    };
    draw(0);draw(0);
    auto* parent=ImGui::FindWindowByName("Northlight settings###NorthlightSettings");assert(parent);
    ImGuiWindow* nav=nullptr;
    for(auto* child:parent->DC.ChildWindows)if(child->ChildId==parent->GetID("nav"))nav=child;
    assert(nav);
    const char* pages[]={"Materials & parallax","Lighting & AO","Renderer & HDR","Diagnostics","Diagnostics","Diagnostics"};
    for(unsigned page=0;page<sizeof(pages)/sizeof(pages[0]);++page){
      if(page==4){
        // Synthetic presentation data in the private test process only. The
        // production menu still consumes actual renderer-owned snapshots.
        using namespace NorthlightMemoryProfile;constexpr uint64_t MiB=1024*1024;
        NorthlightMemoryFootprint::Counter builder,solver;builder.add(NorthlightMemoryFootprint::Vertices,8*MiB);builder.add(NorthlightMemoryFootprint::Acceleration,2*MiB);solver.add(NorthlightMemoryFootprint::Probes,4*MiB);
        store().cpu(true,"offline fixture",123,builder);store().cpu(false,"offline fixture",123,solver);
        Admission admission;admission.valid=true;admission.available=740*MiB;admission.largest=615*MiB;admission.entryRequired=768*MiB;admission.reserve=512*MiB;admission.entryHeadroom=256*MiB;admission.minimumAvailable=738*MiB;admission.maximumKnownCapacity=10*MiB;std::strcpy(admission.stage,"build-deferred (offline fixture)");store().admission(admission);
        Gpu gpu;gpu.managed=64*MiB;gpu.defaults=128*MiB;gpu.system=2*MiB;gpu.complete=true;gpu.staticManagedLedger=32*MiB;gpu.replayDefaultLedger=16*MiB;store().gpu(gpu);
        Sleep(1100); // the menu reads cached snapshots at most once per second
      }
      ImGui::ActivateItemByID(nav->GetID(pages[page]));draw(0);draw(0);
      if(page==5){
        ImGuiWindow* content=nullptr;for(auto* child:parent->DC.ChildWindows)if(child!=nav)content=child;assert(content);
        content->StateStorage.SetInt(content->GetID("CPU memory breakdown"),1);draw(0);draw(0);assert(content->ScrollMax.y>0);
      }
      float peaks[3]{};unsigned changed[3]{};
      for(unsigned mode=0;mode<1;++mode){
        const float white=mode==0?0:mode==1?160:320;
        draw(white);
        ok(real->GetRenderTargetData(device.target,readback));D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::ofstream image(std::string(argv[2])+"/overlay-"+std::to_string(page)+"-"+std::to_string(unsigned(white))+".fgr",std::ios::binary);const uint32_t header[]={0x31524746,W,H,D3DFMT_A16B16G16R16F,8};image.write(reinterpret_cast<const char*>(header),sizeof header);
        for(unsigned y=0;y<H;++y){auto* row=static_cast<const unsigned char*>(lock.pBits)+y*lock.Pitch;image.write(reinterpret_cast<const char*>(row),W*8);
            for(unsigned x=0;x<W;++x){float peak=0;for(unsigned k=0;k<3;++k){std::uint16_t h;std::memcpy(&h,row+x*8+k*2,2);const float value=half(h);assert(std::isfinite(value)&&value>=0);peak=std::max(peak,value);}peaks[mode]=std::max(peaks[mode],peak);changed[mode]+=peak>0;}
        }ok(readback->UnlockRect());assert(changed[mode]>10000&&changed[mode]<W*H);
    }
    assert(peaks[0]>.5f&&peaks[0]<=1);
      std::printf("OVERLAY_GPU_PASS page=%s sdrPeak=%g pixels=%u actualPanel=1 snapshotLoaded=1 privateTarget=1 windowsHDROutput=0\n",pages[page],peaks[0],changed[0]);
    }
    overlay.reset();service.reset();readback->Release();device.target->Release();real->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
