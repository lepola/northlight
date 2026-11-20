#pragma once
#include "host_api.h"
#include "water_environment.h"

// Separately queried companion; render ABI 1 remains unchanged.
constexpr uint32_t kWaterEnvironmentABI=2;
struct WaterHostEnvironment {
    uint32_t size;
    WaterHostStamp source; // exact current session/device generation/frame
    WaterEnvironmentValues values;
    // Optional CURRENT-FRAME octahedral 512x512 RGBA16F, +Z up, +X right of
    // centre, +Y below centre; lower hemisphere folded to the corners. Uses
    // OctahedralUv/Direction in reflection_trace.hlsli; clamp/linear sampling.
    // Rebuilt for this frame from resolved inputs; a host may reuse valid prior
    // colour/lighting/source-visibility observations. Water/depth/output-coverage masks
    // and draw ownership remain current-frame; a halo observation is not an output mask.
    // RGB is premultiplied by coverage alpha; zero is unknown, using the native-dome palette.
    // No local fog, terrain, water or luminous celestial disc/artwork. All four base content
    // flags are required. ABI2 permits explicitly declared sun/moon atmospheric glare
    // (including its procedural core, excluding the luminous body) only with complete coverage.
    // The matching authored broad glow is then replaced; glitter, sheen and direct disc remain.
    // ABI1 remains queryable with its original no-disc/no-halo meaning and flags0..15.
    // No CPU readback. Palette fallback does not reproduce custom sky artwork.
    // Allocation AND contents remain stable through frame retirement, loss or destruction.
    // AddRef protects allocation lifetime; it does not make mutable contents a snapshot.
    IDirect3DTexture9* background;
};
enum WaterHostSurfaceOutcome : uint32_t {
    WaterHostSurfaceUnchanged=0,
    WaterHostSurfaceShaded,
    WaterHostSurfaceRecovered,
    WaterHostSurfaceAbortFrame,
};
struct WaterHostSurfaceStatus {
    uint32_t size,outcome,distanceAtmosphereShaded,fallback;
};
struct WaterHostEnvironmentAPI {
    uint32_t abi,size;
    // Availability clears every frame/lost/reset. Setter must precede the first
    // surface composition and rejects later replacement. Invalid records clear
    // availability; stale/wrong-context calls cannot clear a current record.
    // Does not change WaterHostFogCoversWater: old hosts keep that old contract.
    // While the core applies environment distance, the host cannot also request
    // legacy enhanced water fog. Material-only comparison mode 2 retains host fog.
    int(__cdecl* set_environment)(WaterHostContext*,WaterHostStamp,const WaterHostEnvironment*);
    // This proves only the core transaction. The host must ALSO successfully
    // restore its outer state before selecting any owned-water exemption.
    // distanceAtmosphereShaded requires THIS accepted environment to have been
    // used in the completed distance model, not merely a successful scene draw.
    int(__cdecl* surface_status)(WaterHostContext*,WaterHostStamp,WaterHostSurfaceStatus*);
};
using WaterHostGetEnvironmentAPIFn=const WaterHostEnvironmentAPI*(__cdecl*)(uint32_t,uint32_t);
