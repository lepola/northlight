#include "common.hlsli"

float4 cToLight : register(c11);
float4 cFogColor : register(c14);
// x: sample the actual far-water sky edge for the lowest row; y: inverse far view Z.
float4 cHorizonSample : register(c46);
sampler2D sWaterLevel : register(s4);

sampler2D sSceneBefore : register(s2);
sampler2D sPreviousSky : register(s3);

// The sky remembered by height (rows, more of them near the horizon) and by side: column 0 the half of the sky facing
// away from the sun, column 1 the half facing it. Every true-sky pixel seen, in any direction, feeds its height (so the
// memory keeps up almost all the time, whatever blocks one direction) and both sides by how much it faces the sun.
// A height with no sky in view drifts towards the one above it, so a lookup never has to jump to another, and slowly
// towards the fog's colour now, so a sky seen long ago (before the rain, or at sunset) does not linger in reflections
// while the camera looks down. Colours are stored with alpha 1 once known.
static const float kRows = 8.0;
static const float kLowest = 0.01;
static const float kSpan = 0.95;
static const int kSampleColumns = 16;
static const int kSampleRows = 12;
static const float kSmoothing = 0.1;
static const float kBorrow = 0.03;
static const float kForget = 0.004;

float SunwardShare(float3 direction)
{
    float2 across = direction.xy / max(length(direction.xy), 1.0e-4);
    float2 sun = cToLight.xy / max(length(cToLight.xy), 1.0e-4);
    return smoothstep(-0.6, 0.6, dot(across, sun));
}

// A visible sky/backdrop observation. Water is never an observation of the
// atmosphere; all admission stays in this small memory pass.
float4 SeenSky(float2 pixel)
{
    bool inside=all(pixel>=cViewportPx.xy)&&all(pixel<cViewportPx.xy+cViewportPx.zw);
    float raw=inside?SampleRawDepth(sOpaqueDepth,pixel):0;
    bool visible=inside&&IsTrueSky(raw);
    // Water is not sky: its own reflection would feed back into the memory.
    if(visible&&WaterThickness(pixel,SampleRawDepth(sDepth,pixel))>0)visible=false;
    float3 colour=visible?tex2Dlod(sSceneBefore,float4((pixel+.5)*cTexelGrid.xy,0,0)).rgb:0;
    return float4(colour,visible?1:0);
}

float4 main(float2 texel : VPOS
#ifdef OBSERVE_AIR
    , out float4 observedAir : COLOR1
#endif
) : COLOR0
{
    float2 cell = floor(texel);
    float4 previous = tex2Dlod(sPreviousSky, float4((cell + 0.5) / float2(2.0, kRows), 0, 0));
    float3 sum = 0;
    float weight = 0;
    float4 level=tex2Dlod(sWaterLevel,float4(.5,.5,0,0));
    bool horizon=cHorizonSample.x>.5&&cell.y<.5&&level.g>.5&&
        level.r<cViewToWorld3.z&&abs(cViewToWorld1.z)>1e-4;
    if(horizon)
    {
        for(int x=0;x<kSampleColumns;++x)
        {
            float px=floor(cViewportPx.x+(x+.5)/kSampleColumns*cViewportPx.z);
            float vx=((px+.5-cViewportPx.x)/cViewportPx.z*2-1-cViewToNdc.z)/cViewToNdc.x;
            float vySky=(-vx*cViewToWorld0.z-cViewToWorld2.z)/cViewToWorld1.z;
            float vyFar=vySky+(level.r-cViewToWorld3.z)*cHorizonSample.y/cViewToWorld1.z;
            float skyY=cViewportPx.y+(1-vySky*cViewToNdc.y-cViewToNdc.w)*.5*cViewportPx.w;
            float farY=cViewportPx.y+(1-vyFar*cViewToNdc.y-cViewToNdc.w)*.5*cViewportPx.w;
            bool seen=false;
            // The host's actual liquid extent can differ from its captured far
            // projection. Find the closest admitted sky between that estimate
            // and the geometric horizon, rather than assuming a fixed pixel row.
            // Same 16x12 sample bound as the ordinary sky grid.
            for(int y=0;y<kSampleRows;++y)
            {
                float2 pixel=float2(px,floor(lerp(farY,skyY,(y+.5)/kSampleRows)-1.5));
                float4 observed=SeenSky(pixel);
                if(!seen&&observed.a>.5)
                {
                    float3 view=ViewPosition(pixel,1);
                    float3 direction=normalize(view.x*cViewToWorld0.xyz+view.y*cViewToWorld1.xyz+view.z*cViewToWorld2.xyz);
                    float side=SunwardShare(direction);
                    float w=cell.x>.5?side:1-side;
                    sum+=observed.rgb*w;weight+=w;seen=true;
                }
            }
        }
    }
    else
    {
        for(int y=0;y<kSampleRows;++y)
        {
            for(int x=0;x<kSampleColumns;++x)
            {
                float2 uv=float2((x+.5)/kSampleColumns,(y+.5)/kSampleRows*.8);
                float2 pixel=floor(cViewportPx.xy+uv*cViewportPx.zw);
                float4 observed=SeenSky(pixel);
                float3 view=ViewPosition(pixel,1);
                float3 direction=normalize(view.x*cViewToWorld0.xyz+view.y*cViewToWorld1.xyz+view.z*cViewToWorld2.xyz);
                float row=sqrt(saturate((saturate(direction.z)-kLowest)/kSpan))*kRows-.5;
                float rowWeight=saturate(1-abs(row-cell.y));
                float side=SunwardShare(direction);
                float sideWeight=cell.x>.5?side:1-side;
                float w=observed.a*rowWeight*sideWeight;
                sum+=observed.rgb*w;weight+=w;
            }
        }
    }
#ifdef OBSERVE_AIR
    // Only direct observations can colour the surface seen on the next dive.
    // Borrowed and historical rows still serve the ordinary sky reflection.
    observedAir = weight > 0.05 ? float4(sum / max(weight, 0.05), 1) : 0;
#endif
    if (weight > 0.05)
    {
        float3 seen = sum / weight;
        return float4(previous.a > 0.5 ? lerp(previous.rgb, seen, kSmoothing * saturate(weight)) : seen, 1);
    }
    float4 above = tex2Dlod(sPreviousSky, float4((cell + float2(0.5, 1.5)) / float2(2.0, kRows), 0, 0));
    // Borrowed rows are not observations. Clear them in the normal update so
    // switching extension off immediately restores the original missing-row fallback.
    float4 kept = previous.a > 0.5 ? previous : 0;
    if (above.a > 0.5 && cell.y < kRows - 1.0)
        kept = previous.a > 0.5 ? float4(lerp(previous.rgb, above.rgb, kBorrow), 1) : above;
    return kept.a > 0.5 ? float4(lerp(kept.rgb, cFogColor.rgb, kForget), 1) : kept;
}
