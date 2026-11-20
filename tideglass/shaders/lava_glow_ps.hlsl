// The lava's light on the land and whatever else stands beside it, as in tools/lava_lab.html's rock: strongest where it meets
// the lava and just above it, scorched dark right at the edge. From the world: how far each place is from the lava by the
// ground maps (a fine one near the camera, a coarse one reaching about 300 yards), and how far above it, so a place looks the same from any camera. Worked out on screen it did not: distances taken
// as if the surface faced the camera and depth compared along the view spread it wide from above and all but lost it from low
// down, and coarse levels of the screen's picture of the lava gave it steps. On screen still only what the map cannot know: all
// of it where the camera is over no known ground (inside a building), and what the map shows no shore for yet stands beside lava
// on screen and is no terrain (a building's own walls). What stands on the ground (a character, a rock, a tree) is only lit, its
// own look kept: painted as the land is, it wore the glow as a layer over it. The air hides it as it hides what it falls on:
// what it is drawn over has its fog already, and light added at full strength stood out on the fog far off.
#include "common.hlsli"
#include "lava.hlsli"

sampler2D sLavaScene : register(s2);
sampler2D sLavaHalo : register(s3);
sampler2D sHaloColours : register(s5);
float4 cPalette : register(c91); // x: homogeneous fel share; y: both variants visible

// c9: x, y one over the halo's size; z how many screen pixels to a halo pixel; w pixels a yard is wide at a view depth of 1.
float4 cHalo : register(c9);
// As in water_common.hlsli: the square the ground map covers, and the map (reads as no height when there is none).
float4 cRippleArea : register(c26);
sampler2D sSeabed : register(s7);
// r: how far water is from the land; g: how far the land is from the water (yards, up to GroundMap::kShoreReach).
sampler2D sShore : register(s8);
// The coarse map (Renderer::FarGroundTexture; reads as nothing where there is none): r the ground's height; g 1 where known; b the
// liquid's height; a how far the land is from lava. c90: its corner, and one over its size.
sampler2D sFarGround : register(s4);
float4 cFarArea : register(c90);
// As in water_common.hlsli: the game's fog, and the air between a point and the camera as the lava's own composite takes it.
float4 cFogRange : register(c15);
float4 cSkyLayout : register(c45);
float4 cEnvironment : register(c80);

static const float kSky = 0.99995;
// How far (yards) the light reaches across the land: its near strength and its wider spread; how high it climbs; how far its
// depth may differ from the lava's before it stops counting; the dark right at the edge.
static const float kNearReach = 3.5;
static const float kWideReach = 9.5;
static const float kClimb = 6.0;
static const float kDepthFrom = 6.0;
static const float kDepthTo = 18.0;
static const float kScorch = 0.3;
// How far from the camera (yards) the coarse map is believed: a circle well inside its square.
static const float kFarKnownFrom = 230.0;
static const float kFarKnownTo = 290.0;
// How far from the map's ground (yards) a place still counts as that ground.
static const float kOnTerrain = 1.5;
// Less lava than this in a texel is none: below anything the wide reach's four samples could count (0.003 of their area).
static const float kNoneNear = 1.0e-5;
// How far above the ground map (yards) something starts to count as standing on the ground, from not at all to fully, and how
// far from the camera that is known, as for the lava's shore: a circle well inside the map's square, so no edge of it shows.
static const float kStandsFrom = 0.25;
static const float kStandsFull = 0.6;
static const float kStandsKnownFrom = 50.0;
static const float kStandsKnownTo = 72.0;
// How much brighter the lava's light makes what stands beside it.
static const float kLightOnThings = 0.9;

// The share of the screen about this pixel, `reach` yards wide at this depth, that is lava, with its averages (see lava_halo_ps).
float4 HaloAt(sampler2D halo, float2 uv, float reachPixels)
{
    float2 texel = cHalo.xy;
    float lod = max(log2(max(reachPixels / cHalo.z, 1.0)), 0.0);
    float2 spread = 0.6 * reachPixels / cHalo.z * texel;
    return (tex2Dlod(halo, float4(uv + spread * float2(1, 1), 0, lod)) +
            tex2Dlod(halo, float4(uv + spread * float2(-1, 1), 0, lod)) +
            tex2Dlod(halo, float4(uv + spread * float2(1, -1), 0, lod)) +
            tex2Dlod(halo, float4(uv + spread * float2(-1, -1), 0, lod))) * 0.25;
}

// How much this point stands on the ground rather than being the ground.
float StandsOnGround(float3 world)
{
    float standing = 0.0;
    float2 uv = (world.xy - cRippleArea.xy) * cRippleArea.z;
    if (all(uv > 0.0) && all(uv < 1.0))
    {
        float4 ground = tex2Dlod(sSeabed, float4(uv, 0, 0));
        // (Beside a cell with no height, the height read is half made of nothing.)
        if (ground.g > 0.99)
            standing = smoothstep(kStandsFrom, kStandsFull, world.z - ground.r) *
                       (1.0 - smoothstep(kStandsKnownFrom, kStandsKnownTo, length(world.xy - cViewToWorld3.xy)));
    }
    return standing;
}

// How much of a point the air between it and the camera leaves to be seen.
float AirLeaves(float viewZ, float3 world)
{
    float leaves = 1.0 - saturate((viewZ - cFogRange.x) / max(cFogRange.y - cFogRange.x, 1.0));
    if (cEnvironment.x > 0.5 && cSkyLayout.x > 1.5)
        leaves *= exp(cEnvironment.w * length(world - cViewToWorld3.xyz) / max(cEnvironment.z, 1.0));
    return leaves;
}

// What the light does to what is there.
float3 Lit(float3 scene, float glow, float scorch, float standing, float2 uv, float viewZ)
{
    float fel = cPalette.x;
    [branch] if (cPalette.y > 0.5)
    {
        float yards = cHalo.w / max(abs(viewZ), 1.0);
        float2 wide = HaloAt(sHaloColours, uv, kWideReach * yards).rg;
        float2 near = HaloAt(sHaloColours, uv, kNearReach * yards).rg;
        // At the bank the nearest pool dominates. Further out only the wide glow reaches the receiver.
        float local = 0.8 * smoothstep(0.0, 0.25, near.r);
        fel = saturate(lerp(wide.g / max(wide.r, 1.0e-5), near.g / max(near.r, 1.0e-5), local));
        // With two emitters, add light only where its source can be identified.
        float known = smoothstep(0.0, 0.01, max(near.r, wide.r));
        glow *= known;
        scorch *= known;
    }
    float3 lit = scene * (1.0 - kScorch * scorch);
    float3 light = MagmaColour(lerp(kLavaCool, kLavaWarm, glow * glow), fel);
    // Keeps what is drawn on the land: light falls on it, it does not paint over it.
    float bright = 0.6 + 1.6 * dot(scene, float3(0.3, 0.6, 0.1));
    float3 painted = lerp(lit, light * bright, saturate(glow * 0.95 * kLavaRockGlow));
    return lerp(painted, scene * (1.0 + light * glow * kLightOnThings * kLavaRockGlow), standing);
}

float4 main(float2 pixel : VPOS) : COLOR0
{
    float raw = SampleRawDepth(sOpaqueDepth, pixel);
    float2 uv = (pixel + 0.5) * cTexelGrid.xy;
    float keep = -1.0;
    float4 result = 0.0;
    // A model in front of captured lava receives the same light on both sides of its silhouette.
    [branch] if (raw < abs(SampleRawDepth(sDepth, pixel)) && raw <= kSky)
    {
        float viewZ = ViewZ(raw);
        float3 world = WorldPosition(ViewPosition(pixel, viewZ));
        float2 mapUv = (world.xy - cRippleArea.xy) * cRippleArea.z;
        float overGround = tex2Dlod(sSeabed, float4((cViewToWorld3.xy - cRippleArea.xy) * cRippleArea.z, 0, 0)).g;
        float4 ground = 0.0;
        float land = kWideReach;
        float known = 0.0;
        [branch] if (all(mapUv > 0.0) && all(mapUv < 1.0))
        {
            ground = tex2Dlod(sSeabed, float4(mapUv, 0, 0));
            land = tex2Dlod(sShore, float4(mapUv, 0, 0)).g;
            if (ground.g > 0.99)
                known = 1.0 - smoothstep(kStandsKnownFrom, kStandsKnownTo, length(world.xy - cViewToWorld3.xy));
        }
        [branch] if (overGround > 0.5 && (land < kWideReach || known <= 0.0 || abs(world.z - ground.r) <= kOnTerrain))
        {
            // Further out, the coarse map: blended over the fine one's fade, the same light on either side of it.
            float liquid = ground.b;
            float believed = known;
            [branch] if (known < 1.0)
            {
                float2 farUv = (world.xy - cFarArea.xy) * cFarArea.z;
                [branch] if (all(farUv > 0.0) && all(farUv < 1.0))
                {
                    float4 far_ = tex2Dlod(sFarGround, float4(farUv, 0, 0));
                    if (far_.g > 0.99)
                    {
                        land = lerp(far_.a, land, known);
                        liquid = lerp(far_.b, liquid, known);
                        believed = max(known, 1.0 - smoothstep(kFarKnownFrom, kFarKnownTo, length(world.xy - cViewToWorld3.xy)));
                    }
                }
            }
            // Most of what is not near a shore ends here.
            [branch] if (believed > 0.0 && land < kWideReach)
            {
                float above = world.z - liquid;
                float rises = exp(-max(above, 0.0) / kClimb) * smoothstep(-1.0, -0.2, above) * AirLeaves(viewZ, world) * believed;
                float reachNear = 1.0 - smoothstep(0.0, kNearReach, land);
                float reachWide = 1.0 - smoothstep(0.0, kWideReach, land);
                float glow = (0.55 * reachNear + 0.45 * reachWide) * rises;
                keep = glow - 0.015;
                float3 scene = tex2Dlod(sLavaScene, float4(uv, 0, 0)).rgb;
                result = float4(Lit(scene, glow, reachNear * reachNear * rises, StandsOnGround(world), uv, viewZ), 1.0);
            }
        }
        else
        {
            float yards = cHalo.w / max(abs(viewZ), 1.0);
            float wideLod = max(log2(max(kWideReach * yards / cHalo.z, 1.0)), 0.0);
            // At a level two above the wide reach's, a texel holds all that reach's four samples: no lava there, none near.
            [branch] if (tex2Dlod(sLavaHalo, float4(uv, 0, wideLod + 2.0)).r > kNoneNear)
            {
                float4 wide = HaloAt(sLavaHalo, uv, kWideReach * yards);
                [branch] if (wide.r >= 0.003)
                {
                    float4 near = HaloAt(sLavaHalo, uv, kNearReach * yards);
                    float lavaZ = wide.g / wide.r;
                    float above = world.z - (wide.b / wide.r + cViewToWorld3.z);
                    float depthAgrees = 1.0 - smoothstep(kDepthFrom, kDepthTo, abs(viewZ - lavaZ));
                    float rises = exp(-max(above, 0.0) / kClimb) * smoothstep(-1.0, -0.2, above) * AirLeaves(viewZ, world);
                    float reachNear = smoothstep(0.0, 0.5, near.r);
                    float reachWide = smoothstep(0.02, 0.5, wide.r);
                    float glow = (0.55 * reachNear + 0.45 * reachWide) * depthAgrees * rises;
                    keep = glow - 0.015;
                    float3 scene = tex2Dlod(sLavaScene, float4(uv, 0, 0)).rgb;
                    // Screen proximity identifies nearby heat, but cannot identify a terrain receiver.
                    result = float4(Lit(scene, glow, 0.0, 1.0, uv, viewZ), 1.0);
                }
            }
        }
    }
    clip(keep);
    return result;
}
