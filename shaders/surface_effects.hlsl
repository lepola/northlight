// Northlight-owned supplemental precipitation. Native water rendering is untouched.
sampler2D FinalDepth : register(s3);
float4 Clip : register(c0);       // inverse image size, near, far
float4 Proj : register(c1);       // scale x/y, forward sign, animation time
float4 DepthRange : register(c2); // viewport min, reciprocal extent
float4 Weather : register(c3);    // amount, snow, reserved
float4 Inv0 : register(c4);
float4 Inv1 : register(c5);
float4 Inv2 : register(c6);
float4 Inv3 : register(c7);
float4 Ambient : register(c8);
float4 Direct : register(c9);

float linearDepth(float d) {
    d=saturate((d-DepthRange.x)*DepthRange.y);
    return Clip.z*Clip.w/max(Clip.w-d*(Clip.w-Clip.z),.00001);
}
float3 position(float2 uv,float z) {
    float2 xy=((uv-.5*Clip.xy)*float2(2,-2)+float2(-1,1))/Proj.xy;
    return float3(xy,Proj.z)*z;
}
float3 toWorld(float3 v) {return v.x*Inv0.xyz+v.y*Inv1.xyz+v.z*Inv2.xyz+Inv3.xyz;}
float3 random3(float3 cell) {return frac(sin(float3(dot(cell,float3(127.1,311.7,74.7)),dot(cell,float3(269.5,183.3,246.1)),dot(cell,float3(113.5,271.9,124.6))))*43758.5453);}
float4 precipitation(float2 uv,int layers) {
    float scene=linearDepth(tex2D(FinalDepth,uv).r);float opacity=0;
    [unroll] for(int layer=0;layer<3;++layer) {
        if(layer>=layers)break;
        float distance=layer==0?3.5:layer==1?11:26;
        float weight=layer==0?.45:layer==1?.35:.20;
        float3 p=toWorld(position(uv,distance));
        float time=Proj.w,scale=Weather.y>.5?.8:1.2;
        p+=float3(time*.5,time*.2,time*(Weather.y>.5?1.3:19));p*=scale;
        float3 cell=floor(p),seed=random3(cell);float3 delta=frac(p)-seed;
        if(Weather.y>.5)delta.xy+=sin(time*.7+cell.z+seed.xy*6.28)*.08;
        float radius=Weather.y>.5?.055:.014;
        float shape=Weather.y>.5?saturate(1-length(delta)/radius):saturate(1-length(delta.xy)/radius)*saturate(1-abs(delta.z)/.32);
        shape*=smoothstep(distance-.5,distance+.5,scene)*step(seed.z,Weather.x);
        opacity+=shape*weight;
    }
    float alpha=saturate(opacity*.45*Weather.x);
    float3 color=saturate(float3(.45,.53,.62)+Ambient.rgb*.45+Direct.rgb*.15);
    if(Weather.y>.5)color=lerp(color,1,.55);
    return float4(color*alpha,alpha);
}
float4 PrecipitationQuality(float2 uv:TEXCOORD0):COLOR0{return precipitation(uv,3);}
float4 PrecipitationPerformance(float2 uv:TEXCOORD0):COLOR0{return precipitation(uv,2);}
