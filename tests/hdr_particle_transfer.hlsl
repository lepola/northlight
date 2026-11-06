// Controlled geometry for native particle pixel shaders, including SM3 fog.
float4 SpriteViewport : register(c0);
float4 SpriteFog : register(c1);
struct SpriteOutput {float4 position:POSITION0;float4 color:COLOR0;float2 uv:TEXCOORD0;float fog:FOG0;};
SpriteOutput ParticleFixtureVS(float4 position:POSITION0,float4 color:COLOR0,float2 uv:TEXCOORD0){
    SpriteOutput o;
    o.position=float4((position.xy+.5)*SpriteViewport.xy+float2(-1,1),position.z,1);
    o.color=color;o.uv=uv;o.fog=SpriteFog.x;return o;
}
