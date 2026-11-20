// Ambient occlusion of the solid world, from the depth buffer: how much of the half-sphere above each point is taken
// up by the geometry around it (after McGuire's scalable ambient obscurance). 1 = open, lower = tucked in. Noisy per
// pixel; ao_blur_ps.hlsl smooths it and puts it on the scene.
#include "common.hlsli"

float4 cAo : register(c9);     // radius (yards), fade start, fade end (yards from the camera), pixels per yard at 1 yard
float4 cAoFog : register(c10); // the game's fog start and end (yards), strength, 0

static const int kTaps = 12;
static const float kTurns = 7.0;
static const float kTwoPi = 6.2831853;
// A small lift so flat ground does not shade itself; it grows with distance, where depth is coarser.
static const float kBias = 0.03;
// Only what rises more than this steeply above the surface (the sine of about 20 degrees) shades it: the ground is
// made of flat triangles folded a little against one another, and counting those folds drew their lines on smooth
// terrain and darkened every dip. Walls, feet and things standing on the ground rise far more steeply.
static const float kAngleBias = 0.34;
static const float kBiasGrowth = 25.0;
static const float kMaxRadiusPixels = 150.0;
static const float kMinRadiusPixels = 1.5;

bool NoSurface(float rawDepth)
{
    return IsTrueSky(rawDepth) || BeyondFarClip(rawDepth);
}

float3 ViewAt(float2 pixel)
{
    return ViewPosition(pixel, ViewZ(SampleRawDepth(sDepth, pixel)));
}

float2 ClampToViewport(float2 pixel)
{
    return clamp(pixel, cViewportPx.xy, cViewportPx.xy + cViewportPx.zw - 1.0);
}

// Of the steps to the neighbours on either side, the one nearer in depth; at the edge of the view there is no
// neighbour beyond it, so the step inward is taken.
float3 NearerStep(float3 forward, float3 back, bool atStart, bool atEnd)
{
    return atStart || (!atEnd && abs(forward.z) < abs(back.z)) ? forward : back;
}

// The surface's facing from its neighbours, taking on each axis the side nearer in depth so edges do not bend it.
float3 SurfaceNormal(float2 pixel, float3 centre)
{
    float2 first = cViewportPx.xy;
    float2 last = cViewportPx.xy + cViewportPx.zw - 1.0;
    float3 left = ViewAt(ClampToViewport(pixel - float2(1, 0)));
    float3 right = ViewAt(ClampToViewport(pixel + float2(1, 0)));
    float3 up = ViewAt(ClampToViewport(pixel - float2(0, 1)));
    float3 down = ViewAt(ClampToViewport(pixel + float2(0, 1)));
    float3 across = NearerStep(right - centre, centre - left, pixel.x <= first.x, pixel.x >= last.x);
    float3 along = NearerStep(down - centre, centre - up, pixel.y <= first.y, pixel.y >= last.y);
    float3 n = normalize(cross(across, along));
    return dot(n, centre) > 0.0 ? -n : n;
}

float InterleavedNoise(float2 pixel)
{
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

float4 main(float2 pixel : VPOS) : COLOR0
{
    // Whole pixels, so its neighbours and taps line up with the depth texels even at the edges of the view.
    pixel = floor(pixel);
    float rawDepth = SampleRawDepth(sDepth, pixel);
    if (NoSurface(rawDepth))
        return 1.0;
    float3 centre = ViewPosition(pixel, ViewZ(rawDepth));
    float distance = length(centre);
    float fogged = saturate((distance - cAoFog.x) / max(cAoFog.y - cAoFog.x, 1.0));
    float fade = (1.0 - smoothstep(cAo.y, cAo.z, distance)) * (1.0 - fogged);
    float radius = cAo.x;
    float radiusPixels = min(cAo.w * radius / centre.z, kMaxRadiusPixels);
    if (fade <= 0.0 || radiusPixels < kMinRadiusPixels)
        return 1.0;

    float3 normal = SurfaceNormal(pixel, centre);
    float spin = InterleavedNoise(pixel) * kTwoPi;
    float radius2 = radius * radius;
    float bias = kBias * max(1.0, centre.z / kBiasGrowth);
    float sum = 0.0;
    [unroll] for (int i = 0; i < kTaps; ++i)
    {
        float along = (i + 0.5) / kTaps;
        float angle = along * kTurns * kTwoPi + spin;
        float2 tap = ClampToViewport(floor(pixel + float2(cos(angle), sin(angle)) * along * radiusPixels + 0.5));
        float tapDepth = SampleRawDepth(sDepth, tap);
        if (NoSurface(tapDepth))
            continue;
        float3 v = ViewPosition(tap, ViewZ(tapDepth)) - centre;
        float vv = dot(v, v);
        float room = max(radius2 - vv, 0.0);
        sum += room * room * room * max((dot(v, normal) - kAngleBias * sqrt(vv) - bias) / (0.01 + vv), 0.0);
    }
    // The angle bias takes a share off what does count, most near the corner's edge; it is given back (twice over, as
    // measured on a wall meeting the floor) so corners keep their depth of shade.
    float open = max(0.0, 1.0 - sum * cAoFog.z * 5.0 / (((1.0 - kAngleBias) * (1.0 - kAngleBias)) * radius2 * radius2 * radius2 * kTaps));
    return lerp(1.0, open, fade);
}
