#pragma once
#include "frame_values.h"

// Local integration ABI. The module owns no D3D device or render hooks. Explicit
// visual-time testing may intercept two validated native clock reads and pin the
// core for process lifetime; disabling restores native time without clock writes.
// Create/destroy contexts and all GPU calls on the host's device thread. Capture
// client inputs on the game thread, before queuing the returned immutable handle.
// Retain frame handles until replay retires them. All allocations are released by
// their creating module. Borrowed textures remain alive through end_frame/lost.
constexpr uint32_t kWaterHostABI = 1;
struct WaterHostContext;
struct WaterHostFrame;
struct WaterHostStamp { uint64_t session, generation, frame; };
struct WaterHostCamera
{
    uint32_t size;
    float view[16], projection[16];
    D3DVIEWPORT9 viewport;
    uint32_t nativeFogRangeKnown;
    float nativeFogStart, nativeFogEnd;
};
struct WaterHostReflection
{
    uint32_t size;
    IDirect3DTexture9* colour;
    IDirect3DTexture9* depth;
    float height;
    float projection[16];
};
enum WaterHostPass : uint32_t
{
    WaterHostBeforeModels = 1,
    WaterHostAmbientOcclusion,
    WaterHostRainRings,
    WaterHostPuddles,
    WaterHostBeforeLiquid,
    WaterHostCompositeWater,
    WaterHostWorldDone,
};
// Native is the default each frame. Enhanced coverage must be confirmed by the
// host for this exact frame; DLL presence is never a readiness signal.
enum WaterHostFog : uint32_t {
    WaterHostNativeFog = 0, WaterHostFogCoversWater = 1,
    // Native mirror fog is retained. The host also lights the world with spatial fog this frame.
    WaterHostSpatialFog = 2
};
struct WaterHostResources
{
    uint32_t size;
    IDirect3DTexture9* depth;
    IDirect3DSurface9* boundDepth;
    // Optional tail (legacy size remains accepted): native water positive view distance / coverage.
    // It identifies water independently of later characters/models. Borrowed through end_frame/lost.
    IDirect3DTexture9* nativeWater = nullptr;
    // Optional lit copy of scene_before (liquid ABI 7), same pixels/format/size.
    // Consumed synchronously by CompositeWater only; never retained or released.
    // Other passes and older resource sizes keep the core's original snapshot.
    IDirect3DTexture9* refractionScene = nullptr;
    // Optional liquid ABI 8 input: full-target G16R16F sun/moon visibility at
    // the native liquid surface, in [0,1]. Point sampled; 1 is unshadowed.
    // Includes host shadow-strength/weather policy, excludes light intensity.
    // Consumed synchronously by CompositeWater; null keeps native lighting.
    // The host owns allocation/contents through the call, including failure.
    IDirect3DTexture9* surfaceVisibility = nullptr;
};
struct WaterHostAPI
{
    uint32_t abi, size;
    WaterHostContext* (__cdecl* create)(IDirect3DDevice9*, const char* settingsPath);
    int (__cdecl* destroy)(WaterHostContext*);
    WaterHostFrame* (__cdecl* copy_frame)(const WaterFrameValues*, uint32_t size);
    WaterHostFrame* (__cdecl* capture_client)(const WaterHostCamera*);
    void (__cdecl* retain_frame)(WaterHostFrame*);
    void (__cdecl* release_frame)(WaterHostFrame*);
    int (__cdecl* begin_frame)(WaterHostContext*, WaterHostFrame*, WaterHostStamp*);
    int (__cdecl* set_reflection)(WaterHostContext*, WaterHostStamp, const WaterHostReflection*);
    int (__cdecl* set_fog)(WaterHostContext*, WaterHostStamp, WaterHostFog);
    int (__cdecl* draw)(WaterHostContext*, WaterHostStamp, WaterHostPass, const WaterHostResources*, int shadeSurface);
    int (__cdecl* end_frame)(WaterHostContext*);
    int (__cdecl* lost)(WaterHostContext*);
    int (__cdecl* restored)(WaterHostContext*);
    int (__cdecl* can_unload)();
};
using WaterHostGetAPIFn = const WaterHostAPI* (__cdecl*)(uint32_t abi, uint32_t size);
