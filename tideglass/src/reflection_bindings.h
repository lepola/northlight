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
// Scissors use inclusive starts and exclusive ends. Round outward to keep edge pixels.
inline RECT ScaleArea(RECT area,UINT fullWidth,UINT fullHeight,UINT width,UINT height)
{
    return {LONG(area.left*LONG(width)/LONG(fullWidth)),LONG(area.top*LONG(height)/LONG(fullHeight)),
            LONG((area.right*LONG(width)+fullWidth-1)/fullWidth),LONG((area.bottom*LONG(height)+fullHeight-1)/fullHeight)};
}
inline Layout ResolveLayout(UINT fullWidth,UINT fullHeight,D3DVIEWPORT9 vp,unsigned percent)
{
    const UINT width=(fullWidth*percent+99)/100,height=(fullHeight*percent+99)/100;
    const RECT area=ScaleArea({LONG(vp.X),LONG(vp.Y),LONG(vp.X+vp.Width),LONG(vp.Y+vp.Height)},fullWidth,fullHeight,width,height);
    return {width,height,{DWORD(area.left),DWORD(area.top),DWORD(area.right-area.left),DWORD(area.bottom-area.top),0,1}};
}
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
