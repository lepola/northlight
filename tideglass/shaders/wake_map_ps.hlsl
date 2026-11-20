// The wake map (see Renderer::DrawShipWakeMap and sWakeMapPlace): at each of its points, where that point lies along
// its ship's path, walked once a frame here rather than for every pixel of water.
#include "water_common.hlsli"

struct WakeMapOutput
{
    float4 place : COLOR0;
    float4 way : COLOR1;
};

WakeMapOutput main(float2 texel : VPOS)
{
    float ship = floor(texel.y / kWakeMapSize);
    float2 at = (float2(texel.x, texel.y - ship * kWakeMapSize) + 0.5) / kWakeMapSize;
    WakeMapOutput output;
    output.place = 0.0;
    output.way = 0.0;
    [unroll] for (int b = 0; b < 4; ++b)
    {
        if (ship == b)
        {
            float2 p = lerp(cShipBox[b].xy, cShipBox[b].zw, at);
            float nearest = NearestToPath(b, p);
            PathPlace place = PlaceOnPath(b, p, nearest);
            // Which side of the path p is on: across points left of the way back (the signed side is what blends
            // smoothly across the path).
            float left = dot(place.across, float2(-place.back.y, place.back.x)) >= 0.0 ? 1.0 : -1.0;
            output.place = float4(place.behind, place.age, place.speedThen, place.side * left);
            output.way = float4(place.back, nearest, place.known);
        }
    }
    return output;
}
