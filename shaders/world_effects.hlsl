// Geometry-backed directional shadows, world-space probe GI and volumetric scattering.
// Inputs are independent of the currently visible screen geometry except receiver depth.
sampler2D Scene : register(s0);
sampler2D Depth : register(s1);
sampler2D ShadowNear : register(s2);
sampler2D ShadowFar : register(s3);
sampler2D ProbeR : register(s4);
sampler2D ProbeG : register(s5);
sampler2D ProbeB : register(s6);
sampler2D ProbeVisibility : register(s7);
sampler2D LightingBuffer : register(s8);
sampler2D ProbePrevious : register(s8); // 0.3.197: WorldGI only: previous SH R|G|B in three horizontal thirds of a 3n*n x n texture
sampler2D FogBuffer : register(s9);
sampler2D WaterMask : register(s11);
sampler2D BaselineLighting : register(s12);
sampler2D RegionalFog : register(s13); // ground, day extinction, night extra, layer height
sampler2D RainMask : register(s13); // 0.3.201 (rain mask): WorldComposite only, rain streak alpha (a), 0 without rain
float4 RegionalFogInfo : register(c31); // world node0 XY, inverse field span, night fraction
float4 WaterInfo : register(c30);
float4 RemovalInfo : register(c30); // RemovalSmooth, TemporalLight: y 1 when a lit source is drawn, z 1/(summed source weight), w disc radius in half-res pixels at view distance 1
sampler2D ProbeMetadata : register(s10); // exact integer world-cell xyz, first-valid time w (-1 invalid)
sampler2D AmbientOcclusion : register(s10); // 0.3.174 TemporalLight/WorldComposite: half-res contact AO (a), pre-weighted bloom (rgb), LINEAR; neutral (0,0,0,1) unless the world composites

float4 ImageClip : register(c0); // invWidth, invHeight, near, far
float4 Projection : register(c1); // signed projection X,Y,Zsign,worldMinZ
float4 DepthParams : register(c2); // inverse world depth range,near radius,far radius,shadow texel
row_major float4x4 InverseView : register(c3);
row_major float4x4 NearMatrix : register(c7);
row_major float4x4 FarMatrix : register(c11);
float4 Camera : register(c15);
float4 TemporalReach : register(c15); // w: TemporalLight only, largest drawn source channel per unit of normalised visibility
float4 SunDirection : register(c16); // toward source
float4 DirectLight : register(c17);
float4 AmbientLight : register(c18); // native ambient RGB, evening surface ambient gain in w
float4 GridOrigin : register(c19); // xyz unused; world probe spacing in w
float4 GridInfo : register(c20); // atlas N,GI intensity,shadow strength,probe ready
float4 FogInfo : register(c21); // regional field ready,direct volume gain,direct RGB bound,max distance
float4 FogColor : register(c22); // scattering albedo.rgb, generic-forest daytime air extinction
float4 LightCenter : register(c23);
float4 PassInfo : register(c24); // shadow bias,softness,debug,seconds
float4 LegacyFog : register(c25); // original VS c12.xyz, validated enabled flag
float4 SourcePolicy : register(c27); // first source, diagnostic energy weight, time, regional density
float4 LegacyDirection : register(c28);
float4 LegacyDirect : register(c29);
float4 LegacyFogColor : register(c26); // proven terrain PS3 c2/c6.rgb or fixed fog RGB
float4 VolumeAir : register(c32); // Dusk/STV extinction, Dusk/STV finite layer height
float4 ScreenSize : register(c33); // full width,height; half-resolution width,height
float4 BlurInfo : register(c34); // half-resolution texel step for the fog blur (x,y), unused
// Final composite only: c34 is rebound AFTER both blur passes (and on fog-off
// frames). Reuse the bank without changing other passes or the 68-register
// diagnostic capture format. Horizon haze extras live in free components.
float4 HorizonHaze : register(c34); // haze RGB (game fog colour), optical depth*log2(e) (0 = off)
float4 ShadowRange : register(c35); // inverse directional-shadow depth span, shared with CPU; yzw: horizon lift colour (WorldComposite only)
float4 LocalLightPos[8] : register(c36); // xyz position, w attenuation end
float4 LocalLightColor[8] : register(c44); // rgb diffuse, w 1/(attenuation end - start)
float4 LocalLightInfo : register(c52); // count, strength, share removed in direct daylight (LocalDirect)
sampler2D NormalBuffer : register(s14); // half-res smoothed world normal (xyz), confidence 0/1 (w, 0 at silhouettes); bound for the lighting passes
sampler2D SourceVisibility : register(s15); // 1x1: temporally smoothed clear-sky fraction over the source disc (fog passes)
sampler2D PreviousSourceVisibility : register(s15); // previous value (SourceVisibilityPS only)
float4 SourceInfo : register(c67); // disc tangent radius, history weight, horizon sun (zw)
float4 HorizonSun : register(c67); // zw: horizontal sun direction x lobe strength (WorldComposite only)
sampler2D LightHistory : register(s14); // previous frame's stabilized lighting buffer (temporal pass only)
sampler2D DepthHistory : register(s15); // previous frame's linear view distance per half-res texel
sampler2D SmoothedLighting : register(s9); // 0.3.185 TemporalLight only: the LightingBuffer after RemovalSmooth (s9 is FogBuffer's slot, idle and rebound before its readers)
row_major float4x4 PreviousView : register(c53);
float4 TemporalInfo : register(c57); // history weight (0 disables), horizon shape (yzw)
float4 HorizonShape : register(c57); // yzw: terrain start view Z, 1/ramp (0 = sky only), log2(e)/sin(band) (WorldComposite only)
float4 FogRange : register(c58); // near fade start, 1/(fade length), lamp glow strength, lamp glow soft cap
float4 LocalLightFog[8] : register(c59); // x extinction at the light (LocalFog reads only .x of c59..c66), yzw unused there
// 0.3.198 (rain): c59.zw are the weather scalars (LocalLightFog[0].zw, free: only .x is read); z is 0 without weather and
// every use below reduces to the old math then (w is the exception by design: it carries the old literal .0017, see WorldFog). Written once per
// frame in the bank, read before the lamp fog batch overwrites c59..c62.
float4 WeatherInfo : register(c59); // z direct shadow softening (WorldLighting), w the shared air extinction .0017 + the extra (WorldFog)
// 0.3.199 (fog clouds): FogClouds only, host-folded (NorthlightFogClouds::shaderConstants), written only while the clouds are active (zero otherwise,
// as before). The .x components are the bank's until the lamp fog batches (a later pass) overwrite c59..c66 .x with LocalLightFog. c59.y -2.5 height;
// c60.yzw large-noise origin; c61.x the large noise below which no bank can be, c61.yzw small-noise origin; c62.x the tallest bank top (5.5 height),
// c62.y -threshold x sharpness, c62.z 8 height, c62.w sigmaMax; c63.x .35 sharpness, c63.yz 1/large and 1/small period, c63.w .65 sharpness.
float4 CloudInfo[4] : register(c60);
// 0.3.200 (gpu budget): c60.x (free in the cloud pass) and c64.z (FogTemporal reads only c64.y) lengthen the clouds' and WorldFog's world-fixed
// intervals at reduced GpuBudgetMs levels (fewer steps over the same 128 units); both 0 at full level, where mad(w,1/N,0) is the old w/N exactly.
float4 FogStepInfo : register(c64);
sampler3D CloudNoise : register(s14); // tileable N^3 L8 volume (LINEAR, WRAP); s14 is NormalBuffer/LightHistory in other passes, never bound together with this one

float2 depthUV(float2 uv) {
    // D3D9 raster centers are integer pixels; texture centers are pixel+.5.
    // Choose the exact depth texel first, including in odd-sized half-res passes.
    float2 dimensions=ScreenSize.xy;
    return (clamp(floor(uv*dimensions),0,dimensions-1)+.5)*ImageClip.xy;
}
float normalizedDepth(float2 uv) {
    // Callers pass an explicit depth texel center, never a half-res boundary.
    return saturate((tex2Dlod(Depth,float4(uv,0,0)).r-Projection.w)*DepthParams.x);
}
float viewDistance(float d) {
    return ImageClip.z*ImageClip.w/max(ImageClip.w-d*(ImageClip.w-ImageClip.z),.00001);
}
float3 viewPositionDistance(float2 uv,float z) {
    float2 raster=uv-.5*ImageClip.xy;
    float2 xy=(raster*float2(2,-2)+float2(-1,1))/Projection.xy;
    return float3(xy,Projection.z)*z;
}
float3 viewPosition(float2 uv,float d) {return viewPositionDistance(uv,viewDistance(d));}
float waterDistance(float2 uv,float d) {
    if(WaterInfo.x<.5)return 0;
    float2 m=tex2Dlod(WaterMask,float4(uv,0,0)).rg;
    return m.x>ImageClip.z && m.x<=viewDistance(d)+max(.012,m.x*.0007) && m.y>.003?m.x:0;
}
float receiverDistance(float2 uv,float d) {
    float water=waterDistance(uv,d);return water>0?water:viewDistance(d);
}
float3 affinePoint(float3 p,row_major float4x4 transformMatrix) {
    // Explicit vector rows avoid scalarizing mul(float4,matrix) in Wine's
    // Shader Model 3 compiler while preserving row-vector multiplication.
    return p.x*transformMatrix[0].xyz+p.y*transformMatrix[1].xyz+
           p.z*transformMatrix[2].xyz+transformMatrix[3].xyz;
}
float3 worldPosition(float2 uv,float d) {
    return affinePoint(viewPosition(uv,d),InverseView);
}
float3 surfaceNormal(float2 uv,float d) {
    // Length comparisons commute with the validated orthonormal view rotation.
    // Construct the normal in view coordinates, then rotate it once.
    float3 p=viewPosition(uv,d);
    float2 dx=float2(ImageClip.x,0),dy=float2(0,ImageClip.y);
    float2 lo=.5*ImageClip.xy,hi=1-lo;
    float2 left=max(uv-dx,lo),right=min(uv+dx,hi);
    float2 up=max(uv-dy,lo),below=min(uv+dy,hi);
    float3 l=viewPosition(left,normalizedDepth(left));
    float3 r=viewPosition(right,normalizedDepth(right));
    float3 u=viewPosition(up,normalizedDepth(up));
    float3 b=viewPosition(below,normalizedDepth(below));
    float3 tx=(uv.x<=lo.x||dot(r-p,r-p)<dot(p-l,p-l))&&uv.x<hi.x?r-p:p-l;
    float3 ty=(uv.y<=lo.y||dot(b-p,b-p)<dot(p-u,p-u))&&uv.y<hi.y?b-p:p-u;
    float3 n=cross(tx,ty);n*=rsqrt(max(dot(n,n),1e-12));
    n=dot(n,-p)<0?-n:n;
    return n.x*InverseView[0].xyz+n.y*InverseView[1].xyz+n.z*InverseView[2].xyz;
}

float shadowDepth(float2 uv,bool useNear) {
    if(useNear)return tex2Dlod(ShadowNear,float4(uv,0,0)).r;
    return tex2Dlod(ShadowFar,float4(uv,0,0)).r;
}
float shadowMap(float3 lp,bool useNear,float radius,float2 gradient) {
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
    float visibility=0,total=0;
    [loop]for(int i=0;i<25;++i){
        if(i>=size*size)break;
        float row=floor((i+.5)/size); // +.5: rcp may give 10/5 = 1.99999, which floors to row 1
        float2 offset=float2(i-row*size,row)+start;
        float2 tapUV=(base+offset+.5)*DepthParams.w;
        float2 weight=max(reach-abs(offset-fraction),0);
        float w=weight.x*weight.y;
        // Compare each sample against the depth of the receiver's plane there.
        float expected=receiver+dot(gradient,tapUV-uv);
        visibility+=(expected<=shadowDepth(tapUV,useNear)?1:0)*w;total+=w;
    }
    float edge=saturate((1-max(abs(lp.x),abs(lp.y)))/.12);
    return lerp(1,visibility/max(total,.0001),edge);
}
float directionalShadow(float3 p,float3 normal,float confidence) {
    float facing=dot(normal,SunDirection.xyz);
    // Normal-offset per cascade: at grazing incidence (low sun on flat sand)
    // one far texel (.375 u) spans over a unit of light depth, so any error
    // in the receiver plane exceeds the .1 u constant bias and the far
    // cascade shades the whole plain, with a front that follows the pivot.
    // Lifting the receiver along its normal by one texel times (1-|n.l|)
    // removes that without a larger depth bias; a silhouette pixel's
    // unreliable normal gets no lift.
    float lift=(1-saturate(abs(facing)))*confidence*2/1024;
    float3 q=affinePoint(p+normal*(lift*DepthParams.y),NearMatrix),farQ=affinePoint(p+normal*(lift*DepthParams.z),FarMatrix);
    float blend=saturate((max(abs(q.x),abs(q.y))-.72)/.18);
    // Matrix columns contain light basis vectors divided by cascade radius.
    float2 plane=float2(dot(normal,float3(NearMatrix[0].x,NearMatrix[1].x,NearMatrix[2].x)),
                        -dot(normal,float3(NearMatrix[0].y,NearMatrix[1].y,NearMatrix[2].y)))*DepthParams.y;
    plane*=confidence*2*ShadowRange.x/(facing<0?-max(abs(facing),.15):max(abs(facing),.15));
    float result=0;
    [loop]for(int cascade=0;cascade<2;++cascade) {
        bool useNear=cascade==0;float radius=useNear?DepthParams.y:DepthParams.z;
        float weight=useNear?1-blend:blend;
        if(weight>0){
            float visibility=shadowMap(useNear?q:farQ,useNear,radius,plane*radius);
            result+=visibility*weight;
        }
    }
    return result;
}
float fogShadow(float3 p) {
    // The 192-unit far map covers the 128-unit air volume. Use it throughout:
    // switching cascade resolution around the moving player changes thin
    // foliage visibility even at an otherwise stationary world sample.
    float3 q=affinePoint(p,FarMatrix);
    float2 uv=float2(q.x*.5+.5,.5-q.y*.5)+.5*DepthParams.w;
    if(any(uv<0)||any(uv>1)||q.z<0||q.z>1)return 1;
    // Filter on a 1.5-world-unit light-plane lattice, independent of the shadow
    // camera/pivot. Fine leaf silhouettes otherwise alias into falling streaks
    // when the view-ray samples move. Interpolate comparisons, never depths.
    float2 plane=(q.xy-FarMatrix[3].xy)*float2(DepthParams.z,-DepthParams.z)/1.5;
    float2 fraction=frac(plane);
    float2 weight=fraction*fraction*(3-2*fraction);
    float receiver=q.z-PassInfo.x*4,visibility=0;
    [loop]for(int tap=0;tap<4;++tap){
        float row=floor(tap*.5);float2 bit=float2(tap-row*2,row);
        float2 sampleUV=uv+(bit-fraction)*(1.5/(2*DepthParams.z));
        float z=tex2Dlod(ShadowFar,float4(sampleUV,0,0)).r;
        float2 w=lerp(1-weight,weight,bit);
        visibility+=(receiver<=z?1:0)*w.x*w.y;
    }
    return visibility;
}
float2 probeUV(float3 cell) {
    return (float2(cell.x+cell.z*GridInfo.x,cell.y)+.5)/float2(GridInfo.x*GridInfo.x,GridInfo.x);
}
float4 probeIrradiance(float3 p,float3 n) {
    if(GridInfo.w<.5)return 0;
    // World lattice is independent of the camera/request grid. Wrapped atlas
    // slots are usable only when metadata names this exact world probe.
    float3 grid=p/GridOrigin.w;
    float3 base=floor(grid),fraction=grid-base;
    float4 basis=float4(.28209479,.48860251*n.x,.48860251*n.y,.48860251*n.z);
    // SH radiance -> cosine-convolved diffuse irradiance.
    basis*=float4(3.14159265,2.0943951,2.0943951,2.0943951);
    float3 result=0;float total=0,coverage=0;
    [loop]for(int i=0;i<8;++i){
        float3 bit=float3(i-floor(i/2.0)*2,floor(i/2.0)-floor(i/4.0)*2,floor(i/4.0));
        float3 cell=base+bit;
        float3 wrapped=cell-GridInfo.x*floor(cell/GridInfo.x);
        float2 uv=probeUV(wrapped);
        float4 metadata=tex2Dlod(ProbeMetadata,float4(uv,0,0));
        if(all(metadata.xyz==cell)&&metadata.w>=0){
        float3 t=lerp(1-fraction,fraction,bit);float weight=t.x*t.y*t.z;
        // First residency fades in at this world point; camera movement and
        // same-key atlas publication cannot reset its activation time.
        weight*=saturate((PassInfo.w-metadata.w)*(1/.45));
        coverage+=weight;
        float3 pp=cell*GridOrigin.w;
        float3 delta=p-pp;float distance=length(delta);
        float3 absolute=abs(delta);float axis;
        if(absolute.x>=absolute.y&&absolute.x>=absolute.z)axis=delta.x>=0?0:1;
        else if(absolute.y>=absolute.z)axis=delta.y>=0?2:3;
        else axis=delta.z>=0?4:5;
        float2 visibilityUV=float2(uv.x,(wrapped.y+axis*GridInfo.x+.5)/(GridInfo.x*6));
        float4 moment=tex2Dlod(ProbeVisibility,float4(visibilityUV,0,0));
        float variance=max(moment.y-moment.x*moment.x,.02);
        float excess=max(distance-moment.x,0);
        float visibility=variance/(variance+excess*excess);
        weight*=visibility*visibility*moment.z;
        float3 irradiance=float3(dot(tex2Dlod(ProbeR,float4(uv,0,0)),basis),dot(tex2Dlod(ProbeG,float4(uv,0,0)),basis),dot(tex2Dlod(ProbeB,float4(uv,0,0)),basis));
        // 0.3.197: a same-key re-publication blends from what was on screen over .3 s (moment.w = start, -1000 = none); first residency keeps born's fade.
        float blend=saturate((PassInfo.w-moment.w)*(1/.3));
        [branch]if(blend<1){float2 third=float2(uv.x*(1/3.),uv.y);
            float3 previous=float3(dot(tex2Dlod(ProbePrevious,float4(third,0,0)),basis),dot(tex2Dlod(ProbePrevious,float4(third+float2(1/3.,0),0,0)),basis),dot(tex2Dlod(ProbePrevious,float4(third+float2(2/3.,0),0,0)),basis));
            irradiance=lerp(previous,irradiance,blend);}
        result+=max(irradiance,0)*weight;total+=weight;
        }
    }
    return total>.00001?float4(result*(1/total),saturate(coverage)):0;
}

// Half-resolution smoothed surface normal. Depth-derived normals are constant
// per triangle, while the game painted its lighting with interpolated vertex
// normals; a wide baseline in WORLD units (about 1.5 u each side, clamped in
// pixels) averages neighbouring facets the way vertex normals do, so shadow
// and GI corrections no longer draw triangle edges on low-poly hills. Depth
// discontinuities fall back to the one-pixel neighbour so silhouettes stay crisp.
// A depth test alone is not enough: a character's legs or a post's base sit
// at nearly the ground's depth, and a wide sample landing on them tilted the
// ground normal in a box around every actor (light patches under feet).
// The wide sample must also continue the one-pixel surface direction: on a
// hill the direction bends by half the facet angle at most (accepted), while
// a sample on another object leaves the local plane (rejected, 1-px fallback).
float3 wideNeighbour(float2 uv,float2 dir,float pixels,float2 lo,float2 hi,float3 center,float centerZ,float3 nearPos) {
    float2 q=depthUV(clamp(uv+dir*pixels,lo,hi));
    float d=normalizedDepth(q);float z=viewDistance(d);
    float3 wide=viewPositionDistance(q,z);
    float3 a=nearPos-center,b=wide-center;
    float planar=dot(a,b)*rsqrt(max(dot(a,a)*dot(b,b),1e-12));
    // A shallow secant can end on a WALL while still agreeing with the ground
    // direction. Compare the local tangent at that endpoint as well: crossing
    // the corner must not bend a ground normal toward the wall. This retains
    // smoothing across gentle terrain facets without an offset bright ring.
    float2 inner=clamp(q-dir,lo,hi);
    float3 end=wide-viewPosition(inner,normalizedDepth(inner));
    float continuation=dot(a,end)*rsqrt(max(dot(a,a)*dot(end,end),1e-12));
    // r76: .95 (about 18 degrees; was .85, 31): a base up to about .9 u high at the 1.5 u
    // baseline passed .85 and tilted the ground normal about 15 degrees, a bright fin of
    // under-removed native light along the screen axis from every rock, cart and post base.
    return abs(z-centerZ)>max(.35,centerZ*.04)||min(planar,continuation)<.95?nearPos:wide;
}
// One axis of the surface tangent frame. A pair of neighbours is only
// trusted when it continues through the centre (cosine >= .85): at the foot
// of a wall the sample toward the wall climbs while the other runs along the
// ground, and averaging them tilted the ground normal into the wall's,
// painting a bright GI/shadow-bias strip along every cliff base. Fall back
// from the wide pair to the one-pixel pair, then to the one-sided difference
// on the side closer in depth (the receiver's own surface).
// A silhouette pixel (no consistent pair even at one pixel) reports
// confidence 0 in the normal buffer's w: its one-sided normal may belong to
// the neighbouring surface. Suppress uncertain positive bounce and normal
// bias there, but retain ambient occlusion: skipping negative GI exposed
// a bright native-ambient seam where the ground touches a wall.
float3 tangentAxis(float3 p,float3 a,float3 b,float3 a1,float3 b1,inout float confidence) {
    float3 da=p-a,db=b-p;
    if(dot(da,db)*rsqrt(max(dot(da,da)*dot(db,db),1e-12))>=.85)return b-a;
    da=p-a1;db=b1-p;
    if(dot(da,db)*rsqrt(max(dot(da,da)*dot(db,db),1e-12))>=.85)return b1-a1;
    confidence=0;
    return abs(b1.z-p.z)<abs(a1.z-p.z)?db:da;
}
float4 WorldNormals(float2 uv:TEXCOORD0):COLOR0 {
    float2 c=depthUV(uv);float d=normalizedDepth(c);float z=viewDistance(d);
    if(d>=.99999)return float4(0,0,1,z);
    float3 p=viewPositionDistance(c,z);
    float2 lo=.5*ImageClip.xy,hi=1-lo;
    // Baseline: 1.5 world units projected to pixels at this distance, 2..40 px.
    float pixels=clamp(1.5*Projection.x*ScreenSize.x*.5/z,2,40);
    float confidence=1;
    float3 tx=0,ty=0;
    // Share the two-axis body to stay within the SM3 static instruction budget.
    [loop]for(int axis=0;axis<2;++axis){
        float2 step=axis==0?float2(ImageClip.x,0):float2(0,ImageClip.y);
        float2 left=max(c-step,lo),right=min(c+step,hi);
        float3 a1=viewPosition(left,normalizedDepth(left));
        float3 b1=viewPosition(right,normalizedDepth(right));
        float3 a=wideNeighbour(c,-step,pixels,lo,hi,p,z,a1);
        float3 b=wideNeighbour(c, step,pixels,lo,hi,p,z,b1);
        // r77: the wide pair only when BOTH wide samples pass. One wide sample on a low object's
        // top against the other side's 1-px neighbour spans the whole secant (a tilt of up to
        // 18 degrees) beside the object; the 1-px pair keeps flat ground flat.
        if(all(a==a1)||all(b==b1)){a=a1;b=b1;}
        float3 t=tangentAxis(p,a,b,a1,b1,confidence);
        if(axis==0)tx=t;else ty=t;
    }
    float3 n=cross(tx,ty);n*=rsqrt(max(dot(n,n),1e-12));
    n=dot(n,-p)<0?-n:n;
    float3 world=n.x*InverseView[0].xyz+n.y*InverseView[1].xyz+n.z*InverseView[2].xyz;
    return float4(world,confidence);
}
struct LightingOutput {float4 correction:COLOR0;float4 baseline:COLOR1;};
LightingOutput WorldLighting(float2 uv:TEXCOORD0) {
    float4 smoothNormal=tex2Dlod(NormalBuffer,float4(uv,0,0));
    uv=depthUV(uv);float d=normalizedDepth(uv);
    LightingOutput o;o.correction=float4(0,0,0,SourcePolicy.y);o.baseline=float4(max(AmbientLight.rgb,.15),1);
    if(d>=.99999||waterDistance(uv,d)>0)return o;
    float3 p=worldPosition(uv,d);float3 n=smoothNormal.xyz;
    float shadow=1;
    if(dot(DirectLight.rgb,1)>0)shadow=directionalShadow(p+n*(.08*smoothNormal.w),n,smoothNormal.w);
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
// Second pass to the same FP16 lighting buffer with COLORWRITEENABLE=RGB.
// Probes contain sky + bounce, so replace ambient instead of adding it twice.
// Invalid/uncovered probes contribute zero correction and retain legacy ambient.
float4 WorldGI(float2 uv:TEXCOORD0):COLOR0 {
    float4 smoothNormal=tex2Dlod(NormalBuffer,float4(uv,0,0));
    uv=depthUV(uv);
    float d=normalizedDepth(uv);if(d>=.99999||waterDistance(uv,d)>0)return 0;
    float3 p=worldPosition(uv,d);float3 n=smoothNormal.xyz;
    float4 probe=probeIrradiance(p+n*.25,n);
    float3 correction=(probe.rgb/3.14159265-AmbientLight.rgb)*GridInfo.y*probe.a;
    // Confidence describes the depth-derived NORMAL, not probe coverage.
    // At a wall/ground seam a valid dark probe must still remove native ambient.
    // Only uncertain added bounce is suppressed; missing probes (probe.a=0)
    // still leave the authored ambient untouched.
    correction=min(correction,0)+max(correction,0)*smoothNormal.w;
    // Boost only the resulting ambient + probe/bounce estimate once, preserving
    // its hue and existing occlusion. No absolute brightness floor, direct-light
    // multiplier or brightened sky/fog. Missing probes retain native ambient.
    correction+=max(AmbientLight.rgb+correction,0)*AmbientLight.w;
    return float4(correction,0);
}
// Four POINT reads work on float32 textures without optional linear filtering.
// Empty metadata nodes must never interpolate their dummy ground=0 into real
// terrain heights. Density still fades with the ordinary bilinear weights.
float4 regionalFogAt(float2 uv,out float coverage) {
    float2 grid=uv*64-.5,base=floor(grid),f=grid-base;
    float4 a=tex2Dlod(RegionalFog,float4((base+.5)/64,0,0));
    float4 b=tex2Dlod(RegionalFog,float4((base+float2(1.5,.5))/64,0,0));
    float4 c=tex2Dlod(RegionalFog,float4((base+float2(.5,1.5))/64,0,0));
    float4 d=tex2Dlod(RegionalFog,float4((base+1.5)/64,0,0));
    float4 weights=float4((1-f.x)*(1-f.y),f.x*(1-f.y),(1-f.x)*f.y,f.x*f.y);
    float4 valid=weights*float4(a.w>0,b.w>0,c.w>0,d.w>0);
    float total=dot(valid,1),inv=1/max(total,1e-6);
    // Fade the field border inside the sampler so callers keep fewer registers.
    float edge=saturate(min(min(grid.x,grid.y),min(63-grid.x,63-grid.y))*.5);
    coverage=total*edge*edge*(3-2*edge);
    return float4(dot(valid,float4(a.x,b.x,c.x,d.x))*inv,
        dot(weights,float4(a.y,b.y,c.y,d.y)),dot(weights,float4(a.z,b.z,c.z,d.z)),
        dot(valid,float4(a.w,b.w,c.w,d.w))*inv);
}
// Direct light from the nearest authored local sources (lanterns, braziers,
// fires) on nearby surfaces. The game only glows the lamp model itself; this
// lights the ground and walls around it. Unshadowed; the separate point pass
// still subtracts the baked estimate of the one selected shadowed lamp.
float4 LocalDirect(float2 uv:TEXCOORD0):COLOR0 {
    float3 smoothNormal=tex2Dlod(NormalBuffer,float4(uv,0,0)).xyz;
    // r47 K4: lamps read weaker in direct sun (30%). The first lighting pass (the sun, by day) left
    // its visibility in the baseline alpha (s12); LocalLightInfo.z is the share removed in full sun,
    // faded with the sun's elevation (0 at night, with shadows off or without an active sun).
    // Sun shadow and interiors keep 1.
    float sunlit=saturate((tex2Dlod(BaselineLighting,float4(uv,0,0)).a-(1-GridInfo.z))/max(GridInfo.z,.001));
    uv=depthUV(uv);
    float d=normalizedDepth(uv);if(d>=.99999||waterDistance(uv,d)>0)return 0;
    float3 p=worldPosition(uv,d);float3 n=smoothNormal;
    float3 result=0;
    // Unused slots are zero constants (reach 0), so no per-light branch is needed.
    // LocalLightColor.w is the precomputed 1/(end-start).
    [unroll]for(int i=0;i<8;++i){
        float3 toLight=LocalLightPos[i].xyz-p;
        float inv=rsqrt(max(dot(toLight,toLight),.0025));
        float reach=saturate((LocalLightPos[i].w-1/inv)*LocalLightColor[i].w);
        float lambert=saturate(dot(n,toLight)*inv);
        result+=LocalLightColor[i].rgb*(reach*reach*lambert);
    }
    return float4(result*(LocalLightInfo.y*(1-LocalLightInfo.z*sunlit)),0);
}
// 0.3.159 shadow removal smoothing (always on). In sun shadow the correction removes the game's
// painted sun light in proportion to N.L of the depth-derived FACET normal, but the
// game painted it with smooth vertex normals: the removal steps at every facet crease
// and at the wide/one-pixel normal switch, leaving jagged patches of the game's own
// highlight. Average the removal the composite will apply, as a fraction of the
// transported colour, over a 1.5-unit disc: the centre and 24 golden-angle taps on
// the same surface (depth) in the same shadow state (visibility). Lit pixels keep
// their value. The fraction includes the composite's albedo bound (legacy fog T):
// correction/baseline alone still steps where the facet normal predicts no painted
// light, because the albedo estimate saturates there.
// 0.3.185 direction-dependent halo: looking toward a low sun, the camera-facing face of an actor or
// cliff faces away from it (N.L<=0, removal ratio about 0) in the same shadow state as the ground, and
// the depth weight alone accepts it at the contact: its zero removal leaked into the adjacent ground
// (under-removal, a light halo). Each tap is also weighted by the agreement of the smoothed world
// normal (s14): full up to 35 degrees, none from 50. A wall is ~90 degrees from the ground at any
// distance, which no depth tolerance can tell. 0.3.186: was 25/40, which rejected taps across
// low-poly dune facets (grazing creases of 35-45 degrees whose N.L removal steps the smoothing hides,
// under a low sun or the moon) and brought back faint sharp triangles; creases up to 35 degrees now
// smooth exactly as without the test. A 45 degree crease and a 45 degree face look the same to any
// angle gate, so at 45 both are half-way (face halo 4.8/255 at 25 u, 8.5 untested; crease +4.3/255
// over untested); 55 and the vertical faces are rejected. Source-aware variants (reject only taps
// facing away from the source) changed nothing: the away-facing dune facet is that very case. A
// silhouette centre (confidence 0: its one-sided normal may be the neighbour's) skips the test (the old weights); silhouette taps of
// a confident centre are tested with their own normal. Own half-res pass (RemovalSmooth): the extra
// read does not fit TemporalLight's 512 slots; TemporalLight reads its result as SmoothedLighting (s9)
// and clamps against the raw s8.
// The composite applies correction*min(transported/baseline,T); as a fraction of the
// transported colour that is correction*T/removalScale.
// legacyT>=.001 keeps the first operand >=.00015, so the scene term needs no floor.
float3 removalScale(float3 baseline,float3 scene,float3 fog,float legacyT){
    return max(legacyT*max(baseline,.15),scene-min(fog,scene));
}
float3 smoothRemoval(float4 current,float2 q,float2 base,float2 size,float z){
    float amount=saturate((1-current.a*RemovalInfo.z)/GridInfo.z);
    float3 result=current.rgb;
    // Fractional source weights leave fp16 alpha a hair below 1/RemovalInfo.z in sunlight: skip
    // the taps below 1/256, which also keeps those lit pixels exactly unchanged.
    [branch]if(amount>1.0/256){
        float legacyT=max(mad(LegacyFog.w,saturate(pow(max(mad(z*Projection.z,LegacyFog.x,LegacyFog.y),0),LegacyFog.z))-1,1),.001);
        float3 fog=(1-legacyT)*LegacyFogColor.rgb;
        // 0.3.174: the Scene copy precedes the contact AO when the world composites; apply it here
        // (a neutral 1 otherwise) so the removal sees the colour the composite relights.
        float3 scale=removalScale(tex2Dlod(BaselineLighting,float4(q,0,0)).rgb,tex2Dlod(Scene,float4(q,0,0)).rgb*tex2Dlod(AmbientOcclusion,float4(q,0,0)).a,fog,legacyT);
        float3 ratio=current.rgb*legacyT/scale;
        float radius=clamp(RemovalInfo.w/z,3,16);
        float depthScale=-1.442695/max(.25,z*.03);
        float visibilityScale=2*RemovalInfo.z/GridInfo.z;
        float4 centreNormal=tex2Dlod(NormalBuffer,float4(q,0,0));
        float3 sum=max(ratio,-.45);float total=1,t=.5;float2 dir=float2(1,0);
        [loop]for(int i=0;i<24;++i){
            float2 tq=(clamp(floor(base+.5+dir*(sqrt(t*(1.0/24))*radius)),0,size-1)+.5)/size;
            float4 light=tex2Dlod(LightingBuffer,float4(tq,0,0));
            float4 tapNormal=tex2Dlod(NormalBuffer,float4(tq,0,0));
            float agree=saturate((dot(centreNormal.xyz,tapNormal.xyz)-.6428)*5.67);
            float w=exp2(abs(viewDistance(normalizedDepth(depthUV(tq)))-z)*depthScale)*saturate(1-abs(light.a-current.a)*visibilityScale)*(centreNormal.w>.5?agree:1);
            sum+=max(light.rgb*legacyT/removalScale(tex2Dlod(BaselineLighting,float4(tq,0,0)).rgb,tex2Dlod(Scene,float4(tq,0,0)).rgb*tex2Dlod(AmbientOcclusion,float4(tq,0,0)).a,fog,legacyT),-.45)*w;total+=w;
            dir=float2(dir.x*-.7373688-dir.y*.6754903,dir.x*.6754903-dir.y*.7373688);t+=1;
        }
        result=lerp(ratio,sum/total,amount)*scale/legacyT;
    }
    return result;
}
// Half-res pass before TemporalLight: the lighting buffer with the removal smoothing applied (own
// pass for the slot budget). Lit, sky and no-source pixels are a bit-identical copy.
float4 RemovalSmooth(float2 uv:TEXCOORD0):COLOR0 {
    float2 half=ScreenSize.zw;
    float2 base=floor(uv*half);float2 q=(base+.5)/half;
    float4 current=tex2Dlod(LightingBuffer,float4(q,0,0));
    float d=normalizedDepth(depthUV(q));
    [branch]if(RemovalInfo.y>0&&d<.99999)current.rgb=smoothRemoval(current,q,base,half,viewDistance(d));
    return current;
}
// Temporal stabilization of the half-resolution lighting buffer (shadow
// visibility, GI and local light corrections). The previous frame's result is
// reprojected through the previous view, accepted only where its stored view
// distance agrees with this frame's surface, and clamped to the current 3x3
// cross neighbourhood so moving casters leave no trail. Removes per-frame
// flicker from depth-derived normals, caster set changes and MSAA edges.
struct TemporalOutput {float4 light:COLOR0;float4 depth:COLOR1;};
TemporalOutput TemporalLight(float2 uv:TEXCOORD0) {
    TemporalOutput o;float2 half=ScreenSize.zw;
    float2 base=floor(uv*half);float2 q=(base+.5)/half;
    float4 current=tex2Dlod(SmoothedLighting,float4(q,0,0));
    float2 duv=depthUV(q);float d=normalizedDepth(duv);float z=viewDistance(d);
    o.light=current;o.depth=float4(z,0,0,1);
    if(TemporalInfo.x<=0||d>=.99999)return o;
    float3 p=worldPosition(duv,d);
    float3 v=affinePoint(p,PreviousView);float w=v.z*Projection.z;
    if(w<=ImageClip.z)return o;
    float2 puv=(v.xy*Projection.xy/w)*float2(.5,-.5)+.5;
    if(any(puv<0)||any(puv>1))return o;
    float2 pq=(clamp(floor(puv*half),0,half-1)+.5)/half;
    float hz=tex2Dlod(DepthHistory,float4(pq,0,0)).r;
    if(abs(hz-w)>max(.25,w*.03))return o;
    // 0.3.171: bilinear history (s14 LINEAR in this pass). The nearest history texel drifted
    // up to .75 half-res px while moving (sign flipping with the flow's fraction: waves on
    // static edges). A per-tap weighted depth test does not fit the slot budget (515): read
    // bilinearly only where all four footprint depths agree with this surface; elsewhere
    // (silhouettes) read the nearest texel's centre, which LINEAR returns unmixed, as before.
    float tol=max(.25,w*.03);
    float2 texel=1/half,corner=(floor(puv*half-.5)+.5)*texel; // CLAMP addressing repeats the border
    float4 footprint=float4(tex2Dlod(DepthHistory,float4(corner,0,0)).r,
        tex2Dlod(DepthHistory,float4(corner+float2(texel.x,0),0,0)).r,
        tex2Dlod(DepthHistory,float4(corner+float2(0,texel.y),0,0)).r,
        tex2Dlod(DepthHistory,float4(corner+texel,0,0)).r);
    bool agree=all(abs(footprint-w)<=tol);
    float4 history=tex2Dlod(LightHistory,float4(agree?puv:pq,0,0));
    float4 lo=current,hi=current;
    [unroll]for(int i=0;i<4;++i){
        float2 offset=float2(i==0?-1:(i==1?1:0),i==2?-1:(i==3?1:0));
        float4 s=tex2Dlod(LightingBuffer,float4((clamp(base+offset,0,half-1)+.5)/half,0,0));
        lo=min(lo,s);hi=max(hi,s);
    }
    // 0.3.170: shadow texel flips of an animated caster (idle pose) change visibility
    // by up to 15% (near 5x5 tent) every frame, in step with the whole neighbourhood,
    // so the tight clamp kept none of the history and each flip popped. With a still
    // camera (reprojection within .25 half-res pixel, none from .5; 0.3.171) let the visibility
    // (alpha) history stray up to .15 of full visibility outside the neighbourhood, and
    // the rgb by the direct light that change can carry (TemporalReach.w: the largest
    // drawn source channel per unit of normalised visibility). History visibility
    // inside the neighbourhood: exactly the tight clamp.
    float4 tight=clamp(history,lo,hi);
    float still=saturate(2-4*length(puv*half-(base+.5)));
    float margin=.15/max(RemovalInfo.z,1e-4)*RemovalInfo.y*still;
    float loose=clamp(history.a,lo.a-margin,hi.a+margin);
    float3 reach=abs(loose-tight.a)*RemovalInfo.z*TemporalReach.w;
    o.light=lerp(current,float4(clamp(history.rgb,lo.rgb-reach,hi.rgb+reach),loose),TemporalInfo.x);
    return o;
}
// 1x1 pass: fraction of the source disc (on screen) that is clear sky, from
// 16 depth taps in a 4x4 grid over the disc, or .6 x the clear fraction of a ring
// around it when larger (r43 wrap), blended with last frame's value.
// Off-screen or behind-camera sources count as fully visible (unknown).
float4 SourceVisibilityPS(float2 uv:TEXCOORD0):COLOR0 {
    float previous=saturate(tex2Dlod(PreviousSourceVisibility,float4(.5,.5,0,0)).r);
    float3 sv=float3(dot(SunDirection.xyz,InverseView[0].xyz),dot(SunDirection.xyz,InverseView[1].xyz),dot(SunDirection.xyz,InverseView[2].xyz));
    float sw=sv.z*Projection.z;
    float current=1;
    if(sw>.05){
        float2 suv=(sv.xy*Projection.xy/sw)*float2(.5,-.5)+.5;
        if(all(suv>0)&&all(suv<1)){
            // Disc radius on screen in uv units: tangent radius through the projection.
            float2 r=SourceInfo.x*Projection.xy*.5;
            float clear=0;
            [unroll]for(int i=0;i<16;++i){
                float2 offset=float2((i%4)-1.5,(i/4)-1.5)*(2.0/3.0)*r;
                float2 q=saturate(suv+offset);
                clear+=(tex2Dlod(Depth,float4(q,0,0)).r-Projection.w)*DepthParams.x>=.99999?1:0;
            }
            current=clear*(1.0/16);
            // r43 wrap: 8 taps on a ring at 2.5/3.5 disc radii keep part of the aureole
            // when only the disc is covered (a trunk in front of the sun). Off-screen ring
            // taps do not count; with none on screen the disc value stands.
            float ringClear=0,ringTaps=0;
            [unroll]for(int j=0;j<8;++j){
                float2 q=suv+float2(cos(j*.78539816),sin(j*.78539816))*((j%2)?3.5:2.5)*r;
                float inside=step(abs(q.x-.5),.5)*step(abs(q.y-.5),.5);
                ringTaps+=inside;
                ringClear+=inside*step(.99999,(tex2Dlod(Depth,float4(q,0,0)).r-Projection.w)*DepthParams.x);
            }
            current=max(current,.6*ringClear/max(ringTaps,1));
        }
    }
    return lerp(current,previous,SourceInfo.y).xxxx;
}
float4 WorldFog(float2 uv:TEXCOORD0):COLOR0 {
    if(FogInfo.x<.5)return float4(0,0,0,1);
    uv=depthUV(uv);
    float depth=normalizedDepth(uv);float3 view=viewPositionDistance(uv,receiverDistance(uv,min(depth,.99999)));
    // Rotate a direction directly: adding then subtracting a large camera
    // translation loses precision as the player moves through world space.
    float3 ray=view.x*InverseView[0].xyz+view.y*InverseView[1].xyz+view.z*InverseView[2].xyz;
    float surface=min(length(ray),FogInfo.w); // host supplies the positive 128-unit range
    ray*=rsqrt(max(dot(ray,ray),1e-12));
    float transmittance=1;float3 directScatter=0;
    float cosine=dot(ray,SunDirection.xyz); // both directions are normalized
    // Normalized HG(g=.85) + isotropic mixture: most energy is broad enough
    // for side/back views of geometry-cut shafts; a small forward aureole remains.
    float narrow=.2775*pow(max(1.7225-1.7*cosine,.01),-1.5);
    // Disc visibility only gates the narrow aureole. A branch between the
    // camera and moon must not dim every lit air column in the world.
    // Each volume sample's shadow ray decides whether that air receives light.
    float visible=saturate(tex2Dlod(SourceVisibility,float4(.5,.5,0,0)).r);
    // 0.3.145: the broad part leans gently toward the source (x1.15 facing it,
    // x1 at 90 degrees, x.85 facing away); its sphere integral is unchanged.
    float phasePi=mad(.03*visible,narrow,mad(.033,cosine,.22));
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
    [loop]for(int i=0;i<49;++i){
        float start=max(0,(i-offset)*stepLength);
        if(start>=surface)break;
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
            if(dot(sunSource,1)>0)directScatter+=transmittance*absorb*sunSource*(fogShadow(p)*heightFade);
            transmittance*=1-absorb;
        }
    }
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
// Glow of the nearest authored local lights inside the fog (lanterns, fires):
// in-scattering of an isotropic point source along the view ray in closed form,
// integral of a softened inverse-square source, tapered to zero at its range,
// with the fog extinction sampled at the light. Unshadowed; added to the
// scattering buffer before its blur, soft-capped like the celestial term.
float atanFast(float x){
    // Max error ~0.005 rad, no transcendental instructions.
    float a=abs(x);float r=a<=1?a*(.785398+.273*(1-a)):1.570796-(1/a)*(.785398+.273*(1-1/a));
    return x<0?-r:r;
}
float4 LocalFog(float2 uv:TEXCOORD0):COLOR0 {
    uv=depthUV(uv);
    float depth=normalizedDepth(uv);float3 endpoint=affinePoint(viewPositionDistance(uv,receiverDistance(uv,min(depth,.99999))),InverseView);
    float3 ray=endpoint-Camera.xyz;float D=min(length(ray),max(FogInfo.w,0));
    ray*=rsqrt(max(dot(ray,ray),1e-12));
    float3 result=0;
    // Four lights per pass (SM3 slot budget); up to sixteen across batches.
    [unroll]for(int i=0;i<4;++i){
        float3 oc=Camera.xyz-LocalLightPos[i].xyz;
        float b=dot(oc,ray);float h2=max(dot(oc,oc)-b*b,0);
        float disc=LocalLightPos[i].w*LocalLightPos[i].w-h2;
        float root=sqrt(max(disc,0));
        float t0=max(-b-root,FogRange.x),t1=min(-b+root,D);
        // Finite source core removes the sharp pin of light. Subtracting the
        // boundary value makes volume density vanish continuously at the
        // sphere edge, instead of producing a bright, hard-edged ball.
        float core=max(.75,LocalLightPos[i].w*.06);
        float invH=rsqrt(h2+core*core);
        float boundary=rcp(max(LocalLightPos[i].w*LocalLightPos[i].w+core*core,.001));
        float integral=(disc>0&&t1>t0)?max((atanFast((t1+b)*invH)-atanFast((t0+b)*invH))*invH-(t1-t0)*boundary,0):0;
        // 0.3.199 (fog clouds): the same smooth near ramp as WorldFog's air (0 at FogRange.x, full one fade length later), taken at the
        // ray's closest approach to the light inside the integrated span. The glow's bright core sits there; with only the hard FogRange.x
        // start it was cut off within a unit or two of walking, so the glow popped while approaching a lamp (game test).
        float nearGlow=saturate((clamp(-b,t0,t1)-FogRange.x)*FogRange.y);nearGlow*=nearGlow*(3-2*nearGlow);
        result+=LocalLightColor[i].rgb*(integral*LocalLightFog[i].x*nearGlow);
    }
    float3 scatter=result*FogRange.z;
    float peak=max(scatter.r,max(scatter.g,scatter.b));
    scatter*=FogRange.w/(FogRange.w+peak);
    return float4(scatter,0);
}
float4 upsampleFog(float2 uv,float centerDepth) {
    // Sample actual half-resolution texel centers. This also handles odd full
    // viewport dimensions such as 1728x1117 -> 864x558 without a half-row drift.
    float2 dimensions=ScreenSize.zw;
    float2 grid=uv*dimensions-.5,base=floor(grid),fraction=grid-base;
    float center=receiverDistance(depthUV(uv),centerDepth),total=0,closest=1e20;
    float4 sum=0,fallback=float4(0,0,0,1);
    [loop]for(int i=0;i<4;++i){
        float row=floor(i*.5);
        float2 bit=float2(i-row*2,row);
        float2 q=(clamp(base+bit,0,dimensions-1)+.5)/dimensions;
        float delta=abs(receiverDistance(depthUV(q),normalizedDepth(depthUV(q)))-center);
        float4 sample=tex2Dlod(FogBuffer,float4(q,0,0));
        float2 blend=lerp(1-fraction,fraction,bit);
        float weight=blend.x*blend.y*exp(-delta/max(.2,center*.02));
        sum+=sample*weight;total+=weight;
        if(delta<closest){closest=delta;fallback=sample;}
    }
    return total>.00001?sum*(1/total):fallback;
}
// Depth-aware separable blur of the half-resolution scattering buffer. Canopy
// gaps in the 0.09-unit shadow texels produce shafts one texel wide, which read
// as rain; two 5-tap passes with a 2-texel stride turn them into soft beams.
// Receiver depth keeps a near trunk's short path from bleeding into the sky.
float4 FogBlur(float2 uv:TEXCOORD0):COLOR0 {
    float2 dimensions=ScreenSize.zw;
    float2 base=floor(uv*dimensions);
    float2 centerUV=(base+.5)/dimensions;
    float center=receiverDistance(depthUV(centerUV),normalizedDepth(depthUV(centerUV)));
    float4 sum=0;float total=0;
    [loop]for(int i=-2;i<=2;++i){
        float2 q=(clamp(base+BlurInfo.xy*i,0,dimensions-1)+.5)/dimensions;
        float z=receiverDistance(depthUV(q),normalizedDepth(depthUV(q)));
        float w=(i==0?6:(abs(i)==1?4:1))*exp(-abs(z-center)/max(.5,center*.06));
        sum+=tex2Dlod(FogBuffer,float4(q,0,0))*w;total+=w;
    }
    return sum/max(total,1e-5);
}
// 0.3.199 (fog clouds): moving low fog banks. A second half-resolution raymarch of the same world-fixed intervals as WorldFog (40, no jitter:
// fog has no temporal history) through two scales of tileable 3D noise scrolled by the wind (origins in c60/c61). Returns the cloud
// scattering (rgb) and the cloud transmittance (a); drawn with ONE, SRCALPHA so it adds to the fog buffer and attenuates what is there.
// Exactly (0,0,0,1) where no cloud is met. The base fog only attenuates the cloud light (Tbase, never written to alpha); its density is a cut-down
// copy of WorldFog's (the full lines do not fit the 512-slot budget next to the field fetch and the shadow filter), see the comments below.
float4 FogClouds(float2 uv:TEXCOORD0):COLOR0 {
    // (no FogInfo.x test: the host draws this pass only when c21.x >= .5)
    uv=depthUV(uv);
    float depth=normalizedDepth(uv);float3 view=viewPositionDistance(uv,receiverDistance(uv,min(depth,.99999)));
    float3 ray=view.x*InverseView[0].xyz+view.y*InverseView[1].xyz+view.z*InverseView[2].xyz;
    float surface=min(length(ray),FogInfo.w);
    ray*=rsqrt(max(dot(ray,ray),1e-12));
    float tauBase=0,tCloud=1,directWeight=0,ambientWeight=0; // scalar sums: the sun and sky colours multiply once after the loop; the base fog as optical depth (exp only inside a cloud)
    float3 axis=abs(ray);
    float2 major=axis.x>=axis.y?float2(ray.x,Camera.x):float2(ray.y,Camera.y);
    major=abs(major.x)>=axis.z?major:float2(ray.z,Camera.z);
    float spacing=mad(FogInfo.w,1.0/40,CloudInfo[0].x); // 0.3.200 (gpu budget): c60.x 0 = the 40 intervals; reduced levels fewer (host: 128*(1/N-1/40))
    float stepLength=spacing/abs(major.x);
    float offset=frac(major.y*(major.x<0?-1:1)/spacing);
    [loop]for(int i=0;i<41;++i){
        float start=max(0,(i-offset)*stepLength);
        if(start>=surface)break;
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
            directWeight+=exp(-tauBase)*weight*fogShadow(p)*heightFade; // the sun light is dimmed by the base fog in front of the cloud
            ambientWeight+=weight;
            tCloud*=1-absorb;
        }
        tauBase+=baseSigma*stepSize;
    }
    // The narrow forward aureole of WorldFog is dropped here (register/slot budget): the broad phase alone.
    float3 directScatter=max(DirectLight.rgb,0)*mad(.033,dot(ray,SunDirection.xyz),.22)*(max(FogInfo.y,0)*directWeight); // FogColor.rgb (the albedo) is always 1 (host): left out for the slots
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
// 0.3.199 (fog temporal): temporal accumulation of the half-resolution fog (raw FogBuffer, s9) with last frame's resolved fog (FogHistory, s14
// LINEAR), reprojected through the previous view (c53..c56, still the previous frame's here) and accepted only where the previous frame's
// stored view distance (DepthHistory, s15) agrees; sky pixels reproject their view direction only. History is clamped to the current 3x3
// neighbourhood (all four channels) so a moving lamp or shaft leaves no trail, then lerped with the current by c64.y (0 = pass through).
sampler2D FogHistory : register(s14);
float4 FogTemporalInfo : register(c64); // y history weight (c64.yzw are read by no other shader: LocalFog reads only .x of c59..c66)
float4 FogTemporal(float2 uv:TEXCOORD0):COLOR0 {
    float2 half=ScreenSize.zw;
    float2 base=floor(uv*half);float2 q=(base+.5)/half;
    float4 current=tex2Dlod(FogBuffer,float4(q,0,0));
    if(FogTemporalInfo.y<=0)return current;
    float2 duv=depthUV(q);float d=normalizedDepth(duv);bool sky=d>=.99999;
    float3 v;
    if(sky){
        float3 r=viewPositionDistance(duv,1);
        float3 world=r.x*InverseView[0].xyz+r.y*InverseView[1].xyz+r.z*InverseView[2].xyz;
        v=world.x*PreviousView[0].xyz+world.y*PreviousView[1].xyz+world.z*PreviousView[2].xyz;
    }else v=affinePoint(worldPosition(duv,d),PreviousView);
    float w=v.z*Projection.z;
    if(w<=ImageClip.z)return current;
    float2 puv=(v.xy*Projection.xy/w)*float2(.5,-.5)+.5;
    if(any(puv<0)||any(puv>1))return current;
    float2 pq=(clamp(floor(puv*half),0,half-1)+.5)/half;
    float2 texel=1/half,corner=(floor(puv*half-.5)+.5)*texel;
    float tol=max(.25,w*.03);bool agree=true;
    if(!sky){
        if(abs(tex2Dlod(DepthHistory,float4(pq,0,0)).r-w)>tol)return current;
        float4 footprint=float4(tex2Dlod(DepthHistory,float4(corner,0,0)).r,
            tex2Dlod(DepthHistory,float4(corner+float2(texel.x,0),0,0)).r,
            tex2Dlod(DepthHistory,float4(corner+float2(0,texel.y),0,0)).r,
            tex2Dlod(DepthHistory,float4(corner+texel,0,0)).r);
        agree=all(abs(footprint-w)<=tol);
    }
    float4 history=tex2Dlod(FogHistory,float4(agree?puv:pq,0,0));
    float4 lo=current,hi=current;
    [unroll]for(int i=0;i<8;++i){
        float2 offset=float2(i%3-1,i/3-1);
        offset=i>=4?float2((i+1)%3-1,(i+1)/3-1):offset;
        float4 s=tex2Dlod(FogBuffer,float4((clamp(base+offset,0,half-1)+.5)/half,0,0));
        lo=min(lo,s);hi=max(hi,s);
    }
    return lerp(current,clamp(history,lo,hi),FogTemporalInfo.y);
}
// Distant haze toward the WORLD horizon: in front of the scene, behind local
// scattering. Terrain weight is 0 at or nearer than the start view Z (the
// scene colour is returned bit for bit) and 1 at the game's fog end; sky/WDL
// pixels have weight 1. One optical depth for both, so a far ridge meets the
// sky without a step. Band by world elevation: camera pitch/roll never moves
// it. Full below the horizon, where only the distance weight protects ground.
// No samples, passes or raymarch steps.
float3 horizonHaze(float3 color,float2 uv,float viewZ,bool sky) {
    float range=sky?1:saturate((viewZ-HorizonShape.y)*HorizonShape.z);
    [branch]if(range<=0||HorizonHaze.w<=0)return color;
    float3 ray=viewPositionDistance(uv,1);
    float3 world=ray.x*InverseView[0].xyz+ray.y*InverseView[1].xyz+ray.z*InverseView[2].xyz;
    float elevation=world.z*rsqrt(dot(ray,ray));
    float band=exp2(-max(elevation,0)*HorizonShape.w);
    float amount=range*range*(3-2*range)*(1-exp2(-HorizonHaze.w*band));
    // Distant forward scattering toward a low sun: Henyey-Greenstein g=.6 in
    // azimuth, amplitude sunWeight*cos(sun elevation) from the host, capped.
    // Lift of the fog colour itself, tinted by the glow hue (c35.yzw): black night fog cannot glow.
    float lobe=length(HorizonSun.zw);
    float azimuth=dot(world.xy,HorizonSun.zw)*rsqrt(max(dot(world.xy,world.xy),1e-12))/max(lobe,1e-6);
    float lift=min(lobe*.64*pow(1.36-1.2*azimuth,-1.5),.25);
    float3 haze=HorizonHaze.rgb*mad(lift,ShadowRange.yzw,1);
    return lerp(color,haze,amount);
}
float4 WorldComposite(float2 uv:TEXCOORD0):COLOR0 {
    float4 original=tex2Dlod(Scene,float4(uv,0,0));
    float2 centerUV=depthUV(uv);
    float d=normalizedDepth(centerUV);
    float liquid=waterDistance(centerUV,d);float viewZ=liquid>0?liquid:viewDistance(d);
    // Original Terrain VS uses SIGNED view Z, including right-handed cameras.
    // Unknown fog uploads the safe identity (0,1,1,0), giving T=1 exactly.
    float legacyT=mad(LegacyFog.w,saturate(pow(max(mad(viewZ*Projection.z,LegacyFog.x,LegacyFog.y),0),LegacyFog.z))-1,1);
    bool relight=d<.99999&&liquid<=0;
        float3 baseline=0;
        float3 bounce=0;float shadow=0,total=0,closest=1e20;
        float4 fallbackLight=0;float3 fallbackBase=0;
        float ao=0,fallbackAO=1; // 0.3.174: contact AO on the lighting grid (s10)
        // Fog uses the same four half-resolution texel centres, but DIFFERENT
        // depth weights and fallback. Share only coordinates/raw depth reads;
        // retain receiverDistance (including water) for each fog sample.
        float4 fogSum=0,fogFallback=float4(0,0,0,1);
        float fogTotal=0,fogClosest=1e20;
        // Bilinear tent over the four surrounding half-resolution texels with
        // depth agreement, like the fog upsample: the half-resolution lattice
        // no longer shows as blocks that crawl while the camera moves.
        float2 dimensions=ScreenSize.zw;
        float2 grid=uv*dimensions-.5,base=floor(grid),fraction=grid-base;
        float center=viewDistance(d);
        [loop]for(int i=0;i<4;++i){
            float row=floor(i*.5);
            float2 bit=float2(i-row*2,row);
            float2 q=(clamp(base+bit,0,dimensions-1)+.5)/dimensions;
            float sampleDepth=normalizedDepth(depthUV(q));
            float2 blend=lerp(1-fraction,fraction,bit);
            if(relight){
            float delta=abs(viewDistance(sampleDepth)-center);
            float weight=blend.x*blend.y*exp(-delta/max(.15,center*.01));
            float4 light=tex2Dlod(LightingBuffer,float4(q,0,0));
            float3 lit=tex2Dlod(BaselineLighting,float4(q,0,0)).rgb;
            float occlusion=tex2Dlod(AmbientOcclusion,float4(q,0,0)).a;
            bounce+=light.rgb*weight;shadow+=light.a*weight;baseline+=lit*weight;ao+=occlusion*weight;total+=weight;
            if(delta<closest){closest=delta;fallbackLight=light;fallbackBase=lit;fallbackAO=occlusion;}
            }
            float fogDelta=abs(receiverDistance(depthUV(q),sampleDepth)-viewZ);
            float4 fogSample=tex2Dlod(FogBuffer,float4(q,0,0));
            float fogWeight=blend.x*blend.y*exp(-fogDelta/max(.2,viewZ*.02));
            fogSum+=fogSample*fogWeight;fogTotal+=fogWeight;
            if(fogDelta<fogClosest){fogClosest=fogDelta;fogFallback=fogSample;}
        }
    float4 fog=fogTotal>.00001?fogSum*(1/fogTotal):fogFallback;
    // 0.3.174: the contact AO and bloom composite, folded in (it ran as a separate full-res
    // pass before). Same result as that pass's tail: lit=original*ao, plus bloom where not
    // saturated. AO follows this pass's depth-weighted tent (1 on sky and water); bloom is the
    // hardware-bilinear half-res value, like the old pass's unfiltered glow.
    ao=relight?(total<.02?fallbackAO:ao/total):1;
    float3 bloom=tex2Dlod(AmbientOcclusion,float4(uv,0,0)).rgb;
    original.rgb=saturate(mad(bloom,1-saturate(original.rgb*ao),original.rgb*ao));
    float3 color=original.rgb;
    // Terrain fog is only an estimate on interior/blended materials. It cannot
    // account for more light than the observed color in any channel.
    float3 fogPart=min((1-legacyT)*LegacyFogColor.rgb,original.rgb);
    if(relight){
        // Thin receivers whose depth matches none of the four texels keep the
        // nearest-depth texel instead of losing their shadow and GI entirely.
        if(total<.02){bounce=fallbackLight.rgb;shadow=fallbackLight.a;baseline=fallbackBase;total=1;}
        float inverseWeight=1/max(total,.0001);
        bounce*=inverseWeight;shadow*=inverseWeight;

        // The framebuffer already contains atmospheric fog. Relight only the
        // transported surface term: do not infer material albedo from fog RGB.
        float3 oldLight=max(baseline*inverseWeight,.15);
        float3 transported=max(original.rgb-fogPart,0);
        float3 albedoT=min(transported/oldLight,legacyT);
        color=mad(albedoT,bounce,original.rgb);
        // Bound combined GI/shadow darkening relative to the existing surface;
        // this preserves black and does not introduce an absolute exposure floor.
        color=max(color,mad(-.45,transported,original.rgb));
        if(PassInfo.z==1)color=shadow.xxx;
        if(PassInfo.z==2)color=max(AmbientLight.rgb+bounce,0);
    }
    // The captured scene already contains legacy distance fog. Local integrated
    // radiance lies IN FRONT of that background, including sky/fogged terrain.
    // Endpoint legacyT must not extinguish scattering produced near the camera.
    // Retain fogPart only for safe surface relighting above; never de-fog/divide.
    // Horizon haze extinguishes the far scene (including the depth-occluded
    // sun/moon disc) BEFORE the local scattering is added in front of it.
    if(PassInfo.z<.5){
        float3 unfogged=color;
        // Rain streaks were drawn into the scene before the composite: on mask pixels go back toward the unfogged pixel so they are not hazed.
        float rain=tex2Dlod(RainMask,float4(uv,0,0)).a;
        color=lerp(mad(horizonHaze(color,centerUV,viewZ,d>=.99999&&liquid<=0),fog.a,fog.rgb),unfogged,rain);
    }
    if(PassInfo.z==3)color=fog.rgb;
    return float4(max(color,0),original.a);
}

// Separate geometry pass: c0..3 contains the light's world->clip matrix.
row_major float4x4 LightMatrix:register(c0);
struct ShadowVertex {float4 position:POSITION0;float2 uv:TEXCOORD0;float depth:TEXCOORD1;};
ShadowVertex ShadowVS(float3 p:POSITION0,float3 n:NORMAL0,float2 uv:TEXCOORD0){
    ShadowVertex o;o.position=mul(float4(p,1),LightMatrix);o.uv=uv;o.depth=o.position.z;return o;
}
// Persistent directional cache only; paired with StaticCasterPS, which writes
// exact clamped fragment depth and rejects raw depths beyond the far plane.
ShadowVertex ShadowCacheVS(float3 p:POSITION0,float3 n:NORMAL0,float2 uv:TEXCOORD0){
    ShadowVertex o=ShadowVS(p,n,uv);o.position.z=saturate(o.position.z);return o;
}
float4 Material:register(c0); // rgb unused in depth pass, w alpha cutoff
float4 ShadowPS(float2 uv:TEXCOORD0,float depth:TEXCOORD1):COLOR0 {
    clip(tex2D(Scene,uv).a-Material.w);return depth.xxxx;
}
float4 ShadowReplayPS(float2 uv:TEXCOORD0,float4 lightClip:TEXCOORD7):COLOR0 {
    clip(tex2D(Scene,uv).a-Material.w);return lightClip.zzzz;
}
// Union of a per-frame dynamic depth map (s0, working size) with a cached
// static depth map (s1, cachedSize) at an integer texel offset and an exact
// depth delta. A valid upstream caster remains an occluder when its shifted
// depth is below zero. Empty texels and geometry beyond the far plane stay 1.
float4 UnionOffset:register(c0); // cached texel offset x,y; depth delta; cached size in texels
float4 UnionSize:register(c1); // inverse working size, inverse cached size
float4 ShadowUnion(float2 uv:TEXCOORD0):COLOR0 {
    float2 texel=floor(uv/UnionSize.x);
    float own=tex2Dlod(Scene,float4((texel+.5)*UnionSize.x,0,0)).r;
    float2 cachedTexel=texel+UnionOffset.xy;
    float cached=1;
    if(all(cachedTexel>=0)&&all(cachedTexel<UnionOffset.w)){
        float c=tex2Dlod(Depth,float4((cachedTexel+.5)*UnionSize.y,0,0)).r;
        if(c<1){c+=UnionOffset.z;cached=c<=1?max(c,0):1;}
    }
    return min(own,cached).xxxx;
}
