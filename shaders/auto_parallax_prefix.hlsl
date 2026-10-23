#include "auto_parallax.hlsl"
sampler2D HeightMap:register(s14);
float4 ParallaxProjection:register(c220);
float4 ParallaxControl:register(c221);
float4 ParallaxUV(float2 uv:TEXCOORD0,float4 p:TEXCOORD7):COLOR0 {
    return float4(parallaxUV(HeightMap,uv,p,ParallaxProjection,ParallaxControl),0,1);
}
