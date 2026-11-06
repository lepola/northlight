float4 Viewport : register(c0);
float4 Tint : register(c1);
float4 FogDistance : register(c2);
struct WaterVaryings {
    float4 position : POSITION0;
    float4 color : COLOR0;
    float4 specular : COLOR1;
    float2 uv : TEXCOORD0;
    float2 uv1 : TEXCOORD1;
    float4 distance : TEXCOORD7;
    float fog : FOG0;
};
WaterVaryings WaterVS(float4 position : POSITION0,float2 uv : TEXCOORD0){
    WaterVaryings o;
    o.position=float4((position.xy+.5)*Viewport.xy+float2(-1,1),position.z,1);
    o.color=Tint;o.specular=float4(.03,.07,.11,.2);o.uv=uv;o.uv1=uv;
    o.distance=float4(0,0,0,FogDistance.y);o.fog=FogDistance.x;return o;
}
sampler2D Source : register(s0);
float4 WaterPS(WaterVaryings i) : COLOR0 {
    float4 value=tex2D(Source,i.uv)*i.color;
    value.rgb=lerp(value.rgb,i.specular.rgb,i.fog);return value;
}
struct WaterOutputs {float4 color:COLOR0;float4 mask:COLOR1;};
WaterOutputs WaterMRTPS(WaterVaryings i){
    WaterOutputs o;o.color=WaterPS(i);
    float4 covered=float4(i.distance.w,1,1,1);
    o.mask=o.color.a>.003?covered:0;return o;
}
