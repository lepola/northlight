#include "../shaders/hdr_effects.hlsl"

// Original compatibility transfer, retained as a pixel oracle for sparse work.
float4 HdrLayerReference(float2 uv:TEXCOORD0):COLOR0 {
    float4 layer=tex2D(HdrScene,uv);float alpha=saturate(layer.a);
    float3 color=0;
    [branch]if(HdrParams.x<=.5||alpha>0){
        float3 encoded=HdrParams.x>.5?layer.rgb/max(alpha,.000001):layer.rgb;
        color=encoded;
        [branch]if(HdrParams.y<=.5)color=hdrDecode(saturate(encoded))*HdrColor.rgb;
        if(HdrParams.x>.5)color*=alpha;
    }
    return float4(color,alpha);
}
