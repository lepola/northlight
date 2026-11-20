// Where a slime style fades into water (Twilight Vale's lake at the edge of Felwood's light), the host draws the liquid as
// water and as slime; the slime composite lays its slime over the water by how slime-styled the place is. The share
// follows NativeLiquidFrame::ShareAt (native_liquid.h): the zone's own authored colour, the nearby lights' colours laid
// over it by how far in the place is, farthest from the camera first; then SlimeShare. The shader knows whether
// the liquid is river or ocean from the signed capture mark (negative ocean, positive river).

float4 cSlimeFade : register(c96);              // xyz: the zone's ocean colour; w: lights
float4 cSlimeFadeRiver : register(c97);         // xyz: the zone's river colour; w: 1 with a fade draw this frame
float4 cSlimeFadeCentre[16] : register(c98);    // centre, outer radius
float4 cSlimeFadeOceanLight[16] : register(c114); // colour; 1 / (outer - inner), 0 without one
float4 cSlimeFadeRiverLight[16] : register(c130);
sampler2D sSlimeFade : register(s11);           // signed depth below a fade: negative ocean, positive river, zero for pure slime

float SlimeShare(float3 colour)
{
    float ratio = min(colour.g / max(colour.r, 1.0e-3), colour.g / max(colour.b, 1.0e-3));
    return smoothstep(3.0, 4.0, ratio) * smoothstep(0.10, 0.14, colour.g);
}

// 1 (all slime) except on a fade draw.
float SlimeFadeShare(float2 pixel, float surfaceDepth)
{
    float share = 1.0;
    float mark = tex2Dlod(sSlimeFade, float4((pixel + 0.5) * cTexelGrid.xy, 0, 0)).r;
    [branch] if (cSlimeFadeRiver.w > 0.5 && abs(mark) > 0.0)
    {
        float3 world = WorldPosition(ViewPosition(pixel, ViewZ(surfaceDepth)));
        float3 ocean = cSlimeFade.rgb;
        float3 river = cSlimeFadeRiver.rgb;
        // A fixed bound with an early exit (a bound read from the constant compiles far slower).
        [loop] for (int i = 0; i < 16; ++i)
        {
            [branch] if (i >= cSlimeFade.w) break;
            float reach = cSlimeFadeCentre[i].w - distance(world, cSlimeFadeCentre[i].xyz);
            ocean = lerp(ocean, cSlimeFadeOceanLight[i].rgb, saturate(reach * cSlimeFadeOceanLight[i].w));
            river = lerp(river, cSlimeFadeRiverLight[i].rgb, saturate(reach * cSlimeFadeRiverLight[i].w));
        }
        share = mark < 0.0 ? SlimeShare(ocean) : SlimeShare(river);
    }
    return share;
}
