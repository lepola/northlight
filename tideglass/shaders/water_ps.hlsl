// The water's composite and its reflection pass read the ships' wakes and the foam from the surface detail pass rather
// than working them out (see Renderer::DrawSurfaceDetail): with them the composite ran out of registers, and both
// passes worked out the same wakes.
#if !defined(DEFERRED_SLIME) && !defined(SURFACE_DETAIL_PASS)
#define READS_SURFACE_DETAIL 1
#endif
#if defined(WATER_FILTERED_MATERIAL) || defined(SURFACE_DETAIL_PASS) || defined(REFLECTION_PASS)
#define WATER_SURFACE_MATERIAL 1
#endif
#include "water_common.hlsli"
#ifndef DEFERRED_SLIME
#include "water_foam.hlsli"
#endif

#ifdef READS_SURFACE_DETAIL
// The surface detail pass's results, a texel for every pixel: the whole surface's slope (x, y: its normal's x and y
// over its z) and the foam (z); then the slope foam lies on (see FoamNormal), x and y over z.
sampler2D sSurfaceDetail : WATER_S_REGISTER(WATER_SLOT_sSurfaceDetail);
sampler2D sSurfaceSlopes : WATER_S_REGISTER(WATER_SLOT_sSurfaceSlopes);
#endif

// x: how far from the shallow towards the deep colour the gradient goes when seen from above; y: 1 when the light is
// the moon; z: how hard it rains (0..1); w: how strongly rain rings bend the surface (1 = normal).
float4 cSurface : WATER_C_REGISTER(WATER_SLOT_cSurface);

// The reflection pass's results (see Renderer::DrawReflections), a texel for every pixel: rgb the colour met times a
// (how strongly it shows, faded towards the screen's edges); then its state (the kReflection states, stored from 0
// for none) and how far the ray went to what it met.
sampler2D sReflection : WATER_S_REGISTER(WATER_SLOT_sReflection);
sampler2D sReflectionHit : WATER_S_REGISTER(WATER_SLOT_sReflectionHit);
// What lies under the water (see Renderer::DrawUnderwaterScene), filled in where something stands out of it, and
// (g) the water's thickness there.
sampler2D sUnderwaterScene : WATER_S_REGISTER(WATER_SLOT_sUnderwaterScene);
sampler2D sUnderwaterThickness : WATER_S_REGISTER(WATER_SLOT_sUnderwaterThickness);
// xy: the buffers' size in texels; z: 1 when the reflections were drawn this frame, plus 2 when what lies under the
// water was; w: which ReflectionDebug view to show.
float4 cReflectionGrid : WATER_C_REGISTER(WATER_SLOT_cReflectionGrid);
bool ReflectionsDrawn() { return frac(cReflectionGrid.z * 0.5) > 0.25; }
bool UnderwaterSceneDrawn() { return cReflectionGrid.z > 1.5; }

// Where full-resolution pixel lies in the underwater scene, drawn at half the resolution (see
// Renderer::DrawUnderwaterScene): its texels are half as many each way, rounded up.
float2 HalfResUnderwaterUv(float2 pixel)
{
    return (pixel + 0.5) * 0.5 / floor(0.5 / cTexelGrid.xy + 0.51);
}

static const float kReliefScale = 6.0;
static const float kMaxReliefSlope = 0.35;
static const float kGlowBrightness = 1.3;
static const float kGlowSpill = 0.35;
static const float kScatterNormalBend = 0.6;
// The game's sun and moon discs are far larger than the real ones, so their glitter spreads wider.
static const float kSunRoughness = 0.16;
// A low sun's glitter stretches into a long column towards the horizon, up to this many times stronger.
static const float kLowSunBoost = 0.6;
static const float kLowSunElevation = 0.35;
static const float kLowSunSpread = 0.06;
// The reflected sun disc: its angular radius and soft edge (radians), the glow round it, and how bright it is.
static const float kSunDiscRadius = 0.05;
// Spread the reflected disc into a gradual material shoulder. Atmospheric glare
// remains supplied by the environment; this does not add another fog/glow layer.
static const float kSunDiscSoftness = 0.08;
static const float kMoonDiscSoftness = 0.12;
// The swell's share in bending the view into the water (it counted twice over, once through the normal and once more,
// too strong seen from above).
static const float kSwellRefraction = 1.0;
// The share of the finer waves' slope (ripples, wakes, rain rings, the shore's waves) in bending the view into the
// water: as steep as the swell, they bent it as far and drew their noise cells as ridges on the bottom.
static const float kDetailRefraction = 0.35;
static const float kSunGlowWidth = 0.12;
static const float kSunGlowShare = 0.25;
static const float kSunBallStrength = 6.0;
static const float kSheenRoughness = 0.45;
static const float kSheenWeight = 1.2;
static const float kSpecularAntialiasing = 2.0;
static const float kSpecularVisibility = 0.25;
static const float kGlintExposure = 1.0;
static const float kGlintCeiling = 2.0;
static const float kFoamDirect = 0.7;
static const float kFoamAmbient = 0.9;
static const float kFoamBrightness = 1.05;
static const float kSlimeWaveShading = 2.5;
// The glow band is kept narrow: over shallow slime (Felwood's pools) a wide one lights the whole surface.
static const float kSlimeGlowWidth = 0.35;
static const float kSlimeWaveShadeLimit = 0.35;
static const float kSlimeMissedReflection = 0.6;
static const float kSlimeRingShare = 0.6;
// How far what the slime reflects is coloured by its body: a clear sky lay on green sludge as a pale blue sheen.
static const float kSlimeReflectionPigment = 0.85;
static const float kScatterPower = 4.0;
static const float kScatterCrestBoost = 6.0;
static const float kScatterBase = 0.25;

// What is seen through the liquid, softened the deeper it lies (the liquid scatters the light coming up from it): a
// few taps around the point, leaving out any that land on something above the surface. Slime is murkier than water,
// so it softens faster, by as much as it is less clear (SlimeClarity).
static const float kBlurPerYard = 0.035;
static const float kBlurMaxPixels = 7.0;
static const int kBlurTaps = 8;
static const float kBlurTurn = 2.39996;
// How much shallower (yards) than the bottom being blurred a tap may lie and still count in its blur.
static const float kBlurDepthTolerance = 0.3;

float3 BlurredBottom(float2 centre, float3 world, float thickness, float surfaceDepth, float3 sharp)
{
#ifdef DEFERRED_SLIME
    float radius = kBlurPerYard / Clarity() * thickness * cLookingDown.x;
#else
    float radius = kBlurPerYard * thickness * cLookingDown.x;
#endif
    float pixels = min(length(ViewToPixel(WorldToView(world + float3(radius, 0.0, 0.0))) - ViewToPixel(WorldToView(world))),
                       kBlurMaxPixels);
    if (pixels < 0.5)
        return sharp;
    float3 sum = sharp;
    float count = 1.0;
    // Tap i lies at angle i * kBlurTurn and radius sqrt((i + 0.5) / kBlurTaps), stepped as the loop goes (see Unfolded).
    float2 direction = float2(Unfolded(1.0), 0.0);
    float share = Unfolded(0.5) / kBlurTaps;
    // (A loop rather than unrolled: unrolled, its taps were all held at once, and with the foam the shader ran out of
    // registers.)
    [loop] for (int i = 0; i < kBlurTaps; ++i)
    {
        float2 tap = centre + direction * sqrt(share) * pixels;
        direction = float2(direction.x * cos(kBlurTurn) - direction.y * sin(kBlurTurn),
                           direction.x * sin(kBlurTurn) + direction.y * cos(kBlurTurn));
        share += Unfolded(1.0) / kBlurTaps;
        // (Selected rather than skipped with continue: Microsoft's compiler mishandles continue in unrolled loops.)
        float tapDepth = SampleRawDepth(sOpaqueDepth, tap);
        float beyond = BeyondFarClip(tapDepth) ? 1.0e4 : ViewZ(tapDepth);
        bool usable = InsideViewport(tap) && beyond > ViewZ(surfaceDepth) + kRefractionEase;
        float3 tapColour = tex2Dlod(sSceneBefore, float4((tap + 0.5) * cTexelGrid.xy, 0, 0)).rgb;
#ifndef DEFERRED_SLIME
        // What lies under the water is filled in where something stands out of it; a tap counts where it lies at least
        // as deep as the bottom being blurred: nearer things (a plant, a character's legs) stand sharp in front of it,
        // and taken into its blur drew a soft ghost of them, bent with the bottom, round where they are.
        if (UnderwaterSceneDrawn())
        {
            float2 clamped = clamp(tap, cViewportPx.xy, cViewportPx.xy + cViewportPx.zw - 1.0);
            float2 clampedUv = (clamped + 0.5) * cTexelGrid.xy;
            // Only the water itself (state 3), not what the fill made up beside it: along the shore that was little
            // or nothing, and blurred in it cut dark holes. At half the resolution, the water and its depth are
            // measured here at full resolution, as underwater_scene_ps does, and the seabed read as it is.
            float own = WaterThickness(clamped, SampleRawDepth(sDepth, clamped));
            tapColour = tex2Dlod(sSceneBefore, float4(clampedUv, 0, 0)).rgb;
            float4 tapHit = float4(own > 0.0 ? 3.0 : 1.0, own, 0.0, 0.0);
            usable = tapHit.r > 2.5 && tapHit.r < 3.5 && tapHit.g > thickness - kBlurDepthTolerance;
        }
#endif
        sum += usable ? tapColour : 0.0;
        count += usable ? 1.0 : 0.0;
    }
    return sum / count;
}

// The scene under the surface, shifted as the waves' slope bends the view from the camera. The shift is the same
// size everywhere, so everything under the surface moves together: scaling it by the depth under each pixel would
// jump at the edge of anything standing in the liquid and show it twice. Where the shift would land on something in
// front of the surface it falls back to the unshifted view. Also gives the liquid's thickness where the view lands:
// what is seen there is dimmed by that depth, and the slime's edge glow bends with it.
// Foam on a ripple is lit as the ripple is: brighter on the side facing the light, darker on its back, so it lies over
// the ripples under it (evenly lit, it lay flat over them). normal: the slope it lies on (FoamNormal).
static const float kFoamRelief = 0.7;

float FoamRelief(float3 normal)
{
    float3 toLight = normalize(float3(cToLight.xy, max(cToLight.z, 0.25)));
    float facing = saturate(dot(normal, toLight)) / toLight.z;
    return lerp(1.0, clamp(facing, 0.5, 1.5), kFoamRelief);
}

#if !defined(DEFERRED_SLIME) && defined(READS_SURFACE_DETAIL)
// s9's rain-ring field has already been resolved into surface detail for this pass.
sampler2D sSurfaceVisibility : WATER_S_REGISTER(WATER_SLOT_sSurfaceVisibility);
float4 cSurfaceVisibility : WATER_C_REGISTER(WATER_SLOT_cSurfaceVisibility); // x: current sun/moon map; y: the resolved mirror includes an enclosed water receiver
#endif

float DirectLightVisibility(float2 pixel)
{
    float visibility = 1.0;
#if !defined(DEFERRED_SLIME) && defined(READS_SURFACE_DETAIL)
    [branch] if (cSurfaceVisibility.x > 0.5)
    {
        float2 sources = tex2Dlod(sSurfaceVisibility, float4((pixel + 0.5) * cTexelGrid.xy, 0, 0)).rg;
        visibility = saturate(lerp(sources.r, sources.g, cSurface.y));
    }
    [branch] if (cSurfaceVisibility.y > 0.5)
        visibility *= 1.0 - tex2Dlod(sReflectionHit, float4((pixel + 0.5) * cTexelGrid.xy, 0, 0)).b;
#endif
    return visibility;
}

float3 FoamLight(float visibility)
{
    // Foam is diffuse surface material. Only direct light is shadowed; ambient
    // remains, and the active sun/moon's altitude/weather energy applies once.
#ifdef DEFERRED_SLIME
    float direct = 1.0; // retain the slime material's authored/emissive balance
#else
    float direct = cToLight.w * SunVisibility() * visibility;
#endif
    return saturate(cAmbientColor.rgb * Unfolded(kFoamAmbient) +
                    cDirectColor.rgb * (Unfolded(kFoamDirect) * direct)) * Unfolded(kFoamBrightness);
}

float3 Refracted(float2 pixel, float3 world, float3 normal, float thickness, float surfaceDepth,
                 out float bentThickness)
{
    float2 uv = (pixel + 0.5) * cTexelGrid.xy;
    float3 colour = tex2Dlod(sSceneBefore, float4(uv, 0, 0)).rgb;
    bentThickness = thickness;
#ifdef DEFERRED_SLIME
    float3 slope = float3(normal.xy, 0.0);
#else
    // The big, slow waves bend the view into the water; the finer detail on them far less.
    float3 slope = float3(normal.xy * kDetailRefraction -
                              LargeWaveSlope(world.xy) * WaveStrength() * (kSwellRefraction - kDetailRefraction),
                          0.0);
#endif
    // The same bend everywhere, whatever the depth: bent less where the water is shallow, anything just under the
    // surface (a plant, a character's legs) was bent differently from the bottom beside it, a second, offset
    // refraction in its shape.
    float2 bent = pixel + ViewToPixel(WorldToView(world - slope * kRefractionReach)) - ViewToPixel(WorldToView(world));
    // (One return at the end: Microsoft's compiler mishandles an early one inside a branch.)
    bool filled = false;
#ifndef DEFERRED_SLIME
    // Bent into what lies under the water, filled in where something stands out of it (a character's body, a plant):
    // no check and no snapping back straight, which drew hard-edged holes round what stands in the water.
    filled = UnderwaterSceneDrawn();
    if (filled)
    {
        bent = clamp(bent, cViewportPx.xy, cViewportPx.xy + cViewportPx.zw - 1.0);
        float2 bentUv = (bent + 0.5) * cTexelGrid.xy;
        float4 bentHit;
        // Where the bent view lands on water the seabed is read as it is, at full resolution, with the water's depth
        // measured here; only what the fill made up comes from the half resolution scene, blended between its texels.
        float own = WaterThickness(bent, SampleRawDepth(sDepth, bent));
        if (own > 0.0)
        {
            colour = tex2Dlod(sSceneBefore, float4(bentUv, 0, 0)).rgb;
            bentHit = float4(3.0, own, 0.0, 0.0);
        }
        else
        {
            float2 sceneUv = HalfResUnderwaterUv(bent);
            colour = tex2Dlod(sUnderwaterScene, float4(sceneUv, 0, 0)).rgb;
            bentHit = tex2Dlod(sUnderwaterThickness, float4(sceneUv, 0, 0));
        }
        // The water's depth there: where the fill made it up (round a character's body), the depth of the water it
        // was filled from, as its colour is; this pixel's own (the few inches over a submerged arm) left the sand filled
        // in round the body almost untinted, a bright copy of the body's outline. (The shore's ground is kept, not
        // filled, see underwater_scene_ps.) Bent onto nothing, this pixel's own.
        bentThickness = bentHit.r > 2.5 ? bentHit.g : thickness;
        colour = BlurredBottom(bent, world, bentThickness, surfaceDepth, colour);
        // Bent onto nothing (dry land along the shore with no water near to fill it from): the foam's colour rather
        // than dark.
        if (bentHit.r < 1.5)
            colour = FoamLight(DirectLightVisibility(pixel)) * cFoamColor.rgb;
    }
#endif
    float bentDepth = SampleRawDepth(sOpaqueDepth, bent);
    float beyond = BeyondFarClip(bentDepth) ? 1.0e4 : ViewZ(bentDepth);
    if (!filled && InsideViewport(bent) && beyond > ViewZ(surfaceDepth) + kRefractionEase)
    {
        colour = tex2Dlod(sSceneBefore, float4((bent + 0.5) * cTexelGrid.xy, 0, 0)).rgb;
        bentThickness = max(WaterThickness(bent, surfaceDepth), 0.0);
        colour = BlurredBottom(bent, world, bentThickness, surfaceDepth, colour);
    }
    return colour;
}

float4 cStyleExtras : WATER_C_REGISTER(WATER_SLOT_cStyleExtras);

// How overcast it is (fog closing in, storms), times the OvercastWater setting.
float Overcast()
{
#ifdef DEFERRED_SLIME
    return 0.0;
#else
    return cGroundInfo.w;
#endif
}
static const float kOvercastEvenAbsorption = 0.8;
// The underwater view's absorption over its visibility distance (see underwater_ps).
static const float3 kUnderwaterAbsorptionPerVisibility = float3(2.4, 0.9, 0.7);
static const float kNightWaterLight = 0.33;
// From above the view crosses the surface and the water together, so it clouds over a little sooner.
static const float kSurfaceMurk = 1.6;
static const float kSheenFloor = 0.06;
static const float kVeilDepth = 0.6;
static const float kGlancingStart = 0.6;
static const float kGlancingFull = 0.95;
static const float kMaxGlancingFresnel = 0.95;
static const float kGlancingCalm = 0.7;
static const float kOvercastScatterLoss = 0.7;

float FoamDepth() { return cStyleExtras.y; }
float FoamGlow() { return cFoamColor.w; }

float Luma(float3 color)
{
    return dot(color, float3(0.299, 0.587, 0.114));
}

#ifdef DEFERRED_SLIME
// SlimeFlowRelief uses the native slime image. Water's surface relief is procedural.
float3 WithStockRelief(float3 normal, float2 uv)
{
    if (cStyleExtras.z <= 0.0)
        return normal;
    float2 texel = cTexelGrid.xy;
    float left = Luma(tex2Dlod(sSceneWithWater, float4(uv - float2(texel.x, 0), 0, 0)).rgb);
    float right = Luma(tex2Dlod(sSceneWithWater, float4(uv + float2(texel.x, 0), 0, 0)).rgb);
    float up = Luma(tex2Dlod(sSceneWithWater, float4(uv - float2(0, texel.y), 0, 0)).rgb);
    float down = Luma(tex2Dlod(sSceneWithWater, float4(uv + float2(0, texel.y), 0, 0)).rgb);
    float2 slope = clamp(float2(right - left, down - up) * kReliefScale, -kMaxReliefSlope, kMaxReliefSlope);
    return normalize(normal + float3(-slope * cStyleExtras.z, 0.0));
}
#endif


#include "crest_light.hlsli"

float GgxDistribution(float facingHalf, float roughness)
{
    float a2 = roughness * roughness * roughness * roughness;
    float d = facingHalf * facingHalf * (a2 - 1.0) + 1.0;
    return CrestDensity(a2, d);
}

float NormalScreenVariance(float3 normal)
{
    float3 change = abs(ddx(normal)) + abs(ddy(normal));
    return dot(change, change);
}

float SunReflectionResponse(float3 normal, float3 toCamera, float variance)
{
    // A low sun spreads its glitter sideways, towards the width of its glow in the haze.
    float lowSunSpread = kLowSunSpread * (1.0 - saturate(cToLight.z / kLowSunElevation));
    float roughness = sqrt(kSunRoughness * kSunRoughness + lowSunSpread * lowSunSpread + kSpecularAntialiasing * variance);
    float3 halfway = normalize(cToLight.xyz + toCamera);
    float facingHalf = saturate(dot(normal, halfway));
    float lit = saturate(dot(normal, cToLight.xyz));
    float sunFresnel = kWaterF0 + (1.0 - kWaterF0) * pow(1.0 - saturate(dot(halfway, toCamera)), 5.0);
    float glitter = GgxDistribution(facingHalf, min(roughness, 1.0)) * kSpecularVisibility * lit * sunFresnel;
    float sheen = GgxDistribution(facingHalf, kSheenRoughness) * kSpecularVisibility * lit * sunFresnel;
    float lowSun = 1.0 + kLowSunBoost * (1.0 - saturate(cToLight.z / kLowSunElevation));
    // The sun as the disc it is in this game's sky (large) with a soft glow round it, reflected by each wave facet:
    // facets tilted near enough each show a piece of the bright ball, brightest in its middle.
    float3 mirrored = reflect(-toCamera, normal);
    float angle = acos(clamp(dot(mirrored, cToLight.xyz), -1.0, 1.0));
    // The moon's disc blends out more softly than the sun's.
    float softness = lerp(kSunDiscSoftness, kMoonDiscSoftness, cSurface.y);
    float radius=cSkyLayout.x>1.5?cEnvironmentLight.x:kSunDiscRadius;
    float disc=1.0-smoothstep(radius-softness*.5,radius+softness,angle);
    // A complete provider field can supply this body's atmospheric glare.
    // Keep the authored direct disc, glitter and sheen; replace only broad glow.
    float glow = cSkyLayout.x>1.5&&cEnvironmentLight.z>.5?0.0:exp(-angle / kSunGlowWidth);
    float ball = (disc + glow * kSunGlowShare) * sunFresnel * kSunBallStrength;
    float energy = (glitter + sheen * kSheenWeight + ball) * lowSun * cToLight.w * SunVisibility() *
                   SpecularStrength();
    float response = (1.0 - exp(-energy * kGlintExposure)) * kGlintCeiling;
#if defined(FINE_HIGHLIGHTS) && !defined(DEFERRED_SLIME)
    if (cSurface.y < .5)
    {
        // Compress the bright shoulder and return a compact tip to aligned facets.
        // Use the existing solar direction and pixel footprint at every swell sample.
        float excess = max(response - .40, 0.0);
        float shoulder = min(response, .40) + excess * .48 / (.48 + excess);
        float tip = FilteredCrestLobe(1.0 - facingHalf * facingHalf, .020, variance / 12.0);
        response = lerp(shoulder, response, tip);
    }
#endif
    return response;
}

float3 SunReflection(float3 normal, float3 toCamera, float2 world, float variance)
{
    return cSunColor.rgb * SunReflectionResponse(normal, toCamera, variance);
}

float3 SunReflection(float3 normal, float3 toCamera, float2 world)
{
    return SunReflection(normal, toCamera, world, NormalScreenVariance(normal));
}

float3 Scattered(float3 normal, float3 toCamera, float3 light)
{
    float through = saturate(dot(normalize(-toCamera + normal * kScatterNormalBend), cToLight.xyz));
    float crest = saturate(1.0 - normal.z) * kScatterCrestBoost + kScatterBase;
    return cShallowColor.rgb * light * pow(through, kScatterPower) * crest * cRippleGrid.z * cToLight.w * SunVisibility();
}

// How far the water has melted into the sky's colour at the horizon behind it. The client's fog does not reach the
// renderer's water, so with that fog on the water fades over the same range itself. The renderer's fog does reach
// it, as it reaches the land: then the water keeps its colour and reflections as far as the land stays in view, and
// melts into the sky only where it runs out, from the fog's end gently to just short of the far clip, so the ragged
// end of the drawn water never shows.
static const float kHorizonFadeEnd = 0.95;
static const float kHorizonFadeLatestStart = 0.6;

float DistanceFade(float distance)
{
    if (cGroundInfo.y < 0.5)
    {
        float fog = saturate((distance - cFogRange.x) / max(cFogRange.y - cFogRange.x, 1.0));
        return fog * fog;
    }
    return smoothstep(min(cFogRange.y, FarClip() * kHorizonFadeLatestStart), FarClip() * kHorizonFadeEnd, distance);
}

// The body under the surface: the view through it dimmed by the liquid it crosses towards the liquid's own colour.
float3 Transmitted(float3 refracted, float3 deep, float3 clearness, float bentThickness)
{
    // Under an overcast sky the colour the water gives what is seen through it is far less vivid: it absorbs more
    // evenly, dimming the bottom rather than turning it turquoise.
    float3 absorption = lerp(clearness, dot(clearness, 1.0 / 3.0), Overcast() * kOvercastEvenAbsorption);
    float3 transmittance = exp(-absorption * bentThickness / Clarity());
    return refracted * transmittance + deep * (1.0 - transmittance);
}

// What sets the two liquids apart, one set of functions each; ShadeLiquid below is shared.
#ifdef DEFERRED_SLIME
// Slime: a murky, glowing body with its own pattern, lit by the scene's brightness but not its hue, and a glow where
// it thins against anything it touches.

float ViewThickness(float2 pixel, float distance, float thickness)
{
    return thickness;
}

float3 WithSurfaceDetail(float3 normal, float2 uv, float3 world, float drawnHeight, float distance, float footprint)
{
    // Slime lies indoors as often as out, so its rings come only from the splashes the game draws (none indoors), not
    // from rain at random; being thick, it takes them more gently.
    float2 rings = RainRings(world, 0.0, distance, cSurface.w * kSlimeRingShare);
    return normalize(normal + float3(-rings, 0.0));
}

// The slime glows with its own colour: the scene light sets how bright it is, not its hue (the Undercity's purple
// light would otherwise grey the green).
float3 LiquidLight()
{
    return dot(SceneLight(), float3(0.2126, 0.7152, 0.0722));
}

// Seen through the surface, so the waves and ripples bend it as they would a view into the liquid.
float3 StockColour(float2 uv, float3 world, float3 normal, float3 light)
{
    return SlimeBodyPattern(world.xy - normal.xy * kSlimeBodyRefraction, light);
}

float3 WithCaustics(float3 refracted, float3 world, float3 toCamera, float bentThickness, float thickness)
{
    return refracted;
}

float3 SeenThrough(float3 refracted, float3 world, float3 toCamera, float bentThickness, float distance, float3 stock,
                   float3 light)
{
    float3 deep = lerp(DepthGradient(bentThickness) * light, stock, StockTint());
    // Slime is too murky to show a floor, so its big, slow waves shade the body itself (slopes facing the camera
    // catch more of the glow); the finer detail and the ripples show in its reflections.
    float2 across = normalize(toCamera.xy + 1.0e-5);
    float2 bodySlope = -LargeWaveSlope(world.xy) * WaveStrength();
    deep *= 1.0 + clamp(dot(bodySlope, across) * kSlimeWaveShading, -kSlimeWaveShadeLimit, kSlimeWaveShadeLimit);
    return Transmitted(refracted, deep, kAbsorption, bentThickness);
}

// Slime lies indoors and underground: a reflection that finds nothing shows a dim glow of the slime, not the sky
// remembered from outside.
float3 MissedReflection(float3 reflected, float3 light)
{
    return lerp(cShallowColor.rgb, cDeepColorDepth.rgb, 0.5) * light * kSlimeMissedReflection;
}

// A smooth glow that brightens as the slime thins towards anything it touches (walls, steps, legs), like the green
// edges of the depth view.
float LiquidFoam(float3 world, float3 toCamera, float drawnHeight, float thickness, float bentThickness, float distance,
                 float2 wakeFoam)
{
    return saturate(exp(-bentThickness / (FoamDepth() * kSlimeGlowWidth)) * FoamStrength());
}

bool DebugColour(float3 world, float drawnHeight, float foam, out float4 colour)
{
    colour = 0;
    return false;
}

// Where the liquid thins to nothing it fades to the scene under it, bent like the rest (an unbent copy would show
// whatever sits just under the surface twice).
float LiquidShown(float2 pixel, float thickness)
{
    return saturate(thickness / kShoreFade);
}

#else
// Water: a clear body over the seabed, with ship wakes, shore waves and foam, and rain rings from the rain and from
// the splashes the game draws.

// How far the view goes through the water, from the surface placed at its true height: the game draws neighbouring
// tiles a few inches apart, and at a low angle that made one look much deeper than the next.
float ViewThickness(float2 pixel, float distance, float thickness)
{
    float opaqueRaw = SampleRawDepth(sOpaqueDepth, pixel);
    float opaqueDistance = BeyondFarClip(opaqueRaw) ? 1.0e4 : length(ViewPosition(pixel, ViewZ(opaqueRaw)));
    return max(opaqueDistance - distance, 1.0e-3);
}

// normal with the shore's waves, the ships' wakes (their slope, wake) and the rain's rings on it.
float3 WithShoreWakesRain(float3 normal, float2 wake, float3 world, float drawnHeight, float distance, float footprint)
{
    float2 rings = RainRings(world, cSurface.z, distance, cSurface.w);
    return normalize(normal + float3(-ShoreWaveSlope(world.xy, drawnHeight, footprint) - wake - rings, 0.0));
}

// The ships' wakes' slope comes from the surface detail pass (READS_SURFACE_DETAIL), or is worked out here.
float3 WithSurfaceDetail(float3 normal, float2 uv, float3 world, float drawnHeight, float distance, float footprint)
{
#ifdef READS_SURFACE_DETAIL
    float2 wake = tex2Dlod(sSurfaceDetail, float4(uv, 0, 0)).xy;
#else
    float2 wake = ShipWake(world.xy, drawnHeight, footprint).xy;
#endif
    return WithShoreWakesRain(normal, wake, world, drawnHeight, distance, footprint);
}

float3 LiquidLight()
{
    return SceneLight();
}

float3 StockColour(float2 uv, float3 world, float3 normal, float3 light)
{
    return tex2Dlod(sSceneWithWater, float4(uv, 0, 0)).rgb;
}

// Sunlight focused by the waves into a moving net of bright lines on the bottom where the view ray meets it (see
// CausticsAt), faded out in the last shallows at the shore.
float3 WithCaustics(float3 refracted, float3 world, float3 toCamera, float bentThickness, float thickness)
{
    float3 viewDirection = -toCamera;
    float depth = bentThickness * saturate(-viewDirection.z);
    float3 bottom = world + viewDirection * bentThickness;
    float caustics = CausticsAt(bottom, depth, world.z, Overcast()) * saturate(thickness / kShoreFade);
    return refracted + refracted * caustics * cSunColor.rgb;
}

float3 SeenThrough(float3 refracted, float3 world, float3 toCamera, float bentThickness, float distance, float3 stock,
                   float3 light)
{
    // Seen from above, through lit water: the gradient runs from the shallow colour only part of the way to the deep
    // colour (SurfaceDeepness), never to the darkest; the full range is for the view from under the water.
    float3 gradient = lerp(cShallowColor.rgb, cDeepColorDepth.rgb,
                           saturate(bentThickness / cDeepColorDepth.w) * cSurface.x);
    // Dimmed at night as the view from under the water is, so the two match.
    float3 deep = lerp(gradient * light * lerp(kNightWaterLight, 1.0, saturate(cStyleExtras.w)), stock, StockTint());
    // Based on the view from under the water (UnderwaterVisibility), a little murkier.
    float3 clearness = kUnderwaterAbsorptionPerVisibility * kSurfaceMurk / max(cStyleExtras.x, 1.0);
    float3 underwater = Transmitted(refracted, deep, clearness, bentThickness);
    // A faint veil of the water's own colour even over the shallows (reaching full a little way down, so the waterline
    // shows no edge).
    underwater = lerp(underwater, deep, cLookingDown.y * (1.0 - exp(-bentThickness / kVeilDepth)));
    return underwater;
}

// How tall scenery past the fog's end may stand (yards) and still be seen fogged near the horizon.
static const float kHorizonFogHeight = 40.0;

// What the water reflects where its ray meets no mirrored scenery: the sky, but near the horizon, while the game has
// fog, the fog colour. The game shows fogged scenery or its fog-coloured horizon there, and the mirrored scene leaves out
// whatever lies wholly past the fog's end; the remembered sky low down is a guess wherever scenery hides it on screen.
float3 MissedReflection(float3 reflected, float3 light)
{
    float3 sky = SkyColor(reflected);
    float fogEnd = cFogRange.y;
    float band = fogEnd > max(cFogRange.x, 0.0) ? kHorizonFogHeight / fogEnd : 0.0;
    float fogged = band > 0.0 ? 1.0 - smoothstep(0.25 * band, band, reflected.z) : 0.0;
    return lerp(sky, cFogColor.rgb, fogged);
}

float LiquidFoam(float3 world, float3 toCamera, float drawnHeight, float thickness, float bentThickness, float distance,
                 float2 wakeFoam)
{
    float foam = 0.0;
    [branch] if (FoamStrength() > 0.0)
        foam = WaterFoam(FoamPoint(world.xy, toCamera), drawnHeight, bentThickness, distance,
                         PixelFootprint(distance, toCamera.z), wakeFoam, cLookingDown.w);
    return foam;
}

// Where the water thins to nothing at the shore it fades to the ground under it, bent like the rest (an unbent copy
// would show whatever sits just under the surface twice). Only over the ground (the ground map): against something
// standing in the water (a character, a post) the water is whole right up to where it meets it: faded there, it showed
// the bent view untinted, which just under the waterline lands on the sand filled in round the body, and drew a bright
// orange band along the body's outline. With the ground not known, the fade as at a shore.
float LiquidShown(float2 pixel, float thickness)
{
    float raw = SampleRawDepth(sOpaqueDepth, pixel);
    float3 met = WorldPosition(ViewPosition(pixel, ViewZ(raw)));
    float2 mapUv = RippleUv(met.xy);
    float4 map = InsideRippleArea(mapUv) ? tex2Dlod(sSeabed, float4(mapUv, 0, 0)) : 0.0;
    bool standing = !BeyondFarClip(raw) && raw < kSkyDepth && map.g > 0.5 && met.z - map.r > kOnGround;
    return standing ? 1.0 : saturate(thickness / kShoreFade);
}

// Development (SeabedDebug): the seabed map the foam is built from. Blue where nothing is known; otherwise green shows
// the depth (bright = shallow, dark = a yard or more) and red the foam. 2: the world's water height against the
// surface drawn here: red where the world's is higher, green lower (full at half a yard), blue where the world has no
// water height. 3: the foam where something stands in the water (green), and where it was marked this frame (red).
bool DebugColour(float3 world, float drawnHeight, float foam, out float4 colour)
{
    // (One return at the end: Microsoft's compiler mishandles early ones inside branches, and showed view 1 for view 3.)
    float mode = cGroundInfo.x;
    float2 gridUv = RippleUv(world.xy);
    bool inside = InsideRippleArea(gridUv);
    colour = 0;
    if (mode > 2.5)
    {
        float4 state = inside ? tex2Dlod(sRipples, float4(gridUv, 0, 0)) : 0;
        colour = float4(state.a, state.b, 0.0, 1.0);
    }
    else if (mode > 1.5)
    {
        float4 heights = inside ? tex2Dlod(sSeabed, float4(gridUv, 0, 0)) : 0;
        float difference = (heights.b - drawnHeight) * 2.0;
        colour = heights.a > 0.01 ? float4(saturate(difference), saturate(-difference), 1.0 - heights.a, 1)
                                  : float4(0, 0, 1, 1);
    }
    else if (mode > 0.5)
    {
        float2 seabed = SeabedDepth(world.xy, world.z);
        float known = saturate(seabed.y);
        float shallow = 1.0 - saturate(max(seabed.x, 0.0));
        colour = float4(foam, shallow * known, 1.0 - known, 1.0);
    }
    return mode > 0.5;
}
#endif

// The ray a wave facet reflects towards, seen from toCamera. A facet tipped away from the camera by more than the view
// rises above the water hides behind the crest in front of it, and its reflection (down into the water) is never
// seen: its tilt away is compressed against the view's elevation e (a tilt a becomes a·e / (e + 2a)), so the ray rises
// to e² / (e + 2a) above the horizon, nearing it but never crossing, and in order: neighbouring facets reflect
// neighbouring rays. (Turned back up where it went down, a ray from a facet tipped a little farther away jumped from the
// foot of a hull over its top, drawing a thin line of what lay beyond along every wave; laid flat instead, all such
// rays met one thin slice at the waterline.)
float3 MaskedReflection(float3 toCamera, float3 normal, float3 baseNormal)
{
    float elevation = max(dot(toCamera, baseNormal), 0.001);
    float3 tangent = toCamera - baseNormal * dot(toCamera, baseNormal);
    float3 away = -tangent / max(length(tangent), 1.0e-4);
    float3 slope = baseNormal - normal / max(dot(normal, baseNormal), 1.0e-3);
    float along = dot(slope, away);
    float tippedAway = max(-along, 0.0);
    float kept = tippedAway * elevation / (elevation + 2.0 * tippedAway);
    slope += away * (tippedAway - kept);
    float3 reflected = reflect(-toCamera, normalize(baseNormal - slope));
    reflected += baseNormal * max(1.0e-4 - dot(reflected, baseNormal), 0.0);
    return normalize(reflected);
}

// The liquid's surface at a pixel, as its shading and the reflection pass both need it: where it is (drawn, and
// placed at its true height), how far from the camera, how much liquid the view crosses, its normal with every wave,
// ripple and wake on it, how glancing the view of it is, and the ray it reflects.
struct LiquidSurface
{
    float2 uv;
    float drawnHeight;
    float3 world;
    float distance;
    float3 toCamera;
    float3 baseNormal; // one geometric plane shared by material filtering and reflection lookup
    float thickness;
    float3 normal;
    // The slope foam lies on (see FoamNormal).
    float3 foamNormal;
    // Water: how much foam covers the surface here (from the surface detail pass).
    float foam;
    float facing;
    float glancing;
    float3 reflected;
};

// Where the surface is at pixel: the height the client drew it at, where it lies placed at its true height, how far it
// is from the camera and the way to the camera.
#include "liquid_geometry.hlsli"

float3 SurfaceBaseNormal(float2 pixel, float depth)
{
#ifdef DEFERRED_SLIME
    return float3(0,0,1);
#else
    return LiquidGeometryNormal(pixel,depth);
#endif
}
void PlaceSurfaceOnPlane(float2 pixel, float surfaceDepth, float3 baseNormal, out float drawnHeight,
                         out float3 world, out float distance, out float3 toCamera)
{
    float3 view = ViewPosition(pixel, ViewZ(surfaceDepth));
    world = WorldPosition(view);
    drawnHeight = world.z;
    // Height stabilization belongs to nearly horizontal water. Projecting rising water onto a horizontal
    // plane changes sides at the world horizon and produces a straight join across the river.
    float sloped = smoothstep(1.0e-5, 1.0e-3, 1.0 - baseNormal.z);
    world = lerp(FlattenedSurface(world), world, sloped);
    distance = length(world - cViewToWorld3.xyz);
    toCamera = normalize(cViewToWorld3.xyz - world);
}

void PlaceSurface(float2 pixel, float surfaceDepth, out float drawnHeight, out float3 world, out float distance,
                  out float3 toCamera, out float3 baseNormal)
{
    baseNormal = SurfaceBaseNormal(pixel,surfaceDepth);
    PlaceSurfaceOnPlane(pixel,surfaceDepth,baseNormal,drawnHeight,world,distance,toCamera);
}

// The surface's normal before the shore's waves, the wakes and the rain (see WithSurfaceDetail): the waves, the game's
// own surface's relief and the ripples; and the slope foam lies on (see FoamNormal).
float3 WaveNormal(float3 world, float distance, float3 toCamera, float3 baseNormal, float footprint, float2 uv, out float3 foamNormal)
{
    float2 fineSlope;
    float2 rippleSlope = RippleSlope(world.xy);
    float3 normal = SurfaceNormal(world.xy, distance, dot(toCamera, baseNormal), footprint, fineSlope);
#ifdef DEFERRED_SLIME
    normal = WithStockRelief(normal, uv);
#endif
    foamNormal = FoamNormal(fineSlope, rippleSlope);
    return normalize(normal + float3(-rippleSlope, 0.0));
}

LiquidSurface SurfaceAt(float2 pixel, float surfaceDepth, float thickness)
{
    LiquidSurface surface;
    surface.uv = (pixel + 0.5) * cTexelGrid.xy;
    PlaceSurface(pixel, surfaceDepth, surface.drawnHeight, surface.world, surface.distance, surface.toCamera, surface.baseNormal);
    float3 baseNormal = surface.baseNormal;
    surface.thickness = ViewThickness(pixel, surface.distance, thickness);
    float footprint = PixelFootprint(surface.distance, dot(surface.toCamera, baseNormal));
#ifdef READS_SURFACE_DETAIL
    // Worked out once in the surface detail pass.
    surface.normal = normalize(float3(tex2Dlod(sSurfaceDetail, float4(surface.uv, 0, 0)).xy, 1.0));
    surface.foamNormal = normalize(float3(tex2Dlod(sSurfaceSlopes, float4(surface.uv, 0, 0)).xy, 1.0));
#elif !defined(REFLECTION_PRECOMPUTED_NORMALS)
    {
        float3 normal = WaveNormal(surface.world, surface.distance, surface.toCamera, baseNormal, footprint, surface.uv,
                                   surface.foamNormal);
        surface.normal =
            WithSurfaceDetail(normal, surface.uv, surface.world, surface.drawnHeight, surface.distance, footprint);
    }
#endif
#ifdef READS_SURFACE_DETAIL
    surface.foam = tex2Dlod(sSurfaceDetail, float4(surface.uv, 0, 0)).z;
#else
    surface.foam = 0.0;
#endif
    surface.facing = saturate(dot(surface.normal, surface.toCamera));
    // Towards glancing angles the surface becomes the near-perfect mirror real water is.
    surface.glancing = smoothstep(kGlancingStart, kGlancingFull, 1.0 - surface.facing);
    // At a glancing angle the wave faces tilted away from the camera hide behind those facing it, so the surface
    // reflects more nearly as a mirror: distant things show whole in it rather than torn into strips.
    float3 mirrorNormal = normalize(lerp(surface.normal, baseNormal, surface.glancing * kGlancingCalm));
    [branch] if (FlatReflectionSurface()) mirrorNormal = baseNormal;
    surface.reflected = MaskedReflection(surface.toCamera, mirrorNormal, baseNormal);
    return surface;
}

#include "reflection_lookup.hlsli"

// The reflection pass at a pixel: the mirrored scene's colour, premultiplied by how much it covers.
float4 ReflectionTexel(float2 pixel)
{
    return tex2Dlod(sReflection, float4((pixel + 0.5) * cTexelGrid.xy, 0, 0));
}

// How much the mirrored scene covers a pixel's reflection (0 to 1); none off the view.
float ReflectionCoverage(float2 pixel)
{
    return InsideViewport(pixel) ? ReflectionTexel(pixel).a : 0.0;
}

// The reflection pass has already resolved the slope and wave lookup against the independent
// scenery. The composite reads it at its receiving pixel, so foreground masks cannot move.
float4 SceneReflection(float2 pixel)
{
    float4 colour = ReflectionTexel(pixel);
    return float4(colour.rgb / max(colour.a, 1.0e-4), ReflectionsDrawn() ? colour.a : 0.0);
}

// How far either side mirrored scenery may stand from a reflection gap for the gap to take the fog colour, as a
// fraction of the view's width (the same angle at any resolution), and how tall scenery past the fog's end may be
// (yards).
static const float kGapReach = 0.0375;
static const float kGapFogHeight = 60.0;

// Where the mirror shows nothing low over the water with mirrored scenery close on both sides: a gap between nearer
// trunks, where the reflection leaves out the distance beyond (wholly past the fog's end, so not drawn) and the game
// shows fog. Such a gap takes the fog colour (0 to 1). It eases out with height and with the gap's width, so where it
// does cover sky it has no hard edge; open sea, a clear horizon and anything higher keep the sky.
float ReflectionGap(float2 pixel, float3 reflected)
{
    float fogEnd = cFogRange.y;
    float band = fogEnd > max(cFogRange.x, 0.0) ? kGapFogHeight / fogEnd : 0.0;
    float low = band > 0.0 ? 1.0 - smoothstep(0.5 * band, 2.0 * band, reflected.z) : 0.0;
    float left = 0.0;
    float right = 0.0;
    [branch] if (low > 0.0 && ReflectionsDrawn())
    {
        float step = AcrossView(kGapReach) / 4.0;
        [unroll] for (int i = 1; i <= 4; ++i)
        {
            // Nearer scenery counts more, so a widening gap fades back to the sky rather than switching.
            float weight = 1.0 - 0.2 * (i - 1);
            float2 reach = float2(step * i, 0.0);
            left = max(left, weight * ReflectionCoverage(pixel - reach));
            right = max(right, weight * ReflectionCoverage(pixel + reach));
        }
    }
    return low * min(left, right);
}

// ReflectionDebug: the state of the nearest reflection texel (see water.ini).
bool ReflectionDebugColour(float2 pixel, LiquidSurface surface, out float4 colour)
{
    colour = 0.0;
#ifndef DEFERRED_SLIME
    float4 hit = tex2Dlod(sReflectionHit, float4((pixel + 0.5) * cTexelGrid.xy, 0, 0));
    float state = hit.r;
    colour = state < 2.5 ? float4(1, 1, 0, 1) : float4(0, 0.7, 0, 1);
    // 2: how steeply the reflection rises (grey, white at 0.1 and up); 3: how far the ray went to what it met (grey,
    // white at 60 yd), red where it met nothing.
    if (cReflectionGrid.w > 1.5)
        colour = float4(saturate(surface.reflected.z * 10.0).xxx, 1.0);
    if (cReflectionGrid.w > 2.5)
        colour = state > 2.5 ? float4(saturate(hit.g / 60.0).xxx, 1.0) : float4(1, 0, 0, 1);
    // 4: how strongly the mirrored geometry shows (grey, white for wholly).
    [branch] if (cReflectionGrid.w > 3.5)
        colour = float4(SceneReflection(pixel).a.xxx, 1);
    // 5: how far the waves bend the lookup, against the limit (red across, green up and down; full at the limit), blue
    // outside the main viewport (the scenery image can have a wider view), black where bending is off.
    [branch] if (cReflectionGrid.w > 4.5)
    {
        float2 bend = ReflectionBend(pixel, surface);
        float2 warped = pixel + bend;
        bool on = cReflectionOptions.z > 0.5 && !FlatReflectionSurface();
        colour = on ? float4(saturate(abs(bend) / max(AcrossView(cReflectionTuning.y), 1.0)), InsideViewport(warped) ? 0.0 : 1.0, 1.0) : float4(0, 0, 0, 1);
    }
    return cReflectionGrid.w > 0.5;
#else
    return false;
#endif
}

#ifdef WATER_FILTERED_MATERIAL
// Match the ordinary surface's Fresnel and glancing reflection policy for each subpixel slope.
float MaterialFresnel(float3 normal, float3 toCamera, float3 baseNormal, float distanceRamp, out float3 reflected)
{
    float facing = saturate(dot(normal, toCamera));
    float glancing = smoothstep(kGlancingStart, kGlancingFull, 1.0 - facing);
    float strength = lerp(ReflectionStrength() * distanceRamp, 1.0, glancing);
    float3 mirrorNormal = normalize(lerp(normal, baseNormal, glancing * kGlancingCalm));
    if (FlatReflectionSurface()) mirrorNormal = baseNormal;
    reflected = MaskedReflection(toCamera, mirrorNormal, baseNormal);
    return min(max(kWaterF0 + (1.0 - kWaterF0) * pow(1.0 - facing, 5.0), kSheenFloor) * strength,
               lerp(kMaxFresnel, kMaxGlancingFresnel, glancing));
}

#ifdef WATER_WAVE_MOMENT_MATERIAL
// Positive-semidefinite slope covariance, stored by the same detail producer as the mean.
float2 MomentSlopeOffset(float2 sign, float2 uv)
{
    float4 detail = tex2Dlod(sSurfaceDetail, float4(uv * cModeProbe.w, 0, 0));
    // Keep these late reads separate from SurfaceAt: retaining unused covariance channels
    // through scene reflection exceeds SM3 temporaries. c8.w is the shared runtime-one invariant.
    float4 slopes = tex2Dlod(sSurfaceSlopes, float4(uv * cModeProbe.w, 0, 0));
    float xx = max(detail.a, 0.0), yy = max(slopes.z, 0.0);
    float xy = clamp(slopes.w, -sqrt(xx * yy), sqrt(xx * yy));
    float root = sqrt(max(xx, 1.0e-12));
    float cross = xy / root;
    return float2(root * sign.x, cross * sign.x + sqrt(max(yy - cross * cross, 0.0)) * sign.y);
}
#endif

// Four normalized slope-space samples preserve mean/covariance. Average complete responses,
// including the below-horizon light test, instead of only widening the GGX lobe.
void AddFilteredSwellEnvironment(float3 normal, float3 toCamera, float3 baseNormal, float distanceRamp,
                                 float variance, float3 underwater, float weight, inout float3 color
#ifdef WATER_WAVE_MOMENT_MATERIAL
                                 , float2 sampleUv
#endif
                                 )
{
    float2 slope = -normal.xy / max(normal.z, 1.0e-3);
    float sigma = sqrt(max(variance, 0.0));
    [loop] for (int tap = 0; tap < 4; ++tap)
    {
        float2 offset = float2(tap < 2 ? -1.0 : 1.0, fmod(tap, 2) < 0.5 ? -1.0 : 1.0) * sigma;
#ifdef WATER_WAVE_MOMENT_MATERIAL
        offset = MomentSlopeOffset(float2(tap < 2 ? -1.0 : 1.0, fmod(tap, 2) < 0.5 ? -1.0 : 1.0), sampleUv);
#endif
        float3 facet = normalize(float3(-(slope + offset), 1));
        float3 direction;
        float fresnel = MaterialFresnel(facet, toCamera, baseNormal, distanceRamp, direction);
        // The shared control is enabled only while a programmatic environment is available.
        color += lerp(underwater, SurfaceEnvironmentColor(direction), fresnel) * (0.25 * weight);
    }
}
float3 FilteredSwellSun(float3 normal, float3 toCamera, float variance, float screenVariance
#ifdef WATER_WAVE_MOMENT_MATERIAL
                       , float2 sampleUv
#endif
                       )
{
    float2 slope = -normal.xy / max(normal.z, 1.0e-3);
    float sigma = sqrt(max(variance, 0.0));
    float response = 0;
    [loop] for (int tap = 0; tap < 4; ++tap)
    {
        float2 offset = float2(tap < 2 ? -1.0 : 1.0, fmod(tap, 2) < 0.5 ? -1.0 : 1.0) * sigma;
#ifdef WATER_WAVE_MOMENT_MATERIAL
        offset = MomentSlopeOffset(float2(tap < 2 ? -1.0 : 1.0, fmod(tap, 2) < 0.5 ? -1.0 : 1.0), sampleUv);
#endif
        float3 facet = normalize(float3(-(slope + offset), 1));
        response += SunReflectionResponse(facet, toCamera, screenVariance) * 0.25;
    }
    return cSunColor.rgb * response;
}

#endif

#ifdef FINE_HIGHLIGHTS
float3 HighlightNormal(float3 normal, float3 world, float distance, float3 toCamera, float3 baseNormal)
{
    float footprint = PixelFootprint(distance, dot(toCamera, baseNormal));
    float resolved = Resolved(1.0 / 6.5, footprint) * (1.0 - smoothstep(16.0, 48.0, distance));
    [branch] if (resolved > 0 && cToLight.w * SunVisibility() * SpecularStrength() > 0)
    {
        // Capillary waves break up the reflected disc while each cell still spans several screen pixels.
        float2 fine = Octave(world.xy, float2x2(0.8, -0.6, 0.6, 0.8), 6.5, float2(-0.42, 0.55), 0.010);
        normal = normalize(float3(normal.xy - normal.z * fine * (resolved * WaveStrength()), normal.z));
    }
    return normal;
}
#endif

float4 ShadeLiquid(float2 pixel, float surfaceDepth, float thickness)
{
    LiquidSurface surface = SurfaceAt(pixel, surfaceDepth, thickness);
    float2 uv = surface.uv;
    float drawnHeight = surface.drawnHeight;
    float3 world = surface.world;
    float distance = surface.distance;
    float3 toCamera = surface.toCamera;
    thickness = surface.thickness;
    float3 normal = surface.normal;
    float3 light = LiquidLight();
    float3 stock = StockColour(uv, world, normal, light);
    float bentThickness;
    float3 refracted = Refracted(pixel, world, normal, thickness, surfaceDepth, bentThickness);
#ifdef DEFERRED_SLIME
    // Slime's glowing edge follows the bent view's depth.
    float foam = LiquidFoam(world, toCamera, drawnHeight, thickness, bentThickness, distance, 0.0);
#else
    float foam = surface.foam;
#endif
#ifndef DEFERRED_SLIME
    // RefractionDebug (see water.ini): the parts of the view into the water one at a time.
    float refractionDebug = cWaterDebug.x;
    if (refractionDebug > 0.5 && refractionDebug < 4.5)
    {
        float3 shown = refracted;
        float2 here = (pixel + 0.5) * cTexelGrid.xy;
        if (refractionDebug > 1.5)
            shown = tex2Dlod(sUnderwaterScene, float4(here, 0, 0)).rgb;
        if (refractionDebug > 2.5)
            shown = saturate(tex2Dlod(sUnderwaterThickness, float4(here, 0, 0)).g / 5.0).xxx;
        if (refractionDebug > 3.5)
            shown = saturate(thickness / 5.0).xxx;
        return float4(shown, 1.0);
    }
    if (refractionDebug > 5.5)
        refracted = 0.5;

#endif
    refracted = WithCaustics(refracted, world, toCamera, bentThickness, thickness);
    float sceneExposure=1;
#ifndef DEFERRED_SLIME
    sceneExposure=cWaterDebug.y>0?cWaterDebug.y:1;
#endif
    float3 underwater = SeenThrough(refracted*sceneExposure, world, toCamera, bentThickness, distance, stock, light);

    float facing = surface.facing;
    float distanceRamp = lerp(cInsideAndReflection.z, 1.0, saturate(distance / cInsideAndReflection.w));
    // A small floor keeps a faint moving sheen of sky on the waves even looking straight down.
    // The Reflection setting and the near-camera reduction apply looking down; towards glancing angles the surface
    // becomes the near-perfect mirror real water is, and the bottom disappears behind it.
    float glancing = surface.glancing;
    float strength = lerp(ReflectionStrength() * distanceRamp, 1.0, glancing);
    float fresnel = min(max(kWaterF0 + (1.0 - kWaterF0) * pow(1.0 - facing, 5.0), kSheenFloor) * strength,
                        lerp(kMaxFresnel, kMaxGlancingFresnel, glancing));
    float3 reflected = surface.reflected;
    float3 sky = MissedReflection(reflected, light);
    float skyWeight = 1.0;
#ifndef DEFERRED_SLIME
    float4 mirrored = 0.0;
#endif
    [branch] if (ScreenReflectionsOn())
    {
        float4 screen = SceneReflection(pixel);
#ifdef DEFERRED_SLIME
        // Keep reflected shapes legible against the luminous body at ordinary
        // bank angles. Missing scenery retains the material's original sheen.
        fresnel = max(fresnel, 0.26 * saturate(ReflectionStrength() * distanceRamp) * screen.a);
#endif
#ifndef DEFERRED_SLIME
        // A gap between mirrored scenery takes the fog colour as if mirrored, so later sky handling leaves it alone.
        float gap = ReflectionGap(pixel, reflected) * (1.0 - screen.a);
        screen.rgb = (screen.rgb * screen.a + cFogColor.rgb * gap) / max(screen.a + gap, 1.0e-4);
        screen.a += gap;
        mirrored = screen;
#endif
#ifndef DEFERRED_SLIME
        screen.rgb*=sceneExposure;
#endif
        // Where the mirror shows nothing, the sky. Mirrored scenery already carries native fog over its mirrored path.
        sky = lerp(sky, screen.rgb, screen.a);
        skyWeight = 1.0 - screen.a;
    }
#ifndef DEFERRED_SLIME
    // Reflection lighting alone, before Fresnel, glints, foam, refraction and water-distance fade (ReflectionView).
    [branch] if (ReflectionView() > 0.5)
    {
        float3 shown = sky;
        if (ReflectionView() > 1.5) shown = mirrored.rgb;
        if (ReflectionView() > 2.5) shown = mirrored.a.xxx;
        if (ReflectionView() > 3.5) shown = BeyondWaterColor(world,toCamera);
        return float4(shown, 1);
    }
    if (cWaterDebug.x > 4.5 && cWaterDebug.x < 5.5)
        fresnel = 0.0;
#endif
#ifdef DEFERRED_SLIME
    const float3 kLuma = float3(0.2126, 0.7152, 0.0722);
    sky = lerp(sky, dot(sky, kLuma) * cShallowColor.rgb / max(dot(cShallowColor.rgb, kLuma), 0.01), kSlimeReflectionPigment);
#endif
    float3 color = lerp(underwater, sky, fresnel);

    float3 glintNormal = normal;
#ifdef FINE_HIGHLIGHTS
    glintNormal = HighlightNormal(normal, world, distance, toCamera, surface.baseNormal);
#endif
#ifdef WATER_FILTERED_MATERIAL
    // Screen derivatives describe the resolved mean normal. Sharing this estimate across
    // normalized slope samples avoids derivative work inside a conditional SM3 branch.
    float screenVariance = NormalScreenVariance(glintNormal);
#ifndef DEFERRED_SLIME
    // (RefractionDebug 5 shows no reflection: fresnel is 0 above, so the samples would give back what is taken away.)
    [branch] if (FilterDistantSwell() && !(cWaterDebug.x > 4.5 && cWaterDebug.x < 5.5))
    {
        // Replace only actual sky mass and its underwater complement. Apply subtraction before
        // the loop, so old fallback intermediates do not stay live through each sampled response.
        color -= skyWeight * lerp(underwater, MissedReflection(reflected, light), fresnel);
        float3 baseNormal = surface.baseNormal;
        AddFilteredSwellEnvironment(normal, toCamera, baseNormal, distanceRamp,
            UnresolvedSwellVariance(PixelFootprint(distance, dot(toCamera, baseNormal))), underwater, skyWeight, color
#ifdef WATER_WAVE_MOMENT_MATERIAL
            , uv
#endif
            );
    }
#endif
    float3 sun;
#ifndef DEFERRED_SLIME
    [branch] if (FilterDistantSwell())
        sun = FilteredSwellSun(glintNormal, toCamera,
            UnresolvedSwellVariance(PixelFootprint(distance, dot(toCamera, surface.baseNormal))), screenVariance
#ifdef WATER_WAVE_MOMENT_MATERIAL
            , uv
#endif
            );
    else
#endif
        sun = SunReflection(glintNormal, toCamera, world.xy, screenVariance);
#else
    float3 sun = SunReflection(glintNormal, toCamera, world.xy);
#endif
#ifdef SLIME_SURFACE_SHELTER
    // The mirror resolve marks only pixels owned by an authored indoor liquid.
    [branch] if (cReflectionGrid.w > 0.5)
        sun *= 1.0 - tex2Dlod(sReflectionHit, float4(uv, 0, 0)).b;
#endif
    float directVisibility = DirectLightVisibility(pixel);
    color += sun * directVisibility;
    color += Scattered(normal, toCamera, light) * (1.0 - fresnel) * (1.0 - Overcast() * kOvercastScatterLoss);

    float3 foamColour = cFoamColor.rgb;
#ifndef DEFERRED_SLIME
    // Tinted towards a light water colour (see FoamLines).
    foamColour *= lerp(1.0, kFoamTintColour, kFoamTint);
#endif
    float3 foamLit = lerp(FoamLight(directVisibility) * foamColour * FoamRelief(surface.foamNormal),
                          cFoamColor.rgb * kGlowBrightness, saturate(FoamGlow()));
#ifndef DEFERRED_SLIME
    // Wet foam catches a broad highlight around the glints, including the
    // space between bright facets. Sheltered foam keeps its diffuse light.
    float alignment = saturate(dot(toCamera, float3(-cToLight.xy, cToLight.z)));
    float wetHighlight = pow(alignment, 8.0) * directVisibility *
                         saturate(cToLight.w * SunVisibility() * SpecularStrength());
    foamLit += cSunColor.rgb * (0.3 * wetHighlight);
#endif
    color = lerp(color, foamLit, foam) + cFoamColor.rgb * foam * FoamGlow() * kGlowSpill;

    [flatten]if(cEnvironment.x>.5&&cSkyLayout.x>1.5){
        // Analytic horizontal-plane footprint, evaluated after material work to keep ps_3_0 temporaries short-lived.
        float viewDepth=ViewZ(surfaceDepth);
        float pixelSpan=cEnvironmentLight.y*viewDepth*viewDepth/max(abs(drawnHeight-cViewToWorld3.z),1.0e-4);
        float farCoverage=saturate((FarClip()-viewDepth)/max(2.0*pixelSpan,1.0));
        float transmission=exp(cEnvironment.w*distance/max(cEnvironment.z,1.0))*farCoverage;
        // Reflections follow the mirrored ray; the intervening air follows the viewing ray.
        float3 atmosphere=SurfaceEnvironmentColor(-toCamera);
        color=lerp(atmosphere,color,transmission);
    }else if(cSkyLayout.x<1.5) color=lerp(color,BeyondWaterColor(world,toCamera),DistanceFade(distance));
    // WaterGameFog: the whole surface behind the same air as the land beside it, fogged as the game fogs its own
    // surfaces. Mirrored scenery already carries fog over its mirrored path, so it fades a little sooner than exact.
    color=lerp(color,cFogColor.rgb,GameFog(distance,toCamera));

#ifdef WATER_FILTERED_MATERIAL
    // At grazing angles the last few screen rows can span hundreds of yards.
    // Anchor only the open edge to visible sky, in the same pre-composite space.
    float belowEdge=(cViewToWorld3.z-drawnHeight)*(1.0/ViewZ(surfaceDepth)-1.0/FarClip());
    float edgePixels=max(0,belowEdge)/max(cEnvironmentLight.y,1e-6);
    float edgeWidth=max(2.0,.012*cViewportPx.w);
    [branch] if(cEnvironmentLight.w>.5 && cEnvironment.y>.5 && toCamera.z>0 && edgePixels<edgeWidth && cViewToWorld1.z>.5){
        float2 tap=pixel-float2(0,edgePixels+3);
        [branch] if(InsideViewport(tap) && IsTrueSky(SampleRawDepth(sOpaqueDepth,pixel)) &&
                    IsTrueSky(SampleRawDepth(sOpaqueDepth,tap)) && IsTrueSky(SampleRawDepth(sDepth,tap))){
            float3 backdrop=tex2Dlod(sSceneBefore,float4((tap+.5)*cTexelGrid.xy,0,0)).rgb;
            color=lerp(backdrop,color,smoothstep(0,edgeWidth,edgePixels));
        }
    }
#endif

    // (Each debug view into its own colour: || does not stop at the first here, so the reflection view's colour
    // overwrote the seabed view's.)
    float4 seabedDebug;
    float4 reflectionDebug;
    bool seabedShown = DebugColour(world, drawnHeight, foam, seabedDebug);
    bool reflectionShown = ReflectionDebugColour(pixel, surface, reflectionDebug);
    float4 shaded = float4(lerp(refracted, color, LiquidShown(pixel, thickness)), 1.0);
    return seabedShown ? seabedDebug : reflectionShown ? reflectionDebug : shaded;
}

#if defined(DEFERRED_SLIME) && !defined(REFLECTION_PASS)
struct DeferredSlimeOutput
{
    float4 color : COLOR0;
    float depth : DEPTH;
};

#include "slime_fade.hlsli"

DeferredSlimeOutput main(float2 pixel : VPOS)
{
    float surfaceDepth = SampleRawDepth(sDepth, pixel);
    float thickness = WaterThickness(pixel, surfaceDepth);
    clip(thickness);
    DeferredSlimeOutput output;
    // Over water where the slime style fades: the composited water shows through by the share.
    float share = SlimeFadeShare(pixel, surfaceDepth);
#ifdef SLIME_REFLECTION_VIEW
    // Inspect the same resolved texel as the slime composite, before pigment and Fresnel.
    float4 mirrored = 0;
    [branch] if (ReflectionsDrawn()) mirrored = SceneReflection(pixel);
    output.color = lerp(tex2Dlod(sSceneBefore, float4((pixel + 0.5) * cTexelGrid.xy, 0, 0)),
                        float4(ReflectionView() > 2.5 ? mirrored.aaa : mirrored.rgb, 1), share);
#else
    output.color = lerp(tex2Dlod(sSceneBefore, float4((pixel + 0.5) * cTexelGrid.xy, 0, 0)),
                        ShadeLiquid(pixel, surfaceDepth, thickness), share);
#endif
    output.depth = surfaceDepth;
    return output;
}
#elif defined(SURFACE_DETAIL_PASS)
#ifdef WATER_WAVE_MOMENT_DETAIL
// Fixed stratified samples of this pixel, with no history or per-frame randomization.
// Local ripple/shore/wake/rain offsets stay at the centre, in their existing order;
// their nonlinear transforms are applied before accumulating final slope moments.
void PixelWaveMoments(float2 pixel, out float2 mean, out float3 second, out float blend, out float2 geometricSlope)
{
    float height, distance;
    float3 world, toCamera, baseNormal;
    PlaceSurface(pixel, SampleRawDepth(sDepth,pixel), height, world, distance, toCamera, baseNormal);
    geometricSlope = baseNormal.xy/baseNormal.z;
    float footprint = PixelFootprint(distance, dot(toCamera,baseNormal));
    mean = 0;
    second = 0;
    // Switch before Resolved starts discarding the finest swell (cell=1/0.63;
    // loss begins at a footprint of about0.397 yards). A later handoff first
    // adds whole-octave estimated roughness, then removes it again, drawing a
    // blurry band followed by sharper water. Keep the measured moments coherent.
    blend = smoothstep(0.2, 0.4, footprint);
    // The local pixel footprint on the actual surface. Differentiate view-space positions first:
    // world-coordinate precision at x=5000 is not enough for neighbouring pixels. A local linear
    // footprint, like texture filtering, avoids intersecting samples with a horizontal plane.
    float3 view = ViewPosition(pixel, ViewZ(SampleRawDepth(sDepth,pixel)));
    float3 vx = ddx(view), vy = ddy(view);
    float2 dx = vx.x*cViewToWorld0.xy + vx.y*cViewToWorld1.xy + vx.z*cViewToWorld2.xy;
    float2 dy = vy.x*cViewToWorld0.xy + vy.y*cViewToWorld1.xy + vy.z*cViewToWorld2.xy;
    float elevation = dot(toCamera,baseNormal);
    // No averaging across a discontinuous depth edge or the local surface's own horizon.
    float extent = max(length(vx),length(vy));
    if (!WaveMomentsOn() || elevation <= 0.0 || extent > distance*0.25 || GameFog(distance,toCamera)>=1.0) blend=0;
    [branch] if (blend > 0)
    {
        float2 ripple = RippleSlope(world.xy);
        float2 other = ShoreWaveSlope(world.xy, height, footprint) + ShipWake(world.xy,height,footprint).xy +
                       RainRings(world, cSurface.z, distance, cSurface.w);
        // Two columns by cWaveMomentSamples.x rows (WaterWaveMomentSamples / 2): rows run along the view, where the
        // footprint stretches.
        float rows = cWaveMomentSamples.x > 0.5 ? cWaveMomentSamples.x : 32.0; // zero (old captures): the reference
        // A fixed bound (the 64-sample maximum) with an early exit gives the same code as a bound read from the
        // constant, but compiles in about 3 minutes instead of 20.
        [loop] for (int index = 0; index < 64; ++index)
        {
            float tap = index;
            [branch] if (tap >= 2.0 * rows) break;
            float2 at = float2((fmod(tap, 2.0) + 0.5) * 0.5 - 0.5, (floor(tap * 0.5) + 0.5) / rows - 0.5);
            float2 sampleWorld = world.xy + dx*at.x + dy*at.y;
            float2 unusedFine;
            float2 slope = -SurfaceSlope(sampleWorld, distance, elevation,
                                         footprint * (cModeProbe.w - 1.0), unusedFine, false);
            slope -= ripple * sqrt(1.0 + dot(slope, slope));
            slope -= other * sqrt(1.0 + dot(slope, slope));
            mean += slope;
            second += float3(slope * slope, slope.x * slope.y);
        }
        mean *= 0.5 / rows;
        second *= 0.5 / rows;
    }
}

#endif
// The surface detail pass (see Renderer::DrawSurfaceDetail): at each water pixel, the ships' wakes' slope (x, y) and all
// the foam (z), for the reflection pass and the composite to read; the whole surface's slope in x, y (the normal's x
// and y over its z), and the slope foam lies on in the second target. The depth along the
// view stands in for the bent view's (which the foam's dapple is faded by), which only the composite knows.
struct DetailOutput
{
    float4 detail : COLOR0;
    float4 slopes : COLOR1;
};

DetailOutput main(float2 pixel : VPOS)
{
    float surfaceDepth = SampleRawDepth(sDepth, pixel);
    float thickness = WaterThickness(pixel, surfaceDepth);
    clip(thickness);
#ifdef WATER_WAVE_MOMENT_DETAIL
    // Finish the sampling loop before foam, wakes and resolved normals become live.
    float2 mean;
    float3 second;
    float momentBlend;
    float2 geometricSlope;
    PixelWaveMoments(pixel, mean, second, momentBlend, geometricSlope);
    pixel *= cModeProbe.w; // late reconstruction, without retaining the first surface through the loop
    surfaceDepth = SampleRawDepth(sDepth,pixel);
    thickness = WaterThickness(pixel,surfaceDepth);
#endif
    float drawnHeight;
    float3 world;
    float distance;
    float3 toCamera;
    float3 baseNormal;
#ifdef WATER_WAVE_MOMENT_DETAIL
    // Retain only the plane's two slopes through the sampling loop. Reusing it
    // avoids a second depth neighbourhood while the moment outputs are live.
    baseNormal = normalize(float3(geometricSlope,1));
    PlaceSurfaceOnPlane(pixel,surfaceDepth,baseNormal,drawnHeight,world,distance,toCamera);
#else
    PlaceSurface(pixel, surfaceDepth, drawnHeight, world, distance, toCamera, baseNormal);
#endif
    float depth = ViewThickness(pixel, distance, thickness);
    float footprint = PixelFootprint(distance, dot(toCamera, baseNormal));
    float4 wake = ShipWake(world.xy, drawnHeight, footprint);
    float foam = LiquidFoam(world, toCamera, drawnHeight, depth, depth, distance, wake.zw);
    DetailOutput output;
    output.detail = float4(wake.xy, foam, 1.0);
    output.slopes = 0.0;
    {
        float2 uv = (pixel + 0.5) * cTexelGrid.xy;
        float3 foamNormal;
        float3 normal = WithShoreWakesRain(WaveNormal(world, distance, toCamera, baseNormal, footprint, uv, foamNormal), wake.xy,
                                           world, drawnHeight, distance, footprint);
#ifdef WATER_WAVE_MOMENT_DETAIL
        float variance = UnresolvedSwellVariance(footprint);
        float3 covariance = float3(variance, variance, 0);
        float2 oldMean = normal.xy / max(normal.z, 1.0e-6);
        float3 oldSecond = covariance + float3(oldMean * oldMean, oldMean.x * oldMean.y);
        mean = lerp(oldMean, mean, momentBlend);
        second = lerp(oldSecond, second, momentBlend);
        covariance = second - float3(mean * mean, mean.x * mean.y);
        covariance.xy = max(covariance.xy, 0.0);
        float bound = sqrt(covariance.x * covariance.y);
        covariance.z = clamp(covariance.z, -bound, bound);
        normal = normalize(float3(mean, 1));
#endif
        float2 baseSlope = baseNormal.xy / baseNormal.z;
        output.detail.xy = normal.xy / normal.z + baseSlope;
        output.slopes = float4(foamNormal.xy / foamNormal.z + baseSlope, 0.0, 1.0);
#ifdef WATER_WAVE_MOMENT_DETAIL
        [branch] if (WaveMomentsOn())
        {
            output.detail.a = covariance.x;
            output.slopes.zw = covariance.yz;
        }
#endif
    }
    return output;
}
#elif defined(REFLECTION_PASS)
struct ReflectionOutput
{
    float4 colour : COLOR0;
    float4 hit : COLOR1;
};
#ifdef REFLECTION_SCENERY_REGIONS_DIAGNOSTIC
#include "reflection_terrain_regions_diagnostic.hlsli"
#else
#include "reflection_scenery.hlsli"
#endif

// The reflection pass (see Renderer::DrawReflections): the mirrored geometry at every pixel of water, with how far away
// it is. The buffers are cleared to 0 (none) and only water is drawn.
ReflectionOutput main(float2 pixel : VPOS)
{
    float surfaceDepth = SampleRawDepth(sDepth, pixel);
    float thickness = WaterThickness(pixel, surfaceDepth);
    clip(thickness);
    // Explicit control flow avoids evaluating the surface and scenery lookup for
    // every pixel of every receiving height. clip alone need not skip that work.
    bool belongs=true;
    bool indoors=false;
    if(cSceneryAlignment.z>0.5)
    {
        float2 uv=(SceneryRaster(pixel)+0.5)*cSceneryGrid.zw;
        float receiver=tex2Dlod(sReceiverMask,float4(uv,0,0)).r;
        indoors=receiver < -0.5;
        receiver=abs(receiver);
        belongs=abs(receiver-cSceneryAlignment.w)<0.25;
        // Source quads own their pixels. Beyond that finite import, a proven
        // flat view continues only where native liquid depth matches its level.
        // Intervals do not overlap, so a higher canal cannot claim the sea.
        [branch] if(receiver<0.5 && cReceiverContinuation.x<cReceiverContinuation.y){
            float height=WorldPosition(ViewPosition(pixel,ViewZ(surfaceDepth))).z;
            belongs=height>=cReceiverContinuation.x && height<cReceiverContinuation.y;
        }
    }
    clip(belongs?1:-1);
    float coverage=1;
    [branch] if(cReceiverCoverageFade.w>0){
        float2 world=WorldPosition(ViewPosition(pixel,ViewZ(surfaceDepth))).xy;
        float2 edge=abs(world-cReceiverCoverageFade.xy);
        float fade=saturate((max(edge.x,edge.y-cReceiverContinuation.z)-cReceiverCoverageFade.z)*cReceiverCoverageFade.w);
        coverage=1-fade*fade*(3-2*fade);
    }
    ReflectionOutput result;result.colour=0;result.hit=0;
    [branch] if(belongs && thickness>0 && coverage>0)
    {
        LiquidSurface surface = SurfaceAt(pixel, surfaceDepth, thickness);
        // As scenery loses coverage, let the wave distortion resolve toward
        // the undistorted bank. Actual misses still carry zero colour and alpha.
        float2 lookup=lerp(pixel,ReflectionLookupPixel(pixel,surface),coverage);
        result=SceneryReflection(lookup, pixel, surface);
#ifndef DEFERRED_SLIME
        // The foreground leg already receives host fog. Only the far part of
        // the water-to-scenery path borrows the colour of the lower sky.
        if (!indoors && cSkyLayout.y > 0 && cEnvironment.y > 0.5 && cFogRange.y > cFogRange.x)
        {
            float fog = saturate((result.hit.g - cFogRange.x) / (cFogRange.y - cFogRange.x));
            float3 tint = (0.40 * cSkyLayout.y) * (cEnvironmentPalette[4].rgb - cFogColor.rgb);
            result.colour.rgb += tint * (saturate(2.0 * fog - 1.0) * result.colour.a);
        }
#endif
        result.colour*=coverage;
        result.hit.b = indoors && abs(surface.world.z - cReceiverContinuation.w) < kSameWaterBody ? 1.0 : 0.0;
    }
    return result;
}
#else
float4 main(float2 pixel : VPOS) : COLOR0
{
    float surfaceDepth = SampleRawDepth(sDepth, pixel);
    float thickness = WaterThickness(pixel, surfaceDepth);
    clip(thickness);
    return ShadeLiquid(pixel, surfaceDepth, thickness);
}
#endif
