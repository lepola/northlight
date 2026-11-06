float4 Viewport : register(c0);
float4 Tint : register(c1);
float4 Fog : register(c2);
struct Foreground {
    float4 position : POSITION0;
    float4 color : COLOR0;
    float4 specular : COLOR1;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float3 direction : TEXCOORD3;
    float3 projected : TEXCOORD4;
    float fog : FOG0;
};
Foreground ForegroundVS(float4 position : POSITION0,float2 uv : TEXCOORD0){
    Foreground o;
    o.position=float4((position.xy+.5)*Viewport.xy+float2(-1,1),position.z,1);
    o.color=Tint;o.specular=0;o.uv=uv;o.uv1=uv;
    o.normal=float3(.2,.3,.8);o.direction=float3(.1,.2,.3);o.projected=float3(.5,.5,.5);
    o.fog=Fog.x;return o;
}
