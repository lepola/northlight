// Smooths the ambient occlusion along one screen axis, only across pixels at about the same depth (so it does not
// bleed over the edges of things). Run across, then down; the second run shades the scene with it.
#include "common.hlsli"

sampler2D sAo : register(s2);
sampler2D sScene : register(s3);

float4 cBlur : register(c9);  // step in pixels (x, y), depth tolerance (fraction of the distance), 1 = shade the scene
float4 cShade : register(c10); // the shade's colour, 0

static const int kReach = 6;

float DepthAt(float2 pixel, out bool surface)
{
    float raw = SampleRawDepth(sDepth, pixel);
    surface = !IsTrueSky(raw) && !BeyondFarClip(raw);
    return surface ? ViewZ(raw) : 0.0;
}

// Tucked-in places are multiplied towards the shade's colour: a soft, slightly cool dimming rather than black. Bright
// surfaces keep more of their light (they are bright mostly because the sun or a lamp reaches them, and the shade
// stands for the light from around that cannot), so sunlit sand is not dirtied the way darker ground is shaded.
static const float kHighlightKeep = 0.5;

float4 Finish(float2 pixel, float open)
{
    if (cBlur.w < 0.5)
        return float4(open, open, open, 1.0);
    float3 scene = tex2Dlod(sScene, float4((pixel + 0.5) * cTexelGrid.xy, 0, 0)).rgb;
    float brightness = dot(scene, float3(0.2126, 0.7152, 0.0722));
    float amount = saturate(1.0 - open) * (1.0 - kHighlightKeep * saturate(brightness));
    return float4(scene * lerp(1.0, cShade.rgb, amount), 1.0);
}

float4 main(float2 pixel : VPOS) : COLOR0
{
    // Whole pixels, so its neighbours and taps line up with the depth texels even at the edges of the view.
    pixel = floor(pixel);
    bool surface;
    float z = DepthAt(pixel, surface);
    if (!surface)
        return Finish(pixel, 1.0);
    float sum = 0.0;
    float total = 0.0;
    [unroll] for (int i = -kReach; i <= kReach; ++i)
    {
        float2 tap = clamp(pixel + cBlur.xy * i, cViewportPx.xy, cViewportPx.xy + cViewportPx.zw - 1.0);
        bool tapSurface;
        float tapZ = DepthAt(tap, tapSurface);
        float spread = i / (kReach * 0.6);
        float weight = exp(-spread * spread) * saturate(1.0 - abs(tapZ - z) / (z * cBlur.z));
        if (tapSurface)
        {
            sum += tex2Dlod(sAo, float4((tap + 0.5) * cTexelGrid.xy, 0, 0)).r * weight;
            total += weight;
        }
    }
    return Finish(pixel, total > 0.0 ? sum / total : 1.0);
}
