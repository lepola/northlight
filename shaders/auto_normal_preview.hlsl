sampler2D Diffuse:register(s0);
float4 MacroLight:register(c78);
struct Output {float4 color:COLOR0;float4 depth:COLOR1;};
Output PreviewBase(float2 uv:TEXCOORD0,float4 p:TEXCOORD7){
    Output o;o.color=float4(tex2D(Diffuse,uv).rgb*MacroLight.rgb,1);o.depth=(p.z/p.w).xxxx;return o;
}
sampler2D NormalHeight:register(s2);
float4 PreviewLight:register(c79); // view-space direction, normal strength
float4 PreviewDirect:register(c80);
float4 PreviewAmbient:register(c81);
float4 PreviewProjection:register(c82); // signed projection, height scale in UV units
float4 PreviewPOM(float2 uv:TEXCOORD0,float4 clipPosition:TEXCOORD7):COLOR0 {
    float3 p=float3(clipPosition.xy/PreviewProjection.xy,clipPosition.w*PreviewProjection.z);
    float3 dx=ddx(p),dy=ddy(p);float2 tx=ddx(uv),ty=ddy(uv);
    float signUV=tx.x*ty.y-tx.y*ty.x<0?-1:1;
    float3 tangent=normalize((dx*ty.y-dy*tx.y)*signUV),bitangent=normalize((dy*tx.x-dx*ty.x)*signUV);
    float3 normal=normalize(cross(dx,dy));normal=dot(normal,-p)<0?-normal:normal;
    float3 view=float3(dot(-p,tangent),dot(-p,bitangent),dot(-p,normal));
    float fade=saturate((14-clipPosition.w)/6);
    float2 stepUV=view.xy/max(abs(view.z),.15)*PreviewProjection.w*fade/24;
    float depth=0;float2 sampleUV=uv;float sampled=1-tex2Dgrad(NormalHeight,sampleUV,tx,ty).b;
    [loop]for(int i=0;i<24&&depth<sampled;++i){sampleUV-=stepUV;depth+=1.0/24;sampled=1-tex2Dgrad(NormalHeight,sampleUV,tx,ty).b;}
    float previous=1-tex2Dgrad(NormalHeight,sampleUV+stepUV,tx,ty).b-(depth-1.0/24);
    float after=sampled-depth;
    sampleUV+=stepUV*saturate(after/min(after-previous,-.00001));
    float2 localXY=(tex2Dgrad(NormalHeight,sampleUV,tx,ty).rg*255-128)/127;
    float3 local=float3(localXY,sqrt(saturate(1-dot(localXY,localXY))));
    float3 mapped=normalize(tangent*local.x+bitangent*local.y+normal*local.z);
    mapped=normalize(lerp(normal,mapped,PreviewLight.w));
    return float4(tex2Dgrad(Diffuse,sampleUV,tx,ty).rgb*(PreviewAmbient.rgb+PreviewDirect.rgb*saturate(dot(mapped,PreviewLight.xyz))),1);
}
