#pragma once
#include <d3d9.h>

// Current-frame visibility at liquid receivers, separate from the seabed's light.
// The world renderer supplies its existing source maps/constants and draws each
// source into one channel. Contents are never reused across frames. The enclosing
// world pass owns render-state restoration; this class owns only GPU resources.
class WaterSurfaceVisibility {
    IDirect3DTexture9* texture_ = nullptr;
    IDirect3DSurface9* target_ = nullptr;
    IDirect3DPixelShader9* shader_ = nullptr;
    UINT width_ = 0, height_ = 0;
    bool valid_ = false;
    template<class T> static void release(T*& p) { if(p) p->Release(); p=nullptr; }
public:
    WaterSurfaceVisibility() = default;
    WaterSurfaceVisibility(const WaterSurfaceVisibility&) = delete;
    WaterSurfaceVisibility& operator=(const WaterSurfaceVisibility&) = delete;
    ~WaterSurfaceVisibility() { reset(); }
    void reset() {
        release(target_); release(texture_); release(shader_);
        width_=height_=0; valid_=false;
    }
    // Clear both sources to unoccluded. Disabled/inactive sources keep 1;
    // their intensity is independently supplied by the environment contract.
    bool begin(IDirect3DDevice9* device, UINT width, UINT height, const DWORD* shader) {
        valid_=false;
        if(width_!=width || height_!=height) reset();
        if(!shader_ && FAILED(device->CreatePixelShader(shader,&shader_))) return false;
        if(!texture_) {
            if(FAILED(device->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_G16R16F,
                                           D3DPOOL_DEFAULT,&texture_,nullptr)) ||
               FAILED(texture_->GetSurfaceLevel(0,&target_))) { reset(); return false; }
            width_=width; height_=height;
        }
        return SUCCEEDED(device->SetRenderTarget(0,target_)) &&
               SUCCEEDED(device->SetPixelShader(shader_)) &&
               SUCCEEDED(device->SetRenderState(D3DRS_COLORWRITEENABLE,15)) &&
               SUCCEEDED(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE)) &&
               SUCCEEDED(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0));
    }
    void complete(bool success) { valid_=success; }
    IDirect3DTexture9* texture() const { return valid_?texture_:nullptr; }
};
