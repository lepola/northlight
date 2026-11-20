// The broad pattern the lava runs along (see LavaFlow in lava_ps): r its value; b, a its slope per yard. In a float target:
// eight bits showed as steps across the ribbons that follow it.
#include "lava_bake.hlsli"
// Yards one tile covers: eight cells of a pattern a little under 100 yards across (lava_ps: kFlowTileYards).
static const float kFlowTileYards = 761.905;
float Pattern(float2 uv) { return Fbm(uv, 8.0, 3); }
float4 main(float2 pixel : VPOS) : COLOR0
{
    float2 uv = BakeUv(pixel);
    float e = cBake.x;
    float2 slope = float2(Pattern(uv + float2(e, 0.0)) - Pattern(uv - float2(e, 0.0)),
                          Pattern(uv + float2(0.0, e)) - Pattern(uv - float2(0.0, e))) / (2.0 * e * kFlowTileYards);
    return float4(Pattern(uv), 0.0, slope);
}
