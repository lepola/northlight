#include "common.hlsli"

sampler2D sSceneAfterSlime : register(s2);

static const float kDepthMoved = 1.0e-7;

// cModeProbe.x: 0 the surface's depth, 1 the client's colour there, 2 a fade draw's mark: the depth under it (above 0).
float4 main(float2 pixel : VPOS) : COLOR0
{
    float current = SampleRawDepth(sDepth, pixel);
    float before = SampleRawDepth(sOpaqueDepth, pixel);
    clip(before - current - kDepthMoved);
    float4 result = float4(current * cModeProbe.y, 0, 0, 1);
    if (cModeProbe.x > 1.5)
        result = float4(max(before, 1.0e-6) * cModeProbe.y, 0, 0, 1);
    else if (cModeProbe.x > 0.5)
        result = tex2Dlod(sSceneAfterSlime, float4((pixel + 0.5) * cTexelGrid.xy, 0, 0));
    return result;
}
