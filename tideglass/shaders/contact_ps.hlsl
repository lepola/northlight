#include "water_common.hlsli"

// Where something standing in the water (a character, a post, a plant's stem) crosses its surface, marked on a grid
// matching the ripple simulation's, for its foam (see Renderer::MarkContacts, contact_spread_ps): each cell's point on
// the water is looked for on screen, and it is a crossing where the solid world drawn there (before the liquid, so
// under the surface too) is that point, stands off the ground (the ground map; the shore has its own foam) and rises
// through the water rather than lying flat on it (a lily pad, seen whole from above, marked all of itself and grew foam
// with the view's height). Where the point is hidden, it is marked too when what hides it stands just in front of or
// above it (inside a body, behind the side of it in sight): so the whole of what stands in the water is marked where it
// meets the surface, not only the line of it in sight, which blurred came to little. Only what is in sight is found.
// Slime has no ground map (it lies over building floors as often as terrain), so there the bank and the floor of a room
// are found the same way as what stands in it: whatever of the solid world lies at the surface marks, upright or not,
// and its edge glow is read from that (see water_ps, SlimeContact).

// A point of slime that could not be looked at this frame (off screen, too far, behind something) says so, so that what
// was found there before is kept: its edge is remembered where the camera has seen it (contact_spread_ps), so the glow
// along a wall goes on while the line where they meet is out of sight.

// x: 1 where the ground map tells the ground from what stands on it (water), 0 where there is none (slime).
float4 cContactOptions : register(c60);

static const float kNotSeen = -1.0;

// How near (yards) the solid world drawn at a cell's point must come to it to mark it, and how far under it at most.
static const float kContactBand = 0.25;
static const float kContactBelow = 0.08;
// Beyond this far from the camera (yards) nothing is marked (the foam has faded out by then, see water_ps).
static const float kContactReach = 60.0;
// How upright (1 - the up component of its facing) a surface must be to mark: from not at all to fully. Only what lies
// quite flat is left out (a lily pad); a swimmer's back, curving at the surface, still marks.
static const float kUprightFrom = 0.05;
static const float kUprightFull = 0.2;
// A hidden point is marked where what hides it lies within this far of it across the surface (yards), and at most this
// high above it (from full to none): a body where it meets the water, not its shoulders (broad armour a yard up marked
// all the water under it, a wide disc of foam), nor a bridge or branch over the water.
static const float kCoverReach = 0.25;
static const float kCoverFull = 0.25;
static const float kCoverHeight = 0.5;

float3 MetAt(float2 pixel)
{
    return WorldPosition(ViewPosition(pixel, ViewZ(SampleRawDepth(sOpaqueDepth, pixel))));
}

// The side of the neighbour on either side nearer to met (the other may lie across an edge, on something else).
float3 NearerStep(float3 met, float2 pixel, float2 step)
{
    float3 ahead = MetAt(pixel + step) - met;
    float3 behind = met - MetAt(pixel - step);
    return dot(ahead, ahead) < dot(behind, behind) ? ahead : behind;
}

// How upright the surface drawn at pixel is, from the positions beside it.
float Upright(float3 met, float2 pixel)
{
    float3 facing = cross(NearerStep(met, pixel, float2(1, 0)), NearerStep(met, pixel, float2(0, 1)));
    return 1.0 - abs(facing.z) / max(length(facing), 1.0e-6);
}

float4 main(float2 cell : VPOS) : COLOR0
{
    float3 water = float3(cRippleArea.xy + (cell + 0.5) * cRippleGrid.y, 0.0);
    bool grounded = cContactOptions.x > 0.5;
    float mark = grounded ? 0.0 : kNotSeen;
    float height;
    if (WaterHeightAt(water.xy, height))
    {
        water.z = height;
        float2 pixel = ViewToPixel(WorldToView(water));
        // (A point behind the camera projects onto the screen too, where what is drawn lies far from it.)
        float raw = InsideViewport(pixel) ? SampleRawDepth(sOpaqueDepth, pixel) : 1.0;
        // Only solid ground and things (the sky's depth lies at infinity, whose position is not a number and, marked,
        // spread through the whole grid). Branches rather than selects: the compiler may multiply a select through.
        if (length(water - cViewToWorld3.xyz) < kContactReach && InsideViewport(pixel) && !BeyondFarClip(raw) &&
            raw < kSkyDepth)
        {
            float3 met = MetAt(pixel);
            float2 mapUv = RippleUv(met.xy);
            float4 map = grounded && InsideRippleArea(mapUv) ? tex2Dlod(sSeabed, float4(mapUv, 0, 0)) : 0.0;
            if (!grounded || (map.g > 0.5 && met.z - map.r > kOnGround))
            {
                // At the surface: not what lies just under it (a swimmer's back and arms a few inches down marked
                // all the water over them, a wide disc of foam).
                float near = (1.0 - smoothstep(kContactBand * 0.5, kContactBand, length(met - water))) *
                             (1.0 - smoothstep(kContactBelow * 0.5, kContactBelow, water.z - met.z));
                float crossing = near * (grounded ? smoothstep(kUprightFrom, kUprightFull, Upright(met, pixel)) : 1.0);
                float above = met.z - water.z;
                float covered = 0.0;
                if (length(met - cViewToWorld3.xyz) < length(water - cViewToWorld3.xyz) - kContactBand)
                    covered = (1.0 - smoothstep(kCoverReach * 0.5, kCoverReach, length(met.xy - water.xy))) *
                              (1.0 - smoothstep(kCoverFull, kCoverHeight, above));
                float found = saturate(max(crossing, covered));
                // Seen: the solid world here is this point of the surface, or lies beyond it (the view goes on through
                // the surface to the floor under it).
                bool beyond = length(met - cViewToWorld3.xyz) > length(water - cViewToWorld3.xyz);
                if (grounded || found > 0.0 || beyond)
                    mark = found;
            }
        }
    }
    return float4(mark, 0.0, 0.0, 0.0);
}
