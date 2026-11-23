#pragma once
#include "host_passes.h"
#include "liquid_draw.h"
#include "host_settings.h"
#include "surface_depth.h"
#include "screen_pass.h"
#include <cstring>
#include <cstdio>

inline WaterHostSettings northlightWaterSettings;
inline thread_local WaterHostPasses* northlightTracePasses=nullptr;
inline uint64_t NorthlightWaterTraceBegin(){return northlightTracePasses?northlightTracePasses->TraceBegin():0;}
inline void NorthlightWaterTraceEnd(uint64_t token,const char* name){if(northlightTracePasses)northlightTracePasses->TraceEnd(token,name);}
class NorthlightWaterCall {
    uint64_t token;char name[96]{};
    void Label(const char* value){size_t i=0;if(value)for(;i+1<sizeof name&&value[i];++i)name[i]=value[i];name[i]=0;}
public:
    explicit NorthlightWaterCall(const char* value):token(value?NorthlightWaterTraceBegin():0){if(token)Label(value);}
    ~NorthlightWaterCall(){if(token)NorthlightWaterTraceEnd(token,name);}
    bool Active()const{return token!=0;}
    void Next(const char* value){if(!token)return;NorthlightWaterTraceEnd(token,name);token=value?NorthlightWaterTraceBegin():0;if(token)Label(value);}
};

template<class Capture>
WaterHostFrameRef SelectNorthlightWaterFrame(const WaterHostFrameRef* captured, bool queued, Capture direct)
{
    if (captured) return *captured;
    return queued ? WaterHostFrameRef() : direct();
}

inline WaterHostFrameRef CaptureNorthlightWater(const D3DVIEWPORT9& viewport,WaterHostTextureMap map=nullptr,void* context=nullptr)
{
    if (!northlightWaterSettings.Open()) return {};
    WaterHostCamera camera = {};
    camera.size = sizeof(camera);
    camera.viewport = viewport;
    SIZE_T read = 0;
    constexpr uintptr_t viewAddress = 0x00ADF5E8, projectionAddress = 0x00ADF628;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(viewAddress),
                           camera.view, sizeof(camera.view), &read) || read != sizeof(camera.view) ||
        !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(projectionAddress),
                           camera.projection, sizeof(camera.projection), &read) || read != sizeof(camera.projection)) return {};
    return northlightWaterSettings.Module().Capture(camera,map,context);
}

class NorthlightWaterBridge
{
public:
    void PresentBegin(IDirect3DDevice9* device,uint64_t frame){
        if (northlightWaterSettings.Open() && Initialise(device)) {
            northlightTracePasses = &m_passes;
            m_passes.PresentBegin(2, frame);
        }
    }
    void PresentReady(){m_passes.PresentReady();}
    void PresentEnd(HRESULT result){m_passes.PresentEnd(result);}
    bool Attempted() const { return m_attempted; }
    bool Begin(IDirect3DDevice9* device, const WaterHostFrameRef& frame)
    {
        if (m_attempted) return m_passes.Current();
        m_attempted = true;
        if (!frame || !Initialise(device)) return false;
        m_device = device;
        return m_passes.Begin(frame);
    }
    WaterHostLiquidKind ClassifyLiquid(IDirect3DBaseTexture9* texture,const float* bounds=nullptr,int* ramp=nullptr) { return m_passes.ClassifyLiquid(texture,bounds,ramp); }
    bool ObserveLiquid(WaterHostLiquidKind kind,const float* bounds=nullptr) { return m_passes.ObserveLiquid(kind,bounds); }
    bool BeforeSurface(IDirect3DTexture9* depth, IDirect3DSurface9* boundDepth, bool outdoors, WaterHostLiquidKind kind, const float* bounds=nullptr, int ramp=-1)
    { return m_passes.BeforeSurface({sizeof(WaterHostResources),depth,boundDepth},outdoors,kind,bounds,ramp); }
    bool AfterSurface(IDirect3DTexture9* depth, IDirect3DSurface9* boundDepth, bool drawn)
    { const bool captured=m_passes.AfterSurface({sizeof(WaterHostResources),depth,boundDepth},drawn); m_slime |= captured; return captured; }
    bool HasSurface() const { return m_liquid || m_slime; }
    bool SceneBeforeLiquid(IDirect3DTexture9*& colour, IDirect3DTexture9*& depth)
    { return m_passes.SceneBeforeLiquid(colour, depth); }
    bool CompositeSurfaces(IDirect3DTexture9* depth, IDirect3DSurface9* boundDepth)
    {
        if (!m_passes.CompositeSurfaces({sizeof(WaterHostResources),depth,boundDepth})) return false;
        // Slime- or lava-only scenes still need the shared underwater/world-final pass.
        if (!m_frameColour) {
            if (FAILED(m_device->GetRenderTarget(0,&m_frameColour)) || !m_frameColour ||
                FAILED(m_device->GetViewport(&m_frameViewport))) { ReleaseColour(); return false; }
        }
        if (!m_frameDepth) m_frameDepth=depth;
        return true;
    }
    IDirect3DTexture9* EffectsMask(IDirect3DTexture9* depth, IDirect3DSurface9* boundDepth, IDirect3DTexture9* hostMask)
    { return m_passes.EffectsMask({sizeof(WaterHostResources),depth,boundDepth},hostMask); }
    bool RefreshLiquidDepth(IDirect3DTexture9* resolvedDepth, IDirect3DTexture9* waterMask,
                            float nearPlane, float farPlane, float minDepth, float maxDepth)
    {
        // Native depth now includes slime; ordinary water still comes from the
        // host mask. Reuse the existing minimum-depth conversion for both.
        m_frameDepth = waterMask ? m_depth.Build(m_device,resolvedDepth,waterMask,nearPlane,farPlane,minDepth,maxDepth) : resolvedDepth;
        return m_frameDepth != nullptr;
    }
    bool BeforeLiquid(IDirect3DTexture9* opaqueDepth, IDirect3DSurface9* boundDepth)
    {
        if (m_liquid) return true;
        m_liquid = m_passes.BeforeLiquid({sizeof(WaterHostResources), opaqueDepth, boundDepth});
        return m_liquid;
    }
    bool Composite(IDirect3DTexture9* opaqueDepth, IDirect3DTexture9* mask, IDirect3DSurface9* boundDepth,
                   float nearPlane, float farPlane, float minDepth, float maxDepth, bool enhancedFogReady,
                   IDirect3DTexture9* refractionScene = nullptr, IDirect3DTexture9* surfaceVisibility = nullptr, bool spatialFog = false)
    {
        if (!m_liquid) return false;
        m_liquid = false;
        m_pendingComposite = false;
        ReleaseColour();
        if (FAILED(m_device->GetRenderTarget(0, &m_frameColour)) || !m_frameColour ||
            FAILED(m_device->GetViewport(&m_frameViewport))) {
            ReleaseColour(); m_passes.End(); return false;
        }
        m_nativeWater = mask;
        m_frameDepth = m_depth.Build(m_device, opaqueDepth, mask, nearPlane, farPlane, minDepth, maxDepth);
        if (!m_frameDepth) { ReleaseColour(); m_passes.End(); return false; }
        m_passes.Fog(enhancedFogReady, spatialFog);
        if (northlightWaterSettings.CompositeAfterEffects()) {
            m_pendingComposite = true;
            return true;
        }
        return m_passes.AfterLiquid({sizeof(WaterHostResources), m_frameDepth, boundDepth, mask, refractionScene, surfaceVisibility}, true);
    }
    // scene, sceneDepth and sceneViewport: the host's finished world colour, its resolved depth and viewport, used when
    // no liquid was drawn this frame. The camera can be under a surface it cannot see (looking down from under the sea,
    // 2026-11-21), and the underwater view is drawn here, not with a surface.
    bool WorldDone(IDirect3DSurface9* boundDepth, IDirect3DSurface9* scene = nullptr,
                   IDirect3DTexture9* sceneDepth = nullptr, const D3DVIEWPORT9* sceneViewport = nullptr)
    {
        // Consume the deferred pass once, even if finalisation cannot bind the
        // completed scene. End never flushes it over an incomplete world or UI.
        const bool pending = m_pendingComposite;
        m_pendingComposite = false;
        if (!m_frameColour && scene && sceneDepth && sceneViewport && m_passes.Current()) {
            scene->AddRef();
            m_frameColour = scene;
            m_frameViewport = *sceneViewport;
            if (!m_frameDepth) m_frameDepth = sceneDepth;
        }
        // Remember the sky from the composite's target, excluding water through the
        // converted depth: at the world read that is the world before screen effects,
        // otherwise the completed host frame. Borrowed from m_depth until End/Lost.
        if (!m_frameDepth || !m_frameColour || !boundDepth || !m_device ||
            !EnsureScreenPassStateBlock(m_device, m_finalState)) return false;
        // The ready-world fold writes the scene target supplied at composition,
        // but can leave its half-size AO/bloom target bound. Read the retained
        // scene, never infer finished colour from the post-effects binding.
        ScreenPassState saved;
        if (!saved.Capture(m_device, m_finalState)) return false;
        bool bound = SUCCEEDED(m_device->SetDepthStencilSurface(nullptr));
        for (DWORD slot = 1; slot < 4; ++slot)
            if (saved.color[slot]) bound = SUCCEEDED(m_device->SetRenderTarget(slot, nullptr)) && bound;
        bound = SUCCEEDED(m_device->SetRenderTarget(0, m_frameColour)) && bound;
        bound = SUCCEEDED(m_device->SetDepthStencilSurface(boundDepth)) && bound;
        bound = SUCCEEDED(m_device->SetViewport(&m_frameViewport)) && bound;
        const WaterHostResources resources = {sizeof(WaterHostResources), m_frameDepth, boundDepth, m_nativeWater};
        const bool composed = pending && bound && m_passes.AfterLiquid(resources, true);
        // A memory-only WorldDone can succeed internally while reporting no
        // on-screen draw. Never use its result to skip restoration or composition.
        const bool drawn = bound && m_passes.Draw(WaterHostWorldDone, resources);
        const bool restored = saved.Restore();
        return restored && (composed || drawn);
    }
    bool CapturePass(const char* name,bool after){return m_passes.CapturePass(name,after);}
    bool Current() const { return m_passes.Current(); }
    bool Environment(const WaterEnvironmentValues& values,IDirect3DTexture9* background,float viewOpticalDepth48=0,const WaterLampSamples& lamps={}){return m_passes.Environment(values,background,viewOpticalDepth48,lamps);}
    bool WantsEnvironment(bool& lamps){return m_passes.WantsEnvironment(lamps);}
    bool OwnsDistance(){WaterHostSurfaceStatus status={};return m_passes.SurfaceStatus(status)&&status.outcome==WaterHostSurfaceShaded&&status.distanceAtmosphereShaded!=0;}
    bool SurfaceStatus(WaterHostSurfaceStatus& status){return m_passes.SurfaceStatus(status);}
    IDirect3DTexture9* SurfaceDepth()const{return m_frameDepth;} // borrowed until End/Lost/Shutdown
    bool Reflection(const WaterHostReflection& reflection) { return m_passes.Reflection(reflection); }
    bool Liquid() const { return m_liquid; }
    void End()
    {
        m_pendingComposite = false;
        m_passes.End();
        ReleaseColour();
        m_frameDepth = nullptr;
        m_attempted = m_liquid = m_slime = false;
    }
    void Lost()
    {
        m_pendingComposite = false;
        m_passes.Lost();
        ReleaseColour();
        if (m_finalState) { m_finalState->Release(); m_finalState = nullptr; }
        m_frameDepth = nullptr;
        m_depth.ReleaseDefaultPool();
        m_attempted = m_liquid = m_slime = false;
    }
    bool Restored() { return m_passes.Restored(); }
    bool Shutdown()
    {
        m_pendingComposite = false;
        m_attempted = m_liquid = m_slime = false;
        if(northlightTracePasses==&m_passes)northlightTracePasses=nullptr;
        ReleaseColour();
        if (m_finalState) { m_finalState->Release(); m_finalState = nullptr; }
        m_frameDepth = nullptr;
        m_depth.ReleaseAll();
        m_device = nullptr;
        return m_passes.Shutdown();
    }
private:
    bool Initialise(IDirect3DDevice9* device)
    {
        auto& module = northlightWaterSettings.Module();
        return m_passes.Initialise(module.API(), device, northlightWaterSettings.SettingsPath().c_str(),
                                   module.Diagnostics(), module.Trace(), module.Environment(), module.Liquid(),
                                   module.CaptureAPI());
    }

    void ReleaseColour() { m_nativeWater = nullptr; if (m_frameColour) { m_frameColour->Release(); m_frameColour = nullptr; } }
    WaterHostPasses m_passes;
    IDirect3DTexture9* m_nativeWater = nullptr; // borrowed only while m_frameDepth/m_frameColour are valid
    water::SurfaceDepth m_depth;
    IDirect3DDevice9* m_device = nullptr;
    IDirect3DTexture9* m_frameDepth = nullptr;
    IDirect3DSurface9* m_frameColour = nullptr; // retained on the device owner until End/Lost/Shutdown
    IDirect3DStateBlock9* m_finalState = nullptr;
    D3DVIEWPORT9 m_frameViewport = {};
    bool m_attempted = false, m_liquid = false, m_slime = false, m_pendingComposite = false;
};
