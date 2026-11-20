// s12/s13 hold the independently rendered mirrored colour/depth for this pass.
// The ordinary composite consumes premultiplied colour/confidence and path.
float4 cSceneryGrid : register(c65); // dimensions, reciprocal dimensions
float4 cSceneryPlane : register(c66); // water Z, projection A/B, valid
float4 cSceneryAlignment : register(c67); // x: D3D9 alignment; y: dry-land exclusion; z: receiver mask; w: view ID
sampler2D sReceiverMask : register(s14); // source water geometry, before wave lookup displacement
float4 cSceneryRegions[8] : register(c68); // unit world normal.xyz, world plane distance.w
float4 cSceneryMapping : register(c76); // source/main projection scale.xy and NDC offset.zw
float4 cReceiverContinuation : register(c77); // native height interval (xy), coverage half-height minus half-width (z)
float4 cReceiverCoverageFade : register(c78); // world XY centre, inner X reach, reciprocal fade width
#include "reflection_scenery_raster.hlsli"
ReflectionOutput SampleSceneryReflection(float2 pixel, LiquidSurface surface, out float excludedLand)
{
    // D3D9 integer raster centres and the independently rendered view's FOV.
    float2 at=SceneryRaster(pixel);
    bool available=true;
    float2 corner=floor(at),f=frac(at);
    float4 colour=0;
    float path=0;
    excludedLand=0;
    bool plane=cSceneryPlane.w>0.5 && abs(surface.world.z-cSceneryPlane.x)<kSameWaterBody;
    if(cSceneryPlane.w>1.5 && cSceneryPlane.w<2.5) {
        plane=false;
        [unroll] for(int region=0;region<8;++region) {
            float4 p=cSceneryRegions[region];
            plane=plane || (p.z>.25 && abs(dot(p.xyz,surface.world)-p.w)<kSameWaterBody);
        }
    }
    // Mode3: horizontal per-object heights; actual water pixels define the domain.
    if(cSceneryPlane.w>2.5) plane=true;
    [unroll] for(int k=0;k<4;++k)
    {
        float2 offset=float2(k==1 || k==3 ? 1:0,k>=2 ? 1:0);
        float2 texel=corner+offset,tapUv=(texel+0.5)*cSceneryGrid.zw;
        float raw=tex2Dlod(sReflectionHit,float4(tapUv,0,0)).r;
        bool valid=available && plane && all(texel>=0) && all(texel<cSceneryGrid.xy) && raw>0 && raw<1;
        float2 share=lerp(1-f,f,offset);
        float tapWeight=share.x*share.y;
        float4 sample=tex2Dlod(sReflection,float4(tapUv,0,0));
        float4 contribution=float4(sample.rgb,1);
        float weight=valid ? tapWeight:0;
        float z=valid ? cSceneryPlane.z/(raw-cSceneryPlane.y):1;
        float2 sourcePixel=SceneryPixel(texel)-(cSceneryAlignment.x>.5?.5:0);
        // A clipped reflection tap over dry land is outside the water domain,
        // not evidence for sky. Keep true empty taps within water in the blend.
        if(cSceneryAlignment.y>0.5 && !valid && all(texel>=0) && all(texel<cSceneryGrid.xy))
        {
            float2 guidePixel=cSceneryAlignment.x>0.5 ? sourcePixel+0.5:sourcePixel;
            float opaque=SampleRawDepth(sOpaqueDepth,guidePixel);
            float surfaceDepth=SampleRawDepth(sDepth,guidePixel);
            bool dry=InsideViewport(guidePixel) && opaque>0 && !IsTrueSky(opaque) && !BeyondFarClip(opaque) && surfaceDepth>=opaque;
            excludedLand+=dry ? tapWeight:0;
        }
        float3 source=WorldPosition(ViewPosition(sourcePixel,z));
        // Flat views use their receiving height; sloped fields keep the established
        // warped geometry distance. The receiver mask separates independently drawn views.
        if(cSceneryPlane.w<1.5)source.z=2*cSceneryPlane.x-source.z;
        colour+=contribution*weight;
        path+=length(source-surface.world)*weight;
    }
    ReflectionOutput result;
    result.colour=colour/max(1-excludedLand,1e-5);
    // Empty space retains zero confidence.
    // Clear depth means unavailable in this extra view, not proven global sky.
    result.hit=float4(available?(colour.a>0 ? 3:2):1,path/max(colour.a,1e-5),0,surface.world.z);
    return result;
}

ReflectionOutput SceneryReflection(float2 pixel, float2 unbentPixel, LiquidSurface surface)
{
    float excludedLand;
    ReflectionOutput result=SampleSceneryReflection(pixel,surface,excludedLand);
    // At contact the smaller mirror can straddle its plane cut while the native
    // pixel is still wet. Its surviving bank taps are colour evidence, not a
    // fractional hole showing sky. Ease that recovery out as the water deepens.
    float contact=1-smoothstep(kShoreFade,2*kShoreFade,surface.thickness*abs(surface.toCamera.z));
    // True empty scenery within water retains the ordinary sky fallback.
    [branch] if(result.colour.a<1 && (excludedLand>0 || contact>0) && any(abs(pixel-unbentPixel)>.01)){
        float unused;
        ReflectionOutput fallback=SampleSceneryReflection(unbentPixel,surface,unused);
        float original=result.colour.a;
        float recovery=max(contact,excludedLand>0 ? 1:0);
        float recovered=fallback.colour.a>1e-5 ? (1-original)*recovery:0;
        result.hit.g=(result.hit.g*original+fallback.hit.g*recovered)/max(original+recovered,1e-5);
        result.colour+=float4(fallback.colour.rgb/max(fallback.colour.a,1e-5),1)*recovered;
        result.hit.r=result.colour.a>0 ? 3:2;
    }
    return result;
}
