#pragma once
#include "host_api.h"

// Optional companion to render ABI 1. The host identifies a native liquid draw
// by its geometry/shader, then asks the shared classifier about captured native
// material metadata. Capture is game-thread only; drawing uses the owner/stamp.
constexpr uint32_t kWaterLiquidABI = 10;
using WaterHostTextureMap=uintptr_t(__cdecl*)(void*,uintptr_t);
// Fade (ABI 6): the draw lies where a slime style fades into water. The host draws it as water (its water mask) and
// passes it to before_surface/after_surface with this kind as well, so the core lays the slime over the water by how
// slime-styled each pixel's place is. Where the host has only one of the two paths it takes the draw as slime.
enum WaterHostLiquidKind : uint32_t {
    WaterHostLiquidUnknown = 0, WaterHostLiquidWater, WaterHostLiquidMagma, WaterHostLiquidSlime, WaterHostLiquidFade,
};
struct WaterHostLiquidAPI {
    uint32_t abi, size;
    // bounds: the box around the draw's vertices (LiquidDrawBounds: low xyz, high xyz), or null. The ocean and river
    // styles are regional, so with bounds each surface is judged over its own extent (ABI 6).
    // ramp receives 0 for ocean, 1 for river, -2 for fel magma, -1 otherwise; pass it to before_surface for the same pixel style.
    WaterHostLiquidKind (__cdecl* classify)(WaterHostContext*, WaterHostStamp, IDirect3DBaseTexture9*, const float* bounds, int* ramp);
    // Legacy observation without bounds (conservative reflection coverage).
    // Report a verified non-additive outdoor liquid draw, including a culled
    // underside. Classification alone is not evidence of liquid geometry.
    int (__cdecl* observe)(WaterHostContext*, WaterHostStamp, WaterHostLiquidKind);
    // Outdoors, BeforeLiquid must already have captured the opaque scene. Indoors
    // each surface takes its own snapshot before the native draw. A successful
    // before_surface must be paired with after_surface, even if that draw fails.
    // kind: slime, fade or magma, as classify gave it. Refused (leave the native draw)
    // while that material stays native, as magma does seen from inside it. bounds: the draw's box as classify took it, or
    // null for the full viewport. With a box the capture works only where it lies on screen.
    int (__cdecl* before_surface)(WaterHostContext*, WaterHostStamp, const WaterHostResources*, int outdoors, WaterHostLiquidKind kind, const float* bounds, int ramp);
    int (__cdecl* after_surface)(WaterHostContext*, WaterHostStamp, const WaterHostResources*, int drawn);
    // After opaque models and native liquid draws, before host screen effects/UI.
    int (__cdecl* composite)(WaterHostContext*, WaterHostStamp, const WaterHostResources*);
    WaterHostFrame* (__cdecl* capture_client)(const WaterHostCamera*,WaterHostTextureMap,void*);
    // After composite: the host's mask of what lies on a liquid surface (null if it has none) with this frame's lava added,
    // to pass to its screen effects in place of its own, so they do not shade a surface that glows of itself. Null with
    // no lava this frame. Owned by the core, valid until the frame ends.
    IDirect3DTexture9* (__cdecl* effects_mask)(WaterHostContext*, WaterHostStamp, const WaterHostResources*, IDirect3DTexture9* hostMask);
    // Read-only opaque colour/depth captured before native liquid. Borrowed on
    // the device thread until the next capture, end_frame or lost. A host may
    // light a separate copy and supply it as resources.refractionScene; never
    // modify these textures. Stale stamps or missing capture clear both outputs.
    int (__cdecl* scene_before)(WaterHostContext*, WaterHostStamp, IDirect3DTexture9** colour, IDirect3DTexture9** depth);
    // ABI 9: call while the verified liquid draw's original shader, declaration
    // and constants are bound. Raw bounds may be world or placement-local; the
    // core proves and applies its transform. Null/unsupported keeps full coverage.
    int (__cdecl* observe_draw)(WaterHostContext*, WaterHostStamp, WaterHostLiquidKind, const float* rawBounds);
    // ABI 10: preserve current framebuffer pixels marked by the host in boundDepth.
    // The single stencil bit is read only on the final colour draw.
    int (__cdecl* composite_masked_water)(WaterHostContext*, WaterHostStamp, const WaterHostResources*, uint32_t stencilBit);
};
using WaterHostGetLiquidAPIFn = const WaterHostLiquidAPI* (__cdecl*)(uint32_t, uint32_t);
