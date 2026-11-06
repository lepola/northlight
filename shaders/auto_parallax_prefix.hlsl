#include "auto_parallax.hlsl"
sampler2D HeightMap:register(s14);
float4 ParallaxProjection:register(c220);
float4 ParallaxControl:register(c221);
float4 ParallaxUV(float2 uv:TEXCOORD0,float4 p:TEXCOORD7):COLOR0 {
    return float4(parallaxUV(HeightMap,uv,p,ParallaxProjection,ParallaxControl),0,1);
}
sampler2D SharedTagMap:register(s12);
sampler2D SharedUVMap:register(s13);
float4 SharedPomControl:register(c222);
float4 ParallaxSharedUV(float2 uv:TEXCOORD0,float4 p:TEXCOORD7,float primitive:TEXCOORD6,float2 screen:VPOS):COLOR0 {
    MaterialFrame frame=materialFrame(uv,p,ParallaxProjection);
    float2 pixel=(screen+.5)*SharedPomControl.xy;
    // This point-filtered cache has one mip. These gradients cannot change its
    // lookup, but force every fallback derivative ahead of the divergent branch.
    float3 dx=ddx(frame.p),dy=ddy(frame.p);
    float4 cached=tex2Dgrad(SharedTagMap,pixel,frame.tx+dx.xy+dx.zz,frame.ty+dy.xy+dy.zz);
    [branch]if(cached.z==SharedPomControl.z+floor(primitive+.5))return float4(tex2Dlod(SharedUVMap,float4(pixel,0,0)).xy,1,1);
    return float4(parallaxUVFrame(HeightMap,uv,p.w,ParallaxControl,frame),0,1);
}
