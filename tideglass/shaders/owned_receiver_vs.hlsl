float4 cClip0 : register(c0);
float4 cClip1 : register(c1);
float4 cClip2 : register(c2);
float4 cClip3 : register(c3);
float4 cCamera : register(c4);
struct Output { float4 position : POSITION; float view : TEXCOORD0; };
Output main(float3 position : POSITION,float view : TEXCOORD0)
{
    float4 relative=float4(position-cCamera.xyz,1);
    Output output;
    output.position=float4(dot(relative,cClip0),dot(relative,cClip1),dot(relative,cClip2),dot(relative,cClip3));
    output.view=view;
    return output;
}
