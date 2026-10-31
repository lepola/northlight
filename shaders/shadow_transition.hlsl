// Direction-step variants only. The original entries and their bytecode remain unchanged.
#include "world_effects.hlsl"

sampler2D PreviousStaticNear : register(s6);
sampler2D PreviousStaticFar : register(s7);
sampler2D CurrentDynamicNear : register(s8);
sampler2D CurrentDynamicFar : register(s9);
row_major float4x4 PreviousNearMatrix : register(c80);
row_major float4x4 PreviousFarMatrix : register(c84);
float4 PreviousSunDirection : register(c88);
float4 ShadowTransition : register(c89); // new weight near/far; zw unused
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
    if(any(uv<0)||any(uv>1)||q.z<=0||q.z>=1)return q.z;
    if(nearCascade)return tex2Dlod(CurrentDynamicNear,float4(uv,0,0)).r;
    return tex2Dlod(CurrentDynamicFar,float4(uv,0,0)).r;
}
float transitionShadowMap(float3 q,bool nearCascade,bool previous,float2 gradient) {
    float2 uv=float2(q.x*.5+.5,.5-q.y*.5)+.5*DepthParams.w;
    if(any(uv<.003)||any(uv>.997)||q.z<=0||q.z>=1)return 1;
    float bias=(previous?(nearCascade?PreviousShadowInfo.x:PreviousShadowInfo.y):PassInfo.x)*(nearCascade?1:2);
    float inverseSpan=previous?(nearCascade?PreviousShadowInfo.z:PreviousShadowInfo.w):ShadowRange.x;
    float limit=(nearCascade?2.688:10.752)*inverseSpan/DepthParams.w;
    gradient=clamp(gradient,-limit,limit);
    float size=nearCascade?5:4,reach=size*.5;
    float2 pixel=uv/DepthParams.w-.5;
    float2 base=floor(pixel+(nearCascade?.5:0)),fraction=pixel-base;
    float start=nearCascade?-2:-1;
    float receiver=q.z-bias;
    float visibility=0,total=0;
    [loop]for(int i=0;i<25&&i<size*size;++i) {
        float row=floor((i+.5)/size);
        float2 offset=float2(i-row*size,row)+start;
        float2 tapUV=(base+offset+.5)*DepthParams.w;
        float2 w=max(reach-abs(offset-fraction),0);
        float weight=w.x*w.y;
        float planeDelta=dot(gradient,tapUV-uv);
        float expected=receiver+planeDelta;
        float visible;
        if(previous) {
            float3 planePoint=float3(q.xy+(tapUV-uv)*float2(2,-2),q.z+planeDelta);
            float3 dynamicPoint;
            if(nearCascade)dynamicPoint=affinePoint(planePoint,PreviousNearToCurrent);
            else dynamicPoint=affinePoint(planePoint,PreviousFarToCurrent);
            float dynamicBias=PassInfo.x*(nearCascade?1:2);
            visible=(expected<=previousStaticDepth(tapUV,nearCascade)&&
                     dynamicPoint.z-dynamicBias<=currentDynamicDepth(dynamicPoint,nearCascade))?1:0;
        } else visible=expected<=shadowDepth(tapUV,nearCascade)?1:0;
        visibility+=visible*weight;total+=weight;
    }
    float edge=saturate((1-max(abs(q.x),abs(q.y)))/.12);
    return lerp(1,visibility/max(total,.0001),edge);
}
float directionalShadowFade(float3 p,float3 normal,float confidence) {
    float currentLift=(1-saturate(abs(dot(normal,SunDirection.xyz))))*confidence*2/1024;
    float previousLift=(1-saturate(abs(dot(normal,PreviousSunDirection.xyz))))*confidence*2/1024;
    float3 currentNear=affinePoint(p+normal*(currentLift*DepthParams.y),NearMatrix);
    float3 previousNear=affinePoint(p+normal*(previousLift*DepthParams.y),PreviousNearMatrix);
    float blend=lerp(saturate((max(abs(previousNear.x),abs(previousNear.y))-.72)/.18),
                     saturate((max(abs(currentNear.x),abs(currentNear.y))-.72)/.18),ShadowTransition.x);
    float result=0;
    [loop]for(int cascade=0;cascade<2;++cascade) {
        bool useNear=cascade==0;float radius=useNear?DepthParams.y:DepthParams.z;
        float cascadeWeight=useNear?1-blend:blend;
        if(cascadeWeight>0) {
            float currentVisibility=1;
            [loop]for(int epoch=0;epoch<2;++epoch) {
                bool previous=epoch==1;
                row_major float4x4 m=previous?(useNear?PreviousNearMatrix:PreviousFarMatrix):(useNear?NearMatrix:FarMatrix);
                float3 direction=previous?(useNear?PreviousSunDirection.xyz:PreviousFarDirection.xyz):(useNear?SunDirection.xyz:CurrentFarDirection.xyz);
                float inverseSpan=previous?(useNear?PreviousShadowInfo.z:PreviousShadowInfo.w):ShadowRange.x;
                float facing=dot(normal,direction);
                float lift=(1-saturate(abs(facing)))*confidence*2/1024;
                float3 q=affinePoint(p+normal*(lift*radius),m);
                float2 plane=float2(dot(normal,float3(m[0].x,m[1].x,m[2].x)),
                                    -dot(normal,float3(m[0].y,m[1].y,m[2].y)))*radius;
                plane*=confidence*2*inverseSpan/(facing<0?-max(abs(facing),.15):max(abs(facing),.15));
                float2 uv=float2(q.x*.5+.5,.5-q.y*.5)+.5*DepthParams.w;
                float visibility;
                if(previous&&(any(uv<.003)||any(uv>.997)||q.z<=0||q.z>=1))visibility=currentVisibility;
                else visibility=transitionShadowMap(q,useNear,previous,plane*radius);
                if(!previous)currentVisibility=visibility;
                float newWeight=useNear?ShadowTransition.x:ShadowTransition.y;
                result+=visibility*cascadeWeight*(previous?1-newWeight:newWeight);
            }
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


LightingOutput WorldLightingFade(float2 uv:TEXCOORD0) {
    float4 smoothNormal=tex2Dlod(NormalBuffer,float4(uv,0,0));float2 halfUV=uv;
    uv=depthUV(uv);float d=normalizedDepth(uv);
    LightingOutput o;o.correction=float4(0,0,0,SourcePolicy.y);o.baseline=float4(max(AmbientLight.rgb,.15),1);
    if(d>=.99999||waterDistance(uv,d)>0)return o;
    float3 p=worldPosition(uv,d);float3 n=smoothNormal.xyz;
    float shadow=1;
    // 0.3.209 (task 29): the moving casters near the player shadow from the actor map (ActorShadow, written per source at this
    // pass's half-res grid; white when off); the near cascade skips those, so take the darker of both.
    if(dot(DirectLight.rgb,1)>0){shadow=directionalShadowFade(p+n*(.08*smoothNormal.w),n,smoothNormal.w);
        float2 actor=tex2Dlod(ActorVisibility,float4(halfUV,0,0)).rg;shadow=min(shadow,SunDirection.w>.5?actor.g:actor.r);}
    float visibility=lerp(1,shadow,GridInfo.z);
    // Sun retains the authored angular response, with the regional light color.
    // Moon replaces its share of
    // that response with light from the visible moon, using the smoothed normal.
    // Subtract the old share first: an additive-only moon would double-count
    // native direct light. A fully occluded moon contributes zero new light.
    float3 old=LegacyDirect.rgb*saturate(dot(n,LegacyDirection.xyz));
    float3 painted=old*SourcePolicy.y;
    // 0.3.198 (rain): rain softens the DIRECT shadowing only (z = the share removed; 0 = exactly as before: the branch is not taken). The alpha
    // below, the baseline alpha and GridInfo.z (a divisor elsewhere) keep the true visibility, so the removal and the lamps see the real shadow.
    float moonShadow=shadow,sunVisibility=visibility;
    if(WeatherInfo.z>0){moonShadow=lerp(1,shadow,1-WeatherInfo.z);sunVisibility=lerp(1,shadow,GridInfo.z*(1-WeatherInfo.z));}
    float3 moon=DirectLight.rgb*saturate(dot(n,SunDirection.xyz))*moonShadow;
    float3 sun=DirectLight.rgb*saturate(dot(n,LegacyDirection.xyz))*sunVisibility;
    float3 replacement=lerp(sun,moon,SunDirection.w);
    o.correction=float4(replacement-painted,lerp(visibility,shadow,SunDirection.w)*SourcePolicy.y);
    // Mode 2 isolates GI: the shared buffer must contain no direct correction.
    if(PassInfo.z==2)o.correction.rgb=0;
    // r47 K4: the baseline alpha (read nowhere else) keeps this pass's visibility for LocalDirect.
    // Outside the far cascade the visibility is unknown (the map reads as lit): report 0, so
    // LocalDirect treats it as not sunlit and distant interiors keep their lamps.
    float3 cover=affinePoint(p,FarMatrix);
    float known=max(abs(cover.x),abs(cover.y))<=1&&cover.z>=0&&cover.z<=1?1:0;
    o.baseline=float4(max(old+AmbientLight.rgb,.15),lerp(visibility,shadow,SunDirection.w)*known);return o;
}


float4 WorldFogFade(float2 uv:TEXCOORD0):COLOR0 {
    if(FogInfo.x<.5)return float4(0,0,0,1);
    uv=depthUV(uv);
    float depth=normalizedDepth(uv);float3 view=viewPositionDistance(uv,fogReceiverDistance(uv,depth));
    // Rotate a direction directly: adding then subtracting a large camera
    // translation loses precision as the player moves through world space.
    float3 ray=view.x*InverseView[0].xyz+view.y*InverseView[1].xyz+view.z*InverseView[2].xyz;
    float surface=min(length(ray),FogInfo.w); // host supplies the positive 128-unit range
    ray*=rsqrt(max(dot(ray,ray),1e-12));
    float3 shadowOrigin=affinePoint(Camera.xyz,FarMatrix);
    float3 shadowRay=ray.x*FarMatrix[0].xyz+ray.y*FarMatrix[1].xyz+ray.z*FarMatrix[2].xyz;
    float3 previousOrigin=affinePoint(Camera.xyz,PreviousFarMatrix);
    float3 previousRay=ray.x*PreviousFarMatrix[0].xyz+ray.y*PreviousFarMatrix[1].xyz+ray.z*PreviousFarMatrix[2].xyz;
    float transmittance=1,directWeight=0;
    float cosine=dot(ray,lerp(PreviousFarDirection.xyz,CurrentFarDirection.xyz,ShadowTransition.y)); // both directions are normalized
    // Normalized HG(g=.85) + isotropic mixture: most energy is broad enough
    // for side/back views of geometry-cut shafts; a small forward aureole remains.
    float narrow=.2775*pow(max(1.7225-1.7*cosine,.01),-1.5);
    // The phase describes the air, independent of screen-space disc occlusion.
    // Each sample's shadow ray determines illumination; receiver depth ends
    // the march at foreground geometry, including the player.
    // 0.3.145: the broad part leans gently toward the source (x1.15 facing it,
    // x1 at 90 degrees, x.85 facing away); its sphere integral is unchanged.
    float phasePi=mad(.03,narrow,mad(.033,cosine,.22));
    float3 sunSource=max(DirectLight.rgb,0)*phasePi*saturate(FogColor.rgb)*max(FogInfo.y,0);
    // Intervals are cut by fixed world-axis planes, not concentric shells
    // travelling with the camera. Use the dominant ray axis to avoid grazing
    // steps; clip both end intervals so their optical path length stays exact.
    float3 axis=abs(ray);
    float2 major=axis.x>=axis.y?float2(ray.x,Camera.x):float2(ray.y,Camera.y);
    major=abs(major.x)>=axis.z?major:float2(ray.z,Camera.z);
    float spacing=mad(FogInfo.w,1.0/48,FogStepInfo.z); // 0.3.200 (gpu budget): c64.z 0 = the 48 intervals; reduced levels fewer (host: 128*(1/N-1/48))
    float stepLength=spacing/abs(major.x);
    float offset=frac(major.y*(major.x<0?-1:1)/spacing);
    // Put the exit comparison on the loop: native SM3 compilation of a nested
    // bare break emitted break_ne with identical operands, marching past surface.
    // Intervals ending before the near fade have exactly zero extinction.
    // Skip whole world-grid intervals beyond the regional field support.
    float2 invRay=(ray.xy<0?-1:1)/max(abs(ray.xy),1e-8);
    float2 fieldA=(RegionalFogInfo.xy-Camera.xy)*invRay;
    float2 fieldB=(RegionalFogInfo.xy+63.0/(64*max(RegionalFogInfo.z,1e-8))-Camera.xy)*invRay;
    float2 entry=min(fieldA,fieldB),exit=max(fieldA,fieldB);
    float fieldEntry=max(0,max(entry.x,entry.y)),fieldExit=min(exit.x,exit.y);
    if(fieldExit<fieldEntry)return float4(0,0,0,1);
    float marchEnd=min(surface,fieldExit+stepLength);
    int first=max(0,(int)floor(max(max(FogRange.x,0),fieldEntry-stepLength)/stepLength+offset));
    [loop]for(int i=first;i<49&&max(0,(i-offset)*stepLength)<marchEnd;++i){
        float start=max(0,(i-offset)*stepLength);
        float end=min((i+1-offset)*stepLength,surface);
        float stepSize=end-start;
        float3 p=Camera.xyz+ray*((start+end)*.5);
        float2 fieldUV=(p.xy-RegionalFogInfo.xy)*RegionalFogInfo.z+.5/64;
        float coverage;
        float4 field=regionalFogAt(fieldUV,coverage);
        float altitude=p.z-field.x;
        // Field height also identifies the forest policy: 5 Duskwood,
        // 2.5 STV, 1.25 other forests, .625 sparse outdoor air.
        float profile=saturate((field.w-2.5)/2.5);
        float generalForest=1-saturate((field.w-1.25)/1.25);
        float groundHeight=lerp(field.w,6,RegionalFogInfo.w*(1-profile));
        float vertical=saturate(1-altitude/max(groundHeight,.001));
        // Coverage already rejects absent/indoor nodes and out-of-field UVs.
        float valid=altitude>=0?coverage:0;
        // Authored regional density is world-fixed. No slanted periodic wave
        // whose apparent vertical phase scrolls when the player moves.
        float groundSigma=max(field.y+field.z*RegionalFogInfo.w,0)*vertical*vertical;
        // Existing 5/2.5-unit profile tags select Duskwood/STV. This separate
        // air term reaches tree crowns while preserving
        // finite ground anchoring, indoor exclusions and exact ground policy.
        float airHeight=lerp(VolumeAir.w,VolumeAir.z,profile);
        float airVertical=saturate(1-altitude/max(airHeight,.001));
        // Generic forest air has its own smoothly time-blended host density.
        // Night air must not become thinner than day air while the moon rises.
        float airBase=lerp(VolumeAir.y,VolumeAir.x,profile);
        airBase=lerp(airBase,FogColor.w,generalForest);
        // Shared outdoor haze doubled; regional additions stay independent.
        airBase=WeatherInfo.w+airBase*saturate(mad(field.w,1.6,-1)); // 0.3.198 (rain): c59.w = .0017 + extra air extinction (the literal moved into the constant: no extra slot), mirrored by sigmaAt on the CPU
        float airSigma=airBase*airVertical*airVertical;
        // Fog is thin at the viewer and thickens with distance: density ramps
        // from zero at FogRange.x to full one fade length later, so the player
        // and nearby ground stay clear while the middle distance fills in.
        float nearFade=saturate(((start+end)*.5-FogRange.x)*FogRange.y);nearFade*=nearFade*(3-2*nearFade);
        float sigma=max(groundSigma+airSigma,0)*valid*nearFade;
        if(sigma>0){
            float absorb=1-exp(-sigma*stepSize);
            // A zero source has no valid shadow pass/maps to sample. Its air
            // still contributes environment scattering and extinction.
            float heightFade=saturate(altitude*(1.0/12));heightFade*=heightFade*(3-2*heightFade);
            if(dot(sunSource,1)>0)directWeight+=transmittance*absorb*(fogShadowFade(shadowOrigin+shadowRay*((start+end)*.5),previousOrigin+previousRay*((start+end)*.5))*heightFade);
            transmittance*=1-absorb;
        }
    }
    float3 directScatter=sunSource*directWeight;
    // Bound only accumulated direct scattering, preserving RGB hue and leaving
    // genuine environment fog intact at night or with the source below horizon.
    // No channel of the direct contribution can exceed this source's bound.
    float peak=max(directScatter.r,max(directScatter.g,directScatter.b));
    float cap=max(FogInfo.z,.0001);
    directScatter*=cap/(cap+peak);
    // Constant environment radiance integrates analytically to skySource*(1-T).
    // Keep it independent of direct-source gain and the direct-only soft bound.
    float3 skySource=max(AmbientLight.rgb,0)*saturate(FogColor.rgb)*(SourcePolicy.x*.35);
    return float4(skySource*(1-transmittance)+directScatter,transmittance);
}


float4 FogCloudsFade(float2 uv:TEXCOORD0):COLOR0 {
    // (no FogInfo.x test: the host draws this pass only when c21.x >= .5)
    uv=depthUV(uv);
    float depth=normalizedDepth(uv);float3 view=viewPositionDistance(uv,fogReceiverDistance(uv,depth));
    float3 ray=view.x*InverseView[0].xyz+view.y*InverseView[1].xyz+view.z*InverseView[2].xyz;
    float surface=min(length(ray),FogInfo.w);
    ray*=rsqrt(max(dot(ray,ray),1e-12));
    float3 shadowOrigin=affinePoint(Camera.xyz,FarMatrix);
    float3 shadowRay=ray.x*FarMatrix[0].xyz+ray.y*FarMatrix[1].xyz+ray.z*FarMatrix[2].xyz;
    float3 previousOrigin=affinePoint(Camera.xyz,PreviousFarMatrix);
    float3 previousRay=ray.x*PreviousFarMatrix[0].xyz+ray.y*PreviousFarMatrix[1].xyz+ray.z*PreviousFarMatrix[2].xyz;
    float tauBase=0,tCloud=1,directWeight=0,ambientWeight=0; // scalar sums: the sun and sky colours multiply once after the loop; the base fog as optical depth (exp only inside a cloud)
    float3 axis=abs(ray);
    float2 major=axis.x>=axis.y?float2(ray.x,Camera.x):float2(ray.y,Camera.y);
    major=abs(major.x)>=axis.z?major:float2(ray.z,Camera.z);
    float spacing=mad(FogInfo.w,1.0/40,CloudInfo[0].x); // 0.3.200 (gpu budget): c60.x 0 = the 40 intervals; reduced levels fewer (host: 128*(1/N-1/40))
    float stepLength=spacing/abs(major.x);
    float offset=frac(major.y*(major.x<0?-1:1)/spacing);
    // Skip whole world-grid intervals beyond the regional field support.
    float2 invRay=(ray.xy<0?-1:1)/max(abs(ray.xy),1e-8);
    float2 fieldA=(RegionalFogInfo.xy-Camera.xy)*invRay;
    float2 fieldB=(RegionalFogInfo.xy+63.0/(64*max(RegionalFogInfo.z,1e-8))-Camera.xy)*invRay;
    float2 entry=min(fieldA,fieldB),exit=max(fieldA,fieldB);
    float fieldEntry=max(0,max(entry.x,entry.y)),fieldExit=min(exit.x,exit.y);
    if(fieldExit<fieldEntry)return float4(0,0,0,1);
    float marchEnd=min(surface,fieldExit+stepLength);
    int first=max(0,(int)floor(max(max(FogRange.x,0),fieldEntry-stepLength)/stepLength+offset));
    [loop]for(int i=first;i<41&&max(0,(i-offset)*stepLength)<marchEnd;++i){
        float start=max(0,(i-offset)*stepLength);
        float end=min((i+1-offset)*stepLength,surface);
        float stepSize=end-start;
        float mid=(start+end)*.5;
        float3 rel=ray*mid;
        float3 p=Camera.xyz+rel;
        float2 fieldUV=(p.xy-RegionalFogInfo.xy)*RegionalFogInfo.z+.5/64;
        float coverage;
        float4 field=regionalFogAt(fieldUV,coverage);
        float altitude=p.z-field.x;
        float valid=altitude>=0?coverage:0;
        float nearFade=saturate((mid-FogRange.x)*FogRange.y);nearFade*=nearFade*(3-2*nearFade);
        // 0.3.199 (fog clouds): the ground term of WorldFog, copied exactly (thick Duskwood/STV ground fog must hide the clouds behind it).
        float profile=saturate((field.w-2.5)/2.5);
        float groundHeight=lerp(field.w,6,RegionalFogInfo.w*(1-profile));
        float vertical=saturate(1-altitude/max(groundHeight,.001));
        float groundSigma=max(field.y+field.z*RegionalFogInfo.w,0)*vertical*vertical;
        // Air term reduced to its constant floor for the slot budget (no vertical profile, no forest policy).
        float airSigma=WeatherInfo.w;
        float baseSigma=max(groundSigma+airSigma,0)*valid*nearFade;
        // 0.3.199 (fog clouds, optimisation): the noise only where a bank can be: inside the field above ground, below the tallest bank top
        // (c62.x = 5.5 x height, host), and the small noise only where the large one can still give density (c61.x, host: below it either the
        // bank top or the density is 0 whatever the small noise). Skipped samples had sigma 0.
        float sigma=0;
        [branch]if(min(valid,CloudInfo[2].x-altitude)>0){ // valid > 0 implies altitude >= 0
            float nL=tex3Dlod(CloudNoise,float4(rel*CloudInfo[3].y+CloudInfo[0].yzw,0)).r;
            [branch]if(nL>CloudInfo[1].x){
                float nS=tex3Dlod(CloudNoise,float4(rel*CloudInfo[3].z+CloudInfo[1].yzw,0)).r;
                float density=saturate(mad(nL,CloudInfo[3].w,mad(nS,CloudInfo[3].x,CloudInfo[2].y))); // (.65 nL + .35 nS - threshold) x sharpness, folded on the host
                float low=saturate(1-altitude/max(mad(nL,CloudInfo[2].z,WeatherInfo.y),.001)); // height x (8 nL - 2.5), folded on the host (c62.z 8 height, c59.y -2.5 height) // 0.3.199 (fog clouds): the bank top follows the large noise (0 below nL .31, ~1.5x at .5, ~4x at .8, 5.5x at 1): thick spots tower over the trees (game tests)
                float zone=mad(saturate(mad(field.w,1/3.75,-1.25/3.75)),-.3,1); // lerp(1,.7,saturate((field.w-1.25)/3.75))
                sigma=density*low*low*zone*CloudInfo[2].w*valid*nearFade; // valid > 0 only above ground (altitude >= 0) in a node with a layer height tag
            }
        }
        [branch]if(sigma>0){
            float absorb=1-exp(-sigma*stepSize);
            float heightFade=saturate(altitude*(1.0/12));heightFade*=heightFade*(3-2*heightFade);
            float weight=tCloud*absorb; // the ambient: the pass blend already multiplies the base fog by tCloud, tBase must not enter twice (S*(1-Tb*Tc) in either order)
            // No DirectLight guard and no Tbase*Tcloud early out in this loop: both were cut for the 512-slot budget (the sun term is multiplied by DirectLight after the loop, so no sun = 0).
            directWeight+=exp(-tauBase)*weight*fogShadowFade(shadowOrigin+shadowRay*((start+end)*.5),previousOrigin+previousRay*((start+end)*.5))*heightFade; // the sun light is dimmed by the base fog in front of the cloud
            ambientWeight+=weight;
            tCloud*=1-absorb;
        }
        tauBase+=baseSigma*stepSize;
    }
    // The narrow forward aureole of WorldFog is dropped here (register/slot budget): the broad phase alone.
    float3 directScatter=max(DirectLight.rgb,0)*mad(.033,dot(ray,lerp(PreviousFarDirection.xyz,CurrentFarDirection.xyz,ShadowTransition.y)),.22)*(max(FogInfo.y,0)*directWeight); // FogColor.rgb (the albedo) is always 1 (host): left out for the slots
    float peak=max(directScatter.r,max(directScatter.g,directScatter.b));
    float cap=max(FogInfo.z,.0001);
    directScatter*=cap/(cap+peak);
    // 0.3.199 (fog clouds): the banks take the brighter of the host's cloud colour (c26/c25.w for this pass only: the game's fog colour, the colour
    // the distant world already fades to, raised to a moonlit grey at night; NorthlightFogClouds::colour)
    // and WorldFog's ambient*.35 air radiance, per channel; the storm bands make the game colour grey in rain. By day in rain the air radiance is near
    // black under the storm sky and the game colour wins; at night the game colour is near black while the air WorldFog lit around it
    // is brighter, and a bank that hides that air behind a darker colour reads as a black smear (game tests). The host always sets c25.w 1 here
    // (without a validated game fog its colour is the grey floor alone); FogColor.rgb, the scattering albedo, is always 1: left out for the slots.
    return float4(max(max(AmbientLight.rgb,0)*.35,LegacyFogColor.rgb*LegacyFog.w)*ambientWeight+directScatter,tCloud);
}
