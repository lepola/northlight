// 0.3.202 (rain mask MRT): ps_2_0, the two rain-mask passes (renderer.cpp). Their own source: effects.hlsl holds ps_3_0-only code
// ([branch], loop-sized arrays) that a ps_2_0 compile of the whole file rejects.
sampler2D Scene : register(s0);
sampler2D Depth : register(s1);

// RainMaskMRT replaces the game's fixed-function stage 0 (MODULATE texture x diffuse, colour and alpha) for a rain draw and writes the streak
// alpha to render target 1 as well (alpha only; the mask target blends with MAX).
float4 RainMaskMRT(float4 diffuse : COLOR0, float2 uv : TEXCOORD0, out float4 mask : COLOR1) : COLOR0
{
    float4 c = tex2D(Scene, uv) * diffuse;
    mask = float4(0, 0, 0, c.a);
    return c;
}

// RainScrub: Scene = the final scene depth, Depth = the depth snapshot taken before the frame's first rain draw. Pixels whose depth did not
// change are discarded (the mask stays); a pixel something was drawn over since gets alpha 0 (the pass writes alpha only).
float4 RainScrub(float2 uv : TEXCOORD0) : COLOR0
{
    clip(abs(tex2D(Scene, uv).r - tex2D(Depth, uv).r) - 1e-6);
    return 0;
}
