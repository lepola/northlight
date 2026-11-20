#include "common.hlsli"

float4 main(float2 pixel : VPOS) : COLOR0
{
    return float4(SampleRawDepth(sDepth, pixel), 0, 0, 1);
}
