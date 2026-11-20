#define UNDERWATER_FOAM_PASS 1
#include "water_common.hlsli"
#include "underwater_surface.hlsli"
#include "water_foam.hlsli"

// Foam needs its own pass here as above water: combining all its sources with the underwater mirror exceeds the
// shader model 3 temporary registers. Store the wakes' slope (xy) and foam coverage (z); the underwater shader
// supplies lighting/absorption and reads the slope without walking the ships' paths a second time.
float4 main(float2 pixel : VPOS) : COLOR0
{
    float3 camera = cViewToWorld3.xyz;
    float4 level = UnderwaterLevel(false);
    float raw = SampleRawDepth(sOpaqueDepth, pixel);
    float3 view = ViewPosition(pixel, BeyondFarClip(raw) ? 1.0e4 : ViewZ(raw));
    float3 direction = normalize(WorldPosition(view) - camera);
    float distance; float3 normal; bool native;
    UnderwaterHit(pixel,direction,length(view),level,distance,normal,native);
    if (distance <= 0.0 || distance >= length(view))
        return 0.0;
    float3 hit = camera + direction * distance;
    float footprint = PixelFootprint(distance, abs(direction.z));
    float4 wake = ShipWake(hit.xy, hit.z, footprint);
    // A raised ripple meets the upward ray farther away: the signed counterpart of the above-water projection.
    float2 onSurface = FoamPoint(hit.xy, direction);
    float2 seabed = SeabedDepth(onSurface, hit.z);
    // Where the bottom is unknown, the camera's depth is a lower bound on the water's thickness.
    float depth = seabed.y > 0.0 ? max(seabed.x, 0.0) : level.r - camera.z;
    float foam = 0.0;
    [branch] if (FoamStrength() > 0.0)
        foam = WaterFoam(onSurface, hit.z, depth, distance, footprint, wake.zw, cLookingDown.w);
    return float4(wake.xy, foam, 0.0);
}
