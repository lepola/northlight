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
comp=h.split('float4 WorldComposite(',1)[1].split('// Separate geometry pass:',1)[0]
m=re.search(r'if\(PassInfo\.z<\.5\)\{(.*?)\n    \}',comp,re.S)
body=m.group(1) if m else ''
checks['source: WorldComposite reads the mask once, only inside the PassInfo.z<.5 branch']=(bool(m) and comp.count('RainMask')==1 and body.count('tex2Dlod(RainMask,float4(uv,0,0)).r')==1)
checks['source: unfogged colour kept before haze, lerped back to after the haze + fog mad']=(
    'float3 unfogged=color;' in body and body.index('unfogged=color')<body.index('lerp(mad(horizonHaze(')<body.index('fog.a,fog.rgb)')<body.index(',unfogged,rain)')
    and ',unfogged,rain)' in body)
checks['source: debug views (PassInfo.z != 0) do not touch the mask']=('RainMask' not in comp.split('if(PassInfo.z<.5)',1)[0] and 'RainMask' not in comp.split('PassInfo.z==3',1)[1])

# manifest
wc=manifest['WorldComposite']
checks['manifest: WorldComposite samples s13, <= 512 slots, < 32 temporaries']=(13 in wc['samplers'] and wc['static_instruction_slots']<=512 and wc['temporary_registers']<32)
checks['manifest: WorldComposite bytecode changed']=(wc['sha256']!=BEFORE['WorldComposite'])
# 0.3.205 (gh#20): LocalFog changed on purpose (it returns the raw batch sum); LocalFogBatchCapped is the 0.3.200 LocalFog byte for byte, LocalFogCombine is new
checks['manifest: every other entry byte-identical to 0.3.200 (LocalFog changed on purpose, LocalFogBatchCapped = the old LocalFog, LocalFogCombine new)']=(set(manifest)==set(BEFORE)|{'LocalFogCombine','LocalFogBatchCapped'} and all(manifest[n]['sha256']==s for n,s in BEFORE.items() if n not in ('WorldComposite','LocalFog'))
    and manifest['LocalFog']['sha256']!=BEFORE['LocalFog'] and manifest['LocalFogBatchCapped']['sha256']==BEFORE['LocalFog'])
checks['compiled: WorldComposite.bin exists']=(fp.COMPILED/'WorldComposite.bin').exists()

# numeric reference of the blend: lerp(fogged, unfogged, mask)
def lerp(a,b,t):return tuple(x+(y-x)*t for x,y in zip(a,b))
def composite(color,haze,fog_a,fog_rgb,mask):
    unfogged=color
    fogged=tuple(c*fog_a+f for c,f in zip(haze(color),fog_rgb))
    return lerp(fogged,unfogged,mask)
haze=lambda c:lerp(c,(.6,.7,.8),.35)
col=(.2,.5,.9);fa=.7;fr=(.05,.04,.03)
old=tuple(c*fa+f for c,f in zip(haze(col),fr))
checks['numeric: mask 0 gives exactly the previous result']=(composite(col,haze,fa,fr,0.)==old)
checks['numeric: mask 1 gives the unfogged colour']=all(abs(a-b)<1e-12 for a,b in zip(composite(col,haze,fa,fr,1.),col))
mid=composite(col,haze,fa,fr,.5)
checks['numeric: mask .5 is the midpoint']=all(abs(m_-(o+c)/2)<1e-12 for m_,o,c in zip(mid,old,col))
# 0.3.202 (rain mask MRT): the two ps_2_0 shaders of the MRT mask (shaders/rain_mask.hlsl); the ps_3_0 entries of effects.hlsl stay byte-identical
eff=json.loads((fp.SHADERS/'shader-build.json').read_text())['shaders']
EFF_BEFORE={'AO':'778be1ea3b147c4bd34bed3e8b13536506fea805acf6660e85119f8cbb4a9bb9','AOContactBloom':'15618e0c53efdbe986b4ff74e34ef7d2c3e93e5847ac3b85bdd8c637a24eeeda','Composite':'0bfee11760ad650d421136aa252d412c632dad0af6d84ade6a1305f7219c3988'}
import hashlib
checks['MRT shaders: effects.hlsl untouched (the rain shaders have their own source, hashed in the manifest)']=('RainMaskMRT' not in (fp.SHADERS/'effects.hlsl').read_text() and json.loads((fp.SHADERS/'shader-build.json').read_text()).get('rain_source_sha256')==hashlib.sha256((fp.SHADERS/'rain_mask.hlsl').read_bytes()).hexdigest())
checks['MRT shaders: AO, AOContactBloom and Composite byte-identical to 0.3.201']=all(eff[n]['sha256']==x for n,x in EFF_BEFORE.items())
checks['MRT shaders: RainMaskMRT and RainScrub are ps_2_0, small, with a compiled .bin']=all(n in eff and eff[n]['target']=='ps_2_0' and eff[n]['static_instruction_slots']<=32 and (fp.COMPILED/(n+'.bin')).exists() for n in ('RainMaskMRT','RainScrub'))
ehl=(fp.SHADERS/'rain_mask.hlsl').read_text()
checks['MRT shaders: source writes oC1 = white with the streak alpha (RT1 masked to red); the scrub discards unchanged depth and outputs 0']=('out float4 mask : COLOR1' in ehl and 'mask = float4(1, 1, 1, c.a);' in ehl and 'clip(abs(tex2D(Scene, uv).r - tex2D(Depth, uv).r) - 1e-6);' in ehl and 'return 0;' in ehl.split('float4 RainScrub(',1)[1])
for k,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+k)
sys.exit(0 if all(checks.values()) else 1)
