// Where the lava lies on screen, a quarter the size and with mip levels made from it, for the glow on what stands beside it
// (lava_glow_ps) and the border where the world gives no shore distance (lava_ps) to read at the width each pixel needs.
// r: how much is lava; g, b: that times its view depth and its height from the camera, so that any mip level gives their
// averages over the lava alone; a: how much of what lies around is a shore, where the lava meets something at its own depth,
// and not where something merely stands in front of it. On both sides of the shore and in several steps per texel: found
// only from lava texels, one step away, a shore lost strength wherever it fell between them, which broke a nearly level
// shoreline into dashes that slid along it as the camera moved.
#include "common.hlsli"

// One over the target's size, and how many screen pixels to a target pixel.
float4 cHalo : register(c9);

static const float kNoLava = 0.99999;
// How near in depth (yards, and as a share of the distance) what lies beside the lava must be to count as its shore.
static const float kShoreDepth = 1.5;
static const float kShoreDepthShare = 0.03;

bool NearDepth(float raw, float viewZ)
{
    return abs(ViewZ(raw) - viewZ) < max(kShoreDepth, viewZ * kShoreDepthShare);
}

// From lava: whether this pixel is what it meets. From what it meets: whether this pixel is the lava.
float ShoreAt(float2 pixel, float viewZ, bool fromLava)
{
    float found = 0.0;
    float surface = abs(SampleRawDepth(sDepth, pixel));
    if (fromLava)
    {
        if (surface >= kNoLava)
        {
            float opaque = SampleRawDepth(sOpaqueDepth, pixel);
            if (!BeyondFarClip(opaque) && NearDepth(opaque, viewZ))
                found = 1.0;
        }
    }
    else if (surface < kNoLava && NearDepth(surface, viewZ))
        found = 1.0;
    return found;
}

// Falls evenly from the shore to nothing over two texels, so the level a border is cut at lies the same distance from the
// shore however the shore falls on the texels, once they are read filtered.
float ShoreAround(float2 pixel, float viewZ, bool fromLava)
{
    float found = 0.0;
    [unroll] for (int step_ = 1; step_ <= 4; ++step_)
    {
        float reach = step_ * cHalo.y * 0.5;
        found += ShoreAt(pixel + float2(reach, 0), viewZ, fromLava) + ShoreAt(pixel - float2(reach, 0), viewZ, fromLava) +
                 ShoreAt(pixel + float2(0, reach), viewZ, fromLava) + ShoreAt(pixel - float2(0, reach), viewZ, fromLava);
    }
    return found * 0.0625;
}

// Whether lava and what is not lava both lie within reach of here; nearly all the screen does not need looking at more closely.
bool MixedAround(float2 pixel, bool lava)
{
    float2 far_ = float2(cHalo.y * 2.0, 0.0);
    float2 near_ = float2(cHalo.y, 0.0);
    bool mixed = false;
    mixed = mixed || (abs(SampleRawDepth(sDepth, pixel + far_.xy)) < kNoLava) != lava;
    mixed = mixed || (abs(SampleRawDepth(sDepth, pixel - far_.xy)) < kNoLava) != lava;
    mixed = mixed || (abs(SampleRawDepth(sDepth, pixel + far_.yx)) < kNoLava) != lava;
    mixed = mixed || (abs(SampleRawDepth(sDepth, pixel - far_.yx)) < kNoLava) != lava;
    mixed = mixed || (abs(SampleRawDepth(sDepth, pixel + near_.xy)) < kNoLava) != lava;
    mixed = mixed || (abs(SampleRawDepth(sDepth, pixel - near_.xy)) < kNoLava) != lava;
    mixed = mixed || (abs(SampleRawDepth(sDepth, pixel + near_.yx)) < kNoLava) != lava;
    mixed = mixed || (abs(SampleRawDepth(sDepth, pixel - near_.yx)) < kNoLava) != lava;
    return mixed;
}

struct HaloOutput
{
    float4 geometry : COLOR0;
    float4 colours : COLOR1;
};

HaloOutput main(float2 cell : VPOS)
{
    float2 pixel = floor((cell + 0.5) * cHalo.y);
    float materialDepth = SampleRawDepth(sDepth, pixel);
    float surface = abs(materialDepth);
    bool lava = surface < kNoLava;
    float4 result = 0.0;
    // Hidden lava supplies no visible heat to the screen-space glow estimate.
    [branch] if (lava && surface <= SampleRawDepth(sOpaqueDepth, pixel))
    {
        float viewZ = ViewZ(surface);
        result.rgb = float3(1.0, viewZ, WorldPosition(ViewPosition(pixel, viewZ)).z - cViewToWorld3.z);
    }
    [branch] if (MixedAround(pixel, lava))
    {
        float opaque = lava ? surface : SampleRawDepth(sOpaqueDepth, pixel);
        [branch] if (!BeyondFarClip(opaque))
            result.a = ShoreAround(pixel, ViewZ(opaque), lava);
    }
    HaloOutput output;
    output.geometry = result;
    output.colours = float4(result.r, materialDepth < 0.0 ? result.r : 0.0, 0.0, 0.0);
    return output;
}
