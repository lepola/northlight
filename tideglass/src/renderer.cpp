#include "math/matrix.h"
#include "renderer.h"
#include "scenery_receiver.h"

#include "gpu_timer.h"
#include "screen_pass.h"
#include "reflection_capture.h"
#include "reflection_bindings.h"

#include "log.h"
#include "ps_ao.h"
#include "ps_ao_blur.h"
#include "ps_fxaa.h"
#include "ps_level.h"
#include "ps_overlay.h"
#include "ps_probe.h"
#include "ps_puddle.h"
#include "ps_reflection_scenery.h"
#include "ps_slime_reflection.h"
#include "ps_noise_lattice.h"
#include "ps_surface_detail.h"
#include "ps_wake_map.h"
#include "ps_fill_pull.h"
#include "ps_fill_push.h"
#include "ps_ring.h"
#include "ps_ripple.h"
#include "ps_sky_memory.h"
#include "ps_sky_observed_air.h"
#include "ps_sky_memory_extend.h"
#include "ps_slime_capture.h"
#include "ps_slime_deferred.h"
#include "ps_slime_deferred_fine.h"
#include "ps_slime_undo.h"
#include "ps_slime_lift.h"
#include "ps_lava.h"
#include "ps_lava_stir.h"
#include "ps_lava_mask.h"
#include "ps_lava_halo.h"
#include "ps_lava_glow.h"
#include "ps_lava_bake_cells.h"
#include "ps_lava_bake_slope.h"
#include "ps_lava_bake_noise.h"
#include "ps_lava_bake_foam.h"
#include "ps_lava_bake_flow.h"
#include "ps_snapshot.h"
#include "ps_underwater.h"
#include "ps_underwater_foam.h"
#include "ps_underwater_shared.h"
#include "ps_underwater_scenery.h"
#include "ps_slime_underwater_scenery.h"
#include "ps_underwater_scene.h"
#include "ps_contact.h"
#include "ps_contact_spread.h"
#include "ps_water.h"
#include "ps_water_fine.h"
#include "ps_water_moments.h"
#include "ps_water_moments_fine.h"
#include "ps_surface_detail_moments.h"
#include "vs_fullscreen.h"
#include "vs_ring.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <string>

#include "d3d_resource.h"

using water::SafeRelease;
using screen_pass::BindPointTexture;
using screen_pass::BindLinearTexture;
using screen_pass::SetTarget;

namespace
{
constexpr DWORD kStages = 16;
constexpr UINT kFrameConstants = WATER_SLOT_cWaterTimeWavesFoamClarity;
constexpr UINT kPixelConstants = kFrameConstants + kWaterConstants;
// The puddles' own rows, after the water's (whose rows the puddles use too).
constexpr UINT kPuddleRegister = kPixelConstants;
constexpr UINT kPuddleConstants = 2;
// The ring map's row, after the puddles' (read by the puddles and the water).
constexpr UINT kRingMapRegister = WATER_SLOT_cRingMap;
// How the remembered sky's stage is laid out (see BindSky), after the ring map's row.
constexpr UINT kSkyLayoutRegister = WATER_SLOT_cSkyLayout;
// The reflection buffers' row (read by the water), after it.
constexpr UINT kReflectionRegister = WATER_SLOT_cReflectionGrid;
// The water's debug views' row (read by the water).
constexpr UINT kWaterDebugRegister = WATER_SLOT_cWaterDebug;
bool FilteredWaterMaterial(const Config& cfg, bool environmentAvailable)
{
    return environmentAvailable;
}

constexpr UINT kGameFogRegister = WATER_SLOT_cGameFog, kWaveMomentSamplesRegister = WATER_SLOT_cWaveMomentSamples, kSurfaceVisibilityRegister = WATER_SLOT_cSurfaceVisibility;
bool BindWaterControls(IDirect3DDevice9* dev, const Config& cfg, bool environmentAvailable, bool momentsAvailable = false, bool surfaceVisibilityAvailable = false, bool hasIndoorWater = false)
{
    const float controls[4] = {static_cast<float>(cfg.refractionDebug), cfg.waterSceneExposure,
                               (FilteredWaterMaterial(cfg, environmentAvailable) ? 4.f : 0.f) +
                                   (momentsAvailable && FilteredWaterMaterial(cfg, environmentAvailable) ? 8.f : 0.f),
                               1.f + cfg.openWaterCrestFoam};
    // c88: the game's own distance fog over the water, only where the host shades water distance: the host's haze
    // assumes every pixel already carries the game's fog, as the game's own surfaces do. Its own register: c29 is the
    // underwater window inside the water constants block, uploaded after these controls by some passes.
    const float gameFog[4] = {cfg.waterAtmosphere == 2 && cfg.waterGameFog ? 1.f : 0.f, 0.f, 0.f, 0.f};
    const float samples[4] = {cfg.waterWaveMomentSamples * 0.5f, cfg.waveCrestShape, 0.f, 0.f};
    const float visibility[4] = {surfaceVisibilityAvailable ? 1.f : 0.f, hasIndoorWater ? 1.f : 0.f, 0.f, 0.f};
    return SUCCEEDED(dev->SetPixelShaderConstantF(kWaterDebugRegister, controls, 1)) &&
           SUCCEEDED(dev->SetPixelShaderConstantF(kGameFogRegister, gameFog, 1)) &&
           SUCCEEDED(dev->SetPixelShaderConstantF(kWaveMomentSamplesRegister, samples, 1)) &&
           SUCCEEDED(dev->SetPixelShaderConstantF(kSurfaceVisibilityRegister, visibility, 1));
}

// x: the ships' places along their paths come from the wake map; y, z the ReflectionSurfaceFlat and ReflectionView
// diagnostics (water_common: cLiquidOptions). c49 and c50 are free.
constexpr UINT kLiquidOptionsRegister = WATER_SLOT_cLiquidOptions;
// Round each ship's path, the box its wake can reach (see ShipWakeBoxes), or the square its part of the wake map covers,
// a row each.
constexpr UINT kShipBoxRegister = WATER_SLOT_cShipBox;
constexpr UINT kReflectionOptionsRegister = WATER_SLOT_cReflectionOptions; // c55..56, reflection contract
constexpr DWORD kReflectionStage = reflection::kColourStage;
constexpr DWORD kReflectionHitStage = reflection::kHitStage;
// What lies under the water, filled in where something stands out of it (see DrawUnderwaterScene), and its thickness.
constexpr DWORD kUnderwaterSceneStage = WATER_SLOT_sUnderwaterScene;
constexpr DWORD kUnderwaterThicknessStage = WATER_SLOT_sUnderwaterThickness;
constexpr DWORD kRingMapStage = WATER_SLOT_sRingMap;
// The ring map: the rain rings from the game's splashes, as the surface slope they make, on a square around the camera
// out to where splashes are read.
constexpr UINT kRingMapCells = 2048;
constexpr float kRingMapArea = 2.0f * kSplashReach;
// A splash makes a ring only on water or ground within this height of it (not on a roof or a rock above the water).
constexpr float kRingSurfaceHeight = 0.75f;
// A ring's reach and width on the map (rain_rings.hlsli: kRingReach + kRingWidth).
constexpr float kRingExtent = 0.955f;
// Longer gaps than this (a loading screen, a pause) count as this long for the puddles' filling and drying.
constexpr double kLongestWetnessStep = 1.0;
constexpr UINT kRippleCells = 1024;
constexpr float kRippleCellSize = 0.15f;
constexpr float kRippleWaveSpeed = 2.8f;
constexpr float kRippleStepSeconds = 1.0f / 60.0f;
constexpr int kMaxRippleStepsPerFrame = 4;
constexpr float kRippleDamping = 0.99f;
// The foam where something stands in the water rises towards what is there now over about this long (seconds; see
// contact_spread_ps).
constexpr float kContactFoamRiseSeconds = 0.06f;
constexpr UINT kLavaHaloScale = 4;
// The least time between steps of the lava's stirring, and between looks for what stands in it (just short of a 60 Hz frame
// and of three of them, so 60 Hz steps every frame and finds what stands every third), and how fast that raises it (lava_stir_ps).
constexpr double kLavaStirStepSeconds = 1.0 / 75.0;
constexpr double kLavaContactSeconds = 1.0 / 24.0;
constexpr double kLavaContactRisePerSecond = 10.0;
constexpr UINT kRippleMoverRegister = 4;
constexpr UINT kRippleRadiusRegister = 20;
constexpr UINT kRippleAreaRegister = WATER_SLOT_cRippleArea;
// (Above the water's own: c88 and c89 are WaterGameFog and the wave filter's, which stay set between its passes.)
constexpr UINT kFarAreaRegister = 90;
constexpr float kShoreWaveAmplitude = 0.002f;
constexpr float kShoreWavesPerYard = 2.2f;
constexpr float kShoreWaveSpeed = 1.65f;
// Slime is thick: its waves spread slower than water's and die out sooner, and viscosity smooths away the small,
// fast ripples first so it wobbles rather than splashes. (Much slower waves would turn a swimmer's wake into a
// narrow, sharp chevron.)
constexpr float kSlimeDamping = 0.97f;
constexpr float kSlimeViscosity = 0.06f;
constexpr UINT kRippleViscosityRegister = 36;
constexpr UINT kBoatRow = WATER_SLOT_cBoat;
static_assert(kBoatRow - kFrameConstants + 2 * kMaxBoats == kWaterConstants, "the ships fill the last water constants");
// The ships' paths, a row of the texture each (see BoatPathTexture).
constexpr DWORD kBoatPathStage = WATER_SLOT_sBoatPaths;
// The water's composite and reflection pass read the surface detail pass's results on the same stage instead (see
// DrawSurfaceDetail).
constexpr DWORD kSurfaceDetailStage = WATER_SLOT_sSurfaceDetail;
// The noise lattice and the foam's wandering points, in the surface detail pass only (see DrawNoiseLattice).
constexpr DWORD kNoiseLatticeStage = WATER_SLOT_sNoiseLattice;
constexpr DWORD kFoamPointsStage = WATER_SLOT_sFoamPoints;
// The surface detail pass's second target (the slope foam lies on, WaveNormalOnce) for the composite.
constexpr DWORD kSurfaceSlopesStage = WATER_SLOT_sSurfaceSlopes;
// The wake map (see DrawShipWakeMap): its two targets' stages for the passes that work out the wakes, the points each
// side of a ship's square (water_common's kWakeMapSize too) and the least step between them (yards; wider only when a
// ship's box would not fit).
constexpr DWORD kWakeMapPlaceStage = WATER_SLOT_sWakeMapPlace;
constexpr DWORD kWakeMapWayStage = WATER_SLOT_sWakeMapWay;
constexpr UINT kWakeMapSize = 256;
constexpr float kWakeMapStep = 2.0f;
// A ship's hull for its wake: half its length and half its beam (yards).
constexpr float kHullHalfLength = 20.0f;
constexpr float kHullHalfBeam = 6.0f;
// How far ahead of the bow a ship's wake starts, and how far from its path it reaches (yards; water_common.hlsli:
// kWakeAhead, kPathReach).
constexpr float kWakeAhead = 7.0f;
constexpr float kWakePathReach = 150.0f;

// Round each ship's path (the wake's front ahead of the bow, the ship and the points behind it), the box its wake can
// reach: min x, min y, max x, max y. The water looks along a path only inside its box (ShipWakeBounds).
void ShipWakeBoxes(const FrameInputs& in, float (&boxes)[kMaxBoats][4])
{
    for (int i = 0; i < kMaxBoats; ++i)
    {
        float* box = boxes[i];
        box[0] = box[1] = 1.0e9f;
        box[2] = box[3] = -1.0e9f;
        if (i >= in.boatCount)
            continue;
        const Boat& boat = in.boats[i];
        auto take = [box](float x, float y) {
            box[0] = std::min(box[0], x);
            box[1] = std::min(box[1], y);
            box[2] = std::max(box[2], x);
            box[3] = std::max(box[3], y);
        };
        const float ahead = kHullHalfLength + kWakeAhead;
        take(boat.position[0] + boat.direction[0] * ahead, boat.position[1] + boat.direction[1] * ahead);
        take(boat.position[0], boat.position[1]);
        const int known = std::clamp(boat.trailCount, 1, kBoatTrailPoints);
        for (int point = 0; point < known; ++point)
            take(boat.trail[point].x, boat.trail[point].y);
        box[0] -= kWakePathReach;
        box[1] -= kWakePathReach;
        box[2] += kWakePathReach;
        box[3] += kWakePathReach;
    }
}
constexpr UINT kUnderwaterRegister = 23;
constexpr DWORD kUnderwaterFoamStage = 11;
constexpr UINT kUnderwaterWindowRegister = 29;
constexpr UINT kAbyssRegister = WATER_SLOT_cAbyss;
constexpr UINT kTwilightRegister = 30;
constexpr UINT kGroundRegister = WATER_SLOT_cGroundInfo;
constexpr UINT kSkySides = 2;
constexpr float kFoggiestFogEnd = 200.0f;
constexpr float kClearFogEnd = 800.0f;
constexpr float kOvercastFogTint = 0.35f;
constexpr float kOvercastDimming = 0.15f;
constexpr UINT kToLightRegister = WATER_SLOT_cToLight;
constexpr UINT kFogColorRegister = WATER_SLOT_cFogColor;
constexpr UINT kSunColorRegister = WATER_SLOT_cSunColor;
constexpr UINT kSkyRows = 8;
constexpr double kTimeWrapSeconds = 3600.0;
using water::kMoonLightScale;using water::kHorizonFadeBelow;using water::kHorizonFadeWidth;using water::kMoonTint;
constexpr float kMinViewportDepthExtent = 0.01f;
constexpr float kDeepestDepthInFullRangeViewport = 0.9999995f;
constexpr float kWorldDepthMargin = 2.0e-6f;
// Ambient occlusion: it fades out from half its distance to its distance from the camera, and it smooths only across
// pixels whose depths differ by less than this fraction of their distance.
constexpr float kOcclusionFadeStart = 0.5f;
constexpr float kOcclusionBlurDepthTolerance = 0.04f;
constexpr UINT kOcclusionRegister = 9;
constexpr double kSummarySeconds = 60.0;
constexpr double kProbeSeconds = 30.0;

const D3DVERTEXELEMENT9 kRingVertex[] = {
    {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    D3DDECL_END(),
};

const D3DVERTEXELEMENT9 kFullscreenVertex[] = {
    {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    D3DDECL_END(),
};

struct ProbeSample
{
    float world[3];
    float waterThickness;
};

long long Ticks()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

double SecondsSince(long long ticks)
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(Ticks() - ticks) / static_cast<double>(frequency.QuadPart);
}

using water::math::Invert4x4;

bool ViewToWorld(const FrameInputs& in, float* viewToWorld)
{
    if (!Invert4x4(in.cameraRelativeView, viewToWorld))
        return false;
    viewToWorld[12] = in.cameraPosition[0];
    viewToWorld[13] = in.cameraPosition[1];
    viewToWorld[14] = in.cameraPosition[2];
    return true;
}

float FarClipDistance(const float* proj)
{
    return proj[14] / (1.0f - proj[10]);
}

struct DepthMapping
{
    float atInfinity;
    float perInverseViewZ;
    float deepestWorldDepth;
};

DepthMapping MapDepth(const float* proj, const D3DVIEWPORT9& vp)
{
    const bool usable = vp.MaxZ - vp.MinZ > kMinViewportDepthExtent && vp.MinZ >= 0.0f && vp.MaxZ <= 1.0f;
    const float minZ = usable ? vp.MinZ : 0.0f;
    const float extent = usable ? vp.MaxZ - vp.MinZ : 1.0f;
    const float maxZ = minZ + extent;
    return {minZ + extent * (1.0f + proj[10]) * 0.5f, extent * proj[14] * 0.5f,
            maxZ >= kDeepestDepthInFullRangeViewport ? kDeepestDepthInFullRangeViewport : maxZ + kWorldDepthMargin};
}

// Passes over the world write colour only, leaving the alpha the client keeps in its render target.
constexpr DWORD kColorOnly = D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE;

// The world's viewport with the whole depth range (the client squeezes the world's depth into part of it).
D3DVIEWPORT9 FullDepth(const D3DVIEWPORT9& viewport)
{
    D3DVIEWPORT9 vp = viewport;
    vp.MinZ = 0.0f;
    vp.MaxZ = 1.0f;
    return vp;
}

void ColorToFloat(uint32_t argb, float scale, float* out)
{
    out[0] = ((argb >> 16) & 0xFF) / 255.0f * scale;
    out[1] = ((argb >> 8) & 0xFF) / 255.0f * scale;
    out[2] = (argb & 0xFF) / 255.0f * scale;
    out[3] = 1.0f;
}

bool CopyToTexture(IDirect3DDevice9* dev, IDirect3DSurface9* source, IDirect3DTexture9* texture)
{
    IDirect3DSurface9* surface = nullptr;
    bool ok = SUCCEEDED(texture->GetSurfaceLevel(0, &surface)) &&
              SUCCEEDED(dev->StretchRect(source, nullptr, surface, nullptr, D3DTEXF_NONE));
    if (surface)
        surface->Release();
    return ok;
}

const char* DebugViewName(DebugView view)
{
    return view == DebugView::WorldGrid ? "world grid" : view == DebugView::Depth ? "depth" : "off";
}
}

bool Renderer::Skip(const char* reason)
{
    m_skip = reason;
    return false;
}

bool Renderer::EnsurePixelShader(IDirect3DDevice9* dev, IDirect3DPixelShader9*& shader)
{
    if (shader) return true;
    // Each resource belongs to this renderer/device. Only the selected pass is
    // created; a failed optional program stays unavailable until Reset.
    const struct Entry { IDirect3DPixelShader9** slot; const ShaderProgram* program; } entries[] = {
        {&m_snapshotPs, &p_ps_snapshot},
        {&m_overlayPs, &p_ps_overlay},
        {&m_probePs, &p_ps_probe},
        {&m_waterPs, &p_ps_water},
        {&m_waterFinePs, &p_ps_water_fine},
        {&m_fxaaPs, &p_ps_fxaa},
        {&m_levelPs, &p_ps_level},
        {&m_skyMemoryPs, &p_ps_sky_memory},
        {&m_skyObservedAirPs, &p_ps_sky_observed_air},
        {&m_skyExtendPs, &p_ps_sky_memory_extend},
        {&m_underwaterPs, &p_ps_underwater},
        {&m_underwaterFoamPs, &p_ps_underwater_foam},
        {&m_underwaterSharedPs, &p_ps_underwater_shared},
        {&m_underwaterSceneryPs, &p_ps_underwater_scenery},
        {&m_slimeUnderwaterSceneryPs, &p_ps_slime_underwater_scenery},
        {&m_ripplePs, &p_ps_ripple},
        {&m_slimeCapturePs, &p_ps_slime_capture},
        {&m_slimeUndoPs, &p_ps_slime_undo},
        {&m_slimeLiftPs, &p_ps_slime_lift},
        {&m_slimeDeferredPs, &p_ps_slime_deferred},
        {&m_slimeDeferredFinePs, &p_ps_slime_deferred_fine},
        {&m_lava.surfacePs, &p_ps_lava},
        {&m_lava.stirPs, &p_ps_lava_stir},
        {&m_lava.maskPs, &p_ps_lava_mask},
        {&m_lava.haloPs, &p_ps_lava_halo},
        {&m_lava.glowPs, &p_ps_lava_glow},
        {&m_lava.bakeCellsPs, &p_ps_lava_bake_cells},
        {&m_lava.bakeSlopePs, &p_ps_lava_bake_slope},
        {&m_lava.bakeNoisePs, &p_ps_lava_bake_noise},
        {&m_lava.bakeFoamPs, &p_ps_lava_bake_foam},
        {&m_lava.bakeFlowPs, &p_ps_lava_bake_flow},
        {&m_aoPs, &p_ps_ao},
        {&m_aoBlurPs, &p_ps_ao_blur},
        {&m_puddlePs, &p_ps_puddle},
        {&m_ringPs, &p_ps_ring},
        {&m_surfaceDetailPs, &p_ps_surface_detail},
        {&m_noiseLatticePs, &p_ps_noise_lattice},
        {&m_wakeMapPs, &p_ps_wake_map},
        {&m_fillPullPs, &p_ps_fill_pull},
        {&m_fillPushPs, &p_ps_fill_push},
        {&m_underwaterScenePs, &p_ps_underwater_scene},
        {&m_contactPs, &p_ps_contact},
        {&m_contactSpreadPs, &p_ps_contact_spread},
        {&m_waterMomentsPs, &p_ps_water_moments},
        {&m_waterMomentsFinePs, &p_ps_water_moments_fine},
        {&m_reflectionSceneryPs, &p_ps_reflection_scenery},
        {&m_slimeReflectionPs, &p_ps_slime_reflection},
        {&m_surfaceMomentsPs, &p_ps_surface_detail_moments},
    };
    for (const auto& entry : entries) {
        if (entry.slot != &shader) continue;
        if (std::find(m_failedShaders.begin(), m_failedShaders.end(), entry.program) != m_failedShaders.end()) return false;
        const HRESULT result = entry.program->Create(dev, &shader);
        if (SUCCEEDED(result)) return true;
        D3DCAPS9 caps = {}; dev->GetDeviceCaps(&caps);
        const auto& required = entry.program->requirements;
        WATER_LOG_ERROR("shader %s unavailable (0x%08lx): needs %u temporaries, %u sampler registers, %u slots; device pixel slots %lu; retry after Reset",
                        entry.program->name, static_cast<unsigned long>(result), required.temporaries,
                        required.samplerRegisters, required.instructionSlots, static_cast<unsigned long>(caps.MaxPixelShader30InstructionSlots));
        m_failedShaders.push_back(entry.program);
        return false;
    }
    WATER_LOG_ERROR("unregistered renderer shader resource");
    return false;
}

HRESULT Renderer::BindPixelShader(IDirect3DDevice9* dev, IDirect3DPixelShader9*& shader)
{
    return EnsurePixelShader(dev, shader) ? dev->SetPixelShader(shader) : D3DERR_NOTAVAILABLE;
}

HRESULT Renderer::BindHighlightShader(IDirect3DDevice9* dev, const Config& cfg, IDirect3DPixelShader9*& standard, IDirect3DPixelShader9*& fine)
{
    if (cfg.fineHighlights && cfg.specular > 0 && SUCCEEDED(BindPixelShader(dev, fine))) return D3D_OK;
    return BindPixelShader(dev, standard);
}

void Renderer::PrepareUnderwaterShaders(IDirect3DDevice9* dev, const Config& cfg)
{
    if (!cfg.enable || !cfg.underwater) return;
    const bool scenery = cfg.underwaterScenery && cfg.screenReflections;
    const bool foam = cfg.sharedUnderwaterFoam || scenery;
    if (foam) EnsurePixelShader(dev, m_underwaterFoamPs);
    EnsurePixelShader(dev, scenery ? m_underwaterSceneryPs : (foam ? m_underwaterSharedPs : m_underwaterPs));
}

bool Renderer::EnsureShaders(IDirect3DDevice9* dev)
{
    if (m_vs && m_decl && m_snapshotPs) return true;
    const bool ok = (m_vs || SUCCEEDED(p_vs_fullscreen.Create(dev, &m_vs))) &&
                    (m_decl || SUCCEEDED(dev->CreateVertexDeclaration(kFullscreenVertex, &m_decl))) &&
                    EnsurePixelShader(dev, m_snapshotPs);
    return ok || Skip("shared fullscreen/depth shader unavailable");
}

bool Renderer::EnsureStateBlock(IDirect3DDevice9* dev)
{
    return EnsureScreenPassStateBlock(dev, m_state) || Skip("state block creation failed");
}

bool Renderer::EnsureSnapshotTarget(IDirect3DDevice9* dev, UINT width, UINT height)
{
    if (m_snapshot && m_snapshotWidth == width && m_snapshotHeight == height)
        return true;
    SafeRelease(m_snapshot);
    if (FAILED(dev->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &m_snapshot,
                                  nullptr)))
        return Skip("depth snapshot target creation failed");
    m_snapshotWidth = width;
    m_snapshotHeight = height;
    return true;
}

bool Renderer::EnsureSceneTargets(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc)
{
    if (m_sceneBefore && m_sceneDesc.Width == desc.Width && m_sceneDesc.Height == desc.Height &&
        m_sceneDesc.Format == desc.Format) {
        m_sceneDesc = desc;
        return true;
    }
    SafeRelease(m_sceneBefore);
    SafeRelease(m_sceneWithWater);
    if (FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT,
                                  &m_sceneBefore, nullptr)) ||
        FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT,
                                  &m_sceneWithWater, nullptr)))
    {
        SafeRelease(m_sceneBefore);
        SafeRelease(m_sceneWithWater);
        m_sceneDesc = {};
        return Skip("scene copy targets could not be created");
    }
    m_sceneDesc = desc;
    return true;
}

bool Renderer::BuildFrameConstants(const FrameInputs& in, const Config& cfg, const D3DSURFACE_DESC& depthDesc,
                                   float (&constants)[9][4])
{
    const D3DVIEWPORT9& vp = in.viewport;
    if (vp.Width < 16 || vp.Height < 16 || vp.X + vp.Width > depthDesc.Width || vp.Y + vp.Height > depthDesc.Height)
        return Skip("world viewport outside the depth buffer");
    float viewToWorld[16];
    if (!ViewToWorld(in, viewToWorld))
        return Skip("view matrix not invertible");
    const float* proj = in.glProjection;
    const DepthMapping depth = MapDepth(proj, vp);
    const float frame[9][4] = {
        {static_cast<float>(vp.X), static_cast<float>(vp.Y), static_cast<float>(vp.Width),
         static_cast<float>(vp.Height)},
        {1.0f / depthDesc.Width, 1.0f / depthDesc.Height, cfg.gridSpacing, cfg.overlayOpacity},
        {proj[0], proj[5], proj[8], proj[9]},
        {depth.atInfinity, depth.perInverseViewZ, depth.deepestWorldDepth, m_snapshotValid ? 1.0f : 0.0f},
        {},
        {},
        {},
        {},
        // w is always 1: the shaders' Unfolded() multiplies by it (see common.hlsli).
        {static_cast<float>(cfg.debugView), vp.X + vp.Width * 0.5f, vp.Y + vp.Height * 0.5f, 1.0f},
    };
    std::memcpy(constants, frame, sizeof(frame));
    std::memcpy(constants[4], viewToWorld, sizeof(viewToWorld));
    return true;
}

bool Renderer::EnsureMemoryTargets(IDirect3DDevice9* dev, IDirect3DTexture9* (&targets)[2], UINT width, UINT height,
                                   D3DFORMAT format)
{
    if(targets[0]&&targets[1])return true;
    for(auto*& target:targets){
        SafeRelease(target);
        const D3DVIEWPORT9 vp={0,0,width,height,0,1};
        if(FAILED(dev->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&target,nullptr))||
           !SetTarget(dev,target)||FAILED(dev->SetViewport(&vp))||FAILED(dev->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0))){
            SafeRelease(targets[0]);SafeRelease(targets[1]);return false;
        }
    }
    return true;
}

// From the finished image (after the fog), so the sky is remembered as it is seen, fog glow and all.
void Renderer::RememberSky(IDirect3DDevice9* dev, IDirect3DTexture9* depth, IDirect3DTexture9* finished, const FrameInputs& in,
                           const Config& cfg, bool seesAboveHorizon)
{
    if (!EnsureMemoryTargets(dev, m_sky, kSkySides, kSkyRows))
        return;
    ++m_work[5];
    const int next = m_skyIndex ^ 1;
    if(!SetTarget(dev, m_sky[next]))return;
    D3DVIEWPORT9 vp = {0, 0, kSkySides, kSkyRows, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    const float horizon[]={m_level[0]?1.f:0.f,1.f/std::max(FarClipDistance(in.glProjection),1.f),0,0};
    dev->SetPixelShaderConstantF(46,horizon,1);
    BindPointTexture(dev,4,m_level[0]?m_level[m_levelIndex]:nullptr);
    BindPointTexture(dev,0,depth);
    BindPointTexture(dev, 1, m_snapshot);
    BindPointTexture(dev, 2, finished);
    BindPointTexture(dev, 3, m_sky[m_skyIndex]);
    const DWORD now = GetTickCount();
    bool observeAir = false;
    if (cfg.underwater && seesAboveHorizon && m_surfaceResult == SurfaceResult::Shaded &&
        !in.cameraInWater && !in.cameraInSlime && in.mapId >= 0 && (!m_airTick || now - m_airTick >= 200))
    {
        m_airTick = now;
        m_airMap = -1;
        if (!m_observedAir)
            dev->CreateTexture(kSkySides, kSkyRows, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F,
                               D3DPOOL_DEFAULT, &m_observedAir, nullptr);
        observeAir = m_observedAir && SUCCEEDED(BindPixelShader(dev, m_skyObservedAirPs)) &&
                     SUCCEEDED(dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF)) && SetTarget(dev, m_observedAir, 1);
    }
    if (!observeAir && FAILED(BindPixelShader(dev, m_skyMemoryPs))) return;
    const std::string capture=!m_reflectionCaptureDirectory.empty() ? m_reflectionCaptureDirectory+"/sky-memory" : "";
    const bool captured=!capture.empty() && reflection::CapturePassInputs(dev,capture,m_snapshotPs,ActiveCaptureBurst());
    const bool drawn = SUCCEEDED(DrawScreenTriangle(dev));
    if (observeAir)
    {
        dev->SetRenderTarget(1, nullptr);
        m_airMap = drawn ? in.mapId : -1;
        std::copy_n(in.cameraPosition, 3, m_airPosition);
        std::copy_n(in.toLight, 3, m_airLight);
        m_airWeather = in.weatherType;
        m_airIntensity = in.weatherIntensity;
    }
    if (captured) reflection::CapturePassOutputs(dev,capture,"output",ActiveCaptureBurst());
    m_skyIndex = next;
    // Extend missing rows from the observed sky: reuse the previous memory target after the update has finished.
    // Unbind it before targeting it; only the updated memory is read here.
    for (DWORD stage=0;stage<kStages;++stage) dev->SetTexture(stage,nullptr);
    if(!SetTarget(dev,m_sky[next^1]))return;
    BindPointTexture(dev,3,m_sky[next]);
    if (FAILED(BindPixelShader(dev, m_skyExtendPs))) return;
    if (SUCCEEDED(DrawScreenTriangle(dev))) m_skyIndex=next^1;
}

float Renderer::ObservedAirWeight(const FrameInputs& in) const
{
    if (!m_observedAir || m_airMap < 0 || in.mapId != m_airMap || in.weatherType != m_airWeather)
        return 0;
    float distance2 = 0, lightChange2 = 0;
    for (unsigned i = 0; i < 3; ++i)
    {
        const float moved = in.cameraPosition[i] - m_airPosition[i], turned = in.toLight[i] - m_airLight[i];
        distance2 += moved * moved;
        lightChange2 += turned * turned;
    }
    // The observed glow belongs to the crossing. Ease it out during long swims or a change of sky.
    const float age = (GetTickCount() - m_airTick) * 0.001f;
    return std::clamp(std::min({(120.f - age) / 60.f, (96.f - std::sqrt(distance2)) / 48.f,
        (0.15f - std::sqrt(lightChange2)) / 0.10f, (0.3f - std::abs(in.weatherIntensity - m_airIntensity)) / 0.2f}), 0.f, 1.f);
}

// Bind the accepted environment when available, otherwise the ordinary sky memory.
bool Renderer::BindSky(IDirect3DDevice9* dev, DWORD stage, const Config& cfg, float reflectedFogWeight)
{
    if(m_usingEnvironment&&m_environment){
        const auto& values=m_environment->Values();
        float controls[8][4]={{cfg.waterAtmosphere==1?1.f:0.f,m_environment->Background()?1.f:0.f,values.referenceDistance,std::log(values.transmissionAtReference)},
            {values.light.angularRadius,m_environmentRayStep,0,cfg.waterHorizonBlend?1.f:0.f}};
        const unsigned glare=values.light.moon?WaterBackgroundMoonGlare:WaterBackgroundSunGlare;
        controls[1][2]=m_environment->Background()&&(values.backgroundContent&glare)?1.f:0.f;
        for(unsigned i=0;i<6;++i)ColorToFloat(values.palette[i],1,controls[i+2]);
        // The reflected water-to-scenery leg belongs to the material with either owner of camera distance.
        const float layout[4]={2,reflectedFogWeight,0,0};
        return SUCCEEDED(dev->SetPixelShaderConstantF(WATER_SLOT_cEnvironment,&controls[0][0],8))&&
            SUCCEEDED(dev->SetPixelShaderConstantF(kSkyLayoutRegister,layout,1))&&
            BindLinearTexture(dev,stage,m_environment->Background());
    }
    const bool bound=BindPointTexture(dev,stage,m_sky[0]?m_sky[m_skyIndex]:nullptr);
    const float layout[4] = {0.0f, 0.0f, 0.0f, m_sky[0] ? 0.0f : -1.0f};
    return SUCCEEDED(dev->SetPixelShaderConstantF(kSkyLayoutRegister,layout,1))&&bound;
}

bool Renderer::MeasureLevel(IDirect3DDevice9* dev, IDirect3DTexture9* surfaceDepth, IDirect3DTexture9* (&levels)[2],
                            int& index)
{
    if (!EnsureMemoryTargets(dev, levels, 1))
        return false;
    const int next = index ^ 1;
    if(!SetTarget(dev, levels[next]))return false;
    D3DVIEWPORT9 vp = {0, 0, 1, 1, 0.0f, 1.0f};
    if(FAILED(dev->SetViewport(&vp)))return false;
    if(FAILED(BindPixelShader(dev, m_levelPs)))return false;
    if(!BindPointTexture(dev, 0, surfaceDepth))return false;
    if(!BindPointTexture(dev, 1, m_snapshot))return false;
    if(!BindPointTexture(dev, 2, levels[index]))return false;
    if(FAILED(DrawScreenTriangle(dev)))return false;
    index = next;
    return true;
}

Renderer::SurfacePhysics Renderer::WaterPhysics(const Config& cfg)
{
    return {kRippleWaveSpeed, kRippleDamping, cfg.shoreWaves, true, 0.0f, 1.0f};
}

Renderer::SurfacePhysics Renderer::SlimePhysics(const Config& cfg)
{
    // Slime lies over building floors as often as terrain, so it does not use the ground map: its edge glow uses the
    // depth on screen, and it has no shore waves.
    return {cfg.slimeRippleSpeed, kSlimeDamping, 0.0f, false, kSlimeViscosity, cfg.slimeStyle.rippleResponse};
}

bool Renderer::EnsureRippleTargets(IDirect3DDevice9* dev, SurfaceSim& sim)
{
    if (sim.ripples[0] && sim.ripples[1])
        return true;
    for (auto** target : {&sim.ripples[0], &sim.ripples[1]})
        if (!*target && FAILED(dev->CreateTexture(kRippleCells, kRippleCells, 1, D3DUSAGE_RENDERTARGET,
                                                  D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, target, nullptr)))
        {
            ReleaseSurface(sim);
            return Skip("ripple targets could not be created");
        }
    sim.valid = false;
    return true;
}

void Renderer::ReleaseSurface(SurfaceSim& sim)
{
    for (auto** target : {&sim.ripples[0], &sim.ripples[1]})
        SafeRelease(*target);
    sim.valid = false;
}

// The ships' paths for their wakes, a row each (in the order of the ship constants): each point where the ship was,
// how long ago (seconds) and how fast it was going, its position first. Past the known path the last point repeats
// (the wake fades out before the path ends). A texture rather than constants, so the shaders can walk a path in a loop.
IDirect3DTexture9* Renderer::BoatPathTexture(IDirect3DDevice9* dev, const FrameInputs& in)
{
    if (!m_boatPaths && FAILED(dev->CreateTexture(kBoatTrailPoints, kMaxBoats, 1, 0, D3DFMT_A32B32G32R32F,
                                                  D3DPOOL_MANAGED, &m_boatPaths, nullptr)))
    {
        Skip("ship path texture could not be created");
        return nullptr;
    }
    D3DLOCKED_RECT locked = {};
    if (FAILED(m_boatPaths->LockRect(0, &locked, nullptr, 0)))
        return nullptr;
    for (int i = 0; i < kMaxBoats; ++i)
    {
        float* row = reinterpret_cast<float*>(static_cast<char*>(locked.pBits) + i * locked.Pitch);
        const bool sailing = i < in.boatCount;
        const int known = sailing ? std::clamp(in.boats[i].trailCount, 1, kBoatTrailPoints) : 1;
        for (int point = 0; point < kBoatTrailPoints; ++point)
        {
            const WakePoint from = sailing ? in.boats[i].trail[std::min(point, known - 1)] : WakePoint{};
            row[4 * point] = from.x;
            row[4 * point + 1] = from.y;
            row[4 * point + 2] = from.age;
            row[4 * point + 3] = from.speed;
        }
    }
    m_boatPaths->UnlockRect(0);
    return m_boatPaths;
}

IDirect3DTexture9* Renderer::GroundTexture(IDirect3DDevice9* dev, const GroundMap* ground)
{
    if (!ground)
        return nullptr;
    if (!m_ground && FAILED(dev->CreateTexture(GroundMap::kCells, GroundMap::kCells, 1, 0, D3DFMT_A32B32G32R32F,
                                               D3DPOOL_MANAGED, &m_ground, nullptr)))
    {
        Skip("ground texture could not be created");
        return nullptr;
    }
    if (!m_shore && FAILED(dev->CreateTexture(GroundMap::kCells, GroundMap::kCells, 1, 0, D3DFMT_G32R32F, D3DPOOL_MANAGED,
                                              &m_shore, nullptr)))
    {
        Skip("shore distance texture could not be created");
        return nullptr;
    }
    if (ground->Identity() == m_groundSource && ground->Revision() == m_groundRevision)
        return m_ground;
    D3DLOCKED_RECT shore = {};
    if (SUCCEEDED(m_shore->LockRect(0, &shore, nullptr, 0)))
    {
        // r: how far water is from the land. g: how far the land is from the water.
        const float* water = ground->ShoreDistances();
        const float* land = ground->LandDistances();
        for (int y = 0; y < GroundMap::kCells; ++y)
        {
            float* row = reinterpret_cast<float*>(static_cast<char*>(shore.pBits) + y * shore.Pitch);
            for (int x = 0; x < GroundMap::kCells; ++x)
            {
                row[2 * x] = water[y * GroundMap::kCells + x];
                row[2 * x + 1] = land[y * GroundMap::kCells + x];
            }
        }
        m_shore->UnlockRect(0);
    }
    D3DLOCKED_RECT locked = {};
    if (FAILED(m_ground->LockRect(0, &locked, nullptr, 0)))
        return nullptr;
    if(const auto* packed=ground->PackedTexture()){
        for(int y=0;y<GroundMap::kCells;++y)
            std::memcpy(static_cast<char*>(locked.pBits)+y*locked.Pitch,
                packed+size_t(y)*GroundMap::kCells*4,size_t(GroundMap::kCells)*4*sizeof(float));
    }else {
    const float* heights = ground->Heights();
    // The nearest water's height everywhere (so the surface carries on at it where the world gives no height), and
    // how far the world's own height there is trusted.
    const float* liquids = ground->NearestLiquidHeights();
    const float fallback = ground->NearbyWaterKnown() ? ground->NearbyWater() : 0.0f;
    const float* trust = ground->LiquidTrust();
    for (int y = 0; y < GroundMap::kCells; ++y)
    {
        float* row = reinterpret_cast<float*>(static_cast<char*>(locked.pBits) + y * locked.Pitch);
        for (int x = 0; x < GroundMap::kCells; ++x)
        {
            const float h = heights[y * GroundMap::kCells + x];
            const float l = liquids[y * GroundMap::kCells + x];
            row[4 * x] = h > GroundMap::kUnknown ? h : 0.0f;
            row[4 * x + 1] = h > GroundMap::kUnknown ? 1.0f : 0.0f;
            row[4 * x + 2] = l > GroundMap::kUnknown ? l : fallback;
            row[4 * x + 3] = trust[y * GroundMap::kCells + x];
        }
    }
    }
    m_ground->UnlockRect(0);
    m_groundSource = ground->Identity();
    m_groundRevision = ground->Revision();
    return m_ground;
}

// The far ground map for the lava's glow. r: the ground's height; g: 1 where known; b: the liquid's; a: how far the land is from lava.
IDirect3DTexture9* Renderer::FarGroundTexture(IDirect3DDevice9* dev, const GroundMap* ground)
{
    if (!ground)
        return nullptr;
    if (!m_farGround && FAILED(dev->CreateTexture(GroundMap::kCells, GroundMap::kCells, 1, 0, D3DFMT_A32B32G32R32F, D3DPOOL_MANAGED, &m_farGround, nullptr)))
    {
        Skip("far ground texture could not be created");
        return nullptr;
    }
    if (ground->Identity() == m_farGroundSource && ground->Revision() == m_farGroundRevision)
        return m_farGround;
    const float* land = ground->LandDistances();
    const float* heights = ground->Heights();
    const float* liquids = ground->NearestLiquidHeights();
    D3DLOCKED_RECT locked = {};
    if (FAILED(m_farGround->LockRect(0, &locked, nullptr, 0)))
        return nullptr;
    for (int y = 0; y < GroundMap::kCells; ++y)
    {
        float* row = reinterpret_cast<float*>(static_cast<char*>(locked.pBits) + y * locked.Pitch);
        for (int x = 0; x < GroundMap::kCells; ++x)
        {
            const int i = y * GroundMap::kCells + x;
            row[4 * x] = heights[i] > GroundMap::kUnknown ? heights[i] : 0.0f;
            row[4 * x + 1] = heights[i] > GroundMap::kUnknown ? 1.0f : 0.0f;
            row[4 * x + 2] = liquids[i] > GroundMap::kUnknown ? liquids[i] : 0.0f;
            row[4 * x + 3] = land[i];
        }
    }
    m_farGround->UnlockRect(0);
    m_farGroundSource = ground->Identity();
    m_farGroundRevision = ground->Revision();
    return m_farGround;
}

bool Renderer::AdvanceSurface(IDirect3DDevice9* dev, SurfaceSim& sim, IDirect3DTexture9* level,
                              const SurfacePhysics& physics, const FrameInputs& in, const Config& cfg,
                              const float (&frame)[9][4], float (&constants)[kWaterConstants][4])
{
    if(!cfg.ripples)return true;
    if(!EnsureRippleTargets(dev,sim))return false;
    float* rippleArea = constants[kRippleAreaRegister - kFrameConstants];
    if (sim.advancedThisFrame)
    {
        rippleArea[0] = sim.origin[0];
        rippleArea[1] = sim.origin[1];
        rippleArea[3] = cfg.rippleStrength * physics.rippleResponse;
        return true;
    }
    // A failed GPU step may have changed either buffer. Never reuse that state,
    // including a retry before EndFrame; initialize both buffers on the next call.
    struct SimulationUpdate {
        SurfaceSim& sim;
        bool complete=false;
        ~SimulationUpdate(){if(!complete){sim.valid=false;sim.advancedThisFrame=false;}}
    } update{sim};
    const long long now = Ticks();
    // The harness sets the clock itself (testSeconds): the simulation goes on by as much as that clock moved, none while
    // it is held.
    const double frameSeconds = cfg.testSeconds >= 0.0f
                                    ? (sim.lastTestSeconds >= 0.0f ? std::max(cfg.testSeconds - sim.lastTestSeconds, 0.0f)
                                                                   : 0.0)
                                : sim.lastTicks ? SecondsSince(sim.lastTicks)
                                                : 0.0;
    sim.lastTestSeconds = cfg.testSeconds;
    sim.backlog += frameSeconds;
    sim.lastTicks = now;
    // The same square as the ground map, so the two line up cell for cell.
    static_assert(kRippleCells * kRippleCellSize == GroundMap::kArea, "the ripple grid covers the ground map");
    const float origin[2] = {in.ground ? in.ground->Origin()[0] : GroundMap::OriginFor(in.cameraPosition[0]),
                             in.ground ? in.ground->Origin()[1] : GroundMap::OriginFor(in.cameraPosition[1])};
    float shift[2] = {(origin[0] - sim.origin[0]) / kRippleCellSize,
                      (origin[1] - sim.origin[1]) / kRippleCellSize};
    const D3DVIEWPORT9 grid = {0, 0, kRippleCells, kRippleCells, 0.0f, 1.0f};
    if (!sim.valid || std::fabs(shift[0]) >= kRippleCells || std::fabs(shift[1]) >= kRippleCells)
    {
        for (auto* target : {sim.ripples[0], sim.ripples[1]})
        {
            if(!SetTarget(dev, target))return false;
            if(FAILED(dev->SetViewport(&grid)))return false;
            if(FAILED(dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0)))return false;
        }
        shift[0] = shift[1] = 0.0f;
        sim.valid = true;
        sim.backlog = 0.0;
    }
    sim.origin[0] = origin[0];
    sim.origin[1] = origin[1];
    rippleArea[0] = origin[0];
    rippleArea[1] = origin[1];
    rippleArea[3] = cfg.rippleStrength * physics.rippleResponse;

    IDirect3DTexture9* seabed = physics.usesGround ? GroundTexture(dev, in.ground) : nullptr;
    if(FAILED(dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(kFrameConstants, &constants[0][0], kWaterConstants)))return false;

    int steps = static_cast<int>(sim.backlog / kRippleStepSeconds);
    sim.backlog -= steps * kRippleStepSeconds;
    steps = std::min(steps, kMaxRippleStepsPerFrame);
    const bool pulsePending = std::any_of(in.movers, in.movers + in.moverCount,
                                          [](const Mover& mover) { return !mover.continuous; });
    // Grid transport and one-shot impulses do not spend simulation time. Borrowing
    // a future tick here made the backlog negative at high frame rates; a later
    // negative step count then moved the origin without shifting the history.
    const bool transportOnly = steps == 0;
    const int passes = steps ? steps : (shift[0] != 0.0f || shift[1] != 0.0f || pulsePending ? 1 : 0);

    const float courant = std::pow(physics.waveSpeed * kRippleStepSeconds / kRippleCellSize, 2.0f);
    float movers[kMaxMovers][4] = {};
    float radii[kMaxMovers][4] = {};
    for (int i = 0; i < in.moverCount; ++i)
    {
        const auto& mover=in.movers[i];
        std::memcpy(movers[i], mover.position, sizeof(mover.position));
        movers[i][3] = mover.strength;
        radii[i][0] = mover.radius;
        radii[i][1] = mover.continuous ? 1.0f : 0.0f;
    }
    if(FAILED(BindPixelShader(dev, m_ripplePs)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(kRippleMoverRegister, &movers[0][0], kMaxMovers)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(kRippleRadiusRegister, &radii[0][0], kMaxMovers)))return false;
    const float firstStep[4] = {static_cast<float>(in.moverCount), 1.0f, transportOnly ? 1.0f : 0.0f, 0.0f};
    const float laterSteps[4] = {static_cast<float>(in.moverCount), 0.0f, 0.0f, 0.0f};
    if(FAILED(dev->SetPixelShaderConstantF(2, firstStep, 1)))return false;
    const float gridConstants[4] = {1.0f / kRippleCells, courant, physics.damping, kRippleCellSize};
    if(FAILED(dev->SetPixelShaderConstantF(0, gridConstants, 1)))return false;
    const float viscosity[4] = {physics.viscosity, 0.0f, 0.0f, 0.0f};
    if(FAILED(dev->SetPixelShaderConstantF(kRippleViscosityRegister, viscosity, 1)))return false;
    if(!BindPointTexture(dev, 1, level))return false;
    if(!BindPointTexture(dev, 2, seabed))return false;
    for (int step = 0; step < passes; ++step)
    {
        const float seconds = LiquidSeconds(cfg) + step * kRippleStepSeconds;
        const float shore[4] = {seconds, kShoreWaveAmplitude * physics.shoreWaves, kShoreWavesPerYard, kShoreWaveSpeed};
        if(FAILED(dev->SetPixelShaderConstantF(3, shore, 1)))return false;
        const float originConstants[4] = {origin[0], origin[1], step == 0 ? shift[0] : 0.0f,
                                          step == 0 ? shift[1] : 0.0f};
        if(FAILED(dev->SetPixelShaderConstantF(1, originConstants, 1)))return false;
        const int next = sim.rippleIndex ^ 1;
        if(!SetTarget(dev, sim.ripples[next]))return false;
        if(FAILED(dev->SetViewport(&grid)))return false;
        if(!BindPointTexture(dev, 0, sim.ripples[sim.rippleIndex]))return false;
        if(FAILED(DrawScreenTriangle(dev)))return false;
        sim.rippleIndex = next;
        if(FAILED(dev->SetPixelShaderConstantF(2, laterSteps, 1)))return false;
    }
    if(physics.usesGround&&!MarkContacts(dev,sim,level,seabed,static_cast<float>(frameSeconds),frame,constants))return false;
    sim.advancedThisFrame=true;
    update.complete=true;
    return true;
}

// The foam where something stands in the water, on the ripple grid: where what is on screen crosses the water's
// surface this frame (contact_ps), spread by the distance from the nearest of them across the grid one way and then the
// other (contact_spread_ps), the second way into the grid's state, keeping the foam already there where it is more (it
// fades in the ripple steps). Leaves the render target, viewport, pixel shader, stages 0 to 7 and the frame and water
// constants changed.
// Where something crosses the liquid's surface (contact_ps), spread one way across the grid (contact_spread_ps), into
// m_contactMarks; the caller takes it the other way.
bool Renderer::SpreadContactMarks(IDirect3DDevice9* dev, IDirect3DTexture9* level, IDirect3DTexture9* seabed,
                                  const float (&frame)[9][4], const float (&constants)[kWaterConstants][4])
{
    for (auto** target : {&m_contactMarks, &m_contactSpread})
        if (!*target && FAILED(dev->CreateTexture(kRippleCells, kRippleCells, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R16F,
                                                  D3DPOOL_DEFAULT, target, nullptr)))
        {
            Skip("contact foam targets could not be created");
            return false;
        }
    const D3DVIEWPORT9 grid = {0, 0, kRippleCells, kRippleCells, 0.0f, 1.0f};
    if(!SetTarget(dev, m_contactMarks))return false;
    if(FAILED(dev->SetViewport(&grid)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(kFrameConstants, &constants[0][0], kWaterConstants)))return false;
    if(FAILED(BindPixelShader(dev, m_contactPs)))return false;
    if(!BindPointTexture(dev, 1, m_snapshot))return false;
    if(!BindPointTexture(dev, 4, level))return false;
    if(!BindLinearTexture(dev, 7, seabed))return false;
    if(FAILED(DrawScreenTriangle(dev)))return false;

    const float cell = 1.0f / kRippleCells;
    // Which pass each is (contact_spread_ps: cSpread.z).
    const float count[4] = {0.0f, 0.0f, 0.0f, cell};
    const float across[4] = {cell, 0.0f, 1.0f, cell};
    // The crossings counted by their neighbours, into the second target; the nearest of them one way, back into the
    // first (the marks are done with); then the other way, into the grid.
    if(FAILED(BindPixelShader(dev, m_contactSpreadPs)))return false;
    if(!SetTarget(dev, m_contactSpread))return false;
    if(FAILED(dev->SetPixelShaderConstantF(0, count, 1)))return false;
    if(!BindPointTexture(dev, 0, m_contactMarks))return false;
    if(!BindPointTexture(dev, 1, nullptr))return false;
    if(!BindPointTexture(dev, 2, nullptr))return false;
    if(FAILED(DrawScreenTriangle(dev)))return false;
    if(!SetTarget(dev, m_contactMarks))return false;
    if(FAILED(dev->SetPixelShaderConstantF(0, across, 1)))return false;
    if(!BindPointTexture(dev, 0, m_contactSpread))return false;
    return SUCCEEDED(DrawScreenTriangle(dev));
}

bool Renderer::MarkContacts(IDirect3DDevice9* dev, SurfaceSim& sim, IDirect3DTexture9* level,
                            IDirect3DTexture9* seabed, float frameSeconds, const float (&frame)[9][4],
                            const float (&constants)[kWaterConstants][4])
{
    if(!m_snapshotValid||!seabed)return true;
    if(!SpreadContactMarks(dev,level,seabed,frame,constants))return false;
    const float cell = 1.0f / kRippleCells;
    // How much of the way to the new foam it rises this frame.
    const float rise = std::clamp(1.0f - std::exp(-frameSeconds / kContactFoamRiseSeconds), 0.0f, 1.0f);
    // The pass (contact_spread_ps: cSpread.z), and the rise on its own (a rise of nothing, when no time passed, once read
    // as the count and wrote the counted marks over the whole grid).
    const float down[4] = {0.0f, cell, 2.0f, cell};
    const float riseConstants[4] = {rise, 0.0f, 0.0f, 0.0f};
    const int next = sim.rippleIndex ^ 1;
    if(!SetTarget(dev, sim.ripples[next]))return false;
    if(FAILED(dev->SetPixelShaderConstantF(0, down, 1)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(1, riseConstants, 1)))return false;
    if(!BindPointTexture(dev, 0, m_contactMarks))return false;
    if(!BindPointTexture(dev, 1, sim.ripples[sim.rippleIndex]))return false;
    if(!BindPointTexture(dev, 2, m_contactSpread))return false;
    if(FAILED(DrawScreenTriangle(dev)))return false;
    sim.rippleIndex = next;
    return true;
}

bool Renderer::EnsureDeferredTargets(IDirect3DDevice9* dev, DeferredSurface& surface, const D3DSURFACE_DESC& color, bool keepStock)
{
    if (surface.depth && surface.desc.Width == color.Width && surface.desc.Height == color.Height &&
        surface.desc.Format == color.Format)
        return true;
    SafeRelease(surface.depth);
    SafeRelease(surface.stock);
    SafeRelease(surface.fade);
    if (FAILED(dev->CreateTexture(color.Width, color.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT,
                                  &surface.depth, nullptr)) ||
        (keepStock && FAILED(dev->CreateTexture(color.Width, color.Height, 1, D3DUSAGE_RENDERTARGET, color.Format, D3DPOOL_DEFAULT,
                                                &surface.stock, nullptr))))
    {
        SafeRelease(surface.depth);
        SafeRelease(surface.stock);
        surface.desc = {};
        return Skip("liquid surface targets could not be created");
    }
    surface.desc = color;
    return true;
}

void Renderer::ReleaseDeferred(DeferredSurface& surface)
{
    SafeRelease(surface.level[0]);
    SafeRelease(surface.level[1]);
    SafeRelease(surface.depth);
    SafeRelease(surface.stock);
    SafeRelease(surface.fade);
    surface.desc = {};
    surface.thisFrame = surface.outdoorThisFrame = surface.fadeThisFrame = surface.fadeKept = false;
}

void Renderer::WriteDepthAlways(IDirect3DDevice9* dev, IDirect3DSurface9* target, IDirect3DSurface9* boundDepth,
                                const D3DVIEWPORT9& viewport)
{
    dev->SetRenderTarget(0, target);
    dev->SetDepthStencilSurface(boundDepth);
    const D3DVIEWPORT9 vp = FullDepth(viewport);
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kColorOnly);
}

// In the liquid pass, water may already have been drawn; the depth just before each deferred liquid's draw tells its
// own pixels apart from it.
bool LiquidScreenArea(const FrameInputs& in, const float* bounds, RECT& area, int fadeRamp)
{
    if (!bounds || !in.viewport.Width || !in.viewport.Height) return false;
    for (unsigned c = 0; c < 3; ++c)
        if (!std::isfinite(bounds[c]) || !std::isfinite(bounds[c+3]) || bounds[c] > bounds[c+3]) return false;
    if (fadeRamp >= 0 && fadeRamp < 2 && in.nativeLiquids.baseKnown) {
        // Fade shading only contributes where the authored style has some slime. Split ambiguous boxes conservatively;
        // a tiny green fringe must not make a large, otherwise-water chunk shade the whole viewport. At most 32 leaves.
        area = {LONG(in.viewport.X), LONG(in.viewport.Y), LONG(in.viewport.X), LONG(in.viewport.Y)};
        bool bounded = true, any = false;
        auto include = [&](auto&& self, const float* box, unsigned depth) -> void {
            if (!bounded) return;
            const auto style = in.nativeLiquids.StyleAt(box, unsigned(fadeRamp));
            if (style == LiquidKind::Water) return;
            unsigned axis = 0;
            for (unsigned c = 1; c < 3; ++c) if (box[c+3]-box[c] > box[axis+3]-box[axis]) axis = c;
            if (style == LiquidKind::Fade && depth < 5 && box[axis+3]-box[axis] > 1.f) {
                float first[6], second[6];std::copy(box,box+6,first);std::copy(box,box+6,second);
                first[axis+3] = second[axis] = .5f*(box[axis]+box[axis+3]);
                self(self, first, depth+1);self(self, second, depth+1);
            } else {
                RECT part = {};
                if (!LiquidScreenArea(in,box,part)) { bounded = false;return; }
                if (part.left >= part.right || part.top >= part.bottom) return;
                if (!any) area = part;
                else {
                    area.left=std::min(area.left,part.left);area.top=std::min(area.top,part.top);
                    area.right=std::max(area.right,part.right);area.bottom=std::max(area.bottom,part.bottom);
                }
                any = true;
            }
        };
        include(include,bounds,0);
        return bounded;
    }
    double low[2] = {1e30, 1e30}, high[2] = {-1e30, -1e30};
    for (unsigned corner = 0; corner < 8; ++corner) {
        double p[4] = {}, view[4] = {}, clip[4] = {};
        for (unsigned c = 0; c < 3; ++c) p[c] = double(bounds[c + ((corner & (1u << c)) ? 3 : 0)]) - in.cameraPosition[c];
        // Like ViewToWorld and OwnedWorldGpu, use the captured rotation with the separately captured camera origin.
        // The client's view can retain a translation; applying that after subtracting cameraPosition moves the box twice.
        view[3] = 1;
        for (unsigned c = 0; c < 3; ++c)
            for (unsigned r = 0; r < 3; ++r) view[c] += p[r] * in.cameraRelativeView[r*4+c];
        for (unsigned c = 0; c < 4; ++c)
            for (unsigned r = 0; r < 4; ++r) clip[c] += view[r] * in.glProjection[r*4+c];
        for (double value : clip) if (!std::isfinite(value)) return false;
        if (clip[3] <= 1e-6 || clip[2] < -clip[3]) return false;
        const double screen[2] = {in.viewport.X + (.5 + .5 * clip[0]/clip[3]) * in.viewport.Width,
                                  in.viewport.Y + (.5 - .5 * clip[1]/clip[3]) * in.viewport.Height};
        for (unsigned c = 0; c < 2; ++c) { low[c] = std::min(low[c],screen[c]); high[c] = std::max(high[c],screen[c]); }
    }
    const double left = in.viewport.X, top = in.viewport.Y;
    const double right = left + in.viewport.Width, bottom = top + in.viewport.Height;
    area = {LONG(std::clamp(std::floor(low[0])-1,left,right)), LONG(std::clamp(std::floor(low[1])-1,top,bottom)),
            LONG(std::clamp(std::ceil(high[0])+1,left,right)), LONG(std::clamp(std::ceil(high[1])+1,top,bottom))};
    return true;
}

bool Renderer::SnapshotSurfaceBase(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth, const RECT* area)
{
    D3DSURFACE_DESC desc = {};
    if (FAILED(boundDepth->GetDesc(&desc)))
        return false;
    D3DSURFACE_DESC existing = {};
    if (m_surfaceBaseDepth && (FAILED(m_surfaceBaseDepth->GetLevelDesc(0, &existing)) || existing.Width != desc.Width ||
                             existing.Height != desc.Height))
        SafeRelease(m_surfaceBaseDepth);
    if (!m_surfaceBaseDepth && FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F,
                                                       D3DPOOL_DEFAULT, &m_surfaceBaseDepth, nullptr)))
        return Skip("liquid base depth target creation failed");
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    if(!SetTarget(dev, m_surfaceBaseDepth))return false;
    D3DVIEWPORT9 vp = {0, 0, desc.Width, desc.Height, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    const float texel[4] = {1.0f / desc.Width, 1.0f / desc.Height, 0.0f, 0.0f};
    dev->SetPixelShaderConstantF(1, texel, 1);
    if (FAILED(BindPixelShader(dev, m_snapshotPs))) return false;
    BindPointTexture(dev, 0, depthTexture);
    const bool drawn=SUCCEEDED(DrawScreenArea(dev,area));
    return saved.Restore() && drawn;
}

bool Renderer::CaptureAndUndoSurface(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                                     IDirect3DSurface9* boundDepth, const FrameInputs& in, const Config& cfg,
                                     bool inLiquidPass, LiquidKind kind, bool undo, const RECT* area, int ramp)
{
    DeferredSurface& surface = kind == LiquidKind::Magma ? m_lava.surface : m_slime;
    // The lava is drawn from nothing but where the surface lies; slime keeps the client's colour as well.
    const bool keepStock = kind != LiquidKind::Magma;
    if (!m_snapshotValid || !m_sceneValid)
        return Skip("no scene snapshot before the liquid");
    IDirect3DTexture9* depthBefore = inLiquidPass ? m_surfaceBaseDepth : m_snapshot;
    if (!depthBefore)
        return Skip("no depth captured before the liquid");
    D3DSURFACE_DESC desc = {};
    float frame[9][4];
    if (FAILED(boundDepth->GetDesc(&desc)) || !BuildFrameConstants(in, cfg, desc, frame))
        return false;
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    D3DSURFACE_DESC colorDesc = {};
    bool ok = SUCCEEDED(saved.color[0]->GetDesc(&colorDesc)) && EnsureDeferredTargets(dev, surface, colorDesc, keepStock) &&
              (!keepStock || CopyToTexture(dev, saved.color[0], m_sceneWithWater));
    if (ok)
    {
        const D3DVIEWPORT9 full = {0, 0, desc.Width, desc.Height, 0.0f, 1.0f};
        dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants);
        if (!surface.thisFrame)
        {
            if (keepStock) m_buildingSlime = false;
            if (!keepStock) m_lava.ordinarySeen = m_lava.felSeen = false;
            if(!SetTarget(dev, surface.depth))return false;
            dev->SetViewport(&full);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0xFFFFFFFF, 1.0f, 0);
            surface.area = area ? *area : RECT{};
            surface.areaWhole = !area;
            surface.thisFrame = true;
        }
        if (!area) surface.areaWhole = true;
        else if (!surface.areaWhole) {
            surface.area.left = std::min(surface.area.left, area->left);
            surface.area.top = std::min(surface.area.top, area->top);
            surface.area.right = std::max(surface.area.right, area->right);
            surface.area.bottom = std::max(surface.area.bottom, area->bottom);
        }
        if (FAILED(BindPixelShader(dev, m_slimeCapturePs))) return false;
        BindPointTexture(dev, 0, depthTexture);
        BindPointTexture(dev, 1, depthBefore);
        BindPointTexture(dev, 2, keepStock ? m_sceneWithWater : nullptr);
        // Depth magnitude stays exact. Its sign carries the palette without another full-size target.
        const bool fel = !keepStock && ramp == NativeLiquidFrame::FelMagma;
        const float captureDepth[4] = {0.0f, fel ? -1.0f : 1.0f, 0.0f, 1.0f};
        const float captureColor[4] = {1.0f, 0.0f, 0.0f, 1.0f};
        if(!SetTarget(dev, surface.depth))return false;
        dev->SetViewport(&full);
        dev->SetPixelShaderConstantF(8, captureDepth, 1);
        DrawScreenArea(dev, area);
        if (keepStock)
        {
            if(!SetTarget(dev, surface.stock))return false;
            dev->SetViewport(&full);
            dev->SetPixelShaderConstantF(8, captureColor, 1);
            DrawScreenArea(dev, area);
        }
        // A fade draw is water as well: mark where it lies (with the depth under it), so the composite lays the slime
        // over the water there.
        if (kind == LiquidKind::Fade || surface.fadeThisFrame)
        {
            if (!surface.fade && FAILED(dev->CreateTexture(colorDesc.Width, colorDesc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F,
                                                           D3DPOOL_DEFAULT, &surface.fade, nullptr)))
                ok = Skip("slime fade target creation failed");
            if (ok)
            {
                if (!SetTarget(dev, surface.fade)) return false;
                dev->SetViewport(&full);
                if (!surface.fadeThisFrame)
                    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
                surface.fadeThisFrame = true;
                surface.fadeKept |= kind == LiquidKind::Fade && !undo;
                const float captureFade[4] = {2.0f, kind != LiquidKind::Fade ? 0.0f : (ramp == 0 ? -1.0f : 1.0f), 0.0f, 1.0f};
                dev->SetPixelShaderConstantF(8, captureFade, 1);
                DrawScreenArea(dev, area);
            }
        }

        if (ok && undo)
        {
            WriteDepthAlways(dev, saved.color[0], boundDepth, full);
            if (FAILED(BindPixelShader(dev, m_slimeUndoPs))) return false;
            BindPointTexture(dev, 0, surface.depth);
            BindPointTexture(dev, 1, depthBefore);
            BindPointTexture(dev, 2, m_sceneBefore);
            DrawScreenArea(dev, area);
        }
    }
    if (!saved.Restore()) return false;
    surface.outdoorThisFrame |= ok && inLiquidPass;
    m_buildingSlime |= ok && keepStock && !inLiquidPass;
    m_slimeCaptured |= ok && keepStock;
    if (ok && !keepStock) {
        ++m_lava.work.captures;
        m_lava.felSeen |= ramp == NativeLiquidFrame::FelMagma;
        m_lava.ordinarySeen |= ramp != NativeLiquidFrame::FelMagma;
    }
    return ok;
}

bool Renderer::CompositeDeferredSlime(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                                      IDirect3DSurface9* boundDepth, const FrameInputs& in, const Config& cfg)
{
    if (!m_slime.thisFrame)
        return false;
    m_slime.thisFrame = false;
    const bool fade = m_slime.fadeThisFrame && m_slime.fade;
    const bool fadeKept = fade && m_slime.fadeKept;
    m_slime.fadeThisFrame = m_slime.fadeKept = false;
    PrepareRainRings(dev, boundDepth, in, cfg);
    CompositeScene scene(*this);
    if (!scene.Capture(dev, depthTexture, boundDepth))
        return false;
    D3DSURFACE_DESC desc = {};
    float frame[9][4];
    float constants[kWaterConstants][4];
    if (FAILED(boundDepth->GetDesc(&desc)) || !BuildFrameConstants(in, cfg, desc, frame) ||
        !BuildWaterConstants(in, cfg, cfg.slimeStyle, m_slimeSurface, frame, constants))
        return false;
    // Slime's glints follow its body pigment. Clear-water light colours produced
    // blue-white patches on ripple facets and the thin, less glowing shoreline.
    for (unsigned channel = 0; channel < 3; ++channel)
        constants[kSunColorRegister - kFrameConstants][channel] *= cfg.slimeStyle.shallowColor[channel];
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    TimeBegin(GpuTimer::SlimeSurface);
    bool prepared=SUCCEEDED(dev->SetPixelShaderConstantF(0,&frame[0][0],kFrameConstants))&&
        MeasureLevel(dev,m_slime.depth,m_slime.level,m_slime.levelIndex);
    IDirect3DTexture9* slimeLevel = m_slime.level[0] ? m_slime.level[m_slime.levelIndex] : nullptr;
    prepared=prepared&&AdvanceSurface(dev,m_slimeSurface,slimeLevel,SlimePhysics(cfg),in,cfg,frame,constants);
    TimeEnd(GpuTimer::SlimeSurface);
    if(!prepared){saved.Restore();return false;}
    if (cfg.slime && in.cameraInSlime)
    {
        // From below, the under-slime view draws the surface itself from the scene captured without it.
        saved.Restore();
        return true;
    }
    const bool controls=BindLiquidOptions(dev,cfg,false)&&BindWaterControls(dev,cfg,false);
    // Slime has its own measured level and no water ground/shore field.
    constants[kGroundRegister-kFrameConstants][2]=0;
    TimeBegin(GpuTimer::SlimeRays);
    // Building slime can use the owned scene's imported receiver mask. A host
    // mirror without that mask has no proof that it represents this pool.
    const bool reflections=controls&&(m_slime.outdoorThisFrame||m_sceneryViews)&&cfg.screenReflections&&cfg.reflectionScenery&&
        DrawReflections(dev,m_slime.depth,in,cfg,frame,constants,true);
    TimeEnd(GpuTimer::SlimeRays);
    TimeBegin(GpuTimer::SlimeShade);
    dev->SetRenderTarget(1,nullptr);
    // Fade draws the host left drawn are in the depth just captured: the ground under them instead (slime_lift_ps).
    const bool lifted=fadeKept&&LiftFadeSurfaces(dev,desc,frame);
    dev->SetRenderTarget(0, saved.color[0]);
    WriteDepthAlways(dev, saved.color[0], boundDepth, in.viewport);
    dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants);
    dev->SetPixelShaderConstantF(kFrameConstants, &constants[0][0], kWaterConstants);
    bool bound=controls&&SUCCEEDED(BindHighlightShader(dev,cfg,m_slimeDeferredPs,m_slimeDeferredFinePs))&&
        BindWaterTextures(dev,m_slime.depth,in,cfg,true);
    if(lifted)bound=BindPointTexture(dev,1,m_slimeGround)&&bound;
    D3DSURFACE_DESC reflectionDesc={};
    const bool drawn=reflections&&SUCCEEDED(m_reflections[0]->GetLevelDesc(0,&reflectionDesc));
    const float grid[4]={float(reflectionDesc.Width),float(reflectionDesc.Height),drawn?1.f:0.f,drawn && m_buildingSlime && cfg.specular>0 ? 1.f : 0.f};
    bound=SUCCEEDED(dev->SetPixelShaderConstantF(kReflectionRegister,grid,1))&&bound;
    bound=BindPointTexture(dev,kReflectionStage,drawn?m_reflections[0]:nullptr)&&bound;
    bound=BindPointTexture(dev,kReflectionHitStage,drawn?m_reflections[1]:nullptr)&&bound;
    bound=BindSlimeFade(dev,fade?m_slime.fade:nullptr,in.nativeLiquids)&&bound;
    const bool shaded=bound&&SUCCEEDED(DrawScreenArea(dev,m_slime.areaWhole?nullptr:&m_slime.area));
    TimeEnd(GpuTimer::SlimeShade);
    const bool restored=saved.Restore();
    if(shaded&&restored)++m_slimeComposites;
    return shaded&&restored;
}

bool Renderer::LiftFadeSurfaces(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc, const float (&frame)[9][4])
{
    D3DSURFACE_DESC existing = {};
    if (m_slimeGround && (FAILED(m_slimeGround->GetLevelDesc(0, &existing)) || existing.Width != desc.Width ||
                          existing.Height != desc.Height))
        SafeRelease(m_slimeGround);
    if (!m_slimeGround && FAILED(dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F,
                                                    D3DPOOL_DEFAULT, &m_slimeGround, nullptr)))
        return Skip("slime ground target creation failed");
    if (!SetTarget(dev, m_slimeGround))
        return false;
    const D3DVIEWPORT9 full = {0, 0, desc.Width, desc.Height, 0.0f, 1.0f};
    dev->SetViewport(&full);
    dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kColorOnly);
    dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants);
    return SUCCEEDED(BindPixelShader(dev, m_slimeLiftPs)) && BindPointTexture(dev, 0, m_snapshot) &&
           BindPointTexture(dev, 2, m_slime.depth) && BindPointTexture(dev, 3, m_slime.fade) &&
           SUCCEEDED(DrawScreenTriangle(dev));
}

// Where a fade draw lies (slime_fade.hlsli): the mark and the lights the slime share is worked out from, in the order
// NativeLiquidFrame keeps them. Without a fade draw only the switch (off) is set.
bool Renderer::BindSlimeFade(IDirect3DDevice9* dev, IDirect3DTexture9* mark, const NativeLiquidFrame& liquids)
{
    constexpr unsigned kMax = NativeLiquidFrame::MaxStyleLights;
    float constants[2 + 3 * kMax][4] = {};
    const bool active = mark && liquids.baseKnown;
    const unsigned count = active ? unsigned(std::min<size_t>(liquids.lights.size(), kMax)) : 0;
    for (unsigned kind = 0; kind < 2; ++kind)
        for (unsigned c = 0; c < 3; ++c)
            constants[kind][c] = float(liquids.base[kind][c]);
    constants[0][3] = float(count);
    constants[1][3] = active ? 1.0f : 0.0f;
    for (unsigned i = 0; i < count; ++i)
    {
        const auto& light = liquids.lights[i];
        for (unsigned c = 0; c < 3; ++c)
        {
            constants[2 + i][c] = float(light.centre[c]);
            constants[2 + kMax + i][c] = float(light.colour[0][c]);
            constants[2 + 2 * kMax + i][c] = float(light.colour[1][c]);
        }
        constants[2 + i][3] = float(light.outer);
        const float slope = float(1.0 / (light.outer - light.inner));
        constants[2 + kMax + i][3] = light.usable[0] ? slope : 0.0f;
        constants[2 + 2 * kMax + i][3] = light.usable[1] ? slope : 0.0f;
    }
    const UINT rows = active ? 2 + 3 * kMax : 2;
    return SUCCEEDED(dev->SetPixelShaderConstantF(kSlimeFadeRegister, &constants[0][0], rows)) &&
           BindPointTexture(dev, kSlimeFadeStage, active ? mark : nullptr);
}

// One step of how stirred the lava is (lava_stir_ps), on a grid over the same square as the ground map and the ripples.
bool Renderer::AdvanceLavaStir(IDirect3DDevice9* dev, IDirect3DTexture9* level, const FrameInputs& in, const Config& cfg,
                               const float (&frame)[9][4], float (&constants)[kWaterConstants][4])
{
    SurfaceSim& sim = m_lava.stir;
    float* rippleArea = constants[kRippleAreaRegister - kFrameConstants];
    if (sim.advancedThisFrame)
    {
        rippleArea[0] = sim.origin[0];
        rippleArea[1] = sim.origin[1];
        return true;
    }
    for (auto** target : {&sim.ripples[0], &sim.ripples[1]})
        if (!*target && FAILED(dev->CreateTexture(kRippleCells, kRippleCells, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F,
                                                  D3DPOOL_DEFAULT, target, nullptr)))
        {
            ReleaseSurface(sim);
            return Skip("lava stir targets could not be created");
        }
    struct Update {
        SurfaceSim& sim;
        bool complete = false;
        ~Update() { if (!complete) { sim.valid = false; sim.advancedThisFrame = false; } }
    } update{sim};
    const float origin[2] = {in.ground ? in.ground->Origin()[0] : GroundMap::OriginFor(in.cameraPosition[0]),
                             in.ground ? in.ground->Origin()[1] : GroundMap::OriginFor(in.cameraPosition[1])};
    float shift[2] = {(origin[0] - sim.origin[0]) / kRippleCellSize, (origin[1] - sim.origin[1]) / kRippleCellSize};
    const long long now = Ticks();
    const double seconds = cfg.testSeconds >= 0.0f
                               ? (sim.lastTestSeconds >= 0.0f ? std::max(cfg.testSeconds - sim.lastTestSeconds, 0.0f) : 0.0)
                           : sim.lastTicks ? SecondsSince(sim.lastTicks)
                                           : 0.0;
    // A step is needed when the grid has moved (the shore's maps are read through its place), and otherwise only as often
    // as cooling and trails can show it, and not at all while none of the lava is in sight: the next step makes up the time.
    if (sim.valid && shift[0] == 0.0f && shift[1] == 0.0f && (m_lava.hidden || seconds < kLavaStirStepSeconds))
    {
        rippleArea[0] = sim.origin[0];
        rippleArea[1] = sim.origin[1];
        sim.advancedThisFrame = true;
        update.complete = true;
        return true;
    }
    sim.lastTestSeconds = cfg.testSeconds;
    sim.lastTicks = now;
    const D3DVIEWPORT9 grid = {0, 0, kRippleCells, kRippleCells, 0.0f, 1.0f};
    if (!sim.valid || std::fabs(shift[0]) >= kRippleCells || std::fabs(shift[1]) >= kRippleCells)
    {
        for (auto* target : {sim.ripples[0], sim.ripples[1]})
        {
            if (!SetTarget(dev, target) || FAILED(dev->SetViewport(&grid)) ||
                FAILED(dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0)))
                return false;
        }
        shift[0] = shift[1] = 0.0f;
        sim.valid = true;
    }
    sim.origin[0] = origin[0];
    sim.origin[1] = origin[1];
    rippleArea[0] = origin[0];
    rippleArea[1] = origin[1];

    // Whatever crosses the surface, from the depth buffer: the last way across the grid, into a texture of its own.
    IDirect3DTexture9* seabed = in.ground ? GroundTexture(dev, in.ground) : nullptr;
    IDirect3DTexture9* contact = nullptr;
    // What stands still needs finding less often than the lava steps; the rise makes up the time since it was last looked for.
    m_lava.contactDue += seconds;
    float contactRise = 0.0f;
    if (m_snapshotValid && seabed && !m_lava.hidden && m_lava.contactDue >= kLavaContactSeconds)
    {
        contactRise = static_cast<float>(1.0 - std::exp(-m_lava.contactDue * kLavaContactRisePerSecond));
        m_lava.contactDue = 0.0;
        ++m_lava.work.contactSteps;
        if (!SpreadContactMarks(dev, level, seabed, frame, constants))
            return false;
        const float cell = 1.0f / kRippleCells;
        const float down[4] = {0.0f, cell, 1.0f, cell};
        if (!SetTarget(dev, m_contactSpread) || FAILED(dev->SetViewport(&grid)) ||
            FAILED(dev->SetPixelShaderConstantF(0, down, 1)) || !BindPointTexture(dev, 0, m_contactMarks) ||
            !BindPointTexture(dev, 1, nullptr) || !BindPointTexture(dev, 2, nullptr) || FAILED(DrawScreenTriangle(dev)))
            return false;
        contact = m_contactSpread;
    }

    // Only what is moving: something standing is found by the contact marks, and a pulse lasts one frame.
    float movers[kMaxMovers][4] = {};
    float radii[kMaxMovers][4] = {};
    int count = 0;
    for (int i = 0; i < in.moverCount; ++i)
    {
        const auto& mover = in.movers[i];
        if (!mover.continuous)
            continue;
        std::memcpy(movers[count], mover.position, sizeof(mover.position));
        radii[count][0] = mover.radius;
        ++count;
    }
    const float gridConstants[4] = {1.0f / kRippleCells, static_cast<float>(seconds), kRippleCellSize, 0.0f};
    const float originConstants[4] = {origin[0], origin[1], shift[0], shift[1]};
    const float countConstants[4] = {static_cast<float>(count), contactRise, 0.0f, 0.0f};
    const int next = sim.rippleIndex ^ 1;
    if (FAILED(BindPixelShader(dev, m_lava.stirPs)) || !SetTarget(dev, sim.ripples[next]) ||
        FAILED(dev->SetViewport(&grid)) || FAILED(dev->SetPixelShaderConstantF(0, gridConstants, 1)) ||
        FAILED(dev->SetPixelShaderConstantF(1, originConstants, 1)) ||
        FAILED(dev->SetPixelShaderConstantF(2, countConstants, 1)) ||
        FAILED(dev->SetPixelShaderConstantF(kRippleMoverRegister, &movers[0][0], kMaxMovers)) ||
        FAILED(dev->SetPixelShaderConstantF(kRippleRadiusRegister, &radii[0][0], kMaxMovers)) ||
        !BindPointTexture(dev, 0, sim.ripples[sim.rippleIndex]) || !BindPointTexture(dev, 1, contact) ||
        !BindPointTexture(dev, 2, level) || FAILED(DrawScreenTriangle(dev)))
        return false;
    sim.rippleIndex = next;
    sim.advancedThisFrame = true;
    update.complete = true;
    ++m_lava.work.stirSteps;
    return true;
}

bool Renderer::CompositeDeferredLava(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                                     IDirect3DSurface9* boundDepth, const FrameInputs& in, const Config& cfg)
{
    if (!m_lava.surface.thisFrame)
        return false;
    m_lava.surface.thisFrame = false;
    ++m_lava.work.composites;
    m_lava.PollVisibility(dev);
    // The scene as it now stands: the models drawn since the lava was captured may stand in front of it.
    CompositeScene scene(*this);
    if (!scene.Capture(dev, depthTexture, boundDepth))
        return false;
    D3DSURFACE_DESC desc = {};
    float frame[9][4];
    float constants[kWaterConstants][4];
    if (FAILED(boundDepth->GetDesc(&desc)) || !BuildFrameConstants(in, cfg, desc, frame) ||
        !BuildWaterConstants(in, cfg, cfg.slimeStyle, m_lava.stir, frame, constants))
        return false;
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    bool prepared = SUCCEEDED(dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants)) &&
                    MeasureLevel(dev, m_lava.surface.depth, m_lava.surface.level, m_lava.surface.levelIndex);
    IDirect3DTexture9* level = m_lava.surface.level[0] ? m_lava.surface.level[m_lava.surface.levelIndex] : nullptr;
    prepared = prepared && m_lava.BakePatterns(*this,dev);
    TimeBegin(GpuTimer::LavaStir);
    prepared = prepared && AdvanceLavaStir(dev, level, in, cfg, frame, constants);
    TimeEnd(GpuTimer::LavaStir);
    TimeBegin(GpuTimer::LavaHalo);
    prepared = prepared && DrawLavaHalo(dev, desc, frame);
    TimeEnd(GpuTimer::LavaHalo);
    prepared = prepared && BindLiquidOptions(dev, cfg, false) && BindWaterControls(dev, cfg, false);
    if (!prepared)
    {
        saved.Restore();
        return false;
    }
    dev->SetRenderTarget(1, nullptr);
    dev->SetRenderTarget(0, saved.color[0]);
    WriteDepthAlways(dev, saved.color[0], boundDepth, in.viewport);
    dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants);
    dev->SetPixelShaderConstantF(kFrameConstants, &constants[0][0], kWaterConstants);
    bool bound = SUCCEEDED(BindPixelShader(dev, m_lava.surfacePs));
    bound = BindPointTexture(dev, 0, m_lava.surface.depth) && bound;
    bound = BindPointTexture(dev, 1, m_snapshot) && bound;
    bound = BindSky(dev, 5, cfg) && bound;
    bound = BindLinearTexture(dev, 6, m_lava.stir.Ripples()) && bound;
    bound = BindLinearTexture(dev, 7, GroundTexture(dev, in.ground)) && bound;
    bound = BindLinearTexture(dev, 8, in.ground ? m_shore : nullptr) && bound;
    const auto bindPattern = [&](DWORD stage, IDirect3DTexture9* texture, bool mipped) {
        bool ok = BindLinearTexture(dev, stage, texture);
        for (DWORD axis : {D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV})
            ok = SUCCEEDED(dev->SetSamplerState(stage, static_cast<D3DSAMPLERSTATETYPE>(axis), D3DTADDRESS_WRAP)) && ok;
        if (mipped)
        {
            ok = SUCCEEDED(dev->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR)) && ok;
            ok = SUCCEEDED(dev->SetSamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC)) && ok;
            ok = SUCCEEDED(dev->SetSamplerState(stage, D3DSAMP_MAXANISOTROPY, 4)) && ok;
        }
        return ok;
    };
    bound = bindPattern(9, m_lava.cells, true) && bound;
    bound = bindPattern(10, m_lava.noise, true) && bound;
    bound = bindPattern(11, m_lava.foam, true) && bound;
    bound = bindPattern(13, m_lava.flow, false) && bound;
    bound = BindPointTexture(dev, 12, m_lava.rand) && bound;
    bound = BindLinearTexture(dev, 14, m_lava.halo) && bound;
    bound = SUCCEEDED(dev->SetSamplerState(14, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR)) && bound;
    for (DWORD axis : {D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV})
        bound = SUCCEEDED(dev->SetSamplerState(12, static_cast<D3DSAMPLERSTATETYPE>(axis), D3DTADDRESS_WRAP)) && bound;
    TimeBegin(GpuTimer::LavaSurface);
    const bool counting = m_lava.visibleQuery && !m_lava.visiblePending && SUCCEEDED(m_lava.visibleQuery->Issue(D3DISSUE_BEGIN));
    const bool shaded = bound && SUCCEEDED(DrawScreenTriangle(dev));
    if (counting) m_lava.visiblePending = SUCCEEDED(m_lava.visibleQuery->Issue(D3DISSUE_END));
    TimeEnd(GpuTimer::LavaSurface);
    TimeBegin(GpuTimer::LavaGlow);
    const float farArea[4] = {in.farGround ? in.farGround->Origin()[0] : 0.0f, in.farGround ? in.farGround->Origin()[1] : 0.0f,
                              in.farGround ? 1.0f / in.farGround->Area() : 0.0f, 0.0f};
    const bool farBound = BindLinearTexture(dev, 4, FarGroundTexture(dev, in.farGround)) && SUCCEEDED(dev->SetPixelShaderConstantF(kFarAreaRegister, farArea, 1));
    const bool glowed = shaded && farBound && DrawLavaGlow(dev, desc, in.viewport, saved.color[0]);
    TimeEnd(GpuTimer::LavaGlow);
    const bool restored = saved.Restore();
    m_lava.drewThisFrame = shaded && restored;
    return glowed && restored;
}

// Where the lava lies on screen, for its border and its light on what stands beside it (lava_halo_ps).
bool Renderer::DrawLavaHalo(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc, const float (&frame)[9][4])
{
    const UINT width = std::max(desc.Width / kLavaHaloScale, 1u), height = std::max(desc.Height / kLavaHaloScale, 1u);
    if (!m_lava.EnsureHalo(dev,width,height)) return false;
    const D3DVIEWPORT9 halo = {0, 0, width, height, 0.0f, 1.0f};
    const float haloConstants[4] = {1.0f / width, static_cast<float>(kLavaHaloScale), 0.0f, 0.0f};
    const bool mixed = m_lava.ordinarySeen && m_lava.felSeen;
    if (!SetTarget(dev, m_lava.halo) || (mixed && (!SetTarget(dev, m_lava.haloColours, 1) ||
        FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE1, kColorOnly)))) || FAILED(dev->SetViewport(&halo)) || FAILED(BindPixelShader(dev, m_lava.haloPs)) ||
        FAILED(dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants)) ||
        FAILED(dev->SetPixelShaderConstantF(9, haloConstants, 1)) || !BindPointTexture(dev, 0, m_lava.surface.depth) ||
        !BindPointTexture(dev, 1, m_snapshot) ||
        FAILED(dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0)) || FAILED(DrawScreenTriangle(dev)))
        return false;
    if (mixed) {
        if (FAILED(dev->SetRenderTarget(1, nullptr))) return false;
        m_lava.haloColours->GenerateMipSubLevels();
    }
    m_lava.halo->GenerateMipSubLevels();
    return true;
}

// The lava's light on what stands beside it (lava_glow_ps).
bool Renderer::DrawLavaGlow(IDirect3DDevice9* dev, const D3DSURFACE_DESC& desc, const D3DVIEWPORT9& viewport,
                            IDirect3DSurface9* target)
{
    const UINT width = std::max(desc.Width / kLavaHaloScale, 1u), height = std::max(desc.Height / kLavaHaloScale, 1u);
    float cameraToClip[4] = {};
    dev->GetPixelShaderConstantF(2, cameraToClip, 1);
    const float glowConstants[4] = {1.0f / width, 1.0f / height, static_cast<float>(kLavaHaloScale),
                                    0.5f * viewport.Height * cameraToClip[1]};
    const bool mixed = m_lava.ordinarySeen && m_lava.felSeen;
    const float palette[4] = {m_lava.felSeen ? 1.0f : 0.0f, mixed ? 1.0f : 0.0f, 0, 0};
    bool ok = SUCCEEDED(dev->SetRenderTarget(0, target)) && SUCCEEDED(dev->SetViewport(&viewport)) &&
              SUCCEEDED(dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE)) &&
              SUCCEEDED(dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE)) &&
              SUCCEEDED(BindPixelShader(dev, m_lava.glowPs)) &&
              SUCCEEDED(dev->SetPixelShaderConstantF(9, glowConstants, 1)) && BindPointTexture(dev, 0, m_lava.surface.depth) &&
              BindPointTexture(dev, 1, m_snapshot) && BindPointTexture(dev, 2, m_sceneBefore) &&
              BindLinearTexture(dev, 3, m_lava.halo) &&
              SUCCEEDED(dev->SetSamplerState(3, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR));
    if (mixed)
        ok = ok && BindLinearTexture(dev, 5, m_lava.haloColours) &&
             SUCCEEDED(dev->SetSamplerState(5, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR));
    return ok && SUCCEEDED(dev->SetPixelShaderConstantF(91, palette, 1)) && SUCCEEDED(DrawScreenTriangle(dev));
}

bool Renderer::BuildLavaEffectsMask(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                                    const FrameInputs& in, const Config& cfg, IDirect3DTexture9* hostMask)
{
    m_lava.maskReady = false;
    // (With none of it in sight, the host's own mask is the whole answer.)
    if (!m_lava.drewThisFrame || m_lava.hidden || !depthTexture)
        return false;
    D3DSURFACE_DESC desc = {};
    float frame[9][4];
    if (FAILED(boundDepth->GetDesc(&desc)) || !BuildFrameConstants(in, cfg, desc, frame))
        return false;
    if (!m_lava.EnsureMask(dev,desc.Width,desc.Height)) return Skip("lava effects mask could not be created");
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    const D3DVIEWPORT9 full = {0, 0, desc.Width, desc.Height, 0.0f, 1.0f};
    const bool drawn = SetTarget(dev, m_lava.mask) && SUCCEEDED(dev->SetViewport(&full)) &&
                       SUCCEEDED(dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants)) &&
                       SUCCEEDED(BindPixelShader(dev, m_lava.maskPs)) && BindPointTexture(dev, 0, m_lava.surface.depth) &&
                       BindPointTexture(dev, 1, depthTexture) && BindPointTexture(dev, 2, hostMask) && SUCCEEDED(DrawScreenTriangle(dev));
    const bool restored = saved.Restore();
    m_lava.maskReady = drawn && restored;
    return m_lava.maskReady;
}

bool Renderer::EnsureOcclusionTargets(IDirect3DDevice9* dev, const D3DSURFACE_DESC& depth)
{
    if (m_occlusion[0] && m_occlusionWidth == depth.Width && m_occlusionHeight == depth.Height)
        return true;
    SafeRelease(m_occlusion[0]);
    SafeRelease(m_occlusion[1]);
    for (auto*& target : m_occlusion)
        if (FAILED(dev->CreateTexture(depth.Width, depth.Height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R16F,
                                      D3DPOOL_DEFAULT, &target, nullptr)))
        {
            SafeRelease(m_occlusion[0]);
            SafeRelease(m_occlusion[1]);
            m_occlusionWidth = 0;
            m_occlusionHeight = 0;
            return Skip("ambient occlusion targets could not be created");
        }
    m_occlusionWidth = depth.Width;
    m_occlusionHeight = depth.Height;
    return true;
}

bool Renderer::AmbientOcclusion(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                                const FrameInputs& in, const Config& cfg)
{
    if (cfg.ambientOcclusion <= 0.0f)
        return false;
    D3DSURFACE_DESC desc = {};
    float frame[9][4];
    if (FAILED(boundDepth->GetDesc(&desc)) || !BuildFrameConstants(in, cfg, desc, frame) ||
        !EnsureOcclusionTargets(dev, desc))
        return false;
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    const D3DVIEWPORT9 vp = FullDepth(in.viewport);
    const float pixelsPerYard = 0.5f * vp.Height * in.glProjection[5];
    const float occlusion[2][4] = {
        {cfg.ambientOcclusionRadius, cfg.ambientOcclusionDistance * kOcclusionFadeStart, cfg.ambientOcclusionDistance,
         pixelsPerYard},
        {in.fogStart, in.fogEnd, cfg.ambientOcclusion, 0.0f},
    };
    dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants);
    dev->SetPixelShaderConstantF(kOcclusionRegister, &occlusion[0][0], 2);
    BindPointTexture(dev, 0, depthTexture);
    if(!SetTarget(dev, m_occlusion[0]))return false;
    dev->SetViewport(&vp);
    if (FAILED(BindPixelShader(dev, m_aoPs))) return false;
    DrawScreenTriangle(dev);

    const float across[4] = {1.0f, 0.0f, kOcclusionBlurDepthTolerance, 0.0f};
    const float down[4] = {0.0f, 1.0f, kOcclusionBlurDepthTolerance, 1.0f};
    if (FAILED(BindPixelShader(dev, m_aoBlurPs))) return false;
    if(!SetTarget(dev, m_occlusion[1]))return false;
    dev->SetViewport(&vp);
    dev->SetPixelShaderConstantF(kOcclusionRegister, across, 1);
    BindPointTexture(dev, 2, m_occlusion[0]);
    DrawScreenTriangle(dev);

    // The last run shades a copy of the scene (the scratch copy, taken afresh wherever else it is used) back onto it.
    D3DSURFACE_DESC colorDesc = {};
    if (FAILED(saved.color[0]->GetDesc(&colorDesc)) || !EnsureSceneTargets(dev, colorDesc) ||
        !CopyToTexture(dev, saved.color[0], m_sceneWithWater))
    {
        saved.Restore();
        return Skip("scene copy for the ambient occlusion failed");
    }
    const float shade[4] = {cfg.ambientOcclusionColor[0], cfg.ambientOcclusionColor[1], cfg.ambientOcclusionColor[2],
                            0.0f};
    dev->SetRenderTarget(0, saved.color[0]);
    dev->SetViewport(&vp);
    dev->SetPixelShaderConstantF(kOcclusionRegister, down, 1);
    dev->SetPixelShaderConstantF(kOcclusionRegister + 1, shade, 1);
    BindPointTexture(dev, 2, m_occlusion[1]);
    BindPointTexture(dev, 3, m_sceneWithWater);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kColorOnly);
    const bool drawn=SUCCEEDED(DrawScreenTriangle(dev));
    if (!saved.Restore() || !drawn) return false;
    ++m_groundEffects.occlusion;
    return true;
}

bool Renderer::EnsureRingMap(IDirect3DDevice9* dev)
{
    if (m_ringShaderFailed) return false;
    if ((!m_ringVs && FAILED(p_vs_ring.Create(dev, &m_ringVs))) ||
        (!m_ringDecl && FAILED(dev->CreateVertexDeclaration(kRingVertex, &m_ringDecl)))) {
        m_ringShaderFailed = true;
        return false;
    }

    if (m_ringMap)
        return true;
    if (FAILED(dev->CreateTexture(kRingMapCells, kRingMapCells, 1, D3DUSAGE_RENDERTARGET, D3DFMT_G16R16F,
                                  D3DPOOL_DEFAULT, &m_ringMap, nullptr)))
        return Skip("ring map could not be created");
    return true;
}

namespace
{
// Whether a splash lands on water or ground there (by the ground map), rather than on something standing above it.
bool LandsOnSurface(const GroundMap* ground, const Splash& splash)
{
    if (!ground)
        return true;
    const int x = static_cast<int>((splash.x - ground->Origin()[0]) / GroundMap::kCellSize);
    const int y = static_cast<int>((splash.y - ground->Origin()[1]) / GroundMap::kCellSize);
    if (x < 0 || y < 0 || x >= GroundMap::kCells || y >= GroundMap::kCells)
        return true;
    const float land = ground->Heights()[y * GroundMap::kCells + x];
    const float water = ground->LiquidHeights()[y * GroundMap::kCells + x];
    if (land <= GroundMap::kUnknown && water <= GroundMap::kUnknown)
        return true;
    return std::fabs(splash.z - land) < kRingSurfaceHeight || std::fabs(splash.z - water) < kRingSurfaceHeight;
}

// A splash's lasting pick for a ring (the same splash is always picked or left), 0..1.
float SplashPick(const Splash& splash)
{
    const float h = std::sin(splash.x * 12.9898f + splash.y * 78.233f) * 43758.5453f;
    return h - std::floor(h);
}
}

bool Renderer::DrawRainRings(IDirect3DDevice9* dev, IDirect3DSurface9* boundDepth, const FrameInputs& in,
                             const Config& cfg)
{
    m_ringFrame = m_worldFrame;
    m_ringMapReach = 0.0f;
    if (in.splashCount <= 0 || !in.splashes || cfg.rainRings <= 0.0f || !EnsureRingMap(dev))
        return false;
    const float cell = kRingMapArea / kRingMapCells;
    m_ringMapOrigin[0] = std::floor((in.cameraPosition[0] - kRingMapArea * 0.5f) / cell) * cell;
    m_ringMapOrigin[1] = std::floor((in.cameraPosition[1] - kRingMapArea * 0.5f) / cell) * cell;
    struct RingVertex
    {
        float position[4];
        float ring[4];
    };
    static std::vector<RingVertex> vertices;
    vertices.clear();
    const float corners[6][2] = {{-1, -1}, {1, -1}, {-1, 1}, {-1, 1}, {1, -1}, {1, 1}};
    for (int i = 0; i < in.splashCount; ++i)
    {
        const Splash& splash = in.splashes[i];
        if (SplashPick(splash) >= cfg.rainRings || !LandsOnSurface(in.ground, splash))
            continue;
        const float phase = std::clamp(splash.age / kSplashRingSeconds, 0.0f, 1.0f);
        for (const auto& corner : corners)
        {
            const float x = splash.x + corner[0] * kRingExtent;
            const float y = splash.y + corner[1] * kRingExtent;
            vertices.push_back({{(x - m_ringMapOrigin[0]) / kRingMapArea * 2.0f - 1.0f,
                                 1.0f - (y - m_ringMapOrigin[1]) / kRingMapArea * 2.0f, 0.0f, 1.0f},
                                {corner[0] * kRingExtent, corner[1] * kRingExtent, phase, 0.0f}});
        }
    }
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    if(!SetTarget(dev, m_ringMap))return false;
    D3DVIEWPORT9 vp = {0, 0, kRingMapCells, kRingMapCells, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
    if (!vertices.empty())
    {
        dev->SetVertexShader(m_ringVs);
        dev->SetVertexDeclaration(m_ringDecl);
        if (FAILED(BindPixelShader(dev, m_ringPs))) return false;
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
        dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
        if (FAILED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(vertices.size() / 3), vertices.data(),
                                        sizeof(RingVertex)))) return false;
    }
    if (!saved.Restore()) return false;
    m_ringMapReach = in.splashReach;
    m_ringCount = static_cast<unsigned>(vertices.size() / 6);
    return true;
}

// The ring map for the rain rings on the puddles and the water (see water_common.hlsli); its reach is 0 when it has
// no rings this frame.
void Renderer::PrepareRainRings(IDirect3DDevice9* dev, IDirect3DSurface9* boundDepth, const FrameInputs& in, const Config& cfg)
{
    if (m_ringFrame == m_worldFrame)
        return;
    TimeBegin(GpuTimer::RainRings);
    DrawRainRings(dev, boundDepth, in, cfg);
    TimeEnd(GpuTimer::RainRings);
}

bool Renderer::BindRingMap(IDirect3DDevice9* dev)
{
    const float ringMap[4]={m_ringMapOrigin[0],m_ringMapOrigin[1],1.f/kRingMapArea,m_ringMapReach};
    const bool constants=SUCCEEDED(dev->SetPixelShaderConstantF(kRingMapRegister,ringMap,1));
    return BindLinearTexture(dev,kRingMapStage,m_ringMapReach>0?m_ringMap:nullptr)&&constants;
}

// Everything the water composite reads, also used by its reflection pass.
bool Renderer::BindWaterTextures(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const FrameInputs& in, const Config& cfg, bool slime)
{
    bool ok=BindRingMap(dev);
    if (slime) {
        ok=BindPointTexture(dev,WATER_SLOT_sDepth,depthTexture)&&ok;
        ok=BindPointTexture(dev,WATER_SLOT_sOpaqueDepth,m_snapshot)&&ok;
        ok=BindLinearTexture(dev,WATER_SLOT_sSceneBefore,m_sceneBefore)&&ok;
        ok=BindPointTexture(dev,WATER_SLOT_sSceneWithWater,m_slime.stock)&&ok;
        ok=BindPointTexture(dev,WATER_SLOT_sWaterLevel,m_slime.level[0]?m_slime.level[m_slime.levelIndex]:nullptr)&&ok;
        ok=BindSky(dev,WATER_SLOT_sRememberedSky,cfg)&&ok;
        ok=BindLinearTexture(dev,WATER_SLOT_sRipples,m_slimeSurface.Ripples())&&ok;
        ok=BindLinearTexture(dev,WATER_SLOT_sSeabed,nullptr)&&ok;
        ok=BindLinearTexture(dev,WATER_SLOT_sShore,nullptr)&&ok;
        return BindPointTexture(dev,kBoatPathStage,nullptr)&&ok;
    }
    ok=BindPointTexture(dev,WATER_SLOT_sDepth,depthTexture)&&ok;
    ok=BindPointTexture(dev,WATER_SLOT_sOpaqueDepth,m_snapshot)&&ok;
    ok=BindLinearTexture(dev,WATER_SLOT_sSceneBefore,m_sceneBefore)&&ok;
    ok=BindPointTexture(dev,WATER_SLOT_sSceneWithWater,m_sceneWithWater)&&ok;
    ok=BindPointTexture(dev,WATER_SLOT_sWaterLevel,m_level[0]?m_level[m_levelIndex]:nullptr)&&ok;
    ok=BindSky(dev,WATER_SLOT_sRememberedSky,cfg,in.reflectedFogWeight)&&ok;
    ok=BindLinearTexture(dev,WATER_SLOT_sRipples,m_waterSurface.Ripples())&&ok;
    ok=BindLinearTexture(dev,WATER_SLOT_sSeabed,GroundTexture(dev,in.ground))&&ok;
    ok=BindLinearTexture(dev,WATER_SLOT_sShore,in.ground?m_shore:nullptr)&&ok;
    return BindPointTexture(dev,kBoatPathStage,BoatPathTexture(dev,in))&&ok;
}

void Renderer::ReleaseFillChain(FillChain& chain)
{
    for (int level = 0; level < FillChain::kLevels; ++level)
        for (int i = 0; i < 2; ++i)
        {
            SafeRelease(chain.pull[level][i]);
            SafeRelease(chain.push[level][i]);
        }
}

bool Renderer::EnsureFillChain(IDirect3DDevice9* dev, FillChain& chain, UINT width, UINT height)
{
    if (chain.pull[0][0] && (chain.widths[0] != width || chain.heights[0] != height))
        ReleaseFillChain(chain);
    for (int level = 0; level < FillChain::kLevels; ++level)
    {
        const UINT scale = 1u << level;
        chain.widths[level] = std::max((width + scale - 1) / scale, 1u);
        chain.heights[level] = std::max((height + scale - 1) / scale, 1u);
    }
    auto create = [&](IDirect3DTexture9*& texture, int level) {
        return texture || SUCCEEDED(dev->CreateTexture(chain.widths[level], chain.heights[level], 1,
                                                      D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT,
                                                      &texture, nullptr));
    };
    bool ok = true;
    for (int level = 0; level < FillChain::kLevels && ok; ++level)
        for (int i = 0; i < 2 && ok; ++i)
            ok = create(chain.pull[level][i], level) &&
                 (level == FillChain::kLevels - 1 || create(chain.push[level][i], level));
    if (!ok)
        ReleaseFillChain(chain);
    return ok;
}

namespace
{
bool SetPairTargets(IDirect3DDevice9* dev, IDirect3DTexture9* const (&pair)[2])
{
    if(!SetTarget(dev,pair[0]))return false;
    IDirect3DSurface9* second=nullptr;
    const bool have=pair[1]&&SUCCEEDED(pair[1]->GetSurfaceLevel(0,&second))&&second;
    const bool bound=have&&SUCCEEDED(dev->SetRenderTarget(1,second));
    SafeRelease(second);return bound;
}
}

// Fills in what the chain's finest pull level does not know: pulled up level by level (each averaging what is known
// of the level below, fill_pull_ps) and pushed back down (each taking what it does not know from the level above,
// fill_push_ps), into the finest push level. Leaves the render targets, viewport, pixel shader, stages 0
// to 3 and the first constant changed.
bool Renderer::FillIn(IDirect3DDevice9* dev, FillChain& chain)
{
    constexpr int kLevels = FillChain::kLevels;
    auto levelViewport = [&](int level) {
        const D3DVIEWPORT9 vp = {0, 0, chain.widths[level], chain.heights[level], 0.0f, 1.0f};
        return SUCCEEDED(dev->SetViewport(&vp));
    };
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF)))return false;
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF)))return false;
    if(FAILED(BindPixelShader(dev, m_fillPullPs)))return false;
    for (int level = 1; level < kLevels; ++level)
    {
        if(!SetPairTargets(dev, chain.pull[level]))return false;
        if(!levelViewport(level))return false;
        const float finer[4] = {1.0f / chain.widths[level - 1], 1.0f / chain.heights[level - 1],
                                static_cast<float>(chain.widths[level - 1]),
                                static_cast<float>(chain.heights[level - 1])};
        if(FAILED(dev->SetPixelShaderConstantF(0, finer, 1)))return false;
        if(!BindPointTexture(dev, 0, chain.pull[level - 1][0]))return false;
        if(!BindPointTexture(dev, 1, chain.pull[level - 1][1]))return false;
        if(FAILED(DrawScreenTriangle(dev)))return false;
    }
    if(FAILED(BindPixelShader(dev, m_fillPushPs)))return false;
    for (int level = kLevels - 2; level >= 0; --level)
    {
        IDirect3DTexture9* const(&coarser)[2] = level + 1 == kLevels - 1 ? chain.pull[level + 1] : chain.push[level + 1];
        if(!SetPairTargets(dev, chain.push[level]))return false;
        if(!levelViewport(level))return false;
        const float here[4] = {1.0f / chain.widths[level], 1.0f / chain.heights[level], level == 0 ? 1.0f : 0.0f, 0.0f};
        if(FAILED(dev->SetPixelShaderConstantF(0, here, 1)))return false;
        if(!BindPointTexture(dev, 0, chain.pull[level][0]))return false;
        if(!BindPointTexture(dev, 1, chain.pull[level][1]))return false;
        if(!BindLinearTexture(dev, 2, coarser[0]))return false;
        if(!BindLinearTexture(dev, 3, coarser[1]))return false;
        if(FAILED(DrawScreenTriangle(dev)))return false;
    }
    for (DWORD stage = 0; stage < 4; ++stage)
        if(FAILED(dev->SetTexture(stage, nullptr)))return false;
    if(FAILED(dev->SetRenderTarget(1, nullptr)))return false;
    return true;
}

// The water's reflections at the selected resolve resolution: the ray each reflects traced (reflection_ps), for the composite to
// read. Leaves the render targets, viewport, pixel shader and the first constants changed.
bool Renderer::DrawReflections(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const FrameInputs& in, const Config& cfg,
                               const float (&frame)[9][4], const float (&constants)[kWaterConstants][4], bool slime, bool* hasIndoorWater)
{
    // Only mirrored geometry reflects; without geometry each liquid shows the sky.
    if (!cfg.reflectionScenery) return false;
    D3DSURFACE_DESC full = {};
    if (!m_snapshot || FAILED(m_snapshot->GetLevelDesc(0, &full)))
        return false;
    const auto layout = reflection::ResolveLayout(full.Width,full.Height,in.viewport,cfg.reflectionResolution);
    const float pixelStep[4]={float(full.Width)/layout.width-1.f,float(full.Height)/layout.height-1.f,0,0};
    if(FAILED(dev->SetPixelShaderConstantF(79,pixelStep,1)))return false;
    const UINT width = layout.width;
    const UINT height = layout.height;
    D3DSURFACE_DESC have = {};
    if (m_reflections[0] && (FAILED(m_reflections[0]->GetLevelDesc(0, &have)) || have.Width != width ||
                             have.Height != height))
    {
        SafeRelease(m_reflections[0]);
        SafeRelease(m_reflections[1]);
    }
    for (IDirect3DTexture9*& target : m_reflections)
        if (!target && FAILED(dev->CreateTexture(width, height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F,
                                                 D3DPOOL_DEFAULT, &target, nullptr)))
            return Skip("reflection buffers could not be created");
    // The rays, cleared to 0 (no water) first: only water is drawn.
    if(!SetPairTargets(dev, m_reflections))return false;
    const D3DVIEWPORT9 whole = {0, 0, width, height, 0.0f, 1.0f};
    dev->SetViewport(&whole);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0);
    dev->SetViewport(&layout.viewport);
    dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants);
    dev->SetPixelShaderConstantF(kFrameConstants, &constants[0][0], kWaterConstants);
    if(!BindWaterTextures(dev, depthTexture, in, cfg, slime))return false;
    if(!slime)BindSurfaceDetail(dev);
    auto* views=m_sceneryViews;
    const unsigned count=views?views->ViewCount():1;
    // One interval for the complete set of source views and their resolves.
    // Per-view intervals overflow the bounded query pool in multi-height scenes.
    GpuScope reflectionViews(m_timer,GpuTimer::WaterReflections);
    bool drawn=false;
    for(unsigned index=0;index<count;++index){
        RECT area{};float continuation[4]={1,0,0,0},coverageFade[4]={};
        if(views){
            if(!views->ViewArea(in,index,area))continue;
            water::SceneryImage image;
            const bool ready=views->DrawView(dev,in,index,image);
            if(!ready)return false;
            SetScenery(image.colour,image.depth,image.height,image.projection,
                       image.heights?nullptr:image.planes.data(),image.heights,image.underwater);
            m_sceneryViews=views;
            continuation[3]=image.receiverHeight;
            if(hasIndoorWater && image.hasIndoorWater)*hasIndoorWater=true;
            if(!slime || m_slime.outdoorThisFrame){
                continuation[0]=image.continuation[0];continuation[1]=image.continuation[1];
            }
            if(image.coverageWidth>0){
                const auto& edges=image.coverageEdges;
                const float halfX=(edges[2]-edges[0])*.5f,halfY=(edges[3]-edges[1])*.5f;
                coverageFade[0]=edges[0]+halfX;
                coverageFade[1]=edges[1]+halfY;
                coverageFade[2]=halfX-image.coverageWidth;
                coverageFade[3]=1.f/image.coverageWidth;
                continuation[2]=halfY-halfX; // zero in older square-coverage captures
            }
        }
        if (!m_sceneryColour || !m_sceneryDepth || frame[2][0]<=0 || frame[2][1]<=0)
            return Skip("scenery reflection missing or mismatched frame projection");
        const float mapping[4]={m_sceneryProjection[0]/frame[2][0],m_sceneryProjection[5]/frame[2][1],
            m_sceneryProjection[8]-frame[2][2]*(m_sceneryProjection[0]/frame[2][0]),
            m_sceneryProjection[9]-frame[2][3]*(m_sceneryProjection[5]/frame[2][1])};
        for(unsigned i=0;i<4;++i)if(!std::isfinite(mapping[i]))return Skip("invalid independent scenery projection");
        if(mapping[0]<.5f || mapping[0]>1.00001f || mapping[1]<.5f || mapping[1]>1.00001f ||
           std::fabs(mapping[2])>1 || std::fabs(mapping[3])>1)
            return Skip("unsupported independent scenery projection");
        D3DSURFACE_DESC geometry={};
        if (FAILED(m_sceneryDepth->GetLevelDesc(0,&geometry))) return false;
        const float grid[4]={float(geometry.Width),float(geometry.Height),1.0f/geometry.Width,1.0f/geometry.Height};
        const float plane[4]={m_sceneryHeight,m_sceneryProjection[10],m_sceneryProjection[14],m_sceneryHeightsActive?3.0f:(m_sceneryPlanesActive?2.0f:1.0f)};
        dev->SetPixelShaderConstantF(65,grid,1);dev->SetPixelShaderConstantF(66,plane,1);
        const float alignment[4]={cfg.reflectionSceneryAlign ? 1.0f:0.0f,cfg.reflectionSceneryShoreEdge ? 1.0f:0.0f,views?1.f:0.f,float(index+1)};
        dev->SetPixelShaderConstantF(67,alignment,1);
        dev->SetPixelShaderConstantF(68,m_sceneryPlanes,8);
        dev->SetPixelShaderConstantF(76,mapping,1);
        dev->SetPixelShaderConstantF(77,continuation,1);
        dev->SetPixelShaderConstantF(78,coverageFade,1);
        if (FAILED(BindPixelShader(dev, slime ? m_slimeReflectionPs : m_reflectionSceneryPs))) return false;
        BindPointTexture(dev,12,m_sceneryColour);BindPointTexture(dev,13,m_sceneryDepth);
        if(!BindPointTexture(dev,14,views?views->ReceiverMask():nullptr))return false;
        // Keep the first visible view as an exact replayable pass. The later
        // water composite captures the combined reflection. Repeating all shared
        // full-resolution inputs/outputs per tiny receiver makes captures unbounded.
        const std::string capture=m_reflectionCaptureDirectory.empty()? "":m_reflectionCaptureDirectory+
            "/rays";
        const bool captured=!drawn && !capture.empty() && reflection::CapturePassInputs(dev,capture,m_snapshotPs);
        ++m_work[0];
        if(views)area=reflection::ScaleArea(area,full.Width,full.Height,width,height);
        const bool resolved=SUCCEEDED(DrawScreenArea(dev,views?&area:nullptr));
        if(captured && resolved)reflection::CapturePassOutputs(dev,capture);
        if(!resolved)return false;
        drawn=true;
    }
    dev->SetTexture(12,nullptr);dev->SetTexture(13,nullptr);dev->SetTexture(14,nullptr);dev->SetRenderTarget(1,nullptr);
    return drawn;
}

// The wake map (wake_map_ps): each ship's place along its path at points over the square round its wake's box, walked
// once a frame rather than for every pixel of water (ShipWakeMap). The place changes smoothly (see water_common's
// AlongPath), so read between the points it is as good as walked for the pixel; each square's corner lies on a whole
// number of steps, so the points stay put in the world as the ship moves. Leaves the render targets, viewport, pixel
// shader, stage 10 and the first constants changed; false (and nothing drawn) when there are no ships.
bool Renderer::DrawShipWakeMap(IDirect3DDevice9* dev, const FrameInputs& in, const float (&frame)[9][4],
                               const float (&constants)[kWaterConstants][4], float (&squares)[kMaxBoats][4])
{
    const int ships = std::min(in.boatCount, kMaxBoats);
    if (ships <= 0)
        return false;
    for (IDirect3DTexture9*& map : m_wakeMap)
        if (!map && FAILED(dev->CreateTexture(kWakeMapSize, kWakeMapSize * kMaxBoats, 1, D3DUSAGE_RENDERTARGET,
                                              D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &map, nullptr)))
            return Skip("wake map could not be created");
    for (int i = 0; i < ships; ++i)
    {
        float* square = squares[i];
        const float across = std::max(square[2] - square[0], square[3] - square[1]);
        const float step = std::max(kWakeMapStep, across / (kWakeMapSize - 1));
        square[0] = std::floor(square[0] / step) * step;
        square[1] = std::floor(square[1] / step) * step;
        square[2] = square[0] + step * kWakeMapSize;
        square[3] = square[1] + step * kWakeMapSize;
    }
    if(!SetPairTargets(dev, m_wakeMap))return false;
    const D3DVIEWPORT9 vp = {0, 0, kWakeMapSize, kWakeMapSize * static_cast<DWORD>(ships), 0.0f, 1.0f};
    if(FAILED(dev->SetViewport(&vp)))return false;
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF)))return false;
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(kFrameConstants, &constants[0][0], kWaterConstants)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(kShipBoxRegister, &squares[0][0], kMaxBoats)))return false;
    if(FAILED(BindPixelShader(dev, m_wakeMapPs)))return false;
    if(!BindPointTexture(dev, kBoatPathStage, BoatPathTexture(dev, in)))return false;
    if(FAILED(DrawScreenTriangle(dev)))return false;
    if(FAILED(dev->SetRenderTarget(1, nullptr)))return false;
    return true;
}

// Before a pass that works out the ships' wakes (the surface detail pass, the underwater view): the optimisations being
// tried, and each ship's box, drawing the wake map (and binding it) when it is on. Leaves the render targets, viewport,
// pixel shader, stages 10, 12 and 13 and the first constants changed.
bool Renderer::BindLiquidOptions(IDirect3DDevice9* dev, const Config& cfg, bool mapped)
{
    const float options[4] = {mapped ? 1.0f : 0.0f, cfg.reflectionSurfaceFlat ? 1.0f : 0.0f,
                              static_cast<float>(cfg.reflectionView), 0.0f};
    if(FAILED(dev->SetPixelShaderConstantF(kLiquidOptionsRegister, options, 1)))return false;
    const float reflectionOptions[2][4] = {
        {0.0f, 0.0f, cfg.reflectionScenery ? 1.0f : 0.0f, 0.0f},
        {0.0f, cfg.reflectionWarp, cfg.reflectionBendReach, 0.0f}};
    if(FAILED(dev->SetPixelShaderConstantF(kReflectionOptionsRegister, &reflectionOptions[0][0], 2)))return false;
    return true;
}

bool Renderer::PrepareShipWakes(IDirect3DDevice9* dev, const FrameInputs& in, const Config& cfg,
                                const float (&frame)[9][4], const float (&constants)[kWaterConstants][4])
{
    float boxes[kMaxBoats][4];
    ShipWakeBoxes(in, boxes);
    const bool mapped=in.boatCount>0;
    if(mapped&&!DrawShipWakeMap(dev,in,frame,constants,boxes))return false;
    if(!BindLiquidOptions(dev,cfg,mapped))return false;
    if(FAILED(dev->SetPixelShaderConstantF(kShipBoxRegister, &boxes[0][0], kMaxBoats)))return false;
    if(!BindPointTexture(dev, kWakeMapPlaceStage, mapped ? m_wakeMap[0] : nullptr))return false;
    if(!BindPointTexture(dev, kWakeMapWayStage, mapped ? m_wakeMap[1] : nullptr))return false;
    return true;
}

// Candidate shader failures cannot disable ordinary water or retry creation every frame.
bool Renderer::PrepareWaveMoments(IDirect3DDevice9* dev, const Config& cfg)
{
    if (m_waveMomentsFailed) return false;
    const bool ok = EnsurePixelShader(dev, m_waterMomentsPs) && EnsurePixelShader(dev, m_surfaceMomentsPs);
    if (!ok) {
        m_waveMomentsFailed = true;
        WATER_LOG_ERROR("wave moment shader unavailable; the unfiltered water material stands in until Reset");
    }
    return ok;
}

bool Renderer::EnsureSurfaceDetailTargets(IDirect3DDevice9* dev, const D3DSURFACE_DESC& full, bool needSlopes)
{
    D3DSURFACE_DESC have = {};
    if (m_surfaceDetail[0] && (FAILED(m_surfaceDetail[0]->GetLevelDesc(0, &have)) || have.Width != full.Width ||
                               have.Height != full.Height))
    {
        SafeRelease(m_surfaceDetail[0]);
        SafeRelease(m_surfaceDetail[1]);
    }
    for (unsigned i = 0; i < (needSlopes ? 2u : 1u); ++i)
        if (!m_surfaceDetail[i] && FAILED(dev->CreateTexture(full.Width, full.Height, 1, D3DUSAGE_RENDERTARGET,
                D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &m_surfaceDetail[i], nullptr)))
            return Skip("surface detail target could not be created");
    return true;
}

// The surface detail pass (surface_detail_ps): at each water pixel the ships' wakes' slope and all the water's foam,
// worked out once for the reflection pass and the composite to read (water_ps: READS_SURFACE_DETAIL); worked out in
// the composite as well as everything else, they ran it out of registers. Leaves the render target, viewport, pixel
// shader, stages 0 to 11 and the first constants changed.
bool Renderer::DrawSurfaceDetail(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const FrameInputs& in, const Config& cfg,
                                 const float (&frame)[9][4], const float (&constants)[kWaterConstants][4])
{
    D3DSURFACE_DESC full = {};
    if (!m_snapshot || FAILED(m_snapshot->GetLevelDesc(0, &full)) || !EnsureSurfaceDetailTargets(dev, full, true))
        return false;
    if(!m_noiseLattice[0]&&!DrawNoiseLattice(dev)){
        SafeRelease(m_noiseLattice[0]);SafeRelease(m_noiseLattice[1]);return false;
    }
    if(!SetPairTargets(dev, m_surfaceDetail))return false;
    const D3DVIEWPORT9 whole = {0, 0, full.Width, full.Height, 0.0f, 1.0f};
    if(FAILED(dev->SetViewport(&whole)))return false;
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF)))return false;
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF)))return false;
    if(FAILED(dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0)))return false;
    const D3DVIEWPORT9 vp = FullDepth(in.viewport);
    if(FAILED(dev->SetViewport(&vp)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(kFrameConstants, &constants[0][0], kWaterConstants)))return false;
    if(FAILED(BindPixelShader(dev, m_usingWaveMoments ? m_surfaceMomentsPs : m_surfaceDetailPs)))return false;
    if(!BindWaterTextures(dev, depthTexture, in, cfg))return false;
    if(!BindPointTexture(dev, kNoiseLatticeStage, m_noiseLattice[0]))return false;
    if(!BindPointTexture(dev, kFoamPointsStage, m_noiseLattice[1]))return false;
    if(FAILED(DrawScreenTriangle(dev)))return false;
    if(FAILED(dev->SetRenderTarget(1, nullptr)))return false;
    return true;
}

// The noise's lattice and the foam network's wandering points (noise_lattice_ps), drawn once (again after a Reset) for
// the surface detail pass to read instead of working them out (NoiseTexture, FoamPointTexture). Leaves the render
// targets, viewport and pixel shader changed.
bool Renderer::DrawNoiseLattice(IDirect3DDevice9* dev)
{
    // A texel per cell over the hash's period (water_common's kHashPeriod).
    constexpr UINT kHashPeriod = 289;
    for (IDirect3DTexture9*& target : m_noiseLattice)
        if (!target && FAILED(dev->CreateTexture(kHashPeriod, kHashPeriod, 1, D3DUSAGE_RENDERTARGET,
                                                 D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &target, nullptr)))
        {
            SafeRelease(m_noiseLattice[0]);
            SafeRelease(m_noiseLattice[1]);
            return Skip("noise lattice textures could not be created");
        }
    if(!SetPairTargets(dev, m_noiseLattice))return false;
    const D3DVIEWPORT9 whole = {0, 0, kHashPeriod, kHashPeriod, 0.0f, 1.0f};
    if(FAILED(dev->SetViewport(&whole)))return false;
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF)))return false;
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF)))return false;
    if(FAILED(BindPixelShader(dev, m_noiseLatticePs)))return false;
    if(FAILED(DrawScreenTriangle(dev)))return false;
    if(FAILED(dev->SetRenderTarget(1, nullptr)))return false;
    WATER_LOG_INFO("noise lattice drawn (%ux%u) for NoiseTexture / FoamPointTexture", kHashPeriod, kHashPeriod);
    return true;
}

// The surface detail pass's results for the pass about to draw (none when it did not draw: no wakes, no foam).
bool Renderer::BindSurfaceDetail(IDirect3DDevice9* dev)
{
    const bool detail=BindPointTexture(dev,kSurfaceDetailStage,m_surfaceDetailDrawn?m_surfaceDetail[0]:nullptr);
    return BindPointTexture(dev,kSurfaceSlopesStage,m_surfaceDetailDrawn?m_surfaceDetail[1]:nullptr)&&detail;
}

// What lies under the water, for the refraction to bend into (underwater_scene_ps): the scene captured before the
// liquid where water covers it, with the water's thickness there, and what stands out of the water (a character's
// body, a plant) or lies dry filled in from the water around it (FillIn), so a view bent onto it finds the bottom
// beside it rather than a hole. Leaves the render targets, viewport, pixel shader, stages 0 to 3 and the first
// constants changed.
bool Renderer::DrawUnderwaterScene(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const FrameInputs& in, const Config& cfg,
                                   const float (&frame)[9][4], const float (&constants)[kWaterConstants][4])
{
    D3DSURFACE_DESC full = {};
    if (!m_snapshot || !m_sceneBefore || FAILED(m_snapshot->GetLevelDesc(0, &full)))
        return false;
    // At half the resolution: a texel for every 2x2 pixels, the water reading the seabed itself at full resolution and
    // only what the fill made up from here (see water_ps's Refracted).
    const auto layout = reflection::TargetLayout(full.Width, full.Height, in.viewport, true);
    const UINT width = layout.width;
    const UINT height = layout.height;
    if (!EnsureFillChain(dev, m_underwaterFill, width, height))
        return Skip("underwater scene buffers could not be created");
    if(!SetPairTargets(dev, m_underwaterFill.pull[0]))return false;
    const D3DVIEWPORT9 whole = {0, 0, width, height, 0.0f, 1.0f};
    if(FAILED(dev->SetViewport(&whole)))return false;
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF)))return false;
    if(FAILED(dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 0xF)))return false;
    if(FAILED(dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0f, 0)))return false;
    D3DVIEWPORT9 vp = FullDepth(in.viewport);
    vp.Width = (vp.X + vp.Width + 1) / 2 - vp.X / 2;
    vp.Height = (vp.Y + vp.Height + 1) / 2 - vp.Y / 2;
    vp.X /= 2;
    vp.Y /= 2;
    if(FAILED(dev->SetViewport(&vp)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants)))return false;
    if(FAILED(dev->SetPixelShaderConstantF(kFrameConstants, &constants[0][0], kWaterConstants)))return false;
    if(FAILED(BindPixelShader(dev, m_underwaterScenePs)))return false;
    if(!BindWaterTextures(dev, depthTexture, in, cfg))return false;
    if(FAILED(DrawScreenTriangle(dev)))return false;
    if(!FillIn(dev,m_underwaterFill))return false;
    return true;
}

// The liquids' clock (seconds, wrapping every hour so it keeps its precision), or the harness's fixed time.
float Renderer::LiquidSeconds(const Config& cfg)
{
    if (cfg.testSeconds >= 0.0f)
        return cfg.testSeconds;
    if (m_startTicks == 0)
        m_startTicks = Ticks();
    return static_cast<float>(std::fmod(SecondsSince(m_startTicks), kTimeWrapSeconds));
}

// How hard it rains now (0..1): the game's weather, or the test rain when one is set.
float Renderer::RainNow(const FrameInputs& in, const Config& cfg)
{
    return cfg.testRain >= 0.0f ? cfg.testRain : std::clamp(in.rain, 0.0f, 1.0f);
}

bool Renderer::Puddles(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                       const FrameInputs& in, const Config& cfg)
{
    PrepareRainRings(dev, boundDepth, in, cfg);
    const float rain = RainNow(in, cfg);
    const double elapsed = m_wetnessTicks ? std::min(SecondsSince(m_wetnessTicks), kLongestWetnessStep) : 0.0;
    m_wetnessTicks = Ticks();
    const float change = rain > 0.0f ? static_cast<float>(elapsed) * rain / cfg.puddleFillSeconds
                                     : -static_cast<float>(elapsed) / cfg.puddleDrySeconds;
    m_wetness = std::clamp(m_wetness + change, 0.0f, 1.0f);
    if (cfg.puddles <= 0.0f || (m_wetness <= 0.0f && rain <= 0.0f) || !in.ground || in.cameraInWater)
        return false;
    D3DSURFACE_DESC desc = {};
    float frame[9][4];
    float constants[kWaterConstants][4];
    if (FAILED(boundDepth->GetDesc(&desc)) || !BuildFrameConstants(in, cfg, desc, frame) ||
        !BuildWaterConstants(in, cfg, ConfigFile::WaterStyle(cfg), m_waterSurface, frame, constants))
        return false;
    IDirect3DTexture9* ground = GroundTexture(dev, in.ground);
    if (!ground)
        return false;
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    D3DSURFACE_DESC colorDesc = {};
    if (FAILED(saved.color[0]->GetDesc(&colorDesc)) || !EnsureSceneTargets(dev, colorDesc) ||
        !CopyToTexture(dev, saved.color[0], m_sceneWithWater))
    {
        saved.Restore();
        return Skip("scene copy for the puddles failed");
    }
    const float puddles[kPuddleConstants][4] = {
        {m_wetness, rain, cfg.puddles, cfg.rainRingStrength},
        {in.ground->Origin()[0], in.ground->Origin()[1], 1.0f / GroundMap::kArea, GroundMap::kCellSize},
    };
    const D3DVIEWPORT9 vp = FullDepth(in.viewport);
    dev->SetRenderTarget(0, saved.color[0]);
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kColorOnly);
    dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants);
    dev->SetPixelShaderConstantF(kFrameConstants, &constants[0][0], kWaterConstants);
    dev->SetPixelShaderConstantF(kPuddleRegister, &puddles[0][0], kPuddleConstants);
    BindRingMap(dev);
    if (FAILED(BindPixelShader(dev, m_puddlePs))) return false;
    BindPointTexture(dev, 0, depthTexture);
    BindPointTexture(dev, 1, depthTexture);
    BindLinearTexture(dev, 2, m_sceneWithWater);
    BindSky(dev, 5, cfg);
    BindLinearTexture(dev, 7, ground);
    const bool drawn=SUCCEEDED(DrawScreenTriangle(dev));
    if (!saved.Restore() || !drawn) return false;
    ++m_groundEffects.puddles;
    return true;
}

bool Renderer::EnsureProbeTargets(IDirect3DDevice9* dev)
{
    if (m_probeTarget)
        return true;
    if (SUCCEEDED(dev->CreateTexture(1, 1, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT,
                                     &m_probeTarget, nullptr)) &&
        SUCCEEDED(dev->CreateOffscreenPlainSurface(1, 1, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &m_probeReadback,
                                                   nullptr)))
        return true;
    SafeRelease(m_probeTarget);
    SafeRelease(m_probeReadback);
    return false;
}

bool Renderer::BeginPass(IDirect3DDevice9* dev, IDirect3DSurface9* boundDepth, SavedTargets& saved)
{
    if (m_passRestoreFailed) return Skip("host state restoration failed earlier in this frame");
    if (dev->TestCooperativeLevel() != D3D_OK)
        return Skip("device not ready");
    if (!EnsureShaders(dev) || !EnsureStateBlock(dev))
        return false;
    if (!saved.Capture(dev, m_state))
        return Skip("device state capture failed");
    if (saved.depth != boundDepth)
        return Skip("readable depth not bound");
    bool ready=true;
    for(DWORD i=1;i<4;++i)if(saved.color[i])ready=SUCCEEDED(dev->SetRenderTarget(i,nullptr))&&ready;
    ready=SetFullscreenState(dev)&&ready;
    if(!ready){saved.Restore();return Skip("fullscreen state setup failed");}
    return true;
}

bool Renderer::SetFullscreenState(IDirect3DDevice9* dev)
{
    return SetScreenPassState(dev,m_vs,m_decl);
}

bool Renderer::SnapshotBeforeLiquid(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                                    IDirect3DSurface9* boundDepth, bool captureScene)
{
    m_snapshotValid = false;
    m_sceneValid = false;
    D3DSURFACE_DESC desc = {};
    if (FAILED(boundDepth->GetDesc(&desc)) || !EnsureSnapshotTarget(dev, desc.Width, desc.Height))
        return false;
    IDirect3DSurface9* colour = nullptr;
    D3DSURFACE_DESC colourDesc = {};
    if (captureScene && SUCCEEDED(dev->GetRenderTarget(0,&colour)) &&
        SUCCEEDED(colour->GetDesc(&colourDesc)) && colourDesc.Width == desc.Width && colourDesc.Height == desc.Height)
        captureScene = EnsureSceneTargets(dev,colourDesc);
    else captureScene = false;
    SafeRelease(colour);
    m_snapshotValid = CopySceneDepth(dev,depthTexture,boundDepth,m_snapshot,captureScene ? m_sceneBefore : nullptr,m_sceneValid);
    if (m_snapshotValid) ++m_snapshots;
    return m_snapshotValid;
}

Renderer::CompositeScene::~CompositeScene()
{
    if (m_active) {
        std::swap(m_renderer.m_snapshot, m_renderer.m_compositeDepth);
        std::swap(m_renderer.m_sceneBefore, m_renderer.m_compositeScene);
    }
}

bool Renderer::CompositeScene::Capture(IDirect3DDevice9* dev, IDirect3DTexture9* depth,
                                       IDirect3DSurface9* boundDepth)
{
    auto& r = m_renderer;
    // A captured liquid always has a valid pre-liquid pair. Refuse a target transition rather than pairing
    // the old camera's opaque scene with a different-sized composite. The host starts a new capture there.
    D3DSURFACE_DESC z = {}, colour = {};
    IDirect3DSurface9* target = nullptr;
    const bool described = SUCCEEDED(dev->GetRenderTarget(0, &target)) && target &&
        SUCCEEDED(target->GetDesc(&colour)) && SUCCEEDED(boundDepth->GetDesc(&z));
    SafeRelease(target);
    if (m_active || !r.m_snapshotValid || !r.m_sceneValid || !described ||
        z.Width != r.m_snapshotWidth || z.Height != r.m_snapshotHeight ||
        colour.Width != z.Width || colour.Height != z.Height ||
        colour.Width != r.m_sceneDesc.Width || colour.Height != r.m_sceneDesc.Height ||
        colour.Format != r.m_sceneDesc.Format || colour.MultiSampleType != r.m_sceneDesc.MultiSampleType ||
        colour.MultiSampleQuality != r.m_sceneDesc.MultiSampleQuality)
        return r.Skip("deferred composite has no matching pre-liquid scene");
    if (r.m_compositeDesc.Width != colour.Width || r.m_compositeDesc.Height != colour.Height ||
        r.m_compositeDesc.Format != colour.Format) {
        SafeRelease(r.m_compositeDepth);
        SafeRelease(r.m_compositeScene);
        r.m_compositeDesc = {};
    }
    if (!r.m_compositeDepth && FAILED(dev->CreateTexture(z.Width, z.Height, 1, D3DUSAGE_RENDERTARGET,
        D3DFMT_R32F, D3DPOOL_DEFAULT, &r.m_compositeDepth, nullptr)))
        return r.Skip("deferred composite depth creation failed");
    if (!r.m_compositeScene && FAILED(dev->CreateTexture(colour.Width, colour.Height, 1, D3DUSAGE_RENDERTARGET,
        colour.Format, D3DPOOL_DEFAULT, &r.m_compositeScene, nullptr)))
        return r.Skip("deferred composite colour creation failed");
    r.m_compositeDesc = colour;
    bool colourValid = false;
    if (!r.CopySceneDepth(dev, depth, boundDepth, r.m_compositeDepth, r.m_compositeScene, colourValid) || !colourValid)
        return false;
    // No extra full-screen copy: the existing composite snapshot is written to its own reusable targets.
    std::swap(r.m_snapshot, r.m_compositeDepth);
    std::swap(r.m_sceneBefore, r.m_compositeScene);
    ++r.m_snapshots;
    m_active = true;
    return true;
}

bool Renderer::CopySceneDepth(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                             IDirect3DSurface9* boundDepth, IDirect3DTexture9* depthCopy,
                             IDirect3DTexture9* colourCopy, bool& colourValid)
{
    colourValid = false;
    D3DSURFACE_DESC desc = {};
    if (!depthCopy || FAILED(depthCopy->GetLevelDesc(0,&desc))) return false;
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    if (colourCopy) colourValid = CopyToTexture(dev,saved.color[0],colourCopy);
    if(!SetTarget(dev, depthCopy))return false;
    D3DVIEWPORT9 vp = {0, 0, desc.Width, desc.Height, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    const float texel[4] = {1.0f / desc.Width, 1.0f / desc.Height, 0.0f, 0.0f};
    dev->SetPixelShaderConstantF(1, texel, 1);
    if (FAILED(BindPixelShader(dev, m_snapshotPs))) return false;
    BindPointTexture(dev, 0, depthTexture);
    const bool ok = SUCCEEDED(DrawScreenTriangle(dev));
    return saved.Restore() && ok;
}

bool Renderer::CompositeWater(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                              const FrameInputs& in, const Config& cfg, bool shadeSurface, IDirect3DTexture9* nativeWater,
                              IDirect3DTexture9* refractionScene, IDirect3DTexture9* surfaceVisibility)
{
    m_distanceAtmosphereShaded=false;
    if(m_surfaceResult==SurfaceResult::AbortFrame)return Skip("water frame already abandoned after unrecoverable failure");
    m_surfaceResult=SurfaceResult::Unchanged;
    shadeSurface=shadeSurface&&cfg.surface;
    PrepareRainRings(dev,boundDepth,in,cfg);
    m_usingEnvironment=cfg.waterAtmosphere&&m_environment&&m_environment->Available();
    m_usingWaveMoments = false;
    struct SurfaceEnvironmentScope {bool& active;bool& moments;~SurfaceEnvironmentScope(){active=false;moments=false;}} environmentScope{m_usingEnvironment,m_usingWaveMoments};
    if (!m_snapshotValid || !m_sceneValid)
        return Skip("no scene snapshot before the liquid pass");
    D3DSURFACE_DESC desc = {};
    float frame[9][4];
    if (FAILED(boundDepth->GetDesc(&desc)) || !BuildFrameConstants(in, cfg, desc, frame))
        return false;
    // A host's material lighting belongs in the refraction source, not on the
    // completed water. Borrow its matching copy for this call; preserve the
    // original scene for underwater, later liquids, failures and the next frame.
    struct RefractionColourScope {
        IDirect3DTexture9*& current; IDirect3DTexture9* original;
        ~RefractionColourScope() { current = original; }
    } colourScope{m_sceneBefore, m_sceneBefore};
    if (refractionScene) {
        D3DSURFACE_DESC supplied = {};
        const bool matching = SUCCEEDED(refractionScene->GetLevelDesc(0, &supplied)) &&
            supplied.Width == m_sceneDesc.Width && supplied.Height == m_sceneDesc.Height &&
            supplied.Format == m_sceneDesc.Format;
        if (!matching || refractionScene == m_sceneWithWater) return Skip("invalid host refraction scene");
        m_sceneBefore = refractionScene;
    }
    if (surfaceVisibility) {
        D3DSURFACE_DESC supplied = {};
        if (FAILED(surfaceVisibility->GetLevelDesc(0, &supplied)) ||
            supplied.Width != desc.Width || supplied.Height != desc.Height || supplied.Format != D3DFMT_G16R16F)
            return Skip("invalid host surface visibility");
    }
    m_environmentRayStep=2.f*(std::fabs(frame[4][2]/(frame[0][2]*frame[2][0]))+
                             std::fabs(frame[5][2]/(frame[0][3]*frame[2][1])));
    if(m_usingEnvironment&&!std::isfinite(m_environmentRayStep))return Skip("invalid water projection footprint");
    float constants[kWaterConstants][4];
    if (!BuildWaterConstants(in, cfg, Weathered(ConfigFile::WaterStyle(cfg), in, cfg), m_waterSurface, frame, constants))
        return false;

    // From below, the underwater view draws the surface itself from the scene captured before the liquid pass;
    // shading the client's surface as seen from above would only put a wrong layer over it.
    const bool seenFromBelow = reflection::WaterSurfaceSeenFromBelow(cfg.underwater,in.cameraInWater);
    if (seenFromBelow) {
        // Northlight supplies its water-only mask; standalone reaches this boundary before later models.
        // Keep the actual liquid geometry for the underside, not the camera's single horizontal plane.
        m_nativeWater = nativeWater;
        if (!nativeWater) {
            D3DSURFACE_DESC have={};
            if (m_waterDepth && (FAILED(m_waterDepth->GetLevelDesc(0,&have)) || have.Width!=desc.Width || have.Height!=desc.Height))
                SafeRelease(m_waterDepth);
            if (!m_waterDepth && FAILED(dev->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,
                                                          D3DPOOL_DEFAULT,&m_waterDepth,nullptr)))
                return Skip("underwater liquid depth creation failed");
            bool unused=false;
            m_waterDepthValid=CopySceneDepth(dev,depthTexture,boundDepth,m_waterDepth,nullptr,unused);
            if (!m_waterDepthValid) return false;
        }
    }
    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev,boundDepth,saved)) {
        if(m_passRestoreFailed)m_surfaceResult=SurfaceResult::AbortFrame;
        return false;
    }
    bool attempted=false,shaded=false;
    IDirect3DSurface9* rollback=nullptr;
    // The original native colour is retained until the surface transaction ends.
    // A failed draw may have written part of its target despite returning failure.
    auto finish=[&](bool success){
        IDirect3DSurface9* original=saved.color[0];if(original)original->AddRef();
        const bool restored=saved.Restore();
        bool recovered=true;
        if(attempted&&(!shaded||!restored)){
            IDirect3DSurface9* baseline=nullptr;
            recovered=original && (rollback || SUCCEEDED(m_sceneWithWater->GetSurfaceLevel(0,&baseline)));
            if(recovered)recovered=SUCCEEDED(dev->StretchRect(rollback?rollback:baseline,nullptr,original,nullptr,D3DTEXF_NONE));
            SafeRelease(baseline);
        }
        SafeRelease(original);
        if(!restored||!recovered)m_surfaceResult=SurfaceResult::AbortFrame;
        else if(shaded)m_surfaceResult=SurfaceResult::Shaded;
        else if(attempted)m_surfaceResult=SurfaceResult::Recovered;
        const bool surfaceOutput=cfg.reflectionView==0&&cfg.refractionDebug==0&&cfg.seabedDebug==0&&cfg.reflectionDebug==0;
        m_distanceAtmosphereShaded=success&&restored&&recovered&&shaded&&m_usingEnvironment&&surfaceOutput&&cfg.waterAtmosphere==1;
        return success&&restored&&recovered;
    };
    bool ok=CopyToTexture(dev,saved.color[0],m_sceneWithWater);
    if (ok)
    {
        TimeBegin(GpuTimer::WaterSurface);
        ok=SUCCEEDED(dev->SetPixelShaderConstantF(0,&frame[0][0],kFrameConstants))&&
            MeasureLevel(dev,depthTexture,m_level,m_levelIndex)&&
            AdvanceSurface(dev,m_waterSurface,m_level[0]?m_level[m_levelIndex]:nullptr,WaterPhysics(cfg),in,cfg,frame,constants);
        TimeEnd(GpuTimer::WaterSurface);
    }
    // SkipEmptyWater suppresses only the surface shading work: snapshots, level tracking, simulation and the
    // end-of-world sky memory keep their existing lifetime, including frames when no water draw was submitted.
    if (ok && !seenFromBelow && shadeSurface)
    {
        BeginReflectionCapture(cfg, in);
        m_usingWaveMoments = FilteredWaterMaterial(cfg,m_usingEnvironment) && PrepareWaveMoments(dev,cfg);
        const bool controls=BindWaterControls(dev,cfg,m_usingEnvironment,m_usingWaveMoments);
        TimeBegin(GpuTimer::WaterDetail);
        m_surfaceDetailDrawn=controls&&PrepareShipWakes(dev,in,cfg,frame,constants)&&DrawSurfaceDetail(dev,depthTexture,in,cfg,frame,constants);
        TimeEnd(GpuTimer::WaterDetail);
        if(!m_surfaceDetailDrawn)return finish(false);
        bool hasIndoorWater=false;
        const bool reflections = cfg.screenReflections && DrawReflections(dev, depthTexture, in, cfg, frame, constants, false, &hasIndoorWater);
        if (cfg.reflectionScenery && cfg.screenReflections && !reflections)
        {
            m_candidateFailure = "reflection candidate unavailable; water surface comparison skipped";
            finish(false);
            return Skip(m_candidateFailure);
        }
        TimeBegin(GpuTimer::WaterUnderwaterScene);
        m_underwaterSceneDrawn = DrawUnderwaterScene(dev, depthTexture, in, cfg, frame, constants);
        TimeEnd(GpuTimer::WaterUnderwaterScene);
        const D3DVIEWPORT9 vp=FullDepth(in.viewport);
        // Optional refraction preparation can fail with a smaller MRT still bound.
        // Detach it before binding the native target for raw-refraction fallback.
        bool bound=SUCCEEDED(dev->SetRenderTarget(1,nullptr));
        bound=SUCCEEDED(dev->SetRenderTarget(0,saved.color[0]))&&bound;
        bound=SUCCEEDED(dev->SetViewport(&vp))&&bound;
        bound=SUCCEEDED(dev->SetRenderState(D3DRS_COLORWRITEENABLE,kColorOnly))&&bound;
        bound=SUCCEEDED(dev->SetPixelShaderConstantF(0,&frame[0][0],kFrameConstants))&&bound;
        bound=SUCCEEDED(dev->SetPixelShaderConstantF(kFrameConstants,&constants[0][0],kWaterConstants))&&bound;
        IDirect3DPixelShader9*& composite=m_usingWaveMoments ? m_waterMomentsPs : m_waterPs;
        IDirect3DPixelShader9*& fine=m_usingWaveMoments ? m_waterMomentsFinePs : m_waterFinePs;
        bound=SUCCEEDED(BindHighlightShader(dev,cfg,composite,fine))&&bound;
        bound=BindWaterControls(dev,cfg,m_usingEnvironment,m_usingWaveMoments,surfaceVisibility!=nullptr,reflections && hasIndoorWater)&&bound;
        bound=BindWaterTextures(dev,depthTexture,in,cfg)&&bound;
        bound=BindSurfaceDetail(dev)&&bound;
        // Composite normals/foam already come from surface detail: s9 no longer
        // reads rain rings here. The map is borrowed only for this draw.
        bound=BindPointTexture(dev,WATER_SLOT_sSurfaceVisibility,surfaceVisibility)&&bound;
        D3DSURFACE_DESC reflectionDesc = {};
        IDirect3DTexture9* const* result = m_reflections;
        const bool drawn = reflections && SUCCEEDED(result[0]->GetLevelDesc(0, &reflectionDesc));
        const float grid[4] = {static_cast<float>(reflectionDesc.Width), static_cast<float>(reflectionDesc.Height),
                               (drawn ? 1.0f : 0.0f) + (m_underwaterSceneDrawn ? 2.0f : 0.0f),
                               static_cast<float>(cfg.reflectionDebug)};
        bound=SUCCEEDED(dev->SetPixelShaderConstantF(kReflectionRegister,grid,1))&&bound;
        bound=BindPointTexture(dev,kReflectionStage,drawn?result[0]:nullptr)&&bound;
        bound=BindPointTexture(dev,kReflectionHitStage,drawn?result[1]:nullptr)&&bound;
        bound=BindLinearTexture(dev,kUnderwaterSceneStage,m_underwaterSceneDrawn?m_underwaterFill.push[0][0]:nullptr)&&bound;
        bound=BindLinearTexture(dev,kUnderwaterThicknessStage,m_underwaterSceneDrawn?m_underwaterFill.push[0][1]:nullptr)&&bound;
        const std::string capture = m_reflectionCaptureDirectory.empty() ? "" : m_reflectionCaptureDirectory + "/composite";
        const bool captured = !capture.empty() && reflection::CapturePassInputs(dev, capture, m_snapshotPs,ActiveCaptureBurst());
        TimeBegin(GpuTimer::WaterComposite);
        if(bound && m_sceneDesc.MultiSampleType!=D3DMULTISAMPLE_NONE){
            bound=screen_pass::SaveMultisampleColour(dev,saved.color[0],m_sceneDesc,m_multisampleRollback);
            rollback=m_multisampleRollback;
        }
        if(bound){attempted=true;shaded=SUCCEEDED(DrawScreenTriangle(dev));}
        TimeEnd(GpuTimer::WaterComposite);
        if(captured)reflection::CapturePassOutputs(dev,capture,"output",ActiveCaptureBurst());
        ok=bound&&shaded;
        if(shaded)++m_composites;
        if(!ok)Skip(bound?"water surface draw failed":"water surface bindings failed");
    }
    return finish(ok);
}

// Water under fog, cloud or rain: less light reaches the bottom and the grey sky's reflection dominates, so the
// colours turn duller and greyer, shallow and deep closer together, tinted a little by the fog. How overcast it is
// comes from the game's storm weight and how near its fog closes in.
float Renderer::OvercastAmount(const FrameInputs& in, const Config& cfg)
{
    const float clearFog = std::clamp((in.fogEnd - kFoggiestFogEnd) / (kClearFogEnd - kFoggiestFogEnd), 0.0f, 1.0f);
    const float overcast = 1.0f - (1.0f - std::clamp(in.stormWeight, 0.0f, 1.0f)) * clearFog;
    return overcast * cfg.overcastWater;
}

LiquidStyle Renderer::Weathered(const LiquidStyle& style, const FrameInputs& in, const Config& cfg)
{
    const float amount = OvercastAmount(in, cfg);
    if (amount <= 0.0f)
        return style;
    float fog[4];
    ColorToFloat(in.fogColor, 1.0f, fog);
    const float fogLuma = std::max(0.2126f * fog[0] + 0.7152f * fog[1] + 0.0722f * fog[2], 1.0e-3f);
    auto weather = [&](const float* in3, float* out3) {
        const float luma = 0.2126f * in3[0] + 0.7152f * in3[1] + 0.0722f * in3[2];
        for (int i = 0; i < 3; ++i)
        {
            const float muted = luma + (fog[i] / fogLuma * luma - luma) * kOvercastFogTint;
            out3[i] = (in3[i] + (muted - in3[i]) * amount) * (1.0f - kOvercastDimming * amount);
        }
    };
    LiquidStyle weathered = style;
    weather(style.shallowColor, weathered.shallowColor);
    weather(style.deepColor, weathered.deepColor);
    weather(style.abyssColor, weathered.abyssColor);
    return weathered;
}

WaterEnvironmentValues Renderer::NativeEnvironment(const FrameInputs& in)
{
    WaterEnvironmentValues values={};values.policy=WaterEnvironmentCoreOwnsDistance;
    values.encoding=WaterEnvironmentSurfaceCompositeRGB;values.paletteKnown=in.skyPaletteKnown?1:0;
    std::memcpy(values.palette,in.skyPalette,sizeof values.palette);
    values.environmentKey=in.mapId>=0?uint64_t(in.mapId)+1:0;
    values.light=water::NativeWaterLight(in.toLight,in.sunColor,in.lightIsMoon,1-std::clamp(in.stormWeight,0.f,1.f));
    values.referenceDistance=std::max(FarClipDistance(in.glProjection),1.f);
    values.transmissionAtReference=.35f;
    return values;
}

bool Renderer::BuildWaterConstants(const FrameInputs& in, const Config& cfg, const LiquidStyle& style,
                                   const SurfaceSim& sim, const float (&frame)[9][4],
                                   float (&constants)[kWaterConstants][4])
{
    const float seconds = LiquidSeconds(cfg);
    const float lightScale = in.lightIsMoon ? kMoonLightScale : 1.0f;
    float viewToWorld[16];
    std::memset(constants,0,sizeof(constants));
    const auto row = [&](UINT slot) { return constants[slot-kFrameConstants]; };
    const auto set = [&](UINT slot,const std::array<float,4>& values) {
        std::copy(values.begin(),values.end(),row(slot));
    };
    set(WATER_SLOT_cWaterTimeWavesFoamClarity,{seconds, style.waveStrength, style.foamStrength, style.clarity});
    set(WATER_SLOT_cWaterReflectSpecTint,{style.reflection, cfg.specular, style.stockTint, cfg.screenReflections ? 1.0f : 0.0f});
    set(WATER_SLOT_cToLight,{in.toLight[0], in.toLight[1], in.toLight[2],
         lightScale * std::clamp((in.toLight[2] + kHorizonFadeBelow) / kHorizonFadeWidth, 0.0f, 1.0f)});
    set(WATER_SLOT_cFogRange,{in.fogStart, in.fogEnd, 1.0f - in.stormWeight, style.waveSpeed});
    set(WATER_SLOT_cShallowColor,{style.shallowColor[0], style.shallowColor[1], style.shallowColor[2], FarClipDistance(in.glProjection)});
    set(WATER_SLOT_cDeepColorDepth,{style.deepColor[0], style.deepColor[1], style.deepColor[2], style.gradientDepth});
    set(WATER_SLOT_cStyleExtras,{cfg.underwaterVisibility, style.foamDepth, style.stockRelief, in.daylight});
    set(WATER_SLOT_cFoamColor,{style.foamColor[0], style.foamColor[1], style.foamColor[2], style.foamGlow});
    set(WATER_SLOT_cInsideAndReflection,{style.surfaceTransmission, style.insideBrightness, style.nearReflection, cfg.reflectionRamp});
    set(WATER_SLOT_cRippleArea,{sim.origin[0], sim.origin[1], 1.0f / (kRippleCells * kRippleCellSize),
         cfg.ripples && sim.valid ? cfg.rippleStrength * style.rippleResponse : 0.0f});
    set(WATER_SLOT_cRippleGrid,{1.0f / kRippleCells, kRippleCellSize, cfg.scatter * style.rippleResponse, style.waveCrestShape});
    set(WATER_SLOT_cAbyss,{style.abyssColor[0], style.abyssColor[1], style.abyssColor[2], style.abyssDepth});
    set(kUnderwaterWindowRegister,{cfg.underwaterWindow, static_cast<float>(cfg.underwaterDebug), cfg.underwaterGlancingMirror, 0.0f});
    set(kTwilightRegister,{cfg.twilightShallowColor[0], cfg.twilightShallowColor[1], cfg.twilightShallowColor[2], in.goldenHour});
    set(WATER_SLOT_cGroundInfo,{static_cast<float>(cfg.seabedDebug), in.stockFogPushed ? 1.0f : 0.0f,
         in.ground && in.ground->NearbyWaterKnown() ? 1.0f : 0.0f, OvercastAmount(in, cfg)});
    set(WATER_SLOT_cSurface,{style.surfaceDeepness, in.lightIsMoon ? 1.0f : 0.0f, RainNow(in, cfg), cfg.rainRingStrength});
    set(WATER_SLOT_cLookingDown,{cfg.surfaceBlur, cfg.surfaceVeil, style.waveCrestSharpness, cfg.openWaterFoam});
    std::memcpy(viewToWorld, frame[WATER_SLOT_cViewToWorld0], sizeof(viewToWorld));
    if (!Invert4x4(viewToWorld, row(WATER_SLOT_cWorldToView0)))
        return Skip("world-to-view matrix not invertible");
    ColorToFloat(in.directColor, 1.0f, row(WATER_SLOT_cDirectColor));
    ColorToFloat(in.ambientColor, 1.0f, row(WATER_SLOT_cAmbientColor));
    ColorToFloat(in.fogColor, 1.0f, row(WATER_SLOT_cFogColor));
    if (in.lightIsMoon)
        std::memcpy(row(WATER_SLOT_cSunColor), kMoonTint, sizeof(kMoonTint));
    else
        ColorToFloat(in.sunColor, 1.0f, row(WATER_SLOT_cSunColor));
    if(m_usingEnvironment&&m_environment){
        const auto& source=m_environment->Values().light;
        for(unsigned i=0;i<3;++i){row(WATER_SLOT_cToLight)[i]=source.direction[i];row(WATER_SLOT_cSunColor)[i]=source.colour[i];}
        row(WATER_SLOT_cToLight)[3]=source.visibility;row(WATER_SLOT_cFogRange)[2]=1; // visibility already includes weather once
        row(WATER_SLOT_cSurface)[1]=source.moon?1.f:0.f;
    }
    // Ships for their wakes (rows c34..c41: position and speed, then heading and hull size; their paths are in
    // BoatPathTexture).
    for (int i = 0; i < in.boatCount && i < kMaxBoats; ++i)
    {
        const Boat& boat = in.boats[i];
        float* where = row(WATER_SLOT_cBoat+i);
        float* heading = row(WATER_SLOT_cBoatShape+i);
        where[0] = boat.position[0];
        where[1] = boat.position[1];
        where[2] = boat.position[2];
        where[3] = boat.speed;
        heading[0] = boat.direction[0];
        heading[1] = boat.direction[1];
        heading[2] = kHullHalfLength;
        heading[3] = kHullHalfBeam;
    }
    return true;
}

bool Renderer::DrawUnderwaterFoam(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture,
                                  const D3DSURFACE_DESC& desc, const FrameInputs& in, const Config& cfg,
                                  const float (&frame)[9][4], const float (&water)[kWaterConstants][4])
{
    // The surface composite has consumed its detail buffer by the end-of-world underwater pass.
    m_surfaceDetailDrawn = false;
    if (!EnsureSurfaceDetailTargets(dev, desc, false) || !SetTarget(dev, m_surfaceDetail[0])) return false;
    const D3DVIEWPORT9 vp = FullDepth(in.viewport);
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants);
    dev->SetPixelShaderConstantF(kFrameConstants, &water[0][0], kWaterConstants);
    BindWaterControls(dev, cfg, m_usingEnvironment, m_usingWaveMoments);
    if (FAILED(BindPixelShader(dev, m_underwaterFoamPs))) return false;
    BindPointTexture(dev, 1, m_snapshotValid && m_sceneValid ? m_snapshot : depthTexture);
    BindPointTexture(dev, 4, m_level[0] ? m_level[m_levelIndex] : nullptr);
    BindLinearTexture(dev, 6, m_waterSurface.Ripples());
    BindLinearTexture(dev, 7, GroundTexture(dev, in.ground));
    BindLinearTexture(dev, 8, in.ground ? m_shore : nullptr);
    BindPointTexture(dev, kBoatPathStage, BoatPathTexture(dev, in));
    DrawScreenTriangle(dev);
    return true;
}

void Renderer::DrawUnderwater(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* target,
                              const FrameInputs& in, const Config& cfg, const float (&frame)[9][4])
{
    const bool slime = in.cameraInSlime;
    float water[kWaterConstants][4];
    D3DSURFACE_DESC desc = {};
    SurfaceSim& surface = slime ? m_slimeSurface : m_waterSurface;
    if (!BuildWaterConstants(in, cfg, slime ? cfg.slimeStyle : Weathered(ConfigFile::WaterStyle(cfg), in, cfg), surface,
                             frame, water) ||
        FAILED(target->GetDesc(&desc)) || !EnsureSceneTargets(dev, desc) ||
        !CopyToTexture(dev, target, m_sceneWithWater))
        return;
    ++m_work[slime?8:7];
    if(cfg.waterWorkAudit)TimeBegin(GpuTimer::UnderwaterView);
    if (slime)
    {
        water[kUnderwaterRegister - kFrameConstants][0] = cfg.slimeVisibility;
        water[kUnderwaterWindowRegister - kFrameConstants][0] = cfg.slimeWindow;
        water[kUnderwaterWindowRegister - kFrameConstants][2] = cfg.slimeGlancingMirror;
        water[kUnderwaterWindowRegister - kFrameConstants][3] = 1.0f;
        std::memcpy(water[kTwilightRegister - kFrameConstants], cfg.slimeInsideColor, sizeof(cfg.slimeInsideColor));
        std::memcpy(water[kAbyssRegister - kFrameConstants], cfg.slimeInsideDeepColor,
                    sizeof(cfg.slimeInsideDeepColor));
    }
    else
    {
        if(!AdvanceSurface(dev,m_waterSurface,m_level[0]?m_level[m_levelIndex]:nullptr,WaterPhysics(cfg),in,cfg,frame,water)){
            if(cfg.waterWorkAudit)TimeEnd(GpuTimer::UnderwaterView);
            return;
        }
    }
    if(!PrepareShipWakes(dev,in,cfg,frame,water)){
        if(cfg.waterWorkAudit)TimeEnd(GpuTimer::UnderwaterView);
        return;
    }
    // Under slime as under water: the mirrored scene below the surface shows on its underside (2026-11-09).
    const bool scenery=cfg.underwaterScenery && cfg.screenReflections && in.CameraUnderSurface();
    const bool source=scenery && m_sceneryUnderwater && m_sceneryColour && m_sceneryDepth &&
        std::fabs(m_sceneryProjection[0]-frame[2][0])<=1e-5f &&
        std::fabs(m_sceneryProjection[5]-frame[2][1])<=1e-5f &&
        std::fabs(m_sceneryProjection[8]-frame[2][2])<=1e-5f &&
        std::fabs(m_sceneryProjection[9]-frame[2][3])<=1e-5f;
    const float currentWater=in.cameraWaterHeight;
    const bool nativeWater=scenery && in.cameraWaterHeightKnown;
    IDirect3DTexture9* nativeSurface = slime ? (m_slimeCaptured ? m_slime.depth : nullptr) :
        (m_nativeWater ? m_nativeWater : (m_waterDepthValid ? m_waterDepth : nullptr));
    const float nativeMode = nativeSurface ? (!slime && m_nativeWater ? 1.f : 2.f) : 0.f;
    float nativePlane[4]={nativeWater?1.f:0.f,currentWater,nativeMode,0};
    // The foam pass has no live-depth input; s0 reads the same native surface there.
    BindPointTexture(dev,0,nativeSurface);
    dev->SetPixelShaderConstantF(67,nativePlane,1);
    const bool foam = !slime && (cfg.sharedUnderwaterFoam || scenery) && DrawUnderwaterFoam(dev, depthTexture, desc, in, cfg, frame, water);
    dev->SetRenderTarget(0, target);
    BindPointTexture(dev, kUnderwaterFoamStage, foam ? m_surfaceDetail[0] : nullptr);
    const D3DVIEWPORT9 vp = FullDepth(in.viewport);
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kColorOnly);
    dev->SetPixelShaderConstantF(0, &frame[0][0], kFrameConstants);
    dev->SetPixelShaderConstantF(kFrameConstants, &water[0][0], kWaterConstants);
    // Slime has its own absorption and no foam texture. The same shader can
    // show the ordinary underside while a new scene or device is warming up.
    const bool slimeMirror = slime && scenery && EnsurePixelShader(dev, m_slimeUnderwaterSceneryPs);
    IDirect3DPixelShader9*& underside = slimeMirror ? m_slimeUnderwaterSceneryPs :
        (!slime && scenery ? m_underwaterSceneryPs : (foam ? m_underwaterSharedPs : m_underwaterPs));
    if (FAILED(BindPixelShader(dev, underside))) return;
    D3DSURFACE_DESC mirror={};if(source)m_sceneryDepth->GetLevelDesc(0,&mirror);
    const float reflected[4]={source?1.f:0.f,m_sceneryHeight,float(mirror.Width),float(mirror.Height)};
    dev->SetPixelShaderConstantF(65,reflected,1);
    // Long sightlines near the surface still see sunlit water. The shader keeps
    // the full path for absorption and adds the real descent when looking down.
    float distanceAsDepth = 0.5f;
    float shallowDaylight = 0.f;
    if (!slime && in.cameraWaterHeightKnown)
    {
        const float gradientDepth = water[WATER_SLOT_cDeepColorDepth - kFrameConstants][3];
        const float cameraDepth = currentWater - in.cameraPosition[2];
        const float shallow = std::clamp(1.f - cameraDepth / (2.f * gradientDepth), 0.f, 1.f);
        distanceAsDepth -= 0.35f * shallow;
        shallowDaylight = 0.45f * shallow * std::clamp(in.daylight, 0.f, 1.f);
    }
    const float sourceProjection[4]={m_sceneryProjection[10],m_sceneryProjection[14],shallowDaylight,distanceAsDepth};
    dev->SetPixelShaderConstantF(66,sourceProjection,1);
    // What lies beyond the surface comes from the depth and colour captured before the client drew its own surface
    // (seen from below it would hide everything above).
    const bool beyondSurface = m_snapshotValid && m_sceneValid;
    BindPointTexture(dev, 0, depthTexture);
    BindPointTexture(dev, 1, beyondSurface ? m_snapshot : depthTexture);
    BindLinearTexture(dev, 2, beyondSurface ? m_sceneBefore : m_sceneWithWater);
    BindPointTexture(dev, 3, m_sceneWithWater);
    if (slime)
        BindPointTexture(dev, 4, m_slime.level[0] ? m_slime.level[m_slime.levelIndex] : nullptr);
    else
        BindPointTexture(dev, 4, m_level[0] ? m_level[m_levelIndex] : nullptr);
    const float airWeight = !slime && beyondSurface ? ObservedAirWeight(in) : 0.f;
    if (airWeight > 0 && BindPointTexture(dev, WATER_SLOT_sRememberedSky, m_observedAir))
    {
        nativePlane[3] = airWeight;
        dev->SetPixelShaderConstantF(67, nativePlane, 1);
    }
    BindLinearTexture(dev, 6, surface.Ripples());
    BindLinearTexture(dev, 7, slime ? nullptr : GroundTexture(dev, in.ground));
    BindLinearTexture(dev, 8, !slime && in.ground ? m_shore : nullptr);
    BindPointTexture(dev, kBoatPathStage, slime ? nullptr : BoatPathTexture(dev, in));
    if (slimeMirror || (!slime && scenery)) {
        BindPointTexture(dev, 12, source ? m_sceneryColour : nullptr);
        BindPointTexture(dev, 13, source ? m_sceneryDepth : nullptr);
    }
    // Only the water variants using shared foam have their native surface on s10.
    BindPointTexture(dev, !slime && (scenery || foam) ? 10 : 11, nativeSurface);
    DrawScreenTriangle(dev);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    if(cfg.waterWorkAudit)TimeEnd(GpuTimer::UnderwaterView);
    ++m_underwaterFrames;
}

bool Renderer::Render(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* boundDepth,
                      const FrameInputs& in, const Config& cfg)
{
    if (!cfg.underwater || in.mapId != m_airMap || in.weatherType != m_airWeather)
        m_airMap = -1;
    if (cfg.reflectionCapture <= 0 || cfg.reflectionCapture != m_reflectionCaptureRequest) EndReflectionCapture(true);
    const char* candidateFailure = m_candidateFailure;
    m_candidateFailure = nullptr;
    m_skip = "";
    ++m_frame;
    bool captureNow = m_captureLeft > 0;
    if (!captureNow && cfg.captureFrame > 0 && cfg.captureFrame != m_capturedRequest)
    {
        captureNow = m_pendingCapture == cfg.captureFrame;
        m_pendingCapture = cfg.captureFrame;
    }
    const D3DVIEWPORT9& vp = in.viewport;
    const float* proj = in.glProjection;
    D3DSURFACE_DESC desc = {};
    if (FAILED(boundDepth->GetDesc(&desc)))
        return Skip("depth description failed");
    if (m_lastSummary == 0 || SecondsSince(m_lastSummary) > kSummarySeconds)
    {
        m_lastSummary = Ticks();
        WATER_LOG_INFO("frame %u: surface %s, %s view; viewport %lu,%lu %lux%lu of %ux%u depth %.4f..%.4f; near %.3f "
                       "far %.1f; camera (%.2f %.2f %.2f)%s",
                       m_frame, cfg.surface ? "on" : "off", DebugViewName(cfg.debugView), vp.X, vp.Y, vp.Width,
                       vp.Height, desc.Width, desc.Height, vp.MinZ, vp.MaxZ, -proj[14] / (1.0f + proj[10]),
                       proj[14] / (1.0f - proj[10]), in.cameraPosition[0], in.cameraPosition[1], in.cameraPosition[2],
                       in.cameraInWater ? " under water" : "");
        WATER_LOG_INFO("passes: %u depth snapshots, %u water composites, %u slime composites, %u underwater views, %u "
                       "ambient occlusion, %u puddles, %u end-of-world; rain: weather %u at %.2f, rain %.2f, puddles "
                       "%.2f full, %u rings",
                       m_snapshots, m_composites, m_slimeComposites, m_underwaterFrames, m_groundEffects.occlusion,
                       m_groundEffects.puddles, m_rendered, in.weatherType, in.weatherIntensity, in.rain, m_wetness,
                       m_ringCount);
        WATER_LOG_INFO("lava: %u captures, %u composites, %u stir steps, %u contact steps%s", m_lava.work.captures,
                       m_lava.work.composites, m_lava.work.stirSteps, m_lava.work.contactSteps, m_lava.hidden ? ", out of sight" : "");
        WATER_LOG_INFO("light (%.2f %.2f %.2f) %s: direct %08X sun %08X ambient %08X; fog %08X %.0f..%.0f, storm %.2f",
                       in.toLight[0], in.toLight[1], in.toLight[2], in.lightIsMoon ? "moon" : "sun", in.directColor,
                       in.sunColor, in.ambientColor, in.fogColor, in.fogStart, in.fogEnd, in.stormWeight);
    }
    const bool overlay = cfg.debugView != DebugView::Off;
    const bool underwater = (cfg.underwater && in.cameraInWater) || (cfg.slime && in.cameraInSlime);
    // (With water this frame, the sky is remembered for its reflections whatever else is drawn: going no further without
    // anti-aliasing, the sky memory never changed.)
    if (!overlay && !cfg.antiAliasing && !underwater && !m_snapshotValid)
    {
        if (captureNow)
            Capture(dev, cfg.captureFrame, cfg.captureFrames);
        return Skip(candidateFailure ? candidateFailure : "nothing to draw at the end of the world pass");
    }
    float constants[9][4];
    if (!BuildFrameConstants(in, cfg, desc, constants))
        return false;

    SavedTargets saved(m_passRestoreFailed);
    if (!BeginPass(dev, boundDepth, saved))
        return false;
    IDirect3DSurface9* target = saved.color[0];
    D3DSURFACE_DESC targetDesc = {};
    if (m_snapshotValid && SUCCEEDED(target->GetDesc(&targetDesc)) && EnsureSceneTargets(dev, targetDesc) &&
        CopyToTexture(dev, target, m_sceneWithWater))
    {
        dev->SetPixelShaderConstantF(0, &constants[0][0], kFrameConstants);
        const float toLight[4] = {in.toLight[0], in.toLight[1], in.toLight[2], 0.0f};
        dev->SetPixelShaderConstantF(kToLightRegister, toLight, 1);
        float fog[4];
        ColorToFloat(in.fogColor, 1.0f, fog);
        dev->SetPixelShaderConstantF(kFogColorRegister, fog, 1);
        if(cfg.waterWorkAudit)TimeBegin(GpuTimer::SkyMemory);
        // Looking down to dive must not erase the sky just seen above the bank.
        const float rightZ = constants[4][2] / proj[0], upZ = constants[5][2] / proj[5];
        const float highestRay = constants[6][2] - rightZ * proj[8] - upZ * proj[9] + std::abs(rightZ) + std::abs(upZ);
        RememberSky(dev, depthTexture, m_sceneWithWater, in, cfg, highestRay > 0);
        if(cfg.waterWorkAudit)TimeEnd(GpuTimer::SkyMemory);
        dev->SetRenderTarget(0, target);
    }
    if (underwater)
        DrawUnderwater(dev, depthTexture, target, in, cfg, constants);
    if (cfg.antiAliasing)
        Antialias(dev, target, in, constants);
    if (overlay)
    {
        DrawOverlay(dev, depthTexture, target, in, cfg, constants);
        if (m_lastProbe == 0 || SecondsSince(m_lastProbe) > kProbeSeconds)
            Probe(dev, depthTexture, in, constants);
    }
    if (!saved.Restore()) return Skip("world pass state restoration failed");
    if (captureNow)
        Capture(dev, cfg.captureFrame, cfg.captureFrames);
    // (Only the sky remembered: nothing drawn on the frame.)
    if (!overlay && !cfg.antiAliasing && !underwater)
        return Skip(candidateFailure ? candidateFailure : "nothing to draw at the end of the world pass");
    ++m_rendered;
    return candidateFailure ? Skip(candidateFailure) : true;
}

void Renderer::Antialias(IDirect3DDevice9* dev, IDirect3DSurface9* target, const FrameInputs& in,
                         const float (&constants)[9][4])
{
    D3DSURFACE_DESC desc = {};
    if (FAILED(target->GetDesc(&desc)) || !EnsureSceneTargets(dev, desc) ||
        !CopyToTexture(dev, target, m_sceneWithWater))
        return;
    dev->SetRenderTarget(0, target);
    const D3DVIEWPORT9 vp = FullDepth(in.viewport);
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kColorOnly);
    dev->SetPixelShaderConstantF(0, &constants[0][0], kFrameConstants);
    if (FAILED(BindPixelShader(dev, m_fxaaPs))) return;
    BindLinearTexture(dev, 2, m_sceneWithWater);
    DrawScreenTriangle(dev);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
}

void Renderer::DrawOverlay(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, IDirect3DSurface9* target,
                           const FrameInputs& in, const Config& cfg, const float (&constants)[9][4])
{
    dev->SetRenderTarget(0, target);
    const D3DVIEWPORT9 vp = FullDepth(in.viewport);
    dev->SetViewport(&vp);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, kColorOnly);
    dev->SetPixelShaderConstantF(0, &constants[0][0], kFrameConstants);
    if (FAILED(BindPixelShader(dev, m_overlayPs))) return;
    BindPointTexture(dev, 0, depthTexture);
    BindPointTexture(dev, 1, m_snapshotValid ? m_snapshot : nullptr);
    DrawScreenTriangle(dev);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
}

bool Renderer::SaveRenderTarget(IDirect3DDevice9* dev, const std::string& path)
{
    if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES)
    {
        WATER_LOG_INFO("capture already exists; preserved %s", path.c_str());
        return false;
    }
    IDirect3DSurface9* target = nullptr;
    IDirect3DSurface9* copy = nullptr;
    D3DSURFACE_DESC desc = {};
    D3DLOCKED_RECT locked = {};
    bool ok = SUCCEEDED(dev->GetRenderTarget(0, &target)) && SUCCEEDED(target->GetDesc(&desc)) &&
              (desc.Format == D3DFMT_X8R8G8B8 || desc.Format == D3DFMT_A8R8G8B8) &&
              SUCCEEDED(dev->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM,
                                                         &copy, nullptr)) &&
              SUCCEEDED(dev->GetRenderTargetData(target, copy)) &&
              SUCCEEDED(copy->LockRect(&locked, nullptr, D3DLOCK_READONLY));
    // Restoring a positive CaptureFrame can re-trigger an older request. Both
    // this exclusive open and the early check preserve evidence across restarts.
    const int fd = ok ? _open(path.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE) : -1;
    FILE* file = fd >= 0 ? _fdopen(fd, "wb") : nullptr;
    if (fd >= 0 && !file) _close(fd);
    bool written = false;
    if (file)
    {
        BITMAPINFOHEADER info = {};
        info.biSize = sizeof(info);
        info.biWidth = static_cast<LONG>(desc.Width);
        info.biHeight = -static_cast<LONG>(desc.Height);
        info.biPlanes = 1;
        info.biBitCount = 32;
        BITMAPFILEHEADER header = {};
        header.bfType = 0x4D42;
        header.bfOffBits = sizeof(header) + sizeof(info);
        header.bfSize = header.bfOffBits + desc.Width * desc.Height * 4;
        written = std::fwrite(&header, sizeof(header), 1, file) == 1 &&
                  std::fwrite(&info, sizeof(info), 1, file) == 1;
        for (UINT row = 0; row < desc.Height; ++row)
            written = std::fwrite(static_cast<const char*>(locked.pBits) + row * locked.Pitch, 4, desc.Width, file) == desc.Width && written;
        written = std::fclose(file) == 0 && written;
    }
    if (ok)
        copy->UnlockRect();
    SafeRelease(copy);
    SafeRelease(target);
    if (written)
        WATER_LOG_INFO("captured the render target to %s", path.c_str());
    else
        WATER_LOG_ERROR("capture to %s failed (render target format %d)", path.c_str(), static_cast<int>(desc.Format));
    return written;
}

void Renderer::TimeBegin(int pass)
{
    if (m_timer)
        m_timer->Begin(static_cast<GpuTimer::Pass>(pass));
}

void Renderer::TimeEnd(int pass)
{
    if (m_timer)
        m_timer->End(static_cast<GpuTimer::Pass>(pass));
}

// Host diagnostics share the active water packet and its completion checks.
bool Renderer::CaptureHostPass(IDirect3DDevice9* dev,const char* name,bool after)
{
    if(m_reflectionCaptureDirectory.empty()||!name)return false;
    size_t length=0;
    for(;name[length]&&length<48;++length){
        const char c=name[length];
        if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'))return false;
    }
    if(!length||length==48)return false;
    const std::string pass="host-"+std::string(name);
    const auto found=std::find(m_hostCapturePasses.begin(),m_hostCapturePasses.end(),pass);
    bool ok=false;
    if(after){
        if(found==m_hostCapturePasses.end())return false;
        ok=reflection::CapturePassOutputs(dev,m_reflectionCaptureDirectory+"/"+pass,"output",ActiveCaptureBurst());
    }else{
        if(found!=m_hostCapturePasses.end()||m_hostCapturePasses.size()>=8)return false;
        m_hostCapturePasses.push_back(pass);
        ok=reflection::CapturePassInputs(dev,m_reflectionCaptureDirectory+"/"+pass,m_snapshotPs,ActiveCaptureBurst());
    }
    m_hostCaptureFailed|=!ok;
    return ok;
}

void Renderer::CaptureFinished(IDirect3DDevice9* dev)
{
    // The bounded two-frame experiment queues replayable passes only; a final
    // image has no pass header and must not enter that queue's completion set.
    if(m_reflectionCaptureDirectory.empty()||m_reflectionCaptureBurst)return;
    const std::string directory=m_reflectionCaptureDirectory+"/finished";
    if(!CreateDirectoryA(directory.c_str(),nullptr)){m_hostCaptureFailed=true;return;}
    m_hostCaptureFailed|=!reflection::CapturePassOutputs(dev,directory,"output",ActiveCaptureBurst());
    // This is the finished host world, before UI, paired with this packet's
    // water and host passes. SaveRenderTarget preserves any previous evidence.
    m_hostCaptureFailed|=!SaveRenderTarget(dev,directory+"/view.bmp");
}

void Renderer::EndReflectionCapture(bool aborted)
{
    auto mark = [&](const std::string& directory, bool complete) {
        complete=complete&&!m_hostCaptureFailed;
        for(const auto& pass:m_hostCapturePasses)
            for(const char* marker:{"inputs.ok","outputs.ok"})
                complete=GetFileAttributesA((directory+"/"+pass+"/"+marker).c_str())!=INVALID_FILE_ATTRIBUTES&&complete;
        for (const char* pass : {"rays", "composite"})
        {
            for (const char* marker : {"inputs.ok","outputs.ok"})
                complete = GetFileAttributesA((directory+"/"+pass+"/"+marker).c_str()) != INVALID_FILE_ATTRIBUTES && complete;
        }
        FILE* file = std::fopen((directory+(complete ? "/frame.ok" : "/aborted.txt")).c_str(),"wb");
        if (file) { std::fprintf(file,"%s\n",complete ? "complete" : "incomplete or reset; do not replay as a sequence"); std::fclose(file); }
        WATER_LOG_INFO("reflection input frame %s: %s",directory.c_str(),complete ? "complete" : "incomplete");
        return complete;
    };
    if (m_reflectionCaptureBurst)
    {
        aborted = aborted || m_captureBurst.Failed();
        const bool capturedThisFrame = !m_reflectionCaptureDirectory.empty();
        if (!m_reflectionCaptureDirectory.empty())
        {
            m_burstFrames.push_back(m_reflectionCaptureDirectory);
            --m_reflectionCaptureRemaining; ++m_reflectionCaptureIndex;
            m_reflectionCaptureDirectory.clear();
        }
        if (!aborted && m_reflectionCaptureRemaining > 0)
        {
            if (capturedThisFrame) return;
            // A dry/empty frame between the two samples is a dropped frame,
            // not a consecutive sequence. Never wait across it and claim zero drops.
            aborted = true;
        }
        bool complete = !aborted && m_captureBurst.Flush();
        // Validate both whole frames before marking either complete. A pass
        // omitted by a failed/empty world frame must abort the entire pair.
        for (const auto& directory : m_burstFrames)
            for (const char* pass : {"rays","composite"})
            {
                    for (const char* marker : {"inputs.ok","outputs.ok"})
                    complete = GetFileAttributesA((directory+"/"+pass+"/"+marker).c_str()) != INVALID_FILE_ATTRIBUTES && complete;
            }
        for (const auto& directory : m_burstFrames)
        {
            mark(directory,complete);
            FILE* report = std::fopen((directory+"/burst.json").c_str(),"wb");
            if (report)
            {
                std::fprintf(report,"{\"complete\":%s,\"requested_frames\":2,\"attempted_frames\":%u,\"dropped_frames\":%u,"
                    "\"budget_bytes\":%llu,\"staged_bytes\":%llu,\"submit_cpu_ms\":%.6f,\"post_sequence_readback_ms\":%.6f}\n",
                    complete ? "true" : "false",static_cast<unsigned>(m_burstFrames.size()),
                    complete ? 0u : 2u,static_cast<unsigned long long>(reflection::CaptureBurst::kBudget),
                    static_cast<unsigned long long>(m_captureBurst.Bytes()),m_captureBurst.SubmitMs(),m_captureBurst.FlushMs());
                std::fclose(report);
            }
        }
        m_captureBurst.Discard(); m_burstFrames.clear(); m_reflectionCaptureBurst = false;
        if (!complete) m_reflectionCaptureRemaining = 0;
        return;
    }
    if (m_reflectionCaptureDirectory.empty()) return;
    const bool complete = mark(m_reflectionCaptureDirectory,!aborted);
    --m_reflectionCaptureRemaining; ++m_reflectionCaptureIndex;
    if (!complete) m_reflectionCaptureRemaining = 0;
    m_reflectionCaptureDirectory.clear();
}

void Renderer::BeginReflectionCapture(const Config& cfg, const FrameInputs& in)
{
    if (cfg.reflectionCapture <= 0) { EndReflectionCapture(true); m_reflectionCaptureRemaining = 0; return; }
    if (cfg.reflectionCapture != m_reflectionCaptureRequest)
    {
        m_reflectionCaptureRequest = cfg.reflectionCapture;
        m_reflectionCaptureRemaining = cfg.reflectionCaptureFrames;
        m_reflectionCaptureIndex = 0;
    }
    if (m_reflectionCaptureRemaining <= 0 || !m_reflectionCaptureDirectory.empty()) return;
    const std::string directory = OutputDirectory() + "reflection-" + std::to_string(cfg.reflectionCapture) +
                                  "-" + std::to_string(m_reflectionCaptureIndex);
    if (!CreateDirectoryA(directory.c_str(), nullptr))
    {
        WATER_LOG_ERROR("reflection capture directory already exists or cannot be created: %s", directory.c_str());
        m_reflectionCaptureRemaining = 0;
        return;
    }
    m_reflectionCaptureDirectory = directory;
    m_hostCapturePasses.clear();m_hostCaptureFailed=false;
    if (cfg.reflectionCaptureBurst || m_reflectionCaptureBurst)
    {
        // The named experiment captures baseline inputs, not the expensive
        // candidate intermediates or arbitrary-length recordings.
        if (!cfg.reflectionCaptureBurst || cfg.reflectionCaptureFrames != 2 || cfg.reflectionScenery)
        {
            WATER_LOG_ERROR("two-frame burst requires baseline mode, two frames and diagnostics off");
            EndReflectionCapture(true); m_reflectionCaptureRemaining = 0; return;
        }
        if (m_reflectionCaptureIndex == 0) { m_captureBurst.Discard(); m_reflectionCaptureBurst = true; }
    }
    CopyFileA((OutputDirectory() + "water.ini").c_str(), (directory + "/water.ini").c_str(), TRUE);
    FILE* manifest = std::fopen((directory + "/manifest.json").c_str(), "wb");
    if (manifest)
    {
        std::fprintf(manifest, "{\"version\":2,\"frame\":%u,\"world_frame\":%u,\"sequence_index\":%d,\"capture_mode\":\"%s\",\"camera\":[%.9g,%.9g,%.9g],"
                              "\"clock_seconds\":%.9g,\"normal_inputs\":\"final surface detail; downstream reflection only\","
                              "\"depth\":\"raw R32F copied from INTZ; inverse-depth mapping in uniforms\","
                              "\"color\":\"native format, sampler sRGB state retained\"}\n",
                     m_frame, m_worldFrame, m_reflectionCaptureIndex, m_reflectionCaptureBurst ? "two-frame GPU staging" : "synchronous", in.cameraPosition[0], in.cameraPosition[1], in.cameraPosition[2],
                     LiquidSeconds(cfg));
        std::fclose(manifest);
    }
    WATER_LOG_INFO("reflection input capture %s (readback cost excluded from performance comparisons)", directory.c_str());
}

// Saves this frame as frame-<request>.bmp; or, asked for several, this and the frames after it as
// frame-<request>-<i>.bmp (to see what changes from one frame to the next).
void Renderer::Capture(IDirect3DDevice9* dev, int request, int frames)
{
    m_capturedRequest = request;
    if (m_captureLeft == 0)
    {
        m_captureLeft = frames;
        m_captureSaved = 0;
    }
    const std::string name = frames > 1 ? std::to_string(request) + "-" + std::to_string(m_captureSaved)
                                        : std::to_string(request);
    SaveRenderTarget(dev, OutputDirectory() + "frame-" + name + ".bmp");
    ++m_captureSaved;
    --m_captureLeft;
}

void Renderer::Probe(IDirect3DDevice9* dev, IDirect3DTexture9* depthTexture, const FrameInputs& in,
                     const float (&constants)[9][4])
{
    m_lastProbe = Ticks();
    if (!EnsureProbeTargets(dev))
        return;
    if(!SetTarget(dev, m_probeTarget))return;
    D3DVIEWPORT9 vp = {0, 0, 1, 1, 0.0f, 1.0f};
    dev->SetViewport(&vp);
    dev->SetPixelShaderConstantF(0, &constants[0][0], kFrameConstants);
    if (FAILED(BindPixelShader(dev, m_probePs))) return;
    BindPointTexture(dev, 0, depthTexture);
    BindPointTexture(dev, 1, m_snapshotValid ? m_snapshot : nullptr);
    DrawScreenTriangle(dev);

    IDirect3DSurface9* surface = nullptr;
    D3DLOCKED_RECT locked = {};
    bool read = SUCCEEDED(m_probeTarget->GetSurfaceLevel(0, &surface)) &&
                SUCCEEDED(dev->GetRenderTargetData(surface, m_probeReadback)) &&
                SUCCEEDED(m_probeReadback->LockRect(&locked, nullptr, D3DLOCK_READONLY));
    SafeRelease(surface);
    if (!read)
    {
        WATER_LOG_INFO("probe: readback failed");
        return;
    }
    ProbeSample sample;
    std::memcpy(&sample, locked.pBits, sizeof(sample));
    m_probeReadback->UnlockRect();
    const float dx = sample.world[0] - in.cameraPosition[0];
    const float dy = sample.world[1] - in.cameraPosition[1];
    const float dz = sample.world[2] - in.cameraPosition[2];
    if (sample.waterThickness < -1.5f)
        WATER_LOG_INFO("probe: screen centre is sky or beyond the far clip; camera (%.2f %.2f %.2f)",
                       in.cameraPosition[0], in.cameraPosition[1], in.cameraPosition[2]);
    else
        WATER_LOG_INFO("probe: screen centre at world (%.2f %.2f %.2f), %.1f yd from the camera; water %s%.2f yd",
                       sample.world[0], sample.world[1], sample.world[2], std::sqrt(dx * dx + dy * dy + dz * dz),
                       sample.waterThickness < 0.0f ? "none " : "depth ",
                       sample.waterThickness < 0.0f ? 0.0f : sample.waterThickness);
}

void Renderer::ReleaseDefaultPool()
{
    m_distanceAtmosphereShaded=false;
    m_surfaceResult=SurfaceResult::Unchanged;
    SetScenery(nullptr,nullptr,0,nullptr);
    m_candidateFailure = nullptr;
    EndReflectionCapture(true);
    m_reflectionCaptureRemaining = 0;
    SafeRelease(m_snapshot);
    SafeRelease(m_sceneBefore);
    SafeRelease(m_sceneWithWater);
    SafeRelease(m_multisampleRollback);
    SafeRelease(m_compositeDepth);
    SafeRelease(m_compositeScene);
    m_compositeDesc = {};
    m_sceneDesc = {};
    m_sceneValid = false;
    SafeRelease(m_probeTarget);
    SafeRelease(m_level[0]);
    SafeRelease(m_level[1]);
    SafeRelease(m_sky[0]);
    SafeRelease(m_sky[1]);
    SafeRelease(m_observedAir);
    m_airMap = -1;
    ReleaseDeferred(m_slime);
    SafeRelease(m_slimeGround);
    m_lava.ReleaseDefaultPool();
    SafeRelease(m_surfaceBaseDepth);
    SafeRelease(m_waterDepth);m_nativeWater=nullptr;m_waterDepthValid=m_slimeCaptured=false;
    SafeRelease(m_occlusion[0]);
    SafeRelease(m_occlusion[1]);
    m_occlusionWidth = 0;
    m_occlusionHeight = 0;
    SafeRelease(m_ringMap);
    m_ringMapReach = 0.0f;
    SafeRelease(m_contactMarks);
    SafeRelease(m_contactSpread);
    SafeRelease(m_surfaceDetail[0]);
    SafeRelease(m_surfaceDetail[1]);
    SafeRelease(m_noiseLattice[0]);
    SafeRelease(m_noiseLattice[1]);
    m_surfaceDetailDrawn = false;
    m_usingWaveMoments = false;
    m_waveMomentsFailed = false;
    m_ringShaderFailed = false;
    m_failedShaders.clear();
    SafeRelease(m_wakeMap[0]);
    SafeRelease(m_wakeMap[1]);
    SafeRelease(m_reflections[0]);
    SafeRelease(m_reflections[1]);
    ReleaseFillChain(m_underwaterFill);
    m_underwaterSceneDrawn = false;
    ReleaseSurface(m_waterSurface);
    ReleaseSurface(m_slimeSurface);
    m_slime.thisFrame = false;
    SafeRelease(m_state);
    m_snapshotWidth = 0;
    m_snapshotHeight = 0;
    m_snapshotValid = false;
}

void Renderer::ReleaseAll()
{
    ReleaseDefaultPool();
    SafeRelease(m_probeReadback);
    SafeRelease(m_vs);
    SafeRelease(m_snapshotPs);
    SafeRelease(m_overlayPs);
    SafeRelease(m_probePs);
    SafeRelease(m_waterPs);
    SafeRelease(m_waterFinePs);
    SafeRelease(m_waterMomentsPs);
    SafeRelease(m_waterMomentsFinePs);
    SafeRelease(m_surfaceMomentsPs);
    SafeRelease(m_fxaaPs);
    SafeRelease(m_levelPs);
    SafeRelease(m_skyMemoryPs); SafeRelease(m_skyObservedAirPs); SafeRelease(m_skyExtendPs);
    SafeRelease(m_underwaterPs);
    SafeRelease(m_underwaterFoamPs);
    SafeRelease(m_underwaterSharedPs);
    SafeRelease(m_underwaterSceneryPs);
    SafeRelease(m_slimeUnderwaterSceneryPs);
    SafeRelease(m_ripplePs);
    SafeRelease(m_ground);
    SafeRelease(m_shore);
    SafeRelease(m_farGround);
    m_farGroundSource = 0;
    m_farGroundRevision = 0;
    SafeRelease(m_boatPaths);
    m_groundSource = 0;
    SafeRelease(m_slimeCapturePs);
    SafeRelease(m_slimeLiftPs);
    SafeRelease(m_slimeUndoPs);
    SafeRelease(m_slimeDeferredPs);
    SafeRelease(m_slimeDeferredFinePs);
    m_lava.ReleaseManaged();
    SafeRelease(m_aoPs);
    SafeRelease(m_aoBlurPs);
    SafeRelease(m_puddlePs);
    SafeRelease(m_ringPs);
    SafeRelease(m_reflectionSceneryPs);
    SafeRelease(m_slimeReflectionPs);
    SafeRelease(m_surfaceDetailPs);
    SafeRelease(m_noiseLatticePs);
    SafeRelease(m_wakeMapPs);
    SafeRelease(m_fillPullPs);
    SafeRelease(m_fillPushPs);
    SafeRelease(m_underwaterScenePs);
    SafeRelease(m_contactPs);
    SafeRelease(m_contactSpreadPs);
    SafeRelease(m_ringVs);
    SafeRelease(m_ringDecl);
    SafeRelease(m_decl);
}
