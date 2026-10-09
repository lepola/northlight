#!/usr/bin/env python3
# northlight-test:
"""0.3.203 (particle mask): translucent no-depth-write world particles keep out of the fog, haze and relight - shader source, manifests, the blend arithmetic
of the mask values and the renderer wiring, no compiler and no device. The draw-level behaviour (classification, state restore, the green channel, skip logging)
runs in test_weather_detect's harness over the real renderer.cpp code."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import hashlib
import json
import re

hl=(fp.SHADERS/'rain_mask.hlsl').read_text()
eff=json.loads((fp.SHADERS/'shader-build.json').read_text())
world=(fp.SHADERS/'world_effects.hlsl').read_text()
wm=json.loads((fp.SHADERS/'world-shader-build.json').read_text())['shaders']
r=fp.src('renderer.cpp').read_text()
checks={}

# HLSL: six ps_2_0 entries; the rain shaders are unchanged
ENTRIES=('ParticleOver1','ParticleOver2','ParticleAddA1','ParticleAddA2','ParticleAddC1','ParticleAddC2','ParticleMod1','ParticleMod2')
checks['source: the six particle entries are generated from the three mask kinds and the two colour multipliers']=all(f'({n}, {1 if n.endswith("1") else 2})' in hl for n in ENTRIES) and all(f'PARTICLE_{k}(' in hl for k in ('OVER','ADDA','ADDC'))
checks['source: stage 0 = texture x diffuse, colour times the multiplier (MODULATE / MODULATE2X), alpha not scaled']=('float4 c = tex2D(Scene, uv) * diffuse;' in hl and 'c.rgb *= scale;' in hl)
checks['source: Over writes (1,1,1,a); AddA (1,1,1,a x brightest); AddC (brightest x4); all saturate (fog-less additive layers keep their original blue-only full-weight writes)']=(
    'mask = float4(1, 1, 1, saturate(c.a));' in hl and 'mask = float4(1, 1, 1, saturate(c.a) * particleBrightness(c));' in hl and 'mask = particleBrightness(c).xxxx;' in hl
    and 'return saturate(max(c.r, max(c.g, c.b)));' in hl)
checks['source: the rain shaders are untouched']=('mask = float4(1, 1, 1, c.a);' in hl and 'clip(abs(tex2D(Scene, uv).r - tex2D(Depth, uv).r) - 1e-6);' in hl)
sh=eff['shaders']
checks['manifest: the particle shaders are ps_2_0, small, with a compiled .bin and a generated array']=all(
    sh[n]['target']=='ps_2_0' and sh[n]['static_instruction_slots']<=32 and (fp.COMPILED/(n+'.bin')).exists() and f'static const DWORD k{n}Shader[]' in (fp.GENERATED/'compiled_shaders.h').read_text() for n in ENTRIES)
checks['manifest: the source hash covers rain_mask.hlsl']=eff['rain_source_sha256']==hashlib.sha256((fp.SHADERS/'rain_mask.hlsl').read_bytes()).hexdigest()
BEFORE={'RainMaskMRT':'bac487c7850375bdde1b396e0b5b6ed875b6b704b5dd04d4e6bd50681370d2bc','AO':'778be1ea3b147c4bd34bed3e8b13536506fea805acf6660e85119f8cbb4a9bb9',
        'AOContactBloom':'15618e0c53efdbe986b4ff74e34ef7d2c3e93e5847ac3b85bdd8c637a24eeeda','Composite':'0bfee11760ad650d421136aa252d412c632dad0af6d84ade6a1305f7219c3988','AOBlur':'8f993e4c8118c029f4af218ae30620ef7c994e28c28b7ab46e02a7e070e51d2a','ContactBloom':'497c6c2c8e74e44e3cfbf6551fa7ac316750e3ccb63d78d94e29459ec36955b0'}
checks['manifest: every earlier effect and rain entry (but the scrub) is byte-identical to 0.3.202']=all(sh[n]['sha256']==x for n,x in BEFORE.items())

# the stage result is clamped like the fixed-function stage (RT0 must equal it under the game's fog): the compiled output has a saturating move into oC0
def sat_into_oc0(n):
    asm=(fp.COMPILED/(n+'.bin.asm')).read_text().splitlines()
    out=[l.split(',',1)[1].strip() for l in asm if l.strip().startswith('mov oC0.xyzw,')]
    if len(out)!=1:return False
    reg=out[0].split('.')[0]
    for l in reversed(asm[:[k for k,x in enumerate(asm) if x.strip().startswith('mov oC0.xyzw,')][0]]):
        t=l.strip().split()
        if t and t[1:2] and t[1].startswith(reg+'.'):
            return t[0].endswith('_sat') and t[1].split(',')[0].endswith('.xyzw')
    return False
checks['source: the stage result is saturated (fixed-function MODULATE2X clamps before fog and blend)']=('return saturate(c);' in hl)
checks['compiled: every variant writes oC0 from a saturating move of all four components (the 2X ones too)']=all(sat_into_oc0(n) for n in ENTRIES)

# WorldComposite: the transmittance composite
comp=world.split('float4 compositeImpl(',1)[1].split('// Separate geometry pass:',1)[0]
body=re.search(r'if\(!debugViews\|\|PassInfo\.z<\.5\)\{(.*?)\n    \}\n    if\(debugViews',comp,re.S).group(1)
checks['composite: RainMask and Background are read once each with tex2D at the top, before any flow control']=(
    comp.count('RainMask')==1 and comp.count('Background')==1 and 'float4 mask=tex2D(RainMask,uv);' in comp and 'tex2D(Background,uv).rgb' in comp and comp.index('tex2D(Background,uv)')<comp.index('[loop]'))
checks['composite: B = the snapshot where green or blue >= 1/255 (something touched the pixel), else the pixel without its mod2x halos; bg = B x M']=(
    'float M=mask.a<.05?1:mask.a*(255./128.);' in comp and 'float3 pixel=original.rgb*rcp(M);' in comp and 'float3 B=max(mask.b,mask.y)<.002?pixel:tex2D(Background,uv).rgb;' in comp and 'float3 bg=B*M;' in comp)
checks['composite: the AO/bloom and relight work on bg']=('float3 lit=saturate(mad(bloom,1-saturate(bg*ao),bg*ao));' in comp and 'original.rgb' not in comp.split('float3 lit=',1)[1].split('if(PassInfo.z<.5)',1)[0])
checks['composite: pixel + T x (F(M x B) - B) (the debug entry) / T x F + (1 - lost) x E + airlight (the normal entry) with T = 1 - green for rain streaks and particles alike (no rain lerp)']=(
    'unfogged' not in comp and 'float3 fogged=mad(color,transF,airlight);' in body and 'color=mad(1-mask.y,fogged-B,pixel);' in body and 'float T=1-mask.y;' in body)
checks['composite: the old lerp to the scene colour is gone']=('lerp(color,original.rgb' not in comp)
wc=wm['WorldComposite']
checks['manifest: WorldComposite within the 512 slots of SM3, samplers 0,1,8..14']=(wc['static_instruction_slots']<=512 and wc['temporary_registers']<=32 and wc['samplers']==[0,1,8,9,10,11,12,13,14])

# the arithmetic of the mask values under each blend (D3D9 applies the draw's blend to RT1 with oC1 as the source)
def blend(src,dst,s,d,m):  # RT1 green: s,d factor functions of (oC1 g, oC1 a)
    return s(m)*m[1]+d(m)*dst
one=lambda m:1.;sa=lambda m:m[3];isa=lambda m:1-m[3];sc=lambda m:m[1]
def over(a):return (1,1,1,a)
def addA(a,c):return (1,1,1,a*max(c))
def addC(c):v=max(c);return (v,v,v,v)
g=0.
for a in (.5,.5,.5):g=blend(0,g,sa,isa,over(a))   # SRCALPHA / INVSRCALPHA
checks['numeric: alpha over lays the coverage over earlier coverage (1-(1-a)^n)']=abs(g-(1-.5**3))<1e-12
checks['numeric: alpha over: a zero alpha adds nothing, alpha 1 covers fully']=(blend(0,.3,sa,isa,over(0.))==.3 and blend(0,.3,sa,isa,over(1.))==1.)
g=0.
for _ in range(4):g=min(1.,blend(0,g,sa,one,addA(.5,(1.,.6,.2))))   # SRCALPHA / ONE, bright orange
checks['numeric: additive by alpha accumulates alpha x brightest channel and saturates at 1 (4 layers of .5 -> 1, not a full quad on the first)']=(abs(blend(0,0.,sa,one,addA(.5,(1.,.6,.2)))-.5)<1e-12 and g==1.)
checks['numeric: a dark additive particle adds little (it hides little fog)']=blend(0,0.,sa,one,addA(1.,(.1,.05,.02)))<.11
checks['numeric: ONE/ONE adds the brightest channel']=abs(blend(0,.2,one,one,addC((.6,.3,.1)))-.8)<1e-12
checks['numeric: SRCCOLOR/ONE adds the colour the blend adds (brightest channel squared)']=abs(blend(0,.2,sc,one,addC((.6,.3,.1)))-(.2+.36))<1e-12

# the composite's arithmetic: the pixel is T x B + E, the composite outputs original + T x (F(B) - B) = E + T x F(B), exact for any F
lerp=lambda a,b,t:tuple(x+(y-x)*t for x,y in zip(a,b))
F=lambda c:tuple(.8*x+.1 for x in c)          # stand-in for relight + haze + fog (any function)
def pixel(B,layers):                           # the game's blends in draw order; layers: ('over',colour,a) or ('add',colour)
    c=list(B);T=1.
    for l in layers:
        if l[0]=='over':c=[x*(1-l[2])+y*l[2] for x,y in zip(c,l[1])];T*=1-l[2]
        else:c=[x+y for x,y in zip(c,l[1])]
    return tuple(c),1-T
def composite(B,layers):
    o,g=pixel(B,layers);return tuple(x+(1-g)*(f-b) for x,f,b in zip(o,F(B),B))
def truth(B,layers):                           # the same blends on the fogged background: the particle is not fogged, the background is
    c=list(F(B))
    for l in layers:
        c=[x*(1-l[2])+y*l[2] for x,y in zip(c,l[1])] if l[0]=='over' else [x+y for x,y in zip(c,l[1])]
    return tuple(c)
B=(.2,.5,.3);lay_over=[('over',(.9,.5,.1),.35)];lay_add=[('add',(.5,.3,.05))];lay_mix=[('over',(.2,.2,.2),.5),('add',(.4,.2,.0)),('over',(.9,.5,.1),.25)]
checks['numeric: transmittance composite equals fogging the background and then drawing the particles (over, additive, mixed)']=all(
    all(abs(a-b)<1e-12 for a,b in zip(composite(B,l),truth(B,l))) for l in (lay_over,lay_add,lay_mix))
checks['numeric: green is 1-T of the over draws only; additive adds nothing to it']=(abs(pixel(B,lay_mix)[1]-(1-.5*.75))<1e-12 and pixel(B,lay_add)[1]==0.)
checks['numeric: no particle: the composite is F of the scene colour']=all(abs(a-b)<1e-12 for a,b in zip(composite(B,[]),F(B)))
checks['numeric: the old lerp toward the scene colour leaves the soft edge un-hazed (the fringe the transmittance composite removes)']=any(abs(a-b)>1e-3 for a,b in zip(lerp(F(B),pixel(B,lay_over)[0],pixel(B,lay_over)[1]),truth(B,lay_over)))

# renderer wiring
rl=r.split('\n')
hook=r[r.index('template<class Capture,class Draw> HRESULT drawHook('):][:4500]
checks['hook: the candidate test follows the weather comparison and the mist test, before the RT1 unbind and any capture work']=(
    hook.index('else if(weatherDetect.mistArmed)')<hook.index('if(!rainBlend&&!mist)particle=particleCandidate();')<hook.index('if(rainMrtBound&&!rainBlend&&!particle)rainMrtUnbind();')<hook.index('if(!frameDrawGates){')<hook.index('prepareDraw(capture,count)'))
checks['hook: the late-Z count is behind the rain-mask flag, so dry frames pay one bool']=('if(rainMaskRainDrawn&&!applied)noteRainLateZ(count);' in hook)
pc=r[r.index('bool particleCandidate(){'):r.index('// 0.3.203 (rain): a Z-writing world draw')]
checks['candidate: pre-effects after the terrain, no depth write first (the common draw leaves after one read), then blending']=(
    'if(applied||!terrain||!enabled||failed||!world||rainMaskFailed)return false;' in pc and pc.index('D3DRS_ZWRITEENABLE')<pc.index('D3DRS_ALPHABLENDENABLE') and pc.count('GetRenderState')==2)
pw=r[r.index('bool particleWorldDraw(){'):r.index('bool particleCandidate(){')]
checks['classification: world depth, projection, the world viewport depth range, ZENABLE, and no terrain, UI or water vertex shader']=(
    'sameWorldDepth()' in pw and 'NorthlightWorldDrawDomain::accepts(projectionValid,ze!=FALSE,worldMinDepth,worldMaxDepth,vp.MinZ,vp.MaxZ)' in pw and 'kTagMask|kWaterTag' in pw and 'D3DRS_ZENABLE' in pw)
bg=r[r.index('bool rainMrtBegin(bool particle=false){'):r.index('void rainMrtEnd(){')]
checks['begin: a non-world particle draw is no candidate (uncounted); the blend kinds are alpha over, additive by alpha and ONE/SRCCOLOR+ONE; MODULATE and MODULATE2X only']=(
    'if(particle&&!particleWorldDraw()){rainMrtUnbind();return false;}' in bg and 'bl[0]==D3DBLEND_SRCALPHA&&bl[1]==D3DBLEND_INVSRCALPHA)kind=0;' in bg and 'bl[0]==D3DBLEND_SRCALPHA&&bl[1]==D3DBLEND_ONE)kind=1;' in bg
    and '(bl[0]==D3DBLEND_ONE||bl[0]==D3DBLEND_SRCCOLOR)&&bl[1]==D3DBLEND_ONE)kind=2;' in bg and 'bl[2]==D3DBLENDOP_ADD' in bg and 'st[0]==D3DTOP_MODULATE||st[0]==D3DTOP_MODULATE2X' in bg and 'st[3]==D3DTOP_MODULATE&&texDiffuse(st[4],st[5])' in bg
    and 'variant=unsigned(kind)*2+(st[0]==D3DTOP_MODULATE2X?1:0);' in bg)
checks['begin: the shader variants are created on first use; a failure is logged once and leaves the rain mask alone']=(
    'kParticleOver1Shader,kParticleOver2Shader,kParticleAddA1Shader,kParticleAddA2Shader,kParticleAddC1Shader,kParticleAddC2Shader' in bg and 'particlePSFailed=true;why|=64;' in bg and 'rainMaskFailed' not in bg.split('particlePSFailed=true')[0].split('if(!why&&particle&&!psBound&&!particlePS[variant])')[1])
checks['begin: reasons 128 (blend) and 256 (no stage 0 texture) are particle-only; skips are counted and each distinct reason logged once (at most 8)']=(
    'why|=128' in bg and 'why|=256' in bg and '++particleSkips;' in bg and 'particleSkipLogs<8' in bg and 'PARTICLES mask skip: reason=%u' in bg)
checks['begin: RT1 colour mask: green for rain and fog-less over layers, red+green+blue for the fog-aware over shader, red+blue for the fog-aware additive ones, blue only for the fog-less ones (red = (1-f) x weight), with the particle shader or the patched game shader, rainMrtPS for rain']=(
    'const bool over=patchedPs?patchKind==0:variant<2;' in bg and 'const bool mod2x=!patchedPs&&particle&&variant>=6;' in bg and 'want[1]={DWORD(mod2x?D3DCOLORWRITEENABLE_ALPHA:(!particle||over)?(patchedPs&&patchedFogged?D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_BLUE:D3DCOLORWRITEENABLE_GREEN):patchedPs&&patchedFogged?D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_BLUE:D3DCOLORWRITEENABLE_BLUE)};' in bg and 'ext->SetPixelShader(patchedPs?patchedPs:particle?particlePS[variant]:rainMrtPS);' in bg)
checks['scrub: one pass clears red, green, blue and sets alpha back to 128/255']=('D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_ALPHA)' in r)
pbd=r[r.index('template<class Draw> HRESULT particleBlendDraw('):r.index('template<class Draw> HRESULT rainBlendDraw(')]
checks['particle draw: a claimed draw or a blob shadow is drawn as it was with RT1 unbound; otherwise one draw through rainMrtDraw']=('if(claimed||blobOriginal){rainMrtUnbind();return blobFaintDraw(claimed,draw);}' in pbd and 'rainMrtDraw(draw,true)' in pbd)
wf=r[r.index('    void weatherFrame('):r.index('    void logParticles(')]
checks['WEATHER line carries particleMask, particleSkips, rainLateZ and rainLateZPrims; all zeroed per frame']=(
    'particleMask=%u particleSkips=%u rainLateZ=%u rainLateZPrims=%u' in wf and 'particleMaskCount,particleSkipCount,lateZ,lateZPrims' in wf and 'particleDraws=particleSkips=rainLateZ=rainLateZPrims=0;' in wf)
lp=r[r.index('void logParticles('):r.index('void logWeatherProbe(')]
checks['PARTICLES line: sample frames only, rows of the census, reset every frame']=('if(sampled()&&any)' in lp and 'logf("PARTICLES frame=%u masked=%u skipped=%u rt1Binds=%u cap=%u bg=%d sigs=%u more=%u patched=%u fogged=%u nofog=%u psCache=%u/%u patchRejects={%s } mod2x=%u/%u/%u%s"' in lp and 'particleSigCount=particleSigMore=0;' in lp)
checks['census rows are collected on sample frames only']=('if(particle&&sampled())particleCensus(why,vsModel,bl,st);' in bg)
checks['resources: the particle shaders are dropped with the others, flags per frame reset in clearFrame']=('for(auto& ps:particlePS)drop(ps);particlePatched.clear();particlePSFailed=false;' in r and 'rainDepthOk=rainMaskRainDrawn=particleBgTried=particleBgOk=false;' in r)
bs=r[r.index('bool particleBackgroundStart(){'):r.index('// Everything that makes a rain draw (particle=false)')]
checks['background: one StretchRect of the game target (MSAA resolved) into a lazily created texture of the target format; failure logged once, no particle mask that frame']=(
    'ext->StretchRect(rt,nullptr,particleBgSurface,nullptr,D3DTEXF_NONE)' in bs and 'ext->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,rd.Format,D3DPOOL_DEFAULT,&particleBg,nullptr)' in bs and 'particleBgFormat!=rd.Format' in bs
    and 'if(!particleBgLogged){particleBgLogged=true;logf("PARTICLES background snapshot failed' in bs and bs.count('StretchRect')==1)
checks['begin: the snapshot is taken once per frame at the first mask draw of either kind (rain or particle), after the depth snapshot and with RT1 unbound; failing it skips the draw (reason 512), rain included']=(
    'if(!particleBgTried){rainMrtUnbind();particleBackgroundStart();}' in bg and 'if(!particleBgOk)why|=512;' in bg and 'if(!particle&&rainBgState<0)rainBgState=particleBgOk?1:0;' in bg and bg.index('rainMaskStart(maskTarget)')<bg.index('particleBackgroundStart();')<bg.index('ext->SetRenderTarget(1,maskTarget)'))
checks['begin: particles bind RT1 at most kParticleRebindCap (24) times a frame; past it the draw is made unchanged (reason 1024)']=(
    'static constexpr unsigned kParticleRebindCap=24;' in r and 'if(particle&&rainMrtRuns>=kParticleRebindCap)why|=1024;' in bg)
checks['classification: skinned vertex shaders are candidates (lantern glow cards); translucent actors write Z and fail particleCandidate()']=('vc.skinned' not in pw and 'ok=(vc.entry&(kTagMask|kWaterTag))==0;' in pw)
checks['resources: the background is released with the mask resources (release, size change)']=r[r.index('void releaseResources() {'):r.index('bool error(HRESULT hr')].count('drop(particleBgSurface);drop(particleBg);')==1 and r[r.index('bool resources(UINT w'):r.index('if (!aoPS &&')].count('drop(particleBgSurface);drop(particleBg);')==1
checks['composite handoff: renderEffects gives the world the background only with a mask and a good snapshot, right before the rain mask']=(
    'world->setParticleBackground(rainMaskTex&&particleBgOk?particleBg:nullptr);' in r and 0<r.index('world->setRainMask(rainMaskTex);')-r.index('world->setParticleBackground(')<260)
wr=fp.src('world_renderer.h').read_text()
checks['world: Background on s14 for the composite only (neutralZero otherwise, explicit sampler state), unbound after; the pointer is frame-local']=(
    'd->SetTexture(14,particleBackground?particleBackground:neutralZero);particleBackground=nullptr;' in wr and 'D3DSAMP_SRGBTEXTURE,FALSE);' in wr.split('d->SetTexture(14,particleBackground?',1)[1][:600] and 'd->SetTexture(13,regionalFogTexture);d->SetTexture(14,nullptr);' in wr)
# the game's own particle pixel shaders (vs_3_0 + ps_3_0): a patched variant, not a replacement
checks['begin: a particle draw with a game pixel shader is eligible when the variant for its blend kind exists; the stage setup, texture and vertex shader model are not read for it']=(
    'if(psBound&&!particle)why|=8;' in bg and '&&!(particle&&psBound))why|=4;' in bg and 'else if(psBound)patchKind=kind==2&&bl[0]==D3DBLEND_SRCCOLOR?3:kind;' in bg and 'if(!psBound){' in bg.split('const bool colorOk')[1].split('if(kind>=0&&colorOk)')[0] and 'particlePatched.get(ext,gamePs,unsigned(patchKind))' in bg)
checks['begin: no variant = reason 2048, counted by reason, logged once per shader and kind; the draw is made as the game does']=(
    'if(!patched.shader){why|=2048;++particlePatchRejects[' in bg and 'if(patched.log)logf("PARTICLES game shader hash=%016llx kind=%d: %s"' in bg and 'PARTICLES mask skip: reason=%u' in bg)
checks['begin: the game shader is bound only for the draw: one reference held, restored by rainMrtEnd (never nullptr for it)']=(
    'if(patchedPs){particleGamePs=gamePs;gamePs->AddRef();}' in bg and 'if(particleGamePs){ext->SetPixelShader(particleGamePs);particleGamePs->Release();particleGamePs=nullptr;}' in r and 'else ext->SetPixelShader(nullptr);' in r)
checks['lifetime: variants are forgotten when the game registers a pixel shader at the same address, and cleared with the resources']=(
    'particlePatched.forget(*out);' in r and 'particlePatched.clear();' in r.split('void releaseResources() {')[1].split('bool error(')[0])
checks['counters: patched draws and rejections are zeroed with the PARTICLES line']=('particleFoggedDraws=particlePatchedDraws=mod2xBeforeSnapshot=mod2xAfterSnapshot=mod2xAfterRain=0;memset(particlePatchRejects,0,sizeof particlePatchRejects);' in lp and '#include "particle_shader_patch.h"' in r)
checks['PARTICLES bg= is the frame\'s snapshot result saved before clearFrame resets it; the WEATHER line says whether the snapshot preceded the first rain mask draw']=(
    'particleBgLast=particleBgTried?int(particleBgOk):-1;drop(worldDepth);' in r and 'sampleFrame,particleDraws,particleSkips,rebinds,kParticleRebindCap,particleBgLast,' in lp and 'rainLateZPrims=%u rainBg=%d' in wf and 'const int rainBgCount=rainBgState;rainBgState=-1;' in wf)
checks['late Z census: sample frames only (state reads and rows), blended vs opaque split, a PARTICLES lateZ line with the signature rows, zeroed per frame']=(
    'if(sampled())lateZCensus(count);' in r and 'void lateZCensus(UINT count){' in r and 'logf("PARTICLES lateZ frame=%u draws=%u prims=%u blended=%u opaque=%u sigs=%u more=%u%s"' in lp and 'lateZSigCount=lateZSigMore=lateZBlended=lateZOpaque=0;' in lp
    and 'D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_ALPHATESTENABLE,D3DRS_ZFUNC,D3DRS_COLORWRITEENABLE' in r)
checks['late no-Z census: after the snapshot, not a mask draw, no Z write into the world depth; sample frames only; a PARTICLES lateNoZ line; called after both draw paths']=(
    r.count('if(particleBgOk&&!applied&&sampled())lateNoZCensus(count,maskBefore,particle?1u:rainBlend?2u:0u);')==2 and 'if(particleDraws+rainMaskDraws!=maskBefore||!sameWorldDepth())return;' in r and 'if(FAILED(ext->GetRenderState(D3DRS_ZWRITEENABLE,&zw))||FAILED(ext->GetRenderState(D3DRS_ZENABLE,&ze))||(zw&&ze))return;' in r
    and 'logf("PARTICLES lateNoZ frame=%u draws=%u prims=%u sigs=%u more=%u%s"' in lp and 'noZSigCount=noZSigMore=noZDraws=noZPrims=0;' in lp)
checks['banner: 0.3.203']=('logf("Northlight renderer 0.3.203;' in r)

for k,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+k)
sys.exit(0 if all(checks.values()) else 1)
