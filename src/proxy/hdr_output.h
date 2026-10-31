#pragma once
#include <windows.h>
#include <dxgi1_6.h>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <vector>
#include <cstdint>
#include <sstream>
#include "hdr_presentation.h"

namespace NorthlightHdr {
struct OutputInfo {
    HMONITOR monitor=nullptr;bool available=false,hdr=false;
    float minimum=0,maximum=0,fullFrame=0;
    wchar_t device[32]{};
    std::string profile,name;
};
inline std::string outputUtf8(const wchar_t* value){int n=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);if(n<=1)return "";std::string out(n,0);WideCharToMultiByte(CP_UTF8,0,value,-1,out.data(),n,nullptr,nullptr);out.pop_back();return out;}
inline void outputIdentity(OutputInfo& output){
    UINT32 pathsCount=0,modesCount=0;std::wstring identity;
    if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pathsCount,&modesCount)==ERROR_SUCCESS){
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathsCount);std::vector<DISPLAYCONFIG_MODE_INFO> modes(modesCount);
        if(QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pathsCount,paths.data(),&modesCount,modes.data(),nullptr)==ERROR_SUCCESS)for(unsigned i=0;i<pathsCount;++i){
            const auto& path=paths[i];DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof source,path.sourceInfo.adapterId,path.sourceInfo.id};
            if(DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS||std::wcscmp(source.viewGdiDeviceName,output.device))continue;
            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};target.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,sizeof target,path.targetInfo.adapterId,path.targetInfo.id};
            if(DisplayConfigGetDeviceInfo(&target.header)==ERROR_SUCCESS){identity=target.monitorDevicePath;output.name=outputUtf8(target.monitorFriendlyDeviceName);}break;
        }
    }
    // No transient GDI ordinal is persisted when a stable target identity is unavailable.
    if(!identity.empty()){std::uint64_t hash=14695981039346656037ull;for(wchar_t c:identity){hash^=std::towlower(c);hash*=1099511628211ull;}std::ostringstream out;out<<std::hex<<hash;output.profile=out.str();}
    if(output.name.empty())output.name=outputUtf8(output.device);
}

// Windows output state is independent of Vulkan surface-format support: a
// driver can expose scRGB while Windows is currently presenting SDR.
class OutputMonitor {
    HMODULE module=nullptr;IDXGIFactory1* factory=nullptr;
    HMONITOR candidate=nullptr;ULONGLONG candidateSince=0,lastCheck=0;
    using CreateFactory=HRESULT(WINAPI*)(REFIID,void**);
public:
    OutputInfo info;
    OutputMonitor()=default;OutputMonitor(const OutputMonitor&)=delete;
    OutputMonitor& operator=(const OutputMonitor&)=delete;
    ~OutputMonitor(){if(factory)factory->Release();if(module)FreeLibrary(module);}
    bool sample(HWND window,bool force=false){
        const auto now=GetTickCount64();const auto selected=MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);
        if(selected!=candidate){candidate=selected;candidateSince=now;}
        const bool stale=factory&&!factory->IsCurrent();
        const bool moved=selected!=info.monitor&&now-candidateSince>=100;
        if(!force&&!stale&&!moved&&lastCheck&&now-lastCheck<250)return false;
        if(!force&&!stale&&selected!=info.monitor&&now-candidateSince<100&&info.monitor)return false;
        lastCheck=now;
        if(stale){factory->Release();factory=nullptr;}
        if(!factory){
            if(!module)module=LoadLibraryExW(L"dxgi.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
            auto create=module?reinterpret_cast<CreateFactory>(reinterpret_cast<void*>(GetProcAddress(module,"CreateDXGIFactory1"))):nullptr;
            if(create)create(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&factory));
        }
        OutputInfo next;next.monitor=selected;
        if(factory)for(UINT ai=0;!next.available;++ai){
            IDXGIAdapter1* adapter=nullptr;if(factory->EnumAdapters1(ai,&adapter)!=S_OK)break;
            for(UINT oi=0;!next.available;++oi){
                IDXGIOutput* output=nullptr;if(adapter->EnumOutputs(oi,&output)!=S_OK)break;
                DXGI_OUTPUT_DESC desc{};
                if(SUCCEEDED(output->GetDesc(&desc))&&desc.AttachedToDesktop&&desc.Monitor==selected){
                    IDXGIOutput6* advanced=nullptr;DXGI_OUTPUT_DESC1 color{};
                    if(SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6),reinterpret_cast<void**>(&advanced)))){
                        if(SUCCEEDED(advanced->GetDesc1(&color))){
                            next.available=true;next.hdr=color.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
                            next.minimum=std::isfinite(color.MinLuminance)?color.MinLuminance:0;
                            next.maximum=std::isfinite(color.MaxLuminance)?color.MaxLuminance:0;
                            next.fullFrame=std::isfinite(color.MaxFullFrameLuminance)?color.MaxFullFrameLuminance:0;
                            std::wmemcpy(next.device,color.DeviceName,32);
                        }
                        advanced->Release();
                    }
                }
                output->Release();
            }
            adapter->Release();
        }
        if(next.available)outputIdentity(next);
        const bool changed=next.monitor!=info.monitor||next.profile!=info.profile||next.available!=info.available||next.hdr!=info.hdr||
            next.minimum!=info.minimum||next.maximum!=info.maximum||next.fullFrame!=info.fullFrame;
        info=next;return changed;
    }
};

class OutputChain {
    IDirect3DSwapChain9* chain=nullptr;
public:
    OutputChain()=default;OutputChain(const OutputChain&)=delete;
    OutputChain& operator=(const OutputChain&)=delete;
    ~OutputChain(){reset();}
    void reset(){if(chain){chain->Release();chain=nullptr;}}
    bool exists()const{return chain!=nullptr;}
    IDirect3DSwapChain9* get()const{return chain;}
    HRESULT create(IDirect3DDevice9* calls,Presentation& state,IDirect3DSurface9** back){
        *back=nullptr;reset();state.active=false;
        state.reason=state.ineligible();if(state.reason)return state.lastStatus=S_FALSE;
        auto physical=state.logical;physical.BackBufferFormat=D3DFMT_A16B16G16R16F;physical.EnableAutoDepthStencil=FALSE;
        HRESULT hr=calls->CreateAdditionalSwapChain(&physical,&chain);
        SwapExtension* ext=nullptr;
        if(SUCCEEDED(hr))hr=chain->QueryInterface(SwapIid,reinterpret_cast<void**>(&ext));
        if(SUCCEEDED(hr)&&ext){
            if(!state.formatsPrepared)ext->UnlockAdditionalFormats();
            hr=ext->CheckColorSpaceSupport(ScRgb)?ext->SetColorSpace(ScRgb):D3DERR_NOTAVAILABLE;
            ext->Release();
        }else if(SUCCEEDED(hr))hr=E_NOINTERFACE;
        if(SUCCEEDED(hr))hr=chain->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,back);
        if(FAILED(hr)){reset();state.reason="hdr-additional-swapchain-failed";state.lastStatus=hr;return S_FALSE;}
        state.active=true;state.reason="active";state.lastStatus=S_OK;return S_OK;
    }
    HRESULT present(const RECT* source,const RECT* destination,HWND window,const RGNDATA* dirty,DWORD flags=0){
        return chain?chain->Present(source,destination,window,dirty,flags):D3DERR_INVALIDCALL;
    }
};
}
