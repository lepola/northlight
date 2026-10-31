// scRGB linear BT.709: 1.0 represents a neutral white of 80 nits.
float4 HdrColor : register(c0); // RGB scale; w = UI premultiply flag or world peak / 80 nits
float4 HdrGrade : register(c1); // contrast about 18% grey; exposure multiplier (2^EV)
sampler2D HdrScene : register(s0);
sampler2D HdrAux : register(s1);
sampler2D HdrExtra : register(s2);
sampler2D HdrLegacy : register(s3);
float4 HdrParams : register(c2);
float4 HdrAdaptation : register(c3); // dt, light T90, dark T90, authored reference grey
float4 HdrLimits : register(c4); // automatic min/max EV, reset history, reserved
float4 HdrLegacyFog : register(c5);
float4 HdrLegacyFogColor : register(c6);
float3 hdrDecode(float3 x) {
    x=max(x,0);
    return lerp(x/12.92,pow((x+.055)/1.055,2.4),step(.04045,x));
}
float4 HdrUi(float4 original:COLOR0):COLOR0 {
    float alpha=original.a;
    float3 encoded=HdrColor.w>.5?original.rgb/max(alpha,.000001):original.rgb;
    // These are legacy SDR shader outputs. Reproduce the original UNORM
    // colour range before decoding; owned HDR radiance uses separate passes.
    float3 radiance=hdrDecode(saturate(encoded))*HdrColor.rgb;
    return float4(HdrColor.w>.5?radiance*alpha:radiance,alpha);
}
float4 HdrOverlay(float4 color:COLOR0,float2 uv:TEXCOORD0):COLOR0 {
    return HdrUi(color*tex2D(HdrScene,uv));
}
float4 fixedUiSource(float4 color,float2 uv){
    float4 texel=tex2D(HdrScene,uv);
    float3 rgb=lerp(1,texel.rgb,HdrGrade.x)*lerp(1,color.rgb,HdrGrade.y);
    float alpha=lerp(1,texel.a,HdrGrade.z)*lerp(1,color.a,HdrGrade.w);
    return float4(rgb,alpha);
}
float4 HdrFixedUi(float4 color:COLOR0,float2 uv:TEXCOORD0):COLOR0 {return HdrUi(fixedUiSource(color,uv));}
float4 HdrFixedUi2(float4 color:COLOR0,float2 uv:TEXCOORD0,float2 uv1:TEXCOORD1):COLOR0 {
    float4 source=fixedUiSource(color,uv);
    float4 other=1;
    if(HdrParams.x+HdrAdaptation.x>0)other=tex2D(HdrAux,lerp(uv,uv1,HdrParams.w));
    float3 rgb=lerp(1,other.rgb,HdrParams.x)*lerp(1,source.rgb,HdrParams.y)*lerp(1,color.rgb,HdrParams.z);
    float alpha=lerp(1,other.a,HdrAdaptation.x)*lerp(1,source.a,HdrAdaptation.y)*lerp(1,color.a,HdrAdaptation.z);
    return HdrUi(float4(rgb,alpha));
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

// Fixed-size GPU metering. Bounded log luminance limits isolated bright/black
// samples; G also records bright-area coverage for legacy highlight expansion.
float4 HdrMeter(float2 uv:TEXCOORD0):COLOR0 {
    float3 color=max(tex2D(HdrScene,uv).rgb,0);
    if(HdrParams.z>.5)color=hdrDecode(color);
    float luminance=max(dot(color,float3(.2126,.7152,.0722)),.000001);
    return float4(clamp(log2(luminance),-8,4),step(.65,max(color.r,max(color.g,color.b))),0,1);
}
float4 HdrReduce(float2 uv:TEXCOORD0):COLOR0 {
    float2 base=(floor(uv/(HdrParams.xy*8))*8+.5)*HdrParams.xy;
    float4 sum=0;
    [unroll]for(int y=0;y<8;++y)[unroll]for(int x=0;x<8;++x)
        sum+=tex2Dlod(HdrScene,float4(base+float2(x,y)*HdrParams.xy,0,0));
    return sum/64;
}
float4 HdrReduceLinear(float2 uv:TEXCOORD0):COLOR0 {
    // Each linear sample averages an exact 2x2 block. Meter sizes are powers
    // of two, so these coordinates lie exactly between its four texel centers.
    float2 base=(floor(uv/(HdrParams.xy*8))*8+1)*HdrParams.xy;
    float4 sum=0;
    [unroll]for(int y=0;y<4;++y)[unroll]for(int x=0;x<4;++x)
        sum+=tex2Dlod(HdrScene,float4(base+float2(x,y)*2*HdrParams.xy,0,0));
    return sum/16;
}
float4 HdrDecodeWorld(float2 uv:TEXCOORD0):COLOR0 {
    // A separately captured linear sky already contains the owned HDR sources.
    // The native horizon uses depth .94..1, beyond the main world band.
    // Only an untouched hardware clear value is sky, never that entire band.
    if(HdrParams.z>.5)clip((1.0-1.0/16777216.0)-tex2D(HdrAux,uv).r);
    float4 source=tex2D(HdrScene,uv);float3 color=hdrDecode(source.rgb);
    if(HdrParams.w>0){
        float2 statistics=tex2D(HdrExtra,float2(.5,.5)).rg;
        float intensity=max(color.r,max(color.g,color.b));
        float relative=log2(max(dot(color,float3(.2126,.7152,.0722)),.000001))-statistics.r;
        float weight=smoothstep(.65,1,intensity)*smoothstep(.5,2,relative)*(1-smoothstep(.05,.20,statistics.g));
        color*=exp2(HdrParams.w*weight);
    }
    return float4(color,source.a);
}
float4 HdrCopy(float2 uv:TEXCOORD0):COLOR0 {return tex2D(HdrScene,uv);}
float4 HdrLegacyComposite(float2 uv:TEXCOORD0):COLOR0 {
    float4 base=tex2D(HdrScene,uv);float3 encoded=tex2D(HdrLegacy,uv).rgb;
    float depth=saturate((tex2D(HdrAux,uv).r-HdrParams.x)*HdrParams.y);
    float z=HdrParams.z*HdrParams.w/max(HdrParams.w-depth*(HdrParams.w-HdrParams.z),.000001);
    float transmission=lerp(1,saturate(pow(max(z*HdrLegacyFog.x+HdrLegacyFog.y,0),HdrLegacyFog.z)),HdrLegacyFog.w);
    float3 nativeLinear=hdrDecode(encoded),fogEncoded=min((1-transmission)*HdrLegacyFogColor.rgb,encoded);
    float3 fogLinear=(nativeLinear-hdrDecode(max(encoded-fogEncoded,0)))*base.rgb/max(nativeLinear,.000001);
    float ao=tex2D(HdrExtra,uv).a;
    return float4(max(base.rgb-fogLinear,0)*ao+fogLinear,base.a);
}
float4 HdrAdapt(float2 uv:TEXCOORD0):COLOR0 {
    float measured=tex2D(HdrScene,float2(.5,.5)).r;
    float previous=tex2D(HdrAux,float2(.5,.5)).r;
    float target=clamp(log2(max(HdrAdaptation.w,.000001))-measured,HdrLimits.x,HdrLimits.y);
    float t90=target<previous?HdrAdaptation.y:HdrAdaptation.z;
    float blend=HdrLimits.z>.5?1:1-exp2(-3.32192809489*max(HdrAdaptation.x,0)/max(t90,.001));
    float exposure=clamp(lerp(previous,target,blend),HdrLimits.x,HdrLimits.y);
    return float4(exposure,target,measured,1);
}
float3 hdrBright(float3 color){
    float peak=max(color.r,max(color.g,color.b));
    return color*max(peak-1,0)/max(peak,.000001);
}
float4 HdrBloom(float2 uv:TEXCOORD0):COLOR0 {
    float3 sum=0;
    [unroll]for(int i=-2;i<=2;++i){
        float3 color=tex2D(HdrScene,uv+HdrParams.xy*i).rgb;
        if(HdrParams.z<.5)color=hdrBright(max(color,0));
        float weight=i==0?6:abs(i)==1?4:1;sum+=color*weight;
    }
    return float4(sum/16,1);
}
float4 HdrBloomHorizontal(float2 uv:TEXCOORD0):COLOR0 {
    float3 sum=0;
    [unroll]for(int i=-2;i<=2;++i){
        float3 color=hdrBright(max(tex2D(HdrScene,uv+HdrParams.xy*i).rgb,0));
        float weight=i==0?6:abs(i)==1?4:1;sum+=color*weight;
    }
    return float4(sum/16,1);
}
float4 HdrBloomVertical(float2 uv:TEXCOORD0):COLOR0 {
    // Half-texel pairs give exactly 1,4,6,4,1 with four samples. Unlike
    // 1.2-texel pairs, their weights survive hardware interpolation precision.
    float2 inner=float2(0,.5*HdrParams.y),outer=inner*3;
    return float4((tex2D(HdrScene,uv-outer).rgb*2+tex2D(HdrScene,uv-inner).rgb*6+
                   tex2D(HdrScene,uv+inner).rgb*6+tex2D(HdrScene,uv+outer).rgb*2)/16,1);
}
struct HdrPresentationOutput {float4 hdr:COLOR0;float4 legacy:COLOR1;};
HdrPresentationOutput HdrWorldLinear(float2 uv:TEXCOORD0) {
    float4 source=tex2D(HdrScene,uv);float3 radiance=max(source.rgb,0);
    if(HdrGrade.w>0)radiance+=tex2D(HdrExtra,uv).rgb*HdrGrade.w;
    float luminance=max(dot(radiance,float3(.2126,.7152,.0722)),.000001);
    float automatic=HdrGrade.z>.5?tex2D(HdrAux,float2(.5,.5)).r:0;
    [branch]if(HdrGrade.x!=1)radiance*=pow(luminance/.18,HdrGrade.x-1);
    radiance*=HdrGrade.y*exp2(automatic)*HdrColor.rgb;
    float peak=max(radiance.r,max(radiance.g,radiance.b));
    float span=HdrParams.y;
    float mapped=span>.000001?HdrColor.x+span*(1-exp2(HdrParams.x*max(peak-HdrColor.x,0))):min(peak,HdrColor.w);
    if(peak>HdrColor.x)radiance*=mapped/max(peak,.000001);
    HdrPresentationOutput result;result.hdr=float4(radiance,source.a);
    // Client scene copies remain before display grading, exposure and bloom.
    float3 compatible=saturate(source.rgb);
    compatible=lerp(compatible*12.92,1.055*pow(compatible,1/2.4)-.055,step(.0031308,compatible));
    result.legacy=float4(compatible,source.a);return result;
}
float4 HdrWorldLinearOnly(float2 uv:TEXCOORD0):COLOR0 {return HdrWorldLinear(uv).hdr;}
float4 HdrLegacyEncode(float2 uv:TEXCOORD0):COLOR0 {return HdrWorldLinear(uv).legacy;}
float4 HdrCalibration(float2 uv:TEXCOORD0):COLOR0 {return HdrColor;}
float4 HdrLayer(float2 uv:TEXCOORD0):COLOR0 {
    float4 layer=tex2D(HdrScene,uv);float alpha=saturate(layer.a);
    float3 encoded=HdrParams.x>.5?layer.rgb/max(alpha,.000001):layer.rgb;
    float3 color=HdrParams.y>.5?encoded:hdrDecode(saturate(encoded))*HdrColor.rgb;
    if(HdrParams.x>.5)color*=alpha;
    return float4(color,alpha);
}
