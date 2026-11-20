#include "common.hlsli"

static const float kWorldGridView = 1.0;
static const float kMajorLineEvery = 5.0;
static const float kDepthViewRange = 1600.0;
static const float kDeepWater = 8.0;

float3 GridColor(float3 world)
{
    float2 cells = world.xy / cTexelGrid.z;
    float2 minor = abs(frac(cells - 0.5) - 0.5) / max(fwidth(cells), 1.0e-4);
    float2 major = abs(frac(cells / kMajorLineEvery - 0.5) - 0.5) / max(fwidth(cells / kMajorLineEvery), 1.0e-4);
    float xLine = 1.0 - saturate(min(minor.x, major.x * 0.5));
    float yLine = 1.0 - saturate(min(minor.y, major.y * 0.5));
    return float3(xLine, yLine, 0);
}

float3 WaterColor(float thickness)
{
    return lerp(float3(0.2, 1.0, 1.0), float3(0.0, 0.1, 0.6), saturate(thickness / kDeepWater));
}

float4 main(float2 pixel : VPOS) : COLOR0
{
    float rawDepth = SampleRawDepth(sDepth, pixel);
    float viewZ = ViewZ(rawDepth);
    float3 world = WorldPosition(ViewPosition(pixel, viewZ));
    float3 grid = GridColor(world);
    float thickness = WaterThickness(pixel, rawDepth);
    float opacity = cTexelGrid.w;

    if (BeyondFarClip(rawDepth))
        return float4(0, 0, 0.1, cModeProbe.x == kWorldGridView ? 0 : opacity);

    float4 color;
    if (cModeProbe.x == kWorldGridView)
    {
        float gridAlpha = max(grid.x, grid.y);
        color = float4(grid, gridAlpha * opacity);
        if (thickness > 0)
            color = float4(lerp(WaterColor(thickness), grid, gridAlpha), opacity);
    }
    else
    {
        float shade = saturate(log2(1.0 + viewZ) / log2(1.0 + kDepthViewRange));
        color = float4(shade.xxx, opacity);
        if (thickness > 0)
            color.rgb = lerp(color.rgb, WaterColor(thickness), 0.7);
    }
    return color;
}
