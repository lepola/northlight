// Standalone presentation experiment. The game's files and process are untouched.
#include <windows.h>
#include <d3d9.h>
#include <dxgi1_6.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "hdr_output.h"

struct Display { HMONITOR monitor; RECT rect; unsigned colorSpace; std::wstring name; };
static std::vector<Display> displays;
static unsigned currentSpace(unsigned index) {
    IDXGIFactory1* factory=nullptr;
    if(FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&factory))))return ~0u;
    unsigned space=~0u;
    for(UINT ai=0;;++ai){
        IDXGIAdapter1* adapter=nullptr;if(factory->EnumAdapters1(ai,&adapter)!=S_OK)break;
        for(UINT oi=0;;++oi){
            IDXGIOutput* output=nullptr;if(adapter->EnumOutputs(oi,&output)!=S_OK)break;
            IDXGIOutput6* advanced=nullptr;DXGI_OUTPUT_DESC1 desc{};
            if(SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6),reinterpret_cast<void**>(&advanced)))){
                if(SUCCEEDED(advanced->GetDesc1(&desc))&&displays[index].name==desc.DeviceName)space=unsigned(desc.ColorSpace);
                advanced->Release();
            }
            output->Release();
        }
        adapter->Release();
    }
    factory->Release();return space;
}
static LONG setHdr(unsigned index,bool enabled) {
    UINT32 pathCount=0,modeCount=0;
    LONG status=GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathCount,&modeCount);
    if(status!=ERROR_SUCCESS)return status;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    status=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathCount,paths.data(),&modeCount,modes.data(),nullptr);
    if(status!=ERROR_SUCCESS)return status;
    for(UINT32 i=0;i<pathCount;++i){
        const auto& path=paths[i];DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),path.sourceInfo.adapterId,path.sourceInfo.id};
        if(DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS||displays[index].name!=source.viewGdiDeviceName)continue;
        // Windows 11 separates HDR from wide-gamut SDR. Packet type 16 changes
        // only HDR, leaving the user's WCG setting alone.
        struct HdrPacket {DISPLAYCONFIG_DEVICE_INFO_HEADER header;UINT32 enabled;};
        HdrPacket packet{{static_cast<DISPLAYCONFIG_DEVICE_INFO_TYPE>(16),sizeof(packet),path.targetInfo.adapterId,path.targetInfo.id},UINT32(enabled)};
        status=DisplayConfigSetDeviceInfo(&packet.header);
        if(status==ERROR_INVALID_PARAMETER||status==ERROR_NOT_SUPPORTED){
            DISPLAYCONFIG_SET_ADVANCED_COLOR_STATE legacy{};
            legacy.header={DISPLAYCONFIG_DEVICE_INFO_SET_ADVANCED_COLOR_STATE,sizeof(legacy),path.targetInfo.adapterId,path.targetInfo.id};
            legacy.enableAdvancedColor=enabled;status=DisplayConfigSetDeviceInfo(&legacy.header);
        }
        std::printf("{\"event\":\"set-windows-hdr\",\"display\":%u,\"enabled\":%s,\"status\":%ld}\n",index,enabled?"true":"false",status);
        if(status==ERROR_SUCCESS){
            const ULONGLONG until=GetTickCount64()+3000;
            unsigned space;
            do{space=currentSpace(index);if((space==12)==enabled&&space!=~0u)break;Sleep(25);}while(GetTickCount64()<until);
            std::printf("{\"event\":\"windows-hdr-confirmed\",\"enabled\":%s,\"color_space\":%u}\n",enabled?"true":"false",space);
            if(space==~0u||(space==12)!=enabled)return ERROR_TIMEOUT;
        }
        return status;
    }
    return ERROR_NOT_FOUND;
}
static std::string utf8(const wchar_t* text) {
    int count=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
    std::string result(count,'\0');
    WideCharToMultiByte(CP_UTF8,0,text,-1,result.data(),count,nullptr,nullptr);
    result.pop_back();
    std::string escaped;for(char c:result){if(c=='"'||c=='\\')escaped+='\\';escaped+=c;}return escaped;
}
static void inventory() {
    IDXGIFactory1* factory=nullptr;
    HRESULT hr=CreateDXGIFactory1(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&factory));
    if(FAILED(hr)){std::printf("{\"event\":\"inventory-failed\",\"hr\":%lu}\n",ULONG(hr));return;}
    for(UINT adapterIndex=0;;++adapterIndex){
        IDXGIAdapter1* adapter=nullptr;if(factory->EnumAdapters1(adapterIndex,&adapter)!=S_OK)break;
        DXGI_ADAPTER_DESC1 ad{};adapter->GetDesc1(&ad);
        std::printf("{\"event\":\"adapter\",\"name\":\"%s\",\"vendor\":%u}\n",utf8(ad.Description).c_str(),ad.VendorId);
        for(UINT index=0;;++index){
            IDXGIOutput* output=nullptr;if(adapter->EnumOutputs(index,&output)!=S_OK)break;
            DXGI_OUTPUT_DESC desc{};output->GetDesc(&desc);IDXGIOutput6* advanced=nullptr;DXGI_OUTPUT_DESC1 color{};
            HRESULT advancedHr=output->QueryInterface(__uuidof(IDXGIOutput6),reinterpret_cast<void**>(&advanced));
            if(SUCCEEDED(advancedHr)){advancedHr=advanced->GetDesc1(&color);advanced->Release();}
            std::printf("{\"event\":\"display\",\"name\":\"%s\",\"attached\":%s,\"left\":%ld,\"top\":%ld,\"right\":%ld,\"bottom\":%ld,\"advanced_hr\":%lu,\"color_space\":%u,\"peak\":%.6g,\"full_frame\":%.6g}\n",
                utf8(desc.DeviceName).c_str(),desc.AttachedToDesktop?"true":"false",desc.DesktopCoordinates.left,desc.DesktopCoordinates.top,desc.DesktopCoordinates.right,desc.DesktopCoordinates.bottom,ULONG(advancedHr),unsigned(color.ColorSpace),color.MaxLuminance,color.MaxFullFrameLuminance);
            if(desc.AttachedToDesktop)displays.push_back({desc.Monitor,desc.DesktopCoordinates,unsigned(color.ColorSpace),desc.DeviceName});
            output->Release();
        }
        adapter->Release();
    }
    factory->Release();std::fflush(stdout);
}
static bool result(const char* operation,HRESULT hr) {
    std::printf("{\"event\":\"call\",\"operation\":\"%s\",\"hr\":%lu}\n",operation,ULONG(hr));std::fflush(stdout);return SUCCEEDED(hr);
}
static void pump() {MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
static void locate(HWND window,unsigned index) {
    const auto& r=displays[index].rect;
    SetWindowPos(window,HWND_TOP,r.left+32,r.top+32,352,224,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    pump();
    std::printf("{\"event\":\"move\",\"display\":%u,\"name\":\"%s\",\"color_space\":%u}\n",index,utf8(displays[index].name.c_str()).c_str(),displays[index].colorSpace);
}
static float half(std::uint16_t h) {
    unsigned exponent=(h>>10)&31,mantissa=h&1023;
    float value=exponent==0?std::ldexp(float(mantissa),-24):exponent==31?NAN:std::ldexp(float(1024+mantissa),int(exponent)-25);
    return h&32768?-value:value;
}
static bool checkTexture(IDirect3DDevice9* device,IDirect3DSurface9* texture,IDirect3DSurface9* staging) {
    if(!result("sentinel-copy",device->GetRenderTargetData(texture,staging)))return false;
    D3DLOCKED_RECT lock{};if(!result("sentinel-lock",staging->LockRect(&lock,nullptr,D3DLOCK_READONLY)))return false;
    DWORD pixel=0;std::memcpy(&pixel,lock.pBits,4);staging->UnlockRect();
    std::printf("{\"event\":\"sentinel\",\"pixel\":%lu,\"preserved\":%s}\n",pixel,pixel==D3DCOLOR_XRGB(49,101,154)?"true":"false");return pixel==D3DCOLOR_XRGB(49,101,154);
}
int main(int argc,char** argv) {
    setvbuf(stdout,nullptr,_IONBF,0);inventory();
    if(argc==1)return displays.empty()?2:0;
    if(argc!=4||displays.empty())return 2;
    const unsigned start=unsigned(std::strtoul(argv[2],nullptr,10));if(start>=displays.size())return 2;
    if(!std::strcmp(argv[1],"--set-hdr"))return setHdr(start,std::strcmp(argv[3],"on")==0)==ERROR_SUCCESS?0:9;
    const bool resetProbe=std::strcmp(argv[3],"reset")==0;
    const bool cycleHdr=std::strcmp(argv[3],"cycle")==0;
    const bool proxyCycle=std::strcmp(argv[3],"proxy-cycle")==0;
    HWND window=CreateWindowExW(0,L"STATIC",L"Northlight HDR lifecycle test",WS_OVERLAPPEDWINDOW,0,0,352,224,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    if(!window)return 3;locate(window,start);
    HMODULE module=LoadLibraryA(argv[1]);if(!module)return 3;
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));
    IDirect3D9* api=create?create(D3D_SDK_VERSION):nullptr;if(!api)return 3;
    const bool prepared=NorthlightHdr::unlockFormats(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=320;pp.BackBufferHeight=180;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device=nullptr;
    if(!result("create-device",api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE|D3DCREATE_MULTITHREADED,&pp,&device)))return 4;
    IDirect3DSurface9 *legacy=nullptr,*sentinel=nullptr,*staging=nullptr;IDirect3DTexture9* texture=nullptr;
    if(!result("legacy-buffer",device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&legacy))||
       !result("live-texture",device->CreateTexture(4,4,1,D3DUSAGE_RENDERTARGET,D3DFMT_X8R8G8B8,D3DPOOL_DEFAULT,&texture,nullptr))||
       !result("texture-surface",texture->GetSurfaceLevel(0,&sentinel))||
       !result("staging",device->CreateOffscreenPlainSurface(4,4,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&staging,nullptr)))return 4;
    device->ColorFill(sentinel,nullptr,D3DCOLOR_XRGB(49,101,154));
    if(!checkTexture(device,sentinel,staging))return 5;
    if(proxyCycle){
        bool valid=true;
        for(unsigned step=0;step<5&&valid;++step){
            const bool enabled=step%2!=0;
            valid=setHdr(start,enabled)==ERROR_SUCCESS;
            for(unsigned frame=0;frame<12&&valid;++frame){
                valid=result("proxy-bind-legacy",device->SetRenderTarget(0,legacy))&&
                    result("proxy-clear-world",device->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(49,101,154),1,0))&&
                    result("proxy-present",device->Present(nullptr,nullptr,nullptr,nullptr));
                pump();Sleep(30);
            }
            D3DSURFACE_DESC legacyDesc{};legacy->GetDesc(&legacyDesc);
            valid=valid&&legacyDesc.Format==D3DFMT_X8R8G8B8&&device->GetNumberOfSwapChains()==1&&checkTexture(device,sentinel,staging);
            std::printf("{\"event\":\"proxy-phase\",\"windows_hdr_enabled\":%s,\"valid\":%s}\n",enabled?"true":"false",valid?"true":"false");
        }
        std::printf("{\"event\":\"complete\",\"valid\":%s,\"live_resources_preserved\":%s,\"device_reset_used\":false}\n",valid?"true":"false",valid?"true":"false");
        legacy->Release();sentinel->Release();texture->Release();staging->Release();device->Release();api->Release();FreeLibrary(module);DestroyWindow(window);return valid?0:8;
    }
    if(resetProbe){
        NorthlightHdr::SwapExtension* ext=nullptr;if(SUCCEEDED(NorthlightHdr::extension(device,&ext))){ext->UnlockAdditionalFormats();ext->Release();}
        auto actual=pp;actual.BackBufferFormat=D3DFMT_A16B16G16R16F;
        HRESULT reset=device->Reset(&actual);result("reset-with-live-default-pool-and-backbuffer",reset);
        std::printf("{\"event\":\"reset-probe\",\"reset_succeeded\":%s}\n",SUCCEEDED(reset)?"true":"false");
        legacy->Release();sentinel->Release();texture->Release();staging->Release();
        result("recover-reset",device->Reset(&pp));device->Release();api->Release();FreeLibrary(module);DestroyWindow(window);return 0;
    }
    // An additional chain can be created while the client keeps its buffers and
    // default-pool textures alive. DXVK 2 unlocks formats on this chain itself.
    NorthlightHdr::Presentation state;state.logical=pp;state.stream=state.dxvk=state.settings.requested=true;state.formatsPrepared=prepared;
    NorthlightHdr::OutputChain owner;IDirect3DSurface9* back=nullptr;
    if(!result("create-additional-fp16",owner.create(device,state,&back))||!state.active||!back)return 6;
    IDirect3DSwapChain9* chain=owner.get();
    NorthlightHdr::OutputMonitor observer;observer.sample(window,true);
    NorthlightHdr::SwapExtension* ext=nullptr;
    if(!result("additional-hdr-interface",chain->QueryInterface(NorthlightHdr::SwapIid,reinterpret_cast<void**>(&ext))))return 6;
    IDirect3DSurface9 *readback=nullptr;IDirect3DPixelShader9* shader=nullptr;
    const DWORD code[]={0xffff0200,0x02000001,0x800f0800,0xa0e40000,0x0000ffff};
    if(!result("fp16-readback",device->CreateOffscreenPlainSurface(320,180,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&readback,nullptr))||
       !result("constant-shader",device->CreatePixelShader(code,&shader)))return 6;
    D3DSURFACE_DESC desc{};back->GetDesc(&desc);
    std::printf("{\"event\":\"physical\",\"format\":%u,\"fp16\":%s}\n",unsigned(desc.Format),desc.Format==D3DFMT_A16B16G16R16F?"true":"false");
    bool valid=desc.Format==D3DFMT_A16B16G16R16F;
    for(unsigned step=0;step<(cycleHdr?5u:displays.size()*2+1)&&valid;++step){
        unsigned index=(start+step)%displays.size();locate(window,index);
        if(cycleHdr){
            valid=setHdr(start,step%2!=0)==ERROR_SUCCESS;if(!valid)break;
            // This is a hardware test settling interval, never production work.
            const ULONGLONG until=GetTickCount64()+500;while(GetTickCount64()<until){pump();Sleep(10);}
        }
        BOOL supported=ext->CheckColorSpaceSupport(NorthlightHdr::ScRgb);
        unsigned nativeSpace=currentSpace(index);
        bool hdrActive=nativeSpace==12;
        observer.sample(window,true);
        unsigned space=hdrActive&&supported?NorthlightHdr::ScRgb:0;
        valid=observer.info.available&&observer.info.hdr==hdrActive&&nativeSpace!=~0u&&(!hdrActive||supported)&&result("set-output-color-space",ext->SetColorSpace(space));
        std::printf("{\"event\":\"capability\",\"display\":%u,\"windows_hdr_enabled\":%s,\"scrgb_supported\":%s,\"space\":%u}\n",index,hdrActive?"true":"false",supported?"true":"false",space);
        device->SetRenderTarget(0,back);device->SetDepthStencilSurface(nullptr);device->SetVertexShader(nullptr);device->SetFVF(D3DFVF_XYZRHW);device->SetPixelShader(shader);
        device->SetRenderState(D3DRS_ZENABLE,FALSE);device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
        device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
        float values[]={12.5f,2.5375f,1.f,1.f};device->SetPixelShaderConstantF(0,values,1);
        D3DVIEWPORT9 viewport={0,0,320,180,0,1};device->SetViewport(&viewport);
        const float quad[][4]={{-.5f,-.5f,0,1},{319.5f,-.5f,0,1},{-.5f,179.5f,0,1},{319.5f,179.5f,0,1}};
        if(!result("begin",device->BeginScene()))return 7;
        valid=result("draw-fp16",device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof quad[0]));device->EndScene();
        if(!result("read-fp16",device->GetRenderTargetData(back,readback)))return 7;
        D3DLOCKED_RECT locked{};if(!result("lock-fp16",readback->LockRect(&locked,nullptr,D3DLOCK_READONLY)))return 7;
        std::uint16_t samples[4];std::memcpy(samples,static_cast<char*>(locked.pBits)+4*locked.Pitch+4*8,8);readback->UnlockRect();
        std::printf("{\"event\":\"headroom\",\"red\":%.8g,\"green\":%.8g,\"blue\":%.8g}\n",half(samples[0]),half(samples[1]),half(samples[2]));
        valid=valid&&std::fabs(half(samples[0])-12.5f)<.01&&std::fabs(half(samples[1])-2.5375f)<.01;
        for(unsigned frame=0;frame<3;++frame){valid=result("present-additional",chain->Present(nullptr,nullptr,nullptr,nullptr,0))&&valid;pump();}
        std::printf("{\"event\":\"capability-after-present\",\"scrgb_supported\":%s}\n",ext->CheckColorSpaceSupport(NorthlightHdr::ScRgb)?"true":"false");
        device->SetRenderTarget(0,legacy);
        valid=checkTexture(device,sentinel,staging)&&valid;
        D3DSURFACE_DESC legacyDesc{};legacy->GetDesc(&legacyDesc);valid=valid&&legacyDesc.Format==D3DFMT_X8R8G8B8;
    }
    device->SetRenderTarget(0,legacy);device->SetPixelShader(nullptr);shader->Release();readback->Release();back->Release();ext->Release();owner.reset();
    valid=result("present-original-after-additional-release",device->Present(nullptr,nullptr,nullptr,nullptr))&&valid;
    valid=checkTexture(device,sentinel,staging)&&valid;
    std::printf("{\"event\":\"complete\",\"valid\":%s,\"live_resources_preserved\":%s,\"device_reset_used\":false}\n",valid?"true":"false",valid?"true":"false");
    legacy->Release();sentinel->Release();texture->Release();staging->Release();device->Release();api->Release();FreeLibrary(module);DestroyWindow(window);return valid?0:8;
}
