#include "water_palette.hlsli"
#include "common.hlsli"

float4 cWaterTimeWavesFoamClarity : WATER_C_REGISTER(WATER_SLOT_cWaterTimeWavesFoamClarity);
float4 cWaterReflectSpecTint : WATER_C_REGISTER(WATER_SLOT_cWaterReflectSpecTint);
float4 cToLight : WATER_C_REGISTER(WATER_SLOT_cToLight);
float4 cDirectColor : WATER_C_REGISTER(WATER_SLOT_cDirectColor);
float4 cAmbientColor : WATER_C_REGISTER(WATER_SLOT_cAmbientColor);
float4 cFogColor : WATER_C_REGISTER(WATER_SLOT_cFogColor);
float4 cFogRange : WATER_C_REGISTER(WATER_SLOT_cFogRange);
float4 cWorldToView0 : WATER_C_REGISTER(WATER_SLOT_cWorldToView0);
float4 cWorldToView1 : WATER_C_REGISTER(WATER_SLOT_cWorldToView1);
float4 cWorldToView2 : WATER_C_REGISTER(WATER_SLOT_cWorldToView2);
float4 cWorldToView3 : WATER_C_REGISTER(WATER_SLOT_cWorldToView3);
float4 cSunColor : WATER_C_REGISTER(WATER_SLOT_cSunColor);
float4 cShallowColor : WATER_C_REGISTER(WATER_SLOT_cShallowColor);
float4 cDeepColorDepth : WATER_C_REGISTER(WATER_SLOT_cDeepColorDepth);
float4 cFoamColor : WATER_C_REGISTER(WATER_SLOT_cFoamColor);
float4 cInsideAndReflection : WATER_C_REGISTER(WATER_SLOT_cInsideAndReflection);
float4 cRippleArea : WATER_C_REGISTER(WATER_SLOT_cRippleArea);
float4 cRippleGrid : WATER_C_REGISTER(WATER_SLOT_cRippleGrid); // w: material crest shape, zero in older captures
// Bottom blur, shallow-water veil, swell crest sharpness, open-water foam.
float4 cLookingDown : WATER_C_REGISTER(WATER_SLOT_cLookingDown);
float4 cAbyss : WATER_C_REGISTER(WATER_SLOT_cAbyss);
// x: the seabed debug view; y: 1 when the renderer's fog draws the distance haze (the client's is pushed out); z: 1
// when the world's water height is known somewhere on the ground map (sSeabed); w: how overcast the weather makes the
// water (see Renderer::Weathered).
float4 cGroundInfo : WATER_C_REGISTER(WATER_SLOT_cGroundInfo);
// x: RefractionDebug; y: scene exposure (zero in old captures means neutral); z: bits 4 filtered programmatic material,
// 8 wave moments (1 and 2 retired); w: 1 + open-water crest gain (old captures zero = neutral).
float4 cWaterDebug : WATER_C_REGISTER(WATER_SLOT_cWaterDebug);
float4 cWaveMomentSamples : WATER_C_REGISTER(WATER_SLOT_cWaveMomentSamples); // x: rows of the wave-moment filter (WaterWaveMomentSamples / 2)
float4 cGameFog : WATER_C_REGISTER(WATER_SLOT_cGameFog); // x: 1 WaterGameFog (only with WaterAtmosphere=2); after cEnvironmentPalette, unused elsewhere
// The game's linear distance fog by view depth, as it fogs its terrain and its own water (WaterGameFog). View depth
// from values the water shader keeps live anyway (distance, toCamera): no depth decode in its tight register budget.
float GameFog(float distance, float3 toCamera)
{
    float viewZ = distance * abs(dot(toCamera, cViewToWorld2.xyz)); // sign-free: either view handedness
    return cGameFog.x > 0.5 ? saturate((viewZ - cFogRange.x) / max(cFogRange.y - cFogRange.x, 1.0)) : 0.0;
}
// x: each ship's place along its path is read from the wake map (see MappedPlace), which only the water's own passes
// draw; slime and lava walk the path for each pixel (AlongPath). Diagnostics (tests use them as exact references):
// y the reflection is looked up for a flat surface (ReflectionSurfaceFlat); z what the water shows instead of itself
// (ReflectionView: 1 its reflection, 2 the mirrored image's colour, 3 that image's coverage, 4 the directional horizon).
float4 cLiquidOptions : WATER_C_REGISTER(WATER_SLOT_cLiquidOptions);
bool FlatReflectionSurface() { return cLiquidOptions.y > 0.5; }
float ReflectionView() { return cLiquidOptions.z; }

// Shared above-water material experiment. The control is bound before detail and reflection passes;
// underwater/slime and the unfinished planar producer retain their existing normal policy.
bool FilterDistantSwell()
{
#if defined(WATER_SURFACE_MATERIAL) && !defined(DEFERRED_SLIME)
    return fmod(floor(cWaterDebug.z * 0.25), 2.0) > 0.5;
#else
    return false;
#endif
}

// The producer and final shader share bit8; unsupported paths never publish this layout.
bool WaveMomentsOn()
{
    return FilterDistantSwell() && fmod(floor(cWaterDebug.z * 0.125), 2.0) > 0.5;
}

// Round each ship's path, the box its wake can reach: min x, min y, max x, max y (see Renderer's ShipWakeBoxes); with
// the wake map, the square the ship's part of the map covers, which holds the box.
float4 cShipBox[4] : WATER_C_REGISTER(WATER_SLOT_cShipBox);
// Ships (see Renderer::BuildWaterConstants): x, y, z and speed (0 for none); then heading x, y, half length, half beam
// (half length 0 for none).
float4 cBoat[4] : WATER_C_REGISTER(WATER_SLOT_cBoat);
float4 cBoatShape[4] : WATER_C_REGISTER(WATER_SLOT_cBoatShape);

sampler2D sSceneBefore : WATER_S_REGISTER(WATER_SLOT_sSceneBefore);
sampler2D sSceneWithWater : WATER_S_REGISTER(WATER_SLOT_sSceneWithWater);
sampler2D sWaterLevel : WATER_S_REGISTER(WATER_SLOT_sWaterLevel);
sampler2D sRememberedSky : WATER_S_REGISTER(WATER_SLOT_sRememberedSky);
// x: 2 when sRememberedSky holds the accepted environment, 0 when it holds the sky memory (see Renderer::BindSky).
// w: negative while no sky texture exists, including the first water frame after startup or Reset. Zero retains
// the normal texture-backed layout.
float4 cSkyLayout : WATER_C_REGISTER(WATER_SLOT_cSkyLayout);
// Current clean observed background + native palette, used only by above-water
// surface composition. These values are not completed-scene sky memory.
float4 cEnvironment : WATER_C_REGISTER(WATER_SLOT_cEnvironment); // active, field available, reference distance, log(T)
float4 cEnvironmentLight : WATER_C_REGISTER(WATER_SLOT_cEnvironmentLight); // body radius, per-pixel ray-height step, matching provider atmospheric glare, open-horizon blend
float4 cEnvironmentPalette[6] : WATER_C_REGISTER(WATER_SLOT_cEnvironmentPalette);

sampler2D sRipples : WATER_S_REGISTER(WATER_SLOT_sRipples);
sampler2D sSeabed : WATER_S_REGISTER(WATER_SLOT_sSeabed);
sampler2D sShore : WATER_S_REGISTER(WATER_SLOT_sShore);
#ifndef READS_SURFACE_DETAIL
// Each ship's path, a row each (see Renderer::BoatPathTexture): its position first and a point every few yards back,
// each x, y, how long ago the ship was there (seconds) and how fast it was going. (The water's composite and reflection
// pass read the wakes from the surface detail pass instead; see water_ps.)
sampler2D sBoatPaths : WATER_S_REGISTER(WATER_SLOT_sBoatPaths);
// The wake map (see Renderer::DrawShipWakeMap): each ship's place along its path (PathPlace) at points kWakeMapSize by
// kWakeMapSize over its square of cShipBox, a square each, one under the other. The first: how far behind the bow, how
// long ago the bow passed, how fast the ship went then and how far to the side (left of the path positive); the
// second: the way back along the path (x, y), how near the path comes and how long the known path is.
sampler2D sWakeMapPlace : WATER_S_REGISTER(WATER_SLOT_sWakeMapPlace);
sampler2D sWakeMapWay : WATER_S_REGISTER(WATER_SLOT_sWakeMapWay);
#endif
// How many points each side of a ship's part of the wake map has (Renderer's kWakeMapSize too).
static const float kWakeMapSize = 256.0;

static const float kWaterF0 = 0.02;
static const float kShoreFade = 0.25;
// How far (in yards, per unit of wave slope) the waves shift the view into the liquid.
// How far (yards, per unit of slope) the waves bend the view into the water.
static const float kRefractionReach = 1.0;
static const float kRefractionEase = 0.03;
static const float kSlimeFlowSpeed = 0.075;
static const float kSlimeBodyRefraction = 3.0;
static const float kSlimeBlobScale = 0.35;
static const float kSlimeSwirl = 1.6;
static const float kLargeWaveFade = 400.0;
static const float kRippleFade = 60.0;
static const float kSparkleFade = 18.0;
static const float kLargeWaveGrazingFade = 6.0;
static const float kRippleGrazingFade = 4.0;
static const float kHashPeriod = 289.0;
static const float3 kAbsorption = float3(0.45, 0.16, 0.11);
static const float kWaveSpeedScale = 2.2;
static const float kDirectLightShare = 0.6;
static const float kSameWaterBody = 0.75;
static const float kSkyLowest = 0.01;
static const float kSkySpan = 0.95;
static const float kSkyRows = 8.0;
static const float kMaxFresnel = 0.85;
static const float kReflectionEdgeFade = 0.08;
static const float kMinRayViewZ = 0.3;
static const float kSeabedEdgeFade = 6.0;
static const float kSlimeWaveScale = 0.6;

float Seconds() { return cWaterTimeWavesFoamClarity.x; }
float WaveStrength() { return cWaterTimeWavesFoamClarity.y; }
float FoamStrength() { return cWaterTimeWavesFoamClarity.z; }
float Clarity() { return cWaterTimeWavesFoamClarity.w; }
float ReflectionStrength() { return cWaterReflectSpecTint.x; }
float SpecularStrength() { return cWaterReflectSpecTint.y; }
float StockTint() { return cWaterReflectSpecTint.z; }
bool ScreenReflectionsOn() { return cWaterReflectSpecTint.w > 0.5; }

#include "reflection_common.hlsli"

// The sky memory's cell at a side and row (see sky_memory_ps).
float4 SkyMemoryCell(float side, float row)
{
    return tex2Dlod(sRememberedSky, float4((side + 0.5) / 2.0, (row + 0.5) / kSkyRows, 0, 0));
}

// The sky remembered at a height and side (see sky_memory_ps).
float4 SkyMemory(float side, float row)
{
    row = clamp(row, 0.0, kSkyRows - 1.0);
    float r0 = floor(row);
    float f = row - r0;
    return lerp(SkyMemoryCell(Unfolded(side), r0), SkyMemoryCell(Unfolded(side), min(r0 + 1.0, kSkyRows - 1.0)), f);
}

// The sky remembered in a direction: by its height, blended between the sides facing away from and towards the sun by
// how much it faces the sun (so a sunset glow lands on the sun's side); the fog colour until something has been seen.
float3 SurfaceEnvironmentColor(float3 direction)
{
    // Resolved native colours already include place/time/weather selection.
    // Optional adapter fields can refine the same gradient without image capture.
    float4 seen=0;
    [branch]if(cEnvironment.y>.5){
        float2 uv=clamp(OctahedralUv(direction),.5/512.0,1.0-.5/512.0);
        seen=tex2Dlod(sRememberedSky,float4(uv,0,0));
    }
    float3 colour=seen.rgb;
    [branch]if(seen.a<1.0)colour+=WaterPaletteGradient(direction,cEnvironmentPalette)*(1.0-saturate(seen.a));
    return colour;
}

float3 FallbackSkyColor(float3 direction)
{
#ifdef WATER_FILTERED_MATERIAL
    // This final shader is selected only with an accepted programmatic environment.
    return SurfaceEnvironmentColor(direction);
#else
    float3 result=cFogColor.rgb;
    [branch]if(cSkyLayout.x>1.5)result=SurfaceEnvironmentColor(direction);
    else {
        float row = sqrt(saturate((saturate(direction.z) - kSkyLowest) / kSkySpan)) * kSkyRows - 0.5;
        float2 across = direction.xy / max(length(direction.xy), 1.0e-4);
        float2 sun = cToLight.xy / max(length(cToLight.xy), 1.0e-4);
        float sunward = smoothstep(-0.6, 0.6, dot(across, sun));
        float4 away = SkyMemory(0.0, row);
        float4 toward = SkyMemory(1.0, row);
        float4 remembered = away.a > 0.01 && toward.a > 0.01 ? lerp(away, toward, sunward) : (away.a > 0.01 ? away : toward);
        // An unbound D3D9 sampler can return opaque black. That is not an observed
        // sky cell: use the supplied fog colour until memory exists.
        result=cSkyLayout.w>=0.0&&remembered.a>0.01?remembered.rgb/remembered.a:cFogColor.rgb;
    }
    return result;
#endif
}

float FarClip() { return cShallowColor.w; }

float3 SceneLight()
{
    return saturate(cAmbientColor.rgb + cDirectColor.rgb * kDirectLightShare);
}

float3 DepthGradient(float depth)
{
    float3 shallowToDeep = lerp(cShallowColor.rgb, cDeepColorDepth.rgb, saturate(depth / cDeepColorDepth.w));
    float beyond = max(depth - cDeepColorDepth.w, 0.0);
    return lerp(shallowToDeep, cAbyss.rgb, 1.0 - exp(-beyond / cAbyss.w));
}

// The sky seen in a reflection: always the sky remembered around the camera (taken from the real sky at each height),
// never picked off the screen in the reflected direction, which switched between the two wherever an island or a
// ship stood above on screen and drew a band down the water from every silhouette. What is not sky is reflected by
// the traced reflection.
float3 SkyColor(float3 direction)
{
    return FallbackSkyColor(direction);
}

// What far-off water fades into: the sky remembered at the horizon in that direction (picking it off the screen above
// the water's far edge switched colour at every island or pillar and drew a band down the water).
float3 BeyondWaterColor(float3 surface, float3 toCamera)
{
    float2 away = normalize(-toCamera.xy + 1.0e-5);
    return FallbackSkyColor(float3(away, 0.0));
}

float SunVisibility() { return cFogRange.z; }
float WaveTime() { return Seconds() * cFogRange.w * kWaveSpeedScale; }

float Hash(float2 cell)
{
    cell = cell - floor(cell / kHashPeriod) * kHashPeriod;
    return frac(sin(dot(cell, float2(127.1, 311.7))) * 43758.5453);
}

#ifdef NOISE_TEXTURE
// The noise's lattice read from a texture instead of worked out (NoiseTexture): each cell's Hash and the next cells'
// along x, along y and along both, a texel per cell over one period, drawn by noise_lattice_ps from Hash itself (so the
// same values). One fetch for what took four sines.
sampler2D sNoiseLattice : WATER_S_REGISTER(WATER_SLOT_sNoiseLattice);
float4 NoiseLattice(float2 cell)
{
    cell = cell - floor(cell / kHashPeriod) * kHashPeriod;
    return tex2Dlod(sNoiseLattice, float4((cell + 0.5) / kHashPeriod, 0, 0));
}
#endif

float2 NoiseSlope(float2 p, out float height)
{
    float2 cell = floor(p);
    float2 f = p - cell;
    float2 u = f * f * (3.0 - 2.0 * f);
    float2 du = 6.0 * f * (1.0 - f);
#ifdef NOISE_TEXTURE
    float4 lattice = NoiseLattice(cell);
    float a = lattice.x;
    float b = lattice.y;
    float c = lattice.z;
    float d = lattice.w;
#else
    float a = Hash(cell);
    float b = Hash(cell + float2(1, 0));
    float c = Hash(cell + float2(0, 1));
    float d = Hash(cell + float2(1, 1));
#endif
    float k = a - b - c + d;
    height = a + (b - a) * u.x + (c - a + k * u.x) * u.y;
    return du * float2(b - a + k * u.y, c - a + k * u.x);
}

float2 NoiseSlope(float2 p)
{
    float unused;
    return NoiseSlope(p, unused);
}

float2 Octave(float2 p, float2x2 rotation, float frequency, float2 drift, float amplitude)
{
    float2 q = mul(rotation, p) * frequency + drift * WaveTime();
    return mul(NoiseSlope(q), rotation) * (frequency * amplitude);
}

float ValueNoise(float2 p)
{
    float2 cell = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
#ifdef NOISE_TEXTURE
    float4 lattice = NoiseLattice(cell);
    return lerp(lerp(lattice.x, lattice.y, u.x), lerp(lattice.z, lattice.w, u.x), u.y);
#else
    return lerp(lerp(Hash(cell), Hash(cell + float2(1, 0)), u.x),
                lerp(Hash(cell + float2(0, 1)), Hash(cell + float2(1, 1)), u.x), u.y);
#endif
}

// Rain drops landing on water (a lake, the sea or a puddle): rings spreading from them, as the slope of the surface
// they make. Near the camera each splash the game draws has its ring on the ring map (drawn by
// Renderer::DrawRainRings); beyond it, rings come from drops at random: each cell of a grid has at most one at a time,
// fewer the lighter the rain, and two grids overlap.
#include "rain_rings.hlsli"

// The ring map: x, y of its corner, 1 / its size (yards), and how far from the camera it has rings (0 for none).
float4 cRingMap : WATER_C_REGISTER(WATER_SLOT_cRingMap);
sampler2D sRingMap : WATER_S_REGISTER(WATER_SLOT_sRingMap);

static const float kRingCell = 3.2;
static const float kRingShare = 0.8;
static const float kRingSlope = 0.4;
static const float kRingFade = 25.0;
static const float kRingMapBlend = 5.0;

float2 RandomRings(float2 world, float rain)
{
    float2 slope = 0;
    [unroll] for (int layer = 0; layer < 2; ++layer)
    {
        float2 q = world / kRingCell + layer * float2(0.37, 0.71);
        float2 cell = floor(q) + layer * 57.0;
        if (Hash(cell + 5.3) > rain * kRingShare)
            continue;
        float2 centre = 0.3 + 0.4 * float2(Hash(cell + 3.1), Hash(cell + 7.7));
        float phase = frac(Seconds() / kRingSeconds + Hash(cell + 11.3));
        slope += Ring((frac(q) - centre) * kRingCell, phase);
    }
    return slope;
}

float2 RingMapSlope(float2 world)
{
    float2 uv = (world - cRingMap.xy) * cRingMap.z;
    return all(uv > 0.0) && all(uv < 1.0) ? tex2Dlod(sRingMap, float4(uv, 0, 0)).rg : 0;
}

// strength: how strongly the rings bend the surface (1 = normal).
float2 RainRings(float3 world, float rain, float distance, float strength)
{
    float mapReach = cRingMap.w;
    if (distance >= kRingFade || (rain <= 0.0 && mapReach <= 0.0))
        return 0;
    float randomShare = mapReach > 0.0 ? smoothstep(mapReach - kRingMapBlend, mapReach, distance) : 1.0;
    float2 slope = mapReach > 0.0 ? RingMapSlope(world.xy) * (1.0 - randomShare) : 0;
    if (randomShare > 0.0 && rain > 0.0)
        slope += RandomRings(world.xy, rain) * randomShare;
    return slope * kRingSlope * strength * sqrt(1.0 - distance / kRingFade);
}

// The big, slow wave octaves only.
// How much of a wave octave with noise cells this size (yards) a pixel resolves, when it covers footprint yards of
// the surface (along the way it is stretched): all of it while a cell spans at least 4 pixels' worth, none by 2 (finer
// waves are sampled once a pixel at a point that jumps from wave to wave, and the rays they send scatter at random
// from one pixel to the next, stepping and speckling the reflection; real water averages them within the pixel).
float Resolved(float cell, float footprint)
{
    return footprint > 0.0 ? saturate(cell / footprint * 0.5 - 1.0) : 1.0;
}

// The yards of surface a pixel covers at a distance, stretched by how glancing the view is (elevation: the sine of the
// angle it is seen at).
float PixelFootprint(float distance, float elevation)
{
    float pixelAngle = 2.0 / (cViewToNdc.y * cViewportPx.w);
    return distance * pixelAngle / max(abs(elevation), 0.02);
}

// An octave faded out as far as a pixel covering footprint yards cannot resolve it.
float2 ResolvedOctave(float2 p, float2x2 rotation, float frequency, float2 drift, float amplitude, float footprint)
{
    return Octave(p, rotation, frequency, drift, amplitude) * Resolved(1.0 / frequency, footprint);
}

// Broad material slopes extend the spectrum to scales that distant pixels can resolve.
// These small slopes do not enter the legacy crest-height/refraction/caustic forcing.
float2 BroadSwellSlope(float2 p, float footprint)
{
    return ResolvedOctave(p, float2x2(1, 0, 0, 1), 0.004, float2(0.013, 0.009), 3.0, footprint) +
           ResolvedOctave(p, float2x2(0.8, -0.6, 0.6, 0.8), 0.008, float2(-0.017, 0.011), 2.5, footprint);
}

// Per-axis variance of cubic value-noise slope: 26/175 * (frequency*amplitude)^2.
// Octaves are treated as independent; this is an approximation to their noise correlations.
float LostSlopeVariance(float frequency, float amplitude, float rotationLengthSquared, float footprint)
{
    float kept = Resolved(1.0 / frequency, footprint);
    return (26.0 / 175.0) * frequency * frequency * amplitude * amplitude * rotationLengthSquared * (1.0 - kept * kept);
}

// Extend the swell along its crest. Keeping the cross-crest width preserves
// the original sampling cutoff and the steepness seen across the wave.
float SwellAlongCrest()
{
#ifdef DEFERRED_SLIME
    return 1.0;
#else
    return 1.0 - 0.80 * cRippleGrid.w;
#endif
}

float SwellSharpness()
{
#ifdef DEFERRED_SLIME
    return 0.0;
#else
    return cLookingDown.z;
#endif
}

// A rolling crest with curved fronts. The quadratic faces meet at a smooth
// tip, and meet the next trough with zero slope. Phase is in cycles.
static const float2 kCrestDirection = float2(0.125, 0.025);
static const float2 kCrestBend = float2(0.095, 0.17);
static const float kCrestMeander = 0.37;
static const float kCrestTip = 0.035;
static const float kSwellReplacement = 0.3;
static const float kCrestRelief = 1.45;
static const float kCrestFilterWidth = 1.4;
static const float kCrestMean = 0.62;
static const float kCrestVariation = 0.38;

float PeakedSwell(float2 p, out float2 slope)
{
    float bend = dot(p, kCrestBend) + 0.09 * WaveTime();
    float bendSin, bendCos;
    sincos(bend, bendSin, bendCos);
    float phase = dot(p, kCrestDirection) + 0.125 * WaveTime() + kCrestMeander * bendSin;
    float d = frac(phase + 0.5) - 0.5;
    float tip = max(kCrestTip - abs(d), 0.0);
    // The last two terms remove the mean height over a cycle.
    float height = d*d - abs(d) - tip*tip / (2.0*kCrestTip) + 1.0/6.0 + kCrestTip*kCrestTip/3.0;
    float face = 2.0 * d - clamp(d / kCrestTip, -1.0, 1.0);
    float strength = kCrestMean + kCrestVariation * bendSin;
    float2 phaseSlope = kCrestDirection + kCrestMeander * bendCos * kCrestBend;
    slope = strength * face * phaseSlope + height * kCrestVariation * bendCos * kCrestBend;
    return strength * height;
}

float UnresolvedSwellVariance(float footprint)
{
    float along = SwellAlongCrest();
    // The distant material consumes a scalar variance, half the trace of the stretched slope covariance.
    float swell = (LostSlopeVariance(0.11, 1.10, 1.0, footprint) +
                   LostSlopeVariance(0.27, 0.50, 1.0, footprint)) * (0.5 + 0.5 * along * along);
    float share = SwellSharpness();
    float background = 1.0 - kSwellReplacement * share;
    float kept = Resolved(kCrestFilterWidth, footprint);
    // Average unresolved face energy over both the crest cycle and its slowly varying strength.
    const float faceVariance = (1.0 - 2.0*kCrestTip) * (1.0 - 2.0*kCrestTip) / 3.0;
    const float mean2 = kCrestMean * kCrestMean, variation2 = kCrestVariation * kCrestVariation;
    const float directionEnergy = (mean2 + 0.5*variation2) * dot(kCrestDirection, kCrestDirection) +
        (0.5*mean2 + 0.125*variation2) * kCrestMeander*kCrestMeander * dot(kCrestBend, kCrestBend);
    float crest = share * kCrestRelief;
    return (swell * background * background + crest*crest * (0.5*faceVariance*directionEnergy) * (1.0 - kept*kept) +
            LostSlopeVariance(0.63, 0.22, 0.9925, footprint) +
            LostSlopeVariance(0.004, 3.0, 1.0, footprint) +
            LostSlopeVariance(0.008, 2.5, 1.0, footprint)) * WaveStrength() * WaveStrength();
}

// The swell's slope at p (seen by a pixel covering footprint yards, see Resolved; 0 for all of it).
// The swell's height at p (yards, about nothing on average): the same octaves as LargeWaveSlope, whose slopes are this
// height's (NoiseSlope is ValueNoise's own slope).
float OctaveHeight(float2 p, float2x2 rotation, float frequency, float2 drift, float amplitude)
{
    return (ValueNoise(mul(rotation, p) * frequency + drift * WaveTime()) - 0.5) * amplitude;
}

float LargeWaveHeight(float2 p)
{
    float2 swell = float2(p.x, p.y * SwellAlongCrest());
    float body = OctaveHeight(swell, float2x2(1.0, 0.0, 0.0, 1.0), 0.11, float2(0.110, 0.075), 1.10) +
           OctaveHeight(swell, float2x2(0.80, -0.60, 0.60, 0.80), 0.27, float2(-0.120, 0.160), 0.50);
    float share = SwellSharpness();
    [branch] if (share > 0.0)
    {
        float2 unusedSlope;
        float peak = PeakedSwell(p, unusedSlope);
        body += share * (kCrestRelief * peak - kSwellReplacement * body);
    }
    return body + OctaveHeight(p, float2x2(0.33, -0.94, 0.94, 0.33), 0.63, float2(0.110, -0.090), 0.22);
}

float2 LargeWaveSlope(float2 p, float footprint = 0.0)
{
    float2 swell = float2(p.x, p.y * SwellAlongCrest());
    float2 slope = ResolvedOctave(swell, float2x2(1.0, 0.0, 0.0, 1.0), 0.11, float2(0.110, 0.075), 1.10, footprint) +
                   ResolvedOctave(swell, float2x2(0.80, -0.60, 0.60, 0.80), 0.27, float2(-0.120, 0.160), 0.50, footprint);
    // Convert the noise gradient back to world-space slope for lighting and refraction.
    slope.y *= SwellAlongCrest();
    float share = SwellSharpness();
    [branch] if (share > 0.0)
    {
        float2 crestSlope;
        PeakedSwell(p, crestSlope);
        slope += share * (kCrestRelief * Resolved(kCrestFilterWidth, footprint) * crestSlope - kSwellReplacement * slope);
    }
    // The shorter crossing chop interrupts long highlights when looking along the swell.
    return slope + ResolvedOctave(p, float2x2(0.33, -0.94, 0.94, 0.33), 0.63, float2(0.110, -0.090), 0.22, footprint);
}

// The small surface ripples' slope at p (the waves' finer octaves riding the swell), seen by a pixel covering footprint
// yards (see Resolved).
float2 SmallRippleSlope(float2 p, float footprint, out float2 crestSlope)
{
    const float2x2 rotation = float2x2(-0.50, -0.87, 0.87, -0.50);
    float h;
    float2 derivative = NoiseSlope(mul(rotation, p) * 1.6 + float2(-0.45, -0.34) * WaveTime(), h);
    float2 band = mul(derivative, rotation) * (1.6 * 0.07) * Resolved(1.0 / 1.6, footprint);
    // Adding h*h - h to the height steepens the upper face and flattens the trough.
    crestSlope = band * (2.0 * h - 1.0);
    return band + ResolvedOctave(p, float2x2(0.97, 0.26, -0.26, 0.97), 3.7, float2(0.75, -0.62), 0.03, footprint);
}

// The waves' normal at p, seen from distance at elevation (the sine of the angle) by a pixel covering footprint yards
// of the surface (see Resolved).
// fineSlope: the small ripples' part of it, as they are faded here (for the foam, see FoamNormal).
#ifdef WATER_WAVE_MOMENT_DETAIL
float2 SurfaceSlope(float2 p, float distance, float elevation, float footprint, out float2 fineSlope, bool shapeCrests)
#else
float3 SurfaceNormal(float2 p, float distance, float elevation, float footprint, out float2 fineSlope)
#endif
{
#ifdef DEFERRED_SLIME
    // Thick slime moves in broader, softer waves than water.
    p *= kSlimeWaveScale;
    footprint *= kSlimeWaveScale;
#endif
    float largeFade = saturate(1.0 - distance / kLargeWaveFade) * saturate(elevation * kLargeWaveGrazingFade + 0.25);
    float rippleFade = saturate(1.0 - distance / kRippleFade) * saturate(elevation * kRippleGrazingFade);
    float2 slope = LargeWaveSlope(p, footprint);
    if (FilterDistantSwell())
    {
        largeFade = 1.0;
        slope += BroadSwellSlope(p, footprint);
    }
    float2 ripple = float2(0.0, 0.0);
    fineSlope = float2(0.0, 0.0);
    [branch] if (rippleFade > 0.0)
    {
        float2 crestSlope;
        ripple = SmallRippleSlope(p, footprint, crestSlope);
        fineSlope = ripple * rippleFade * WaveStrength();
#ifdef SURFACE_DETAIL_PASS
#ifdef WATER_WAVE_MOMENT_DETAIL
        if (shapeCrests)
#endif
            ripple += crestSlope * cWaveMomentSamples.y * saturate(1.0 - footprint * 5.0);
#endif
    }
    slope = slope * largeFade + ripple * rippleFade;
#ifndef DEFERRED_SLIME
    float sparkleFade = saturate(1.0 - distance / kSparkleFade) * saturate(elevation * kRippleGrazingFade);
    [branch] if (sparkleFade > 0.0)
    {
        float2 sparkle = ResolvedOctave(p, float2x2(0.60, 0.80, -0.80, 0.60), 9.0, float2(-1.10, 0.85), 0.009, footprint) +
                         ResolvedOctave(p, float2x2(-0.28, 0.96, -0.96, -0.28), 8.3, float2(0.95, 1.05), 0.008, footprint);
        slope += sparkle * sparkleFade;
    }
#endif
    slope *= WaveStrength();
#ifdef WATER_WAVE_MOMENT_DETAIL
    return slope;
#else
    return normalize(float3(-slope, 1.0));
#endif
}

#ifdef WATER_WAVE_MOMENT_DETAIL
float3 SurfaceNormal(float2 p, float distance, float elevation, float footprint, out float2 fineSlope)
{
    return normalize(float3(-SurfaceSlope(p, distance, elevation, footprint, fineSlope, true), 1.0));
}
#endif

float3 SurfaceNormal(float2 p, float distance, float elevation, float footprint)
{
    float2 fineSlope;
    return SurfaceNormal(p, distance, elevation, footprint, fineSlope);
}

static const float kRippleEdgeFade = 10.0;
static const float kMaxRippleSlope = 0.3;

// The ripples' slope at world (0 outside the grid, and fading towards its edge).
float2 RippleSlope(float2 world)
{
    float2 uv = (world - cRippleArea.xy) * cRippleArea.z;
    float2 slope = 0.0;
    // (One return at the end: Microsoft's compiler mishandles early ones inside branches.)
    if (cRippleArea.w > 0.0 && all(uv >= 0.0) && all(uv <= 1.0))
    {
        float texel = cRippleGrid.x;
        float left = tex2Dlod(sRipples, float4(uv - float2(texel, 0), 0, 0)).r;
        float right = tex2Dlod(sRipples, float4(uv + float2(texel, 0), 0, 0)).r;
        float down = tex2Dlod(sRipples, float4(uv - float2(0, texel), 0, 0)).r;
        float up = tex2Dlod(sRipples, float4(uv + float2(0, texel), 0, 0)).r;
        slope = float2(right - left, up - down) / (2.0 * cRippleGrid.y) * cRippleArea.w;
        slope = clamp(slope, -kMaxRippleSlope, kMaxRippleSlope);
        slope *= saturate(min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y)) * kRippleEdgeFade);
    }
    return slope;
}

float3 WithRipples(float3 normal, float2 world)
{
    return normalize(normal + float3(-RippleSlope(world), 0.0));
}

// How far the ripples (a swimmer's, rain's) lift the surface at world (yards; 0 outside the grid).
float RippleHeight(float2 world)
{
    float2 uv = (world - cRippleArea.xy) * cRippleArea.z;
    float edge = saturate(min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y)) * kRippleEdgeFade);
    return all(uv >= 0.0) && all(uv <= 1.0) ? tex2Dlod(sRipples, float4(uv, 0, 0)).r * cRippleArea.w * edge : 0.0;
}

// Foam floats on the surface, so it rides the waves under it: where the ripples lift the water the foam is drawn where
// the lifted surface is seen from the camera (the view meets it sooner, nearer the camera), and it is lit by the slope
// it lies on (see FoamNormal). Only the ripples and the small surface ripples; the swell's slope pushed it about and
// wobbled its edge. (Pushed sideways by the slope, as the view into the water is bent, it looked refracted, under the
// water.) toCamera: from the surface towards the camera.
float2 FoamPoint(float2 world, float3 toCamera)
{
    return world + toCamera.xy / max(toCamera.z, 0.2) * RippleHeight(world);
}

// The slope the foam lies on (see FoamPoint): the small ripples' (SurfaceNormal's fineSlope) and the ripples'.
float3 FoamNormal(float2 fineSlope, float2 rippleSlope)
{
    return normalize(float3(-(fineSlope + rippleSlope), 1.0));
}

// How near (yards) the ground map's height a point of the solid world lies for it to count as the ground itself, rather
// than something standing on it (a character, a plant, a post).
static const float kOnGround = 0.3;

// The ground map's cell (yards): its area matches the ripple grid's, at a quarter of the resolution.
float GroundCellSize() { return cRippleGrid.y * 4.0; }

float2 RippleUv(float2 world)
{
    return (world - cRippleArea.xy) * cRippleArea.z;
}

bool InsideRippleArea(float2 uv)
{
    return all(uv >= 0.0) && all(uv <= 1.0);
}

float2 SeabedDepth(float2 world, float surfaceHeight)
{
    float2 uv = RippleUv(world);
    if (!InsideRippleArea(uv))
        return 0;
    // Trusted less towards the edge of the area kept around the camera, so what depends on it fades out there. The
    // water's own height from the world data is used where known; the height passed in only where it is not.
    float4 heights = tex2Dlod(sSeabed, float4(uv, 0, 0));
    float edge = saturate(min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y)) * kSeabedEdgeFade);
    float surface = lerp(surfaceHeight, heights.b, saturate(heights.a));
    return float2(surface - heights.r, heights.g * edge);
}

// The water surface at this point of the drawn mesh, placed on the true water height: from the world data (each body
// of water at its own height), or the level measured on screen where the world gives none. The drawn tiles sit at
// slightly different heights that change as the camera moves; one level for everything would switch between
// neighbouring bodies of water as the camera moved and shift everything built on the surface.
// The water's height at p: the world's where it gives one, otherwise the nearest known water's; beyond the square kept
// around the camera, the height at its edge carries on. (One level for everything outside where the world gives a
// height mixed in other water nearby and showed the square's edge as a step in the water.) With no water height known
// anywhere around, the level measured from the water on screen; false with none at all.
bool WaterHeightAt(float2 p, out float height)
{
    height = 0.0;
    if (cGroundInfo.z > 0.5)
    {
        float halfCell = 2.0 * cRippleGrid.x;
        float2 uv = clamp(RippleUv(p), halfCell, 1.0 - halfCell);
        height = tex2Dlod(sSeabed, float4(uv, 0, 0)).b;
        return true;
    }
    float4 level = tex2Dlod(sWaterLevel, float4(0.5, 0.5, 0, 0));
    height = level.r;
    return level.g > 0.5;
}

float3 FlattenedSurface(float3 world)
{
    float3 camera = cViewToWorld3.xyz;
    float3 ray = world - camera;
    if (abs(ray.z) < 1.0e-3)
        return world;
    float height;
    if (!WaterHeightAt(world.xy, height))
        return world;
    // Surfaces far from that height (another body of water) are left as drawn, eased rather than cut off, so a tile
    // that happens to be a little off never shows a hard edge.
    float keep = 1.0 - smoothstep(kSameWaterBody * 0.6, kSameWaterBody * 1.4, abs(world.z - height));
    // Beyond this camera-centred map its clamped height invented a flat
    // continuation of rising rivers. Leave the native surface there instead.
    if (cGroundInfo.z>.5) {
        float2 uv=RippleUv(world.xy);
        float edge=min(min(uv.x,1-uv.x),min(uv.y,1-uv.y));
        keep*=smoothstep(2*cRippleGrid.x,8*cRippleGrid.x,edge);
    }
    if (keep <= 0.0)
        return world;
    float t = (height - camera.z) / ray.z;
    return t > 0.0 ? lerp(world, camera + ray * t, keep) : world;
}

// The slime's body pattern: slow, broad blobs of brighter lime drifting through the darker green, warped so they
// swirl rather than scroll. Made here rather than taken from the client's texture, whose coarse pattern only
// smudges when softened.
float3 SlimeBodyPattern(float2 p, float3 light)
{
    float t = Seconds() * kSlimeFlowSpeed;
    float2 warp = float2(ValueNoise(p * 0.21 + float2(t, 0.0)), ValueNoise(p * 0.21 + float2(5.2, t * 0.8))) - 0.5;
    float2 q = p * kSlimeBlobScale + warp * kSlimeSwirl;
    float blobs = ValueNoise(q + float2(-t * 0.6, t * 0.4)) * 0.65 + ValueNoise(q * 2.3 + float2(t * 0.5, t * 0.7)) * 0.35;
    float patch = smoothstep(0.45, 0.8, blobs);
    float3 dark = cDeepColorDepth.rgb;
    float3 bright = lerp(cShallowColor.rgb, cFoamColor.rgb, 0.35);
    return lerp(dark, bright, patch) * light;
}

// Shoreline waves and foam, shared by the surface seen from above and from below.
static const float kWaveReach = 7.0;
static const float kWaveLength = 3.0;
static const float kWavePeriod = 4.0;
static const float kFrontSharpness = 0.08;
static const float kFrontTrail = 0.12;
static const float kBreakDistance = 5.25;
static const float kWashFade = 0.3;
static const float kWashRise = 0.08;
static const float kWashWidth = 0.7;
static const float kWashStrength = 0.8;
static const float kFoamAmount = 1.9;
static const float kWaveTimingDrift = 0.12;
static const float kSmallestWave = 0.55;
static const float kOpenWaterWidening = 2.5;
static const float kWaveSlope = 0.18;
static const float kProfileStep = 0.01;
static const float kFoamStartDepth = 0.03;
// The shore distance is measured between cell centres; the waterline lies about half a cell nearer.
static const float kShoreCellOffset = 0.3;

#ifndef DEFERRED_SLIME
// The foam's line network was made half as large again and half as fast (kFoamCellSize, kFoamDrift); its other
// patterns follow it (the open water's dapple and stretches, the grain, the warps of the rim
// round what stands in the water, the ripples' stretches): drawn this much larger, moving this much slower. (Left at
// their first size and pace, they swept through the slower network and tangled it.)
static const float kFoamPatternScale = 1.5;
static const float kFoamPatternPace = 0.5;
float FoamPatternScale() { return kFoamPatternScale; }
float FoamPatternSeconds() { return Seconds() * kFoamPatternPace; }

// Foam drawn as line work (designed in tools/foam_lab.html, whose "Copy shader values" these settings come from):
// curved filaments that kink and taper, joining into loose networks where the foam is thick, breaking into separate
// squiggles as it thins, rounded where they meet as bubble walls are. One material for every source of foam (the
// shore's waves, ships' wakes, what stands in the water): a source gives only how thick the foam is and the axis it
// streams along. (A soft noise shown past a threshold read as cloud, a texture with no drawing in it.)
static const float kFoamCellSize = 1.97;
static const float kFoamLineWidth = 0.105;
static const float kFoamLineSoftness = 0.150;
static const float kFoamTaper = 0.84;
static const float kFoamCornerRounding = 0.30;
static const float kFoamBend = 0.00;
static const float kFoamKink = 0.80;
static const float kFoamStretch = 1.60;
static const float kFoamBreakup = 0.67;
static const float kFoamFill = 0.23;
static const float kFoamTexture = 0.20;
static const float kFoamTint = 1.00;
static const float kFoamDrift = 1.35;
// How much of the foam shows at most.
static const float kFoamMost = 0.70;
// The colour the foam is tinted towards (a light water colour), as far as kFoamTint says.
static const float3 kFoamTintColour = float3(0.59, 0.89, 0.92);
// From how large a pixel's footprint (in cells) the lines give way to their average, and by how large they have (fine
// lines seen from far off shimmered).
static const float kFoamLinesFadeFrom = 0.12;
static const float kFoamLinesFadeTo = 0.4;

float2 FoamHash2(float2 p)
{
    p = p - floor(p / kHashPeriod) * kHashPeriod;
    return frac(sin(float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)))) * 43758.5453);
}

// The lesser of a and b, blended over k where they are close: where cell walls meet, their distances are close and
// the lines swell into rounded fillets.
float FoamSmoothMin(float a, float b, float k)
{
    float h = max(k - abs(a - b), 0.0) / max(k, 1.0e-5);
    return min(a, b) - h * h * k * 0.25;
}

// r measured with the axis `along` shortened by stretch (the network's cells, and so its lines, drawn out along it).
float2 FoamStretched(float2 r, float2 along, float stretch)
{
    return float2(dot(r, along) / stretch, dot(r, float2(-along.y, along.x)));
}

#ifdef FOAM_POINT_TEXTURE
// Where each cell's point wanders (FoamPointTexture): the sine and cosine of the phase FoamHash2 gives it, x then y,
// drawn by noise_lattice_ps from FoamHash2 itself, so a point is 0.5 + 0.42 sin(t + phase) without the hash's sines.
sampler2D sFoamPoints : WATER_S_REGISTER(WATER_SLOT_sFoamPoints);
float2 WanderingPoint(float2 cell, float sinT, float cosT)
{
    cell = cell - floor(cell / kHashPeriod) * kHashPeriod;
    float4 phase = tex2Dlod(sFoamPoints, float4((cell + 0.5) / kHashPeriod, 0, 0));
    return 0.5 + 0.42 * (sinT * phase.yw + cosT * phase.xz);
}
#endif

// The distance (in cells) from x to the nearest wall of a cellular pattern whose points wander slowly (t): a network
// of curved lines. Stretched along `along` by measuring distance that way as shorter; the points stay where they are,
// so where the stretch turns nothing tears.
float FoamCellEdge(float2 x, float t, float2 along, float stretch)
{
    float2 n = floor(x);
    float2 f = frac(x);
    float2 nearestCell = 0.0;
    float2 nearest = 0.0;
    float best = 1.0e9;
#ifdef FOAM_POINT_TEXTURE
    float sinT = sin(t);
    float cosT = cos(t);
#endif
    // The nearest point lies within a cell either way unless the pattern is stretched, which measures some further off
    // as nearer (searching two cells either way always cost half the network's time).
    int reach = stretch > 1.2 ? 2 : 1;
    [loop] for (int j = -reach; j <= reach; ++j)
    {
        [loop] for (int i = -reach; i <= reach; ++i)
        {
            float2 g = float2(i, j);
#ifdef FOAM_POINT_TEXTURE
            float2 o = WanderingPoint(n + g, sinT, cosT);
#else
            float2 o = 0.5 + 0.42 * sin(t + 6.2831853 * FoamHash2(n + g));
#endif
            float2 r = FoamStretched(g + o - f, along, stretch);
            float d = dot(r, r);
            if (d < best)
            {
                best = d;
                nearest = r;
                nearestCell = g;
            }
        }
    }
    float edge = 1.0e9;
    // The walls lie between the nearest point and the points round it: those of the cells next to its own (3x3) nearly
    // always. (Two cells out, 5x5, caught the rare wall the nearer ones miss, at nearly three times the cost.)
    int around = 1;
    [loop] for (int l = -around; l <= around; ++l)
    {
        [loop] for (int m = -around; m <= around; ++m)
        {
            float2 g = nearestCell + float2(m, l);
#ifdef FOAM_POINT_TEXTURE
            float2 o = WanderingPoint(n + g, sinT, cosT);
#else
            float2 o = 0.5 + 0.42 * sin(t + 6.2831853 * FoamHash2(n + g));
#endif
            float2 r = FoamStretched(g + o - f, along, stretch);
            float2 between = r - nearest;
            if (dot(between, between) > 1.0e-5)
                edge = FoamSmoothMin(edge, dot(0.5 * (nearest + r), normalize(between)), kFoamCornerRounding);
        }
    }
    return edge;
}

// A flow's axis without its sign (the stretch cares only for the axis): the direction's angle doubled. Flows of
// opposite sign agree, so they blend smoothly where they meet.
float2 FoamAxis(float2 direction)
{
    float2 d = direction / max(length(direction), 1.0e-5);
    return float2(d.x * d.x - d.y * d.y, 2.0 * d.x * d.y);
}

// The foam at p (yards), where it is density thick (0..1), streaming along flow (FoamAxis, weighted by how strongly),
// seen by a pixel covering footprint yards of the surface.
float FoamLines(float2 p, float density, float2 flow, float footprint)
{
    float shown = 0.0;
    // (One return at the end: Microsoft's compiler mishandles early ones inside branches.)
    // (Only where it shows at all: the network is the costliest part of the water.)
    [branch] if (density > 0.02)
    {
        // Far off, where the lines are finer than a pixel, their average; where they have given way to it wholly, the
        // network is not worked out at all.
        float averaged = smoothstep(kFoamLinesFadeFrom, kFoamLinesFadeTo, footprint / kFoamCellSize);
        float far = saturate(density * (0.45 + kFoamFill * density));
        float lines = far;
        [branch] if (averaged < 1.0)
        {
            float t = Seconds();
            float slow = t * 0.08 * kFoamDrift;
            float2 q = p / kFoamCellSize;
            float2 bend =
                float2(ValueNoise(q * 0.45 + float2(3.1, slow)), ValueNoise(q * 0.45 + float2(slow, 8.7))) - 0.5;
            q += bend * kFoamBend * 1.6;
            float2 kink = float2(abs(ValueNoise(q * 1.3 + 11.0 + slow) - 0.5),
                                 abs(ValueNoise(q * 1.3 + 23.0 - slow) - 0.5)) - 0.25;
            q += kink * kFoamKink * 1.2;
            float strength = min(length(flow) / 0.3, 1.0);
            float angle = 0.5 * atan2(flow.y, flow.x + 1.0e-9);
            float edge = FoamCellEdge(q, t * 0.35 * kFoamDrift, float2(cos(angle), sin(angle)),
                                      1.0 + (kFoamStretch - 1.0) * strength);
            float swell = lerp(1.0 - kFoamTaper, 1.0, smoothstep(0.2, 0.8, ValueNoise(q * 0.9 + 41.0 + slow)));
            float width = kFoamLineWidth * swell * lerp(0.7, 1.0, density);
            // Smoothed by the pixel's footprint on the surface (in cells), not the change between neighbouring pixels,
            // which is not to be had inside this branch.
            float aa = footprint / kFoamCellSize * 0.75;
            float stroke = 1.0 - smoothstep(width - kFoamLineSoftness - aa, width + aa, edge);
            float pieces = ValueNoise(q * 0.55 - float2(slow * 0.7, 0.0) + 57.0);
            float cut = 1.0 - density * lerp(1.0, 1.35, kFoamBreakup) + kFoamBreakup * 0.35 * (1.0 - density);
            float keep = lerp(1.0, smoothstep(cut - 0.08, cut + 0.08, pieces), kFoamBreakup);
            float fill = kFoamFill * density * density * lerp(0.6, 1.0, ValueNoise(q * 1.7 + 71.0 + slow));
            lines = lerp(saturate(stroke * keep * lerp(0.55, 1.0, density) + fill), far, averaged);
        }
        // Faded out as the density falls to nothing, so it has no edge where it ends.
        shown = lines * smoothstep(0.0, 0.5, density);
    }
    return shown;
}

// Lines of foam along the tops of waves, drawn as things of their own over the foam (see tools/foam_lab.html): how
// strongly, how wide (half-width, yards), how soft their edges (in widths) and how much they break into pieces; and
// from what share of a wave's full size they start and have fully come (the lab measures this as a height, but its
// waves are made up; the game's differ in height from source to source).
static const float kCrestLine = 1.00;
static const float kCrestLineWidth = 0.180;
static const float kCrestLineSoftness = 1.00;
static const float kCrestLineBreakup = 1.00;
static const float kCrestLineFrom = 0.35;
static const float kCrestLineFull = 0.7;
// How much the crest lines kink as they wander across their waves (see CrestBend): their own, half the network's first
// (they follow the waves' shape either way; this only breaks them up).
static const float kCrestLineKink = 0.50;

// How far (yards) a crest line is pushed across its wave at p, by the foam's own warps, so it wanders and kinks as the
// foam's lines do rather than running ruled along the wave.
float CrestBend(float2 p)
{
    float slow = Seconds() * 0.08 * kFoamDrift;
    float2 q = p / kFoamCellSize;
    float kink = abs(ValueNoise(q * 1.3 + 11.0 + slow) - 0.5) - 0.25;
    float bend = ValueNoise(q * 0.45 + float2(3.1, slow)) - 0.5;
    return (kink * kCrestLineKink + bend * kFoamBend) * 0.5 * kFoamCellSize;
}

// A crest line at p: across is how far (yards) p lies from the crest across the wave (bent, see CrestBend); size the
// wave's share of its full size there; halfWidth the line's half-width at its thickest (yards); footprint the yards of
// surface a pixel covers.
float CrestLine(float2 p, float across, float size, float halfWidth, float footprint)
{
    float slow = Seconds() * 0.08 * kFoamDrift;
    float2 q = p / kFoamCellSize;
    float width = halfWidth * lerp(1.0 - kFoamTaper, 1.0, smoothstep(0.2, 0.8, ValueNoise(q * 0.6 + 41.0 + slow)));
    float aa = min(footprint * 0.75, width);
    float stroke = 1.0 - smoothstep(width * (1.0 - kCrestLineSoftness), width * (1.0 + kCrestLineSoftness) + aa,
                                    abs(across));
    // Where a pixel covers more than the line is wide, the share of it the line covers, spread evenly (drawn crisp,
    // it landed on or off the line from one frame to the next as the view moved, and shimmered far off).
    float spread = saturate(2.0 * width / max(footprint, 1.0e-4)) * (1.0 - smoothstep(0.0, footprint, abs(across)));
    stroke = lerp(stroke, spread, smoothstep(width, 3.0 * width, footprint));
    float pieces = lerp(1.0, smoothstep(0.3, 0.42, ValueNoise(q * 0.3 + 91.0 - slow)), kCrestLineBreakup);
    return stroke * pieces * smoothstep(kCrestLineFrom, kCrestLineFull, size) * kCrestLine;
}

// The ripples' crests (a swimmer's wake and rings, rain's rings; see RippleHeight): a line along each, and foam
// gathering where they are high and sharp, streaming along them (see tools/foam_lab.html: crest lines, crest foam). The
// crest is found from how the height curves round the point (it curves down most sharply across the crest), as the lab
// does for its waves; the ripples have no shape known ahead. Only sharp crests foam: the grid also carries the shore's
// swell (ripple_ps: ShoreForcing), as high as a swimmer's ripples or more but long and gentle (over a crest it curves
// at most about 0.5 per yard, a swimmer's 2.5 to 4), and its foam is the shore's own (ShoreFoam). Their lines are
// finer than the shore's (a ripple is a fraction of a yard across) and not bent (which way is across flips from ring
// to ring, and bent that way the line would split).
// The height (yards) at which a ripple's crest line and foam have fully come; they start at kCrestLineFrom of it. How
// sharply (per yard) a crest curves where its foam starts and has fully come.
static const float kRippleCrestFull = 0.08;
static const float kRippleFoamCurveFrom = 0.6;
static const float kRippleFoamCurveFull = 1.5;
static const float kRippleCrestWidth = 0.05;
// How much foam the ripples' crests gather at most (the lab's crest foam).
static const float kCrestFoam = 1.00;

void RippleCrests(float2 p, float footprint, out float crestLine, out float density, out float2 flow)
{
    crestLine = 0.0;
    density = 0.0;
    flow = 0.0;
    float2 uv = RippleUv(p);
    [branch] if (cRippleArea.w > 0.0 && InsideRippleArea(uv))
    {
        float e = cRippleGrid.y;
        float h = RippleHeight(p);
        // Below kCrestLineFrom of a ripple's full height there is neither a crest line nor crest foam (see below), so
        // there is nothing to look for.
        [branch] if (h > kCrestLineFrom * kRippleCrestFull)
        {
            float east = RippleHeight(p + float2(e, 0.0));
            float west = RippleHeight(p - float2(e, 0.0));
            float north = RippleHeight(p + float2(0.0, e));
            float south = RippleHeight(p - float2(0.0, e));
            float twist = (RippleHeight(p + float2(e, e)) - RippleHeight(p + float2(e, -e)) -
                           RippleHeight(p + float2(-e, e)) + RippleHeight(p - float2(e, e))) / (4.0 * e * e);
            float2 slope = float2(east - west, north - south) / (2.0 * e);
            float curveX = (east - 2.0 * h + west) / (e * e);
            float curveY = (north - 2.0 * h + south) / (e * e);
            float spread = sqrt(0.25 * (curveX - curveY) * (curveX - curveY) + twist * twist);
            float curve = 0.5 * (curveX + curveY) - spread;
            float2 a = float2(twist, curve - curveX);
            float2 b = float2(curve - curveY, twist);
            float2 across = dot(a, a) > dot(b, b) ? a : b;
            across = dot(across, across) > 1.0e-12 ? normalize(across) : float2(1.0, 0.0);
            float fromCrest = curve < -1.0e-3 ? abs(dot(slope, across)) / -curve : 10.0;
            // Only where it truly curves over a crest, and where the slope lies across it (on a flank the sharpest
            // curve can run along the ripple, and measured that way the slope looks like nothing, as on a crest).
            float sideways = length(slope - across * dot(slope, across));
            float sharp = smoothstep(0.5, 2.0, -curve) *
                          (1.0 - smoothstep(1.0, 2.0, sideways / (max(-curve, 1.0e-3) * kRippleCrestWidth)));
            crestLine = CrestLine(p, fromCrest, saturate(h / kRippleCrestFull), kRippleCrestWidth, footprint) * sharp;
            float steepness = length(slope);
            float stretches = smoothstep(
                0.2, 0.65, ValueNoise(p / FoamPatternScale() * 0.45 + float2(FoamPatternSeconds() * 0.03, 0.0) + 29.0));
            density = kCrestFoam * smoothstep(kCrestLineFrom, kCrestLineFull, h / kRippleCrestFull) *
                      smoothstep(kRippleFoamCurveFrom, kRippleFoamCurveFull, -curve) * stretches;
            flow = FoamAxis(float2(-slope.y, slope.x)) * density * smoothstep(0.0, 0.2, steepness);
        }
    }
}

// Open water's foam (see tools/foam_lab.html, the open water scene). Some stretches of the swell, chosen by a slow
// pattern hundreds of yards across, gather foam on their tops; and a sparse dapple of squiggles drifts across the deeper water, drawn out into streaks so its clusters join through thinner
// trails. How large the chosen stretches are (per yard) and how many are chosen; from what share of the swell's full
// height its tops foam and have fully; the dapple's strength and the water depth (yards) over which it comes in.
static const float kChosenSwellScale = 0.006;
static const float kChosenSwellFrom = 0.55;
static const float kChosenSwellFull = 0.78;
static const float kSwellCrestFrom = 0.3;
static const float kSwellCrestFull = 0.75;
static const float kDapple = 0.5;
static const float kDappleDepthFrom = 1.0;
static const float kDappleDepthFull = 3.0;
// The chosen swells' foam fades out by this far from the camera (yards); and open water's foam is none at all past
// this far.
static const float kChosenSwellReach = 150.0;
static const float kOpenWaterReach = 400.0;
// From how large a pixel's footprint on the surface (yards) open water's foam fades, and by how large it has gone.
static const float kOpenWaterFootprintFrom = 1.0;
static const float kOpenWaterFootprintTo = 2.5;

// amount: how much foam open water gathers (OpenWaterFoam in water.ini).
void OpenWaterFoam(float2 p, float depth, float distance, float footprint, float amount, out float density,
                   out float2 flow)
{
    flow = 0.0;
    density = 0.0;
    // (All in a branch, so the compiler cannot move any of it earlier, into the ships' wakes' path loop, where it ran
    // the shader out of registers.)
    // Faded out where a pixel covers more of the surface than the foam's patterns can show (a few yards; sampled once a
    // pixel there, they shimmered as the view moved).
    // (Its patterns' own size and pace (FoamPatternScale): q is where p lies among them, a footprint in their terms.)
    float scale = FoamPatternScale();
    float2 q = p / scale;
    float patternFootprint = footprint / scale;
    float resolved = 1.0 - smoothstep(kOpenWaterFootprintFrom, kOpenWaterFootprintTo, patternFootprint);
    [branch] if (distance < kOpenWaterReach && resolved * amount > 0.0)
    {
        float chosen = smoothstep(kChosenSwellFrom, kChosenSwellFull,
                                  ValueNoise(p * kChosenSwellScale + float2(0.0, Seconds() * 0.003) + 5.0)) *
                       (1.0 - smoothstep(kChosenSwellReach * 0.6, kChosenSwellReach, distance));
        float t = FoamPatternSeconds();
        // The dapple: a broad pattern drawn out by a slow warp, thinned (not cut) by a finer one.
        float2 streak = float2(ValueNoise(q * 0.15 + float2(t * 0.02, 3.0)), ValueNoise(q * 0.15 + float2(9.0, t * 0.02))) - 0.5;
        // (The finer pattern gives way to its average where a pixel covers more than its cells, or it shimmered.)
        float fine = lerp(smoothstep(0.3, 0.7, ValueNoise(q * 1.1 - float2(0.0, t * 0.05) + 13.0)), 0.5,
                          smoothstep(0.4, 1.2, patternFootprint));
        float dapple = smoothstep(0.45, 0.85, ValueNoise((q + streak * 3.0) * float2(0.2, 0.34) + float2(t * 0.03, 0.0) + 19.0)) *
                       lerp(0.45, 1.0, fine) * kDapple * smoothstep(kDappleDepthFrom, kDappleDepthFull, depth);
        density = dapple;
        [branch] if (chosen > 0.001)
        {
            float full = 0.91 * WaveStrength();
            float share = saturate(LargeWaveHeight(p) / max(full, 1.0e-3));
            // Below kSwellCrestFrom of the swell's full height its tops have no foam.
            [branch] if (share > kSwellCrestFrom)
            {
                float stretches = smoothstep(0.2, 0.65, ValueNoise(q * 0.45 + float2(t * 0.03, 0.0) + 29.0));
                float tops = kCrestFoam * smoothstep(kSwellCrestFrom, kSwellCrestFull, share) * stretches * chosen;
                // Crest concentration scales linearly; deformation scales with
                // its square, reducing the extra tangle more than brightness.
                // The base dapple network and waves/ripples stay intact.
                float gain=cWaterDebug.w>=1?cWaterDebug.w-1:1;
                tops*=gain;
                density = max(density, tops);
                // (The crests no longer stretch the foam's pattern along the swell: it tangled the network.)
                flow = 0.0;
            }
        }
        density = saturate(density * resolved * amount);
        flow *= resolved * amount;
    }
}

// A fine, bubbly grain over all foam, as much as kFoamTexture says (1 where it adds nothing).
float FoamGrain(float2 p)
{
    float t = FoamPatternSeconds();
    p /= FoamPatternScale();
    float grain = ValueNoise(p * 7.0 + float2(t * 0.3, -t * 0.2)) * 0.6 + ValueNoise(p * 15.0 - float2(t * 0.4, 0.0)) * 0.4;
    return lerp(1.0, saturate(smoothstep(0.25, 0.75, grain) * 1.3), kFoamTexture);
}

#endif

// Waves rolling in to the shore. The distance to the nearest dry ground (from the world's heights) lays wave fronts
// along every coast; they travel inwards, their timing varying slowly along the shore so the fronts are not parallel
// lines, grow foamy as they near the edge and break there, leaving a wash of foam at the waterline that fades as it
// drains before the next one. The same fronts raise ridges in the surface, so reflections and refraction move with
// the foam. The mask travels with the fronts; the foam texture under it stays put.
struct ShoreWaves
{
    float known;
    float distance;
    float along;
    float2 toShore;
};

#ifdef DEFERRED_SLIME
// Slime's waves are the sea's made shorter and slower: slow folds that lap up a bank. They run where slime is the
// terrain's own liquid (Felwood's pools), from the ground map's shore as water's do. Slime in a building (a canal, a
// room's pool) has no shore there and no waves. Measured in kSlimeLapScale times the yards, the sea's fronts fit a
// pool.
static const float kSlimeLapScale = 1.45;
static const float kSlimeLapPace = 0.6;

// How strong the laps are (the SlimeShoreWaves setting; 0 where no slime drawn this frame is the terrain's).
float SlimeLaps() { return cLookingDown.z; }
float ShoreSeconds() { return Seconds() * kSlimeLapPace; }

bool ReadShoreWaves(float2 p, float surfaceHeight, out ShoreWaves waves)
{
    waves.known = 0.0;
    waves.distance = kWaveReach;
    waves.along = 0.0;
    waves.toShore = 0.0;
    float2 uv = RippleUv(p);
    // (One return at the end: Microsoft's compiler mishandles early ones inside branches.)
    bool found = false;
    [branch] if (SlimeLaps() > 0.0 && InsideRippleArea(uv))
    {
        float4 heights = tex2Dlod(sSeabed, float4(uv, 0, 0));
        // Only slime lying at the height the world gives its liquid here: a pool in a building over the same ground
        // is not the terrain's, and the ground map's shore is not its edge.
        float own = 1.0 - smoothstep(kSameWaterBody * 0.6, kSameWaterBody * 1.4, abs(heights.b - surfaceHeight));
        float edge = saturate(min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y)) * kSeabedEdgeFade);
        // On the slime's side of its edge only, as for water.
        float known = saturate(heights.g) * edge * own * smoothstep(0.0, kFoamStartDepth, surfaceHeight - heights.r);
        float distance = max(tex2Dlod(sShore, float4(uv, 0, 0)).r - kShoreCellOffset, 0.0) * kSlimeLapScale;
        [branch] if (known > 0.0 && distance < kWaveReach)
        {
            float step = cRippleArea.z * GroundCellSize();
            float2 slope = float2(tex2Dlod(sShore, float4(uv + float2(step, 0), 0, 0)).r -
                                      tex2Dlod(sShore, float4(uv - float2(step, 0), 0, 0)).r,
                                  tex2Dlod(sShore, float4(uv + float2(0, step), 0, 0)).r -
                                      tex2Dlod(sShore, float4(uv - float2(0, step), 0, 0)).r);
            waves.toShore = -slope / max(length(slope), 1.0e-4);
            waves.distance = distance;
            waves.known = known;
            float2 q = p * kSlimeLapScale;
            waves.along = ValueNoise(q * 0.07 + float2(ShoreSeconds() * 0.01, 0.0)) * 2.0 + ValueNoise(q * 0.19) * 0.8;
            found = true;
        }
    }
    return found;
}
#else
float ShoreSeconds() { return Seconds(); }

bool ReadShoreWaves(float2 p, float surfaceHeight, out ShoreWaves waves)
{
    waves.known = 0.0;
    waves.distance = kWaveReach;
    waves.along = 0.0;
    waves.toShore = 0.0;
    float2 uv = RippleUv(p);
    if (!InsideRippleArea(uv))
        return false;
    float2 seabed = SeabedDepth(p, surfaceHeight);
    // On the water side of the waterline only: where the ground is at or above the surface there are no waves.
    waves.known = saturate(seabed.y) * smoothstep(0.0, kFoamStartDepth, seabed.x);
    waves.distance = max(tex2Dlod(sShore, float4(uv, 0, 0)).r - kShoreCellOffset, 0.0);
    if (waves.known <= 0.0 || waves.distance > kWaveReach)
        return false;
    float step = cRippleArea.z * GroundCellSize();
    float2 slope = float2(tex2Dlod(sShore, float4(uv + float2(step, 0), 0, 0)).r -
                              tex2Dlod(sShore, float4(uv - float2(step, 0), 0, 0)).r,
                          tex2Dlod(sShore, float4(uv + float2(0, step), 0, 0)).r -
                              tex2Dlod(sShore, float4(uv - float2(0, step), 0, 0)).r);
    waves.toShore = -slope / max(length(slope), 1.0e-4);
    waves.along = ValueNoise(p * 0.07 + float2(Seconds() * 0.01, 0.0)) * 2.0 + ValueNoise(p * 0.19) * 0.8;
    return true;
}
#endif

// Where a point is in the train of waves: whole numbers count the waves, the fraction is 0 at a front and grows
// behind it, and fronts move towards the shore as time passes. The timing drifts a little so they do not arrive
// like clockwork.
float WavePhase(float distance, float along)
{
    float t = ShoreSeconds();
    return distance / kWaveLength + t / kWavePeriod + along + kWaveTimingDrift * sin(t * 0.37 + along * 3.0);
}

// Each wave has its own size, so some come in bigger than others.
float WaveSize(float phase, float along)
{
    // Neighbouring stretches keep their own size. Blend across their shared edge
    // so a single breaking crest can taper along its length.
    float segment = along * 2.0 - 0.5;
    float2 cell = float2(floor(phase), floor(segment));
    float weight = smoothstep(0.25, 0.75, frac(segment));
    float size = lerp(Hash(cell), Hash(cell + float2(0.0, 1.0)), weight);
    return lerp(kSmallestWave, 1.0, size);
}

// A wave's cross-section: a steep face towards the shore and a gentle back. Far out it is a wide, low swell; it
// narrows into a steeper ridge as it nears the shore.
float WaveProfile(float behindFront, float distance)
{
    float open = saturate(distance / kWaveReach);
    float sharpness = lerp(kFrontSharpness, kFrontSharpness * kOpenWaterWidening, open);
    float trail = lerp(kFrontTrail, kFrontTrail * kOpenWaterWidening, open);
    return smoothstep(0.0, sharpness, behindFront) * exp(-behindFront / trail);
}

// How the waves tilt the surface here (to add to its slope): the profile's steepness across the front, strongest as
// the waves near the shore.
// The shoreline waves' slope at p, seen by a pixel covering footprint yards (see Resolved; 0 for all of it): the
// steep front is a fraction of a wave long, and from far off at a glancing angle, pixel rows a yard or more apart
// landed on it at random, turning their reflections every which way.
float2 ShoreWaveSlope(float2 p, float surfaceHeight, float footprint = 0.0)
{
    ShoreWaves waves;
    if (!ReadShoreWaves(p, surfaceHeight, waves))
        return 0.0;
    float phase = WavePhase(waves.distance, waves.along);
    float f = frac(phase);
    float rise = (WaveProfile(f + kProfileStep, waves.distance) - WaveProfile(f, waves.distance)) /
                 (kProfileStep * kWaveLength);
    float strength = kWaveSlope * saturate(1.0 - waves.distance / kWaveReach) * WaveSize(phase, waves.along) *
                     waves.known * Resolved(kWaveLength * 0.5, footprint);
    // Rising behind the front means the surface slopes up away from the shore.
    return -waves.toShore * rise * strength;
}

#ifndef DEFERRED_SLIME
// Foam from a breaking front and the wash behind it share the same shoreline samples and wave phase.
void ShoreFoam(float2 p, float surfaceHeight, float footprint, out float density, out float crest, out float2 flow)
{
    density = 0.0;
    crest = 0.0;
    flow = 0.0;
    ShoreWaves waves;
    if (ReadShoreWaves(p, surfaceHeight, waves))
    {
        float phase = WavePhase(waves.distance, waves.along);
        float waveSize = WaveSize(phase, waves.along);
        float behind = frac(phase);
        float open = saturate(waves.distance / kWaveReach);
        // The profile (WaveProfile) peaks at about seven eighths of the way up its face.
        float peak = 0.875 * lerp(kFrontSharpness, kFrontSharpness * kOpenWaterWidening, open);
        // Signed (behind the crest positive, ahead of it negative), so the bend moves the line across rather than
        // opening it into two either side of the crest; taken to the nearest crest, this wave's or the next's.
        float fromCrest = behind - peak;
        float across = (fromCrest - floor(fromCrest + 0.5)) * kWaveLength;
        float size = waveSize * saturate(1.0 - waves.distance / kWaveReach) * waves.known;
        crest = CrestLine(p, across + CrestBend(p), size, kCrestLineWidth * 1.40, footprint);

        float front = WaveProfile(frac(phase), waves.distance) * waveSize;
        float crestFoam = front * saturate(1.0 - waves.distance / kBreakDistance);
        // The wash: rises as a front reaches the waterline and drains away to nothing before the next one, so it never
        // switches on at once (the timing varies along the shore, and a sudden switch drew a line moving along it).
        float arriving = WavePhase(0.0, waves.along);
        float sinceBreak = frac(arriving);
        float drained = exp(-1.0 / kWashFade);
        float wash = smoothstep(0.0, kWashRise, sinceBreak) * saturate((exp(-sinceBreak / kWashFade) - drained) / (1.0 - drained));
        float washFoam = wash * kWashStrength * WaveSize(arriving - 1.0, waves.along) *
                         exp(-waves.distance / (kWashWidth * (0.4 + wash)));
        density = saturate(max(crestFoam, washFoam) * kFoamAmount) * waves.known;
        flow = FoamAxis(float2(-waves.toShore.y, waves.toShore.x)) * density;
    }
}
#endif

// Caustics: sunlight (or moonlight) focused by the waves into bright filaments on the bottom. Two scales of ridged noise
// at the size of the surface's ripples, drifting with them and warped by the big waves' slope so the pattern bends with
// the surface; strongest a little way down, fading with depth and dimmed by cloud. Returns how much to brighten the
// bottom (to be tinted by the light's colour).
static const float kCausticScale = 0.8;
static const float kCausticFineScale = 2.1;
static const float kCausticWarp = 1.6;
static const float kCausticSharpness = 3.0;
static const float kCausticDepth = 12.0;
static const float kCausticFadeIn = 1.0;
static const float kCausticBrightness = 1.0;
static const float kCausticOvercastLoss = 0.7;
static const float kCausticPace = 0.5;
static const float kCausticWaveBand = 2.0;
static const float kCausticBandGlow = 0.35;

float CausticRidge(float2 p)
{
    return 1.0 - abs(ValueNoise(p) * 2.0 - 1.0);
}

float CausticLayer(float2 p, float t)
{
    float ridges = CausticRidge(p + float2(-0.45, -0.34) * t) * CausticRidge(p * 1.37 + float2(0.75, -0.62) * t * 0.6 + 3.1);
    return pow(saturate(ridges), kCausticSharpness);
}

float CausticsAt(float3 bottom, float depth, float surfaceHeight, float overcast)
{
    float light = cToLight.w * SunVisibility() * (1.0 - kCausticOvercastLoss * overcast);
    if (light <= 0.0 || depth <= 0.0)
        return 0.0;
    float t = WaveTime() * kCausticPace;
    float2 p = bottom.xy + LargeWaveSlope(bottom.xy) * kCausticWarp;
    float pattern = CausticLayer(p * kCausticScale, t * kCausticScale) * 0.65 +
                    CausticLayer(p * kCausticFineScale + 5.7, t * kCausticFineScale * 0.8) * 0.45;
#ifndef DEFERRED_SLIME
    // Each shoreline wave focuses light into a brighter band that travels in with it over the shallows, growing as
    // the wave does (from nothing where the waves begin, as its slope does in ShoreWaveSlope, so the bands have no
    // hard starting line), and near its full strength over most of the shallows.
    ShoreWaves waves;
    if (ReadShoreWaves(bottom.xy, surfaceHeight, waves))
    {
        float phase = WavePhase(waves.distance, waves.along);
        float outward = saturate(waves.distance / kWaveReach);
        float band = WaveProfile(frac(phase), waves.distance) * WaveSize(phase, waves.along) * waves.known *
                     (1.0 - outward * outward);
        pattern = pattern * (1.0 + band * kCausticWaveBand) + band * kCausticBandGlow;
    }
#endif
    float strength = exp(-depth / kCausticDepth) * saturate(depth / kCausticFadeIn);
    return pattern * strength * light * kCausticBrightness;
}

#if !defined(DEFERRED_SLIME) && !defined(READS_SURFACE_DETAIL)
// A ship's wake: one broad swell spreading out from each side of the ship as it passes, so it trails behind as a V,
// with a few smaller ripples following it, closer and closer together, and white foam breaking along its leading edge
// near the ship. Inside the V a strip of churned, choppy water trails behind the stern, widening and settling. It all
// adds to the surface's own waves. Laid along the path the ship came by, so the wake bends where it turned. Only for
// ships floating on this water.
static const int kBoatTrailPoints = 24;
static const float kBoatNearSurface = 6.0;
static const float kWakeFullSpeed = 8.0;
// How fast the swell spreads for the ship's speed: this sets the V's half angle (atan 0.36, about 20 degrees). Above
// kWakeNarrowingSpeed the V narrows (as a fast hull's does), the spread falling as speed to the power -kWakeNarrowing.
static const float kWakeSpread = 0.36;
static const float kWakeNarrowingSpeed = 10.0;
static const float kWakeNarrowing = 0.7;
// The swell's foam is gone this far (yards) behind the front at the latest, however fast the ship.
static const float kWakeFoamReach = 70.0;
// The swell's width (yards): from its leading edge to where the first ripple behind it begins; it broadens with age.
static const float kWakeSwellWidth = 10.0;
static const float kWakeSwellBroadening = 1.6;
static const float kWakeSwellHeight = 1.2;
static const float kWakeRippleSqueeze = 1.6;
static const float kWakeSecondSwellAt = 1.5;
static const float kWakeSecondSwell = 0.55;
// The ridge along the swell's leading edge: how high (against the swell), how steeply it rises and how long its back
// is (yards), and how much both widen per second of age, so it is sharp near the ship and broadens behind.
static const float kWakeRidge = 0.6;
static const float kWakeRidgeRise = 0.35;
static const float kWakeRidgeBack = 2.5;
static const float kWakeRidgeWidening = 0.5;
// How much the foam along the leading edge widens per second of age.
static const float kWakeFoamWidening = 0.8;
// How far (yards, near the ship) in from where the foam starts the wave's leading ridge rises.
static const float kWakeRidgeInset = 0.9;
static const float kWakeRippleFade = 0.8;
static const float kWakeLife = 14.0;
// How far back along the hull (in half lengths) the swell takes to rise to full size from the bow.
static const float kWakeRiseAlongHull = 0.4;
// The bow's shape (from above): the hull widens from a point at the bow to its full beam over this much of its half
// length, and the swell leaves from along it.
static const float kBowTaper = 0.8;
static const float kBowShape = 0.7;
// How far (yards) ahead of the bow the wake starts.
static const float kWakeAhead = 7.0;
// Where the wake starts to fade (a share of the known path's length) before the path runs out.
static const float kWakeKnownFade = 0.7;
// How long (seconds) the swell's foam stays full, and when it has thinned out to nothing.
static const float kWakeFoamFullFor = 1.0;
static const float kWakeFoamLife = 5.0;
// How far (yards) ahead of where the spread puts the front the foam starts; the swell's leading edge is moved out to
// meet it.
static const float kWakeFoamLead = 2.0;
// How the swell slows as it spreads: part of it (kWakeKeepsGoing) carries on at its first pace, the rest slows,
// going half as fast after kWakeSlowing seconds.
static const float kWakeSlowing = 5.0;
static const float kWakeKeepsGoing = 0.5;
static const float kWakeFoam = 1.15;
// The foam along the swell's leading edge (yards in from it): where it is full and where it has thinned out.
static const float kWakeFoamRise = 1.5;
static const float kWakeFoamFull = 1.5;
static const float kWakeFoamEnd = 4.0;
static const float kWakeFoamBreakup = 0.18;
static const float kWakeWobble = 1.5;
// The swell's ripples fade out over this far (in hull beams) from the path, where the two sides' would meet in a crease.
static const float kWakeCentreFade = 1.5;
// The churned strip behind the stern: its width at the stern (in hull beams), how fast it widens, how far it lasts, how
// rough it is and how much foam flecks it.
static const float kChurnWidth = 0.7;
static const float kChurnWidening = 0.08;
static const float kChurnLength = 45.0;
static const float kChurnSlope = 0.14;
static const float kChurnScale = 0.45;
static const float kChurnFoam = 0.45;

struct PathPlace
{
    // Whether p is near enough the path for the wake to reach it (nothing else is filled in if not).
    bool near;
    // How long the known path is: the wake fades out before its end.
    float known;
    float behind;
    // How long ago the bow passed here (seconds), and how fast the ship was going then.
    float age;
    float speedThen;
    float side;
    float2 back;
    float2 across;
};

// Where p lies along ship b's path: how far behind the bow, how far to the side, and the directions back along the path
// and across it towards p. Each stretch of the path is carried on as a straight line and measured from that line, so on
// a straight path every stretch gives the same answer and adding a new point changes nothing; the stretches are blended,
// each counting less the further past its own ends p lies, so where the path turns the answer changes smoothly (neither
// jumping on the inside of the turn nor pulling towards the points). Stretches from a far part of the path (where it
// comes back on itself) are left out.
static const float kPathOverrun = 4.0;
static const float kPathFarMargin = 10.0;
static const float kPathFarFade = 4.0;
// Beyond this far (yards) from a ship's path its wake does not reach. (Renderer's ShipWakeBoxes has it too, as it has
// kWakeAhead.)
static const float kPathReach = 150.0;

float4 BoatPathPoint(int b, int n)
{
    return tex2Dlod(sBoatPaths, float4((n + 0.5) / kBoatTrailPoints, (b + 0.5) / 4.0, 0.0, 0.0));
}

// How near p comes to ship b's path at all (yards); too far and the wake does not reach it. (The path is walked in
// loops, here and in PlaceOnPath, not unrolled: unrolled 24 times for each of 4 ships it needs more registers than a
// ps_3_0 shader has.)
float NearestToPath(int b, float2 p)
{
    float2 heading = cBoatShape[b].xy;
    float2 bow = cBoat[b].xy + heading * (cBoatShape[b].z + kWakeAhead);
    float nearest = 1.0e6;
    float2 a = bow;
    [loop] for (int n = 0; n < kBoatTrailPoints; ++n)
    {
        float2 next = BoatPathPoint(b, n).xy;
        float2 segment = next - a;
        float t = saturate(dot(p - a, segment) / max(dot(segment, segment), 1.0e-6));
        nearest = min(nearest, length(p - a - segment * t));
        a = next;
    }
    return nearest;
}

// Where p lies along ship b's path, p being nearest yards from the path at its nearest (see AlongPath), however far
// that is.
PathPlace PlaceOnPath(int b, float2 p, float nearest)
{
    float2 heading = cBoatShape[b].xy;
    float halfLength = cBoatShape[b].z;

    // The wake's front (a little ahead of the bow), passing now; each path point was passed by it that far before the
    // ship's middle.
    float ahead = halfLength + kWakeAhead;
    float2 bow = cBoat[b].xy + heading * ahead;
    PathPlace place;
    place.near = nearest <= kPathReach;
    float2 from = bow;
    // How long ago the bow was here: now while the ship sails, and the time since it stopped or went while its wake
    // settles (its whole path ages together, the bow with it, so no young foam stays where it was).
    float4 first = BoatPathPoint(b, 0);
    float fromAge = first.z;
    // The speed with the path's first point: the ship's while it sails, and the speed it had when it stopped while
    // its wake settles (its own speed then eases to nothing).
    float fromSpeed = first.w;
    float travelled = 0.0;
    float lowestScore = 1.0e6;
    float weights = 0.0;
    float behindSum = 0.0;
    float ageSum = 0.0;
    float speedSum = 0.0;
    float sideSum = 0.0;
    float2 backSum = 0.0;
    float2 leftSum = 0.0;
    float2 lastBack = -heading;
    [loop] for (int i = 0; i < kBoatTrailPoints; ++i)
    {
        float4 row = BoatPathPoint(b, i);
        float2 to = row.xy;
        float toSpeed = row.w;
        float toAge = row.z + ahead / max(toSpeed, 1.0);
        float2 segment = to - from;
        float segmentLength = length(segment);
        bool real = segmentLength > 1.0e-3;
        float2 back = real ? segment / segmentLength : lastBack;
        lastBack = back;
        float2 left = float2(-back.y, back.x);
        float2 offset = p - from;
        float along = dot(offset, back);
        // How far past this stretch's ends p lies (ahead of the bow the first carries on forwards), and how far p is
        // from the stretch itself.
        float before = i == 0 ? 0.0 : max(-along, 0.0);
        float after = max(along - segmentLength, 0.0);
        float overrun = before + after;
        float distance = length(offset - back * clamp(along, 0.0, segmentLength));
        // Weighed against the best stretch so far (rescaling the sums when a better one turns up), so however far past
        // every stretch's ends p lies, the best still counts fully.
        float score = pow(overrun / kPathOverrun, 2.0) + max(distance - nearest - kPathFarMargin, 0.0) / kPathFarFade;
        if (score < lowestScore)
        {
            float rescale = exp(score - lowestScore);
            weights *= rescale;
            behindSum *= rescale;
            ageSum *= rescale;
            speedSum *= rescale;
            sideSum *= rescale;
            backSum *= rescale;
            leftSum *= rescale;
            lowestScore = score;
        }
        float weight = exp(lowestScore - score);
        weights += weight;
        behindSum += weight * (travelled + along);
        // The age and speed along the stretch, carried on past its ends.
        float ageRate = real ? (toAge - fromAge) / segmentLength : 1.0 / max(toSpeed, 1.0);
        ageSum += weight * (fromAge + along * ageRate);
        speedSum += weight * lerp(fromSpeed, toSpeed, saturate(along / max(segmentLength, 1.0e-3)));
        sideSum += weight * dot(offset, left);
        backSum += weight * back;
        leftSum += weight * left;
        travelled += segmentLength;
        from = to;
        fromAge = toAge;
        fromSpeed = toSpeed;
    }
    float side = sideSum / weights;
    float2 left = leftSum / max(length(leftSum), 1.0e-4);
    place.known = travelled;
    place.behind = behindSum / weights;
    place.age = ageSum / weights;
    place.speedThen = speedSum / weights;
    place.side = abs(side);
    place.back = backSum / max(length(backSum), 1.0e-4);
    place.across = side >= 0.0 ? left : -left;
    return place;
}

// Where p lies along ship b's path, walked for p itself; nothing is filled in beyond kPathReach.
PathPlace AlongPath(int b, float2 p)
{
    float2 heading = cBoatShape[b].xy;
    PathPlace place;
    place.near = false;
    place.known = 0.0;
    place.behind = 0.0;
    place.age = 0.0;
    place.speedThen = 0.0;
    place.side = 0.0;
    place.back = -heading;
    place.across = float2(heading.y, -heading.x);
    float nearest = NearestToPath(b, p);
    // (An if block rather than an early return: Microsoft's compiler mishandles the return once this is inlined into
    // a branch.)
    if (nearest <= kPathReach)
        place = PlaceOnPath(b, p, nearest);
    return place;
}

// Where p lies along ship b's path, read from the wake map (see sWakeMapPlace), blended between its four nearest
// points by hand (the map's 32-bit values need not filter).
PathPlace MappedPlace(int b, float2 p)
{
    float4 square = cShipBox[b];
    float2 at = (p - square.xy) / (square.zw - square.xy) * kWakeMapSize - 0.5;
    float2 corner = clamp(floor(at), 0.0, kWakeMapSize - 2.0);
    float2 t = saturate(at - corner);
    float2 uv = (corner + 0.5) / float2(kWakeMapSize, 4.0 * kWakeMapSize) + float2(0.0, b / 4.0);
    float2 step = 1.0 / float2(kWakeMapSize, 4.0 * kWakeMapSize);
    float4 place = lerp(lerp(tex2Dlod(sWakeMapPlace, float4(uv, 0, 0)),
                             tex2Dlod(sWakeMapPlace, float4(uv + float2(step.x, 0.0), 0, 0)), t.x),
                        lerp(tex2Dlod(sWakeMapPlace, float4(uv + float2(0.0, step.y), 0, 0)),
                             tex2Dlod(sWakeMapPlace, float4(uv + step, 0, 0)), t.x), t.y);
    float4 way = lerp(lerp(tex2Dlod(sWakeMapWay, float4(uv, 0, 0)),
                           tex2Dlod(sWakeMapWay, float4(uv + float2(step.x, 0.0), 0, 0)), t.x),
                      lerp(tex2Dlod(sWakeMapWay, float4(uv + float2(0.0, step.y), 0, 0)),
                           tex2Dlod(sWakeMapWay, float4(uv + step, 0, 0)), t.x), t.y);
    PathPlace mapped;
    mapped.near = way.z <= kPathReach;
    mapped.known = way.w;
    mapped.behind = place.x;
    mapped.age = place.y;
    mapped.speedThen = place.z;
    mapped.side = abs(place.w);
    mapped.back = way.xy / max(length(way.xy), 1.0e-4);
    float2 left = float2(-mapped.back.y, mapped.back.x);
    mapped.across = place.w >= 0.0 ? left : -left;
    return mapped;
}

// The wake's height across the swell (inside: yards in from its leading edge; width: the swell's width; age: seconds
// since the ship passed): a sharp ridge on the leading edge, the broad swell behind it with a longer back, then ripples
// closer and closer together, dying away, and a second, smaller swell (with no foam) further in.
float WakeSwell(float inside, float width, float age)
{
    float x = inside / width;
    // pow is only defined for a base of 0 or more (ahead of the edge, where x is below 0, the smoothstep is 0 anyway).
    float first = smoothstep(0.0, 0.3, x) * cos(6.2832 * 0.9 * pow(max(x, 0.0), kWakeRippleSqueeze)) *
                  exp(-x / kWakeRippleFade);
    // The second, smaller swell behind it, like the water bouncing back.
    float y = x - kWakeSecondSwellAt;
    float second = smoothstep(0.0, 0.35, y) * exp(-max(y, 0.0) / 0.45) * kWakeSecondSwell;
    // The ridge on the leading edge: a steep front and a long back.
    float widen = 1.0 + age * kWakeRidgeWidening;
    float ridge = smoothstep(0.0, kWakeRidgeRise * widen, inside) * exp(-max(inside, 0.0) / (kWakeRidgeBack * widen)) *
                  kWakeRidge;
    return first + second + ridge;
}

// The churned water's roughness at p: two scales of noise tumbling over each other; returns its slope and (z) how
// broken it is here, for foam flecks.
float3 Churn(float2 p)
{
    float2 q = p * kChurnScale;
    float2 flow = float2(Seconds() * 0.12, -Seconds() * 0.09);
    float e = 0.15;
    float centre = ValueNoise(q + flow) + ValueNoise(q * 2.3 - flow * 1.4) * 0.5;
    float right = ValueNoise(q + float2(e, 0) + flow) + ValueNoise((q + float2(e, 0)) * 2.3 - flow * 1.4) * 0.5;
    float up = ValueNoise(q + float2(0, e) + flow) + ValueNoise((q + float2(0, e)) * 2.3 - flow * 1.4) * 0.5;
    return float3(float2(right - centre, up - centre) / e, centre / 1.5);
}

// One ship's wake added to the wake so far at p, which lies at place along ship b's path.
float4 WithShipWake(float4 wake, int b, float2 p, PathPlace place, float footprint)
{
    float speed = cBoat[b].w;
    float halfLength = cBoatShape[b].z;
    float beam = cBoatShape[b].w;
    float knownFade = 1.0 - smoothstep(place.known * kWakeKnownFade, place.known, place.behind);
    float moving = saturate(speed / kWakeFullSpeed) * knownFade;
    // The swell keeps the size the ship's speed gave it when it passed.
    float movingThen = saturate(place.speedThen / kWakeFullSpeed) * knownFade;
    float behind = place.behind;
    float hullBehind = behind - kWakeAhead;
    // Measured from the hull's side (narrowing to a point at the bow), so the swell leaves from along it.
    float taper = halfLength * kBowTaper;
    float hullSide = beam * pow(saturate(behind / taper), kBowShape);
    float side = place.side - hullSide;

    if (behind > 0.0)
    {
        // How long ago the bow passed here, so how far the swell has spread (it slows as it goes, so the V's arms
        // bend in behind the ship), and how far in from its leading edge this point is (the edge wanders a little
        // so it is not a ruled line).
        float age = max(place.age, 0.0);
        float spreadRatio = kWakeSpread * pow(min(kWakeNarrowingSpeed / max(place.speedThen, 1.0), 1.0), kWakeNarrowing);
        float outward = spreadRatio * place.speedThen;
        float spread = outward * lerp(kWakeSlowing * log(1.0 + age / kWakeSlowing), age, kWakeKeepsGoing);
        float wobble = (ValueNoise(p * 0.06) - 0.5) * kWakeWobble;
        // How fast the front widens going back (the bow's shape near it, then the swell's spread), so the distance
        // in from it is measured square to it: where the front crosses the path at the bow the foam keeps its width.
        float hullSlope = behind < taper ? beam * kBowShape * pow(max(behind, 0.5) / taper, kBowShape - 1.0) / taper : 0.0;
        float frontSlope = min(spreadRatio * lerp(1.0 / (1.0 + age / kWakeSlowing), 1.0, kWakeKeepsGoing) + hullSlope, 6.0);
        float square = rsqrt(1.0 + frontSlope * frontSlope);
        float inside = (spread - side + wobble) * square;
        float width = kWakeSwellWidth + age * kWakeSwellBroadening;
        if (inside > -kWakeFoamLead && inside < width * 4.0)
        {
            float size = kWakeSwellHeight * lerp(0.7, 1.0, ValueNoise(p * 0.03 + 7.0)) *
                         saturate(behind / (halfLength * kWakeRiseAlongHull)) * exp(-age / kWakeLife) * movingThen *
                         smoothstep(0.0, beam * kWakeCentreFade, place.side);
            float step = 0.1;
            float intoSwell = inside + kWakeFoamLead;
            // The wave's leading ridge sits under the foam's visible edge (a little in from where it starts,
            // further as the foam widens).
            float foamWiden = 1.0 + age * kWakeFoamWidening;
            float intoWave = intoSwell - kWakeRidgeInset * foamWiden;
            float rise = (WakeSwell(intoWave + step, width, age) - WakeSwell(intoWave, width, age)) / step;
            // Moving in from the edge goes back along the path (less so further back, where the swell has slowed)
            // and in towards it.
            float2 inward = (place.back * frontSlope - place.across) * square;
            wake.xy += inward * rise * size;
            // Foam breaking along the leading edge, near the ship; it thins out and is gone after a few seconds.
            // A solid core with soft sides: where the mask is weaker only the foam's thickest parts show, so the
            // edges break up while the line itself stays whole.
            // Measured from the swell's leading edge, where the foam starts whatever its age.
            float crest = smoothstep(0.0, kWakeFoamRise * foamWiden, intoSwell) *
                          (1.0 - smoothstep(kWakeFoamFull * foamWiden, kWakeFoamEnd * foamWiden, intoSwell));
            float breakup = smoothstep(0.25, 0.75, ValueNoise(p * kWakeFoamBreakup + float2(3.1, Seconds() * 0.05)));
            float fading = (1.0 - smoothstep(kWakeFoamFullFor, kWakeFoamLife, age)) *
                           (1.0 - smoothstep(kWakeFoamReach * 0.4, kWakeFoamReach, behind));
            wake.z = max(wake.z, crest * crest * fading * movingThen * kWakeFoam * lerp(0.75, 1.0, breakup));
            // The line along the leading ridge's top (see CrestLine), which peaks a little way up its steep front.
            float ridgeTop = intoWave - kWakeRidgeRise * (1.0 + age * kWakeRidgeWidening) * 0.9;
            wake.w = max(wake.w, CrestLine(p, ridgeTop + CrestBend(p), size / kWakeSwellHeight, kCrestLineWidth, footprint) * fading);
        }
    }

    // The churned strip behind the stern.
    float behindStern = hullBehind - 2.0 * halfLength;
    float churnWidth = beam * kChurnWidth + max(behindStern, 0.0) * kChurnWidening;
    float churnAmount = exp(-pow(place.side / churnWidth, 2.0)) * exp(-max(behindStern, 0.0) / kChurnLength) *
                        smoothstep(-halfLength * 0.5, halfLength * 0.25, behindStern) * moving;
    if (churnAmount > 0.01)
    {
        float3 churn = Churn(p);
        wake.xy += churn.xy * churnAmount * kChurnSlope;
        wake.z = max(wake.z, smoothstep(0.55, 0.85, churn.z) * churnAmount * kChurnFoam);
    }

    // (No foam of its own along the hull or at the bow: the foam where things meet the water finds the hull there, in
    // its own shape, as it does a character; drawn here too, it put a round patch in front of the ship.)
    return wake;
}

// The ships' wakes at p: the wave slope (world x, y), how much foam (z) and the line along the leading ridge (w), seen
// by a pixel covering footprint yards of the surface.
float4 ShipWake(float2 p, float surfaceHeight, float footprint)
{
    float4 wake = 0.0;
    [unroll] for (int b = 0; b < 4; ++b)
    {
        // An empty slot has no hull; a ship that has stopped (speed 0) still has a wake settling behind it. (An if, not
        // continue: Microsoft's compiler mishandles continue in this unrolled loop, dropping most of the path's blend.)
        // Outside the box its wake can reach, the path (two walks along it) is not looked at.
        bool inBox = all(p >= cShipBox[b].xy) && all(p <= cShipBox[b].zw);
        if (cBoatShape[b].z > 0.0 && abs(cBoat[b].z - surfaceHeight) <= kBoatNearSurface && inBox)
        {
            PathPlace place;
            if (cLiquidOptions.x > 0.5)
                place = MappedPlace(b, p);
            else
                place = AlongPath(b, p);
            if (place.near)
                wake = WithShipWake(wake, b, p, place, footprint);
        }
    }
    wake.zw = saturate(wake.zw);
    return wake;
}

#endif
