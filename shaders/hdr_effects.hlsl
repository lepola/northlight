// scRGB linear BT.709: 1.0 represents a neutral white of 80 nits.
float4 HdrColor : register(c0); // RGB scale; w = UI premultiply flag or world peak / 80 nits
float4 HdrGrade : register(c1); // contrast about 18% grey; exposure multiplier (2^EV)
sampler2D HdrScene : register(s0);
float3 hdrDecode(float3 x) {
    x=max(x,0);
    return lerp(x/12.92,pow((x+.055)/1.055,2.4),step(.04045,x));
}
float4 HdrUi(float4 original:COLOR0):COLOR0 {
    float alpha=original.a;
    float3 encoded=HdrColor.w>.5?original.rgb/max(alpha,.000001):original.rgb;
    float3 radiance=hdrDecode(encoded)*HdrColor.rgb;
    return float4(HdrColor.w>.5?radiance*alpha:radiance,alpha);
}
float4 HdrWorld(float2 uv:TEXCOORD0):COLOR0 {
    float4 original=tex2D(HdrScene,uv);
    float3 radiance=hdrDecode(original.rgb);
    float luminance=max(dot(radiance,float3(.2126,.7152,.0722)),.000001);
    radiance*=pow(luminance/.18,HdrGrade.x-1)*HdrGrade.y*HdrColor.rgb;
    float peak=max(radiance.r,max(radiance.g,radiance.b));
    radiance*=min(1,HdrColor.w/max(peak,.000001));
    return float4(radiance,original.a);
}
