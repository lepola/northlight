#include "water_common.hlsli"

// What lies under the water, for the refraction to bend into (see Renderer::DrawUnderwaterScene): the scene captured
// before the liquid where water covers it, with how much water the view crosses there, and the ground just above the
// water as it is (the bottom carries on up into the shore, and a view bent just past the waterline shows it); what
// stands taller out of the water (a character's body, a plant) is left to be filled in (fill_pull_ps,
// fill_push_ps) from the water around it. Stored as the fill reads it: rgb and a 1 where known; then the state
// (3 known, 1 to fill), the water's thickness, and how much it counts in filling what is not. Drawn at half the
// resolution: each texel is the first of its 2x2 pixels.

// The water over a pixel (yards) from which it starts to count in the fill, and below which it counts fully: the fill is
// of the bottom, and what lies just under the surface is more likely part of what stands out of it (a character's
// submerged legs and arms, which filled in round the body drew a copy of them there).
static const float kFillFromDepth = 0.3;
static const float kFillFullDepth = 1.0;
// What the shallowest water, and the shore's ground, still count.
static const float kFillShallowShare = 0.05;
// What is out of the water counts as the shore's ground, kept as it is, where it lies within this far (yards) of the
// ground's own height (the ground map; a character or a plant stands above it, and is filled in) and this far above
// the water (kOnGround): filled in from the bottom further out, which the game draws darker, the ground along the
// waterline showed as a dark band where the view was bent onto it.
static const float kShoreGround = 0.6;

struct SceneOutput
{
    float4 colour : COLOR0;
    float4 hit : COLOR1;
};

SceneOutput main(float2 texel : VPOS)
{
    float2 pixel = texel * 2.0;
    float thickness = WaterThickness(pixel, SampleRawDepth(sDepth, pixel));
    float3 colour = tex2Dlod(sSceneBefore, float4((pixel + 0.5) * cTexelGrid.xy, 0, 0)).rgb;
    float raw = SampleRawDepth(sOpaqueDepth, pixel);
    float3 ground = WorldPosition(ViewPosition(pixel, ViewZ(raw)));
    float height;
    float2 mapUv = RippleUv(ground.xy);
    float4 map = InsideRippleArea(mapUv) ? tex2Dlod(sSeabed, float4(mapUv, 0, 0)) : 0.0;
    bool onGround = map.g > 0.5 && abs(ground.z - map.r) < kOnGround;
    bool shore = !BeyondFarClip(raw) && onGround && WaterHeightAt(ground.xy, height) && ground.z - height < kShoreGround;
    // (Selected rather than returned early: Microsoft's compiler mishandles an early return inside a branch.)
    bool water = thickness > 0.0;
    SceneOutput output;
    output.colour = water || shore ? float4(colour, 1.0) : 0.0;
    output.hit = water   ? float4(3.0, thickness,
                                  max(smoothstep(kFillFromDepth, kFillFullDepth, thickness), kFillShallowShare), 0.0)
                 : shore ? float4(3.0, 0.0, kFillShallowShare, 0.0)
                         : float4(1.0, 0.0, 0.0, 0.0);
    return output;
}
