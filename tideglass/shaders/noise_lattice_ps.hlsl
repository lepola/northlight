// The noise's lattice and the foam network's wandering points, a texel per cell over one hash period (see
// water_common's NoiseLattice and WanderingPoint), worked out by the same functions so the values are the same.
#include "water_common.hlsli"

struct LatticeOutput
{
    // Hash at the cell and at the next cells along x, along y and along both.
    float4 lattice : COLOR0;
    // The sine and cosine of each point's phase (6.2831853 times FoamHash2), x then y.
    float4 points : COLOR1;
};

LatticeOutput main(float2 pixel : VPOS)
{
    float2 cell = floor(pixel);
    LatticeOutput output;
    output.lattice = float4(Hash(cell), Hash(cell + float2(1, 0)), Hash(cell + float2(0, 1)), Hash(cell + float2(1, 1)));
    float2 phase = 6.2831853 * FoamHash2(cell);
    output.points = float4(sin(phase.x), cos(phase.x), sin(phase.y), cos(phase.y));
    return output;
}
