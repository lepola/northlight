#pragma once
// Shared by the files that define Renderer's methods (renderer.cpp and renderer_*.cpp) and by nothing else: the shader
// register and sampler numbers, tuning constants and small helpers that more than one of them uses.
#include "render/renderer.h"

#include "math/matrix.h"
#include "base/gpu_timer.h"
#include "screen_pass.h"
#include "reflection/reflection_capture.h"
#include "reflection/reflection_bindings.h"
#include "base/log.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <string>
#include "d3d_resource.h"

namespace render_detail
{
using water::SafeRelease;
using screen_pass::BindPointTexture;
using screen_pass::BindLinearTexture;
using screen_pass::SetTarget;

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
inline bool FilteredWaterMaterial(const Config& cfg, bool environmentAvailable)
{
    return environmentAvailable;
}

constexpr UINT kGameFogRegister = WATER_SLOT_cGameFog, kWaveMomentSamplesRegister = WATER_SLOT_cWaveMomentSamples, kSurfaceVisibilityRegister = WATER_SLOT_cSurfaceVisibility;
inline bool BindWaterControls(IDirect3DDevice9* dev, const Config& cfg, bool environmentAvailable, bool momentsAvailable = false, bool surfaceVisibilityAvailable = false, bool hasIndoorWater = false)
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
// How much of the contact foam fades each ripple step (ripple_ps), so it lingers a moment behind something moving.
constexpr float kContactFoamFade = 0.04f;
// Slime's edge glow lies along walls that stay put: it eases to what is found, up or down, over about this long.
constexpr float kSlimeEdgeEaseSeconds = 0.3f;
constexpr UINT kLavaHaloScale = 4;
// The least time between steps of the lava's stirring, and between looks for what stands in it (just short of a 60 Hz frame
// and of three of them, so 60 Hz steps every frame and finds what stands every third), and how fast that raises it (lava_stir_ps).
constexpr double kLavaStirStepSeconds = 1.0 / 75.0;
constexpr double kLavaContactSeconds = 1.0 / 24.0;
constexpr double kLavaContactRisePerSecond = 10.0;
constexpr UINT kRippleMoverRegister = 4;
constexpr UINT kRippleRadiusRegister = 20;
constexpr UINT kRippleAreaRegister = WATER_SLOT_cRippleArea;
// contact_ps: whether a ground map tells the ground from what stands on it (clear of the frame's and the water's own).
constexpr UINT kContactOptionsRegister = 60;
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
inline void ShipWakeBoxes(const FrameInputs& in, float (&boxes)[kMaxBoats][4])
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

inline const D3DVERTEXELEMENT9 kRingVertex[] = {
    {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    {0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
    D3DDECL_END(),
};

inline const D3DVERTEXELEMENT9 kFullscreenVertex[] = {
    {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
    D3DDECL_END(),
};

struct ProbeSample
{
    float world[3];
    float waterThickness;
};

inline long long Ticks()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

inline double SecondsSince(long long ticks)
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(Ticks() - ticks) / static_cast<double>(frequency.QuadPart);
}

using water::math::Invert4x4;

inline bool ViewToWorld(const FrameInputs& in, float* viewToWorld)
{
    if (!Invert4x4(in.cameraRelativeView, viewToWorld))
        return false;
    viewToWorld[12] = in.cameraPosition[0];
    viewToWorld[13] = in.cameraPosition[1];
    viewToWorld[14] = in.cameraPosition[2];
    return true;
}

inline float FarClipDistance(const float* proj)
{
    return proj[14] / (1.0f - proj[10]);
}

struct DepthMapping
{
    float atInfinity;
    float perInverseViewZ;
    float deepestWorldDepth;
};

inline DepthMapping MapDepth(const float* proj, const D3DVIEWPORT9& vp)
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
inline D3DVIEWPORT9 FullDepth(const D3DVIEWPORT9& viewport)
{
    D3DVIEWPORT9 vp = viewport;
    vp.MinZ = 0.0f;
    vp.MaxZ = 1.0f;
    return vp;
}

inline void ColorToFloat(uint32_t argb, float scale, float* out)
{
    out[0] = ((argb >> 16) & 0xFF) / 255.0f * scale;
    out[1] = ((argb >> 8) & 0xFF) / 255.0f * scale;
    out[2] = (argb & 0xFF) / 255.0f * scale;
    out[3] = 1.0f;
}

inline bool CopyToTexture(IDirect3DDevice9* dev, IDirect3DSurface9* source, IDirect3DTexture9* texture)
{
    IDirect3DSurface9* surface = nullptr;
    bool ok = SUCCEEDED(texture->GetSurfaceLevel(0, &surface)) &&
              SUCCEEDED(dev->StretchRect(source, nullptr, surface, nullptr, D3DTEXF_NONE));
    if (surface)
        surface->Release();
    return ok;
}

inline const char* DebugViewName(DebugView view)
{
    return view == DebugView::WorldGrid ? "world grid" : view == DebugView::Depth ? "depth" : "off";
}

// Both textures of a pair as render targets 0 and 1.
inline bool SetPairTargets(IDirect3DDevice9* dev, IDirect3DTexture9* const (&pair)[2])
{
    if(!SetTarget(dev,pair[0]))return false;
    IDirect3DSurface9* second=nullptr;
    const bool have=pair[1]&&SUCCEEDED(pair[1]->GetSurfaceLevel(0,&second))&&second;
    const bool bound=have&&SUCCEEDED(dev->SetRenderTarget(1,second));
    SafeRelease(second);return bound;
}
}
