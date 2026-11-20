// The crust's plates, with finer breaks inside them that stay shut until the heat rises past them. r: distance to the
// nearest crack; g: the plate's tone and a little detail.
#include "lava_bake.hlsli"
float4 main(float2 pixel : VPOS) : COLOR0
{
    float2 uv = BakeUv(pixel);
    float2 warp = float2(Fbm(uv, 5.0, 3), Fbm(uv + 0.37, 5.0, 3)) - 0.5;
    float2 kink = float2(Fbm(uv + 0.11, 19.0, 2), Fbm(uv + 0.71, 19.0, 2)) - 0.5;
    float2 q = uv + warp * 0.05 + kink * 0.012;
    float2 big = Network(q * 16.0, 16.0, 0.0);
    float2 fine = Network(q * 44.0 + 9.0, 44.0, 0.0);
    float edge = min(big.x, 0.15 + fine.x * 0.42);
    float detail = Fbm(uv, 24.0, 4);
    return float4(edge / 0.7, big.y * 0.62 + detail * 0.38, 0.0, 1.0);
}
