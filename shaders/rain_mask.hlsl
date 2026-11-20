// 0.3.202 (rain mask MRT): ps_2_0, the two rain-mask passes (renderer.cpp). Their own source: effects.hlsl holds ps_3_0-only code
// ([branch], loop-sized arrays) that a ps_2_0 compile of the whole file rejects.
sampler2D Scene : register(s0);
sampler2D Depth : register(s1);
// x: the streaks' opacity (RainOpacity, 0..1), set with the shader.
float4 RainControls : register(c0);

// RainMaskMRT replaces the game's fixed-function stage 0 (MODULATE texture x diffuse, colour and alpha) for a rain draw and writes white with
// the streak alpha to render target 1 as well: RT1 is colour-masked to red, so the draw's own SRCALPHA/INVSRCALPHA blend lays the streaks'
// coverage over each other in red (RT0's colour and alpha blend as without the mask).
float4 RainMaskMRT(float4 diffuse : COLOR0, float2 uv : TEXCOORD0, out float4 mask : COLOR1) : COLOR0
{
    float4 c = tex2D(Scene, uv) * diffuse;
    c.a *= RainControls.x;
    mask = float4(1, 1, 1, c.a);
    return c;
}

// RainScrub: Scene = the final scene depth, Depth = the depth snapshot taken before the frame's first rain draw. Pixels whose depth did not
// change are discarded (the mask stays); a pixel something was drawn over since gets 0 (the pass writes red only).
float4 RainScrub(float2 uv : TEXCOORD0) : COLOR0
{
    clip(abs(tex2D(Scene, uv).r - tex2D(Depth, uv).r) - 1e-6);
    return 0;
}

// Optional cooperating-water capture: native colour/alpha unchanged. RT1.R is A;
// RT2.RGB accumulates premultiplied precipitation C under the SAME alpha blend.
float4 RainColourMRT(float4 diffuse:COLOR0,float2 uv:TEXCOORD0,
                    out float4 mask:COLOR1,out float4 rain:COLOR2):COLOR0 {
    float4 c=tex2D(Scene,uv)*diffuse;
    c.a*=RainControls.x;
    mask=float4(1,1,1,c.a);rain=c;return c;
}

// Restore this foreground layer before any host world/celestial effects.
sampler2D ConvertedWaterDepth:register(s2);
sampler2D OpaqueDepth:register(s3);
float4 RainColourRestore(float2 uv:TEXCOORD0):COLOR0 {
    float a=tex2D(Depth,uv).r;
    // Reuse the core's actual converted depth: includes near/far/.003 coverage rejection.
    // Equal to opaque means this pixel was not replaced by the surface pass.
    float surface=tex2D(ConvertedWaterDepth,uv).r;
    float opaque=tex2D(OpaqueDepth,uv).r;
    return a>0&&surface<opaque?float4(tex2D(Scene,uv).rgb,a):0;
}
