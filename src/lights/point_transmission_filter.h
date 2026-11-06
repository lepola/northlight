#pragma once

// Binary coverage masks require nearest sampling; native cutouts retain their filter.
struct NorthlightPointTransmissionFilter {
    IDirect3DDevice9* device=nullptr;
    DWORD minFilter=0,magFilter=0,changed=0;
    HRESULT begin(IDirect3DDevice9* value,bool enabled){
        if(!enabled)return S_OK;
        HRESULT hr=value->GetSamplerState(0,D3DSAMP_MINFILTER,&minFilter);
        if(FAILED(hr))return hr;
        hr=value->GetSamplerState(0,D3DSAMP_MAGFILTER,&magFilter);
        if(FAILED(hr))return hr;
        device=value;
        if(minFilter!=D3DTEXF_POINT){changed|=1;hr=device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);if(FAILED(hr))return hr;}
        if(magFilter!=D3DTEXF_POINT){changed|=2;return device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);}
        return S_OK;
    }
    HRESULT restore(){
        if(!device)return S_OK;
        auto* value=device;device=nullptr;
        HRESULT a=changed&1?value->SetSamplerState(0,D3DSAMP_MINFILTER,minFilter):S_OK;
        HRESULT b=changed&2?value->SetSamplerState(0,D3DSAMP_MAGFILTER,magFilter):S_OK;changed=0;
        return FAILED(a)?a:b;
    }
    ~NorthlightPointTransmissionFilter(){restore();}
};
