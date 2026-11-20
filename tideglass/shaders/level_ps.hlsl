#include "common.hlsli"

sampler2D sPreviousLevel : register(s2);

static const float kSameBody = 0.75;
static const float kSmoothing = 0.08;
static const float kNoWaterDistance = 1.0e9;
static const int kSamplesPerSide = 4;
static const float kFirstSampleRow = 0.35;

float4 main() : COLOR0
{
    float4 previous = tex2Dlod(sPreviousLevel, float4(0.5, 0.5, 0, 0));
    bool havePrevious = previous.g > 0.5;
    float matchedSum = 0.0;
    float matchedCount = 0.0;
    float nearestHeight = 0.0;
    float nearestDistance = kNoWaterDistance;
    for (int y = 0; y < kSamplesPerSide; ++y)
    {
        for (int x = 0; x < kSamplesPerSide; ++x)
        {
            float2 uv = float2((x + 0.5) / kSamplesPerSide,
                               kFirstSampleRow + (y + 0.5) / kSamplesPerSide * (1.0 - kFirstSampleRow));
            float2 pixel = floor(cViewportPx.xy + uv * cViewportPx.zw);
            float surface = abs(SampleRawDepth(sDepth, pixel)); // magma stores its palette in the sign
            float opaque = SampleRawDepth(sOpaqueDepth, pixel);
            if (surface < opaque && !BeyondFarClip(surface))
            {
                float3 view = ViewPosition(pixel, ViewZ(surface));
                float height = WorldPosition(view).z;
                float distance = length(view);
                if (distance < nearestDistance)
                {
                    nearestDistance = distance;
                    nearestHeight = height;
                }
                if (havePrevious && abs(height - previous.r) < kSameBody)
                {
                    matchedSum += height;
                    matchedCount += 1.0;
                }
            }
        }
    }
    if (matchedCount > 0.0)
        return float4(lerp(previous.r, matchedSum / matchedCount, kSmoothing), 1, matchedCount, 1);
    if (nearestDistance < kNoWaterDistance)
        return float4(nearestHeight, 1, 0, 1);
    return previous;
}
