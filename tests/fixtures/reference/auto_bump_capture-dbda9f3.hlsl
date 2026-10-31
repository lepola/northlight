// Original material coverage and depth are shared by the normal and occlusion targets.
sampler2D Diffuse:register(s0);
sampler2D LayerMask:register(s1);
sampler2D GeneratedNormal:register(s2);
sampler2D GeneratedNormal1:register(s3);
float4 NativeLayerMask:register(c0);
float4 BumpCapture:register(c20); // w: WMO opaque coverage offset; M2 native-alpha selector
float4 CaptureProjection:register(c21);
float4 ParallaxControl:register(c22);
float4 OcclusionControl:register(c23); // shadow strength, shadow steps, cavity strength, near range (0 disables)
float4 OcclusionLight:register(c24); // view direction, 1 sun / 2 moon / 0 inactive
float4 TerrainToWorld[3]:register(c26); // view vectors to the world frame, terrain only
#ifdef NORTHLIGHT_SHARED_POM
float4 SharedPomControl:register(c25);
#define POM_ID_INPUT ,float primitive:TEXCOORD6
#define POM_ID_PARAMETER ,float primitive
#define POM_ID_ARGUMENT ,primitive
#else
#define POM_ID_INPUT
#define POM_ID_PARAMETER
#define POM_ID_ARGUMENT
#endif
#include "auto_parallax.hlsl"
struct MaterialValue {float3 normal,reference,occlusion;float2 uv,uv1;};
struct CaptureResult {float4 normal:COLOR0;float4 occlusion:COLOR1;
#ifdef NORTHLIGHT_SHARED_POM
    float4 uv:COLOR2;float4 uv1:COLOR3;
#endif
};
float2 octEncode(float3 n){n/=max(dot(abs(n),1),1e-6);return (n.z>=0?n.xy:(1-abs(n.yx))*float2(n.x>=0?1:-1,n.y>=0?1:-1))*.5+.5;}
MaterialValue materialValue(sampler2D map,float2 uv,float4 p,float3 smoothNormal,float4 parallaxControl) {
    MaterialFrame f=materialFrame(uv,p,CaptureProjection);
    MaterialValue v;v.uv=parallaxUVFrame(map,uv,p.w,parallaxControl,f);v.uv1=0;
    float4 data=tex2Dgrad(map,v.uv,f.tx,f.ty);
    float2 xy=(data.rg*255-128)/127;
    float3 local=float3(xy,sqrt(saturate(1-dot(xy,xy))));
    float3 n=f.normal,t=f.tangent,b=f.bitangent;
    if(dot(smoothNormal,smoothNormal)>1e-12){
        n=normalize(smoothNormal);t-=n*dot(t,n);b-=n*dot(b,n);
        t*=rsqrt(max(dot(t,t),1e-12));b*=rsqrt(max(dot(b,b),1e-12));
    }
    v.reference=n;v.normal=normalize(t*local.x+b*local.y+n*local.z);
    v.occlusion=1;
    [branch]if(p.w<OcclusionControl.w){
        v.occlusion.b=lerp(1,data.a,OcclusionControl.z);
        if(OcclusionControl.x>0){
            float shade=lerp(1,parallaxShadow(map,v.uv,data.b,p.w,f,parallaxControl,OcclusionLight,OcclusionControl.y),OcclusionControl.x);
            v.occlusion.rg=OcclusionLight.w>1.5?float2(1,shade):float2(shade,1);
        }
    }
    return v;
}
MaterialValue materialValue(sampler2D map,float2 uv,float4 p,float3 smoothNormal){return materialValue(map,uv,p,smoothNormal,ParallaxControl);}
MaterialValue materialValue(sampler2D map,float2 uv,float4 p){return materialValue(map,uv,p,0);}
MaterialValue normalMaterialValue(sampler2D map,float2 uv,float4 p,float3 smoothNormal){return materialValue(map,uv,p,smoothNormal,float4(0,ParallaxControl.yzw));}
MaterialValue normalMaterialValue(sampler2D map,float2 uv,float4 p){return normalMaterialValue(map,uv,p,0);}
CaptureResult captureValue(MaterialValue v,float4 p,float alpha POM_ID_PARAMETER) {
    clip(alpha-BumpCapture.z);
    // Store the perturbation relative to the capture's smooth reference.
    // A neutral map is (0,0,1), even when scene-depth normals differ from it.
    float3 delta=v.normal-v.reference;
    float along=mad(dot(v.reference.xy,delta.xy),rcp(max(1+v.reference.z,1e-4)),delta.z);
    float3 detail=float3(mad(-v.reference.xy,along,delta.xy),max(dot(v.reference,v.normal),0));
    float2 encoded=mad(detail.xy,rcp(max(dot(abs(detail),1),1e-6))*.5,.5);
    CaptureResult o;o.normal=float4(encoded,p.w,saturate(alpha));o.occlusion=float4(v.occlusion,saturate(alpha));
#ifdef NORTHLIGHT_SHARED_POM
    o.uv=float4(v.uv,SharedPomControl.z+floor(primitive+.5),1);o.uv1=float4(v.uv1,0,1);
#endif
    return o;
}
CaptureResult BumpOpaque(float2 uv:TEXCOORD0,float4 p:TEXCOORD7 POM_ID_INPUT){clip(BumpCapture.y-p.w);return captureValue(materialValue(GeneratedNormal,uv,p),p,1 POM_ID_ARGUMENT);}
CaptureResult BumpLayer(float2 uv:TEXCOORD0,float2 maskUV:TEXCOORD1,float4 p:TEXCOORD7 POM_ID_INPUT){clip(BumpCapture.y-p.w);return captureValue(materialValue(GeneratedNormal,uv,p),p,saturate(dot(tex2D(LayerMask,maskUV),NativeLayerMask)) POM_ID_ARGUMENT);}
CaptureResult captureTerrainValue(MaterialValue v,float4 p,float alpha POM_ID_PARAMETER){
    v.normal=v.normal.x*TerrainToWorld[0].xyz+v.normal.y*TerrainToWorld[1].xyz+v.normal.z*TerrainToWorld[2].xyz;
    v.reference=v.reference.x*TerrainToWorld[0].xyz+v.reference.y*TerrainToWorld[1].xyz+v.reference.z*TerrainToWorld[2].xyz;
    CaptureResult o=captureValue(v,p,alpha POM_ID_ARGUMENT);
    // Signed depth identifies world-frame detail without another render target.
    // Terrain is a height field: its authored normal has no -Z pole.
    o.normal.b=-p.w;return o;
}
CaptureResult BumpTerrainOpaque(float2 uv:TEXCOORD0,float3 normal:TEXCOORD5,float4 p:TEXCOORD7 POM_ID_INPUT){clip(BumpCapture.y-p.w);return captureTerrainValue(materialValue(GeneratedNormal,uv,p,normal),p,1 POM_ID_ARGUMENT);}
CaptureResult BumpTerrainLayer(float2 uv:TEXCOORD0,float2 maskUV:TEXCOORD1,float3 normal:TEXCOORD5,float4 p:TEXCOORD7 POM_ID_INPUT){clip(BumpCapture.y-p.w);return captureTerrainValue(materialValue(GeneratedNormal,uv,p,normal),p,saturate(dot(tex2D(LayerMask,maskUV),NativeLayerMask)) POM_ID_ARGUMENT);}
CaptureResult BumpWall(float2 uv:TEXCOORD0,float4 color:COLOR0,float4 p:TEXCOORD7 POM_ID_INPUT){clip(BumpCapture.y-p.w);
    MaterialValue v=materialValue(GeneratedNormal,uv,p);return captureValue(v,p,tex2Dgrad(Diffuse,v.uv,ddx(uv),ddy(uv)).a*color.a+BumpCapture.w POM_ID_ARGUMENT);
}
CaptureResult BumpGrass(float2 uv:TEXCOORD0,float4 fade:COLOR1,float4 p:TEXCOORD7 POM_ID_INPUT){clip(BumpCapture.y-p.w);return captureValue(materialValue(GeneratedNormal,uv,p),p,tex2D(Diffuse,uv).a*fade.x POM_ID_ARGUMENT);}
CaptureResult BumpWallOpaque(float2 uv:TEXCOORD0,float4 color:COLOR0,float4 p:TEXCOORD7 POM_ID_INPUT){clip(BumpCapture.y-p.w);return captureValue(materialValue(GeneratedNormal,uv,p),p,color.a+BumpCapture.w POM_ID_ARGUMENT);}
CaptureResult BumpComposite(float2 uv:TEXCOORD0,float2 uv1:TEXCOORD1,float4 color:COLOR0,float4 blend:COLOR1,float4 p:TEXCOORD7 POM_ID_INPUT){clip(BumpCapture.y-p.w);
    MaterialValue first=materialValue(GeneratedNormal,uv,p),second=materialValue(GeneratedNormal1,uv1,p);
    // Add the solid-coverage offset before the existing saturation. Folding it
    // into MAD keeps the shared composite within the 512-slot SM3 limit.
    float alpha=mad(mad(tex2Dgrad(Diffuse,first.uv,ddx(uv),ddy(uv)).a-tex2Dgrad(LayerMask,second.uv,ddx(uv1),ddy(uv1)).a,blend.w,tex2Dgrad(LayerMask,second.uv,ddx(uv1),ddy(uv1)).a),color.a,BumpCapture.w);
    first.uv1=second.uv;first.normal=mad(first.normal-second.normal,saturate(blend.w),second.normal);first.occlusion=mad(first.occlusion-second.occlusion,saturate(blend.w),second.occlusion);return captureValue(first,p,alpha POM_ID_ARGUMENT);
}

// Native alpha is computed by the original material program in BumpNativePatch.
CaptureResult BumpNative(float2 uv:TEXCOORD0,float4 nativeColor:COLOR0,float4 p:TEXCOORD7 POM_ID_INPUT){clip(BumpCapture.y-p.w);return captureValue(materialValue(GeneratedNormal,uv,p),p,lerp(1,nativeColor.a,BumpCapture.w) POM_ID_ARGUMENT);}

// These entries are selected only when the host already disables POM. They
// preserve the same frame, coverage, cavity and relative-normal encoding.
#ifndef NORTHLIGHT_SHARED_POM
CaptureResult BumpNormalOpaque(float2 uv:TEXCOORD0,float4 p:TEXCOORD7){clip(BumpCapture.y-p.w);return captureValue(normalMaterialValue(GeneratedNormal,uv,p),p,1);}
CaptureResult BumpNormalLayer(float2 uv:TEXCOORD0,float2 maskUV:TEXCOORD1,float4 p:TEXCOORD7){clip(BumpCapture.y-p.w);return captureValue(normalMaterialValue(GeneratedNormal,uv,p),p,saturate(dot(tex2D(LayerMask,maskUV),NativeLayerMask)));}
CaptureResult BumpNormalTerrainOpaque(float2 uv:TEXCOORD0,float3 normal:TEXCOORD5,float4 p:TEXCOORD7){clip(BumpCapture.y-p.w);return captureTerrainValue(normalMaterialValue(GeneratedNormal,uv,p,normal),p,1);}
CaptureResult BumpNormalTerrainLayer(float2 uv:TEXCOORD0,float2 maskUV:TEXCOORD1,float3 normal:TEXCOORD5,float4 p:TEXCOORD7){clip(BumpCapture.y-p.w);return captureTerrainValue(normalMaterialValue(GeneratedNormal,uv,p,normal),p,saturate(dot(tex2D(LayerMask,maskUV),NativeLayerMask)));}
CaptureResult BumpNormalGrass(float2 uv:TEXCOORD0,float4 fade:COLOR1,float4 p:TEXCOORD7){clip(BumpCapture.y-p.w);return captureValue(normalMaterialValue(GeneratedNormal,uv,p),p,tex2D(Diffuse,uv).a*fade.x);}
#endif
