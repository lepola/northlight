// Map the local surface reflection into the vertically mirrored scenery, then add the waves'
// bounded distortion. A finite reach approximates slope parallax; flat water keeps the existing
// wave-only lookup. The wave shift eases into its limit so neighbouring pixels retain different shifts.
float2 ReflectionBend(float2 pixel, LiquidSurface surface)
{
    float3 baseNormal = surface.baseNormal;
    float sloped = smoothstep(1.0e-5, 1.0e-3, 1.0-baseNormal.z);
    float3 flatWay = -surface.toCamera;
    // Owned scenery is mirrored vertically at its receiving heights (owned_world_vs), not rotated
    // around the local river plane. Correct that basis before adding the bounded wave distortion.
    float3 sourceNormal = normalize(lerp(baseNormal,float3(0,0,1),sloped));
    float3 baseWay = reflect(reflect(flatWay,baseNormal),sourceNormal);
    float3 waveWay = reflect(surface.reflected,sourceNormal);
    // A finite lookup stands in for scene parallax; keep the water's waves at their existing reach.
    const float kSlopeMirrorReach = 10.0;
    float3 baseView = WorldToView(surface.world+baseWay*kSlopeMirrorReach);
    float3 straightView = WorldToView(surface.world+flatWay*kSlopeMirrorReach);
    baseView.z=max(baseView.z,kMinRayViewZ);straightView.z=max(straightView.z,kMinRayViewZ);
    float2 baseBend = (ViewToPixel(baseView)-ViewToPixel(straightView))*sloped;
    float3 flatView = WorldToView(surface.world + baseWay * cReflectionTuning.z);
    float3 waveView = WorldToView(surface.world + waveWay * cReflectionTuning.z);
    flatView.z = max(flatView.z, kMinRayViewZ);
    waveView.z = max(waveView.z, kMinRayViewZ);
    float2 bend = ViewToPixel(waveView) - ViewToPixel(flatView);
    float limit = AcrossView(max(cReflectionTuning.y, 0.0));
    return baseBend + bend * (limit / (limit + length(bend) + 1.0e-4));
}

// Resolve against the independent scenery image, before the receiving water is masked by
// foreground geometry. Warping an already masked image displaced a swimmer's silhouette.
float2 ReflectionLookupPixel(float2 pixel, LiquidSurface surface)
{
    float2 at = pixel;
    [branch] if (cReflectionOptions.z > 0.5 && !FlatReflectionSurface())
        at += ReflectionBend(pixel, surface);
    return at;
}
