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

// x, y: one grid cell in uv, along the way taken (for the count, x is 1 where the grid remembers what it found, as
// slime's does, and y how much a mark that stays grows towards full this frame); z: which pass, 0 to count the
// neighbours, 1 for the first way, 2 for the last (which keeps the grid's state); w: one grid cell in uv.
float4 cSpread : register(c0);
// x: for the last way, how much of the way to the new foam it rises this frame (0 when no time passed: the state kept
// as it is); y: 1 where it falls to less at the same pace (slime's glow, which lies along walls that do not move: left
// to rise at once and fade in the ripple steps, as foam behind a swimmer does, it flickered with the frame rate).
float4 cRise : register(c1);
sampler2D sMarks : register(s0);
// The grid's state: for the count, where slime keeps what it found before; for the last way, what it keeps.
sampler2D sState : register(s1);
// The last way: the crossings as counted, kept in the grid's a: for the debug view (SeabedDebug 3) and, for slime, as
// its edge where the camera has seen it, which the count carries on where it is out of sight and its shore is
// measured from (slime_shore_ps).
sampler2D sCounted : register(s2);

// How far the foam reaches from a crossing: the Gaussian's width (cells, its standard deviation) and how far out it is
// taken on each side (cells).
static const float kSpreadWidth = 2.5;
static const int kSpreadReach = 7;
// How much a crossing counts with all its neighbours marked (so a line of them one cell thick counts fully, and a single
// cell half).
static const float kCountGain = 2.0;
// What the grid keeps where slime was looked at and found clear (slime_shore_ps tells an island by it): little enough
// below nothing to leave a mark beside it standing when the two are read together.
static const float kSeenOpen = -1.0 / 64.0;

// (Below nothing where a point of slime could not be looked at this frame, see contact_ps, or was found clear.)
float MarkAt(float2 uv)
{
    return max(tex2Dlod(sMarks, float4(uv, 0, 0)).r, 0.0);
}

float4 main(float2 cell : VPOS) : COLOR0
{
    float2 uv = (cell + 0.5) * cSpread.w;
    float step = cSpread.w;
    float4 result;
    // Every pixel in a draw uses the same pass. Keep each texture read inside
    // its pass so the count and the two spreads only sample their own inputs.
    [branch] if (cSpread.z < 0.5)
    {
        float counted = (MarkAt(uv) * 4.0 +
                         (MarkAt(uv + float2(step, 0)) + MarkAt(uv - float2(step, 0)) + MarkAt(uv + float2(0, step)) +
                          MarkAt(uv - float2(0, step))) * 2.0 +
                         MarkAt(uv + float2(step, step)) + MarkAt(uv - float2(step, step)) +
                         MarkAt(uv + float2(step, -step)) + MarkAt(uv - float2(step, -step))) / 16.0;
        counted = saturate(counted * kCountGain);
        // r: what the foam or glow spreads from; g: what the grid keeps in its a.
        result = float4(counted, counted, 0.0, 0.0);
        // Where the grid remembers: a point looked at and found clear is kept as open slime, and one that was not
        // looked at this frame keeps what was found there before. What it keeps of a mark grows only as the mark
        // stays, so that it tells what holds the slime (a wall, a bank) from what comes and goes in it (a bubble):
        // the glow spreads from either at once, the waves' edge is drawn only from the first (slime_shore_ps).
        [branch] if (cSpread.x > 0.5)
        {
            float kept = tex2Dlod(sState, float4(uv, 0, 0)).a;
            bool seen = tex2Dlod(sMarks, float4(uv, 0, 0)).r >= 0.0;
            result.r = seen ? counted : max(kept, 0.0);
            result.g = !seen ? kept : counted > 0.0 ? min(counted, max(kept, 0.0) + cSpread.y) : kSeenOpen;
        }
    }
    else
    {
        float nearest = 0.0;
        [unroll] for (int k = -kSpreadReach; k <= kSpreadReach; ++k)
            nearest = max(nearest, MarkAt(uv + cSpread.xy * k) * exp(-0.5 * k * k / (kSpreadWidth * kSpreadWidth)));
        result = float4(nearest, 0.0, 0.0, 0.0);
        [branch] if (cSpread.z > 1.5)
        {
            float4 state = tex2Dlod(sState, float4(uv, 0, 0));
            float change = saturate(nearest) - state.b;
            float foam = state.b + (cRise.y > 0.5 ? change : max(change, 0.0)) * cRise.x;
            result = float4(state.rg, foam, tex2Dlod(sCounted, float4(uv, 0, 0)).g);
        }
    }
    return result;
}
