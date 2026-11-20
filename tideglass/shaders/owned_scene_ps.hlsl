float4 cFogRange : register(c0);
float4 cFogColour : register(c1);

float4 main(float4 colour : COLOR0, float above : TEXCOORD0, float distance : TEXCOORD1) : COLOR0
{
    clip(above);
    float fog = saturate((distance - cFogRange.x) * cFogRange.y);
    return float4(lerp(colour.rgb, cFogColour.rgb, fog), 1);
}
