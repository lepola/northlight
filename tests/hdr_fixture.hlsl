sampler2D FixtureTexture : register(s0);
row_major float4x4 FixtureMatrix : register(c0);
struct Input {float4 position:POSITION0;float4 color:COLOR0;float2 uv:TEXCOORD0;};
struct Output {float4 position:POSITION0;float4 color:COLOR0;float2 uv:TEXCOORD0;};
Output HdrFixtureVS(Input input){Output output;output.position=mul(input.position,FixtureMatrix);output.color=input.color;output.uv=input.uv;return output;}
float4 HdrFixturePS(float4 color:COLOR0,float2 uv:TEXCOORD0):COLOR0{return tex2D(FixtureTexture,uv)*color;}
