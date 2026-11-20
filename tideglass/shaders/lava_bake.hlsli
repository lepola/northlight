// The lava's tiling patterns, baked once (and again after a Reset) by Renderer::BakeLavaPatterns, so the surface only
// samples them. The same functions as tools/lava_lab.html's bake shaders, so its preview shows what the game draws.

// One over the target's size.
float4 cBake : register(c0);
sampler2D sBakeInput : register(s0);

float2 BakeUv(float2 pixel) { return (pixel + 0.5) * cBake.x; }

// Random numbers from a fixed table made on the CPU (Renderer::BakeLavaPatterns), so the patterns are the same on any GPU:
// worked out here, how they came out depended on the driver (frac(sin(x) * big) left whole patches of a tile with points
// so unevenly spread they read as solid line). Each period reads its own part of the table.
sampler2D sBakeTable : register(s1);
float4 FromTable(float2 cell, float period)
{
    float2 at = cell + float2(period * 7.0, period * 13.0);
    at -= floor(at / 256.0) * 256.0;
    return tex2Dlod(sBakeTable, float4((at + 0.5) / 256.0, 0, 0));
}
float2 Hash22(float2 cell, float period)
{
    float4 t = FromTable(cell + float2(101.0, 57.0), period);
    return (t.rb * 255.0 + t.ga) / 256.0;
}
float Hash12(float2 cell, float period)
{
    float4 t = FromTable(cell, period);
    return (t.r * 255.0 + t.g) / 256.0;
}
// GLSL's mod (floored): HLSL's fmod truncates, and differs for negative values.
float2 Wrap(float2 x, float period) { return x - floor(x / period) * period; }

// Value noise repeating every `period` cells.
float Tiled(float2 p, float period)
{
    float2 i = floor(p);
    float2 f = p - i;
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = Hash12(Wrap(i, period), period);
    float b = Hash12(Wrap(i + float2(1, 0), period), period);
    float c = Hash12(Wrap(i + float2(0, 1), period), period);
    float d = Hash12(Wrap(i + float2(1, 1), period), period);
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}
float Fbm(float2 uv, float period, int octaves)
{
    float sum = 0.0;
    float amp = 0.5;
    float total = 0.0;
    for (int i = 0; i < octaves; ++i)
    {
        sum += Tiled(uv * period + i * 11.3, period) * amp;
        total += amp;
        amp *= 0.5;
        period *= 2.0;
    }
    return sum / total;
}
float SmoothMin(float a, float b, float k)
{
    float h = max(k - abs(a - b), 0.0) / max(k, 1.0e-5);
    return min(a, b) - h * h * k * 0.25;
}
// The distance to the nearest wall of a network of cells repeating every `period`; x, the wall's distance (joined round
// by `rounding`), y the cell's point, for telling cells apart.
float2 Network(float2 x, float period, float rounding)
{
    float2 n = floor(x);
    float2 f = x - n;
    float2 mg = 0.0;
    float2 mr = 0.0;
    float md = 8.0;
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i)
        {
            float2 g = float2(i, j);
            float2 r = g + Hash22(Wrap(n + g, period), period) - f;
            float d = dot(r, r);
            if (d < md)
            {
                md = d;
                mr = r;
                mg = g;
            }
        }
    float id = Hash12(Wrap(n + mg, period) + 3.0, period + 1.0);
    md = 8.0;
    for (int j2 = -2; j2 <= 2; ++j2)
        for (int i2 = -2; i2 <= 2; ++i2)
        {
            float2 g = mg + float2(i2, j2);
            float2 r = g + Hash22(Wrap(n + g, period), period) - f;
            float2 between = mr - r;
            if (dot(between, between) > 0.00001)
            {
                float wall = dot(0.5 * (mr + r), normalize(r - mr));
                md = rounding > 0.0 ? SmoothMin(md, wall, rounding) : min(md, wall);
            }
        }
    return float2(md, id);
}
