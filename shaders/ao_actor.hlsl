// Issue #106: finite actor occlusion, without weakening static-scene AO.
// Original effects.hlsl entry points and bytecode remain unchanged. Only a
// complete, depth-matched native actor ownership mask enables these variants.
#include "effects.hlsl"

sampler2D AOActorMask : register(s4);
float ActorAt(float2 uv) {
    uv=(floor(uv/ImageAndClip.xy)+.5)*ImageAndClip.xy;
    return tex2Dlod(AOActorMask,float4(uv,0,0)).r;
}
bool ActorContact(float2 uv,float3 p,float3 n) {
    if(ActorAt(uv)>0)return true;
    float3 t=cross(n,abs(n.z)<.9?float3(0,0,1):float3(0,1,0));
    t*=rsqrt(max(dot(t,t),1e-12));
    float3 b=cross(n,t);
    float2 dir=float2(1,0);
    // A ground-plane ring misses a low foot at shallow camera pitch. Lifted
    // probes cover that contact volume without increasing the accepted .25yd
    // tangent distance or protecting the distant halo under the torso.
    [loop]for(int k=0;k<13;++k){
        float3 q;
        if(k<8){
            q=p+.25*(t*dir.x+b*dir.y);
            dir=float2(dir.x-dir.y,dir.x+dir.y)*.707106781186548;
        }else{
            if(k==8)dir=float2(1,0);
            q=p+.125*n;
            if(k>8){q+=.125*(t*dir.x+b*dir.y);dir=float2(-dir.y,dir.x);}
        }
        if(q.z<=ImageAndClip.z)continue;
        float2 probe=q.xy*Lighting.xy/q.z*float2(.5,-.5)+.5;
        if(min(min(probe.x,probe.y),min(1-probe.x,1-probe.y))<=0)continue;
        [branch]if(ActorAt(probe)>254.5/255.){
            float4 a=AOSample(probe);float3 delta=a.xyz-p;
            if(a.w>0 && dot(delta,n)>0 && max(dot(delta,delta)-dot(delta,n)*dot(delta,n),0)<=.25*.25)return true;
        }
    }
    return false;
}
float ActorWidth(float2 uv,float z) {
    uv=(floor(uv/ImageAndClip.xy)+.5)*ImageAndClip.xy;
    float dx=.0625*Lighting.x/z;
    float2 gap=0;
    [loop]for(int k=1;k<=8;++k){
        [branch]if(gap.x==0){if(ActorAt(uv-float2(k*dx,0))<254.5/255.)gap.x=k;}
        [branch]if(gap.y==0){if(ActorAt(uv+float2(k*dx,0))<254.5/255.)gap.y=k;}
        if(min(gap.x,gap.y)>0)break;
        // Missing gaps are at least k+1 steps away. Once their minimum sum
        // exceeds nine, a <=1yd midpoint width is impossible; retain baseline.
        float2 earliest=gap>0?gap:float2(k+1,k+1);
        if(earliest.x+earliest.y>9)break;
    }
    float width=max((gap.x+gap.y-1)*.125,.125);
    return min(gap.x,gap.y)>0 && width<=1?width:0;
}
float4 SideArc(float4 h,float cosine,float sine) {
    h=clamp(h,-1,1);
    float4 a=abs(h);
    float4 theta=(((-.0187293*a+.0742610)*a-.2121144)*a+1.5707288)*sqrt(max(1-a,0));
    theta=h<0?3.14159265359-theta:theta;
    return .5*(cosine*(1-h*h)+sine*(theta-h*sqrt(max(1-h*h,0))));
}
float ActorGTAO(float2 uv) {
    uv=DepthTexelUV(uv,1/ImageAndClip.xy);
    float d=ReadDepth(uv);if(d>=SKY_DEPTH || IsWater(uv,d))return 1;
    float4 prepared=tex2Dlod(Ambient,float4(uv,0,0));
    float3 p=Position(uv,d),n=prepared.xyz*rsqrt(max(dot(prepared.xyz,prepared.xyz),1e-12));
    bool protectedReceiver=prepared.a>.5;
    float2 uvRadius=.5*Options.y*Lighting.xy/p.z;
    uvRadius*=min(1,.25/max(uvRadius.x,uvRadius.y));
    float3 view=-p*rsqrt(max(dot(p,p),1e-12));
    float2 direction=AORotation(uv);
    float loss=0,opened=0;
    [loop]for(int slice=0;slice<4;++slice){
        float3 sd=float3(direction.x,-direction.y,0);
        float3 t=sd-view*dot(sd,view),axis=cross(t,view);
        axis*=rsqrt(max(dot(axis,axis),1e-12));
        float3 pn=n-axis*dot(n,axis);
        float il=rsqrt(max(dot(pn,pn),1e-12)),plen=1/il;
        float co=saturate(dot(pn,view)*il);
        float si=(dot(t,pn)>=0?1:-1)*sqrt(max(1-co*co,0));
        opened+=plen*(co+AOAcos(co)*abs(si));
        [loop]for(int side=0;side<2;++side){
            float sign=side==0?1:-1;
            float low=-sign*si,environment=low,front=low;
#if NORTHLIGHT_GTAO_ISOLATED_HORIZON
            float solidEnvironment=low,environmentCoverage=0;
#endif
            float2 chosenUV=uv;float3 chosenQ=p;float chosenWeight=0;
            [loop]for(int step=0;step<4;++step){
                float sr=(step+1)*.2;
                float2 offset=sign*direction*uvRadius*sr*sr;
                float2 tapUV=uv+offset;
                float4 q=AOSample(tapUV);float3 delta=q.xyz-p;
                float id=rsqrt(max(dot(delta,delta),1e-12)),distance=1/id;
                float weight=saturate((Options.y-distance)/(Options.y*.5))*q.w;
                weight*=distance>.00001 && any(abs(offset)>.51*ImageAndClip.xy) && dot(n,delta)>.08*distance?1:0;
                float h=lerp(low,dot(delta,view)*id,weight);
                bool actor=false;
                [branch]if(!protectedReceiver && weight>0)actor=ActorAt(tapUV)>254.5/255.;
                if(actor){if(h>front){front=h;chosenUV=tapUV;chosenQ=q.xyz;chosenWeight=weight;}}
                else {
#if NORTHLIGHT_GTAO_ISOLATED_HORIZON
                    float thinDistance=sqrt(dot(delta,delta)+3*delta.z*delta.z);
                    float thinWeight=saturate((Options.y-thinDistance)/(Options.y*.5))*q.w;
                    thinWeight*=distance>.00001 && any(abs(offset)>.51*ImageAndClip.xy) && dot(n,delta)>.08*distance?1:0;
                    solidEnvironment=max(solidEnvironment,h);
                    environmentCoverage+=h>low+.00001?1:0;
                    environment=max(environment,lerp(low,dot(delta,view)*id,thinWeight));
#else
                    environment=max(environment,h);
#endif
                }
            }
#if NORTHLIGHT_GTAO_ISOLATED_HORIZON
            environment=lerp(environment,solidEnvironment,saturate(environmentCoverage-1));
#endif
            float high=max(environment,front),back=environment;
            [branch]if(front>environment+.00001){
                float thickness=ActorWidth(chosenUV,chosenQ.z);
                [branch]if(thickness>0){
                    // The winning tap already carries its snapped position and
                    // complete range weight. Do not fetch/decode its depth again.
                    float3 q=chosenQ;
                    float3 qb=q+q*rsqrt(max(dot(q,q),1e-12))*thickness-p;
                    float b=lerp(low,dot(qb,view)*rsqrt(max(dot(qb,qb),1e-12)),chosenWeight);
                    back=max(environment,min(b,front));
                }
            }
            float s=sign*si;
            [branch]if(high>low+.00001){
                float4 arcs=SideArc(float4(low,high,environment,back),co,s);
                loss+=plen*max(arcs.x-arcs.y-arcs.z+arcs.w,0);
            }
        }
        direction=float2(direction.x-direction.y,direction.x+direction.y)*.707106781186548;
    }
    return saturate(1-Lighting.z*loss/max(opened,.00001));
}
float4 GTAOActorContactBloom(float2 uv:TEXCOORD0):COLOR0{return float4(ContactBloomRgb(uv),ActorGTAO(uv));}
float4 AOActorPrepare(float2 uv:TEXCOORD0):COLOR0{
    uv=DepthTexelUV(uv,1/ImageAndClip.xy);
    float d=ReadDepth(uv);if(d>=SKY_DEPTH)return float4(0,0,-1,1);
    float3 p=Position(uv,d),n=SurfaceNormal(uv,p);n*=dot(n,p)>0?-1:1;
    return float4(n,ActorContact(uv,p,n)?1:0);
}

// Preserve HBAO's own bias and distance kernel. Only its actor tap receives
// the finite band's fraction of cosine-weighted slice coverage.
float ActorSliceRatio(float3 p,float3 n,float3 q,float width) {
    float3 view=-p*rsqrt(max(dot(p,p),1e-12)),delta=q-p;
    float3 tangent=delta-view*dot(delta,view),axis=cross(tangent,view);
    axis*=rsqrt(max(dot(axis,axis),1e-12));
    float3 projected=n-axis*dot(n,axis);
    float il=rsqrt(max(dot(projected,projected),1e-12));
    float co=saturate(dot(projected,view)*il);
    float si=(dot(tangent,projected)>=0?1:-1)*sqrt(max(1-co*co,0));
    float lo=-si,front=max(lo,dot(delta,view)*rsqrt(max(dot(delta,delta),1e-12)));
    float3 backDelta=q+q*rsqrt(max(dot(q,q),1e-12))*width-p;
    float back=max(lo,min(front,dot(backDelta,view)*rsqrt(max(dot(backDelta,backDelta),1e-12))));
    float4 arc=SideArc(float4(lo,front,back,front),co,si);
    float denominator=arc.x-arc.y;
    return denominator>1e-9?saturate((arc.z-arc.y)/denominator):1;
}
float ActorHBAO(float2 uv) {
    uv=DepthTexelUV(uv,1/ImageAndClip.xy);
    float d=ReadDepth(uv);if(d>=SKY_DEPTH || IsWater(uv,d))return 1;
    float4 prep=tex2Dlod(Ambient,float4(uv,0,0));
    float3 p=Position(uv,d),n=prep.xyz*rsqrt(max(dot(prep.xyz,prep.xyz),1e-12));
    bool protect=prep.a>.5;
    float2 uvRadius=.5*Options.y*Lighting.xy/p.z;
    uvRadius*=min(1,.25/max(uvRadius.x,uvRadius.y));
    float2 direction=AORotation(uv);
    float2 obscurance=0;
    [loop]for(int directionIndex=0;directionIndex<8;++directionIndex){
        [loop]for(int step=0;step<4;++step){
            float2 tapUV=uv+direction*uvRadius*((step+1)*.2);
            float4 q=AOSample(tapUV);float3 delta=q.xyz-p;
            float d2=max(dot(delta,delta),1e-12);
            float id=rsqrt(d2);
            float weight=saturate(dot(n,delta)*id-.1)*saturate(1-d2/(Options.y*Options.y))*q.w;
            float originalWeight=weight;
            [branch]if(!protect && weight>0 && p.z-q.z>.5){
                [branch]if(ActorAt(tapUV)>254.5/255.){
                    float width=ActorWidth(tapUV,q.z);
                    [branch]if(width>0){
                        float ratio=ActorSliceRatio(p,n,q.xyz,width);
                        weight*=lerp(1,ratio,saturate((p.z-q.z-.5)*5));
                    }
                }
            }
            obscurance+=float2(originalWeight,weight);
        }
        direction=float2(direction.x-direction.y,direction.x+direction.y)*.707106781186548;
    }
    float2 visibility=saturate(1-Lighting.z*obscurance*(2./(32.*.9)));
    // Large-radius coarse HBAO taps are undersampled for a thin silhouette.
    // Keep the tested full correction through 2yd and taper to baseline at 3yd.
    return lerp(visibility.x,visibility.y,saturate(3-Options.y));
}

// Legacy composition already carries its original color bounce in RGB.
// Alpha-only variants eliminate the bloom fetches that COLORWRITE_ALPHA discards.
float4 HBAOActorContactBloom(float2 uv:TEXCOORD0):COLOR0 { return float4(ContactBloomRgb(uv),ActorHBAO(uv)); }
float4 HBAOActorOnly(float2 uv:TEXCOORD0):COLOR0 { return float4(0,0,0,ActorHBAO(uv)); }
float4 GTAOActorOnly(float2 uv:TEXCOORD0):COLOR0 { return float4(0,0,0,ActorGTAO(uv)); }
