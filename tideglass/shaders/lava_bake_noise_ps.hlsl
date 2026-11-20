// The molten swirl and the slower patterns. r: broad and warped; g: ridged, for ribbons; b: broad; a: blobs.
#include "lava_bake.hlsli"
float4 main(float2 pixel : VPOS) : COLOR0
{
    float2 uv = BakeUv(pixel);
    float a = Fbm(uv + (float2(Fbm(uv, 3.0, 3), Fbm(uv + 0.5, 3.0, 3)) - 0.5) * 0.28, 4.0, 5);
    float2 warp = float2(Fbm(uv + 0.23, 4.0, 3), Fbm(uv + 0.61, 4.0, 3)) - 0.5;
    float ridged = 1.0 - abs(2.0 * Fbm(uv + warp * 0.3, 5.0, 4) - 1.0);
    float b = Fbm(uv + 0.41, 3.0, 4);
    float blobs = Fbm(uv + 0.83, 7.0, 3);
    return float4(saturate((a - 0.25) / 0.5), saturate((ridged - 0.45) / 0.55), saturate((b - 0.25) / 0.5),
                  saturate((blobs - 0.25) / 0.5));
}
