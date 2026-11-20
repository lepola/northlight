#include "water_registers.hlsli"
float4 cViewportPx : WATER_C_REGISTER(WATER_SLOT_cViewportPx);
float4 cTexelGrid : WATER_C_REGISTER(WATER_SLOT_cTexelGrid);
float4 cViewToNdc : WATER_C_REGISTER(WATER_SLOT_cViewToNdc);
float4 cDepthMapping : WATER_C_REGISTER(WATER_SLOT_cDepthMapping);
float4 cViewToWorld0 : WATER_C_REGISTER(WATER_SLOT_cViewToWorld0);
float4 cViewToWorld1 : WATER_C_REGISTER(WATER_SLOT_cViewToWorld1);
float4 cViewToWorld2 : WATER_C_REGISTER(WATER_SLOT_cViewToWorld2);
float4 cViewToWorld3 : WATER_C_REGISTER(WATER_SLOT_cViewToWorld3);
float4 cModeProbe : WATER_C_REGISTER(WATER_SLOT_cModeProbe);

// cModeProbe.w is always 1. A value multiplied by it cannot be folded by the compiler, so unrolled loop steps and
// constants spread across vectors are worked out instead of each taking a literal constant register of its own
// (ps_3_0 has 224 in all, shared with the uniforms above).
float Unfolded(float value) { return value * cModeProbe.w; }

sampler2D sDepth : WATER_S_REGISTER(WATER_SLOT_sDepth);
sampler2D sOpaqueDepth : WATER_S_REGISTER(WATER_SLOT_sOpaqueDepth);

static const float kNoWater = -1.0;
static const float kBeyondFarClip = -2.0;
static const float kSkyDepth = 0.99995;

float SampleRawDepth(sampler2D depth, float2 pixel)
{
    return tex2Dlod(depth, float4((pixel + 0.5) * cTexelGrid.xy, 0, 0)).r;
}

bool BeyondFarClip(float rawDepth)
{
    return rawDepth > cDepthMapping.z;
}

bool IsTrueSky(float rawDepth)
{
    return rawDepth >= kSkyDepth;
}

bool HasOpaqueSnapshot()
{
    return cDepthMapping.w > 0.5;
}

float ViewZ(float rawDepth)
{
    return cDepthMapping.y / (rawDepth - cDepthMapping.x);
}

float3 ViewPosition(float2 pixel, float viewZ)
{
    float2 viewportUv = (pixel + 0.5 - cViewportPx.xy) / cViewportPx.zw;
    float2 ndc = float2(viewportUv.x * 2.0 - 1.0, 1.0 - viewportUv.y * 2.0);
    return float3((ndc - cViewToNdc.zw) / cViewToNdc.xy * viewZ, viewZ);
}

float3 WorldPosition(float3 view)
{
    return view.x * cViewToWorld0.xyz + view.y * cViewToWorld1.xyz + view.z * cViewToWorld2.xyz + cViewToWorld3.xyz;
}

// Pixel-center mapping for reduced textures, including odd source dimensions.
// The target size is explicit: multiplying by 0.5 is not the same mapping when
// the reduced texture rounds up at an odd width or height.
float2 TexturePixelFromScreen(float2 pixel, float2 textureSize)
{
    return (pixel + 0.5) * cTexelGrid.xy * textureSize - 0.5;
}

float WaterThickness(float2 pixel, float surfaceDepth)
{
    if (!HasOpaqueSnapshot())
        return kNoWater;
    float opaqueDepth = SampleRawDepth(sOpaqueDepth, pixel);
    if (surfaceDepth >= opaqueDepth)
        return kNoWater;
    float surfaceZ = ViewZ(surfaceDepth);
    float3 surface = ViewPosition(pixel, surfaceZ);
    float rayLengthPerViewZ = length(surface) / surfaceZ;
    float opaqueZ = BeyondFarClip(opaqueDepth) ? 1.0e4 : ViewZ(opaqueDepth);
    return (opaqueZ - surfaceZ) * rayLengthPerViewZ;
}
