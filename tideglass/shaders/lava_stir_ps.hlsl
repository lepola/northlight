// How stirred the lava is, one step each frame, on a grid over the same square as the ripples': the hot foam's only input
// (see lava_ps). Whatever crosses the surface keeps a small patch close against it (the contact marks, contact_ps and
// contact_spread_ps, which catch anything in sight); what moves stirs a wider path. Stirred lava spreads a little, so a
// trail widens as it ages, and settles to exactly nothing, so settled lava is untouched lava.
#include "lava.hlsli"

float4 cGrid : register(c0);   // x: one cell in uv; y: seconds since the last step; z: yards a cell
float4 cOrigin : register(c1); // x, y: the grid's corner, yards; z, w: cells it moved since the last step
float4 cMoverCount : register(c2); // x: how many move; y: how far to the contact marks this step, 0 when none were looked for
float4 cMover[16] : register(c4);
float4 cMoverRadius[16] : register(c20);

sampler2D sState : register(s0);
sampler2D sContact : register(s1);
sampler2D sLavaLevel : register(s2);

// How much what stands in the lava stirs it, against what moves through it.
static const float kStandingMark = 0.9;
// How far above and below the surface something counts as being in it (as ripple_ps).
static const float kAboveSurface = 0.6;
static const float kBelowSurface = 3.0;

float StateAt(float2 cell)
{
    float2 uv = (cell + 0.5) * cGrid.x;
    // (Selected rather than returned early: Microsoft's compiler mishandles an early return inside a branch.)
    float inside = all(uv >= 0.0) && all(uv <= 1.0) ? 1.0 : 0.0;
    return tex2Dlod(sState, float4(uv, 0, 0)).r * inside;
}

float4 main(float2 pixel : VPOS) : COLOR0
{
    float dt = cGrid.y;
    float2 source = pixel + cOrigin.zw;
    float stirred = StateAt(source);
    float around = (StateAt(source + float2(1, 0)) + StateAt(source - float2(1, 0)) + StateAt(source + float2(0, 1)) +
                    StateAt(source - float2(0, 1))) * 0.25;
    stirred = lerp(stirred, around, 1.0 - exp(-dt * 7.0));
    stirred = max(stirred * exp(-dt / kLavaFoamLife) - dt * 0.582 / kLavaFoamLife, 0.0);

    float rise = 1.0 - exp(-dt * 10.0);
    float2 uv = (pixel + 0.5) * cGrid.x;
    float contact = tex2Dlod(sContact, float4(uv, 0, 0)).r;
    stirred += max(contact * kStandingMark - stirred, 0.0) * cMoverCount.y;

    float2 world = cOrigin.xy + (pixel + 0.5) * cGrid.z;
    float4 level = tex2Dlod(sLavaLevel, float4(0.5, 0.5, 0, 0));
    for (int i = 0; i < 16; ++i)
    {
        if (i < cMoverCount.x && (level.g < 0.5 || (cMover[i].z > level.r - kBelowSurface && cMover[i].z < level.r + kAboveSurface)))
        {
            float away = max(length(world - cMover[i].xy) - cMoverRadius[i].x, 0.0);
            float mark = exp(-away * away / (2.0 * kLavaFoamReach * kLavaFoamReach));
            stirred += max(mark - stirred, 0.0) * rise;
        }
    }
    return float4(saturate(stirred), 0.0, 0.0, 1.0);
}
