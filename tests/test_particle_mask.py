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
ENTRIES=('ParticleOver1','ParticleOver2','ParticleAddA1','ParticleAddA2','ParticleAddC1','ParticleAddC2')
checks['source: the six particle entries are generated from the three mask kinds and the two colour multipliers']=all(f'({n}, {1 if n.endswith("1") else 2})' in hl for n in ENTRIES) and all(f'PARTICLE_{k}(' in hl for k in ('OVER','ADDA','ADDC'))
checks['source: stage 0 = texture x diffuse, colour times the multiplier (MODULATE / MODULATE2X), alpha not scaled']=('float4 c = tex2D(Scene, uv) * diffuse;' in hl and 'c.rgb *= scale;' in hl)
checks['source: Over writes (1,1,1,a); AddA (1,1,1,a x brightest); AddC (brightest x4); all saturate']=(
    'mask = float4(1, 1, 1, saturate(c.a));' in hl and 'mask = float4(1, 1, 1, saturate(c.a) * particleBrightness(c));' in hl and 'mask = particleBrightness(c).xxxx;' in hl
    and 'return saturate(max(c.r, max(c.g, c.b)));' in hl)
checks['source: the rain shaders are untouched']=('mask = float4(1, 1, 1, c.a);' in hl and 'clip(abs(tex2D(Scene, uv).r - tex2D(Depth, uv).r) - 1e-6);' in hl)
sh=eff['shaders']
checks['manifest: the particle shaders are ps_2_0, small, with a compiled .bin and a generated array']=all(
    sh[n]['target']=='ps_2_0' and sh[n]['static_instruction_slots']<=32 and (fp.COMPILED/(n+'.bin')).exists() and f'static const DWORD k{n}Shader[]' in (fp.GENERATED/'compiled_shaders.h').read_text() for n in ENTRIES)
checks['manifest: the source hash covers rain_mask.hlsl']=eff['rain_source_sha256']==hashlib.sha256((fp.SHADERS/'rain_mask.hlsl').read_bytes()).hexdigest()
BEFORE={'RainMaskMRT':'bac487c7850375bdde1b396e0b5b6ed875b6b704b5dd04d4e6bd50681370d2bc','RainScrub':'ebd42e4fb0abda3ada825d75bfb148a2c5f4b2f9df307b4e9ac84da92e0bed10','AO':'778be1ea3b147c4bd34bed3e8b13536506fea805acf6660e85119f8cbb4a9bb9',
        'AOContactBloom':'15618e0c53efdbe986b4ff74e34ef7d2c3e93e5847ac3b85bdd8c637a24eeeda','Composite':'0bfee11760ad650d421136aa252d412c632dad0af6d84ade6a1305f7219c3988','AOBlur':'8f993e4c8118c029f4af218ae30620ef7c994e28c28b7ab46e02a7e070e51d2a','ContactBloom':'497c6c2c8e74e44e3cfbf6551fa7ac316750e3ccb63d78d94e29459ec36955b0'}
checks['manifest: every earlier effect and rain entry is byte-identical to 0.3.202']=all(sh[n]['sha256']==x for n,x in BEFORE.items())

# WorldComposite: the green mask channel
comp=world.split('float4 WorldComposite(',1)[1].split('// Separate geometry pass:',1)[0]
body=re.search(r'if\(PassInfo\.z<\.5\)\{(.*?)\n    \}',comp,re.S).group(1)
checks['composite: RainMask read once, red and green together, inside the PassInfo.z<.5 branch']=(comp.count('RainMask')==1 and body.count('tex2Dlod(RainMask,float4(uv,0,0)).rg')==1)
checks['composite: rain (red) lerps back to the unfogged colour first, particles (green) then to the scene colour']=(
    'unfogged,mask.x)' in body and 'color=lerp(color,original.rgb,mask.y);' in body and body.index('unfogged,mask.x)')<body.index('original.rgb,mask.y)'))
checks['composite: original.rgb is never overwritten (the AO/bloom colour is `lit`), so the scene colour is still there at the end']=(
    re.search(r'\boriginal\.rgb\s*=[^=]',comp) is None and 'float3 lit=saturate(' in comp and 'float3 color=lit;' in comp and 'tex2Dlod(Scene,float4(uv,0,0))' in comp and comp.count('tex2Dlod(Scene,')==1)
checks['composite: the relight reads lit, not the scene colour']=('min((1-legacyT)*LegacyFogColor.rgb,lit)' in comp and 'max(lit-fogPart,0)' in comp and 'mad(albedoT,bounce,lit)' in comp and 'mad(-.45,transported,lit)' in comp)
checks['composite: debug views do not touch the mask']=('RainMask' not in comp.split('if(PassInfo.z<.5)',1)[0] and 'RainMask' not in comp.split('PassInfo.z==3',1)[1])
wc=wm['WorldComposite']
checks['manifest: WorldComposite within the 512 slots of SM3, 509 now, samplers unchanged']=(wc['static_instruction_slots']==509 and wc['temporary_registers']<32 and wc['samplers']==[0,1,8,9,10,11,12,13])

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
    'kParticleOver1Shader,kParticleOver2Shader,kParticleAddA1Shader,kParticleAddA2Shader,kParticleAddC1Shader,kParticleAddC2Shader' in bg and 'particlePSFailed=true;why|=64;' in bg and 'rainMaskFailed' not in bg.split('particlePSFailed=true')[0].split('if(!why&&particle&&!particlePS[variant])')[1])
checks['begin: reasons 128 (blend) and 256 (no stage 0 texture) are particle-only; skips are counted and each distinct reason logged once (at most 8)']=(
    'why|=128' in bg and 'why|=256' in bg and '++particleSkips;' in bg and 'particleSkipLogs<8' in bg and 'PARTICLES mask skip: reason=%u' in bg)
checks['begin: the particle writes green on RT1 with the particle shader; rain keeps red and rainMrtPS']=('want[1]={particle?DWORD(D3DCOLORWRITEENABLE_GREEN):DWORD(D3DCOLORWRITEENABLE_RED)};' in bg and 'ext->SetPixelShader(particle?particlePS[variant]:rainMrtPS);' in bg)
checks['scrub: one pass clears red and green']=('D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN)' in r)
pbd=r[r.index('template<class Draw> HRESULT particleBlendDraw('):r.index('template<class Draw> HRESULT rainBlendDraw(')]
checks['particle draw: a claimed draw or a blob shadow is drawn as it was with RT1 unbound; otherwise one draw through rainMrtDraw']=('if(claimed||blobOriginal){rainMrtUnbind();return blobFaintDraw(claimed,draw);}' in pbd and 'rainMrtDraw(draw,true)' in pbd)
wf=r[r.index('    void weatherFrame('):r.index('    void logParticles(')]
checks['WEATHER line carries particleMask, particleSkips, rainLateZ and rainLateZPrims; all zeroed per frame']=(
    'particleMask=%u particleSkips=%u rainLateZ=%u rainLateZPrims=%u' in wf and 'particleMaskCount,particleSkipCount,lateZ,lateZPrims' in wf and 'particleDraws=particleSkips=rainLateZ=rainLateZPrims=0;' in wf)
lp=r[r.index('void logParticles('):r.index('void logWeatherProbe(')]
checks['PARTICLES line: sample frames only, rows of the census, reset every frame']=('if(sampled()&&any)' in lp and 'logf("PARTICLES frame=%u masked=%u skipped=%u sigs=%u more=%u%s"' in lp and 'particleSigCount=particleSigMore=0;' in lp)
checks['census rows are collected on sample frames only']=('if(particle&&sampled())particleCensus(why,vsModel,bl,st);' in bg)
checks['resources: the particle shaders are dropped with the others, flags per frame reset in clearFrame']=('for(auto& ps:particlePS)drop(ps);particlePSFailed=false;' in r and 'rainDepthOk=rainMaskRainDrawn=false;' in r)
checks['banner: 0.3.203']=('logf("Northlight renderer 0.3.203;' in r)

for k,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+k)
sys.exit(0 if all(checks.values()) else 1)
