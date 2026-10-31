#undef NDEBUG
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <vector>
#include "forwarders.h"
#include "hdr_presentation.h"

struct SwapSupport:NorthlightHdr::SwapExtension {
    unsigned references=1;std::vector<unsigned> spaces;bool failColor=false;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**) override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++references;}
    ULONG STDMETHODCALLTYPE Release() override{return --references;}
    BOOL STDMETHODCALLTYPE CheckColorSpaceSupport(unsigned) override{return TRUE;}
    HRESULT STDMETHODCALLTYPE SetColorSpace(unsigned space) override{spaces.push_back(space);return failColor&&space?D3DERR_NOTAVAILABLE:S_OK;}
    HRESULT STDMETHODCALLTYPE SetHDRMetaData(const void*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE GetCurrentOutputDesc(NorthlightHdr::OutputDesc*) override{return S_OK;}
    void STDMETHODCALLTYPE UnlockAdditionalFormats() override{}
};
struct Swap:ForwardIDirect3DSwapChain9 {
    SwapSupport support;unsigned references=1;Swap():ForwardIDirect3DSwapChain9(nullptr){}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++references;}
    ULONG STDMETHODCALLTYPE Release() override{return --references;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override{
        *out=nullptr;if(id!=NorthlightHdr::SwapIid)return E_NOINTERFACE;
        support.AddRef();*out=&support;return S_OK;
    }
};
struct Device:ForwardIDirect3DDevice9 {
    Swap swap;std::vector<D3DPRESENT_PARAMETERS> resets;std::vector<HRESULT> results;bool normalize=false;
    Device():ForwardIDirect3DDevice9(nullptr){}
    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT n,IDirect3DSwapChain9** out) override{assert(n==0);swap.AddRef();*out=&swap;return S_OK;}
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* p) override{
        assert(p);resets.push_back(*p);const auto hr=resets.size()<=results.size()?results[resets.size()-1]:S_OK;
        if(SUCCEEDED(hr)&&normalize){p->BackBufferCount=1;p->MultiSampleQuality=3;}
        return hr;
    }
    void balanced()const{assert(swap.references==1&&swap.support.references==1);}
};
static NorthlightHdr::Presentation configured(D3DPRESENT_PARAMETERS& pp){
    pp={};pp.BackBufferWidth=800;pp.BackBufferHeight=450;pp.BackBufferCount=2;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    pp.hDeviceWindow=reinterpret_cast<HWND>(1);pp.Windowed=TRUE;pp.MultiSampleType=D3DMULTISAMPLE_2_SAMPLES;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    NorthlightHdr::Presentation p;p.settings.requested=true;p.active=p.stream=p.dxvk=p.formatsPrepared=true;p.logical=pp;return p;
}
int main(){
    unsigned cases=0;
    for(const auto sample:{D3DMULTISAMPLE_NONE,D3DMULTISAMPLE_2_SAMPLES,D3DMULTISAMPLE_4_SAMPLES})for(bool normalize:{false,true}){
        Device d;D3DPRESENT_PARAMETERS pp;auto p=configured(pp);pp.MultiSampleType=sample;d.normalize=normalize;
        assert(p.reset(&d,&d,&pp)==S_OK&&p.active&&d.resets.size()==1);
        const auto& physical=d.resets[0];assert(physical.BackBufferFormat==D3DFMT_A16B16G16R16F&&physical.MultiSampleType==sample);
        assert(physical.BackBufferWidth==800&&physical.BackBufferHeight==450&&physical.PresentationInterval==pp.PresentationInterval);
        assert(pp.BackBufferFormat==D3DFMT_X8R8G8B8&&p.logical.BackBufferFormat==pp.BackBufferFormat&&p.logical.MultiSampleType==sample);
        assert(pp.BackBufferCount==(normalize?1u:2u)&&pp.MultiSampleQuality==(normalize?3u:0u));
        assert(d.swap.support.spaces==std::vector<unsigned>{NorthlightHdr::ScRgb});d.balanced();++cases;
    }
    for(unsigned failure=0;failure<3;++failure){
        Device d;D3DPRESENT_PARAMETERS pp;auto p=configured(pp);
        if(failure==1)d.swap.support.failColor=true;else d.results={D3DERR_DEVICELOST,failure==2?D3DERR_DEVICELOST:S_OK};
        const auto hr=p.reset(&d,&d,&pp);assert(hr==(failure==2?D3DERR_DEVICELOST:S_OK)&&!p.active);
        assert(d.resets.size()==2&&d.resets[0].BackBufferFormat==D3DFMT_A16B16G16R16F&&d.resets[1].BackBufferFormat==D3DFMT_X8R8G8B8);
        assert(FAILED(p.lastStatus)&&pp.BackBufferFormat==D3DFMT_X8R8G8B8);d.balanced();++cases;
    }
    for(unsigned mode=0;mode<4;++mode){
        Device d;D3DPRESENT_PARAMETERS pp;auto p=configured(pp);
        if(mode==0)p.active=false;if(mode==1)p.settings.requested=false;if(mode==2)pp.Windowed=FALSE;if(mode==3)pp.BackBufferWidth=0;
        assert(p.reset(&d,&d,&pp)==S_OK);assert(d.resets[0].BackBufferFormat==D3DFMT_X8R8G8B8);
        assert(d.resets.size()==(mode==0||mode==3?2u:1u));assert(p.active==(mode==0||mode==3));d.balanced();++cases;
    }
    {Device d;D3DPRESENT_PARAMETERS pp;auto p=configured(pp);assert(p.reset(&d,&d,nullptr)==D3DERR_INVALIDCALL&&p.active&&d.resets.empty());++cases;}
    std::printf("HDR_RESET_PASS cases=%u activeHdrResets=1 failureRollback=1 msaa=0/2/4 parametersPreserved=1 referencesBalanced=1\n",cases);
}
