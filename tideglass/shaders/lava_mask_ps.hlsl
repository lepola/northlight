// The lava added to the host's mask of what lies on a liquid surface (Renderer::BuildLavaEffectsMask), for its screen
// effects: it casts its shadows, ambient occlusion and lights on the solid world under that mask, not on the mask's
// pixels, and it fogs them by the distance given. The lava glows of itself: shaded, it went dull under a cliff's shadow.
// r: view distance; g: coverage as the host's own mask holds it, above 1 to mark a surface with no seabed to light.
#include "common.hlsli"

sampler2D sHostMask : register(s2);

static const float kNoLava = 0.99999;

float4 main(float2 pixel : VPOS) : COLOR0
{
    float surface = abs(SampleRawDepth(sDepth, pixel));
    float4 host = tex2Dlod(sHostMask, float4((pixel + 0.5) * cTexelGrid.xy, 0, 0));
    // Capture precedes some models. Only the front surface can declare a pixel emissive.
    float visibleDepth = SampleRawDepth(sOpaqueDepth, pixel);
    return surface < kNoLava && surface <= visibleDepth ? float4(ViewZ(surface), 2.0, 0.0, 1.0) : host;
}
