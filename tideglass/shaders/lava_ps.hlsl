// The lava: the material of tools/lava_lab.html, drawn over the surfaces the client's magma drew (captured and undone
// as slime's are, see Renderer::CaptureAndUndoSurface). It glows of itself, so nothing here reads the sun or the sky but
// the air between it and the camera. Written to read only baked patterns (lava_bake_*_ps), the shore distance and how
// stirred it is (lava_stir_ps).
#define DEFERRED_SLIME 1
#include "water_common.hlsli"
#include "lava.hlsli"

sampler2D sLavaCells : register(s9);
sampler2D sLavaNoise : register(s10);
sampler2D sLavaFoam : register(s11);
sampler2D sLavaRand : register(s12);
sampler2D sLavaFlow : register(s13);
sampler2D sLavaHalo : register(s14);

static const float kTau = 6.2831853;
static const float3 kLavaSun = float3(-0.55, 0.45, 0.70);
// Seconds each of the two flowing molten layers lives, and each of the two crust layers.
static const float kMoltenPeriod = 6.5;
static const float kCrustPeriod = 18.0;
static const float kCellsPerTile = 16.0;
// How many screen pixels to a texel of the base level of lava_halo_ps.
static const float kHaloScale = 4.0;
static const float kBubbleCell = 6.0;
static const float kEmberCell = 3.2;
// Yards one tile of the flow's pattern covers (lava_bake_flow_ps), how strongly it sets the across-stream position,
// the fastest the lava runs, and how far from the shore the flow slows to nothing.
static const float kFlowTileYards = 761.905;
static const float kFlowStrength = 118.0;
static const float kFlowFastest = 1.0;
static const float kFlowBankReach = 12.0;
static const float kNoLava = 0.99999;
// What the shore does to the lava is known only near the camera. It fades out by distance (a circle well inside the square
// the ground map covers), so no edge of that map ever shows; "far from any shore" beyond it.
static const float kShoreKnownFrom = 50.0;
static const float kShoreKnownTo = 72.0;
static const float kFarFromShore = 12.0;

float3 LavaRamp(float v)
{
    v = saturate(v);
    float3 c = lerp(kLavaCool, kLavaWarm, smoothstep(0.0, 0.34, v));
    c = lerp(c, kLavaHot, smoothstep(0.34, 0.68, v));
    return lerp(c, kLavaHottest, smoothstep(0.72, 1.0, v));
}

float ShoreMapAt(float2 p)
{
    return tex2Dlod(sShore, float4(RippleUv(p), 0, 0)).r;
}

float BankCrust(float yards)
{
    return (1.0 - smoothstep(kLavaBorderWidth * 0.55, kLavaBorderWidth, yards)) * kLavaBorder;
}

// A building can contain a whole pool beneath the terrain mesh. Its local liquid-query
// footprint is not a bank. Allow for interpolation along a real sloping bank.
float TerrainAtLava(float4 heights, float z)
{
    float terrainHeight = heights.r / max(heights.g, 1.0e-4);
    return heights.g * (1.0 - smoothstep(kLavaBorderWidth, 2.0 * kLavaBorderWidth, terrainHeight - z));
}

// World distance to the bank. Use the visible intersection where terrain cannot describe the pool.
float LavaShore(float2 p, float z, float across, out float screenShore)
{
    float shore = kFarFromShore;
    float near = 1.0 - smoothstep(kShoreKnownFrom, kShoreKnownTo, across);
    float2 uv = RippleUv(p);
    if (cGroundInfo.z <= 0.5 || !all(uv > 0.0) || !all(uv < 1.0)) near = 0.0;
    float terrain = 0.0;
    if (near > 0.0)
    {
        float4 heights = tex2Dlod(sSeabed, float4(uv, 0, 0));
        terrain = TerrainAtLava(heights, z);
        float known = terrain * near * saturate(heights.a * 10.0) * (abs(heights.b - z) < 2.0 ? 1.0 : 0.0);
        shore = lerp(kFarFromShore, min(ShoreMapAt(p), kFarFromShore), known);
    }
    if (near < 1.0)
    {
        float4 atCamera = tex2Dlod(sSeabed, float4(RippleUv(cViewToWorld3.xy), 0, 0));
        terrain = lerp(TerrainAtLava(atCamera, z), terrain, near);
    }
    screenShore = 1.0 - terrain;
    return shore;
}

struct LavaOutput
{
    float4 color : COLOR0;
    float depth : DEPTH;
};

// Two flowing layers of one pattern, each showing alone for part of its time: read only the one that shows, then.
float4 FlowingLayers(sampler2D pattern, float2 a, float2 b, float blend, float2 acrossPixel, float2 downPixel)
{
    float4 value = tex2Dgrad(pattern, blend >= 1.0 ? b : a, acrossPixel, downPixel);
    [branch] if (blend > 0.0 && blend < 1.0)
        value = lerp(value, tex2Dgrad(pattern, b, acrossPixel, downPixel), blend);
    return value;
}

LavaOutput Shade(float2 pixel, float surfaceDepth, float fel)
{
    float3 world = WorldPosition(ViewPosition(pixel, ViewZ(surfaceDepth)));
    float3 camera = cViewToWorld3.xyz;
    // Where the native surface steps up or down it stands steeply, and a material laid out by place alone smeared into
    // stripes down it: height moves the pattern as well (only steps show it; level lava just lies a little further along).
    float2 place = world.xy;
    float2 p = place + world.z * float2(0.71, 0.53);
    float3 toPoint = world - camera;
    float dist = length(toPoint);
    float3 ray = toPoint / dist;

    // The surface under the next pixels along, found on its plane: the depth there may belong to something else, and a
    // gradient taken from it blurred the lava along every edge. For the patterns' mip levels, and for softening edges with
    // distance rather than letting them shimmer.
    float height = world.z - camera.z;
    float3 rayRight = WorldPosition(ViewPosition(pixel + float2(1, 0), 1.0)) - camera;
    float3 rayDown = WorldPosition(ViewPosition(pixel + float2(0, 1), 1.0)) - camera;
    float along = height / (abs(ray.z) > 1.0e-4 ? ray.z : -1.0e-4);
    float alongRight = height / (abs(rayRight.z) > 1.0e-4 ? rayRight.z : -1.0e-4);
    float alongDown = height / (abs(rayDown.z) > 1.0e-4 ? rayDown.z : -1.0e-4);
    float2 acrossPixel = camera.xy + rayRight.xy * alongRight - place;
    float2 downPixel = camera.xy + rayDown.xy * alongDown - place;
    // (No wider, or skimming the surface blurred it all to one flat colour.)
    float widest = dist * 0.04;
    if (dot(acrossPixel, acrossPixel) > widest * widest) acrossPixel *= widest / length(acrossPixel);
    if (dot(downPixel, downPixel) > widest * widest) downPixel *= widest / length(downPixel);
    float2 pixelYards = float2(length(acrossPixel), length(downPixel));
    float footprint = sqrt(max(pixelYards.x * pixelYards.y, 1.0e-8));

    float screenShore;
    float shore = LavaShore(place, world.z, length(toPoint.xy), screenShore);

    // The across-stream position: the lava runs along its contour lines, at a speed set by how close together they lie, so
    // every current runs one way along its whole length. It is the same everywhere, whatever is known of the shore, and only
    // the speed eases off near a bank: the pattern changing with what was known showed the ground map's edge as a square.
    float4 flowPattern = tex2Dlod(sLavaFlow, float4(p / kFlowTileYards, 0, 0));
    float acrossValue = (flowPattern.r - 0.5) * kFlowStrength;
    float2 acrossSlope = kFlowStrength * flowPattern.ba;
    float2 flow = float2(acrossSlope.y, -acrossSlope.x);
    float bankRise = saturate(shore / kFlowBankReach);
    flow *= bankRise * bankRise * (3.0 - 2.0 * bankRise);
    float speed = length(flow);
    if (speed > kFlowFastest)
    {
        flow *= kFlowFastest / speed;
        speed = kFlowFastest;
    }
    float2 broadUv = p * 0.0137;
    float4 broad = tex2Dgrad(sLavaNoise, broadUv, acrossPixel * 0.0137, downPixel * 0.0137);

    // Large currents: stretches of hot lava along lines of equal across-stream position. Dividing by the speed turns the
    // difference in position into yards, so a current is about the same width all along its length.
    float line_ = abs(frac(acrossValue * kLavaCurrentLines + 0.5) - 0.5) / max(kLavaCurrentLines, 1.0e-3);
    float toCurrent = line_ / clamp(length(acrossSlope), 0.14, kFlowFastest);
    float currentWidth = kLavaCurrentWidth * (0.55 + 0.9 * broad.g);
    float current = (1.0 - smoothstep(currentWidth * 0.45, currentWidth, toCurrent)) * smoothstep(1.5, 9.0, shore);

    float heat = kLavaHeat + (broad.r - 0.5) * kLavaHeatPatches * 2.0 + current * kLavaCurrentHeat;
    heat -= (1.0 - smoothstep(0.0, max(kLavaShoreCool, 0.01), shore)) * 0.75;

    // Two layers carried along the flow, each restarting while the other shows, as in any flow map. Each place restarts at
    // its own moment, so there is no pulse across the whole lake, but only gently from place to place: where the restart
    // time changes quickly the pattern folds over and runs backwards. Blended before they are cut into plates; both read
    // the same pattern, apart only by how far the flow has carried it.
    float2 velocity = flow * kLavaFlow * (1.0 + current * kLavaCurrentSpeed);
    float restart = 0.3 * sin(dot(p, float2(0.047, 0.031)) + 1.3) + 0.2 * sin(dot(p, float2(-0.029, 0.053)) + 4.1);
    float seconds = Seconds();

    // The molten lava runs at full speed and changes over often.
    float phase = seconds / kMoltenPeriod + restart;
    float phaseA = frac(phase);
    float phaseB = frac(phase + 0.5);
    float blend = smoothstep(0.2, 0.8, abs(2.0 * phaseA - 1.0));
    float2 carried = velocity * kMoltenPeriod;
    float toSwirl = 1.0 / kLavaSwirlSize;
    float4 swirl = FlowingLayers(sLavaNoise, (p - carried * (phaseA - 0.5)) * toSwirl, (p - carried * (phaseB - 0.5)) * toSwirl, blend,
                                 acrossPixel * toSwirl, downPixel * toSwirl);

    // The crust is dragged along more slowly and keeps its shape far longer, so plates read as rafts, not boiling.
    phase = seconds / kCrustPeriod + restart * 1.4;
    phaseA = frac(phase);
    phaseB = frac(phase + 0.5);
    blend = smoothstep(0.32, 0.68, abs(2.0 * phaseA - 1.0));
    carried = velocity * kLavaCrustDrag * kCrustPeriod;
    float toCells = 1.0 / (kLavaPlateSize * kCellsPerTile);
    float4 cells = FlowingLayers(sLavaCells, (p - carried * (phaseA - 0.5)) * toCells, (p - carried * (phaseB - 0.5)) * toCells, blend,
                                 acrossPixel * toCells, downPixel * toCells);
    float ribbonRate = 0.019;
    float ribbons = tex2Dgrad(sLavaNoise, float2(acrossValue * ribbonRate, broad.a * 0.2 + 0.31),
                              float2(dot(acrossSlope, acrossPixel) * ribbonRate, 0.0),
                              float2(dot(acrossSlope, downPixel) * ribbonRate, 0.0)).g;

    // The crust: plates part along their cracks as the heat rises; the id makes some melt before their neighbours.
    float edge = cells.r * 0.7;
    float id = cells.g;
    float2 slope = cells.ba * 2.0 - 1.0;
    float h = clamp(heat, 0.0, 1.2);

    // Hot foam: the water foam's line drawing (FoamLines), given how stirred the lava is, showing hotter lava instead of
    // white. The lines fatten until they close up nearest what stirred it, and thin and break into pieces as it settles.
    // Just one lookup where nothing has stirred the lava.
    float2 gridUv = RippleUv(place);
    float gridFade = smoothstep(0.0, 0.03, min(min(gridUv.x, 1.0 - gridUv.x), min(gridUv.y, 1.0 - gridUv.y)));
    float stirred = tex2Dlod(sRipples, float4(gridUv, 0, 0)).r * gridFade;
    float foam = 0.0;
    [branch] if (stirred > 0.002)
    {
        float4 net = tex2Dlod(sLavaFoam, float4(p / (kLavaFoamCell * 12.0), 0, max(log2(footprint / kLavaFoamCell * 85.0), 0.0)));
        float lineEdge = net.r * 0.75;
        // The lines hold their shape for most of the cooling; only the last of it thins them away.
        float thick = smoothstep(0.0, 0.5, stirred);
        float width = kLavaFoamWidth * lerp(0.35, 1.0, net.g) * lerp(0.7, 1.0, thick) + 0.5 * smoothstep(0.68, 1.0, stirred);
        float aa = footprint / kLavaFoamCell * 0.75;
        float stroke = 1.0 - smoothstep(width - kLavaFoamSoftness - aa, width + aa, lineEdge);
        float cut = 1.0 - thick * lerp(1.0, 1.35, kLavaFoamBreakup) + kLavaFoamBreakup * 0.35 * (1.0 - thick);
        float keep = lerp(1.0, smoothstep(cut - 0.08, cut + 0.08, net.b), kLavaFoamBreakup);
        // Far off, where the lines are finer than a pixel, their average.
        float far_ = thick * (0.45 + 0.23 * thick);
        foam = lerp(stroke * keep, far_, smoothstep(0.12, 0.4, footprint / kLavaFoamCell)) * smoothstep(0.0, 0.15, stirred);
    }

    float open_ = kLavaCrack + h * h * 0.62 + (id - 0.5) * kLavaBreakup * h;
    float soft = max(0.012, footprint / kLavaPlateSize * 0.8);
    float crust = smoothstep(open_ - soft, open_ + soft, edge);
    float inside = edge - open_;
    // The border: a band of crust along the shore, its inner edge wandering and lit from below as a crack is. It comes from the
    // world: past where the ground map reaches there is none, as on land the glow fades there. Where the pool has no matching
    // terrain layer the shore is found on screen, as a thin line from the finest level: a width worked out on screen
    // changed with the camera, and coarser levels turned its edge to scallops that slid as the camera moved. Where it meets
    // nothing it lies past the wander of the border's edge, or the border showed in patches wherever the pattern ran low.
    float borderYards = shore;
    if (screenShore > 0.0)
    {
        float meets = saturate(tex2Dlod(sLavaHalo, float4((pixel + 0.5) * cTexelGrid.xy, 0, 0)).a * 4.8);
        borderYards = min(shore, lerp(kFarFromShore, kLavaBorderWidth * 2.5 * (1.0 - meets), screenShore));
    }
    float bankWander = (swirl.b - 0.5) * kLavaBorderWidth * 0.9;
    float borderAt = borderYards + bankWander;
    float border = BankCrust(borderAt);
    if (borderYards < shore)
    {
        // The screen contact spans up to two halo texels. At a grazing angle that
        // can cover several yards of lava. Keep only its share of the physical crust.
        float contactWidth = 2.0 * kHaloScale * max(pixelYards.x, pixelYards.y);
        float coverage = saturate(kLavaBorderWidth / max(0.0001, contactWidth));
        border = lerp(BankCrust(shore + bankWander), border, coverage);
    }
    crust = max(crust, border);
    // Where it is thickest the hot lava shows through the crust, and the crust closes back over as the lines thin.
    crust *= 1.0 - kLavaFoamMelt * smoothstep(0.4, 0.62, foam);

    // The molten lava, painted in soft bands, hottest along its currents and where cracks run.
    float v = -0.11 + swirl.r * 0.54 + (swirl.g - 0.5) * 0.3;
    v += (ribbons - 0.45) * kLavaRibbons * 0.62;
    v += (min(h, 1.0) - 0.5) * 0.3;
    // Veins run along live cracks; where the plates have melted away they fade, or the old cracks stay drawn as polygons.
    // (By heat alone: a plate's own melt value jumps across a crack, and stepped the veins' edge there.)
    v += kLavaVeins * exp(-edge * 10.0) * (1.0 - 0.85 * smoothstep(0.1, 0.34, kLavaCrack + h * h * 0.62));
    v += current * 0.17;

    // Each cell has at most one bubble: it swells slowly, then bursts and leaves a ring that spreads and fades. The speed
    // sets the swell; the burst keeps its own short time, since a slowed burst just looks soft.
    float2 bq = p / kBubbleCell;
    float2 bi = floor(bq);
    float4 br = tex2Dlod(sLavaRand, float4(frac((bi + 0.5) / 64.0 + 0.5), 0, 0));
    [branch] if (br.x < kLavaBubbles)
    {
        float period = (2.8 + 4.2 * br.w) / kLavaBubbleSpeed;
        float life = frac(seconds / period + br.w * 13.0);
        float burst = max(0.5, 1.0 - 1.3 / period);
        float radius = kLavaBubbleSize * (0.45 + 0.55 * br.w);
        float d = length(bq - bi - 0.5 - (br.yz - 0.5) * 0.45) * kBubbleCell;
        float grow = smoothstep(0.0, burst, life) * (1.0 - smoothstep(burst, burst + 0.2 / period, life));
        float grown = radius * grow + 1.0e-4;
        float dome = 1.0 - smoothstep(grown * 0.55, grown, d);
        float after = max(life - burst, 0.0) / (1.0 - burst);
        float ringWidth = max(0.1, footprint);
        float ring = step(burst, life) * (1.0 - after) * (1.0 - smoothstep(0.0, ringWidth + 0.13, abs(d - radius * (0.6 + after * 1.6))));
        v += (dome * (0.34 + 0.34 * (1.0 - smoothstep(0.0, grown, d))) * grow + ring * 0.5) * (1.0 - crust);
    }
    // Toward a hotter value that keeps some of the lava's own shading, painted in its own bands. As it settles it returns
    // to exactly what is below.
    v = lerp(v, max(v, kLavaFoamHeat + (v - 0.5) * 0.35), foam * smoothstep(0.0, 0.65, stirred));
    float vb = v * kLavaBands;
    float bandFloor = floor(vb);
    // Bands soften with distance, where hard steps would shimmer.
    float bandSoft = min(0.5, kLavaBandSoftness + footprint * 0.1);
    float painted = (bandFloor + smoothstep(0.5 - bandSoft, 0.5 + bandSoft, vb - bandFloor)) / kLavaBands;
    float3 colour = LavaRamp(painted);

    // The crust's top: chunky bevels catching the light, a tone for each plate, lit from below where it thins.
    float lit = dot(slope, normalize(kLavaSun.xy));
    float3 crustColour = lerp(kLavaCrustDark, kLavaCrustLight, smoothstep(0.34, 0.74, id) * 0.62);
    crustColour *= 1.0 + lit * kLavaBevel * 0.95;
    crustColour += kLavaCrustLight * max(lit, 0.0) * kLavaBevel * 0.55;
    float under = exp(-max(inside, 0.0) * kLavaUnderglowFalloff);
    // Darkest where it meets the land; glowing where it meets the molten lava.
    crustColour *= lerp(0.55, 1.0, saturate(smoothstep(0.0, kLavaBorderWidth * 0.7, borderYards) + 1.0 - min(border * 2.0, 1.0)));
    under = max(under, smoothstep(kLavaBorderWidth * 0.3, kLavaBorderWidth * 0.8, borderAt) * border);
    // Crust beside the foam is lit from below, as it is beside a crack.
    under = max(under, smoothstep(0.0, 0.45, foam) * smoothstep(0.0, 0.5, stirred));
    crustColour = lerp(crustColour, lerp(kLavaCool, kLavaWarm, under * under * 0.75), under * kLavaFringe);
    colour = lerp(colour, crustColour, crust);

    // Sparks in two sheets rising above the surface, found by following the ray up to each. Each spark is measured across
    // the ray, so it is round on screen from any angle, has its own moment to glow, and shrinks no smaller than a pixel but
    // dims instead. Near grazing the sheets fade out: seen edge-on they bunch into one flat layer.
    float sparks = 0.0;
    float glancing = smoothstep(0.1, 0.26, -ray.z);
    float rayPixel = 2.0 / (cViewportPx.w * abs(cViewToNdc.y));
    [branch] if (glancing > 0.0)
    {
        [unroll] for (int l = 0; l < 2; ++l)
        {
            float cycle = frac(seconds * 0.06 + l * 0.5);
            float below = -height - (cycle * 9.0 + 0.4);
            if (below > 0.0)
            {
                float toSheet = below / -ray.z;
                float2 wander = float2(sin(seconds * 0.31 + l * 2.1), cos(seconds * 0.23 + l * 4.0)) * 0.9;
                float2 q = (camera.xy + ray.xy * toSheet + wander) / kEmberCell + l * 3.7;
                float2 ci = floor(q);
                float4 er = tex2Dlod(sLavaRand, float4(frac((ci + 0.5) / 64.0 + 0.25), 0, 0));
                if (er.x < kLavaEmbers * 0.32)
                {
                    float3 offset = float3((ci + 0.5 + (er.yz - 0.5) * 0.6 - q) * kEmberCell, 0.0);
                    float3 acrossRay = offset - ray * dot(offset, ray);
                    float size = max(0.07, rayPixel * toSheet * 0.8);
                    float own = frac(cycle * 2.0 + er.w * 7.0);
                    float glow = smoothstep(0.0, 0.2, own) * (1.0 - smoothstep(0.45, 0.9, own));
                    float dimmed = 0.07 / size;
                    sparks += exp(-dot(acrossRay, acrossRay) / (size * size)) * glow * dimmed * dimmed *
                              smoothstep(0.0, 0.1, cycle) * (1.0 - smoothstep(0.6, 1.0, cycle)) * smoothstep(0.0, 2.5, below);
                }
            }
        }
    }
    colour += lerp(kLavaWarm, kLavaHot, 0.6) * sparks * glancing * smoothstep(0.1, 0.5, heat + 0.3) * 1.4;

    colour = MagmaColour(colour, fel);

    // The air between it and the camera, as the water's composite gives it (water_ps).
    float3 toCamera = -ray;
    if (cEnvironment.x > 0.5 && cSkyLayout.x > 1.5)
    {
        float viewDepth = ViewZ(surfaceDepth);
        float pixelSpan = cEnvironmentLight.y * viewDepth * viewDepth / max(abs(height), 1.0e-4);
        float farCoverage = saturate((FarClip() - viewDepth) / max(2.0 * pixelSpan, 1.0));
        float transmission = exp(cEnvironment.w * dist / max(cEnvironment.z, 1.0)) * farCoverage;
        colour = lerp(SurfaceEnvironmentColor(ray), colour, transmission);
    }
    else if (cGroundInfo.y < 0.5)
    {
        float fog = saturate((dist - cFogRange.x) / max(cFogRange.y - cFogRange.x, 1.0));
        colour = lerp(colour, cFogColor.rgb, fog * fog);
    }
    else
    {
        // The renderer's fog shows as the sky at the horizon behind what it hides.
        colour = lerp(colour, BeyondWaterColor(world, toCamera),
                      smoothstep(min(cFogRange.y, FarClip() * 0.6), FarClip() * 0.95, dist));
    }
    colour = lerp(colour, cFogColor.rgb, GameFog(dist, toCamera));

    LavaOutput output;
    output.color = float4(colour, 1.0);
    output.depth = surfaceDepth;
    return output;
}

LavaOutput main(float2 pixel : VPOS)
{
    float materialDepth = SampleRawDepth(sDepth, pixel);
    float surfaceDepth = abs(materialDepth);
    // Models drawn after the lava was captured may stand in front of it.
    float show = min(kNoLava - surfaceDepth, SampleRawDepth(sOpaqueDepth, pixel) - surfaceDepth);
    clip(show);
    // The rest only where there is lava: a clip alone need not stop the work on the pixels it discards.
    LavaOutput output;
    output.color = 0.0;
    output.depth = surfaceDepth;
    [branch] if (show >= 0.0)
        output = Shade(pixel, surfaceDepth, materialDepth < 0.0);
    return output;
}
