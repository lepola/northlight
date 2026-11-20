// One raindrop's ring drawn onto the ring map (added to whatever other rings are there): the surface slope it makes.
#include "rain_rings.hlsli"

float4 main(float4 ring : TEXCOORD0) : COLOR0
{
    return float4(Ring(ring.xy, ring.z), 0.0, 0.0);
}
