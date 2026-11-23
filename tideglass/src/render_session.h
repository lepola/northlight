#pragma once
#include "frame_lifetime.h"
#include "renderer.h"
#include "owned_reflection.h"
#include "frame_diagnostics.h"
#include "liquid_draw_transform.h"
#include <utility>

namespace water
{
// Textures belong to the host, on this session's device, until EndFrame or LoseDevice.
// Projection and plane rows are copied. The depth encoding follows reflection_scenery.hlsli.
struct ReflectionView
{
    IDirect3DTexture9* colour = nullptr;
    IDirect3DTexture9* depth = nullptr;
    float height = 0;
    const float* projection = nullptr;
    const float* planes = nullptr;
    bool heights = false, underwater = false;
};

// HostView: nothing is produced here, and a host may supply a view through the Reflection call. The numbers are what
// the frame profile records.
enum class ReflectionProducer { HostView = 0, Owned = 1, Sky = 2 };

// Common effect/lifecycle owner. The standalone proxy and cooperative hosts own
// depth acquisition, native draw interception and the device submission schedule.
class RenderSession
{
public:
    RenderSession() : m_thread(GetCurrentThreadId()) {}
    FrameStamp BeginFrame(IDirect3DDevice9* device)
    {
        if (!Owner() || !device || (m_device && m_device != device)) return {};
        m_device = device;
        EndFrame();
        const auto stamp = m_lifetime.Begin();
        if(stamp.session){m_environment.Clear();m_effects.BeginFrame();m_effects.SetEnvironment(&m_environment);}
        m_diagnostics.Begin(stamp);
        if (!m_externalTimer && m_diagnostics.Gpu()) {
            m_profileTimer.ObserveDetailed([this](uint64_t tag,bool valid,double span,const double* parts,uint64_t mask){m_diagnostics.GPU(tag,valid,span,parts,mask);});
            m_profileTimer.BeginFrame(device,stamp.frame);m_timer=&m_profileTimer;m_effects.SetTimer(m_timer);
        } else if(!m_externalTimer) {if(m_timer==&m_profileTimer)m_profileTimer.Release();m_timer=nullptr;m_effects.SetTimer(nullptr);}
        m_choice = ReflectionProducer::Sky;
        m_useFrameConfig = false;
        m_ownedDrawAttempted = m_ownedDrawReady = false;
        m_liquidCoverage.Clear();
        return stamp;
    }
    void PrepareUnderwaterShaders(IDirect3DDevice9* device, const Config& cfg)
    {
        if (Owner() && device) m_effects.PrepareUnderwaterShaders(device, cfg);
    }
    FrameStamp Frame() const { return m_lifetime.Current(); }
    bool LiquidDrawBoundsSupported() { return m_choice==ReflectionProducer::Owned && m_liquidTransform.Supported(m_device); }
    bool DeferredSurfaceArea(const FrameInputs& frame, const float* bounds, bool inLiquidPass, LiquidKind kind, RECT& area, int fadeRamp);
    void ObserveLiquidDraw(const float* rawBounds)
    {
        if(!Owner() || !Frame().session || m_choice!=ReflectionProducer::Owned)return;
        float world[6];
        const bool known=m_liquidTransform.Bounds(m_device,m_liquidCamera,rawBounds,world);
        ObserveLiquidBounds(known?world:nullptr,known);
    }
    void ObserveLiquidBounds(const float* bounds,bool worldSpace)
    {
        if(Owner() && Frame().session && m_liquidCoverage.Observe(bounds,worldSpace))
            m_ownedDrawAttempted=m_ownedDrawReady=false;
    }
    bool CaptureHostPass(IDirect3DDevice9* d,const char* name,bool after)
    { return Owner()&&m_lifetime.Current().session&&m_effects.CaptureHostPass(d,name,after); }
    void CaptureFinished(IDirect3DDevice9* d)
    { if(Owner()&&m_lifetime.Current().session)m_effects.CaptureFinished(d); }
    void EndFrame()
    {
        FrameDiagnostics::Scope profile(m_diagnostics,FrameDiagnostics::Lifecycle);
        if (!Owner() || !m_lifetime.Current().session) return;
        m_effects.EndFrame();m_effects.SetEnvironment(nullptr);m_environment.Clear();
        if (m_owned) m_owned->EndFrame();
        if (!m_externalTimer) m_profileTimer.EndFrame();
        m_lifetime.End();
    }
    void LoseDevice()
    {
        if (!Owner()) return;
        EndFrame();
        m_diagnostics.Finish("reset");
        m_profileTimer.Release();
        m_lifetime.Lose();
        m_liquidTransform.Clear();
        if (m_owned) m_owned->LoseDevice();
        m_environment.Clear();m_effects.SetEnvironment(nullptr);m_effects.ReleaseDefaultPool();
    }
    bool RestoreDevice(IDirect3DDevice9* device)
    {
        if (!Owner() || !device || (m_device && device != m_device)) return false;
        m_device = device;
        m_lifetime.Restore();
        return true;
    }
    void ReleaseAll()
    {
        if (!Owner()) return;
        LoseDevice();
        m_effects.ReleaseAll();
        if (m_owned) m_owned->ReleaseAll();
    }
    bool SetEnvironment(FrameStamp frame,const WaterEnvironmentValues& values,IDirect3DTexture9* background){
        return Owner()&&m_lifetime.Accepts(frame)&&m_environment.Set(m_device,frame,values,background);
    }
    bool ClearEnvironment(FrameStamp frame){
        if(!Owner()||!m_lifetime.Accepts(frame)||m_environment.Sealed())return false;
        m_environment.Clear();return true;
    }
    void NativeEnvironment(const FrameInputs& in,const Config& cfg){
        if(cfg.waterAtmosphere&&!m_environment.Sealed())SetEnvironment(Frame(),Renderer::NativeEnvironment(in),nullptr);
    }
    bool EnvironmentAvailable()const{return m_lifetime.Accepts(m_environment.Source())&&m_environment.Available();}
    unsigned EnvironmentFallback()const{return m_environment.Fallback();}
    bool DistanceAtmosphereShaded()const{return m_effects.DistanceAtmosphereShaded();}
    bool SetReflection(FrameStamp frame, const ReflectionView& view)
    {
        if (!Owner() || !m_lifetime.Accepts(frame) || bool(view.colour) != bool(view.depth)) return false;
        m_effects.SetScenery(view.colour, view.depth, view.height, view.projection,
                             view.planes, view.heights, view.underwater);
        return true;
    }
    void SetTimer(GpuTimer* timer) { if (Owner()) { m_externalTimer=timer!=nullptr;m_timer = timer; m_effects.SetTimer(timer); } }
    FrameDiagnostics& Diagnostics(){return m_diagnostics;}
    const FrameDiagnostics& Diagnostics() const{return m_diagnostics;}
    void ConfigureDiagnostics(const Config& c,unsigned source){if(Owner())m_diagnostics.Configure(c,m_outputDirectory,source);}
    ReflectionProducer ConfigureReflection(const FrameInputs& input, const Config& settings);
    void DisableOwnedReflection() { if (Owner() && m_owned) m_owned->Disable(); }
    ReflectionProducer Producer() const { return m_choice; }
    bool UpdateOwnedReflection(const FrameInputs& input);
    void SetOutputDirectory(const std::string& directory) { if (Owner()) {m_outputDirectory=directory;m_effects.SetOutputDirectory(directory);} }
    const std::array<unsigned, 9>& WorkCounts() const { return m_effects.WorkCounts(); }
    Renderer::SurfaceResult LastSurfaceResult() const { return m_effects.LastSurfaceResult(); }
    const char* LastSkipReason() const { return m_skip ? m_skip : m_effects.LastSkipReason(); }

    bool SceneBeforeLiquid(IDirect3DTexture9*& colour, IDirect3DTexture9*& depth) const
    {
        colour = depth = nullptr;
        if (!Owner()) return false;
        auto* capturedColour = m_effects.SceneBeforeLiquid();
        auto* capturedDepth = m_effects.OpaqueDepthBeforeLiquid();
        if (!capturedColour || !capturedDepth) return false;
        colour = capturedColour; depth = capturedDepth;
        return true;
    }
    bool SnapshotBeforeLiquid(IDirect3DDevice9* d, IDirect3DTexture9* t, IDirect3DSurface9* z, bool capture)
    { FrameDiagnostics::Scope profile(m_diagnostics,FrameDiagnostics::Snapshot);GpuScope gpu(m_externalTimer?nullptr:m_timer,GpuTimer::LiquidSnapshot);return t && z && Apply(d, &Renderer::SnapshotBeforeLiquid, t, z, capture); }
    bool SnapshotSurfaceBase(IDirect3DDevice9* d, IDirect3DTexture9* t, IDirect3DSurface9* z, const RECT* area = nullptr)
    { FrameDiagnostics::Scope profile(m_diagnostics,FrameDiagnostics::Snapshot);GpuScope gpu(m_externalTimer?nullptr:m_timer,GpuTimer::LiquidSnapshot);return t && z && Apply(d, &Renderer::SnapshotSurfaceBase, t, z, area); }
    bool CaptureAndUndoSurface(IDirect3DDevice9* d, IDirect3DTexture9* t, IDirect3DSurface9* z,
                               const FrameInputs& in, const Config& cfg, bool liquid, LiquidKind kind, bool undo = true,
                               const RECT* area = nullptr, int ramp = -1)
    { FrameDiagnostics::Scope profile(m_diagnostics,FrameDiagnostics::Slime);GpuScope gpu(m_externalTimer?nullptr:m_timer,GpuTimer::SlimeCapture);return t && z && Apply(d, &Renderer::CaptureAndUndoSurface, t, z, in, Effective(cfg), liquid, kind, undo, area, ramp); }
    bool CompositeWater(IDirect3DDevice9* d, IDirect3DTexture9* t, IDirect3DSurface9* z,
                        const FrameInputs& in, const Config& cfg, bool shade, IDirect3DTexture9* nativeWater = nullptr,
                        IDirect3DTexture9* refractionScene = nullptr, IDirect3DTexture9* surfaceVisibility = nullptr, uint32_t stencilBit = 0)
    {
        FrameDiagnostics::Scope profile(m_diagnostics,FrameDiagnostics::Composite);
        GpuScope gpu(m_externalTimer?nullptr:m_timer,GpuTimer::Water);
        m_diagnostics.Input(in);
        if (!Owner() || !t || !z) return false;
        m_environment.Seal();
        if (shade && m_choice == ReflectionProducer::Owned) UpdateOwnedReflection(in);
        return Apply(d, &Renderer::CompositeWater, t, z, in, Effective(cfg), shade, nativeWater, refractionScene, surfaceVisibility, stencilBit);
    }
    bool HasDeferredSurfaces() const { return m_effects.HasDeferredSlime() || m_effects.HasDeferredLava(); }
    bool CompositeDeferredSlime(IDirect3DDevice9* d, IDirect3DTexture9* t, IDirect3DSurface9* z,
                                const FrameInputs& in, const Config& cfg)
    {
        if (!Owner() || !t || !z || !m_effects.HasDeferredSlime()) return false;
        FrameDiagnostics::Scope profile(m_diagnostics,FrameDiagnostics::Slime);
        GpuScope gpu(m_externalTimer?nullptr:m_timer,GpuTimer::Slime);
        m_diagnostics.Input(in);
        if ((cfg.screenReflections || in.cameraInSlime) && m_choice == ReflectionProducer::Owned)
        {
            GpuScope owned(m_externalTimer?nullptr:m_timer,GpuTimer::SlimeOwned);
            UpdateOwnedReflection(in);
        }
        return Apply(d, &Renderer::CompositeDeferredSlime, t, z, in, Effective(cfg));
    }
    bool CompositeDeferredLava(IDirect3DDevice9* d, IDirect3DTexture9* t, IDirect3DSurface9* z,
                               const FrameInputs& in, const Config& cfg)
    {
        if (!Owner() || !t || !z || !m_effects.HasDeferredLava()) return false;
        FrameDiagnostics::Scope profile(m_diagnostics,FrameDiagnostics::Slime);
        GpuScope gpu(m_externalTimer?nullptr:m_timer,GpuTimer::Lava);
        m_diagnostics.Input(in);
        return Apply(d, &Renderer::CompositeDeferredLava, t, z, in, Effective(cfg));
    }
    IDirect3DTexture9* LavaEffectsMask(IDirect3DDevice9* d, IDirect3DTexture9* t, IDirect3DSurface9* z,
                                       const FrameInputs& in, const Config& cfg, IDirect3DTexture9* hostMask)
    {
        if (!Owner() || !t || !z) return nullptr;
        return Apply(d, &Renderer::BuildLavaEffectsMask, t, z, in, Effective(cfg), hostMask) ? m_effects.LavaEffectsMask() : nullptr;
    }
    bool AmbientOcclusion(IDirect3DDevice9* d, IDirect3DTexture9* t, IDirect3DSurface9* z,
                          const FrameInputs& in, const Config& cfg)
    { FrameDiagnostics::Scope profile(m_diagnostics,FrameDiagnostics::Ambient);GpuScope gpu(m_externalTimer?nullptr:m_timer,GpuTimer::AmbientOcclusion);return t && z && Apply(d, &Renderer::AmbientOcclusion, t, z, in, Effective(cfg)); }
    bool Puddles(IDirect3DDevice9* d, IDirect3DTexture9* t, IDirect3DSurface9* z,
                 const FrameInputs& in, const Config& cfg)
    { FrameDiagnostics::Scope profile(m_diagnostics,FrameDiagnostics::Puddles);GpuScope gpu(m_externalTimer?nullptr:m_timer,GpuTimer::Puddles);return t && z && Apply(d, &Renderer::Puddles, t, z, in, Effective(cfg)); }
    bool DrawRainRings(IDirect3DDevice9* d, IDirect3DSurface9* z, const FrameInputs& in, const Config& cfg)
    { FrameDiagnostics::Scope profile(m_diagnostics,FrameDiagnostics::Rings);GpuScope gpu(m_externalTimer?nullptr:m_timer,GpuTimer::RainRings);return z && Apply(d, &Renderer::DrawRainRings, z, in, Effective(cfg)); }
    bool Render(IDirect3DDevice9* d, IDirect3DTexture9* t, IDirect3DSurface9* z,
                const FrameInputs& in, const Config& cfg)
    { FrameDiagnostics::Scope profile(m_diagnostics,FrameDiagnostics::WorldDone);GpuScope gpu(m_externalTimer?nullptr:m_timer,GpuTimer::WorldDone);return t && z && Apply(d, &Renderer::Render, t, z, in, Effective(cfg)); }

private:
    friend struct RenderSessionTestAccess;
    bool Owner() const { return GetCurrentThreadId() == m_thread; }
    const Config& Effective(const Config& settings) const { return m_useFrameConfig ? m_frameConfig : settings; }
    void SelectSky();
    void ReportReflection(const char* reason);
    template<class Method, class... Args> bool Apply(IDirect3DDevice9* device, Method method, Args&&... args)
    {
        if (!Owner()) return false;
        if(m_diagnostics.Bypass(m_frameConfig)){m_skip="diagnostic water bypass";return false;}
        m_skip = nullptr;
        if (!device || device != m_device || !m_lifetime.Accepts(Frame()))
        {
            m_skip = "water session has no current frame on this device thread";
            return false;
        }
        return (m_effects.*method)(device, std::forward<Args>(args)...);
    }
    DWORD m_thread;
    IDirect3DDevice9* m_device = nullptr;
    FrameLifetime m_lifetime;
    SurfaceEnvironment m_environment;
    Renderer m_effects;
    FrameDiagnostics m_diagnostics;
    GpuTimer m_profileTimer{8};
    std::string m_outputDirectory;
    bool m_externalTimer=false;
    std::unique_ptr<OwnedReflection> m_owned;
    LiquidDrawCoverage m_liquidCoverage;
    LiquidDrawTransform m_liquidTransform;
    LiquidDrawCamera m_liquidCamera;
    DWORD m_coverageLogged=0;
    Config m_frameConfig;
    bool m_useFrameConfig = false, m_reportOwned = false;
    bool m_ownedDrawAttempted = false, m_ownedDrawReady = false;
    ReflectionProducer m_choice = ReflectionProducer::Sky;
    int m_reportChoice = -1;
    const char* m_reportReason = nullptr;
    const char* m_reportRetention = nullptr;
    GpuTimer* m_timer = nullptr;
    uint64_t m_publishedRequest = 0, m_publishedGeneration = 0;
    unsigned m_publishedUploads = 0;
    const char* m_skip = nullptr;
};
}
