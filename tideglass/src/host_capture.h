#pragma once
#include "host_api.h"

// Optional synchronous diagnostic capability. Capture only while this frame's
// ReflectionCapture is active. The host brackets a fullscreen pixel pass after
// binding its inputs and before restoring them; no textures are retained here.
// Names are bounded filename components. Calls return zero when idle or failed;
// diagnostic failure must never suppress rendering. The core owns the packet.
constexpr uint32_t kWaterCaptureABI = 1;
struct WaterHostCaptureAPI {
    uint32_t abi, size;
    int (__cdecl* pass)(WaterHostContext*, WaterHostStamp, const char* name, int after);
};
using WaterHostGetCaptureAPIFn = const WaterHostCaptureAPI* (__cdecl*)(uint32_t, uint32_t);
