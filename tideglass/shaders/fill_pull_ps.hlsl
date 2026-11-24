// The fill's pull (see Renderer::FillIn): a level half the size of the one below it, each texel the average of the
// known texels of its 2x2 block. RGB and depth in alpha use the same weights. Green in the second target carries
// how much of the block is known.

// x, y: one texel of the finer level in uv; z, w: its size in texels.
float4 cFiner : register(c0);
sampler2D sColour : register(s0);
sampler2D sHit : register(s1);

struct PullOutput
{
    float4 colour : COLOR0;
    float4 hit : COLOR1;
};

void AddTexel(float2 texel, inout float4 colour, inout float known)
{
    float2 uv = (min(texel, cFiner.zw - 1.0) + 0.5) * cFiner.xy;
    float4 hit = tex2Dlod(sHit, float4(uv, 0, 0));
    colour += tex2Dlod(sColour, float4(uv, 0, 0)) * hit.g;
    known += hit.g;
}

PullOutput main(float2 texel : VPOS)
{
    float2 first = texel * 2.0;
    float4 colour = 0.0;
    float known = 0.0;
    AddTexel(first, colour, known);
    AddTexel(first + float2(1.0, 0.0), colour, known);
    AddTexel(first + float2(0.0, 1.0), colour, known);
    AddTexel(first + float2(1.0, 1.0), colour, known);
    PullOutput output;
    output.colour = colour / max(known, 1.0e-4);
    output.hit = float4(0.0, known * 0.25, 0.0, 0.0);
    return output;
}
