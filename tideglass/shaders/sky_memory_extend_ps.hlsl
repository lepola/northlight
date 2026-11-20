// Source is the updated2x8 sky memory. Alpha1 identifies observations; borrowed
// colours use premultiplied alpha0.25 and can never seed another extension.
sampler2D sUpdatedSky : register(s3);
float4 main(float2 pixel : VPOS) : COLOR0
{
    float2 cell=floor(pixel);
    float4 own=tex2Dlod(sUpdatedSky,float4((cell+0.5)/float2(2,8),0,0));
    float4 lower=0,upper=0;
    float lowerRow=-100,upperRow=100;
    [unroll] for (int row=0;row<8;++row)
    {
        float4 sample=tex2Dlod(sUpdatedSky,float4((cell.x+0.5)/2,(row+0.5)/8,0,0));
        bool observed=sample.a>0.5;
        bool lo=observed && row<=cell.y && row>lowerRow;
        bool hi=observed && row>=cell.y && row<upperRow;
        lower=lo ? sample : lower;lowerRow=lo ? row : lowerRow;
        upper=hi ? sample : upper;upperRow=hi ? row : upperRow;
    }
    bool haveLower=lower.a>0.5,haveUpper=upper.a>0.5;
    float t=saturate((cell.y-lowerRow)/max(upperRow-lowerRow,1));
    float3 colour=haveLower && haveUpper ? lerp(lower.rgb/lower.a,upper.rgb/upper.a,t) :
                  haveLower ? lower.rgb/lower.a : haveUpper ? upper.rgb/upper.a : 0;
    float4 extended=float4(colour*0.25,haveLower || haveUpper ? 0.25 : 0);
    return own.a>0.5 ? own : extended;
}
