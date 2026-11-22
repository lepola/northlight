// The world's water height at a place (the nearest known water's where the world gives none there; see
// GroundMap::NearestLiquidHeights), from the ground map.
float WorldWaterHeight(float2 world)
{
    float halfCell = 2.0 * cRippleGrid.x;
    return tex2Dlod(sSeabed, float4(clamp(RippleUv(world), halfCell, 1.0 - halfCell), 0, 0)).b;
}

// The same surface plane for underwater shading and its separate foam coverage pass.
float4 cUnderwaterNativePlane : register(c67); // camera plane enabled, height, native surface mode, nearby observed sky weight
float4 UnderwaterLevel(bool slime)
{
    float4 level = tex2Dlod(sWaterLevel, float4(0.5, 0.5, 0, 0));
    if (!slime && cGroundInfo.z > 0.5)
        level = float4(WorldWaterHeight(cViewToWorld3.xy), 1.0, 0.0, 0.0);
    if (!slime && cUnderwaterNativePlane.x > 0.5)
        level = float4(cUnderwaterNativePlane.y, 1.0, 0.0, 0.0);
    return level;
}


#ifdef UNDERWATER_FOAM_PASS
// Foam does not need live foreground depth, so the renderer binds the native surface at s0.
#define sNativeLiquid sDepth
#elif defined(SHARED_UNDERWATER_FOAM)
// Wake slopes already come from the foam pass; its boat-path sampler is unused here.
sampler2D sNativeLiquid : register(s10);
#else
sampler2D sNativeLiquid : register(s11);
#endif

// Native geometry is independent of foreground characters and of the camera's water height. The latter
// is a fallback for missing native surfaces, not a plane that can turn a sloping river into a character.
void UnderwaterHit(float2 pixel, float3 direction, float sceneDistance, float4 level,
                   out float distance, out float3 normal, out bool native)
{
    float3 camera = cViewToWorld3.xyz;
    float2 sample = tex2Dlod(sNativeLiquid, float4((pixel + 0.5) * cTexelGrid.xy, 0, 0)).rg;
    float raw = sample.r;
    float viewDistance = cUnderwaterNativePlane.z < 1.5 ? raw : ViewZ(raw);
    float3 view = ViewPosition(pixel, max(viewDistance, 0.01));
    float measuredDistance = length(view);
    bool covered = cUnderwaterNativePlane.z < 1.5 ? sample.g > 0.003 :
        !BeyondFarClip(raw) && SampleRawDepth(sOpaqueDepth,pixel) - raw > 1.0e-7;
    native = cUnderwaterNativePlane.z > 0.5 && covered && viewDistance > 0.0 && measuredDistance < sceneDistance;
    // Differentiate before adding the world origin: at world x=5000 a nearby pixel's offset can be
    // smaller than a float's spacing, producing striped normals along a shallow bank. Rotate the
    // view-space cross product instead; translation must never affect a surface normal.
    // Evaluate derivatives before divergent flow, using liquid geometry rather than foreground.
    float3 viewCross = cross(ddx(view),ddy(view));
    float3 crossed = viewCross.x*cViewToWorld0.xyz + viewCross.y*cViewToWorld1.xyz + viewCross.z*cViewToWorld2.xyz;
    float magnitude = dot(crossed,crossed);
    normal = magnitude > 1.0e-10 ? crossed * rsqrt(max(magnitude,1.0e-10)) : float3(0,0,1);
    normal *= normal.z < 0.0 ? -1.0 : 1.0;
    if (!native) normal = float3(0,0,1);
    distance = native ? measuredDistance :
        (level.g > 0.5 && level.r > camera.z && direction.z > 1.0e-3 ? (level.r-camera.z)/direction.z : -1.0);
}
