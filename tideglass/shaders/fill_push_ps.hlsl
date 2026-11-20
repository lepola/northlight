// The fill's push (see Renderer::FillIn): coming back down the levels, each texel keeps what is known of it and takes
// the rest from the (already filled) level above, blended across, so a hole fills smoothly with what is around it,
// from farther afield the larger it is. At the finest level a texel to be filled (state 1) is marked filled (state 4)
// where anything above was known to fill it from.

// x, y: one texel of this level in uv; z: 1 at the finest level.
float4 cLevel : register(c0);
sampler2D sColour : register(s0);
sampler2D sHit : register(s1);
sampler2D sCoarserColour : register(s2);
sampler2D sCoarserHit : register(s3);

struct PushOutput
{
    float4 colour : COLOR0;
    float4 hit : COLOR1;
};

PushOutput main(float2 texel : VPOS)
{
    float2 uv = (texel + 0.5) * cLevel.xy;
    float4 own = tex2Dlod(sColour, float4(uv, 0, 0));
    float4 ownHit = tex2Dlod(sHit, float4(uv, 0, 0));
    float4 coarser = tex2Dlod(sCoarserColour, float4(uv, 0, 0));
    float4 coarserHit = tex2Dlod(sCoarserHit, float4(uv, 0, 0));
    float known = ownHit.b;
    float state = ownHit.r;
    // At the finest level only the texels to be filled are; the rest keep what they have.
    bool unseen = state > 0.5 && state < 1.5;
    if (cLevel.z > 0.5 && !unseen)
        known = 1.0;
    PushOutput output;
    output.colour = lerp(coarser, own, known);
    float path = lerp(coarserHit.g, ownHit.g, known);
    float filled = max(known, coarserHit.b);
    if (cLevel.z > 0.5 && unseen && coarserHit.b > 0.0)
        state = 4.0;
    output.hit = float4(state, path, filled, 0.0);
    return output;
}
