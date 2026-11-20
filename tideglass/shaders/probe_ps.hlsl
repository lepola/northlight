#include "common.hlsli"

float4 main() : COLOR0
{
    float2 pixel = floor(cModeProbe.yz);
    float rawDepth = SampleRawDepth(sDepth, pixel);
    if (BeyondFarClip(rawDepth))
        return float4(0, 0, 0, kBeyondFarClip);
    float3 world = WorldPosition(ViewPosition(pixel, ViewZ(rawDepth)));
    return float4(world, WaterThickness(pixel, rawDepth));
}
