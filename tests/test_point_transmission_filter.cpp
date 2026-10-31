#include <cassert>
#include <cstdio>
#include <initializer_list>
using DWORD=unsigned;
using HRESULT=int;
constexpr HRESULT S_OK=0;
constexpr DWORD D3DSAMP_MINFILTER=1,D3DSAMP_MAGFILTER=2,D3DTEXF_POINT=1;
bool FAILED(HRESULT value){return value<0;}
struct IDirect3DDevice9 {
    DWORD minFilter=3,magFilter=5;
    unsigned calls=0,fail=~0u;
    HRESULT GetSamplerState(DWORD sampler,DWORD kind,DWORD* value){
        assert(!sampler);if(calls++==fail)return -1;
        *value=kind==D3DSAMP_MINFILTER?minFilter:magFilter;return S_OK;
    }
    HRESULT SetSamplerState(DWORD sampler,DWORD kind,DWORD value){
        assert(!sampler);if(calls++==fail)return -1;
        (kind==D3DSAMP_MINFILTER?minFilter:magFilter)=value;return S_OK;
    }
};
#include "point_transmission_filter.h"
int main(){
    IDirect3DDevice9 native;
    {NorthlightPointTransmissionFilter filter;assert(filter.begin(&native,false)==S_OK);}
    assert(!native.calls&&native.minFilter==3&&native.magFilter==5);
    for(unsigned failure=0;failure<4;++failure){
        IDirect3DDevice9 device;device.fail=failure;
        {NorthlightPointTransmissionFilter filter;assert(FAILED(filter.begin(&device,true)));}
        assert(device.minFilter==3&&device.magFilter==5);
    }
    IDirect3DDevice9 success;
    {NorthlightPointTransmissionFilter filter;assert(filter.begin(&success,true)==S_OK);
        assert(success.minFilter==D3DTEXF_POINT&&success.magFilter==D3DTEXF_POINT);
        assert(filter.restore()==S_OK);assert(filter.restore()==S_OK);}
    assert(success.calls==6&&success.minFilter==3&&success.magFilter==5);
    IDirect3DDevice9 earlyReturn;
    {NorthlightPointTransmissionFilter filter;assert(filter.begin(&earlyReturn,true)==S_OK);}
    assert(earlyReturn.calls==6&&earlyReturn.minFilter==3&&earlyReturn.magFilter==5);
    for(unsigned failure:{4u,5u}){
        IDirect3DDevice9 device;device.fail=failure;
        {NorthlightPointTransmissionFilter filter;assert(filter.begin(&device,true)==S_OK);assert(FAILED(filter.restore()));}
        assert(device.calls==6); // Both restores are attempted even when the first fails.
    }
    std::puts("PASS exact sampler restoration, no native changes, failed get/set/restore and early-exit cleanup");
}
