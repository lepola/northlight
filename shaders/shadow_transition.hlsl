// Direction-step queries reuse the current production lighting and volume bodies.
#define NORTHLIGHT_SHADOW_TRANSITION 1
#include "world_effects.hlsl"

sampler2D PreviousStaticNear : register(s6);
sampler2D PreviousStaticFar : register(s7);
sampler2D CurrentDynamicNear : register(s8);
sampler2D CurrentDynamicFar : register(s9);
row_major float4x4 PreviousNearMatrix : register(c80);
row_major float4x4 PreviousFarMatrix : register(c84);
float4 PreviousSunDirection : register(c88);
float4 ShadowTransition : register(c89); // new weight near/far, PCSS enabled, reserved
row_major float4x4 PreviousNearToCurrent : register(c90);
row_major float4x4 PreviousFarToCurrent : register(c94);
float4 PreviousFarDirection : register(c98);
float4 CurrentFarDirection : register(c99);
float4 PreviousShadowInfo : register(c100); // old bias near/far, inverse depth span near/far

float previousStaticDepth(float2 uv,bool nearCascade) {
    if(nearCascade)return tex2Dlod(PreviousStaticNear,float4(uv,0,0)).r;
    return tex2Dlod(PreviousStaticFar,float4(uv,0,0)).r;
}
float currentDynamicDepth(float3 q,bool nearCascade) {
    float2 uv=float2(q.x*.5+.5,.5-q.y*.5)+.5*DepthParams.w;
    // A frozen history footprint can leave the current working map at its border.
    float3 edge=float3(min(uv,1-uv),min(q.z,1-q.z));
    if(min(edge.x,edge.y)<0||edge.z<=0)return q.z;
    if(nearCascade)return tex2Dlod(CurrentDynamicNear,float4(uv,0,0)).r;
    return tex2Dlod(CurrentDynamicFar,float4(uv,0,0)).r;
}
float transitionDepth(float2 uv,bool nearCascade,bool previous,float expected){
    if(!previous)return shadowDepth(uv,nearCascade);
    float bias=PassInfo.x*(nearCascade?1:2);
    float3 oldPoint=float3((uv-.5*DepthParams.w)*float2(2,-2)+float2(-1,1),expected+bias);
    float3 currentPoint=nearCascade?affinePoint(oldPoint,PreviousNearToCurrent):affinePoint(oldPoint,PreviousFarToCurrent);
    float scale=nearCascade?PreviousNearToCurrent[2].z:PreviousFarToCurrent[2].z;
    // Only validated small direction steps reach this variant: scale is positive.
    float dynamicDepth=expected+(currentDynamicDepth(currentPoint,nearCascade)-currentPoint.z+bias)/scale;
    return min(previousStaticDepth(uv,nearCascade),dynamicDepth);
}
float transitionShadowMap(float3 lp,bool useNear,bool previous,float2 gradient,bool soft) {
    // Raster NDC maps to integer D3D9 pixel centers. Sampling the resulting
    // texture requires +.5 texel; this also aligns receiver-plane PCF offsets.
    float2 uv=float2(lp.x*.5+.5,.5-lp.y*.5)+.5*DepthParams.w;
    if(any(uv<.003)||any(uv>.997)||lp.z<=0||lp.z>=1)return 1;
    // The far cascade texel covers four times the world distance of the near
    // one, so its slope error is four times larger: scale the bias with it.
    // Constant bias shifts a shadow edge on the ground by bias/tan(elevation),
    // and a bias DIFFERENCE between cascades makes shadows jump where the eye
    // centred cascades hand over while the camera orbits. Keep both small
    // (near 0.05 u, far 0.1 u) and let the receiver plane absorb slopes.
    float receiver=lp.z-PassInfo.x*(useNear?1:2);
    // Depth-derived normals are noisy on foliage. Limit the receiver-plane
    // slope so one flipped normal cannot swing every expected depth, but
    // keep genuine grazing terrain inside the limit down to a 2-degree light
    // incidence (below that saturate(n.L) makes the shadow term negligible).
    // Same world-space slope limit for either depth span; XY texels stay
    // near .094 u / far .375 u. Only normalized depth units change.
    float limit=(useNear?2.688:10.752)*ShadowRange.x/DepthParams.w;
    gradient=clamp(gradient,-limit,limit);
    // Continuous tent (4x4 was 3x3): the wider support gently softens
    // tree/fence edges without random sampling or changing shadow depth bias.
    // Far: radius 2 texels (4x4). Near (0.3.170): radius 2.5 (5x5). An animated
    // caster (idle pose) flips single near texels every frame; one tap then
    // weighs at most 14.8% instead of 25%, for a 24% wider near penumbra.
    // The far cascade keeps exactly its 4x4 taps and weights.
    float size=useNear?5:4,reach=size*.5;
    float2 pixel=uv/DepthParams.w-.5;
    // Odd sizes centre on the nearest texel, even sizes on the one below: every tap in the support.
    float2 base=floor(pixel+(useNear?.5:0)),fraction=pixel-base;
    float start=useNear?-2:-1;
    float visibility=0,total=0,gaps=0,blockers=0;
    // The exit lives in the loop condition: Microsoft's compiler turned the nested bare break into break_ne with identical operands.
    [loop]for(int y=0;y<5&&y<size;++y){
    float rowOffset=y+start,rowUV=(base.y+rowOffset+.5)*DepthParams.w;
    float rowWeight=max(reach-abs(rowOffset-fraction.y),0);
    [loop]for(int x=0;x<5&&x<size;++x){
        float offset=x+start;
        float2 tapUV=float2((base.x+offset+.5)*DepthParams.w,rowUV);
        float w=max(reach-abs(offset-fraction.x),0)*rowWeight;
        // Compare each sample against the depth of the receiver's plane there.
        float expected=receiver+dot(gradient,tapUV-uv);
        float stored=transitionDepth(tapUV,useNear,previous,expected);
        visibility+=(expected<=stored?1:0)*w;total+=w;
        if(soft&&stored<expected){gaps+=expected-stored;++blockers;}
    }
    }
    if(soft){
        // Keep every contact tap above, then search the wider support. The
        // separation uses the receiver plane, so slopes do not create softness.
        const float widest=4.25;
        [loop]for(int by=0;by<4;++by)[loop]for(int bx=0;bx<4;++bx){
            float2 tap=floor(pixel+(float2(bx,by)-1.5)*(widest/1.5)+.5);
            float2 tapUV=(tap+.5)*DepthParams.w;
            float expected=receiver+dot(gradient,tapUV-uv),stored=transitionDepth(tapUV,useNear,previous,expected);
            if(stored<expected){gaps+=expected-stored;++blockers;}
        }
        float texelWorld=2*(useNear?DepthParams.y:DepthParams.z)*DepthParams.w;
        // The fixed shadow span/radius and texel size are positive.
        float softReach=clamp(.04*gaps/max(blockers,1)/(ShadowRange.x*texelWorld),reach,widest);
        // No extra filtering at contact. A sparse search miss retains the
        // complete base PCF instead of erasing thin geometry.
        if(softReach>reach+.001){
            float2 first=floor(pixel-softReach)+1;
            float extent=floor(2*softReach)+1;visibility=0;total=0;
            [loop]for(int sy=0;sy<9&&sy<extent;++sy){
            float rowTap=first.y+sy,rowUV=(rowTap+.5)*DepthParams.w;
            float rowWeight=max(softReach-abs(rowTap-pixel.y),0);
            [loop]for(int sx=0;sx<9&&sx<extent;++sx){
                float2 tap=float2(first.x+sx,rowTap),tapUV=float2((tap.x+.5)*DepthParams.w,rowUV);
                float w=max(softReach-abs(tap.x-pixel.x),0)*rowWeight;
                float expected=receiver+dot(gradient,tapUV-uv);
                visibility+=(expected<=transitionDepth(tapUV,useNear,previous,expected)?1:0)*w;total+=w;
            }
            }
        }
    }
    float edge=saturate((1-max(abs(lp.x),abs(lp.y)))/.12);
    return lerp(1,visibility/max(total,.0001),edge);
}
row_major float4x4 CurrentNearMatrix : register(c105);
row_major float4x4 CurrentFarMatrix : register(c109);
float4 CurrentNearDirection : register(c113);
float4 CurrentFarDirectionFallback : register(c114);
float2 transitionPlane(float3 normal,float confidence,float facing,float radius,row_major float4x4 m){
    float2 plane=float2(dot(normal,float3(m[0].x,m[1].x,m[2].x)),-dot(normal,float3(m[0].y,m[1].y,m[2].y)))*radius;
    return plane*(confidence*2*ShadowRange.x/(facing<0?-max(abs(facing),.15):max(abs(facing),.15)));
}
float directionalShadowFade(float3 p,float3 normal,float confidence,bool previous){
    float cnFacing=dot(normal,CurrentNearDirection.xyz),cfFacing=dot(normal,CurrentFarDirectionFallback.xyz);
    float pnFacing=dot(normal,PreviousSunDirection.xyz),pfFacing=dot(normal,PreviousFarDirection.xyz);
    float cnLift=(1-saturate(abs(cnFacing)))*confidence*2/1024,cfLift=(1-saturate(abs(cfFacing)))*confidence*2/1024;
    float pnLift=(1-saturate(abs(pnFacing)))*confidence*2/1024,pfLift=(1-saturate(abs(pfFacing)))*confidence*2/1024;
    // Current fallback projections precede both old projections: as in the
    // baseline, p's final use is the far affine point, never a matrix selector.
    float3 cn=affinePoint(p+normal*(cnLift*DepthParams.y),CurrentNearMatrix);
    float3 cf=affinePoint(p+normal*(cfLift*DepthParams.z),CurrentFarMatrix);
    float3 pnCover=affinePoint(p,PreviousNearMatrix),pfCover=affinePoint(p,PreviousFarMatrix);
    float3 pn=affinePoint(p+normal*(pnLift*DepthParams.y),PreviousNearMatrix);
    float3 pf=affinePoint(p+normal*(pfLift*DepthParams.z),PreviousFarMatrix);
    bool nearHistory=!any(abs(pnCover*float3(1,1,2)-float3(0,0,1))>.99);
    bool farHistory=!any(abs(pfCover*float3(1,1,2)-float3(0,0,1))>.99);
    float blend=lerp(saturate((max(abs(pn.x),abs(pn.y))-.72)/.18),saturate((max(abs(cn.x),abs(cn.y))-.72)/.18),ShadowTransition.x);
    float3 q=nearHistory?pn:cn,farQ=farHistory?pf:cf;
    float result=0;
    [loop]for(int cascade=0;cascade<2;++cascade){
        bool nearCascade=cascade==0;float radius=nearCascade?DepthParams.y:DepthParams.z;
        float weight=(nearCascade?1-blend:blend)*(1-(nearCascade?ShadowTransition.x:ShadowTransition.y));
        [branch]if(weight>0){
            row_major float4x4 m=nearCascade?(nearHistory?PreviousNearMatrix:CurrentNearMatrix):(farHistory?PreviousFarMatrix:CurrentFarMatrix);
            float facing=nearCascade?(nearHistory?pnFacing:cnFacing):(farHistory?pfFacing:cfFacing);
            float2 plane=transitionPlane(normal,confidence,facing,radius,m);
            result+=transitionShadowMap(nearCascade?q:farQ,nearCascade,nearCascade?nearHistory:farHistory,plane*radius,ShadowTransition.z>.5)*weight;
        }
    }
    return result;
}

// Both epochs use their own world-fixed filter phase. A current dynamic comparison
// is ANDed with each old static comparison before filtering, never after PCF.
float fogShadowFade(float3 currentQ,float3 previousQ) {
    float result=0,currentVisibility=1;
    [loop]for(int epoch=0;epoch<2;++epoch) {
        bool previous=epoch==1;
        float3 q=previous?previousQ:currentQ;
        float2 uv=float2(q.x*.5+.5,.5-q.y*.5)+.5*DepthParams.w;
        float visibility=previous?currentVisibility:1;
        if(all(uv>=0)&&all(uv<=1)&&q.z>=0&&q.z<=1) {
            float2 origin=previous?PreviousFarMatrix[3].xy:FarMatrix[3].xy;
            float2 plane=(q.xy-origin)*float2(DepthParams.z,-DepthParams.z)/1.5;
            float2 fraction=frac(plane),weight=fraction*fraction*(3-2*fraction);
            float bias=(previous?PreviousShadowInfo.y:PassInfo.x)*4;
            visibility=0;
            [loop]for(int tap=0;tap<4;++tap) {
                float row=floor(tap*.5);float2 bit=float2(tap-row*2,row);
                float2 delta=(bit-fraction)*(1.5/(2*DepthParams.z));
                float2 sampleUV=uv+delta;
                float visible;
                if(previous) {
                    float z=tex2Dlod(PreviousStaticFar,float4(sampleUV,0,0)).r;
                    float3 dynamicPoint=affinePoint(float3(q.xy+delta*float2(2,-2),q.z),PreviousFarToCurrent);
                    visible=(q.z-bias<=z&&dynamicPoint.z-PassInfo.x*4<=currentDynamicDepth(dynamicPoint,false))?1:0;
                } else visible=q.z-bias<=tex2Dlod(ShadowFar,float4(sampleUV,0,0)).r?1:0;
                float2 w=lerp(1-weight,weight,bit);
                visibility+=visible*w.x*w.y;
            }
        }
        if(!previous)currentVisibility=visibility;
        result+=visibility*(previous?1-ShadowTransition.y:ShadowTransition.y);
    }
    return result;
}

row_major float4x4 CurrentFarToPrevious : register(c101);
float fogShadowProjected(float3 q){return fogShadowFade(q,affinePoint(q,CurrentFarToPrevious));}

// Keep the current projections as direct constant-bank expressions, like the
// baseline. Selecting a whole matrix inside the loop changes native DX9 JIT
// contraction/order and can shift a 1024-map PCF phase by one float ULP.
float currentDirectionalShadowFade(float3 p,float3 normal,float confidence){
    // Consume the old projection first so p's final use is the current far
    // projection, matching the baseline's affine accumulation order.
    float oldLift=(1-saturate(abs(dot(normal,PreviousSunDirection.xyz))))*confidence*2/1024;
    float3 oldNear=affinePoint(p+normal*(oldLift*DepthParams.y),PreviousNearMatrix);
    float oldBlend=saturate((max(abs(oldNear.x),abs(oldNear.y))-.72)/.18);
    float nearFacing=dot(normal,CurrentNearDirection.xyz),farFacing=dot(normal,CurrentFarDirection.xyz);
    float nearLift=(1-saturate(abs(nearFacing)))*confidence*2/1024;
    float farLift=(1-saturate(abs(farFacing)))*confidence*2/1024;
    float3 q=affinePoint(p+normal*(nearLift*DepthParams.y),NearMatrix);
    float3 farQ=affinePoint(p+normal*(farLift*DepthParams.z),FarMatrix);
    float blend=lerp(oldBlend,saturate((max(abs(q.x),abs(q.y))-.72)/.18),ShadowTransition.x);
    float2 nearPlane=float2(dot(normal,float3(NearMatrix[0].x,NearMatrix[1].x,NearMatrix[2].x)),-dot(normal,float3(NearMatrix[0].y,NearMatrix[1].y,NearMatrix[2].y)))*DepthParams.y;
    nearPlane*=confidence*2*ShadowRange.x/(nearFacing<0?-max(abs(nearFacing),.15):max(abs(nearFacing),.15));
    float2 farPlane=float2(dot(normal,float3(FarMatrix[0].x,FarMatrix[1].x,FarMatrix[2].x)),-dot(normal,float3(FarMatrix[0].y,FarMatrix[1].y,FarMatrix[2].y)))*DepthParams.z;
    farPlane*=confidence*2*ShadowRange.x/(farFacing<0?-max(abs(farFacing),.15):max(abs(farFacing),.15));
    float result=0;
    [loop]for(int cascade=0;cascade<2;++cascade){
        bool nearCascade=cascade==0;float radius=nearCascade?DepthParams.y:DepthParams.z;
        float weight=(nearCascade?1-blend:blend)*(nearCascade?ShadowTransition.x:ShadowTransition.y);
        [branch]if(weight>0)result+=shadowMapFiltered(nearCascade?q:farQ,nearCascade,radius,(nearCascade?nearPlane:farPlane)*radius,ShadowTransition.z>.5)*weight;
    }
    return result;
}
float4 DirectionalShadowTransitionCurrent(float2 uv:TEXCOORD0):COLOR0 {
 float2 screen=uv;uv=depthUV(uv);float depth=normalizedDepth(uv);
 if(depth>=.99999||waterDistance(uv,depth)>0)return 1;
 float4 normal=tex2Dlod(NormalBuffer,float4(screen,0,0));
 float3 p=worldPosition(uv,depth)+normal.xyz*(.08*normal.w);
 float v=currentDirectionalShadowFade(p,normal.xyz,normal.w);return float4(v,v,v,1);
}
float4 DirectionalShadowTransitionPrevious(float2 uv:TEXCOORD0):COLOR0 {
 float2 screen=uv;uv=depthUV(uv);float depth=normalizedDepth(uv);
 if(depth>=.99999||waterDistance(uv,depth)>0)return 0;
 float4 normal=tex2Dlod(NormalBuffer,float4(screen,0,0));
 float3 p=worldPosition(uv,depth)+normal.xyz*(.08*normal.w);
 float v=directionalShadowFade(p,normal.xyz,normal.w,true);return float4(v,v,v,1);
}
float4 WorldFogTransition(float2 uv:TEXCOORD0):COLOR0 {return WorldFog(uv);}
float4 WorldFogActorTransition(float2 uv:TEXCOORD0):COLOR0 {return WorldFogActor(uv);}
float4 WorldFogActorTiledTransition(float2 uv:TEXCOORD0):COLOR0 {return WorldFogActorTiled(uv);}
float4 FogCloudsTransition(float2 uv:TEXCOORD0):COLOR0 {return FogClouds(uv);}
float4 FogCloudsActorTransition(float2 uv:TEXCOORD0):COLOR0 {return FogCloudsActor(uv);}
float4 FogCloudsActorTiledTransition(float2 uv:TEXCOORD0):COLOR0 {return FogCloudsActorTiled(uv);}
