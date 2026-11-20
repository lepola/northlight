#include "common.hlsli"

sampler2D sSceneBeforeSlime : register(s2);

static const float kNoSlime = 0.99999;

struct UndoOutput
{
    float4 color : COLOR0;
    float depth : DEPTH;
};

UndoOutput main(float2 pixel : VPOS)
{
    clip(kNoSlime - SampleRawDepth(sDepth, pixel));
    UndoOutput output;
    output.color = tex2Dlod(sSceneBeforeSlime, float4((pixel + 0.5) * cTexelGrid.xy, 0, 0));
    output.depth = SampleRawDepth(sOpaqueDepth, pixel);
    return output;
}
