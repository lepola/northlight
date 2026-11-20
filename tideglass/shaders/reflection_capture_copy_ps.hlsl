// Native point copy for the bounded GPU capture experiment. No colour transform.
sampler2D sSource : register(s0);
float4 cCopy : register(c1); // reciprocal dimensions, source mip, unused
float4 main(float2 pixel : VPOS) : COLOR0
{
    return tex2Dlod(sSource,float4((pixel+0.5)*cCopy.xy,0,cCopy.z));
}
