// The hot foam's line network, curved and kinked with rounded joins as the water foam's are. r: distance to the nearest
// line; g: how thick the line is along its length; b: which stretches remain as the foam thins.
#include "lava_bake.hlsli"
float4 main(float2 pixel : VPOS) : COLOR0
{
    float2 uv = BakeUv(pixel);
    float2 bend = float2(Fbm(uv + 0.19, 4.0, 2), Fbm(uv + 0.67, 4.0, 2)) - 0.5;
    float2 kink = abs(float2(Fbm(uv + 0.43, 13.0, 2), Fbm(uv + 0.91, 13.0, 2)) - 0.5) - 0.13;
    float2 q = uv + bend * 0.05 + kink * 0.07;
    float lines = max(Network(q * 12.0, 12.0, 0.3).x, 0.0);
    float swell = Fbm(q + 0.31, 11.0, 2);
    float pieces = Fbm(uv + 0.57, 6.0, 2);
    return float4(saturate(lines / 0.75), saturate((swell - 0.25) / 0.5), saturate((pieces - 0.25) / 0.5), 1.0);
}
