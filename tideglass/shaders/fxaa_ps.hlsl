#include "common.hlsli"

sampler2D sScene : register(s2);

static const float kReduceMul = 1.0 / 8.0;
static const float kReduceMin = 1.0 / 128.0;
static const float kSpanMax = 8.0;
static const float kEdgeThresholdMin = 0.0312;
static const float kEdgeThreshold = 0.125;
static const float3 kLumaWeights = float3(0.299, 0.587, 0.114);

float3 SceneAt(float2 uv)
{
    return tex2Dlod(sScene, float4(uv, 0, 0)).rgb;
}

float4 main(float2 pixel : VPOS) : COLOR0
{
    float2 texel = cTexelGrid.xy;
    float2 uv = (pixel + 0.5) * texel;
    float3 centre = SceneAt(uv);
    float lumaNW = dot(SceneAt(uv + float2(-1, -1) * texel), kLumaWeights);
    float lumaNE = dot(SceneAt(uv + float2(1, -1) * texel), kLumaWeights);
    float lumaSW = dot(SceneAt(uv + float2(-1, 1) * texel), kLumaWeights);
    float lumaSE = dot(SceneAt(uv + float2(1, 1) * texel), kLumaWeights);
    float lumaM = dot(centre, kLumaWeights);
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    if (lumaMax - lumaMin < max(kEdgeThresholdMin, lumaMax * kEdgeThreshold))
        return float4(centre, 1);

    float2 direction = float2(-((lumaNW + lumaNE) - (lumaSW + lumaSE)), (lumaNW + lumaSW) - (lumaNE + lumaSE));
    float reduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.25 * kReduceMul, kReduceMin);
    float scale = 1.0 / (min(abs(direction.x), abs(direction.y)) + reduce);
    direction = clamp(direction * scale, -kSpanMax, kSpanMax) * texel;

    float3 nearPair = 0.5 * (SceneAt(uv + direction * (1.0 / 3.0 - 0.5)) + SceneAt(uv + direction * (2.0 / 3.0 - 0.5)));
    float3 widePair = nearPair * 0.5 + 0.25 * (SceneAt(uv - direction * 0.5) + SceneAt(uv + direction * 0.5));
    float lumaWide = dot(widePair, kLumaWeights);
    return float4(lumaWide < lumaMin || lumaWide > lumaMax ? nearPair : widePair, 1);
}
