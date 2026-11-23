sampler2D source : register(s0);
float4 fogRange : register(c0);
float4 fogColour : register(c1);
float4 material : register(c2);
float4 ambient : register(c4);
float4 direct : register(c5);
float4 scroll : register(c11);
float4 main(float2 uv : TEXCOORD0, float above : TEXCOORD1, float distance : TEXCOORD2, float diffuse : TEXCOORD3) : COLOR0
{
    clip(above);
    float4 sample = tex2D(source, uv + scroll.xy);
    float fog = saturate((distance - fogRange.x) * fogRange.y);
    float3 lighting = lerp(saturate(ambient.rgb + direct.rgb * diffuse), 1, fmod(material.w,2));
    return float4(lerp(sample.rgb * lighting, fogColour.rgb, fog), sample.a);
}
