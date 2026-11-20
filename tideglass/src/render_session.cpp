#include "render_session.h"
#include "log.h"
#include "gpu_timer.h"

namespace water
{
bool RenderSession::DeferredSurfaceArea(const FrameInputs& frame, const float* bounds, bool inLiquidPass, LiquidKind kind, RECT& area, int fadeRamp)
{
    // Terrain liquid batches use world vertices. A pool drawn with a building can use its model origin.
    float world[6];
    if (!inLiquidPass) {
        LiquidDrawCamera camera;
        camera.Set(frame);
        const bool known=m_liquidTransform.Bounds(m_device, camera, bounds, world);
        // Building slime is drawn before the terrain liquid pass which normally
        // supplies the mirror's visible receiver bounds.
        if(kind!=LiquidKind::Magma && m_choice==ReflectionProducer::Owned)
            ObserveLiquidBounds(known?world:nullptr,known);
        if (!known) return false;
        bounds = world;
    }
    return LiquidScreenArea(frame, bounds, area, fadeRamp);
}

void RenderSession::SelectSky()
{
    m_choice = ReflectionProducer::Sky;
    m_frameConfig.screenReflections = false;
    m_frameConfig.reflectionScenery = false;
}

ReflectionProducer RenderSession::ConfigureReflection(const FrameInputs& input, const Config& settings)
{
    FrameDiagnostics::Scope profile(m_diagnostics,FrameDiagnostics::Selection);
    m_diagnostics.Input(input);
    m_liquidCamera.Set(input);
    m_frameConfig=settings; // Frozen for the frame: later reloads of the settings cannot affect it.
    if(auto* row=m_diagnostics.Current())row->water=settings.enable&&!m_diagnostics.Bypass(settings);
    if (!Owner() || !m_lifetime.Accepts(Frame())) return ReflectionProducer::Sky;
    if(m_diagnostics.Bypass(settings)){if(m_owned)m_owned->Disable();SelectSky();return m_choice;}
    SetReflection(Frame(), {}); // producer selection comes first; retain no old borrowed source
    m_useFrameConfig = settings.reflectionProducer != 0;
    if (!m_useFrameConfig) {
        if (m_owned) m_owned->Disable();
        m_choice = ReflectionProducer::HostView;
        ReportReflection("host view");
        return m_choice;
    }
    m_reportOwned = true;
    if (settings.reflectionProducer == 2) {
        if (m_owned) m_owned->Disable();
        SelectSky();
        ReportReflection("sky selected");
        return m_choice;
    }
    if (!m_owned) m_owned = std::make_unique<OwnedReflection>();
    m_owned->SetDiagnostics(&m_diagnostics);
    if (m_owned->Begin(m_device, Frame(), input, settings)) {
        m_choice = ReflectionProducer::Owned;
        m_frameConfig.reflectionScenery = true;
        m_frameConfig.reflectionSceneryAlign = true;
    } else SelectSky();
    ReportReflection(m_owned->Reason());
    return m_choice;
}

bool RenderSession::UpdateOwnedReflection(const FrameInputs& input)
{
    if (!Owner() || m_choice != ReflectionProducer::Owned || !m_owned || !m_lifetime.Accepts(Frame())) return false;
    if (m_ownedDrawAttempted) return m_ownedDrawReady;
    m_ownedDrawAttempted = true;
    m_owned->SetLiquidCoverage(&m_liquidCoverage);
    if(m_frameConfig.traceLiquids && GetTickCount()-m_coverageLogged>=2000){
        m_coverageLogged=GetTickCount();
        WATER_LOG_INFO("liquid receiver coverage complete=%u draws=%u",unsigned(m_liquidCoverage.Complete()),unsigned(m_liquidCoverage.Draws().size()));
    }
    OwnedReflectionOutput output;
    const bool timed = m_timer && m_timer->Begin(GpuTimer::OwnedScene);
    const unsigned views=input.CameraUnderSurface()?0:m_owned->ViewCount();
    unsigned first=0;RECT area{};
    while(first<views && !m_owned->ViewArea(input,first,area))++first;
    if(views && first==views){
        if(timed)m_timer->End(GpuTimer::OwnedScene);
        m_ownedDrawReady=true;return true; // no receiving water in this view
    }
    const bool drawn = m_owned->Draw(m_device, Frame(), input, output,
                                     views?first:OwnedWaterReceivers::Missing);
    if (timed) m_timer->End(GpuTimer::OwnedScene);
    ReflectionView view;
    if (drawn) {
        view.colour = output.colour; view.depth = output.depth; view.projection = output.projection;
        view.planes = output.heights ? nullptr : output.planes.data(); view.height = output.height;
        view.heights = output.heights;view.underwater=output.underwater;
    }
    if (drawn && SetReflection(Frame(), view)) {
        if(views)m_effects.SetSceneryViews(m_owned.get());
        m_ownedDrawReady = true;
        if (m_publishedRequest != m_owned->PublishedRequest() || m_publishedGeneration != Frame().generation ||
            m_publishedUploads != m_owned->Uploads()) {
            m_publishedRequest = m_owned->PublishedRequest(); m_publishedGeneration = Frame().generation;
            m_publishedUploads = m_owned->Uploads();
            WATER_LOG_INFO("owned reflection published fixture=%u partial=%u request=%llu generation=%llu frame=%llu active_cpu=%llu replacement_cpu=%llu gpu_estimate=%llu staging_cpu=%llu",
                           unsigned(!m_owned->IsWorld()), unsigned(m_owned->IsWorld()),
                           static_cast<unsigned long long>(m_publishedRequest), static_cast<unsigned long long>(m_publishedGeneration),
                           static_cast<unsigned long long>(Frame().frame), static_cast<unsigned long long>(m_owned->ActiveCpuBytes()),
                           static_cast<unsigned long long>(m_owned->ReplacementCpuBytes()), static_cast<unsigned long long>(m_owned->GpuBytes()),
                           static_cast<unsigned long long>(m_owned->StagingCpuBytes()));
        }
        return true;
    }
    // The sky stands in; the controller's failure state decides when to try again.
    SetReflection(Frame(), {});
    SelectSky();
    ReportReflection(m_owned->Reason());
    return false;
}

void RenderSession::ReportReflection(const char* reason)
{
    if(auto* row=m_diagnostics.Current()){
        row->producer=int(m_choice);std::snprintf(row->reason,sizeof(row->reason),"%s",reason?reason:"");
        if(m_owned){const auto& state=m_owned->DiagnosticState();row->preparation=unsigned(state.state);row->workerMilliseconds=state.preparationMilliseconds;row->sourceBytes=state.sourceBytes;
            row->request=m_owned->Request();row->activeBytes=m_owned->ActiveCpuBytes();row->replacementBytes=m_owned->ReplacementCpuBytes();row->gpuBytes=m_owned->GpuBytes();row->stagingBytes=m_owned->StagingCpuBytes();row->uploads=m_owned->Uploads();row->draws=m_owned->Draws();}
        row->work=0;for(auto count:m_effects.WorkCounts())row->work+=count;
    }
    const char* retention=m_owned?m_owned->RetentionReason():"no controller";
    if (!m_reportOwned || (m_reportChoice == int(m_choice) && m_reportReason == reason && m_reportRetention == retention)) return;
    m_reportChoice = int(m_choice); m_reportReason = reason;m_reportRetention=retention;
    const auto cpu = m_owned ? m_owned->ActiveCpuBytes() : 0;
    const auto replacement = m_owned ? m_owned->ReplacementCpuBytes() : 0;
    const auto gpu = m_owned ? m_owned->GpuBytes() : 0;
    const auto active=m_owned?m_owned->ActiveIdentity():OwnedSceneIdentity{};
    const auto pending=m_owned?m_owned->RequestedIdentity():OwnedSceneIdentity{};
    WATER_LOG_INFO("reflection producer=%d reason=%s fixture=%u partial=%u request=%llu active_cpu=%llu replacement_cpu=%llu gpu_estimate=%llu staging_cpu=%llu uploads=%u draws=%u retain=%s active_request=%llu active_region=(%d,%d) requested_region=(%d,%d)",
                   int(m_choice), reason, unsigned(!m_owned || !m_owned->IsWorld()), unsigned(m_owned && m_owned->IsWorld()),
                   static_cast<unsigned long long>(m_owned ? m_owned->Request() : 0),
                   static_cast<unsigned long long>(cpu), static_cast<unsigned long long>(replacement),
                   static_cast<unsigned long long>(gpu), static_cast<unsigned long long>(m_owned ? m_owned->StagingCpuBytes() : 0),
                   m_owned ? m_owned->Uploads() : 0, m_owned ? m_owned->Draws() : 0,retention,
                   static_cast<unsigned long long>(m_owned?m_owned->ActiveRequest():0),
                   active.regionX,active.regionY,pending.regionX,pending.regionY);
}
}
