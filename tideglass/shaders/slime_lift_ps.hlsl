// The ground under a fade draw the host left drawn (standalone keeps it: its water is found by the depth it leaves), for
// the slime composite to measure the slime's thickness from: where the depth now is the fade surface itself, the depth
// that lay under it when it was drawn. Elsewhere (a model drawn later in front, other slime) the depth as it is.
#include "common.hlsli"

sampler2D sSlimeSurface : register(s2);
sampler2D sFadeBelow : register(s3);

static const float kSameSurface = 1.0e-6;

float4 main(float2 pixel : VPOS) : COLOR0
{
    float now = SampleRawDepth(sDepth, pixel);
    float surface = SampleRawDepth(sSlimeSurface, pixel);
    float below = abs(SampleRawDepth(sFadeBelow, pixel));
    float result = now;
    if (below > 0.0 && abs(now - surface) <= kSameSurface)
        result = below;
    return float4(result, 0, 0, 1);
}
