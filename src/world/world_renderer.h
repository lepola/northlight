#pragma once
#include "stream_hooks.h"
#include "world_context.h"
#include "effect_switches.h"
#include "world_camera.h"
#include "wmo_context.h"
#include "projection.h"
#include "celestial_context.h"
#include "celestial_sources.h"
#include "twilight_fill.h"
#include "regional_fog.h"
#include "regional_shadow_range.h"
#include "celestial_profiles.h"
#include "world_gi.h"
#include "world_local_lights.h"
#include "local_light_selection.h"
#include "local_light_scissor.h"
#include "point_light_shadow.h"
#include "local_light_compiled_shaders.h"
#include "replay_bounds.h"
#include "replay_bounds_schedule.h"
#include "replay_bounds_metadata.h"
#include "replay_bounds_job.h"
#include <atomic>
#include <optional>
#include "world_math.h"
#include "world_mesh_plan.h"
#include "world_streaming.h"
#include "world_mesh_pages.h"
#include "world_mesh_page_gpu.h"
#include "streaming_budget.h"
#include "streaming_phase_profile.h"
#include "cpu_retirement.h"
#include "deferred_gpu_release.h"
#include "geometry_memory.h"
#include "terrain_reach_fallback.h"
#include "memory_admission_probe.h"
#include <chrono>
#include "world_probe_cache.h"
#include "world_probe_progress.h"
#include "probe_activation.h"
#include "probe_blend.h" // 0.3.197
#include "patch_terrain_shadow.h"
#include "celestial_time_warp.h"
#include "cascade_anchor.h"
#include "shadow_pivot.h"
#include "world_dynamic_probes.h"
#include "shadow_bounds.h"
#include "static_cache_slices.h"
#include "terrain_shadow_candidates.h"
#include "legacy_fog.h"
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
#include "static_shadow_coverage.h"
#include "celestial_terrain.h"
#include "quality_settings.h"
#include "diagnostics_switch.h"
#include "gi_solve_pool.h"
#include "near_reserve.h"
#include "effects_buckets.h"
#include <cstdarg>
#include "rigid_memory.h"

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
        bool staticOnly=false,partial=false;
    };
    // probeCenter (0.3.153 GIProbeAhead): probe window origin, solve order and move trigger; geometryCenter (0.3.169 lead):
    // the region build centre and its refresh trigger; camera (eye) for everything else, including every coverage check.
    struct Request { std::shared_ptr<const NorthlightActorGeometry::ActorJob> actorJob;uint64_t actorSerial=0; V camera,probeCenter,geometryCenter; NorthlightGI::Lighting light; std::string map; uint64_t id=0,baseId=0;DWORD queuedAt=0;unsigned reason=0; };
    IDirect3DDevice9* d;
    std::string root;
    std::unique_ptr<StaticShadow::Streamer> staticStream;
    StaticShadow::GpuCache staticCasters;
    std::shared_ptr<const StaticShadow::Snapshot> staticScene;
    StaticShadowDedup::Matcher staticMatcher;
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
public:
    // Read-only access to the COMMITTED terrain pages. Never start uploads,
    // loads, shadow updates or mesh publication from the native sky draw hook.
    uint64_t celestialTerrainGeneration()const{
        char map[64]={};float camera[3];
        if(failed||activeMesh<0||batches.empty()||!NorthlightWorldContext::readMapAndCamera(map,camera)||uploadedMap!=map)return 0;
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
            logf("CELESTIAL terrain mask map=%s generation=%llu draws=%u triangles=%llu candidates=%zu accepted=%u runs=%u listed=%d redraws=%u,%u reuses=%u,%u ms=%.3f peakMs=%.3f",
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
    IDirect3DTexture9* neutralZero=nullptr; // 0.3.202 (rain): 1x1 (0,0,0,0) on s13 (RainMask) when no mask was drawn this frame
    IDirect3DTexture9* rainMask=nullptr; // 0.3.202 (rain): the proxy's rain streak alpha mask (non-owning, set per frame, cleared after the composite)
    IDirect3DTexture9* neutralAO=nullptr; // 0.3.174: 1x1 (0,0,0,1) on s10 when the proxy does not fold its AO/bloom
    std::shared_ptr<const NorthlightRegionalFog::Field> uploadedFogField;
    NorthlightWorldContext::TerrainContext context;
    NorthlightCelestial::Context celestial,celestialLight;
    unsigned celestialOrbitReports=0;
    NorthlightCelestialOrbit::LightMotion celestialLightMotion;
    std::string celestialLightMap;V celestialLightCamera;
    bool continuousCelestialShadows=false;
    NorthlightCelestialProfiles::Table celestialProfiles;
    NorthlightRegionalShadow::Table shadowRanges;
    NorthlightCelestialProfiles::Transition paletteTransition;
    NorthlightCelestialProfiles::Profile framePalette;
    bool paletteFrameValid=false;std::string paletteFrameMap;DWORD paletteLogAt=0;
    struct PaletteRegion {std::string map;NorthlightRegionalFog::Region region;int tx=0,ty=0;};
    std::shared_ptr<const PaletteRegion> publishedPaletteRegion; // guarded by mutex
    bool celestialValid=false,shadowFrameReady=false;float authoredFill=1;
    // 0.3.200: the last good celestial read (see updateWorldContext), held across single rejected reads.
    static constexpr DWORD CelestialHoldMs=500;NorthlightCelestial::Context celestialHeld;std::string celestialHeldMap;DWORD celestialHeldAt=0;bool celestialHeldOk=false;unsigned long celestialHolds=0;
    NorthlightEffectSwitches::Settings effects;
    bool neutralShadowMaps=false;
    V sourceDirections[2],sourceColors[2];
    float sourceWeights[2]={1,0};
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
    NorthlightHorizonHaze::State horizonHazeState; /* game fog end/colour, smoothed per map */
    /* glow hue: the game's light slots (band 9 native glare, band 10 sunHalo), held for 2 s on one map without a proven read. */
    std::uint32_t lightSlots[NorthlightSunHue::Slots]={};bool lightSlotsValid=false;std::string lightSlotsMap;DWORD lightSlotsAt=0;
    NorthlightSunHue::GlowHue glowHueFrame;
    NorthlightHorizonHaze::Constants hazeFrame; /* last composite haze constants, for the celestial veil */
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
    unsigned long long replaySlotTested[4]={},replaySlotBounded[4]={},replaySlotCulled[4]={}; /* per source*2+cascade, 600-frame window */
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
        for(const auto& b:batches){
            if(b.terrain&&!fixedTerrainChunks().count({b.chunkX,b.chunkY}))continue;
            if(NorthlightShadowBounds::directionalClipReject(b.boundsLow,b.boundsHigh,matrix))continue;
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
    void invalidateShadowCache(){staticCasters.settle();dropStaticDirtyJobs();for(auto& k:shadowCacheKey){k.valid=false;k.staticContent.valid=false;}for(auto& c:staticSlices)c.reset();pointCacheValid=false;for(int s=0;s<2;++s){farShadow[s].invalidate();nearShadow[s].invalidate();}}
    uint64_t liveChunkHash()const{uint64_t h=1469598103934665603ull;for(const auto& c:liveTerrainChunks){h^=uint64_t(uint32_t(c.first))*0x9E3779B97F4A7C15ull+uint64_t(uint32_t(c.second));h*=1099511628211ull;}return h;}
    IDirect3DPixelShader9 *cachedFastPS=nullptr,*cachedOpaqueFastPS=nullptr,*cachedOpaquePS=nullptr;
    IDirect3DPixelShader9 *lightingPS=nullptr,*giPS=nullptr,*fogPS=nullptr,*fogBlurPS=nullptr,*localDirectPS=nullptr,*removalPS=nullptr,*temporalPS=nullptr,*localFogPS=nullptr,*normalsPS=nullptr,*sourceVisPS=nullptr,*finalPS=nullptr,*shadowPS=nullptr,*cachedShadowPS=nullptr,*replayPS=nullptr;
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
    // 0.3.197: soft cap, incumbent bias and fades for the local lights (task 13). The clock is the selection's own: its gap and reset policy mirror useHistory.
    NorthlightLocalLightSelection::Tracker localLightTracker;std::string localLightMap;V localLightCamera;int64_t localLightQpc=0;double localSelectUsSum=0,localSelectUsMax=0;unsigned localSelectFrames=0;
    // Additive local light passes add exactly +0 outside their batch's projected
    // spheres; scissor them there. Counters cover one LOCAL log interval.
    NorthlightLocalLightScissor::View localScissorView;
    unsigned localScissorBatches=0,localScissorClipped=0,localScissorSkipped=0;double localScissorCoverage=0;
    // 0.3.200 (jobs): known = the batch's rect from atmosphereWork (the same batchRect of the same view and positions), else computed here.
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
    IDirect3DVertexBuffer9* vertices=nullptr; // non-owning alias of meshPool[activeMesh]
    IDirect3DIndexBuffer9* indices=nullptr;
    // Two persistent page pools: the inactive generation stages bounded static
    // resources, and all pages become drawable together after complete upload.
    struct MeshBuffers {IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;UINT vbCapacity=0,ibCapacity=0;};
    std::vector<MeshBuffers> meshPool[2];int activeMesh=-1;
    DWORD memoryPressureLogAt=0,memoryPressureUntil=0;
    int stagingSlot()const{return activeMesh<0?0:1-activeMesh;}
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
        memoryPressureUntil=now+1000;
        if(!memoryPressureLogAt||now-memoryPressureLogAt>=60000){memoryPressureLogAt=now;logGeometryMemory(stage,memory,0,exact&&!cachedSample);}
        return false;
    }
    bool acquireMeshPage(size_t page,bool vertex,UINT bytes,NorthlightGeometryMemory::Sample& memory){
        MeshBuffers& slot=meshPool[stagingSlot()][page];
        if(vertex?slot.vb&&slot.vbCapacity>=bytes:slot.ib&&slot.ibCapacity>=bytes)return true;
        // At most 512 KiB per resource, including rounded unused capacity.
        const UINT capacity=roundBuffer(bytes,64u<<10);
        if(!NorthlightGeometryMemory::admitsSmallPageGrowth(memory.valid,memory.available,capacity))return false;
        if(vertex){
            drop(slot.vb);slot.vbCapacity=0;
            if(FAILED(d->CreateVertexBuffer(capacity,D3DUSAGE_WRITEONLY,0,D3DPOOL_MANAGED,&slot.vb,nullptr))||!slot.vb){drop(slot.vb);return false;}
            slot.vbCapacity=capacity;
        }else{
            drop(slot.ib);slot.ibCapacity=0;
            if(FAILED(d->CreateIndexBuffer(capacity,D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_MANAGED,&slot.ib,nullptr))||!slot.ib){drop(slot.ib);return false;}
            slot.ibCapacity=capacity;
        }
        NorthlightGeometryMemory::debitGrowth(memory,capacity);return true;
    }
    void releaseMeshPool(){for(auto& pool:meshPool){for(auto& slot:pool){drop(slot.vb);drop(slot.ib);}pool.clear();}activeMesh=-1;vertices=nullptr;indices=nullptr;}
    std::vector<IDirect3DTexture9*> materials;
    NorthlightStreaming::DeferredRelease<IDirect3DTexture9> retiredMaterials;
    size_t uploadedTextureBytes=0;
    using Batch=NorthlightWorldMeshPages::Batch;
    bool bindMeshPage(const Batch& batch,UINT& boundPage){
        if(boundPage==batch.page)return true;
        if(activeMesh<0||batch.page>=meshPool[activeMesh].size())return false;
        auto& page=meshPool[activeMesh][batch.page];
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
        std::string map;
        NorthlightWorldMeshPages::Upload upload;
        IDirect3DTexture9* white=nullptr;
        std::vector<IDirect3DTexture9*> textures;
        size_t material=0,materialRow=0;
        IDirect3DTexture9* partialTexture=nullptr;
        DWORD started=GetTickCount();unsigned frames=0,textureUploads=0;
        // The upload cursor owns no GPU data; only textures are owned here.
        ~PendingMesh(){drop(partialTexture);drop(white);for(auto& t:textures)drop(t);}
    };
    std::unique_ptr<PendingMesh> pendingMesh;
    NorthlightWorldStreaming::Retry meshRetry;
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
        bool boneKnown=false;float bone=NAN; /* 0.3.176 (S2): this frame's selection tested its rigid bone (reset at capture) */
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
        uint32_t staticProofMask=0;uint64_t staticProofRevision=0;std::string staticProofModel;
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
    NorthlightShadowFate::Tracker shadowFate;unsigned otherBlendRejected=0,otherBudgetRejected=0,otherProjectionRejected=0;
    NorthlightActorShadowSelection::History actorShadowHistory;
    std::vector<NorthlightActorShadowSelection::Draw> actorShadowDraws;
    NorthlightActorShadowSelection::Scratch actorShadowScratch;size_t actorShadowToggles=0;unsigned actorShadowFrames=0;
    float actorShadowOrigin[3]={};bool actorShadowOriginValid=false,actorShadowOver=false;unsigned actorShadowTransitions=0,actorShadowCapBinds=0;double actorShadowPrepareMs=0,actorShadowChooseMs=0; /* last frame's shadow pivot ranks the actor quota */
    size_t actorShadowRadiusToggles=0,actorShadowRadiusFlicker=0,actorShadowRadiusRekeyed=0; /* ActorShadowRadius per log window: inside<->outside changes, changes within RadiusDwell of the last one, identities kept across a key change */
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
    std::uint64_t previousCacheHits=0;
    size_t capturedConstantBytes=0,capturedConstantCalls=0;
    // 0.3.180 (C1): the device mirror's constant clock, read in place under the draw's gate.
    NorthlightReplayCaptureConstants::ClockSource<DeviceMirror> constantEpochSource;
    // 0.3.180 (C0): constantEpochStats keeps the self-check (verify()) and pose counters; the epoch and
    // block counters of RenderProfile sample frames go to constantEpochProfile. Both are cleared per frame.
    NorthlightReplayCaptureConstants::Stats constantEpochStats,constantEpochProfile;
    NorthlightReplayCaptureConstants::SelfCheck constantSelfCheckState{0,&constantEpochStats};
    unsigned capturedSM1Draws=0,capturedRelativeDraws=0;
    unsigned skinnedCandidates=0,skinnedBlendRejected=0,skinnedProjectionRejected=0,skinnedBudgetRejected=0,skinnedSnapshotRejected=0,skinnedAccepted=0;
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
        bool skinned=false,sm1=false;
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
        sample.available=info.ullAvailVirtual;
        uintptr_t address=0;MEMORY_BASIC_INFORMATION region={};
        while(VirtualQuery(reinterpret_cast<const void*>(address),&region,sizeof region)==sizeof region){
            if(region.State==MEM_FREE)sample.largest=std::max(sample.largest,uint64_t(region.RegionSize));
            const uint64_t next=uint64_t(reinterpret_cast<uintptr_t>(region.BaseAddress))+uint64_t(region.RegionSize);
            if(next<=address||next>std::numeric_limits<uintptr_t>::max())break;
            address=uintptr_t(next);
        }
        sample.valid=sample.largest>0;return sample;
    }
    static NorthlightGeometryMemory::Sample geometryAdmissionFor(NorthlightMemoryAdmission::Probe& probe,NorthlightGeometryMemory::Budget budget,bool* exact=nullptr){
        // Remember only candidate addresses. Every admission gets current
        // aggregate availability and a fresh query of the candidate free block.
        // A stale/small witness falls back to the complete address-space walk.
        const auto result=probe.sample(budget,
            [](uint64_t& available){MEMORYSTATUSEX info={};info.dwLength=sizeof info;
                if(!GlobalMemoryStatusEx(&info))return false;available=info.ullAvailVirtual;return true;},
            [](uint64_t address,NorthlightMemoryAdmission::Region& output){MEMORY_BASIC_INFORMATION region={};
                if(VirtualQuery(reinterpret_cast<const void*>(uintptr_t(address)),&region,sizeof region)!=sizeof region)return false;
                output.base=uint64_t(reinterpret_cast<uintptr_t>(region.BaseAddress));output.size=uint64_t(region.RegionSize);output.free=region.State==MEM_FREE;return true;},
            uint64_t(std::numeric_limits<uintptr_t>::max()));
        if(exact)*exact=result.fullScan;
        return result.memory;
    }
    NorthlightGeometryMemory::Sample geometryAdmission(NorthlightGeometryMemory::Budget budget,bool* exact=nullptr){
        return geometryAdmissionFor(admissionProbe,budget,exact);
    }
    static void logGeometryMemory(const char* stage,NorthlightGeometryMemory::Sample sample,size_t generations=0,bool exactLargest=true){
        logf("GEOMETRY MEMORY stage=%s availableVirtualMiB=%llu largestFreeMiB=%llu generations=%zu valid=%u largestFreeExact=%u",
            stage,(unsigned long long)(sample.available>>20),(unsigned long long)(sample.largest>>20),generations,unsigned(sample.valid),unsigned(exactLargest));
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
                celestialValid,celestialValid?celestial.dayFraction:0.,staticAllowLoads));}
        catch(...){if(staticDrawFailures++<4)logf("STATIC SHADOW request deferred: allocation failure; existing shadows retained");}
    }
    void updateStaticCasters(bool allowUploads=true,NorthlightStreaming::Budget* budget=nullptr){
        staticCasters.settle(); /* 0.3.152: no plan job across the static cache update */
        ++staticFrame;
        if(!staticStream)return;
        const DWORD now=GetTickCount();
        auto next=staticStream->snapshot();
        // Never carry another world's casters while its replacement loads.
        if(staticScene&&staticScene->map!=lastRequest.map){staticCasters.reset();staticMatcher.clear();staticScene.reset();staticOwnerGeneration=UINT64_MAX;}
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
            logf("STATIC SHADOW map=%s generation=%llu placements=%u cpuModels=%u readyGPU=%u pendingCPU=%u pendingGPU=%u metadataReads=%llu modelReads=%llu reused=%llu cpuMiB=%.2f cpuPeakMiB=%.2f transientPeakMiB=%.2f gpuMiB=%.2f gpuPeakMiB=%.2f uploadBytes=%llu totalUploadBytes=%llu uploadMs=%.3f readMs=%.3f packMs=%.3f maxJobMs=%.3f latencyMs=%.3f instancing=%u draws=%llu instances=%llu cpuFailures=%llu gpuFailures=%llu dedupSkipped=%u complete=%u dedupAttempts=%u dedupMatches=%u localOwners=%zu metadataMiB=%.2f selectionMs=%.3f",
                 staticScene->map.c_str(),(unsigned long long)staticScene->mapGeneration,c.placements,c.models,g.readyModels,c.pendingModels,g.pendingModels,
                 (unsigned long long)c.metadataReads,(unsigned long long)c.modelReads,(unsigned long long)c.modelReuses,
                 double(c.cpuBytes)/1048576,double(c.cpuPeak)/1048576,double(c.transientPeak)/1048576,double(g.residentBytes)/1048576,double(g.peakBytes)/1048576,
                 (unsigned long long)g.frameBytes,(unsigned long long)g.uploadedBytes,g.uploadMs,c.readMs,c.packMs,c.maxJobMs,c.latencyMs,unsigned(g.instancing),
                 (unsigned long long)g.drawCalls,(unsigned long long)g.instances,(unsigned long long)c.failures,(unsigned long long)g.failures,staticDedupSkipped,unsigned(c.complete),staticDedupAttempts,staticDedupMatches,(uploadedStaticOwners?uploadedStaticOwners->placements:size_t(0)),double(c.metadataBytes)/1048576,c.selectionMs);
            staticDedupSkipped=staticDedupAttempts=staticDedupMatches=0;
        }
    }
    void work() {
      // Below-normal priority: the solve competes with the single game thread
      // for the same performance cores under Rosetta; it is latency-tolerant.
      SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
      try {
        // GIThreads>1: helpers at the same below-normal priority; probe values are scheduling-independent.
        std::unique_ptr<NorthlightGI::SolvePool> solvePool;
        const unsigned solverThreads=NorthlightQuality::giSolverThreads(quality,NorthlightStream::cores());
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
          NorthlightTerrainReach::State reachState;NorthlightTerrainReach::Episode memoryStall;
          // Episode logs (begin on the first memory refusal, end when the build is delivered or dropped).
          auto stallBegin=[&](const char* stage,NorthlightGeometryMemory::Sample sample){
              if(memoryStall.refused(GetTickCount()))logf("WORLD geometry memory stall begin stage=%s availableMiB=%llu largestMiB=%llu generations=%zu",
                  stage,(unsigned long long)(sample.available>>20),(unsigned long long)(sample.largest>>20),generations.live());
          };
          auto stallEnd=[&](const char* outcome){
              uint32_t ms=0;unsigned tries=0;
              if(memoryStall.end(GetTickCount(),ms,tries))logf("WORLD geometry memory stall end ms=%u attempts=%u outcome=%s",ms,tries,outcome);
          };
          NorthlightLocalLights::Cache lightCache(root+"world-cache/lights");
          // Last delivered region. Any build that does not deliver forces the next
          // request to build again, as the 0.3.151 worker did without a BVH.
          std::string builtMap;V builtCenter;bool builtValid=false,wanted=false,retry=false;uint64_t seen=0;unsigned superseded=0;
          for(;;){
            Request r;
            {std::unique_lock<std::mutex> lock(mutex);buildWake.wait(lock,[&]{return stopping||builderExit||retry||geometryWanted||request.id!=seen;});
             if(stopping||builderExit)return;r=request;seen=r.id;retry=false;wanted=wanted||geometryWanted;geometryWanted=false;}
            if(workerMemoryTrim.exchange(false,std::memory_order_relaxed))localGeometry.reset(); /* memory guard; rebuilt incrementally */
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
            auto deferBuild=[&](const char* stage,NorthlightGeometryMemory::Sample sample,unsigned delay){
                DWORD now=GetTickCount();if(!memoryReportAt||now-memoryReportAt>=1000){memoryReportAt=now;logGeometryMemory(stage,sample,generations.live(),workerMemoryExact);}
                // The consumed request must survive a stationary camera. Never
                // replace a newer request with r; next loop reads latest state.
                bool abandoned=false;
                {std::unique_lock<std::mutex> lock(mutex);retry=true;
                 buildWake.wait_for(lock,std::chrono::milliseconds(delay),[&]{return stopping||builderExit||request.id!=r.id;});
                 // The request moved on and this region no longer applies: the stalled build is dropped.
                 abandoned=stopping||builderExit||!NorthlightWorldStreaming::applicable(r.map,r.geometryCenter,request.map,request.camera);}
                if(abandoned)stallEnd("superseded");
            };
            auto built=std::make_unique<Built>();
            // BEGIN REGION BUILD: tests substitute a synthetic region for this block.
            {
                const auto memoryBefore=workerAdmissionProbe.stats();const double memoryMsBefore=workerMemoryMs;
                auto phaseStart=std::chrono::steady_clock::now();
                auto phaseElapsed=[&](){const auto now=std::chrono::steady_clock::now();const double ms=std::chrono::duration<double,std::milli>(now-phaseStart).count();phaseStart=now;return ms;};
                auto memory=workerMemory(NorthlightGeometryMemory::buildBudget(64*NorthlightGeometryMemory::MiB));
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
                if(!NorthlightGeometryMemory::admits(memory,NorthlightGeometryMemory::buildBudget(64*NorthlightGeometryMemory::MiB))){localGeometry.reset();memory=workerMemory(NorthlightGeometryMemory::buildBudget(64*NorthlightGeometryMemory::MiB));}
                if(!NorthlightGeometryMemory::admits(memory,NorthlightGeometryMemory::buildBudget(64*NorthlightGeometryMemory::MiB))){stallBegin("build-deferred",memory);deferBuild("build-deferred",memory,1000);continue;}
                if(NorthlightDiagnostics::enabled())logGeometryMemory("before-load",memory,generations.live(),workerMemoryExact);
                std::string error;
                const V center=r.geometryCenter;
                int tx=int(std::floor((17066.6666667-center.y)/533.3333333));
                int ty=int(std::floor((17066.6666667-center.x)/533.3333333));
                std::vector<std::string> tiles;
                for(int y=ty-1;y<=ty+1;++y)for(int x=tx-1;x<=tx+1;++x){
                    char name[512];std::snprintf(name,sizeof name,"%sworld-cache/%s/%d_%d.fg3",root.c_str(),r.map.c_str(),x,y);
                    FILE* f=std::fopen(name,"rb");if(f){std::fclose(f);tiles.emplace_back(name);}
                }
                bool localDeferred=false;NorthlightGeometryMemory::Sample localSample;
                NorthlightGI::AllocationAdmission localAdmission=[&](uint64_t bytes){
                    if(bytes<NorthlightGeometryMemory::MiB)return true;
                    auto sample=workerMemory(NorthlightGeometryMemory::buildBudget(bytes));
                    if(NorthlightGeometryMemory::admits(sample,NorthlightGeometryMemory::buildBudget(bytes)))return true;
                    localDeferred=true;localSample=sample;logGeometryMemory("local-geometry-allocation-deferred",sample,generations.live(),workerMemoryExact);return false;
                };
                auto replacement=std::make_shared<NorthlightGI::BVH>();
                if(!generations.track(replacement))throw std::runtime_error("Geometry generation admission invariant");
                if(tiles.empty()||!localGeometry.build(tiles,root+"world-cache/models",r.map,center-V(288,288,320),center+V(288,288,320),*replacement,error,localAdmission,quality.giFastBVH!=0)||!replacement->triangleCount()){
                    replacement.reset();
                    if(localDeferred){localGeometry.reset();stallBegin("local-geometry",localSample);deferBuild("local-geometry-retry",workerMemory(),1000);continue;}
                    result->message=error.empty()?"No cached geometry for "+r.map:error;publishError();continue;
                }
                const auto& localStats=localGeometry.stats();
                if(NorthlightDiagnostics::enabled())logf("WORLD local incremental: reads=%llu built=%llu reused=%llu reusedTriangles=%llu builtTriangles=%llu retainedMiB=%.2f flatMiB=%.2f bvhMiB=%.2f loadMs=%.3f pieceMs=%.3f assembleMs=%.3f bvhMs=%.3f totalMs=%.3f",
                    (unsigned long long)localStats.modelReads,(unsigned long long)localStats.pieceBuilds,(unsigned long long)localStats.pieceReuses,
                    (unsigned long long)localStats.reusedTriangles,(unsigned long long)localStats.builtTriangles,double(localStats.retainedBytes)/1048576,
                    double(localStats.flatBytes)/1048576,double(localStats.bvhBytes)/1048576,localStats.loadMs,localStats.pieceMs,localStats.assembleMs,localStats.bvhMs,localStats.totalMs);
                if(!currentGeometry()){++superseded;stallEnd("superseded");continue;}
                memory=workerMemory();if(NorthlightDiagnostics::enabled())logGeometryMemory("after-bvh",memory,generations.live(),workerMemoryExact);
                if(!NorthlightGeometryMemory::admits(memory,NorthlightGeometryMemory::buildBudget())){
                    replacement.reset();stallBegin("plan",memory);deferBuild("plan-deferred",memory,1000);continue;
                }
                // Reduced-reach retry re-enters here with the local BVH (`replacement`, still tracked in
                // `generations`) kept: it does not depend on the terrain stages.
                float reach=0;
                terrainStage:
                auto plan=std::make_shared<NorthlightWorldMesh::WorldMeshUploadPlan>();
                // GI stays local. Load only authored terrain over the complete
                // directional caster volume into a separate GPU shadow source.
                const double localPhaseMs=phaseElapsed();
                bool terrainDeferred=false;NorthlightGeometryMemory::Sample terrainSample;uint64_t terrainLargest=0;unsigned terrainChecks=0;
                NorthlightGI::AllocationAdmission terrainAdmission=[&](uint64_t bytes){
                    terrainLargest=std::max(terrainLargest,bytes);
                    // Small metadata cannot fragment a large block. Keep the
                    // process reserve at stage entry and check every large growth.
                    if(bytes<NorthlightGeometryMemory::MiB)return true;
                    ++terrainChecks;
                    auto sample=workerMemory(NorthlightGeometryMemory::buildBudget(bytes));
                    if(NorthlightGeometryMemory::admits(sample,NorthlightGeometryMemory::buildBudget(bytes)))return true;
                    terrainDeferred=true;terrainSample=sample;logGeometryMemory("shadow-terrain-allocation-deferred",sample,generations.live(),workerMemoryExact);
                    logf("WORLD terrain allocation requestMiB=%.2f requiredContiguousMiB=%.2f",double(bytes)/1048576,double(bytes+NorthlightGeometryMemory::ContiguousMargin)/1048576);
                    return false;
                };
                const float profileReach=shadowRanges.at(r.map,NorthlightRegionalFog::zoneAt(paletteRegion->region,center.x,center.y));
                const auto reachChoice=NorthlightTerrainReach::choose(reachState,profileReach,GetTickCount());
                reach=reachChoice.reach;
                if(reachChoice.restored)logf("WORLD shadow terrain reach restored to=%.0f",reach);
                // Memory refusal of an extended build: rebuild the terrain stages at the base reach now (margins unchanged).
                // The retry can pass: terrainAdmission re-samples address space on every call, and the refused extended
                // attempt's own terrain/plan/page allocations are freed (goto destroys them) before the reduced attempt.
                // The 745-767/63 MiB sample is taken mid-build after the extended terrain load; stage entry and the moment
                // after a torn-down attempt show ~965-984/754 and ~977 MiB available.
                auto reduceReach=[&]{
                    if(!NorthlightTerrainReach::memoryRefused(reachState,reach,GetTickCount()))return false;
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
                const bool terrainLoaded=NorthlightGI::loadInstancedScenes(shadowTiles,root+"world-cache/models",center-V(reach,reach,reach),center+V(reach,reach,reach),shadowTerrain,error,1,terrainAdmission,terrainFilter,terrainChunkFilter);
                const double terrainLoadMs=phaseElapsed();
                if(!terrainLoaded||!NorthlightShadowTerrain::build(replacement->scene(),shadowTerrain,center,*plan,error,terrainAdmission,reach)){
                    if(terrainDeferred){
                        stallBegin("shadow-terrain",terrainSample);
                        if(reduceReach())goto terrainStage;
                        shadowTerrain=NorthlightGI::WorldScene{};plan.reset();replacement.reset();deferBuild("shadow-terrain-retry",workerMemory(),1000);continue;}
                    result->message=error;publishError();continue;
                }
                const double shadowPlanMs=phaseElapsed();
                if(NorthlightDiagnostics::enabled())logf("WORLD terrain allocation largestMiB=%.2f checks=%u",double(terrainLargest)/1048576,terrainChecks);
                if(NorthlightDiagnostics::enabled())logf("WORLD shadow terrain: localTriangles=%zu shadowTriangles=%u fixedChunks=%zu reach=%.0f tiles=%zu",replacement->triangleCount(),plan->triangleCount,plan->fixedTerrainChunks.size(),reach,shadowTiles.size());
                if(!currentGeometry()){++superseded;stallEnd("superseded");continue;}
                shadowTerrain=NorthlightGI::WorldScene{}; // merged shadow plan already owns its data
                auto pages=std::make_shared<NorthlightWorldMeshPages::Plan>();
                if(!NorthlightWorldMeshPages::build(*plan,*pages,error,terrainAdmission)){
                    if(terrainDeferred){
                        stallBegin("mesh-page",terrainSample);
                        if(reduceReach())goto terrainStage;
                        pages.reset();plan.reset();replacement.reset();deferBuild("mesh-page-retry",workerMemory(),1000);continue;}
                    result->message=error;publishError();continue;
                }
                if(!NorthlightWorldMeshPages::seal(*plan,pages,error,terrainAdmission)){
                    if(terrainDeferred){
                        stallBegin("mesh-page-seal",terrainSample);
                        if(reduceReach())goto terrainStage;
                        pages.reset();plan.reset();replacement.reset();deferBuild("mesh-page-seal-retry",workerMemory(),1000);continue;}
                    result->message=error;publishError();continue;
                }
                if(NorthlightDiagnostics::enabled())logf("WORLD mesh pages=%zu batches=%zu originalBatches=%zu vertexMiB=%.2f indexMiB=%.2f maxResourceKiB=512",pages->pages.size(),pages->batches.size(),plan->batches.size(),double(pages->vertexBytes)/1048576,double(pages->indexBytes)/1048576);
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
                if(NorthlightDiagnostics::enabled())logf("GI local lights map=%s count=%zu (indirect only; authored direct light retained)",r.map.c_str(),built->lights.size());
                const auto& region=paletteRegion->region;
                built->fog=std::make_shared<NorthlightRegionalFog::Field>(NorthlightRegionalFog::buildField(replacement->scene(),region,center.x,center.y));
                if(NorthlightDiagnostics::enabled())logf("REGIONAL FOG map=%s tiles=%zu missing=%u groundCells=%u fogCells=%u airCells=%u indoorCells=%u citySurfaceCells=%u",r.map.c_str(),region.tiles.size(),region.missing,built->fog->groundCells,built->fog->fogCells,built->fog->airCells,built->fog->indoorCells,built->fog->citySurfaceCells);
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
            wake.notify_one();built.reset();
            stallEnd(memoryStall.reduced?"reduced":"published");
            builtMap=r.map;builtCenter=r.geometryCenter;builtValid=true;wanted=false;superseded=0;
          }
          }catch(const std::bad_alloc&){workerFaultCode.store(1);}
           catch(const std::exception&){workerFaultCode.store(2);}
           catch(...){workerFaultCode.store(3);}
        });
        std::shared_ptr<NorthlightGI::BVH> bvh;std::shared_ptr<NorthlightWorldMesh::WorldMeshUploadPlan> scenePlan;std::shared_ptr<const PreparedCommit> scenePrepared;V sceneCenter;std::shared_ptr<const NorthlightRegionalFog::Field> sceneFog;std::string sceneMap;uint64_t serial=0,sceneBuild=0;
        std::shared_ptr<Snapshot> previousLighting;
        NorthlightGI::ProbeCache probeCache;
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
        unsigned concurrentSolves=0;DWORD stallMs=0,stallStart=0;bool stalled=false;
        for(;;){
            Request r;std::unique_ptr<Built> adopted;
            {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return stopping||pending||builtGeometry;});if(stopping)return;r=request;pending=false;workerBusy=true;adopted=std::move(builtGeometry);}
            buildWake.notify_one(); /* the builder follows the same request stream */
            struct WorkerIdle {std::atomic<bool>& busy;~WorkerIdle(){busy=false;}} idle{workerBusy};
            auto result=std::make_shared<Snapshot>();result->map=r.map;result->center=r.camera;
            DWORD started=GetTickCount();result->requestId=r.id;result->requestedAt=r.queuedAt;result->queueMs=started-r.queuedAt;
            bool concurrent=false;
            if(adopted){
                bvh=std::move(adopted->bvh);scenePlan=std::move(adopted->plan);scenePrepared=std::move(adopted->prepared);sceneFog=std::move(adopted->fog);
                sceneLights=std::move(adopted->lights);rawSceneLights=std::move(adopted->rawLights);sceneMap=std::move(adopted->map);sceneCenter=adopted->center;
                sceneBuild=adopted->id;superseded+=adopted->superseded;if(stalled){stallMs+=GetTickCount()-stallStart;stalled=false;}
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
                    probeCache.clear();probeCacheGeometry.reset();
                    dynamicProbes.reset(actors?&actors->scene():nullptr);
                    scenePlan.reset();scenePrepared.reset();sceneFog.reset();bvh.reset();
                    if(!stalled){stalled=true;stallStart=started;}
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
            if(!quality.gi)continue; /* GI=0: geometry for shadows only; no actor, probe or atlas work */
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
                probeCache.clear();probeCacheGeometry=bvh;probeCacheLight=r.light;
                preparedLight=NorthlightGI::prepareLighting(probeCacheLight);
                publicationCadence.reset();++lightingGeneration;
                // Keep already displayed same-map GI until replacement probes
                // are ready. This is display fallback only, never solver input.
                displayFallback.clear();
                {std::lock_guard<std::mutex> lock(mutex);
                 if(published&&published->map==r.map)displayFallback=published->atlas;
                 else if(previousLighting&&previousLighting->map==r.map)displayFallback=previousLighting->atlas;}
                dynamicProbes.reset(actors?&actors->scene():nullptr);
            }
            // Publish new geometry immediately. Shadow rendering must not wait
            // for the probe solve; retain prior GI until its replacement arrives.
            {std::lock_guard<std::mutex> lock(mutex);
             if(stopping)return;if(request.id!=r.id){++superseded;continue;}
             if(!published||published->bvh!=bvh){
                auto geometry=std::make_shared<Snapshot>();
                if(published&&published->map==r.map)*geometry=*published;
                else if(previousLighting&&previousLighting->map==r.map)*geometry=*previousLighting;
                geometry->bvh=bvh;geometry->meshPlan=scenePlan;geometry->prepared=scenePrepared;geometry->retirementBytes=bvh->retainedBytes()+scenePlan->cpuBytes()+sizeof(Snapshot);geometry->map=r.map;geometry->center=sceneCenter;geometry->fogField=sceneFog;geometry->localLights=rawSceneLights;
                published=geometry;previousLighting.reset();
             }}
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
                        [&](NorthlightGI::ProbeGridKey k,V q){return NorthlightGI::solveProbePrepared(*bvh,q,preparedLight,quality.giRays,NorthlightGI::probeSeed(k));},prefetched);
                }
                V p=NorthlightGI::probeWindowPosition(result->origin,order[cursor]);
                NorthlightGI::ProbeGridKey key;
                if(!NorthlightGI::probeGridKey(p,key))throw std::runtime_error("Invalid world probe coordinate");
                NorthlightGI::Probe probe;
                if(probeCache.get(key,probe))++result->reusedProbes;
                else{probe=prefetchedMask&(1u<<(cursor%8))?prefetched[cursor%8]:NorthlightGI::solveProbePrepared(*bvh,p,preparedLight,quality.giRays,NorthlightGI::probeSeed(key));probeCache.put(key,probe);++result->solvedProbes;publicationCadence.solved();}
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
                [&](){std::lock_guard<std::mutex> lock(mutex);return stopping||request.id!=r.id;},
                result->dynamicReused,result->dynamicSolved)){
                noteSuperseded();result->atlas.clear();publishProgress();continue;
            }
            result->solveMs=GetTickCount()-solveStart;result->superseded=superseded;result->retargeted=retargeted;
            result->movingStats=movingStats;result->pathRecordBytes=dynamicProbes.pathBytes();
            bool publishObsolete=false;
            {std::lock_guard<std::mutex> lock(mutex);if(stopping)return;
             if(request.id!=r.id)publishObsolete=true;
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
    void clearMesh(){staticCasters.settle();dropStaticDirtyJobs();vertices=nullptr;indices=nullptr;for(auto& m:materials)drop(m);materials.clear();uploadedTextureBytes=0;batches.clear();terrainBatchList.clear();shadowTerrainList.clear();terrainBatchListGeneration=UINT64_MAX;uploadedAlphaCutoffs.clear();uploadedLocalShadowRecords.reset();uploaded.reset();}
    bool target(UINT w,UINT h,D3DFORMAT fmt,IDirect3DTexture9** t,IDirect3DSurface9** s){return check(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,fmt,D3DPOOL_DEFAULT,t,nullptr),"world render texture")&&check((*t)->GetSurfaceLevel(0,s),"world render surface");}
    bool resources(UINT w,UINT h,D3DFORMAT fmt){
        if(width==w&&height==h&&color)return true;
        releaseGPU();width=w;height=h;
        if(!check(d->CreatePixelShader(kWorldGIShader,&giPS),"world GI shader")||!check(d->CreatePixelShader(kWorldLightingShader,&lightingPS),"world lighting shader")||!check(d->CreatePixelShader(kWorldFogShader,&fogPS),"volume shader")||!check(d->CreatePixelShader(kFogBlurShader,&fogBlurPS),"volume blur shader")||!check(d->CreatePixelShader(kLocalDirectShader,&localDirectPS),"local direct light shader")||!check(d->CreatePixelShader(kRemovalSmoothShader,&removalPS),"removal smoothing shader")||!check(d->CreatePixelShader(kTemporalLightShader,&temporalPS),"temporal light shader")||!check(d->CreatePixelShader(kLocalFogShader,&localFogPS),"local fog glow shader")||!check(d->CreatePixelShader(kWorldNormalsShader,&normalsPS),"world normals shader")||!check(d->CreatePixelShader(kSourceVisibilityPSShader,&sourceVisPS),"source visibility shader")||!check(d->CreatePixelShader(kWorldCompositeShader,&finalPS),"world composite shader")||!check(d->CreatePixelShader(kShadowPSShader,&shadowPS),"shadow shader")||!check(d->CreatePixelShader(kShadowReplayPSShader,&replayPS),"replay shader")||!check(d->CreateVertexShader(kShadowVSShader,&shadowVS),"shadow vertex shader")||!check(d->CreateVertexShader(kShadowCacheVSShader,&cachedShadowVS),"cached shadow vertex shader")||!check(d->CreatePixelShader(kStaticCasterPSShader,&cachedShadowPS),"cached shadow depth shader"))return false;
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
        if(!check(d->CreatePixelShader(kShadowUnionShader,&unionPS),"shadow union shader"))return false;
        invalidateShadowCache();
        {const UINT n=NorthlightGI::probeLayout().atlas;for(int i=0;i<5;++i)if(!check(d->CreateTexture(n*n,i==3?n*6:n,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&probe[i],nullptr),"GI probe texture"))return false;if(!probePrev&&FAILED(d->CreateTexture(n*n*3,n,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&probePrev,nullptr))){probePrev=nullptr;static bool logged=false;if(!logged){logged=true;logf("GI probe blend texture unavailable; same-key probe re-publication switches instantly");}}}
        // AO 1, bloom 0 for TemporalLight/WorldComposite on frames the proxy composites itself.
        if(!check(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&neutralAO,nullptr),"neutral AO texture"))return false;
        {D3DLOCKED_RECT lock={};if(!check(neutralAO->LockRect(0,&lock,nullptr,0),"neutral AO lock"))return false;*static_cast<DWORD*>(lock.pBits)=0xff000000u;neutralAO->UnlockRect(0);}
        if(!neutralZero){if(SUCCEEDED(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&neutralZero,nullptr))){ /* 0.3.202 (rain): optional; without it the composite gets no rain mask */
            D3DLOCKED_RECT lock={};if(SUCCEEDED(neutralZero->LockRect(0,&lock,nullptr,0))){*static_cast<DWORD*>(lock.pBits)=0u;neutralZero->UnlockRect(0);}else drop(neutralZero);}}
        if(!target(w/2,h/2,D3DFMT_A16B16G16R16F,&baselineLight,&baselineSurface)||!target(w/2,h/2,D3DFMT_A16B16G16R16F,&light,&lightSurface)||!target(w/2,h/2,D3DFMT_A16B16G16R16F,&smoothLight,&smoothSurface)||!target(w/2,h/2,D3DFMT_A16B16G16R16F,&fog,&fogSurface)||!target(w/2,h/2,D3DFMT_A16B16G16R16F,&fogBlurred,&fogBlurredSurface)||!target(w/2,h/2,D3DFMT_A16B16G16R16F,&fogHistory,&fogHistorySurface)||!target(w/2,h/2,D3DFMT_A16B16G16R16F,&normalBuffer,&normalSurface)||!target(1,1,D3DFMT_A16B16G16R16F,&sourceVis[0][0],&sourceVisSurface[0][0])||!target(1,1,D3DFMT_A16B16G16R16F,&sourceVis[0][1],&sourceVisSurface[0][1])||!target(1,1,D3DFMT_A16B16G16R16F,&sourceVis[1][0],&sourceVisSurface[1][0])||!target(1,1,D3DFMT_A16B16G16R16F,&sourceVis[1][1],&sourceVisSurface[1][1])||!target(w/2,h/2,D3DFMT_A16B16G16R16F,&temporalLight[0],&temporalLightSurface[0])||!target(w/2,h/2,D3DFMT_A16B16G16R16F,&temporalLight[1],&temporalLightSurface[1])||!target(w/2,h/2,D3DFMT_R32F,&temporalDepth[0],&temporalDepthSurface[0])||!target(w/2,h/2,D3DFMT_R32F,&temporalDepth[1],&temporalDepthSurface[1])||!target(w,h,fmt,&color,&colorSurface))return false;
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
        logf("WORLD pending mesh released orphan=%s site=%s map=%s",!active?"inactive":pendingMesh->map!=active->map?"map":"replaced",site,pendingMesh->map.c_str());
        retirePendingCpu();pendingMesh.reset();meshRetry.clear();
    }
    bool streamingFailure(HRESULT hr,const char* stage,const char* detail=""){
        const auto* plan=pendingMesh?pendingMesh->plan.get():(active?active->meshPlan.get():nullptr);
        if(streamingReports++<12)logf("WORLD streaming retry: stage=%s hr=%08lx detail=%s vertexBytes=%llu indexBytes=%llu vertices=%u triangles=%u sourceMatch=%d oldMesh=%d retryMs=1000",stage,(unsigned long)hr,detail,
            (unsigned long long)(plan?plan->vertexBytes:0),(unsigned long long)(plan?plan->indexBytes:0),plan?plan->vertexCount:0,plan?plan->triangleCount:0,
            plan&&plan->pagesSealed,vertices&&indices&&active&&uploadedMap==active->map);
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
            uint64_t largestTexture=0;for(const auto& material:active->meshPlan->materials)largestTexture=std::max(largestTexture,uint64_t(material.bgra.size()));
            const auto& plan=*active->meshPlan;
            const char* invalid=NorthlightWorldMeshPages::validateSealed(plan);
            if(invalid||!plan.pages||plan.pages->pages.empty())return streamingFailure(D3DERR_INVALIDCALL,"validate pages",invalid?invalid:"missing pages");
            auto budget=NorthlightGeometryMemory::uploadBudget(plan.pages->vertexBytes,plan.pages->indexBytes,plan.textureBytes,largestTexture);
            budget.largest=2*std::max<uint64_t>(NorthlightWorldMeshPages::PageVertexByteLimit,largestTexture)+NorthlightGeometryMemory::ChunkMargin;
            NorthlightGeometryMemory::Sample memory;bool exact=true;
            {auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Admission);memory=geometryAdmission(budget,&exact);}
            if(NorthlightDiagnostics::enabled())logGeometryMemory("before-upload",memory,0,exact);
            if(!NorthlightGeometryMemory::admits(memory,budget))return streamingFailure(E_OUTOFMEMORY,"upload-admission","address-space reserve or contiguous block");
            pendingMesh=std::make_unique<PendingMesh>();pendingMesh->bvh=active->bvh;
            pendingMesh->plan=active->meshPlan;pendingMesh->prepared=active->prepared;pendingMesh->map=active->map;
            auto& next=*pendingMesh;next.textures.reserve(next.plan->materials.size());
            auto& pool=meshPool[stagingSlot()];if(pool.size()<plan.pages->pages.size())pool.resize(plan.pages->pages.size());
            std::vector<NorthlightWorldMeshPages::PageBytes> sizes;sizes.reserve(plan.pages->pages.size());
            for(const auto& page:plan.pages->pages)sizes.push_back({page.vertices.size()*sizeof(NorthlightGI::WorldVertex),page.indices.size()*sizeof(uint32_t)});
            if(!next.upload.begin(std::move(sizes)))return streamingFailure(D3DERR_INVALIDCALL,"page sizes");
        }
        if(pendingMesh){frameStaging=true; /* 0.3.176 (D3) */
            auto& next=*pendingMesh;auto& plan=*next.plan;
            ++next.frames;
            // Each bounded driver operation yields back to the shared deadline.
            struct PageBudget {
                NorthlightStreaming::Budget& parent;
                bool available()const{return parent.available()&&parent.elapsedMs()<.5;}
                size_t chunk(size_t n)const{return available()?parent.chunk(n):0;}
                void consume(size_t n){parent.consume(n);}
            } pageBudget{streamBudget};
            NorthlightGeometryMemory::Sample pageMemory;bool sampled=false;
            auto perform=[&](NorthlightWorldMeshPages::Step step,size_t pageIndex,size_t offset,size_t bytes){
                using S=NorthlightWorldMeshPages::Step;
                auto& page=meshPool[stagingSlot()][pageIndex];const auto& cpu=plan.pages->pages[pageIndex];
                if(step==S::CreateVertices||step==S::CreateIndices){
                    const bool vb=step==S::CreateVertices;
                    const bool reuse=vb?page.vb&&page.vbCapacity>=bytes:page.ib&&page.ibCapacity>=bytes;
                    if(reuse)return true;
                    if(!sampled){
                        auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Admission);
                        // Full address-space admission already ran at generation
                        // start. Bounded <=512 KiB growth needs a fresh aggregate
                        // reserve check, not another VirtualQuery walk per page.
                        MEMORYSTATUSEX status={};status.dwLength=sizeof status;
                        pageMemory.valid=GlobalMemoryStatusEx(&status)!=FALSE;
                        pageMemory.available=status.ullAvailVirtual;sampled=true;
                    }
                    auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Buffers);
                    return acquireMeshPage(pageIndex,vb,UINT(bytes),pageMemory);
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
            while(canStage()&&budget&&next.material<plan.materials.size()&&count<16){
                const auto& m=plan.materials[next.material];
                const bool white=m.width==1&&m.height==1&&m.bgra.size()==4&&
                    m.bgra[0]==255&&m.bgra[1]==255&&m.bgra[2]==255&&m.bgra[3]==255;
                if(white&&next.white){next.white->AddRef();next.textures.push_back(next.white);++next.material;++count;continue;}
                if(!next.partialTexture){
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
            if(streamBudget.available()&&next.upload.status()==NorthlightWorldMeshPages::Result::Ready&&next.material==plan.materials.size()){
                auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Commit);
                // Allocate small commit metadata before releasing the previous GPU set.
                std::vector<float> cutoffs;cutoffs.reserve(plan.materials.size());
                for(const auto& material:plan.materials)cutoffs.push_back(material.alphaCutoff);
                std::vector<Batch> committedBatches=plan.pages->batches;
                std::string committedMap=next.map;
                const bool prepared=next.prepared&&next.prepared->plan==next.plan.get()&&next.prepared->owners&&next.prepared->fixed;preparedOwnerCommits+=prepared;
                auto committedFixed=prepared?next.prepared->fixed:std::make_shared<const FixedChunks>(plan.fixedTerrainChunks);
                auto committedOwners=prepared?next.prepared->owners:StaticShadow::makeOwners(ownerPlacements(plan));
                // Keep the previous complete geometry drawable until publication fits.
                if(uploadedTextureBytes<=NorthlightStreaming::DeferredRelease<IDirect3DTexture9>::MaxBytes&&
                   !retiredMaterials.enqueue(materials,uploadedTextureBytes))return vertices&&indices&&uploadedMap==active->map;
                clearMesh();activeMesh=stagingSlot();vertices=meshPool[activeMesh][0].vb;indices=meshPool[activeMesh][0].ib;
                materials=std::move(next.textures);uploadedTextureBytes=size_t(plan.textureBytes);vertexCount=plan.vertexCount;
                uploadedLocalShadowRecords=plan.localShadowRecords;
                batches=std::move(committedBatches);uploadedAlphaCutoffs=std::move(cutoffs);
                // Replaced shared sets may be last owners (~7000 + ~3000 nodes).
                {auto& reaper=NorthlightStreaming::cpuRetirement();
                 std::swap(fixedTerrain,committedFixed);std::swap(uploadedStaticOwners,committedOwners);fixedTerrainBits.assign(fixedTerrain.get());
                 if(committedFixed)retirementBacklog.retire(reaper,committedFixed,committedFixed->size()*48);
                 if(committedOwners)retirementBacklog.retire(reaper,committedOwners,committedOwners->placements*(sizeof(StaticShadow::Placement)+64));}
                uploaded=next.bvh;uploadedMap=std::move(committedMap);
                ++meshGeneration;rebuildTerrainLists();
                if(NorthlightDiagnostics::enabled())logf("WORLD staged mesh committed: vertices=%u triangles=%zu materials=%zu textureUploads=%u stagingFrames=%u stagingMs=%lu",vertexCount,size_t(plan.triangleCount),materials.size(),next.textureUploads,next.frames,(unsigned long)(GetTickCount()-next.started));
                retirePendingCpu();pendingMesh.reset();meshRetry.clear();
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
                for(unsigned z=0;z<n;++z)for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){
                    const auto& entry=active->atlas[x+y*n+z*n*n];const auto& p=entry.probe;
                    float* dst=(float*)((char*)lock.pBits+y*lock.Pitch)+(x+z*n)*4;
                    if(channel<3){for(int k=0;k<4;++k)dst[k]=channel==0?p.sh[k].x:channel==1?p.sh[k].y:p.sh[k].z;}
                    else if(channel==3)for(unsigned axis=0;axis<6;++axis){float* moment=(float*)((char*)lock.pBits+(y+axis*n)*lock.Pitch)+(x+z*n)*4;moment[0]=p.moments[axis].mean;moment[1]=p.moments[axis].meanSquare;moment[2]=entry.occupied&&p.valid?1.f:0.f;moment[3]=probePrev?probeBlend.start(x+y*n+z*n*n):NorthlightProbeBlend::None;}
                    else{dst[0]=float(entry.key.x);dst[1]=float(entry.key.y);dst[2]=float(entry.key.z);dst[3]=probeActivation.update(x+y*n+z*n*n,entry,activationNow);}
                }
                probe[channel]->UnlockRect(0);
            }
            if(probePrev){D3DLOCKED_RECT lock; /* 0.3.197: what was on screen before each slot's latest SH, R|G|B in three horizontal thirds */
                if(!check(probePrev->LockRect(0,&lock,nullptr,0),"probe blend upload"))return false;
                for(unsigned z=0;z<n;++z)for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x){const float* previous=probeBlend.previous(x+y*n+z*n*n);float* row=(float*)((char*)lock.pBits+y*lock.Pitch)+(x+z*n)*4;for(int c=0;c<3;++c)for(int k=0;k<4;++k)row[c*n*n*4+k]=previous[c*4+k];}
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
        NorthlightDiagnostics::configure(quality.diagnostics!=0);
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
    explicit WorldRenderer(IDirect3DDevice9* device):d(device),stateBlocks(device){terrainBoundsCache.setIdentityProvider(&bufferIdentity);terrainBoundsCache.setVersionProvider(&NorthlightTrackedBuffers::version);terrainBoundsCache.setDeclarationCache(&declarationCache);replaySnapshots.setDeclarationProvider(&declarationCache,[](void* c,IDirect3DVertexDeclaration9* decl,const D3DVERTEXELEMENT9*& e,UINT& n){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->get(decl,e,n);});replaySnapshots.setLayoutProvider(&declarationCache,[](void* c,IDirect3DVertexDeclaration9* decl,UINT* extent){return static_cast<NorthlightVertexDeclarations::Cache*>(c)->captureLayout(decl,extent);});replaySnapshots.setIdentityProvider(&bufferIdentity);replaySnapshots.setVersionProvider(&NorthlightTrackedBuffers::version);replaySnapshots.setMetadataProvider(&NorthlightTrackedBuffers::captureMetadata);QueryPerformanceFrequency(&captureFrequency);char path[MAX_PATH*3];WideCharToMultiByte(CP_UTF8,0,rootPath,-1,path,sizeof path,nullptr,nullptr);root=path;std::string paletteError;
        loadQuality();effects.gi=NorthlightQuality::giPass(quality,effects.gi);
        staticPrebuild.steps=float(quality.shadowDirectionSteps); /* prebuild targets the same direction lattice */
        minSkinnedShadowTriangles=quality.minSkinnedTriangles;captureBudgetMiB=quality.captureBudgetMiB;actorShadowBudgetMiB=quality.actorShadowBudgetMiB;shadowFateDiagnostics=NorthlightQuality::shadowFate(quality);
        replaySnapshots.configureBudget(size_t(captureBudgetMiB)*1048576,size_t(captureBudgetMiB)*524288);setNearReserve();
        logf("SHADOW experiment minSkinnedTriangles=%u captureBudgetMiB=%u actorShadowBudgetMiB=%u actorShadowRadius=%u distance=sampled-current-vertex unknown=first GI=preserved-at-capture-budget",minSkinnedShadowTriangles,captureBudgetMiB,actorShadowBudgetMiB,quality.actorShadowRadius);
        (void)NorthlightStreaming::cpuRetirement();
        bool paletteLoaded=NorthlightCelestialProfiles::load(root+"celestial-profiles.ini",celestialProfiles,paletteError);
        logf("CELESTIAL profiles loaded=%d zones=%zu %s",paletteLoaded,celestialProfiles.zones.size(),paletteError.c_str());
        std::string rangeError;bool rangesLoaded=NorthlightRegionalShadow::load(root+"shadow-range-profiles.ini",shadowRanges,rangeError);
        logf("SHADOW regional terrain loaded=%d zones=%zu %s",rangesLoaded,shadowRanges.zones.size(),rangeError.c_str());
        staticStream=std::make_unique<StaticShadow::Streamer>(root+"world-cache");
        staticCasters.setAdmission([this](size_t bytes){return admitStaticAllocation(bytes);});
        worker=std::thread([this]{work();});}
    ~WorldRenderer(){prepareWorker.join(); /* 0.3.179: first: an abandoned worker may still be inside a record */
        logSnapshotAb(); /* 0.3.181: the SNAPSHOT ab window at device destroy */
        {std::lock_guard<std::mutex> lock(mutex);stopping=true;}wake.notify_one();if(worker.joinable())worker.join();releaseGPU();for(auto& p:captureShaders)drop(p.second.replacement);for(auto& p:terrainShadowShaders)drop(p.second);}
    void releaseGPU(){replayBoundsAbandon();releaseReplayProbe();staticCasters.settle();rigidMemoryClear();prepareQuiesce();neutralShadowMaps=false;prepareCaches->sampled.clear();prepareCaches->bones.clear();prepareCachesStale=false;if(replays.empty()&&heldShadowReplays.empty())prepareFrameRelease(); /* 0.3.179: no replay left to point at them */actorShadowHistory.clear();actorShadowOriginValid=false;replayBoundsMetadata.clear();replayBoundsCache.clear();declarationCache.clear();uploadedStaticOwners.reset();staticOwnerGeneration=UINT64_MAX;staticCasters.reset();staticMatcher.clear();staticScene.reset();staticRetryTick=0;stateBlocks.clear();uploadedTerrain.clear();liveTerrainIndexCount=liveDirectionalIndexCount=0;fixedTerrain.reset();fixedTerrainBits.reset();liveTerrainGeneration=0;drop(regionalFogTexture);drop(neutralAO);drop(neutralZero);rainMask=nullptr;uploadedFogField.reset();releasePointGPU();probeActivation.reset();probeBlend.reset();drop(baselineSurface);drop(baselineLight);releaseReplayGPU();liveTerrainGPU.clear();{auto& queries=fence();NorthlightDynamicRing::reset(liveIndexRing,0,queries);queries.drop();}drop(liveIndicesGPU);liveIndexBytes=0;liveIndexBase=0;pendingMesh.reset();clearMesh();retiredMaterials.clear();releaseMeshPool();uploadedMap.clear();for(auto& t:shadow)drop(t);for(auto& s:shadowSurface)drop(s);for(auto& t:shadowCache)drop(t);for(auto& s:shadowCacheSurface)drop(s);drop(shadowCacheDepth);drop(shadowVerifySurface);drop(shadowVerify);for(auto& r:shadowVerifyRead)drop(r);drop(shadowScratch);drop(shadowScratchSurface);drop(unionPS);invalidateShadowCache();for(auto& t:probe)drop(t);drop(probePrev);drop(shadowDepth);drop(lightSurface);drop(smoothSurface);drop(fogSurface);drop(fogBlurredSurface);drop(fogHistorySurface);drop(fogHistory);drop(fogTemporalPS);fogHistoryValid=false;drop(colorSurface);drop(light);drop(smoothLight);drop(fog);drop(fogBlurred);drop(color);drop(lightingPS);drop(giPS);drop(fogPS);drop(fogBlurPS);drop(fogCloudsPS);drop(cloudNoise);drop(localDirectPS);drop(removalPS);drop(temporalPS);drop(localFogPS);drop(normalsPS);drop(normalBuffer);drop(normalSurface);drop(sourceVisPS);for(int a=0;a<2;++a)for(int b=0;b<2;++b){drop(sourceVis[a][b]);drop(sourceVisSurface[a][b]);}sourceVisValid=false;for(int i=0;i<2;++i){drop(temporalLight[i]);drop(temporalLightSurface[i]);drop(temporalDepth[i]);drop(temporalDepthSurface[i]);}temporalValid=false;drop(finalPS);drop(shadowPS);drop(replayPS);drop(shadowVS);drop(cachedShadowVS);drop(cachedShadowPS);drop(cachedFastPS);drop(cachedOpaqueFastPS);drop(cachedOpaquePS);drop(shadowDecl);width=height=0;uploadedSerial=0;}
    // Explicit enable/retry only, called after the wrapper's clearFrame(). This
    // never calls endFrame(), so packet capture and cleanup run exactly once.
    void recover(){meshRetry.clear();if(!failed)return;releaseGPU();failed=false;valid=false;streamingReports=0;logf("WORLD explicit recovery requested");}
    void releaseStateCache(){stateBlocks.clear();invalidateShadowCache();}
    void setEffects(const NorthlightEffectSwitches::Settings& requested){
        auto next=requested;next.gi=NorthlightQuality::giPass(quality,next.gi); /* GI=0: F8 cannot enable a GI that is never solved */
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
    void reset(){shadowsComposited=false;pivotValid=false;pivotDistance=12.f;pivotCorrection.reset();pivotSelfCaptured=false;cascadeAnchor.reset();endFrame();replaySnapshots.clearIndexCache();actorJobComplete.reset();actorJobSerial_=0;actorSceneMap_.clear();lastActorCapture=0;terrainBoundsCache.clearPersistent();previousCacheHits=0;freeReplays.clear();pooledSnapshotBytes=0;valid=false;failed=false;releaseGPU();}
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
    }
    void endFrame(bool retainPool=true){
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
            logf("WORLD CPU capture terrain=%.3fms replay=%.3fms terrainCalls=%u terrainUP=%u replayCandidates=%u unknownCalls=%u acceptedReplay=%zu captureSkipped=%u",terrainCaptureTicks*ms,replayCaptureTicks*ms,terrainCaptureCalls,terrainUPCalls,replayCaptureCalls,unknownCaptureCalls,replays.size(),unsigned(captureMode==CaptureSkipped));
        }
        if(captureSampled)logf("WORLD capture cache hits=%llu frameHits=%llu entries=%zu bytes=%llu constantsReadBytes=%zu constantReadCalls=%zu sm1Draws=%u relativeDraws=%u",(unsigned long long)terrainBoundsCache.persistentHits(),(unsigned long long)(terrainBoundsCache.persistentHits()-previousCacheHits),terrainBoundsCache.persistentEntries(),(unsigned long long)terrainBoundsCache.persistentBytes(),capturedConstantBytes,capturedConstantCalls,capturedSM1Draws,capturedRelativeDraws);
        if(captureSampled)logf("WORLD optimized caches terrainTrackedHits=%llu terrainAvoidedBytes=%llu boundsHits=%llu boundsMisses=%llu boundsAvoidedVertices=%llu boundsLookupMs=%.3f staticMaintenanceVisits=%llu staticPublications=%llu",
            (unsigned long long)terrainBoundsCache.trackedHits(),(unsigned long long)terrainBoundsCache.avoidedReadBytes(),
            (unsigned long long)replayBoundsCache.hits(),(unsigned long long)replayBoundsCache.misses(),(unsigned long long)replayBoundsCache.avoidedVertices(),replayBoundsCache.lookupMilliseconds(),
            (unsigned long long)staticCasters.stats().maintenanceVisits,(unsigned long long)staticCasters.stats().publications);
        if(captureSampled){const auto& t=terrainBoundsCache.maintenance();
            logf("TERRAIN content cache entryLimit=%zu byteLimit=33554432 entries=%zu bytes=%llu lookupMisses=%u evictions=%u countPressure=%u bytePressure=%u readBytes=%llu",
                terrainBoundsCache.persistentEntryLimit(),terrainBoundsCache.persistentEntries(),(unsigned long long)terrainBoundsCache.persistentBytes(),
                t.lookupMisses,t.evictions,t.countPressure,t.bytePressure,(unsigned long long)terrainBoundsCache.bytesRead());}
        if(captureSampled)logf("MODEL index cache hits=%u misses=%u entries=%u bytes=%zu snapshotCacheHits=%u snapshotCacheMisses=%u snapshotCacheEntries=%zu snapshotCacheBytes=%zu revalidated=%u revalidationMismatches=%u trackedHits=%u avoidedReadBytes=%zu",replaySnapshots.indexCacheHits(),replaySnapshots.indexCacheMisses(),replaySnapshots.indexCacheEntries(),replaySnapshots.indexCacheBytes(),replaySnapshots.snapshotCacheHits(),replaySnapshots.snapshotCacheMisses(),replaySnapshots.snapshotCacheEntries(),replaySnapshots.snapshotCacheBytes(),replaySnapshots.snapshotRevalidated(),replaySnapshots.snapshotRevalidationMismatches(),replaySnapshots.trackedHits(),replaySnapshots.avoidedReadBytes());
        if(captureSampled){const auto& m=replaySnapshots.maintenance();
            logf("MODEL snapshot maintenance snapshotEvictions=%u snapshotEvictedBytes=%zu snapshotMs=%.3f indexEvictions=%u indexEvictedBytes=%zu indexMs=%.3f missEvicted=%u missRevision=%u missUnknown=%u missCollision=%u missRevalidated=%u historySlots=2048",
                m.snapshotEvictions,m.snapshotEvictedBytes,m.snapshotMs,m.indexEvictions,m.indexEvictedBytes,m.indexMs,
                m.missEvicted,m.missRevision,m.missUnknown,m.missCollision,replaySnapshots.snapshotRevalidationMismatches());}
        if(captureSampled)logf("MODEL capture metadata batches=%u knownBuffers=%u fallbackBuffers=%u fastSnapshotHits=%u",replaySnapshots.metadataBatches(),replaySnapshots.metadataHits(),replaySnapshots.metadataFallbacks(),replaySnapshots.fastCacheHits());
        if(captureSampled)logf("MODEL frame capture skinnedCandidates=%u accepted=%u blendRejected=%u projectionRejected=%u budgetRejected=%u snapshotRejected=%u acceptedSkinnedBytes=%zu acceptedOtherBytes=%zu rejectedReadBytes=%zu totalReadBytes=%zu compactedDraws=%u spanVertices=%zu uniqueVertices=%zu savedVertexBytes=%zu nearAdmitted=%u nearBytes=%zu nearRefused=%u nearSelf=%d nearReserve=%zu",skinnedCandidates,skinnedAccepted,skinnedBlendRejected,skinnedProjectionRejected,skinnedBudgetRejected,skinnedSnapshotRejected,acceptedSkinnedBytes,acceptedOtherBytes,captureRejectedBytes,replaySnapshots.bytesRead(),replaySnapshots.compactedDraws(),replaySnapshots.sourceSpanVertices(),replaySnapshots.uniqueVertices(),replaySnapshots.savedVertexBytes(),nearAdmitted,nearBytes,nearRefused,int(nearAnchorReady&&nearAnchor.self),replaySnapshots.nearReserve());
        if(captureSampled)logf("MODEL frame capture other blendRejected=%u projectionRejected=%u budgetRejected=%u",otherBlendRejected,otherProjectionRejected,otherBudgetRejected);
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
        skinnedCandidates=skinnedBlendRejected=skinnedProjectionRejected=skinnedBudgetRejected=skinnedSnapshotRejected=skinnedAccepted=0;otherBlendRejected=otherBudgetRejected=otherProjectionRejected=0;
        captureRejectedBytes=acceptedSkinnedBytes=acceptedOtherBytes=0;nearAdmitted=nearRefused=0;nearBytes=0;nearAnchorReady=false;
        previousCacheHits=terrainBoundsCache.persistentHits();capturedConstantBytes=capturedConstantCalls=0;capturedSM1Draws=capturedRelativeDraws=0;
        terrainCaptureTicks=replayCaptureTicks=0;terrainCaptureCalls=terrainUPCalls=replayCaptureCalls=unknownCaptureCalls=0;captureSampled=false;
        valid=false;traceContext=0;shadowFrameReady=false;legacyFog=NorthlightLegacyFog::Constants{};
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
    }
    bool ready()const{return valid&&active&&active->bvh&&!failed&&!workerFault();}
    const char* lastSkipReason()const{return skipReason;} /* 0.3.169: valid after render() returned false */
    bool captureSkippedLastFrame()const{return lastCaptureSkipped;} /* for the sampled CPU profile, logged after endFrame */
    unsigned capturePhaseReadsLastFrame()const{return lastCapturePhaseReads;} /* 0.3.150: clock reads of the capture-phase subset (inside the capture timers), likewise */
    bool hasContext()const{return valid&&!failed&&!workerFault();}
    unsigned frameTraceContext()const{return traceContext;} /* 0.3.200 (frame trace) */
    bool actorShadowsEnabled()const{return quality.actorShadows!=0;}
    bool commandStream()const{return quality.commandStream!=0;} /* 0.3.192 (CS): the replay-thread stream was requested; creation-time key, see stream_hooks.h */
    unsigned blobShadowStrength()const{return quality.blobShadowStrength;} /* 0.3.193: read once at device creation */
    // 0.3.198 (rain): the smoothed weather state, set once per frame at the frame boundary (Device::finishFrameImpl); no effect reads it yet.
    NorthlightWeather::State weatherState{};
    void setWeather(const NorthlightWeather::State& s){weatherState=s;}
    void setRainMask(IDirect3DTexture9* t){rainMask=t;} /* 0.3.202 (rain): non-owning, for the next render() only */
    const NorthlightWeather::State& weather()const{return weatherState;}
    // 0.3.198 (rain): the settings (Weather 0/1, RainFog 0..2) and the per-frame scalars derived from them and weather():
    // render() and the celestial renderers read this one place; all zero (identity) with Weather=0 or no weather.
    unsigned weatherSetting()const{return quality.weather;}
    unsigned rainFogSetting()const{return quality.rainFog;}
    // 0.3.200 (gpu budget): GpuBudgetMs (0 = off) and the controller's level for the next frames; off forces level 0 (the full path, unchanged).
    unsigned gpuBudgetMs()const{return quality.gpuBudgetMs;}
    void setGpuBudgetLevel(unsigned level){gpuBudgetLevel=quality.gpuBudgetMs?std::min(level,NorthlightGpuBudget::MaxLevel):0u;}
    bool rainBlendSetting()const{return quality.weather!=0;} /* 0.3.199 (rain): the draw hook's alpha-blended rain streaks and rain-only mist skip, with Weather=1 */
    NorthlightWeatherEffects::Frame weatherEffects()const{return NorthlightWeatherEffects::derive(weatherState,quality.weather,quality.rainFog);}
    bool contactAO()const{return quality.contactAO!=0;} /* 0.3.201 (task 18): read once at device creation */
    bool frameDrawGates()const{return quality.frameDrawGates!=0;} /* 0.3.187: read once at device creation */ /* 0.3.158: ActorShadows=0 leaves actor shadows to the game's blobs */
    const float* legacyFogParameters()const{return legacyFog.parameters;}
    const NorthlightCelestialProfiles::Profile& celestialPalette(const char* map,const float* camera){
        // Freeze once per frame: early disc, late halo, direct/GI and fog cannot
        // observe different worker publications or temporal transition states.
        if(paletteFrameValid&&paletteFrameMap==map)return framePalette;
        std::shared_ptr<const PaletteRegion> region;
        {std::lock_guard<std::mutex> lock(mutex);region=publishedPaletteRegion;}
        auto target=celestialProfiles.fallback;
        if(region&&region->map==map)target=NorthlightCelestialProfiles::sample(celestialProfiles,region->region,map,camera[0],camera[1]);
        framePalette=paletteTransition.update(target,map,camera[0],camera[1],double(GetTickCount())*.001);
        paletteFrameMap=map;paletteFrameValid=true;
        DWORD now=GetTickCount();if(NorthlightDiagnostics::enabled()&&(!paletteLogAt||now-paletteLogAt>=10000)){paletteLogAt=now;
            uint32_t zone=region&&region->map==map?NorthlightRegionalFog::zoneAt(region->region,camera[0],camera[1]):0;
            logf("CELESTIAL palette map=%s zone=%u moonDisc=(%.3f %.3f %.3f) lightMix=%.3f/%.3f strength=%.3f/%.3f",map,zone,framePalette.disc[1][0],framePalette.disc[1][1],framePalette.disc[1][2],framePalette.mix[0],framePalette.mix[1],framePalette.strength[0],framePalette.strength[1]);
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
    bool celestialContext(NorthlightCelestial::Context& out,float* inverseView,float* projectionOut)const{
        if(!hasContext()||!celestialValid)return false;
        out=celestial;memcpy(inverseView,context.inverseView,64);memcpy(projectionOut,projection,12);return true;
    }
    bool waterContext(NorthlightWaterContext& out,float nearZ,float farZ,float minZ,float maxZ)const{
        if(!hasContext())return false;
        memcpy(out.inverseView,context.inverseView,sizeof out.inverseView);memcpy(out.projection,projection,sizeof projection);
        out.nearZ=nearZ;out.farZ=farZ;out.minZ=minZ;out.maxZ=maxZ;
        const auto copy=[](float* to,V from){to[0]=from.x;to[1]=from.y;to[2]=from.z;};
        copy(out.sunDirection,sourceDirections[0]);copy(out.sunColor,sourceColors[0]);
        copy(out.moonDirection,sourceDirections[1]);copy(out.moonColor,sourceColors[1]);memcpy(out.skyColor,context.ambient,12);
        out.seconds=float(DWORD(GetTickCount()-animationEpoch))*.001f;
        if(shadowFrameReady)for(int source=0;source<2;++source){
            if(NorthlightGI::dot(sourceColors[source],sourceColors[source])<1e-10f)continue;
            copy(out.sourceShadowDirection[source],sourceDirections[source]);
            for(int cascade=0;cascade<2;++cascade){out.sourceShadows[source][cascade]=shadow[source*2+cascade];memcpy(out.sourceShadowMatrices[source][cascade],sourceMatrices[source][cascade],64);}
        }
        return true;
    }
    bool recognizesWmo(IDirect3DVertexShader9* shader)const{return wmoShaders.count(shader)!=0;}
    bool isWorldShader(IDirect3DVertexShader9* shader)const{return terrainShaders.count(shader)||captureShaders.count(shader)||wmoShaders.count(shader);}
    bool isSkinnedShader(IDirect3DVertexShader9* shader)const{auto it=actorPrograms.find(shader);return it!=actorPrograms.end()&&it->second->skinned;}
    bool wmoContext(IDirect3DVertexShader9* shader){
        if(failed)return false;if(valid)return true;
        auto it=wmoShaders.find(shader);if(it==wmoShaders.end())return false;
        float columns[16],rows[16];Projection q;
        if(FAILED(d->GetVertexShaderConstantF(2,columns,4))||!decodeColumnProjection(columns,q,rows))return false;
        char map[64]={};NorthlightWorldCamera::Camera camera;NorthlightWorldCamera::Diagnostics why;
        NorthlightWmoContext::Lighting light;
        bool cameraRead=NorthlightWorldCamera::read(map,rows[11],camera,&why);
        bool globalRead=cameraRead&&NorthlightWmoContext::readGlobalLighting(camera.camera,light);
        bool lightingRead=globalRead&&NorthlightWmoContext::context(camera.view,light,context);
        if(cameraRead&&!lightingRead&&it->second->lighting){float values[12];
            lightingRead=SUCCEEDED(d->GetVertexShaderConstantF(10,values,3))&&NorthlightWmoContext::decodeLitShader(it->second->hash,camera.view,values,context);
        }
        if(!cameraRead||!lightingRead){
            if(++wmoRejects==1||(wmoRejects%3600==0&&NorthlightDiagnostics::enabled()))logf("CITY context rejected camera=%s count=%u",NorthlightWorldCamera::rejectName(why.reason),wmoRejects);
            return false;
        }
        valid=true;traceContext=3;projection[0]=rows[0];projection[1]=rows[5];projection[2]=rows[11];
        readOriginalFog(30,it->second->fog);
        updateWorldContext(map,camera.camera,globalRead?&light:nullptr);
        if(++wmoContexts==1||(wmoContexts%600==0&&NorthlightDiagnostics::enabled()))logf("CITY WMO context accepted map=%s count=%u fogProof=%d globalLight=%d",map,wmoContexts,it->second->fog,globalRead);
        return true;
    }
    void terrainContext(){
        if(valid||failed)return;float view[16]={},lighting[12]={},p[16]={},camera[3]={};char map[64]={};
        bool registers=SUCCEEDED(d->GetVertexShaderConstantF(0,view,4))&&SUCCEEDED(d->GetVertexShaderConstantF(4,p,4));
        bool nativeRead=SUCCEEDED(d->GetVertexShaderConstantF(24,lighting,3));
        bool gameContext=NorthlightWorldContext::readMapAndCamera(map,camera);
        NorthlightWorldCamera::Camera independent;NorthlightWorldCamera::Diagnostics why;char cameraMap[64]={};
        bool cameraRead=registers&&gameContext&&NorthlightWorldCamera::read(cameraMap,p[11],independent,&why);
        bool cameraMatches=cameraRead&&!std::strcmp(map,cameraMap)&&NorthlightWorldCamera::agreesWithTerrain(independent,view);
        NorthlightWmoContext::Lighting global;
        bool globalRead=cameraMatches&&NorthlightWmoContext::readGlobalLighting(camera,global);
        bool decoded=registers&&gameContext&&NorthlightWmoContext::terrainContext(view,nativeRead?lighting:nullptr,camera,globalRead?&global:nullptr,context);
        traceContext=globalRead?1u:2u;
        bool agreement=decoded;
        if(!agreement){
            if(++contextRejects==1||(contextRejects%3600==0&&NorthlightDiagnostics::enabled()))logf("WORLD context rejected: registers=%d affineLight=%d clientRead=%d cameraAgreement=%d map=%s shaderCamera=(%.2f %.2f %.2f) gameCamera=(%.2f %.2f %.2f) light=(%.3f %.3f %.3f) count=%u",registers,decoded,gameContext,agreement,map,context.camera[0],context.camera[1],context.camera[2],camera[0],camera[1],camera[2],lighting[0],lighting[1],lighting[2],contextRejects);return;}
        if(++cameraChecks==1||(cameraChecks%3600==0&&NorthlightDiagnostics::enabled()))logf("CITY camera audit read=%d terrainAgreement=%d reject=%s permissiveSignatures=%d failingSignature=%d",cameraRead,cameraMatches,NorthlightWorldCamera::rejectName(why.reason),int(NorthlightWorldCamera::kPermissiveCameraSignatures),NorthlightWorldCamera::failingSignature());
        valid=true;projection[0]=p[0];projection[1]=p[5];projection[2]=p[11];
        readOriginalFog(12,true);
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
        bool fogKnown=fogRegisters&&NorthlightLegacyFog::decode(fogShader.major,fogShader.verified,fogEnabled!=0,fogTable,fogParameters,fogColor,fogARGB,projection[2],legacyFog);
        if(++fogReports==1||(fogReports%600==0&&NorthlightDiagnostics::enabled()))logf("WORLD legacy fog known=%d ps=%u verified=%d colorRegister=%d enabled=%.0f params=(%.7g %.7g %.7g) color=(%.5f %.5f %.5f)",fogKnown,fogShader.major,fogShader.verified,fogShader.colorRegister,legacyFog.parameters[3],legacyFog.parameters[0],legacyFog.parameters[1],legacyFog.parameters[2],legacyFog.color[0],legacyFog.color[1],legacyFog.color[2]);
    }
    void updateWorldContext(const char* map,const float* camera,const NorthlightWmoContext::Lighting* global=nullptr){
        if(unsigned fault=workerFault()){if(!failed)logf("WORLD worker stopped: %s; restart required",workerFaultMessage(fault));failed=true;valid=false;return;}
        if(!reportedContext){logf("WORLD context validated: map=%s camera=(%.2f %.2f %.2f) sun=(%.3f %.3f %.3f)",map,camera[0],camera[1],camera[2],context.lightDirection[0],context.lightDirection[1],context.lightDirection[2]);reportedContext=true;}
        celestialValid=NorthlightCelestial::read(camera,context.direct,celestial);
        // 0.3.200: a sky block read rejected for one frame (the two copies differ while the game rewrites it, more often with the game frames
        // ahead) dropped the sun weight to 0 for that frame: the sun light, shadows and shafts flashed off. The last good read on the same map
        // stands in for up to CelestialHoldMs; a longer failure (loading, an indoor map without a sky) still ends at the fallback.
        {const DWORD now=GetTickCount();
         if(celestialValid){celestialHeld=celestial;celestialHeldMap=map;celestialHeldAt=now;celestialHeldOk=true;}
         else if(celestialHeldOk&&celestialHeldMap==map&&DWORD(now-celestialHeldAt)<=CelestialHoldMs){celestial=celestialHeld;celestialValid=true;++celestialHolds;}}
        if(celestialValid){
            // A pure render-clock orbit shared by discs, shadows and fog.
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
            if(++celestialOrbitReports%600==1&&(celestialOrbitReports==1||NorthlightDiagnostics::enabled()))logf("CELESTIAL orbit gameDay=%.6f sun=%.2f->%.2f moon=%.2f->%.2f schedule=native sunCrest=85 moonCrest=43 weights=%.3f/%.3f nativeAlpha=%.3f/%.3f rendererAlpha=%.1f/%.1f",celestial.dayFraction,nativeSun,orbit.sun.elevation,nativeMoon,orbit.moon.elevation,celestial.sunWeight,celestial.moonWeight,nativeSunAlpha,nativeMoonAlpha,celestial.sun.alpha,celestial.moon.alpha);
        }
        if(!celestialValid){celestialLightMotion.reset();continuousCelestialShadows=false;}
        // glow hue: the light slots of this frame's proven read, else held for 2 s on the same map.
        const DWORD slotsNow=GetTickCount();
        if(lightSlotsMap!=map||DWORD(slotsNow-lightSlotsAt)>NorthlightSunHue::HoldMs){lightSlotsValid=false;lightSlotsMap=map;}
        if(global&&global->slotsProven){memcpy(lightSlots,global->slots,sizeof lightSlots);lightSlotsValid=true;lightSlotsAt=slotsNow;}
        glowHueFrame=NorthlightSunHue::compute(lightSlotsValid?lightSlots:nullptr,celestialPalette(map,camera).disc[1]);
        sourceDirections[0]=vec(context.lightDirection);sourceColors[0]=vec(context.direct);
        sourceDirections[1]=V(0,0,1);sourceColors[1]=V();sourceWeights[0]=1;sourceWeights[1]=0;
        auto resolvedSources=NorthlightCelestialSources::resolve(vec(context.lightDirection),vec(context.direct),
            celestialValid?vec(celestialLight.sun.direction):vec(context.lightDirection),
            celestialValid?vec(celestialLight.moon.direction):V(0,0,1),
            celestialValid?celestialLight.sunWeight:0.f,celestialValid?celestialLight.moonWeight:0.f);
        if(celestialValid){resolvedSources.sources[0].color=vec(celestialLight.sunColor);resolvedSources.sources[1].color=vec(celestialLight.moonColor);}
        authoredFill=resolvedSources.authoredFill;
        for(unsigned source=0;source<2;++source){rawSourceDirections[source]=resolvedSources.sources[source].direction;sourceDirections[source]=continuousCelestialShadows?resolvedSources.sources[source].direction:NorthlightWorldMath::quantizeDirection(resolvedSources.sources[source].direction,float(quality.shadowDirectionSteps));sourceColors[source]=resolvedSources.sources[source].color;sourceWeights[source]=resolvedSources.sources[source].weight;}
        requestStaticCasters(map);
        Request r;r.map=map;r.camera=vec(camera);r.light.sunDirection=sourceDirections[0];r.light.sunIrradiance=sourceColors[0]*3.14159265f;r.light.skyRadiance=vec(context.ambient);r.light.maxBounces=quality.giBounces;
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
        if(lastRequest.map==r.map&&completedActorSceneMap()==r.map){r.actorJob=completedActorJob();r.actorSerial=actorJobSerial();}
        std::shared_ptr<Snapshot> retiredSnapshot;
        {std::lock_guard<std::mutex> lock(mutex);
            if(published&&published!=observedPublication.lock()){
                observedPublication=published;if(!published->message.empty())logf("WORLD cache: %s",published->message.c_str());
            }
            if(published&&published!=active&&NorthlightWorldStreaming::adopts(published->map,published->center,bool(published->bvh),active&&active->bvh,r.map,r.camera)){
                bool newGI=published->serial&&(!active||active->serial!=published->serial);retiredSnapshot=std::move(active);active=published;
                if(newGI&&NorthlightDiagnostics::enabled())logf("GI activated tick=%lu id=%llu ageMs=%lu queueMs=%lu geometryMs=%lu actorMs=%lu solveMs=%lu reused=%u solved=%u cancelled=%u origin=(%.1f %.1f %.1f) dynamicReused=%u dynamicSolved=%u staticOnly=%u partial=%u processed=%u generation=%llu retargeted=%u actorTextureDecode=worker actorTextures=%u actorTextureBytes=%zu pathRecorded=%llu pathReplayedRays=%llu pathRetracedRays=%llu pathRecordKB=%zu",(unsigned long)GetTickCount(),(unsigned long long)active->requestId,(unsigned long)(GetTickCount()-active->requestedAt),(unsigned long)active->queueMs,(unsigned long)active->geometryMs,(unsigned long)active->actorMs,(unsigned long)active->solveMs,active->reusedProbes,active->solvedProbes,active->superseded,active->origin.x,active->origin.y,active->origin.z,active->dynamicReused,active->dynamicSolved,unsigned(active->staticOnly),unsigned(active->partial),active->processedProbes,(unsigned long long)active->lightingGeneration,active->retargeted,active->actorTexturesDecoded,active->actorTextureEncodedBytes,(unsigned long long)active->movingStats.recorded,(unsigned long long)active->movingStats.replayed,(unsigned long long)active->movingStats.retraced,active->pathRecordBytes/1024);
            }
            r.reason=(lastRequest.map!=r.map?1u:0u)|(different(quantize(lastRequest.probeCenter,float(quality.giProbeMoveStep)),quantize(r.probeCenter,float(quality.giProbeMoveStep)),.1f)?2u:0u)|
                (different(lastRequest.light.sunDirection,r.light.sunDirection,.02f)?4u:0u)|(different(lastRequest.light.sunIrradiance,r.light.sunIrradiance,.03f)?8u:0u)|(different(lastRequest.light.skyRadiance,r.light.skyRadiance,.01f)?16u:0u)|
                (lastRequest.light.additionalDirections.empty()||different(lastRequest.light.additionalDirections[0].direction,r.light.additionalDirections[0].direction,.02f)||different(lastRequest.light.additionalDirections[0].irradiance,r.light.additionalDirections[0].irradiance,.03f)?32u:0u)|
                (NorthlightWorldStreaming::leadMoved(lastRequest.camera,lastRequest.geometryCenter,r.camera,r.geometryCenter)?128u:0u);
            if(r.reason){r.id=request.id+1;r.baseId=r.id;r.queuedAt=GetTickCount();request=r;lastRequest=r;pending=true;wake.notify_one();
                if(NorthlightDiagnostics::enabled())logf("GI request tick=%lu id=%llu reason=%u camera=(%.2f %.2f %.2f) sun=(%.5f %.5f %.5f)",(unsigned long)r.queuedAt,(unsigned long long)r.id,r.reason,r.camera.x,r.camera.y,r.camera.z,r.light.sunDirection.x,r.light.sunDirection.y,r.light.sunDirection.z);
            }
            // 0.3.156 watchdog (log only): the builder waited for a free geometry generation
            // for GenerationStallLogMs; name the holders once per episode. Nothing is dropped.
            if(generationStall.due(GetTickCount(),GenerationStallLogMs))
                logf("WORLD geometry stalled ms=%lu pending=%u pendingMap=%s pendingActive=%u pendingPublished=%u active=%u activeId=%llu activeCenter=(%.1f %.1f %.1f) published=%u publishedId=%llu publishedMap=%s publishedCenter=(%.1f %.1f %.1f) publishedApplicable=%u camera=(%.1f %.1f %.1f) map=%s",
                    (unsigned long)(GetTickCount()-generationStall.since),unsigned(bool(pendingMesh)),pendingMesh?pendingMesh->map.c_str():"-",unsigned(pendingMesh&&active&&pendingMesh->bvh==active->bvh),unsigned(pendingMesh&&published&&pendingMesh->bvh==published->bvh),
                    unsigned(bool(active)),(unsigned long long)(active?active->requestId:0),active?active->center.x:0.f,active?active->center.y:0.f,active?active->center.z:0.f,
                    unsigned(bool(published)),(unsigned long long)(published?published->requestId:0),published?published->map.c_str():"-",published?published->center.x:0.f,published?published->center.y:0.f,published?published->center.z:0.f,
                    unsigned(published&&NorthlightWorldStreaming::applicable(published->map,published->center,r.map,r.camera)),r.camera.x,r.camera.y,r.camera.z,r.map.c_str());
            }
        {auto phase=streamingPhases.measure(NorthlightStreaming::PhaseProfile::Retire);
         auto& reaper=NorthlightStreaming::cpuRetirement();retirementBacklog.retry(reaper);
         const bool adopted=retiredSnapshot&&active&&retiredSnapshot->bvh!=active->bvh; /* a new region, not a GI publication */
         if(retiredSnapshot)retirementBacklog.retireOrFree(reaper,retiredSnapshot,snapshotRetireBytes(*retiredSnapshot));
         retiredSnapshot.reset();
         // 0.3.169: hold the snapshot past its 96-unit coverage until a replacement applies (above)
         // or the hard limit or a map change retires it.
         const char* retired=active&&!NorthlightWorldStreaming::retained(active->map,active->center,r.map,r.camera)?(active->map!=r.map?"map":"retired"):nullptr;
         const V held=active?active->center:V();
         if(retired){retirementBacklog.retireOrFree(reaper,active,snapshotRetireBytes(*active));active.reset();}
         releaseOrphanedPending("range");
         const float distance=active&&active->map==r.map?std::sqrt(NorthlightGI::dot(r.camera-active->center,r.camera-active->center)):0.f;
         if(distance>coverMax)coverMax=distance;
         const bool holding=active&&!NorthlightWorldStreaming::applicable(active->map,active->center,r.map,r.camera);
         const bool logHold=holdEpisodes<=32||NorthlightDiagnostics::enabled();
         if(retired||(coverageHold&&(adopted||!holding))){
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
    bool terrainShadowActive()const{return effects.shadows&&ready()&&shadowsComposited;}
    bool shadowsRequested()const{return effects.shadows;} /* 0.3.187: changes only in setEffects (frame boundary) */
    void registerPixelShader(IDirect3DPixelShader9* shader){
        fogShaders.erase(shader);UINT size=0;
        {auto old=terrainShadowShaders.find(shader);if(old!=terrainShadowShaders.end()){drop(old->second);terrainShadowShaders.erase(old);}}
        if(!shader||FAILED(shader->GetFunction(nullptr,&size))||size<8||size>65536||size%4)return;
        std::vector<uint32_t> code(size/4);if(FAILED(shader->GetFunction(code.data(),&size)))return;
        FogShader info;info.major=(code[0]>>8)&255;
        info.colorRegister=NorthlightLegacyFog::ps3FogColorRegister(code.data(),code.size());info.verified=info.colorRegister>=0;fogShaders[shader]=info;
    }
    void registerShader(IDirect3DVertexShader9* shader,uint64_t hash){
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
        auto admission=[&](size_t){if(!memoryChecked){memoryChecked=true;memoryAllowed=admitsGrowth("replay-cache",64*NorthlightGeometryMemory::MiB);}return memoryAllowed;};
        auto bindReplay=[&](Replay& p){NorthlightReplayGPU::bindResident(replayGpuCache,d,p,allowCache,admission);};
        replayTimeDeferredIndices.clear();
        {auto bind=streamingPhases.measure(NorthlightStreaming::PhaseProfile::ReplayBind);
        for(size_t i=0;i<replays.size();++i){bindReplay(*replays[i]);
            if(!replays[i]->gpuCached&&replayGpuCache.timeDeferredLast())replayTimeDeferredIndices.push_back(i);}
        NorthlightReplayGPU::commitAdmissions(replayGpuCache,replays,bindReplay);}
        phase.next(1); // Layout, bulk-buffer growth/copy and output bindings.
        UINT totals[4]={};std::uint64_t totalIndices=0;size_t sharedBulkDraws=0,sharedBulkBytes=0;
        auto layout=[&](size_t count){
            for(auto& t:totals)t=0;totalIndices=0;sharedBulkDraws=sharedBulkBytes=0;replayBulkLayout.begin();
            for(size_t i=0;i<count;++i){auto& p=replays[i];if(p->gpuCached)continue;
                const size_t owner=replayBulkLayout.add(p->shared.get(),i);
                if(owner!=i){const auto& first=*replays[owner];
                    for(unsigned s=0;s<4;++s){p->stride[s]=first.stride[s];p->offset[s]=first.offset[s];}
                    p->start=first.start;++sharedBulkDraws;sharedBulkBytes+=p->mesh().byteSize();continue;
                }
                for(unsigned s=0;s<4;++s){auto& source=p->mesh().streams[s];p->stride[s]=source.stride;p->offset[s]=totals[s];
                    std::uint64_t next=std::uint64_t(totals[s])+((source.bytes.size()+15)&~std::size_t(15));if(next>64u*1024u*1024u)return false;totals[s]=UINT(next);}
                p->start=p->indexed?UINT(totalIndices):0;totalIndices+=p->mesh().indices.size();}
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
             for(;fit<replays.size();++fit){auto& p=replays[fit];if(p->gpuCached||!replayBulkLayout.unique(fit))continue;bool ok=true;
                 for(unsigned s=0;s<4&&ok;++s){std::uint64_t next=std::uint64_t(running[s])+((p->mesh().streams[s].bytes.size()+15)&~std::size_t(15));ok=next<=replayVertexBytes[s];}
                 if(!ok||(runningIndices+p->mesh().indices.size())*4>replayIndexBytes)break;
                 for(unsigned s=0;s<4;++s)running[s]=UINT(running[s]+((p->mesh().streams[s].bytes.size()+15)&~std::size_t(15)));runningIndices+=p->mesh().indices.size();}
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
            for(size_t i=0;i<replays.size();++i){auto& p=replays[i];if(!p->gpuCached&&replayBulkLayout.unique(i)&&!p->mesh().indices.empty())std::memcpy(static_cast<std::uint32_t*>(memory)+p->start,p->mesh().indices.data(),p->mesh().indices.size()*4);}
            if(!check(replayIndicesGPU->Unlock(),"model snapshot index unlock"))return false;
            indexBase=slot.offset/4;
        }
        // The copies are done: from here on offset/start are absolute in the ring buffers (shared duplicates included;
        // gpuCached replays carry their own batch offsets and are never touched).
        for(auto& p:replays){if(p->gpuCached)continue;for(unsigned s=0;s<4;++s)if(!p->mesh().streams[s].bytes.empty())p->offset[s]+=vertexBase[s];if(p->indexed)p->start+=indexBase;}
        // 0.3.176 (U3'): a binding already held keeps its reference (no Release/AddRef pair).
        for(auto& p:replays){if(p->gpuCached)continue;for(unsigned s=0;s<4;++s){IDirect3DVertexBuffer9* const stream=p->mesh().streams[s].bytes.empty()?nullptr:replayVerticesGPU[s];
                if(p->stream[s]!=stream){drop(p->stream[s]);if(stream){p->stream[s]=stream;stream->AddRef();}}}
            IDirect3DIndexBuffer9* const index=p->indexed?replayIndicesGPU:nullptr;if(p->index!=index){drop(p->index);if(index){p->index=index;index->AddRef();}}}
        phase.stop(); // Report I/O is not part of cache/bulk timing.
        if(captureSampled){size_t bulk=indexBytes;for(auto total:totals)bulk+=total;
            const auto residency=NorthlightReplayGPU::batchStats(replayGpuCache);
            deferLogf("MODEL GPU cache hits=%u reusedBytes=%zu newBytes=%zu residentBytes=%zu bulkUploadBytes=%zu batches=%zu batchBytes=%zu liveBytes=%zu separateBytes=%zu batchedUploads=%u separateUploads=%u compactions=%u compactedBytes=%zu batchFailures=%u",replayGpuCache.hits(),replayGpuCache.reused(),replayGpuCache.uploaded(),replayGpuCache.bytes(),bulk,
                residency.batches,residency.batchBytes,residency.liveBytes,residency.separateBytes,residency.batchedUploads,residency.separateUploads,residency.compactions,residency.compactedBytes,residency.batchFailures);
            deferLogf("MODEL bulk sharing duplicateDraws=%zu avoidedBytes=%zu actualBytes=%zu",sharedBulkDraws,sharedBulkBytes,bulk);
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
        for(auto& p:replays)p->staticProofMask=0;
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
    bool drawRoot(IDirect3DVertexShader9* shader,float* root){
        float rows[12];const auto* program=paletteRows(shader,rows);if(!program)return false;
        float bank[4*256];std::memcpy(bank+4*program->paletteBase,rows,sizeof rows);
        return NorthlightActorDeformation::rootWorld(*program,bank,context.inverseView,root);
    }
    // 0.3.172 near capture reserve (near_reserve.h): asked only for a skinned draw the budget is
    // about to turn away for bytes. The anchor is taken once per capture frame, at the first ask
    // (the previous selection's self and this frame's camera; shadowPivot() is not called).
    NorthlightNearReserve::Anchor nearAnchor;bool nearAnchorReady=false;
    unsigned nearAdmitted=0,nearRefused=0;size_t nearBytes=0;
    bool nearDraw(IDirect3DVertexShader9* shader){
        if(!nearAnchorReady){nearAnchorReady=true;const float forward[3]={context.inverseView[8]*projection[2],context.inverseView[9]*projection[2],context.inverseView[10]*projection[2]};
            nearAnchor=NorthlightNearReserve::anchor(actorShadowHistory.selfHold()>0?actorShadowHistory.selfAt():nullptr,context.camera,forward,pivotDistance);}
        float root[3];return drawRoot(shader,root)&&NorthlightNearReserve::test(nearAnchor,root)!=NorthlightNearReserve::None;
    }
    bool nearCandidate(bool priority,IDirect3DVertexShader9* shader){
        return priority&&replaySnapshots.nearReserve()&&!replaySnapshots.countExhausted(priority)&&nearDraw(shader);}
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
        if(smallShadow&&!actorCaptureEnabled()){if(sample)++smallShadowEarly;return;}
        fate.reason=NorthlightShadowFate::CaptureBudget;
        // Only reject when no legal snapshot can fit, including cache hits
        // and UP draws. A previous oversized failure is not an exhaustion proof.
        // A near skinned draw may read from the near reserve once the main budget is spent.
        bool nearby=false;
        if(replaySnapshots.captureExhausted(priority)){const bool candidate=nearCandidate(priority,current);
            if(candidate&&!replaySnapshots.captureExhausted(priority,true))nearby=true;
            else{nearRefused+=candidate;captureShortfall=true;
                if(sample&&priority){++skinnedSnapshotRejected;++skinnedBudgetRejected;}
                if(sample&&!priority)++otherBudgetRejected;
                return;}
        }
        fate.reason=NorthlightShadowFate::Blend;
        DWORD blend=0,alpha=0,ref=0,alphaFunc=D3DCMP_ALWAYS;if(FAILED(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))||blend){if(sample&&priority)++skinnedBlendRejected;if(sample&&!priority)++otherBlendRejected;return;}
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
        std::unique_ptr<Replay,ReplayRecycle> p(acquireReplay().release(),ReplayRecycle{this});p->shadowSkinned=priority;p->shadowSelected=!smallShadow;p->shadowSmall=smallShadow;p->boneKnown=false;p->fateSlot=-1;p->fateClass=smallShadow?NorthlightShadowFate::Small:NorthlightShadowFate::NotRanked;p->fateDistance=0;p->projectionKind=kind;p->shader=metadata.replacement;p->shader->AddRef();p->originalShader=current;p->originalShader->AddRef();p->pointBounds={};
        if(replaySnapshots.lookup()==NorthlightDrawSnapshot::Frame::Lookup::Prefetch&&!freeReplays.empty())prefetchReplay(*freeReplays.back()); /* 0.3.181 (S2) */
        if(FAILED(d->GetVertexDeclaration(&p->decl))||!p->decl)return;
        NorthlightDrawSnapshot::Draw draw{type,base,minimum,vertexTotal,start,count,indexed};NorthlightDrawSnapshot::Diagnostics why;
        const size_t readBefore=replaySnapshots.bytesRead();
        replaySnapshots.sampleMaintenance(sample);
        auto read=[&]{return userVertices?replaySnapshots.readUP(p->decl,draw,userIndices,userFormat,userVertices,userStride,p->snapshot,&why,priority,nearby):replaySnapshots.read(d,p->decl,draw,p->snapshot,&why,priority,&p->shared,nearby);};
        bool captured=read();size_t nearFrom=readBefore;
        // Turned away for bytes: a near draw is read once more, from the near reserve.
        if(!captured&&why.error==NorthlightDrawSnapshot::Error::Budget&&!nearby&&nearCandidate(priority,current)){
            nearby=true;nearFrom=replaySnapshots.bytesRead();captured=read();if(!captured)nearRefused+=why.error==NorthlightDrawSnapshot::Error::Budget;}
        if(captured&&nearby){++nearAdmitted;nearBytes+=replaySnapshots.bytesRead()-nearFrom;}
        if(!captured){if(why.error==NorthlightDrawSnapshot::Error::Budget){fate.reason=NorthlightShadowFate::CaptureBudget;captureShortfall=true;}
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
    NorthlightJobs::Stats takeJobStats(){return replayJobs_.take();} /* renderer.cpp's JOBS line, RenderProfile sample frames */
    static constexpr float LampFogNear=3.5f; /* c58.x: the lamp glow's near fade start, the scissor view's fog near */
    // The frame's D3D-free light and fog work. Its inputs are final once upload() has run (the snapshot and its lights, the fog field,
    // the camera, the celestial state, the settings); its outputs (the tracker, the clouds' clock and state, localScissorView and this
    // frame) are read only after the join at the constant bank. c holds the bank's c22.w, c31.w and c32 (the same expressions).
    struct AtmosphereFrame {
        NorthlightLocalLightSelection::Selection lights;NorthlightFogClouds::Frame cf;float c[33][4]={};
        float airFloor=.0017f,veil[2]={1,1}; /* veil: the sky transmittance toward the sun without / with the clouds' term */
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
        a.lights=active?localLightTracker.update(active->localLights,context.camera,NorthlightGpuBudget::lightLimit(quality.localLightLimit,gpuBudgetLevel),float(selectDt),selectContinuous) /* 0.3.200 (gpu budget): the setting at level 0 */:NorthlightLocalLightSelection::Selection{};
        auto& localLights=a.lights;
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
        // Same camera, projection, near plane and glow start as c0..c6/c58; each batch's rect of the direct (8) and fog (4) passes.
        localScissorView=NorthlightLocalLightScissor::view(context.inverseView,projection,nearZ,LampFogNear,w,h);
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
        a.veil[0]=a.veil[1]=1;
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
            }
        }
    }
    // The later cascades' replay culling (per packet: the static proof's containment in the slot's cached matrix, bit 0; the pose
    // bounds' clip test against the slot's matrix, bit 1). Kicked after the frame's first bounds join for every later active slot and
    // read at that slot's replay loop when both matrices still match (else the loop tests inline, as before). Reads the packets only.
    struct ReplayCull {WorldRenderer* r=nullptr;int slot=0;bool kicked=false;float matrix[16]={},cached[16]={};std::vector<uint8_t> flags;NorthlightJobs::Counter done;
        void operator()(){r->replayCullWork(*this);}};
    ReplayCull replayCulls[4];
    void replayCullWork(ReplayCull& job){
        const size_t n=replays.size();job.flags.resize(n);
        for(size_t index=0;index<n;++index){const auto& p=*replays[index];uint8_t f=0;
            if((p.staticProofMask&(1u<<job.slot))&&StaticShadow::containsBounds(job.cached,p.staticProofLow,p.staticProofHigh))f|=1;
            if(p.pointBounds.valid&&NorthlightShadowBounds::clipReject(V(p.pointBounds.low[0],p.pointBounds.low[1],p.pointBounds.low[2]),V(p.pointBounds.high[0],p.pointBounds.high[1],p.pointBounds.high[2]),job.matrix))f|=2;
            job.flags[index]=f;}
    }
    void replayCullKick(int fromSlot,const bool* sourceActive){
        for(int slot=fromSlot+1;slot<4;++slot){if(!sourceActive[slot/2])continue;auto& job=replayCulls[slot];
            job.r=this;job.slot=slot;std::memcpy(job.matrix,sourceMatrices[slot/2][slot%2],sizeof job.matrix);std::memcpy(job.cached,staticSlotMatrix[slot],sizeof job.cached);
            job.kicked=true;replayJobs_.kick(job.done,job);}
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
         if(valid&&completedActorSceneMap()==lastRequest.map&&!pending&&!workerBusy&&actorJobSerial()!=lastRequest.actorSerial&&submissionStart-actorRequestTick>=250){
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
        // 0.3.200 (jobs): the later slots' replay culling is kicked at the frame's first bounds join (replayCullKick), joined per slot and here.
        bool replayCullsKicked=false;struct CullSettle {WorldRenderer& r;~CullSettle(){r.replayCullSettle();}} cullSettle{*this};
        bool anyShadowCacheRender=false;
        ++shadowPasses;
        if(NorthlightStaticPrebuild::Scheduler::Enabled)for(int source=0;source<2;++source)staticPrebuild.observe(source,effects.shadows&&sourceActive[source],rawSourceDirections[source],GetTickCount());
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
                for(auto& b:batches){
                    if(b.terrain&&!fixedTerrainChunks().count({b.chunkX,b.chunkY}))continue;
                    if(NorthlightShadowBounds::directionalClipReject(b.boundsLow,b.boundsHigh,cachedMatrix)){if(real)++culledBatches[cascade];continue;}
                    size_t passes=1;
                    if(partial){NorthlightShadowBounds::TexelRect footprint;const bool known=NorthlightShadowBounds::texelFootprint(b.boundsLow,b.boundsHigh,cachedMatrix,long(ShadowCacheSize),StaticCacheDirtyMargin,footprint);
                        staticDirtyHits.clear();for(const auto& r:staticDirty)if(!known||NorthlightShadowBounds::intersects(footprint,r))staticDirtyHits.push_back(&r);
                        if(staticDirtyHits.empty()){++shadowCachePartialSkippedDraws;continue;}passes=staticDirtyHits.size();}
                    if(real)++drawnBatches[cascade];
                    const bool interior=NorthlightShadowBounds::depthFullyInside(b.boundsLow,b.boundsHigh,cachedMatrix);
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
            const auto action=NorthlightQuality::cascadeAction(reuse,interval,shadowPasses,!reason&&key.valid&&!pull,diagnosticCapture!=0,replaysComplete);
            cascadeActions[slot]=action==NorthlightQuality::CascadeAction::Render?'R':action==NorthlightQuality::CascadeAction::Defer?'D':'U';
            if(action!=NorthlightQuality::CascadeAction::Render){
                if(action==NorthlightQuality::CascadeAction::Defer){captureDemand=true;++captureDeferrals;}else ++(cascade?farReuses:nearReuses);
                memcpy(matrices[cascade],reuse.matrix,64);
                if(profile)profile->mark(source==0?(cascade==0?"SunNear":"SunFar"):(cascade==0?"MoonNear":"MoonFar"));bucket(NorthlightEffectsBuckets::Bucket(NorthlightEffectsBuckets::SunNear+source*2+cascade));continue;}
            reuse.begin(interval); /* nothing written yet; a failure below stays unusable */
            if(cascade==0)nearRendered=captureMode==CaptureFresh&&interval>1&&actorShadows;
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
            const double dynamicStart=phaseNow();
            // Dynamic part of this frame: live terrain and replays into the scratch map.
            d->SetDepthStencilSurface(nullptr);if(!check(d->SetRenderTarget(0,shadowScratchSurface),"shadow scratch target"))return false;
            d->SetDepthStencilSurface(shadowDepth);D3DVIEWPORT9 vp={0,0,1024,1024,0,1};d->SetViewport(&vp);
            if(!check(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0),"clear shadow"))return false;
            d->SetVertexDeclaration(shadowDecl);d->SetStreamSource(0,vertices,0,sizeof(NorthlightGI::WorldVertex));for(int i=0;i<4;++i)d->SetStreamSourceFreq(i,1);d->SetIndices(indices);
            d->SetVertexShader(shadowVS);d->SetVertexShaderConstantF(0,matrices[cascade],4);d->SetPixelShader(shadowPS);
            // Cached-mesh terrain chunks that the game did not draw live this frame.
            UINT terrainBoundPage=UINT_MAX;
            if(jobs)replayJobs_.wait(terrainDone); /* 0.3.200 (jobs): the candidates' first reader */
            if(!terrainCandidates.forEach(batches,fixedTerrainChunks(),liveTerrainChunks,[&](const Batch& b){
                if(NorthlightShadowBounds::clipReject(b.boundsLow,b.boundsHigh,matrices[cascade])){++culledBatches[cascade];return true;}
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
            if(jobs&&!replayCullsKicked){replayCullsKicked=true;replayCullKick(slot,sourceActive);} /* 0.3.200 (jobs): the bounds are final; this slot tests inline */
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
                ++replaySlotTested[slot];replaySlotBounded[slot]+=p->pointBounds.valid;
                if(p->pointBounds.valid&&(cull?(cull[index]&2)!=0:NorthlightShadowBounds::clipReject(V(p->pointBounds.low[0],p->pointBounds.low[1],p->pointBounds.low[2]),V(p->pointBounds.high[0],p->pointBounds.high[1],p->pointBounds.high[2]),matrices[cascade]))){++culledReplayDraws;++replaySlotCulled[slot];continue;}
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
        if(jobs)replayJobs_.wait(atmosphereDone);else atmosphereWork(atmosphere,nearZ,w,h,debug);
        auto& localLights=atmosphere.lights;
        localDirectCount=localLights.count;localDirectNearest=localLights.nearest;
        c[52][0]=float(std::min(localDirectCount,NorthlightLocalLightSelection::DirectBatchSize));c[52][1]=.9f*lampGain;
        // Near fade: no added fog within 3.5 units of the viewer, full at 15.5.
        // Same soft ramp, shifted 0.5 world units closer.
        c[58][0]=3.5f;c[58][1]=1.f/12;c[58][2]=10.f*lampGain*wx.lampFogGain();c[58][3]=.12f*lampGain*wx.lampFogGain(); /* 0.3.198 (rain): lamp fog gain (x1 since 0.3.199); 0.3.199 (fog clouds): 10 / .12 (were 13 / .156): the glow read too strong in the game test */
        const float airFloor=atmosphere.airFloor; /* atmosphereWork: .0017f plus the rain's extra, thinned in dense zones */
        c[59][2]=wx.shadowSoften();c[59][3]=airFloor; /* 0.3.198 (rain): direct shadow softening (0 when dry), air extinction floor (WorldFog) */
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
        skyTransmittanceFrame=cf.active?atmosphere.veil[1]:atmosphere.veil[0]; /* the clouds' term only when the pass is really active (1: no fog field or no fog) */
        c[33][0]=float(w);c[33][1]=float(h);c[33][2]=float(w/2);c[33][3]=float(h/2);
        c[1][0]=projection[0];c[1][1]=projection[1];c[1][2]=projection[2];c[1][3]=minZ;
        c[2][0]=1.f/(maxZ-minZ);c[2][1]=48;c[2][2]=192;c[2][3]=1.f/1024;
        memcpy(c[3],context.inverseView,64);memcpy(c[7],sourceMatrices[firstSource][0],64);memcpy(c[11],sourceMatrices[firstSource][1],64);
        memcpy(c[15],context.camera,12);memcpy(c[16],context.lightDirection,12);memcpy(c[17],context.direct,12);memcpy(c[18],context.ambient,12);
        c[18][3]=NorthlightTwilightFill::gain(celestialValid,celestialValid?celestial.dayFraction:-1.,
            celestialValid?celestialLight.sun.direction[2]:0.f,celestialValid?celestialLight.moon.direction[2]:0.f);
        c[18][3]+=wx.ambientLift(); /* 0.3.198 (rain): a little more sky ambient from the GI pass in rain (+0 when dry; the probes themselves are untouched) */
        c[19][0]=active->origin.x;c[19][1]=active->origin.y;c[19][2]=active->origin.z;c[19][3]=8;
        c[20][0]=float(NorthlightGI::probeLayout().atlas);c[20][1]=NorthlightQuality::giIntensity(quality);c[20][2]=.85f;c[20][3]=active->serial?1.f:0.f;
        DWORD now=GetTickCount();
        c[21][0]=uploadedFogField&&(uploadedFogField->fogCells||uploadedFogField->airCells)?1.f:0.f;c[21][1]=1.2f*wx.shaftGain();c[21][2]=.38f;c[21][3]=128; /* 0.3.198 (rain): shafts fade in rain, x1 when dry */
        // 0.3.200 (gpu budget): fewer march intervals at reduced levels (c60.x clouds, c64.z WorldFog; both stay 0, the bank unchanged, at level 0).
        if(gpuBudgetLevel){if(cf.active)c[60][0]=NorthlightGpuBudget::spacingDelta(c[21][3],NorthlightGpuBudget::cloudSteps(gpuBudgetLevel),40);c[64][2]=NorthlightGpuBudget::spacingDelta(c[21][3],NorthlightGpuBudget::fogSteps(gpuBudgetLevel),48);}
        memcpy(c[23],context.camera,12);c[24][0]=NorthlightWorldMath::ShadowBiasWorld*NorthlightWorldMath::InverseShadowDepth;c[24][1]=2;c[24][2]=float(debug);c[24][3]=float(DWORD(now-animationEpoch))*.001f;
        memcpy(c[25],legacyFog.parameters,16);memcpy(c[26],legacyFog.color,16);
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
        const auto haze=NorthlightHorizonHaze::constants(horizonHazeState,{quality.horizonHaze,quality.horizonHazeStart,quality.horizonHazeBand,quality.horizonHazeTerrain},
            farZ,hazeZone,effects.fog,celestialValid,hazeSun,sourceWeights[0],hazeLift,wx.hazeTauScale());
        c[57][1]=haze.shape[0];c[57][2]=haze.shape[1];c[57][3]=haze.shape[2];c[67][2]=haze.sun[0];c[67][3]=haze.sun[1];
        c[35][1]=haze.lift[0];c[35][2]=haze.lift[1];c[35][3]=haze.lift[2]; /* lift colour = the glow hue (WorldComposite reads c35.yzw) */
        hazeFrame=haze;
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
            float rgb[4]={sourceActive[source]?sourceColors[source].x:0.f,sourceActive[source]?sourceColors[source].y:0.f,sourceActive[source]?sourceColors[source].z:0.f,0};
            // fog passes only: the sun's direct scatter takes the glow hue; the forward soft cap
            // rises so the aureole brightens toward the sun. c17/c18 are restored after the fog loop.
            if(volume&&source==0)NorthlightSunHue::fogDirect(rgb,glowHueFrame,rgb);
            d->SetPixelShaderConstantF(16,dir,1);d->SetPixelShaderConstantF(17,rgb,1);
            if(volume){c[21][1]=1.2f*volumePalette.fogGain[source]*wx.shaftGain();c[21][2]=source==0?NorthlightSunHue::SunForwardCap:NorthlightSunHue::MoonForwardCap;d->SetPixelShaderConstantF(21,c[21],1);d->SetTexture(15,sourceVis[source][1-sourceVisIndex]);}
            c[27][0]=first?(volume?1.f:1.f-authoredFill):0.f;c[27][1]=sourceWeights[source];d->SetPixelShaderConstantF(27,c[27],1);
        };
        d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
        // Smoothed normals for the lighting, GI and local light passes (s14).
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
        d->SetRenderTarget(0,normalSurface);d->SetPixelShader(normalsPS);if(!check(quad(w/2,h/2),"world normals pass"))return false;
        d->SetTexture(14,normalBuffer);d->SetSamplerState(14,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(14,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(14,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(14,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(14,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(14,D3DSAMP_SRGBTEXTURE,FALSE);
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
        if(profile)profile->mark("WorldLighting");
        bucket(NorthlightEffectsBuckets::Lighting);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_COLORWRITEENABLE,7);d->SetPixelShader(giPS);
        if(effects.gi)d->SetTexture(8,probePrev); /* 0.3.197: WorldGI reads the previous SH on s8 (LightingBuffer is not read here); null without the texture, moment.w is then None */
        if(effects.gi&&!check(quad(w/2,h/2),"world GI pass"))return false;
        d->SetTexture(8,textures[8]); /* 0.3.197: restore the frame-start binding */
        if(localDirectCount&&debug==0){
            d->SetPixelShader(localDirectPS);d->SetRenderState(D3DRS_SCISSORTESTENABLE,NorthlightLocalLightScissor::Enabled);
            d->SetTexture(12,baselineLight); /* sun visibility (baseline alpha); the temporal pass rebinds it anyway */
            for(unsigned firstLight=0;firstLight<localDirectCount;firstLight+=NorthlightLocalLightSelection::DirectBatchSize){
                const auto batch=localLights.batch<NorthlightLocalLightSelection::DirectBatchSize>(firstLight);
                const float info[4]={float(batch.count),c[52][1],lampSunlitCut,0};
                d->SetPixelShaderConstantF(36,batch.position[0].data(),NorthlightLocalLightSelection::DirectBatchSize);
                d->SetPixelShaderConstantF(44,batch.color[0].data(),NorthlightLocalLightSelection::DirectBatchSize);
                d->SetPixelShaderConstantF(52,info,1);
                if(localScissor(batch.position,batch.count,&atmosphere.directRects[firstLight/NorthlightLocalLightSelection::DirectBatchSize])&&!check(quad(w/2,h/2),"local direct light batch")){d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);return false;}
            }
            d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
        }
        if(profile)profile->mark("GI");bucket(NorthlightEffectsBuckets::GI);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
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
        {   // Temporal stabilization: light + history -> temporalLight[cur] (MRT with view distance).
            const unsigned cur=temporalIndex,prev=1-temporalIndex;
            d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
            // 0.3.174: s10 switches from ProbeMetadata (WorldGI, done) to the AO/bloom target for the
            // removal smoothing and WorldComposite; the frame-start loop rebinds probe[4] POINT.
            d->SetTexture(10,foldAO?foldAO:neutralAO);d->SetSamplerState(10,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(10,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
            // 0.3.185: the removal smoothing is its own half-res pass, light -> smoothLight (a copy where nothing is
            // removed; skipped without a drawn source, c30.y=0, where s9 is the raw light). It reads s0 Scene, s1 Depth, s8 light,
            // s10 AO, s12 baseline and s14 NormalBuffer (POINT, as the normals setup left it; histories are read later).
            d->SetTexture(8,light);d->SetTexture(12,baselineLight);d->SetTexture(14,normalBuffer);d->SetTexture(15,nullptr); /* s12 stays bound for the temporal pass and the composite */
            d->SetSamplerState(14,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(14,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
            if(!check(d->SetRenderTarget(0,smoothSurface),"removal smoothing target"))return false;
            const bool removalDrawn=c[30][1]>0; // no drawn source: smoothRemoval would be a pure copy, TemporalLight reads the raw light
            if(removalDrawn){d->SetPixelShader(removalPS);if(!check(quad(w/2,h/2),"removal smoothing pass"))return false;}
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
        if(profile)profile->mark("PointLighting");
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
                NorthlightFogClouds::colour(c[26],c[25][3]>=.5f,c[31][3],cloudColour);
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
            d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_COLORWRITEENABLE,7);d->SetPixelShader(localFogPS);
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
            d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
            d->SetPixelShaderConstantF(21,c[21],1);
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
        d->SetRenderTarget(0,targetSurface);d->SetTexture(8,temporalLight[1-temporalIndex]);d->SetTexture(9,fogResolved?fogBlurred:fog);d->SetPixelShader(finalPS);if(!check(quad(w,h),"world composite"))return false;composited=true;d->SetTexture(13,regionalFogTexture);if(profile)profile->mark("WorldComposite");
        if(diagnosticCapture){
            gpuDiagnosticArmed=false;unsigned capture=++gpuDiagnosticCaptures;
            const std::string& directory=gpuDiagnosticDirectory;
            float actual[68][4]={};HRESULT read=d->GetPixelShaderConstantF(0,actual[0],68);
            if(SUCCEEDED(read)){
                char suffix[80];std::snprintf(suffix,sizeof suffix,"/capture-%u-constants.f32",capture);
                if(FILE* f=std::fopen((directory+suffix).c_str(),"wb")){std::fwrite(actual,sizeof actual,1,f);std::fclose(f);}
            }
            logf("WORLD GPU diagnostic capture=%u map=%s eye=(%.3f %.3f %.3f) moon=(%.6f %.6f %.6f) constantsHRESULT=%08lx",capture,active->map.c_str(),context.camera[0],context.camera[1],context.camera[2],sourceDirections[1].x,sourceDirections[1].y,sourceDirections[1].z,(unsigned long)read);
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
        if(captureSampled)logf("VOLUME sources sunRGB=%.5f,%.5f,%.5f moonRGB=%.5f,%.5f,%.5f ambientRGB=%.5f,%.5f,%.5f sunGain=1.2 moonGain=1.2 directCaps=0.38,0.24 airCells=%u airDensity=%.5f,%.5f forestAir=%.4f night=%.3f",
            sourceColors[0].x,sourceColors[0].y,sourceColors[0].z,sourceColors[1].x,sourceColors[1].y,sourceColors[1].z,
            context.ambient[0],context.ambient[1],context.ambient[2],uploadedFogField?uploadedFogField->airCells:0,c[32][0],c[32][1],c[22][3],c[31][3]);
        if(profileSampled()){ /* hue candidates (RenderProfile): compare at Brill / Durotar coast / Orgrimmar with the Light DBC */
            const auto& g=glowHueFrame;float fogDirectRGB[3],fogAmbientRGB[3],liftRGB[3],slot[5][3];
            const float sunRGB[3]={sourceColors[0].x,sourceColors[0].y,sourceColors[0].z};NorthlightSunHue::fogDirect(sunRGB,g,fogDirectRGB);NorthlightSunHue::coolAmbient(c[18],g,sourceWeights[0],fogAmbientRGB);NorthlightSunHue::horizonLift(g,liftRGB);
            const unsigned slots[5]={NorthlightSunHue::SlotDirect,NorthlightSunHue::SlotFog,NorthlightSunHue::SlotSun,NorthlightSunHue::SlotSunHalo,NorthlightSunHue::SlotSkyAboveHorizon};
            for(unsigned i=0;i<5;++i)NorthlightSunHue::unpack(lightSlots[slots[i]],slot[i]);
            logf("SUNHUE valid=%d slots=%d native=%.3f strength=%.3f direct=%.3f,%.3f,%.3f legacyFog=%.3f,%.3f,%.3f band0=%.3f,%.3f,%.3f band7fog=%.3f,%.3f,%.3f band9sunGlare=%.3f,%.3f,%.3f band10sunHalo=%.3f,%.3f,%.3f slot6band5sky=%.3f,%.3f,%.3f "
                "H=%.3f,%.3f,%.3f core=%.3f,%.3f,%.3f moon=%.3f,%.3f,%.3f lift=%.3f,%.3f,%.3f fogDirect=%.3f,%.3f,%.3f fogAmbient=%.3f,%.3f,%.3f caps=%.2f,%.2f",
                int(g.valid),int(lightSlotsValid),g.native,g.strength,context.direct[0],context.direct[1],context.direct[2],legacyFog.color[0],legacyFog.color[1],legacyFog.color[2],
                slot[0][0],slot[0][1],slot[0][2],slot[1][0],slot[1][1],slot[1][2],slot[2][0],slot[2][1],slot[2][2],slot[3][0],slot[3][1],slot[3][2],slot[4][0],slot[4][1],slot[4][2],
                g.sun[0],g.sun[1],g.sun[2],g.sunCore[0],g.sunCore[1],g.sunCore[2],g.moon[0],g.moon[1],g.moon[2],liftRGB[0],liftRGB[1],liftRGB[2],
                fogDirectRGB[0],fogDirectRGB[1],fogDirectRGB[2],fogAmbientRGB[0],fogAmbientRGB[1],fogAmbientRGB[2],NorthlightSunHue::SunForwardCap,NorthlightSunHue::MoonForwardCap);
        }
        if(frames==1||frames%600==0){if(frames==1||NorthlightDiagnostics::enabled())logf("LOCAL direct lights=%u limit=%u nearestReach=%.1f visibilityEnd=%.1f fogDistance=%.1f strength=%.3f lampGain=%.3f sunlitCut=%.3f available=%zu scissor=%d batches=%u clipped=%u skipped=%u coverage=%.3f fading=%u selectUs=%.1f selectMaxUs=%.1f",localDirectCount,quality.localLightLimit,localDirectNearest,NorthlightLocalLightSelection::VisibilityEnd,localLights.fogDistance,c[52][1],lampGain,lampSunlitCut,active?active->localLights.size():size_t(0),
            int(NorthlightLocalLightScissor::Enabled),localScissorBatches,localScissorClipped,localScissorSkipped,localScissorBatches?localScissorCoverage/localScissorBatches:1.,
            localLightTracker.fading,localSelectFrames?localSelectUsSum/localSelectFrames:0.,localSelectUsMax);
            localScissorBatches=localScissorClipped=localScissorSkipped=0;localScissorCoverage=0;localSelectUsSum=localSelectUsMax=0;localSelectFrames=0;
            if(frames>1&&NorthlightDiagnostics::enabled())logf("GI probe blend publishes=%u blendedSlots=%u texture=%d",probeBlendPublishes,probeBlendSlots,int(probePrev!=nullptr));
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
        if(captureSampled)logf("WORLD terrain reuse=%u snapshots=%zu uploadBytes=%zu vertexUploadBytes=%zu reusedVertices=%zu arenaMiB=%.2f rollovers=%u change=%s newOwners=%u directionalBuilds=%u directionalTriangles=%zu straddlingTriangleTests=%zu collectVisited=%zu terrainMs=%.3f,%.3f,%.3f",unsigned(terrainUploadReused),frameTerrain.size(),terrainUploadBytes,liveTerrainGPU.uploadedBytes,liveTerrainGPU.reusedVertices,double(liveTerrainGPU.bytes())/1048576,liveTerrainGPU.rollovers,
            terrainChange,liveTerrainGPU.newOwners,liveTerrainGPU.directionalBuilds,liveTerrainGPU.directionalTriangles,terrainTriangleTests,liveTerrainGPU.collectVisited,terrainPartMs[0],terrainPartMs[1],terrainPartMs[2]);
        if(frames==1||(frames%600==0&&NorthlightDiagnostics::enabled()))logf("WORLD replay constants bytes=%zu previousBytes=%zu calls=%zu previousCalls=%zu",replayConstantBytes,replays.size()*2*(1024*4+16*4+64*4+16*4),replayConstantCalls,replays.size()*2*4);
        if(frames==1||frames%600==0){
            if(frames==1||NorthlightDiagnostics::enabled())logf("WORLD replay cull tested=%llu,%llu,%llu,%llu bounded=%llu,%llu,%llu,%llu culled=%llu,%llu,%llu,%llu (sun near,far moon near,far)",
                replaySlotTested[0],replaySlotTested[1],replaySlotTested[2],replaySlotTested[3],replaySlotBounded[0],replaySlotBounded[1],replaySlotBounded[2],replaySlotBounded[3],
                replaySlotCulled[0],replaySlotCulled[1],replaySlotCulled[2],replaySlotCulled[3]);
            for(unsigned n=0;n<4;++n)replaySlotTested[n]=replaySlotBounded[n]=replaySlotCulled[n]=0;}
        if(frames==1||(frames%600==0&&NorthlightDiagnostics::enabled()))logf("WORLD replay poses draws=%zu groups=%u prepared=%zu reused=%zu scope=directional exact=1",replays.size(),replays.empty()?0:replays.back()->constantGroup+1,replayPosePrepared,replayPoseReused);
        DWORD elapsed=GetTickCount()-submissionStart;
        if(elapsed>40&&slowReports++<12)logf("WORLD slow submission: %lu ms; replay=%zu liveTerrainChunks=%zu",(unsigned long)elapsed,replays.size(),liveTerrainChunks.size());
        shadowsComposited=effects.shadows&&(sourceActive[0]||sourceActive[1])&&debug==0;
        bucket(NorthlightEffectsBuckets::Composite); /* the world composite, temporal and the frame's logs */
        return true;
    }
};
