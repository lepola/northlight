// Screen and world helpers shared by the liquids.
float4 cReflectionOptions : WATER_C_REGISTER(WATER_SLOT_cReflectionOptions); // z: 1 the reflection pass bends the independent scenery lookup
float4 cReflectionTuning : WATER_C_REGISTER(WATER_SLOT_cReflectionTuning); // y: how far the waves may shift that lookup, a fraction of the view's width; z: the bend's reach, yards

// A distance across the view given as a fraction of its width, in pixels: the same angle at any resolution.
float AcrossView(float fraction)
{
    return fraction * cViewportPx.z;
}

float3 WorldToView(float3 world)
{
    return world.x * cWorldToView0.xyz + world.y * cWorldToView1.xyz + world.z * cWorldToView2.xyz + cWorldToView3.xyz;
}

float2 ViewToPixel(float3 view)
{
    float2 ndc = view.xy * cViewToNdc.xy / view.z + cViewToNdc.zw;
    return cViewportPx.xy + float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * cViewportPx.zw - 0.5;
}

bool InsideViewport(float2 pixel)
{
    return all(pixel >= cViewportPx.xy) && all(pixel <= cViewportPx.xy + cViewportPx.zw - 1.0);
}

float SceneViewZ(float2 pixel)
{
    float raw = SampleRawDepth(sOpaqueDepth, pixel);
    return BeyondFarClip(raw) ? 1.0e5 : ViewZ(raw);
}

float EdgeFade(float2 pixel)
{
    float2 uv = (pixel - cViewportPx.xy) / cViewportPx.zw;
    float2 edge = min(uv, 1.0 - uv) / kReflectionEdgeFade;
    return saturate(min(edge.x, edge.y));
}

// A direction's place (0..1 each way) in an octahedral map of every direction: the upper half (world z up) the diamond
// in the middle, the lower half folded out into the corners.
float2 OctahedralUv(float3 direction)
{
    float3 d = direction / (abs(direction.x) + abs(direction.y) + abs(direction.z));
    float2 corner = float2(d.x >= 0.0 ? 1.0 : -1.0, d.y >= 0.0 ? 1.0 : -1.0);
    float2 p = d.z >= 0.0 ? d.xy : (1.0 - abs(d.yx)) * corner;
    return p * 0.5 + 0.5;
}
