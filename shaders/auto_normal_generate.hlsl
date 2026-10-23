sampler2D Diffuse:register(s0);
float4 Texel:register(c0); // source texel size, normal strength, unused
float4 HeightProfile:register(c1);
#include "auto_material_height.hlsl"
float luminance(float2 uv){return inferredHeight(tex2Dlod(Diffuse,float4(uv,0,0)).rgb,HeightProfile);}
float4 AutoNormal(float2 uv:TEXCOORD0):COLOR0 {
    // Central differences in texture space, independent of the screen footprint.
    float x=luminance(uv+float2(Texel.x,0))-luminance(uv-float2(Texel.x,0));
    float y=luminance(uv+float2(0,Texel.y))-luminance(uv-float2(0,Texel.y));
    float3 normal=normalize(float3(-x*Texel.z,-y*Texel.z,1));
    float center=luminance(uv);
    float2 radius=Texel.xy*4;
    float around=(luminance(uv+float2(radius.x,0))+luminance(uv-float2(radius.x,0))+luminance(uv+float2(0,radius.y))+luminance(uv-float2(0,radius.y)))*.25;
    float cavity=1-saturate((around-center-.01)*3);
    // Positive tangent Z is reconstructed, leaving B for height and A for cavity AO.
    return float4(normal.xy*(127.0/255)+128.0/255,center,cavity);
}

// Broad painted anatomy and skin tones never become model volume. The radius
// matches modelValue at the bounded CPU height mip (1/32 of the texture).
float modelHeight(float2 uv){
    float3 weight=float3(.299,.587,.114);
    float center=dot(tex2Dlod(Diffuse,float4(uv,0,0)).rgb,weight);
    float around=dot(tex2Dlod(Diffuse,float4(uv+float2(1./32,0),0,0)).rgb+tex2Dlod(Diffuse,float4(uv-float2(1./32,0),0,0)).rgb+tex2Dlod(Diffuse,float4(uv+float2(0,1./32),0,0)).rgb+tex2Dlod(Diffuse,float4(uv-float2(0,1./32),0,0)).rgb,weight)*.25;
    return .5+clamp((center-around)*.7*HeightProfile.z,-.12,.12);
}
float4 AutoNormalModel(float2 uv:TEXCOORD0):COLOR0 {
    float x=modelHeight(uv+float2(Texel.x,0))-modelHeight(uv-float2(Texel.x,0));
    float y=modelHeight(uv+float2(0,Texel.y))-modelHeight(uv-float2(0,Texel.y));
    float3 normal=normalize(float3(-x*Texel.z,-y*Texel.z,1));float center=modelHeight(uv);
    return float4(normal.xy*(127.0/255)+128.0/255,center,1-saturate((.5-center-.01)*3));
}
