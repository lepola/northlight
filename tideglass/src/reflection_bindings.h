#pragma once
#include <d3d9.h>
#include "../shaders/water_registers.hlsli"

namespace reflection
{
// Register/texture contract is documented in shaders/reflection_common.hlsli.
// c0..8 frame, c9..41 water, c45 sky layout,
// c46 output grid/debug, c48 wake map flag (c49..50 free), c51..54 ships, c55..56 reflection options.
constexpr UINT kOptionsRegister = WATER_SLOT_cReflectionOptions;
constexpr DWORD kColourStage = WATER_SLOT_sReflection;
constexpr DWORD kHitStage = WATER_SLOT_sReflectionHit;
struct Layout { UINT width, height; D3DVIEWPORT9 viewport; };
inline Layout TargetLayout(UINT width, UINT height, D3DVIEWPORT9 viewport, bool half)
{
    viewport.MinZ = 0; viewport.MaxZ = 1;
    if (half)
    {
        width = (width + 1) / 2; height = (height + 1) / 2;
        viewport.Width = (viewport.X + viewport.Width + 1) / 2 - viewport.X / 2;
        viewport.Height = (viewport.Y + viewport.Height + 1) / 2 - viewport.Y / 2;
        viewport.X /= 2; viewport.Y /= 2;
    }
    return {width, height, viewport};
}
}
