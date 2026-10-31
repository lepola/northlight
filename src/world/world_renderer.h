#pragma once
#include "settings_schema.h"
#include "stream_hooks.h"
#include "weather_memory.h"
#include "dither_noise.h"
#include "name_stencil.h"
#include "world_context.h"
#include "effect_switches.h"
#include "world_camera.h"
#include "wmo_context.h"
#include "underwater_fade.h"
#include "projection.h"
#include "celestial_context.h"
#include "celestial_sources.h"
#include "celestial_glow.h"
#include "apparent_horizon.h"
#include "twilight_fill.h"
#include "night_floor.h"
#include "regional_fog.h"
#include "regional_shadow_range.h"
#include "celestial_profiles.h"
#if NORTHLIGHT_DEV_TUNING
#include "zone_tuning.h"
#endif
#include "world_gi.h"
#include "world_local_lights.h"
#include "local_light_selection.h"
#include "equipment_lights.h"
#include "particle_lights.h"
#include "particle_uploads.h"
#include "equipment_light_proof.h"
#include "local_light_scissor.h"
#include "local_light_batches.h"
#include "point_light_shadow.h"
#include "local_light_compiled_shaders.h"
#include "replay_bounds.h"
#include "replay_bounds_schedule.h"
#include "replay_bounds_metadata.h"
#include "replay_bounds_job.h"
#include <atomic>
#include <optional>
#include "world_math.h"
#include "patch_terrain_shadow_coverage.h"
#include "world_mesh_plan.h"
#include "world_streaming.h"
#include "world_mesh_pages.h"
#include "world_mesh_page_gpu.h"
#include "streaming_budget.h"
#include "streaming_phase_profile.h"
#include "cpu_retirement.h"
#include "deferred_gpu_release.h"
#include "geometry_memory.h"
#include "address_space_snapshot.h"
#include "terrain_reach_fallback.h"
#include "mesh_pool_trim.h"
#include "mesh_buffer_store.h"
#include "memory_admission_probe.h"
#include <chrono>
#include "world_probe_cache.h"
#include "world_probe_progress.h"
#include "probe_activation.h"
#include "probe_blend.h" // 0.3.197
#include "probe_texture_upload.h"
#include "patch_terrain_shadow.h"
#include "celestial_time_warp.h"
#include "cascade_anchor.h"
#include "actor_shadow_map.h" // 0.3.209 (task 29)
#include "actor_side_hold.h" // the inset hysteresis of the actor map
#include "actor_side_tracker.h" // crowd/self shimmer diagnostics: actor map vs near cascade per caster
#include "shadow_pivot.h"
#include "world_dynamic_probes.h"
#include "shadow_bounds.h"
#include "static_cache_slices.h"
#include "terrain_shadow_candidates.h"
#include "legacy_fog.h"
#include "spell_fog.h"
#include "spell_fog_proof.h"
#include "spell_fog_march.h"
#include "horizon_haze.h"
#include "weather_state.h"
#include "weather_effects.h"
#include "fog_clouds.h"
#include "gpu_budget.h" // 0.3.200 (gpu budget)
#include "job_system.h" // 0.3.200 (jobs)
#include "sun_hue.h"
#include "replay_constant_ranges.h"
#include "replay_pose_groups.h"
#include "replay_capture_constants.h"
#include "replay_shadow_policy.h"
#include "shader_constant_usage.h"
#include "capture_phase_profile.h"
#include "world_compiled_shaders.h"
#include "patch_shadow_shader.h"
#include "terrain_capture_bounds.h"
#include "draw_snapshot.h"
#include "replay_gpu_cache.h"
#include "replay_gpu_batches.h"
#include "replay_bulk_layout.h"
#include "dynamic_ring.h"
#include "lock_meter.h"
#include "replay_copies.h"
#include "vertex_declaration_cache.h"
#include "replay_draw_state.h"
#include "render_thread_probe.h"
#include "actor_deformation.h"
#include "sampled_vertex_cache.h"
#include "prepare_worker.h"
#include "shadow_fate.h"
#include "actor_texture.h"
#include "actor_scene_job.h"
#include "worker_actor_memo.h"
#include <set>
#include <unordered_set>
#include <map>
#include <tuple>
#include "world_shader_signatures.h"
#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <stdexcept>
#include "world_diagnostics.h"
#include "shadow_terrain.h"
#include "live_terrain_gpu.h"
#include "static_shadow_request.h"
#include "static_shadow_gpu.h"
#include "static_plan_prebuild.h"
#include "static_shadow_dedup.h"
#include "static_m2_dedup.h"
#include "static_shadow_coverage.h"
#include "celestial_terrain.h"
#include "quality_settings.h"
#include "diagnostics_switch.h"
#include "memory_switches.h"
#include "gi_solve_pool.h"
#include "near_reserve.h"
#include "capture_admission.h"
#include "effects_buckets.h"
#include <cstdarg>
#include "rigid_memory.h"

// 0.3.212 (task 32): the name mask's composite bind, shared by WorldComposite and the renderer's legacy composite. With a depth-stencil holding this frame's name marks the quad draws with it bound and
// kComposite set (no depth test, stencil NOTEQUAL on the mark bit, nothing written), so marked pixels keep the game's colour; the replaced depth-stencil and the three states that matter after it (STENCILENABLE,
// ZENABLE, ZWRITEENABLE; the other stencil states have no effect while STENCILENABLE is off) are restored when the scope ends. No marks (null): no calls. No depth bound before is the normal case:
// GetDepthStencilSurface then returns D3DERR_NOTFOUND and the previous depth is restored as null.
struct NameMaskCompositeBind {
    IDirect3DDevice9* device;IDirect3DSurface9* previousDepth=nullptr;DWORD previous[3]={};bool bound=false;
    static constexpr D3DRENDERSTATETYPE kRestored[3]={D3DRS_STENCILENABLE,D3DRS_ZENABLE,D3DRS_ZWRITEENABLE};
    NameMaskCompositeBind(IDirect3DDevice9* d,IDirect3DSurface9* maskDepth):device(d){
        if(!maskDepth)return;
        const HRESULT got=device->GetDepthStencilSurface(&previousDepth);
        if((SUCCEEDED(got)||got==D3DERR_NOTFOUND)&&SUCCEEDED(device->SetDepthStencilSurface(maskDepth))){
            bound=true;for(unsigned i=0;i<3;++i)device->GetRenderState(kRestored[i],&previous[i]);
            for(const auto& set:NorthlightNameStencil::kComposite)device->SetRenderState(D3DRENDERSTATETYPE(set.state),set.value);
        }
    }
    ~NameMaskCompositeBind(){
        if(bound){for(unsigned i=0;i<3;++i)device->SetRenderState(kRestored[i],previous[i]);device->SetDepthStencilSurface(previousDepth);}
        if(previousDepth)previousDepth->Release();
    }
    NameMaskCompositeBind(const NameMaskCompositeBind&)=delete;NameMaskCompositeBind& operator=(const NameMaskCompositeBind&)=delete;
};

// Included after SavedState. Worker never accesses D3D or game memory.
class WorldRenderer {
    using V=NorthlightGI::Vec3;
    /* 0.3.137: render-thread commit data built by the GI worker from the same
       immutable plan (see PreparedCommitData). Null fields fall back to the
       0.3.136 render-thread copies. */
    using FixedChunks=std::set<std::pair<int,int>>;
    struct PreparedCommit {const void* plan=nullptr; /* identity of the source plan; checked at commit */
        std::shared_ptr<const StaticShadow::OwnerSet> owners;std::shared_ptr<const FixedChunks> fixed;};
    static constexpr bool PreparedCommitData=StaticShadow::PreparedCoveredOwners;
    struct Snapshot {
        std::shared_ptr<NorthlightGI::BVH> bvh;
        std::shared_ptr<NorthlightWorldMesh::WorldMeshUploadPlan> meshPlan;
        std::vector<NorthlightGI::ProbeAtlasEntry> atlas;
        std::vector<NorthlightLocalLights::Light> localLights;
        V origin, center;
        std::string map, message;
        uint64_t serial=0;
        std::shared_ptr<const NorthlightRegionalFog::Field> fogField;
        unsigned validProbes=0;
        uint64_t requestId=0;
        DWORD requestedAt=0,queueMs=0,geometryMs=0,actorMs=0,solveMs=0;
        unsigned actorTexturesDecoded=0;size_t actorTextureEncodedBytes=0;
        size_t retirementBytes=0;
        std::shared_ptr<const PreparedCommit> prepared; /* paired with meshPlan */
        unsigned reusedProbes=0,solvedProbes=0,superseded=0,dynamicReused=0,dynamicSolved=0;
        unsigned processedProbes=0,retargeted=0;
        uint64_t lightingGeneration=0;
        NorthlightGI::MovingSolveStats movingStats;size_t pathRecordBytes=0;
        NorthlightGI::ProbeRelightCache::Stats relightStats;
        bool staticOnly=false,partial=false;
    };
    // probeCenter (0.3.153 GIProbeAhead): probe window origin, solve order and move trigger; geometryCenter (0.3.169 lead):
    // the region build centre and its refresh trigger; camera (eye) for everything else, including every coverage check.
    struct Request { std::shared_ptr<const NorthlightActorGeometry::ActorJob> actorJob;uint64_t actorSerial=0; V camera,probeCenter,geometryCenter; NorthlightGI::Lighting light; std::string map; uint64_t id=0,baseId=0;DWORD queuedAt=0;unsigned reason=0;bool giEnabled=true; };
    IDirect3DDevice9* d;
    std::string root;
    std::unique_ptr<StaticShadow::Streamer> staticStream;
    StaticShadow::GpuCache staticCasters;
    std::shared_ptr<const StaticShadow::Snapshot> staticScene;
    StaticShadowDedup::Matcher staticMatcher;
    StaticM2Dedup::Memory staticM2Memory;unsigned long long staticM2Frame=0; /* rigid M2 doodads the static cache draws: see prepareStaticM2 */
    unsigned staticM2Skipped=0,staticM2Verified=0,staticM2Rejected=0;
    V staticFramePivot;
    bool staticPivotReady=false,staticAllowLoads=true;
    DWORD staticMemoryTick=0,staticLogTick=0,staticRetryTick=0;
    uint64_t staticFrame=0,staticAdmissionFrame=UINT64_MAX,staticReservedBytes=0;
    NorthlightGeometryMemory::Sample staticMemorySample;
    bool staticMemoryExact=true;
    NorthlightMemoryAdmission::Probe admissionProbe;
    unsigned staticDrawFailures=0,staticDedupSkipped=0,staticDedupCursor=0,staticDedupAttempts=0,staticDedupMatches=0;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable wake;
    bool stopping=false,pending=false;
    NorthlightGeometryMemory::StallWatch generationStall; /* 0.3.156, guarded by mutex: builder marks, render thread reports */
    static constexpr uint32_t GenerationStallLogMs=10000;
    std::atomic<bool> workerBusy{false};DWORD actorRequestTick=0;
    std::atomic<unsigned> workerFaultCode{0};
    std::atomic<unsigned> geometryBuildEstimateMs{1000}; /* 0.3.169: builder's smoothed build time, read by the render thread's lead */
    bool settingsRequestDirty=false;
public:
    void applyLiveSettings(const NorthlightSettings::Config& next){
        // Frame cleanup has joined D3D-free jobs. Worker solver keys stay immutable;
        // GI enable and bounce count travel in each Request.
        auto profiles=next.celestial;
#if NORTHLIGHT_DEV_TUNING
        celestialShipped=profiles;profiles=NorthlightZoneTuning::applyCelestial(celestialShipped,*devSnapshot()); /* the saved table is the new shipped base; the developer overrides stay on top */
#endif
        const auto effective=NorthlightSettings::liveQualityUpdate(quality,next.quality);
        const bool giRequest=NorthlightSettings::giRequestChanged(quality,effective,celestialProfiles,profiles);
        const bool actorResources=quality.actorShadows!=effective.actorShadows||quality.actorShadowDetail!=effective.actorShadowDetail;
        const bool directions=quality.shadowDirectionSteps!=effective.shadowDirectionSteps;
        const bool giToggle=quality.gi!=effective.gi;
        for(const auto& k:NorthlightQuality::Keys)if(NorthlightSettings::liveQuality(k.name))quality.*k.field=effective.*k.field;
        quality.preset=effective.preset;settingsRequestDirty=settingsRequestDirty||giRequest;
        if(giToggle)setEffects(requestedEffects); /* the user's F8 state through giPass, with setEffects' temporal reset */
        minSkinnedShadowTriangles=quality.minSkinnedTriangles;captureBudgetMiB=quality.captureBudgetMiB;actorShadowBudgetMiB=quality.actorShadowBudgetMiB;setNearReserve();
        replaySnapshots.configureBudget(size_t(captureBudgetMiB)*1048576,size_t(captureBudgetMiB)*524288);
        shadowFateDiagnostics=NorthlightQuality::shadowFate(quality);
        NorthlightDiagnostics::configure(quality.diagnostics!=0);NorthlightWeatherMemory::configure(quality.weatherMemory!=0);NorthlightMemorySwitches::configure(quality.memMap!=0);
        NorthlightRenderThreadProbe::configure(NorthlightQuality::renderProfile(quality),NorthlightQuality::replayProbe(quality));
        NorthlightWorldContext::SelfReadStats::timed.store(NorthlightQuality::renderProfile(quality),std::memory_order_relaxed);
        if(actorResources)releaseGPU(true);else if(directions)invalidateShadowCache();
        staticPrebuild.steps=float(quality.shadowDirectionSteps);
        celestialProfiles=std::move(profiles);paletteFrameValid=false;
    }
    // Read-only access to the COMMITTED terrain pages. Never start uploads,
    // loads, shadow updates or mesh publication from the native sky draw hook.
    uint64_t celestialTerrainGeneration()const{
        char map[64]={};float camera[3];
        if(failed||meshStore.table().empty()||batches.empty()||!NorthlightWorldContext::readMapAndCamera(map,camera)||uploadedMap!=map)return 0;
        return meshGeneration+1;
    }
    // 0.3.175 (S3): the renderer's effects buckets (RenderProfile sample frames; marks are no-ops otherwise).
    void setEffectsBuckets(NorthlightEffectsBuckets::Frame* frame){effectsBuckets=frame;}
    // 0.3.175 (S1): the mask draws this generation's terrain list (terrainBatchList, rebuilt at the
    // mesh commit, never here) in merged runs (NorthlightCelestialTerrain::forEachRun: exact). A list
    // of another generation falls back to the whole batch list, one run per batch as before.
    bool drawCelestialTerrain(unsigned body,const float* matrix){
        if(!celestialTerrainGeneration())return false;
        const int64_t started=NorthlightRenderThreadProbe::profiling()?QpcClock::now():0;
        UINT page=UINT_MAX;NorthlightCelestialTerrain::RunStats runs;
        const NorthlightCelestialTerrain::Frustum frustum(matrix);
        auto accept=[&](const Batch& b){return b.terrain&&!frustum.reject(b.boundsLow,b.boundsHigh);};
        auto emit=[&](const NorthlightCelestialTerrain::Run& r){Batch key;key.page=r.page;
            return bindMeshPage(key,page)&&SUCCEEDED(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,r.minVertex,UINT(r.vertexEnd-r.minVertex),r.start,r.count));};
        const bool listed=terrainBatchListGeneration==meshGeneration&&terrainBatchListSize==batches.size();
        bool ok;
        if(listed)ok=NorthlightCelestialTerrain::forEachRun(batches,terrainBatchList,NorthlightWorldMeshPages::PageIndexLimit,accept,emit,runs);
        else{ok=true;for(const auto& b:batches){if(!accept(b))continue;++runs.accepted;++runs.runs;runs.triangles+=b.count;
            if(!bindMeshPage(b,page)||FAILED(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,b.minVertex,b.vertexCount,b.start,b.count))){ok=false;break;}}}
        if(!ok)return false;
        auto& m=celestialMask;++m.redraws[body&1];m.accepted=runs.accepted;m.runs=runs.runs;m.triangles=runs.triangles;m.candidates=listed?terrainBatchList.size():batches.size();m.listed=listed;
        if(started&&captureFrequency.QuadPart>0){m.ms=double(QpcClock::now()-started)*1000.0/double(captureFrequency.QuadPart);m.peakMs=std::max(m.peakMs,m.ms);}
        const DWORD now=GetTickCount();
        if(NorthlightDiagnostics::enabled()&&(!m.lastLog||now-m.lastLog>=10000)){m.lastLog=now;
            logf("CELESTIAL terrain mask map=\"%s\" generation=%llu draws=%u triangles=%llu candidates=%zu accepted=%u runs=%u listed=%d redraws=%u,%u reuses=%u,%u ms=%.3f peakMs=%.3f",
                uploadedMap.c_str(),(unsigned long long)meshGeneration,m.runs,(unsigned long long)m.triangles,m.candidates,m.accepted,m.runs,int(m.listed),
                m.redraws[0],m.redraws[1],m.reuses[0],m.reuses[1],m.ms,m.peakMs);
            m.redraws[0]=m.redraws[1]=m.reuses[0]=m.reuses[1]=0;m.peakMs=0;}
        return true;
    }
    void noteCelestialTerrainReuse(unsigned body){++celestialMask.reuses[body&1];}
    unsigned workerFault()const noexcept {return workerFaultCode.load();}
    static const char* workerFaultMessage(unsigned code)noexcept {
        return code==1?"GI worker allocation failed":code==2?"GI worker exception":code==3?"GI worker unknown exception":"";
    }
private:
    Request request,lastRequest;
    std::shared_ptr<Snapshot> published,active;
    std::weak_ptr<Snapshot> observedPublication;
    std::string loggedCacheMessage; // last logged "WORLD cache" message: uncached maps republish the same error per GI request, log it once
    std::weak_ptr<NorthlightGI::BVH> uploaded; // GPU commit must not retain the CPU scene.
    std::vector<float> uploadedAlphaCutoffs;
    uint64_t uploadedSerial=0;
    std::shared_ptr<const StaticShadow::OwnerSet> uploadedStaticOwners;
    uint64_t staticOwnerGeneration=UINT64_MAX;
    uint64_t meshGeneration=0; // committed mesh identity; content signatures decide shadow reuse
    NorthlightEffectsBuckets::Frame* effectsBuckets=nullptr;
    void bucket(NorthlightEffectsBuckets::Bucket b){if(effectsBuckets)effectsBuckets->mark(b);}
    NorthlightProbeActivation probeActivation;
    NorthlightProbeBlend::Mirror probeBlend; // 0.3.197: same-key re-publication blend (probePrev, s8 in the GI pass)
    unsigned probeBlendPublishes=0,probeBlendSlots=0; // 0.3.197: per LOCAL log interval
    bool valid=false,failed=false,reportedContext=false;
    unsigned traceContext=0; /* 0.3.200 (frame trace): this frame's context path: 0 none, 1 terrain+global light, 2 terrain native light (camera disagreed), 3 WMO */
    unsigned contextRejects=0,frames=0,slowReports=0;
    DWORD diagnosticTick=0;
    // 0.3.169 coverage hold and geometry lead (render thread). coverMax: largest eye-to-active
    // distance since the last WORLD camera line. A hold episode is active beyond 96 units.
    NorthlightWorldStreaming::Motion geometryMotion;float geometryLead=0,coverMax=0,holdMax=0;
    bool coverageHold=false;DWORD holdStart=0;unsigned holdEpisodes=0;
    const char* skipReason="";
    bool gpuDiagnosticArmed=true;unsigned gpuDiagnosticCaptures=0;
    std::string gpuDiagnosticDirectory;
    IDirect3DTexture9* regionalFogTexture=nullptr;
    IDirect3DTexture9* neutralWhite=nullptr; // 0.3.209 (task 29): 1x1 white on s12 (ActorVisibility) for WorldLighting on frames without an ActorShadow pass
    IDirect3DTexture9* neutralZero=nullptr; // 0.3.202 (rain): 1x1 (0,0,0,0) on s13 (RainMask) when no mask was drawn this frame
    IDirect3DTexture9* ditherNoise=nullptr; // WorldComposite s2: 64x64 signed blue-noise offsets (dither_noise.h); neutralZero when missing
    IDirect3DSurface9* nameMaskDepth=nullptr; // 0.3.212 (task 32): non-owning, set per frame before render(), consumed by the composite quad
    IDirect3DTexture9* rainMask=nullptr; // 0.3.202 (rain): the proxy's rain streak alpha mask (non-owning, set per frame, cleared after the composite)
    IDirect3DTexture9* neutralAO=nullptr; // 0.3.174: 1x1 (0,0,0,1) on s10 when the proxy does not fold its AO/bloom
    std::shared_ptr<const NorthlightRegionalFog::Field> uploadedFogField;
    NorthlightWorldContext::TerrainContext context;
    NorthlightCelestial::Context celestial,celestialLight;
    unsigned celestialOrbitReports=0;
    NorthlightCelestialOrbit::LightMotion celestialLightMotion;
    NorthlightApparentHorizon::Smooth apparentHorizon; // the camera's far edge for the discs (celestialContext), one frame behind
    std::string celestialLightMap;V celestialLightCamera;
    bool continuousCelestialShadows=false;
    NorthlightCelestialProfiles::Table celestialProfiles;
    NorthlightRegionalShadow::Table shadowRanges;
    NorthlightCelestialProfiles::Transition paletteTransition;
    NorthlightCelestialProfiles::Profile framePalette;
    bool paletteFrameValid=false;std::string paletteFrameMap;DWORD paletteLogAt=0;
#if NORTHLIGHT_DEV_TUNING
    // Developer-only zone tuning (scripts/dev_tuning/zone_tuner.py), never compiled into a release build. Two files in the game root:
    // northlight-tuning.ini (polled, render thread) and northlight-tuning-state.json (written). celestialProfiles holds the EFFECTIVE table
    // (shipped + overrides); celestialShipped keeps the pristine one. The worker reads terrain_radius / ground fog scales from devOverrides.
    NorthlightCelestialProfiles::Table celestialShipped;
    std::shared_ptr<const NorthlightZoneTuning::Overrides> devOverrides=std::make_shared<const NorthlightZoneTuning::Overrides>(); // guarded by devMutex
    std::mutex devMutex;
    std::atomic<float> devTerrainReach{0};
    std::shared_ptr<const NorthlightZoneTuning::Overrides> devBuilt; // guarded by devMutex: the overrides snapshot of the last published build
    std::atomic<unsigned> devBuilds{0};                              // published builds so far
    std::string devError;unsigned devRevision=0;
    DWORD devPollAt=0,devStateAt=0,devChangeAt=0,devRequestedAt=0;bool devStampSeen=false;uint64_t devStamp[2]={};
    unsigned devSeenRevision=~0u,devBuildsAtRequest=0; // render thread: the revision last seen (for the quiet timer), builds counted at the last request
    std::shared_ptr<const NorthlightZoneTuning::Overrides> devSnapshot(){std::lock_guard<std::mutex> lock(devMutex);return devOverrides;}
    std::shared_ptr<const NorthlightZoneTuning::Overrides> devBuiltSnapshot(){std::lock_guard<std::mutex> lock(devMutex);return devBuilt;}
    static std::wstring devPath(const wchar_t* name){return std::wstring(rootPath)+name;}
    void devReload(){
        std::string text,error;const bool present=readSmallFile(devPath(L"northlight-tuning.ini"),text);
        NorthlightZoneTuning::Overrides next;std::istringstream in(text);
        if(present&&!NorthlightZoneTuning::parse(in,next,error)){devError=error;logf("TUNING overrides revision=%u keys=%u error=%s",devRevision,devSnapshot()->count(),error.c_str());return;}
        devError.clear();
        next.revision=devRevision+1;
        const auto published=std::make_shared<const NorthlightZoneTuning::Overrides>(std::move(next));
        celestialProfiles=NorthlightZoneTuning::applyCelestial(celestialShipped,*published);
        {std::lock_guard<std::mutex> lock(devMutex);devOverrides=published;}
        logf("TUNING overrides revision=%u keys=%u error=%s",++devRevision,published->count(),"");
    }
    void devWriteState(const char* map,const float* camera,uint32_t zone){
        const auto ov=devSnapshot();NorthlightZoneTuning::State s;
        s.build="dev-tuning";s.tick=GetTickCount();s.inWorld=true;s.map=map;s.zone=zone;std::memcpy(s.camera,camera,sizeof s.camera);
        s.night=NorthlightRegionalFog::nightFactor(celestialValid?celestial.dayFraction:-1.f);
        s.enabled=ov->enabled;s.loaded=ov->enabled?ov->count():0;s.groundFogActive=NorthlightZoneTuning::groundFogActive(zone);s.error=devError;s.revision=devRevision;{const auto built=devBuiltSnapshot();s.builtRevision=built?built->revision:0;}
        s.base=NorthlightZoneTuning::baseValues(celestialShipped,shadowRanges,s.map,zone);
        s.effective=NorthlightZoneTuning::effectiveValues(s.base,celestialProfiles,*ov,s.map,zone);
        const float reach=devTerrainReach.load();s.terrainReach=reach>0?reach:s.effective.terrainRadius;
        const std::string json=NorthlightZoneTuning::stateJson(s);
        const std::wstring path=devPath(L"northlight-tuning-state.json"),tmp=path+L".tmp";
        HANDLE f=CreateFileW(tmp.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return;
        DWORD wrote=0;const bool ok=WriteFile(f,json.data(),DWORD(json.size()),&wrote,nullptr)&&wrote==json.size();CloseHandle(f);
        if(!ok||!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING))DeleteFileW(tmp.c_str());
    }
    // Render thread, once per frame (from celestialPalette): poll the overrides (250 ms), ask the worker to rebuild after a terrain /
    // ground fog change (the existing geometryRebuild request, at most every 750 ms), write the state (500 ms).
    void devPoll(const char* map,const float* camera,uint32_t zone){
        const DWORD now=GetTickCount();
        if(!devPollAt||DWORD(now-devPollAt)>=250){
            devPollAt=now?now:1;WIN32_FILE_ATTRIBUTE_DATA a{};uint64_t stamp[2]={};
            if(GetFileAttributesExW(devPath(L"northlight-tuning.ini").c_str(),GetFileExInfoStandard,&a)){
                stamp[0]=(uint64_t(a.ftLastWriteTime.dwHighDateTime)<<32)|a.ftLastWriteTime.dwLowDateTime;stamp[1]=(uint64_t(a.nFileSizeHigh)<<32)|a.nFileSizeLow;}
            if(!devStampSeen||stamp[0]!=devStamp[0]||stamp[1]!=devStamp[1]){devStampSeen=true;devStamp[0]=stamp[0];devStamp[1]=stamp[1];devReload();}
        }
        // Only real override changes ask for a rebuild: the last published build used a snapshot that is not worker-equivalent to the
        // current one. Debounced (the snapshot unchanged for 2 s); one request outstanding, the next waits for a build to be published (or 5 s).
        const auto current=devSnapshot();const auto built=devBuiltSnapshot();
        if(current->revision!=devSeenRevision){devSeenRevision=current->revision;devChangeAt=now;}
        const unsigned builds=devBuilds.load(std::memory_order_relaxed);
        if(built&&!current->sameWorker(*built)&&DWORD(now-devChangeAt)>=2000&&(builds!=devBuildsAtRequest||DWORD(now-devRequestedAt)>=5000)){
            devBuildsAtRequest=builds;devRequestedAt=now?now:1;
            {std::lock_guard<std::mutex> lock(mutex);geometryRebuild=true;pending=true;}
            wake.notify_one();logf("TUNING worker rebuild requested (terrain_radius / ground fog scale changed)");
        }
        if(!devStateAt||DWORD(now-devStateAt)>=500){devStateAt=now?now:1;devWriteState(map,camera,zone);}
    }
#endif
    struct PaletteRegion {std::string map;NorthlightRegionalFog::Region region;int tx=0,ty=0;};
    std::shared_ptr<const PaletteRegion> publishedPaletteRegion; // guarded by mutex
    bool celestialValid=false,shadowFrameReady=false;float authoredFill=1;
    // 0.3.200: the last good celestial read (see updateWorldContext), held across single rejected reads.
    static constexpr DWORD CelestialHoldMs=500;NorthlightCelestial::Context celestialHeld;std::string celestialHeldMap;DWORD celestialHeldAt=0;bool celestialHeldOk=false;unsigned long celestialHolds=0;
    NorthlightEffectSwitches::Settings effects,requestedEffects; /* requestedEffects: the hotkey state before the GI setting masks it */
    bool neutralShadowMaps=false;
    V sourceDirections[2],sourceColors[2],sourceGlowColors[2]; // sourceGlowColors: the same sources for the fog volume pass (glow_strength / glow hue instead of light_strength)
    float sourceWeights[2]={1,0};
    NorthlightUnderwater::Fade underwaterFade; // the camera in liquid: the sun and moon fade out (updateWorldContext)
    DWORD underwaterLogAt=0;bool underwaterLogged=false;
    float sourceMatrices[2][2][16]={};
    // Far/NearShadowInterval>1 only: the map and matrix of the last complete far/near cascade render per source.
    NorthlightQuality::ShadowMapReuse farShadow[2],nearShadow[2];unsigned shadowPasses=0;
    // Model capture skip (quality_settings.h skipModelCapture), decided at the frame's first model draw.
    // captureDemand: a skipped frame had to keep a map/cube past its schedule; the next frame captures.
    enum CaptureMode:unsigned char {CaptureUndecided,CaptureFresh,CaptureSkipped};CaptureMode captureMode=CaptureUndecided;
    bool captureDemand=false,lastCaptureSkipped=false;unsigned lastCapturePhaseReads=0;int lastRenderDebug=0;size_t pointReplayCount=0;
    unsigned nearReuses=0,farReuses=0,captureSkippedFrames=0,captureDeferrals=0; /* 600-frame log window */
    DWORD animationEpoch=GetTickCount();
    NorthlightLegacyFog::Constants legacyFog;
    NorthlightLegacyFog::Hold legacyFogHold; /* uploaded fog (c25/c26): readable fog passes through, an unreadable frame holds the last value 2 s per map, then fades w; horizon haze and logs keep the raw legacyFog */
    bool legacyFogKnown=false; /* per frame: the last readOriginalFog decode() succeeded (a known fog-off result counts) */
    unsigned traceFog=0; /* per frame (FRAMETRACE fog=): 0 unknown, 1 read in the context draw, 2 composite used the held (or post-hold fading) value, fog-off included */
    NorthlightHorizonHaze::State horizonHazeState; /* game fog end/colour, smoothed per map */
    /* glow hue: the game's light slots (band 9 native glare, band 10 sunHalo), held for 2 s on one map without a proven read. */
    std::uint32_t lightSlots[NorthlightSunHue::Slots]={};bool lightSlotsValid=false;std::string lightSlotsMap;DWORD lightSlotsAt=0;
    NorthlightSunHue::GlowHue glowHueFrame;
    NorthlightWmoContext::LightingHold globalLightHold; /* 0.3.207: sky block reads caught between the colour copy and the glare darkening (wmo_context.h) */
    NorthlightHorizonHaze::Constants hazeFrame; /* last composite haze constants, for the celestial veil */
    NorthlightSpellFog::Hold spellFogHold;DWORD spellFogTick=0;bool spellFogResetLogged=false; /* SpellFog: the smoothed per-frame estimate (spell_fog.h) */
    float skyTransmittanceFrame=1; /* CPU estimate of the fog transmittance toward the sun, for the veil */
    struct FogShader { unsigned major=0;int colorRegister=-1;bool verified=false; };
    std::unordered_map<IDirect3DPixelShader9*,FogShader> fogShaders;
    unsigned fogReports=0;
    float projection[3]={1,1,1};
    IDirect3DTexture9 *shadow[4]={},*probe[5]={},*probePrev=nullptr,*light=nullptr,*smoothLight=nullptr,*fog=nullptr,*fogBlurred=nullptr,*color=nullptr,*baselineLight=nullptr;
    IDirect3DTexture9 *normalBuffer=nullptr;IDirect3DSurface9* normalSurface=nullptr;
    IDirect3DTexture9* sourceVis[2][2]={};IDirect3DSurface9* sourceVisSurface[2][2]={};unsigned sourceVisIndex=0;bool sourceVisValid=false;
    IDirect3DTexture9 *temporalLight[2]={},*temporalDepth[2]={};IDirect3DSurface9 *temporalLightSurface[2]={},*temporalDepthSurface[2]={};
    unsigned temporalIndex=0;bool temporalValid=false;float previousView[16]={};V previousCamera;std::string temporalMap;
    IDirect3DSurface9 *shadowSurface[4]={},*shadowDepth=nullptr,*lightSurface=nullptr,*smoothSurface=nullptr,*fogSurface=nullptr,*fogBlurredSurface=nullptr,*colorSurface=nullptr,*baselineSurface=nullptr;
    // Static shadow cache: per (source,cascade) an R32F map with a 128-texel
    // margin holding static batches only, re-rendered when the snapped light
    // origin travels beyond the margin or the geometry/direction key changes.
    // Per frame only live terrain and replays are drawn (scratch) and united.
    static constexpr UINT ShadowCacheMargin=128,ShadowCacheSize=1024+2*ShadowCacheMargin;
    IDirect3DTexture9 *shadowCache[4]={},*shadowScratch=nullptr;
    IDirect3DSurface9 *shadowCacheSurface[4]={},*shadowCacheDepth=nullptr,*shadowScratchSurface=nullptr;
    // 0.3.209 (task 29): per-source actor shadow map (R32F 1024, moving casters only, shadows/actor_shadow_map.h) and the half-res visibility it feeds (A8R8G8B8: r sun, g moon; white = lit).
    // actorMatrices/actorValid are stored with the near map's reuse state: the near map and the actor map of a source always come from the same frame.
    IDirect3DTexture9 *actorShadow[2]={},*actorVisibility=nullptr,*actorTiles[2]={},*actorTilesMid=nullptr;IDirect3DSurface9 *actorShadowSurface[2]={},*actorVisibilitySurface=nullptr,*actorTilesSurface[2]={},*actorTilesMidSurface=nullptr;
    float actorMatrices[2][16]={};bool actorValid[2]={};
    bool nearActorSkip[2]={}; /* the near map was drawn without the replays its actor map holds: reusable only together with that actor map */
    NorthlightActorSide::Tracker actorSide[2]; /* diagnostics: which map each skinned caster went to, per source */
    NorthlightActorSideHold::Hold actorHold[2]; /* the casters held in the actor map (hysteresis), per source; about 48 KB each */
    unsigned replayNearSize=0; /* diagnostics: the bounds job's near list of the last frame */
    unsigned long replayNearStops=0; /* diagnostics: near walks that ended on their budget share */
    V actorCenter[2];bool actorCenterValid[2]={}; /* leashed centre per source (actor_shadow_map.h leash) */unsigned long actorNearSkipped=0,actorDrawn=0;std::vector<uint32_t> actorReplays; /* the near loop's skipped replays: exactly what the actor map draws */DWORD actorShadowLogAt[2]={}; /* per source: the moon's line is not starved by the sun's timer */
    IDirect3DPixelShader9* unionPS=nullptr;
    std::shared_ptr<const NorthlightLocalShadowSignature::Records> uploadedLocalShadowRecords;
    struct ShadowCacheKey {
        bool valid=false,localContentKnown=false;NorthlightWorldMath::ShadowFrame frame;V direction;
        uint64_t serial=0,chunkHash=0,staticSignature=0;
        NorthlightLocalShadowSignature::Digest localContent;
        NorthlightLocalShadowSignature::Memo localMemo;
        StaticShadow::GpuCache::ContentRecord staticContent;
    };
    ShadowCacheKey shadowCacheKey[4];
    DWORD shadowCacheLogAt=0,shadowPhaseLogAt=0;unsigned shadowCacheRenders=0,shadowCacheReuses=0,culledReplayDraws=0;
    unsigned long long replaySlotTested[4]={},replaySlotBounded[4]={},replaySlotCulled[4]={},replaySlotM2Skipped[4]={}; /* per source*2+cascade, 600-frame window */
    // Static-model-only content changes redraw just the texels the changed
    // slices can touch (old and new bounds), under an identical scissor, with the
    // complete ordered draw list: per pixel this is the full redraw. false=0.3.135.
    static constexpr bool StaticCacheDirtyRects=true;static constexpr long StaticCacheDirtyMargin=2,StaticCacheDirtyTile=64;static constexpr size_t StaticCacheDirtyMaxRects=8;
    unsigned shadowCachePartialRenders=0,shadowCacheStaticFullRenders=0,shadowCachePartialSkippedDraws=0,shadowCachePartialRects=0;unsigned long long shadowCachePartialTexels=0;
    // 0.3.151 StaticCacheSlices>1: per slot, a rect redraw in progress over several frames.
    NorthlightStaticSlices::Cycle staticSlices[4];unsigned staticSliceBands=0,staticSliceRestarts=0;
    std::vector<StaticShadow::CasterBounds> staticDirtyScratch;std::vector<NorthlightShadowBounds::TexelRect> staticDirtyFootprints,staticDirty;std::vector<D3DRECT> staticDirtyClears;std::vector<const NorthlightShadowBounds::TexelRect*> staticDirtyHits;std::vector<NorthlightShadowBounds::TexelRect> staticDirtyBounding;std::vector<size_t> staticDirtyCosts;
    // 0.3.137 direction-step plan prebuild (static_plan_prebuild.h).
    NorthlightStaticPrebuild::Scheduler staticPrebuild;V rawSourceDirections[2];
    int staticScissorCaps=-1;unsigned staticDedupUncommitted=0;unsigned shadowCachePartialBounding=0,shadowCacheCostFull=0;unsigned long long shadowCachePartialCalls=0,shadowCachePartialFullCalls=0;
    // 0.3.152: the buffers and counters of one dirty-rect evaluation: the render
    // thread's members, or a kicked slot's StaticDirtyJob (on the plan worker).
    struct DirtyWork {std::vector<StaticShadow::CasterBounds>& scratch;std::vector<NorthlightShadowBounds::TexelRect>& footprints;std::vector<NorthlightShadowBounds::TexelRect>& dirty;std::vector<NorthlightShadowBounds::TexelRect>& bounding;std::vector<size_t>& costs;
        unsigned& boundingChoices;unsigned& costFull;unsigned long long& calls;unsigned long long& fullCalls;};
    struct StaticDirtyJob {
        bool eligible=false,ready=false,result=false;uint64_t frame=0;
        std::vector<StaticShadow::CasterBounds> scratch;std::vector<NorthlightShadowBounds::TexelRect> footprints,dirty,bounding;std::vector<size_t> costs;
        unsigned boundingChoices=0,costFull=0;unsigned long long calls=0,fullCalls=0;
        DirtyWork work(){return {scratch,footprints,dirty,bounding,costs,boundingChoices,costFull,calls,fullCalls};}
    };
    StaticDirtyJob staticDirtyJobs[4];float staticSlotMatrix[4][16]={};unsigned long long staticDirtyJobsUsed=0;double staticKickMs=0; /* kickMs: render-thread kick incl. the first slot's prepare */
    // R3: renderer-side inputs of a worker result (keys, batches, terrain chunks) change only
    // after a settle; these writers also drop any result not used yet.
    void dropStaticDirtyJobs(){for(auto& job:staticDirtyJobs)job.ready=false;}
    // Disjoint texel rects covering every changed static slice (old and new).
    // 0.3.152: a slot whose plan the plan worker built may already hold the result
    // (same inputs, frozen since the kick: the slot's key, its plan, the batches).
    bool staticDirtyRects(const ShadowCacheKey& key,const float* matrix,bool staticChanged=true){
        staticDirty.clear();staticDirtyFootprints.clear();
        if(staticScissorCaps<0){D3DCAPS9 caps{};staticScissorCaps=SUCCEEDED(d->GetDeviceCaps(&caps))&&(caps.RasterCaps&D3DPRASTERCAPS_SCISSORTEST)?1:0;}if(!staticScissorCaps)return false;
        const ptrdiff_t slot=&key-shadowCacheKey;
        if(slot>=0&&slot<4&&staticChanged){auto& job=staticDirtyJobs[slot];staticCasters.joinPlan(matrix);
            if(job.ready&&job.frame==staticFrame&&staticCasters.kickedModeCurrent()&&!std::memcmp(staticSlotMatrix[slot],matrix,sizeof staticSlotMatrix[slot])){
                job.ready=false;staticDirty.swap(job.dirty);shadowCachePartialBounding+=job.boundingChoices;shadowCacheCostFull+=job.costFull;shadowCachePartialCalls+=job.calls;shadowCachePartialFullCalls+=job.fullCalls;++staticDirtyJobsUsed;
                return job.result;}}
        return dirtyRectsFrom({staticDirtyScratch,staticDirtyFootprints,staticDirty,staticDirtyBounding,staticDirtyCosts,shadowCachePartialBounding,shadowCacheCostFull,shadowCachePartialCalls,shadowCachePartialFullCalls},
            key,matrix,staticChanged,staticCasters.stats().instancing,staticCasters);
    }
    // The CPU part, for either thread. Plan: the GpuCache or the worker's detached
    // view (same changedBounds/drawCalls).
    template<class Plan> bool dirtyRectsFrom(DirtyWork w,const ShadowCacheKey& key,const float* matrix,bool staticChanged,bool instancing,const Plan& plan){
        w.dirty.clear();w.footprints.clear();size_t models=0;
        w.scratch.clear();
        if(staticChanged){try {if(!plan.changedBounds(matrix,key.staticContent,w.scratch,models))return false;}catch(...){return false;}}
        // Unchanged static content is redrawn inside the rects too: it must be the recorded
        // content in the same instancing mode (changedBounds checks this when static changed).
        else if(!key.staticContent.valid||key.staticContent.instancing!=instancing)return false;
        for(const auto& box:w.scratch){NorthlightShadowBounds::TexelRect footprint;
            if(!box.bounded||!NorthlightShadowBounds::texelFootprint(box.low,box.high,matrix,long(ShadowCacheSize),StaticCacheDirtyMargin,footprint))return false;
            w.footprints.push_back(footprint);}
        NorthlightShadowBounds::dirtyRects(w.footprints,long(ShadowCacheSize),StaticCacheDirtyTile,StaticCacheDirtyMaxRects,w.dirty);
        // A wide draw is issued once per rect it touches. Pick the cheapest
        // exact option by draw calls: disjoint rects, their bounding rect, or full.
        w.bounding.clear();if(w.dirty.size()>1){NorthlightShadowBounds::TexelRect all;for(const auto& r:w.dirty)all=NorthlightShadowBounds::unite(all,r);w.bounding.push_back(all);}
        const std::vector<const std::vector<NorthlightShadowBounds::TexelRect>*> candidates={nullptr,&w.dirty,w.bounding.empty()?&w.dirty:&w.bounding};
        try {plan.drawCalls(matrix,candidates,long(ShadowCacheSize),StaticCacheDirtyMargin,w.costs);}catch(...){return false;}
        /* 0.3.215 (task 45): the flat bitmap answers the per-batch fixed-chunk test without a red-black tree walk (same membership: it falls back to the set off-grid or when it does not describe this set). */
        const auto& fixedSet=fixedTerrainChunks();const bool fixedBits=fixedTerrainBits.source()==&fixedSet;
        const bool affine=NorthlightShadowBounds::affineLightMatrix(matrix);
        for(const auto& b:batches){
            if(b.terrain&&!(fixedBits?fixedTerrainBits.contains({b.chunkX,b.chunkY}):fixedSet.count({b.chunkX,b.chunkY})!=0))continue;
            if(affine&&NorthlightShadowBounds::directionalClipRejectAffine(b.boundsLow,b.boundsHigh,matrix))continue;
            NorthlightShadowBounds::TexelRect footprint;const bool known=NorthlightShadowBounds::texelFootprint(b.boundsLow,b.boundsHigh,matrix,long(ShadowCacheSize),StaticCacheDirtyMargin,footprint);
            ++w.costs[0];for(size_t k=1;k<candidates.size();++k)for(const auto& r:*candidates[k])if(!known||NorthlightShadowBounds::intersects(footprint,r))++w.costs[k];
        }
        if(w.costs[2]<w.costs[1]){w.dirty.swap(w.bounding);w.costs[1]=w.costs[2];++w.boundingChoices;}
        if(w.costs[1]>=w.costs[0]){++w.costFull;return false;}
        w.calls+=w.costs[1];w.fullCalls+=w.costs[0];
        return true;
    }
    // 0.3.152 (static_plan_job.h): with every plan input final for the frame, the
    // first drawn slot's plan is prepared now and the later slots' missing plans
    // are built on the plan worker in loop order, joined at each slot's first use
    // (GpuCache::kickPlans). A later slot that may take the static-models rect path
    // (valid key and content, no placement reason) gets its
    // dirty-rect CPU part computed there right after its build. Until the join the
    // job reads only frozen state: that slot's key, the batches and terrain chunks.
    void staticPlanKick(const NorthlightWorldMath::ShadowCachePlacement* placements,const bool* sourceActive){
        int order[4];unsigned count=0;
        for(int source=0;source<2;++source)if(sourceActive[source])for(int cascade=0;cascade<2;++cascade)order[count++]=source*2+cascade;
        if(count<2)return;
        if(staticScissorCaps<0){D3DCAPS9 caps{};staticScissorCaps=SUCCEEDED(d->GetDeviceCaps(&caps))&&(caps.RasterCaps&D3DPRASTERCAPS_SCISSORTEST)?1:0;}
        for(auto& job:staticDirtyJobs){job.ready=job.eligible=false;job.frame=staticFrame;job.boundingChoices=job.costFull=0;job.calls=job.fullCalls=0;}
        for(unsigned i=1;i<count;++i){const int slot=order[i];const auto& key=shadowCacheKey[slot];
            staticDirtyJobs[slot].eligible=StaticCacheDirtyRects&&staticScissorCaps==1&&!placements[slot].reason&&key.valid&&key.staticContent.valid;}
        const float* later[3];for(unsigned i=1;i<count;++i)later[i-1]=staticSlotMatrix[order[i]];
        // A throwing changedBounds/drawCalls leaves the slot to the render thread (which returns false only if it throws there too).
        struct Traced {const StaticShadow::GpuCache::DetachedView& view;bool& threw;
            bool changedBounds(const float* m,const StaticShadow::GpuCache::ContentRecord& old,std::vector<StaticShadow::CasterBounds>& out,size_t& models)const{try{return view.changedBounds(m,old,out,models);}catch(...){threw=true;throw;}}
            void drawCalls(const float* m,const std::vector<const std::vector<NorthlightShadowBounds::TexelRect>*>& c,long size,long margin,std::vector<size_t>& out)const{try{view.drawCalls(m,c,size,margin,out);}catch(...){threw=true;throw;}}};
        try {staticCasters.kickPlans(staticSlotMatrix[order[0]],later,count-1,[this,order,count](unsigned,const StaticShadow::GpuCache::DetachedView& view){
                for(unsigned i=1;i<count;++i){const int slot=order[i];auto& job=staticDirtyJobs[slot];
                    if(!job.eligible||std::memcmp(staticSlotMatrix[slot],view.matrix(),sizeof staticSlotMatrix[slot])||view.signature()==shadowCacheKey[slot].staticSignature)continue; /* same signature: no static change */
                    bool threw=false;
                    try {const bool result=dirtyRectsFrom(job.work(),shadowCacheKey[slot],staticSlotMatrix[slot],true,false,Traced{view,threw});if(!threw){job.result=result;job.ready=true;}}catch(...){}
                }});}
        catch(...){staticCasters.reset();staticOwnerGeneration=UINT64_MAX;staticRetryTick=GetTickCount();} /* the first slot's prepare, as staticSignature() */
    }
    // Debug self-check (off): after each rect redraw, render the same slot in full
    // into a scratch target, read both back and count differing texels.
    static constexpr bool StaticCacheDirtyRectVerify=false;
    IDirect3DTexture9* shadowVerify=nullptr;IDirect3DSurface9 *shadowVerifySurface=nullptr,*shadowVerifyRead[2]={};
    unsigned shadowCacheVerified=0,shadowCacheVerifyMismatches=0,shadowCacheScissorLeaks=0;
    template<class Render> bool verifyStaticCache(int slot,Render render){
        if(!shadowVerifySurface&&!target(ShadowCacheSize,ShadowCacheSize,D3DFMT_R32F,&shadowVerify,&shadowVerifySurface))return false;
        for(auto*& read:shadowVerifyRead)if(!read&&!check(d->CreateOffscreenPlainSurface(ShadowCacheSize,ShadowCacheSize,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&read,nullptr),"shadow verify readback"))return false;
        const int full=render(shadowVerifySurface);if(full<0)return false;
        if(!check(d->GetRenderTargetData(shadowCacheSurface[slot],shadowVerifyRead[0]),"shadow verify read")||!check(d->GetRenderTargetData(shadowVerifySurface,shadowVerifyRead[1]),"shadow verify read"))return false;
        D3DLOCKED_RECT a{},b{};unsigned long long differing=0;
        if(SUCCEEDED(shadowVerifyRead[0]->LockRect(&a,nullptr,D3DLOCK_READONLY))){
            if(SUCCEEDED(shadowVerifyRead[1]->LockRect(&b,nullptr,D3DLOCK_READONLY))){
                for(UINT y=0;y<ShadowCacheSize;++y){const auto* x=static_cast<const uint32_t*>(a.pBits)+size_t(y)*(a.Pitch/4);const auto* z=static_cast<const uint32_t*>(b.pBits)+size_t(y)*(b.Pitch/4);if(std::memcmp(x,z,ShadowCacheSize*4))for(UINT i=0;i<ShadowCacheSize;++i)differing+=x[i]!=z[i];}
                shadowVerifyRead[1]->UnlockRect();}
            shadowVerifyRead[0]->UnlockRect();}
        ++shadowCacheVerified;if(differing){++shadowCacheVerifyMismatches;logf("WORLD shadow cache VERIFY MISMATCH slot=%d texels=%llu",slot,differing);}
        return true;
    }
    void invalidateShadowCache(){staticCasters.settle();dropStaticDirtyJobs();for(auto& k:shadowCacheKey){k.valid=false;k.staticContent.valid=false;}for(auto& c:staticSlices)c.reset();pointCacheValid=false;for(int s=0;s<2;++s){farShadow[s].invalidate();nearShadow[s].invalidate();actorValid[s]=false;nearActorSkip[s]=false;actorCenterValid[s]=false;}}
    uint64_t liveChunkHash()const{uint64_t h=1469598103934665603ull;for(const auto& c:liveTerrainChunks){h^=uint64_t(uint32_t(c.first))*0x9E3779B97F4A7C15ull+uint64_t(uint32_t(c.second));h*=1099511628211ull;}return h;}
    IDirect3DPixelShader9 *cachedFastPS=nullptr,*cachedOpaqueFastPS=nullptr,*cachedOpaquePS=nullptr;
    IDirect3DPixelShader9 *lightingPS=nullptr,*giPS=nullptr,*fogPS=nullptr,*fogBlurPS=nullptr,*localDirectPS=nullptr,*localDirect16PS=nullptr,*removalPS=nullptr,*temporalPS=nullptr,*localFogPS=nullptr,*normalsPS=nullptr,*sourceVisPS=nullptr,*finalPS=nullptr,*shadowPS=nullptr,*cachedShadowPS=nullptr,*replayPS=nullptr;
    IDirect3DPixelShader9 *actorShadowPS=nullptr,*actorTilesPS=nullptr; /* 0.3.209 (task 29): optional (with their targets); without them the actor shadow map stays off */
    // 0.3.199 (fog clouds): the moving fog banks' own half-resolution pass (FogClouds), its noise volume and the wind clock. The volume's bytes are
    // generated once per process on a worker thread (first active frame, never DllMain) and shared by every device; the holder is leaked on
    // purpose and the worker detached, so neither DLL unload nor process exit waits on a thread under the loader lock.
    struct FogCloudNoise{
        std::vector<uint8_t> data;NorthlightFogClouds::Quantiles quantiles{};std::atomic<bool> ready{false},started{false};
        void request(){
            if(started.exchange(true))return;
            try{std::thread([this]{
                const auto t0=std::chrono::steady_clock::now();
                try{data=NorthlightFogClouds::generate();quantiles=NorthlightFogClouds::quantiles(data.data());}catch(...){return;} /* allocation failure: never ready, no retry */
                const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t0).count();
                ready.store(true,std::memory_order_release);logf("WORLD fog clouds noise ms=%.1f",ms);
            }).detach();}catch(...){}
        }
    };
    static FogCloudNoise& fogCloudNoise(){static FogCloudNoise* noise=new FogCloudNoise;return *noise;}
    IDirect3DPixelShader9* fogCloudsPS=nullptr;IDirect3DVolumeTexture9* cloudNoise=nullptr;bool cloudNoiseFailed=false;
    // 0.3.199 (fog temporal): last frame's resolved half-resolution fog (swapped with fogBlurred after the pass, so one extra target is enough), valid only after a frame that ran the pass.
    IDirect3DPixelShader9* fogTemporalPS=nullptr;IDirect3DTexture9* fogHistory=nullptr;IDirect3DSurface9* fogHistorySurface=nullptr;bool fogHistoryValid=false;
    static constexpr float FogTemporalWeight=.85f; /* history weight in c64.y (0 = pass through) */
    // 0.3.200 (gpu budget): the level the proxy's controller chose (0 = full, always with GpuBudgetMs=0).
    unsigned gpuBudgetLevel=0;
    unsigned cpuBudgetLevel=0; /* task 45: the CPU budget controller's level (ActorShadowCpuMs): 0 = ActorShadowRadius as set */
    NorthlightFogClouds::Wind cloudWind;int64_t cloudQpc=0;float cloudDenseZone=0,cloudLush=1; /* smoothed dense-zone profile at the camera (Duskwood 1), see denseZoneDamp; smoothed lush zone at the camera (forest/grass 1), see derive */
    // The N^3 volume as L8 (A8R8G8B8 with the value in every channel where L8 volumes are missing); one failure disables the clouds for good, nothing else.
    bool ensureCloudNoise(){
        if(cloudNoise)return true;
        auto& noise=fogCloudNoise();
        if(cloudNoiseFailed||memoryPressure||!noise.ready.load(std::memory_order_acquire))return false;
        constexpr unsigned N=NorthlightFogClouds::N;
        IDirect3DVolumeTexture9* t=nullptr;bool l8=true;
        HRESULT hr=d->CreateVolumeTexture(N,N,N,1,0,D3DFMT_L8,D3DPOOL_MANAGED,&t,nullptr);
        if(FAILED(hr)||!t){t=nullptr;l8=false;hr=d->CreateVolumeTexture(N,N,N,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&t,nullptr);}
        D3DLOCKED_BOX box={};
        if(FAILED(hr)||!t||FAILED(t->LockBox(0,&box,nullptr,0))||!box.pBits){
            if(t)t->Release();cloudNoiseFailed=true;logf("WORLD fog clouds disabled: noise volume HRESULT=%08lx",(unsigned long)hr);return false;}
        for(unsigned z=0;z<N;++z)for(unsigned y=0;y<N;++y){
            const uint8_t* src=noise.data.data()+size_t(N)*(y+size_t(N)*z);
            uint8_t* row=static_cast<uint8_t*>(box.pBits)+size_t(z)*box.SlicePitch+size_t(y)*box.RowPitch;
            if(l8)memcpy(row,src,N);else for(unsigned x=0;x<N;++x)reinterpret_cast<uint32_t*>(row)[x]=0xff000000u|src[x]*0x010101u;
        }
        t->UnlockBox(0);cloudNoise=t;logf("WORLD fog clouds noise volume %ux%ux%u %s",N,N,N,l8?"L8":"A8R8G8B8");return true;
    }
    unsigned localDirectCount=0;float localDirectNearest=0;
    // 0.3.205 (gh#20): the lamp fog glow batches add their raw sums into localFogAccum (half resolution, like the fog buffer) and localFogCombinePS caps the total once
    // into the fog buffer.
    IDirect3DPixelShader9* localFogCombinePS=nullptr;IDirect3DTexture9* localFogAccum=nullptr;IDirect3DSurface9* localFogAccumSurface=nullptr;
    // 0.3.197: soft cap, incumbent bias and fades for the local lights (task 13). The clock is the selection's own: its gap and reset policy mirror useHistory.
    NorthlightEquipmentLights::Tracker equipmentLights;std::unordered_map<IDirect3DVertexShader9*,unsigned> equipmentShaders;
    NorthlightParticleLights::Frame particleLights;std::string particleMap;
    std::string equipmentMap;NorthlightEquipmentLights::CaptureBudget equipmentCapture;
    NorthlightLocalLightSelection::Tracker localLightTracker;std::string localLightMap;V localLightCamera;int64_t localLightQpc=0;double localSelectUsSum=0,localSelectUsMax=0;unsigned localSelectFrames=0;
    // Additive local light passes add exactly +0 outside their batch's projected
    // spheres; scissor them there. Counters cover one LOCAL log interval.
    NorthlightLocalLightScissor::View localScissorView;
    unsigned localScissorBatches=0,localScissorClipped=0,localScissorSkipped=0;double localScissorCoverage=0;
    unsigned localOffscreenSum=0,localArranged=0,localWidePasses=0;double localArrangeBefore=0,localArrangeAfter=0; /* LOCAL direct window: off-screen candidates, arranged frames, LocalDirect16 passes, cost model before/after */
    // 0.3.200 (jobs): known = the batch's rect from atmosphereWork (the same batchRect of the same view and positions), else computed here.
    // RenderProfile sample frames only: a tile-based GPU (Metal under DXVK) writes a timestamp taken inside a render pass when that pass
    // ends, so the lamp passes, which share the light (and fog) target with the passes around them, showed up in the next target switch
    // (LocalLights 0.001 ms, RemovalSmooth carrying their cost). A one-pixel clear of its own target ends the pass; `resume` is bound again
    // (the viewport resets to it; callers set the scissor per draw). It costs one store and load of the target on sample frames, never on others.
    IDirect3DSurface9* profileSplitSurface=nullptr;bool profileSplitFailed=false;
    void profileSplit(NorthlightGpuProfile* profile,IDirect3DSurface9* resume){
        if(!profile||!profile->recording()||!resume||profileSplitFailed)return;
        if(!profileSplitSurface&&FAILED(d->CreateRenderTarget(1,1,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&profileSplitSurface,nullptr))){profileSplitSurface=nullptr;profileSplitFailed=true;return;}
        if(SUCCEEDED(d->SetRenderTarget(0,profileSplitSurface)))d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0);
        d->SetRenderTarget(0,resume);
    }
    template<std::size_t N> bool localScissor(const std::array<NorthlightLocalLightSelection::Constant,N>& position,unsigned count,const NorthlightLocalLightScissor::Rect* known=nullptr){
        if(!NorthlightLocalLightScissor::Enabled)return true;
        // Fail open: without a proven view the batch draws unclipped.
        if(!localScissorView.valid){d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);++localScissorBatches;localScissorCoverage+=1;return true;}
        const auto r=known?*known:NorthlightLocalLightScissor::batchRect(localScissorView,position,count);
        const double all=double(localScissorView.halfW)*localScissorView.halfH;
        ++localScissorBatches;localScissorCoverage+=all>0?double(r.area())/all:1.;
        if(!r.area()){++localScissorSkipped;return false;} // no pixel can change: skip the draw only
        if(r.area()<all)++localScissorClipped;
        const RECT clip={r.left,r.top,r.right,r.bottom};d->SetScissorRect(&clip);return true;
    }
    IDirect3DVertexShader9 *shadowVS=nullptr,*cachedShadowVS=nullptr;
    IDirect3DVertexDeclaration9* shadowDecl=nullptr;
    IDirect3DVertexBuffer9* vertices=nullptr; // non-owning aliases of page 0 of the active table; also the "mesh present" flags
    IDirect3DIndexBuffer9* indices=nullptr;
    // 0.3.216 (issue #66): ONE store of page buffers (mesh_buffer_store.h) instead of two full slots. meshStore does all the
    // bookkeeping (content keys, active table, claims, idle state); meshBuffers[id] holds the D3D resources of store entry
    // `id` (capacities live in the entry). The active table is drawn; a staging build reuses buffers whose key matches a new
    // page, uploads only the misses into idle/new buffers and switches the table atomically at commit. An ACTIVE buffer is
    // never created, written or released (acquireMeshPage/perform check meshStore.writable(id), trims take idle ones only).
    struct MeshBuffers {IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;};
    NorthlightMeshBufferStore::Store meshStore;std::vector<MeshBuffers> meshBuffers;NorthlightMeshPoolTrim::State meshPoolTrim;
    void syncMeshBuffers(){meshBuffers.resize(meshStore.entries().size());}
    // 0.3.216 (issue #66): keys of the pages the ACTIVE table holds, published for the geometry worker, which frees the CPU data of
    // every page of a new plan whose key is in it (the GPU has it already). Keys only; guarded by `mutex`. Replaced at each mesh
    // commit, cleared with the pool and whenever an elided page missed at staging (the next plan then elides nothing).
    std::shared_ptr<const NorthlightMeshPageKey::KeySet> residentPages;std::string residentMap;unsigned elidedFallbacks=0;
    void publishResidentPages(const std::string& map){
        std::shared_ptr<const NorthlightMeshPageKey::KeySet> keys;
        if(!map.empty()&&!meshStore.table().empty()){try{keys=std::make_shared<const NorthlightMeshPageKey::KeySet>(meshStore.activeKeys());}catch(...){keys.reset();}}
        std::lock_guard<std::mutex> lock(mutex);residentPages=std::move(keys);residentMap=residentPages?map:std::string();
    }
    DWORD memoryPressureLogAt=0,memoryPressureUntil=0;
    static UINT roundBuffer(uint64_t bytes,uint64_t stepBytes){return UINT(NorthlightGeometryMemory::roundUp(bytes,stepBytes));}
    // Growth admission for any large GPU buffer. A refused growth is remembered
    // for one second so pressure does not add an address-space walk per frame.
    bool admitsGrowth(const char* stage,uint64_t capacityBytes,const NorthlightGeometryMemory::Sample* cachedSample=nullptr){
        DWORD now=GetTickCount();
        if(memoryPressureUntil&&now<memoryPressureUntil)return false;
        const auto budget=NorthlightGeometryMemory::dynamicBudget(capacityBytes);
        bool exact=true;
        auto memory=cachedSample?*cachedSample:geometryAdmission(budget,&exact);
        if(NorthlightGeometryMemory::admits(memory,budget))return true;
        memoryPressureUntil=now+1000;NorthlightMemMap::deferredRequest.store(true,std::memory_order_relaxed); /* task 42: MEMMAP memory-deferred request */
        if(!memoryPressureLogAt||now-memoryPressureLogAt>=60000){memoryPressureLogAt=now;logGeometryMemory(stage,memory,0,exact&&!cachedSample,&budget);}
        return false;
    }
    bool acquireMeshPage(uint32_t id,bool vertex,UINT bytes,NorthlightGeometryMemory::Sample& memory){
        if(!meshStore.writable(id)||id>=meshBuffers.size())return false; // only a buffer claimed by this staging, never an active one
        MeshBuffers& slot=meshBuffers[id];const auto& entry=meshStore.entries()[id];
        if(vertex?slot.vb&&entry.vbCapacity>=bytes:slot.ib&&entry.ibCapacity>=bytes)return true;
        // At most 512 KiB per resource, including rounded unused capacity.
        const UINT capacity=UINT(NorthlightMeshBufferStore::roundBuffer(bytes));
        if(!NorthlightGeometryMemory::admitsSmallPageGrowth(memory.valid,memory.available,capacity))return false;
        if(vertex){
            drop(slot.vb);meshStore.setCapacity(id,true,0);
            if(FAILED(d->CreateVertexBuffer(capacity,D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,&slot.vb,nullptr))||!slot.vb){drop(slot.vb);return false;}
            meshStore.setCapacity(id,true,capacity);
        }else{
            drop(slot.ib);meshStore.setCapacity(id,false,0);
            if(FAILED(d->CreateIndexBuffer(capacity,D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_MANAGED,&slot.ib,nullptr))||!slot.ib){drop(slot.ib);return false;}
            meshStore.setCapacity(id,false,capacity);
        }
        NorthlightGeometryMemory::debitGrowth(memory,capacity);return true;
    }
    void releaseMeshPool(){for(auto& slot:meshBuffers){drop(slot.vb);drop(slot.ib);}meshBuffers.clear();meshStore.reset();publishResidentPages(std::string());vertices=nullptr;indices=nullptr;meshPoolTrim.reset();}
    // Frees idle buffers (returns bytes): those not referenced by the active table and not claimed by a staging.
    // Never while a staged upload is pending; the active table (and vertices/indices, which alias its page 0) stays.
    // Order (mesh_buffer_store.h trimIdle): invalid-key buffers first (partial content from a cancelled staging), then
    // the valid ones idle for the longest. maxBytes: stop once this many bytes were released (always at least one
    // buffer when any is idle); unbounded for the memory guard.
    size_t trimMeshPools(uint64_t maxBytes=UINT64_MAX){
        if(pendingMesh)return 0;
        const uint64_t freed=meshStore.trimIdle(maxBytes,[&](uint32_t id){if(id<meshBuffers.size()){drop(meshBuffers[id].vb);drop(meshBuffers[id].ib);}});
        syncMeshBuffers();return size_t(freed);
    }
    // After a staged commit (pendingMesh already cleared): releases idle capacity the next build is unlikely to need,
    // with hysteresis (mesh_pool_trim.h): target = windowMax(mesh) + windowMax(uploaded this commit). Runs once per
    // commit, O(buffers); the active table's buffers never leave.
    void trimMeshPoolAfterCommit(uint64_t stagedCapacity){
        if(pendingMesh||meshStore.table().empty())return;
        const uint64_t pool=meshStore.poolBytes(),mesh=meshStore.activeBytes();
        const uint64_t request=meshPoolTrim.onCommit(pool,mesh,stagedCapacity); // at most MaxReleaseBytes per commit
        if(!request)return;
        const size_t freed=trimMeshPools(request);
        if(freed)logf("WORLD mesh pool trimmed freedMiB=%.1f poolMiB=%.1f meshMiB=%.1f",double(freed)/1048576,double(pool-freed)/1048576,double(mesh)/1048576);
    }
    std::vector<IDirect3DTexture9*> materials;
    NorthlightStreaming::DeferredRelease<IDirect3DTexture9> retiredMaterials;
    size_t uploadedTextureBytes=0;
    using MaterialPixels=std::vector<NorthlightWorldMesh::MaterialUpload>;
    std::shared_ptr<MaterialPixels> uploadedMaterialPixels;
    static constexpr size_t MaterialReuseBytes=4u<<20;
    size_t materialReuseBytes=0;
    IDirect3DTexture9* reusableMaterial(const NorthlightWorldMesh::MaterialUpload& material)const{
        if(!uploadedMaterialPixels)return nullptr;
        for(size_t i=0;i<uploadedMaterialPixels->size()&&i<materials.size();++i){
            const auto& old=(*uploadedMaterialPixels)[i];
            if(!old.bgra.empty()&&old.textureIdentity==material.textureIdentity&&
               old.width==material.width&&old.height==material.height&&old.bgra==material.bgra)return materials[i];
        }
        return nullptr;
    }
    using Batch=NorthlightWorldMeshPages::Batch;
    bool bindMeshPage(const Batch& batch,UINT& boundPage){
        if(boundPage==batch.page)return true;
        const uint32_t id=meshStore.bufferOf(batch.page);
        if(id>=meshBuffers.size())return false; // also NorthlightMeshBufferStore::None
        auto& page=meshBuffers[id];
        if(!page.vb||!page.ib||FAILED(d->SetStreamSource(0,page.vb,0,sizeof(NorthlightGI::WorldVertex)))||FAILED(d->SetIndices(page.ib)))return false;
        boundPage=batch.page;return true;
    }
    std::vector<Batch> batches;
    // 0.3.175 (S1a): indices of this mesh generation's terrain batches, in batch order; rebuilt only
    // at the mesh commit (rebuildTerrainLists), valid while terrainBatchListGeneration==meshGeneration.
    // 0.3.175 (S2): shadowTerrainList, the terrain batches outside this generation's fixed chunks
    // (the directional terrain selection then tests only the live chunks), for terrainListFixed.
    std::vector<uint32_t> terrainBatchList,shadowTerrainList;uint64_t terrainBatchListGeneration=UINT64_MAX;size_t terrainBatchListSize=0;const void* terrainListFixed=nullptr;
    void rebuildTerrainLists(){
        terrainBatchListGeneration=UINT64_MAX;terrainBatchList.clear();shadowTerrainList.clear();
        const auto& fixed=fixedTerrainChunks();
        try{for(size_t i=0;i<batches.size()&&i<UINT32_MAX;++i)if(batches[i].terrain){terrainBatchList.push_back(uint32_t(i));
                if(!fixed.count({batches[i].chunkX,batches[i].chunkY}))shadowTerrainList.push_back(uint32_t(i));}}
        catch(...){terrainBatchList.clear();shadowTerrainList.clear();return;} /* fallback: the whole batch list */
        terrainBatchListSize=batches.size();terrainListFixed=&fixed;terrainBatchListGeneration=meshGeneration;
    }
    // CELESTIAL terrain mask line (rate-limited): the last redraw, and per body (sun, moon) redraws/reuses since the line.
    struct CelestialMaskStats {size_t candidates=0;unsigned accepted=0,runs=0,redraws[2]={},reuses[2]={};uint64_t triangles=0;bool listed=false;double ms=0,peakMs=0;DWORD lastLog=0;} celestialMask;
    NorthlightTerrainCandidates::Scratch<> directionalTerrainScratch; // bounded capacity only; eligibility is render-local
    std::shared_ptr<const std::set<std::pair<int,int>>> fixedTerrain; /* immutable, shared with the worker-prepared commit */
    const std::set<std::pair<int,int>>& fixedTerrainChunks()const{static const std::set<std::pair<int,int>> none;return fixedTerrain?*fixedTerrain:none;}
    NorthlightShadowTerrain::FixedChunkBits fixedTerrainBits; /* 0.3.176 (U1c): beside fixedTerrain, filled at the commit */
    std::string uploadedMap;
    IDirect3DIndexBuffer9* liveIndicesGPU=nullptr;
    UINT liveIndexBytes=0;
    NorthlightDynamicRing::FrameFence frameFence; /* 0.3.192 (DXVK3): ONE EVENT query per frame for every upload ring, issued in endFrame() */
    NorthlightDynamicRing::FrameFence& fence(){frameFence.device=d;return frameFence;}
    NorthlightDynamicRing::Ring liveIndexRing;UINT liveIndexBase=0; /* 0.3.192 (DXVK3): liveIndexBytes = the ring capacity; liveIndexBase = the ring offset of the current lists, in indices */
    struct PendingMesh {
        std::shared_ptr<NorthlightGI::BVH> bvh;
        std::shared_ptr<NorthlightWorldMesh::WorldMeshUploadPlan> plan;
        std::shared_ptr<const PreparedCommit> prepared; /* from the same snapshot as plan; may be null */
        std::shared_ptr<NorthlightWorldMesh::UploadPayload> payload; /* 0.3.214 (task 42): pages + cutout textures for the whole staging */
        std::string map;
        NorthlightWorldMeshPages::Upload upload;
        IDirect3DTexture9* white=nullptr;
        std::vector<IDirect3DTexture9*> textures;
        size_t material=0,materialRow=0;
        IDirect3DTexture9* partialTexture=nullptr;
        DWORD started=GetTickCount();unsigned frames=0,textureUploads=0,textureReuses=0;
        // The upload cursor owns no GPU data; only textures are owned here.
        ~PendingMesh(){drop(partialTexture);drop(white);for(auto& t:textures)drop(t);}
    };
    std::unique_ptr<PendingMesh> pendingMesh;
    NorthlightWorldStreaming::Retry meshRetry;
    bool geometryRebuild=false; /* guarded by mutex: render thread asks the worker to rebuild the active generation */
    std::weak_ptr<NorthlightGI::BVH> payloadRebuildAsked;DWORD payloadRebuildTick=0;unsigned payloadRebuilds=0;
    unsigned streamingReports=0,preparedOwnerCommits=0;
    std::atomic<unsigned> generationDeferrals{0}; /* worker: canAdmit()==false events */
    /* 0.3.138: worker waits for the CPU reaper before a generation deferral. */
    std::atomic<unsigned> generationWaits{0},generationWaitPeakUs{0};
    size_t ownerChangesPeak=0;unsigned replayCreatedPeak=0;uint64_t replayTimeDeferred=0,replayGrowths=0,replayFallbacks=0,replayBudgetOverrides=0; /* overrides: meshes created past the time budget to avoid bulk growth */std::vector<size_t> replayTimeDeferredIndices; /* growth/fallback: cumulative, all frames */
    double streamingCpuPeakMs=0;unsigned streamingBudgetOverruns=0;
    NorthlightStreaming::PhaseProfile streamingPhases;
    NorthlightStreaming::RetirementBacklog retirementBacklog; /* render thread only */
    NorthlightVertexDeclarations::Cache declarationCache;
    NorthlightReplayBounds::Cache replayBoundsCache;
    NorthlightReplayMetadata::Cache<NorthlightReplayBounds::Prepared,IDirect3DVertexShader9,IDirect3DVertexDeclaration9> replayBoundsMetadata;
    std::unordered_set<IDirect3DVertexShader9*> terrainShaders;
    NorthlightLiveTerrainGPU::Cache<NorthlightTerrainCapture::MeshSnapshot,NorthlightGI::WorldVertex> liveTerrainGPU;
    size_t liveTerrainIndexCount=0,liveDirectionalIndexCount=0; /* 0.3.176 (U1a): the live IB holds the point lists, then the directional */
    uint64_t liveTerrainGeneration=0;
    // Ordered immutable captures preserve exact point-light batch offsets.
    // Vertex residency is per capture; the compact active index stream keeps
    // the directional pass at one draw even when residency is fragmented.
    using TerrainSnapshot = std::shared_ptr<const NorthlightTerrainCapture::MeshSnapshot>;
    std::vector<TerrainSnapshot> frameTerrain, uploadedTerrain;
    size_t frameTerrainVertices=0,frameTerrainIndices=0,terrainUploadBytes=0;
    bool terrainUploadReused=false;
    NorthlightStateBlockPool stateBlocks;
    unsigned terrainAttempts=0,terrainSnapshots=0,terrainFailures=0;
    NorthlightTerrainCapture::FrameCache terrainBoundsCache;
    NorthlightTerrainCandidates::ChunkSet liveTerrainChunks; /* 0.3.175: the set plus a flat bitmap */
    UINT width=0,height=0,vertexCount=0;
    struct Replay {
        IDirect3DVertexShader9* shader=nullptr,*originalShader=nullptr;
        NorthlightReplayBounds::Bounds pointBounds;
        NorthlightReplayBounds::WorkInfo boundsWork; // scheduling hint; cleared every frame, never an identity proof
        std::shared_ptr<const NorthlightReplayBounds::Prepared> boundsPrepared;
        IDirect3DVertexDeclaration9* decl=nullptr;
        IDirect3DVertexBuffer9* stream[4]={};
        UINT offset[4]={},stride[4]={};
        IDirect3DIndexBuffer9* index=nullptr;
        IDirect3DBaseTexture9* texture=nullptr;
        float constantStorage[1024]={};BOOL boolStorage[16]={};int intStorage[64]={};
        const float* constants=constantStorage;const BOOL* bools=boolStorage;const int* ints=intStorage;
        NorthlightConstantEpoch::Stamp constantStamp;
        Replay()=default;Replay(const Replay&)=delete;Replay& operator=(const Replay&)=delete;
        float cutoff=-1;
        NorthlightShaderConstants::Usage constantUsage;
        unsigned constantGroup=0; // exact consecutive banks, rebuilt from this frame's captures
        unsigned projectionKind=2;DWORD addressU=D3DTADDRESS_WRAP,addressV=D3DTADDRESS_WRAP;
        NorthlightDrawSnapshot::Mesh snapshot; // owned copy: DYNAMIC/UP draws and misses the cache could not store
        std::shared_ptr<const NorthlightDrawSnapshot::Mesh> shared; // immutable snapshot: tracked generations or legacy static cache
        const NorthlightDrawSnapshot::Mesh& mesh()const{return shared?*shared:snapshot;}
        bool gpuCached=false,shadowSkinned=false,shadowSelected=true,shadowSmall=false;int fateSlot=-1;unsigned char fateClass=0;float fateDistance=0; /* shadow fate diagnostics */
        bool bulkSingle=false; /* 0.3.211: uploadReplay's layout verdict (single-stream bulk placement) for the absolute pass */
        bool selfShadow=false; /* a draw of the selection's self body (set by the selection, reset at capture): the side tracker's self, ring 0 of the bounds job's near list */bool boneKnown=false;float bone=NAN; /* 0.3.176 (S2): this frame's selection tested its rigid bone (reset at capture) */
        float captureRoot[3]={};bool captureRootValid=false; /* 0.3.211 (task 31): the palette root computed once at capture (capture admission); passive for the selection */
        float anchor[3]={};bool anchorValid=false; /* an injected rigid prop (rigidMemoryInject): the origin of its remembered bone, in world space; it has no palette root */
        // The per-instance position known without bounds: the palette root, or the rigid prop's anchor. Null: neither.
        const float* position()const{return captureRootValid?captureRoot:anchorValid?anchor:nullptr;}
        // 0.3.177 (r83): filled at capture for selected skinned draws, read by prepareRecord (the stable
        // selection's prepare, possibly on the prepare worker): the program (null: none) and a copy of
        // the declaration (the declaration cache may reuse its slot within the frame). 0.3.179 (T1): the
        // program is the capture metadata's, not a handle; a program retired while a frame may point at it
        // lives in retiredPrograms until that frame is recycled (0.3.183: after a watchdog, pinned in
        // prepareQuarantinedPrograms until the worker has settled).
        const NorthlightActorDeformation::Program* program=nullptr;
        std::uint64_t prepareStamp=0; /* 0.3.183: the prepare epoch at capture (preparePublish); 0: pooled, rejected or injected */
        // 0.3.179 (T2): the frame's declaration copy (prepareDecls); null: the per-record copy below (the
        // arena was full) or not declared.
        const NorthlightActorPrepare::DeclCopy* declCopy=nullptr;
        std::array<D3DVERTEXELEMENT9,MAXD3DDECLLENGTH+1> elements{};UINT elementCount=0;bool declared=false;
        const D3DVERTEXELEMENT9* declarationElements()const{return declCopy?declCopy->elements:elements.data();}
        UINT declarationCount()const{return declCopy?declCopy->count:elementCount;}
        uint32_t staticProofMask=0,staticM2Mask=0;uint64_t staticProofRevision=0;std::string staticProofModel;const std::string* staticM2Model=nullptr; /* staticM2Model: the M2 proof's model key, owned by its StaticM2Dedup entry (valid for the frame) */ /* staticM2Mask: slots whose cache holds this rigid M2 (bounds in staticProofLow/High) */
        V staticProofLow,staticProofHigh;
        D3DPRIMITIVETYPE type;INT base;UINT min,vertices,start,count;bool indexed;
        void releaseResources(bool retainSnapshot=false){NorthlightReplayCaptureConstants::reset(*this);program=nullptr;declCopy=nullptr;prepareStamp=0;declared=false;elementCount=0;drop(shader);drop(originalShader);pointBounds={};boundsWork={};boundsPrepared.reset();drop(decl);drop(index);drop(texture);for(auto& s:stream)drop(s);shared.reset();if(!retainSnapshot)snapshot=NorthlightDrawSnapshot::Mesh{};}
        ~Replay(){releaseResources();}
    };
    std::vector<std::unique_ptr<Replay>> replays,freeReplays,heldShadowReplays;
    /* Sampled-frame instancing audit: geometry identity of each drawn replay in one pass. */
    struct ReplayMeshKey {const void* index;const void* stream;const void* decl;UINT offset,start,count;INT base;unsigned group;
        bool operator<(const ReplayMeshKey& o)const{return std::tie(index,stream,decl,offset,start,count,base,group)<std::tie(o.index,o.stream,o.decl,o.offset,o.start,o.count,o.base,o.group);}
        bool sameMesh(const ReplayMeshKey& o)const{return index==o.index&&stream==o.stream&&decl==o.decl&&offset==o.offset&&start==o.start&&count==o.count&&base==o.base;}};
    std::vector<ReplayMeshKey> replayMeshKeys;
    std::vector<NorthlightReplayShadowPolicy::Candidate> shadowCandidates;
    unsigned minSkinnedShadowTriangles=0,captureBudgetMiB=32,actorShadowBudgetMiB=0;bool shadowFateDiagnostics=false;
    NorthlightQuality::Settings quality; /* northlight-quality.ini; Settings{} is the full-quality default */
    bool shadowSelectionDone=false;
    unsigned replayTriangleBins[6]={},skinnedTriangleBins[6]={},acceptedTriangleBins[6]={},acceptedSkinnedTriangleBins[6]={};
    unsigned smallShadowEarly=0,smallShadowGI=0;
    size_t pooledSnapshotBytes=0;
    NorthlightDrawSnapshot::Frame replaySnapshots;
    // 0.3.181 (r89 S2/S3): the snapshot lookup mode, set at a capture frame's first snapshot read:
    // Prefetch, except on RenderProfile=1 non-sample frames, which rotate Map/Predict/Prefetch and are
    // metered (SNAPSHOT ab, every 600 such frames and at destruction). All modes are exact.
    NorthlightDrawSnapshot::SnapshotMeter<std::chrono::steady_clock> snapshotMeter;
    bool snapshotFrameBegun=false;unsigned snapshotFrameSerial=0,snapshotReportFrames=0;
    void snapshotBeginFrame(bool sample){
        using Lookup=NorthlightDrawSnapshot::Frame::Lookup;snapshotFrameBegun=true;
        const bool metered=NorthlightRenderThreadProbe::profiling()&&!sample;
        replaySnapshots.setLookup(metered?Lookup(snapshotFrameSerial%3):Lookup::Prefetch);
        snapshotMeter.beginFrame(metered,snapshotFrameSerial);
    }
    void snapshotEndFrame(){ /* before the snapshot frame is cleared (endFrame) */
        if(!snapshotFrameBegun)return;snapshotFrameBegun=false;
        const auto& r=replaySnapshots;snapshotMeter.endFrame(unsigned(r.lookup()),r.fastCacheHits(),r.predictTried(),r.predictHits(),r.predictMisses());
        ++snapshotFrameSerial;if(++snapshotReportFrames>=600){snapshotReportFrames=0;logSnapshotAb();}
    }
    void logSnapshotAb(){
        if(!NorthlightDiagnostics::enabled())return;bool any=false;NorthlightDrawSnapshot::Frame::KeyBench k;
        snapshotMeter.report([&](unsigned c,const auto& s){
            if(!any){any=true;k=NorthlightDrawSnapshot::Frame::benchKeys();}
            if(NorthlightDiagnostics::enabled())logf("SNAPSHOT ab class=%s frames=%u/%u/%u medianNs=%.1f/%.1f/%.1f p25=%.1f/%.1f/%.1f p75=%.1f/%.1f/%.1f pairNs=%.1f spans=%llu predictTried=%llu predictHits=%llu predictMisses=%llu fastHits=%llu keyEqLegacyNs=%.1f keyEqNs=%.1f hashNs=%.1f",
                NorthlightDrawSnapshot::SnapshotMeter<std::chrono::steady_clock>::className(c),s.frames[0],s.frames[1],s.frames[2],s.median[0],s.median[1],s.median[2],s.p25[0],s.p25[1],s.p25[2],s.p75[0],s.p75[1],s.p75[2],s.pairNs,
                s.spans,s.predictTried,s.predictHits,s.predictMisses,s.fastHits,k.legacyNs,k.equalNs,k.hashNs);});
    }
    // The next pooled Replay's lines that the capture touches after acquireReplay() (Prefetch mode).
    void prefetchReplay(const Replay& r){
        using NorthlightDrawSnapshot::prefetch;
        prefetch(&r.shader);prefetch(&r.originalShader);prefetch(&r.pointBounds);prefetch(&r.snapshot);prefetch(&r.shared);
        prefetch(&r.shadowSkinned);prefetch(&r.fateDistance);prefetch(&r.projectionKind);
    }
    std::unique_ptr<NorthlightActorPrepare::Caches> prepareCaches=std::make_unique<NorthlightActorPrepare::Caches>(); /* sampled vertex inputs and rigid bones (0.3.177: one owner at a time) */
    // 0.3.177 (r83): the prepare worker's state (world_shadow_experiment.inl; the worker itself is declared
    // after retiredPrograms). After a watchdog the abandoned worker keeps its caches and (0.3.183) only the
    // abandoned frame's replays (quarantined, never recycled), its arena and the programs pinned at the
    // abandon, all until it settles.
    std::unique_ptr<NorthlightActorPrepare::Caches> prepareAbandonedCaches,prepareCheckCaches;
    // 0.3.179 (T2): this frame's declaration copies (null: allocation failed, per-record copies), and the
    // arena an abandoned worker's frame may still read (0.3.183: moved there at the abandon, at most one).
    std::unique_ptr<NorthlightActorPrepare::DeclArena> prepareDecls=std::make_unique<NorthlightActorPrepare::DeclArena>();
    std::vector<std::unique_ptr<NorthlightActorPrepare::DeclArena>> prepareQuarantinedDecls;
    std::vector<std::unique_ptr<Replay>> prepareQuarantine;
    std::vector<std::shared_ptr<const NorthlightActorDeformation::Program>> prepareQuarantinedPrograms; /* 0.3.183: every program pinned at the abandon */
    NorthlightShadowFate::Tracker shadowFate;unsigned otherBlendRejected=0,otherFadeRejected=0,otherBudgetRejected=0,otherProjectionRejected=0;
    NorthlightActorShadowSelection::History actorShadowHistory;
    std::vector<NorthlightActorShadowSelection::Draw> actorShadowDraws;
    NorthlightActorShadowSelection::Scratch actorShadowScratch;size_t actorShadowToggles=0;unsigned actorShadowFrames=0;
    float actorShadowOrigin[3]={};bool actorShadowOriginValid=false,actorShadowOver=false;unsigned actorShadowTransitions=0,actorShadowCapBinds=0;double actorShadowPrepareMs=0,actorShadowChooseMs=0; /* last frame's shadow pivot ranks the actor quota */
    size_t actorShadowRadiusToggles=0,actorShadowRadiusFlicker=0,actorShadowRadiusRekeyed=0,actorShadowRadiusRekeyFlips=0,actorShadowAttachToFree=0,actorShadowFreeToAttach=0,actorShadowAttachToFreeBeyond=0,actorShadowFreeToAttachBeyond=0; /* ActorShadowRadius per log window: inside<->outside changes, changes within RadiusDwell of the last one, identities kept across a key change */
    NorthlightReplayGPU::SelectedCache replayGpuCache;
    NorthlightReplayBulk::Layout replayBulkLayout;
    IDirect3DVertexBuffer9* replayVerticesGPU[4]={};
    IDirect3DIndexBuffer9* replayIndicesGPU=nullptr;
    UINT replayVertexBytes[4]={},replayIndexBytes=0; /* 0.3.192 (DXVK3): the ring capacities */
    NorthlightDynamicRing::Ring replayVertexRing[4],replayIndexRing;
    // 0.3.177 (r83): shared handles: a replay captured with a program keeps it for the frame even if
    // registerShader() erases the entry (the prepare worker reads it without the map).
    std::unordered_map<IDirect3DVertexShader9*,std::shared_ptr<const NorthlightActorDeformation::Program>> actorPrograms;
    std::unordered_map<IDirect3DVertexShader9*,NorthlightActorDeformation::Program> actorUVPrograms;
    std::shared_ptr<NorthlightActorGeometry::ActorJob> actorJob=std::make_shared<NorthlightActorGeometry::ActorJob>();
    std::shared_ptr<const NorthlightActorGeometry::ActorJob> actorJobComplete;
    uint64_t actorJobSerial_=0;DWORD lastActorCapture=0;bool actorCaptureDecided=false,actorCaptureDue=false;std::string actorSceneMap_;
    unsigned actorVerticesEvaluated=0,actorDraws=0,actorSkippedAlpha=0,snapshotRejects=0;

    LONGLONG terrainCaptureTicks=0,replayCaptureTicks=0;
    LARGE_INTEGER captureFrequency={};
    unsigned terrainCaptureCalls=0,terrainUPCalls=0,replayCaptureCalls=0,unknownCaptureCalls=0;
    unsigned rejSmall=0,rejGate=0,rejCut=0,rejBudget=0,rejOther=0; /* task 43: per frame; WORLD CPU capture line */
    std::uint64_t previousCacheHits=0;
    size_t capturedConstantBytes=0,capturedConstantCalls=0;
    // 0.3.180 (C1): the device mirror's constant clock, read in place under the draw's gate.
    NorthlightReplayCaptureConstants::ClockSource<DeviceMirror> constantEpochSource;
    // 0.3.180 (C0): constantEpochStats keeps the self-check (verify()) and pose counters; the epoch and
    // block counters of RenderProfile sample frames go to constantEpochProfile. Both are cleared per frame.
    NorthlightReplayCaptureConstants::Stats constantEpochStats,constantEpochProfile;
    NorthlightReplayCaptureConstants::SelfCheck constantSelfCheckState{0,&constantEpochStats};
    unsigned capturedSM1Draws=0,capturedRelativeDraws=0;
    unsigned skinnedCandidates=0,skinnedBlendRejected=0,skinnedFadeRejected=0,skinnedProjectionRejected=0,skinnedBudgetRejected=0,skinnedSnapshotRejected=0,skinnedAccepted=0;
    size_t captureRejectedBytes=0,acceptedSkinnedBytes=0,acceptedOtherBytes=0;
    bool captureSampled=false;
    bool captureShortfall=false; /* this capture frame lost a draw to the capture limits (bytes or count) */
    // 0.3.140 cadence of the constant-epoch self-check (replay_capture_constants.h
    // verify(): repairs a pose on mismatch): the renderer's frame%120==0, independent
    // of Diagnostics and RenderProfile. The diagnostic `sample` is a separate, gated flag.
    bool constantSelfCheck=false;
    enum CapturePhase {CaptureState,CaptureProjection,CaptureSnapshot,CaptureConstants,CaptureMaterial,CaptureFinalize,CaptureActor,CapturePhaseCount};
    NorthlightCapturePhases::Stats<CapturePhaseCount> capturePhases;
    NorthlightCapturePhases::Stats<3> actorPhases;
    NorthlightCapturePhases::Stats<2> replayUploadPhases;
    unsigned capturePhaseSerial=0,capturePhaseDraws=0,capturePhaseAccepted=0,alphaStateQueriesSkipped=0;
    unsigned actorPhaseCandidates=0,actorTextureReads=0,replayUploadCalls=0,replayUploadFallbacks=0;
    void clearCaptureDiagnostics(){
        capturePhases.clear();actorPhases.clear();replayUploadPhases.clear();constantEpochStats={};constantEpochProfile={};constantSelfCheckState.serial=0;
        capturePhaseDraws=capturePhaseAccepted=alphaStateQueriesSkipped=0;
        actorPhaseCandidates=actorTextureReads=replayUploadCalls=replayUploadFallbacks=0;
    }
    std::unique_ptr<Replay> acquireReplay(){
        if(freeReplays.empty())return std::make_unique<Replay>();
        auto p=std::move(freeReplays.back());freeReplays.pop_back();pooledSnapshotBytes-=p->snapshot.capacityBytes();return p;
    }
    void recycleReplay(Replay* raw){
        if(prepareHeld(*raw)){try{prepareQuarantine.emplace_back(raw);}catch(...){} return;} /* 0.3.177: an abandoned worker may read it; on failure it leaks (0.3.183: its frame's only) */
        std::unique_ptr<Replay> p(raw);size_t capacity=p->snapshot.capacityBytes();
        bool keep=capacity<=replayPoolLimit()-std::min(replayPoolLimit(),pooledSnapshotBytes);p->releaseResources(keep);
        if(keep)pooledSnapshotBytes+=capacity;
        try{freeReplays.push_back(std::move(p));}catch(...){if(keep)pooledSnapshotBytes-=capacity;}
    }
    struct ReplayRecycle {WorldRenderer* owner;void operator()(Replay* replay)const{if(replay)owner->recycleReplay(replay);}};
    // Immutable for a registered shader. Re-registration replaces the complete
    // record before the pointer can be used for a different shader object.
    struct CaptureShader {
        IDirect3DVertexShader9* replacement=nullptr;
        unsigned projectionKind=0;
        NorthlightShaderConstants::Usage usage;
        bool skinned=false,sm1=false,fog=false;
        std::shared_ptr<const NorthlightActorDeformation::Program> program; /* 0.3.179 (T1): actorPrograms' object (null: none) */
    };
    std::unordered_map<IDirect3DVertexShader9*,CaptureShader> captureShaders;
    // 0.3.196 (task 12): one-entry lookup cache for repeated draws of the same shader. terrainShaders/captureShaders change only in
    // registerShader (a freed address can be re-registered), which bumps worldShaderGen first, so a stale entry never matches.
    // The cached CaptureShader pointer is a map node (stable across rehash) and is dropped with the generation on any erase.
    std::uint32_t worldShaderGen=0;
    struct ShaderLookup{IDirect3DVertexShader9* shader=nullptr;std::uint32_t gen=~0u;bool terrain=false;const CaptureShader* capture=nullptr;};
    ShaderLookup lastShader;unsigned long long shaderLookupHits=0,shaderLookupMisses=0;
    const ShaderLookup& lookupShader(IDirect3DVertexShader9* shader){
        if(lastShader.shader==shader&&lastShader.gen==worldShaderGen){++shaderLookupHits;return lastShader;}
        ++shaderLookupMisses;auto it=captureShaders.find(shader);
        lastShader.shader=shader;lastShader.gen=worldShaderGen;lastShader.terrain=terrainShaders.count(shader)!=0;lastShader.capture=it==captureShaders.end()?nullptr:&it->second;return lastShader;
    }
    // 0.3.179 (T1): programs erased by registerShader while replays may still point at them; released after
    // endFrame's recycle (0.3.183: an abandoned worker reads the copies pinned in prepareQuarantinedPrograms).
    std::vector<std::shared_ptr<const NorthlightActorDeformation::Program>> retiredPrograms;
    void retireProgram(std::shared_ptr<const NorthlightActorDeformation::Program>&& program){
        if(!program)return;
        try{retiredPrograms.push_back(std::move(program));}catch(...){new std::shared_ptr<const NorthlightActorDeformation::Program>(std::move(program));} /* no memory: leak it, never free it early */
    }
    // 0.3.177 (r83): the prepare worker. Declared after everything its records point at (replays, caches,
    // declaration arenas, quarantine, since 0.3.179 actorPrograms, captureShaders and retiredPrograms, and
    // since 0.3.183 prepareQuarantinedPrograms),
    // so it is destroyed first; ~WorldRenderer also joins it explicitly before anything else.
    NorthlightActorPrepare::Worker<Replay> prepareWorker;
    // Game terrain pixel shaders with their own shadow term neutralised
    // (patch_terrain_shadow.h); nullptr records a shader the patch rejected.
    // Bound only for terrain draws while the extension's shadows are active.
    std::unordered_map<IDirect3DPixelShader9*,IDirect3DPixelShader9*> terrainShadowShaders;
    std::unordered_map<IDirect3DVertexShader9*,IDirect3DVertexShader9*> terrainCoverageVertices;
    struct TerrainCoveragePixel {IDirect3DPixelShader9* shader=nullptr;unsigned constants=0;bool known=false;};
    std::unordered_map<IDirect3DPixelShader9*,std::array<TerrainCoveragePixel,2>> terrainCoveragePixels;
    float terrainCoverageMatrices[2][16]={},terrainCoverageView[16]={},terrainCoverageRows[24]={};
    bool terrainCoverageRowsValid=false;
    unsigned terrainCoverageSources=1;
    unsigned terrainCoverageReports=0;
    unsigned terrainShadowPatched=0,terrainShadowRejected=0,terrainShadowReports=0;
    // True only after a render() that drew at least one shadow source and
    // ran the lighting composite; terrain draws of the next frame consult it.
    bool shadowsComposited=false;
    // Shadow cascades are centred on the camera's orbit pivot (the player),
    // not the eye: orbiting the camera then leaves every cascade texel, the
    // near/far hand-over and the cached static maps exactly where they were,
    // so foliage shadows and light shafts do not change with the view. The
    // pivot distance is recovered from the intersection of successive centre
    // rays while the camera rotates; walking keeps the last distance.
    V pivotEye,pivotForward;bool pivotValid=false;float pivotDistance=12.f;unsigned pivotUpdates=0;
    // 0.3.190 (shadow_pivot.h): the distance follows a zoom/collision snap and the captured self. pivotSelfCaptured:
    // the latest selection (render thread, selectShadowReplays) captured the self (radiusSelf==1), at frame pivotSelfFrame.
    NorthlightShadowPivot::State pivotCorrection;bool pivotSelfCaptured=false;unsigned pivotSelfFrame=0;
    // 0.3.159: the cascade frames use (pivot.x, pivot.y, lowest pivot z of the last second), so a
    // jump leaves every pivot-relative shadow band where it was (cascade_anchor.h).
    NorthlightCascadeAnchor::Anchor cascadeAnchor;std::string cascadeAnchorMap;
    V shadowPivot(){
        const V eye=vec(context.camera);
        V forward=V(context.inverseView[8],context.inverseView[9],context.inverseView[10])*projection[2];
        const float length=std::sqrt(NorthlightGI::dot(forward,forward));if(!(length>1e-6f))return eye;forward=forward*(1.f/length);
        if(!pivotValid){pivotEye=eye;pivotForward=forward;pivotValid=true;}
        else{
            const float b=NorthlightGI::dot(pivotForward,forward);
            // Two rays only 2 degrees apart locate their crossing to within
            // gap/sin(2 deg), i.e. tens of units, and the estimate swung
            // between 12 and 26 u while orbiting. Wait for a 12 degree turn
            // (error under 3 u for a .6 u miss), then filter slowly.
            if(b<.978f){
                const V w0=pivotEye-eye;const float d=NorthlightGI::dot(pivotForward,w0),e=NorthlightGI::dot(forward,w0),denom=1-b*b;
                const float sRef=(b*e-d)/denom,t=(e-b*d)/denom;
                const V onRef=pivotEye+pivotForward*sRef,onNow=eye+forward*t,gap=onRef-onNow;
                if(t>1&&t<80&&NorthlightGI::dot(gap,gap)<.36f){
                    const float step=std::max(-2.f,std::min(2.f,(t-pivotDistance)*.25f));
                    pivotDistance+=step;++pivotUpdates;
                }
                pivotEye=eye;pivotForward=forward;
            }else{
                // Walking moves the pivot point itself; an orbit with the right
                // distance does not. Resetting on eye motion alone discarded the
                // reference every 2 u, before a 12 degree turn could accumulate.
                const V refPivot=pivotEye+pivotForward*pivotDistance,nowPivot=eye+forward*pivotDistance,moved=nowPivot-refPivot;
                if(NorthlightGI::dot(moved,moved)>16){pivotEye=eye;pivotForward=forward;}
            }
        }
        // 0.3.190: zoom, collision snap and the captured self move the distance along the ray (ShadowPivotCorrection=0: untouched).
        const bool selfFresh=pivotSelfCaptured&&frames-pivotSelfFrame<=4&&actorShadowHistory.selfHold()>0;
        pivotDistance=NorthlightShadowPivot::correct(quality.shadowPivotCorrection!=0,pivotCorrection,&eye.x,&forward.x,pivotDistance,selfFresh?actorShadowHistory.selfAt():nullptr);
        return eye+forward*pivotDistance;
    }
    std::unordered_map<IDirect3DVertexShader9*,const WmoShaderSignature*> wmoShaders;
    unsigned cameraChecks=0,wmoContexts=0,wmoRejects=0;
    static V vec(const float* p){return V(p[0],p[1],p[2]);}
    static bool different(V a,V b,float e){return NorthlightGI::dot(a-b,a-b)>e*e;}
    static V quantize(V p,float step){return V(std::floor(p.x/step)*step,std::floor(p.y/step)*step,std::floor(p.z/step)*step);}
#include "world_point_rendering.inl"
    static NorthlightGeometryMemory::Sample geometryMemory() {
        MEMORYSTATUSEX info={};info.dwLength=sizeof info;
        NorthlightGeometryMemory::Sample sample;
        if(!GlobalMemoryStatusEx(&info))return sample;
        sample.available=info.ullAvailVirtual;uint64_t walkedFree=0;
        uintptr_t address=0;MEMORY_BASIC_INFORMATION region={};
        while(VirtualQuery(reinterpret_cast<const void*>(address),&region,sizeof region)==sizeof region){
            if(region.State==MEM_FREE){sample.largest=std::max(sample.largest,uint64_t(region.RegionSize));walkedFree+=uint64_t(region.RegionSize);}
            const uint64_t next=uint64_t(reinterpret_cast<uintptr_t>(region.BaseAddress))+uint64_t(region.RegionSize);
            if(next<=address||next>std::numeric_limits<uintptr_t>::max())break;
            address=uintptr_t(next);
        }
        sample.available=NorthlightGeometryMemory::noteWalk(sample.available,walkedFree);
        sample.valid=sample.largest>0;return sample;
    }
    static NorthlightGeometryMemory::Sample geometryAdmissionFor(NorthlightMemoryAdmission::Probe& probe,NorthlightGeometryMemory::Budget budget,bool* exact=nullptr){
        // Remember only candidate addresses. Every admission gets current
        // aggregate availability and a fresh query of the candidate free block.
        // A stale/small witness falls back to the complete address-space walk.
        const auto result=probe.sample(budget,
            [](uint64_t& available){MEMORYSTATUSEX info={};info.dwLength=sizeof info;
                if(!GlobalMemoryStatusEx(&info))return false;available=NorthlightGeometryMemory::corrected(info.ullAvailVirtual);return true;},
            [](uint64_t address,NorthlightMemoryAdmission::Region& output){MEMORY_BASIC_INFORMATION region={};
                if(VirtualQuery(reinterpret_cast<const void*>(uintptr_t(address)),&region,sizeof region)!=sizeof region)return false;
                output.base=uint64_t(reinterpret_cast<uintptr_t>(region.BaseAddress));output.size=uint64_t(region.RegionSize);output.free=region.State==MEM_FREE;return true;},
            uint64_t(std::numeric_limits<uintptr_t>::max()));
        if(exact)*exact=result.fullScan;
        return result.memory;
    }
    // Issue #40: true when world-cache/<map>/ holds at least one .fg3 tile. The cache
    // covers only the continents; instance maps have no folder at all. Only a missing folder or
    // tile means "no cache": any other failure (access, lock, I/O) answers true, so the normal
    // build runs and reports it instead of the map's geometry being off until the map changes.
    bool worldCacheHasTiles(const std::string& map)const{
        if(map.empty())return false;
        WIN32_FIND_DATAA data;const HANDLE h=FindFirstFileA((root+"world-cache/"+map+"/*.fg3").c_str(),&data);
        if(h==INVALID_HANDLE_VALUE){const DWORD error=GetLastError();return error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND;}
        FindClose(h);return true;}
    NorthlightGeometryMemory::Sample geometryAdmission(NorthlightGeometryMemory::Budget budget,bool* exact=nullptr){
        return geometryAdmissionFor(admissionProbe,budget,exact);
    }
    // largestFreeMiB is ">=N" unless exact (a witnessed lower bound is not a measurement); refused= names the
    // limit of `budget` that failed (available/largest/...) or none, need= its requirement.
    static void logGeometryMemory(const char* stage,NorthlightGeometryMemory::Sample sample,size_t generations=0,bool exactLargest=true,const NorthlightGeometryMemory::Budget* budget=nullptr){
        char largest[32];NorthlightGeometryMemory::formatLargestMiB(largest,sizeof largest,sample.largest,exactLargest);
        const auto why=budget?NorthlightGeometryMemory::refusal(sample,*budget):NorthlightGeometryMemory::Refusal{};
        logf("GEOMETRY MEMORY stage=%s availableVirtualMiB=%llu largestFreeMiB=%s generations=%zu valid=%u largestFreeExact=%u refused=%s need=%llu",
            stage,(unsigned long long)(sample.available>>20),largest,generations,unsigned(sample.valid),unsigned(exactLargest),why.limit,(unsigned long long)(why.need>>20));
    }
    bool admitStaticAllocation(size_t managedBytes){
        // Preserve one aggregate transaction per static frame and its existing
        // reservation debits. A witnessed block is only a lower bound: a later
        // larger resource must re-query rather than treat that bound as the max.
        const bool first=staticAdmissionFrame!=staticFrame;
        if(first){staticAdmissionFrame=staticFrame;staticReservedBytes=0;}
        NorthlightGeometryMemory::Budget budget;
        budget.available=NorthlightGeometryMemory::ProcessReserve+64*NorthlightGeometryMemory::MiB+managedBytes+staticReservedBytes;
        budget.largest=managedBytes+NorthlightGeometryMemory::ChunkMargin;budget.valid=true;
        if(first)staticMemorySample=geometryAdmission(budget,&staticMemoryExact);
        else if(!staticMemoryExact&&staticMemorySample.valid&&staticMemorySample.available>=budget.available&&staticMemorySample.largest<budget.largest){
            auto current=geometryAdmission(budget,&staticMemoryExact);
            current.available=std::min(current.available,staticMemorySample.available);
            staticMemorySample=current;
        }
        if(!NorthlightGeometryMemory::admits(staticMemorySample,budget))return false;
        staticReservedBytes+=managedBytes;return true;
    }
    void requestStaticCasters(const char* map){
        if(!staticStream)return;
        if(staticRecoveryPaused&&!(active&&active->bvh))return;
        staticFramePivot=shadowPivot();staticPivotReady=true;
        const DWORD now=GetTickCount();
        if(!staticMemoryTick||DWORD(now-staticMemoryTick)>=1000){
            staticMemoryTick=now;
            const auto budget=NorthlightGeometryMemory::buildBudget(64*NorthlightGeometryMemory::MiB);
            staticAllowLoads=NorthlightGeometryMemory::admits(geometryAdmission(budget),budget);
        }
        const bool lit[2]={NorthlightGI::dot(sourceColors[0],sourceColors[0])>1e-10f,
                           NorthlightGI::dot(sourceColors[1],sourceColors[1])>1e-10f};
        try {staticStream->request(StaticShadow::makeRequest(map,staticFramePivot,sourceDirections,lit,
                celestialValid,celestialValid?celestial.dayFraction:0.,staticAllowLoads));
             if(staticRecoveryPaused){staticStream->setPaused(false);staticRecoveryPaused=false;logf("STATIC SHADOW recovery paused=0 reason=geometry-ready");}}
        catch(...){if(staticDrawFailures++<4)logf("STATIC SHADOW request deferred: allocation failure; existing shadows retained");}
    }
    void updateStaticCasters(bool allowUploads=true,NorthlightStreaming::Budget* budget=nullptr){
        staticCasters.settle(); /* 0.3.152: no plan job across the static cache update */
        ++staticFrame;
        if(!staticStream)return;
        const DWORD now=GetTickCount();
        auto next=staticStream->snapshot();
        // Never carry another world's casters while its replacement loads.
        if(staticScene&&staticScene->map!=lastRequest.map){staticCasters.reset();staticMatcher.clear();staticM2Memory.clear();staticScene.reset();staticOwnerGeneration=UINT64_MAX;}
        if(next&&next->map==lastRequest.map)staticScene=std::move(next);
        if(staticOwnerGeneration!=meshGeneration){
            auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Owners);
            try {staticCasters.setCoveredOwners(uploadedStaticOwners?std::shared_ptr<const StaticShadow::OwnerMap>(uploadedStaticOwners,&uploadedStaticOwners->owners):nullptr);staticOwnerGeneration=meshGeneration;
                 ownerChangesPeak=std::max(ownerChangesPeak,staticCasters.lastOwnerChanges());}
            catch(...){staticCasters.setCoveredPlacements({});staticOwnerGeneration=UINT64_MAX;}
        }
        if(staticScene&&(!staticRetryTick||DWORD(now-staticRetryTick)>=1000)){
            auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::StaticCache);
            try {if(!staticCasters.update(d,staticScene,staticFrame,allowUploads,budget))staticRetryTick=now;else staticRetryTick=0;
                 streamingPhases.record(NorthlightStreaming::PhaseProfile::StaticPublish,staticCasters.stats().publishFrameMs);}
            catch(...){staticRetryTick=now;if(staticDrawFailures++<4)logf("STATIC SHADOW upload deferred: allocation failure; base shadows retained");}
        }
        if(staticScene&&NorthlightDiagnostics::enabled()&&(!staticLogTick||DWORD(now-staticLogTick)>=2000)){
            staticLogTick=now;const auto& c=staticScene->stats;const auto& g=staticCasters.stats();
            logf("STATIC INSTANCE STREAM totalLocks=%llu totalDiscards=%llu totalBytes=%llu",
                 (unsigned long long)g.instanceLocks,(unsigned long long)g.instanceDiscards,(unsigned long long)g.instanceBytes);
            logf("STATIC SHADOW PLANS builds=%llu hits=%llu invalidations=%llu placementTests=%llu batchTests=%llu modelBuilds=%llu modelReuses=%llu modelFallbacks=%llu",
                 (unsigned long long)g.planBuilds,(unsigned long long)g.planHits,(unsigned long long)g.planInvalidations,
                 (unsigned long long)g.placementTests,(unsigned long long)g.batchTests,
                 (unsigned long long)g.modelPlanBuilds,(unsigned long long)g.modelPlanReuses,(unsigned long long)g.modelPlanFallbacks);
            logf("STATIC SHADOW PLAN COST planMs=%.3f peakMs=%.3f coveredMemoFills=%llu evictions=%llu fastLookups=%u lru=%u selfChecks=%llu selfCheckMismatches=%llu prebuilds=%u prebuildHits=%u prebuildMisses=%u prebuildFailures=%u prebuildUnstable=%u prebuildDiscards=%u planDiscards=%llu reuseMs=%.3f rebuildMs=%.3f walkMs=%.3f asyncKicks=%llu asyncBuilds=%llu stolen=%llu asyncFailures=%llu waitMs=%.3f workerMs=%.3f asyncDirtyRects=%llu kickMs=%.3f",
                 g.planMs,g.planPeakMs,(unsigned long long)g.coveredMemoFills,(unsigned long long)g.planEvictions,unsigned(StaticShadow::GpuCache::FastPlanLookups),unsigned(StaticShadow::GpuCache::LruPlanSlots),
                 (unsigned long long)g.planSelfChecks,(unsigned long long)g.planSelfCheckMismatches,staticPrebuild.stats.built,staticPrebuild.stats.hits,staticPrebuild.stats.misses,staticPrebuild.stats.failures,staticPrebuild.stats.unstable,staticPrebuild.stats.discarded,(unsigned long long)g.planDiscards,
                 g.planReuseMs,g.planRebuildMs,g.planWalkMs,(unsigned long long)g.asyncKicks,(unsigned long long)g.asyncBuilds,(unsigned long long)g.asyncStolen,(unsigned long long)g.asyncFailures,g.asyncWaitMs,g.asyncWorkerMs,staticDirtyJobsUsed,staticKickMs);
            logf("STATIC SHADOW PUBLISH publications=%llu publishMs=%.3f peakMs=%.3f exactDiffs=%llu diffFallbacks=%llu ownerTests=%llu idleReleases=%llu exact=%u grouped=%u idle=%u",
                 (unsigned long long)g.publications,g.publishMs,g.publishPeakMs,(unsigned long long)g.publishDiffs,(unsigned long long)g.publishDiffFallbacks,(unsigned long long)g.ownerTests,(unsigned long long)g.planIdleReleases,
                 unsigned(StaticShadow::GpuCache::ExactPublishDiff),unsigned(StaticShadow::GpuCache::GroupedOwnerInvalidation),unsigned(StaticShadow::GpuCache::IdlePlanRelease));
            logf("STATIC SHADOW map=\"%s\" generation=%llu placements=%u cpuModels=%u readyGPU=%u pendingCPU=%u pendingGPU=%u metadataReads=%llu modelReads=%llu reused=%llu cpuMiB=%.2f cpuPeakMiB=%.2f transientPeakMiB=%.2f gpuMiB=%.2f gpuPeakMiB=%.2f uploadBytes=%llu totalUploadBytes=%llu uploadMs=%.3f readMs=%.3f packMs=%.3f maxJobMs=%.3f latencyMs=%.3f instancing=%u draws=%llu instances=%llu cpuFailures=%llu gpuFailures=%llu dedupSkipped=%u complete=%u dedupAttempts=%u dedupMatches=%u localOwners=%zu metadataMiB=%.2f selectionMs=%.3f m2Skipped=%u m2Verified=%u m2Rejected=%u",
                 staticScene->map.c_str(),(unsigned long long)staticScene->mapGeneration,c.placements,c.models,g.readyModels,c.pendingModels,g.pendingModels,
                 (unsigned long long)c.metadataReads,(unsigned long long)c.modelReads,(unsigned long long)c.modelReuses,
                 double(c.cpuBytes)/1048576,double(c.cpuPeak)/1048576,double(c.transientPeak)/1048576,double(g.residentBytes)/1048576,double(g.peakBytes)/1048576,
                 (unsigned long long)g.frameBytes,(unsigned long long)g.uploadedBytes,g.uploadMs,c.readMs,c.packMs,c.maxJobMs,c.latencyMs,unsigned(g.instancing),
                 (unsigned long long)g.drawCalls,(unsigned long long)g.instances,(unsigned long long)c.failures,(unsigned long long)g.failures,staticDedupSkipped,unsigned(c.complete),staticDedupAttempts,staticDedupMatches,(uploadedStaticOwners?uploadedStaticOwners->placements:size_t(0)),double(c.metadataBytes)/1048576,c.selectionMs,staticM2Skipped,staticM2Verified,staticM2Rejected);
            staticDedupSkipped=staticDedupAttempts=staticDedupMatches=staticM2Skipped=staticM2Verified=staticM2Rejected=0;
        }
    }
    void work() {
      // Below-normal priority: the solve competes with the single game thread
      // for the same performance cores under Rosetta; it is latency-tolerant.
      SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
      try {
        // GIThreads>1: helpers at the same below-normal priority; probe values are scheduling-independent.
        std::unique_ptr<NorthlightGI::SolvePool> solvePool;
        NorthlightQuality::Settings solverQuality;solverQuality.giThreads=quality.giThreads;
        const unsigned solverThreads=NorthlightQuality::giSolverThreads(solverQuality,NorthlightStream::cores());
        if(solverThreads>1)solvePool=std::make_unique<NorthlightGI::SolvePool>(solverThreads-1,[]{SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);});
        if(quality.giThreads>1)logf("QUALITY GI solver threads requested=%u effective=%u cores=%u priority=below-normal",quality.giThreads,solverThreads,NorthlightStream::cores());
        // 0.3.153: geometry regions are built on their own below-normal thread
        // while this worker keeps solving camera moves against the published
        // previous generation. The builder alone owns the local geometry cache,
        // memory admission, light cache and palette region; it never touches GI
        // state. Guarded by mutex: the finished build, the worker's request for
        // one after it dropped its region, and builder exit.
        struct Built {std::shared_ptr<NorthlightGI::BVH> bvh;std::shared_ptr<NorthlightWorldMesh::WorldMeshUploadPlan> plan;std::shared_ptr<const PreparedCommit> prepared;
            std::shared_ptr<const NorthlightRegionalFog::Field> fog;std::vector<NorthlightGI::PointLight> lights;std::vector<NorthlightLocalLights::Light> rawLights;
            std::string map;V center;uint64_t id=0;DWORD ms=0;unsigned superseded=0;};
        std::unique_ptr<Built> builtGeometry;bool geometryWanted=false,builderExit=false;
        std::condition_variable buildWake;std::thread builder;
        // Joins before anything the builder uses is destroyed, also when this worker faults.
        struct BuilderJoin {std::mutex& mutex;std::condition_variable& wake;bool& exit;std::thread& thread;
            ~BuilderJoin(){{std::lock_guard<std::mutex> lock(mutex);exit=true;}wake.notify_all();if(thread.joinable())thread.join();}} builderJoin{mutex,buildWake,builderExit,builder};
        builder=std::thread([&]{
          SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
          try {
          NorthlightGeometryMemory::Generations<NorthlightGI::BVH> generations;
          NorthlightGI::LocalSceneCache localGeometry;
          // Independent worker-owned address witness. Every allocation still
          // queries CURRENT aggregate free memory and a CURRENT free region;
          // a stale/split witness falls back to the complete address-space walk.
          NorthlightMemoryAdmission::Probe workerAdmissionProbe;
          bool workerMemoryExact=true;double workerMemoryMs=0;
          auto workerMemory=[&](NorthlightGeometryMemory::Budget budget=NorthlightGeometryMemory::buildBudget()){
              const auto begin=std::chrono::steady_clock::now();
              const auto result=geometryAdmissionFor(workerAdmissionProbe,budget,&workerMemoryExact);
              workerMemoryMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
              return result;
          };
          DWORD memoryReportAt=0;
          NorthlightTerrainReach::State reachState;NorthlightGI::SceneSizeHint terrainSizeHint; /* terrain scene counts of the last build; keyed by reach, reset per map */NorthlightTerrainReach::Episode memoryStall;
          NorthlightGeometryMemory::EntryLift entryLift,chunkLift;
          std::string entryLiftMap;V entryLiftCenter;
          // Episode logs (begin on the first memory refusal, end when the build is delivered or dropped).
          // `required` is the refused check's aggregate floor: published (at most the plain entry floor)
          // as the memory guard's demand, so the mod's own caches shrink while the builder waits.
          auto stallBegin=[&](const char* stage,NorthlightGeometryMemory::Sample sample,uint64_t required,uint64_t requiredLargest=0){
              NorthlightMemMap::deferredRequest.store(true,std::memory_order_relaxed); /* task 42: the Device queues a MEMMAP memory-deferred point (throttled there) */
              geometryMemoryDemand.store(std::min(required,NorthlightGeometryMemory::buildBudget().available),std::memory_order_relaxed);
              if(memoryStall.refused(GetTickCount())){
                  char largest[32];NorthlightGeometryMemory::formatLargestMiB(largest,sizeof largest,sample.largest,workerMemoryExact);
                  NorthlightGeometryMemory::Budget checked;checked.available=required;checked.largest=requiredLargest;checked.valid=true;
                  const auto why=NorthlightGeometryMemory::refusal(sample,checked);
                  logf("WORLD geometry memory stall begin stage=%s availableMiB=%llu largestMiB=%s generations=%zu refused=%s need=%llu",
                      stage,(unsigned long long)(sample.available>>20),largest,generations.live(),why.limit,(unsigned long long)(why.need>>20));}
          };
          auto stallEnd=[&](const char* outcome){
              geometryMemoryDemand.store(0,std::memory_order_relaxed);
              uint32_t ms=0;unsigned tries=0;
              if(memoryStall.end(GetTickCount(),ms,tries))logf("WORLD geometry memory stall end ms=%u attempts=%u outcome=%s",ms,tries,outcome);
          };
          auto abandonGeometry=[&](){
              // A measured peak belongs to the abandoned spatial region. It
              // must not refuse a different region before that build starts.
              entryLift.clear();chunkLift.clear();terrainSizeHint={};stallEnd("superseded");
          };
          NorthlightLocalLights::Cache lightCache(root+"world-cache/lights");
          // Last delivered region. Any build that does not deliver forces the next
          // request to build again, as the 0.3.151 worker did without a BVH.
          std::string builtMap;V builtCenter;bool builtValid=false,wanted=false,retry=false;uint64_t seen=0;unsigned superseded=0;
          std::string cacheCheckedMap;bool cacheMapHasTiles=true,uncachedPublished=false;unsigned uncachedSkips=0;uint64_t uncachedSkippedId=0;
          for(;;){
            Request r;
            {std::unique_lock<std::mutex> lock(mutex);buildWake.wait(lock,[&]{return stopping||builderExit||retry||geometryWanted||request.id!=seen;});
             if(stopping||builderExit)return;r=request;seen=r.id;retry=false;wanted=wanted||geometryWanted;geometryWanted=false;}
            if(workerMemoryTrim.exchange(false,std::memory_order_relaxed))localGeometry.reset(); /* the entry lift stays: it is a floor on measured free memory, so what a trim freed already counts */
            // Publish small authored zone data before expensive geometry work.
            // No cache reads are performed by the sky or world draw callbacks.
            int paletteTX=int(std::floor((NorthlightRegionalFog::WorldZero-r.camera.y)/NorthlightRegionalFog::TileSize));
            int paletteTY=int(std::floor((NorthlightRegionalFog::WorldZero-r.camera.x)/NorthlightRegionalFog::TileSize));
            std::shared_ptr<const PaletteRegion> paletteRegion;
            {std::lock_guard<std::mutex> lock(mutex);paletteRegion=publishedPaletteRegion;}
            if(!paletteRegion||paletteRegion->map!=r.map||paletteRegion->tx!=paletteTX||paletteRegion->ty!=paletteTY){
                auto next=std::make_shared<PaletteRegion>();next->map=r.map;next->tx=paletteTX;next->ty=paletteTY;
                next->region=NorthlightRegionalFog::loadRegion(root+"world-cache/fog",r.map,r.camera.x,r.camera.y);
                paletteRegion=next;
                {std::lock_guard<std::mutex> lock(mutex);publishedPaletteRegion=next;}
            }
            if(!wanted&&builtValid&&!NorthlightWorldStreaming::needsGeometry(builtMap,builtCenter,r.map,r.geometryCenter))continue;
            builtValid=false;
            auto result=std::make_shared<Snapshot>();result->map=r.map;result->center=r.geometryCenter;
            DWORD started=GetTickCount();result->requestId=r.id;result->requestedAt=r.queuedAt;result->queueMs=started-r.queuedAt;
            auto publishError=[&](){
                stallEnd("error"); // the build ends here whatever the publication outcome
                std::lock_guard<std::mutex> lock(mutex);
                if(stopping||request.id!=r.id){++superseded;return false;}
                // A failed replacement must not replace usable geometry with
                // an empty error snapshot. Its existing spatial expiry remains.
                if(published&&published->bvh&&NorthlightWorldStreaming::retained(published->map,published->center,request.map,request.camera)){
                    logf("WORLD replacement deferred; previous region retained: %s",result->message.c_str());return false;
                }
                published=result;return true;
            };
            // Geometry validity is spatial, independent of the latest GI
            // request ID. A camera retarget must not discard a usable build.
            auto currentGeometry=[&](){std::lock_guard<std::mutex> lock(mutex);return !stopping&&NorthlightWorldStreaming::applicable(r.map,r.geometryCenter,request.map,request.camera);};
            auto deferBuild=[&](const char* stage,NorthlightGeometryMemory::Sample sample,unsigned delay,const NorthlightGeometryMemory::Budget* budget=nullptr){
                DWORD now=GetTickCount();if(!memoryReportAt||now-memoryReportAt>=1000){memoryReportAt=now;logGeometryMemory(stage,sample,generations.live(),workerMemoryExact,budget);}
                // The consumed request must survive a stationary camera. Never
                // replace a newer request with r; next loop reads latest state.
                bool abandoned=false;
                {std::unique_lock<std::mutex> lock(mutex);retry=true;
                 buildWake.wait_for(lock,std::chrono::milliseconds(delay),[&]{return stopping||builderExit||request.id!=r.id;});
                 // The request moved on and this region no longer applies: the stalled build is dropped.
                 abandoned=stopping||builderExit||!NorthlightWorldStreaming::applicable(r.map,r.geometryCenter,request.map,request.camera);}
                if(abandoned)abandonGeometry();
            };
            // Issue #40: a map without world-cache tiles (dungeons, raids, battlegrounds) is checked
            // once per entry; its error is published once and no build runs until the map changes.
            if(r.map!=cacheCheckedMap){
                if(!cacheMapHasTiles&&NorthlightDiagnostics::enabled())logf("WORLD cache absent end map=\"%s\" skippedBuilds=%u",cacheCheckedMap.c_str(),uncachedSkips);
                cacheCheckedMap=r.map;cacheMapHasTiles=worldCacheHasTiles(r.map);uncachedPublished=false;uncachedSkips=0;entryLift.clear();chunkLift.clear();reachState={};terrainSizeHint={};
                if(!cacheMapHasTiles)logf("WORLD cache absent map=\"%s\"; geometry builds skipped until the map changes",r.map.c_str());
            }
            if(!cacheMapHasTiles){
                if(!uncachedPublished){result->message="No cached geometry for "+r.map;uncachedPublished=publishError();}
                else if(r.id!=uncachedSkippedId){uncachedSkippedId=r.id;++uncachedSkips;} /* one count per GI request, not per builder wake */
                continue;
            }
            // Keep the refused attempt's anchor: a stream of small requests
            // must not carry its peak estimate across arbitrarily far travel.
            if(entryLift.armed&&!NorthlightWorldStreaming::applicable(entryLiftMap,entryLiftCenter,r.map,r.camera)){
                entryLift.clear();chunkLift.clear();terrainSizeHint={};
            }
            auto built=std::make_unique<Built>();
            bool chunkedLocal=false,coldRecovery=false;
#if NORTHLIGHT_DEV_TUNING
            const auto tuning=devSnapshot(); // ONE snapshot per build: the terrain reach and the ground fog scales both use it
#endif
            // BEGIN REGION BUILD: tests substitute a synthetic region for this block.
            {
                const auto memoryBefore=workerAdmissionProbe.stats();const double memoryMsBefore=workerMemoryMs;
                auto phaseStart=std::chrono::steady_clock::now();
                auto phaseElapsed=[&](){const auto now=std::chrono::steady_clock::now();const double ms=std::chrono::duration<double,std::milli>(now-phaseStart).count();phaseStart=now;return ms;};
                // Probe with the lifted budget: a witness block below its contiguous floor falls back to a full walk.
                auto memory=workerMemory(entryLift.apply(NorthlightGeometryMemory::buildBudget(64*NorthlightGeometryMemory::MiB),GetTickCount()));
                if(!generations.canAdmit()&&NorthlightStreaming::RetireOversizedWhenIdle){
                    // An old generation may be in the reaper queue instead of
                    // having been freed on the render thread (0.3.137). Wait
                    // for that destruction (bounded) rather than 100 ms steps.
                    const auto waitStart=std::chrono::steady_clock::now();
                    NorthlightStreaming::cpuRetirement().waitIdle(NorthlightStreaming::RetireWaitMs);++generationWaits;
                    const unsigned us=unsigned(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-waitStart).count());
                    unsigned peak=generationWaitPeakUs.load();while(us>peak&&!generationWaitPeakUs.compare_exchange_weak(peak,us)){}
                }
                if(!generations.canAdmit()){++generationDeferrals;{std::lock_guard<std::mutex> lock(mutex);generationStall.deferred(GetTickCount());}deferBuild("generation-deferred",memory,100);continue;}
                {std::lock_guard<std::mutex> lock(mutex);generationStall.admitted();}
                // The incremental cache is dropped only when the plain entry budget fails; a raised
                // entry floor (entryLift) alone keeps it for the attempt that eventually runs.
                const auto plainEntry=NorthlightGeometryMemory::buildBudget(64*NorthlightGeometryMemory::MiB);
                auto entryBudget=entryLift.apply(plainEntry,GetTickCount());
                if(!NorthlightGeometryMemory::admits(memory,plainEntry)){localGeometry.reset();memory=workerMemory(plainEntry);}
                // BEGIN CHUNKED ENTRY SELECTION
                if(!NorthlightGeometryMemory::admits(memory,entryBudget)){
                    const auto chunkBudget=chunkLift.apply(NorthlightGeometryMemory::chunkBuildBudget(),GetTickCount());
                    memory=workerMemory(chunkBudget);entryBudget=chunkBudget;chunkedLocal=true;
                }
                if(!NorthlightGeometryMemory::admits(memory,entryBudget)){
                    // A bounded candidate also replaces a retained generation.
                    // Waiting for that generation to expire made lighting blink
                    // at every 160-unit boundary below the normal 480 MiB floor.
                    const auto cold=chunkLift.apply(NorthlightGeometryMemory::coldChunkBuildBudget(),GetTickCount());
                    memory=workerMemory(cold);entryBudget=cold;coldRecovery=true;
                }
                // END CHUNKED ENTRY SELECTION
                if(!NorthlightGeometryMemory::admits(memory,entryBudget)){stallBegin("build-deferred",memory,entryBudget.available,entryBudget.largest);deferBuild("build-deferred",memory,1000,&entryBudget);continue;}
                const auto geometryBudget=[&](uint64_t bytes=0){return coldRecovery?NorthlightGeometryMemory::coldInBuildBudget(bytes):NorthlightGeometryMemory::inBuildBudget(bytes);};
                const uint64_t entryAvailable=memory.available,entryLargest=memory.largest;
                geometryMemoryDemand.store(0,std::memory_order_relaxed); // admitted: the build's own use is not a demand (the episode stays open)
                // A memory refusal after entry: the next entry waits for what this attempt lacked.
                auto liftEntry=[&](const char* stage,uint64_t required,uint64_t observed,uint64_t requiredLargest){
                    entryLiftMap=r.map;entryLiftCenter=r.geometryCenter;
                    auto& lift=chunkedLocal?chunkLift:entryLift;lift.refused(entryAvailable,required,observed,GetTickCount(),requiredLargest);
                    logf("WORLD geometry entry raised stage=%s requiredAvailableMiB=%llu requiredLargestMiB=%llu entryMiB=%llu refusedMiB=%llu stageRequiredMiB=%llu holdMs=%u",
                        stage,(unsigned long long)(entryLift.available>>20),(unsigned long long)(entryLift.largest>>20),(unsigned long long)(entryAvailable>>20),
                        (unsigned long long)(observed>>20),(unsigned long long)(required>>20),unsigned(NorthlightGeometryMemory::LiftMs));
                };
                if(NorthlightDiagnostics::enabled())logGeometryMemory("before-load",memory,generations.live(),workerMemoryExact,&entryBudget);
                std::string error;
                const V center=r.geometryCenter;
                int tx=int(std::floor((17066.6666667-center.y)/533.3333333));
                int ty=int(std::floor((17066.6666667-center.x)/533.3333333));
                std::vector<std::string> tiles;
                for(int y=ty-1;y<=ty+1;++y)for(int x=tx-1;x<=tx+1;++x){
                    char name[512];std::snprintf(name,sizeof name,"%sworld-cache/%s/%d_%d.fg3",root.c_str(),r.map.c_str(),x,y);
                    FILE* f=std::fopen(name,"rb");if(f){std::fclose(f);tiles.emplace_back(name);}
                }
                bool localDeferred=false;NorthlightGeometryMemory::Sample localSample;uint64_t localRequired=0,localLargest=0;
                // Interior checks use inBuildBudget (stage entry already reserved the build headroom).
                NorthlightGI::AllocationAdmission localAdmission=[&](uint64_t bytes){
                    if(bytes<NorthlightGeometryMemory::MiB)return true;
                    const auto budget=geometryBudget(bytes);auto sample=workerMemory(budget);
                    if(NorthlightGeometryMemory::admits(sample,budget))return true;
                    localDeferred=true;localSample=sample;localRequired=budget.available;localLargest=budget.largest;logGeometryMemory("local-geometry-allocation-deferred",sample,generations.live(),workerMemoryExact,&budget);return false;
                };
                auto replacement=std::make_shared<NorthlightGI::BVH>();
                if(!generations.track(replacement))throw std::runtime_error("Geometry generation admission invariant");
                auto buildLocal=[&]{return !tiles.empty()&&localGeometry.build(tiles,root+"world-cache/models",r.map,center-V(288,288,320),center+V(288,288,320),*replacement,error,localAdmission,quality.giFastBVH!=0,{},{},chunkedLocal);};
                bool localBuilt=buildLocal();
                auto noteLocalAllocationFailure=[&]{if(!localBuilt&&!localDeferred&&(error.find("Geometry allocation deferred")!=std::string::npos||error.find("Cannot allocate")!=std::string::npos)){const auto budget=geometryBudget();localSample=workerMemory(budget);localRequired=budget.available;localLargest=budget.largest;localDeferred=true;}};
                noteLocalAllocationFailure();
                if(!localBuilt&&localDeferred&&!chunkedLocal){
                    localGeometry.reset();chunkedLocal=true;localDeferred=false;const auto budget=chunkLift.apply(NorthlightGeometryMemory::chunkBuildBudget(),GetTickCount());
                    const auto retryMemory=workerMemory(budget);if(NorthlightGeometryMemory::admits(retryMemory,budget))localBuilt=buildLocal();
                    else{localDeferred=true;localSample=retryMemory;localRequired=budget.available;localLargest=budget.largest;}
                }
                noteLocalAllocationFailure();
                if(localBuilt)localDeferred=false;
                if(!localBuilt||!replacement->triangleCount()){
                    replacement.reset();
                    if(localDeferred){localGeometry.reset();liftEntry("local-geometry",localRequired,localSample.available,localLargest);stallBegin("local-geometry",localSample,localRequired,localLargest);deferBuild("local-geometry-retry",workerMemory(),1000);continue;}
                    result->message=error.empty()?"No cached geometry for "+r.map:error;publishError();continue;
                }
                const auto localStats=localGeometry.stats();
                if(NorthlightDiagnostics::enabled())logf("WORLD local incremental: reads=%llu built=%llu reused=%llu reusedTriangles=%llu builtTriangles=%llu retainedMiB=%.2f flatMiB=%.2f bvhMiB=%.2f loadMs=%.3f pieceMs=%.3f assembleMs=%.3f bvhMs=%.3f totalMs=%.3f",
                    (unsigned long long)localStats.modelReads,(unsigned long long)localStats.pieceBuilds,(unsigned long long)localStats.pieceReuses,
                    (unsigned long long)localStats.reusedTriangles,(unsigned long long)localStats.builtTriangles,double(localStats.retainedBytes)/1048576,
                    double(localStats.flatBytes)/1048576,double(localStats.bvhBytes)/1048576,localStats.loadMs,localStats.pieceMs,localStats.assembleMs,localStats.bvhMs,localStats.totalMs);
                if(!currentGeometry()){++superseded;abandonGeometry();continue;}
                // The local BVH and its cache are this build's own memory: entry reserved their headroom,
                // so this gate keeps the in-build floor. The entry floor here refused every attempt that
                // entered within one BVH of 768 MiB and rebuilt it each second (0.3.212 player log).
                const auto planBudget=geometryBudget();
                memory=workerMemory(planBudget);if(NorthlightDiagnostics::enabled())logGeometryMemory("after-bvh",memory,generations.live(),workerMemoryExact,&planBudget);
                if(!NorthlightGeometryMemory::admits(memory,planBudget)){
                    replacement.reset();liftEntry("plan",planBudget.available,memory.available,planBudget.largest);stallBegin("plan",memory,planBudget.available,planBudget.largest);deferBuild("plan-deferred",memory,1000,&planBudget);continue;
                }
                // Reduced-reach retry re-enters here with the local BVH (`replacement`, still tracked in
                // `generations`) kept: it does not depend on the terrain stages.
                float reach=0;
                terrainStage:
                auto plan=std::make_shared<NorthlightWorldMesh::WorldMeshUploadPlan>();
                // GI stays local. Load only authored terrain over the complete
                // directional caster volume into a separate GPU shadow source.
                const double localPhaseMs=phaseElapsed();
                bool terrainDeferred=false;NorthlightGeometryMemory::Sample terrainSample;uint64_t terrainLargest=0,terrainRequired=0,terrainRequiredLargest=0;unsigned terrainChecks=0;
                NorthlightGI::AllocationAdmission terrainAdmission=[&](uint64_t bytes){
                    terrainLargest=std::max(terrainLargest,bytes);
                    // Small metadata cannot fragment a large block. Stage entry passed buildBudget (768 MiB);
                    // every large growth inside the stage is checked against inBuildBudget.
                    if(bytes<NorthlightGeometryMemory::MiB)return true;
                    ++terrainChecks;
                    const auto budget=geometryBudget(bytes);auto sample=workerMemory(budget);
                    if(!NorthlightGeometryMemory::admits(sample,budget)&&localGeometry.stats().retainedBytes){
                        // Pieces accelerate the NEXT local build; the finished
                        // BVH owns its scene. Release this optional duplicate
                        // before refusing a terrain/plan/page allocation.
                        const auto cached=localGeometry.stats().retainedBytes;
                        localGeometry.reset();sample=workerMemory(budget);
                        if(NorthlightDiagnostics::enabled())logf("WORLD geometry reclaimed incremental cache estimatedMiB=%.2f availableMiB=%llu stage=shadow-plan",
                             double(cached)/1048576,(unsigned long long)(sample.available>>20));
                    }
                    if(NorthlightGeometryMemory::admits(sample,budget))return true;
                    terrainDeferred=true;terrainSample=sample;terrainRequired=budget.available;terrainRequiredLargest=budget.largest;logGeometryMemory("shadow-terrain-allocation-deferred",sample,generations.live(),workerMemoryExact,&budget);
                    logf("WORLD terrain allocation requestMiB=%.2f requiredContiguousMiB=%.2f",double(bytes)/1048576,double(bytes+NorthlightGeometryMemory::ContiguousMargin)/1048576);
                    return false;
                };
#if NORTHLIGHT_DEV_TUNING
                const uint32_t reachZone=NorthlightRegionalFog::zoneAt(paletteRegion->region,center.x,center.y);
                const float profileReach=NorthlightZoneTuning::terrainRadius(*tuning,shadowRanges.at(r.map,reachZone),r.map,reachZone);
#else
                const float profileReach=shadowRanges.at(r.map,NorthlightRegionalFog::zoneAt(paletteRegion->region,center.x,center.y));
#endif
                const auto reachChoice=NorthlightTerrainReach::choose(reachState,profileReach,GetTickCount(),entryAvailable,entryLargest);
                reach=reachChoice.reach;
#if NORTHLIGHT_DEV_TUNING
                devTerrainReach.store(reach);
#endif
                if(reachChoice.tierChanged)logf("WORLD shadow terrain reach tier from=%.0f to=%.0f reason=headroom availableMiB=%llu largestMiB=%llu",reachChoice.tierFrom,reach,(unsigned long long)(entryAvailable>>20),(unsigned long long)(entryLargest>>20));
                if(reachChoice.restored)logf("WORLD shadow terrain reach restored to=%.0f",reach);
                // Memory refusal of an extended build: rebuild the terrain stages at the base reach now.
                // Interior allocations use inBuildBudget (640 MiB floor, more for requests >128 MiB), so the extended
                // build falls back only when an interior allocation is refused under that floor. terrainAdmission
                // re-samples on every call and the refused attempt's allocations are freed (goto destroys them) first.
                auto reduceReach=[&]{
                    // Restore only once an entry sample covers what this extended attempt lacked.
                    uint64_t restore=0;
                    if(!NorthlightGeometryMemory::add(entryAvailable,terrainRequired>terrainSample.available?terrainRequired-terrainSample.available:0,restore))restore=UINT64_MAX;
                    if(!NorthlightTerrainReach::memoryRefused(reachState,reach,GetTickCount(),restore,terrainRequiredLargest))return false;
                    memoryStall.markReduced();
                    logf("WORLD shadow terrain reach reduced from=%.0f to=%.0f reason=memory",reach,NorthlightTerrainReach::BaseReach);return true;
                };
                const bool extended=reach>NorthlightShadowTerrain::Radius;
                const int tileReach=extended?int(std::ceil(reach/NorthlightRegionalFog::TileSize)):2;
                std::function<bool(V,V)> terrainFilter,terrainChunkFilter;
                if(extended)terrainFilter=[center](V lo,V hi){return NorthlightRegionalShadow::selected(center,lo,hi);};
                if(extended)terrainChunkFilter=[center](V lo,V hi){return NorthlightRegionalShadow::selectedChunk(center,lo,hi);};
                std::vector<std::string> shadowTiles;
                for(int y=ty-tileReach;y<=ty+tileReach;++y)for(int x=tx-tileReach;x<=tx+tileReach;++x){
                    const double zero=NorthlightRegionalFog::WorldZero,size=NorthlightRegionalFog::TileSize;
                    if(extended&&!terrainFilter(V(float(zero-(y+1)*size),float(zero-(x+1)*size),-100000),V(float(zero-y*size),float(zero-x*size),100000)))continue;
                    char name[512];std::snprintf(name,sizeof name,"%sworld-cache/%s/%d_%d.fg3",root.c_str(),r.map.c_str(),x,y);
                    FILE* f=std::fopen(name,"rb");if(f){std::fclose(f);shadowTiles.emplace_back(name);}
                }
                NorthlightGI::WorldScene shadowTerrain;
                { uint32_t reachBits;std::memcpy(&reachBits,&reach,sizeof reachBits);terrainSizeHint.key=reachBits; } // same map (reset above) and same reach only
                const bool terrainLoaded=NorthlightGI::loadInstancedScenes(shadowTiles,root+"world-cache/models",center-V(reach,reach,reach),center+V(reach,reach,reach),shadowTerrain,error,1,terrainAdmission,terrainFilter,terrainChunkFilter,&terrainSizeHint,{},true,chunkedLocal);
                if(terrainLoaded)terrainDeferred=false; // a refusal inside a successful load can only be the optional size-hint reserve (every other refusal fails the load)
                const double terrainLoadMs=phaseElapsed();
                if(!terrainLoaded||!(chunkedLocal?NorthlightShadowTerrain::buildConsuming(replacement->scene(),shadowTerrain,center,*plan,error,terrainAdmission,reach,{},replacement.get()):NorthlightShadowTerrain::build(replacement->scene(),shadowTerrain,center,*plan,error,terrainAdmission,reach,{},true))){
                    if(terrainDeferred){
                        stallBegin("shadow-terrain",terrainSample,terrainRequired,terrainRequiredLargest);
                        if(reduceReach())goto terrainStage;
                        shadowTerrain=NorthlightGI::WorldScene{};plan.reset();replacement.reset();liftEntry("shadow-terrain",terrainRequired,terrainSample.available,terrainRequiredLargest);deferBuild("shadow-terrain-retry",workerMemory(),1000);continue;}
                    result->message=error;publishError();continue;
                }
                const double shadowPlanMs=phaseElapsed();
                if(NorthlightDiagnostics::enabled())logf("WORLD terrain allocation largestMiB=%.2f checks=%u hinted=%u",double(terrainLargest)/1048576,terrainChecks,terrainSizeHint.hinted?1u:0u);
                if(NorthlightDiagnostics::enabled())logf("WORLD shadow terrain: localTriangles=%zu shadowTriangles=%u fixedChunks=%zu reach=%.0f tiles=%zu",replacement->triangleCount(),plan->triangleCount,plan->fixedTerrainChunks.size(),reach,shadowTiles.size());
                if(!currentGeometry()){++superseded;abandonGeometry();continue;}
                shadowTerrain=NorthlightGI::WorldScene{}; // merged shadow plan already owns its data
                auto pages=std::make_shared<NorthlightWorldMeshPages::Plan>();
                // 0.3.216 (issue #66): pages the GPU already holds (same map, keys published at the last commit) are freed inside
                // build() the moment they are finished, so the worker's peak holds only the pages that must be uploaded.
                std::shared_ptr<const NorthlightMeshPageKey::KeySet> resident;
                {std::lock_guard<std::mutex> lock(mutex);if(residentPages&&residentMap==r.map)resident=residentPages;}
                NorthlightWorldMeshPages::Options pagingOptions;pagingOptions.resident=resident.get();
                if(!NorthlightWorldMeshPages::build(*plan,*pages,error,terrainAdmission,pagingOptions)){
                    if(terrainDeferred){
                        stallBegin("mesh-page",terrainSample,terrainRequired,terrainRequiredLargest);
                        if(reduceReach())goto terrainStage;
                        pages.reset();plan.reset();replacement.reset();liftEntry("mesh-page",terrainRequired,terrainSample.available,terrainRequiredLargest);deferBuild("mesh-page-retry",workerMemory(),1000);continue;}
                    result->message=error;publishError();continue;
                }
                NorthlightWorldMeshPages::ElisionStats elided;
                for(const auto& page:pages->pages)if(page.elided){++elided.pages;elided.bytes+=uint64_t(page.key.vertexBytes)+page.key.indexBytes;}
                if(!NorthlightWorldMeshPages::seal(*plan,pages,error,terrainAdmission)){
                    if(terrainDeferred){
                        stallBegin("mesh-page-seal",terrainSample,terrainRequired,terrainRequiredLargest);
                        if(reduceReach())goto terrainStage;
                        pages.reset();plan.reset();replacement.reset();liftEntry("mesh-page-seal",terrainRequired,terrainSample.available,terrainRequiredLargest);deferBuild("mesh-page-seal-retry",workerMemory(),1000);continue;}
                    result->message=error;publishError();continue;
                }
                if(NorthlightDiagnostics::enabled())logf("WORLD mesh pages=%zu batches=%zu originalBatches=%zu vertexMiB=%.2f indexMiB=%.2f maxResourceKiB=512 elidedPages=%zu elidedMiB=%.2f",pages->pages.size(),pages->batches.size(),plan->batches.size(),double(pages->vertexBytes)/1048576,double(pages->indexBytes)/1048576,elided.pages,double(elided.bytes)/1048576);
                memory=workerMemory();if(NorthlightDiagnostics::enabled())logGeometryMemory("after-plan",memory,generations.live(),workerMemoryExact);
                const double pagesMs=phaseElapsed();
                // Worker-side owner map for the render-thread commit. Failure
                // only falls back to the 0.3.136 render-thread construction.
                std::shared_ptr<const PreparedCommit> prepared;
                if(PreparedCommitData){try {auto next=std::make_shared<PreparedCommit>();next->plan=plan.get();next->owners=StaticShadow::makeOwners(ownerPlacements(*plan));
                    next->fixed=std::make_shared<const FixedChunks>(plan->fixedTerrainChunks);prepared=std::move(next);}catch(...){prepared.reset();}}
                built->plan=plan;built->prepared=std::move(prepared);built->bvh=replacement;built->map=r.map;built->center=center;
                float eye[]={center.x,center.y,center.z};
                if(lightCache.loadLights(r.map,eye,520,built->rawLights))for(auto& light:built->rawLights)
                    built->lights.push_back({vec(light.position),vec(light.diffuse)*3.14159265f,light.attenuationStart,light.attenuationEnd});
                if(NorthlightDiagnostics::enabled())logf("GI local lights map=\"%s\" count=%zu (indirect only; authored direct light retained)",r.map.c_str(),built->lights.size());
                const auto& region=paletteRegion->region;
                const NorthlightRegionalFog::ZoneScale* fogScale=nullptr;
#if NORTHLIGHT_DEV_TUNING
                const NorthlightRegionalFog::ZoneScale devFogScale=[&](uint32_t zone,float& day,float& nightExtra){NorthlightZoneTuning::fogScale(*tuning,r.map,zone,day,nightExtra);};
                fogScale=&devFogScale;
#endif
                built->fog=std::make_shared<NorthlightRegionalFog::Field>(NorthlightRegionalFog::buildField(replacement->scene(),region,center.x,center.y,fogScale));
                if(NorthlightDiagnostics::enabled())logf("REGIONAL FOG map=\"%s\" tiles=%zu missing=%u groundCells=%u fogCells=%u airCells=%u indoorCells=%u citySurfaceCells=%u",r.map.c_str(),region.tiles.size(),region.missing,built->fog->groundCells,built->fog->fogCells,built->fog->airCells,built->fog->indoorCells,built->fog->citySurfaceCells);
                const double environmentMs=phaseElapsed();const auto& memoryAfter=workerAdmissionProbe.stats();
                if(NorthlightDiagnostics::enabled())logf("WORLD geometry phases buildId=%llu localMs=%.3f terrainLoadMs=%.3f shadowPlanMs=%.3f pagesMs=%.3f environmentMs=%.3f memoryMs=%.3f memoryRequests=%llu witnessHits=%llu fullScans=%llu regionQueries=%llu memoryTimeIncluded=1",
                    (unsigned long long)r.id,localPhaseMs,terrainLoadMs,shadowPlanMs,pagesMs,environmentMs,workerMemoryMs-memoryMsBefore,
                    (unsigned long long)(memoryAfter.requests-memoryBefore.requests),(unsigned long long)(memoryAfter.witnessHits-memoryBefore.witnessHits),
                    (unsigned long long)(memoryAfter.fullScans-memoryBefore.fullScans),(unsigned long long)(memoryAfter.regionQueries-memoryBefore.regionQueries));
            }
            // END REGION BUILD
            built->id=r.id;built->ms=GetTickCount()-started;built->superseded=superseded;
            geometryBuildEstimateMs.store((3*geometryBuildEstimateMs.load(std::memory_order_relaxed)+unsigned(std::min<DWORD>(built->ms,4000)))/4,std::memory_order_relaxed);
            // Replaces an unadopted older build; it satisfies every earlier worker request.
            // The replaced build (tens of MB) is freed after unlocking, never under the mutex.
            {std::lock_guard<std::mutex> lock(mutex);if(stopping||builderExit)return;
             std::swap(builtGeometry,built);geometryWanted=false;}
#if NORTHLIGHT_DEV_TUNING
            {std::lock_guard<std::mutex> lock(devMutex);devBuilt=tuning;} // what the last published build used
            devBuilds.fetch_add(1,std::memory_order_relaxed);
#endif
            wake.notify_one();built.reset();
            stallEnd(memoryStall.reduced?"reduced":"published");entryLift.clear();chunkLift.clear();
            builtMap=r.map;builtCenter=r.geometryCenter;builtValid=true;wanted=false;superseded=0;
          }
          }catch(const std::bad_alloc&){workerFaultCode.store(1);}
           catch(const std::exception&){workerFaultCode.store(2);}
           catch(...){workerFaultCode.store(3);}
        });
        std::shared_ptr<NorthlightGI::BVH> bvh;std::shared_ptr<NorthlightWorldMesh::WorldMeshUploadPlan> scenePlan;std::shared_ptr<const PreparedCommit> scenePrepared;V sceneCenter;std::shared_ptr<const NorthlightRegionalFog::Field> sceneFog;std::string sceneMap;uint64_t serial=0,sceneBuild=0;
        std::shared_ptr<Snapshot> previousLighting;
        NorthlightGI::ProbeCache probeCache;
        NorthlightGI::ProbeRelightCache relightCache;std::string relightMap;
        NorthlightGI::DynamicProbeLayer dynamicProbes;
        std::vector<NorthlightGI::PointLight> sceneLights;
        std::vector<NorthlightLocalLights::Light> rawSceneLights;
        std::shared_ptr<NorthlightGI::BVH> actors;
        NorthlightWorkerActorMemo<NorthlightActorGeometry::ActorJob> actorMemo;
        uint64_t solvedActorHash=0,completedActorHash=0,completedBaseId=0;
        std::shared_ptr<NorthlightGI::BVH> probeCacheGeometry;
        NorthlightGI::Lighting probeCacheLight;NorthlightGI::PreparedLighting preparedLight;
        NorthlightGI::ProbePublicationCadence publicationCadence;
        std::vector<NorthlightGI::ProbeAtlasEntry> displayFallback;
        uint64_t lightingGeneration=0;unsigned superseded=0,retargeted=0;
        NorthlightGI::MovingSolveStats movingStats;
        // 0.3.153 per geometry swap: passes completed on the previous generation
        // while the builder ran, and time with no usable region (logged at publication).
        unsigned concurrentSolves=0;DWORD stallMs=0,stallStart=0;bool stalled=false;std::string stallMap;
        for(;;){
            Request r;std::unique_ptr<Built> adopted;
            {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return stopping||pending||builtGeometry;});if(stopping)return;r=request;pending=false;workerBusy=true;adopted=std::move(builtGeometry);if(geometryRebuild){geometryRebuild=false;geometryWanted=true;}} /* 0.3.214 (task 42): render thread lost the GPU mesh and the payload */
            buildWake.notify_one(); /* the builder follows the same request stream */
            struct WorkerIdle {std::atomic<bool>& busy;~WorkerIdle(){busy=false;}} idle{workerBusy};
            // No solve runs here: a memory trim or a new map frees the relight slots (1-4 MiB); configure() allocates them again.
            if(giMemoryTrim.exchange(false,std::memory_order_relaxed)||(!relightMap.empty()&&relightMap!=r.map))relightCache.release();
            relightMap=r.map;
            auto result=std::make_shared<Snapshot>();result->map=r.map;result->center=r.camera;
            DWORD started=GetTickCount();result->requestId=r.id;result->requestedAt=r.queuedAt;result->queueMs=started-r.queuedAt;
            bool concurrent=false;
            if(adopted){
                bvh=std::move(adopted->bvh);scenePlan=std::move(adopted->plan);scenePrepared=std::move(adopted->prepared);sceneFog=std::move(adopted->fog);
                sceneLights=std::move(adopted->lights);rawSceneLights=std::move(adopted->rawLights);sceneMap=std::move(adopted->map);sceneCenter=adopted->center;
                sceneBuild=adopted->id;superseded+=adopted->superseded;if(stalled){if(stallMap==sceneMap)stallMs+=GetTickCount()-stallStart;stalled=false;} /* an episode begun on another map is not this region's stall */
            }else if(!bvh||NorthlightWorldStreaming::needsGeometry(sceneMap,sceneCenter,r.map,r.geometryCenter)){
                // The builder is replacing this region. Borrow the published
                // previous generation and keep its probe cache while the original
                // 64-unit GI region holds; the new generation clears it on adoption.
                bool serve;
                {std::lock_guard<std::mutex> lock(mutex);
                 serve=bvh&&sceneMap==r.map&&NorthlightWorldStreaming::within(sceneCenter,r.camera,64)&&published&&published->bvh==bvh;
                 if(!serve){
                     if(published&&published->bvh==bvh&&bvh){
                         previousLighting=std::make_shared<Snapshot>(*published);
                         previousLighting->bvh.reset();previousLighting->meshPlan.reset();
                     }
                     geometryWanted=true;
                 }}
                if(!serve){
                    // Release worker ownership, but leave the last published
                    // complete region available to the renderer until a
                    // replacement arrives or its unchanged coverage ends.
                    // Only the first drop has a region to release; adoption resets the probe state again.
                    if(bvh){
                        probeCache.clear();probeCacheGeometry.reset();
                        dynamicProbes.reset(actors?&actors->scene():nullptr);
                        scenePlan.reset();scenePrepared.reset();sceneFog.reset();bvh.reset();
                    }
                    if(stalled&&stallMap!=r.map)stalled=false; /* the episode began on another map */
                    if(!stalled){stalled=true;stallStart=started;stallMap=r.map;}
                    buildWake.notify_one();continue;
                }
                concurrent=true;
            }
            result->geometryMs=adopted?adopted->ms:GetTickCount()-started;
            // BEGIN GEOMETRY HANDOFF: runs before actor work and GI request-ID
            // cancellation. Tests execute this production block with retargets.
            {std::lock_guard<std::mutex> lock(mutex);
             if(stopping)return;
             if(!NorthlightWorldStreaming::applicable(sceneMap,sceneCenter,request.map,request.camera)){
                 ++superseded;pending=true;continue;
             }
             if(!published||published->bvh!=bvh){
                 auto geometry=std::make_shared<Snapshot>();
                 if(published&&published->map==sceneMap)*geometry=*published;
                 else if(previousLighting&&previousLighting->map==sceneMap)*geometry=*previousLighting;
                 geometry->bvh=bvh;geometry->meshPlan=scenePlan;geometry->prepared=scenePrepared;geometry->retirementBytes=bvh->retainedBytes()+scenePlan->cpuBytes()+sizeof(Snapshot);
                 geometry->map=sceneMap;geometry->center=sceneCenter;geometry->fogField=sceneFog;geometry->localLights=rawSceneLights;
                 published=geometry;previousLighting.reset();
                 if(NorthlightDiagnostics::enabled())logf("WORLD geometry published buildId=%llu latestId=%llu buildMs=%lu cameraLag=%.2f retainedGI=%u refreshDistance=32 coverageDistance=96 concurrentSolves=%u stallMs=%lu",
                     (unsigned long long)sceneBuild,(unsigned long long)request.id,(unsigned long)result->geometryMs,
                     std::sqrt(NorthlightGI::dot(request.camera-sceneCenter,request.camera-sceneCenter)),unsigned(geometry->serial!=0),concurrentSolves,(unsigned long)stallMs);
                 concurrentSolves=0;stallMs=0;
             }
             // The geometry is already drawable up to the existing 96-unit
             // limit. GI still needs its original, stricter 64-unit region.
             // 0.3.169: a region built ahead (lead) that the eye has not reached
             // yet waits for the next request instead of re-running this check.
             // Liveness: waiting needs the lead point within the 32-unit refresh and
             // the eye beyond 64, so a lead of at least 64-32. Its decay or reversal
             // (stop, turn back) moves the lead point by more than the reason-128 step,
             // which issues that next request.
             static_assert(64-NorthlightWorldStreaming::GeometryRefreshDistance>NorthlightWorldStreaming::GeometryLeadMoveStep,"lead waiting liveness");
             if(!NorthlightWorldStreaming::within(sceneCenter,request.camera,64)){
                 pending=NorthlightWorldStreaming::needsGeometry(sceneMap,sceneCenter,request.map,request.geometryCenter);continue;
             }
             if(request.id!=r.id){++retargeted;r=request;pending=false;}
             result->map=sceneMap;result->center=sceneCenter;result->requestId=r.id;result->requestedAt=r.queuedAt;
            }
            // END GEOMETRY HANDOFF
            if(!r.giEnabled)continue; /* GI=0: geometry for shadows only; no actor, probe or atlas work */
            DWORD actorStarted=GetTickCount();
            bool actorsChanged=false;
            if(!actorMemo.matches(r.actorJob)){
              NorthlightActorGeometry::Result resolvedActors;
              if(r.actorJob)resolvedActors=r.actorJob->resolve();
              result->actorTexturesDecoded=resolvedActors.texturesDecoded;result->actorTextureEncodedBytes=resolvedActors.textureEncodedBytes;
              actorsChanged=resolvedActors.hash!=solvedActorHash;
              if(actorsChanged){
                actors.reset();std::string actorError;
                if(resolvedActors.scene&&!resolvedActors.scene->triangles.empty()){
                    auto next=std::make_shared<NorthlightGI::BVH>();auto scene=*resolvedActors.scene;
                    if(next->build(std::move(scene),actorError,quality.giFastBVH!=0))actors=next; /* GIFastBVH=0: 0.3.137 tracer */
                    else logf("GI actor BVH rejected: %s",actorError.c_str());
                }
                solvedActorHash=resolvedActors.hash;
                // Pose/packet change only: keep corrections whose nearby actor
                // bounds are unchanged (0.3.35); the static generation is intact.
                dynamicProbes.observe(actors?&actors->scene():nullptr);
              }
              actorMemo.remember(r.actorJob);
            }
            result->actorMs=GetTickCount()-actorStarted;
            // An unchanged actor packet must not republish/upload an identical
            // 4096-entry probe atlas every capture interval. Only skip after a
            // complete solve for this actor generation has actually published.
            if(r.reason==64&&!actorsChanged&&completedActorHash==solvedActorHash&&completedBaseId==r.baseId&&r.baseId!=0&&probeCacheGeometry==bvh)continue;
            r.light.points=sceneLights;r.light.movingGeometry=nullptr;
            // A probe belongs to a fixed world point and a geometry/lighting
            // generation. Camera motion alone must not recompute that point.
            if(probeCacheGeometry!=bvh||!NorthlightGI::sameStaticLighting(probeCacheLight,r.light)){
                if(probeCacheGeometry!=bvh)relightCache.reset();
                probeCache.clear();probeCacheGeometry=bvh;probeCacheLight=r.light;
                preparedLight=NorthlightGI::prepareLighting(probeCacheLight);
                relightCache.configure(preparedLight,quality.giRays);
                publicationCadence.reset();++lightingGeneration;
                // Keep already displayed same-map GI until replacement probes
                // are ready. This is display fallback only, never solver input.
                displayFallback.clear();
                {std::lock_guard<std::mutex> lock(mutex);
                 if(published&&published->map==r.map)displayFallback=published->atlas;
                 else if(previousLighting&&previousLighting->map==r.map)displayFallback=previousLighting->atlas;}
                dynamicProbes.reset(actors?&actors->scene():nullptr);
            }
            // Geometry was already handed off before actor work. A second
            // request-ID gate here discarded useful work on every camera move.
            result->fogField=sceneFog;result->bvh=bvh;result->meshPlan=scenePlan;result->prepared=scenePrepared;result->retirementBytes=bvh->retainedBytes()+scenePlan->cpuBytes()+sizeof(Snapshot);result->localLights=rawSceneLights;result->origin=NorthlightGI::probeWindowOrigin(r.probeCenter);
            result->lightingGeneration=lightingGeneration;
            DWORD solveStart=GetTickCount();bool obsolete=false,replaced=false;
            // Called with mutex held. A camera-only request may consume this
            // generation's completed world probes, but a new map, distant BVH
            // region or changed lighting must never accept this publication.
            uint64_t checkedRequestId=r.id;bool checkedCompatibility=true;
            auto compatiblePublication=[&](){
                if(probeCacheGeometry!=bvh)return false;
                if(checkedRequestId==request.id)return checkedCompatibility;
                checkedRequestId=request.id;
                auto latestLight=request.light;latestLight.points=sceneLights;latestLight.movingGeometry=nullptr;
                checkedCompatibility=NorthlightGI::staticFallbackCompatible(r.map,sceneMap,request.map,
                    r.camera,sceneCenter,request.camera,probeCacheLight,latestLight);
                return checkedCompatibility;
            };
            // World-keyed transport remains valid across a camera retarget.
            // A different actor packet still cancels its moving correction.
            auto compatibleTransport=[&](){return compatiblePublication()&&request.actorJob==r.actorJob;};
            auto publishProgress=[&](){
                if(!publicationCadence.due(GetTickCount(),result->processedProbes))return;
                {std::lock_guard<std::mutex> lock(mutex);if(stopping||!compatiblePublication())return;}
                // Every export owns its storage. Neither subsequent solves nor
                // cancellation can modify an atlas already seen by rendering.
                auto progress=std::make_shared<Snapshot>(*result);
                progress->atlas=probeCache.atlas();progress->dynamicReused=progress->dynamicSolved=0;
                if(!dynamicProbes.applyCached(progress->atlas,
                    [&](){std::lock_guard<std::mutex> lock(mutex);return stopping||!compatiblePublication();},
                    progress->dynamicReused))return;
                NorthlightGI::retainProbeDisplayFallback(progress->atlas,displayFallback);
                progress->solveMs=GetTickCount()-solveStart;progress->superseded=superseded;
                progress->retargeted=retargeted;progress->partial=true;progress->staticOnly=progress->dynamicReused==0;
                progress->relightStats=relightCache.stats();
                std::lock_guard<std::mutex> lock(mutex);if(stopping||!compatiblePublication())return;
                progress->serial=++serial;published=std::move(progress);publicationCadence.published(GetTickCount());
            };
            auto noteSuperseded=[&](){
                std::lock_guard<std::mutex> lock(mutex);
                if(!stopping&&request.reason==2&&compatiblePublication())++retargeted;else ++superseded;
            };
            const auto order=NorthlightGI::probeSolveOrder(r.probeCenter);
            std::array<NorthlightGI::Probe,8> prefetched;unsigned prefetchedMask=0;
            for(unsigned cursor=0;cursor<order.size();++cursor){
                if(cursor%8==0){
                    // 0.3.153: a finished build also stops a pass on the previous generation.
                    {std::lock_guard<std::mutex> lock(mutex);if(stopping)return;obsolete=request.id!=r.id;replaced=builtGeometry!=nullptr;}
                    if(obsolete||replaced)break;
                    publishProgress();
                    // GIThreads>1: solve this group's uncached probes in parallel. A pure,
                    // per-key-seeded function, consumed below in the original order and cache sequence.
                    if(solvePool)prefetchedMask=NorthlightGI::prefetchProbeGroup(*solvePool,probeCache,result->origin,order,cursor,
                        [&](NorthlightGI::ProbeGridKey k,V q){return relightCache.solve(*bvh,k,q,preparedLight,quality.giRays,NorthlightGI::probeSeed(k));},prefetched);
                }
                V p=NorthlightGI::probeWindowPosition(result->origin,order[cursor]);
                NorthlightGI::ProbeGridKey key;
                if(!NorthlightGI::probeGridKey(p,key))throw std::runtime_error("Invalid world probe coordinate");
                NorthlightGI::Probe probe;
                if(probeCache.get(key,probe))++result->reusedProbes;
                else{probe=prefetchedMask&(1u<<(cursor%8))?prefetched[cursor%8]:relightCache.solve(*bvh,key,p,preparedLight,quality.giRays,NorthlightGI::probeSeed(key));probeCache.put(key,probe);++result->solvedProbes;publicationCadence.solved();}
                ++result->processedProbes;if(probe.valid)++result->validProbes;
            }
            if(obsolete||replaced){
                if(obsolete)noteSuperseded();publishProgress();
                // The pending request selects a new nearest-first window and
                // current actor packet. Its static cache and publication clock
                // survive camera-only retargeting; no completed probe is lost.
                continue;
            }
            result->atlas=probeCache.atlas();movingStats={};
            // Preserve all static world slots across pose/culling changes. The
            // observed actor scene only supplies a local radiance correction.
            if(!dynamicProbes.apply(result->atlas,
                [&](NorthlightGI::ProbeGridKey key,V p,NorthlightGI::StaticPathRecord* paths){
                    if(paths&&actors)return NorthlightGI::solveProbeMoving(*bvh,p,preparedLight,quality.giRays,NorthlightGI::probeSeed(key),*actors,*paths,lightingGeneration,&movingStats);
                    return NorthlightGI::solveProbePrepared(*bvh,p,preparedLight,quality.giRays,NorthlightGI::probeSeed(key),actors.get());},
                [&](){std::lock_guard<std::mutex> lock(mutex);return stopping||!compatibleTransport();},
                result->dynamicReused,result->dynamicSolved)){
                noteSuperseded();result->atlas.clear();publishProgress();continue;
            }
            result->solveMs=GetTickCount()-solveStart;result->superseded=superseded;result->retargeted=retargeted;
            result->movingStats=movingStats;result->pathRecordBytes=dynamicProbes.pathBytes();
            result->relightStats=relightCache.stats();
            bool publishObsolete=false;
            {std::lock_guard<std::mutex> lock(mutex);if(stopping)return;
             if(!compatibleTransport())publishObsolete=true;
             else{result->serial=++serial;published=result;publicationCadence.published(GetTickCount());displayFallback.clear();completedActorHash=solvedActorHash;completedBaseId=r.baseId;concurrentSolves+=concurrent;}}
            if(publishObsolete){noteSuperseded();result->atlas.clear();publishProgress();}
        }
      }catch(const std::bad_alloc&){workerFaultCode.store(1);}
       catch(const std::exception&){workerFaultCode.store(2);}
       catch(...){workerFaultCode.store(3);}
      // The thread has stopped. Do not allocate a diagnostic snapshot in an
      // allocation-failure handler. The render thread reports workerFault().
      workerBusy=false;
    }
    bool check(HRESULT h,const char* s){if(SUCCEEDED(h))return true;if(!failed)logf("WORLD DISABLED: %s HRESULT=%08lx",s,(unsigned long)h);failed=true;return false;}
    void clearMesh(){staticCasters.settle();dropStaticDirtyJobs();vertices=nullptr;indices=nullptr;for(auto& m:materials)drop(m);materials.clear();uploadedTextureBytes=0;uploadedMaterialPixels.reset();materialReuseBytes=0;batches.clear();terrainBatchList.clear();shadowTerrainList.clear();terrainBatchListGeneration=UINT64_MAX;uploadedAlphaCutoffs.clear();uploadedLocalShadowRecords.reset();uploaded.reset();}
    bool target(UINT w,UINT h,D3DFORMAT fmt,IDirect3DTexture9** t,IDirect3DSurface9** s){return check(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,fmt,D3DPOOL_DEFAULT,t,nullptr),"world render texture")&&check((*t)->GetSurfaceLevel(0,s),"world render surface");}
    bool resources(UINT w,UINT h,D3DFORMAT fmt){
        if(width==w&&height==h&&color)return true;
        releaseGPU(true);width=w;height=h; /* 0.3.214 (task 42): the uploaded MANAGED mesh survives a resolution change */
        if(!check(d->CreatePixelShader(kWorldGIShader,&giPS),"world GI shader")||!check(d->CreatePixelShader(kWorldLightingShader,&lightingPS),"world lighting shader")||!check(d->CreatePixelShader(kWorldFogShader,&fogPS),"volume shader")||!check(d->CreatePixelShader(kFogBlurShader,&fogBlurPS),"volume blur shader")||!check(d->CreatePixelShader(kLocalDirectShader,&localDirectPS),"local direct light shader")||!check(d->CreatePixelShader(kLocalDirect16Shader,&localDirect16PS),"local direct 16-light shader")||!check(d->CreatePixelShader(kRemovalSmoothShader,&removalPS),"removal smoothing shader")||!check(d->CreatePixelShader(kTemporalLightShader,&temporalPS),"temporal light shader")||!check(d->CreatePixelShader(kLocalFogShader,&localFogPS),"local fog glow shader")||!check(d->CreatePixelShader(kLocalFogCombineShader,&localFogCombinePS),"local fog combine shader")||!check(d->CreatePixelShader(kWorldNormalsShader,&normalsPS),"world normals shader")||!check(d->CreatePixelShader(kSourceVisibilityPSShader,&sourceVisPS),"source visibility shader")||!check(d->CreatePixelShader(kWorldCompositeShader,&finalPS),"world composite shader")||!check(d->CreatePixelShader(kShadowPSShader,&shadowPS),"shadow shader")||!check(d->CreatePixelShader(kShadowReplayPSShader,&replayPS),"replay shader")||!check(d->CreateVertexShader(kShadowVSShader,&shadowVS),"shadow vertex shader")||!check(d->CreateVertexShader(kShadowCacheVSShader,&cachedShadowVS),"cached shadow vertex shader")||!check(d->CreatePixelShader(kStaticCasterPSShader,&cachedShadowPS),"cached shadow depth shader"))return false;
        if(quality.actorShadows&&quality.actorShadowDetail&&(FAILED(d->CreatePixelShader(kActorShadowShader,&actorShadowPS))||FAILED(d->CreatePixelShader(kActorTilesShader,&actorTilesPS)))){drop(actorShadowPS);drop(actorTilesPS);logf("WORLD actor shadow map disabled: shader creation failed");} /* 0.3.209 (task 29) */
        if(FAILED(d->CreatePixelShader(kFogCloudsShader,&fogCloudsPS))){fogCloudsPS=nullptr;logf("WORLD fog clouds disabled: shader creation failed");} /* 0.3.199 (fog clouds): optional, the rest of the world never depends on it */
        if(FAILED(d->CreatePixelShader(kFogTemporalShader,&fogTemporalPS))){fogTemporalPS=nullptr;logf("WORLD fog temporal disabled: shader creation failed");} /* 0.3.199 (fog temporal): optional as well, the raw fog is the fallback */
        // Optional equivalent variants; unsupported creation retains .95 depth semantics.
        if(FAILED(d->CreatePixelShader(kStaticCasterFastPSShader,&cachedFastPS)))drop(cachedFastPS);
        if(FAILED(d->CreatePixelShader(kStaticCasterOpaqueFastPSShader,&cachedOpaqueFastPS)))drop(cachedOpaqueFastPS);
        if(FAILED(d->CreatePixelShader(kStaticCasterOpaquePSShader,&cachedOpaquePS)))drop(cachedOpaquePS);
        for(int i=0;i<4;++i)if(!target(1024,1024,D3DFMT_R32F,&shadow[i],&shadowSurface[i]))return false;
        if(!check(d->CreateDepthStencilSurface(1024,1024,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&shadowDepth,nullptr),"shadow depth"))return false;
        for(int i=0;i<4;++i)if(!target(ShadowCacheSize,ShadowCacheSize,D3DFMT_R32F,&shadowCache[i],&shadowCacheSurface[i]))return false;
        if(!check(d->CreateDepthStencilSurface(ShadowCacheSize,ShadowCacheSize,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&shadowCacheDepth,nullptr),"shadow cache depth"))return false;
        if(!target(1024,1024,D3DFMT_R32F,&shadowScratch,&shadowScratchSurface))return false;
        // 0.3.209 (task 29): the actor maps, their 64x64 tile minimum and the half-res visibility only with the shaders (ActorShadowDetail=1), and optional:
        // a failure turns the actor shadow map off (0.3.207 shadows) instead of the world renderer. WorldLighting then reads neutralWhite.
        if(actorShadowPS){
            auto optional=[&](UINT tw,UINT th,D3DFORMAT fmt,IDirect3DTexture9** t,IDirect3DSurface9** s){
                if(SUCCEEDED(d->CreateTexture(tw,th,1,D3DUSAGE_RENDERTARGET,fmt,D3DPOOL_DEFAULT,t,nullptr))&&SUCCEEDED((*t)->GetSurfaceLevel(0,s)))return true;
                drop(*t);return false;};
            const UINT tiles=NorthlightActorShadowMap::Size/NorthlightActorShadowMap::TileTexels;
            if(!optional(NorthlightActorShadowMap::Size,NorthlightActorShadowMap::Size,D3DFMT_R32F,&actorShadow[0],&actorShadowSurface[0])||!optional(NorthlightActorShadowMap::Size,NorthlightActorShadowMap::Size,D3DFMT_R32F,&actorShadow[1],&actorShadowSurface[1])
               ||!optional(NorthlightActorShadowMap::Size/NorthlightActorShadowMap::TileStep,NorthlightActorShadowMap::Size/NorthlightActorShadowMap::TileStep,D3DFMT_R32F,&actorTilesMid,&actorTilesMidSurface)||!optional(tiles,tiles,D3DFMT_R32F,&actorTiles[0],&actorTilesSurface[0])||!optional(tiles,tiles,D3DFMT_R32F,&actorTiles[1],&actorTilesSurface[1])||!optional(w/2,h/2,D3DFMT_A8R8G8B8,&actorVisibility,&actorVisibilitySurface)){
                releaseActorTargets();logf("WORLD actor shadow map disabled: render targets unavailable");}
        }
        if(!check(d->CreatePixelShader(kShadowUnionShader,&unionPS),"shadow union shader"))return false;
        invalidateShadowCache();
        {const UINT n=NorthlightGI::probeLayout().atlas;for(int i=0;i<5;++i)if(!check(d->CreateTexture(n*n,i==3?n*6:n,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&probe[i],nullptr),"GI probe texture"))return false;if(!probePrev&&FAILED(d->CreateTexture(n*n*3,n,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&probePrev,nullptr))){probePrev=nullptr;static bool logged=false;if(!logged){logged=true;logf("GI probe blend texture unavailable; same-key probe re-publication switches instantly");}}}
        // AO 1, bloom 0 for TemporalLight/WorldComposite on frames the proxy composites itself.
        if(!check(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&neutralAO,nullptr),"neutral AO texture"))return false;
        {D3DLOCKED_RECT lock={};if(!check(neutralAO->LockRect(0,&lock,nullptr,0),"neutral AO lock"))return false;*static_cast<DWORD*>(lock.pBits)=0xff000000u;neutralAO->UnlockRect(0);}
        if(!check(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&neutralWhite,nullptr),"neutral white texture"))return false;
        {D3DLOCKED_RECT lock={};if(!check(neutralWhite->LockRect(0,&lock,nullptr,0),"neutral white lock"))return false;*static_cast<DWORD*>(lock.pBits)=0xffffffffu;neutralWhite->UnlockRect(0);}
        if(!neutralZero){if(SUCCEEDED(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&neutralZero,nullptr))){ /* 0.3.202 (rain): optional; without it the composite gets no rain mask */
            D3DLOCKED_RECT lock={};if(SUCCEEDED(neutralZero->LockRect(0,&lock,nullptr,0))){*static_cast<DWORD*>(lock.pBits)=0u;neutralZero->UnlockRect(0);}else drop(neutralZero);}}
        if(!ditherNoise){using namespace NorthlightDitherNoise;if(SUCCEEDED(d->CreateTexture(N,N,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&ditherNoise,nullptr))){ /* optional: without it the composite adds neutralZero (no dither) */
            static const std::vector<float> tile=offsets(); /* generated once per process (void and cluster, a few tens of ms) */
            D3DLOCKED_RECT lock={};if(SUCCEEDED(ditherNoise->LockRect(0,&lock,nullptr,0))){
                for(unsigned y=0;y<N;++y){float* row=reinterpret_cast<float*>(static_cast<char*>(lock.pBits)+size_t(y)*lock.Pitch);
                    for(unsigned x=0;x<N;++x){const float v=tile[y*N+x];row[4*x]=row[4*x+1]=row[4*x+2]=v;row[4*x+3]=0;}}
                ditherNoise->UnlockRect(0);}else drop(ditherNoise);}}
        if(!target(w/2,h/2,D3DFMT_A16B16G16R16F,&baselineLight,&baselineSurface)||!target(w/2,h/2,D3DFMT_A16B16G16R16F,&light,&lightSurface)||!target(w/2,h/2,D3DFMT_A16B16G16R16F,&smoothLight,&smoothSurface)||!target(w/2,h/2,D3DFMT_A16B16G16R16F,&fog,&fogSurface)||!target(w/2,h/2,D3DFMT_A16B16G16R16F,&fogBlurred,&fogBlurredSurface)||!target(w/2,h/2,D3DFMT_A16B16G16R16F,&fogHistory,&fogHistorySurface)||!target(w/2,h/2,D3DFMT_A16B16G16R16F,&normalBuffer,&normalSurface)||!target(1,1,D3DFMT_A16B16G16R16F,&sourceVis[0][0],&sourceVisSurface[0][0])||!target(1,1,D3DFMT_A16B16G16R16F,&sourceVis[0][1],&sourceVisSurface[0][1])||!target(1,1,D3DFMT_A16B16G16R16F,&sourceVis[1][0],&sourceVisSurface[1][0])||!target(1,1,D3DFMT_A16B16G16R16F,&sourceVis[1][1],&sourceVisSurface[1][1])||!target(w/2,h/2,D3DFMT_A16B16G16R16F,&temporalLight[0],&temporalLightSurface[0])||!target(w/2,h/2,D3DFMT_A16B16G16R16F,&temporalLight[1],&temporalLightSurface[1])||!target(w/2,h/2,D3DFMT_R32F,&temporalDepth[0],&temporalDepthSurface[0])||!target(w/2,h/2,D3DFMT_R32F,&temporalDepth[1],&temporalDepthSurface[1])||!target(w,h,fmt,&color,&colorSurface))return false;
        if(!target(w/2,h/2,D3DFMT_A16B16G16R16F,&localFogAccum,&localFogAccumSurface))return false; /* 0.3.205 (gh#20): the lamp fog glow accumulator */
        const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
        return check(d->CreateVertexDeclaration(elements,&shadowDecl),"shadow declaration");
    }
    /* 0.3.138: charge only what this snapshot would actually free. Most
       retirements share the BVH/plan with their successor; charging the full
       ~200 MB each time kept the reaper "full" for small items. Accounting
       only: a racing reference drop can undercount, never change content. */
    static size_t snapshotRetireBytes(const Snapshot& s){
        size_t bytes=sizeof(Snapshot)+s.atlas.capacity()*sizeof(NorthlightGI::ProbeAtlasEntry);
        if(s.bvh&&s.bvh.use_count()==1)bytes+=s.bvh->retainedBytes();
        if(s.meshPlan&&s.meshPlan.use_count()==1)bytes+=size_t(s.meshPlan->cpuBytes());
        return bytes;
    }
    static std::vector<StaticShadow::Placement> ownerPlacements(const NorthlightWorldMesh::WorldMeshUploadPlan& plan){
        std::vector<StaticShadow::Placement> owners;owners.reserve(plan.completePlacements.size());
        for(const auto& owner:plan.completePlacements){StaticShadow::Placement p;
            p.uid=owner.uid;p.category=owner.category;p.modelKey=owner.modelKey;
            std::copy(owner.matrix,owner.matrix+9,p.matrix);p.translation=owner.translation;p.low=owner.low;p.high=owner.high;
            owners.push_back(std::move(p));
        }
        return owners;
    }
    void retirePendingCpu(){
        if(!pendingMesh)return;
        // No D3D owner leaves this thread. The packet may be the last CPU
        // geometry owner after a commit/cancel, so release it on the reaper.
        auto& pending=*pendingMesh;
        if(pending.plan&&!pending.textures.empty())retiredMaterials.enqueue(pending.textures,size_t(pending.plan->textureBytes));
        // 0.3.214 (task 42): the payload is the largest CPU object. Its plan reference is gone after a commit or an
        // orphaned plan, so this reference is the last one: free it on the reaper. Otherwise (failed attempt that
        // retries the same plan) only drop this reference.
        if(pending.payload){if(pending.plan&&!pending.plan->payload.live())retirementBacklog.retireOrFree(NorthlightStreaming::cpuRetirement(),pending.payload,size_t(pending.payload->bytes));else pending.payload.reset();}
        if(pending.plan)retirementBacklog.retireOrFree(NorthlightStreaming::cpuRetirement(),pending.plan,pending.plan->cpuBytes());
        if(pending.bvh)retirementBacklog.retireOrFree(NorthlightStreaming::cpuRetirement(),pending.bvh,pending.bvh->retainedBytes());
        // Queue pressure uses synchronous reclamation, preserving the memory
        // reserve; it never retains an unbounded chain of old generations.
    }
    // 0.3.156: a staged upload can only commit into the snapshot it was taken from.
    // Once that snapshot is no longer active it never commits, yet it keeps a geometry
    // generation alive; with an out-of-range published build that fills Generations<BVH,2>.
    template<class Pending,class Active> static bool pendingOrphaned(const Pending* pending,const Active* active){return pending&&(!active||pending->map!=active->map||pending->bvh!=active->bvh);}
    // D3D thread only: the draw hooks (updateWorldContext) and render()/upload(). Not reached
    // while effects are off (F10): recovery then happens on the first frame after re-enabling.
    void releaseOrphanedPending(const char* site){
        if(!pendingOrphaned(pendingMesh.get(),active.get()))return;
        logf("WORLD pending mesh released orphan=%s site=%s map=\"%s\"",!active?"inactive":pendingMesh->map!=active->map?"map":"replaced",site,pendingMesh->map.c_str());
        if(active&&pendingMesh->plan&&pendingMesh->plan!=active->meshPlan)pendingMesh->plan->payload.discard(); /* 0.3.214 (task 42): a superseded plan is never uploaded */
        meshStore.cancel(); /* 0.3.216: claimed buffers return to idle; the written ones already have an invalid key */
        retirePendingCpu();pendingMesh.reset();meshRetry.clear();
    }
    bool expiredMeshPending=false,expiredMeshMapChanged=false;
    // Release only at endFrame, after replay consumers are finished. A hard-expired
    // region cannot draw again; its active page table must not defeat trimIdle().
    // On the same map without memory demand or pressure the pages stay: the next build
    // elides the ones it shares (residentPages) instead of uploading them all again, and
    // its commit trims the rest. A demand or pressure that comes later still releases them.
    void releaseExpiredMesh(){
        if(!expiredMeshPending)return;
        if(active&&active->bvh){expiredMeshPending=expiredMeshMapChanged=false;return;} // a replacement was adopted
        if(!expiredMeshMapChanged&&!memoryPressure&&!geometryMemoryDemand.load(std::memory_order_relaxed))return;
        expiredMeshPending=expiredMeshMapChanged=false;
        const uint64_t bytes=meshStore.poolBytes()+uploadedTextureBytes;
        clearMesh();releaseMeshPool();uploadedMap.clear();
        uploadedStaticOwners.reset();staticOwnerGeneration=UINT64_MAX;
        fixedTerrain.reset();fixedTerrainBits.reset();uploadedTerrain.clear();
        liveTerrainIndexCount=liveDirectionalIndexCount=0;liveTerrainGeneration=0;
        invalidateShadowCache();temporalValid=fogHistoryValid=false;
        workerMemoryTrim.store(true,std::memory_order_relaxed);
        logf("WORLD expired mesh released managedMiB=%.2f",double(bytes)/1048576);
    }
    // 0.3.214 (task 42): the active generation's payload was freed after its commit and the GPU copy is gone
    // (device Reset, resize, recover). Ask the builder for a fresh generation once per bvh (again after 15 s
    // if it never arrived); until it is adopted the world simply has no shadow mesh.
    void requestPayloadRebuild(){
        const auto& w=payloadRebuildAsked;const DWORD now=GetTickCount();
        if(!w.owner_before(active->bvh)&&!active->bvh.owner_before(w)&&DWORD(now-payloadRebuildTick)<15000u)return;
        payloadRebuildAsked=active->bvh;payloadRebuildTick=now;++payloadRebuilds;
        deferLogf("WORLD mesh upload payload was released after commit; GPU mesh lost, rebuilding the generation map=\"%s\" count=%u",active->map.c_str(),payloadRebuilds);
        {std::lock_guard<std::mutex> lock(mutex);geometryRebuild=true;pending=true;}
        wake.notify_one();
    }
    bool streamingFailure(HRESULT hr,const char* stage,const char* detail=""){
        const auto* plan=pendingMesh?pendingMesh->plan.get():(active?active->meshPlan.get():nullptr);
        if(streamingReports++<12)logf("WORLD streaming retry: stage=%s hr=%08lx detail=%s vertexBytes=%llu indexBytes=%llu vertices=%u triangles=%u sourceMatch=%d oldMesh=%d retryMs=1000",stage,(unsigned long)hr,detail,
            (unsigned long long)(plan?plan->vertexBytes:0),(unsigned long long)(plan?plan->indexBytes:0),plan?plan->vertexCount:0,plan?plan->triangleCount:0,
            plan&&plan->pagesSealed,vertices&&indices&&active&&uploadedMap==active->map);
        meshStore.cancel(); /* 0.3.216: as in releaseOrphanedPending; idempotent without an open staging */
        retirePendingCpu();pendingMesh.reset();meshRetry.fail(GetTickCount());
        return vertices&&indices&&active&&uploadedMap==active->map;
    }
    bool uploadRegionalFog(){
        if(uploadedFogField==active->fogField)return true;
        if(!active->fogField){uploadedFogField.reset();return true;}
        constexpr unsigned n=NorthlightRegionalFog::N;
        if(!regionalFogTexture&&!check(d->CreateTexture(n,n,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&regionalFogTexture,nullptr),"regional fog field"))return false;
        D3DLOCKED_RECT lock={};if(!check(regionalFogTexture->LockRect(0,&lock,nullptr,0),"regional fog lock"))return false;
        if(!lock.pBits||lock.Pitch<INT(n*sizeof(NorthlightRegionalFog::Texel))){regionalFogTexture->UnlockRect(0);return check(E_FAIL,"regional fog layout");}
        for(unsigned y=0;y<n;++y)memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,active->fogField->texels.data()+y*n,n*sizeof(NorthlightRegionalFog::Texel));
        if(!check(regionalFogTexture->UnlockRect(0),"regional fog unlock"))return false;
        uploadedFogField=active->fogField;return true;
    }
    bool frameStaging=false,frameProbeUpload=false; /* 0.3.176 (D3): this frame staged world mesh pages / uploaded the probe atlas */
    bool upload(NorthlightStreaming::Budget& streamBudget){
        retiredMaterials.drain(streamBudget);
        if(!uploadRegionalFog())return false;
        static_assert(sizeof(NorthlightGI::WorldVertex)==32,"FGS vertex layout");
        releaseOrphanedPending("upload");
        if(uploaded.lock()!=active->bvh&&!pendingMesh&&streamBudget.available()){
            if(!meshRetry.ready(GetTickCount()))return uploadedMap==active->map&&vertices&&indices;
            if(!active->meshPlan)return streamingFailure(D3DERR_INVALIDCALL,"validate plan","missing upload plan");
            const auto& plan=*active->meshPlan;
            auto payload=plan.payload.share();
            if(!payload){requestPayloadRebuild();return uploadedMap==active->map&&vertices&&indices;} /* 0.3.214 (task 42) */
            uint64_t largestTexture=0;for(const auto& material:payload->materials)largestTexture=std::max(largestTexture,uint64_t(material.bgra.size()));
            const char* invalid=NorthlightWorldMeshPages::validateSealed(plan);
            if(invalid||!payload->pages||payload->pages->pages.empty())return streamingFailure(D3DERR_INVALIDCALL,"validate pages",invalid?invalid:"missing pages");
            // 0.3.216 (issue #66): look every new page up by content key among the store's buffers (active and idle); a hit needs no
            // upload. Admission below covers what this staging can newly allocate: the bytes of the MISSED pages (a hit or a reused
            // idle buffer adds no address space) plus the textures, not the whole mesh. All misses (no hit) is the old full copy.
            std::vector<NorthlightMeshPageKey::Key> pageKeys;pageKeys.reserve(payload->pages->pages.size());
            std::vector<uint8_t> elidedPages;bool anyElided=false;elidedPages.reserve(payload->pages->pages.size());
            for(const auto& page:payload->pages->pages){pageKeys.push_back(page.key);elidedPages.push_back(page.elided);anyElided|=page.elided;}
            if(!meshStore.beginStaging(pageKeys,anyElided?elidedPages:std::vector<uint8_t>())){
                if(!meshStore.elidedMisses())return streamingFailure(D3DERR_INVALIDCALL,"page staging","staging already open");
                // An elided page has no CPU data and its buffer is gone (trimmed, replaced, device reset): this plan cannot be
                // staged. Nothing was claimed. Publish no keys (the rebuilt plan elides nothing, so this cannot repeat) and drop the
                // payload: the next frame asks the worker for a fresh generation through the payload-rebuild path.
                if(elidedFallbacks++<12)logf("WORLD elided page missed at staging: misses=%zu of %zu pages, rebuilding the generation without elision (fallback %u)",meshStore.elidedMisses(),pageKeys.size(),elidedFallbacks);
                {std::lock_guard<std::mutex> lock(mutex);residentPages.reset();residentMap.clear();}
                active->meshPlan->payload.discard();
                return uploadedMap==active->map&&vertices&&indices;
            }
            syncMeshBuffers();
            const auto& staged=meshStore.staging();
            uint64_t missVertexBytes=0,missIndexBytes=0;for(uint32_t page:staged.upload){missVertexBytes+=pageKeys[page].vertexBytes;missIndexBytes+=pageKeys[page].indexBytes;}
            auto budget=NorthlightGeometryMemory::uploadBudget(missVertexBytes,missIndexBytes,plan.textureBytes,largestTexture);
            budget.largest=2*std::max<uint64_t>(staged.upload.empty()?0:NorthlightWorldMeshPages::PageVertexByteLimit,largestTexture)+NorthlightGeometryMemory::ChunkMargin;
            NorthlightGeometryMemory::Sample memory;bool exact=true;
            {auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Admission);memory=geometryAdmission(budget,&exact);}
            if(NorthlightDiagnostics::enabled())logGeometryMemory("before-upload",memory,0,exact,&budget);
            if(!NorthlightGeometryMemory::admits(memory,budget))return streamingFailure(E_OUTOFMEMORY,"upload-admission","address-space reserve or contiguous block");
            pendingMesh=std::make_unique<PendingMesh>();pendingMesh->bvh=active->bvh;
            pendingMesh->plan=active->meshPlan;pendingMesh->prepared=active->prepared;pendingMesh->map=active->map;pendingMesh->payload=payload;
            auto& next=*pendingMesh;next.textures.reserve(payload->materials.size());
            // The upload cursor runs over the miss list only; its page index maps to a payload page and a claimed buffer in perform().
            std::vector<NorthlightWorldMeshPages::PageBytes> sizes;sizes.reserve(staged.upload.size());
            for(uint32_t pageIndex:staged.upload){const auto& page=payload->pages->pages[pageIndex];sizes.push_back({page.vertices.size()*sizeof(NorthlightGI::WorldVertex),page.indices.size()*sizeof(uint32_t)});}
            if(!next.upload.begin(std::move(sizes)))return streamingFailure(D3DERR_INVALIDCALL,"page sizes");
        }
        if(pendingMesh){frameStaging=true; /* 0.3.176 (D3) */
            auto& next=*pendingMesh;auto& plan=*next.plan;const auto& payload=*next.payload;
            ++next.frames;
            // Each bounded driver operation yields back to the shared deadline.
            struct PageBudget {
                NorthlightStreaming::Budget& parent;
                bool available()const{return parent.available()&&parent.elapsedMs()<.5;}
                size_t chunk(size_t n)const{return available()?parent.chunk(n):0;}
                void consume(size_t n){parent.consume(n);}
            } pageBudget{streamBudget};
            NorthlightGeometryMemory::Sample pageMemory;bool sampled=false;
            auto perform=[&](NorthlightWorldMeshPages::Step step,size_t cursor,size_t offset,size_t bytes){
                using S=NorthlightWorldMeshPages::Step;
                const auto& staging=meshStore.staging();
                if(!staging.open||cursor>=staging.upload.size())return false;
                const uint32_t id=staging.uploadBuffer[cursor];
                if(!meshStore.markWriting(id)||id>=meshBuffers.size())return false; // never an active (drawn) buffer; the old key dies before the first write
                auto& page=meshBuffers[id];const auto& cpu=payload.pages->pages[staging.upload[cursor]];
                if(cpu.elided)return false; // an elided page is always a store hit and never in the upload list
                const auto& entry=meshStore.entries()[id];
                if(step==S::CreateVertices||step==S::CreateIndices){
                    const bool vb=step==S::CreateVertices;
                    const bool reuse=vb?page.vb&&entry.vbCapacity>=bytes:page.ib&&entry.ibCapacity>=bytes;
                    if(reuse)return true;
                    if(!sampled){
                        auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Admission);
                        // Full address-space admission already ran at generation
                        // start. Bounded <=512 KiB growth needs a fresh aggregate
                        // reserve check, not another VirtualQuery walk per page.
                        MEMORYSTATUSEX status={};status.dwLength=sizeof status;
                        pageMemory.valid=GlobalMemoryStatusEx(&status)!=FALSE;
                        pageMemory.available=NorthlightGeometryMemory::corrected(status.ullAvailVirtual);sampled=true;
                    }
                    auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Buffers);
                    return acquireMeshPage(id,vb,UINT(bytes),pageMemory);
                }
                if(step==S::PreloadVertices||step==S::PreloadIndices){
                    auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Preload);
                    if(step==S::PreloadVertices)page.vb->PreLoad();else page.ib->PreLoad();return true;
                }
                auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Copies);
                const bool vb=step==S::CopyVertices;void* data=nullptr;
                HRESULT hr=vb?page.vb->Lock(UINT(offset),UINT(bytes),&data,0):page.ib->Lock(UINT(offset),UINT(bytes),&data,0);
                if(FAILED(hr))return false;
                if(data)std::memcpy(data,(vb?reinterpret_cast<const char*>(cpu.vertices.data()):reinterpret_cast<const char*>(cpu.indices.data()))+offset,bytes);
                hr=vb?page.vb->Unlock():page.ib->Unlock();return data&&SUCCEEDED(hr);
            };
            while(pageBudget.available()&&next.upload.status()==NorthlightWorldMeshPages::Result::Pending){
                if(next.upload.advance(perform,pageBudget)==NorthlightWorldMeshPages::Result::Failed)return streamingFailure(E_FAIL,"mesh page upload");
            }
            size_t budget=streamBudget.remainingBytes();
            auto canStage=[&]{return pageBudget.available()&&next.upload.status()==NorthlightWorldMeshPages::Result::Ready;};
            {auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Materials);
            unsigned count=0;
            while(canStage()&&budget&&next.material<payload.materials.size()&&count<16){
                const auto& m=payload.materials[next.material];
                const bool white=m.width==1&&m.height==1&&m.bgra.size()==4&&
                    m.bgra[0]==255&&m.bgra[1]==255&&m.bgra[2]==255&&m.bgra[3]==255;
                if(white&&next.white){next.white->AddRef();next.textures.push_back(next.white);++next.material;++count;continue;}
                if(!next.partialTexture){
                    if(auto* reused=reusableMaterial(m)){
                        reused->AddRef();next.textures.push_back(reused);++next.material;++count;++next.textureReuses;
                        if(white){next.white=reused;reused->AddRef();}
                        continue;
                    }
                    HRESULT hr=d->CreateTexture(m.width,m.height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&next.partialTexture,nullptr);
                    if(FAILED(hr)||!next.partialTexture)return streamingFailure(FAILED(hr)?hr:E_FAIL,"staged material");
                    next.materialRow=0;
                }
                if(!canStage())break;
                const size_t rowBytes=size_t(m.width)*4;
                if(rowBytes>budget)break;
                const UINT rows=UINT(std::min({size_t(m.height)-next.materialRow,budget/rowBytes,size_t(64)}));
                RECT rect={0,LONG(next.materialRow),LONG(m.width),LONG(next.materialRow+rows)};
                D3DLOCKED_RECT lock={};HRESULT hr=next.partialTexture->LockRect(0,&lock,&rect,0);
                if(FAILED(hr))return streamingFailure(hr,"staged material lock");
                if(!lock.pBits||lock.Pitch<INT(rowBytes)){next.partialTexture->UnlockRect(0);return streamingFailure(E_FAIL,"staged material layout");}
                for(UINT y=0;y<rows;++y)memcpy((char*)lock.pBits+y*lock.Pitch,m.bgra.data()+(next.materialRow+y)*rowBytes,rowBytes);
                hr=next.partialTexture->UnlockRect(0);if(FAILED(hr))return streamingFailure(hr,"staged material unlock");
                next.materialRow+=rows;budget-=rows*rowBytes;streamBudget.consume(rows*rowBytes);
                if(next.materialRow==m.height){
                    auto* t=next.partialTexture;t->PreLoad();next.partialTexture=nullptr;next.textures.push_back(t);++next.material;++count;++next.textureUploads;
                    if(white){next.white=t;t->AddRef();}
                }
            }
            }
            if(streamBudget.available()&&next.upload.status()==NorthlightWorldMeshPages::Result::Ready&&next.material==payload.materials.size()){
                auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Commit);
                // Allocate small commit metadata before releasing the previous GPU set.
                std::vector<float> cutoffs;cutoffs.reserve(payload.materials.size());
                for(const auto& material:payload.materials)cutoffs.push_back(material.alphaCutoff);
                std::vector<Batch> committedBatches=payload.pages->batches;
                std::string committedMap=next.map;
                const bool prepared=next.prepared&&next.prepared->plan==next.plan.get()&&next.prepared->owners&&next.prepared->fixed;preparedOwnerCommits+=prepared;
                auto committedFixed=prepared?next.prepared->fixed:std::make_shared<const FixedChunks>(plan.fixedTerrainChunks);
                auto committedOwners=prepared?next.prepared->owners:StaticShadow::makeOwners(ownerPlacements(plan));
                // Retain only a bounded subset of immutable texels, by moving
                // existing upload storage (no second full BGRA allocation).
                auto committedPixels=std::make_shared<MaterialPixels>(payload.materials.size());
                size_t retainedPixels=0;
                // Keep the previous complete geometry drawable until publication fits.
                if(uploadedTextureBytes<=NorthlightStreaming::DeferredRelease<IDirect3DTexture9>::MaxBytes&&
                   !retiredMaterials.enqueue(materials,uploadedTextureBytes))return vertices&&indices&&uploadedMap==active->map;
                size_t elidedAtCommit=0;for(const auto& page:payload.pages->pages)elidedAtCommit+=page.elided;
                for(size_t i=0;i<next.payload->materials.size();++i){
                    auto& material=next.payload->materials[i];
                    if(material.bgra.capacity()>MaterialReuseBytes-retainedPixels)continue;
                    retainedPixels+=material.bgra.capacity();(*committedPixels)[i]=std::move(material);
                }
                auto previousPixels=std::move(uploadedMaterialPixels);
                const size_t previousPixelBytes=materialReuseBytes;
                const bool coldStart=meshStore.table().empty();clearMesh();const auto stagedPages=meshStore.commit(); /* 0.3.216: atomic switch of the active page table; replaced buffers go idle */
                {const uint32_t first=meshStore.bufferOf(0);vertices=first<meshBuffers.size()?meshBuffers[first].vb:nullptr;indices=first<meshBuffers.size()?meshBuffers[first].ib:nullptr;}
                materials=std::move(next.textures);uploadedTextureBytes=size_t(plan.textureBytes);vertexCount=plan.vertexCount;
                uploadedMaterialPixels=std::move(committedPixels);materialReuseBytes=retainedPixels;
                if(previousPixels&&!NorthlightStreaming::cpuRetirement().retire(previousPixels,previousPixelBytes+previousPixels->capacity()*sizeof(NorthlightWorldMesh::MaterialUpload)))previousPixels.reset();
                uploadedLocalShadowRecords=plan.localShadowRecords;
                batches=std::move(committedBatches);uploadedAlphaCutoffs=std::move(cutoffs);
                // Replaced shared sets may be last owners (~7000 + ~3000 nodes).
                {auto& reaper=NorthlightStreaming::cpuRetirement();
                 std::swap(fixedTerrain,committedFixed);std::swap(uploadedStaticOwners,committedOwners);fixedTerrainBits.assign(fixedTerrain.get());
                 if(committedFixed)retirementBacklog.retire(reaper,committedFixed,committedFixed->size()*48);
                 if(committedOwners)retirementBacklog.retire(reaper,committedOwners,committedOwners->placements*(sizeof(StaticShadow::Placement)+64));}
                uploaded=next.bvh;uploadedMap=std::move(committedMap);
                publishResidentPages(uploadedMap);
                ++meshGeneration;rebuildTerrainLists();
                // 0.3.214 (task 42): the GPU copy is MANAGED and complete; drop the plan's CPU payload (pages, cutout
                // textures). The pending packet's reference is then the last and is freed on the reaper below.
                const uint64_t payloadBytes=plan.payload.discard();const uint64_t releasedBytes=payloadBytes>retainedPixels?payloadBytes-retainedPixels:0;
                if(NorthlightDiagnostics::enabled())logf("WORLD staged mesh committed: vertices=%u triangles=%zu materials=%zu textureUploads=%u textureReuses=%u retainedMaterialMiB=%.2f stagingFrames=%u stagingMs=%lu releasedPayloadMiB=%.2f pages=%zu reusedPages=%zu uploadedPages=%zu reusedMiB=%.2f uploadedMiB=%.2f elidedPages=%zu",vertexCount,size_t(plan.triangleCount),materials.size(),next.textureUploads,next.textureReuses,double(materialReuseBytes)/1048576,next.frames,(unsigned long)(GetTickCount()-next.started),double(releasedBytes)/1048576,
                    stagedPages.pages,stagedPages.reusedPages,stagedPages.uploadedPages,double(stagedPages.reusedBytes)/1048576,double(stagedPages.uploadedBytes)/1048576,elidedAtCommit);
                retirePendingCpu();pendingMesh.reset();meshRetry.clear();
                trimMeshPoolAfterCommit(coldStart?0:stagedPages.uploadedCapacity); /* a cold start held no buffer idle */
            }
        }
        if(!vertices||!indices||uploadedMap!=active->map)return false;
        // Geometry can be ready before a new map's first GI solve. Shadows
        // may render immediately; GridInfo.w prevents reading the old atlas.
        if(active->serial==0)uploadedSerial=0;
        if(uploadedSerial!=active->serial){
            auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Probes);frameProbeUpload=true; /* 0.3.176 (D3) */
            if(active->atlas.size()!=NorthlightGI::probeLayout().atlasSize())return false;
            probeActivation.begin(active->map);
            float activationNow=float(DWORD(GetTickCount()-animationEpoch))*.001f;
            probeBlend.begin(active->map);const unsigned blendedSlots=probePrev?probeBlend.publish(active->atlas,activationNow):0; // 0.3.197
            const unsigned n=NorthlightGI::probeLayout().atlas;
            for(int channel=0;channel<5;++channel){D3DLOCKED_RECT lock;if(!check(probe[channel]->LockRect(0,&lock,nullptr,0),"probe upload"))return false;
                NorthlightProbeUpload::write(unsigned(channel),lock.pBits,size_t(lock.Pitch),n,active->atlas,probeBlend,probeActivation,activationNow,probePrev!=nullptr,true); // Locked D3D storage cannot alias the CPU atlas.
                probe[channel]->UnlockRect(0);
            }
            if(probePrev){D3DLOCKED_RECT lock; /* 0.3.197: what was on screen before each slot's latest SH, R|G|B in three horizontal thirds */
                if(!check(probePrev->LockRect(0,&lock,nullptr,0),"probe blend upload"))return false;
                NorthlightProbeUpload::previous(lock.pBits,size_t(lock.Pitch),n,probeBlend);
                probePrev->UnlockRect(0);}
            ++probeBlendPublishes;probeBlendSlots+=blendedSlots; /* 0.3.197: logged with LOCAL direct (the upload span has no log line) */
            uploadedSerial=active->serial;
        }return true;
    }
    // 0.3.176 (D1): this frame's terrain change (sample frames), per-triangle tests, and (RenderProfile
    // sample frames) the arena / index build / lock+copy times; -1: not measured.
    const char* terrainChange="none";size_t terrainTriangleTests=0;double terrainPartMs[3]={-1,-1,-1};
    static const char* classifyTerrain(const std::vector<TerrainSnapshot>& now,const std::vector<TerrainSnapshot>& before,bool reused){
        if(reused)return "none";if(now==before)return "identity"; /* the same owners in order: a generation or buffer change */
        if(now.size()!=before.size())return "membership";
        try{std::vector<const void*> a,b;for(const auto& s:now)a.push_back(s.get());for(const auto& s:before)b.push_back(s.get());
            std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());return a==b?"order":"membership";}catch(...){return "unknown";}
    }
    // 0.3.192 (DXVK3): (re)creates the live IB at the given ring capacity; on failure no buffer and no ring.
    bool recreateLiveIndices(UINT capacity){
        auto& queries=fence();
        drop(liveIndicesGPU);liveIndexBytes=0;NorthlightDynamicRing::reset(liveIndexRing,0,queries);liveIndexBase=0;
        if(!check(d->CreateIndexBuffer(capacity,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&liveIndicesGPU,nullptr),"live index buffer"))return false;
        liveIndexBytes=capacity;NorthlightDynamicRing::reset(liveIndexRing,capacity,queries);return true;
    }
    bool uploadLiveTerrain(){
        terrainUploadBytes=0;terrainUploadReused=false;terrainTriangleTests=0;for(auto& ms:terrainPartMs)ms=-1;
        liveTerrainGPU.beginFrame();
        const bool timedParts=profileSampled();auto partStart=timedParts?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        auto part=[&](int k){if(!timedParts)return;const auto now=std::chrono::steady_clock::now();terrainPartMs[k]=std::chrono::duration<double,std::milli>(now-partStart).count();partStart=now;};
        const bool reuse=liveTerrainGeneration==meshGeneration&&frameTerrain==uploadedTerrain && (frameTerrain.empty()||(liveTerrainGPU.vertices()&&liveIndicesGPU));
        if(captureSampled)terrainChange=classifyTerrain(frameTerrain,uploadedTerrain,reuse);
        // 0.3.192 (DXVK3): a reuse frame draws the live IB slice again without a place(): re-tag its span with this frame's
        // fence so it stays pending (no NOOVERWRITE overwrite of a slice still drawn) until this frame's draws are submitted.
        // The newest span is the current lists (place() appends, the older spans are earlier lists); liveIndexBase is untouched.
        if(reuse){terrainUploadReused=true;if(liveTerrainIndexCount||liveDirectionalIndexCount)NorthlightDynamicRing::touch(liveIndexRing,fence());return true;}
        NorthlightGeometryMemory::Sample arenaMemory;bool arenaSampled=false;
        std::optional<NorthlightStreaming::PhaseProfile::Scope> arenaPhase;arenaPhase.emplace(streamingPhases.peaks[NorthlightStreaming::PhaseProfile::TerrainArena]);
        const HRESULT terrainResult=liveTerrainGPU.update(d,frameTerrain,
            [&](size_t bytes){
                if(bytes>8*NorthlightGeometryMemory::MiB){
                    if(memoryPressureUntil&&GetTickCount()<memoryPressureUntil)return false;
                    arenaMemory=geometryAdmission(NorthlightGeometryMemory::dynamicBudget(bytes));arenaSampled=true;
                    // Refusing optional retention capacity must not suppress
                    // the original 8 MiB active-set allocation for one second.
                    return NorthlightGeometryMemory::admits(arenaMemory,NorthlightGeometryMemory::dynamicBudget(bytes));
                }
                return admitsGrowth("live-terrain-arena",bytes,arenaSampled?&arenaMemory:nullptr);
            },
            [](const NorthlightTerrainCapture::Position& p){NorthlightGI::WorldVertex v;v.position=V(p.x,p.y,p.z);return v;});
        arenaPhase.reset();part(0);
        if(terrainResult==S_FALSE){
            // Preserve the existing memory-pressure fallback: the complete
            // cached terrain covers this frame; do not advertise absent live chunks.
            liveTerrainIndexCount=liveDirectionalIndexCount=0;liveIndexBase=0;liveTerrainChunks.clear();uploadedTerrain.clear();return true;
        }
        if(!check(terrainResult,"live terrain arena upload"))return false;
        std::optional<NorthlightStreaming::PhaseProfile::Scope> indexPhase;indexPhase.emplace(streamingPhases.peaks[NorthlightStreaming::PhaseProfile::TerrainIndices]);
        // 0.3.176 (U1a/U1c): the totals first, then the lists straight into the locked IB (the bytes and
        // order of the 0.3.175 concatenation); fixed-chunk membership from the bitmap beside the set.
        const auto& fixed=fixedTerrainChunks();const bool bits=fixedTerrainBits.source()==&fixed;
        if(!liveTerrainGPU.prepare(frameTerrain,meshGeneration,[&](const auto& snapshot,uint32_t offset,std::vector<uint32_t>& output){
            NorthlightShadowTerrain::appendLiveDirectionalBy(snapshot,[&](std::pair<int,int> c){return bits?fixedTerrainBits.contains(c):fixed.count(c)!=0;},offset,output,captureSampled?&terrainTriangleTests:nullptr);
        },liveTerrainIndexCount,liveDirectionalIndexCount))return check(E_FAIL,"live terrain arena membership");
        indexPhase.reset();part(1);
        if(!liveTerrainIndexCount){uploadedTerrain=frameTerrain;liveTerrainGeneration=meshGeneration;return true;}
        auto indexUpload=streamingPhases.measure(NorthlightStreaming::PhaseProfile::TerrainIndexUpload);
        UINT ib=UINT((liveTerrainIndexCount+liveDirectionalIndexCount)*sizeof(uint32_t));
        if(ib>liveIndexBytes){
            const UINT ibCapacity=NorthlightDynamicRing::ringCapacity(ib);
            if(!admitsGrowth("live-terrain-index-growth",ibCapacity)){
                liveTerrainIndexCount=liveDirectionalIndexCount=0;liveIndexBase=0;liveTerrainChunks.clear();uploadedTerrain.clear();return true;
            }
            if(!recreateLiveIndices(ibCapacity))return false;
        }
        // 0.3.192 (DXVK3): the lists are appended to a fence-checked ring (dynamic_ring.h): no DISCARD, so no
        // full-buffer charge on DXVK 3.x, while an earlier frame's draws may still read their slice.
        auto& queries=fence();
        auto slot=NorthlightDynamicRing::place(liveIndexRing,ib,4,queries);
        if(slot.discarded&&NorthlightDynamicRing::oversized(liveIndexRing,liveIndexRing.peak)){
            if(!recreateLiveIndices(NorthlightDynamicRing::ringCapacity(liveIndexRing.peak)))return false;
            slot=NorthlightDynamicRing::place(liveIndexRing,ib,4,queries);NorthlightLockMeter::ringShrink();
        }
        if(slot.wrapped)NorthlightLockMeter::ringWrap(slot.fenceReuse,slot.discarded);
        if(slot.discarded)NorthlightLockMeter::discard(NorthlightLockMeter::LiveIB,liveIndexBytes);
        void* data=nullptr;
        if(!check(liveIndicesGPU->Lock(slot.offset,ib,&data,slot.flags),"live index upload"))return false;
        liveTerrainGPU.write(static_cast<uint32_t*>(data));
        if(!check(liveIndicesGPU->Unlock(),"live index unlock"))return false;
        liveIndexBase=slot.offset/sizeof(uint32_t);
        part(2);
        uploadedTerrain=frameTerrain;liveTerrainGeneration=meshGeneration;terrainUploadBytes=liveTerrainGPU.uploadedBytes+ib;
        return true;
    }
    // Win32 wide-path read (non-ASCII game folders); absent file -> false.
    static bool readSmallFile(const std::wstring& path,std::string& out){
        HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(f==INVALID_HANDLE_VALUE)return false;
        char buffer[4096];DWORD got=0;bool ok=true;out.clear();
        while((ok=ReadFile(f,buffer,sizeof buffer,&got,nullptr)!=FALSE)&&got&&out.size()<262144)out.append(buffer,got);
        CloseHandle(f);return ok;
    }
    void loadQuality(){
        std::string own,old;std::vector<std::string> problems;
        const bool hasOwn=readSmallFile(std::wstring(rootPath)+L"northlight-quality.ini",own),hasOld=readSmallFile(std::wstring(rootPath)+L"shadow-experiment.ini",old);
        try{std::istringstream a(NorthlightQuality::narrow(own)),b(NorthlightQuality::narrow(old));quality=NorthlightQuality::load(hasOwn?&a:nullptr,hasOld?&b:nullptr,problems);}
        catch(...){quality=NorthlightQuality::Settings{};problems.assign(1,"allocation failure; full-quality defaults");}
        if(NorthlightQuality::bigEndianUtf16(own)||NorthlightQuality::bigEndianUtf16(old))problems.push_back("UTF-16 big-endian file not supported; save as UTF-8 or ANSI");
        logf("QUALITY %s file=%s legacyShadowExperiment=%s problems=%zu",NorthlightQuality::describe(quality).c_str(),hasOwn?"northlight-quality.ini":"absent",hasOld?"read":"absent",problems.size());
        for(size_t i=0;i<problems.size()&&i<32;++i)logf("QUALITY warning %s",problems[i].c_str());
        // 0.3.158: ActorShadows=0 forces the replay-derived keys off before anything reads them.
        const std::string forced=NorthlightQuality::forcedOff(quality);quality=NorthlightQuality::effective(quality);
        logf("QUALITY ActorShadows=%u: %s; forced off: %s",quality.actorShadows,quality.actorShadows?"actor and static shadows":"static shadows only",forced.c_str());
        logf("QUALITY ActorShadowDetail=%u: %s",quality.actorShadowDetail,NorthlightQuality::actorShadowMap(quality,true)?"actor shadow map and soft filter":"actor shadows in the near cascade"); /* 0.3.209 (task 29) */
        NorthlightDiagnostics::configure(quality.diagnostics!=0);
        NorthlightWeatherMemory::configure(quality.weatherMemory!=0); /* WeatherMemory=0: no weather reads, also not in the stream snapshot */
        NorthlightMemorySwitches::configure(quality.memMap!=0); /* task 42: MemMap (also read earlier by the process-level MEMMAP points) */
        NorthlightRenderThreadProbe::configure(NorthlightQuality::renderProfile(quality),NorthlightQuality::replayProbe(quality));
        NorthlightWorldContext::SelfReadStats::timed.store(NorthlightQuality::renderProfile(quality),std::memory_order_relaxed); /* 0.3.160 */
        // 0.3.153 GIDistance: the probe layout is fixed by the first renderer of the process.
        static const bool layoutConfigured=NorthlightGI::configureProbeLayout(NorthlightGI::probeLayoutFor(NorthlightQuality::giProbeGrid(quality)));
        const auto& layout=NorthlightGI::probeLayout();
        logf("QUALITY GI distance=%u effective=%u grid=%ux%ux%u atlas=%u^3 cacheProbes=%zu configured=%u probeAhead=%u",quality.giDistance,layout.n*4-4,layout.n,layout.n,layout.nz,layout.atlas,layout.atlasSize(),unsigned(layoutConfigured),quality.giProbeAhead);
    }
    HRESULT quad(UINT w,UINT h){struct Q{float x,y,z,rhw,u,v;};Q q[]={{-.5f,-.5f,0,1,0,0},{float(w)-.5f,-.5f,0,1,1,0},{-.5f,float(h)-.5f,0,1,0,1},{float(w)-.5f,float(h)-.5f,0,1,1,1}};D3DVIEWPORT9 vp={0,0,w,h,0,1};d->SetViewport(&vp);return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Q));}
public:
    // Buffer identity for the snapshot cache: a private-data token created on
    // first sight and owned by the COM object, so pointer reuse cannot alias.
    static std::uint64_t bufferIdentity(void* buffer,bool indexBuffer){
        return NorthlightTrackedBuffers::identity(buffer,indexBuffer);
    }
    void setConstantEpochSource(NorthlightReplayCaptureConstants::ClockSource<DeviceMirror> source){
        // A different provider can have the same numeric serials.
        for(auto& replay:replays)replay->constantStamp={};
        constantEpochSource=source;
    }
    explicit WorldRenderer(IDirect3DDevice9* device):d(device),stateBlocks(device){terrainBoundsCache.setIdentityProvider(&bufferIdentity);terrainBoundsCache.setVersionProvider(&NorthlightTrackedBuffers::version);terrainBoundsCache.setMetadataProvider(&NorthlightTrackedBuffers::captureMetadata);terrainBoundsCache.setDeclarationCache(&declarationCache);replaySnapshots.setDeclarationProvider(&declarationCache,[](void* c,IDirect3DVertexDeclaration9* decl,const D3DVERTEXELEMENT9*& e,UINT& n){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->get(decl,e,n);});replaySnapshots.setLayoutProvider(&declarationCache,[](void* c,IDirect3DVertexDeclaration9* decl,UINT* extent){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->captureLayout(decl,extent);});replaySnapshots.setIdentityProvider(&bufferIdentity);replaySnapshots.setVersionProvider(&NorthlightTrackedBuffers::version);replaySnapshots.setMetadataProvider(&NorthlightTrackedBuffers::captureMetadata);QueryPerformanceFrequency(&captureFrequency);char path[MAX_PATH*3];WideCharToMultiByte(CP_UTF8,0,rootPath,-1,path,sizeof path,nullptr,nullptr);root=path;std::string paletteError;
        loadQuality();effects.gi=NorthlightQuality::giPass(quality,effects.gi);
        staticPrebuild.steps=float(quality.shadowDirectionSteps); /* prebuild targets the same direction lattice */
        minSkinnedShadowTriangles=quality.minSkinnedTriangles;captureBudgetMiB=quality.captureBudgetMiB;actorShadowBudgetMiB=quality.actorShadowBudgetMiB;shadowFateDiagnostics=NorthlightQuality::shadowFate(quality);
        replaySnapshots.configureBudget(size_t(captureBudgetMiB)*1048576,size_t(captureBudgetMiB)*524288);setNearReserve();
        logf("SHADOW experiment minSkinnedTriangles=%u captureBudgetMiB=%u actorShadowBudgetMiB=%u actorShadowRadius=%u distance=sampled-current-vertex unknown=first GI=preserved-at-capture-budget",minSkinnedShadowTriangles,captureBudgetMiB,actorShadowBudgetMiB,quality.actorShadowRadius);
        (void)NorthlightStreaming::cpuRetirement();
        bool paletteLoaded=NorthlightCelestialProfiles::load(root+"celestial-profiles.ini",celestialProfiles,paletteError);
        logf("CELESTIAL profiles loaded=%d zones=%zu %s",paletteLoaded,celestialProfiles.zones.size(),paletteError.c_str());
#if NORTHLIGHT_DEV_TUNING
        celestialShipped=celestialProfiles;
        logf("TUNING %s: %snorthlight-tuning.ini is polled, never ship this DLL",NorthlightZoneTuning::Marker,root.c_str());
#endif
        std::string rangeError;bool rangesLoaded=NorthlightRegionalShadow::load(root+"shadow-range-profiles.ini",shadowRanges,rangeError);
        logf("SHADOW regional terrain loaded=%d zones=%zu %s",rangesLoaded,shadowRanges.zones.size(),rangeError.c_str());
        staticStream=std::make_unique<StaticShadow::Streamer>(root+"world-cache");
        staticCasters.setAdmission([this](size_t bytes){return admitStaticAllocation(bytes);});
        worker=std::thread([this]{work();});}
    ~WorldRenderer(){prepareWorker.join(); /* 0.3.179: first: an abandoned worker may still be inside a record */
        logSnapshotAb(); /* 0.3.181: the SNAPSHOT ab window at device destroy */
        {std::lock_guard<std::mutex> lock(mutex);stopping=true;}wake.notify_one();if(worker.joinable())worker.join();releaseGPU();for(auto& p:captureShaders)drop(p.second.replacement);for(auto& p:terrainShadowShaders)drop(p.second);for(auto& p:terrainCoverageVertices)drop(p.second);for(auto& p:terrainCoveragePixels)for(auto& variant:p.second)drop(variant.shader);}
    void releaseActorTargets(){for(auto& t:actorShadow)drop(t);for(auto& t:actorShadowSurface)drop(t);for(auto& t:actorTiles)drop(t);for(auto& t:actorTilesSurface)drop(t);drop(actorTilesMid);drop(actorTilesMidSurface);drop(actorVisibility);drop(actorVisibilitySurface);drop(actorShadowPS);drop(actorTilesPS);}
    // 0.3.214 (task 42): keepMesh (device Reset, resolution change) keeps what describes the UPLOADED mesh: the MANAGED
    // page pool + material textures (they survive Reset in D3D9 and DXVK), the vertices/indices aliases, batches, terrain
    // lists, fixedTerrain(+bits), uploadedStaticOwners, uploaded/uploadedMap/meshGeneration and a staged pendingMesh (MANAGED
    // too; its cursor continues). Everything else (DEFAULT targets, dynamic rings, queries, state blocks, caches derived from
    // them) is released as before. recover() and the destructor release the mesh as well; the CPU payload is gone by then, so
    // upload() asks the builder for a fresh generation.
    void releaseGPU(bool keepMesh=false){replayBoundsAbandon();releaseReplayProbe();staticCasters.settle();rigidMemoryClear();admission.reset();admissionOn=false;gatedBodies.clear();prepareQuiesce();neutralShadowMaps=false;prepareCaches->sampled.clear();prepareCaches->bones.clear();prepareCachesStale=false;if(replays.empty()&&heldShadowReplays.empty())prepareFrameRelease(); /* 0.3.179: no replay left to point at them */actorShadowHistory.clear();actorShadowOriginValid=false;replayBoundsMetadata.clear();replayBoundsCache.clear();declarationCache.clear();if(!keepMesh)uploadedStaticOwners.reset();staticOwnerGeneration=UINT64_MAX;staticCasters.reset();staticMatcher.clear();staticM2Memory.clear();staticScene.reset();staticRetryTick=0;stateBlocks.clear();uploadedTerrain.clear();liveTerrainIndexCount=liveDirectionalIndexCount=0;if(!keepMesh){fixedTerrain.reset();fixedTerrainBits.reset();}liveTerrainGeneration=0;drop(regionalFogTexture);drop(neutralAO);drop(neutralZero);drop(ditherNoise);rainMask=nullptr;uploadedFogField.reset();releasePointGPU();probeActivation.reset();probeBlend.reset();drop(baselineSurface);drop(baselineLight);releaseReplayGPU();liveTerrainGPU.clear();{auto& queries=fence();NorthlightDynamicRing::reset(liveIndexRing,0,queries);queries.drop();}drop(liveIndicesGPU);liveIndexBytes=0;liveIndexBase=0;if(!keepMesh){pendingMesh.reset();clearMesh();}retiredMaterials.clear();if(!keepMesh){releaseMeshPool();uploadedMap.clear();}for(auto& t:shadow)drop(t);for(auto& s:shadowSurface)drop(s);for(auto& t:shadowCache)drop(t);for(auto& s:shadowCacheSurface)drop(s);drop(shadowCacheDepth);drop(shadowVerifySurface);drop(shadowVerify);for(auto& r:shadowVerifyRead)drop(r);drop(shadowScratch);drop(shadowScratchSurface);releaseActorTargets();drop(neutralWhite);actorValid[0]=actorValid[1]=false;actorCenterValid[0]=actorCenterValid[1]=false;drop(unionPS);invalidateShadowCache();for(auto& t:probe)drop(t);drop(probePrev);drop(shadowDepth);drop(lightSurface);drop(smoothSurface);drop(fogSurface);drop(fogBlurredSurface);drop(fogHistorySurface);drop(fogHistory);drop(fogTemporalPS);fogHistoryValid=false;drop(colorSurface);drop(light);drop(smoothLight);drop(fog);drop(fogBlurred);drop(color);drop(lightingPS);drop(giPS);drop(fogPS);drop(fogBlurPS);drop(fogCloudsPS);drop(cloudNoise);drop(localDirectPS);drop(localDirect16PS);drop(profileSplitSurface);profileSplitFailed=false;drop(removalPS);drop(temporalPS);drop(localFogPS);drop(localFogCombinePS);drop(localFogAccumSurface);drop(localFogAccum);drop(normalsPS);drop(normalBuffer);drop(normalSurface);drop(sourceVisPS);for(int a=0;a<2;++a)for(int b=0;b<2;++b){drop(sourceVis[a][b]);drop(sourceVisSurface[a][b]);}sourceVisValid=false;for(int i=0;i<2;++i){drop(temporalLight[i]);drop(temporalLightSurface[i]);drop(temporalDepth[i]);drop(temporalDepthSurface[i]);}temporalValid=false;drop(finalPS);drop(shadowPS);drop(replayPS);drop(shadowVS);drop(cachedShadowVS);drop(cachedShadowPS);drop(cachedFastPS);drop(cachedOpaqueFastPS);drop(cachedOpaquePS);drop(shadowDecl);width=height=0;uploadedSerial=0;}
    // Explicit enable/retry only, called after the wrapper's clearFrame(). This
    // never calls endFrame(), so packet capture and cleanup run exactly once.
    void recover(){meshRetry.clear();if(!failed)return;releaseGPU(true);failed=false;valid=false;streamingReports=0;logf("WORLD explicit recovery requested");}
    void releaseStateCache(){stateBlocks.clear();invalidateShadowCache();}
    void setEffects(const NorthlightEffectSwitches::Settings& requested){
        requestedEffects=requested;auto next=requested;next.gi=NorthlightQuality::giPass(quality,next.gi); /* GI=0: F8 cannot enable a GI that is never solved */
        if(effects==next)return;
        // Called at the frame boundary; never reuse lighting from a different
        // effect combination. The shared geometry and GI cache stay warm.
        temporalValid=false;sourceVisValid=false;
        if(effects.shadows!=next.shadows){
            shadowsComposited=false;shadowFrameReady=false;pointReady=false;
            neutralShadowMaps=false;invalidateShadowCache();
        }
        effects=next;
    }
    void reset(){equipmentLights.reset();equipmentMap.clear();particleLights.reset();particleMap.clear();shadowsComposited=false;pivotValid=false;pivotDistance=12.f;pivotCorrection.reset();pivotSelfCaptured=false;cascadeAnchor.reset();endFrame();replaySnapshots.clearIndexCache();actorJobComplete.reset();actorJobSerial_=0;actorSceneMap_.clear();lastActorCapture=0;terrainBoundsCache.clearPersistent();previousCacheHits=0;freeReplays.clear();pooledSnapshotBytes=0;valid=false;failed=false;releaseGPU(true); /* task 42: the committed MANAGED mesh survives any Reset, failed or not */}
    // 0.3.176 (U0/S0): the sample-frame lines of the selection and upload spans (and the RIGID event
    // lines) are formatted where they are today and written here, from endFrame, so no log write (a
    // vfprintf and fflush under the log lock) is inside a bucketed span. The text is the same.
    std::vector<std::string> deferredLines;
    __attribute__((format(printf,2,3))) void deferLogf(const char* format,...){
        va_list a;va_start(a,format);va_list b;va_copy(b,a);const int n=std::vsnprintf(nullptr,0,format,a);va_end(a);
        if(n>=0)try{std::string line(size_t(n),'\0');std::vsnprintf(&line[0],size_t(n)+1,format,b);deferredLines.push_back(std::move(line));}catch(...){}
        va_end(b);}
    void flushDeferredLogs(){for(const auto& line:deferredLines)logf("%s",line.c_str());deferredLines.clear();}
    // 0.3.192 (DXVK3): the per-interval lock volume (lock_meter.h); runs on the Diagnostics 2000 ms tick of endFrame.
    DWORD lockMeterTick=0;
    void logLockMeter(){
        const auto m=NorthlightLockMeter::takeInterval();const double frames=m.frames?double(m.frames):1.0,kib=1024.0;
        logf("LOCK METER frames=%u discardKiB/frame avg=%.0f max=%.0f stagingKiB/frame avg=%.0f max=%.0f over10MiB=%u | replayVB=%.0f replayIB=%.0f liveIB=%.0f arena=%.0f instances=%.0f game=%.0f | ring wraps=%u fenceReuse=%u discards=%u shrinks=%u | readback/frame locks=%.1f KiB=%.0f dynamic=%u/%.0fKiB defaultStatic=%u/%.0fKiB other=%u/%.0fKiB per frame flag=0x%x processVertices=%u | copies/frame served=%.1f/%.0fKiB fallbackLocks=%.1f fills=%u/%.0fKiB evictions=%u invalidations=%u refused=%u resident=%.0fKiB/%ubuffers cap=%.0fKiB large=%.0fKiB/%u/%u",
             unsigned(m.frames),m.discardSum/frames/kib,double(m.discardMax)/kib,m.stagingSum/frames/kib,double(m.stagingMax)/kib,unsigned(m.over10MiB),
             m.site[NorthlightLockMeter::ReplayVB]/frames/kib,m.site[NorthlightLockMeter::ReplayIB]/frames/kib,m.site[NorthlightLockMeter::LiveIB]/frames/kib,
             m.site[NorthlightLockMeter::Arena]/frames/kib,m.site[NorthlightLockMeter::Instances]/frames/kib,m.site[NorthlightLockMeter::Game]/frames/kib,
             unsigned(m.ringWraps),unsigned(m.ringFenceReuse),unsigned(m.ringDiscards),unsigned(m.ringShrinks),
             m.readLocks/frames,m.readBytes/frames/kib,unsigned(m.readClass[NorthlightLockMeter::ReadDynamic]),m.readClassBytes[NorthlightLockMeter::ReadDynamic]/frames/kib,unsigned(m.readClass[NorthlightLockMeter::ReadDefaultStatic]),m.readClassBytes[NorthlightLockMeter::ReadDefaultStatic]/frames/kib,unsigned(m.readClass[NorthlightLockMeter::ReadOther]),m.readClassBytes[NorthlightLockMeter::ReadOther]/frames/kib,
             unsigned(NorthlightUpload::readBackLock()),unsigned(m.processVertices),
             m.copyServed/frames,m.copyServedBytes/frames/kib,m.copyFallback/frames,unsigned(m.copyFills),m.copyFillBytes/kib,unsigned(m.copyEvictions),unsigned(m.copyInvalidations),unsigned(m.copyRefused),m.copyResidentBytes/kib,unsigned(m.copyResidentBuffers),m.copyCapBytes/kib,m.copyLargeBytes/kib,unsigned(m.copyLargeGrants),unsigned(m.copyLargeDrops));
        const auto r=NorthlightReplayCopies::takeRefusals();
        logf("REPLAY COPIES refused budget=%llu tooLarge=%llu backoff=%llu unsafe=%llu locked=%llu pressure=%llu allocation=%llu largestBufferKiB=%.0f largeCapMiB=%.0f maxLargeMiB=%.0f",(unsigned long long)r.budget,(unsigned long long)r.tooLarge,(unsigned long long)r.backoff,(unsigned long long)r.unsafe,(unsigned long long)r.locked,(unsigned long long)r.pressure,(unsigned long long)r.allocation,double(r.largestBuffer)/kib,double(NorthlightReplayCopies::kLargeCapBytes)/1048576,double(NorthlightReplayCopies::kLargeCopyBytes)/1048576);
    }
    void endFrame(bool retainPool=true){
        if(equipmentLightsOn()){equipmentLights.nextFrame();equipmentCapture.nextFrame();particleLights.nextFrame();NorthlightParticleUploads::cache().nextFrame();}
        flushDeferredLogs(); /* 0.3.176 (U0/S0): after every bucketed span of the frame */
        frameFence.endFrame(); /* 0.3.192 (DXVK3): the one Issue of the frame's fence, after every replay/terrain draw, before any early return; reset() calls endFrame() too */
        NorthlightLockMeter::endFrame();
        if(NorthlightReplayCopies::enabled.load(std::memory_order_relaxed))NorthlightReplayCopies::advanceFrame(); /* 0.3.192 (CS): the CPU copies' thrash/fill time base, once per frame on the replay thread */
        if(NorthlightDiagnostics::enabled()){const DWORD tick=GetTickCount();
            if(!lockMeterTick){lockMeterTick=tick?tick:1;NorthlightLockMeter::takeInterval();}
            else if(DWORD(tick-lockMeterTick)>=2000){lockMeterTick=tick;logLockMeter();}}
        prepareQuiesce();prepareEndFrame(); /* 0.3.177: before the replays are recycled */
        replayBoundsAbandon(); /* render() joined it; packets are recycled below */
        paletteFrameValid=false;staticPivotReady=false;rigidMemory.clearDrawn(); /* 0.3.173: drawn marks are per capture frame */
        if(!valid||failed||workerFault())stateBlocks.clear();
        /* 0.3.149 RenderProfile lines: after every measured span of the frame */
        if(profileSampled())logRenderProfile();else{replayProfileUsed.clear();replayGiPacked.clear();}
        frameStaging=frameProbeUpload=false;
        logReplayProbeWindow();
        for(int slot=0;slot<4;++slot){lastCascadeActions[slot]=cascadeActions[slot];cascadeActions[slot]='-';}
        if(captureSampled&&captureFrequency.QuadPart>0){double ms=1000.0/double(captureFrequency.QuadPart);
            logf("WORLD CPU capture terrain=%.3fms replay=%.3fms terrainCalls=%u terrainUP=%u replayCandidates=%u unknownCalls=%u acceptedReplay=%zu captureSkipped=%u rejSmall=%u rejGate=%u rejCut=%u rejBudget=%u",terrainCaptureTicks*ms,replayCaptureTicks*ms,terrainCaptureCalls,terrainUPCalls,replayCaptureCalls,unknownCaptureCalls,replays.size(),unsigned(captureMode==CaptureSkipped),rejSmall,rejGate,rejCut,rejBudget);
        }
        if(captureSampled){logf("WORLD capture cache hits=%llu frameHits=%llu entries=%zu bytes=%llu constantsReadBytes=%zu constantReadCalls=%zu sm1Draws=%u relativeDraws=%u",(unsigned long long)terrainBoundsCache.persistentHits(),(unsigned long long)(terrainBoundsCache.persistentHits()-previousCacheHits),terrainBoundsCache.persistentEntries(),(unsigned long long)terrainBoundsCache.persistentBytes(),capturedConstantBytes,capturedConstantCalls,capturedSM1Draws,capturedRelativeDraws);
            logf("BUFFER content noChangeWrites=%llu noChangeMiB=%.3f",(unsigned long long)NorthlightTrackedBuffers::noChangeWrites.load(std::memory_order_relaxed),double(NorthlightTrackedBuffers::noChangeBytes.load(std::memory_order_relaxed))/1048576);}
        if(captureSampled)logf("WORLD optimized caches terrainTrackedHits=%llu terrainAvoidedBytes=%llu boundsHits=%llu boundsMisses=%llu boundsAvoidedVertices=%llu boundsLookupMs=%.3f staticMaintenanceVisits=%llu staticPublications=%llu",
            (unsigned long long)terrainBoundsCache.trackedHits(),(unsigned long long)terrainBoundsCache.avoidedReadBytes(),
            (unsigned long long)replayBoundsCache.hits(),(unsigned long long)replayBoundsCache.misses(),(unsigned long long)replayBoundsCache.avoidedVertices(),replayBoundsCache.lookupMilliseconds(),
            (unsigned long long)staticCasters.stats().maintenanceVisits,(unsigned long long)staticCasters.stats().publications);
        if(captureSampled){const auto& t=terrainBoundsCache.maintenance();
            logf("TERRAIN content cache entryLimit=%zu byteLimit=33554432 entries=%zu bytes=%llu lookupMisses=%u evictions=%u countPressure=%u bytePressure=%u readBytes=%llu",
                terrainBoundsCache.persistentEntryLimit(),terrainBoundsCache.persistentEntries(),(unsigned long long)terrainBoundsCache.persistentBytes(),
                t.lookupMisses,t.evictions,t.countPressure,t.bytePressure,(unsigned long long)terrainBoundsCache.bytesRead());
            logf("TERRAIN revalidation tracked=%u unproven=%u lifetimeChanged=%u vertexChanged=%u indexChanged=%u unstable=%u exactReuses=%u rebuilds=%u indexTokenHits=%u",
                t.trackedHits,t.unproven,t.lifetimeChanged,t.vertexChanged,t.indexChanged,t.unstable,t.exactReuses,t.rebuilds,t.indexTokenHits);
            logf("TERRAIN metadata batches=%u hits=%u fallbacks=%u",t.metadataBatches,t.metadataHits,t.metadataFallbacks);}
        if(captureSampled)logf("MODEL index cache hits=%u misses=%u entries=%u bytes=%zu snapshotCacheHits=%u snapshotCacheMisses=%u snapshotCacheEntries=%zu snapshotCacheBytes=%zu revalidated=%u revalidationMismatches=%u trackedHits=%u avoidedReadBytes=%zu",replaySnapshots.indexCacheHits(),replaySnapshots.indexCacheMisses(),replaySnapshots.indexCacheEntries(),replaySnapshots.indexCacheBytes(),replaySnapshots.snapshotCacheHits(),replaySnapshots.snapshotCacheMisses(),replaySnapshots.snapshotCacheEntries(),replaySnapshots.snapshotCacheBytes(),replaySnapshots.snapshotRevalidated(),replaySnapshots.snapshotRevalidationMismatches(),replaySnapshots.trackedHits(),replaySnapshots.avoidedReadBytes());
        if(captureSampled){const auto& m=replaySnapshots.maintenance();
            logf("MODEL snapshot maintenance snapshotEvictions=%u snapshotEvictedBytes=%zu snapshotMs=%.3f indexEvictions=%u indexEvictedBytes=%zu indexMs=%.3f missEvicted=%u missRevision=%u missUnknown=%u missCollision=%u missRevalidated=%u historySlots=2048",
                m.snapshotEvictions,m.snapshotEvictedBytes,m.snapshotMs,m.indexEvictions,m.indexEvictedBytes,m.indexMs,
                m.missEvicted,m.missRevision,m.missUnknown,m.missCollision,replaySnapshots.snapshotRevalidationMismatches());}
        if(captureSampled)logf("MODEL capture metadata batches=%u knownBuffers=%u fallbackBuffers=%u fastSnapshotHits=%u",replaySnapshots.metadataBatches(),replaySnapshots.metadataHits(),replaySnapshots.metadataFallbacks(),replaySnapshots.fastCacheHits());
        if(captureSampled)logf("MODEL frame capture skinnedCandidates=%u accepted=%u blendRejected=%u fadeRejected=%u projectionRejected=%u budgetRejected=%u snapshotRejected=%u acceptedSkinnedBytes=%zu acceptedOtherBytes=%zu rejectedReadBytes=%zu totalReadBytes=%zu compactedDraws=%u spanVertices=%zu uniqueVertices=%zu savedVertexBytes=%zu nearAdmitted=%u nearBytes=%zu nearRefused=%u nearSelf=%d nearReserve=%zu radiusGated=%u radiusGatedBytes=%zu cutRejected=%u cutYards=%.1f insideRefused=%u insideRefusedActors=%u noRoot=%u admissionCentre=%s rootReads=%u rootMemoHits=%u",skinnedCandidates,skinnedAccepted,skinnedBlendRejected,skinnedFadeRejected,skinnedProjectionRejected,skinnedBudgetRejected,skinnedSnapshotRejected,acceptedSkinnedBytes,acceptedOtherBytes,captureRejectedBytes,replaySnapshots.bytesRead(),replaySnapshots.compactedDraws(),replaySnapshots.sourceSpanVertices(),replaySnapshots.uniqueVertices(),replaySnapshots.savedVertexBytes(),nearAdmitted,nearBytes,nearRefused,int(nearAnchorReady&&nearAnchor.self),replaySnapshots.nearReserve(),
            admission.counters().radiusGated,admission.counters().radiusGatedBytes,admission.counters().cutRejected,admission.cutActive()?double(admission.cut()):0.0,admission.counters().insideRefusedDraws,admission.counters().insideRefusedActors,admission.counters().noRoot,
            admissionOn?(admission.centreSelf()?"self":"pivot"):"off",rootReads,rootMemoHits);
        if(captureSampled)logf("MODEL frame capture other blendRejected=%u fadeRejected=%u projectionRejected=%u budgetRejected=%u",otherBlendRejected,otherFadeRejected,otherProjectionRejected,otherBudgetRejected);
        if(captureSampled&&captureFrequency.QuadPart>0){
            const double ms=1000.0/double(captureFrequency.QuadPart);
            // Raw totals of the rotating subset; do not multiply by 16. Rejected
            // candidates contribute only the phases they actually reached.
            const auto& c=capturePhases.ticks;
            logf("MODEL capture phases stride=16 measured=%u accepted=%u clockReads=%u stateMs=%.3f projectionMs=%.3f snapshotMs=%.3f constantsMs=%.3f materialMs=%.3f finalizeMs=%.3f actorMs=%.3f alphaQueriesSkipped=%u",capturePhaseDraws,capturePhaseAccepted,capturePhases.clockReads,c[CaptureState]*ms,c[CaptureProjection]*ms,c[CaptureSnapshot]*ms,c[CaptureConstants]*ms,c[CaptureMaterial]*ms,c[CaptureFinalize]*ms,c[CaptureActor]*ms,alphaStateQueriesSkipped);
            const auto& a=actorPhases.ticks;
            logf("MODEL actor capture phases candidates=%u textureReads=%u packets=%u vertices=%u clockReads=%u probeMs=%.3f textureMs=%.3f packetMs=%.3f",actorPhaseCandidates,actorTextureReads,actorDraws,actorVerticesEvaluated,actorPhases.clockReads,a[0]*ms,a[1]*ms,a[2]*ms);
            const auto& u=replayUploadPhases.ticks;
            logf("MODEL GPU upload phases calls=%u fallbackRetries=%u clockReads=%u cacheMs=%.3f bulkMs=%.3f",replayUploadCalls,replayUploadFallbacks,replayUploadPhases.clockReads,u[0]*ms,u[1]*ms);
        }
        if(captureSampled){const auto& c=constantEpochProfile;const auto& s=constantEpochStats; /* 0.3.180 (C0): epoch/block counters of this sample frame; pose and self-check counters */
            logf("MODEL capture constants epochTests=%llu snapshotHits=%llu bytesAvoided=%llu poseFastHits=%llu blockTests=%llu blockShared=%llu blockRewrites=%llu blockCopies=%llu registersReused=%llu registersFetched=%llu poseBlockHits=%llu poseBytesAvoided=%llu selfChecks=%llu selfCheckMismatches=%llu",
            (unsigned long long)c.tests,(unsigned long long)c.hits,(unsigned long long)c.bytesAvoided,(unsigned long long)s.poseFastHits,
            (unsigned long long)c.blockTests,(unsigned long long)c.blockShared,(unsigned long long)c.blockRewrites,(unsigned long long)c.blockCopies,
            (unsigned long long)c.registersReused,(unsigned long long)c.registersFetched,(unsigned long long)s.poseBlockHits,
            (unsigned long long)s.poseBytesAvoided,(unsigned long long)s.selfChecks,(unsigned long long)s.selfCheckMismatches);
            char blocks[16*21]={};size_t used=0;
            for(unsigned n=0;n<NorthlightConstantEpoch::FloatBlocks&&used<sizeof blocks;++n)
                used+=std::snprintf(blocks+used,sizeof blocks-used,n?",%llu":"%llu",(unsigned long long)c.dirtyBlocks[n]);
            logf("MODEL capture constant blocks registersPerBlock=16 dirty=%s",blocks);}
        if(captureSampled)logf("MODEL triangle histogram bins=lt25,25to49,50to99,100to499,500to1999,ge2000 candidates=%u,%u,%u,%u,%u,%u skinned=%u,%u,%u,%u,%u,%u captured=%u,%u,%u,%u,%u,%u capturedSkinned=%u,%u,%u,%u,%u,%u smallEarly=%u smallGI=%u",
            replayTriangleBins[0],replayTriangleBins[1],replayTriangleBins[2],replayTriangleBins[3],replayTriangleBins[4],replayTriangleBins[5],
            skinnedTriangleBins[0],skinnedTriangleBins[1],skinnedTriangleBins[2],skinnedTriangleBins[3],skinnedTriangleBins[4],skinnedTriangleBins[5],
            acceptedTriangleBins[0],acceptedTriangleBins[1],acceptedTriangleBins[2],acceptedTriangleBins[3],acceptedTriangleBins[4],acceptedTriangleBins[5],
            acceptedSkinnedTriangleBins[0],acceptedSkinnedTriangleBins[1],acceptedSkinnedTriangleBins[2],acceptedSkinnedTriangleBins[3],acceptedSkinnedTriangleBins[4],acceptedSkinnedTriangleBins[5],smallShadowEarly,smallShadowGI);
        for(auto& n:replayTriangleBins)n=0;for(auto& n:skinnedTriangleBins)n=0;for(auto& n:acceptedTriangleBins)n=0;for(auto& n:acceptedSkinnedTriangleBins)n=0;
        smallShadowEarly=smallShadowGI=0;shadowSelectionDone=false;captureShortfall=false;
        lastCapturePhaseReads=capturePhases.clockReads+actorPhases.clockReads;clearCaptureDiagnostics();if(captureSampled)++capturePhaseSerial;
        skinnedCandidates=skinnedBlendRejected=skinnedFadeRejected=skinnedProjectionRejected=skinnedBudgetRejected=skinnedSnapshotRejected=skinnedAccepted=0;otherBlendRejected=otherFadeRejected=otherBudgetRejected=otherProjectionRejected=0;
        captureRejectedBytes=acceptedSkinnedBytes=acceptedOtherBytes=0;nearAdmitted=nearRefused=0;nearBytes=0;nearAnchorReady=false;
        admission.endFrame(size_t(captureBudgetMiB)*1048576,replaySnapshots.bytesRead());admissionOn=false;rootMemo.clear();rootReads=rootMemoHits=0;gatedBodies.clear(); /* 0.3.211 (task 31): the next frame's cut, after the log read this frame's counters; no-op when no skinned draw armed it */
        previousCacheHits=terrainBoundsCache.persistentHits();capturedConstantBytes=capturedConstantCalls=0;capturedSM1Draws=capturedRelativeDraws=0;
        terrainCaptureTicks=replayCaptureTicks=0;terrainCaptureCalls=terrainUPCalls=replayCaptureCalls=unknownCaptureCalls=0;captureSampled=false;
        rejSmall=rejGate=rejCut=rejBudget=rejOther=0;
        valid=false;traceContext=0;traceFog=0;legacyFogKnown=false;shadowFrameReady=false;legacyFog=NorthlightLegacyFog::Constants{};
        // Bound retained vector capacities across changing scenes. Reuse storage,
        // never old geometry: each subsequent draw still re-reads every byte.
        // Give the current scene first claim on the pool, instead of letting
        // unused records from a previous crowded frame monopolize its budget.
        if(retainPool){
            for(auto& unused:freeReplays){pooledSnapshotBytes-=unused->snapshot.capacityBytes();unused->snapshot=NorthlightDrawSnapshot::Mesh{};}
            for(auto& p:replays)recycleReplay(p.release());replays.clear();
            for(auto& p:heldShadowReplays)recycleReplay(p.release());heldShadowReplays.clear();
        }else{
            if(prepareUnsettled()){for(auto& p:replays)recycleReplay(p.release());for(auto& p:heldShadowReplays)recycleReplay(p.release());} /* 0.3.177: quarantined */
            replays.clear();heldShadowReplays.clear();freeReplays.clear();pooledSnapshotBytes=0;}
        prepareFrameRelease(); /* 0.3.179: after the recycle */
        terrainBoundsCache.clearFrame();liveTerrainChunks.clear();frameTerrain.clear();frameTerrainVertices=frameTerrainIndices=0;pointLiveBatches.clear();pointReady=false;
        /* The snapshot frame advances on capture frames only. With the version provider set here every
           untracked hit revalidates anyway; without it the serial&15==frame&15 slice, counted in all
           frames, could never reach some serials when captures run every N frames. */
        snapshotEndFrame(); /* 0.3.181 (S3): reads the frame's snapshot counters */
        if(captureMode!=CaptureSkipped)replaySnapshots.clearFrame();
        lastCaptureSkipped=captureMode==CaptureSkipped;actorCaptureDecided=false;actorJob.reset();captureMode=CaptureUndecided;actorVerticesEvaluated=actorDraws=actorSkippedAlpha=0;
        releaseExpiredMesh();
        reclaimInactiveCasters();
    }
    bool ready()const{return valid&&active&&active->bvh&&!failed&&!workerFault();}
    // task 42 (MEMMAP northlight line): the render thread's own view, no lock and no counters beyond plain reads. Render thread only
    // (`active` and the mesh pools are its own). Not included, as they need the streamer's or the builder's locks or are worker locals:
    // static shadow cpu/gpu, local scene retained/flat/bvh, replay caches.
    int memmapTally(char* out,size_t size)const{
        const double pool=double(meshStore.poolBytes()),idle=double(meshStore.idleBytes());
        double plan=0,payload=0,bvh=0;
        if(active){if(active->meshPlan){plan=double(active->meshPlan->cpuBytes());payload=double(active->meshPlan->payload.bytes());}if(active->bvh)bvh=double(active->bvh->retainedBytes());}
        return std::snprintf(out,size,"meshPoolMiB=%.1f meshPlanCpuMiB=%.1f uploadPayloadLiveMiB=%.1f materialReuseMiB=%.1f giBvhMiB=%.1f geometryDemandMiB=%.1f meshIdleMiB=%.1f",pool/1048576.0,plan/1048576.0,payload/1048576.0,double(materialReuseBytes)/1048576.0,bvh/1048576.0,
            double(geometryMemoryDemand.load(std::memory_order_relaxed))/1048576.0,idle/1048576.0);
    }
    const char* lastSkipReason()const{return skipReason;} /* 0.3.169: valid after render() returned false */
    bool captureSkippedLastFrame()const{return lastCaptureSkipped;} /* for the sampled CPU profile, logged after endFrame */
    unsigned capturePhaseReadsLastFrame()const{return lastCapturePhaseReads;} /* 0.3.150: clock reads of the capture-phase subset (inside the capture timers), likewise */
    bool hasContext()const{return valid&&!failed&&!workerFault();}
    unsigned frameTraceFog()const{return traceFog;}
    unsigned frameTraceContext()const{return traceContext;} /* 0.3.200 (frame trace) */
    bool actorShadowsEnabled()const{return quality.actorShadows!=0;}
    bool commandStream()const{return quality.commandStream!=0;} /* 0.3.192 (CS): the replay-thread stream was requested; creation-time key, see stream_hooks.h */
    unsigned blobShadowStrength()const{return quality.blobShadowStrength;} /* 0.3.193: read once at device creation */
    // 0.3.198 (rain): the smoothed weather state, set once per frame at the frame boundary (Device::finishFrameImpl); no effect reads it yet.
    NorthlightWeather::State weatherState{};
    void setWeather(const NorthlightWeather::State& s){weatherState=s;}
    void setNameMask(IDirect3DSurface9* depthStencil){nameMaskDepth=depthStencil;} /* 0.3.212 (task 32): the world depth-stencil holding this frame's name marks (non-owning, for the next render() only; null: no marks) */
    void setRainMask(IDirect3DTexture9* t){rainMask=t;} /* 0.3.202 (rain): non-owning, for the next render() only */
    const NorthlightWeather::State& weather()const{return weatherState;}
    // 0.3.198 (rain): the settings (Weather 0/1, RainFog 0..2) and the per-frame scalars derived from them and weather():
    // render() and the celestial renderers read this one place; all zero (identity) with Weather=0 or no weather.
    unsigned weatherSetting()const{return quality.weather;}
    unsigned rainFogSetting()const{return quality.rainFog;}
    // 0.3.200 (gpu budget): GpuBudgetMs (0 = off) and the controller's level for the next frames; off forces level 0 (the full path, unchanged).
    unsigned gpuBudgetMs()const{return quality.gpuBudgetMs;}
    void setGpuBudgetLevel(unsigned level){gpuBudgetLevel=quality.gpuBudgetMs?std::min(level,NorthlightGpuBudget::MaxLevel):0u;}
    // task 45: ActorShadowCpuMs (0 = off) and the CPU controller's level; off forces level 0. effectiveActorShadowRadius() is the one place the radius is read
    // for work: the capture admission and the selection use it (the setting itself stays in the logs that describe the setting).
    unsigned actorShadowCpuMs()const{return quality.actorShadowCpuMs;}
    void setCpuBudgetLevel(unsigned level){cpuBudgetLevel=quality.actorShadowCpuMs?std::min(level,NorthlightGpuBudget::MaxLevel):0u;}
    unsigned effectiveActorShadowRadius()const{return NorthlightGpuBudget::actorShadowRadius(quality.actorShadowRadius,cpuBudgetLevel);}
    bool rainBlendSetting()const{return quality.weather!=0;} /* 0.3.199 (rain): the draw hook's alpha-blended rain streaks and rain-only mist skip, with Weather=1 */
    NorthlightWeatherEffects::Frame weatherEffects()const{return NorthlightWeatherEffects::derive(weatherState,quality.weather,quality.rainFog);}
    bool nameMask()const{return quality.nameMask!=0;} bool nameMaskDebug()const{return quality.nameMaskDebug!=0;} /* 0.3.212 (task 32): read once at device creation */
    bool contactAO()const{return quality.contactAO!=0;}
    unsigned aoMethod()const{return quality.aoMethod;}
    float aoStrength()const{return .6f*float(quality.aoStrength)/100.f;}
    float aoRadius()const{return float(quality.aoRadius)/100.f;}
    bool equipmentLightsOn()const{return quality.equipmentLights!=0;}
    NorthlightParticleLights::Frame& particleFrame(){return particleLights;}
    bool particleContext(float* inverseView,float* camera,float* projectionOut){
        if(!equipmentLightsOn()||!hasContext()||!active)return false;
        if(particleMap!=active->map){particleLights.reset();particleMap=active->map;}
        std::memcpy(inverseView,context.inverseView,64);std::memcpy(camera,context.camera,12);std::memcpy(projectionOut,projection,12);return true;
    }
    void registerEquipmentShader(IDirect3DVertexShader9* shader,const DWORD* words,size_t count){
        if(equipmentLightsOn()){const unsigned lights=NorthlightEquipmentLightProof::analyze(reinterpret_cast<const uint32_t*>(words),count);equipmentShaders[shader]=lights;
            if(lights&&NorthlightDiagnostics::enabled())logf("EQUIPMENT shader proven nativePointCount=%u shader=%p",lights,static_cast<void*>(shader));}
    }
    void captureEquipmentLights(IDirect3DVertexShader9* shader,IDirect3DSurface9* mainDepth){
        if(!equipmentLightsOn()||!hasContext()||!active)return;
        const auto it=equipmentShaders.find(shader);if(it==equipmentShaders.end()||!it->second)return;
        if(equipmentMap!=active->map){equipmentLights.reset();equipmentMap=active->map;}
        const bool actor=isSkinnedShader(shader);
        if(!equipmentCapture.take(actor,equipmentLights.banks,equipmentLights.backgroundBanks))return;
        NorthlightDrawSnapshot::Ref<IDirect3DSurface9> ds;if(!mainDepth||FAILED(d->GetDepthStencilSurface(ds.out()))||ds.p!=mainDepth){++equipmentCapture.domainRejected;return;}
        // The proven constants are view-space: require the current main-world
        // projection before using this frame's inverse view (portraits excluded).
        float q[16];if(FAILED(d->GetVertexShaderConstantF(2,q,4))){++equipmentCapture.domainRejected;return;}for(float v:q)if(!std::isfinite(v)){++equipmentCapture.domainRejected;return;}if(std::fabs(q[0]-projection[0])>.005f||std::fabs(q[5]-projection[1])>.005f||std::fabs(q[14]-projection[2])>.005f||std::fabs(q[15])>.001f){++equipmentCapture.domainRejected;return;}
        float bank[44];if(FAILED(d->GetVertexShaderConstantF(17,bank,11))||!equipmentLights.admit(bank,it->second,actor))return;
        const auto now=GetTickCount();
        for(unsigned i=0;i<it->second;++i){NorthlightEquipmentLights::Native n;if(NorthlightEquipmentLights::decode(bank,i,context.inverseView,context.camera,n)){++equipmentCapture.decoded;equipmentLights.observe(n,now,active->localLights,context.camera);}else ++equipmentCapture.rejected;}
    }
    unsigned spellLayerSetting()const{return quality.spellLayer;} /* task 34: SpellLayer, SpellLayerClasses and SpellTrace are read once at device creation */
    unsigned spellLayerClassesSetting()const{return quality.spellLayerClasses;}
    unsigned spellTraceSetting()const{return quality.spellTrace;}
    void spellFogHint(const float game[3],bool bare){spellFogHold.addHint(game,bare);} /* SpellFog: a falling-ramp game constant the layer saw; fitted with the next publish (spell_fog.h Hold) */
    bool weatherMemorySetting()const{return quality.weatherMemory!=0;} /* WeatherMemory: the game's weather state drives the weather effects (weather_memory.h); 0 = the texture detector only */
    bool depthFuncAware()const{return quality.depthFuncAware!=0;} /* DepthFuncAware: read once at device creation */
    unsigned spellFogSetting()const{return quality.spellFog;} /* SpellFog (task 34): read once with the others */
    const NorthlightSpellFog::Frame& spellFogFrame()const{return spellFogHold.out;} /* Northlight's fog toward the view axis (previous frame's estimate while the next frame's records are made): the spell layer folds it into the recorded fog constants */
    bool frameDrawGates()const{return quality.frameDrawGates!=0;} /* 0.3.187: read once at device creation */ /* 0.3.158: ActorShadows=0 leaves actor shadows to the game's blobs */
    const float* legacyFogParameters()const{return legacyFog.parameters;}
    const NorthlightLegacyFog::Constants& legacyFogConstants()const{
        if(!legacyFogKnown&&active&&legacyFogHold.has&&legacyFogHold.map==active->map&&
           DWORD(GetTickCount()-legacyFogHold.lastUpdate)<=NorthlightLegacyFog::Hold::HoldMs)return legacyFogHold.held;
        return legacyFog;
    }
    bool hasOriginalFog()const{return legacyFogKnown;}
    const NorthlightCelestialProfiles::Profile& celestialPalette(const char* map,const float* camera){
        // Freeze once per frame: early disc, late halo, direct/GI and fog cannot
        // observe different worker publications or temporal transition states.
        if(paletteFrameValid&&paletteFrameMap==map)return framePalette;
        std::shared_ptr<const PaletteRegion> region;
        {std::lock_guard<std::mutex> lock(mutex);region=publishedPaletteRegion;}
#if NORTHLIGHT_DEV_TUNING
        devPoll(map,camera,region&&region->map==map?NorthlightRegionalFog::zoneAt(region->region,camera[0],camera[1]):0);
#endif
        auto target=celestialProfiles.fallback;
        if(region&&region->map==map)target=NorthlightCelestialProfiles::sample(celestialProfiles,region->region,map,camera[0],camera[1]);
        framePalette=paletteTransition.update(target,map,camera[0],camera[1],double(GetTickCount())*.001);
        paletteFrameMap=map;paletteFrameValid=true;
        DWORD now=GetTickCount();if(NorthlightDiagnostics::enabled()&&(!paletteLogAt||now-paletteLogAt>=10000)){paletteLogAt=now;
            uint32_t zone=region&&region->map==map?NorthlightRegionalFog::zoneAt(region->region,camera[0],camera[1]):0;
            logf("CELESTIAL palette map=\"%s\" zone=%u moonDisc=(%.3f %.3f %.3f) lightMix=%.3f/%.3f strength=%.3f/%.3f",map,zone,framePalette.disc[1][0],framePalette.disc[1][1],framePalette.disc[1][2],framePalette.mix[0],framePalette.mix[1],framePalette.strength[0],framePalette.strength[1]);
        }return framePalette;
    }
    // the per-frame glow hue for the celestial renderer (sun: the game's band 10
    // sunHalo; moon: the regional moon_disc tint). Updated with the world context, so the
    // sky-phase disc sees the previous frame's value. valid=false: draw today's colours.
    const NorthlightSunHue::GlowHue& glowHue()const{return glowHueFrame;}
    // the horizon-haze constants of the last world composite (optical depth 0 = no haze:
    // fog off, HorizonHaze=0, unknown fog colour, or no world frame yet). The veil reads haze[3], shape[2].
    const NorthlightHorizonHaze::Constants& horizonHazeConstants()const{return hazeFrame;}
    // CPU estimate (0..1) of the 128-unit fog volume's transmittance on a sky pixel toward
    // the sun, i.e. the fog.a the composite applies there. 1 with fog off or where the field has no air.
    float skyTransmittance()const{return skyTransmittanceFrame;}
    float underwaterScale()const{return underwaterFade.scale();}
    bool celestialContext(NorthlightCelestial::Context& out,float* inverseView,float* projectionOut)const{
        if(!hasContext()||!celestialValid)return false;
        out=celestial;out.horizonSine=apparentHorizon.valid?apparentHorizon.value:0.f;memcpy(inverseView,context.inverseView,64);memcpy(projectionOut,projection,12);return true;
    }
    bool recognizesWmo(IDirect3DVertexShader9* shader)const{return wmoShaders.count(shader)!=0;}
    bool isWorldShader(IDirect3DVertexShader9* shader)const{return terrainShaders.count(shader)||captureShaders.count(shader)||wmoShaders.count(shader);}
    bool isSkinnedShader(IDirect3DVertexShader9* shader)const{auto it=actorPrograms.find(shader);return it!=actorPrograms.end()&&it->second->skinned;}
    // A main-world actor can be the only visible geometry while looking up.
    // Read this frame's camera/light; never carry the last terrain view forward.
    bool modelContext(IDirect3DVertexShader9* shader){
        if(failed)return false;if(valid)return true;
        auto it=captureShaders.find(shader);
        if(it==captureShaders.end()||!it->second.skinned||it->second.projectionKind!=2)return false;
        float columns[16],rows[16];Projection q;
        if(FAILED(d->GetVertexShaderConstantF(2,columns,4))||!decodeColumnProjection(columns,q,rows))return false;
        char map[64]={};NorthlightWorldCamera::Camera camera;NorthlightWorldCamera::Diagnostics why;
        NorthlightWmoContext::Lighting light;
        if(!NorthlightWorldCamera::read(map,rows[11],camera,&why)||!NorthlightWmoContext::readGlobalLighting(camera.camera,light))return false;
        auto lightHold=globalLightHold;lightHold.filter(light,map,GetTickCount());
        if(!NorthlightWmoContext::context(camera.view,light,context))return false;
        globalLightHold=std::move(lightHold);
        valid=true;traceContext=4;projection[0]=rows[0];projection[1]=rows[5];projection[2]=rows[11];
        readOriginalFog(30,it->second.fog);if(legacyFogKnown)traceFog=1;
        updateWorldContext(map,camera.camera,&light);
        if(!modelContexts++||(modelContexts%600==0&&NorthlightDiagnostics::enabled()))logf("WORLD model context accepted map=\"%s\" count=%u fogProof=%d",map,modelContexts,it->second.fog);
        return hasContext();
    }
    unsigned modelContexts=0;
    bool wmoContext(IDirect3DVertexShader9* shader){
        if(failed)return false;
        auto it=wmoShaders.find(shader);if(it==wmoShaders.end())return false;
        if(valid){
            if(!legacyFogKnown&&it->second->fog){readOriginalFog(30,true);if(legacyFogKnown)traceFog=1;}
            return true;
        }
        float columns[16],rows[16];Projection q;
        if(FAILED(d->GetVertexShaderConstantF(2,columns,4))||!decodeColumnProjection(columns,q,rows))return false;
        char map[64]={};NorthlightWorldCamera::Camera camera;NorthlightWorldCamera::Diagnostics why;
        NorthlightWmoContext::Lighting light;
        bool cameraRead=NorthlightWorldCamera::read(map,rows[11],camera,&why);
        bool globalRead=cameraRead&&NorthlightWmoContext::readGlobalLighting(camera.camera,light);
        auto lightHold=globalLightHold;if(globalRead)lightHold.filter(light,map,GetTickCount()); /* kept below only when this read is accepted */
        const bool globalLighting=globalRead&&NorthlightWmoContext::context(camera.view,light,context);
        bool lightingRead=globalLighting;
        if(cameraRead&&!lightingRead&&it->second->lighting){float values[12];
            lightingRead=SUCCEEDED(d->GetVertexShaderConstantF(10,values,3))&&NorthlightWmoContext::decodeLitShader(it->second->hash,camera.view,values,context);
        }
        if(!cameraRead||!lightingRead){
            if(++wmoRejects==1||(wmoRejects%3600==0&&NorthlightDiagnostics::enabled()))logf("CITY context rejected camera=%s count=%u",NorthlightWorldCamera::rejectName(why.reason),wmoRejects);
            return false;
        }
        if(globalLighting)globalLightHold=std::move(lightHold);
        valid=true;traceContext=3;projection[0]=rows[0];projection[1]=rows[5];projection[2]=rows[11];
        readOriginalFog(30,it->second->fog);if(legacyFogKnown)traceFog=1;
        updateWorldContext(map,camera.camera,globalRead?&light:nullptr);
        if(++wmoContexts==1||(wmoContexts%600==0&&NorthlightDiagnostics::enabled()))logf("CITY WMO context accepted map=\"%s\" count=%u fogProof=%d globalLight=%d",map,wmoContexts,it->second->fog,globalRead);
        return true;
    }
    void terrainContext(){
        if(failed)return;
        // Camera context may already come from an actor whose shader has no fog.
        // A later terrain draw must still supply this frame's native atmosphere.
        if(valid){if(!legacyFogKnown){readOriginalFog(12,true);if(legacyFogKnown)traceFog=1;}return;}
        float view[16]={},lighting[12]={},p[16]={},camera[3]={};char map[64]={};
        bool registers=SUCCEEDED(d->GetVertexShaderConstantF(0,view,4))&&SUCCEEDED(d->GetVertexShaderConstantF(4,p,4));
        bool nativeRead=SUCCEEDED(d->GetVertexShaderConstantF(24,lighting,3));
        bool gameContext=NorthlightWorldContext::readMapAndCamera(map,camera);
        NorthlightWorldCamera::Camera independent;NorthlightWorldCamera::Diagnostics why;char cameraMap[64]={};
        bool cameraRead=registers&&gameContext&&NorthlightWorldCamera::read(cameraMap,p[11],independent,&why);
        bool cameraMatches=cameraRead&&!std::strcmp(map,cameraMap)&&NorthlightWorldCamera::agreesWithTerrain(independent,view);
        NorthlightWmoContext::Lighting global;
        bool globalRead=cameraMatches&&NorthlightWmoContext::readGlobalLighting(camera,global);
        auto lightHold=globalLightHold;if(globalRead)lightHold.filter(global,map,GetTickCount()); /* kept below only when this read is accepted */
        bool globalLighting=false;
        bool decoded=registers&&gameContext&&NorthlightWmoContext::terrainContext(view,nativeRead?lighting:nullptr,camera,globalRead?&global:nullptr,context,&globalLighting);
        traceContext=globalRead?1u:2u;
        bool agreement=decoded;
        if(!agreement){
            if(++contextRejects==1||(contextRejects%3600==0&&NorthlightDiagnostics::enabled()))logf("WORLD context rejected: registers=%d affineLight=%d clientRead=%d cameraAgreement=%d map=\"%s\" shaderCamera=(%.2f %.2f %.2f) gameCamera=(%.2f %.2f %.2f) light=(%.3f %.3f %.3f) count=%u",registers,decoded,gameContext,agreement,map,context.camera[0],context.camera[1],context.camera[2],camera[0],camera[1],camera[2],lighting[0],lighting[1],lighting[2],contextRejects);return;}
        if(++cameraChecks==1||(cameraChecks%3600==0&&NorthlightDiagnostics::enabled()))logf("CITY camera audit read=%d terrainAgreement=%d reject=%s permissiveSignatures=%d failingSignature=%d",cameraRead,cameraMatches,NorthlightWorldCamera::rejectName(why.reason),int(NorthlightWorldCamera::kPermissiveCameraSignatures),NorthlightWorldCamera::failingSignature());
        if(globalLighting)globalLightHold=std::move(lightHold);
        valid=true;projection[0]=p[0];projection[1]=p[5];projection[2]=p[11];
        readOriginalFog(12,true);if(legacyFogKnown)traceFog=1;
        updateWorldContext(map,camera,globalRead?&global:nullptr);
    }
    void readOriginalFog(UINT fogRegister,bool known){
        // Read the original terrain fog before any extension render pass changes constants/state.
        IDirect3DPixelShader9* originalPS=nullptr;FogShader fogShader;
        if(SUCCEEDED(d->GetPixelShader(&originalPS))&&originalPS){
            auto it=fogShaders.find(originalPS);if(it!=fogShaders.end())fogShader=it->second;
        }drop(originalPS);
        float fogParameters[4]={},fogColor[4]={};DWORD fogEnabled=0,fogTable=0,fogARGB=0;
        bool fogRegisters=known&&SUCCEEDED(d->GetVertexShaderConstantF(fogRegister,fogParameters,1));
        if(fogShader.major==3)fogRegisters=fogRegisters&&SUCCEEDED((fogShader.colorRegister>=0?d->GetPixelShaderConstantF(UINT(fogShader.colorRegister),fogColor,1):D3DERR_INVALIDCALL));
        else fogRegisters=fogRegisters&&SUCCEEDED(d->GetRenderState(D3DRS_FOGENABLE,&fogEnabled))&&SUCCEEDED(d->GetRenderState(D3DRS_FOGTABLEMODE,&fogTable))&&SUCCEEDED(d->GetRenderState(D3DRS_FOGCOLOR,&fogARGB));
        NorthlightLegacyFog::Constants measured;
        bool fogKnown=fogRegisters&&NorthlightLegacyFog::decode(fogShader.major,fogShader.verified,fogEnabled!=0,fogTable,fogParameters,fogColor,fogARGB,projection[2],measured);
        if(fogKnown){legacyFog=measured;legacyFogKnown=true;}
        if(++fogReports==1||(fogReports%600==0&&NorthlightDiagnostics::enabled()))logf("WORLD legacy fog known=%d ps=%u verified=%d colorRegister=%d enabled=%.0f params=(%.7g %.7g %.7g) color=(%.5f %.5f %.5f)",fogKnown,fogShader.major,fogShader.verified,fogShader.colorRegister,legacyFog.parameters[3],legacyFog.parameters[0],legacyFog.parameters[1],legacyFog.parameters[2],legacyFog.color[0],legacyFog.color[1],legacyFog.color[2]);
    }
    void updateWorldContext(const char* map,const float* camera,const NorthlightWmoContext::Lighting* global=nullptr){
        if(unsigned fault=workerFault()){if(!failed)logf("WORLD worker stopped: %s; restart required",workerFaultMessage(fault));failed=true;valid=false;return;}
        if(!reportedContext){logf("WORLD context validated: map=\"%s\" camera=(%.2f %.2f %.2f) sun=(%.3f %.3f %.3f)",map,camera[0],camera[1],camera[2],context.lightDirection[0],context.lightDirection[1],context.lightDirection[2]);reportedContext=true;}
        {   // The camera's liquid type from the proven predictor reads (wmo_context.h); unproven frames hold the state for UnderwaterFade::HoldMs.
            const bool known=global&&global->predictors;const DWORD now=GetTickCount();
            underwaterFade.update(map,known,known&&global->liquidType!=0,now);
            if(underwaterFade.underwater!=underwaterLogged&&(DWORD(now-underwaterLogAt)>250||NorthlightDiagnostics::enabled())){underwaterLogAt=now;underwaterLogged=underwaterFade.underwater;logf("WORLD underwater %s liquid=%u",underwaterFade.underwater?"on":"off",known?global->liquidType:0u);} // a flip suppressed by the rate limit is logged later, once the state still differs
        }
        celestialValid=NorthlightCelestial::read(camera,context.direct,celestial);
        // 0.3.200: a sky block read rejected for one frame (the two copies differ while the game rewrites it, more often with the game frames
        // ahead) dropped the sun weight to 0 for that frame: the sun light, shadows and shafts flashed off. The last good read on the same map
        // stands in for up to CelestialHoldMs; a longer failure (loading, an indoor map without a sky) still ends at the fallback.
        {const DWORD now=GetTickCount();
         if(celestialValid){celestialHeld=celestial;celestialHeldMap=map;celestialHeldAt=now;celestialHeldOk=true;}
         else if(celestialHeldOk&&celestialHeldMap==map&&DWORD(now-celestialHeldAt)<=CelestialHoldMs){celestial=celestialHeld;celestialValid=true;++celestialHolds;}}
        if(celestialValid){
            // The native billboards' orbit (resolveRendererSky), shared by discs, shadows and fog.
            const float nativeSunAlpha=celestial.sun.alpha,nativeMoonAlpha=celestial.moon.alpha;
            const double nativeSun=NorthlightCelestialOrbit::elevationDegrees(celestial.sun.direction[2]);
            const double nativeMoon=NorthlightCelestialOrbit::elevationDegrees(celestial.moon.direction[2]);
            const auto orbit=NorthlightCelestial::resolveRendererSky(celestial,context.direct);
            const auto lightOrbit=celestialLightMotion.update(orbit,celestial.dayFraction,GetTickCount(),
                celestialLightMap!=map||different(celestialLightCamera,vec(camera),40));
            celestialLightMap=map;celestialLightCamera=vec(camera);
            celestialLight=celestial;
            std::memcpy(celestialLight.sun.direction,lightOrbit.sun.direction,sizeof celestialLight.sun.direction);
            std::memcpy(celestialLight.moon.direction,lightOrbit.moon.direction,sizeof celestialLight.moon.direction);
            NorthlightCelestial::applyRendererPolicy(celestialLight,context.direct);
            const auto palette=celestialPalette(map,camera);
            NorthlightCelestialProfiles::apply(palette,context.direct,celestial);
            NorthlightCelestialProfiles::apply(palette,context.direct,celestialLight);
            continuousCelestialShadows=false; // Native-speed orbit: ordinary cache policy, no accelerated phases.
            if(++celestialOrbitReports%600==1&&(celestialOrbitReports==1||NorthlightDiagnostics::enabled()))logf("CELESTIAL orbit gameDay=%.6f sun=%.2f->%.2f moon=%.2f->%.2f schedule=native sunCrest=85 moonCrest=55 weights=%.3f/%.3f nativeAlpha=%.3f/%.3f rendererAlpha=%.1f/%.1f",celestial.dayFraction,nativeSun,orbit.sun.elevation,nativeMoon,orbit.moon.elevation,celestial.sunWeight,celestial.moonWeight,nativeSunAlpha,nativeMoonAlpha,celestial.sun.alpha,celestial.moon.alpha);
        }
        if(!celestialValid){celestialLightMotion.reset();continuousCelestialShadows=false;}
        // glow hue: the light slots of this frame's proven read, else held for 2 s on the same map.
        const DWORD slotsNow=GetTickCount();
        if(lightSlotsMap!=map||DWORD(slotsNow-lightSlotsAt)>NorthlightSunHue::HoldMs){lightSlotsValid=false;lightSlotsMap=map;}
        if(global&&global->slotsProven){memcpy(lightSlots,global->slots,sizeof lightSlots);lightSlotsValid=true;lightSlotsAt=slotsNow;}
        glowHueFrame=NorthlightSunHue::compute(lightSlotsValid?lightSlots:nullptr,celestialPalette(map,camera).disc[1]);
        sourceDirections[0]=vec(context.lightDirection);sourceColors[0]=vec(context.direct);sourceGlowColors[0]=sourceColors[0];sourceGlowColors[1]=V();
        sourceDirections[1]=V(0,0,1);sourceColors[1]=V();sourceWeights[0]=1;sourceWeights[1]=0;
        // The sun and moon light (shadows, rays) from 6 deg at the lowest (MinLightElevation), its weight ending with the disc (discVisibility);
        // the discs and the twilight keep their real elevation.
        float sunLightDirection[3],moonLightDirection[3];std::memcpy(sunLightDirection,celestialLight.sun.direction,sizeof sunLightDirection);
        std::memcpy(moonLightDirection,celestialLight.moon.direction,sizeof moonLightDirection);
        NorthlightCelestial::clampLightElevation(sunLightDirection);NorthlightCelestial::clampLightElevation(moonLightDirection);
        auto resolvedSources=NorthlightCelestialSources::resolve(vec(context.lightDirection),vec(context.direct),
            celestialValid?vec(sunLightDirection):vec(context.lightDirection),
            celestialValid?vec(moonLightDirection):V(0,0,1),
            celestialValid?celestialLight.sunWeight:0.f,celestialValid?celestialLight.moonWeight:0.f);
        if(celestialValid){resolvedSources.sources[0].color=vec(celestialLight.sunColor);resolvedSources.sources[1].color=vec(celestialLight.moonColor);}
        authoredFill=resolvedSources.authoredFill;
        for(unsigned source=0;source<2;++source){rawSourceDirections[source]=resolvedSources.sources[source].direction;sourceDirections[source]=continuousCelestialShadows?resolvedSources.sources[source].direction:NorthlightWorldMath::quantizeDirection(resolvedSources.sources[source].direction,float(quality.shadowDirectionSteps));sourceColors[source]=resolvedSources.sources[source].color;sourceGlowColors[source]=sourceColors[source];sourceWeights[source]=resolvedSources.sources[source].weight;}
        if(celestialValid){sourceGlowColors[0]=vec(celestialLight.sunGlowColor);sourceGlowColors[1]=vec(celestialLight.moonGlowColor);}
        requestStaticCasters(map);
        Request r;r.map=map;r.camera=vec(camera);r.light.sunDirection=sourceDirections[0];r.light.sunIrradiance=sourceColors[0]*3.14159265f;r.light.skyRadiance=vec(context.ambient);r.light.maxBounces=quality.giBounces;r.giEnabled=quality.gi!=0;
        r.light.additionalDirections.push_back({sourceDirections[1],sourceColors[1]*3.14159265f});
        // 0.3.153 GIProbeAhead: centre the probe window ahead of the eye along the horizontal
        // view direction (the third-person camera sits behind the character). 0 = the eye.
        r.probeCenter=r.camera;
        if(quality.giProbeAhead){const V view=V(context.inverseView[8],context.inverseView[9],0)*projection[2];const float length=std::sqrt(NorthlightGI::dot(view,view));
            if(length>1e-3f)r.probeCenter=r.camera+view*(float(quality.giProbeAhead)/length);}
        // 0.3.169 geometry lead: centre the region build ahead along the travel velocity.
        {const V forward=V(context.inverseView[8],context.inverseView[9],context.inverseView[10])*projection[2];const float length=std::sqrt(NorthlightGI::dot(forward,forward));
         geometryMotion.update(r.map,r.camera,length>1e-6f?r.camera+forward*(pivotDistance/length):r.camera,GetTickCount());
         r.geometryCenter=NorthlightWorldStreaming::leadCenter(r.camera,geometryMotion.velocity(),float(geometryBuildEstimateMs.load(std::memory_order_relaxed))*.001f);
         geometryLead=std::sqrt(NorthlightGI::dot(r.geometryCenter-r.camera,r.geometryCenter-r.camera));}
        if(NorthlightQuality::giActorCapture(quality)&&lastRequest.map==r.map&&completedActorSceneMap()==r.map){r.actorJob=completedActorJob();r.actorSerial=actorJobSerial();}
        std::shared_ptr<Snapshot> retiredSnapshot,expiredPublication;
        {std::lock_guard<std::mutex> lock(mutex);
            // The publication is another owner of the old BVH/plan. Retiring only
            // active left it resident indefinitely while the builder waited for VA.
            // BEGIN EXPIRED PUBLICATION
            if(published&&published->bvh&&!NorthlightWorldStreaming::retained(published->map,published->center,r.map,r.camera))
                expiredPublication=std::move(published);
            // END EXPIRED PUBLICATION
            if(published&&published!=observedPublication.lock()){
                observedPublication=published;if(!published->message.empty()&&published->message!=loggedCacheMessage)logf("WORLD cache: %s",published->message.c_str());loggedCacheMessage=published->message;
            }
            if(published&&published!=active&&NorthlightWorldStreaming::adopts(published->map,published->center,bool(published->bvh),active&&active->bvh,r.map,r.camera)){
                bool newGI=published->serial&&(!active||active->serial!=published->serial);retiredSnapshot=std::move(active);active=published;
                if(newGI&&NorthlightDiagnostics::enabled())logf("GI activated tick=%lu id=%llu ageMs=%lu queueMs=%lu geometryMs=%lu actorMs=%lu solveMs=%lu reused=%u solved=%u cancelled=%u origin=(%.1f %.1f %.1f) dynamicReused=%u dynamicSolved=%u staticOnly=%u partial=%u processed=%u generation=%llu retargeted=%u actorTextureDecode=worker actorTextures=%u actorTextureBytes=%zu pathRecorded=%llu pathReplayedRays=%llu pathRetracedRays=%llu pathRecordKB=%zu",(unsigned long)GetTickCount(),(unsigned long long)active->requestId,(unsigned long)(GetTickCount()-active->requestedAt),(unsigned long)active->queueMs,(unsigned long)active->geometryMs,(unsigned long)active->actorMs,(unsigned long)active->solveMs,active->reusedProbes,active->solvedProbes,active->superseded,active->origin.x,active->origin.y,active->origin.z,active->dynamicReused,active->dynamicSolved,unsigned(active->staticOnly),unsigned(active->partial),active->processedProbes,(unsigned long long)active->lightingGeneration,active->retargeted,active->actorTexturesDecoded,active->actorTextureEncodedBytes,(unsigned long long)active->movingStats.recorded,(unsigned long long)active->movingStats.replayed,(unsigned long long)active->movingStats.retraced,active->pathRecordBytes/1024);
            }
            r.reason=(settingsRequestDirty?256u:0u)|(lastRequest.map!=r.map?1u:0u)|(different(quantize(lastRequest.probeCenter,float(quality.giProbeMoveStep)),quantize(r.probeCenter,float(quality.giProbeMoveStep)),.1f)?2u:0u)|
                (different(lastRequest.light.sunDirection,r.light.sunDirection,.02f)?4u:0u)|(different(lastRequest.light.sunIrradiance,r.light.sunIrradiance,.03f)?8u:0u)|(different(lastRequest.light.skyRadiance,r.light.skyRadiance,.01f)?16u:0u)|
                (lastRequest.light.additionalDirections.empty()||different(lastRequest.light.additionalDirections[0].direction,r.light.additionalDirections[0].direction,.02f)||different(lastRequest.light.additionalDirections[0].irradiance,r.light.additionalDirections[0].irradiance,.03f)?32u:0u)|
                (NorthlightWorldStreaming::leadMoved(lastRequest.camera,lastRequest.geometryCenter,r.camera,r.geometryCenter)?128u:0u);
            if(r.reason){r.id=request.id+1;r.baseId=r.id;r.queuedAt=GetTickCount();request=r;lastRequest=r;settingsRequestDirty=false;pending=true;wake.notify_one();
                if(NorthlightDiagnostics::enabled())logf("GI request tick=%lu id=%llu reason=%u camera=(%.2f %.2f %.2f) sun=(%.5f %.5f %.5f)",(unsigned long)r.queuedAt,(unsigned long long)r.id,r.reason,r.camera.x,r.camera.y,r.camera.z,r.light.sunDirection.x,r.light.sunDirection.y,r.light.sunDirection.z);
            }
            // 0.3.156 watchdog (log only): the builder waited for a free geometry generation
            // for GenerationStallLogMs; name the holders once per episode. Nothing is dropped.
            if(generationStall.due(GetTickCount(),GenerationStallLogMs))
                logf("WORLD geometry stalled ms=%lu pending=%u pendingMap=\"%s\" pendingActive=%u pendingPublished=%u active=%u activeId=%llu activeCenter=(%.1f %.1f %.1f) published=%u publishedId=%llu publishedMap=\"%s\" publishedCenter=(%.1f %.1f %.1f) publishedApplicable=%u camera=(%.1f %.1f %.1f) map=\"%s\"",
                    (unsigned long)(GetTickCount()-generationStall.since),unsigned(bool(pendingMesh)),pendingMesh?pendingMesh->map.c_str():"-",unsigned(pendingMesh&&active&&pendingMesh->bvh==active->bvh),unsigned(pendingMesh&&published&&pendingMesh->bvh==published->bvh),
                    unsigned(bool(active)),(unsigned long long)(active?active->requestId:0),active?active->center.x:0.f,active?active->center.y:0.f,active?active->center.z:0.f,
                    unsigned(bool(published)),(unsigned long long)(published?published->requestId:0),published?published->map.c_str():"-",published?published->center.x:0.f,published?published->center.y:0.f,published?published->center.z:0.f,
                    unsigned(published&&NorthlightWorldStreaming::applicable(published->map,published->center,r.map,r.camera)),r.camera.x,r.camera.y,r.camera.z,r.map.c_str());
            }
        {auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Retire);
         auto& reaper=NorthlightStreaming::cpuRetirement();retirementBacklog.retry(reaper);
         if(expiredPublication){
             logf("WORLD expired publication released map=\"%s\" cpuMiB=%.2f",expiredPublication->map.c_str(),double(expiredPublication->retirementBytes)/1048576);
             retirementBacklog.retireOrFree(reaper,expiredPublication,snapshotRetireBytes(*expiredPublication));
         }
         const bool adopted=retiredSnapshot&&active&&retiredSnapshot->bvh!=active->bvh; /* a new region, not a GI publication */
         if(retiredSnapshot)retirementBacklog.retireOrFree(reaper,retiredSnapshot,snapshotRetireBytes(*retiredSnapshot));
         retiredSnapshot.reset();
         // 0.3.169: hold the snapshot past its 96-unit coverage until a replacement applies (above)
         // or the hard limit or a map change retires it.
         const char* retired=active&&!NorthlightWorldStreaming::retained(active->map,active->center,r.map,r.camera)?(active->map!=r.map?"map":"retired"):nullptr;
         const V held=active?active->center:V();const bool heldDrawable=active&&active->bvh; /* issue #40: an uncached map's one error snapshot stays at the entry point */
         if(retired){expiredMeshMapChanged|=heldDrawable&&active->map!=r.map;retirementBacklog.retireOrFree(reaper,active,snapshotRetireBytes(*active));active.reset();expiredMeshPending|=heldDrawable;}
         releaseOrphanedPending("range");
         const float distance=active&&active->map==r.map?std::sqrt(NorthlightGI::dot(r.camera-active->center,r.camera-active->center)):0.f;
         if(distance>coverMax)coverMax=distance;
         const bool holding=active&&active->bvh&&!NorthlightWorldStreaming::applicable(active->map,active->center,r.map,r.camera); /* only drawable geometry is held */
         const bool logHold=holdEpisodes<=32||NorthlightDiagnostics::enabled();
         if((retired&&(heldDrawable||coverageHold))||(coverageHold&&(adopted||!holding))){
             if(!coverageHold){++holdEpisodes;holdStart=GetTickCount();holdMax=0;} /* retired in one step: a teleport or a map change */
             if(retired)holdMax=std::max(holdMax,std::sqrt(NorthlightGI::dot(r.camera-held,r.camera-held)));
             if(logHold)logf("WORLD coverage hold end ms=%lu maxDist=%.1f reason=%s",(unsigned long)(GetTickCount()-holdStart),holdMax,retired?retired:adopted?"adopted":"returned");
             coverageHold=false;
         }
         if(!coverageHold&&holding){coverageHold=true;++holdEpisodes;holdStart=GetTickCount();holdMax=distance;
             if(holdEpisodes<=32||NorthlightDiagnostics::enabled())logf("WORLD coverage hold begin dist=%.1f eye=(%.1f %.1f %.1f) centre=(%.1f %.1f %.1f) fwd=(%.3f %.3f %.3f) pivotDistance=%.1f lead=%.1f",distance,r.camera.x,r.camera.y,r.camera.z,active->center.x,active->center.y,active->center.z,
                 context.inverseView[8]*projection[2],context.inverseView[9]*projection[2],context.inverseView[10]*projection[2],pivotDistance,geometryLead);
         }else if(coverageHold)holdMax=std::max(holdMax,distance);}
        DWORD now=GetTickCount();if(NorthlightDiagnostics::enabled()&&now-diagnosticTick>=250){diagnosticTick=now;
            logf("WORLD camera tick=%lu rendered=%u eye=(%.2f %.2f %.2f) forward=(%.4f %.4f %.4f) sun=(%.5f %.5f %.5f) GI=%llu pivotDistance=%.1f pivotUpdates=%u coverMax=%.1f lead=%.1f pivotSource=%s snapCorrections=%u selfDistance=%.1f radiusSelf=%d nearBlendAtSelf=%.2f",(unsigned long)now,frames,context.camera[0],context.camera[1],context.camera[2],context.inverseView[8]*projection[2],context.inverseView[9]*projection[2],context.inverseView[10]*projection[2],context.lightDirection[0],context.lightDirection[1],context.lightDirection[2],(unsigned long long)(active?active->serial:0),pivotDistance,pivotUpdates,coverMax,geometryLead,
                NorthlightShadowPivot::name(pivotCorrection.source),pivotCorrection.snapCorrections,pivotCorrection.selfDistance,int(pivotCorrection.hasSelf),pivotCorrection.nearBlendAtSelf(sourceMatrices[0][0])); /* 0.3.190: the near matrix the shader gets (source 0) */
            coverMax=0;
        }
    }
    IDirect3DPixelShader9* terrainShadowReplacement(IDirect3DPixelShader9* shader){
        auto found=terrainShadowShaders.find(shader);if(found!=terrainShadowShaders.end())return found->second;
        IDirect3DPixelShader9* replacement=nullptr;UINT size=0;
        if(shader&&SUCCEEDED(shader->GetFunction(nullptr,&size))&&size>=8&&size<=65536&&size%4==0){
            std::vector<uint32_t> code(size/4),output;NorthlightTerrainShadow::PatchInfo info;
            if(SUCCEEDED(shader->GetFunction(code.data(),&size))&&NorthlightTerrainShadow::patch(code.data(),code.size(),output,&info)){
                if(FAILED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(output.data()),&replacement)))replacement=nullptr;
                if(replacement&&terrainShadowReports++<4)logf("TERRAIN SHADOW patched ps_%u_%u: replaced=%u shadow=r%u.%u one=c%d.%u instructions=%u",info.major,info.minor,info.replaced,info.shadowRegister,info.shadowComponent,info.oneConstant,info.oneComponent,info.instructions);
            }
        }
        if(replacement)++terrainShadowPatched;else ++terrainShadowRejected;
        terrainShadowShaders[shader]=replacement;return replacement;
    }
    bool terrainShadowCoverage(IDirect3DVertexShader9* vertex,IDirect3DPixelShader9* pixel,
                               IDirect3DVertexShader9*& patchedVertex,IDirect3DPixelShader9*& patchedPixel,
                               unsigned& constants,unsigned& constantCount,float* rows){
        auto v=terrainCoverageVertices.find(vertex);
        if(v==terrainCoverageVertices.end()){
            IDirect3DVertexShader9* shader=nullptr;UINT size=0;std::vector<uint32_t> code,output;
            if(vertex&&SUCCEEDED(vertex->GetFunction(nullptr,&size))&&size>=8&&size<=65536&&size%4==0){
                code.resize(size/4);
                if(SUCCEEDED(vertex->GetFunction(code.data(),&size))&&NorthlightTerrainCoverage::vertex(code.data(),code.size(),output))
                    if(FAILED(d->CreateVertexShader(reinterpret_cast<const DWORD*>(output.data()),&shader)))shader=nullptr;
            }
            v=terrainCoverageVertices.emplace(vertex,shader).first;
        }
        if(!v->second)return false;
        auto& result=terrainCoveragePixels[pixel][terrainCoverageSources-1];
        if(!result.known){
            result.known=true;UINT size=0;std::vector<uint32_t> code,output;
            NorthlightTerrainCoverage::PixelInfo proof;
            if(pixel&&SUCCEEDED(pixel->GetFunction(nullptr,&size))&&size>=8&&size<=65536&&size%4==0){
                code.resize(size/4);
                if(SUCCEEDED(pixel->GetFunction(code.data(),&size))&&NorthlightTerrainCoverage::pixel(code.data(),code.size(),output,proof,terrainCoverageSources)){
                    if(FAILED(d->CreatePixelShader(reinterpret_cast<const DWORD*>(output.data()),&result.shader)))result.shader=nullptr;
                    result.constants=proof.constants;
                }
            }
            if(terrainCoverageReports++<8)logf("TERRAIN SHADOW farCoverage=%u sources=%u psConstants=%u addedSlots=%u",unsigned(result.shader!=nullptr),terrainCoverageSources,result.constants,proof.addedSlots);
        }
        if(!result.shader)return false;
        if(!terrainCoverageRowsValid||std::memcmp(terrainCoverageView,context.inverseView,64)){
            for(unsigned source=0;source<terrainCoverageSources;++source){float projected[16];NorthlightWorldMath::replayProjection(context.inverseView,terrainCoverageMatrices[source],projected);std::memcpy(terrainCoverageRows+source*12,projected,48);}
            std::memcpy(terrainCoverageView,context.inverseView,64);terrainCoverageRowsValid=true;
        }
        patchedVertex=v->second;patchedPixel=result.shader;constants=result.constants;constantCount=terrainCoverageSources*3;
        std::memcpy(rows,terrainCoverageRows,constantCount*16);return true;
    }
    bool terrainShadowActive()const{return effects.shadows&&ready()&&shadowsComposited;}
    bool shadowsRequested()const{return effects.shadows;} /* 0.3.187: changes only in setEffects (frame boundary) */
    void registerPixelShader(IDirect3DPixelShader9* shader){
        fogShaders.erase(shader);UINT size=0;
        {auto old=terrainShadowShaders.find(shader);if(old!=terrainShadowShaders.end()){drop(old->second);terrainShadowShaders.erase(old);}}
        {auto old=terrainCoveragePixels.find(shader);if(old!=terrainCoveragePixels.end()){for(auto& variant:old->second)drop(variant.shader);terrainCoveragePixels.erase(old);}}
        if(!shader||FAILED(shader->GetFunction(nullptr,&size))||size<8||size>65536||size%4)return;
        std::vector<uint32_t> code(size/4);if(FAILED(shader->GetFunction(code.data(),&size)))return;
        FogShader info;info.major=(code[0]>>8)&255;
        info.colorRegister=NorthlightLegacyFog::ps3FogColorRegister(code.data(),code.size());info.verified=info.colorRegister>=0;fogShaders[shader]=info;
    }
    void registerShader(IDirect3DVertexShader9* shader,uint64_t hash){
        {auto old=terrainCoverageVertices.find(shader);if(old!=terrainCoverageVertices.end()){drop(old->second);terrainCoverageVertices.erase(old);}}
        ++worldShaderGen; /* 0.3.196 (task 12): before any map changes */
        replayBoundsMetadata.invalidateShader(shader);
        wmoShaders.erase(shader);if(auto* info=NorthlightWmoContext::signature(hash))wmoShaders[shader]=info;
        terrainShaders.erase(shader);if(contains(kTerrainVS,hash))terrainShaders.insert(shader);
        auto begin=std::begin(kWorldShaderSignatures),end=std::end(kWorldShaderSignatures);
        auto it=std::lower_bound(begin,end,hash,[](const WorldShaderSignature& a,uint64_t h){return a.hash<h;});
        auto old=captureShaders.find(shader);if(old!=captureShaders.end()){drop(old->second.replacement);retireProgram(std::move(old->second.program));captureShaders.erase(old);}
        {auto program=actorPrograms.find(shader); /* 0.3.177: the caches are cleared by their owner at its next open */
         if(program!=actorPrograms.end()){prepareCachesStale=true;retireProgram(std::move(program->second));actorPrograms.erase(program);}}
        actorUVPrograms.erase(shader);
        if(it==end||it->hash!=hash)return;
        unsigned kind=contains(kTerrainVS,hash)?1:it->projectionKind;
        if(kind==1)return; // Terrain uses an immediate position snapshot, including SM1.
        UINT size=0;if(FAILED(shader->GetFunction(nullptr,&size))||size>65536)return;
        std::vector<uint32_t> words(size/4),output;
        if(FAILED(shader->GetFunction(words.data(),&size)))return;
        NorthlightShadowShader::PatchInfo info;
        if(!NorthlightShadowShader::patch(words.data(),words.size(),output,&info)||!info.texcoord0XY)return;
        NorthlightDrawSnapshot::Ref<IDirect3DVertexShader9> replacement;
        if(SUCCEEDED(d->CreateVertexShader(reinterpret_cast<const DWORD*>(output.data()),replacement.out()))&&replacement.p){
            CaptureShader metadata;metadata.replacement=replacement.p;metadata.projectionKind=kind;
            metadata.usage=NorthlightShaderConstants::analyze(words.data(),words.size(),kind);
            metadata.fog=NorthlightSpellFog::vsFogProven(words.data(),words.size());
            NorthlightActorDeformation::Program program;
            if(NorthlightActorDeformation::compile(words.data(),words.size(),program)){
                metadata.skinned=program.skinned;metadata.sm1=program.major==1;
                metadata.program=std::make_shared<const NorthlightActorDeformation::Program>(std::move(program));
                actorPrograms.emplace(shader,metadata.program);
            }
            if(NorthlightActorDeformation::compile(words.data(),words.size(),program,true))actorUVPrograms.emplace(shader,std::move(program));
            // Publish only complete metadata. RAII retains ownership on any
            // compiler/map allocation exception before this transfer.
            captureShaders.emplace(shader,metadata);replacement.p=nullptr;
        }
    }
    void appendTerrain(TerrainSnapshot snapshot){
        bool fresh=false;for(auto& chunk:snapshot->bounds.chunks)if(!liveTerrainChunks.count({chunk.x,chunk.y}))fresh=true;
        if(!fresh)return;
        if(frameTerrainVertices+snapshot->positions.size()>262144||frameTerrainIndices+snapshot->indices.size()>1572864)return;
        pointRecordTerrain(UINT(frameTerrainIndices),UINT(snapshot->indices.size()/3),snapshot->bounds);
        for(auto& chunk:snapshot->bounds.chunks)liveTerrainChunks.emplace(chunk.x,chunk.y);
        frameTerrainVertices+=snapshot->positions.size();frameTerrainIndices+=snapshot->indices.size();
        frameTerrain.push_back(std::move(snapshot));++terrainSnapshots;
    }
    void captureUP(D3DPRIMITIVETYPE type,UINT minimum,UINT vertexTotal,UINT count,const void* indexData,D3DFORMAT format,const void* vertexData,UINT stride,bool indexed,IDirect3DVertexShader9* shader,bool selfCheck,bool sample){
        constantSelfCheck=selfCheck;sample=sample&&NorthlightDiagnostics::enabled();
        if(!ready())return;
        if(!lookupShader(shader).terrain){captureSampled|=sample;captureModel(type,0,minimum,vertexTotal,0,count,indexed,shader,sample,indexData,format,vertexData,stride);return;}
        captureSampled|=sample;if(sample){++terrainCaptureCalls;++terrainUPCalls;}CpuScope cpu(sample?&terrainCaptureTicks:nullptr);
        ++terrainAttempts;NorthlightTerrainCapture::MeshSnapshot snapshot;NorthlightTerrainCapture::Diagnostics why;
        bool ok=indexed?terrainBoundsCache.readMeshUP(d,type,minimum,vertexTotal,count,indexData,format,vertexData,stride,context.view,snapshot,&why):
                        terrainBoundsCache.readPrimitiveUP(d,type,count,vertexData,stride,context.view,snapshot,&why);
        if(!ok){if(terrainFailures++<12)logf("TERRAIN UP snapshot rejected: %s hr=%08lx type=%u stride=%u",NorthlightTerrainCapture::rejectName(why.reason),(unsigned long)why.hr,why.positionType,why.stride);return;}
        appendTerrain(std::make_shared<const NorthlightTerrainCapture::MeshSnapshot>(std::move(snapshot)));
    }
    void capture(D3DPRIMITIVETYPE type,INT base,UINT min,UINT vertexTotal,UINT start,UINT count,bool indexed,IDirect3DVertexShader9* current,bool selfCheck,bool sample){
        constantSelfCheck=selfCheck;sample=sample&&NorthlightDiagnostics::enabled();
        if(!ready()||replays.size()>=4096||(type!=D3DPT_TRIANGLELIST&&type!=D3DPT_TRIANGLESTRIP))return;
        captureSampled|=sample;bool isTerrain=lookupShader(current).terrain;
        if(isTerrain){
            if(sample)++terrainCaptureCalls;CpuScope cpu(sample?&terrainCaptureTicks:nullptr);
            ++terrainAttempts;
            if(!indexed){if(terrainFailures++<12)logf("TERRAIN snapshot rejected: non-indexed buffer draw");return;}
            float projectionRows[16];if(FAILED(d->GetVertexShaderConstantF(4,projectionRows,4))){if(terrainFailures++<12)logf("TERRAIN projection constants unavailable");return;}
            if(std::fabs(projectionRows[0]-projection[0])>.005f||std::fabs(projectionRows[5]-projection[1])>.005f||std::fabs(projectionRows[11]-projection[2])>.005f){if(terrainFailures++<12)logf("TERRAIN projection mismatch");return;}
            TerrainSnapshot snapshot;NorthlightTerrainCapture::Diagnostics why;
            if(!terrainBoundsCache.readMeshShared(d,type,base,min,vertexTotal,start,count,context.view,snapshot,&why)){
                if(terrainFailures++<12)logf("TERRAIN snapshot rejected: %s hr=%08lx vertexUsage=%lx indexUsage=%lx type=%u stride=%u",NorthlightTerrainCapture::rejectName(why.reason),(unsigned long)why.hr,(unsigned long)why.vertexUsage,(unsigned long)why.indexUsage,why.positionType,why.stride);return;}
            appendTerrain(snapshot);return;
        }
        captureModel(type,base,min,vertexTotal,start,count,indexed,current,sample,nullptr,D3DFMT_INDEX16,nullptr,0);
    }
    void releaseReplayGPU(){replayGpuCache.clear();auto& queries=fence();for(auto& ring:replayVertexRing)NorthlightDynamicRing::reset(ring,0,queries);NorthlightDynamicRing::reset(replayIndexRing,0,queries);queries.drop();for(auto& buffer:replayVerticesGPU)drop(buffer);drop(replayIndicesGPU);for(auto& bytes:replayVertexBytes)bytes=0;replayIndexBytes=0;}
    bool uploadReplay(bool allowCache=true){
        NorthlightCapturePhases::Scope<2> phase(captureSampled?&replayUploadPhases:nullptr);
        if(captureSampled)++replayUploadCalls;
        if(!allowCache){replayGpuCache.clear();for(auto& p:replays)if(p->gpuCached){for(auto& stream:p->stream)drop(stream);drop(p->index);}}
        {auto expire=streamingPhases.measure(NorthlightStreaming::PhaseProfile::ReplayExpire);replayGpuCache.beginFrame();}bool memoryChecked=false,memoryAllowed=false;
        auto growthAdmission=[&](size_t){if(!memoryChecked){memoryChecked=true;memoryAllowed=admitsGrowth("replay-cache",64*NorthlightGeometryMemory::MiB);}return memoryAllowed;};
        auto bindReplay=[&](Replay& p){NorthlightReplayGPU::bindResident(replayGpuCache,d,p,allowCache,growthAdmission);};
        replayTimeDeferredIndices.clear();
        {auto bind=streamingPhases.measure(NorthlightStreaming::PhaseProfile::ReplayBind);
        for(size_t i=0;i<replays.size();++i){bindReplay(*replays[i]);
            if(!replays[i]->gpuCached&&replayGpuCache.timeDeferredLast())replayTimeDeferredIndices.push_back(i);}
        NorthlightReplayGPU::commitAdmissions(replayGpuCache,replays,bindReplay);}
        phase.next(1); // Layout, bulk-buffer growth/copy and output bindings.
        UINT totals[4]={};std::uint64_t totalIndices=0;size_t sharedBulkDraws=0,sharedBulkBytes=0,multiStreamDraws=0;
        /* 0.3.211 (task 31): a single-stream bulk replay sits at a stride multiple (rel) so every one binds the ring slot start
           and selects its vertices by BaseVertexIndex/start: same-stride replays share one SetStreamSource. Layout and the
           address-space prefix fit both go through bulkPlace, so they stay byte-identical. Multi-stream: 16-padded, as before. */
        auto singleStream=[](const auto& p){const auto& m=p.mesh();return !m.streams[0].bytes.empty()&&m.streams[0].stride&&m.streams[1].bytes.empty()&&m.streams[2].bytes.empty()&&m.streams[3].bytes.empty();};
        auto bulkPlace=[&](const auto& p,bool single,unsigned s,std::uint64_t total,std::uint64_t& at){ /* single: singleStream(p), computed once per replay */
            const auto& source=p.mesh().streams[s];
            if(s==0&&single){at=(total+source.stride-1)/source.stride*source.stride;return at+source.bytes.size();}
            at=(total+15)&~std::uint64_t(15);return at+((source.bytes.size()+15)&~std::size_t(15));};
        auto layout=[&](size_t count){
            for(auto& t:totals)t=0;totalIndices=0;sharedBulkDraws=sharedBulkBytes=multiStreamDraws=0;replayBulkLayout.begin();
            for(size_t i=0;i<count;++i){auto& p=replays[i];if(p->gpuCached)continue;
                const size_t owner=replayBulkLayout.add(p->shared.get(),i);
                if(owner!=i){const auto& first=*replays[owner];
                    for(unsigned s=0;s<4;++s){p->stride[s]=first.stride[s];p->offset[s]=first.offset[s];}
                    p->start=first.start;p->base=first.base;p->bulkSingle=first.bulkSingle;++sharedBulkDraws;sharedBulkBytes+=p->mesh().byteSize();continue;
                }
                const bool single=p->bulkSingle=singleStream(*p);
                for(unsigned s=0;s<4;++s){auto& source=p->mesh().streams[s];p->stride[s]=source.stride;
                    std::uint64_t at=0;const std::uint64_t next=bulkPlace(*p,single,s,totals[s],at);if(next>64u*1024u*1024u)return false;p->offset[s]=UINT(at);totals[s]=UINT(next);}
                p->start=p->indexed?UINT(totalIndices):0;p->base=0;totalIndices+=p->mesh().indices.size();
                if(single){const UINT first=p->offset[0]/p->stride[0];if(p->indexed)p->base=INT(first);else p->start=first;} // offset[0] is still relative here
                else multiStreamDraws+=!p->mesh().streams[1].bytes.empty()||!p->mesh().streams[2].bytes.empty()||!p->mesh().streams[3].bytes.empty();}
            return totalIndices<=16u*1024u*1024u;
        };
        // A creation-time deferral must never force bulk growth (or exceed the
        // bulk limits): then create those meshes exactly as 0.3.137 would.
        {const bool fits=layout(replays.size());bool grow=!fits||totalIndices*4>replayIndexBytes;
         for(unsigned s=0;s<4;++s)grow=grow||totals[s]>replayVertexBytes[s];
         if(grow&&!replayTimeDeferredIndices.empty()){
             replayGpuCache.ignoreTimeBudget(true);
             for(size_t i:replayTimeDeferredIndices){bindReplay(*replays[i]);replayBudgetOverrides+=replays[i]->gpuCached;}
             replayGpuCache.ignoreTimeBudget(false);
         }
         if(!fits||(grow&&!replayTimeDeferredIndices.empty())){if(!layout(replays.size()))return false;}}
        {const auto& cache=replayGpuCache.stats();streamingPhases.record(NorthlightStreaming::PhaseProfile::ReplayCreate,cache.createMs);
         replayCreatedPeak=std::max(replayCreatedPeak,cache.created);replayTimeDeferred+=cache.timeDeferred;}
        {bool grow=totalIndices*4>replayIndexBytes;uint64_t capacity=0;
         for(unsigned s=0;s<4;++s){if(totals[s]>replayVertexBytes[s])grow=true;capacity+=totals[s]>replayVertexBytes[s]?NorthlightDynamicRing::ringCapacity(totals[s]):replayVertexBytes[s];}
         capacity+=totalIndices*4>replayIndexBytes?NorthlightDynamicRing::ringCapacity(totalIndices*4):replayIndexBytes;
         if(grow&&!admitsGrowth("replay-growth",capacity)){
             // Optional residency must not displace casters that the original
             // bulk path could fit. Reclaim it before applying the old fallback.
             if(allowCache&&replayGpuCache.bytes()){
                 if(captureSampled)++replayUploadFallbacks;
                 ++replayFallbacks;
                 phase.stop(); // The recursive retry owns its own interval.
                 return uploadReplay(false);
             }
             // Under address-space pressure draw the prefix of casters that fits
             // the existing buffers this frame; nothing is allocated.
             size_t fit=0;UINT running[4]={};std::uint64_t runningIndices=0;
             for(;fit<replays.size();++fit){auto& p=replays[fit];if(p->gpuCached||!replayBulkLayout.unique(fit))continue;bool ok=true;std::uint64_t nexts[4]={};const bool single=singleStream(*p);
                 for(unsigned s=0;s<4&&ok;++s){std::uint64_t at=0;nexts[s]=bulkPlace(*p,single,s,running[s],at);ok=nexts[s]<=replayVertexBytes[s];}
                 if(!ok||(runningIndices+p->mesh().indices.size())*4>replayIndexBytes)break;
                 for(unsigned s=0;s<4;++s)running[s]=UINT(nexts[s]);runningIndices+=p->mesh().indices.size();}
             if(prepareUnsettled())for(size_t i=fit;i<replays.size();++i)recycleReplay(replays[i].release()); /* 0.3.177: quarantined */
             replays.resize(fit);if(!layout(fit))return false;
         }}
        // 0.3.192 (DXVK3): each bulk buffer is a fence-checked ring (dynamic_ring.h): this call's streams are appended at
        // slot.offset (NOOVERWRITE; DISCARD only while an earlier frame's draws may still read the slice), so a frame
        // is no longer charged the full 8 MiB buffer as a DXVK 3.x DISCARD. layout() stays relative to 0 and the
        // ring base is added to the replays once, right before the bindings.
        auto& queries=fence();UINT vertexBase[4]={},indexBase=0;
        auto ringSlot=[&](NorthlightDynamicRing::Ring& ring,UINT& capacity,UINT bytes,UINT align,NorthlightLockMeter::Site site,auto&& recreate,NorthlightDynamicRing::Slot& slot){
            if(capacity<bytes){auto grow=streamingPhases.measure(NorthlightStreaming::PhaseProfile::ReplayGrow);++replayGrowths;
                const UINT bigger=NorthlightDynamicRing::ringCapacity(bytes);NorthlightDynamicRing::reset(ring,0,queries);capacity=0;
                if(!recreate(bigger))return false;capacity=bigger;NorthlightDynamicRing::reset(ring,bigger,queries);}
            slot=NorthlightDynamicRing::place(ring,bytes,align,queries);
            if(slot.discarded&&NorthlightDynamicRing::oversized(ring,ring.peak)){ // would DISCARD an oversized buffer: a right-sized new one charges nothing
                const UINT smaller=NorthlightDynamicRing::ringCapacity(ring.peak);NorthlightDynamicRing::reset(ring,0,queries);capacity=0;
                if(!recreate(smaller))return false;capacity=smaller;NorthlightDynamicRing::reset(ring,smaller,queries);
                slot=NorthlightDynamicRing::place(ring,bytes,align,queries);NorthlightLockMeter::ringShrink();}
            if(slot.wrapped)NorthlightLockMeter::ringWrap(slot.fenceReuse,slot.discarded);
            if(slot.discarded)NorthlightLockMeter::discard(site,capacity);
            return true;};
        for(unsigned s=0;s<4;++s){if(!totals[s])continue;
            NorthlightDynamicRing::Slot slot;
            if(!ringSlot(replayVertexRing[s],replayVertexBytes[s],totals[s],16,NorthlightLockMeter::ReplayVB,[&](UINT capacity){drop(replayVerticesGPU[s]);
                    return check(d->CreateVertexBuffer(capacity,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&replayVerticesGPU[s],nullptr),"model snapshot vertex buffer");},slot))return false;
            auto copy=streamingPhases.measure(NorthlightStreaming::PhaseProfile::ReplayCopy);
            void* memory=nullptr;if(!check(replayVerticesGPU[s]->Lock(slot.offset,totals[s],&memory,slot.flags),"model snapshot vertex lock"))return false;
            for(size_t i=0;i<replays.size();++i){auto& p=replays[i];if(p->gpuCached||!replayBulkLayout.unique(i))continue;auto& source=p->mesh().streams[s];if(!source.bytes.empty())std::memcpy(static_cast<std::uint8_t*>(memory)+p->offset[s],source.bytes.data(),source.bytes.size());}
            if(!check(replayVerticesGPU[s]->Unlock(),"model snapshot vertex unlock"))return false;
            vertexBase[s]=slot.offset;
        }
        UINT indexBytes=UINT(totalIndices*4);
        if(indexBytes){NorthlightDynamicRing::Slot slot;
            if(!ringSlot(replayIndexRing,replayIndexBytes,indexBytes,4,NorthlightLockMeter::ReplayIB,[&](UINT capacity){drop(replayIndicesGPU);
                    return check(d->CreateIndexBuffer(capacity,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&replayIndicesGPU,nullptr),"model snapshot index buffer");},slot))return false;
            auto copy=streamingPhases.measure(NorthlightStreaming::PhaseProfile::ReplayCopy);
            void* memory=nullptr;if(!check(replayIndicesGPU->Lock(slot.offset,indexBytes,&memory,slot.flags),"model snapshot index lock"))return false;
            for(size_t i=0;i<replays.size();++i){auto& p=replays[i];if(!p->gpuCached&&p->indexed&&replayBulkLayout.unique(i)&&!p->mesh().indices.empty()) /* 0.3.211 (task 31): a non-indexed start is now its first vertex */ std::memcpy(static_cast<std::uint32_t*>(memory)+p->start,p->mesh().indices.data(),p->mesh().indices.size()*4);}
            if(!check(replayIndicesGPU->Unlock(),"model snapshot index unlock"))return false;
            indexBase=slot.offset/4;
        }
        // The copies are done: from here on offset/start are absolute in the ring buffers (shared duplicates included;
        // gpuCached replays carry their own batch offsets and are never touched).
        for(auto& p:replays){if(p->gpuCached)continue;const bool single=p->bulkSingle; /* single-stream (laid out above): the bind offset is the slot start, rel went into base/start */
            for(unsigned s=0;s<4;++s)if(!p->mesh().streams[s].bytes.empty())p->offset[s]=single?vertexBase[s]:p->offset[s]+vertexBase[s];if(p->indexed)p->start+=indexBase;}
        // 0.3.176 (U3'): a binding already held keeps its reference (no Release/AddRef pair).
        for(auto& p:replays){if(p->gpuCached)continue;for(unsigned s=0;s<4;++s){IDirect3DVertexBuffer9* const stream=p->mesh().streams[s].bytes.empty()?nullptr:replayVerticesGPU[s];
                if(p->stream[s]!=stream){drop(p->stream[s]);if(stream){p->stream[s]=stream;stream->AddRef();}}}
            IDirect3DIndexBuffer9* const index=p->indexed?replayIndicesGPU:nullptr;if(p->index!=index){drop(p->index);if(index){p->index=index;index->AddRef();}}}
        phase.stop(); // Report I/O is not part of cache/bulk timing.
        if(captureSampled){size_t bulk=indexBytes;for(auto total:totals)bulk+=total;
            const auto residency=NorthlightReplayGPU::batchStats(replayGpuCache);
            deferLogf("MODEL GPU cache hits=%u reusedBytes=%zu newBytes=%zu residentBytes=%zu bulkUploadBytes=%zu batches=%zu batchBytes=%zu liveBytes=%zu separateBytes=%zu batchedUploads=%u separateUploads=%u compactions=%u compactedBytes=%zu batchFailures=%u",replayGpuCache.hits(),replayGpuCache.reused(),replayGpuCache.uploaded(),replayGpuCache.bytes(),bulk,
                residency.batches,residency.batchBytes,residency.liveBytes,residency.separateBytes,residency.batchedUploads,residency.separateUploads,residency.compactions,residency.compactedBytes,residency.batchFailures);
            deferLogf("MODEL bulk sharing duplicateDraws=%zu avoidedBytes=%zu actualBytes=%zu multiStreamDraws=%zu",sharedBulkDraws,sharedBulkBytes,bulk,multiStreamDraws);
            const auto population=replayGpuCache.population();const auto& policy=replayGpuCache.stats();
            deferLogf("MODEL GPU policy entries=%zu resident=%zu probation=%zu countPressure=%u bytePressure=%u roomRejected=%u promotionCountOnly=%u promotionCountRejected=%u evictions=%u warmup=%u uploadDeferred=%u admissionRejected=%u entryLimit=%zu scanPasses=%u scanVisits=%u scanMemoHits=%u attemptDeferred=%u uploadByteDeferred=%u expiredEntries=%u expiredBytes=%zu evictionChecks=%u lruMoves=%u",population.entries,population.resident,population.probation,policy.countPressure,policy.bytePressure,policy.roomRejected,policy.promotionCountOnly,policy.promotionCountRejected,policy.evictions,policy.warmup,policy.uploadDeferred,policy.admissionRejected,replayGpuCache.entryLimit(),policy.scanPasses,policy.scanVisits,policy.scanMemoHits,policy.attemptDeferred,policy.uploadByteDeferred,policy.expiredEntries,policy.expiredBytes,policy.evictionChecks,policy.lruMoves);
            const auto& cleared=replayGpuCache.clearStats();
            deferLogf("MODEL GPU clears lifetimeCalls=%llu lifetimeEntries=%llu lifetimeBytes=%llu",(unsigned long long)cleared.calls,(unsigned long long)cleared.entries,(unsigned long long)cleared.bytes);
        }
        return true;
    }
    bool actorMaterial(IDirect3DBaseTexture9* base,NorthlightActorTexture::Snapshot& snapshot){
        if(!base||base->GetType()!=D3DRTYPE_TEXTURE)return false;
        auto* texture=static_cast<IDirect3DTexture9*>(base);UINT levels=texture->GetLevelCount();if(!levels)return false;
        D3DSURFACE_DESC desc={};UINT level=0;
        for(;level<levels;++level){if(FAILED(texture->GetLevelDesc(level,&desc)))return false;if(desc.Width<=128&&desc.Height<=128)break;}
        if(level==levels||!desc.Width||!desc.Height)return false;
        using Format=NorthlightActorTexture::Format;Format format;
        switch(desc.Format){case D3DFMT_A8R8G8B8:format=Format::BGRA8;break;case D3DFMT_X8R8G8B8:format=Format::BGRX8;break;case D3DFMT_R5G6B5:format=Format::RGB565;break;
        case D3DFMT_DXT1:format=Format::BC1;break;case D3DFMT_DXT3:format=Format::BC2;break;case D3DFMT_DXT5:format=Format::BC3;break;
        case D3DFMT_A1R5G5B5:format=Format::ARGB1555;break;case D3DFMT_X1R5G5B5:format=Format::XRGB1555;break;case D3DFMT_A4R4G4B4:format=Format::ARGB4444;break;default:return false;}
        if(desc.Usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL))return false;
        UINT rows=NorthlightActorTexture::rowCount(desc.Height,format);
        UINT rowBytes=UINT(NorthlightActorTexture::rowBytes(desc.Width,format));
        std::vector<std::uint8_t> raw(std::size_t(rows)*rowBytes); // Allocate before taking a game texture lock.
        D3DLOCKED_RECT locked={};if(FAILED(texture->LockRect(level,&locked,nullptr,D3DLOCK_READONLY)))return false;
        bool readable=locked.pBits&&locked.Pitch>=INT(rowBytes);
        if(readable)for(UINT row=0;row<rows;++row)std::memcpy(raw.data()+std::size_t(row)*rowBytes,static_cast<const std::uint8_t*>(locked.pBits)+std::size_t(row)*unsigned(locked.Pitch),rowBytes);
        HRESULT hr=texture->UnlockRect(level);if(FAILED(hr)||!readable)return false;
        if(!NorthlightActorTexture::readable(raw.data(),raw.size(),desc.Width,desc.Height,rowBytes,format))return false;
        snapshot.bytes=std::move(raw);snapshot.width=desc.Width;snapshot.height=desc.Height;snapshot.pitch=rowBytes;snapshot.format=format;return true;
    }
    bool actorCaptureEnabled(){if(!NorthlightQuality::giActorCapture(quality))return false; /* GI=0 or GIDynamicProbes=0 */
        if(!actorCaptureDecided){actorCaptureDecided=true;actorCaptureDue=!lastActorCapture||GetTickCount()-lastActorCapture>=200;}if(actorCaptureDue&&!actorJob)actorJob=std::make_shared<NorthlightActorGeometry::ActorJob>();return actorCaptureDue;}
    void appendActor(IDirect3DVertexShader9* original,const Replay& replay,bool sample=false){
        if(!actorCaptureEnabled())return;
        auto it=actorPrograms.find(original);if(it==actorPrograms.end())return;const auto& program=*it->second;
        if(!program.skinned&&!replay.mesh().dynamic)return;
        if(actorJob->packets.size()>=128)return;
        if(actorVerticesEvaluated+replay.mesh().vertexCount>16384)return;
        NorthlightCapturePhases::Scope<3> phase(sample?&actorPhases:nullptr);
        if(sample)++actorPhaseCandidates;
        const D3DVERTEXELEMENT9* elements=nullptr;UINT n=0;if(!declarationCache.get(replay.decl,elements,n))return;
        // Evaluate one vertex first to avoid skinning distant crowds. 96 units
        // allows large models intersecting the exact 48-unit triangle region.
        NorthlightDrawSnapshot::Mesh first;first.vertexCount=1;
        for(unsigned s=0;s<4;++s){first.streams[s].stride=replay.mesh().streams[s].stride;if(!replay.mesh().streams[s].bytes.empty())first.streams[s].bytes.assign(replay.mesh().streams[s].bytes.begin(),replay.mesh().streams[s].bytes.begin()+first.streams[s].stride);}
        std::vector<NorthlightActorDeformation::Position> positions;
        if(!NorthlightActorDeformation::worldPositions(program,first,elements,n,replay.constants,context.inverseView,positions))return;
        V firstPosition(positions[0].x,positions[0].y,positions[0].z);V distance=firstPosition-vec(context.camera);if(NorthlightGI::dot(distance,distance)>96*96)return;
        actorVerticesEvaluated+=replay.mesh().vertexCount;
        phase.next(1); // Copy current game texture; pure decode runs on the GI worker.
        NorthlightGI::WorldMaterial material;material.albedo=V(.35f,.35f,.35f);material.alphaCutoff=std::max(0.f,replay.cutoff);
        NorthlightActorTexture::Snapshot texture;
        auto uvProgram=actorUVPrograms.find(original);bool hasUV=uvProgram!=actorUVPrograms.end();
        if(sample&&hasUV)++actorTextureReads;
        bool textureRead=hasUV&&actorMaterial(replay.texture,texture);
        if(replay.cutoff>=0&&!textureRead){++actorSkippedAlpha;return;}
        material.addressU=replay.addressU;material.addressV=replay.addressV;
        phase.next(2); // Immutable packet construction and queueing.
        NorthlightActorGeometry::Packet packet;packet.sharedMesh=replay.shared;if(!packet.sharedMesh)packet.mesh=replay.mesh();packet.position=program;
        if(hasUV)packet.uv=uvProgram->second;packet.hasUV=hasUV;packet.alphaTest=replay.cutoff>=0;
        packet.elements.assign(elements,elements+n);std::memcpy(packet.constants.data(),replay.constants,sizeof replay.constantStorage);std::memcpy(packet.inverseView.data(),context.inverseView,64);
        packet.material=std::move(material);packet.texture=std::move(texture);actorJob->packets.push_back(std::move(packet));++actorDraws;
        if(sample&&NorthlightRenderThreadProbe::profiling())try{replayGiPacked.push_back(&replay);}catch(...){} /* capture waste (RenderProfile) */
    }
    void finishActorScene(){
        if(!actorCaptureEnabled())return;lastActorCapture=GetTickCount();actorJob->center=vec(context.camera);
        actorJobComplete=std::move(actorJob);actorCaptureDue=false;++actorJobSerial_;actorSceneMap_=active?active->map:std::string{};
        if(captureSampled)deferLogf("WORLD actor packets draws=%u queuedVertices=%u skippedAlpha=%u snapshotReadBytes=%zu material=actual-rgba128-or-neutral035",actorDraws,actorVerticesEvaluated,actorSkippedAlpha,replaySnapshots.bytesRead());
    }
    std::shared_ptr<const NorthlightActorGeometry::ActorJob> completedActorJob()const{return actorJobComplete;}
    uint64_t actorJobSerial()const{return actorJobSerial_;}
    const std::string& completedActorSceneMap()const{return actorSceneMap_;}
    void prepareStaticProofs(){
        for(auto& p:replays){p->staticProofMask=p->staticM2Mask=0;p->staticM2Model=nullptr;}
        if(!staticScene||staticScene->map!=lastRequest.map||!staticCasters.stats().readyModels||replays.empty())return;
        StaticShadowDedup::Budget budget;
        try {
            // Expensive shader evaluation is bounded too, not only matching.
            // Rotate the starting point so a difficult first draw cannot starve
            // other eligible rigid WMO packets indefinitely.
            const size_t start=staticDedupCursor++%replays.size();size_t evaluated=0;
            for(size_t j=0;j<replays.size()&&!budget.expired()&&evaluated<1024;++j){
                auto& p=*replays[(start+j)%replays.size()];const auto& mesh=p.mesh();
                auto program=actorPrograms.find(p.originalShader);
                if(!recognizesWmo(p.originalShader)||program==actorPrograms.end()||program->second->skinned||mesh.dynamic||p.cutoff>=0||!mesh.vertexCount||mesh.vertexCount>256||mesh.primitiveCount>512)continue;
                if(program->second->operations.size()*mesh.vertexCount>8192)continue;
                const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
                if(!declarationCache.get(p.decl,elements,count))continue;
                evaluated+=mesh.vertexCount;++staticDedupAttempts;
                std::vector<NorthlightActorDeformation::Position> positions;
                if(!NorthlightActorDeformation::worldPositions(*program->second,mesh,elements,count,p.constants,context.inverseView,positions)||budget.expired())continue;
                std::vector<StaticShadowDedup::Triangle> triangles;
                if(!StaticShadowDedup::makeTriangles(positions,mesh.indices,mesh.indexed,mesh.primitiveCount,mesh.topology==D3DPT_TRIANGLESTRIP,triangles,512))continue;
                auto proof=staticMatcher.match(*staticScene,triangles,{true,false,false,false},budget,
                    [this](const StaticShadow::Placement& place,uint64_t revision){return staticCasters.coverage(place,revision)?15u:0u;});
                if(!proof.valid)continue;
                ++staticDedupMatches;p.staticProofMask=proof.slotMask;p.staticProofModel=proof.modelKey;p.staticProofRevision=proof.modelRevision;
                p.staticProofLow=V(INFINITY,INFINITY,INFINITY);p.staticProofHigh=V(-INFINITY,-INFINITY,-INFINITY);
                for(const auto& t:triangles)for(auto v:t){p.staticProofLow.x=std::min(p.staticProofLow.x,v.x);p.staticProofLow.y=std::min(p.staticProofLow.y,v.y);p.staticProofLow.z=std::min(p.staticProofLow.z,v.z);p.staticProofHigh.x=std::max(p.staticProofHigh.x,v.x);p.staticProofHigh.y=std::max(p.staticProofHigh.y,v.y);p.staticProofHigh.z=std::max(p.staticProofHigh.z,v.z);}
            }
        }catch(...){for(auto& p:replays)p->staticProofMask=0;}
        try{prepareStaticM2();}catch(...){for(auto& p:replays){p->staticM2Mask=0;p->staticM2Model=nullptr;}}
    }
    // Rigid M2 doodads (static_m2_dedup.h): a captured opaque skinned draw that uses one bone of an audited palette
    // program, whose bone's world matrix is a static-cache placement (rigidStaticCovered's match) that has not moved
    // for StableFrames capture frames, and whose world-space vertices coincide with the placement's opaque model
    // vertices. The decision is remembered per identity (shared snapshot, program, declaration, range, matrix); the
    // matrix compare runs every capture frame, so a doodad that starts to move (a door, a windmill) gets its replay
    // back at once. staticM2Mask then names the slots whose cache holds the model; the replay loop also tests the
    // slot's cached matrix against the bounds. Needs the rigid placement index of this scene (a track builds it).
    void prepareStaticM2(){
        if(!StaticM2Dedup::Enabled||!staticScene||staticScene->map!=lastRequest.map||!staticCasters.stats().readyModels||!rigidIndexCurrent())return;
        const auto started=std::chrono::steady_clock::now();const unsigned long long sceneRevision=rigidSceneRevision(*staticScene);
        ++staticM2Frame;unsigned verifications=0;std::vector<NorthlightActorDeformation::Position> world;
        for(auto& up:replays){auto& p=*up;
            if(!p.shadowSkinned||p.cutoff>=0||!p.shared||p.type!=D3DPT_TRIANGLELIST)continue;
            const auto* program=rigidProgram(p.originalShader);if(!program)continue;
            float bone=NAN;
            if(p.boneKnown)bone=p.bone;
            else{const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;if(declarationCache.get(p.decl,elements,count))bone=prepareCaches->bones.bone(*program,p.mesh(),p.shared,p.decl,elements,count);}
            float matrix[12];
            if(!(bone>=0&&bone<=74&&std::floor(bone)==bone)||!NorthlightRigidGeometry::worldBone(p.constants+4*(31+3*unsigned(bone)),context.inverseView,matrix))continue;
            auto& e=staticM2Memory.touch(p.shared,p.originalShader,p.decl,p.start,p.count,matrix,staticM2Frame);
            if(e.sceneRevision!=sceneRevision){e.sceneRevision=sceneRevision;e.state=StaticM2Dedup::Entry::Unverified;}
            StaticM2Dedup::Facts facts;facts.rigid=true;facts.stableFrames=e.stable;
            if(e.state==StaticM2Dedup::Entry::Unverified&&e.stable>=StaticM2Dedup::StableFrames&&verifications<2&&
               std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<.3){
                ++verifications;e.state=StaticM2Dedup::Entry::Rejected;
                size_t found=SIZE_MAX;const auto& all=staticScene->placements;
                rigidIndex.find(matrix+9,.05f,[&](std::uint32_t i){const auto& place=all[i];const float t[3]={place.translation.x,place.translation.y,place.translation.z};
                    if(!NorthlightRigidGeometry::staticPlacement(matrix+9,matrix,t,place.matrix))return false;found=i;return true;});
                const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;auto model=found==SIZE_MAX?staticScene->models.end():staticScene->models.find(all[found].modelKey);
                if(found!=SIZE_MAX&&model!=staticScene->models.end()&&model->second&&declarationCache.get(p.decl,elements,count)&&p.mesh().vertexCount<=4096&&
                   NorthlightActorDeformation::worldPositions(*program,p.mesh(),elements,count,p.constants,context.inverseView,world)&&
                   StaticM2Dedup::boundsOf(world,e.low,e.high)&&StaticM2Dedup::inside(all[found],e.low,e.high)&&
                   p.count==p.mesh().primitiveCount&&StaticM2Dedup::covers(*model->second,all[found],world,p.mesh().indices,p.mesh().indexed,p.count)){
                    e.state=StaticM2Dedup::Entry::Matched;e.placementIndex=found;e.placement=all[found];e.revision=model->second->contentRevision;++staticM2Verified;}
                else ++staticM2Rejected;
            }
            facts.placed=facts.modelMatches=e.state==StaticM2Dedup::Entry::Matched;
            if(facts.placed){facts.boundsInside=StaticM2Dedup::inside(e.placement,e.low,e.high);facts.cacheValid=staticCasters.coverageAt(e.placementIndex,e.placement,e.revision);}
            if(StaticM2Dedup::decide(facts)!=StaticM2Dedup::Verdict::Skip)continue;
            p.staticM2Mask=15u;p.staticM2Model=&e.placement.modelKey;p.staticProofRevision=e.revision;p.staticProofLow=e.low;p.staticProofHigh=e.high;
        }
    }
    uint64_t staticSignature(const float* matrix){
        try {return staticCasters.signature(matrix);}
        catch(...){staticCasters.reset();staticOwnerGeneration=UINT64_MAX;staticRetryTick=GetTickCount();return 0;}
    }
    NorthlightLocalShadowSignature::Digest localShadowSignature(ShadowCacheKey& key,const float* matrix){
        return key.localMemo.get(uploadedLocalShadowRecords.get(),meshGeneration,fixedTerrainChunks(),matrix);
    }
    bool drawStaticCasters(const float* matrix,const std::vector<NorthlightShadowBounds::TexelRect>* rects=nullptr){
        try {if(staticCasters.draw(d,matrix,rects,long(ShadowCacheSize),StaticCacheDirtyMargin))return true;}catch(...){}
        if(staticDrawFailures++<8)logf("STATIC SHADOW draw retry: base terrain/local shadows retained");
        staticCasters.reset();staticOwnerGeneration=UINT64_MAX;staticRetryTick=GetTickCount();return false;
    }
#include "world_rigid_memory.inl"
#include "world_shadow_experiment.inl"
#include "world_memory_guard.inl"
#include "world_replay_probe.inl"
    // The frame's first model draw decides whether this frame captures replays at all
    // (NorthlightQuality::skipModelCapture). Terrain capture and the game's draws are unaffected.
    bool modelCaptureSkipped(){
        if(captureMode==CaptureUndecided){bool skip=false;
            const bool shadows=NorthlightQuality::actorShadowWork(quality,effects.shadows); /* 0.3.158: ActorShadows=0 = shadows off for capture */
            if(NorthlightQuality::captureSkipPossible(quality,shadows)){NorthlightQuality::CaptureInputs in;
                in.shadows=shadows;in.demand=captureDemand;in.diagnostic=lastRenderDebug==1;in.actorDue=actorCaptureEnabled();in.nextPass=shadowPasses+1;
                for(int source=0;source<2;++source)in.sourceActive[source]=NorthlightGI::dot(sourceColors[source],sourceColors[source])>1e-10f;
                in.pointDue=in.shadows&&!in.actorDue&&!in.demand&&pointRefreshPredicted();
                skip=NorthlightQuality::skipModelCapture(quality,in,nearShadow,farShadow);}
            captureMode=skip?CaptureSkipped:CaptureFresh;if(skip)++captureSkippedFrames;else captureDemand=false;}
        return captureMode==CaptureSkipped;
    }
    // The 3 palette rows (bone 0, at paletteBase) of the draw about to be issued, answered by the
    // device mirror. Null: not a palette program, or the rows cannot be read.
    const NorthlightActorDeformation::Program* paletteRows(IDirect3DVertexShader9* shader,float* rows){
        auto program=actorPrograms.find(shader);
        if(program==actorPrograms.end()||program->second->paletteBase<0||program->second->paletteBase+2>=256||FAILED(d->GetVertexShaderConstantF(UINT(program->second->paletteBase),rows,3)))return nullptr;
        return program->second.get();
    }
    // Palette root (bone 0 origin, world) of the draw about to be issued: paletteRows() through
    // the capture's inverse view. False: not a palette program, or the rows cannot be read.
    bool drawRoot(IDirect3DVertexShader9* shader,float* root,const NorthlightActorDeformation::Program** used=nullptr){
        float rows[12];const auto* program=paletteRows(shader,rows);if(!program)return false;if(used)*used=program;
        float bank[4*256];std::memcpy(bank+4*program->paletteBase,rows,sizeof rows);
        return NorthlightActorDeformation::rootWorld(*program,bank,context.inverseView,root);
    }
    // 0.3.172 near capture reserve (near_reserve.h): asked only for a skinned draw the budget is
    // about to turn away for bytes. The anchor is taken once per capture frame, at the first ask
    // (the previous selection's self and this frame's camera; shadowPivot() is not called).
    NorthlightNearReserve::Anchor nearAnchor;bool nearAnchorReady=false;
    unsigned nearAdmitted=0,nearRefused=0;size_t nearBytes=0;
    /* 0.3.211 (task 31): capture admission (capture_admission.h). The centre is the near anchor's: taken once per capture frame at the first
       skinned draw (the previous selection's self, else the pivot eye+forward*pivotDistance). admissionOn: ActorShadows and a usable centre. */
    /* The root of consecutive skinned draws is read once: the mirror's write serials prove the palette rows unchanged (RootMemo). Inside the
       game draw's guard like captureBlocks(); a source that cannot certify falls back to the read. Cleared every frame (new inverse view). */
    NorthlightCaptureAdmission::RootMemo rootMemo;unsigned rootReads=0,rootMemoHits=0;
    bool drawRootMemo(IDirect3DVertexShader9* shader,float* root){
        if(!constantEpochSource.attached()||!constantEpochSource.valid()){++rootReads;return drawRoot(shader,root);}
        const auto& live=constantEpochSource.live();
        if(rootMemo.hit(shader,live.reset,live.floatBlock)){++rootMemoHits;if(rootMemo.valid)std::memcpy(root,rootMemo.root,sizeof rootMemo.root);return rootMemo.valid;}
        ++rootReads;const NorthlightActorDeformation::Program* program=nullptr;const bool ok=drawRoot(shader,root,&program);
        if(program)rootMemo.store(shader,unsigned(program->paletteBase),live.reset,live.floatBlock,root,ok); /* paletteRows() proved 0 <= paletteBase, paletteBase+2 < 256 */
        return ok;
    }
    NorthlightCaptureAdmission::Admission admission;bool admissionOn=false;std::vector<float> gatedBodies; /* roots of radius-gated bodies, for rigid memory */
    bool actorCaptureWouldRun()const{return NorthlightQuality::giActorCapture(quality)&&(actorCaptureDecided?actorCaptureDue:(!lastActorCapture||GetTickCount()-lastActorCapture>=200));}
    void nearAnchorTake(){
        if(!nearAnchorReady){nearAnchorReady=true;const float forward[3]={context.inverseView[8]*projection[2],context.inverseView[9]*projection[2],context.inverseView[10]*projection[2]};
            nearAnchor=NorthlightNearReserve::anchor(actorShadowHistory.selfHold()>0?actorShadowHistory.selfAt():nullptr,context.camera,forward,pivotDistance);
            NorthlightCaptureAdmission::Context centre;float pivot[3];centre.camera=context.camera;centre.yards=float(effectiveActorShadowRadius());centre.gi=actorCaptureWouldRun();
            if(nearAnchor.self){centre.centre=nearAnchor.selfAt;centre.self=true;}
            else if(nearAnchor.ray){for(unsigned k=0;k<3;++k)pivot[k]=nearAnchor.eye[k]+nearAnchor.forward[k]*pivotDistance;centre.centre=pivot;}
            admissionOn=quality.actorShadows&&centre.centre;if(admissionOn)admission.arm(centre);}
    }
    bool nearDraw(const float* root){return root&&NorthlightNearReserve::test(nearAnchor,root)!=NorthlightNearReserve::None;}
    bool nearCandidate(bool priority,const float* root){
        return priority&&replaySnapshots.nearReserve()&&!replaySnapshots.countExhausted(priority)&&nearDraw(root);}
    void captureModel(D3DPRIMITIVETYPE type,INT base,UINT minimum,UINT vertexTotal,UINT start,UINT count,bool indexed,IDirect3DVertexShader9* current,bool sample,const void* userIndices,D3DFORMAT userFormat,const void* userVertices,UINT userStride){
        if(modelCaptureSkipped())return; /* previous replays/actor packets are not needed this frame */
        const CaptureShader* found=lookupShader(current).capture;if(!found){if(sample)++unknownCaptureCalls;return;}
        const auto& metadata=*found;
        // 0.3.173 rigid memory: did the game draw a remembered prop? Before every capture rejection.
        if(!rigidDrawKeys.empty()&&rigidDrawKeys.contains(current,count))rigidMemoryDrawn(current,count);
        // Shadow fate (diagnostic window only): the outcome recorded at return.
        struct FateScope {NorthlightShadowFate::Tracker& tracker;int slot=-1;NorthlightShadowFate::Fate reason=NorthlightShadowFate::Unknown;
            ~FateScope(){tracker.record(slot,reason,false);}} fate{shadowFate};
        if(shadowFate.active()){NorthlightShadowFate::Key key;IDirect3DVertexBuffer9* vb=nullptr;UINT offset=0,stride=0;IDirect3DIndexBuffer9* ib=nullptr;
            if(!userVertices&&SUCCEEDED(d->GetStreamSource(0,&vb,&offset,&stride))&&vb){key.vb=reinterpret_cast<std::uintptr_t>(vb);vb->Release();}
            else key.vb=reinterpret_cast<std::uintptr_t>(NorthlightStream::upIdentity?NorthlightStream::upIdentity:userVertices); /* 0.3.192 (CS): the game's pointer, not the recorded copy's (identity only; the data is read from userVertices) */
            if(indexed&&!userIndices&&SUCCEEDED(d->GetIndices(&ib))&&ib){key.ib=reinterpret_cast<std::uintptr_t>(ib);ib->Release();}
            key.shader=current;key.base=base;key.start=start;key.count=count;key.minimum=minimum;
            // Per instance: the palette root (identical models share buffers and ranges).
            float root[3];if(drawRoot(current,root))for(unsigned a=0;a<3;++a)key.cell[a]=std::int32_t(std::floor(root[a]));
            fate.slot=shadowFate.slot(key,metadata.skinned,count);fate.reason=NorthlightShadowFate::Cap4096;}
        if(replays.size()>=4096){captureShortfall=true;return;}
        if(sample)++replayCaptureCalls;CpuScope cpu(sample?&replayCaptureTicks:nullptr);
        // Rotate the 1/16 draw subset between sampled frames. No timing calls
        // or new diagnostic counters run in this path on ordinary frames.
        const bool detailed=sample&&((replayCaptureCalls-1)&15u)==(capturePhaseSerial&15u);
        NorthlightCapturePhases::Scope<CapturePhaseCount> phase(detailed?&capturePhases:nullptr,CaptureState);
        if(detailed)++capturePhaseDraws;
        const bool priority=metadata.skinned;
        if(sample&&priority)++skinnedCandidates;
        const unsigned triangleBin=NorthlightReplayShadowPolicy::bucket(count);
        if(sample){++replayTriangleBins[triangleBin];if(priority)++skinnedTriangleBins[triangleBin];}
        const bool smallShadow=(type==D3DPT_TRIANGLELIST||type==D3DPT_TRIANGLESTRIP)&&
            NorthlightReplayShadowPolicy::small(priority,count,minSkinnedShadowTriangles);
        // Preserve actor GI captures at their existing 200 ms cadence. On other
        // frames skip only this shadow capture, before its device state queries.
        // The game's original draw and the shared draw-domain gate are untouched.
        fate.reason=NorthlightShadowFate::Small;
        if(smallShadow&&!actorCaptureEnabled()){if(sample)++smallShadowEarly;++rejSmall;return;}
        // 0.3.215 (task 47): a faded actor (stealth, ghosts) draws a depth-only pre-pass (no RGB) before its blended colour pass.
        // It looks opaque, so reject it here, before the admission, the budget and the near reserve (its weapons and parts too).
        fate.reason=NorthlightShadowFate::Fade;
        {DWORD colorWrite=15;if(SUCCEEDED(d->GetRenderState(D3DRS_COLORWRITEENABLE,&colorWrite))&&(colorWrite&7)==0){if(sample&&priority)++skinnedFadeRejected;if(sample&&!priority)++otherFadeRejected;return;}}
        // 0.3.215 (task 47): a blended draw is never captured, so reject it before the admission and the budget too; it no longer
        // counts as a radius-gated, cut or budget-refused draw, feeds the next cut or marks the frame short.
        fate.reason=NorthlightShadowFate::Blend;
        {DWORD blend=0;if(FAILED(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))||blend){if(sample&&priority)++skinnedBlendRejected;if(sample&&!priority)++otherBlendRejected;return;}}
        // 0.3.211 (task 31): the palette root once per skinned draw, then the capture admission (radius gate, distance cut)
        // before any budget is spent; near-reserve hits always pass. A draw without a root is never gated or cut.
        float root[3];bool rooted=false;NorthlightCaptureAdmission::Decision admit;
        if(priority&&quality.actorShadows){nearAnchorTake();rooted=drawRootMemo(current,root);
            if(admissionOn){admit=admission.decide(reinterpret_cast<std::uintptr_t>(current),count,vertexTotal,rooted?root:nullptr,nearDraw(rooted?root:nullptr));
                if(admit.verdict!=NorthlightCaptureAdmission::Verdict::Admit){
                    if(admit.verdict==NorthlightCaptureAdmission::Verdict::RadiusGate)++rejGate;else ++rejCut;
                    if(admit.verdict==NorthlightCaptureAdmission::Verdict::RadiusGate){fate.reason=NorthlightShadowFate::RadiusGate;gatedBodies.insert(gatedBodies.end(),root,root+3);}
                    else{fate.reason=NorthlightShadowFate::DistanceCut;captureShortfall=true;}
                    return;}}}
        fate.reason=NorthlightShadowFate::CaptureBudget;
        // Only reject when no legal snapshot can fit, including cache hits
        // and UP draws. A previous oversized failure is not an exhaustion proof.
        // A near skinned draw may read from the near reserve once the main budget is spent.
        bool nearby=false;
        if(replaySnapshots.captureExhausted(priority)){const bool candidate=nearCandidate(priority,rooted?root:nullptr);
            if(candidate&&!replaySnapshots.captureExhausted(priority,true))nearby=true;
            else{nearRefused+=candidate;captureShortfall=true;++rejBudget;if(admit.rooted)admission.budgetRefused(admit,root,vertexTotal,count);
                if(sample&&priority){++skinnedSnapshotRejected;++skinnedBudgetRejected;}
                if(sample&&!priority)++otherBudgetRejected;
                return;}
        }
        DWORD alpha=0,ref=0,alphaFunc=D3DCMP_ALWAYS;
        fate.reason=NorthlightShadowFate::AlphaFunc;
        if(FAILED(d->GetRenderState(D3DRS_ALPHATESTENABLE,&alpha)))return;
        if(alpha){if(FAILED(d->GetRenderState(D3DRS_ALPHAREF,&ref))||FAILED(d->GetRenderState(D3DRS_ALPHAFUNC,&alphaFunc)))return;}
        else if(sample)alphaStateQueriesSkipped+=2;
        if(alpha&&alphaFunc!=D3DCMP_GREATER&&alphaFunc!=D3DCMP_GREATEREQUAL)return;
        // Validate the main camera before touching model buffers. Full constant
        // banks are copied only for snapshots that survive range/budget checks.
        phase.next(CaptureProjection);fate.reason=NorthlightShadowFate::Projection;
        float q[16];const unsigned kind=metadata.projectionKind;
        if(FAILED(d->GetVertexShaderConstantF(kind==1?4:2,q,4)))return;
        if(sample){capturedConstantBytes+=64;++capturedConstantCalls;}
        if(std::fabs(q[0]-projection[0])>.005f||std::fabs(q[5]-projection[1])>.005f||std::fabs(q[kind==1?11:14]-projection[2])>.005f||std::fabs(q[15])>.001f){if(sample&&priority)++skinnedProjectionRejected;if(sample&&!priority)++otherProjectionRejected;return;}
        // A rejected draw returns its record to the pool as well. Exhausted
        // geometry budgets must not allocate/zero a fresh constant bank per draw.
        phase.next(CaptureSnapshot);fate.reason=NorthlightShadowFate::Snapshot;
        if(!snapshotFrameBegun)snapshotBeginFrame(sample);
        NorthlightDrawSnapshot::SnapshotSpan<decltype(snapshotMeter)> snapshotSpan(snapshotMeter); /* 0.3.181 (S3): to the constants phase or a return */
        std::unique_ptr<Replay,ReplayRecycle> p(acquireReplay().release(),ReplayRecycle{this});p->shadowSkinned=priority;p->shadowSelected=!smallShadow;p->shadowSmall=smallShadow;p->boneKnown=false;p->selfShadow=false;p->captureRootValid=rooted;if(rooted)std::memcpy(p->captureRoot,root,sizeof p->captureRoot);p->anchorValid=false;p->fateSlot=-1;p->fateClass=smallShadow?NorthlightShadowFate::Small:NorthlightShadowFate::NotRanked;p->fateDistance=0;p->projectionKind=kind;p->shader=metadata.replacement;p->shader->AddRef();p->originalShader=current;p->originalShader->AddRef();p->pointBounds={};
        if(replaySnapshots.lookup()==NorthlightDrawSnapshot::Frame::Lookup::Prefetch&&!freeReplays.empty())prefetchReplay(*freeReplays.back()); /* 0.3.181 (S2) */
        if(FAILED(d->GetVertexDeclaration(&p->decl))||!p->decl)return;
        NorthlightDrawSnapshot::Draw draw{type,base,minimum,vertexTotal,start,count,indexed};NorthlightDrawSnapshot::Diagnostics why;
        const size_t readBefore=replaySnapshots.bytesRead();
        replaySnapshots.sampleMaintenance(sample);
        auto read=[&]{return userVertices?replaySnapshots.readUP(p->decl,draw,userIndices,userFormat,userVertices,userStride,p->snapshot,&why,priority,nearby):replaySnapshots.read(d,p->decl,draw,p->snapshot,&why,priority,&p->shared,nearby);};
        bool captured=read();size_t nearFrom=readBefore;
        // Turned away for bytes: a near draw is read once more, from the near reserve.
        if(!captured&&why.error==NorthlightDrawSnapshot::Error::Budget&&!nearby&&nearCandidate(priority,rooted?root:nullptr)){
            nearby=true;nearFrom=replaySnapshots.bytesRead();captured=read();if(!captured)nearRefused+=why.error==NorthlightDrawSnapshot::Error::Budget;}
        if(captured&&nearby){++nearAdmitted;nearBytes+=replaySnapshots.bytesRead()-nearFrom;}
        if(captured&&admit.rooted)admission.admitted(admit,reinterpret_cast<std::uintptr_t>(current),count,root,replaySnapshots.bytesRead()-readBefore);
        if(!captured){if(why.error==NorthlightDrawSnapshot::Error::Budget){fate.reason=NorthlightShadowFate::CaptureBudget;captureShortfall=true;if(admit.rooted)admission.budgetRefused(admit,root,vertexTotal,count);}
            if(sample){captureRejectedBytes+=replaySnapshots.bytesRead()-readBefore;if(priority){++skinnedSnapshotRejected;skinnedBudgetRejected+=why.error==NorthlightDrawSnapshot::Error::Budget;}else otherBudgetRejected+=why.error==NorthlightDrawSnapshot::Error::Budget;}if(snapshotRejects++<12)logf("MODEL snapshot rejected: %s hr=%08lx stream=%u",NorthlightDrawSnapshot::errorName(why.error),(unsigned long)why.hr,why.stream);return;}
        snapshotSpan.end();phase.next(CaptureConstants);fate.reason=NorthlightShadowFate::Constants;
        const auto& usage=metadata.usage;
        p->constantUsage=usage;
        const auto f=usage.floats,b=usage.booleans,i=usage.integers;
        const Replay* previous=replays.empty()?nullptr:replays.back().get();
        if(!NorthlightReplayCaptureConstants::captureBlocks(*p,previous,constantEpochSource,[&]{
            if((f.count&&FAILED(d->GetVertexShaderConstantF(f.first,p->constantStorage+4*f.first,f.count)))||
               (b.count&&FAILED(d->GetVertexShaderConstantB(b.first,p->boolStorage+b.first,b.count)))||
               (i.count&&FAILED(d->GetVertexShaderConstantI(i.first,p->intStorage+4*i.first,i.count))))return false;
            if(sample){capturedConstantBytes+=16*f.count+4*b.count+16*i.count;
                capturedConstantCalls+=(f.count!=0)+(b.count!=0)+(i.count!=0);}
            return true;
        },[&](unsigned first,unsigned count,float* out){
            if(FAILED(d->GetVertexShaderConstantF(first,out,count)))return false;
            if(sample){capturedConstantBytes+=16*count;++capturedConstantCalls;}
            return true;
        },sample?&constantEpochProfile:nullptr,constantSelfCheck,constantSelfCheckState))return;
        if(sample){capturedSM1Draws+=metadata.sm1;capturedRelativeDraws+=usage.relativeFloat;}
        phase.next(CaptureMaterial);fate.reason=NorthlightShadowFate::Material;
        if(FAILED(d->GetTexture(0,&p->texture))||(alpha&&!p->texture))return;
        if(FAILED(d->GetSamplerState(0,D3DSAMP_ADDRESSU,&p->addressU))||FAILED(d->GetSamplerState(0,D3DSAMP_ADDRESSV,&p->addressV)))return;
        if(p->addressU<D3DTADDRESS_WRAP||p->addressU>D3DTADDRESS_CLAMP||p->addressV<D3DTADDRESS_WRAP||p->addressV>D3DTADDRESS_CLAMP)return;
        phase.next(CaptureFinalize);
        p->cutoff=alpha?(float(ref)+(alphaFunc==D3DCMP_GREATER?.5f:0.f))/255.f:-1.f;
        p->type=type;p->base=0;p->min=0;p->vertices=p->mesh().vertexCount;p->start=0;p->count=count;p->indexed=indexed;
        if(sample){if(priority){++skinnedAccepted;acceptedSkinnedBytes+=p->mesh().byteSize();}else acceptedOtherBytes+=p->mesh().byteSize();}
        p->constantGroup=replays.empty()?0:replays.back()->constantGroup+
            (NorthlightReplayCaptureConstants::samePose(*replays.back(),*p,sample?&constantEpochStats:nullptr)?0:1);
        phase.next(CaptureActor);
        appendActor(current,*p,sample);
        phase.next(CaptureFinalize);
        if(sample){++acceptedTriangleBins[triangleBin];if(priority)++acceptedSkinnedTriangleBins[triangleBin];smallShadowGI+=smallShadow;}
        p->fateSlot=fate.slot;fate.slot=-1; /* the selection records this draw's outcome */
        // 0.3.179 (M1): the handoff (fill + publish) as one span on sampled records of RenderProfile sample frames.
        const bool handoff=p->shadowSelected&&p->shadowSkinned,timedHandoff=handoff&&prepareHandoffSample();
        const auto handoffStart=timedHandoff?prepareMeter.start():std::chrono::steady_clock::time_point{};
        if(handoff)prepareFill(*p,metadata); /* 0.3.177: the stable selection's inputs */
        replays.emplace_back(p.release());preparePublish();if(timedHandoff)prepareMeter.stop(handoffStart);if(detailed)++capturePhaseAccepted;
    }

    // One directional replay draw in the 0.3.142 order: geometry, pose constants,
    // material, draw. The cascade loop and DiagReplayProbe (world_replay_probe.inl)
    // share it. Split marks the state/own/draw spans on RenderProfile sample frames;
    // Off compiles them away. StateOnly/LogicOnly (probe modes) drop the draw / every
    // D3D9 call at compile time. ok(hr,stage) false stops the pass (the cascade: check()).
    // 0.3.200 (jobs): ReplayJobs=1 (core/job_system.h). The pool starts lazily on the renderer thread inside render(); the jobs are pure
    // CPU work on inputs that are final when they are kicked and untouched until their join (no D3D, no logs, no game memory).
    NorthlightJobs::System replayJobs_;bool replayJobsTried_=false;
    struct JobJoin {NorthlightJobs::System& s;NorthlightJobs::Counter& c;~JobJoin(){s.wait(c);c.clearFailure();}}; /* every kicked job is joined before render() returns */
    bool jobsOn()const{return quality.replayJobs!=0&&replayJobs_.started();}
    bool jobsStart(){
        if(quality.replayJobs&&!replayJobsTried_){replayJobsTried_=true;
            const unsigned cores=NorthlightStream::cores(); /* the replay thread excluded while the stream runs */
            if(replayJobs_.start(cores))logf("JOBS workers=%u cores=%u",replayJobs_.workers(),cores);
            else logf("JOBS unavailable: no worker thread; the per-frame work stays on the renderer thread");}
        return jobsOn();
    }
    void takeCullEarly(unsigned& kicked,unsigned& skipped){kicked=replayCullEarly;skipped=replayCullEarlySkipped;replayCullEarly=replayCullEarlySkipped=0;} /* 0.3.217: renderer.cpp's JOBS line */
    NorthlightJobs::Stats takeJobStats(){return replayJobs_.take();} /* renderer.cpp's JOBS line, RenderProfile sample frames */
    static constexpr float LampFogNear=3.5f; /* c58.x: the lamp glow's near fade start, the scissor view's fog near */
    // The frame's D3D-free light and fog work. Its inputs are final once upload() has run (the snapshot and its lights, the fog field,
    // the camera, the celestial state, the settings); its outputs (the tracker, the clouds' clock and state, localScissorView and this
    // frame) are read only after the join at the constant bank. c holds the bank's c22.w, c31.w and c32 (the same expressions).
    struct AtmosphereFrame {
        NorthlightLocalLightSelection::Selection lights;NorthlightFogClouds::Frame cf;float c[33][4]={};
        float airFloor=.0017f,veil[2]={1,1}; /* veil: the sky transmittance toward the sun without / with the clouds' term */
        float spellTau[2][NorthlightSpellFog::kSamples]={};bool spellValid=false; /* SpellFog: the optical depth along the view axis at kDistance, without / with the clouds' term (the same sigma as the veil) */
        NorthlightLocalLightScissor::Rect directRects[NorthlightLocalLightSelection::Slots/NorthlightLocalLightSelection::DirectBatchSize],
            fogRects[NorthlightLocalLightSelection::Slots/NorthlightLocalLightSelection::FogBatchSize];
    };
    AtmosphereFrame atmosphere;NorthlightJobs::Counter atmosphereDone;
    void atmosphereWork(AtmosphereFrame& a,float nearZ,UINT w,UINT h,int debug){
        auto& c=a.c;const auto wx=weatherEffects();
        // 0.3.197: tracked selection. A camera jump (teleport), a long gap (F10 off, loading) or F12 debug snap the
        // factors without fades; a different map (and a rebuilt device, releasePointGPU) forgets the tracker.
        if(!active||active->map!=localLightMap)localLightTracker.reset();
        const int64_t selectT0=QpcClock::now();
        const double selectDt=localLightQpc&&captureFrequency.QuadPart>0?double(selectT0-localLightQpc)/double(captureFrequency.QuadPart):0;
        const bool selectContinuous=active&&localLightQpc&&selectDt<=NorthlightLocalLightSelection::MaxGapSeconds&&active->map==localLightMap&&!different(vec(context.camera),localLightCamera,40)&&debug==0;
        if(equipmentLightsOn()){
            equipmentLights.publish(GetTickCount(),active?&active->localLights:nullptr);
            for(unsigned i=0;i<equipmentLights.suppressedCount;++i)localLightTracker.forget(equipmentLights.suppressed[i]);
            NorthlightEquipmentLights::replaceFadingDuplicates(localLightTracker,equipmentLights.output.data(),equipmentLights.outputCount);
            if(active)particleLights.publish(equipmentLights.output.data(),equipmentLights.outputCount,active->localLights,context.camera,localLightTracker,selectContinuous?float(selectDt):0.f);
            else particleLights.reset();
        }
        // Same camera, projection, near plane and glow start as c0..c6/c58: the selection prefers lights in view (debug views keep the old order), and
        // each batch's rect of the direct (8) and fog (4) passes.
        localScissorView=NorthlightLocalLightScissor::view(context.inverseView,projection,nearZ,LampFogNear,w,h);
        a.lights=active?localLightTracker.update(active->localLights,context.camera,NorthlightGpuBudget::lightLimit(quality.localLightLimit,gpuBudgetLevel),float(selectDt),selectContinuous,debug==0?&localScissorView:nullptr,equipmentLightsOn()?particleLights.output.data():nullptr,equipmentLightsOn()?particleLights.outputCount:0,equipmentLightsOn()?particleLights.immediate.data():nullptr,equipmentLightsOn()?particleLights.immediateCount:0) /* one shared dynamic cap; particles carry current-frame alpha */:NorthlightLocalLightSelection::Selection{};
        auto& localLights=a.lights;
        {const auto arranged=NorthlightLocalLightBatches::arrange(localLights,localScissorView); /* screen-coherent batches (the passes are sums) */
         if(NorthlightDiagnostics::enabled()){localOffscreenSum+=localLightTracker.offscreen;localArranged+=arranged.arranged;localArrangeBefore+=arranged.before;localArrangeAfter+=arranged.after;}} /* counted with selectUs */
        if(NorthlightDiagnostics::enabled()){const double us=double(QpcClock::now()-selectT0)*1e6/(captureFrequency.QuadPart>0?double(captureFrequency.QuadPart):1.);localSelectUsSum+=us;localSelectUsMax=std::max(localSelectUsMax,us);++localSelectFrames;} /* selectUs on LOCAL direct */
        localLightQpc=selectT0;localLightCamera=vec(context.camera);if(active&&localLightMap!=active->map)localLightMap=active->map;
        // 0.3.199 (fog clouds): the frame clock of the clouds (a gap or the first frame counts as 0; advance() clamps) and the dense-zone damping: where the
        // regional fog is already thick (the field's profile tag at the camera, Duskwood 1) the rain's extra air and the clouds thin out, the profile
        // smoothed over ~3 s so a zone border never pops. A dry frame's air floor stays exactly .0017f (the extra is 0 whatever the damping).
        const int64_t cloudNow=QpcClock::now();
        const float cloudDt=cloudQpc&&captureFrequency.QuadPart>0?float(double(cloudNow-cloudQpc)/double(captureFrequency.QuadPart)):0.f;
        cloudQpc=cloudNow;
        // The same camera texel says whether the zone is lush (forest/grass): without rain only lush zones get banks (no field, indoors or unknown: unchanged; it starts lush).
        {float denseTarget=0,lushTarget=cloudLush;
         if(uploadedFogField){const auto& f=*uploadedFogField;
             const float fx=(context.camera[0]-f.originX)/NorthlightRegionalFog::Spacing,fy=(context.camera[1]-f.originY)/NorthlightRegionalFog::Spacing;
             if(fx>=0&&fy>=0&&fx<NorthlightRegionalFog::N-1&&fy<NorthlightRegionalFog::N-1){const unsigned k=unsigned(fy)*NorthlightRegionalFog::N+unsigned(fx);const auto& t=f.texels[k];
                 if(t.height>0)denseTarget=std::clamp((t.height-2.5f)/2.5f,0.f,1.f);
                 if(t.height>0)lushTarget=f.lush[k]?1.f:0.f;}}
         cloudDenseZone=NorthlightFogClouds::smoothDense(cloudDenseZone,denseTarget,cloudDt);
         cloudLush=NorthlightFogClouds::smoothDense(cloudLush,lushTarget,cloudDt);}
        const float denseDamp=NorthlightFogClouds::denseZoneDamp(cloudDenseZone);
        const float airFloor=.0017f+wx.airExtinction()*denseDamp; /* the shared outdoor air extinction (the shader's old literal .0017) plus the rain's extra (thinned in dense zones): exactly .0017f when dry */
        a.airFloor=airFloor;
        for(unsigned first=0;first<localLights.count;first+=NorthlightLocalLightSelection::DirectBatchSize){const auto batch=localLights.batch<NorthlightLocalLightSelection::DirectBatchSize>(first);
            a.directRects[first/NorthlightLocalLightSelection::DirectBatchSize]=NorthlightLocalLightScissor::batchRect(localScissorView,batch.position,batch.count);}
        for(unsigned first=0;first<localLights.count;first+=NorthlightLocalLightSelection::FogBatchSize){const auto batch=localLights.batch<NorthlightLocalLightSelection::FogBatchSize>(first);
            a.fogRects[first/NorthlightLocalLightSelection::FogBatchSize]=NorthlightLocalLightScissor::batchRect(localScissorView,batch.position,batch.count);}
        c[31][3]=NorthlightRegionalFog::nightFactor(celestialValid?celestial.dayFraction:-1.f);
        // Neutral scattering albedo preserves the zone/source palette. Atmospheric
        // style is separate from ground fog and surface irradiance.
        c[22][3]=.0035f+.0031f*c[31][3]; // generic forest day .0035, night .0066 (+20%)
        // Forest atmosphere follows verified render time, independently of source
        // visibility. Smooth nightFactor avoids a camera-driven density switch.
        // STV total air including the shared .0017: .0054 day (2x),
        // .01245 night (1.5x). Duskwood and general forest air retain their density.
        c[32][0]=.0055f+.0089f*c[31][3];c[32][1]=.0037f+.00705f*c[31][3];
        const auto& volumePalette=paletteFrameValid?framePalette:celestialProfiles.fallback;
        const float volumeHeightScale=NorthlightCelestialProfiles::fogHeightScale(volumePalette,c[31][3]);
        c[32][2]=64.f*volumeHeightScale;c[32][3]=48.f*volumeHeightScale;
        // Extinction at each picked light for its glow in the fog: same ground
        // and air policy as the shader, evaluated at the light's own position.
        // 0.3.199 (fog clouds): the wind advances on the render thread's own clock (a gap or the first frame counts as 0, advance() clamps), the frame's
        // constants come from the settings and the weather. Inactive (off, no coverage, fog effect off, debug view, noise or shader missing):
        // c60..c63 keep their zeros, the pass is not drawn and sigmaAt below adds nothing.
        if(quality.fogClouds)cloudWind.advance(cloudDt,wx.fog);
        // The noise (and its quantile table) is requested in render() before this, from the settings alone: derive() cannot be active before
        // the table exists. The device part (ensureCloudNoise) is decided at the bank: here the clouds' term is a candidate and the veil keeps both sums.
        auto& cf=a.cf;
        cf=NorthlightFogClouds::derive(quality.fogClouds,unsigned(std::lround(float(quality.fogCloudDensity)*denseDamp)),wx.fog,c[31][3],cloudWind,context.camera,fogCloudNoise().ready.load(std::memory_order_acquire)?&fogCloudNoise().quantiles:nullptr,cloudLush);
        const bool cloudCandidate=cf.active&&effects.fog&&debug==0&&fogCloudsPS;
        const uint8_t* cloudData=cloudCandidate?fogCloudNoise().data.data():nullptr;
        a.veil[0]=a.veil[1]=1;a.spellValid=false;
        if(effects.fog&&active&&uploadedFogField){
            const auto& field=*uploadedFogField;const float night=c[31][3];
            // cloudy: the same sigma plus the clouds' term (the old sigma+= on an active pass), else the same sigma.
            auto sigmaAt=[&](float lx,float ly,float lz,bool clouds,float& cloudy){
                const float fx=(lx-field.originX)/NorthlightRegionalFog::Spacing,fy=(ly-field.originY)/NorthlightRegionalFog::Spacing;
                float sigma=0;cloudy=0;
                if(fx>=0&&fy>=0&&fx<NorthlightRegionalFog::N-1&&fy<NorthlightRegionalFog::N-1){
                    const auto& t=field.texels[unsigned(fy)*NorthlightRegionalFog::N+unsigned(fx)];
                    if(t.height>0){
                        const float altitude=lz-t.ground;
                        const float profile=std::clamp((t.height-2.5f)/2.5f,0.f,1.f),generalForest=1-std::clamp((t.height-1.25f)/1.25f,0.f,1.f);
                        const float groundHeight=t.height+(6-t.height)*night*(1-profile);
                        const float vertical=std::clamp(1-altitude/std::max(groundHeight,.001f),0.f,1.f);
                        const float ground=altitude>=0?std::max(t.day+t.nightExtra*night,0.f)*vertical*vertical:0;
                        float airBase=c[32][1]+(c[32][0]-c[32][1])*profile;airBase=airBase+(c[22][3]-airBase)*generalForest;
                        airBase=airFloor+airBase*std::clamp((t.height-.625f)/.625f,0.f,1.f); /* 0.3.198 (rain): mirrors the shader (+0 when dry) */
                        const float airHeight=c[32][3]+(c[32][2]-c[32][3])*profile;
                        const float airVertical=std::clamp(1-altitude/std::max(airHeight,.001f),0.f,1.f);
                        sigma=altitude>=0?ground+airBase*airVertical*airVertical:0;cloudy=sigma;
                        if(clouds&&cloudCandidate){const float point[3]={lx,ly,lz};cloudy+=NorthlightFogClouds::sigmaAt(cloudData,cf,context.camera,point,t.ground,t.height,1.f);} /* 0.3.199 (fog clouds): the sun-ray veil follows the clouds; the lamp glow does not (it read too strong) */
                    }
                }
                return sigma;
            };
            float unused=0;
            for(unsigned i=0;i<localLights.count;++i)localLights.fog[i][0]=sigmaAt(localLights.position[i][0],localLights.position[i][1],localLights.position[i][2],false,unused);
            // the veil's estimate of the composite's fog.a on a sky pixel toward the sun: the
            // 128-unit volume along the sun ray from the eye, with the shader's near fade.
            if(field.fogCells||field.airCells){
                float depth=0,cloudyDepth=0;const V ray=sourceDirections[0];
                for(unsigned i=0;i<16;++i){const float t=(float(i)+.5f)*8;
                    const float fade=std::clamp((t-3.5f)/12.f,0.f,1.f);float cloudy=0;
                    const float sigma=sigmaAt(context.camera[0]+ray.x*t,context.camera[1]+ray.y*t,context.camera[2]+ray.z*t,true,cloudy);
                    depth+=sigma*fade*fade*(3-2*fade)*8;cloudyDepth+=cloudy*fade*fade*(3-2*fade)*8;}
                a.veil[0]=std::exp(-depth);a.veil[1]=std::exp(-cloudyDepth);
                // SpellFog: WorldFog's extinction mirrored term by term (spell_fog_march.h: the bilinear field with its coverage, the night profile, VolumeAir, the forest air, the rain's air extinction,
                // the near fade, the heights relative to the ground at every step) for the rays a spell draw has: from the eye toward a point at the camera's heading, kDistance away, at the height of
                // an effect above the ground THERE (a lantern glow, a spell at the player's height), not along the horizontal axis at eye height, which can sit above the ground fog layer that
                // the world at that distance is seen through. The clouds' bank is added when its pass runs, at the same samples (their coverage as the shader's valid).
                {V fwd=V(context.inverseView[8],context.inverseView[9],context.inverseView[10])*projection[2];const float fl=std::sqrt(NorthlightGI::dot(fwd,fwd));
                 if(fl>1e-6f){
                    NorthlightSpellFog::March m;m.night=night;std::memcpy(m.volume,c[32],16);m.forestAir=c[22][3];m.airFloor=airFloor;
                    float hx=fwd.x,hy=fwd.y;const float hl=std::sqrt(hx*hx+hy*hy);if(hl>.05f){hx/=hl;hy/=hl;}else{hx=1;hy=0;}
                    constexpr float kEffectHeight=2.5f;
                    const float eye[3]={context.camera[0],context.camera[1],context.camera[2]};
                    const auto noClouds=[](float,float,float,const NorthlightSpellFog::FieldSample&){return 0.f;};
                    const auto clouds=[&](float x,float y,float z,const NorthlightSpellFog::FieldSample& fs){
                        if(!cloudCandidate)return 0.f;
                        const float point[3]={x,y,z};return NorthlightFogClouds::sigmaAt(cloudData,cf,context.camera,point,fs.ground,fs.height,fs.coverage);};
                    for(unsigned k=0;k<NorthlightSpellFog::kSamples;++k){
                        const float z=NorthlightSpellFog::kDistance[k];
                        float to[3]={eye[0]+hx*z,eye[1]+hy*z,eye[2]};
                        const NorthlightSpellFog::FieldSample g=NorthlightSpellFog::fieldAt(field,to[0],to[1]);
                        if(g.coverage>0){ // the effect's height above the ground at the end point; the horizontal reach shortens so the ray is z long
                            const float dz=std::clamp(g.ground+kEffectHeight-eye[2],-.9f*z,.9f*z),dh=std::sqrt(std::max(z*z-dz*dz,1.f));
                            to[0]=eye[0]+hx*dh;to[1]=eye[1]+hy*dh;to[2]=eye[2]+dz;
                        }
                        float t0=NorthlightSpellFog::opticalDepth(field,m,eye,to,24,noClouds);
                        float t1=cloudCandidate?NorthlightSpellFog::opticalDepth(field,m,eye,to,24,clouds):t0;
                        if(k){t0=std::max(t0,a.spellTau[0][k-1]);t1=std::max(t1,a.spellTau[1][k-1]);}
                        a.spellTau[0][k]=t0;a.spellTau[1][k]=t1;
                    }
                    a.spellValid=true;}}
            }
        }
    }
    // The later cascades' replay culling (per packet: the static proof's containment in the slot's cached matrix, bit 0; the pose
    // bounds' clip test against the slot's matrix, bit 1). Kicked after the frame's first bounds join for every later active slot and
    // read at that slot's replay loop when both matrices still match (else the loop tests inline, as before). Reads the packets only.
    // 0.3.217 (jobs): the first rendering slot's own job may be kicked earlier, before its static and terrain draws, but only when the
    // bounds worker has already finished (replayCullKickOwn): the join then does not wait, and the later slots still go at the old join.
    struct ReplayCull {WorldRenderer* r=nullptr;int slot=0;bool kicked=false;float matrix[16]={},cached[16]={};std::vector<uint8_t> flags;NorthlightJobs::Counter done;
        void operator()(){r->replayCullWork(*this);}};
    ReplayCull replayCulls[4];
    void replayCullWork(ReplayCull& job){
        const size_t n=replays.size();job.flags.resize(n);const bool affine=NorthlightShadowBounds::affineLightMatrix(job.matrix);
        for(size_t index=0;index<n;++index){const auto& p=*replays[index];uint8_t f=0;
            if((p.staticProofMask&(1u<<job.slot))&&StaticShadow::containsBounds(job.cached,p.staticProofLow,p.staticProofHigh))f|=1;
            if(affine&&p.pointBounds.valid&&NorthlightShadowBounds::clipRejectAffine(V(p.pointBounds.low[0],p.pointBounds.low[1],p.pointBounds.low[2]),V(p.pointBounds.high[0],p.pointBounds.high[1],p.pointBounds.high[2]),job.matrix))f|=2;
            job.flags[index]=f;}
    }
    void replayCullKickSlot(int slot){
        auto& job=replayCulls[slot];
        job.r=this;job.slot=slot;std::memcpy(job.matrix,sourceMatrices[slot/2][slot%2],sizeof job.matrix);std::memcpy(job.cached,staticSlotMatrix[slot],sizeof job.cached);
        job.kicked=true;replayJobs_.kick(job.done,job);
    }
    void replayCullKick(int fromSlot,const bool* sourceActive){
        for(int slot=fromSlot+1;slot<4;++slot)if(sourceActive[slot/2])replayCullKickSlot(slot);
    }
    unsigned replayCullEarly=0,replayCullEarlySkipped=0; /* 0.3.217 (jobs): first-slot kicks before / without the static draws, per JOBS window */
    // 0.3.217 (jobs): the current slot's own job, only when the bounds job already finished: the join (apply) then never waits. Else nothing.
    void replayCullKickOwn(int slot){
        if(!replayBoundsWorker.finished()){++replayCullEarlySkipped;return;}
        replayBoundsJoin();++replayCullEarly;replayCullKickSlot(slot);
    }
    // Null: test inline (no job for the slot, or its inputs differ). The job is joined either way.
    const uint8_t* replayCullFor(int slot,const float* cached,const float* matrix){
        auto& job=replayCulls[slot];if(!job.kicked)return nullptr;
        job.kicked=false;const bool ok=replayJobs_.wait(job.done);job.done.clearFailure();
        return ok&&job.flags.size()==replays.size()&&!std::memcmp(job.matrix,matrix,sizeof job.matrix)&&!std::memcmp(job.cached,cached,sizeof job.cached)?job.flags.data():nullptr;
    }
    void replayCullSettle(){for(auto& job:replayCulls)if(job.kicked){job.kicked=false;replayJobs_.wait(job.done);job.done.clearFailure();}}
    template<class Split,class Check> bool submitReplay(const Replay* p,NorthlightReplayDrawState::Cache& replayBindings,NorthlightReplayPoses::Pass<BOOL>& poseConstants,const float* rows,Split& split,size_t& replayConstantBytes,size_t& replayConstantCalls,Check&& ok){
        if constexpr(!Split::Calls)return poseConstants.prepare(*p,rows); /* DiagReplayProbe logic-only mode: no D3D9 call */
        if(!ok(replayBindings.geometry(*p),"replay geometry state"))return false;
        split.mark(NorthlightRenderThreadProbe::State);
        if(!poseConstants.prepare(*p,rows))return false;
        const float* desired=poseConstants.desired();
        auto f=poseConstants.floats;auto b=poseConstants.booleans;auto i=poseConstants.integers;
        split.mark(NorthlightRenderThreadProbe::Own);
        if(f.count){if(!ok(d->SetVertexShaderConstantF(f.first,desired+4*f.first,f.count),"replay float constants"))return false;replayConstantBytes+=f.count*16;++replayConstantCalls;split.constant(NorthlightRenderThreadProbe::Floats);}
        if(b.count){if(!ok(d->SetVertexShaderConstantB(b.first,p->bools+b.first,b.count),"replay bool constants"))return false;replayConstantBytes+=b.count*4;++replayConstantCalls;split.constant(NorthlightRenderThreadProbe::Bools);}
        if(i.count){if(!ok(d->SetVertexShaderConstantI(i.first,p->ints+4*i.first,i.count),"replay integer constants"))return false;replayConstantBytes+=i.count*16;++replayConstantCalls;split.constant(NorthlightRenderThreadProbe::Ints);}
        if(!ok(replayBindings.material(*p),"replay material state"))return false;
        split.mark(NorthlightRenderThreadProbe::State);
        HRESULT hr=D3D_OK;
        if constexpr(Split::Draws)hr=p->indexed?d->DrawIndexedPrimitive(p->type,p->base,p->min,p->vertices,p->start,p->count):d->DrawPrimitive(p->type,p->start,p->count);
        split.mark(NorthlightRenderThreadProbe::Draw);
        return ok(hr,"animated shadow draw");
    }
    // 0.3.174: true once this frame's WorldComposite quad has drawn (the proxy's FOLD fallback reads it).
    bool composited=false;
    // foldScene/foldAO (0.3.174 FOLD): the proxy's pre-AO scene copy replaces the world colour
    // copy, and its AOContactBloom target (bloom rgb, AO alpha) is applied by TemporalLight's
    // removal smoothing and WorldComposite. Null: the proxy composited already (neutral s10).
    bool render(IDirect3DSurface9* targetSurface,IDirect3DTexture9* depth,UINT w,UINT h,D3DFORMAT fmt,float nearZ,float farZ,float minZ,float maxZ,int debug,NorthlightGpuProfile* profile,IDirect3DTexture9* waterMask,IDirect3DTexture9* foldScene=nullptr,IDirect3DTexture9* foldAO=nullptr){
        shadowsComposited=false;composited=false;lastRenderDebug=debug;
        if(debug!=1)gpuDiagnosticArmed=true;
        // 0.3.169 lastSkipReason(): why this call returned false. Untagged returns below the
        // upload gate are failed device calls in the draw stages ("draw").
        skipReason="fault";if(workerFault())return false;
        DWORD submissionStart=GetTickCount();
        // false only when this frame's model capture was skipped: no replays, and every
        // per-frame replay consumer (selection history and fate frame, GPU cache, bounds,
        // proofs, snapshot frame) is left untouched. Selection counts capture frames and
        // rescales its frame constants by NearShadowInterval (selectionTuning()).
        const bool freshReplays=captureMode!=CaptureSkipped;
        // 0.3.158 ActorShadows=0: replays exist only on GI capture frames and no shadow consumes
        // them (replayShadows), while every map and cube is complete without them (replaysComplete),
        // so nothing defers or demands a capture. ActorShadows=1: both are exactly freshReplays.
        const bool actorShadows=quality.actorShadows!=0;
        const bool replayShadows=freshReplays&&actorShadows;
        const bool replaysComplete=freshReplays||!actorShadows;
        replayProbeFrame(); /* DiagReplayProbe window of this frame (nothing unless RenderProfile) */
        finishActorScene();
        if(replayShadows)selectShadowReplays();
        bucket(NorthlightEffectsBuckets::Selection);
        if(effects.shadows&&replayShadows)replayBoundsKick(); /* 0.3.143: bounds worker; joined by the first pointBounds reader */
        struct BoundsJoin {WorldRenderer& r;~BoundsJoin(){r.replayBoundsJoin();}} boundsJoin{*this};
        // 0.3.152 RenderProfile: the upload window after selection and the bounds kick (locks,
        // ready/resources/upload, live terrain) up to the replay upload; logged with WORLD profile frame.
        uploadWindowTicks=-1;const int64_t uploadWindowStart=profileSampled()?QpcClock::now():0;
        {std::lock_guard<std::mutex> lock(mutex);
         if(valid&&NorthlightQuality::giActorCapture(quality)&&completedActorSceneMap()==lastRequest.map&&!pending&&!workerBusy&&actorJobSerial()!=lastRequest.actorSerial&&submissionStart-actorRequestTick>=250){
             Request r=lastRequest;r.actorJob=completedActorJob();r.actorSerial=actorJobSerial();r.reason=64;
             r.id=request.id+1;r.queuedAt=submissionStart;request=r;lastRequest=r;pending=true;actorRequestTick=submissionStart;wake.notify_one();
         }}
        NorthlightStreaming::Budget streamBudget;
        releaseOrphanedPending("render"); /* 0.3.156: also while !ready(), when upload() never runs */
        if(!ready()||!resources(w,h,fmt)||!upload(streamBudget)){
            skipReason=!valid?"context":failed?"failed":workerFault()?"fault":!active?"inactive":!active->bvh?"nobvh":
                !vertices||!indices||uploadedMap!=active->map?"mesh":"upload";
            // Cache maintenance must not depend on the local GI becoming ready.
            // No new GPU allocation competes with a deferred world build here.
            updateStaticCasters(false,&streamBudget);return false;
        }
        streamBudget.pause();
        auto timedTerrain=[&]{auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Terrain);return uploadLiveTerrain();};
        auto timedReplay=[&]{if(uploadWindowStart)uploadWindowTicks=QpcClock::now()-uploadWindowStart;auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Replay);return uploadReplay();};
        skipReason="terrain";bool terrainUploaded=false;
        if(effects.shadows&&(!(terrainUploaded=timedTerrain())||(replayShadows&&!timedReplay()))){if(terrainUploaded)skipReason="replay";streamBudget.resume();updateStaticCasters(false,&streamBudget);return false;}
        streamBudget.resume();
        skipReason="state";SavedState save(d,&stateBlocks);if(!save.ok)return false;
        skipReason="draw";
        if(profile)profile->mark("WorldUpload");
        bucket(NorthlightEffectsBuckets::Upload);
        // 0.3.200 (jobs): ReplayJobs=1 kicks the frame's D3D-free CPU work here, as soon as its inputs are final (the snapshot, the fog field, the
        // batches and terrain chunks committed by upload()); each result is joined right before the first D3D call that needs it, and the guards
        // join on every return. ReplayJobs=0 (or no worker): nothing is kicked and each body runs inline at its old place. Same code, same results.
        const bool jobs=jobsStart();replayJobs_.take();replayJobs_.timing(jobs&&profileSampled()); /* this frame's JOBS accounting (sample frames: timed) */
        // The fog clouds' noise (and its quantile table) is requested from the settings alone: derive() cannot be active before the table exists, so a
        // request gated on cf.active would never start it. One background generation per process; FogClouds=0 or density 0 still does nothing.
        if(quality.fogClouds&&quality.fogCloudDensity&&effects.fog&&debug==0&&!fogCloudNoise().ready.load(std::memory_order_acquire))fogCloudNoise().request();
        auto atmosphereJob=[&]{atmosphereWork(atmosphere,nearZ,w,h,debug);};
        JobJoin atmosphereJoin{replayJobs_,atmosphereDone};
        if(jobs)replayJobs_.kick(atmosphereDone,atmosphereJob);
        // 0.3.211 (task 31): the colour gate is the lighting weight gate: a source colour is direct*weight, so a zero-weight moon
        // draws no cascade, actor map or water moon map (tests/test_moon_cascade_weight.py).
        bool sourceActive[2]={NorthlightGI::dot(sourceColors[0],sourceColors[0])>1e-10f,NorthlightGI::dot(sourceColors[1],sourceColors[1])>1e-10f};
        // Terrain shadow candidates (the cached terrain chunks the game did not draw live): read by the directional passes' terrain draws.
        NorthlightTerrainCandidates::Selection<> terrainCandidates(directionalTerrainScratch);double terrainPrepareMs=0;
        auto terrainJob=[&]{
            std::chrono::steady_clock::time_point terrainPrepareStart;
            if(captureSampled)terrainPrepareStart=std::chrono::steady_clock::now();
            if(effects.shadows&&(sourceActive[0]||sourceActive[1])){
                // 0.3.175 (S2): this generation's terrain-outside-fixed list, when it belongs to these batches and fixed set.
                if(terrainBatchListGeneration==meshGeneration&&terrainBatchListSize==batches.size()&&terrainListFixed==&fixedTerrainChunks())
                    terrainCandidates.prepareListed(batches,shadowTerrainList,fixedTerrainChunks(),liveTerrainChunks,captureSampled);
                else terrainCandidates.prepare(batches,fixedTerrainChunks(),liveTerrainChunks,captureSampled);}
            if(captureSampled)terrainPrepareMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-terrainPrepareStart).count();
        };
        NorthlightJobs::Counter terrainDone;JobJoin terrainJoin{replayJobs_,terrainDone};
        if(jobs)replayJobs_.kick(terrainDone,terrainJob);
        {auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Static);
         updateStaticCasters(effects.shadows,&streamBudget);
         if(effects.shadows&&replayShadows){auto proofs=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Proofs);prepareStaticProofs();}}
        if(profile)profile->mark("StaticCasterUpload");
        bucket(NorthlightEffectsBuckets::Static);
        streamingCpuPeakMs=std::max(streamingCpuPeakMs,streamBudget.elapsedMs());
        streamingBudgetOverruns+=streamBudget.elapsedMs()>1.0;
        if(captureSampled){logf("WORLD streaming optionalCpuPeakMs=%.3f softBudgetOverruns=%u retiredMaterialMiB=%.2f",streamingCpuPeakMs,streamingBudgetOverruns,double(retiredMaterials.bytes())/1048576);streamingCpuPeakMs=0;streamingBudgetOverruns=0;}
        if(captureSampled){const auto& p=streamingPhases.peaks;
            logf("WORLD streaming phases peakMs admission=%.3f buffers=%.3f copies=%.3f preload=%.3f materials=%.3f commit=%.3f probes=%.3f static=%.3f",p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7]);
            const auto retired=NorthlightStreaming::cpuRetirement().stats();
            logf("WORLD streaming subphases peakMs owners=%.3f staticCache=%.3f proofs=%.3f retire=%.3f terrainUpload=%.3f replayUpload=%.3f retireAccepted=%llu retireOversized=%llu retireRefused=%llu retireParked=%zu retireDeferred=%llu retireSynchronous=%llu retireGenerationFrees=%llu generationDeferred=%u preparedOwners=%u",
                p[8],p[9],p[10],p[11],p[12],p[13],(unsigned long long)retired.accepted,(unsigned long long)retired.oversized,(unsigned long long)retired.refused,
                retirementBacklog.parked(),(unsigned long long)retirementBacklog.deferred,(unsigned long long)retirementBacklog.synchronous,(unsigned long long)retirementBacklog.generationFrees,generationDeferrals.load(),preparedOwnerCommits);
            logf("WORLD streaming splits peakMs replayExpire=%.3f replayBind=%.3f replayCreate=%.3f replayGrow=%.3f replayCopy=%.3f terrainArena=%.3f terrainIndices=%.3f terrainIndexUpload=%.3f staticPublish=%.3f replayCreatedPeak=%u replayTimeDeferred=%llu replayGrowths=%llu replayFallbacks=%llu replayBudgetOverrides=%llu terrainRollovers=%u ownerChangesPeak=%zu generationWaits=%u generationWaitPeakMs=%.3f",
                p[14],p[15],p[16],p[17],p[18],p[19],p[20],p[21],p[22],replayCreatedPeak,(unsigned long long)replayTimeDeferred,(unsigned long long)replayGrowths,(unsigned long long)replayFallbacks,(unsigned long long)replayBudgetOverrides,liveTerrainGPU.rollovers,ownerChangesPeak,generationWaits.load(),generationWaitPeakUs.load()*.001);
            replayCreatedPeak=0;ownerChangesPeak=0;
            streamingPhases.clear();}
        if(captureSampled){const auto& memory=admissionProbe.stats();
            logf("WORLD memory admission requests=%llu witnessHits=%llu fullScans=%llu regionQueries=%llu statusFailures=%llu malformedRegions=%llu fresh=1",
                (unsigned long long)memory.requests,(unsigned long long)memory.witnessHits,(unsigned long long)memory.fullScans,
                (unsigned long long)memory.regionQueries,(unsigned long long)memory.statusFailures,(unsigned long long)memory.malformedRegions);}
        const V pivot=staticPivotReady?staticFramePivot:shadowPivot();
        actorShadowOrigin[0]=pivot.x;actorShadowOrigin[1]=pivot.y;actorShadowOrigin[2]=pivot.z;actorShadowOriginValid=true;
        // Raw pivot above (ActorShadowRadius) and for the static caster request; the cascade frames,
        // their cache placement and the static prebuild use the jump-stable anchor.
        const bool anchorNewMap=cascadeAnchorMap!=lastRequest.map;if(anchorNewMap)cascadeAnchorMap=lastRequest.map; /* the requested map: the snapshot can lag */
        // 0.3.209 (task 29): the actor shadow map is centred on a leashed anchor of the raw pivot (not texel-snapped, not the snapped cascade pivot): the pivot is
        // camera-derived and drifts while the camera orbits an idle player, so the centre holds still within Leash and is dragged along beyond it. Per source; reset on a new map.
        const bool actorMapOn=NorthlightQuality::actorShadowMap(quality,effects.shadows)&&actorShadowPS&&actorTilesPS&&actorShadow[0]&&actorShadow[1]&&actorTiles[0]&&actorTiles[1]&&actorTilesMid&&actorVisibility;
        float actorFrameMatrix[2][16];bool actorFrameOn[2]={}; /* actorMapOn with a finite matrix: checked here once, not per replay in the near loop */
        for(int source=0;source<2;++source){
            if(anchorNewMap)actorCenterValid[source]=false;
            if(!actorMapOn){actorCenterValid[source]=false;memset(actorFrameMatrix[source],0,64);continue;}
            actorCenter[source]=NorthlightActorShadowMap::leash(actorCenter[source],actorCenterValid[source],pivot,sourceDirections[source]);actorCenterValid[source]=true;
            NorthlightActorShadowMap::matrix(actorCenter[source],pivot,sourceDirections[source],actorFrameMatrix[source]);
            actorFrameOn[source]=NorthlightActorShadowMap::finiteMatrix(actorFrameMatrix[source]);if(!actorFrameOn[source])actorCenterValid[source]=false;
        }
        const V cascadePivot=cascadeAnchor.update(pivot,vec(context.camera),uint32_t(GetTickCount()),anchorNewMap);
        int firstSource=sourceActive[0]||!sourceActive[1]?0:1;
        NorthlightWorldMath::ShadowFrame cascadeFrames[2][2];
        for(int source=0;source<2;++source)for(int cascade=0;cascade<2;++cascade){
            const float radius=cascade==0?48.f:192.f;
            cascadeFrames[source][cascade]=NorthlightWorldMath::shadowFrame(cascadePivot,sourceDirections[source],radius);
            NorthlightWorldMath::shadowMatrixFrom(cascadeFrames[source][cascade],radius,sourceMatrices[source][cascade]);
        }
        const uint64_t chunkHash=liveChunkHash();
        // 0.3.152: each drawn slot's cache placement and matrix, once per frame (the
        // cascade loop reuses both; a key changes only in its own slot's iteration).
        NorthlightWorldMath::ShadowCachePlacement slotPlacement[4];
        for(int source=0;source<2;++source)if(effects.shadows&&sourceActive[source])for(int cascade=0;cascade<2;++cascade){
            const int slot=source*2+cascade;const float radius=cascade==0?48.f:192.f;const ShadowCacheKey& key=shadowCacheKey[slot];
            slotPlacement[slot]=NorthlightWorldMath::shadowCachePlacement(cascadeFrames[source][cascade],key.frame,key.valid,
                !different(key.direction,sourceDirections[source],1e-6f),long(ShadowCacheMargin));
            NorthlightWorldMath::shadowMatrixFrom(slotPlacement[slot].frame,radius*float(ShadowCacheSize)/1024.f,staticSlotMatrix[slot]);
        }
        struct StaticPlanJoin {WorldRenderer& r;~StaticPlanJoin(){try{r.staticCasters.settle();}catch(...){}}} staticPlanJoin{*this}; /* before any return below */
        if(effects.shadows){const auto kickStart=std::chrono::steady_clock::now();staticPlanKick(slotPlacement,sourceActive);staticKickMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-kickStart).count();}
        if(effects.shadows&&replayShadows)pointCalculateReplayBounds(); // both cascades and the point cube
        const unsigned diagnosticCapture=debug==1&&gpuDiagnosticArmed&&gpuDiagnosticCaptures<4?gpuDiagnosticCaptures+1:0;
        if(diagnosticCapture&&gpuDiagnosticDirectory.empty()){
            const std::string parent=root+"northlight-diagnostics";   /* beside northlight-renderer.log */
            CreateDirectoryA(parent.c_str(),nullptr);
            gpuDiagnosticDirectory=parent+"/session-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount());
            CreateDirectoryA(gpuDiagnosticDirectory.c_str(),nullptr);
            logf("WORLD GPU diagnostic directory=%s",gpuDiagnosticDirectory.c_str());
        }
        d->SetDepthStencilSurface(nullptr);for(int i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
        for(int i=0;i<14;++i)d->SetTexture(i,nullptr);
        const struct{D3DRENDERSTATETYPE s;DWORD v;} states[]={
            {D3DRS_ZENABLE,TRUE},{D3DRS_ZWRITEENABLE,TRUE},{D3DRS_ZFUNC,D3DCMP_LESSEQUAL},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_FOGENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_DEPTHBIAS,0},{D3DRS_SLOPESCALEDEPTHBIAS,0},{D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_MULTISAMPLEMASK,0xffffffff}};
        for(auto& s:states)d->SetRenderState(s.s,s.v);
        if(!effects.shadows&&!neutralShadowMaps){
            // The sun, moon and fog keep their direct lighting when shadows
            // are off. Neutral depth plus the constant transform below gives
            // visibility 1 without sampling a previous frame's shadow maps.
            D3DVIEWPORT9 vp={0,0,1024,1024,0,1};d->SetViewport(&vp);
            for(auto* surface:shadowSurface){
                if(!check(d->SetRenderTarget(0,surface),"neutral shadow target")||
                   !check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0),"neutral shadow clear"))return false;
            }
            neutralShadowMaps=true;
        }
        d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
        unsigned culledBatches[2]={},drawnBatches[2]={};
        size_t replayConstantBytes=0,replayConstantCalls=0,replayPosePrepared=0,replayPoseReused=0;
        bucket(NorthlightEffectsBuckets::ShadowSetup);
        if(!jobs)terrainJob(); /* 0.3.200 (jobs): ReplayJobs=0 prepares here, as before; ReplayJobs=1 joins at the first terrain draw below */
        bucket(NorthlightEffectsBuckets::TerrainSelection);
        // 0.3.200 (jobs): the replay culling is kicked once (replayCullKick; 0.3.217: from the first rendering slot, before its static and terrain draws), joined per slot and here.
        bool replayCullsKicked=false;struct CullSettle {WorldRenderer& r;~CullSettle(){r.replayCullSettle();}} cullSettle{*this};
        bool anyShadowCacheRender=false;
        ++shadowPasses;
        if(NorthlightStaticPrebuild::Scheduler::Enabled)for(int source=0;source<2;++source)staticPrebuild.observe(source,effects.shadows&&sourceActive[source],rawSourceDirections[source],GetTickCount());
        for(int source=0;source<2;++source)if(!actorMapOn||!sourceActive[source]){actorValid[source]=false;actorHold[source].invalidate();} /* 0.3.209 (task 29): a source that draws no near map keeps no actor map */
        for(int source=0;source<2;++source){
          if(!effects.shadows||!sourceActive[source])continue;
          auto& matrices=sourceMatrices[source];bool nearRendered=false; /* this source's near map redrawn with fresh replays */
          for(int cascade=0;cascade<2;++cascade){
            const auto phaseStart=std::chrono::steady_clock::now();
            auto phaseNow=[&](){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-phaseStart).count();};
            double selectMs=0,localMs=0,staticMs=0,dynamicMs=0,selectPlanMs=0;const double planBefore=staticCasters.stats().planMs;
            uint64_t staticDraws=0,staticInstances=0,replayDraws=0,replayTriangles=0;
            unsigned localDraws=0;
            const int slot=source*2+cascade;
            ShadowCacheKey& key=shadowCacheKey[slot];
            const auto& placement=slotPlacement[slot]; /* 0.3.152: placed once per frame, before the plan kick */
            long offX=placement.offX,offY=placement.offY;float dz=placement.dz;const char* reason=placement.reason;
            d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
            const float* cachedMatrix=staticSlotMatrix[slot];
            if(!reason&&NorthlightLocalShadowSignature::needsRefresh(key.localContentKnown,key.localContent,key.serial,
                bool(uploadedLocalShadowRecords),localShadowSignature(key,cachedMatrix),meshGeneration))reason="local-content";
            bool partialStatic=false;
            // 0.3.151 StaticCacheSlices>1 (static_cache_slices.h): a rect redraw in horizontal bands,
            // one per frame. Any other reason redraws everything now. The key keeps the pre-cycle
            // content until the last band, which takes the rect path below with the cycle's reason.
            auto& slices=staticSlices[slot];if(reason)slices.reset();
            bool sliceBand=false; /* an intermediate band: drawn before the cascade action, without a reason */
            const uint64_t staticNow=reason?0:staticSignature(cachedMatrix);
            const bool staticChanged=!reason&&key.staticSignature!=staticNow;
            if(slices.current(staticNow)&&staticChanged&&!diagnosticCapture&&key.staticContent.valid&&key.staticContent.instancing==staticCasters.stats().instancing){
                staticDirty=slices.band(); /* unchanged since the cycle (re)started: its next band */
                if(slices.last()){reason=slices.reason;partialStatic=true;}else sliceBand=true;
            }else if(staticChanged){reason="static-models";
                partialStatic=StaticCacheDirtyRects&&key.valid&&staticDirtyRects(key,cachedMatrix);
                if(partialStatic)reason="static-models-partial";else ++shadowCacheStaticFullRenders;
                if(NorthlightStaticSlices::sliceable(quality.staticCacheSlices,partialStatic,diagnosticCapture!=0,StaticCacheDirtyRectVerify)){
                    staticSliceRestarts+=slices.active;
                    slices.start(staticDirty,quality.staticCacheSlices,reason,staticNow,long(ShadowCacheSize),StaticCacheDirtyTile,StaticCacheDirtyMaxRects);
                    staticDirty=slices.band();if(!slices.last()){reason=nullptr;sliceBand=true;}
                }else if(partialStatic&&slices.active){ /* finish now; drawn bands hold content newer than the key */
                    staticDirtyFootprints=staticDirty;staticDirtyFootprints.insert(staticDirtyFootprints.end(),slices.drawn.begin(),slices.drawn.end());
                    NorthlightShadowBounds::dirtyRects(staticDirtyFootprints,long(ShadowCacheSize),StaticCacheDirtyTile,StaticCacheDirtyMaxRects,staticDirty);}
            }else if(slices.active){ /* content is back at the recorded key: redraw the bands drawn so far */
                NorthlightShadowBounds::dirtyRects(slices.drawn,long(ShadowCacheSize),StaticCacheDirtyTile,StaticCacheDirtyMaxRects,staticDirty);
                if(staticDirty.empty())slices.reset();
                else if(key.staticContent.valid&&key.staticContent.instancing==staticCasters.stats().instancing){reason=slices.reason;partialStatic=true;}
                else{reason="static-models";++shadowCacheStaticFullRenders;}
            }
            selectMs=phaseNow();selectPlanMs=staticCasters.stats().planMs-planBefore;
            if(reason||sliceBand)anyShadowCacheRender=true;
            // One cache render: the full clear+redraw, or (partial) the identical
            // ordered sequence limited to the disjoint dirty rects. -1: device failure.
            // `real` false is the debug self-check's full render into a scratch target;
            // `band` an intermediate StaticCacheSlices band (counted as a band, not a partial render).
            double staticStart=0;
            auto renderCache=[&](IDirect3DSurface9* target,bool partial,bool real,bool band=false)->int{
                struct ScissorOff {IDirect3DDevice9* d;bool on;~ScissorOff(){if(on)d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);}} scissorOff{d,partial};
                d->SetDepthStencilSurface(nullptr);if(!check(d->SetRenderTarget(0,target),"shadow cache target"))return -1;
                d->SetDepthStencilSurface(shadowCacheDepth);D3DVIEWPORT9 cvp={0,0,ShadowCacheSize,ShadowCacheSize,0,1};d->SetViewport(&cvp);
                if(partial){
                    // Same viewport/clip; the scissor only discards fragments, so
                    // texels inside it are the full redraw and all others hold it already.
                    // Rects are disjoint, so per-draw scissor switches never reorder a pixel's fragments.
                    // Explicit clear rects (colour and the shared depth) with the scissor still off.
                    staticDirtyClears.clear();for(const auto& r:staticDirty){staticDirtyClears.push_back({LONG(r.left),LONG(r.top),LONG(r.right),LONG(r.bottom)});shadowCachePartialTexels+=(unsigned long long)r.area();}
                    d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
                    if(!staticDirtyClears.empty()&&!check(d->Clear(DWORD(staticDirtyClears.size()),staticDirtyClears.data(),D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0),"clear shadow cache rects"))return -1;
                    d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);if(!band){++shadowCachePartialRenders;shadowCachePartialRects+=unsigned(staticDirty.size());}
                }else if(!check(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0),"clear shadow cache"))return -1;
                d->SetVertexDeclaration(shadowDecl);d->SetStreamSource(0,vertices,0,sizeof(NorthlightGI::WorldVertex));for(int i=0;i<4;++i)d->SetStreamSourceFreq(i,1);d->SetIndices(indices);
                d->SetVertexShader(cachedShadowVS);d->SetVertexShaderConstantF(0,cachedMatrix,4);d->SetPixelShader(cachedShadowPS);
                IDirect3DPixelShader9* boundCachePS=cachedShadowPS;
                UINT boundPage=UINT_MAX;const NorthlightShadowBounds::TexelRect* staticScissor=nullptr;
                /* 0.3.215 (task 45): the flat bitmap answers the per-batch fixed-chunk test without a red-black tree walk (same membership: it falls back to the set off-grid or when it does not describe this set). */
                const auto& fixedSet=fixedTerrainChunks();const bool fixedBits=fixedTerrainBits.source()==&fixedSet;
                const bool affine=NorthlightShadowBounds::affineLightMatrix(cachedMatrix);
                for(auto& b:batches){
                    if(b.terrain&&!(fixedBits?fixedTerrainBits.contains({b.chunkX,b.chunkY}):fixedSet.count({b.chunkX,b.chunkY})!=0))continue;
                    if(affine&&NorthlightShadowBounds::directionalClipRejectAffine(b.boundsLow,b.boundsHigh,cachedMatrix)){if(real)++culledBatches[cascade];continue;}
                    size_t passes=1;
                    if(partial){NorthlightShadowBounds::TexelRect footprint;const bool known=NorthlightShadowBounds::texelFootprint(b.boundsLow,b.boundsHigh,cachedMatrix,long(ShadowCacheSize),StaticCacheDirtyMargin,footprint);
                        staticDirtyHits.clear();for(const auto& r:staticDirty)if(!known||NorthlightShadowBounds::intersects(footprint,r))staticDirtyHits.push_back(&r);
                        if(staticDirtyHits.empty()){++shadowCachePartialSkippedDraws;continue;}passes=staticDirtyHits.size();}
                    if(real)++drawnBatches[cascade];
                    const bool interior=affine&&NorthlightShadowBounds::depthFullyInsideAffine(b.boundsLow,b.boundsHigh,cachedMatrix);
                    const bool opaque=uploadedAlphaCutoffs[b.material]<=0;
                    IDirect3DPixelShader9* ps=interior?(opaque&&cachedOpaqueFastPS?cachedOpaqueFastPS:cachedFastPS):(opaque?cachedOpaquePS:cachedShadowPS);
                    if(!ps)ps=cachedShadowPS;
                    if(ps!=boundCachePS){if(!check(d->SetPixelShader(ps),"static cache depth variant"))return -1;boundCachePS=ps;}
                    float material[]={1,1,1,uploadedAlphaCutoffs[b.material]};d->SetPixelShaderConstantF(0,material,1);d->SetTexture(0,materials[b.material]);
                    if(!bindMeshPage(b,boundPage))return -1;
                    for(size_t k=0;k<passes;++k){
                        if(partial&&staticScissor!=staticDirtyHits[k]){const RECT r={LONG(staticDirtyHits[k]->left),LONG(staticDirtyHits[k]->top),LONG(staticDirtyHits[k]->right),LONG(staticDirtyHits[k]->bottom)};d->SetScissorRect(&r);staticScissor=staticDirtyHits[k];}
                        if(!check(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,b.minVertex,b.vertexCount,b.start,b.count),"static shadow cache draw"))return -1;
                    }
                }
                if(real){localDraws=drawnBatches[cascade];localMs=phaseNow()-selectMs;staticStart=phaseNow();}
                const bool ok=drawStaticCasters(cachedMatrix,partial?&staticDirty:nullptr);
                if(partial){DWORD enabled=FALSE;if(StaticCacheDirtyRectVerify&&SUCCEEDED(d->GetRenderState(D3DRS_SCISSORTESTENABLE,&enabled))&&!enabled)++shadowCacheScissorLeaks;shadowCachePartialSkippedDraws+=unsigned(staticCasters.stats().rectSkipped);}
                return ok?1:0;
            };
            if(sliceBand){
                // Into the cache target only; the cascade's own schedule decides whether this frame's map
                // unites it. Any failure drops the cycle and the key: a full redraw follows.
                const int drawn=renderCache(shadowCacheSurface[slot],true,true,true);if(drawn<0)return false;
                staticMs=phaseNow()-staticStart;staticDraws=staticCasters.stats().drawCalls;staticInstances=staticCasters.stats().instances;
                if(drawn>0){slices.drew();++staticSliceBands;}else{slices.reset();key.valid=false;key.staticContent.valid=false;}
                d->SetTexture(0,nullptr);d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
            }
            // Far/NearShadowInterval>1 only: between updates keep the last complete map with
            // its own matrix, unless its static cache needs any re-render this frame. A frame
            // without fresh replays keeps a complete map even then (one frame; the re-render
            // and its prebuilt plan wait) and makes the next frame capture.
            auto& reuse=cascade?farShadow[source]:nearShadow[source];const unsigned interval=cascade?quality.farShadowInterval:quality.nearShadowInterval;
            const bool pull=cascade==1&&NorthlightQuality::pullFar(quality.nearShadowInterval,interval,shadowPasses,reuse,nearRendered);
            // 0.3.209 (task 29): a near map drawn with the actor-map skip has no shadow of those casters of its own; once its actor map is gone (the source went
            // inactive, ActorShadowDetail off, a non-finite frame) it is redrawn instead of reused.
            const bool actorLost=cascade==0&&nearActorSkip[source]&&!actorValid[source];
            const auto action=NorthlightQuality::cascadeAction(reuse,interval,shadowPasses,!reason&&key.valid&&!pull&&!actorLost,diagnosticCapture!=0,replaysComplete);
            cascadeActions[slot]=action==NorthlightQuality::CascadeAction::Render?'R':action==NorthlightQuality::CascadeAction::Defer?'D':'U';
            if(action!=NorthlightQuality::CascadeAction::Render){
                if(action==NorthlightQuality::CascadeAction::Defer){captureDemand=true;++captureDeferrals;}else ++(cascade?farReuses:nearReuses);
                memcpy(matrices[cascade],reuse.matrix,64);
                if(profile)profile->mark(source==0?(cascade==0?"SunNear":"SunFar"):(cascade==0?"MoonNear":"MoonFar"));bucket(NorthlightEffectsBuckets::Bucket(NorthlightEffectsBuckets::SunNear+source*2+cascade));continue;}
            reuse.begin(interval); /* nothing written yet; a failure below stays unusable */
            if(cascade==0)actorValid[source]=nearActorSkip[source]=false; /* 0.3.209 (task 29): redrawn with this near map below; a reused near map (above) keeps the actor map and matrix it was made with */
            if(cascade==0)nearRendered=captureMode==CaptureFresh&&interval>1&&actorShadows;
            // 0.3.217 (jobs): this slot's matrices (set before the loop), the packets and the static proofs are final here; with the bounds job already finished
            // its own cull runs on a worker while the slot draws its static cache and terrain. Not finished: nothing here, the old order (inline test) holds.
            if(jobs&&actorShadows&&!replayCullsKicked)replayCullKickOwn(slot);
            if(NorthlightStaticPrebuild::Scheduler::Enabled&&reason)staticPrebuild.rerender(source,cascade,cachedMatrix,!std::strcmp(reason,"direction"),staticCasters.planReady(cachedMatrix),[&](const float* m){staticCasters.discardPlan(m);});
            if(reason){
                // Static batches only, over the margin extent, keyed for reuse.
                // Reuse the already selected matrix for content-only refreshes.
                // The union below retains its integer offset and depth delta.
                const int rendered=renderCache(shadowCacheSurface[slot],partialStatic,true);if(rendered<0)return false;
                if(slices.active){staticSliceBands+=partialStatic;slices.reset();} /* the last band (or a full/finishing redraw) completes the cycle */
                const bool staticOK=rendered>0;
                staticMs=phaseNow()-staticStart;staticDraws=staticCasters.stats().drawCalls;staticInstances=staticCasters.stats().instances;
                // A rect redraw relies on the recorded content matching what is
                // in the texture: any doubt (failure, instancing mode flip) forces a full redraw.
                const bool recordable=StaticCacheDirtyRects&&staticOK&&(!partialStatic||key.staticContent.instancing==staticCasters.stats().instancing);
                if(recordable){try {staticCasters.record(cachedMatrix,key.staticContent);}catch(...){key.staticContent.valid=false;}}else key.staticContent.valid=false;
                if(StaticCacheDirtyRectVerify&&partialStatic&&staticOK&&!verifyStaticCache(slot,[&](IDirect3DSurface9* t){return renderCache(t,false,false);}))return false;
                key.valid=staticOK&&(recordable||!StaticCacheDirtyRects);key.frame=placement.frame;key.direction=sourceDirections[source];key.serial=meshGeneration;key.chunkHash=chunkHash;key.staticSignature=staticSignature(cachedMatrix);key.localContent=localShadowSignature(key,cachedMatrix);key.localContentKnown=bool(uploadedLocalShadowRecords);++shadowCacheRenders;
                DWORD tick=GetTickCount();if(NorthlightDiagnostics::enabled()&&(!shadowCacheLogAt||tick-shadowCacheLogAt>=1000)){shadowCacheLogAt=tick;logf("WORLD shadow cache rerender source=%d cascade=%d reason=%s anchorRetained=%u prebuilds=%u prebuildHits=%u prebuildMisses=%u prebuildDiscards=%u",source,cascade,reason,unsigned(!placement.reason),staticPrebuild.stats.built,staticPrebuild.stats.hits,staticPrebuild.stats.misses,staticPrebuild.stats.discarded);}
            } else {key.serial=meshGeneration;++shadowCacheReuses;}
            if(diagnosticCapture){
                char name[80];std::snprintf(name,sizeof name,"s%d-c%d-static",source,cascade);
                NorthlightWorldDiagnostics::dump(d,shadowCacheSurface[slot],gpuDiagnosticDirectory,name,diagnosticCapture);
                NorthlightWorldDiagnostics::shadowFrame(gpuDiagnosticDirectory,diagnosticCapture,source,cascade,cachedMatrix,matrices[cascade],offX,offY,dz,key.serial,key.staticSignature,reason);
            }
            // The slot's cache is complete for cachedMatrix now: no intermediate band, no pending cycle, unchanged or just re-rendered, and its content record
            // describes that texture (staticM2 skip, below). A failed render cleared key.valid / staticContent.valid.
            const bool cacheCurrent=key.valid&&key.staticContent.valid&&!sliceBand&&!slices.active&&(reason||!staticChanged)&&staticCasters.recordCurrent(key.staticContent,cachedMatrix);
            const double dynamicStart=phaseNow();
            // Dynamic part of this frame: live terrain and replays into the scratch map.
            d->SetDepthStencilSurface(nullptr);if(!check(d->SetRenderTarget(0,shadowScratchSurface),"shadow scratch target"))return false;
            d->SetDepthStencilSurface(shadowDepth);D3DVIEWPORT9 vp={0,0,1024,1024,0,1};d->SetViewport(&vp);
            if(!check(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0),"clear shadow"))return false;
            d->SetVertexDeclaration(shadowDecl);d->SetStreamSource(0,vertices,0,sizeof(NorthlightGI::WorldVertex));for(int i=0;i<4;++i)d->SetStreamSourceFreq(i,1);d->SetIndices(indices);
            d->SetVertexShader(shadowVS);d->SetVertexShaderConstantF(0,matrices[cascade],4);d->SetPixelShader(shadowPS);
            // Cached-mesh terrain chunks that the game did not draw live this frame.
            UINT terrainBoundPage=UINT_MAX;
            const bool replayAffine=NorthlightShadowBounds::affineLightMatrix(matrices[cascade]);
            if(jobs)replayJobs_.wait(terrainDone); /* 0.3.200 (jobs): the candidates' first reader */
            if(!terrainCandidates.forEach(batches,fixedTerrainChunks(),liveTerrainChunks,[&](const Batch& b){
                if(replayAffine&&NorthlightShadowBounds::clipRejectAffine(b.boundsLow,b.boundsHigh,matrices[cascade])){++culledBatches[cascade];return true;}
                ++drawnBatches[cascade];float material[]={1,1,1,uploadedAlphaCutoffs[b.material]};
                d->SetPixelShaderConstantF(0,material,1);d->SetTexture(0,materials[b.material]);
                return bindMeshPage(b,terrainBoundPage)&&check(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,b.minVertex,b.vertexCount,b.start,b.count),"static shadow cache draw");
            }))return false;
            if(liveDirectionalIndexCount){
                float opaque[]={1,1,1,-1};d->SetPixelShaderConstantF(0,opaque,1);d->SetTexture(0,nullptr);
                d->SetStreamSource(0,liveTerrainGPU.vertices(),0,sizeof(NorthlightGI::WorldVertex));d->SetIndices(liveIndicesGPU);NorthlightDynamicRing::touch(liveIndexRing,fence()); /* 0.3.192 (DXVK3): drawn this frame */
                if(!check(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,liveTerrainGPU.vertexCapacity(),UINT(liveIndexBase+liveTerrainIndexCount),UINT(liveDirectionalIndexCount/3)),"live terrain shadow"))return false;
            }
            // Original model transforms and bone palette survive; replace view->clip only.
            if(diagnosticCapture){
                char name[80];std::snprintf(name,sizeof name,"s%d-c%d-terrain",source,cascade);
                NorthlightWorldDiagnostics::dump(d,shadowScratchSurface,gpuDiagnosticDirectory,name,diagnosticCapture);
            }
            if(actorShadows){ /* 0.3.158: ActorShadows=0 draws no replays; terrain above and the union below stay */
            replayBoundsJoin(); /* 0.3.143: first pointBounds reader */
            if(jobs&&!replayCullsKicked){replayCullsKicked=true;replayCullKick(slot,sourceActive);} /* 0.3.200 (jobs): the bounds are final; the later slots; this slot tests inline unless 0.3.217 kicked its own job early */
            const uint8_t* cull=replayCullFor(slot,cachedMatrix,matrices[cascade]); /* null: tested inline below, as before */
            float rows[16];NorthlightWorldMath::replayProjection(context.inverseView,matrices[cascade],rows);
            d->SetPixelShader(replayPS);
            // Fresh banks after the static pass. Fold the shadow projection into
            // desired constants first; compare only the shader's captured ranges.
            NorthlightReplayPoses::Pass<BOOL> poseConstants;
            NorthlightReplayDrawState::Cache replayBindings(d);
            if(captureSampled)replayMeshKeys.clear();const size_t constantCallsBefore=replayConstantCalls;
            // 0.3.149 RenderProfile: a profile sample frame splits this loop into own logic / state
            // calls / draws (ReplaySplit, clock reads per span: a secondary number) and records the
            // drawn packets for the capture-waste count; with DiagReplayProbe the sun near map
            // records them for the probe and is timed as a whole. Every other frame (always with
            // RenderProfile=0) runs the Off instantiation, whose hooks are empty: the 0.3.148 loop.
            const bool profiled=profileSampled(),probeRecord=replayProbeActive()&&slot==0&&captureMode==CaptureFresh&&!diagnosticCapture;
            // 0.3.209 (task 29): the near cascade leaves the replays wholly inside the actor map's inset to the actor map, which draws
            // exactly those (actorReplays): no replay is drawn into both. The list is reserved first, so recording a skip never throws.
            bool nearSkip=cascade==0&&actorFrameOn[source];
            if(nearSkip){actorReplays.clear();try{actorReplays.reserve(replays.size());}catch(...){nearSkip=false;}}
            if(cascade==0)nearActorSkip[source]=nearSkip;
            unsigned nearSkipped=0;
            const bool sideDiag=nearSkip&&NorthlightDiagnostics::enabled(); /* diagnostics only: the side tracker, the hold counters */
            if(nearSkip)actorHold[source].begin(sideDiag,unsigned(frames)+1);else if(cascade==0)actorHold[source].invalidate();
            if(sideDiag)actorSide[source].begin();
            auto replayLoop=[&](auto& split)->bool{
              using NorthlightRenderThreadProbe::Own;
              split.start();
              for(size_t index=0;index<replays.size();++index){const auto& p=replays[index];
                // Only full geometric proof plus cached-volume containment can
                // replace a live rigid draw. Dynamic/alpha/unknown draws survive.
                if(key.valid&&(p->staticProofMask&(1u<<slot))&&staticCasters.stats().readyModels&&
                   (cull?(cull[index]&1)!=0:StaticShadow::containsBounds(cachedMatrix,p->staticProofLow,p->staticProofHigh))){
                    // Diagnostic only (no behaviour change): the proved model must be in
                    // this slot's committed cache content. Must stay 0 (same-frame commit).
                    if(key.staticContent.valid&&!key.staticContent.contains(p->staticProofModel,p->staticProofRevision))++staticDedupUncommitted;
                    ++staticDedupSkipped;continue;
                }
                // The actor map takes a caster whose bounds lie fully inside the inset, and keeps a skinned one while they lie within Stay (hysteresis,
                // actor_side_hold.h). Its identity there: the palette root, or the centre of its valid bounds (a rigid attachment has no root).
                bool inActor=false;
                const float* position=p->shadowSkinned?p->position():nullptr; /* once per replay: the hold and the diagnostics share it */
                if(nearSkip){const float* matrix=actorFrameMatrix[source];const float extent=NorthlightActorShadowMap::insetExtent(p->pointBounds.valid,p->pointBounds.low,p->pointBounds.high,matrix,NorthlightActorShadowMap::Stay);
                    if(extent<=NorthlightActorShadowMap::Stay){
                        if(!p->shadowSkinned)inActor=extent<=NorthlightActorShadowMap::Inset;
                        else{const std::uint64_t model=NorthlightActorSideHold::holdModel(*p);float centre[3];const float* at=position; /* a rigid attachment's model differs from a rooted caster's */
                            if(!p->captureRootValid){
                                if(!at){for(unsigned k=0;k<3;++k)centre[k]=.5f*(p->pointBounds.low[k]+p->pointBounds.high[k]);at=centre;}} /* no position at all: the centre of the valid bounds (finite extent) */
                            inActor=actorHold[source].decide(model,at,extent);
                            if(inActor&&position)actorHold[source].stamp(model,position,matrix,extent);}}
                    else if(p->shadowSkinned&&!p->pointBounds.valid&&p->shared){ /* no bounds this frame: stays in the actor map only by the hold's carry rule */
                        if(position)inActor=actorHold[source].carry(NorthlightActorSideHold::holdModel(*p),position,matrix);}}
                if(inActor){actorReplays.push_back(uint32_t(index));++nearSkipped;
                    if(sideDiag&&p->shadowSkinned)(p->shared?actorSide[source].note(NorthlightActorSide::key(p->shared.get(),p->decl,p->count,position!=nullptr,position),true,p->selfShadow,p->captureRootValid):actorSide[source].unkeyed());continue;}
                if(sideDiag&&p->shadowSkinned){auto& side=actorSide[source];if(p->shared)side.note(NorthlightActorSide::key(p->shared.get(),p->decl,p->count,position!=nullptr,position),false,p->selfShadow,p->captureRootValid,!p->pointBounds.valid);else side.unkeyed();
                    /* no valid bounds, yet the palette root lies inside the inset: it belongs to the actor map and fell to the near cascade for lack of bounds;
                       without a shared snapshot (Unsupported) it can never have bounds: unsupportedNear */
                    if(!p->pointBounds.valid&&p->captureRootValid&&NorthlightActorShadowMap::insideInset(true,p->captureRoot,p->captureRoot,actorFrameMatrix[source])){if(p->shared)side.invalidNear();else side.unsupportedNear();}}
                // A rigid M2 doodad at its resting pose is in this slot's static cache with the same geometry (prepareStaticM2): drawn there, not twice.
                // Hard condition: this slot's cache is current for this frame (rendered or unchanged, no band or rect redraw pending) and its
                // recorded content holds the model at this revision with the replay's box inside the drawn bounds. Otherwise the replay is drawn.
                if(p->staticM2Mask&(1u<<slot)&&p->staticM2Model&&cacheCurrent&&staticCasters.stats().readyModels&&StaticShadow::containsBounds(cachedMatrix,p->staticProofLow,p->staticProofHigh)&&
                   key.staticContent.containsBox(*p->staticM2Model,p->staticProofRevision,p->staticProofLow,p->staticProofHigh,StaticM2Dedup::BoundsPad)){
                    ++staticM2Skipped;++replaySlotM2Skipped[slot];continue;
                }
                ++replaySlotTested[slot];replaySlotBounded[slot]+=p->pointBounds.valid;
                if(p->pointBounds.valid&&(cull?(cull[index]&2)!=0:(replayAffine&&NorthlightShadowBounds::clipRejectAffine(V(p->pointBounds.low[0],p->pointBounds.low[1],p->pointBounds.low[2]),V(p->pointBounds.high[0],p->pointBounds.high[1],p->pointBounds.high[2]),matrices[cascade])))){++culledReplayDraws;++replaySlotCulled[slot];continue;}
                split.mark(Own);
                if(!submitReplay(p.get(),replayBindings,poseConstants,rows,split,replayConstantBytes,replayConstantCalls,[&](HRESULT h,const char* s){return check(h,s);}))return false;
                split.drawn(p.get());
                ++replayDraws;replayTriangles+=p->count;
                if(captureSampled)try{replayMeshKeys.push_back({p->index,p->stream[0],p->decl,p->offset[0],p->start,p->count,p->base,p->constantGroup});}catch(...){}
              }
              split.mark(Own);
              return true;
            };
            ReplaySplit split;split.timed=profiled;if(profiled)split.used=&replayProfileUsed;
            if(probeRecord){replayProbeList.clear();split.record=&replayProbeList;}
            const int64_t loopStart=probeRecord?QpcClock::now():0;
            if(profiled||probeRecord){if(!replayLoop(split))return false;}
            else {NorthlightRenderThreadProbe::Off off;if(!replayLoop(off))return false;}
            const double realLoopMs=probeRecord&&captureFrequency.QuadPart>0?double(QpcClock::now()-loopStart)*1000.0/double(captureFrequency.QuadPart):0.0;
            replayPosePrepared+=poseConstants.prepared;replayPoseReused+=poseConstants.reused;
            if(captureSampled){
                /* meshRepeats: draws whose geometry range+declaration already appeared in this
                   pass (an upper bound for instancing: only stream 0 is keyed, so multi-stream
                   declarations may overcount); exactRepeats also share the pose bank. */
                std::sort(replayMeshKeys.begin(),replayMeshKeys.end());size_t meshRepeats=0,exactRepeats=0;
                for(size_t k=1;k<replayMeshKeys.size();++k)if(replayMeshKeys[k].sameMesh(replayMeshKeys[k-1])){++meshRepeats;exactRepeats+=replayMeshKeys[k].group==replayMeshKeys[k-1].group;}
                const auto& c=replayBindings.counts;
                logf("WORLD replay bindings source=%d cascade=%d draws=%llu meshRepeats=%zu exactRepeats=%zu declarations=%u streams=%u indices=%u shaders=%u textures=%u samplers=%u cutoffs=%u constantCalls=%zu texturesSkipped=%u samplersSkipped=%u opaqueKept=%u opaqueSkip=%d",
                    source,cascade,(unsigned long long)replayDraws,meshRepeats,exactRepeats,c.declarations,c.streams,c.indices,c.shaders,c.textures,c.samplers,c.cutoffs,replayConstantCalls-constantCallsBefore,
                    c.legacyTextures-c.textures,c.legacySamplers-c.samplers,c.opaqueKept,int(replayBindings.skipping()));
            }
            if(profiled){const auto& c=replayBindings.counts;keepReplaySplit(slot,split,replayDraws,c.declarations+c.streams+c.indices+c.shaders+c.textures+c.samplers+c.cutoffs);}
            // DiagReplayProbe: this map's drawn replays once more into a private target (image
            // unchanged); its GPU time gets its own profile mark in probing windows. Never on a
            // diagnostic capture frame (probeRecord), so it may precede the live dump below.
            if(probeRecord&&!split.recordFailed){const bool marks=profile&&replayProbeMode!=NorthlightRenderThreadProbe::ProbeOff;
                if(marks)profile->mark("SunNearLoop");replayProbe(rows,realLoopMs);if(marks)profile->mark("ReplayProbe");}
            if(cascade==0&&actorFrameOn[source]){ // 0.3.209 (task 29): this source's actor map, in the same iteration as its near map (same replays, same frame)
                if(profile)profile->mark(source==0?"SunNearReplays":"MoonNearReplays");bucket(NorthlightEffectsBuckets::Bucket(NorthlightEffectsBuckets::SunNear+source*2)); /* the near terrain and replays so far (own mark name: SunNear/MoonNear follow the union); the actor map is billed on its own below */
                const float* am=actorFrameMatrix[source];
                d->SetDepthStencilSurface(nullptr);if(!check(d->SetRenderTarget(0,actorShadowSurface[source]),"actor shadow target"))return false;
                d->SetDepthStencilSurface(shadowDepth);d->SetViewport(&vp);
                if(!check(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0),"clear actor shadow"))return false;
                float actorRows[16];NorthlightWorldMath::replayProjection(context.inverseView,am,actorRows);
                d->SetVertexShaderConstantF(0,am,4);d->SetPixelShader(replayPS);
                NorthlightReplayPoses::Pass<BOOL> actorPoses;NorthlightReplayDrawState::Cache actorBindings(d);
                NorthlightRenderThreadProbe::Off actorSplit;unsigned drawnHere=0;
                // Only the replays the near loop above skipped (static proof already excluded there, all inside the inset): a replay
                // partly outside it, or without bounds, stays in the near map alone (it was drawn into both before, twice the cost
                // in crowds and with a low sun, whose long footprint brings many far replays into the map).
                if(nearSkip)for(const uint32_t index:actorReplays){const auto& p=replays[index];
                    if(!submitReplay(p.get(),actorBindings,actorPoses,actorRows,actorSplit,replayConstantBytes,replayConstantCalls,[&](HRESULT h,const char* s){return check(h,s);}))return false;
                    ++drawnHere;
                }
                replayPosePrepared+=actorPoses.prepared;replayPoseReused+=actorPoses.reused;
                if(diagnosticCapture){char name[80];std::snprintf(name,sizeof name,"s%d-actor",source);NorthlightWorldDiagnostics::dump(d,actorShadowSurface[source],gpuDiagnosticDirectory,name,diagnosticCapture);}
                memcpy(actorMatrices[source],am,64);actorValid[source]=true;
                if(profile)profile->mark(source==0?"SunActor":"MoonActor");bucket(NorthlightEffectsBuckets::Bucket(NorthlightEffectsBuckets::SunActor+source));
                actorNearSkipped+=nearSkipped;actorDrawn+=drawnHere;if(sideDiag)actorSide[source].endFrame();
                const DWORD actorTick=GetTickCount();
                if(NorthlightDiagnostics::enabled()&&(!actorShadowLogAt[source]||actorTick-actorShadowLogAt[source]>=1000)){actorShadowLogAt[source]=actorTick;
                    logf("WORLD actor shadow map source=%d drawn=%u nearSkipped=%u of %zu replays; totals drawn=%lu nearSkipped=%lu sideFlipsSinceLog=%u sideFlipFramesSinceLog=%u sideMatchedSinceLog=%u framesSinceLog=%u sideTracked=%u sideUnkeyed=%u invalidBoundsNear=%u unsupportedNear=%u selfSide=%s selfSideFlipsSinceLog=%u holdBand=%u holdOverflows=%lu nearListSize=%u nearWalkStops=%lu sideFlipsRootedSinceLog=%u sideFlipsUnrootedSinceLog=%u invalidBoundsNearSinceLog=%u unrootedNoBounds=%u unrootedNoBoundsSinceLog=%u selfUnrooted=%u selfNoBoundsFramesSinceLog=%u holdCarried=%u holdCarriedTotal=%lu",source,drawnHere,nearSkipped,replays.size(),actorDrawn,actorNearSkipped,
                        actorSide[source].sideFlips,actorSide[source].sideFlipFrames,actorSide[source].sideMatched,actorSide[source].frames,actorSide[source].tracked,actorSide[source].unkeyedNow,actorSide[source].invalidBoundsNear,actorSide[source].unsupportedNow,NorthlightActorSide::name(actorSide[source].selfSide),actorSide[source].selfFlips,
                        actorHold[source].bandHeld,actorHold[source].overflows,replayNearSize,replayNearStops,
                        actorSide[source].sideFlipsRooted,actorSide[source].sideFlipsUnrooted,actorSide[source].invalidBoundsNearSum,actorSide[source].unrootedNoBounds,actorSide[source].unrootedNoBoundsSum,actorSide[source].selfUnrooted,actorSide[source].selfNoBoundsFrames,actorHold[source].carried,actorHold[source].carriedTotal);actorSide[source].resetWindow();}
            }
            } /* actorShadows */
            // Union: min(dynamic scratch, cached static at its integer texel offset + exact depth delta).
            if(diagnosticCapture){
                char name[80];std::snprintf(name,sizeof name,"s%d-c%d-live",source,cascade);
                NorthlightWorldDiagnostics::dump(d,shadowScratchSurface,gpuDiagnosticDirectory,name,diagnosticCapture);
            }
            d->SetDepthStencilSurface(nullptr);if(!check(d->SetRenderTarget(0,shadowSurface[slot]),"shadow target"))return false;
            d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
            d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetStreamSourceFreq(0,1);
            d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
            d->SetTexture(0,shadowScratch);d->SetTexture(1,shadowCache[slot]);
            for(unsigned sampler=0;sampler<2;++sampler){d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(sampler,D3DSAMP_SRGBTEXTURE,FALSE);}
            float unionConstants[2][4]={{float(long(ShadowCacheMargin)+offX),float(long(ShadowCacheMargin)-offY),dz,float(ShadowCacheSize)},{1.f/1024,1.f/float(ShadowCacheSize),0,0}};
            d->SetPixelShaderConstantF(0,&unionConstants[0][0],2);d->SetPixelShader(unionPS);
            if(!check(quad(1024,1024),"shadow union"))return false;
            d->SetTexture(0,nullptr);d->SetTexture(1,nullptr);
            d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
            d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
            if(profile)profile->mark(source==0?(cascade==0?"SunNear":"SunFar"):(cascade==0?"MoonNear":"MoonFar"));
            bucket(NorthlightEffectsBuckets::Bucket(NorthlightEffectsBuckets::SunNear+source*2+cascade));
            dynamicMs=phaseNow()-dynamicStart;
            /* Only a frame that ran its capture commits: a skipped frame drew no replays, and a frame
               without any model draw (undecided) must not be reused for N frames either. */
            if(captureMode==CaptureFresh||!actorShadows)reuse.commit(interval,shadowPasses,matrices[cascade]);else captureDemand=true;
            const double phaseMs=phaseNow();const DWORD phaseTick=GetTickCount();
            if(captureSampled||(NorthlightDiagnostics::enabled()&&phaseMs>4.0&&(!shadowPhaseLogAt||phaseTick-shadowPhaseLogAt>=1000))){
                shadowPhaseLogAt=phaseTick;
                logf("WORLD shadow phase gpuFrame=%llu worldFrame=%u source=%d cascade=%d cache=%s cpuMs=%.3f selectMs=%.3f localMs=%.3f staticMs=%.3f dynamicMs=%.3f localDraws=%u staticDraws=%llu staticInstances=%llu replayDraws=%llu replayTriangles=%llu anchorRetained=%u offsetX=%ld offsetY=%ld depthDelta=%.9f selectPlanMs=%.3f planMs=%.3f",
                    (unsigned long long)(profile?profile->sampledFrame():0),frames,source,cascade,reason?reason:sliceBand?"static-slice":"reuse",phaseMs,selectMs,localMs,staticMs,dynamicMs,localDraws,
                    (unsigned long long)staticDraws,(unsigned long long)staticInstances,(unsigned long long)replayDraws,(unsigned long long)replayTriangles,unsigned(!placement.reason),offX,offY,dz,selectPlanMs,staticCasters.stats().planMs-planBefore);
            }
        }
        }
        if(NorthlightStaticPrebuild::Scheduler::Enabled&&effects.shadows)staticPrebuild.frame(cascadePivot,sourceDirections,sourceActive,!anyShadowCacheRender&&!continuousCelestialShadows&&staticScene&&staticCasters.stats().readyModels,GetTickCount(),
            [&](int,int cascade,V direction,float* m){const float radius=cascade==0?48.f:192.f;NorthlightWorldMath::shadowMatrixFrom(NorthlightWorldMath::shadowFrame(cascadePivot,direction,radius),radius*float(ShadowCacheSize)/1024.f,m);},
            [&](const float* m){return staticCasters.planReady(m);},
            [&](const float* m){try {staticCasters.signature(m);return true;}catch(...){return false;}}, /* a failed early build never resets the cache */
            [&](const float* m){staticCasters.discardPlan(m);});
        replayCullSettle();if(jobs)replayJobs_.wait(terrainDone); /* 0.3.200 (jobs): no cascade drew terrain: joined here */
        if(captureSampled){const auto& selection=terrainCandidates.stats();
            logf("WORLD terrain selection scanned=%llu candidates=%zu membershipChecks=%llu passes=%u reusedMembershipChecks=%llu fallbackPasses=%u scratchBytes=%zu prepareMs=%.3f",
                (unsigned long long)selection.scanned,terrainCandidates.candidates(),(unsigned long long)selection.membershipChecks,selection.passes,
                (unsigned long long)selection.reusedMembershipChecks,selection.fallbackPasses,terrainCandidates.bytes(),terrainPrepareMs);}
        shadowFrameReady=effects.shadows;
        // The local-lamp shadow correction subtracts an estimate of the lamp's
        // baked light where the lamp is occluded. By day that darkens sunlit
        // ground next to lamp posts (and costs ~6 ms), so it runs only while the
        // sun's weight is below one half (dusk, night, dawn).
        const unsigned pointUpdatesBefore=pointUpdates;
        if(effects.shadows&&debug==0&&sourceWeights[0]<.5f&&quality.pointShadows)renderPointShadow(replaysComplete,actorShadows);else pointReady=false;
        if(pointUpdates!=pointUpdatesBefore&&profileSampled())profilePointUsed(); /* capture waste (RenderProfile) */
        if(profile)profile->mark("PointShadow");
        bucket(NorthlightEffectsBuckets::Point);
        double lightingSetupMs[4]={},spellFitMs=0;
        int64_t lightingClock=profileSampled()&&captureFrequency.QuadPart>0?QpcClock::now():0;
        auto lightingMark=[&](unsigned part){if(!lightingClock)return;const int64_t t=QpcClock::now();lightingSetupMs[part]=1000.*double(t-lightingClock)/double(captureFrequency.QuadPart);lightingClock=t;};
        d->SetDepthStencilSurface(nullptr);d->SetTexture(0,nullptr);
        if(!foldScene&&!check(d->StretchRect(targetSurface,nullptr,colorSurface,nullptr,D3DTEXF_NONE),"world color copy"))return false;
        d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetStreamSourceFreq(0,1);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
        d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);d->SetRenderState(D3DRS_WRAP0,0);
        float c[68][4]={};c[0][0]=1.f/w;c[0][1]=1.f/h;c[0][2]=nearZ;c[0][3]=farZ;
        // 0.3.198 (rain): the frame's weather scalars (all 0 and every gain exactly 1 without weather: the bank below is then unchanged).
        // The new constants live in c59.yzw (LocalLightFog[0].yzw; only .x of c59..c66 is read, by LocalFog) and are read by WorldLighting (z)
        // and WorldFog (w); the lamp fog batch overwrites c59..c62 after all three have run, the bank is uploaded whole each frame.
        const auto wx=weatherEffects();
        c[67][0]=celestialValid?std::max(celestial.sun.angularRadius,celestial.moon.angularRadius):.03f;c[67][1]=sourceVisValid?.85f:0.f;
        // Temporal history is valid only for the same map, a continuous camera
        // and normal rendering; teleports, F10 and diagnostics restart it.
        const bool useHistory=temporalValid&&temporalMap==active->map&&!different(vec(context.camera),previousCamera,40)&&debug==0;
        memcpy(c[53],previousView,64);c[57][0]=useHistory?.75f:0.f;
        // Up to 16 authored lamps, sent through existing SM3-safe 8-light
        // direct / 4-light fog batches. Never expand the shader register bank.
        const float lampNight=NorthlightRegionalFog::nightFactor(celestialValid?celestial.dayFraction:-1.f);
        const float lampGain=.9f*NorthlightLocalLightSelection::nightGain(lampNight);
        // the first lighting pass is the sun's (with real shadows) only then: its visibility is in the
        // baseline alpha. The renderer's own orbit gives the sun weight; without it (game light only) no dimming.
        const float lampSunlitCut=NorthlightLocalLightSelection::sunlitCut(celestialValid?sourceWeights[0]:0.f,sourceActive[0]&&effects.shadows);
        // 0.3.200 (jobs): the selection, the clouds' state and frame, the lamps' fog extinction, the veil and the scissor rects come from
        // atmosphereWork(): on a job kicked after upload() (ReplayJobs=1), joined here, or inline here (ReplayJobs=0, the old place).
        lightingMark(0);
        if(jobs)replayJobs_.wait(atmosphereDone);else atmosphereWork(atmosphere,nearZ,w,h,debug);
        lightingMark(1);
        auto& localLights=atmosphere.lights;
        localDirectCount=localLights.count;localDirectNearest=localLights.nearest;
        if(equipmentLightsOn()&&captureSampled){
            const auto uploads=NorthlightParticleUploads::cache().stats();
            logf("PARTICLELIGHT probes=%u distant=%u draws=%u background=%u accepted=%u deferred=%u triangles=%u points=%u output=%u shaderMiss=%u textureMiss=%u bufferMiss=%u stateMiss=%u (blendOp=%u alphaTest=%u colorWrite=%u depth=%u sampler=%u projection=%u) geometryMiss=%u dropped=%u uploadHits=%llu uploadMisses=%llu uploadKiB=%u ttlMs=0 readback=0",particleLights.probes,particleLights.distant,particleLights.draws,particleLights.backgroundDraws,particleLights.accepted,particleLights.deferred,particleLights.triangles,particleLights.points,particleLights.immediateCount,particleLights.shaderMiss,particleLights.textureMiss,particleLights.bufferMiss,particleLights.stateMiss,particleLights.stateWhy[0],particleLights.stateWhy[1],particleLights.stateWhy[2],particleLights.stateWhy[3],particleLights.stateWhy[4],particleLights.stateWhy[5],particleLights.geometryMiss,particleLights.dropped,static_cast<unsigned long long>(uploads.hits),static_cast<unsigned long long>(uploads.misses),NorthlightParticleUploads::cache().bytes()/1024);
            for(unsigned i=0;i<particleLights.textureMissCount;++i){const auto& m=particleLights.textureMisses[i];
                static const char* const seen[]={"never","held","large","evicted","invalidated","partial","failed","unsupported","released"};
                logf("PARTICLELIGHT textureMiss key=%p seen=%s type=%u size=%ux%u levels=%u format=0x%x pool=%u usage=0x%x",m.key,m.seen<9?seen[m.seen]:"?",m.type,m.width,m.height,m.levels,m.format,m.pool,m.usage);}
            particleLights.textureMissCount=0;
            for(unsigned i=0;i<particleLights.outputCount;++i){const auto& light=particleLights.output[i];
                if(std::find(particleLights.immediate.begin(),particleLights.immediate.begin()+particleLights.immediateCount,light.sourceId)==particleLights.immediate.begin()+particleLights.immediateCount)continue;
                logf("PARTICLELIGHT source id=%llu position=(%.3f %.3f %.3f) rgb=(%.4f %.4f %.4f) range=%.2f cameraYards=%.2f",static_cast<unsigned long long>(light.sourceId),light.position[0],light.position[1],light.position[2],light.diffuse[0],light.diffuse[1],light.diffuse[2],light.attenuationEnd,std::sqrt(NorthlightEquipmentLights::distance2(light.position,context.camera)));}
            logf("EQUIPMENT lights nativePoint=1 attempts=%u banks=%u observed=%u duplicateStatic=%u dropped=%u active=%u output=%u maxLights=8 maxBanks=16 ttlMs=250 falloff=bounded-linear-approx candidates=%u actorCandidates=%u actorReads=%u backgroundReads=%u backgroundBanks=%u duplicateBanks=%u readLimited=%u bankLimited=%u domainRejected=%u decoded=%u rejected=%u",equipmentCapture.reads,equipmentLights.banks,equipmentLights.observed,equipmentLights.duplicates,equipmentLights.dropped,equipmentLights.count,equipmentLights.outputCount,equipmentCapture.candidates,equipmentCapture.actorCandidates,equipmentCapture.actorReads,equipmentCapture.backgroundReads,equipmentLights.backgroundBanks,equipmentLights.bankDuplicates,equipmentCapture.readLimited,equipmentCapture.bankLimited,equipmentCapture.domainRejected,equipmentCapture.decoded,equipmentCapture.rejected);
            for(unsigned i=0;i<equipmentLights.count;++i){const auto& entry=equipmentLights.entries[i];if(entry.gain<=.0001f)continue;
                const auto light=NorthlightEquipmentLights::light(entry.source,entry.id,entry.gain);
                const float selfDistance=actorShadowHistory.selfHold()>0?std::sqrt(NorthlightEquipmentLights::distance2(light.position,actorShadowHistory.selfAt())):-1.f;
                logf("EQUIPMENT source id=%llu position=(%.3f %.3f %.3f) nativeRGB=(%.4f %.4f %.4f) denominator=(%.4f %.4f %.4f) rgb=(%.4f %.4f %.4f) range=%.2f gain=%.3f cameraYards=%.2f selfYards=%.2f",static_cast<unsigned long long>(light.sourceId),light.position[0],light.position[1],light.position[2],entry.source.color[0],entry.source.color[1],entry.source.color[2],entry.source.attenuation[0],entry.source.attenuation[1],entry.source.attenuation[2],light.diffuse[0],light.diffuse[1],light.diffuse[2],light.attenuationEnd,entry.gain,std::sqrt(NorthlightEquipmentLights::distance2(light.position,context.camera)),selfDistance);}
        }
        c[52][0]=float(std::min(localDirectCount,NorthlightLocalLightSelection::DirectBatchSize));c[52][1]=.9f*lampGain;
        // Near fade: no added fog within 3.5 units of the viewer, full at 15.5.
        // Same soft ramp, shifted 0.5 world units closer.
        c[58][0]=3.5f;c[58][1]=1.f/12;c[58][2]=10.f*lampGain*wx.lampFogGain();c[58][3]=.12f*lampGain*wx.lampFogGain(); /* 0.3.198 (rain): lamp fog gain (x1 since 0.3.199); 0.3.199 (fog clouds): 10 / .12 (were 13 / .156): the glow read too strong in the game test */
        const float airFloor=atmosphere.airFloor; /* atmosphereWork: .0017f plus the rain's extra, thinned in dense zones */
        c[59][2]=std::max(wx.shadowSoften(),1.f-underwaterFade.scale()); /* underwater: the sun correction goes to 0 and the shadow softens fully (the rain path), alpha and baseline keep the true visibility */ c[59][3]=airFloor; /* 0.3.198 (rain): direct shadow softening (0 when dry), air extinction floor (WorldFog) */
        if(uploadedFogField){c[31][0]=uploadedFogField->originX;c[31][1]=uploadedFogField->originY;}
        c[31][2]=1.f/(NorthlightRegionalFog::N*NorthlightRegionalFog::Spacing);
        c[31][3]=atmosphere.c[31][3]; /* nightFactor, atmosphereWork */
        // Neutral scattering albedo preserves the zone/source palette (c22.w and c32: atmosphereWork, the same expressions).
        c[22][0]=c[22][1]=c[22][2]=1;c[22][3]=atmosphere.c[22][3];memcpy(c[32],atmosphere.c[32],16);
        const auto& volumePalette=paletteFrameValid?framePalette:celestialProfiles.fallback;
        // 0.3.199 (fog clouds): the frame derived in atmosphereWork; the device part of its activity (the noise volume) is decided here.
        auto& cf=atmosphere.cf;
        cf.active=cf.active&&effects.fog&&debug==0&&fogCloudsPS&&ensureCloudNoise();
        if(cf.active){
            NorthlightFogClouds::shaderConstants(cf,&c[59]); /* c59.y, c60..c63 (host-folded for the shader's early outs) */
        }
        if(profileSampled())logf("WORLD fog clouds active=%d coverage=%.3f threshold=%.3f height=%.1f speed=%.2f dir=(%.2f %.2f) sigmaMax=%.4f lush=%.2f noiseReady=%d",cf.active?1:0,cf.coverage,cf.threshold,cf.height,cloudWind.speed,cloudWind.dir[0],cloudWind.dir[1],cf.sigmaMax,cloudLush,fogCloudNoise().ready.load(std::memory_order_acquire)?1:0);
        {   // SpellFog: the smoothed estimate for the next frame's records (identity when SpellFog is off, the fog effect is off or has no field, or a debug view is on)
            NorthlightSpellFog::Frame raw;const bool wanted=quality.spellFog&&effects.fog&&active&&uploadedFogField&&debug==0&&atmosphere.spellValid;
            if(wanted){raw.active=true;for(unsigned i=0;i<NorthlightSpellFog::kSamples;++i)raw.tau[i]=atmosphere.spellTau[cf.active?1:0][i];for(unsigned k=0;k<3;++k)raw.scatter[k]=std::max(context.ambient[k],0.f)*.35f;}
            const DWORD tick=GetTickCount();const float dt=spellFogTick?float(DWORD(tick-spellFogTick))*.001f:0.f;spellFogTick=tick;
            const int64_t fitStart=lightingClock?QpcClock::now():0;
            spellFogHold.update(raw,wanted,dt);
            if(fitStart)spellFitMs=1000.*double(QpcClock::now()-fitStart)/double(captureFrequency.QuadPart);
            if(spellFogHold.resets&&!spellFogResetLogged){spellFogResetLogged=true;logf("SPELLFOG estimate was not finite: the hold was reset and SpellFog goes on (resets=%u)",spellFogHold.resets);}
        }
        skyTransmittanceFrame=cf.active?atmosphere.veil[1]:atmosphere.veil[0]; /* the clouds' term only when the pass is really active (1: no fog field or no fog) */
        c[33][0]=float(w);c[33][1]=float(h);c[33][2]=float(w/2);c[33][3]=float(h/2);
        c[1][0]=projection[0];c[1][1]=projection[1];c[1][2]=projection[2];c[1][3]=minZ;
        c[2][0]=1.f/(maxZ-minZ);c[2][1]=48;c[2][2]=192;c[2][3]=1.f/1024;
        memcpy(c[3],context.inverseView,64);memcpy(c[7],sourceMatrices[firstSource][0],64);memcpy(c[11],sourceMatrices[firstSource][1],64);
        memcpy(c[15],context.camera,12);memcpy(c[16],context.lightDirection,12);memcpy(c[17],context.direct,12);memcpy(c[18],context.ambient,12);
        /* The ambient policies follow the native orbit, the sun and moon shown and lighting the world (celestial_time_warp.h). */
        const auto naturalSky=NorthlightCelestialOrbit::evaluateNative(celestialValid?celestial.dayFraction:-1.);
        c[18][3]=NorthlightTwilightFill::gain(naturalSky.valid,naturalSky.valid?celestial.dayFraction:-1.,
            naturalSky.valid?naturalSky.sun.direction[2]:0.f,naturalSky.valid?naturalSky.moon.direction[2]:0.f);
        c[18][3]+=wx.ambientLift(); /* 0.3.198 (rain): a little more sky ambient from the GI pass in rain (+0 when dry; the probes themselves are untouched) */
        NorthlightNightFloor::nightAmbient(quality.nightBrightness,NorthlightNightFloor::weight(naturalSky.valid,naturalSky.valid?naturalSky.sun.direction[2]:0.f),c[18],c[19]);
        c[19][3]=8; /* c19.rgb: NightBrightness light per unit of albedo, from c18's ambient (WorldComposite only; 0 = off); c19.w: probe spacing */
        c[20][0]=float(NorthlightGI::probeLayout().atlas);c[20][1]=NorthlightQuality::giIntensity(quality);c[20][2]=.85f;c[20][3]=active->serial?1.f:0.f;
        DWORD now=GetTickCount();
        c[21][0]=uploadedFogField&&(uploadedFogField->fogCells||uploadedFogField->airCells)?1.f:0.f;c[21][1]=1.2f*wx.shaftGain();c[21][2]=.38f;c[21][3]=128; /* 0.3.198 (rain): shafts fade in rain, x1 when dry */
        // 0.3.200 (gpu budget): fewer march intervals at reduced levels (c60.x clouds, c64.z WorldFog; both stay 0, the bank unchanged, at level 0).
        if(gpuBudgetLevel){if(cf.active)c[60][0]=NorthlightGpuBudget::spacingDelta(c[21][3],NorthlightGpuBudget::cloudSteps(gpuBudgetLevel),40);c[64][2]=NorthlightGpuBudget::spacingDelta(c[21][3],NorthlightGpuBudget::fogSteps(gpuBudgetLevel),48);}
        memcpy(c[23],context.camera,12);c[24][0]=NorthlightWorldMath::ShadowBiasWorld*NorthlightWorldMath::InverseShadowDepth;c[24][1]=2;c[24][2]=float(debug);c[24][3]=float(DWORD(now-animationEpoch))*.001f;
        // Readable fog as measured, else the held value, for every c25/c26 reader (WorldComposite, smoothRemoval, FogClouds); render() runs once per frame, so dt is one frame.
        const auto uploadedFog=legacyFogHold.update(active->map,legacyFog,legacyFogKnown,now);if(traceFog==0&&!legacyFogKnown&&legacyFogHold.has)traceFog=2;
        memcpy(c[25],uploadedFog.parameters,16);memcpy(c[26],uploadedFog.color,16);
        memcpy(c[28],context.lightDirection,12);memcpy(c[29],context.direct,12);
        c[27][2]=float(DWORD(now-animationEpoch))*.001f;
        c[27][3]=c[21][0];
        c[35][0]=NorthlightWorldMath::InverseShadowDepth;
        // Horizon haze: the game's own fog end, smoothed per map and held while
        // unknown. Extras in free components c57.yzw / c67.zw; c34 is set for the
        // final pass only. Fog off (Ctrl+Shift+F7) or HorizonHaze=0: optical depth 0.
        horizonHazeState.update(active->map,legacyFog.parameters,legacyFog.color,projection[2],double(now)*.001);
        const float hazeSun[3]={sourceDirections[0].x,sourceDirections[0].y,sourceDirections[0].z};
        const float hazeZone=NorthlightCelestialProfiles::horizonHaze(volumePalette,c[31][3]);
        float hazeLift[3];NorthlightSunHue::horizonLift(glowHueFrame,hazeLift);
        // The warm lobe toward the sun goes with the sun's light, which ends with its disc (discVisibility).
        const float hazeSunWeight=underwaterFade.scale()*sourceWeights[0];
        const auto haze=NorthlightHorizonHaze::constants(horizonHazeState,{quality.horizonHaze,quality.horizonHazeStart,quality.horizonHazeBand,quality.horizonHazeTerrain},
            farZ,hazeZone,effects.fog,celestialValid,hazeSun,hazeSunWeight,hazeLift,wx.hazeTauScale());
        c[57][1]=haze.shape[0];c[57][2]=haze.shape[1];c[57][3]=haze.shape[2];c[67][2]=haze.sun[0];c[67][3]=haze.sun[1];
        c[35][1]=haze.lift[0];c[35][2]=haze.lift[1];c[35][3]=haze.lift[2]; /* lift colour = the glow hue (WorldComposite reads c35.yzw) */
        hazeFrame=haze;
        {   // The far edge toward the sun for the discs and the glow ring, from the regional field's surface (water over ground).
            float target=0;
            if(celestialValid&&uploadedFogField){const auto& field=*uploadedFogField;
                target=NorthlightApparentHorizon::sine(context.camera,celestial.sun.direction,farZ,[&](float x,float y,float& height){
                    const auto g=NorthlightSpellFog::fieldAt(field,x,y);height=g.ground;return g.coverage>.5f;});}
            c[64][3]=apparentHorizon.update(target,now); /* HorizonEdge.w: the composite's haze band starts at the far edge */
        }
        // 0.3.159 shadow removal smoothing (TemporalLight smoothRemoval, c30.yzw), always on. The drawn
        // lighting passes are the sources with a positive weight (plus firstSource at weight 0), so
        // their summed weight is the lighting-buffer alpha of a fully lit pixel. Without a drawn
        // weight nothing painted is removed and y stays 0. w: the 0.75-unit disc radius
        // (1.5 across) in half-res pixels at view distance 1.
        const float drawnWeight=std::max(sourceWeights[0],0.f)+std::max(sourceWeights[1],0.f);
        if(drawnWeight>.001f){c[30][1]=1;c[30][2]=1/drawnWeight;c[30][3]=.75f*std::fabs(projection[0])*float(w/2)*.5f;}
        // 0.3.170 TemporalLight (c15.w, TemporalReach): the largest direct-light channel per unit of
        // normalised visibility over the drawn, active sources. It bounds how far a flickering shadow
        // may carry the rgb history outside its neighbourhood (0: exactly the tight clamp).
        for(int source=0;source<2;++source)if(sourceWeights[source]>0&&sourceActive[source])
            c[15][3]=std::max(c[15][3],std::max({sourceColors[source].x,sourceColors[source].y,sourceColors[source].z})*drawnWeight/sourceWeights[source]);
        lightingMark(2);
        c[30][0]=waterMask?1.f:0.f;d->SetPixelShaderConstantF(0,&c[0][0],68);
        IDirect3DTexture9* textures[]={foldScene?foldScene:color,depth,shadow[0],shadow[1],probe[0],probe[1],probe[2],probe[3],nullptr,nullptr,probe[4],waterMask,nullptr,regionalFogTexture};
        for(int i=0;i<14;++i){d->SetTexture(i,textures[i]);d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_MINFILTER,(i==0||i==9)?D3DTEXF_LINEAR:D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MAGFILTER,(i==0||i==9)?D3DTEXF_LINEAR:D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE);}
        auto setSource=[&](int source,bool first,bool volume=false){
            d->SetTexture(2,shadow[source*2]);d->SetTexture(3,shadow[source*2+1]);
            // Collapse only shadow queries to the centre at depth .5. With a
            // cleared depth of 1, even slope-corrected PCF is exactly unoccluded.
            const float neutralMatrix[16]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,.5f,1};
            d->SetPixelShaderConstantF(7,effects.shadows?sourceMatrices[source][0]:neutralMatrix,4);
            d->SetPixelShaderConstantF(11,effects.shadows?sourceMatrices[source][1]:neutralMatrix,4);
            float dir[4]={sourceDirections[source].x,sourceDirections[source].y,sourceDirections[source].z,source==1?1.f:0.f}; // w selects moon surface relighting
            // Match the shadow-render activity threshold exactly. The mandatory
            // ambient-only volume pass must never read an unrendered map for
            // a sub-threshold celestial source.
            const V& shown=volume?sourceGlowColors[source]:sourceColors[source]; // the fog volume's glow has its own strength and hue; surface passes keep the light colour
            float rgb[4]={sourceActive[source]?shown.x:0.f,sourceActive[source]?shown.y:0.f,sourceActive[source]?shown.z:0.f,0};
            if(volume){const float keep=underwaterFade.scale();rgb[0]*=keep;rgb[1]*=keep;rgb[2]*=keep;} /* underwater: the fog/volume pass (shafts) fades with the sun; the lighting and actor passes keep the real colour (their shadow test and alpha read it) */
            // fog passes only: the sun's direct scatter takes the glow hue; the forward soft cap
            // rises so the aureole brightens toward the sun. c17/c18 are restored after the fog loop.
            if(volume&&source==0)NorthlightSunHue::fogDirect(rgb,glowHueFrame,rgb);
            d->SetPixelShaderConstantF(16,dir,1);d->SetPixelShaderConstantF(17,rgb,1);
            if(volume){c[21][1]=1.2f*volumePalette.fogGain[source]*wx.shaftGain();c[21][2]=source==0?NorthlightSunHue::SunForwardCap:NorthlightSunHue::MoonForwardCap;d->SetPixelShaderConstantF(21,c[21],1);d->SetTexture(15,sourceVis[source][1-sourceVisIndex]);}
            c[27][0]=first?(volume?1.f:1.f-authoredFill):0.f;c[27][1]=sourceWeights[source];d->SetPixelShaderConstantF(27,c[27],1);
        };
        lightingMark(3);
        bucket(NorthlightEffectsBuckets::LightingSetup);
        d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
        // Smoothed normals for the lighting, GI and local light passes (s14).
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        d->SetTexture(14,nullptr);d->SetTexture(15,nullptr);
        d->SetRenderState(D3DRS_COLORWRITEENABLE1,15);
        if(!check(d->SetRenderTarget(0,normalSurface),"world normals target")||
           !check(d->SetRenderTarget(1,c[30][1]>0?temporalDepthSurface[temporalIndex]:nullptr),"current distance target"))return false;
        d->SetPixelShader(normalsPS);if(!check(quad(w/2,h/2),"world normals pass"))return false;
        if(!check(d->SetRenderTarget(1,nullptr),"current distance unbind"))return false;
        d->SetTexture(14,normalBuffer);d->SetSamplerState(14,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(14,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(14,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(14,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(14,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(14,D3DSAMP_SRGBTEXTURE,FALSE);
        // 0.3.209 (task 29): actor visibility (half res, r sun, g moon). Per drawn source with an actor map, first its 64x64 tile minimum (ActorTiles in
        // two 4x4 steps, 1024 -> 256 (shared target) -> 64 (own target per source); one 256-tap step was latency bound, ~.15 ms), then the visibility target is cleared white and gets one ActorShadow pass per source masked to its channel (s12 the actor
        // map, s10 its tiles; s10 is rebound to the probe metadata after). All tile passes come first, so the visibility target is cleared and drawn in one
        // run without switching away from it (a switch made a tiled GPU store and reload the half-res target, ~.07 ms). A frame without any pass
        // (ActorShadowDetail=0, shadows off, no valid map) skips the target and its clear: WorldLighting reads neutralWhite on s12 instead.
        // At dusk both sources draw a pass, each refetching depth and normals: the tile early out keeps the lit majority at four taps in both.
        if(profile)profile->mark("WorldNormals");bucket(NorthlightEffectsBuckets::Lighting); /* 0.3.209 (task 29): the normals pass, so ActorShadow below is billed alone */
        bool actorPass[2]={},anyActorPass=false;
        for(int source=0;source<2;++source){
            actorPass[source]=!((sourceWeights[source]<=0&&source!=firstSource)||!effects.shadows||!sourceActive[source]||!actorValid[source]||!actorShadow[source]||!actorShadowPS||!actorTilesPS||!actorTiles[source]||!actorTilesMid||!actorVisibility);
            anyActorPass=anyActorPass||actorPass[source];
        }
        const float actorInfo[4]={1.f,1.f/float(NorthlightActorShadowMap::Size),NorthlightActorShadowMap::Radius,0.f};
        if(anyActorPass){
            d->SetPixelShader(actorTilesPS);
            const UINT mid=NorthlightActorShadowMap::Size/NorthlightActorShadowMap::TileStep,tiles=NorthlightActorShadowMap::Size/NorthlightActorShadowMap::TileTexels;
            const float midInfo[4]={1.f,1.f/float(mid),NorthlightActorShadowMap::Radius,0.f};
            for(int source=0;source<2;++source){
                if(!actorPass[source])continue;
                if(!check(d->SetRenderTarget(0,actorTilesMidSurface),"actor tile target"))return false;
                d->SetTexture(12,actorShadow[source]);d->SetPixelShaderConstantF(72,actorInfo,1);
                if(!check(quad(mid,mid),"actor shadow tile pass"))return false;
                if(!check(d->SetRenderTarget(0,actorTilesSurface[source]),"actor tile target"))return false;
                d->SetTexture(12,actorTilesMid);d->SetPixelShaderConstantF(72,midInfo,1);
                if(!check(quad(tiles,tiles),"actor shadow tile pass"))return false;
            }
            if(profile)profile->mark("ActorTiles");
            if(!check(d->SetRenderTarget(0,actorVisibilitySurface),"actor visibility target")||!check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0),"clear actor visibility"))return false;
        }
        for(int source=0;source<2;++source){
            if(!actorPass[source])continue;
            setSource(source,false); /* c16 direction, c17 colour, c7/c11 maps: the bank ActorShadow reads (the lighting loop sets them again) */
            d->SetPixelShaderConstantF(68,actorMatrices[source],4);d->SetPixelShaderConstantF(72,actorInfo,1);
            d->SetTexture(12,actorShadow[source]);d->SetTexture(10,actorTiles[source]);
            d->SetRenderState(D3DRS_COLORWRITEENABLE,source?D3DCOLORWRITEENABLE_GREEN:D3DCOLORWRITEENABLE_RED);
            d->SetPixelShader(actorShadowPS);
            if(!check(quad(w/2,h/2),"actor shadow visibility pass"))return false;
        }
        d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        if(anyActorPass)d->SetTexture(10,textures[10]); /* the frame-start binding (probe metadata) again */
        if(profile)profile->mark("ActorShadow");bucket(NorthlightEffectsBuckets::ActorShadow);
        d->SetTexture(12,anyActorPass?actorVisibility:neutralWhite);
        d->SetRenderTarget(0,lightSurface);d->SetPixelShader(lightingPS);
        bool first=true;
        // A strength-zero source still subtracts its authored surface share.
        for(int source=0;source<2;++source){if(sourceWeights[source]<=0&&source!=firstSource)continue;
            setSource(source,first);d->SetRenderState(D3DRS_ALPHABLENDENABLE,!first);
            if(!check(d->SetRenderTarget(1,first?baselineSurface:nullptr),"baseline MRT"))return false;
            d->SetRenderState(D3DRS_COLORWRITEENABLE1,15);
            if(!check(quad(w/2,h/2),"celestial shadows pass"))return false;first=false;
        }
        d->SetRenderTarget(1,nullptr);
        d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        d->SetTexture(12,textures[12]); /* 0.3.209 (task 29): the frame-start binding (null) again; the visibility was WorldLighting's only */
        if(profile)profile->mark("WorldLighting");
        bucket(NorthlightEffectsBuckets::Lighting);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_COLORWRITEENABLE,7);d->SetPixelShader(giPS);
        if(effects.gi)d->SetTexture(8,probePrev); /* 0.3.197: WorldGI reads the previous SH on s8 (LightingBuffer is not read here); null without the texture, moment.w is then None */
        if(effects.gi&&!check(quad(w/2,h/2),"world GI pass"))return false;
        d->SetTexture(8,textures[8]); /* 0.3.197: restore the frame-start binding */
        if(profile)profile->mark("GI"); /* task 43: the GI pass alone; the lamp batches are LocalLights */
        if(localDirectCount&&debug==0){
            profileSplit(profile,lightSurface); /* sample frames: GI's timestamp ends here, LocalLights' below */
            d->SetPixelShader(localDirectPS);d->SetRenderState(D3DRS_SCISSORTESTENABLE,NorthlightLocalLightScissor::Enabled);
            d->SetTexture(12,baselineLight); /* sun visibility (baseline alpha); the temporal pass rebinds it anyway */
            // Two neighbouring 8-lamp batches whose joint rect costs less than both (local_light_batches.h wide()) draw as one LocalDirect16
            // pass (its lamps in c68..c99; c52 as for LocalDirect). Without a proven view every rect is the whole target, so pairs merge.
            constexpr unsigned N=NorthlightLocalLightSelection::DirectBatchSize;IDirect3DPixelShader9* bound=localDirectPS;
            for(unsigned firstLight=0;firstLight<localDirectCount;){
                const unsigned index=firstLight/N;
                const bool pair=localDirect16PS&&firstLight+N<localDirectCount;
                const NorthlightLocalLightScissor::Rect whole{0,0,long(w/2),long(h/2)};
                const auto& a=localScissorView.valid?atmosphere.directRects[index]:whole;const auto& b=localScissorView.valid?atmosphere.directRects[index+1]:whole;
                if(pair&&NorthlightLocalLightBatches::wide(a,b)){
                    const auto batch=localLights.batch<2*N>(firstLight);const auto joint=NorthlightLocalLightBatches::unite(a,b);
                    const float info[4]={float(batch.count),c[52][1],lampSunlitCut,0};
                    if(bound!=localDirect16PS){d->SetPixelShader(localDirect16PS);bound=localDirect16PS;}
                    d->SetPixelShaderConstantF(68,batch.position[0].data(),2*N);d->SetPixelShaderConstantF(84,batch.color[0].data(),2*N);
                    d->SetPixelShaderConstantF(52,info,1);++localWidePasses;
                    if(localScissor(batch.position,batch.count,&joint)&&!check(quad(w/2,h/2),"local direct 16-light batch")){d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);return false;}
                    firstLight+=2*N;continue;
                }
                const auto batch=localLights.batch<N>(firstLight);
                const float info[4]={float(batch.count),c[52][1],lampSunlitCut,0};
                if(bound!=localDirectPS){d->SetPixelShader(localDirectPS);bound=localDirectPS;}
                d->SetPixelShaderConstantF(36,batch.position[0].data(),N);
                d->SetPixelShaderConstantF(44,batch.color[0].data(),N);
                d->SetPixelShaderConstantF(52,info,1);
                if(localScissor(batch.position,batch.count,&atmosphere.directRects[index])&&!check(quad(w/2,h/2),"local direct light batch")){d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);return false;}
                firstLight+=N;
            }
            d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
            profileSplit(profile,lightSurface);
        }
        if(profile)profile->mark("LocalLights");bucket(NorthlightEffectsBuckets::GI);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        if(debug==0&&pointReady){
            renderPointLighting(depth,waterMask,w,h,nearZ,farZ,minZ,maxZ);
            // Optional pass temporarily owns PS c0..9 and s0..2. Restore the
            // entire world bank and texture/filter contract before fog/final.
            d->SetPixelShaderConstantF(0,&c[0][0],68);
            for(unsigned sampler=0;sampler<3;++sampler){
                d->SetTexture(sampler,textures[sampler]);
                d->SetSamplerState(sampler,D3DSAMP_MINFILTER,sampler==0?D3DTEXF_LINEAR:D3DTEXF_POINT);
                d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,sampler==0?D3DTEXF_LINEAR:D3DTEXF_POINT);
            }
            d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        }
        // Task 43: the old PointLighting interval in three marks: PointLight (the shadowed point lamp), RemovalSmooth, TemporalLight.
        if(profile)profile->mark("PointLight");
        {   // Temporal stabilization: light + history -> temporalLight[cur] (MRT with view distance).
            const unsigned cur=temporalIndex,prev=1-temporalIndex;
            d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
            // 0.3.174: s10 switches from ProbeMetadata (WorldGI, done) to the AO/bloom target for the
            // removal smoothing and WorldComposite; the frame-start loop rebinds probe[4] POINT.
            d->SetTexture(10,foldAO?foldAO:neutralAO);d->SetSamplerState(10,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(10,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
            // 0.3.185: the removal smoothing is its own half-res pass, light -> smoothLight (a copy where nothing is
            // removed; skipped without a drawn source, c30.y=0, where s9 is the raw light). It reads s0 Scene, s1 Depth, s8 light,
            // s10 AO, s12 baseline, s14 normals and s15 current distance (POINT; histories are read later).
            d->SetTexture(8,light);d->SetTexture(12,baselineLight);d->SetTexture(14,normalBuffer);d->SetTexture(15,c[30][1]>0?temporalDepth[temporalIndex]:nullptr); /* s12 stays bound for the temporal pass and the composite */
            d->SetSamplerState(14,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(14,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
            d->SetSamplerState(15,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(15,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
            d->SetSamplerState(15,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(15,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(15,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(15,D3DSAMP_SRGBTEXTURE,FALSE);
            if(!check(d->SetRenderTarget(0,smoothSurface),"removal smoothing target"))return false;
            const bool removalDrawn=c[30][1]>0; // no drawn source: smoothRemoval would be a pure copy, TemporalLight reads the raw light
            if(removalDrawn){d->SetPixelShader(removalPS);if(!check(quad(w/2,h/2),"removal smoothing pass"))return false;}
            if(profile)profile->mark("RemovalSmooth");
            d->SetTexture(15,nullptr); // Current distance becomes TemporalLight's MRT again; history reads the other index.
            if(!check(d->SetRenderTarget(0,temporalLightSurface[cur]),"temporal light target")||!check(d->SetRenderTarget(1,temporalDepthSurface[cur]),"temporal depth target"))return false;
            d->SetRenderState(D3DRS_COLORWRITEENABLE1,15);
            // 0.3.171: LightHistory (s14) is read bilinearly at the unrounded reprojection; the depth history
            // (s15, R32F) stays POINT. The normals setup rebinds s14 POINT every frame.
            // 0.3.185: SmoothedLighting is s9 (FogBuffer's slot: idle until the blur, which binds it, and the composite
            // rebinds it; s9 is POINT here, LINEAR again afterwards), s8 stays the raw buffer for the neighbourhood clamp.
            d->SetTexture(9,removalDrawn?smoothLight:light);d->SetSamplerState(9,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(9,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
            d->SetTexture(14,temporalLight[prev]);d->SetTexture(15,temporalDepth[prev]);
            for(unsigned sampler=14;sampler<16;++sampler){d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_MINFILTER,sampler==14?D3DTEXF_LINEAR:D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,sampler==14?D3DTEXF_LINEAR:D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(sampler,D3DSAMP_SRGBTEXTURE,FALSE);}
            d->SetPixelShader(temporalPS);if(!check(quad(w/2,h/2),"temporal light pass"))return false;
            d->SetRenderTarget(1,nullptr);d->SetTexture(14,nullptr);d->SetTexture(15,nullptr);d->SetTexture(9,nullptr);d->SetSamplerState(9,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(9,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
            temporalIndex=prev;temporalValid=true;memcpy(previousView,context.view,64);previousCamera=vec(context.camera);temporalMap=active->map;
        }
        if(profile)profile->mark("TemporalLight");
        bucket(NorthlightEffectsBuckets::PointLighting);
        // Source visibility (1x1 per source, temporally smoothed): drives the
        // fog aureole when the body is behind geometry; broad scattering uses
        // world shadow visibility independently of the body's screen position.
        bool fogResolved=false; // 0.3.199 (fog temporal): the final fog is fogBlurred (the pass ran and the pointers rotated), else fog
        if(effects.fog){
        {const unsigned cur=sourceVisIndex,prev=1-sourceVisIndex;
         d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetPixelShader(sourceVisPS);
         for(int source=0;source<2;++source){if(!sourceActive[source]&&source!=firstSource)continue;
            float dir[4]={sourceDirections[source].x,sourceDirections[source].y,sourceDirections[source].z,0};d->SetPixelShaderConstantF(16,dir,1);
            d->SetRenderTarget(0,sourceVisSurface[source][cur]);d->SetTexture(15,sourceVis[source][prev]);
            d->SetSamplerState(15,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(15,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(15,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(15,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(15,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
            if(!check(quad(1,1),"source visibility pass"))return false;}
         sourceVisIndex=prev;sourceVisValid=true;}
        d->SetRenderTarget(0,fogSurface);d->SetPixelShader(fogPS);first=true;
        float fogAmbient[4]={0,0,0,c[18][3]};NorthlightSunHue::coolAmbient(c[18],glowHueFrame,sourceWeights[0],fogAmbient);
        d->SetPixelShaderConstantF(18,fogAmbient,1); /* cooler environment scatter while the sun is up (w kept) */
        int lastFogSource=firstSource;bool lastFogFirst=true;
        for(int source=0;source<2;++source){if(!sourceActive[source]&&source!=firstSource)continue;
            setSource(source,first,true);d->SetRenderState(D3DRS_ALPHABLENDENABLE,!first);d->SetRenderState(D3DRS_COLORWRITEENABLE,first?15:7);
            if(!check(quad(w/2,h/2),"celestial volumetric raymarch"))return false;lastFogSource=source;lastFogFirst=first;first=false;
        }
        // 0.3.199 (fog clouds): the moving fog banks, a second half-resolution raymarch into the same fog buffer: rgb added (ONE), the fog already
        // there attenuated by the cloud transmittance (alpha, SRCALPHA); alpha out is the old alpha times it. The first source lights it, ambient once
        // (c27.x=1 in setSource). Afterwards every state the later passes read is put back exactly as the loop above left it (the last source's
        // setSource again, blend, write mask, s14 and the alpha-blend operands); inactive frames skip all of this.
        if(cf.active&&c[21][0]>=.5f){
            if(profile)profile->mark("FogMarch");
            /* 0.3.199 (optimisation): the separate-alpha operands are not saved: the effects run inside the game-state save (SavedState) and with
               SEPARATEALPHABLENDENABLE back at FALSE below they are ignored. The common sun-only frame drew the source loop once with firstSource
               and first=true: setSource then already holds this pass's state, so it is neither set here nor put back after. */
            const bool sameSource=lastFogSource==firstSource&&lastFogFirst;
            if(!sameSource)setSource(firstSource,true,true);
            { /* 0.3.199 (fog clouds): the banks' colour (the game fog colour raised to a moonlit grey at night) in c25.w/c26 for this pass only; restored below */
                float cloudColour[4],cloudFog[4]={c[25][0],c[25][1],c[25][2],1};
                { /* continuous in the Hold's post-hold w fade: blend the game-fog and the no-fog colour by w (continuous in w; equal to the old result when w is 0 or 1) */
                  const float fw=c[25][3];NorthlightFogClouds::colour(c[26],fw>0,c[31][3],cloudColour);
                  if(fw>0&&fw<1){float off[4];NorthlightFogClouds::colour(c[26],false,c[31][3],off);for(int i=0;i<4;++i)cloudColour[i]=off[i]+(cloudColour[i]-off[i])*fw;}}
                d->SetPixelShaderConstantF(25,cloudFog,1);d->SetPixelShaderConstantF(26,cloudColour,1);}
            d->SetTexture(14,cloudNoise);d->SetSamplerState(14,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(14,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);d->SetSamplerState(14,D3DSAMP_ADDRESSW,D3DTADDRESS_WRAP);
            const DWORD cloudFilter=D3DTEXF_LINEAR;d->SetSamplerState(14,D3DSAMP_MINFILTER,cloudFilter);d->SetSamplerState(14,D3DSAMP_MAGFILTER,cloudFilter);d->SetSamplerState(14,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(14,D3DSAMP_SRGBTEXTURE,FALSE);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
            d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLENDALPHA,D3DBLEND_ZERO);d->SetRenderState(D3DRS_DESTBLENDALPHA,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_BLENDOPALPHA,D3DBLENDOP_ADD);
            d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetPixelShader(fogCloudsPS);
            const bool cloudsDrawn=check(quad(w/2,h/2),"fog clouds raymarch");
            if(!sameSource)setSource(lastFogSource,lastFogFirst,true);
            d->SetPixelShaderConstantF(25,c[25],2); /* 0.3.199 (fog clouds): the bank's game fog parameters and colour back for the lamp fog, blur and composite */
            d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
            d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE,!lastFogFirst);d->SetRenderState(D3DRS_COLORWRITEENABLE,lastFogFirst?15:7);d->SetPixelShader(fogPS);
            d->SetTexture(14,nullptr);d->SetSamplerState(14,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(14,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(14,D3DSAMP_ADDRESSW,D3DTADDRESS_CLAMP);
            d->SetSamplerState(14,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(14,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
            if(!cloudsDrawn)return false;
            if(profile)profile->mark("FogClouds");
        }
        d->SetPixelShaderConstantF(17,c[17],2); /* restore the bank's direct/ambient before lamp fog, blur and composite */
        if(localDirectCount&&debug==0){
            // 0.3.205 (gh#20): every batch adds its raw glow into localFogAccum and LocalFogCombine caps the total once into the fog buffer (a cap per batch of four
            // made the glow depend on how the closest-first lights group). A batch adds exactly +0 outside its rect, so only the union of the drawn batches' rects is
            // cleared and combined, and with no batch on screen neither runs. localScissor fails open (no proven view): then every batch and the combine are unclipped.
            const unsigned fogBatches=(localDirectCount+NorthlightLocalLightSelection::FogBatchSize-1)/NorthlightLocalLightSelection::FogBatchSize;
            const bool whole=!NorthlightLocalLightScissor::Enabled||!localScissorView.valid;
            NorthlightLocalLightScissor::Rect area{};bool any=whole;
            if(!whole)for(unsigned b=0;b<fogBatches;++b){const auto& r=atmosphere.fogRects[b];if(!r.area())continue;
                if(any){area.left=std::min(area.left,r.left);area.top=std::min(area.top,r.top);area.right=std::max(area.right,r.right);area.bottom=std::max(area.bottom,r.bottom);}else area=r;
                any=true;}
            if(!any){localScissorBatches+=fogBatches;localScissorSkipped+=fogBatches;} // what localScissor counts for the batches it skips
            else{
            const RECT clip={area.left,area.top,area.right,area.bottom};const D3DRECT cleared={area.left,area.top,area.right,area.bottom};
            d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE); // alpha adds like rgb (ONE,ONE): the sum of the lamps' capped peaks
            if(!check(d->SetRenderTarget(0,localFogAccumSurface),"lamp fog accumulator target")||!check(d->Clear(whole?0:1,whole?nullptr:&cleared,D3DCLEAR_TARGET,0,1,0),"lamp fog accumulator clear"))return false; // scissor is off here
            d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetPixelShader(localFogPS);
            // Analytic lamp glow can reach farther without extending the
            // celestial raymarch or adding samples to it. Restore after use.
            const float lampFogInfo[4]={c[21][0],c[21][1],c[21][2],localLights.fogDistance};
            d->SetPixelShaderConstantF(21,lampFogInfo,1);d->SetRenderState(D3DRS_SCISSORTESTENABLE,NorthlightLocalLightScissor::Enabled);
            for(unsigned firstLight=0;firstLight<localDirectCount;firstLight+=NorthlightLocalLightSelection::FogBatchSize){
                const auto batch=localLights.batch<NorthlightLocalLightSelection::FogBatchSize>(firstLight);
                d->SetPixelShaderConstantF(36,batch.position[0].data(),NorthlightLocalLightSelection::FogBatchSize);
                d->SetPixelShaderConstantF(44,batch.color[0].data(),NorthlightLocalLightSelection::FogBatchSize);
                d->SetPixelShaderConstantF(59,batch.fog[0].data(),NorthlightLocalLightSelection::FogBatchSize);
                if(localScissor(batch.position,batch.count,&atmosphere.fogRects[firstLight/NorthlightLocalLightSelection::FogBatchSize])&&!check(quad(w/2,h/2),"local fog glow batch")){d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);return false;}
            }
            // The fog buffer is the target again (SetRenderTarget resets the scissor rect, so the union is set after it); s9 (idle here, rebound before every
            // reader) holds the accumulator for the capping quad.
            d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,7);d->SetPixelShader(localFogCombinePS);
            d->SetTexture(9,localFogAccum);d->SetSamplerState(9,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(9,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
            const bool restored=check(d->SetRenderTarget(0,fogSurface),"lamp fog target restore");
            if(restored&&!whole){d->SetScissorRect(&clip);d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);}
            const bool combined=restored&&check(quad(w/2,h/2),"local fog glow combine");
            d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
            d->SetTexture(9,nullptr);d->SetSamplerState(9,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(9,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
            d->SetPixelShaderConstantF(21,c[21],1);
            if(!combined)return false;
            }
        }
        }else{
            // Composite is color * fog.a + fog.rgb: transparent fog is (0,0,0,1).
            // Clear every disabled frame so no fog history/scratch can leak in.
            d->SetTexture(9,nullptr);D3DVIEWPORT9 vp={0,0,w/2,h/2,0,1};d->SetViewport(&vp);
            if(!check(d->SetRenderTarget(0,fogSurface),"neutral fog target")||
               !check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"neutral fog clear"))return false;
        }
        if(profile)profile->mark("Fog");bucket(NorthlightEffectsBuckets::Fog);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        // 0.3.199 (fog temporal): the fog is complete; accumulate it with last frame's resolved fog (reprojected through the previous view still in c53..c56,
        // depth-checked against the previous frame's distance in temporalDepth[temporalIndex] since TemporalLight already rotated, neighbourhood-clamped).
        // fog (raw, s9) + fogHistory (s14) -> fogBlurred, then fogBlurred<->fogHistory swap (texture and surface): fogHistory holds this frame's resolved fog for
        // the next one. History not usable (first frame, map change, camera jump, debug, fog was off last frame): weight 0, a plain copy that makes it valid.
        // Off (no fog effect, debug, no shader): none of this runs, the sequence and bindings below are the old ones.
        if(effects.fog&&fogTemporalPS&&fogHistory&&debug==0){
            const float info[4]={0,fogHistoryValid&&useHistory?FogTemporalWeight:0.f,0,0};
            d->SetPixelShaderConstantF(53,c[53],4);d->SetPixelShaderConstantF(64,info,1);
            d->SetRenderTarget(0,fogBlurredSurface);d->SetTexture(9,fog);d->SetTexture(14,fogHistory);d->SetTexture(15,temporalDepth[temporalIndex]);
            for(unsigned sampler=14;sampler<16;++sampler){d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_MINFILTER,sampler==14?D3DTEXF_LINEAR:D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,sampler==14?D3DTEXF_LINEAR:D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(sampler,D3DSAMP_SRGBTEXTURE,FALSE);}
            d->SetPixelShader(fogTemporalPS);const bool resolved=check(quad(w/2,h/2),"fog temporal pass");
            d->SetTexture(14,nullptr);d->SetTexture(15,nullptr);d->SetSamplerState(14,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(14,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
            if(!resolved){fogHistoryValid=false;return false;}
            std::swap(fogBlurred,fogHistory);std::swap(fogBlurredSurface,fogHistorySurface);fogHistoryValid=true;fogResolved=true;
            if(profile)profile->mark("FogTemporal");
        }else fogHistoryValid=false;
        // Separable depth-aware blur: fog -> fogBlurred (horizontal) -> fog (vertical); with the temporal pass the resolved fogHistory -> fog -> fogBlurred (the final fog).
        if(effects.fog){float blur[4]={2,0,0,0};d->SetPixelShader(fogBlurPS);
         d->SetRenderTarget(0,fogResolved?fogSurface:fogBlurredSurface);d->SetTexture(9,fogResolved?fogHistory:fog);d->SetPixelShaderConstantF(34,blur,1);if(!check(quad(w/2,h/2),"volume blur horizontal"))return false;
         blur[0]=0;blur[1]=2;d->SetRenderTarget(0,fogResolved?fogBlurredSurface:fogSurface);d->SetTexture(9,fogResolved?fog:fogBlurred);d->SetPixelShaderConstantF(34,blur,1);if(!check(quad(w/2,h/2),"volume blur vertical"))return false;}
        // c34 belongs to the blur above and to HorizonHaze ONLY in the final pass;
        // set on fog-off frames too, where the blur did not run.
        d->SetPixelShaderConstantF(34,haze.haze,1);
        /* 0.3.202 (rain): RainMask on s13 for the composite only (the loop above already set POINT/CLAMP/no mip/sRGB off on it); the pointer is frame-local, the regional fog field goes back right after the quad */
        d->SetTexture(13,rainMask?rainMask:neutralZero);rainMask=nullptr; /* never the regional fog field: null when neutralZero could not be created */
        /* Dither on s2 (ShadowNear, idle after the fog passes): POINT/no mip/sRGB off from the frame-start loop, WRAP for the 64-pixel tile; CLAMP again after the quad */
        d->SetTexture(2,ditherNoise?ditherNoise:neutralZero);d->SetSamplerState(2,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(2,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
        // TemporalLight already rotated the index: 1-index is this frame's
        // distance, including frames without a directional light source.
        d->SetTexture(15,temporalDepth[1-temporalIndex]);
        d->SetSamplerState(15,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(15,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
        d->SetSamplerState(15,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(15,D3DSAMP_SRGBTEXTURE,FALSE);
        d->SetSamplerState(15,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(15,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        d->SetRenderTarget(0,targetSurface);d->SetTexture(8,temporalLight[1-temporalIndex]);d->SetTexture(9,fogResolved?fogBlurred:fog);d->SetPixelShader(finalPS);
        /* 0.3.212 (task 32): with this frame's name marks the quad draws with the world depth-stencil bound and a stencil test that rejects marked pixels (they keep the game's colour in the target); no marks: the quad as before. */
        bool compositeOk=false;
        {IDirect3DSurface9* maskDepth=nameMaskDepth;nameMaskDepth=nullptr;NameMaskCompositeBind mask(d,maskDepth);compositeOk=check(quad(w,h),"world composite");}
        d->SetTexture(15,nullptr);d->SetTexture(2,nullptr);d->SetSamplerState(2,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(2,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        if(!compositeOk)return false;composited=true;d->SetTexture(13,regionalFogTexture);if(profile)profile->mark("WorldComposite");
        if(diagnosticCapture){
            gpuDiagnosticArmed=false;unsigned capture=++gpuDiagnosticCaptures;
            const std::string& directory=gpuDiagnosticDirectory;
            float actual[68][4]={};HRESULT read=d->GetPixelShaderConstantF(0,actual[0],68);
            if(SUCCEEDED(read)){
                char suffix[80];std::snprintf(suffix,sizeof suffix,"/capture-%u-constants.f32",capture);
                if(FILE* f=std::fopen((directory+suffix).c_str(),"wb")){std::fwrite(actual,sizeof actual,1,f);std::fclose(f);}
            }
            logf("WORLD GPU diagnostic capture=%u map=\"%s\" eye=(%.3f %.3f %.3f) moon=(%.6f %.6f %.6f) constantsHRESULT=%08lx",capture,active->map.c_str(),context.camera[0],context.camera[1],context.camera[2],sourceDirections[1].x,sourceDirections[1].y,sourceDirections[1].z,(unsigned long)read);
            using NorthlightWorldDiagnostics::dump;
            dump(d,shadowSurface[firstSource*2],directory,"shadow-near",capture);
            dump(d,shadowSurface[firstSource*2+1],directory,"shadow-far",capture);
            dump(d,normalSurface,directory,"normals",capture);
            dump(d,baselineSurface,directory,"baseline",capture);
            dump(d,lightSurface,directory,"light",capture);
            dump(d,c[30][1]>0?smoothSurface:lightSurface,directory,"smoothed-light",capture); // what TemporalLight read: the raw light when no source is drawn
            dump(d,temporalLightSurface[1-temporalIndex],directory,"temporal-light",capture);
            dump(d,temporalDepthSurface[1-temporalIndex],directory,"distance",capture);
            dump(d,fogResolved?fogBlurredSurface:fogSurface,directory,"fog",capture);
            // 0.3.174 FOLD: the proxy's scene copy, before AO and bloom (the colour copy was skipped).
            if(foldScene){IDirect3DSurface9* folded=nullptr;if(SUCCEEDED(foldScene->GetSurfaceLevel(0,&folded))){dump(d,folded,directory,"scene",capture);folded->Release();}}
            else dump(d,colorSurface,directory,"scene",capture);
            logf("WORLD GPU diagnostic capture=%u scene=%s",capture,foldScene?"pre-AO (folded AO/bloom)":"post-AO composite");
        }
        if(++frames==1||(frames%600==0&&NorthlightDiagnostics::enabled()))logf("WORLD frame=%u GI probes=%u valid=%u rays=%u bounces=%u cacheTriangles=%zu replayDraws=%zu liveTerrainChunks=%zu terrainAttempts=%u terrainSnapshots=%u cascadesPerSource=2x1024 volumeStepsMax=49 fogWorldSpacing=2.667 fogShadowFilter=1.5 fogLightHeight=12 fogAmbient=0.35",frames,NorthlightGI::probeLayout().count(),active->validProbes,quality.giRays,quality.giBounces,active->bvh->triangleCount(),replays.size(),liveTerrainChunks.size(),terrainAttempts,terrainSnapshots);
        if(frames%600==0&&NorthlightDiagnostics::enabled())logf("WORLD global light holds=%u predicted=%u mispredicted=%u",globalLightHold.held,globalLightHold.predictedAccepts,globalLightHold.predictedMisses); /* 0.3.207: sky block reads between the copy and the glare darkening; colours matched / not matched by the glow prediction */
        if(captureSampled)logf("VOLUME sources sunRGB=%.5f,%.5f,%.5f moonRGB=%.5f,%.5f,%.5f ambientRGB=%.5f,%.5f,%.5f sunGain=1.2 moonGain=1.2 directCaps=0.38,0.24 airCells=%u airDensity=%.5f,%.5f forestAir=%.4f night=%.3f",
            sourceColors[0].x,sourceColors[0].y,sourceColors[0].z,sourceColors[1].x,sourceColors[1].y,sourceColors[1].z,
            context.ambient[0],context.ambient[1],context.ambient[2],uploadedFogField?uploadedFogField->airCells:0,c[32][0],c[32][1],c[22][3],c[31][3]);
        if(profileSampled()){ /* hue candidates (RenderProfile): compare at Brill / Durotar coast / Orgrimmar with the Light DBC */
            const auto& g=glowHueFrame;float fogDirectRGB[3],fogAmbientRGB[3],liftRGB[3],slot[5][3];
            const float sunRGB[3]={sourceGlowColors[0].x,sourceGlowColors[0].y,sourceGlowColors[0].z};NorthlightSunHue::fogDirect(sunRGB,g,fogDirectRGB);NorthlightSunHue::coolAmbient(c[18],g,sourceWeights[0],fogAmbientRGB);NorthlightSunHue::horizonLift(g,liftRGB);
            const unsigned slots[5]={NorthlightSunHue::SlotDirect,NorthlightSunHue::SlotFog,NorthlightSunHue::SlotSun,NorthlightSunHue::SlotSunHalo,NorthlightSunHue::SlotSkyAboveHorizon};
            for(unsigned i=0;i<5;++i)NorthlightSunHue::unpack(lightSlots[slots[i]],slot[i]);
            logf("SUNHUE valid=%d slots=%d native=%.3f strength=%.3f direct=%.3f,%.3f,%.3f legacyFog=%.3f,%.3f,%.3f band0=%.3f,%.3f,%.3f band7fog=%.3f,%.3f,%.3f band9sunGlare=%.3f,%.3f,%.3f band10sunHalo=%.3f,%.3f,%.3f slot6band5sky=%.3f,%.3f,%.3f "
                "H=%.3f,%.3f,%.3f core=%.3f,%.3f,%.3f moon=%.3f,%.3f,%.3f lift=%.3f,%.3f,%.3f fogDirect=%.3f,%.3f,%.3f fogAmbient=%.3f,%.3f,%.3f caps=%.2f,%.2f",
                int(g.valid),int(lightSlotsValid),g.native,g.strength,context.direct[0],context.direct[1],context.direct[2],legacyFog.color[0],legacyFog.color[1],legacyFog.color[2],
                slot[0][0],slot[0][1],slot[0][2],slot[1][0],slot[1][1],slot[1][2],slot[2][0],slot[2][1],slot[2][2],slot[3][0],slot[3][1],slot[3][2],slot[4][0],slot[4][1],slot[4][2],
                g.sun[0],g.sun[1],g.sun[2],g.sunCore[0],g.sunCore[1],g.sunCore[2],g.moon[0],g.moon[1],g.moon[2],liftRGB[0],liftRGB[1],liftRGB[2],
                fogDirectRGB[0],fogDirectRGB[1],fogDirectRGB[2],fogAmbientRGB[0],fogAmbientRGB[1],fogAmbientRGB[2],NorthlightSunHue::SunForwardCap,NorthlightSunHue::MoonForwardCap);
        }
        if(frames==1||frames%600==0){if(frames==1||NorthlightDiagnostics::enabled())logf("LOCAL direct lights=%u limit=%u nearestReach=%.1f visibilityEnd=%.1f fogDistance=%.1f strength=%.3f lampGain=%.3f sunlitCut=%.3f available=%zu scissor=%d batches=%u clipped=%u skipped=%u coverage=%.3f fading=%u selectUs=%.1f selectMaxUs=%.1f offscreen=%.1f arranged=%u widePasses=%u arrangedCost=%.3f",localDirectCount,quality.localLightLimit,localDirectNearest,NorthlightLocalLightSelection::VisibilityEnd,localLights.fogDistance,c[52][1],lampGain,lampSunlitCut,active?active->localLights.size():size_t(0),
            int(NorthlightLocalLightScissor::Enabled),localScissorBatches,localScissorClipped,localScissorSkipped,localScissorBatches?localScissorCoverage/localScissorBatches:1.,
            localLightTracker.fading,localSelectFrames?localSelectUsSum/localSelectFrames:0.,localSelectUsMax,
            localSelectFrames?double(localOffscreenSum)/localSelectFrames:0.,localArranged,localWidePasses,localArrangeBefore>0?localArrangeAfter/localArrangeBefore:1.);
            localScissorBatches=localScissorClipped=localScissorSkipped=0;localScissorCoverage=0;localSelectUsSum=localSelectUsMax=0;localSelectFrames=0;
            localOffscreenSum=localArranged=localWidePasses=0;localArrangeBefore=localArrangeAfter=0;
            if(frames>1&&NorthlightDiagnostics::enabled())logf("GI probe blend publishes=%u blendedSlots=%u texture=%d",probeBlendPublishes,probeBlendSlots,int(probePrev!=nullptr));
            if(frames>1&&NorthlightDiagnostics::enabled()){
                const auto& r=active->relightStats;
                logf("GI relight hits=%llu tracedProbes=%llu shapeResets=%llu cacheKiB=%.1f",(unsigned long long)r.hits,(unsigned long long)r.misses,(unsigned long long)r.resets,double(r.bytes)/1024);
            }
            probeBlendPublishes=probeBlendSlots=0;}
        if(frames==1||(frames%600==0&&NorthlightDiagnostics::enabled()))logf("HORIZON haze strength=%u start=%u band=%u terrain=%u fog=%u colorKnown=%d fogEnd=%.1f far=%.1f startZ=%.1f tau=%.3f zone=%.3f rgb=%.3f,%.3f,%.3f sun=%.4f,%.4f",
            quality.horizonHaze,quality.horizonHazeStart,quality.horizonHazeBand,quality.horizonHazeTerrain,unsigned(effects.fog),int(horizonHazeState.colorKnown),horizonHazeState.end,farZ,haze.shape[0],
            haze.haze[3]/NorthlightHorizonHaze::Log2e,hazeZone,haze.haze[0],haze.haze[1],haze.haze[2],haze.sun[0],haze.sun[1]);
        if(frames==1||(frames%600==0&&NorthlightDiagnostics::enabled())){ /* every 600 frames with Diagnostics=1 (RenderProfile needs it too), so one test session answers the hue question on stock and HD */
            const auto& g=glowHueFrame;float band9[3],band10[3];NorthlightSunHue::unpack(lightSlots[NorthlightSunHue::SlotSun],band9);NorthlightSunHue::unpack(lightSlots[NorthlightSunHue::SlotSunHalo],band10);
            logf("CELESTIAL glowHue src=%s native=%.2f strength=%.2f moonStrength=%.0f rgb=%.3f,%.3f,%.3f core=%.3f,%.3f,%.3f moon=%.3f,%.3f,%.3f band9=%.3f,%.3f,%.3f band10=%.3f,%.3f,%.3f direct=%.3f,%.3f,%.3f fog=%.3f,%.3f,%.3f",
                !g.valid||g.strength<=0?"fallback":g.native>=.5f?"native":"sunHalo",g.native,g.strength,g.moonStrength,g.sun[0],g.sun[1],g.sun[2],g.sunCore[0],g.sunCore[1],g.sunCore[2],g.moon[0],g.moon[1],g.moon[2],
                band9[0],band9[1],band9[2],band10[0],band10[1],band10[2],context.direct[0],context.direct[1],context.direct[2],legacyFog.color[0],legacyFog.color[1],legacyFog.color[2]);
        }
        if(frames==1||(frames%600==0&&NorthlightDiagnostics::enabled()))logf("CELESTIAL valid=%d gameDay=%.6f source0Weight=%.4f moonWeight=%.4f authoredFill=%.4f sunZ=%.4f moonZ=%.4f regionalFog=%.4f",celestialValid,celestialValid?celestial.dayFraction:0.f,sourceWeights[0],sourceWeights[1],authoredFill,sourceDirections[0].z,sourceDirections[1].z,c[27][3]);
        if(frames==1||frames%600==0){if(frames==1||NorthlightDiagnostics::enabled())logf("WORLD shadow batches near=%u far=%u culledNear=%u culledFar=%u liveUploadBytes=%zu cacheRenders=%u cacheReuses=%u culledReplays=%u staticFull=%u staticPartial=%u partialRects=%u partialBounding=%u costFull=%u partialTexelPct=%.1f partialSkippedDraws=%u partialCalls=%llu partialFullCalls=%llu dedupUncommitted=%u verified=%u verifyMismatches=%u scissorLeaks=%u slicedBands=%u sliceRestarts=%u",drawnBatches[0],drawnBatches[1],culledBatches[0],culledBatches[1],terrainUploadBytes,shadowCacheRenders,shadowCacheReuses,culledReplayDraws,
            shadowCacheStaticFullRenders,shadowCachePartialRenders,shadowCachePartialRects,shadowCachePartialBounding,shadowCacheCostFull,shadowCachePartialRenders?100.0*double(shadowCachePartialTexels)/(double(shadowCachePartialRenders)*ShadowCacheSize*ShadowCacheSize):0.0,shadowCachePartialSkippedDraws,shadowCachePartialCalls,shadowCachePartialFullCalls,staticDedupUncommitted,shadowCacheVerified,shadowCacheVerifyMismatches,shadowCacheScissorLeaks,staticSliceBands,staticSliceRestarts);
            shadowCacheRenders=shadowCacheReuses=culledReplayDraws=0;shadowCacheStaticFullRenders=shadowCachePartialRenders=shadowCachePartialSkippedDraws=shadowCachePartialRects=shadowCachePartialBounding=shadowCacheCostFull=0;shadowCachePartialTexels=shadowCachePartialCalls=shadowCachePartialFullCalls=0;staticDedupUncommitted=0;staticSliceBands=staticSliceRestarts=0;}
        if(frames==1||frames%600==0){if(frames==1||NorthlightDiagnostics::enabled())logf("SHADOW reuse nearInterval=%u farInterval=%u nearReuses=%u farReuses=%u captureSkippedFrames=%u captureDeferrals=%u captureSkip=%u",
            quality.nearShadowInterval,quality.farShadowInterval,nearReuses,farReuses,captureSkippedFrames,captureDeferrals,unsigned(NorthlightQuality::captureSkipPossible(quality,NorthlightQuality::actorShadowWork(quality,effects.shadows))));
            nearReuses=farReuses=captureSkippedFrames=captureDeferrals=0;}
        if(lightingClock)logf("WORLD lighting setup prefixMs=%.3f atmosphereMs=%.3f constantsMs=%.3f bindingsMs=%.3f spellFitMs=%.3f spellFitRevision=%u",lightingSetupMs[0],lightingSetupMs[1],lightingSetupMs[2],lightingSetupMs[3],spellFitMs,spellFogHold.out.revision);
        if(captureSampled)logf("WORLD terrain reuse=%u snapshots=%zu uploadBytes=%zu vertexUploadBytes=%zu reusedVertices=%zu arenaMiB=%.2f rollovers=%u change=%s newOwners=%u directionalBuilds=%u directionalTriangles=%zu straddlingTriangleTests=%zu collectVisited=%zu terrainMs=%.3f,%.3f,%.3f",unsigned(terrainUploadReused),frameTerrain.size(),terrainUploadBytes,liveTerrainGPU.uploadedBytes,liveTerrainGPU.reusedVertices,double(liveTerrainGPU.bytes())/1048576,liveTerrainGPU.rollovers,
            terrainChange,liveTerrainGPU.newOwners,liveTerrainGPU.directionalBuilds,liveTerrainGPU.directionalTriangles,terrainTriangleTests,liveTerrainGPU.collectVisited,terrainPartMs[0],terrainPartMs[1],terrainPartMs[2]);
        if(frames==1||(frames%600==0&&NorthlightDiagnostics::enabled()))logf("WORLD replay constants bytes=%zu previousBytes=%zu calls=%zu previousCalls=%zu",replayConstantBytes,replays.size()*2*(1024*4+16*4+64*4+16*4),replayConstantCalls,replays.size()*2*4);
        if(frames==1||frames%600==0){
            if(frames==1||NorthlightDiagnostics::enabled())logf("WORLD replay cull tested=%llu,%llu,%llu,%llu bounded=%llu,%llu,%llu,%llu culled=%llu,%llu,%llu,%llu staticM2Skipped=%llu,%llu,%llu,%llu (sun near,far moon near,far)",
                replaySlotTested[0],replaySlotTested[1],replaySlotTested[2],replaySlotTested[3],replaySlotBounded[0],replaySlotBounded[1],replaySlotBounded[2],replaySlotBounded[3],
                replaySlotCulled[0],replaySlotCulled[1],replaySlotCulled[2],replaySlotCulled[3],replaySlotM2Skipped[0],replaySlotM2Skipped[1],replaySlotM2Skipped[2],replaySlotM2Skipped[3]);
            for(unsigned n=0;n<4;++n)replaySlotTested[n]=replaySlotBounded[n]=replaySlotCulled[n]=replaySlotM2Skipped[n]=0;}
        if(frames==1||(frames%600==0&&NorthlightDiagnostics::enabled()))logf("WORLD replay poses draws=%zu groups=%u prepared=%zu reused=%zu scope=directional exact=1",replays.size(),replays.empty()?0:replays.back()->constantGroup+1,replayPosePrepared,replayPoseReused);
        DWORD elapsed=GetTickCount()-submissionStart;
        if(elapsed>40&&slowReports++<12)logf("WORLD slow submission: %lu ms; replay=%zu liveTerrainChunks=%zu",(unsigned long)elapsed,replays.size(),liveTerrainChunks.size());
        shadowsComposited=effects.shadows&&(sourceActive[0]||sourceActive[1])&&debug==0;
        if(shadowsComposited){
            terrainCoverageSources=0;
            for(unsigned source=0;source<2;++source)if(sourceActive[source])std::memcpy(terrainCoverageMatrices[terrainCoverageSources++],sourceMatrices[source][1],64);
            terrainCoverageRowsValid=false;
        }
        bucket(NorthlightEffectsBuckets::Composite); /* the world composite, temporal and the frame's logs */
        return true;
    }
};
