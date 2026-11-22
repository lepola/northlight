#include "water_common.hlsli"
#include "underwater_surface.hlsli"

#ifdef SHARED_UNDERWATER_FOAM
sampler2D sUnderwaterFoam : register(s11);
#endif

#ifdef UNDERWATER_SCENERY
// Water takes wake slopes from shared foam. Slime has no ship wake. Both leave s12/s13 for the mirror.
sampler2D sUnderwaterScenery : register(s12);
sampler2D sUnderwaterSceneryDepth : register(s13);
float4 cUnderwaterScenery : register(c65); // current source valid, authoritative water Z, dimensions
#endif
float4 cUnderwaterSceneryProjection : register(c66); // native source projection A/B, D3D9 alignment, direct sightline distance as depth
float4 cUnderwater : register(c23);
float4 cInside : register(c25);
float4 cWindow : register(c29);
float4 cTwilightShallow : register(c30);

static const float kWaterIndex = 1.333;
// UnderwaterWindow 1 widens Snell's window as if the water bent light this little (about 72 degrees from
// straight up instead of 49), and lets this much of the world above show past the window's edge.
static const float kWidestWindowIndex = 1.05;
static const float kLeakPastWindow = 0.85;
// Past the window's edge the mirror builds back up towards glancing angles, as reflections do above water.
static const float kLeakFalloff = 4.0;
static const float3 kUnderwaterAbsorption = float3(2.4, 0.9, 0.7);
static const float3 kSlimeAbsorption = float3(2.2, 0.7, 2.6);
static const float kNoSurfaceDepth = 2.0;
static const float kSkyDistance = 1.0e4;
static const float kCriticalBlendStart = 0.8;
static const float kOpaqueCeilingShade = 0.85;
static const float kWaterlineBlend = 1.5;
static const float kNightUnderwaterLight = 0.33;
static const float kSunSpotPower = 400.0;
static const float kSunSpotIntensity = 3.0;
static const float kSunHaloPower = 12.0;
static const float kSunHaloIntensity = 0.35;
static const float kDistanceAsDepth = 0.5;
static const float kDepthDarkeningScale = 1.0;
static const float kDistanceDarkeningScale = 0.8;
static const float kDeepDimming = 0.6;
static const float kLightFromAbove = 0.15;
static const float kSurfaceSideTolerance = 0.3;
// Seen from below, the fine wave detail flips rays between the world above and the mirror in noisy patches, so
// the underside keeps the big waves' bending but only this share of the slope.
static const float kUndersideSlope = 0.7;
static const float kGlancingUndersideSlope = 0.3;
static const float kGlancingUndersideRise = 0.1;
static const float kSteepUndersideRise = 0.5;
// How far (in visibility lengths) the stand-in for the mirror follows a reflected ray to the seabed; past it the
// reflection is lost in the water colour anyway.
static const float kMirrorReachInVisibility = 1.5;
static const float kAssumedSeabedDepth = 3.0;
// The beach just above the waterline looks like the seabed just below it, so a missed reflection in the shallows
// may land on it; trees and cliffs higher up may not.
static const float kShoreAboveWaterline = 1.5;
// How near (yards) the screen's point must be to a mirrored point under the water to lend it its colour.
static const float kSameSpotOnScreen = 1.0;
static const float kSteepestMirrorRay = 0.05;
static const int kSeabedSteps = 12;
static const int kSeabedRefinements = 4;
// UnderwaterGlancingMirror 1 closes the view through the surface this quickly past the window's edge, so at
// glancing angles the surface is a full mirror.
static const float kFullMirrorFalloff = 40.0;
// As above water, a reflected ray is kept pointing at least this far into the water.
static const float kLowestMirrorDip = 0.005;
static const float kRefractionShift = 0.06;
static const float kBentViewEase = 0.08;
static const float kInFrontOfSurface = 0.5;
static const float kInFrontMarginPerYard = 0.01;
static const float kFoamFromBelow = 0.75;
static const float kFoamBelowBrightness = 0.9;
static const float kSlimeUndersidePattern = 0.45;
// Slime bends the view more strongly, so its ripples and smaller waves visibly move the room seen through it.
static const float kSlimeRefractionBoost = 2.0;

float Visibility() { return cUnderwater.x; }
float WindowWidening() { return saturate(cWindow.x); }
bool ShowBeyondSurface() { return cWindow.y > 0.5 && cWindow.y < 1.5; }
// UnderwaterDebug 2: which way each pixel was drawn (see Traced).
bool ShowPaths() { return cWindow.y > 1.5; }

// For UnderwaterDebug 2: the picture tinted by the way it was drawn. Red: the surface seen from below; green: something
// in front of the surface; blue: the seabed or open water, brighter the more of the shallow colour its haze keeps.
float4 Traced(float3 color, float3 path)
{
    return float4(lerp(color, path, 0.6), 1.0);
}

float Shallowness(float3 direction, float distance, float cameraDepth)
{
    float depthThere = cameraDepth + max(-direction.z, 0.0) * distance;
    return exp(-(depthThere / (cDeepColorDepth.w * kDepthDarkeningScale) +
                 distance / (Visibility() * kDistanceDarkeningScale)));
}
float GlancingMirror() { return saturate(cWindow.z); }
bool UnderSlime()
{
#ifdef SHARED_UNDERWATER_FOAM
    return false;
#elif defined(SLIME_UNDERSIDE)
    return true;
#else
    return cWindow.w > 0.5;
#endif
}
float WindowIndex() { return lerp(kWaterIndex, kWidestWindowIndex, WindowWidening()); }

// The water's own colours set the hue; the scene light only sets how bright it is (with the daylight kept under
// water, the warm sun would otherwise turn the teal green).
float3 UnderwaterLight()
{
    float brightness = dot(SceneLight(), float3(0.2126, 0.7152, 0.0722));
    return brightness * lerp(kNightUnderwaterLight, 1.0, saturate(cUnderwater.w));
}

float3 WaterColorAlong(float3 direction, float distance, float cameraDepth, float distanceAsDepth = kDistanceAsDepth)
{
    float3 gradient;
    if (UnderSlime())
    {
        // Under the slime its own two inside colours (in the twilight and abyss registers): the haze around you
        // and what it fades to, darkening all the way to what can be seen.
        float depthThere = max(0.0, cameraDepth - direction.z * distance);
        float deepness = 1.0 - exp(-(depthThere / (cDeepColorDepth.w * kDepthDarkeningScale) +
                                     distance / (Visibility() * kDistanceDarkeningScale)));
        gradient = lerp(cTwilightShallow.rgb, cAbyss.rgb, deepness) * cInside.y;
    }
    else
    {
        // The shallow colour close by, giving way with distance and depth to the colour of deep water, then the abyss:
        // the depth colour counts the way through the water as depth too, so near things keep a light tint, farther
        // ones darken, and open water with nothing in view ends in the abyss colour whichever way it looks. Looking
        // up, the water crossed counts as deep as the camera (judged by where the view ends, the surface, it took
        // the shallow colour and the surface's underside stood out pale above the horizon).
        float depthThere = cameraDepth + max(-direction.z, 0.0) * distance;
        float deepness = 1.0 - exp(-(depthThere / (cDeepColorDepth.w * kDepthDarkeningScale) +
                                     distance / (Visibility() * kDistanceDarkeningScale)));
        float3 shallow = lerp(cShallowColor.rgb, cTwilightShallow.rgb, saturate(cTwilightShallow.w));
        gradient = lerp(shallow * cInside.y, DepthGradient(depthThere + distance * distanceAsDepth) * kDeepDimming,
                        deepness);
    }
    return gradient * UnderwaterLight() * (1.0 + kLightFromAbove * saturate(direction.z));
}

float3 Absorbed(float3 color, float distance, float3 waterColor)
{
    // Water lets blue-green through; slime lets green through (outdoors, water's rates turn it cyan).
    float3 absorption = UnderSlime() ? kSlimeAbsorption : kUnderwaterAbsorption;
    float3 transmittance = exp(-absorption * distance / Visibility());
    return color * transmittance + waterColor * (1.0 - transmittance);
}

// The ground height and whether it is known, from the ground map; past its edge the ground at the edge carries on,
// so rays leaving the square around the camera still meet a seabed (they flickered between meeting it and not).
float2 GroundAlongMirror(float2 world)
{
    float halfCell = 2.0 * cRippleGrid.x;
    return tex2Dlod(sSeabed, float4(clamp(RippleUv(world), halfCell, 1.0 - halfCell), 0, 0)).rg;
}

// Where the reflected ray meets the seabed: stepped through the world against the ground heights around the camera
// (the ground map), so a ray skimming a slope finds it smoothly as the waves tip it (searched for on screen, such rays
// flipped between finding the seabed and not, leaving dotted rings along the waves). How far along the ray, or -1
// where it meets nothing known within reach.
float SeabedAlongRay(float3 hit, float3 mirrored, float reach)
{
    float previous = 0.0;
    float found = -1.0;
    // t = reach * i / kSeabedSteps, stepped as the loop goes (see Unfolded).
    float stride = Unfolded(reach) / kSeabedSteps;
    float t = 0.0;
    [unroll] for (int i = 0; i < kSeabedSteps; ++i)
    {
        t += stride;
        float3 p = hit + mirrored * t;
        float2 ground = GroundAlongMirror(p.xy);
        if (found < 0.0 && ground.g > 0.5 && p.z <= ground.r)
            found = t;
        if (found < 0.0)
            previous = t;
    }
    if (found < 0.0)
        return -1.0;
    float low = previous;
    float high = found;
    [unroll] for (int j = 0; j < kSeabedRefinements; ++j)
    {
        float middle = 0.5 * (low + high);
        float3 p = hit + mirrored * middle;
        float groundHeight = GroundAlongMirror(p.xy).r;
        if (p.z <= groundHeight)
            high = middle;
        else
            low = middle;
    }
    return high;
}

// A reflection is the object it shows, seen over the longer way (to the surface and back down to it): hazed just as
// that object seen straight through the water that far off would be, so the two match but for the distance.
float3 HazeOver(float3 object, float path, float cameraDepth)
{
    return WaterColorAlong(normalize(object - cViewToWorld3.xyz), path, cameraDepth);
}

float3 SeenOverPath(float3 color, float3 object, float path, float cameraDepth)
{
    return Absorbed(color, path, HazeOver(object, path, cameraDepth));
}

// Where a point lands on the screen, what the screen shows there (its point and its colour before the water), if it lands
// inside the view on something nearer than the far clip.
bool ScreenPoint(float3 object, out float2 pixel, out float3 shown, out float3 seen)
{
    float3 view = WorldToView(object);
    pixel = ViewToPixel(view);
    shown = object;
    seen = 0.0;
    if (view.z <= kMinRayViewZ || !InsideViewport(pixel) || BeyondFarClip(SampleRawDepth(sOpaqueDepth, pixel)))
        return false;
    shown = WorldPosition(ViewPosition(pixel, SceneViewZ(pixel)));
    seen = tex2Dlod(sSceneBefore, float4((pixel + 0.5) * cTexelGrid.xy, 0, 0)).rgb;
    return true;
}

// Where the screen shows nothing for the mirror (the part the remembered sky plays above water): the seabed the
// reflected ray meets in the ground map, coloured as the screen shows that spot where it is in view, else the water
// for that distance.
float3 SeabedInMirror(float3 hit, float3 mirrored, float cameraDepth, float before)
{
    float reach = Visibility() * kMirrorReachInVisibility;
    float travelled = SeabedAlongRay(hit, mirrored, reach);
    if (travelled < 0.0)
    {
        if (GroundAlongMirror(hit.xy).g > 0.5)
            return HazeOver(hit + mirrored * reach, before + reach, cameraDepth);
        float depth = max(cameraDepth * 2.0, kAssumedSeabedDepth);
        travelled = min(depth / max(-mirrored.z, kSteepestMirrorRay), reach);
    }
    float3 seabed = hit + mirrored * travelled;
    float path = before + travelled;
    float3 along = HazeOver(seabed, path, cameraDepth);
    float2 pixel;
    float3 shown, seen;
    if (!ScreenPoint(seabed, pixel, shown, seen) || shown.z > hit.z + kShoreAboveWaterline)
        return along;
    // Towards the edge of the screen it gives way to the water alone, rather than switching at the edge.
    return lerp(along, SeenOverPath(seen, seabed, path, cameraDepth), EdgeFade(pixel));
}

// The colour the screen shows for a point under the water where the screen does show it (lit as the view itself is, by
// the host's light and shadows), else the given colour. The mirrored geometry is lit apart from the screen, and in a storm
// its bed came out brighter and greener than the bed seen directly (2026-11-09). Gives way at the screen's edge.
float3 ScreenColourWhereShown(float3 object, float3 color)
{
    float2 pixel;
    float3 shown, seen;
    if (!ScreenPoint(object, pixel, shown, seen) || length(shown - object) > kSameSpotOnScreen)
        return color;
    return lerp(color, seen, EdgeFade(pixel));
}

// The underside's mirror: the mirrored geometry where it is available, seen over the longer way; otherwise the seabed
// from the ground map stands in. Only what lies under the surface can show in it (from below, what is above is seen
// through Snell's window instead).
// before: how far the view came to reach the surface.
float3 MirroredUnderwater(float3 hit, float3 direction, float3 downward, float cameraDepth, float before)
{
    float3 mirrored = reflect(direction, downward);
    // As above water, a ray a steep wave sends back into the surface is turned back rather than laid flat.
    mirrored.z = -max(abs(mirrored.z), kLowestMirrorDip);
    mirrored = normalize(mirrored);
    float3 standIn = SeabedInMirror(hit, mirrored, cameraDepth, before);
#ifdef UNDERWATER_SCENERY
    float3 result=standIn;
    [branch] if(cUnderwaterScenery.x>.5){
        float3 virtualPoint=hit+mirrored*min(Visibility(),10.0);
        virtualPoint.z=2*cUnderwaterScenery.y-virtualPoint.z;
        float3 view=WorldToView(virtualPoint);
        float2 pixel=ViewToPixel(view);
        float2 uv=(pixel-cViewportPx.xy+.5)/cViewportPx.zw;
        float raw=tex2Dlod(sUnderwaterSceneryDepth,float4(uv,0,0)).r;
        if(view.z>kMinRayViewZ && all(uv>=0) && all(uv<=1) && raw>0 && raw<1){
            // The mirror uses a full-range native D3D projection. Scene depth
            // has the client's GL remap/viewport squeeze and cannot decode it.
            float sourceZ=cUnderwaterSceneryProjection.y/(raw-cUnderwaterSceneryProjection.x);
            float3 object=WorldPosition(ViewPosition(pixel,sourceZ));
            object.z=2*cUnderwaterScenery.y-object.z;
            float3 color=tex2Dlod(sUnderwaterScenery,float4(uv,0,0)).rgb;
            result=SeenOverPath(ScreenColourWhereShown(object,color),object,before+length(object-hit),cameraDepth);
        }
    }
    return result;
#else
    return standIn;
#endif
}

float3 SunThroughWindow(float3 transmitted)
{
    float facing = saturate(dot(transmitted, cToLight.xyz));
    float spot = pow(facing, kSunSpotPower) * kSunSpotIntensity + pow(facing, kSunHaloPower) * kSunHaloIntensity;
    return cSunColor.rgb * spot * cToLight.w * SunVisibility();
}

// The world above, seen through the surface: the scene captured before the client drew its water, shifted on
// screen by the waves' slope. A shift that lands on something under the water (or off screen) keeps the unshifted
// view, which is always above the surface.
float3 SeenThroughSurface(float2 pixel, float3 hit, float3 downward, inout bool sky)
{
    float2 uv = (pixel + 0.5) * cTexelGrid.xy;
    float3 straight = tex2Dlod(sSceneBefore, float4(uv, 0, 0)).rgb;
    float3 slope = float3(-downward.xy, 0.0);
    float3 tilt = WorldToView(hit + slope) - WorldToView(hit);
    float shift = kRefractionShift * (UnderSlime() ? kSlimeRefractionBoost : 1.0);
    float2 bent = pixel + float2(tilt.x, -tilt.y) * shift * cViewportPx.w;
    if (!InsideViewport(bent))
        return straight;
    float raw = SampleRawDepth(sOpaqueDepth, bent);
    sky = sky && IsTrueSky(raw);
    float3 landed = WorldPosition(ViewPosition(bent, BeyondFarClip(raw) ? kSkyDistance : ViewZ(raw)));
    // Eased rather than switched, so the edge of where the bent view is usable draws no lines.
    float usable = smoothstep(hit.z - kSurfaceSideTolerance - kBentViewEase, hit.z - kSurfaceSideTolerance, landed.z);
    return lerp(straight, tex2Dlod(sSceneBefore, float4((bent + 0.5) * cTexelGrid.xy, 0, 0)).rgb, usable);
}

float3 UndersideNormal(float3 hit, float distance, float3 wake, float3 direction, float3 baseNormal)
{
    // Slime's underside moves with the same broader waves as its top (and the ripples), at their full slope.
    float footprint = PixelFootprint(distance, direction.z) * (UnderSlime() ? kSlimeWaveScale : 1.0);
    float3 surface = SurfaceNormal(UnderSlime() ? hit.xy * kSlimeWaveScale : hit.xy, distance, 1.0, footprint);
    float3 normal = WithRipples(surface, hit.xy);
    // The shoreline waves and ships' wakes roll over the underside too.
    if (!UnderSlime())
        normal = normalize(normal + float3(-ShoreWaveSlope(hit.xy, hit.z, footprint) - wake.xy, 0.0));
    float slope = UnderSlime() ? 1.0 : kUndersideSlope;
    // Seen at a glancing angle the wave faces tipped away hide behind the others, so the far surface is calmer, as
    // above water: tipped towards the camera by the full waves, faces far off let the sky through in flickering
    // patches, turned cyan by the long way through the water, where the surface should be an even mirror.
    slope *= lerp(1.0, kGlancingUndersideSlope, 1.0 - smoothstep(kGlancingUndersideRise, kSteepUndersideRise, direction.z));
    return -normalize(baseNormal * normal.z + float3(normal.xy * slope, 0.0));
}

// The surface seen from below, hazed by the water on the way to it: what shows through it and on it (the world
// above, foam, the slime's pattern) by the way up to the surface; what it mirrors over the whole way down again.
float3 SurfaceFromBelow(float2 pixel, float3 hit, float3 direction, float distance, float cameraDepth,
                        float3 surfaceWater, float3 baseNormal, bool sky)
{
    if (cInside.x <= 0.0)
        return surfaceWater * kOpaqueCeilingShade;
    float footprint = PixelFootprint(distance, abs(direction.z));
#ifdef SHARED_UNDERWATER_FOAM
    // The coverage pass also supplies the wakes' slope: do not walk ship paths again in this shader.
    float4 wake = tex2Dlod(sUnderwaterFoam, float4((pixel + 0.5) * cTexelGrid.xy, 0, 0));
#else
    float4 wake = UnderSlime() ? 0.0 : ShipWake(hit.xy, hit.z, footprint);
#endif
    float3 downward = UndersideNormal(hit, distance, wake.xyz, direction, baseNormal);
    float cosine = saturate(dot(-direction, downward));
    float index = WindowIndex();
    float sinTransmitted2 = index * index * (1.0 - cosine * cosine);
    float3 mirror = MirroredUnderwater(hit, direction, downward, cameraDepth, distance);
    float leakAtEdge = kLeakPastWindow * WindowWidening();
    float reflectance;
    if (sinTransmitted2 >= 1.0)
        reflectance =
            1.0 - leakAtEdge * exp(-(sinTransmitted2 - 1.0) * lerp(kLeakFalloff, kFullMirrorFalloff, GlancingMirror()));
    else
    {
        float fresnel = kWaterF0 + (1.0 - kWaterF0) * pow(1.0 - sqrt(1.0 - sinTransmitted2), 5.0);
        float nearCritical = saturate((sinTransmitted2 - kCriticalBlendStart) / (1.0 - kCriticalBlendStart));
        // Scaled so that at the window's edge it meets the mirror past it without a step.
        reflectance = saturate(max(fresnel, nearCritical * nearCritical)) * (1.0 - leakAtEdge);
    }
    float3 seen = SeenThroughSurface(pixel, hit, downward, sky);
    float3 transmitted = refract(direction, downward, index);
    if (dot(transmitted, transmitted) > 1.0e-4)
    {
        transmitted = normalize(transmitted);
#ifndef SLIME_UNDERSIDE
        [branch] if (sky && cUnderwaterNativePlane.w > 0)
        {
            float row = sqrt(saturate((transmitted.z-kSkyLowest)/kSkySpan))*kSkyRows-0.5;
            float2 across = transmitted.xy / max(length(transmitted.xy), 1.0e-4);
            float2 sun = cToLight.xy / max(length(cToLight.xy), 1.0e-4);
            float side = smoothstep(-0.6, 0.6, dot(across,sun));
            float4 air = lerp(SkyMemory(0,row),SkyMemory(1,row),side);
            // Keep the native sky detail. Unobserved directions have zero coverage.
            seen += (air.rgb - seen*air.a)*(0.8*cUnderwaterNativePlane.w);
        }
#endif
        seen += SunThroughWindow(transmitted);
    }
    seen = Absorbed(seen, distance, surfaceWater);
    float3 result = lerp(mirror, lerp(seen, mirror, reflectance), saturate(cInside.x));
    // Foam seen from below: the broken surface lit from above, brighter patches that come and go with the waves.
#ifndef SHARED_UNDERWATER_FOAM
    if (!UnderSlime())
    {
        float2 flow;
        float shore = ShoreFoam(hit.xy, hit.z, flow);
        float lines = FoamLines(hit.xy, max(shore, wake.z), flow, footprint);
        float crests = max(ShoreCrestLine(hit.xy, hit.z, footprint), wake.w);
        float foam = max(lines, crests) * FoamGrain(hit.xy) * kFoamFromBelow;
        result = lerp(result, Absorbed(UnderwaterLight() * kFoamBelowBrightness, distance, surfaceWater), foam);
    }
#endif
    // The slime's own swirling pattern shows on its underside too, bent by the same waves.
    if (UnderSlime())
        result = lerp(result, Absorbed(SlimeBodyPattern(hit.xy + downward.xy * kSlimeBodyRefraction, UnderwaterLight()),
                                       distance, surfaceWater),
                      kSlimeUndersidePattern);
    return result;
}

// The bottom seen while swimming gets the same caustics as seen from above (not under slime).
float3 WithCaustics(float3 color, float3 spot, float4 level)
{
    if (UnderSlime() || level.g < 0.5)
        return color;
    return color + color * CausticsAt(spot, level.r - spot.z, level.r, cGroundInfo.w) * cSunColor.rgb;
}

float4 main(float2 pixel : VPOS) : COLOR0
{
    float raw = SampleRawDepth(sOpaqueDepth, pixel);
    float3 camera = cViewToWorld3.xyz;
    float3 view = ViewPosition(pixel, BeyondFarClip(raw) ? kSkyDistance : ViewZ(raw));
    float sceneDistance = length(view);
    float3 direction = normalize(WorldPosition(view) - camera);
    float2 uv = (pixel + 0.5) * cTexelGrid.xy;
    float3 color = tex2Dlod(sSceneWithWater, float4(uv, 0, 0)).rgb;

    // The surface overhead at the world's water height where it is known, as the surface is placed above water (see
    // FlattenedSurface); the level measured from the water on screen where it is not, and under slime.
    float4 level = UnderwaterLevel(UnderSlime());
    bool surfaceKnown = level.g > 0.5 && level.r > camera.z;
    float depthBelow = surfaceKnown ? level.r - camera.z : kNoSurfaceDepth;
    float3 waterColor = WaterColorAlong(direction, sceneDistance, depthBelow, cUnderwaterSceneryProjection.w);
    float toSurface; float3 baseNormal; bool nativeSurface;
    UnderwaterHit(pixel,direction,sceneDistance,level,toSurface,baseNormal,nativeSurface);
    if (toSurface > 0.0)
    {
        // Something drawn after the water (parts of characters) sits in front of the surface: show it, not the surface.
        // Only what was drawn after it: the seabed rising above eye level is nearer than the surface too, and taken
        // for such a thing it lost its caustics.
        // Nor the client's own surface: a few inches off the true level, it is nearer by a long way at a glancing
        // angle, and drew the water's colour at the camera's depth as a band along the horizon; what is in front
        // sits clearly below the surface.
        float nowRaw = SampleRawDepth(sDepth, pixel);
        float nowDistance = BeyondFarClip(nowRaw) ? kSkyDistance : length(ViewPosition(pixel, ViewZ(nowRaw)));
        // (By a margin that grows with distance, as the depth buffer grows coarser.)
        bool belowSurface = camera.z + direction.z * nowDistance <
                            level.r - kSurfaceSideTolerance - nowDistance * kInFrontMarginPerYard;
        if (nowDistance < min(toSurface, sceneDistance) - kInFrontOfSurface && (nativeSurface || belowSurface))
        {
            float3 inFront = Absorbed(color, nowDistance, WaterColorAlong(direction, nowDistance, depthBelow));
            return ShowPaths() ? Traced(inFront, float3(0, 1, 0)) : float4(inFront, 1.0);
        }
        if (toSurface < sceneDistance)
        {
            // Debug: where the view crosses the surface, show the captured scene beyond it, untouched.
            if (ShowBeyondSurface())
                return float4(tex2Dlod(sSceneBefore, float4(uv, 0, 0)).rgb, 1.0);
            float3 sceneSeen = Absorbed(WithCaustics(tex2Dlod(sSceneBefore, float4(uv, 0, 0)).rgb, camera + direction *
                                                     sceneDistance, level), sceneDistance, waterColor);
            float3 surfaceWater = WaterColorAlong(direction, toSurface, depthBelow);
            float3 surfaceSeen =
                SurfaceFromBelow(pixel, camera + direction * toSurface, direction, toSurface, depthBelow, surfaceWater, baseNormal, IsTrueSky(raw));
            float join = saturate((sceneDistance - toSurface) / kWaterlineBlend);
            float3 seenUp = lerp(sceneSeen, surfaceSeen, join);
#ifdef SHARED_UNDERWATER_FOAM
            // The waterline blend hides a surface mirror against a nearby bank or post. Foam lies on that
            // contact, so composite it afterwards; fading it with the mirror erased precisely those rims.
            float foam = tex2Dlod(sUnderwaterFoam, float4(uv, 0, 0)).z * kFoamFromBelow;
            if (cInside.x > 0.0)
                seenUp = lerp(seenUp, Absorbed(UnderwaterLight() * kFoamBelowBrightness, toSurface, surfaceWater), foam);
#endif
            return ShowPaths() ? Traced(seenUp, float3(join, 0, 1.0 - join)) : float4(seenUp, 1.0);
        }
    }
    float3 seen = Absorbed(WithCaustics(color, camera + direction * sceneDistance, level), sceneDistance, waterColor);
    return ShowPaths() ? Traced(seen, float3(0, 0, 0.2 + 0.8 * Shallowness(direction, sceneDistance, depthBelow)))
                       : float4(seen, 1.0);
}
