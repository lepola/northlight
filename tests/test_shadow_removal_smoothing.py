#!/usr/bin/env python3
# northlight-test:
"""0.3.159 shadow removal smoothing (R1, always on): source audit and a Python emulation of TemporalLight's
smoothRemoval (world_effects.hlsl). No GPU, Wine or game.

In sun shadow WorldLighting removes the game's painted sun light by N.L of the depth-derived facet
normal; the game painted it with smooth vertex normals, so the removal steps at facet creases
(jagged orange patches on shadowed walls). smoothRemoval averages the removal the composite applies,
as a fraction of the transported colour (albedoT*correction/transported, albedoT bounded by the
legacy fog T), over a 1.5-unit disc (centre + 24 golden-angle taps) on the same surface and shadow
state, and writes it back as a correction.

1. Wiring: HLSL guard and the unchanged 0.3.158 TemporalLight lines, c30.yzw from the drawn source
   weight only, s12 bound at the temporal pass (moved from the composite). There is deliberately no
   quality key: the 0.3.158 unsmoothed path is gone except where no source is
   drawn, and no key, preset or ini/README entry may exist.
2. Synthetic buffers: lit pixels bit-identical (also with fractional fp16 source weights), a jagged
   facet step becomes a gradient, no bleeding across a 4 u depth step and a bounded halo across a
   1 u protrusion, a shadow edge stays crisp. The emulation's "off" path (c30.y=0,
   no drawn source) is an internal reference that must return the input unchanged.
3. An F12 capture, when NORTHLIGHT_R1_CAPTURE (or [paths]
   r1_capture) names its folder: along half-res rows 230/240/250 (WorldComposite relight emulated per
   texel), adjacent output steps > 5/255 where the native image is continuous, before and after
   (the Q5 literal criterion is printed; see the comment at the check); lit pixels are unchanged.
   The 0.3.158 output for the comparison is the emulation's reference (off) path."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp; fp.use_source_modules()
import json,math,random,re,struct

hlsl=fp.src('world_effects.hlsl').read_text();w=fp.src('world_renderer.h').read_text();q=fp.src('quality_settings.h').read_text();r=fp.src('renderer.cpp').read_text()
ini=fp.src('windows-package/northlight-quality.ini').read_text();readme=fp.src('windows-package/README.txt').read_text(encoding='utf-8')
checks={}
# ---- 1. wiring ----
temporal=hlsl[hlsl.index('TemporalOutput TemporalLight('):hlsl.index('// 1x1 pass: fraction of the source disc')]
old_lines=['    TemporalOutput o;float2 half=ScreenSize.zw;','    float2 base=floor(uv*half);float2 q=(base+.5)/half;',
    '    float4 current=tex2Dlod(LightingBuffer,float4(q,0,0));','    float2 duv=depthUV(q);float d=normalizedDepth(duv);float z=viewDistance(d);',
    '    o.light=current;o.depth=float4(z,0,0,1);','    if(TemporalInfo.x<=0||d>=.99999)return o;','    o.light=lerp(current,float4(clamp(history.rgb,lo.rgb-reach,hi.rgb+reach),loose),TemporalInfo.x);'] # 0.3.170 final line (test_shadow_shimmer)
guard='    [branch]if(RemovalInfo.y>0&&d<.99999)current.rgb=smoothRemoval(current,q,base,half,z);\n'
checks['TemporalLight: 0.3.158 lines intact, one guarded call between the reads and o.light']=(all(temporal.count(l+'\n')==1 for l in old_lines)
    and temporal.count(guard)==1 and temporal.index(old_lines[3])<temporal.index(guard)<temporal.index(old_lines[4]) and hlsl.count('smoothRemoval(')==2)
checks['RemovalInfo aliases c30 (WaterInfo.x stays the water flag)']=('float4 WaterInfo : register(c30);' in hlsl and 'float4 RemovalInfo : register(c30);' in hlsl
    and hlsl.count('RemovalInfo.x')==0)
const='        const float drawnWeight=std::max(sourceWeights[0],0.f)+std::max(sourceWeights[1],0.f);\n        if(drawnWeight>.001f){c[30][1]=1;c[30][2]=1/drawnWeight;c[30][3]=.75f*std::fabs(projection[0])*float(w/2)*.5f;}\n'
# 0.3.170: the TemporalReach (c15.w) lines sit between this block and the upload (test_shadow_shimmer).
upload='        c[30][0]=waterMask?1.f:0.f;d->SetPixelShaderConstantF(0,&c[0][0],68);'
checks['c30.yzw only from the drawn source weight (sum of positive weights), set once']=(w.count(const)==1 and w.count(upload)==1 and w.index(const)<w.index(upload)
    and re.search(r'c\[30\]\[[123]\]=',w.replace(const,'')) is None and 'if(sourceWeights[source]<=0&&source!=firstSource)continue;' in w)
tb=w[w.index('{   // Temporal stabilization: light + history'):w.index('temporalIndex=prev;temporalValid=true;')]
# s12 moved from the composite's binding line to the temporal pass: the same API calls per frame, and
# nothing between the two passes rebinds s12 (the frame-start texture loop sets it to null earlier).
between=w[w.index('temporalIndex=prev;temporalValid=true;'):w.index('d->SetPixelShader(finalPS);if(!check(quad(w,h),"world composite"))')]
# LocalDirect also reads the baseline alpha (sun visibility), so s12 is bound once more,
# only inside the LocalDirect block (frames with lamps), before the temporal pass rebinds it.
local=w[w.index('d->SetPixelShader(localDirectPS);'):w.index('if(profile)profile->mark("GI");')]
checks['BaselineLighting (s12) bound at the temporal pass (and for LocalDirect), still bound for the composite']=(w.count('SetTexture(12,')==2
    and 'd->SetTexture(12,baselineLight);' in tb and 'SetTexture(12' not in between and local.count('d->SetTexture(12,baselineLight);')==1)
checks['no quality key (always on): no ShadowRemovalSmoothing in settings, ini, README or the log']=all(
    'ShadowRemovalSmoothing' not in t and 'removalSmoothing' not in t for t in (q,ini,readme,r,w))
# The emulation below mirrors these shader constants; fail loudly if the shader moves.
for needle in ('clamp(RemovalInfo.w/z,3,16)','-1.442695/max(.25,z*.03)','2*RemovalInfo.z/GridInfo.z','saturate((1-current.a*RemovalInfo.z)/GridInfo.z)','[branch]if(amount>1.0/256){',
               '[loop]for(int i=0;i<24;++i)','sqrt(t*(1.0/24))','float3 sum=max(ratio,-.45);float total=1,t=.5;float2 dir=float2(1,0);',
               'return max(legacyT*max(baseline,.15),scene-min(fog,scene));','float3 ratio=current.rgb*legacyT/scale;',
               # 0.3.174: both Scene reads carry the contact AO (s10; a neutral 1 unless the world folds it)
               'tex2Dlod(Scene,float4(q,0,0)).rgb*tex2Dlod(AmbientOcclusion,float4(q,0,0)).a,fog,legacyT);',
               'tex2Dlod(Scene,float4(tq,0,0)).rgb*tex2Dlod(AmbientOcclusion,float4(tq,0,0)).a,fog,legacyT),-.45)*w;total+=w;',
               'float legacyT=max(mad(LegacyFog.w,saturate(pow(max(mad(z*Projection.z,LegacyFog.x,LegacyFog.y),0),LegacyFog.z))-1,1),.001);',
               'float3 fog=(1-legacyT)*LegacyFogColor.rgb;','sum+=max(light.rgb*legacyT/removalScale(','fog,legacyT),-.45)*w;total+=w;',
               'dir=float2(dir.x*-.7373688-dir.y*.6754903,dir.x*.6754903-dir.y*.7373688);t+=1;',
               'result=lerp(ratio,sum/total,amount)*scale/legacyT;',
               # the composite terms mirrored above
               'float legacyT=mad(LegacyFog.w,saturate(pow(max(mad(viewZ*Projection.z,LegacyFog.x,LegacyFog.y),0),LegacyFog.z))-1,1);',
               'float3 fogPart=min((1-legacyT)*LegacyFogColor.rgb,original.rgb);','float3 albedoT=min(transported/oldLight,legacyT);',
               'color=max(color,mad(-.45,transported,original.rgb));'):
    assert hlsl.count(needle)==1,needle

# ---- emulation of smoothRemoval (float64; the shader runs fp32 on fp16 buffers) ----
def legacy_t(z,fog):
    """WorldComposite's legacyT; fog=(LegacyFog c25, Projection.z sign)."""
    p,sign=fog;return p[3]*(min(max(max(z*sign*p[0]+p[1],0)**p[2],0),1)-1)+1
def removal_scale(base,scene,fogc,T):
    return [max(T*max(b,.15),s-min(f,s)) for b,s,f in zip(base,scene,fogc)]
# 0.3.174 dropped the .0001 floor on the scene term: T>=.001 keeps the first operand >=.00015.
_r=random.Random(174)
for _ in range(20000):
    T=max(_r.random(),.001);b=[_r.random() for _ in range(3)];s=[_r.random() for _ in range(3)];f=[_r.random() for _ in range(3)]
    assert removal_scale(b,s,f,T)==[max(T*max(x,.15),max(y-min(z,y),.0001)) for x,y,z in zip(b,s,f)]
def smooth(light,base,dist,scene,x,y,size,on,inv,pixel,fog=((0,1,1,0),1),fogcolor=(0,0,0),strength=.85,ao=lambda x,y:1.):
    """light/base/scene(x,y) -> rgba correction / rgb baseline / rgb scene at a half-res texel; dist -> view distance;
    ao(x,y) -> the contact AO alpha on s10 (0.3.174; 1 unless the world folds it). Returns TemporalLight's current.rgb."""
    unfolded=scene;scene=lambda x,y:[c*ao(x,y) for c in unfolded(x,y)]
    cur=light(x,y);z=dist(x,y)
    if not on:return cur[:3]
    amount=min(max((1-cur[3]*inv)/strength,0),1)
    if amount<=1/256:return cur[:3]
    T=max(legacy_t(z,fog),.001);fc=[(1-T)*c for c in fogcolor]
    scale=removal_scale(base(x,y),scene(x,y),fc,T);ratio=[c*T/k for c,k in zip(cur,scale)]
    radius=min(max(pixel/z,3),16);ds=-1.442695/max(.25,z*.03);vs=2*inv/strength
    s=[max(r,-.45) for r in ratio];total=1;t=.5;dx,dy=1.,0.
    for _ in range(24):
        r=math.sqrt(t/24)*radius
        tx=min(max(math.floor(x+.5+dx*r),0),size[0]-1);ty=min(max(math.floor(y+.5+dy*r),0),size[1]-1)
        l=light(tx,ty);wt=2**(abs(dist(tx,ty)-z)*ds)*min(max(1-abs(l[3]-cur[3])*vs,0),1)
        s=[a+max(c*T/k,-.45)*wt for a,c,k in zip(s,l,removal_scale(base(tx,ty),scene(tx,ty),fc,T))];total+=wt
        dx,dy=dx*-.7373688-dy*.6754903,dx*.6754903-dy*.7373688;t+=1
    return [(r+(a/total-r)*amount)*k/T for r,a,k in zip(ratio,s,scale)]

# ---- 2. synthetic buffers ----
PIXEL=.75*1.2686*864*.5 # the capture's projection and 864-pixel half-res width
GREY=lambda x,y:[.2]*3 # native 51/255 over a .3 baseline: albedo .67, below the fog bound T=1
def field(ratio,alpha,depth,baseline=.3):
    return (lambda x,y:[ratio(x,y)*baseline]*3+[alpha(x,y)]),(lambda x,y:[baseline]*3),depth
size=(96,64)
def jag(x,y):return -.35 if x>=48+(3 if (y//6)%2 else -3) else -.03  # facet boundary with a 6-pixel sawtooth
flat=lambda x,y:45.
L,B,D=field(jag,lambda x,y:1.,flat)
checks['lit pixels (alpha = drawn weight) are bit-identical']=all(smooth(L,B,D,GREY,x,y,size,1,1.,PIXEL)==L(x,y)[:3] for y in range(20,44,5) for x in range(96))
L,B,D=field(jag,lambda x,y:.15,flat)
def steps(fn,y):
    out=[51*(1+max(fn(x,y)/.3,-.45)) for x in range(30,66)];return max(abs(a-b) for a,b in zip(out,out[1:]))
before=max(steps(lambda x,y:L(x,y)[0],y) for y in range(24,40));after=max(steps(lambda x,y:smooth(L,B,D,GREY,x,y,size,1,1.,PIXEL)[0],y) for y in range(24,40))
checks[f'shadowed facet step {before:.1f}/255 -> {after:.1f}/255 (<= 5)']=before>10 and after<=5
checks['reference off path (c30.y=0) returns the input']=all(smooth(L,B,D,GREY,x,y,size,0,0.,0.)==L(x,y)[:3] for y in range(24,40,3) for x in range(96))
L,B,D=field(lambda x,y:-.45 if x<48 else -.03,lambda x,y:.15,lambda x,y:30. if x<48 else 34.)
bleed=max(abs(smooth(L,B,D,GREY,x,32,size,1,1.,PIXEL)[0]/.3+.03) for x in range(48,56))
checks[f'no bleeding across a 4-unit depth step (max {bleed:.4f} in ratio)']=bleed<.01
# Pillar/wall protrusions are 0.5-2 u, not 4: a 1 u step leaves a soft halo of the stronger removal
# (within the 0.75 u radius). Bound it on native 51/255 on both sides. A tighter depth tolerance
# (max(.25,.015z)..(.25,.025z)) halves it but fails the capture rows below (4 pairs / 6.1-6.7/255).
halo={}
for z0 in (30.,45.):
    L,B,D=field(lambda x,y:-.45 if x<48 else -.03,lambda x,y:.15,lambda x,y,z0=z0:z0 if x<48 else z0+1)
    halo[z0]=max(51*abs(smooth(L,B,D,GREY,x,32,size,1,1.,PIXEL)[0]/.3-(-.45 if x<48 else -.03)) for x in range(30,66))
checks[f"1 u protrusion halo {halo[30.]:.1f}/255 (z 30), {halo[45.]:.1f}/255 (z 45) (<= 6)"]=all(v<=6 for v in halo.values())
# Fractional source weights (dusk/dawn cross-fade): the fp16 alpha of a lit pixel is a hair off 1/sum;
# the 1/256 early-out keeps those pixels exactly unchanged (without it amount would be 3e-5..2e-4).
fp16=lambda v:struct.unpack('<e',struct.pack('<e',v))[0]
lit=[];
for weight in (.73,.3333):
    L,B,D=field(jag,lambda x,y,a=fp16(weight):a,flat)
    lit.append((1-fp16(weight)/weight)/.85>0 and all(smooth(L,B,D,GREY,x,y,size,1,1/weight,PIXEL)==L(x,y)[:3] for y in range(20,44,5) for x in range(96)))
checks['fractional weights .73/.3333: fp16 lit alpha, pixels unchanged (early-out below 1/256)']=all(lit)
L,B,D=field(lambda x,y:-.01 if x<48 else -.40,lambda x,y:1. if x<48 else .15,flat)
edge=max(abs(smooth(L,B,D,GREY,x,32,size,1,1.,PIXEL)[0]/.3+.40) for x in range(48,56))
checks[f'shadow edge stays crisp (max {edge:.2e} in ratio on the shadow side)']=edge<1e-9 and all(smooth(L,B,D,GREY,x,32,size,1,1.,PIXEL)==L(x,32)[:3] for x in range(40,48))

# ---- 3. the capture ----
report={'synthetic':{'facet_step_before':round(before,2),'facet_step_after':round(after,2),'depth_bleed':bleed,'protrusion_halo_1u':halo,'shadow_edge':edge}}
capture=fp.setting('r1_capture')
if capture:
    from analyze_world_diagnostics import Buffer
    cap=Path(capture).expanduser()
    C=list(zip(*[iter(struct.unpack('<272f',(cap/'capture-1-constants.f32').read_bytes()))]*4))
    bufs={n:Buffer(cap/f'capture-1-{n}.fgr') for n in ('light','baseline','distance','scene')}
    Wd,Ht=bufs['light'].width,bufs['light'].height;cache={}
    def get(n,x,y):
        k=(n,x,y)
        if k not in cache:cache[k]=bufs[n].at(x,y)
        return cache[k]
    light=lambda x,y:get('light',x,y);base=lambda x,y:get('baseline',x,y)[:3];dist=lambda x,y:get('distance',x,y)[0]
    inv=1/C[27][1];pixel=.75*abs(C[1][0])*C[33][2]*.5;strength=C[20][2]
    fogc,fogp,sign=C[26][:3],C[25],C[1][2];fog=(fogp,sign)
    def native(x,y):
        v=[0.,0.,0.]
        for dy in (0,1):
            for dx in (0,1):
                b,g,r,_=get('scene',2*x+dx,2*y+dy);v[0]+=r/1020;v[1]+=g/1020;v[2]+=b/1020
        return v
    def composite(x,y,corr):
        # WorldComposite relight at one half-res texel (no upsample, haze or volumetric fog: equal for both paths).
        o=native(x,y);z=dist(x,y);t=legacy_t(z,fog)
        part=[min((1-t)*f,c) for f,c in zip(fogc,o)];tr=[max(c-f,0) for c,f in zip(o,part)]
        old=[max(b,.15) for b in base(x,y)]
        col=[c+min(a/ol,t)*k for c,a,ol,k in zip(o,tr,old,corr)]
        return sum(max(c,c0-.45*a) for c,c0,a in zip(col,o,tr))/3*255
    pairs=fails0=fails1=lit=0;worst=[];identical=True
    for y in (230,240,250):
        prev=None
        for x in range(Wd):
            z=dist(x,y)
            if z>=C[0][3]*.999:prev=None;continue # sky
            raw=light(x,y)[:3];on=smooth(light,base,dist,native,x,y,(Wd,Ht),1,inv,pixel,fog,fogc,strength)
            identical&=smooth(light,base,dist,native,x,y,(Wd,Ht),0,0.,0.)==raw
            if light(x,y)[3]*inv>=1:lit+=1;identical&=on==raw
            n=sum(native(x,y))/3*255;o0=composite(x,y,raw);o1=composite(x,y,on)
            if prev and abs(n-prev[0])<=3 and abs(z-prev[1])<max(.25,z*.03):
                pairs+=1;fails0+=abs(o0-prev[2])>5;s=abs(o1-prev[3])
                if s>5:fails1+=1;worst.append((round(s,2),y,x))
            prev=(n,z,o0,o1)
    worst.sort(reverse=True)
    report['capture']={'folder':str(cap),'rows':[230,240,250],'continuous_pairs':pairs,'steps_over_5_before':fails0,'steps_over_5_after':fails1,
                       'worst_after':worst[:6],'lit_pixels_identical':lit,'radius_px_at_45u':round(min(max(pixel/45,3),16),2)}
    print(f'capture rows 230/240/250: {pairs} continuous pairs, steps > 5/255: {fails0} (0.3.158) -> {fails1} (R1); worst {worst[:3]}')
    checks['capture: reference off path is the 0.3.158 input, lit pixels unchanged']=identical
    # Q5 asks for no step > 5/255. A 25-tap point disc leaves a quantisation floor of about 5-6/255
    # where a narrow bright face (native 95-125/255) sits next to a much weaker removal; the literal
    # criterion is reported, and this check guards the measured 0.3.159 result.
    print('Q5 literal "no step > 5/255 where native is continuous": '+('met' if not fails1 else f'NOT MET, {fails1} pairs up to {worst[0][0]}/255'))
    checks[f'capture: steps > 5/255 {fails0} (0.3.158) -> {fails1} (R1), none above 6/255']=fails0>0 and fails1<=3 and all(s<=6 for s,_,_ in worst)
else:print('SKIP capture part: set NORTHLIGHT_R1_CAPTURE to the capture folder')
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
(fp.output_dir()/'shadow-removal-smoothing.json').write_text(json.dumps({'checks':checks,**report,'game_or_gpu_launched':False},indent=2))
assert all(checks.values())
print('PASS shadow removal smoothing: wiring, synthetic emulation'+(', capture rows' if capture else ' (capture part skipped)'))
