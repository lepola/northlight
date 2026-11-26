float4 cClip0 : register(c0);
float4 cClip1 : register(c1);
float4 cClip2 : register(c2);
float4 cClip3 : register(c3);
float4 cCamera : register(c4);
float4 cWorld0 : register(c6);
float4 cWorld1 : register(c7);
float4 cWorld2 : register(c8);
float4 cWorld3 : register(c9);
float4 cDraw : register(c10); // model flag, rigid receiver height
float4 cToLight : register(c11);
float4 cReceiver : register(c13); // x: flat view override, y: height
float4 cTerrainSpecular : register(c12); // current native sun colour and terrain exponent
struct Output
{
    float4 position : POSITION;
    float2 uv : TEXCOORD0;
    float above : TEXCOORD1;
    float distance : TEXCOORD2;
    float diffuse : TEXCOORD3;
    float receiverHeight : TEXCOORD4;
    float3 specular : TEXCOORD5;
    float2 alpha : TEXCOORD6; // terrain: the cell's place in its tile's alpha atlas
    float3 painted : TEXCOORD7; // terrain paint or authored interior model lighting
};
// One draw at a time takes its placement and draw mode from c6-c10. OWNED_WORLD_INSTANCED (models, many copies in one
// draw) takes them from the instance stream instead; OWNED_WORLD_TERRAIN adds each vertex's alpha atlas coordinate
// (stream 1), so cells sharing layer textures draw together. Everything else is the same.
Output Shade(float3 position,float3 normal,float2 uv,float water,float4 world0,float4 world1,float4 world2,float4 draw,float2 alpha,float3 painted)
{
    float4 local=float4(position,1);
    float3 world=float3(dot(local,world0),dot(local,world1),dot(local,world2));
    float3 direction=float3(dot(normal,world0.xyz),dot(normal,world1.xyz),dot(normal,world2.xyz));
    float height=lerp(water,draw.y,draw.x);
    height=lerp(height,cReceiver.y,cReceiver.x);
    height=lerp(height,draw.w,draw.z);
    Output output;
    output.above=(world.z-height)*(1-2*draw.z);
    output.receiverHeight=height;
    // The reflection mirrors the terrain as the player sees it: the native lobe is
    // evaluated toward the physical camera. Toward the mirrored camera, camera-facing
    // cliffs caught a highlight the direct view never shows (2026-11-02 cliff report).
    // Terrain texture alpha is a specular mask, not opacity. Models keep their
    // own material policy and do not inherit this terrain lobe.
    float3 eye=cCamera.xyz-world;
    eye*=rsqrt(max(dot(eye,eye),1e-8));
    float3 halfway=eye+cToLight.xyz;
    halfway*=rsqrt(max(dot(halfway,halfway),1e-8));
    output.specular=cTerrainSpecular.rgb*(1-draw.x)*
        pow(saturate(dot(normalize(direction),halfway)),cTerrainSpecular.w);
    world.z=2*height-world.z;
    float4 relative=float4(world-cCamera.xyz,1);
    output.position=float4(dot(relative,cClip0),dot(relative,cClip1),dot(relative,cClip2),dot(relative,cClip3));
    output.uv=uv;
    output.alpha=alpha;
    output.painted=painted;
    // Native material fog is interpolated axial view depth, not radial distance.
    // Clip W is positive forward depth for the validated client projection.
    output.distance=output.position.w;
    output.diffuse=max(dot(normalize(direction),cToLight.xyz),0);
    return output;
}
#ifdef OWNED_WORLD_INSTANCED
Output main(float3 position : POSITION,float3 normal : NORMAL,float2 uv : TEXCOORD0,float4 modelLight : TEXCOORD1,
            float4 world0 : TEXCOORD2,float4 world1 : TEXCOORD3,float4 world2 : TEXCOORD4,float4 draw : TEXCOORD5)
{
    return Shade(position,normal,uv,0,world0,world1,world2,draw,0,modelLight.rgb);
}
#elif defined(OWNED_WORLD_TERRAIN)
Output main(float3 position : POSITION,float3 normal : NORMAL,float2 uv : TEXCOORD0,float water : TEXCOORD1,float2 alpha : TEXCOORD2,
            float4 colour : COLOR0)
{
    // The game doubles it, so that half is unpainted. (0x80 of 255 is held to be exactly half here.)
    return Shade(position,normal,uv,water,cWorld0,cWorld1,cWorld2,cDraw,alpha,colour.rgb*(255.0/128.0));
}
#else
Output main(float3 position : POSITION,float3 normal : NORMAL,float2 uv : TEXCOORD0,float4 modelLight : TEXCOORD1)
{
    return Shade(position,normal,uv,0,cWorld0,cWorld1,cWorld2,cDraw,0,modelLight.rgb);
}
#endif
