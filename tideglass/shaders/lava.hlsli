// The lava's look: tools/lava_lab.html's values, as its Copy shader values button gives them. To change the look, tune it
// there and paste its block over this one. (The floating rocks are off and not drawn here; their values stay so the block
// can be pasted whole.)
static const float kLavaHeat = 0.63; // 0 solid crust, 1 nearly all molten
static const float kLavaHeatPatches = 0.42; // how far heat wanders across the surface
static const float kLavaShoreCool = 1.5; // yards from shore over which it cools
static const float kLavaPlateSize = 3.3; // yards across a crust plate
static const float kLavaCrack = 0.120; // crack half-width when cold, in plates
static const float kLavaBreakup = 0.90; // how much sooner some plates melt than others
static const float kLavaFlow = 0.31; // multiplies the flow's yards per second
static const float kLavaCrustDrag = 0.18; // share of the flow's speed the crust keeps
static const float kLavaCurrentWidth = 10.5; // half-width of a large current, yards
static const float kLavaCurrentHeat = 0.14; // extra heat along a current
static const float kLavaCurrentSpeed = 0.65; // how much faster lava runs in a current
static const float kLavaCurrentLines = 0.055; // currents per unit of across-stream position
static const float kLavaSwirlSize = 30; // yards across one repeat of the molten pattern
static const float kLavaBands = 5; // steps of colour across the molten range
static const float kLavaBandSoftness = 0.50; // 0 hard steps, 0.5 a smooth gradient
static const float kLavaRibbons = 0.54; // bright lines running with the flow
static const float kLavaVeins = 0.15; // brightness along cracks and old crack lines
static const float kLavaBevel = 0.00; // chunky lit edge on each plate
static const float kLavaUnderglowFalloff = 10.0; // higher hugs the crack closer
static const float kLavaBorderWidth = 1.40; // yards of crust along the shore
static const float kLavaBorder = 0.90; // 0 none, 1 solid crust along the shore
static const float kLavaFringe = 0.44; // how strongly plate edges glow
static const float kLavaRockAmount = 0.27; // share of 36-yard cells with a rock
static const float kLavaRockSize = 1.00; // typical radius
static const float kLavaRockDrift = 0.66; // share of the flow's speed
static const float kLavaCollar = 0.58; // heat where a rock meets the lava
static const float kLavaBubbles = 0.13; // share of 6-yard cells with a bubble
static const float kLavaBubbleSize = 0.60; // bubble radius, yards
static const float kLavaBubbleSpeed = 0.16; // 1 swells over 3 to 7 seconds; lower is slower
static const float kLavaEmbers = 0.20; // how many sparks rise
static const float kLavaRockGlow = 0.85; // how strongly the lava lights the land beside it
static const float kLavaFoamLife = 12.0; // seconds until the centre of a trail has returned to the lava below; a narrow trail fades sooner
static const float kLavaFoamReach = 0.60; // how far from a moving body it is stirred
static const float kLavaFoamHeat = 0.92; // the value on the lava's colour ramp that the foam exposes
static const float kLavaFoamCell = 1.15; // yards across a cell of the foam's network
static const float kLavaFoamWidth = 0.045; // line half-width, in cells
static const float kLavaFoamSoftness = 0.140; // how far the line's edge fades, in cells
static const float kLavaFoamBreakup = 0.47; // how much thin foam breaks into pieces
static const float kLavaFoamMelt = 1.00; // how far thick foam cuts through the crust
static const float3 kLavaCrustDark = float3(0.388, 0.051, 0.051);
static const float3 kLavaCrustLight = float3(0.682, 0.122, 0.086);
static const float3 kLavaCool = float3(0.686, 0.098, 0.055);
static const float3 kLavaWarm = float3(1.000, 0.294, 0.059);
static const float3 kLavaHot = float3(0.988, 0.490, 0.051);
static const float3 kLavaHottest = float3(1.000, 0.690, 0.341);

// Map increasing heat to deep green, lime and yellow-green. Apply before fog or lighting the receiver.
float3 MagmaColour(float3 warm, float fel)
{
    float3 green = float3(warm.g * 1.12 + warm.b * 0.10, warm.r * 0.43 + warm.g * 0.70, warm.b * 0.55);
    return lerp(warm, green, fel);
}
