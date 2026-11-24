// Shared foam coverage. Call after water_common.hlsli; lighting belongs to each side of the surface.

// Foam where the water meets something standing in it (ContactFoam): how far from the camera it shows; how far (yards)
// the foam reaches from where it meets the water; and the rim hugging it: how strongly, how wide (yards), how far its
// long warbles reach, how soft its edge is (in rim widths) and how much its edge warbles finely (see tools/foam_lab.html).
static const float kContactFoamFarthest = 60.0;
static const float kContactFoamReach = 0.27;
static const float kContactRim = 1.00;
static const float kContactRimWidth = 0.155;
static const float kContactRimWarp = 2.15;
static const float kContactRimSoftness = 0.88;
static const float kContactRimWarble = 1.50;
// How fast the rim's warps drift.
static const float kContactRimDrift = 0.27;
// The width (grid cells) of the contact foam's falloff on the ripple grid (contact_spread_ps: kSpreadWidth), for
// turning it back into a distance.
static const float kContactFieldWidth = 2.5;
// A band of foam hugging what stands in the water, warped where each point lies into long reaching warbles with a finer
// warble on its edge, never thinner than half its width so it always reaches the body. away: yards from where it meets
// the water; footprint: the yards of surface a pixel covers.
float ContactRim(float2 p, float away, float footprint)
{
    float slow = FoamPatternSeconds() * kContactRimDrift;
    p /= FoamPatternScale();
    float warp = ValueNoise(p * 1.6 + float2(slow, -slow)) * 0.65 + ValueNoise(p * 4.0 - float2(slow * 1.5, 0.0)) * 0.35;
    float warble = ValueNoise(p * 4.5 + float2(slow * 2.0, -slow)) * 0.65 +
                   ValueNoise(p * 9.0 - float2(0.0, slow * 2.5)) * 0.35;
    float reach = max(kContactRimWidth * 0.5, kContactRimWidth * (1.0 + (warp - 0.5) * 2.4 * kContactRimWarp)) +
                  (warble - 0.5) * kContactRimWidth * kContactRimWarble;
    float aa = footprint * 0.75;
    return (1.0 - smoothstep(reach - aa - kContactRimWidth * kContactRimSoftness, reach + aa, away)) * kContactRim;
}

// Foam where the water meets something standing in it (a character, a post, a plant's stem), from the ripple grid
// (contact_ps, contact_spread_ps, ripple_ps), which holds a falloff with the distance from where it meets the water,
// A exp(-d^2 / 2w^2). The distance comes from how fast the falloff's logarithm changes (d / w^2 times the distance's
// own slope, whatever A is: A is below 1 where only part of the waterline was marked, and taken from the value alone
// the rim stood off from the body there); it gives how thick the line foam is (falling over kContactFoamReach) and
// where the rim hugging it lies. The grid's value itself fades it out as it lingers behind something moving.
float ContactField(float2 uv)
{
    return InsideRippleArea(uv) ? tex2Dlod(sRipples, float4(uv, 0, 0)).b : 0.0;
}

void ContactFoam(float2 onSurface, float distance, float footprint, out float density, out float rim)
{
    float near = 1.0 - smoothstep(kContactFoamFarthest * 0.6, kContactFoamFarthest, distance);
    float2 uv = RippleUv(onSurface);
    float texel = cRippleGrid.x;
    float field = ContactField(uv);
    density = 0.0;
    rim = 0.0;
    float present = smoothstep(0.02, 0.25, field) * near;
    // A zero contact weight discards both outputs. Skip the neighbouring
    // field samples and the animated rim across that part of the surface.
    [branch] if (present > 0.0)
    {
        float2 change = float2(log(max(ContactField(uv + float2(texel, 0.0)), 1.0e-4)) -
                                   log(max(ContactField(uv - float2(texel, 0.0)), 1.0e-4)),
                               log(max(ContactField(uv + float2(0.0, texel)), 1.0e-4)) -
                                   log(max(ContactField(uv - float2(0.0, texel)), 1.0e-4))) /
                        (2.0 * cRippleGrid.y);
        float width = kContactFieldWidth * cRippleGrid.y;
        float away = width * width * length(change);
        density = exp(-away * away / (2.0 * kContactFoamReach * kContactFoamReach)) * present;
        rim = ContactRim(onSurface, away, footprint) * present;
    }
}

// All the water's foam, drawn by the one line foam (FoamLines): each source gives how thick it is (and the shore's, the
// way it streams); the rim round what stands in the water and the lines along the tops of the shore's waves, the
// wakes' leading ridges, the ripples and the chosen swells lie over it. Open water has its dapple and its swells'
// foam (OpenWaterFoam). The shore's is measured against the
// surface drawn here (its own height), not the smoothed water level, which shifts a little as the camera moves and, on
// a gentle bank, would move the waterline a long way.
float WaterFoam(float2 onSurface, float drawnHeight, float depth, float distance, float footprint,
                float2 wakeFoam, float openWaterAmount)
{
    // Each source folded in as it comes (thickness, the way it streams, and the lines over it), so few are held at once;
    // open water's first, the heaviest, while the fewest are.
    float sourceLine;
    float sourceFoam;
    float2 sourceFlow;
    OpenWaterFoam(onSurface, depth, distance, footprint, openWaterAmount, sourceFoam, sourceFlow);
    float density = sourceFoam;
    float2 flow = sourceFlow;
    float crests;
    ShoreFoam(onSurface, drawnHeight, footprint, sourceFoam, crests, sourceFlow);
    density = max(density, max(sourceFoam, wakeFoam.x));
    flow += sourceFlow;
    crests = max(crests, wakeFoam.y);
    float contact;
    float rim;
    ContactFoam(onSurface, distance, footprint, contact, rim);
    density = max(density, contact);
    crests = max(crests, rim);
    RippleCrests(onSurface, footprint, sourceLine, sourceFoam, sourceFlow);
    density = max(density, sourceFoam);
    flow += sourceFlow;
    crests = max(crests, sourceLine);
    float lines = FoamLines(onSurface, density, flow, footprint);
    return saturate(max(lines, crests) * FoamGrain(onSurface) * kFoamMost * FoamStrength());
}

