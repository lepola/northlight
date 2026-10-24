// Shared by native diffuse lookups and the normal/occlusion replay.
struct MaterialFrame {float3 p,tangent,bitangent,normal;float2 tx,ty;};
MaterialFrame materialFrame(float2 uv,float4 clipPosition,float4 projection) {
    MaterialFrame f;
    float3 p=float3(clipPosition.xy/projection.xy,clipPosition.w*projection.z);
    float3 dx=ddx(p),dy=ddy(p);float2 tx=ddx(uv),ty=ddy(uv);
    float determinant=tx.x*ty.y-tx.y*ty.x;
    float handedness=determinant<0?-1:1;
    float3 tangent=(dx*ty.y-dy*tx.y)*handedness,bitangent=(dy*tx.x-dx*ty.x)*handedness;
    tangent*=rsqrt(max(dot(tangent,tangent),1e-12));bitangent*=rsqrt(max(dot(bitangent,bitangent),1e-12));
    float3 normal=cross(dx,dy);normal*=rsqrt(max(dot(normal,normal),1e-12));normal=dot(normal,-p)<0?-normal:normal;
    f.p=p;f.tangent=tangent;f.bitangent=bitangent;f.normal=normal;f.tx=tx;f.ty=ty;return f;
}
float parallaxFade(float distance,float2 tx,float2 ty,float range){return saturate((range-distance)/max(range*.4,1))*saturate(2-sqrt(max(dot(tx,tx),dot(ty,ty)))*128);}
float2 parallaxUVFrame(sampler2D heightMap,float2 uv,float distance,float4 control,MaterialFrame f) {
    float2 tx=f.tx,ty=f.ty;float3 p=f.p,tangent=f.tangent,bitangent=f.bitangent,normal=f.normal;
    float fade=parallaxFade(distance,tx,ty,control.y);
    if(control.x<=0||fade<=0)return uv;
    float3 view=float3(dot(-p,tangent),dot(-p,bitangent),abs(dot(-p,normal)));
    float steps=clamp(control.z,8,32),stepDepth=1/steps;
    float2 rayUV=clamp(view.xy/max(view.z,.15*length(p)),-4,4)*control.x*fade;
    float2 stepUV=rayUV*stepDepth;
    // The authored surface is height .5, rather than the top of the volume.
    // March from above it: values above/below .5 protrude/recede respectively.
    float depth=0;float2 sampleUV=uv+rayUV*.5;
    float sampled=1-tex2Dgrad(heightMap,sampleUV,tx,ty).b;
    // Reuse only identical coordinates; subtraction/addition may round.
    float previous=sampled;float2 priorUV=sampleUV;
    [loop]for(int i=0;i<32&&i<steps&&depth<sampled;++i){previous=sampled;priorUV=sampleUV;sampleUV-=stepUV;depth+=stepDepth;sampled=1-tex2Dgrad(heightMap,sampleUV,tx,ty).b;}
    [branch]if(any(priorUV!=sampleUV+stepUV))previous=1-tex2Dgrad(heightMap,sampleUV+stepUV,tx,ty).b;
    previous-=depth-stepDepth;
    float after=sampled-depth;
    sampleUV+=stepUV*saturate(after/min(after-previous,-.00001));
    return sampleUV;
}
float2 parallaxUV(sampler2D heightMap,float2 uv,float4 clipPosition,float4 projection,float4 control) {
    return parallaxUVFrame(heightMap,uv,clipPosition.w,control,materialFrame(uv,clipPosition,projection));
}
float parallaxShadow(sampler2D heightMap,float2 uv,float height,float distance,MaterialFrame f,float4 control,float4 light,float steps) {
    float3 ray=float3(dot(light.xyz,f.tangent),dot(light.xyz,f.bitangent),dot(light.xyz,f.normal));
    float fade=parallaxFade(distance,f.tx,f.ty,control.y);
    if(control.x<=0||light.w<=0||ray.z<=.04||fade<=0)return 1;
    float span=1-height;
    float2 extent=clamp(ray.xy/max(ray.z,.08),-6,6)*control.x*span*fade;
    float blocked=0;
    [loop]for(int i=1;i<=16&&i<=steps&&blocked<1;++i){
        float part=i/steps;
        float h=tex2Dgrad(heightMap,uv+extent*part,f.tx,f.ty).b;
        blocked=max(blocked,saturate((h-height-span*part-.012)*16));
    }
    return 1-blocked;
}
