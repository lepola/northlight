#define main ResolveNativePixel
#include "water_ps.hlsl"
#undef main

// Zero keeps native resolution, including captures taken before this register was used.
float4 cResolvePixel : register(c79);
ReflectionOutput main(float2 pixel : VPOS)
{
    return ResolveNativePixel(floor((pixel + 0.5) * (1.0 + cResolvePixel.xy)));
}
