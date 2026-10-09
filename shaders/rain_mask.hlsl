// 0.3.202 (rain mask MRT): ps_2_0, the two rain-mask passes (renderer.cpp). Their own source: effects.hlsl holds ps_3_0-only code
// ([branch], loop-sized arrays) that a ps_2_0 compile of the whole file rejects.
sampler2D Scene : register(s0);
sampler2D Depth : register(s1);

// RainMaskMRT replaces the game's fixed-function stage 0 (MODULATE texture x diffuse, colour and alpha) for a rain draw and writes white with
// the streak alpha to render target 1 as well: RT1 is colour-masked to red, so the draw's own SRCALPHA/INVSRCALPHA blend lays the streaks'
// coverage over each other in red (RT0's colour and alpha blend as without the mask).
float4 RainMaskMRT(float4 diffuse : COLOR0, float2 uv : TEXCOORD0, out float4 mask : COLOR1) : COLOR0
{
    float4 c = tex2D(Scene, uv) * diffuse;
    mask = float4(1, 1, 1, c.a);
    return c;
}

// RainScrub: Scene = the final scene depth, Depth = the depth snapshot taken before the frame's first rain draw. Pixels whose depth did not
// change are discarded (the mask stays); a pixel something was drawn over since gets (0,0,0,128/255): no coverage, no touch, and the multiplicative
// factor of the mod2x halos (alpha, 128/255 = x1, see ParticleMod) back to its start.
float4 RainScrub(float2 uv : TEXCOORD0) : COLOR0
{
    clip(abs(tex2D(Scene, uv).r - tex2D(Depth, uv).r) - 1e-6);
    return float4(0, 0, 0, 128.0 / 255.0);
}

// 0.3.203 (particle mask): translucent world particles (torch and brazier flames, sparks, spell particles) draw no depth, so WorldComposite
// would fog, haze and relight them with the background's depth. Each shader replaces the game's fixed-function stage 0 (colour MODULATE
// or MODULATE2X of texture x diffuse, alpha MODULATE, clamped like the stage) and writes the particle's coverage to oC1 (render target 1; the red
// rain coverage is untouched). The draw's own blend applies to oC1 too, so the value written is what that blend must add up to. Blue = "a particle
// touched this pixel", green = 1 - T, the background's transmittance through the alpha-over particles (pixel = T x background + emission):
//   Over  (SRCALPHA/INVSRCALPHA)         (1,1,1,a):  g' = a + g(1-a) in green AND blue (colour mask green|blue);
//   AddA  (SRCALPHA/ONE)                 (1,1,1,v):  blue only: b' = b + v, v = a x the brightest channel, saturating at 1; green untouched (no attenuation);
//   AddC  (ONE/ONE and SRCCOLOR/ONE)     (v,v,v,v):  blue only: ONE adds v, SRCCOLOR adds v x v, v = the brightest channel (the colour the blend adds).
// The multiplier (1 or 2) is the stage's colour op; the 1 / 2 suffix names it.
float4 particleStage(float4 diffuse, float2 uv, float scale)
{
    float4 c = tex2D(Scene, uv) * diffuse;
    c.rgb *= scale;
    return saturate(c); // the fixed-function stage clamps its result to [0,1] before fog and blend: RT0 gets exactly that
}
float particleBrightness(float4 c)
{
    return saturate(max(c.r, max(c.g, c.b)));
}
#define PARTICLE_OVER(name, scale) float4 name(float4 diffuse : COLOR0, float2 uv : TEXCOORD0, out float4 mask : COLOR1) : COLOR0 \
    { float4 c = particleStage(diffuse, uv, scale); mask = float4(1, 1, 1, saturate(c.a)); return c; }
#define PARTICLE_ADDA(name, scale) float4 name(float4 diffuse : COLOR0, float2 uv : TEXCOORD0, out float4 mask : COLOR1) : COLOR0 \
    { float4 c = particleStage(diffuse, uv, scale); mask = float4(1, 1, 1, saturate(c.a) * particleBrightness(c)); return c; }
#define PARTICLE_ADDC(name, scale) float4 name(float4 diffuse : COLOR0, float2 uv : TEXCOORD0, out float4 mask : COLOR1) : COLOR0 \
    { float4 c = particleStage(diffuse, uv, scale); mask = particleBrightness(c).xxxx; return c; }
// Mod  (DESTCOLOR/SRCCOLOR, the M2 "mod2x" blend: pixel = 2 x src x dst)   (.,.,.,y):  alpha only (colour mask ALPHA). The mask's alpha starts at 128/255 (factor M = 1; the composite reads M = alpha x 255/128,
//   at most ~2) and the draw's own blend turns it into 2 x y x alpha, so y = the luminance of the colour the blend multiplies by (src.rgb x 2 / 2) keeps alpha = M/2 across stacked halos.
#define PARTICLE_MOD(name, scale) float4 name(float4 diffuse : COLOR0, float2 uv : TEXCOORD0, out float4 mask : COLOR1) : COLOR0 \
    { float4 c = particleStage(diffuse, uv, scale); mask = dot(c.rgb, float3(0.299, 0.587, 0.114)).xxxx; return c; }
PARTICLE_OVER(ParticleOver1, 1)
PARTICLE_OVER(ParticleOver2, 2)
PARTICLE_ADDA(ParticleAddA1, 1)
PARTICLE_ADDA(ParticleAddA2, 2)
PARTICLE_ADDC(ParticleAddC1, 1)
PARTICLE_ADDC(ParticleAddC2, 2)
PARTICLE_MOD(ParticleMod1, 1)
PARTICLE_MOD(ParticleMod2, 2)
