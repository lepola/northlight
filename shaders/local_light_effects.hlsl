// One authored static point source. Cube shadow is world geometry, including
// offscreen casters. Receiver normal is depth-derived. Original baked direct
// light decomposition is unavailable: subtraction is an explicitly bounded
// estimate, never an extra duplicate light contribution.
sampler2D LocalDepth : register(s0);
samplerCUBE LocalCube : register(s1);
sampler2D LocalWater : register(s2);
// LocalLighting PS constants:
float4 LocalImage : register(c0); // inverse full width/height, near,far
float4 LocalProjection : register(c1); // signed proj X,Y,Zsign, depth viewport MinZ
float4 LocalDepthInfo : register(c2); // inverse viewport depth range, water mask ready, correction strength, world-unit bias
row_major float4x4 LocalInverseView : register(c3);
float4 LocalPosition : register(c7); // xyz source position,w attenuation start
float4 LocalColor : register(c8); // rgb source color*intensity,w attenuation end
float4 LocalShadow : register(c9); // cube near,far,inverse resolution,PCF angular radius in texels
// Shadow VS constants c0..c3 are separately bound, row-major clip matrix.
row_major float4x4 LocalFaceMatrix : register(c0);
// Shadow PS uses c0: xyz unused,w alpha cutoff (<0 disables alpha test).
float4 LocalMaterial : register(c0);
// Shadow PS c1 (0.3.151): x squared attenuation end, y the face matrix's half-texel skew.
// Nothing beyond the light's range is stored: the correction there is exactly zero
// for every receiver, and a cached static face then depends only on in-range casters.
float4 LocalSphere : register(c1);
sampler2D LocalAlpha : register(s0);
struct LocalCaster {float4 position:POSITION0;float2 uv:TEXCOORD0;float4 clip:TEXCOORD7;};
LocalCaster LocalShadowVS(float3 position:POSITION0,float2 uv:TEXCOORD0) {
    LocalCaster o;
    o.position=position.x*LocalFaceMatrix[0]+position.y*LocalFaceMatrix[1]+position.z*LocalFaceMatrix[2]+LocalFaceMatrix[3];
    o.clip=o.position;o.uv=uv;return o;
}
float4 LocalShadowPS(float2 uv:TEXCOORD0,float4 clipPosition:TEXCOORD7,float4 diffuse:COLOR0):COLOR0 {
    // Light-space offset from the face clip position (faceMatrix: right=x+h*w, up=y-h*w, forward=w).
    float3 offset=float3(clipPosition.x+LocalSphere.y*clipPosition.w,clipPosition.y-LocalSphere.y*clipPosition.w,clipPosition.w);
    clip(LocalSphere.x-dot(offset,offset));
    [branch]if(LocalMaterial.w>=0){
        float alpha=LocalMaterial.x==2?diffuse.a:tex2D(LocalAlpha,uv).a;
        if(LocalMaterial.x==1)alpha*=diffuse.a;
        clip(alpha-LocalMaterial.w);
    }
    return saturate(clipPosition.z/clipPosition.w);
}
float2 localDepthUV(float2 uv) {
    float2 dims=floor(1/LocalImage.xy+.5);
    return (clamp(floor(uv*dims),0,dims-1)+.5)*LocalImage.xy;
}
float localDepth(float2 uv){return saturate((tex2Dlod(LocalDepth,float4(uv,0,0)).r-LocalProjection.w)*LocalDepthInfo.x);}
float localDistance(float d){return LocalImage.z*LocalImage.w/max(LocalImage.w-d*(LocalImage.w-LocalImage.z),.00001);}
float3 localView(float2 uv,float d) {
    float2 xy=((uv-.5*LocalImage.xy)*float2(2,-2)+float2(-1,1))/LocalProjection.xy;
    return float3(xy,LocalProjection.z)*localDistance(d);
}
float3 localWorld(float3 v){return v.x*LocalInverseView[0].xyz+v.y*LocalInverseView[1].xyz+v.z*LocalInverseView[2].xyz+LocalInverseView[3].xyz;}
float3 localNormal(float2 uv,float3 p) {
    float2 lo=.5*LocalImage.xy,hi=1-lo;
    float2 l=max(uv-float2(LocalImage.x,0),lo),r=min(uv+float2(LocalImage.x,0),hi);
    float2 u=max(uv-float2(0,LocalImage.y),lo),b=min(uv+float2(0,LocalImage.y),hi);
    float3 pl=localView(l,localDepth(l)),pr=localView(r,localDepth(r));
    float3 pu=localView(u,localDepth(u)),pb=localView(b,localDepth(b));
    float3 tx=(uv.x<=lo.x||dot(pr-p,pr-p)<dot(p-pl,p-pl))&&uv.x<hi.x?pr-p:p-pl;
    float3 ty=(uv.y<=lo.y||dot(pb-p,pb-p)<dot(p-pu,p-pu))&&uv.y<hi.y?pb-p:p-pu;
    float3 n=cross(tx,ty);n*=rsqrt(max(dot(n,n),1e-12));n=dot(n,-p)<0?-n:n;
    return n.x*LocalInverseView[0].xyz+n.y*LocalInverseView[1].xyz+n.z*LocalInverseView[2].xyz;
}
float localVisibility(float3 delta,float3 normal) {
    float distance=length(delta);
    // Task 43: dot(normal,delta)>=0 is a face turned from the lamp (facing 0, estimate 0): the correction is 0 whatever
    // the visibility, so the 4-tap cube PCF is skipped.
    if(distance<=LocalShadow.x||distance>=LocalShadow.y||dot(normal,delta)>=0)return 1;
    float3 direction=delta/max(distance,.00001);
    // World-unit normal bias remains independent of cube near/far precision.
    float grazing=1-abs(dot(normal,direction));
    float3 biased=delta+normal*LocalDepthInfo.w*(1+grazing*2);
    float3 axis=abs(direction.z)<.9?float3(0,0,1):float3(0,1,0);
    float3 tangent=normalize(cross(direction,axis)),bitangent=cross(direction,tangent);
    float angular=2*LocalShadow.z*LocalShadow.w;
    float visibility=0;
    [unroll]for(int i=0;i<4;++i) {
        float2 offset=float2((i==0||i==2)?-.5:.5,(i<2)?-.5:.5);
        float3 ray=biased+distance*angular*(tangent*offset.x+bitangent*offset.y);
        // Receiver plane intersection for each perturbed ray prevents PCF
        // self-shadow on sloping surfaces; avoid unbounded grazing corrections.
        float denom=dot(normal,ray);float numerator=dot(normal,biased);
        float scale=abs(denom)>.1*distance?clamp(numerator/denom,.8,1.2):1;
        float major=max(abs(ray.x),max(abs(ray.y),abs(ray.z)))*scale;
        float receiver=LocalShadow.y/(LocalShadow.y-LocalShadow.x)*(1-LocalShadow.x/max(major,LocalShadow.x));
        float stored=texCUBElod(LocalCube,float4(ray,0)).r;
        visibility+=receiver<=stored+.000002?1:0;
    }
    return visibility*.25;
}
float4 LocalLighting(float2 uv:TEXCOORD0):COLOR0 {
    uv=localDepthUV(uv);float depth=localDepth(uv);if(depth>=.999999)return 0;
    if(LocalDepthInfo.y>.5){float2 w=tex2Dlod(LocalWater,float4(uv,0,0)).rg;
        if(w.x>LocalImage.z&&w.x<=localDistance(depth)+max(.012,w.x*.0007)&&w.y>.003)return 0;}
    float3 v=localView(uv,depth),p=localWorld(v),delta=p-LocalPosition.xyz;
    float distance=length(delta);if(distance>=LocalColor.w||distance<=.0001)return 0;
    float3 n=localNormal(uv,v);float facing=saturate(dot(n,-delta/distance));
    float atten=saturate((LocalColor.w-distance)/max(LocalColor.w-LocalPosition.w,.0001));
    float occlusion=1-localVisibility(delta,n);
    float3 estimate=min(max(LocalColor.rgb,0),2)*atten*facing;
    // Signed correction is irradiance/pi in the existing world lighting buffer.
    // The cap prevents a single uncalibrated legacy emitter erasing daylight.
    return float4(-min(estimate,.35)*occlusion*saturate(LocalDepthInfo.z),0);
}
