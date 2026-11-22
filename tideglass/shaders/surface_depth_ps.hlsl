sampler2D opaqueDepth : register(s0);
sampler2D distanceCoverage : register(s1);
float4 projection : register(c0); // normalized depth A/B, viewport minimum/span
float4 grid : register(c1); // reciprocal dimensions, near/far distance

float opaqueDistance(float depth)
{
    return projection.y / (saturate((depth - projection.z) / projection.w) - projection.x);
}

float WetInverseDepth(float2 uv)
{
    float2 surface = tex2Dlod(distanceCoverage, float4(uv, 0, 0)).rg;
    return surface.y > 0.003 && surface.x > grid.z && surface.x <= grid.w ? 1.0 / max(grid.z, surface.x) : 0;
}

// Reciprocal view depth is affine on a plane. A one-pixel seam can run
// through several rows. Require independent opposing pairs on the same plane.
float TileJoinDepth(float2 uv)
{
    float2 dx = float2(grid.x, 0), dy = float2(0, grid.y);
    float left = WetInverseDepth(uv - dx), up = WetInverseDepth(uv - dy);
    float depth = 0;
    [branch] if (left + up > 0)
    {
        float right = WetInverseDepth(uv + dx), down = WetInverseDepth(uv + dy);
        bool horizontal = min(left, right) > 0, vertical = min(up, down) > 0;
        float alongX = 0.5 * (left + right), alongY = 0.5 * (up + down);
        if (horizontal && vertical)
        {
            float centre = 0.5 * (alongX + alongY);
            if (abs(alongX - alongY) < 0.00005 * centre) depth = centre;
        }
        else [branch] if (horizontal || vertical)
        {
            float4 diagonals = float4(WetInverseDepth(uv - dx - dy), WetInverseDepth(uv + dx + dy),
                                      WetInverseDepth(uv - dx + dy), WetInverseDepth(uv + dx - dy));
            float centre = horizontal ? alongX : alongY;
            float2 error = abs(0.5 * (diagonals.xz + diagonals.yw) - centre);
            if (all(diagonals > 0) && max(error.x, error.y) < 0.00005 * centre) depth = centre;
        }
    }
    return depth;
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
    else [branch] if (surface.x == 0 && surface.y == 0)
    {
        float inverseDepth = TileJoinDepth(uv);
        float candidate = projection.z + projection.w * (projection.x + projection.y * inverseDepth);
        if (inverseDepth > 0 && candidate < original - 0.00001)
            result = candidate;
    }
    return float4(result, 0, 0, 1);
}
