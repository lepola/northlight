// The plates again, with the slope of each plate's dome in b and a for its bevel.
#include "lava_bake.hlsli"
float Dome(float2 uv) { return smoothstep(0.0, 0.17, tex2Dlod(sBakeInput, float4(uv, 0, 0)).r * 0.7); }
float4 main(float2 pixel : VPOS) : COLOR0
{
    float2 uv = BakeUv(pixel);
    float e = 1.5 * cBake.x;
    float2 slope = float2(Dome(uv + float2(e, 0.0)) - Dome(uv - float2(e, 0.0)),
                          Dome(uv + float2(0.0, e)) - Dome(uv - float2(0.0, e)));
    float4 base = tex2Dlod(sBakeInput, float4(uv, 0, 0));
    return float4(base.rg, saturate(0.5 + slope * 3.0));
}
