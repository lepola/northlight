float4 cGrid : register(c0);
float4 cOrigin : register(c1);
// x: mover count; y: one-shot impulses on this pass; z: transport/impulses only,
// with no elapsed simulation tick (camera movement between ticks).
float4 cMoverCount : register(c2);
float4 cShore : register(c3);
float4 cMover[16] : register(c4);
float4 cMoverRadius[16] : register(c20);
float4 cViscosity : register(c36);

sampler2D sState : register(s0);
sampler2D sWaterLevel : register(s1);
sampler2D sSeabed : register(s2);

static const int kMaxMovers = 16;
static const float kAboveSurface = 0.6;
static const float kBelowSurface = 3.0;
static const float kDryDepth = 0.02;
static const float kShoreReach = 5.0;
static const float kShoreStart = 1.0;
// How much of the foam where something stands in the water (b; spread from where it crosses the surface each frame,
// see contact_spread_ps) fades each step, so it lingers a moment behind something moving.
static const float kFoamFade = 0.04;

float TexelSize() { return cGrid.x; }
float Courant() { return cGrid.y; }
float Damping() { return cGrid.z; }
float CellSize() { return cGrid.w; }
float GridSize() { return 1.0 / cGrid.x; }

// r: the surface's height; g: its height the step before; b: foam where something stands in the water; a: where it
// was marked last (see contact_spread_ps; for the debug view).
float4 StateAt(float2 texel)
{
    if (any(texel < 0.0) || any(texel >= GridSize()))
        return 0;
    return tex2Dlod(sState, float4((texel + 0.5) * TexelSize(), 0, 0));
}

bool TouchesWater(float moverHeight, float4 level)
{
    return level.g < 0.5 || (moverHeight > level.r - kBelowSurface && moverHeight < level.r + kAboveSurface);
}

float ShoreForcing(float depth, float2 world)
{
    float wander = sin(world.x * 0.13 + sin(world.y * 0.07) * 2.0) * 1.7 + sin(world.y * 0.11 - world.x * 0.05) * 1.3;
    float phase = depth * cShore.z + cShore.x * cShore.w + wander;
    float envelope = exp(-depth / kShoreReach) * saturate(depth / kShoreStart);
    return cShore.y * sin(phase) * envelope;
}

float4 main(float2 pixel : VPOS) : COLOR0
{
    float4 level = tex2Dlod(sWaterLevel, float4(0.5, 0.5, 0, 0));
    float4 heights = tex2Dlod(sSeabed, float4((pixel + 0.5) * TexelSize(), 0, 0));
    float surface = level.g > 0.5 ? lerp(level.r, heights.b, saturate(heights.a)) : heights.b;
    bool depthKnown = heights.g > 0.5 && (heights.a > 0.99 || level.g > 0.5);
    float depth = surface - heights.r;
    if (depthKnown && depth <= kDryDepth)
        return 0;

    float2 source = pixel + cOrigin.zw;
    float4 centre = StateAt(source);
    float4 neighbours = StateAt(source + float2(-1, 0)) + StateAt(source + float2(1, 0)) +
                        StateAt(source + float2(0, -1)) + StateAt(source + float2(0, 1));
    float laplacian = neighbours.r - 4.0 * centre.r;
    // (saturate also turns a value that is not a number to 0.)
    bool advances = cMoverCount.z < 0.5;
    float foam = saturate(centre.b * (advances ? 1.0 - kFoamFade : 1.0));
    // Viscosity (slime) smooths the surface a little each step, which kills the small ripples first.
    float height = advances ? (2.0 * centre.r - centre.g + Courant() * laplacian) * Damping() + cViscosity.x * laplacian
                            : centre.r;

    float2 world = cOrigin.xy + (pixel + 0.5) * CellSize();
    if (advances && depthKnown)
        height += ShoreForcing(depth, world);
    for (int i = 0; i < kMaxMovers; ++i)
    {
        bool continuous = cMoverRadius[i].y > 0.5;
        bool injectsThisStep = continuous ? advances : cMoverCount.y > 0.5;
        if (i < cMoverCount.x && injectsThisStep && TouchesWater(cMover[i].z, level))
        {
            float2 offset = world - cMover[i].xy;
            float radius = cMoverRadius[i].x;
            float distance2 = dot(offset, offset) / (radius * radius);
            height -= cMover[i].w * (exp(-distance2) - 0.25 * exp(-distance2 * 0.25));
        }
    }
    return float4(height, advances ? centre.r : centre.g, foam, centre.a);
}
