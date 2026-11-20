float4 cClip0 : register(c0);
float4 cClip1 : register(c1);
float4 cClip2 : register(c2);
float4 cClip3 : register(c3);
float4 cCamera : register(c4);
float4 cPlane : register(c5);

struct Output
{
    float4 position : POSITION;
    float4 colour : COLOR0;
    float above : TEXCOORD0;
    float distance : TEXCOORD1;
};

Output main(float3 world : POSITION, float4 colour : COLOR0)
{
    float4 relative = float4(world - cCamera.xyz, 1);
    Output output;
    output.position = float4(dot(relative, cClip0), dot(relative, cClip1),
                             dot(relative, cClip2), dot(relative, cClip3));
    output.colour = colour;
    output.above = dot(world, cPlane.xyz) - cPlane.w;
    // The mirror is already in cClip: match native axial view-depth fog.
    output.distance = output.position.w;
    return output;
}
