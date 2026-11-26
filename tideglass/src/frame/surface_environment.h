#pragma once
#include "water_environment.h"
#include "frame/frame_lifetime.h"
#include <d3d9.h>
#include <cmath>

namespace water {
// One current-frame resource, retained and retired on the existing device owner.
// The renderer borrows this stable record. No completed-scene memory is eligible.
class SurfaceEnvironment {
public:
    ~SurfaceEnvironment(){Clear();}
    SurfaceEnvironment()=default;
    SurfaceEnvironment(const SurfaceEnvironment&)=delete;
    SurfaceEnvironment& operator=(const SurfaceEnvironment&)=delete;
    void Clear(){if(m_background)m_background->Release();m_background=nullptr;m_values={};m_valid=false;m_sealed=false;m_stamp={};m_fallback=WaterEnvironmentNotProvided;}
    void Seal(){m_sealed=true;}
    bool Sealed()const{return m_sealed;}
    bool Available()const{return m_valid;}
    FrameStamp Source()const{return m_stamp;}
    unsigned Fallback()const{return m_fallback;}
    const WaterEnvironmentValues& Values()const{return m_values;}
    IDirect3DTexture9* Background()const{return m_background;}
    bool Set(IDirect3DDevice9* device,FrameStamp stamp,const WaterEnvironmentValues& values,IDirect3DTexture9* background){
        if(m_sealed)return false;
        Clear();m_fallback=WaterEnvironmentInvalidInput;
        if(!device||!stamp.session||!stamp.generation||!stamp.frame||
           values.policy>WaterEnvironmentCoreOwnsDistance||values.encoding!=WaterEnvironmentSurfaceCompositeRGB||
           values.paletteKnown!=1||values.backgroundContent&~127u||values.light.moon>1||
           !std::isfinite(values.referenceDistance)||values.referenceDistance<=0||
           !std::isfinite(values.transmissionAtReference)||values.transmissionAtReference<=0||values.transmissionAtReference>1||
           !std::isfinite(values.light.visibility)||values.light.visibility<0||values.light.visibility>1||
           !std::isfinite(values.light.angularRadius)||values.light.angularRadius<=0||values.light.angularRadius>.5f)return false;
        const bool glare=(values.backgroundContent&(WaterBackgroundSunGlare|WaterBackgroundMoonGlare))!=0;
        const uint32_t complete=15u|WaterBackgroundCompleteCoverage;
        if(glare&&(!background||(values.backgroundContent&complete)!=complete))return false;
        float length=0;
        for(unsigned i=0;i<3;++i){
            if(!std::isfinite(values.light.direction[i])||!std::isfinite(values.light.colour[i])||values.light.colour[i]<0||values.light.colour[i]>16)return false;
            length+=values.light.direction[i]*values.light.direction[i];
        }
        if(!std::isfinite(length)||std::fabs(length-1)>1e-3f)return false;
        m_values=values;m_stamp=stamp;
        if(values.policy==WaterEnvironmentHostRetainsDistance){m_fallback=WaterEnvironmentHostRetained;return true;}
        m_fallback=WaterEnvironmentPaletteOnly; // valid native palette is mandatory for every unknown direction
        if(background){
            if((values.backgroundContent&15u)!=15u)m_fallback=WaterEnvironmentUncertifiedBackground;
            else {
                D3DSURFACE_DESC desc={};IDirect3DDevice9 *owner=nullptr,*targetOwner=nullptr;
                IDirect3DSurface9* target=nullptr;IUnknown *fieldIdentity=nullptr,*targetIdentity=nullptr;
                bool valid=SUCCEEDED(background->GetLevelDesc(0,&desc))&&
                    desc.Width==kWaterBackgroundSize&&desc.Height==kWaterBackgroundSize&&
                    desc.Format==D3DFMT_A16B16G16R16F&&desc.Pool==D3DPOOL_DEFAULT&&
                    background->GetLevelCount()==1&&SUCCEEDED(background->GetDevice(&owner))&&owner&&
                    SUCCEEDED(device->GetRenderTarget(0,&target))&&target&&SUCCEEDED(target->GetDevice(&targetOwner))&&targetOwner&&
                    SUCCEEDED(owner->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(&fieldIdentity)))&&fieldIdentity&&
                    SUCCEEDED(targetOwner->QueryInterface(__uuidof(IUnknown),reinterpret_cast<void**>(&targetIdentity)))&&targetIdentity&&fieldIdentity==targetIdentity;
                // Host wrappers may expose a different device interface from their resources. A bound target identifies the actual rendering device.
                if(fieldIdentity)fieldIdentity->Release();
                if(targetIdentity)targetIdentity->Release();
                if(target)target->Release();
                if(targetOwner)targetOwner->Release();
                if(owner)owner->Release();
                if(!valid){Clear();m_fallback=WaterEnvironmentInvalidInput;return false;}
                m_background=background;m_background->AddRef();m_fallback=WaterEnvironmentNoFallback;
            }
        }
        m_valid=true;return true;
    }
private:
    WaterEnvironmentValues m_values={};
    FrameStamp m_stamp={};
    IDirect3DTexture9* m_background=nullptr;
    bool m_valid=false,m_sealed=false;
    unsigned m_fallback=WaterEnvironmentNotProvided;
};
}
