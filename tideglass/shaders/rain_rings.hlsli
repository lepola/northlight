// The shape of a rain ring: spreading from where a drop landed over its life, one crest travelling outward and
// fading. Shared by the ring map (ring_ps.hlsl, one ring drawn per splash the game makes) and the rings made up at
// random on water and puddles beyond it (water_common.hlsli).
static const float kRingReach = 0.825;
static const float kRingWidth = 0.13;
static const float kRingSeconds = 1.6;

// One ring's surface slope, from where the drop landed (offset, yards) partway through its life (phase 0..1): a single
// low, rounded crest spreading outward and fading.
float2 Ring(float2 offset, float phase)
{
    float away = length(offset);
    float x = (away - phase * kRingReach) / kRingWidth;
    float crest = abs(x) < 1.0 ? -sin(x * 3.1415927) : 0.0;
    return offset / max(away, 1.0e-3) * crest * pow(saturate(1.0 - phase), 1.5);
}
