#include "water_common.hlsli"
#include "crest_light.hlsli"

sampler2D sSurfaceDetail : WATER_S_REGISTER(WATER_SLOT_sSurfaceDetail);
float4 cLampPositions[4] : WATER_C_REGISTER(WATER_SLOT_cLampPositions);
float4 cLampColours[4] : WATER_C_REGISTER(WATER_SLOT_cLampColours);

float4 main(float2 pixel : VPOS) : COLOR0
{
    float depth=SampleRawDepth(sDepth,pixel);
    float thickness=WaterThickness(pixel,depth);
    clip(thickness);
    float3 world=WorldPosition(ViewPosition(pixel,ViewZ(depth)));
    float3 toCamera=cViewToWorld3.xyz-world;
    float distance=length(toCamera);
    float3 view=toCamera/max(.0001,distance);
    float4 detail=tex2Dlod(sSurfaceDetail,float4((pixel+.5)*cTexelGrid.xy,0,0));
    float3 normal=normalize(float3(detail.xy,1));
    float3 change = abs(ddx(normal)) + abs(ddy(normal));
    float variance = dot(change, change) / 12.0;
    float3 sum = 0;
    [loop] for (int i = 0; i < 4; ++i)
    {
        float3 delta = cLampPositions[i].xyz - world;
        float radius = cLampPositions[i].w;
        float d2 = dot(delta, delta);
        [branch] if (delta.z > 0 && d2 < radius * radius)
        {
            float d = sqrt(max(d2, .0001));
            float3 halfVector = normalize(delta / d + view);
            float2 axis = cLampPositions[i].xy - cViewToWorld3.xy;
            axis *= rsqrt(max(dot(axis, axis), .0001));
            float2 slope = halfVector.xy / max(halfVector.z, .001);
            float alongSlope = dot(slope, axis);
            float bounded = alongSlope * rsqrt(1.0 + alongSlope * alongSlope / (.17 * .17));
            float3 target = normalize(float3(slope + axis * (bounded - alongSlope), 1.0));
            float3 miss = cross(normal, target);
            float angle2 = dot(miss, miss);
            float soft = FilteredCrestLobe(angle2, .115, variance);
            float core = FilteredCrestLobe(angle2, .042, variance);

            float eyeHeight = max(toCamera.z, .001);
            float2 mirror = lerp(cViewToWorld3.xy, cLampPositions[i].xy, eyeHeight / (eyeHeight + delta.z));
            float2 offset = world.xy - mirror;
            float side = dot(offset, float2(-axis.y, axis.x)) / (.83 * .45);
            float along = dot(offset, axis);
            float envelope = exp(-side * side) / (1.0 + along * along / 37.0);
            float3 tint = cLampColours[i].rgb;
            float peak = max(tint.r, max(tint.g, tint.b));
            float3 centre = peak * pow(saturate(tint / max(peak, .0001)), .58);
            float falloff = saturate((radius - d) * cLampColours[i].w);
            sum += (tint * (.80 * .36 * soft) + centre * (.98 * core)) * envelope * falloff;
        }
    }
    float air=cEnvironment.x>.5 ? exp(cEnvironment.w*distance/max(cEnvironment.z,1)) : 1;
    return float4(sum*(SpecularStrength()*air*(1-GameFog(distance,view))*(1-saturate(detail.z))*saturate(thickness/.35)),0);
}
