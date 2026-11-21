#pragma once
#include "config.h"
#include "scenery_views.h"
#include "reflection_coverage.h"
#include "liquid_draw_coverage.h"
#include "frame_lifetime.h"
#include "owned_scene_renderer.h"
#include <array>

namespace water
{
class FrameDiagnostics;
using OwnedReflectionOutput=SceneryImage;

// Coordinates the bounded CPU slot, active CPU residency and owner-only GPU work.
// Session policy selects current/owned/sky; hosts supply only captured world facts.
class OwnedReflection : public SceneryViews
{
public:
    bool Begin(IDirect3DDevice9* device, FrameStamp frame, const FrameInputs& input, const Config& settings);
    bool Draw(IDirect3DDevice9* device, FrameStamp frame, const FrameInputs& input, OwnedReflectionOutput& output,unsigned receiver=OwnedWaterReceivers::Missing);
    unsigned ViewCount()const override;
    bool ViewArea(const FrameInputs&,unsigned,RECT&)const override;
    IDirect3DTexture9* ReceiverMask()const override;
    bool DrawView(IDirect3DDevice9* d,const FrameInputs& in,unsigned view,SceneryImage& out)override
    {return Draw(d,m_frame,in,out,view);}
    // Borrowed only for this frame. Later native draws invalidate cropped views
    // before a subsequent composite, without rebuilding or uploading scenery.
    void SetLiquidCoverage(const LiquidDrawCoverage* coverage)
    {m_liquidCoverage=coverage;m_viewAreas.clear();m_drawn=false;}
    void Disable();
    void EndFrame();
    void LoseDevice();
    void ReleaseAll();
    const OwnedPreparedScene& DiagnosticState() const {return m_diagnosticState;}
    void SetDiagnostics(FrameDiagnostics* p){m_diagnostics=p;}
    const char* Reason() const { return m_reason; }
    const char* RetentionReason() const { return m_retentionReason; }
    const OwnedSceneIdentity& RequestedIdentity() const { return m_identity; }
    const OwnedSceneIdentity& ActiveIdentity() const { return m_activeIdentity; }
    uint64_t ActiveRequest() const { return m_activeRequest; }
    std::size_t ActiveCpuBytes() const { return m_active ? m_active->preparedBytes : 0; }
    std::size_t ReplacementCpuBytes() const;
    // Two bounded scene slots only during a handoff; never an unbounded scene cache.
    static constexpr std::size_t TransitionGpuLimit = 2 * OwnedWorldGpu::ResidencyLimit;
    std::size_t GpuBytes() const { return m_renderers[0].Bytes() + m_renderers[1].Bytes(); }
    std::size_t StagingCpuBytes() const { return m_renderers[0].StagingCpuBytes() + m_renderers[1].StagingCpuBytes(); }
    bool IsWorld() const { return m_world; }
    unsigned Uploads() const { return m_renderers[0].Uploads() + m_renderers[1].Uploads(); }
    unsigned Draws() const { return m_renderers[0].Draws() + m_renderers[1].Draws(); }
    uint64_t Request() const { return m_request; }
    uint64_t PublishedRequest() const { return m_drawReplacement || !m_active ? m_request : m_activeRequest; }
    static bool InCoverage(const FrameInputs& input,bool lake=false);
private:
    bool Fail(const char* reason);
    uint64_t ObserveDisplays(const FrameInputs&,const world::OwnedWorldRegion&);
    OwnedSceneIdentity SelectRequest(OwnedSceneIdentity,const FrameInputs&) const;
    bool CanRetain(const FrameInputs&) const;
    const char* RetentionFailure(const FrameInputs&) const;
    static bool ReceiverMatches(const OwnedCpuScene&,const FrameInputs&);
    bool Retain(IDirect3DDevice9*, const FrameInputs&, const Config&, const char* reason);
    bool ReplacementFailed(IDirect3DDevice9*, const FrameInputs&, const Config&, const char* reason);
    bool ReadyFrame(IDirect3DDevice9*, const FrameInputs&, OwnedSceneRenderer&, const char* reason);
    bool Upload(IDirect3DDevice9*, OwnedSceneRenderer&, const OwnedCpuScene&, const FrameInputs&, unsigned budget);
    OwnedSceneRenderer& ActiveRenderer() { return m_renderers[m_currentRenderer]; }
    OwnedSceneRenderer& ReplacementRenderer() { return m_renderers[m_currentRenderer ^ 1]; }
    void RetireObsolete();
    void RetryFailure();
    bool SameDrawView(const FrameInputs& input) const;
    OwnedFixtureDescription Fixture(const Config& settings) const;
    FrameDiagnostics* m_diagnostics=nullptr;
    OwnedPreparedScene m_diagnosticState{};
    OwnedScenePreparation m_preparation;
    OwnedSceneRenderer m_renderers[2];
    unsigned m_currentRenderer = 0;
    std::shared_ptr<const OwnedCpuScene> m_active;
    const OwnedCpuScene* m_waiting = nullptr; // slot remains occupied through Draw/publication
    OwnedSceneIdentity m_identity;
    OwnedSceneIdentity m_activeIdentity;
    FrameStamp m_frame;
    uint64_t m_request = 0;
    const LiquidDrawCoverage* m_liquidCoverage=nullptr;
    mutable std::vector<RECT> m_viewAreas; // computed once per frame from actual wet quads
    unsigned m_drawnReceiver=OwnedWaterReceivers::Missing;
    uint64_t m_activeRequest = 0;
    std::array<uint32_t,world::MaxLiveAssets> m_liveDisplays={};
    int m_liveMap=-1;
    uint64_t m_liveSource=0,m_receiverRevision=0;
    DWORD m_liveLog=0;
    bool m_drawReplacement = false, m_replacementFailed = false;
    uint64_t m_sourceRevision = 2, m_reportedPreparation = 0;
    std::shared_ptr<const std::wstring> m_archiveList,m_textureCacheDirectory;
    ReflectionDistance m_distance=ReflectionDistance::Standard;
    int m_textureCacheMode=0,m_textureCacheMiB=256,m_textureQuality=0;
    uint64_t m_preparationRevision=0,m_reportedBakeBatches=0;
    bool m_halfTextures=false; // the GPU refused this scenery at full texture detail; it is prepared again at half
    bool m_world = false, m_streaming=false;
    world::OwnedWorldRegion m_region;
    ReflectionCoverage m_coverage;
    uint32_t m_coverageTick=0;
    world::OwnedWorldRegion m_liveRegion;
    OwnedWorldDrawOptions m_drawOptions;
    int m_sourceLighting=0,m_reportedSourceLighting=-1;
    bool m_reportedSceneKnown=false;
    bool m_enabled = false, m_failed = false, m_failDraw = false, m_frameReady = false, m_drawn = false;
    float m_view[16] = {}, m_projection[16] = {}, m_camera[3] = {};
    D3DVIEWPORT9 m_viewport = {};
    const char* m_reason = "disabled";
    const char* m_retentionReason = "not requested";
    const char* m_reportedRetention = nullptr;
};
}
