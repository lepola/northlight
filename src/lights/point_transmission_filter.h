#pragma once

// Binary coverage masks require nearest sampling; native cutouts retain their filter.
struct NorthlightPointTransmissionFilter {
    IDirect3DDevice9* device=nullptr;
    DWORD minFilter=0,magFilter=0;
    HRESULT begin(IDirect3DDevice9* value,bool enabled){
        if(!enabled)return S_OK;
        HRESULT hr=value->GetSamplerState(0,D3DSAMP_MINFILTER,&minFilter);
        if(FAILED(hr))return hr;
        hr=value->GetSamplerState(0,D3DSAMP_MAGFILTER,&magFilter);
        if(FAILED(hr))return hr;
        device=value;
        hr=device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);
        return FAILED(hr)?hr:device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
    }
    HRESULT restore(){
        if(!device)return S_OK;
        auto* value=device;device=nullptr;
        HRESULT a=value->SetSamplerState(0,D3DSAMP_MINFILTER,minFilter);
        HRESULT b=value->SetSamplerState(0,D3DSAMP_MAGFILTER,magFilter);
        return FAILED(a)?a:b;
    }
    ~NorthlightPointTransmissionFilter(){restore();}
};
