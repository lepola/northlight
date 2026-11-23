#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>

// Host-neutral water-surface inputs; no client addresses or host equations.
constexpr uint32_t kWaterBackgroundSize=512;

enum WaterEnvironmentPolicy : uint32_t {
    WaterEnvironmentHostRetainsDistance=0,
    // A promise for this frame: exempt only confirmed shaded/owned water from
    // background distance atmosphere. Spatial fog, bloom and grading may remain.
    WaterEnvironmentCoreOwnsDistance=1,
};
enum WaterEnvironmentEncoding : uint32_t {
    // RGB at the core surface-composition target, before later host spatial fog,
    // bloom and grading. Provider exposure/encoding must match that target.
    // Not linear radiance; sampler sRGB conversion is disabled.
    WaterEnvironmentSurfaceCompositeRGB=1,
};
enum WaterBackgroundContent : uint32_t {
    WaterBackgroundHorizonTreated=1,
    WaterBackgroundLocalFogExcluded=2,
    // ABI1 excludes discs and halos. ABI2 excludes luminous discs/artwork;
    // procedural atmospheric glare is allowed only when explicitly declared below.
    WaterBackgroundCelestialExcluded=4,
    WaterBackgroundForegroundExcluded=8,
    WaterBackgroundSunGlare=16,
    WaterBackgroundMoonGlare=32,
    // Provider guarantees alpha=1 in every direction. Required with glare, so
    // authored glow suppression never applies to a palette-only fallback region.
    WaterBackgroundCompleteCoverage=64,
};
struct WaterEnvironmentLight {
    float direction[3]; // normalized world-space direction TO the active sun/moon
    float colour[3];    // water-calibrated artistic scene RGB; billboard emission is not water lighting
    float visibility;   // altitude/weather/body opacity ONCE, 0..1. Excludes camera
                        // on-screen visibility and camera-centred halo occlusion.
                        // No active light: 0 with finite direction (0,0,1)/radius.
    float angularRadius; // radians, matching the visible body
    uint32_t moon;
};
// Compact, world-space host light samples. A zero radius is an unused slot.
struct WaterLampSamples {
    float positions[4][4]={}; // xyz and outer radius in yards
    float colours[4][4]={};   // RGB irradiance and inverse attenuation width
};
struct WaterEnvironmentValues {
    uint32_t policy,encoding,backgroundContent;
    uint32_t paletteKnown;
    uint32_t palette[6]; // packed ARGB native sky bands, zenith to horizon
    uint64_t environmentKey; // current provider/map identity; no directional history is kept
    WaterEnvironmentLight light;
    float referenceDistance; // yards, for the core's artistic surface-path law
    float transmissionAtReference; // 0 < T <= 1. exp(log(T)*distance/referenceDistance),
                                    // Euclidean camera-to-surface yards. T=1 is clear;
                                    // T=0 is rejected (would erase all surface response).
};

enum WaterEnvironmentFallback : uint32_t {
    WaterEnvironmentNoFallback=0,
    WaterEnvironmentNotProvided,
    WaterEnvironmentHostRetained,
    WaterEnvironmentPaletteOnly,
    WaterEnvironmentUncertifiedBackground,
    WaterEnvironmentInvalidInput,
    WaterEnvironmentDisabled,
};

namespace water {
inline constexpr float kMoonLightScale=.65f,kHorizonFadeBelow=.02f,kHorizonFadeWidth=.25f;
inline constexpr float kMoonTint[4]={.62f,.70f,.90f,1.f};
// Existing water calibration, shared with providers. Weather-only visibility is applied once;
// callers select the body and supply native scene colour, independently of billboard appearance.
inline WaterEnvironmentLight NativeWaterLight(const float* direction,uint32_t sunColour,bool moon,float weatherVisibility){
    WaterEnvironmentLight light={};float length=0;
    for(unsigned i=0;i<3;++i)length+=direction[i]*direction[i];
    if(std::isfinite(length)&&length>1e-6f)for(unsigned i=0;i<3;++i)light.direction[i]=direction[i]/std::sqrt(length);
    else light.direction[2]=1;
    for(unsigned i=0;i<3;++i)light.colour[i]=moon?kMoonTint[i]:float((sunColour>>((2-i)*8))&255)/255.f;
    light.visibility=(moon?kMoonLightScale:1.f)*std::clamp((direction[2]+kHorizonFadeBelow)/kHorizonFadeWidth,0.f,1.f)*weatherVisibility;
    light.angularRadius=.05f;light.moon=moon?1:0;return light;
}
}
