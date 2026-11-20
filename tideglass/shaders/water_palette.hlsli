// Tideglass native sky-dome interpolation. Shared with cooperating host adapters.
#ifndef TIDEGLASS_WATER_PALETTE
#define TIDEGLASS_WATER_PALETTE
float WaterDomeBand(float2 ray,float2 lower,float2 upper)
{
    // Intersect a direction with the straight segment between two coloured rings.
    // Interpolation in elevation alone assumes a spherical dome, which this client does not use.
    return saturate((ray.y*lower.x-ray.x*lower.y)/
                    max(ray.x*(upper.y-lower.y)-ray.y*(upper.x-lower.x),1.0e-6));
}
float3 WaterPaletteGradient(float3 direction,float4 palette[6])
{
    // Verified 3.3.5a dome geometry (constructor 0x7F2470), expressed as radius/height.
    // The native approximate trigonometry and vertical offset put its fog ring below the horizon.
    // These are geometry constants; place/time/weather continue to come from resolved native colours.
    float2 ray=float2(length(direction.xy),direction.z);
    float3 colour=lerp(palette[5].rgb,palette[4].rgb,
        WaterDomeBand(ray,float2(.6875,-.0196067812),float2(.664704,.00258921881)));
    colour=lerp(colour,palette[3].rgb,
        WaterDomeBand(ray,float2(.664704,.00258921881),float2(.641332,.0241612188)));
    colour=lerp(colour,palette[2].rgb,
        WaterDomeBand(ray,float2(.641332,.0241612188),float2(.568,.0848932188)));
    colour=lerp(colour,palette[1].rgb,
        WaterDomeBand(ray,float2(.568,.0848932188),float2(.490348,.139145219)));
    return lerp(colour,palette[0].rgb,
        WaterDomeBand(ray,float2(.490348,.139145219),float2(0,.292893219)));
}
#endif
