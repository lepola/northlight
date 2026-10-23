// Original material coverage and depth are shared by the normal and occlusion targets.
sampler2D Diffuse:register(s0);
sampler2D LayerMask:register(s1);
sampler2D GeneratedNormal:register(s2);
sampler2D GeneratedNormal1:register(s3);
float4 NativeLayerMask:register(c0);
float4 BumpCapture:register(c20);
float4 CaptureProjection:register(c21);
float4 ParallaxControl:register(c22);
float4 OcclusionControl:register(c23); // shadow strength, shadow steps, cavity strength, target enabled
float4 OcclusionLight:register(c24); // view direction, 1 sun / 2 moon / 0 inactive
#include "auto_parallax.hlsl"
struct MaterialValue {float3 normal,occlusion;float2 uv;};
struct CaptureResult {float4 normal:COLOR0;float4 occlusion:COLOR1;};
float2 octEncode(float3 n){n/=max(dot(abs(n),1),1e-6);return (n.z>=0?n.xy:(1-abs(n.yx))*float2(n.x>=0?1:-1,n.y>=0?1:-1))*.5+.5;}
MaterialValue materialValue(sampler2D map,float2 uv,float4 p) {
    MaterialFrame f=materialFrame(uv,p,CaptureProjection);
    MaterialValue v;v.uv=parallaxUVFrame(map,uv,p.w,ParallaxControl,f);
    float4 data=tex2Dgrad(map,v.uv,f.tx,f.ty);
    float2 xy=(data.rg*255-128)/127;
    float3 local=float3(xy,sqrt(saturate(1-dot(xy,xy))));
    v.normal=normalize(f.tangent*local.x+f.bitangent*local.y+f.normal*local.z);
    v.occlusion=1;
    if(OcclusionControl.w>.5){
        v.occlusion.b=lerp(1,data.a,OcclusionControl.z);
        if(OcclusionControl.x>0){
            float shade=lerp(1,parallaxShadow(map,v.uv,data.b,p.w,f,ParallaxControl,OcclusionLight,OcclusionControl.y),OcclusionControl.x);
            if(OcclusionLight.w>1.5)v.occlusion.g=shade;else v.occlusion.r=shade;
        }
    }
    return v;
}
CaptureResult captureValue(MaterialValue v,float4 p,float alpha) {
    clip(alpha-BumpCapture.z);
    CaptureResult o;o.normal=float4(octEncode(v.normal),p.w,saturate(alpha));o.occlusion=float4(v.occlusion,saturate(alpha));return o;
}
CaptureResult BumpOpaque(float2 uv:TEXCOORD0,float4 p:TEXCOORD7){clip(BumpCapture.y-p.w);return captureValue(materialValue(GeneratedNormal,uv,p),p,1);}
CaptureResult BumpLayer(float2 uv:TEXCOORD0,float2 maskUV:TEXCOORD1,float4 p:TEXCOORD7){clip(BumpCapture.y-p.w);return captureValue(materialValue(GeneratedNormal,uv,p),p,saturate(dot(tex2D(LayerMask,maskUV),NativeLayerMask)));}
CaptureResult BumpWall(float2 uv:TEXCOORD0,float4 color:COLOR0,float4 p:TEXCOORD7){clip(BumpCapture.y-p.w);
    MaterialValue v=materialValue(GeneratedNormal,uv,p);return captureValue(v,p,tex2Dgrad(Diffuse,v.uv,ddx(uv),ddy(uv)).a*color.a);
}
CaptureResult BumpGrass(float2 uv:TEXCOORD0,float4 fade:COLOR1,float4 p:TEXCOORD7){clip(BumpCapture.y-p.w);return captureValue(materialValue(GeneratedNormal,uv,p),p,tex2D(Diffuse,uv).a*fade.x);}
CaptureResult BumpWallOpaque(float2 uv:TEXCOORD0,float4 color:COLOR0,float4 p:TEXCOORD7){clip(BumpCapture.y-p.w);return captureValue(materialValue(GeneratedNormal,uv,p),p,color.a);}
CaptureResult BumpComposite(float2 uv:TEXCOORD0,float2 uv1:TEXCOORD1,float4 color:COLOR0,float4 blend:COLOR1,float4 p:TEXCOORD7){clip(BumpCapture.y-p.w);
    MaterialValue first=materialValue(GeneratedNormal,uv,p),second=materialValue(GeneratedNormal1,uv1,p);
    float alpha=lerp(tex2Dgrad(LayerMask,second.uv,ddx(uv1),ddy(uv1)).a,tex2Dgrad(Diffuse,first.uv,ddx(uv),ddy(uv)).a,blend.w)*color.a;
    first.normal=normalize(lerp(second.normal,first.normal,saturate(blend.w)));first.occlusion=lerp(second.occlusion,first.occlusion,saturate(blend.w));return captureValue(first,p,alpha);
}

// Native alpha is computed by the original material program in BumpNativePatch.
CaptureResult BumpNative(float2 uv:TEXCOORD0,float4 nativeColor:COLOR0,float4 p:TEXCOORD7){clip(BumpCapture.y-p.w);return captureValue(materialValue(GeneratedNormal,uv,p),p,lerp(1,nativeColor.a,BumpCapture.w));}
