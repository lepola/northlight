#pragma once
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <string>
#include "hdr_settings_contract.h"

namespace NorthlightHdr {
inline constexpr GUID SwapIid={0x13776e93,0x4aa9,0x430a,{0xa4,0xec,0xfe,0x9e,0x28,0x11,0x81,0xd5}};
inline constexpr GUID FormatsIid={0x65b55086,0xe3e3,0x4c3e,{0xb3,0xa0,0x86,0x81,0x5c,0xce,0x2c,0x4c}};
inline constexpr unsigned ScRgb=1000104002;
struct OutputDesc {float red[2],green[2],blue[2],white[2],minimum,maximum,fullFrame;};
struct SwapExtension: IUnknown {
    virtual BOOL STDMETHODCALLTYPE CheckColorSpaceSupport(unsigned)=0;
    virtual HRESULT STDMETHODCALLTYPE SetColorSpace(unsigned)=0;
    virtual HRESULT STDMETHODCALLTYPE SetHDRMetaData(const void*)=0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentOutputDesc(OutputDesc*)=0;
    virtual void STDMETHODCALLTYPE UnlockAdditionalFormats()=0;
};
struct FormatsExtension: IUnknown {virtual void STDMETHODCALLTYPE UnlockAdditionalFormats()=0;};
// DXVK 3 snapshots this flag when constructing a device, before any Reset.
inline bool unlockFormats(IDirect3D9* api){
    FormatsExtension* formats=nullptr;
    if(FAILED(api->QueryInterface(FormatsIid,reinterpret_cast<void**>(&formats)))||!formats)return false;
    formats->UnlockAdditionalFormats();formats->Release();return true;
}
inline HRESULT extension(IDirect3DDevice9* d,SwapExtension** out){
    *out=nullptr;IDirect3DSwapChain9* sc=nullptr;HRESULT hr=d->GetSwapChain(0,&sc);
    if(SUCCEEDED(hr)&&sc){hr=sc->QueryInterface(SwapIid,reinterpret_cast<void**>(out));sc->Release();}return hr;
}
inline HRESULT colorSpace(IDirect3DDevice9* d,unsigned space){
    SwapExtension* ext=nullptr;HRESULT hr=extension(d,&ext);if(SUCCEEDED(hr)&&ext){hr=ext->SetColorSpace(space);ext->Release();}return hr;
}
struct Settings {
    bool requested=false;float paperWhite=203,uiWhite=160,maxNits=1000,contrast=1,exposure=0;
    static Settings load(const wchar_t* root){
        const std::wstring ini=std::wstring(root)+L"northlight-renderer.ini";Settings value;
        using namespace NorthlightHdrSettingsContract;
        auto readKey=[&](unsigned i){const auto& key=Keys[i];std::string name=key.name;std::wstring wide(name.begin(),name.end());wchar_t text[64]{};GetPrivateProfileStringW(L"Renderer",wide.c_str(),L"",text,64,ini.c_str());std::wstring w=text;return read(key,std::string(w.begin(),w.end()));};
        value.requested=readKey(0)==1;value.paperWhite=readKey(1);value.uiWhite=readKey(2);value.maxNits=readKey(3);value.contrast=readKey(4);value.exposure=readKey(5);
        return value;
    }
};
struct Presentation {
    Settings settings;D3DPRESENT_PARAMETERS logical{};bool active=false,stream=false,dxvk=false,formatsPrepared=false;
    HRESULT lastStatus=S_OK;
    const char* reason="disabled";
    const char* ineligible()const{
        if(!settings.requested)return "disabled";if(!stream)return "command-stream-disabled";if(!dxvk)return "backend-not-dxvk";
        if(!logical.hDeviceWindow)return "missing-window";if(!logical.Windowed)return "windowed-mode-required";
        if(logical.BackBufferFormat!=D3DFMT_X8R8G8B8&&logical.BackBufferFormat!=D3DFMT_A8R8G8B8)return "unsupported-legacy-format";
        return nullptr;
    }
    bool eligible()const{return ineligible()==nullptr;}
    HRESULT activate(IDirect3DDevice9* realDevice,IDirect3DDevice9* calls){
        active=false;reason=ineligible();if(reason)return lastStatus=S_FALSE;
        SwapExtension* ext=nullptr;HRESULT hr=extension(realDevice,&ext);if(FAILED(hr)||!ext){reason="dxvk-hdr-interface-missing";lastStatus=FAILED(hr)?hr:E_NOINTERFACE;return S_FALSE;}
        if(!ext->CheckColorSpaceSupport(ScRgb)){ext->Release();reason="host-scrgb-unavailable";lastStatus=D3DERR_NOTAVAILABLE;return S_FALSE;}
        if(!formatsPrepared)ext->UnlockAdditionalFormats(); // DXVK 2: swapchain-local flag.
        ext->Release();
        D3DPRESENT_PARAMETERS actual=logical;actual.BackBufferFormat=D3DFMT_A16B16G16R16F;
        hr=calls->Reset(&actual);
        if(SUCCEEDED(hr))hr=colorSpace(realDevice,ScRgb);
        if(SUCCEEDED(hr)){active=true;reason="active";return lastStatus=S_OK;}
        reason="dxvk-hdr-reset-failed";lastStatus=hr;actual=logical;HRESULT rollback=calls->Reset(&actual);
        if(SUCCEEDED(rollback))colorSpace(realDevice,0);
        return FAILED(rollback)?rollback:S_FALSE;
    }
    HRESULT reset(IDirect3DDevice9* realDevice,IDirect3DDevice9* calls,D3DPRESENT_PARAMETERS* game){
        const bool wasActive=active;active=false;
        HRESULT hr=calls->Reset(game);if(FAILED(hr))return hr;
        logical=*game;if(wasActive)colorSpace(realDevice,0);
        if(!settings.requested)return hr;
        const HRESULT enabled=activate(realDevice,calls);return FAILED(enabled)?enabled:hr;
    }
};
}
