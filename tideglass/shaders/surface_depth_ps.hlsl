sampler2D opaqueDepth : register(s0);
sampler2D distanceCoverage : register(s1);
float4 projection : register(c0); // normalized depth A/B, viewport minimum/span
float4 grid : register(c1); // reciprocal dimensions, near/far distance

float opaqueDistance(float depth)
{
    return projection.y / (saturate((depth - projection.z) / projection.w) - projection.x);
}

float4 main(float2 pixel : VPOS) : COLOR0
{
    float2 uv = (pixel + 0.5) * grid.xy;
    float original = tex2Dlod(opaqueDepth, float4(uv, 0, 0)).r;
    float2 surface = tex2Dlod(distanceCoverage, float4(uv, 0, 0)).rg;
    float result = original;
    if (surface.x > grid.z && surface.x <= grid.w && surface.y > 0.003)
    {
        float normalized = saturate(projection.x + projection.y / max(surface.x, grid.z));
        float candidate = projection.z + projection.w * normalized;
        result = min(original, candidate);
        // The mask only holds what passed the depth test, but its distance, converted here, can land on or just behind the
        // ground where the water runs out. Our surface would then skip the pixel and leave the client's water showing, a
        // dotted line along the shore that grows with distance. Later geometry in front of the water is far nearer than this.
        if (candidate >= original && surface.x <= opaqueDistance(original) + max(0.012, surface.x * 0.0007))
            result = original - 1.5e-7;
    }
    return float4(result, 0, 0, 1);
}
