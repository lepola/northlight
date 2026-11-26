#define main ResolveNativePixel
#include "water_ps.hlsl"
#undef main

// Zero keeps native resolution, including captures taken before this register was used.
float4 cResolvePixel : register(c79);

bool VisibleWater(float2 pixel)
{
    return SampleRawDepth(sDepth, pixel) < SampleRawDepth(sOpaqueDepth, pixel);
}

ReflectionOutput main(float2 pixel : VPOS)
{
    float2 scale = 1.0 + cResolvePixel.xy;
    float2 samplePixel = floor((pixel + 0.5) * scale);
    // A coarse texel can cover both a foreground object and visible water.
    // The final composite still tests every native pixel against opaque depth.
    [branch] if (any(scale > 1.0))
    {
        float2 first = cViewportPx.xy;
        float2 last = first + cViewportPx.zw - 1.0;
        samplePixel = clamp(samplePixel, first, last);
        [branch] if (!VisibleWater(samplePixel))
        {
            // Test native neighbours with the same normalized coordinates used
            // by the final point lookup. Inverting the scale can round the
            // boundary to the other side on odd or offset viewports.
            float2 size = cResolvePixel.zw;
            if (size.x == 0.0) size = floor(rcp(scale * cTexelGrid.xy) + 0.5);
            float2 left = samplePixel - 1.0;
            float2 right = samplePixel + 1.0;
            float2 leftCell = floor(((left + 0.5) * cTexelGrid.xy) * size);
            float2 rightCell = floor(((right + 0.5) * cTexelGrid.xy) * size);
            float2 other = (leftCell == pixel) ? left : ((rightCell == pixel) ? right : samplePixel);
            other = clamp(other, first, last);
            if (VisibleWater(float2(other.x, samplePixel.y))) samplePixel.x = other.x;
            else if (VisibleWater(float2(samplePixel.x, other.y))) samplePixel.y = other.y;
            else if (VisibleWater(other)) samplePixel = other;
        }
    }
    return ResolveNativePixel(samplePixel);
}
