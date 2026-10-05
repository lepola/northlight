#pragma once
#include <string>

namespace NorthlightDxvkCompatibility {
// RESZ is exposed AND executed only with the AMD D3D9 identity (still true in DXVK 3.1.1).
// Captures read game buffers on the CPU; their mappings must be CPU-cached on
// discrete GPUs. 0.3.189: DXVK 2.x reads d3d9.cachedDynamicBuffers, >=3.0 renamed it
// d3d9.cachedWriteOnlyBuffers (it applies to DYNAMIC directly-mapped buffers, which captures
// read on the CPU); each version ignores the other's unknown key, so both are set.
// DXVK_CONFIG is process-local and overrides only these file options. All other
// file options and explicit inline overrides survive.
inline std::string config(const std::string& existing){
    return "d3d9.customVendorId = 1002; d3d9.cachedDynamicBuffers = True; d3d9.cachedWriteOnlyBuffers = True; "+existing;
}
}
