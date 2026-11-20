// Rain on the ground: the terrain darkens as it gets wet, and puddles gather in its hollows and flat places, reflecting
// the world and the sky, with rings where the drops land. Drawn over the solid world before the liquids. The ground's
// shape comes from the world data (the ground map), so only the terrain itself gets wet this way: rocks, roofs,
// bridges and models stand above it and are left alone.
#include "water_common.hlsli"

// x: how full the puddles are (0 dry .. 1 full, rising while it rains and falling after); y: how hard it rains now;
// z: strength; w: how strongly rain rings bend the surface (1 = normal)
float4 cPuddles : register(c42);
// The ground map: x, y of its corner, 1 / its size (yards), its cell size (yards)
float4 cPuddleGround : register(c43);

// A pixel within this height of the ground map is the terrain; above or below it, something else.
static const float kOnGroundNear = 0.15;
static const float kOnGroundFar = 0.4;
// Ground steeper than this rise per yard sheds its water.
static const float kFlatSlope = 0.06;
static const float kSteepSlope = 0.2;
// How far around (yards) a hollow is measured: small dips and wider basins.
static const float kNearRing = 1.2;
static const float kWideRing = 3.5;
// How much deeper than its surroundings (yards) counts as a full hollow.
static const float kHollowDepth = 0.1;
// The patches water gathers in (per yard), the wobble given to their outlines (yards, per yard) and the fine breakup
// of their edges (per yard).
static const float kPatchScale = 0.42;
static const float kWobble = 0.9;
static const float kWobbleScale = 0.7;
static const float kBreakupScale = 2.3;
static const float kBreakup = 0.12;
static const float kPuddleEdge = 0.03;
static const float kFadeStart = 35.0;
static const float kFadeEnd = 70.0;
static const float kGroundEdgeFade = 8.0;
// Wet ground is a touch darker, and the ground under a puddle a touch more; mostly a puddle shows as a reflection.
static const float kDampDarkening = 0.02;
static const float kUnderPuddleDarkening = 0.08;
// Clear looking straight down, a mirror at a glancing angle.
static const float kPuddleF0 = 0.03;
static const float kPuddleMirror = 0.9;
static const float kSunGlint = 900.0;
// How far (yards, per unit of slope) the rings bend the view of the ground under a puddle.
static const float kPuddleRefraction = 0.25;
// A faint stir on the puddles' surface, so their reflections are not glass-flat.
static const float kStirScale = 2.5;
static const float kStir = 0.015;

float Wetness() { return cPuddles.x; }
float RainNow() { return cPuddles.y; }

float2 GroundUv(float2 world)
{
    return (world - cPuddleGround.xy) * cPuddleGround.z;
}

// The ground height there, and 1 where it is known.
float2 GroundAt(float2 world)
{
    return tex2Dlod(sSeabed, float4(GroundUv(world), 0, 0)).rg;
}

float4 main(float2 pixel : VPOS) : COLOR0
{
    pixel = floor(pixel);
    float3 scene = tex2Dlod(sSceneBefore, float4((pixel + 0.5) * cTexelGrid.xy, 0, 0)).rgb;
    float rawDepth = SampleRawDepth(sDepth, pixel);
    if (IsTrueSky(rawDepth) || BeyondFarClip(rawDepth))
        return float4(scene, 1);
    float3 view = ViewPosition(pixel, ViewZ(rawDepth));
    float3 world = WorldPosition(view);
    float distance = length(view);
    float2 uv = GroundUv(world.xy);
    float edge = saturate(min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y)) * kGroundEdgeFade);
    float reach = edge * (1.0 - smoothstep(kFadeStart, kFadeEnd, distance));
    if (reach <= 0.0)
        return float4(scene, 1);
    float4 ground = tex2Dlod(sSeabed, float4(uv, 0, 0));
    float onGround = ground.g * (1.0 - smoothstep(kOnGroundNear, kOnGroundFar, abs(world.z - ground.r)));
    // Under a lake or river the ground is not rained on.
    bool underLiquid = ground.a > 0.0 && ground.b > ground.r;
    if (onGround <= 0.0 || underLiquid)
        return float4(scene, 1);

    float cell = cPuddleGround.w;
    float2 east = GroundAt(world.xy + float2(cell, 0));
    float2 west = GroundAt(world.xy - float2(cell, 0));
    float2 north = GroundAt(world.xy + float2(0, cell));
    float2 south = GroundAt(world.xy - float2(0, cell));
    float slope = length(float2(east.x - west.x, north.x - south.x)) / (2.0 * cell);
    float known = min(min(east.y, west.y), min(north.y, south.y));
    float nearRing = 0.0;
    float wideRing = 0.0;
    [unroll] for (int i = 0; i < 8; ++i)
    {
        float angle = i * 0.7853982;
        float2 around = float2(cos(angle), sin(angle));
        float2 nearSample = GroundAt(world.xy + around * kNearRing);
        float2 wideSample = GroundAt(world.xy + around * kWideRing);
        nearRing += nearSample.x;
        wideRing += wideSample.x;
        known = min(known, min(nearSample.y, wideSample.y));
    }
    float hollow = (0.6 * (nearRing / 8.0 - ground.r) + 0.4 * (wideRing / 8.0 - ground.r)) / kHollowDepth;
    float flat = 1.0 - smoothstep(kFlatSlope, kSteepSlope, slope);
    float2 wobbled = world.xy + (float2(ValueNoise(world.xy * kWobbleScale), ValueNoise(world.xy * kWobbleScale + 31.0))
                                 - 0.5) * 2.0 * kWobble;
    float patches = ValueNoise(wobbled * kPatchScale) * 0.7 + ValueNoise(wobbled * kPatchScale * 2.3 + 17.0) * 0.3;
    float breakup = (ValueNoise(world.xy * kBreakupScale + 53.0) - 0.5) * 2.0 * kBreakup;
    // Where water gathers first: hollows, then flat patches; the fuller the puddles, the lower the bar.
    float gathers = (saturate(hollow) * 0.5 + patches * 0.6 + breakup) * flat * saturate(known * 2.0 - 1.0);
    float bar = 1.0 - Wetness() * 0.6;
    float puddle = smoothstep(bar, bar + kPuddleEdge, gathers) * onGround * reach * cPuddles.z;
    float damp = saturate(Wetness() * 4.0) * onGround * reach * saturate(cPuddles.z);
    float nearPuddle = smoothstep(bar - 0.2, bar, gathers);

    float3 color = scene * (1.0 - kDampDarkening * damp * (0.6 + 0.4 * nearPuddle));
    if (puddle <= 0.0)
        return float4(color, 1);

    float3 camera = cViewToWorld3.xyz;
    float3 toCamera = normalize(camera - world);
    float2 stir = NoiseSlope(world.xy * kStirScale + Seconds() * float2(0.3, 0.2)) * kStir;
    float2 rings = RainRings(world, RainNow(), distance, cPuddles.w);
    float3 normal = normalize(float3(-rings - stir, 1.0));
    float3 reflected = reflect(-toCamera, normal);
    float fresnel = lerp(kPuddleF0, kPuddleMirror, pow(1.0 - saturate(dot(normal, toCamera)), 5.0));
    float3 sky = SkyColor(reflected);
    float glint = pow(saturate(dot(reflected, cToLight.xyz)), kSunGlint) * SunVisibility() * cToLight.w;
    // The ground under the water, bent by the rings on it.
    float2 bent = ViewToPixel(WorldToView(world + float3(rings * kPuddleRefraction, 0.0)));
    float3 under = tex2Dlod(sSceneBefore, float4((clamp(bent, cViewportPx.xy, cViewportPx.xy + cViewportPx.zw - 1.0) +
                                                   0.5) * cTexelGrid.xy, 0, 0)).rgb * (1.0 - kUnderPuddleDarkening);
    float3 wet = lerp(under, sky, fresnel) + cSunColor.rgb * glint;
    return float4(lerp(color, wet, saturate(puddle)), 1);
}
