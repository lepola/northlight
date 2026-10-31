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

# sha256 of every other entry before the change (0.3.200 manifest); the entries the check compares are
# rebased to the one Microsoft d3dcompiler_47 build (Wine's builtin compiler is gone)
BEFORE={'WorldNormals':'e647de5f5ba48db3fa790f329731d066a47fa965fdf474dcc568c337f36fac34','WorldLighting':'066f20c71ece663605e7a17c0e1e37510d83dc6a968efa49b39f9afc990d55d3',
 'WorldGI':'98e33a0ff1a8412e7dd0d1801596cc4dc8f5dc2d4fde4880e4760a6eee267992','WorldFog':'69ab6e52d46455c0aac6bd74b07b25a892d658cdee55b8b688af033eff7f51d1',
 'FogBlur':'a058d8c3d631ff6f7b9a0f5d8a7c9a5290e927b673ef6bb0aabbe1af06ff005a','FogClouds':'99a7ae79e0907bedd2ad3675656e6fb307be04ddf4cb757322b361b8f3657292',
 'FogTemporal':'109d660cfb840966860cad5150ccb8111d4335c86a6c2d2800302878247f4cae','LocalDirect':'830bd4d6d2ef73a8141608b93773b1115d415e038b03476293a88cd3d909aff3',
 'RemovalSmooth':'485461e2b8a2bf2928a5819a8cd13527e1ee21203934aeae22187fb54b5e22c1','TemporalLight':'9392d2a1566c6f88d7a4e613fa67ff4fd5b4c58e1cd9edef0850637171b45574',
 'LocalFog':'bf1b7073635c9d064779277099aab540516464d3f0016a150e6666c639e973a7','SourceVisibilityPS':'92d8784a4ce86a5be232c60a311e5cd350586ab227a6285117970cf2bbc6a8b2',
 'WorldComposite':'1f3884872a55ec91f9ee42fd153aa988d16c81382de23cdd22b3347794240cd4','ShadowVS':'ef0d0273c0630c9992e0cdd40cb4d812cf58c26ad1a456c6a10b7d6c1f17962f',
 'ShadowCacheVS':'170f8b0774125a59a13c1ceb634bb9e1a4d586e803c05a2883ef1652cb95b3a5','ShadowPS':'33b4b97c97e1fe2834da7b06a7bd8a2ef0eaacf9966455e0e066ad801a15473f',
 'ShadowReplayPS':'2812775436581fd44ad92f3d69607792161ea8440e05aa2b9b8310b2155c0805','ShadowUnion':'43b59a16f449ca2c0b4e940dad1716a84f1f6eb21de4347a508ac3092cb9b479'}

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
# 0.3.205 (gh#20): LocalFog changed on purpose (it returns the raw batch sum and the capped peaks), LocalFogCombine is new
# 0.3.208: WorldNormals (colour-picked one-sided tangent side) changed on purpose
# 0.3.209 (task 29): WorldLighting reads the actor visibility (s12) on purpose, ActorShadow and ActorTiles are new
# task 43: RemovalSmooth branches around a zero-weight tap's reads on purpose (the same output)
checks['manifest: shaders outside the reviewed feature changes retain their historical hashes']=(set(manifest)==set(BEFORE)|{'LocalFogCombine', 'ActorTiles', 'ActorShadow', 'LocalDirect16'} and all(manifest[n]['sha256']==s for n,s in BEFORE.items() if n not in ('LocalDirect', 'LocalDirect16', 'FogClouds', 'LocalFog', 'RemovalSmooth', 'WorldComposite', 'WorldFog', 'WorldLighting', 'WorldNormals'))
    and manifest['LocalFog']['sha256']!=BEFORE['LocalFog'])
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
# The Microsoft d3dcompiler_47 build (rebased from the Wine builtin hashes)
EFF_BEFORE={'AO':'fa7a14b42bb3b1238fb440c91deebbc6dc85ad6be2b7770ade831e592e5826c6','AOContactBloom':'69f09b7b034b86e714fe56b0aa596fe0896b927940db9b0cc80ee98984545105','Composite':'36d90667788788500a5e0a6a7c8f100fe858ed9ec550dd489edb8fe5a6ca02f2'}
import hashlib
checks['MRT shaders: effects.hlsl untouched (the rain shaders have their own source, hashed in the manifest)']=('RainMaskMRT' not in (fp.SHADERS/'effects.hlsl').read_text() and json.loads((fp.SHADERS/'shader-build.json').read_text()).get('rain_source_sha256')==hashlib.sha256((fp.SHADERS/'rain_mask.hlsl').read_bytes()).hexdigest())
checks['MRT shaders: AO, AOContactBloom and Composite byte-identical to 0.3.201']=all(eff[n]['sha256']==x for n,x in EFF_BEFORE.items())
checks['MRT shaders: RainMaskMRT and RainScrub are ps_2_0, small, with a compiled .bin']=all(n in eff and eff[n]['target']=='ps_2_0' and eff[n]['static_instruction_slots']<=32 and (fp.COMPILED/(n+'.bin')).exists() for n in ('RainMaskMRT','RainScrub'))
ehl=(fp.SHADERS/'rain_mask.hlsl').read_text()
checks['MRT shaders: source writes oC1 = white with the streak alpha (RT1 masked to red); the scrub discards unchanged depth and outputs 0']=('out float4 mask : COLOR1' in ehl and 'mask = float4(1, 1, 1, c.a);' in ehl and 'clip(abs(tex2D(Scene, uv).r - tex2D(Depth, uv).r) - 1e-6);' in ehl and 'return 0;' in ehl.split('float4 RainScrub(',1)[1])
for k,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+k)
sys.exit(0 if all(checks.values()) else 1)
