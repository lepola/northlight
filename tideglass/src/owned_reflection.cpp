#include "owned_reflection.h"
#include "owned_receiver_visibility.h"
#include <optional>
#include "frame_diagnostics.h"
#include "log.h"
#include "owned_world_region.h"
#include "math/projection.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace water
{
bool OwnedReflection::InCoverage(const FrameInputs& input,bool lake)
{
    // Declared diagnostic area, not a proof that these two boxes are a complete world.
    const auto* p = input.cameraPosition;
    return input.mapId == 0 && !input.cameraInSlime && (!input.cameraInWater || input.cameraWaterHeightKnown) &&
           world::OwnedRiverRegion::Camera(p,lake) &&
           input.viewport.Width >= 16 && input.viewport.Height >= 16;
}

OwnedFixtureDescription OwnedReflection::Fixture(const Config& settings) const
{
    // Source: the frozen October 22 ADT survey recorded in PLAN.md. Distinct
    // colours make the two receiving planes recognisable before material import.
    OwnedFixtureDescription fixture;
    fixture.count = 2;
    fixture.boxes[0] = {{{-9418.75f, -160.417318f, 63.631035f}}, {{1, 2, 3}}, 0xffff6030,
                        {{0, 0, 1, 57.631035f}}};
    fixture.boxes[1] = {{{-9285.416016f, -160.417318f, 68.967882f}}, {{1, 2, 3}}, 0xff30b0ff,
                        {{-.0741011821f, .0038793515f, .9972431827f, 750.2322793f}}};
    fixture.delayMilliseconds = static_cast<unsigned>(settings.reflectionOwnedDelayMs);
    fixture.fail = settings.reflectionOwnedFail;
    return fixture;
}

void OwnedReflection::RetireObsolete()
{
    const auto result = m_preparation.Read();
    const bool complete = result.state == OwnedPreparationState::Ready || result.state == OwnedPreparationState::Failed ||
                          result.state == OwnedPreparationState::Cancelled;
    if (complete && (result.state == OwnedPreparationState::Cancelled || !m_enabled || m_failed || m_replacementFailed || result.request != m_request))
        m_preparation.Retire();
}
void OwnedReflection::Disable()
{
    m_coverage={};
    if (m_enabled) {
        m_enabled = false;
        m_preparation.SetContext(m_identity, false);
        m_waiting = nullptr;
        ActiveRenderer().ReleaseDefaultPool();
        ReplacementRenderer().ReleaseDefaultPool();
        m_active.reset();
        m_request = m_activeRequest = 0;
    }
    m_frameReady = m_drawn = false;
    m_frame = {};
    m_reason = m_world ? "disabled or outside world coverage" : "disabled or outside fixture coverage";
    RetireObsolete();
}

uint64_t OwnedReflection::ObserveDisplays(const FrameInputs& input,const world::OwnedWorldRegion& region)
{
    // Remember bounded asset identities across local travel. Only this frame's
    // factual poses are drawn, so retaining an asset cannot leave a departed ship.
    if((m_streaming && !m_liveRegion.WithinRadius(input.cameraPosition,world::OwnedWorldRegion::SourceReach)) ||
       m_liveMap!=input.mapId || m_liveSource!=m_sourceRevision){
        m_liveDisplays={};m_liveMap=input.mapId;m_liveSource=m_sourceRevision;m_liveRegion=region;
    }
    for(unsigned i=0;i<std::min(input.reflectionObjectCount,kMaxReflectionObjects);++i){
        const auto& object=input.reflectionObjects[i];if(!ValidReflectionObject(object))continue;
        if(std::find(m_liveDisplays.begin(),m_liveDisplays.end(),object.display)!=m_liveDisplays.end())continue;
        auto slot=std::find(m_liveDisplays.begin(),m_liveDisplays.end(),0u);
        if(slot==m_liveDisplays.end())slot=std::find_if(m_liveDisplays.begin(),m_liveDisplays.end(),[&](auto display){
            for(unsigned j=0;j<std::min(input.reflectionObjectCount,kMaxReflectionObjects);++j)
                if(input.reflectionObjects[j].display==display)return false;
            return true;
        });
        if(slot!=m_liveDisplays.end())*slot=object.display;
    }
    uint64_t key=0;for(auto id:m_liveDisplays)key=(key^id)*1099511628211ull;
    return key;
}

OwnedSceneIdentity OwnedReflection::SelectRequest(OwnedSceneIdentity desired,const FrameInputs& input) const
{
    // Finish useful work before following a newer camera/display request. The
    // worker and upload slot retain their immutable identity through publication.
    // Map/source/settings/receiver changes and travel out of its water window
    // still cancel promptly. Newly observed displays join the next replacement.
    const world::OwnedWorldRegion requested{m_identity.map,m_identity.regionX,m_identity.regionY};
    const bool pending=m_request && (!m_active || !(m_activeIdentity==m_identity));
    if(m_streaming && m_enabled && !m_failed && !m_replacementFailed && pending &&
       desired.SameSource(m_identity) && desired.preparation==m_identity.preparation && desired.receiver==m_identity.receiver &&
       requested.Contains(input.cameraPosition,world::OwnedWorldRegion::WaterReach))return m_identity;
    return desired;
}

bool OwnedReflection::Begin(IDirect3DDevice9* device, FrameStamp frame, const FrameInputs& input, const Config& settings)
{
    EndFrame();
    m_retentionReason="not requested";
    // The world streams by region round the camera; the fixture's boxes keep their declared Elwynn area.
    const bool streaming=settings.reflectionProducer==3;
    const auto region=world::OwnedWorldRegion::Follow(m_region,input.mapId,input.cameraPosition);
    const bool coverage=streaming ? region.Valid() && (!(input.cameraInWater||input.cameraInSlime) || input.CameraUnderSurface()) &&
        input.viewport.Width>=16 && input.viewport.Height>=16 :
        InCoverage(input);
    if (!device || !frame.session || !settings.enable || (settings.reflectionProducer != 1 && settings.reflectionProducer != 3) ||
        !settings.screenReflections || !coverage) { Disable(); return false; }
    const bool world=settings.reflectionProducer==3;m_streaming=streaming;m_region=region;
    if (input.CameraUnderSurface() && (!world || !settings.underwaterScenery)) {Disable();return false;}
    if (world && !settings.reflectionOwnedArchives) {Disable();m_world=true;m_reason="world archive list not configured";return false;}
    if (world!=m_world || (world && (m_archiveList!=settings.reflectionOwnedArchives) &&
        (!m_archiveList || *m_archiveList!=*settings.reflectionOwnedArchives))) {
        m_world=world;m_archiveList=world ? settings.reflectionOwnedArchives : nullptr;++m_sourceRevision;
    }
    const auto& cachePath=settings.reflectionOwnedTextureCacheDirectory;
    const bool cachePathChanged=bool(cachePath)!=bool(m_textureCacheDirectory) ||
        (cachePath && m_textureCacheDirectory && *cachePath!=*m_textureCacheDirectory);
    if(m_shortDistance!=settings.shortReflectionDistance || cachePathChanged || m_textureCacheMode!=settings.reflectionOwnedTextureCache ||
       m_textureCacheMiB!=settings.reflectionOwnedTextureCacheMiB || m_textureQuality!=settings.reflectionOwnedTextureQuality) {
        m_shortDistance=settings.shortReflectionDistance;
        m_textureCacheDirectory=cachePath;m_textureCacheMode=settings.reflectionOwnedTextureCache;
        m_textureCacheMiB=settings.reflectionOwnedTextureCacheMiB;m_textureQuality=settings.reflectionOwnedTextureQuality;
        ++m_preparationRevision;
    }
    m_sourceLighting=settings.reflectionOwnedLighting;
    // Reflections that land only on water the game's fog fully covers are not drawn, allowing for how far the
    // water's lookup may shift.
    m_drawOptions.fogSkip=settings.waterAtmosphere==2 && settings.waterGameFog;
    m_drawOptions.warp=settings.reflectionWarp;
    m_drawOptions.parts=unsigned(settings.reflectionSceneParts);
    const auto liveKey=ObserveDisplays(input,region);
    // Receiver views are resident alongside the scene; camera travel between
    // them does not invalidate assets, preparation or GPU uploads. Legacy test
    // sources without resident views still prepare their single selected body.
    if(m_active && m_active->worldRender && m_active->worldRender->views.empty() &&
       m_activeIdentity.map==input.mapId && m_activeIdentity.receiver==m_receiverRevision &&
       !ReceiverMatches(*m_active,input))++m_receiverRevision;
    m_frame = frame;
    m_coverageTick=GetTickCount();
    m_viewAreas.clear();
    OwnedSceneIdentity identity{input.mapId, m_world ? m_sourceRevision : 1,
        (uint64_t(settings.reflectionOwnedDelayMs) << 1) | uint64_t(settings.reflectionOwnedFail) |
        (m_world ? (uint64_t(1)<<32) | (uint64_t(1)<<33) | (uint64_t(1)<<35) : 0) | // buildings, mip prefix, read batch
        (streaming ? (uint64_t(1)<<36) : 0),streaming?region.x:0,streaming?region.y:0,liveKey,m_receiverRevision,m_preparationRevision};
    identity=SelectRequest(identity,input);
    if (!m_enabled || !(identity == m_identity)) {
        WATER_LOG_INFO("owned request change old=%llu active=%llu map=%d->%d region=(%d,%d)->(%d,%d) live=%llu->%llu receiver=%llu->%llu camera=(%.3f %.3f %.3f)",
            static_cast<unsigned long long>(m_request),static_cast<unsigned long long>(m_activeRequest),m_identity.map,identity.map,
            m_identity.regionX,m_identity.regionY,identity.regionX,identity.regionY,
            static_cast<unsigned long long>(m_identity.liveAssets),static_cast<unsigned long long>(identity.liveAssets),
            static_cast<unsigned long long>(m_identity.receiver),static_cast<unsigned long long>(identity.receiver),
            input.cameraPosition[0],input.cameraPosition[1],input.cameraPosition[2]);
        m_waiting = nullptr;
        ReplacementRenderer().ReleaseDefaultPool();
        // New scenery (map, region or live objects) tries full texture detail again.
        if(m_identity.map!=identity.map || m_identity.regionX!=identity.regionX || m_identity.regionY!=identity.regionY ||
           m_identity.liveAssets!=identity.liveAssets) m_halfTextures=false;
        // Region changes replace scenery, not its source/material contract. Keep
        // the active immutable scene and redraw it from the current camera until
        // the replacement can actually render. Map/config/source changes retire it.
        const bool keep = streaming && m_active && m_activeIdentity.SameSource(identity);
        if (!keep) {
            m_coverage={};
            ActiveRenderer().ReleaseGeometry();
            m_active.reset();
            m_activeRequest = 0;
        }
        m_identity = identity;
        m_enabled = true;
        m_failed = false;
        m_replacementFailed = false;
        m_request = keep && m_activeIdentity == identity ? m_activeRequest : 0;
        m_preparation.SetContext(identity, true);
    }
    if (m_failDraw != settings.reflectionOwnedFailDraw) RetryFailure();
    m_failDraw = settings.reflectionOwnedFailDraw;
    RetireObsolete();
    if (m_failed) return false;
    if (m_replacementFailed) {
        m_reason = "world replacement failed; active coverage unavailable";
        return Retain(device,input,settings,"retaining owned world after replacement failure");
    }
    if (!m_request) {
        if (m_world){
            OwnedWorldDescription description{*m_archiveList,static_cast<unsigned>(settings.reflectionOwnedDelayMs),settings.reflectionOwnedFail,true,true,false,true,streaming,{m_identity.map,m_identity.regionX,m_identity.regionY}};
            description.shortDistance=m_shortDistance;
            description.textureMipBias=unsigned(std::max(m_textureQuality,m_halfTextures?1:0));
            if(m_textureCacheMode && m_textureCacheDirectory)description.textureCacheDirectory=*m_textureCacheDirectory;
            description.populateTextures=m_textureCacheMode==2;
            description.textureCacheLimit=uint64_t(m_textureCacheMiB)*1024*1024;
            description.liveDisplays=m_liveDisplays;description.selectReceiver=true;
            std::copy(input.cameraPosition,input.cameraPosition+3,description.receiver.begin());
            if(input.cameraWaterHeightKnown)description.receiver[2]=input.cameraWaterHeight;
            if(m_active && m_activeIdentity.SameSource(m_identity) &&
               m_activeIdentity.regionX==m_identity.regionX && m_activeIdentity.regionY==m_identity.regionY &&
               m_activeIdentity.receiver==m_identity.receiver && m_activeIdentity.preparation==m_identity.preparation)
                description.reuse=m_active;
            m_request=m_preparation.SubmitWorld(description);
        }
        else m_request=m_preparation.Submit(Fixture(settings));
    }
    const auto prepared = m_preparation.Read();
    m_diagnosticState=prepared;
    if(prepared.state!=OwnedPreparationState::Busy && prepared.bake.batches!=m_reportedBakeBatches) {
        const auto& b=prepared.bake;m_reportedBakeBatches=b.batches;
        WATER_LOG_INFO("owned texture bake batches=%llu written=%llu reused=%llu skipped=%llu evicted=%llu failures=%llu cancelled=%llu last=%u disk_bytes=%llu written_bytes=%llu ms=%.3f max_batch_ms=%.3f pending=%d",
            static_cast<unsigned long long>(b.batches),static_cast<unsigned long long>(b.written),static_cast<unsigned long long>(b.reused),
            static_cast<unsigned long long>(b.skipped),static_cast<unsigned long long>(b.evicted),static_cast<unsigned long long>(b.failures),
            static_cast<unsigned long long>(b.cancelled),unsigned(b.last),static_cast<unsigned long long>(b.diskBytes),
            static_cast<unsigned long long>(b.writtenBytes),b.milliseconds,b.maxBatchMilliseconds,int(b.pending));
    }
    if (prepared.state == OwnedPreparationState::Failed && prepared.request == m_request) {
        if (m_world && prepared.worldResult && prepared.worldResult->error==world::SceneError::NoWater)
            WATER_LOG_INFO("owned world request=%llu: no water within reach of region (%d,%d)",static_cast<unsigned long long>(m_request),m_identity.regionX,m_identity.regionY);
        else if (m_world && prepared.worldResult)
            WATER_LOG_INFO("owned world preparation failed request=%llu error=%d archive_error=%d asset=%s",
                static_cast<unsigned long long>(m_request),int(prepared.worldResult->error),int(prepared.worldResult->archiveError),prepared.worldResult->asset.c_str());
        if (streaming && m_active)
            return ReplacementFailed(device,input,settings,"retaining owned world after CPU preparation failed");
        return Fail(m_world ? "CPU world preparation failed" : "CPU fixture preparation failed");
    }
    if (m_active && m_activeIdentity == m_identity) m_waiting = nullptr;
    else if (prepared.state == OwnedPreparationState::Ready && prepared.request == m_request && prepared.identity == m_identity)
        m_waiting = prepared.scene;
    if(m_waiting && m_waiting->worldRender && m_waiting->worldRender->views.empty() && !ReceiverMatches(*m_waiting,input)){
        ++m_receiverRevision;m_waiting=nullptr;
        return Retain(device,input,settings,"retaining scene while changed receiving body is prepared");
    }
    if (m_world && m_waiting && m_reportedPreparation!=m_request) {
        m_reportedPreparation=m_request;const auto& scene=*m_waiting->worldScene;
        WATER_LOG_INFO("owned world prepared partial=1 request=%llu texture_bias=%u models=%u textures=%u instances=%u water=%u omitted_model_refs=%u omitted_wmo_refs=%u omitted_water_surfaces=%u cpu=%llu archive_ms=%.3f assets_ms=%.3f derived_ms=%.3f",
            static_cast<unsigned long long>(m_request),prepared.worldResult?prepared.worldResult->textureMipBias:0u,unsigned(scene.models.size()),unsigned(scene.textures.size()),unsigned(scene.region->instances.size()),unsigned(scene.region->water.size()),
            scene.omittedModelReferences,scene.omittedWmoReferences,scene.omittedWaterSurfaces,static_cast<unsigned long long>(m_waiting->preparedBytes),
            prepared.worldResult?prepared.worldResult->archiveMilliseconds:0,prepared.worldResult?prepared.worldResult->assetMilliseconds:0,prepared.worldResult?prepared.worldResult->derivedMilliseconds:0);
        if(prepared.worldResult){const auto& r=*prepared.worldResult;
            WATER_LOG_INFO("owned world stages tiles_ms=%.1f live_ms=%.1f models_ms=%.1f buildings_ms=%.1f textures_ms=%.1f instances_ms=%.1f lookups=%u lookup_ms=%.1f reads=%u read_ms=%.1f file_checks=%u file_check_ms=%.1f",
                r.stageMilliseconds[0],r.stageMilliseconds[1],r.stageMilliseconds[2],r.stageMilliseconds[3],r.stageMilliseconds[4],r.stageMilliseconds[5],
                r.archive.lookups,r.archive.findMilliseconds,r.archive.reads,r.archive.readMilliseconds,r.archive.fileChecks,r.archive.fileCheckMilliseconds);
            WATER_LOG_INFO("owned scenery reuse used=%d fallback_error=%d source_bytes=%llu model_reach=%.0f",
                int(r.reusedScenery),int(r.reuseError),static_cast<unsigned long long>(r.sourceBytes),r.modelReach);
            WATER_LOG_INFO("owned texture cache hits=%u misses=%u failures=%u open=%u last=%u bytes=%llu cache_ms=%.3f source_ms=%.3f decode_ms=%.3f",
                r.textureCacheHits,r.textureCacheMisses,r.textureCacheFailures,unsigned(r.textureCacheOpen),unsigned(r.textureCacheLast),
                static_cast<unsigned long long>(r.textureCacheBytes),r.textureCacheMilliseconds,r.textureSourceMilliseconds,r.textureDecodeMilliseconds);}
        if(m_waiting->worldRender){unsigned receiving=0;const auto& data=*m_waiting->worldRender;
            for(unsigned i=0;i<scene.region->water.size();++i)receiving+=data.receivers.Body(i)==data.receiverBody;
            WATER_LOG_INFO("owned receiving body=%u tiles=%u of=%u live_assets=%u views=%u flat_views=%u continuations=%u complete_reach=%.1f",data.receiverBody,receiving,
                unsigned(scene.region->water.size()),unsigned(scene.liveAssets.size()),unsigned(data.views.size()),
                unsigned(std::count_if(data.views.begin(),data.views.end(),[](const auto& view){return view.horizontal;})),
                unsigned(std::count_if(data.views.begin(),data.views.end(),[](const auto& view){return view.continuation[0]<view.continuation[1];})),scene.waterCompleteReach);}
        if(m_streaming)WATER_LOG_INFO("owned local region map=%d key=(%d,%d) terrain=%u",m_identity.map,m_identity.regionX,m_identity.regionY,unsigned(scene.region->terrain.size()));
        WATER_LOG_INFO("owned distant terrain tiles=%u vertices=%u bytes=%llu status=%u",scene.horizonTiles,unsigned(scene.region->horizon.vertices.size()),
            static_cast<unsigned long long>(scene.region->horizon.Bytes()),unsigned(scene.horizonError));
        // One summary line per scene; each building and texture issue only at LogLevel 2 (a region holds thousands).
        unsigned groups=0,unsupported=0,attachments=0,attachmentErrors=0,omitted=0,retained=0;
        for(const auto& source:scene.wmoSources){
            groups+=unsigned(source.models.size());unsupported+=int(source.unsupported)!=0;attachments+=unsigned(source.attachments.size());
            attachmentErrors+=int(source.attachmentError)!=0;omitted+=source.omittedDoodads;
            WATER_LOG_DEBUG("owned building groups=%u unsupported=%d attachments=%u attachment_error=%d omitted_doodad_entries=%u asset=%s",
                unsigned(source.models.size()),int(source.unsupported),unsigned(source.attachments.size()),int(source.attachmentError),source.omittedDoodads,source.name.c_str());
        }
        for (const auto& issue : scene.textureIssues) {
            retained+=issue.retainedLevels?1:0;
            if(issue.retainedLevels) WATER_LOG_DEBUG("owned world texture retained_levels=%u rejected_level=%u asset=%s",issue.retainedLevels,issue.rejectedLevel,issue.name.c_str());
            else WATER_LOG_DEBUG("owned world unsupported texture error=%d asset=%s",int(issue.error),issue.name.c_str());
        }
        WATER_LOG_INFO("owned buildings=%u groups=%u unsupported=%u attachments=%u attachment_errors=%u omitted_doodad_entries=%u; textures with retained mips=%u unsupported=%u",
            unsigned(scene.wmoSources.size()),groups,unsupported,attachments,attachmentErrors,omitted,retained,unsigned(scene.textureIssues.size())-retained);
    }
    const bool replacing = m_active && !(m_activeIdentity == m_identity);
    const auto* scene = replacing ? m_waiting : m_active ? m_active.get() : m_waiting;
    if (!scene) {
        m_reason = m_world ? "CPU world preparation pending" : "CPU fixture preparation pending";
        return Retain(device,input,settings,"retaining owned world during CPU preparation");
    }
    // A Reset may have removed the active GPU data while its CPU data survived.
    // Restore that scene first; do not double the configured upload work per frame.
    if (replacing && !ActiveRenderer().HasGeometry() && CanRetain(input))
        return Retain(device,input,settings,"retaining owned world after device restoration");
    auto& renderer = replacing ? ReplacementRenderer() : ActiveRenderer();
    const bool uploadReady = renderer.HasGeometry() || Upload(device,renderer,*scene,input,settings.reflectionOwnedUploadBytes);
    if (!uploadReady) {
        if (m_world && renderer.UploadFailed()) {
            // Fit rather than fail: prepare the same scenery once more with half-resolution textures
            // (about a third less GPU memory), keeping the active scene meanwhile.
            if (!m_streaming && !m_halfTextures && !m_textureQuality) {
                m_halfTextures=true;++m_preparationRevision;
                WATER_LOG_INFO("owned GPU upload refused at full texture detail request=%llu; preparing half-resolution textures",
                    static_cast<unsigned long long>(m_request));
                renderer.ReleaseDefaultPool();m_waiting=nullptr;m_preparation.Retire();
                m_reason="world replacement refitting at half texture detail";
                return Retain(device,input,settings,"retaining owned world while half-resolution textures are prepared");
            }
            if (replacing) return ReplacementFailed(device,input,settings,"retaining owned world after GPU upload failed");
            return Fail("world GPU upload failed");
        }
        m_reason = m_world ? "world upload pending or unavailable" : "fixture upload pending or unavailable";
        return Retain(device,input,settings,"retaining owned world during GPU upload");
    }
    m_drawReplacement = replacing;
    if (ReadyFrame(device,input,renderer,m_world ? "partial owned world ready" : "owned geometry fixture ready")) return true;
    return Retain(device,input,settings,"retaining owned world while replacement targets are unavailable");
}

bool OwnedReflection::Upload(IDirect3DDevice9* device,OwnedSceneRenderer& renderer,const OwnedCpuScene& scene,const FrameInputs& input,unsigned budget)
{
    const unsigned width=(input.viewport.Width+1)/2,height=(input.viewport.Height+1)/2;
    const auto targets=std::size_t(width)*height*(scene.worldRender&&!scene.worldRender->views.empty()?12:8);
    const auto* reusable=&renderer==&ActiveRenderer()?nullptr:&ActiveRenderer();
    if(m_diagnostics){FrameDiagnostics::Scope upload(*m_diagnostics,FrameDiagnostics::Upload);return renderer.Upload(device,scene,budget,targets,reusable);}
    return renderer.Upload(device,scene,budget,targets,reusable);
}
bool OwnedReflection::ReadyFrame(IDirect3DDevice9* device,const FrameInputs& input,OwnedSceneRenderer& renderer,const char* reason)
{
    const unsigned width=(input.viewport.Width+1)/2,height=(input.viewport.Height+1)/2;
    if (!renderer.EnsureTargets(device,width,height)) {
        m_reason = m_world ? "world targets unavailable" : "fixture targets unavailable";
        return false;
    }
    m_frameReady = true;
    m_reason = reason;
    assert(GpuBytes() <= TransitionGpuLimit);
    return true;
}

bool OwnedReflection::ReceiverMatches(const OwnedCpuScene& scene,const FrameInputs& input)
{
    if(!scene.worldScene || !scene.worldRender || scene.worldRender->receivers.Empty())return true;
    float point[]={input.cameraPosition[0],input.cameraPosition[1],input.cameraWaterHeightKnown?input.cameraWaterHeight:input.cameraPosition[2]};
    const auto body=scene.worldRender->receivers.Select(scene.worldScene->region->water,point,true);
    return body==OwnedWaterReceivers::Missing || body==scene.worldRender->receiverBody;
}
bool OwnedReflection::CanRetain(const FrameInputs& input) const
{
    return !RetentionFailure(input);
}
const char* OwnedReflection::RetentionFailure(const FrameInputs& input) const
{
    const world::OwnedWorldRegion active{m_activeIdentity.map,m_activeIdentity.regionX,m_activeIdentity.regionY};
    if(!m_streaming)return "streaming disabled";
    if(!m_active)return "no published scene";
    if(input.mapId!=active.map)return "map changed";
    if(m_active->worldRender && m_active->worldRender->views.empty() && !ReceiverMatches(*m_active,input))return "receiving body changed";
    if(active.Contains(input.cameraPosition,world::OwnedWorldRegion::WaterReach))return nullptr;
    const auto* data=m_active->worldRender.get();
    // Multi-view scenes carry their receiving geometry with them. Each resolve
    // tests that geometry (or its proven flat continuation), so leaving the
    // camera's old water window does not invalidate the resident mirrors.
    // Keep the existing bounded scenery radius; missing receivers remain sky.
    if(data && !data->views.empty() &&
       active.WithinRadius(input.cameraPosition,world::OwnedWorldRegion::MiddleReach))return nullptr;
    // A flat receiver's mirror transform does not stop being valid at the
    // archive window edge. Keep drawing the already resident scenery while a
    // replacement loads, within a bounded radius inside the source window.
    // Mixed/sloped fields and known different water elevations cannot extend.
    if(data && data->horizontalReceiver && active.WithinRadius(input.cameraPosition,world::OwnedWorldRegion::MiddleReach) &&
       (!input.cameraWaterHeightKnown || std::abs(input.cameraWaterHeight-data->receiverHeight)<.1f))return nullptr;
    return "outside compatible receiving-water coverage";
}
bool OwnedReflection::Retain(IDirect3DDevice9* device,const FrameInputs& input,const Config& settings,const char* reason)
{
    m_drawReplacement = false;
    const auto* failure=RetentionFailure(input);
    m_retentionReason=failure?failure:"compatible";
    if(failure!=m_reportedRetention){
        if(failure)WATER_LOG_INFO("owned retention unavailable reason=%s active=%llu requested=%llu camera=(%.3f %.3f %.3f) water_known=%d water_height=%.3f active_region=(%d,%d) requested_region=(%d,%d)",
            failure,static_cast<unsigned long long>(m_activeRequest),static_cast<unsigned long long>(m_request),
            input.cameraPosition[0],input.cameraPosition[1],input.cameraPosition[2],input.cameraWaterHeightKnown,input.cameraWaterHeight,
            m_activeIdentity.regionX,m_activeIdentity.regionY,m_identity.regionX,m_identity.regionY);
        m_reportedRetention=failure;
    }
    if (failure) return false;
    auto& renderer = ActiveRenderer();
    if (!renderer.HasGeometry() && !Upload(device,renderer,*m_active,input,settings.reflectionOwnedUploadBytes)) {
        m_retentionReason="active GPU restoration pending";return false;
    }
    return ReadyFrame(device,input,renderer,reason);
}
bool OwnedReflection::ReplacementFailed(IDirect3DDevice9* device,const FrameInputs& input,const Config& settings,const char* reason)
{
    m_replacementFailed = true;
    ReplacementRenderer().ReleaseDefaultPool();
    m_waiting = nullptr;
    m_preparation.Retire();
    m_reason = "world replacement failed; active coverage unavailable";
    return Retain(device,input,settings,reason);
}

bool OwnedReflection::SameDrawView(const FrameInputs& input) const
{
    return !std::memcmp(m_view, input.cameraRelativeView, sizeof(m_view)) &&
           !std::memcmp(m_projection, input.glProjection, sizeof(m_projection)) &&
           !std::memcmp(m_camera, input.cameraPosition, sizeof(m_camera)) &&
           !std::memcmp(&m_viewport, &input.viewport, sizeof(m_viewport));
}
bool OwnedReflection::Fail(const char* reason)
{
    m_frameReady = m_drawn = false;
    m_failed = true;
    m_reason = reason;
    m_waiting = nullptr;
    ActiveRenderer().ReleaseGeometry();
    ReplacementRenderer().ReleaseDefaultPool();
    m_preparation.Retire();
    if (!m_active) m_request = 0; // compatible active CPU data keeps its source identity through GPU failure
    return false;
}
bool OwnedReflection::Draw(IDirect3DDevice9* device, FrameStamp frame, const FrameInputs& input, OwnedReflectionOutput& output,unsigned receiver)
{
    output = {};
    if (!m_frameReady || frame.session != m_frame.session || frame.generation != m_frame.generation ||
        frame.frame != m_frame.frame) return false;
    if ((m_streaming ? !m_region.Contains(input.cameraPosition,world::OwnedWorldRegion::Hold) : !InCoverage(input)) || input.mapId != m_identity.map) return Fail(m_world ? "world view left coverage after selection" : "fixture view left coverage after selection");
    if (!m_drawReplacement && m_active && !(m_activeIdentity == m_identity) && !CanRetain(input))
        return Fail("retained world view left coverage after selection");
    if (m_failDraw) return Fail(m_world ? "diagnostic world draw failure" : "diagnostic fixture draw failure");
    auto* renderer = m_drawReplacement ? &ReplacementRenderer() : &ActiveRenderer();
    const auto* scene = m_drawReplacement ? m_waiting : m_active ? m_active.get() : m_waiting;
    if (!scene || scene->draws.size() > output.planes.size()/4 || (m_drawn && !SameDrawView(input)))
        return Fail(m_world ? "world view changed after selection" : "fixture view or plane support changed after selection");
    if(m_drawn && receiver!=m_drawnReceiver)m_drawn=false;
    if (!m_drawn) {
        std::optional<FrameDiagnostics::Scope> profile;
        if(m_diagnostics)profile.emplace(*m_diagnostics,FrameDiagnostics::OwnedDraw);
        auto sourceView=input;
        if(m_sourceLighting && input.sceneLightingKnown){
            std::memcpy(sourceView.toLight,input.sceneToLight,sizeof sourceView.toLight);
            sourceView.ambientColor=input.sceneAmbientColor;sourceView.directColor=input.sceneDirectColor;
        }
        if(m_sourceLighting==2){sourceView.ambientColor=0xffffffff;sourceView.directColor=0;}
        if(m_sourceLighting==2||m_sourceLighting==3)sourceView.sunColor=0;
        if(m_sourceLighting==3)sourceView.directColor=0;
        if(m_sourceLighting==4)sourceView.ambientColor=0;
        if(m_reportedSourceLighting!=m_sourceLighting||m_reportedSceneKnown!=input.sceneLightingKnown){
            WATER_LOG_INFO("owned material source lighting=%d nativeKnown=%d direction=(%.6f %.6f %.6f) ambient=%08X direct=%08X fog=%.3f..%.3f",
                m_sourceLighting,input.sceneLightingKnown,sourceView.toLight[0],sourceView.toLight[1],sourceView.toLight[2],
                sourceView.ambientColor,sourceView.directColor,sourceView.fogStart,sourceView.fogEnd);
            m_reportedSourceLighting=m_sourceLighting;m_reportedSceneKnown=input.sceneLightingKnown;
        }
        if(m_world && scene->worldScene && !scene->worldScene->region->horizon.vertices.empty() &&
           scene->worldRender && (receiver<scene->worldRender->views.size()?scene->worldRender->views[receiver].horizontal:scene->worldRender->horizontalReceiver) && sourceView.fogEnd>sourceView.fogStart && !sourceView.CameraUnderSurface())
            math::ExtendClientFarPlane(sourceView.glProjection,world::HorizonReach+world::OwnedWorldRegion::MiddleReach+128);
        auto options=m_drawOptions;options.receiver=receiver;
        options.receiverAreaKnown=ViewArea(input,receiver,options.receiverArea);
        options.census=scene->worldScene && GetTickCount()-m_liveLog>2000; // the frame whose stats are logged below
        renderer->SetOptions(options);
        if (!renderer->Draw(device, sourceView, *scene)) return Fail(m_world ? "world GPU draw failed" : "fixture GPU draw failed");
        if (!m_active || m_drawReplacement) {
            // A try-lock miss leaves the immutable payload in its occupied slot.
            // It may back this published draw safely; retry the transfer next frame.
            if (auto accepted = m_preparation.Accept(m_drawReplacement)) {
                const bool replacing = m_drawReplacement;
                const auto start = std::chrono::steady_clock::now();
                if (m_drawReplacement) {
                    m_currentRenderer ^= 1;
                    // The old CPU payload stays alive through GPU retirement.
                    ReplacementRenderer().ReleaseDefaultPool();
                    m_drawReplacement = false;
                }
                const auto released = std::chrono::steady_clock::now();
                if (replacing) m_preparation.FinishReplacement(std::move(m_active));
                m_active = std::move(accepted);
                if (replacing)
                    WATER_LOG_INFO("owned scenery retirement frame=%llu gpu_ms=%.3f cpu_ms=%.3f",
                        static_cast<unsigned long long>(frame.frame),
                        std::chrono::duration<double,std::milli>(released-start).count(),
                        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-released).count());
                m_activeIdentity = m_identity;
                m_activeRequest = m_request;
                m_waiting = nullptr;
                scene = m_active.get();
            }
        }
        std::memcpy(m_view, input.cameraRelativeView, sizeof(m_view));
        std::memcpy(m_projection, input.glProjection, sizeof(m_projection));
        std::memcpy(m_camera, input.cameraPosition, sizeof(m_camera));
        m_viewport = input.viewport;
        m_drawn = true;m_drawnReceiver=receiver;
        if(auto* row=m_diagnostics?m_diagnostics->Current():nullptr){
            const auto& stats=renderer->Stats();
            row->ownedDrawn=true;row->ownedCells+=stats.terrainCells;row->ownedInstances+=stats.instances;
            row->ownedFogSkipped+=stats.fogSkipped;row->ownedPastFog+=stats.pastFog;row->ownedTriangles+=stats.triangles;
        }
        if(scene->worldScene && GetTickCount()-m_liveLog>2000){
            m_liveLog=GetTickCount();
            const auto& stats=renderer->Stats();
            WATER_LOG_INFO("owned live reflection captured=%u cached_displays=%u model_draws=%u fog_skipped=%u past_fog=%u request=%llu frame=%llu active_request=%llu",input.reflectionObjectCount,unsigned(scene->worldScene->liveAssets.size()),stats.liveDraws,stats.fogSkipped,stats.pastFog,static_cast<unsigned long long>(m_request),static_cast<unsigned long long>(frame.frame),static_cast<unsigned long long>(PublishedRequest()));
            WATER_LOG_INFO("owned scene parts=%u terrain_cells=%u terrain_sets=%u instances=%u models=%u model_batches=%u batches=%u draws=%u triangles=%llu pose_ms=%.3f size_batches=%u/%u/%u/%u/%u/%u/%u size_triangles=%llu/%llu/%llu/%llu/%llu/%llu/%llu",
                m_drawOptions.parts,stats.terrainCells,stats.terrainSets,stats.instances,stats.models,stats.modelBatches,stats.batches,stats.draws,static_cast<unsigned long long>(stats.triangles),stats.poseMs,
                stats.sizeBatches[0],stats.sizeBatches[1],stats.sizeBatches[2],stats.sizeBatches[3],stats.sizeBatches[4],stats.sizeBatches[5],stats.sizeBatches[6],
                static_cast<unsigned long long>(stats.sizeTriangles[0]),static_cast<unsigned long long>(stats.sizeTriangles[1]),static_cast<unsigned long long>(stats.sizeTriangles[2]),
                static_cast<unsigned long long>(stats.sizeTriangles[3]),static_cast<unsigned long long>(stats.sizeTriangles[4]),static_cast<unsigned long long>(stats.sizeTriangles[5]),
                static_cast<unsigned long long>(stats.sizeTriangles[6]));
            for(unsigned i=0;i<std::min(input.reflectionObjectCount,kMaxReflectionObjects);++i){const auto& object=input.reflectionObjects[i];
                WATER_LOG_INFO("owned live pose id=%llu display=%u position=(%.3f %.3f %.3f)",static_cast<unsigned long long>(object.identity),object.display,object.transform[12],object.transform[13],object.transform[14]);}
        }
    }
    output.colour = renderer->Colour(); output.depth = renderer->Depth();
    output.projection = renderer->Projection();
    if(scene->worldRender && receiver<scene->worldRender->views.size() && !input.CameraUnderSurface()){
        output.continuation=scene->worldRender->views[receiver].continuation;
        const auto& source=*scene->worldScene;
        if(m_streaming && source.waterCompleteReach>0){
            m_coverage.Update(input.cameraPosition,source.sourceRegion,source.waterCompleteReach,m_coverageTick,m_shortDistance);
            output.coverageEdges=m_coverage.edges;
            output.coverageWidth=m_coverage.width;
        }
    }
    if (scene->worldScene) {output.heights=true;output.underwater=input.CameraUnderSurface();output.height=input.cameraWaterHeight;return output.colour && output.depth && output.projection;}
    for (unsigned i = 0; i < scene->draws.size(); ++i)
        for (unsigned c = 0; c < 4; ++c) output.planes[i*4+c] = scene->draws[i].plane[c];
    output.height = scene->draws[0].plane[3] / scene->draws[0].plane[2];
    return output.colour && output.depth && output.projection;
}

unsigned OwnedReflection::ViewCount()const
{
    const auto* scene=m_drawReplacement?m_waiting:m_active?m_active.get():m_waiting;
    return scene && scene->worldRender?unsigned(scene->worldRender->views.size()):0;
}
bool OwnedReflection::ViewArea(const FrameInputs& in,unsigned view,RECT& area)const
{
    const auto* scene=m_drawReplacement?m_waiting:m_active?m_active.get():m_waiting;
    if(!scene || !scene->worldRender || view>=scene->worldRender->views.size())return false;
    if(m_viewAreas.empty())m_viewAreas.resize(scene->worldRender->views.size(),RECT{0,0,-1,0});
    auto& cached=m_viewAreas[view];
    if(cached.right<0){
        const auto& receiver=scene->worldRender->views[view];
        bool visible=ReceiverViewScreenArea(in,receiver,scene->worldScene->region->water,cached);
        const bool continued=receiver.continuation[0]<receiver.continuation[1];
        if(m_liquidCoverage && m_liquidCoverage->Complete()){
            // A continuation can lie beyond the imported geometry. Finite views
            // also intersect their source footprint: other pools at the same
            // height cannot make an otherwise absent body visible.
            const std::array<float,2> heights=continued?receiver.continuation:
                std::array<float,2>{receiver.bounds[2]-.75f,receiver.bounds[5]+.75f};
            RECT native{};
            const bool drawn=ObservedReceiverScreenArea(in,*m_liquidCoverage,heights,native,continued?nullptr:&receiver.bounds);
            if(continued){cached=native;visible=drawn;}
            else if(visible && drawn){
                cached.left=std::max(cached.left,native.left);cached.top=std::max(cached.top,native.top);
                cached.right=std::min(cached.right,native.right);cached.bottom=std::min(cached.bottom,native.bottom);
                visible=cached.left<cached.right && cached.top<cached.bottom;
            }else visible=false;
        }else if(continued){
            const auto& source=*scene->worldScene;const auto& region=source.sourceRegion;
            const float reach=source.waterCompleteReach;
            const std::array<float,4> complete={region.Centre(0)-reach,region.Centre(1)-reach,
                                               region.Centre(0)+reach,region.Centre(1)+reach};
            RECT extra{};
            if(ReceiverHeightScreenArea(in,receiver.continuation,extra,reach>0?&complete:nullptr)){
                if(!visible)cached=extra;
                else {cached.left=std::min(cached.left,extra.left);cached.top=std::min(cached.top,extra.top);
                      cached.right=std::max(cached.right,extra.right);cached.bottom=std::max(cached.bottom,extra.bottom);}
                visible=true;
            }
        }
        if(!visible)cached={};
    }
    area=cached;return area.left<area.right && area.top<area.bottom;
}
IDirect3DTexture9* OwnedReflection::ReceiverMask()const
{
    return m_renderers[m_currentRenderer^(m_drawReplacement?1:0)].ReceiverMask();
}

void OwnedReflection::EndFrame()
{
    m_liquidCoverage=nullptr;
    m_frame = {};
    m_frameReady = m_drawn = false;
    m_drawReplacement = false;
    ActiveRenderer().BeginFrame();
    ReplacementRenderer().BeginFrame();
}
void OwnedReflection::RetryFailure()
{
    if ((m_failed || m_replacementFailed) && m_active && !(m_activeIdentity == m_identity))
        m_request = 0;
    m_failed = m_replacementFailed = false;
}
void OwnedReflection::LoseDevice()
{
    m_coverage={};
    EndFrame();
    ActiveRenderer().ReleaseDefaultPool();
    ReplacementRenderer().ReleaseDefaultPool();
    RetryFailure(); // Compatible CPU data survives; GPU generation never does.
}
void OwnedReflection::ReleaseAll()
{
    Disable();
    ActiveRenderer().ReleaseAll();
    ReplacementRenderer().ReleaseAll();
}
std::size_t OwnedReflection::ReplacementCpuBytes() const
{
    const auto result = m_preparation.Read();
    return result.state == OwnedPreparationState::Busy ? (m_world ? OwnedScenePreparation::WorldPreparationLimit : OwnedScenePreparation::FixtureCpuLimit) : result.replacementBytes;
}
}
