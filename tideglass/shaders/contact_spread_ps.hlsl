// The foam where something stands in the water, spread out from where it crosses the surface (see
// Renderer::MarkContacts), by how near the nearest crossing is rather than how much crosses round about: solid foam at
// any crossing (a body's broad footprint or a post's thin edge alike), thinning smoothly with the distance from it.
// (Summed by a blur, it was solid far out round a broad footprint and ended in a hard rim, and hardly showed round a
// thin one.) First each crossing marked this frame (contact_ps) counts by how many of its neighbours are marked too, so
// a blade of grass alone marks little; then the strongest crossing nearby, weighed down by its distance, is taken
// across the grid one way and then the other, which together take it by the distance itself (the weight is a
// Gaussian, which splits into the two ways exactly). The last way also keeps the foam already on the grid (fading in
// the ripple steps, see ripple_ps) where it is more, so it lingers a moment behind something moving; where it is less,
// the foam rises towards it over a few frames rather than at once (at once, it flickered with every small change in
// what was marked, as a body moves and the camera with it).

// x, y: one grid cell in uv, along the way taken (unused for the count); z: which pass, 0 to count the neighbours, 1 for
// the first way, 2 for the last (which keeps the grid's state); w: one grid cell in uv.
float4 cSpread : register(c0);
// x: for the last way, how much of the way to the new foam it rises this frame (0 when no time passed: the state kept
// as it is).
float4 cRise : register(c1);
sampler2D sMarks : register(s0);
sampler2D sState : register(s1);
// The last way: the crossings as counted, kept in the grid's a for the debug view (SeabedDebug 3).
sampler2D sCounted : register(s2);

// How far the foam reaches from a crossing: the Gaussian's width (cells, its standard deviation) and how far out it is
// taken on each side (cells).
static const float kSpreadWidth = 2.5;
static const int kSpreadReach = 7;
// How much a crossing counts with all its neighbours marked (so a line of them one cell thick counts fully, and a single
// cell half).
static const float kCountGain = 2.0;

float MarkAt(float2 uv)
{
    return tex2Dlod(sMarks, float4(uv, 0, 0)).r;
}

float4 main(float2 cell : VPOS) : COLOR0
{
    float2 uv = (cell + 0.5) * cSpread.w;
    float step = cSpread.w;
    float counted = (MarkAt(uv) * 4.0 +
                     (MarkAt(uv + float2(step, 0)) + MarkAt(uv - float2(step, 0)) + MarkAt(uv + float2(0, step)) +
                      MarkAt(uv - float2(0, step))) * 2.0 +
                     MarkAt(uv + float2(step, step)) + MarkAt(uv - float2(step, step)) +
                     MarkAt(uv + float2(step, -step)) + MarkAt(uv - float2(step, -step))) / 16.0;
    float nearest = 0.0;
    [unroll] for (int k = -kSpreadReach; k <= kSpreadReach; ++k)
        nearest = max(nearest, MarkAt(uv + cSpread.xy * k) * exp(-0.5 * k * k / (kSpreadWidth * kSpreadWidth)));
    float4 state = tex2Dlod(sState, float4(uv, 0, 0));
    float foam = state.b + max(saturate(nearest) - state.b, 0.0) * cRise.x;
    float4 kept = float4(state.rg, foam, tex2Dlod(sCounted, float4(uv, 0, 0)).r);
    // (Selected rather than returned early: Microsoft's compiler mishandles an early return inside a branch.)
    return cSpread.z > 1.5 ? kept : cSpread.z > 0.5 ? float4(nearest, 0.0, 0.0, 0.0)
                                                    : float4(saturate(counted * kCountGain), 0.0, 0.0, 0.0);
}
