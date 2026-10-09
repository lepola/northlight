#!/usr/bin/env python3
# northlight-test:
"""0.3.202 (rain mask): WorldComposite keeps rain streaks out of the haze - source, manifest and numeric checks, no compiler.
The mask (RainMask, s13, alpha = streak alpha, 0 without rain) is read once inside the PassInfo.z<.5 branch and the result is lerped back
toward the unfogged colour; every other compiled entry stays byte-identical to HEAD~ (0.3.200)."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import json
import re

h=(fp.SHADERS/'world_effects.hlsl').read_text()
manifest=json.loads((fp.SHADERS/'world-shader-build.json').read_text())['shaders']
checks={}

# sha256 of every other entry before the change (0.3.200 manifest)
BEFORE={'WorldNormals':'e647de5f5ba48db3fa790f329731d066a47fa965fdf474dcc568c337f36fac34','WorldLighting':'066f20c71ece663605e7a17c0e1e37510d83dc6a968efa49b39f9afc990d55d3',
 'WorldGI':'be71727902a86616b9f08e8816ad0eab064b517a0bdba0f5162e21b7f534e598','WorldFog':'0436709c5843d1179287da4efc946f4a448f7d233e81ac2068fe54bc47432f6c',
 'FogBlur':'b48b12488d6233e53031b9f947e643a47d5892fc7fd478356a0a08037a3a1e78','FogClouds':'a7952daac14f844119501a3fd632d481f2e84408bd6d10c7c2721a8977657176',
 'FogTemporal':'9c7c3c17bdd66e739fa8a239cbde4fc4d3b6e10b6884507578a75e460e4caf89','LocalDirect':'ad4107aef51fb39a9f5ed21d7f3884c63f4e580954ddf34d0fb0b46a7bbca7cc',
 'RemovalSmooth':'485461e2b8a2bf2928a5819a8cd13527e1ee21203934aeae22187fb54b5e22c1','TemporalLight':'c2dcff7332ca899b45c1f3b7580b1ef505a602bbc03cd96e5fd3f45d5f2cc75b',
 'LocalFog':'bf1b7073635c9d064779277099aab540516464d3f0016a150e6666c639e973a7','SourceVisibilityPS':'65a5e68fe23513a5750c8714975b0d4276a2b5241d44d2314fd044bb9b73671e',
 'WorldComposite':'1f3884872a55ec91f9ee42fd153aa988d16c81382de23cdd22b3347794240cd4','ShadowVS':'10172c530a92bd9cd5bbf13dff11ebbb53888c355b5b9b13a8aebe764215c165',
 'ShadowCacheVS':'54092470516c229529f1e86cadacb240ac6fcb2a877114b00e0cfed1d4f7a753','ShadowPS':'318fe2a445a91294b3f1b0a4711ba226fee07397c1f2816d9d96d09a18fdbffd',
 'ShadowReplayPS':'194f5665d22f12a8c0e337be2938d036386c4e7982f048051a804714f85a592d','ShadowUnion':'d090109910314885bc31fdf67ebd4c393f95c499da6dbd0c055498981f28d4c6'}

# HLSL
checks['source: RainMask declared on s13 (aliasing RegionalFog, as s8/s10 do)']=('sampler2D RainMask : register(s13);' in h)
comp=h.split('float4 compositeImpl(',1)[1].split('// Separate geometry pass:',1)[0]
m=re.search(r'if\(!debugViews\|\|PassInfo\.z<\.5\)\{(.*?)\n    \}\n    if\(debugViews',comp,re.S)
body=m.group(1) if m else ''
checks['source: WorldComposite reads the mask once, with tex2D at the top before any flow control (0.3.203; the rain term is its red)']=(bool(m) and comp.count('RainMask')==1 and comp.count('float4 mask=tex2D(RainMask,uv);')==1 and comp.index('float4 mask=tex2D(RainMask,uv);')<comp.index('[loop]'))
checks['source: no rain lerp any more: rain streaks are alpha-over layers of the transmittance composite (green = 1-T, blue = touched); the red channel is unused (0.3.203)']=(
    'unfogged' not in comp and 'mask.x' not in comp and 'float3 fogged=mad(lerp(color,hz.rgb,h),fog.a,fog.rgb);' in body and 'float T=1-mask.y;' in body)
# manifest
wc=manifest['WorldComposite']
checks['manifest: WorldComposite samples s13, <= 512 slots, <= 32 temporaries']=(13 in wc['samplers'] and wc['static_instruction_slots']<=512 and wc['temporary_registers']<=32)
checks['manifest: WorldComposite bytecode changed']=(wc['sha256']!=BEFORE['WorldComposite'])
checks['manifest: every other entry byte-identical to 0.3.200 (WorldCompositeDebug is new: 0.3.203)']=(set(manifest)==set(BEFORE)|{'WorldCompositeDebug'} and all(manifest[n]['sha256']==s for n,s in BEFORE.items() if n!='WorldComposite'))
checks['compiled: WorldComposite.bin exists']=(fp.COMPILED/'WorldComposite.bin').exists()

# numeric reference: the streak is an alpha-over layer, the pixel = T x background + emission; the composite outputs original + T x (F(B) - B) (0.3.203, same rule as the particles)
def over(c,col,a):return tuple(x*(1-a)+y*a for x,y in zip(c,col))
F=lambda c:tuple(.8*x+.1 for x,y in zip(c,c))   # stand-in for relight + haze + fog
def composite(B,layers):
    c=B;T=1.
    for col,a,add in layers:
        if add:c=tuple(x+y for x,y in zip(c,col))
        else:c=over(c,col,a);T*=1-a
    return tuple(x+T*(f-b) for x,f,b in zip(c,F(B),B))
def truth(B,layers):                              # the same layers drawn over the fogged background
    c=F(B)
    for col,a,add in layers:c=tuple(x+y for x,y in zip(c,col)) if add else over(c,col,a)
    return c
B=(.2,.5,.9);streak=((.8,.85,.9),.3,False);glow=((.5,.3,.05),0.,True)
checks['numeric: no layer gives F(B)']=all(abs(x-y)<1e-12 for x,y in zip(composite(B,[]),F(B)))
checks['numeric: a rain streak is exactly the fogged background with the streak drawn over it (not an a^2 approximation)']=all(abs(x-y)<1e-12 for x,y in zip(composite(B,[streak]),truth(B,[streak])))
def streak_term(layers_without,layers_with):   # what the streak adds over the pixel beneath it, with the composite
    return tuple(x-y for x,y in zip(composite(B,layers_with),composite(B,layers_without)))
beneath_plain=F(B);beneath_glow=tuple(x+y for x,y in zip(F(B),glow[0]))
want=lambda beneath:tuple(streak[1]*(c-b) for c,b in zip(streak[0],beneath))
checks['numeric: a streak over a far background, with and without a particle glow on the same pixel, adds a x (streak colour - what is beneath it), beneath = the fogged background (plus the glow)']=(
    all(abs(x-y)<1e-12 for x,y in zip(streak_term([],[streak]),want(beneath_plain))) and all(abs(x-y)<1e-12 for x,y in zip(streak_term([glow],[glow,streak]),want(beneath_glow))))
checks['numeric: glow and streak in either order equal the fogged background with both drawn over it']=all(abs(x-y)<1e-12 for x,y in zip(composite(B,[glow,streak]),truth(B,[glow,streak]))) and all(abs(x-y)<1e-12 for x,y in zip(composite(B,[streak,glow]),truth(B,[streak,glow])))
# 0.3.203: mod2x halos (pixel = 2 x src x dst) over the pixel after the streaks: pixel = M x (T x B + E). The mask's alpha carries M/2 (start 128/255 = x1); the composite is
# pixel/M + T x (F(M x B) - B) = E + T x F(M x B): the streak is not multiplied, the halo is fogged with the background it multiplies, exactly like an untouched neighbour.
def comp_halo(B,T,E,M):
    pixel=tuple(M*(T*b+e) for b,e in zip(B,E))
    return tuple(p/M+T*(f-b) for p,f,b in zip(pixel,F(tuple(M*b for b in B)),B))
E=tuple(.3*x for x in streak[0]);Tt=1-streak[1]
for M in (1.,1.4,1.99):
    out=comp_halo(B,Tt,E,M);want=tuple(e+Tt*f for e,f in zip(E,F(tuple(M*b for b in B))))
    checks['numeric: a streak under a mod2x halo M=%.2f outputs E + T x F(M x B)'%M]=all(abs(x-y)<1e-12 for x,y in zip(out,want))
    out_n=comp_halo(B,1.,(0,0,0),M)
    checks['numeric: the untouched neighbour of that pixel (halo only) is F(M x B), the same background the streak pixel is fogged with (M=%.2f)'%M]=all(abs(x-y)<1e-12 for x,y in zip(out_n,F(tuple(M*b for b in B))))
    checks['numeric: the streak adds T x F(M x B) + E - F(M x B) = a x (streak - F(M x B)) over the neighbour, not multiplied by M (M=%.2f)'%M]=all(abs((x-y)-streak[1]*(sc-f))<1e-12 for x,y,sc,f in zip(out,out_n,streak[0],F(tuple(M*b for b in B))))
checks['numeric: with M = 1 the halo formula is the previous composite']=all(abs(x-y)<1e-12 for x,y in zip(comp_halo(B,Tt,E,1.),composite(B,[streak])))
# the alpha channel arithmetic: RT1 blend DESTCOLOR/SRCCOLOR with oC1.a = luminance(src.rgb): a' = 2 x lum x a, 8 bit
q=lambda x:round(max(0.,min(1.,x))*255)/255
a8=128/255;lum=lambda c:.299*c[0]+.587*c[1]+.114*c[2]
checks['numeric: the clear value 128/255 reads as M = 1 exactly, neutral (alpha 0) is guarded to 1']=(abs(a8*255./128.-1.)<1e-12 and 0*255./128.<.05)
for halos in (((.5,.5,.5),),((.9,.7,.5),),((.9,.7,.5),(.8,.8,.8)),((1.,1.,1.),(1.,1.,1.))):
    a=a8;M_true=1.
    for c in halos:a=q(2*lum(c)*a);M_true=min(2.,M_true*2*lum(c))
    checks['numeric: stacked halos %s accumulate alpha = M/2 (8 bit, error < 1.5%%, saturating at ~2)'%(halos,)]=abs(a*255./128.-min(M_true,255./128.))<.03
checks['numeric: a halo of src 0.5 (lum .5) leaves the factor at 1']=abs(q(2*.5*a8)-a8)<1e-12
# 0.3.202 (rain mask MRT): the two ps_2_0 shaders of the MRT mask (shaders/rain_mask.hlsl); the ps_3_0 entries of effects.hlsl stay byte-identical
eff=json.loads((fp.SHADERS/'shader-build.json').read_text())['shaders']
EFF_BEFORE={'AO':'778be1ea3b147c4bd34bed3e8b13536506fea805acf6660e85119f8cbb4a9bb9','AOContactBloom':'15618e0c53efdbe986b4ff74e34ef7d2c3e93e5847ac3b85bdd8c637a24eeeda','Composite':'0bfee11760ad650d421136aa252d412c632dad0af6d84ade6a1305f7219c3988'}
import hashlib
checks['MRT shaders: effects.hlsl untouched (the rain shaders have their own source, hashed in the manifest)']=('RainMaskMRT' not in (fp.SHADERS/'effects.hlsl').read_text() and json.loads((fp.SHADERS/'shader-build.json').read_text()).get('rain_source_sha256')==hashlib.sha256((fp.SHADERS/'rain_mask.hlsl').read_bytes()).hexdigest())
checks['MRT shaders: AO, AOContactBloom and Composite byte-identical to 0.3.201']=all(eff[n]['sha256']==x for n,x in EFF_BEFORE.items())
checks['MRT shaders: RainMaskMRT and RainScrub are ps_2_0, small, with a compiled .bin']=all(n in eff and eff[n]['target']=='ps_2_0' and eff[n]['static_instruction_slots']<=32 and (fp.COMPILED/(n+'.bin')).exists() for n in ('RainMaskMRT','RainScrub'))
ehl=(fp.SHADERS/'rain_mask.hlsl').read_text()
checks['MRT shaders: source writes oC1 = white with the streak alpha (RT1 masked to green and blue); the scrub discards unchanged depth and outputs (0,0,0,128/255): no coverage, mod2x factor back to x1']=('out float4 mask : COLOR1' in ehl and 'mask = float4(1, 1, 1, c.a);' in ehl and 'clip(abs(tex2D(Scene, uv).r - tex2D(Depth, uv).r) - 1e-6);' in ehl and 'return float4(0, 0, 0, 128.0 / 255.0);' in ehl.split('float4 RainScrub(',1)[1])
for k,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+k)
sys.exit(0 if all(checks.values()) else 1)
