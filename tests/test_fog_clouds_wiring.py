#!/usr/bin/env python3
# northlight-test:
"""0.3.199 (fog clouds): wiring of the moving fog banks - source checks, no compiler.
The FogClouds shader (own entry after FogBlur; WorldFog and every other entry byte-identical), the c60..c63 CloudInfo block (.yzw only),
the pass guarded by the derived frame with every state the later passes read put back, the CPU sigmaAt mirror under the same guard,
and nothing new on the game thread (stream hooks, proxies, snapshots)."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import json
import re

w=fp.src('world_renderer.h').read_text()
h=(fp.SHADERS/'world_effects.hlsl').read_text()
py=(fp.REPO/'scripts'/'shaders'/'compile_world_shaders.py').read_text()
manifest=json.loads((fp.SHADERS/'world-shader-build.json').read_text())['shaders']
checks={}
WORLD_FOG_SHA='0436709c5843d1179287da4efc946f4a448f7d233e81ac2068fe54bc47432f6c' # 0.3.200 (gpu budget) bytecode (the interval as mad with c64.z), 512 slots
fc=manifest.get('FogClouds',{})
names=re.findall(r'\("(\w+)", "[vp]s_3_0"\)',py)

# shader entry and build
checks['build: FogClouds is an ENTRIES member placed right after FogBlur']=('FogBlur' in names and 'FogClouds' in names and names.index('FogClouds')==names.index('FogBlur')+1)
checks['manifest: FogClouds ps_3_0, <= 512 slots, < 32 temporaries, samples s14 (the volume)']=(fc.get('target')=='ps_3_0' and fc.get('static_instruction_slots',999)<=512 and fc.get('temporary_registers',99)<32 and 14 in fc.get('samplers',[]))
checks['manifest: WorldFog bytecode unchanged (sha, 512 slots)']=(manifest['WorldFog']['sha256']==WORLD_FOG_SHA and manifest['WorldFog']['static_instruction_slots']==512)
checks['manifest: s14 is read by no fog-stage entry other than FogClouds (FogBlur, WorldComposite, LocalFog, WorldFog)']=all(14 not in manifest[n]['samplers'] for n in ('FogBlur','WorldComposite','LocalFog','WorldFog'))
checks['compiled: FogClouds.bin exists']=(fp.COMPILED/'FogClouds.bin').exists()
gen=fp.src('world_compiled_shaders.h').read_text()
checks['generated header: kFogCloudsShader']='static const DWORD kFogCloudsShader[]' in gen

# HLSL
code=re.sub(r'//[^\n]*','',h)
checks['hlsl: CloudInfo[4] at c60 and sampler3D CloudNoise at s14']=('float4 CloudInfo[4] : register(c60);' in h and 'sampler3D CloudNoise : register(s14);' in h)
checks['hlsl: CloudInfo read by component only; .x only c61.x / c62.x / c63.x (the bank values until the lamp fog batches overwrite them) and 0.3.200 c60.x (the interval)']=(
    set(re.findall(r'CloudInfo\[(\d)\]\.x',code))<={'0','1','2','3'} and code.count('CloudInfo[0].x')==1 and 'CloudInfo[0].yzw' in code and 'CloudInfo[1].yzw' in code and not re.search(r'CloudInfo(\[\d\])?[^.\w\[]',code.replace('float4 CloudInfo[4]','')))
checks['hlsl: the .x ownership is documented at the declaration']='overwrite c59..c66 .x with LocalLightFog' in h
i=h.index('float4 FogBlur(');j=h.index('float4 FogClouds(');k=h.index('// Distant haze toward the WORLD horizon')
checks['hlsl: FogClouds sits after FogBlur and before the horizon haze section (WorldFog slice for test_fog_motion unchanged)']=i<j<k and h.index('float4 WorldFog(')<h.index('// Glow of')<i
body=h[j:h.index('// 0.3.199 (fog temporal): temporal accumulation')] # 0.3.199 (fog temporal): FogTemporal follows FogClouds
checks['hlsl: no jitter or history, 40 world-fixed intervals (0.3.200: fewer at reduced GpuBudgetMs levels via c60.x), returns (0,0,0,1) with no cloud']=('mad(FogInfo.w,1.0/40,CloudInfo[0].x)' in body and 'i<41' in body and 'the host draws this pass only when c21.x >= .5' in body and 'jitter' not in body and 'frac(major.y*(major.x<0?-1:1)/spacing)' in body)
checks['hlsl: the cloud sigma matches the CPU sigma (.65/.35 mix, quantile threshold x sharpness ramp, squared height falloff, zone .7, sigmaMax, field coverage x near fade)']=all(s in body for s in (
    'saturate(mad(nL,CloudInfo[3].w,mad(nS,CloudInfo[3].x,CloudInfo[2].y)))','saturate(1-altitude/max(mad(nL,CloudInfo[2].z,WeatherInfo.y),.001))','mad(saturate(mad(field.w,1/3.75,-1.25/3.75)),-.3,1)',
    '[branch]if(min(valid,CloudInfo[2].x-altitude)>0){','[branch]if(nL>CloudInfo[1].x){','CloudInfo[2].w*valid*nearFade','tex3Dlod(CloudNoise,float4(rel*CloudInfo[3].y+CloudInfo[0].yzw,0))','tex3Dlod(CloudNoise,float4(rel*CloudInfo[3].z+CloudInfo[1].yzw,0))'))
checks['hlsl: direct part soft-capped by FogInfo.z, shadowed by fogShadow, branch only inside a cloud']=('cap=max(FogInfo.z,.0001)' in body and 'fogShadow(p)' in body and '[branch]if(sigma>0)' in body)

wf=h[h.index('float4 WorldFog('):h.index('// Glow of')]
exact=['float profile=saturate((field.w-2.5)/2.5);','float groundHeight=lerp(field.w,6,RegionalFogInfo.w*(1-profile));','float vertical=saturate(1-altitude/max(groundHeight,.001));',
    'float groundSigma=max(field.y+field.z*RegionalFogInfo.w,0)*vertical*vertical;','float altitude=p.z-field.x;','float valid=altitude>=0?coverage:0;',
    'float nearFade=saturate(((start+end)*.5-FogRange.x)*FogRange.y);nearFade*=nearFade*(3-2*nearFade);'.replace('((start+end)*.5-FogRange.x)','(mid-FogRange.x)')]
checks['hlsl: FogClouds copies WorldFog\'s groundSigma lines (profile, groundHeight, vertical, groundSigma), altitude and valid exactly; nearFade with mid for (start+end)*.5']=all(l in body and l.replace('(mid-FogRange.x)','((start+end)*.5-FogRange.x)') in wf for l in exact)
checks['hlsl: base fog only attenuates the cloud light (tBase; the ambient does not carry it: the blend already multiplies the fog by tCloud), alpha out stays tCloud; air term is the documented constant floor WeatherInfo.w (slot budget)']=(
    'tauBase+=baseSigma*stepSize;' in body and 'float weight=tCloud*absorb;' in body and 'directWeight+=exp(-tauBase)*weight*fogShadow(p)*heightFade;' in body and 'ambientWeight+=weight;' in body and 'float airSigma=WeatherInfo.w;' in body and 'for the slot budget' in body and body.rstrip().endswith('directScatter,tCloud);\n}'.rstrip()))
# renderer wiring
checks['cpp: noise generated off the game and render threads on first need (std::thread, detached, leaked holder), logged once']=(
    'std::thread([this]' in w and '.detach()' in w and 'new FogCloudNoise' in w and 'WORLD fog clouds noise ms=%.1f' in w and 'fogCloudNoise().request()' in w)
checks['cpp: volume created lazily, L8 with an A8R8G8B8 fallback, honours the pitches and the memory guard, one failure disables the clouds only']=(
    'D3DFMT_L8' in w and 'D3DFMT_A8R8G8B8,D3DPOOL_MANAGED' in w and 'box.SlicePitch' in w and 'box.RowPitch' in w and 'memoryPressure' in w[w.index('bool ensureCloudNoise()'):w.index('bool ensureCloudNoise()')+600] and 'cloudNoiseFailed=true' in w)
checks['cpp: shader created non-fatally, shader and volume released with the others']=(
    'CreatePixelShader(kFogCloudsShader,&fogCloudsPS)' in w and 'drop(fogCloudsPS);drop(cloudNoise);' in w)
checks['cpp: frame derived from the settings; active only with the fog effect, no debug view, noise and shader ready']=(
    'NorthlightFogClouds::derive(quality.fogClouds,unsigned(std::lround(float(quality.fogCloudDensity)*denseDamp)),wx.fog,c[31][3],cloudWind,context.camera,fogCloudNoise().ready.load(std::memory_order_acquire)?&fogCloudNoise().quantiles:nullptr,cloudLush)' in w and
    'cf.active=cf.active&&effects.fog&&debug==0&&fogCloudsPS&&ensureCloudNoise();' in w and w.index('c[31][3]=NorthlightRegionalFog::nightFactor')<w.index('NorthlightFogClouds::derive('))
checks['cpp: c59.y and c60..c63 written only while active, by shaderConstants (0.3.200: c60.x, the interval, only at a reduced level and while active)']=(
    w.count('if(cf.active){\n            NorthlightFogClouds::shaderConstants(cf,&c[59]);')==1 and re.findall(r'c\[(6[0-3])\]\[(\d)\]=',w)==[('60','0')] and 'if(gpuBudgetLevel){if(cf.active)c[60][0]=NorthlightGpuBudget::spacingDelta(' in w and 'c[59][1]' not in w)
# the pass
a=w.index('if(cf.active&&c[21][0]>=.5f){');b=w.index('d->SetPixelShaderConstantF(17,c[17],2);',a);c_=w.index('if(localDirectCount&&debug==0){',b)
pas=w[a:b]
checks['cpp: the pass is guarded by cf.active and the field-ready flag, between the source loop and the c17/c18 restore (before lamp fog)']=(
    w.index('"celestial volumetric raymarch"')<a<b<c_ and w.count('d->SetPixelShader(fogCloudsPS)')==1 and w.count('"fog clouds raymarch"')==1)
checks['cpp: the pass draws with ONE / SRCALPHA colour and ZERO / SRCALPHA alpha, volume on s14 WRAP + LINEAR, first source']=all(s in pas for s in (
    'setSource(firstSource,true,true);','SetTexture(14,cloudNoise)','D3DSAMP_ADDRESSW,D3DTADDRESS_WRAP','D3DRS_DESTBLEND,D3DBLEND_SRCALPHA','D3DRS_SEPARATEALPHABLENDENABLE,TRUE','D3DRS_SRCBLENDALPHA,D3DBLEND_ZERO','D3DRS_DESTBLENDALPHA,D3DBLEND_SRCALPHA'))
checks['cpp: states restored after the pass: source state (setSource of the loop\'s last source unless it is the pass\'s own), ONE/ONE/ADD, separate alpha off (its operands then unused), blend enable, write mask, fogPS, s14']=all(s in pas for s in (
    'const bool sameSource=lastFogSource==firstSource&&lastFogFirst;','if(!sameSource)setSource(firstSource,true,true);','if(!sameSource)setSource(lastFogSource,lastFogFirst,true);','D3DRS_SRCBLEND,D3DBLEND_ONE','D3DRS_DESTBLEND,D3DBLEND_ONE','D3DRS_BLENDOP,D3DBLENDOP_ADD','D3DRS_SEPARATEALPHABLENDENABLE,FALSE','D3DRS_ALPHABLENDENABLE,!lastFogFirst','D3DRS_COLORWRITEENABLE,lastFogFirst?15:7','SetPixelShader(fogPS)',
    'SetTexture(14,nullptr)','D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP','D3DSAMP_MINFILTER,D3DTEXF_POINT')) and pas.index('setSource(lastFogSource')<pas.rindex('SetTexture(14,nullptr)') and 'GetRenderState' not in pas
checks['cpp: the source loop records the state its last iteration left']='lastFogSource=source;lastFogFirst=first;first=false;' in w
checks['cpp: profile marks FogMarch / FogClouds only with the pass (<= 20 marks per frame stays true: 16 existing names + 2)']=(pas.count('profile->mark("FogMarch")')==1 and pas.count('profile->mark("FogClouds")')==1)
# sigmaAt mirror
mirror=[l for l in w.splitlines() if 'NorthlightFogClouds::sigmaAt(' in l]
checks['cpp: sigmaAt adds the cloud term only under cf.active, to the base value, inside the t.height>0 branch']=(
    len(mirror)==1 and mirror[0].strip().startswith('if(clouds&&cf.active){') and 'sigma+=NorthlightFogClouds::sigmaAt(cloudData,cf,context.camera,point,t.ground,t.height,1.f)' in mirror[0] and
    w.index('if(t.height>0){')<w.index(mirror[0])<w.index('return sigma;'))
# 0.3.199 (fog clouds): lush zones - the camera texel's CPU-only lush flag, smoothed; the field's lush comes from lushZone (forests, grass, Duskwood, STV, Mulgore, Stormwind), outdoors only
rf=fp.src('regional_fog.h').read_text()
checks['cpp: lush at the camera texel, smoothed, unchanged indoors']='lushTarget=cloudLush;' in w and 'if(t.height>0)lushTarget=f.lush[k]?1.f:0.f;' in w and 'cloudLush=NorthlightFogClouds::smoothDense(cloudLush,lushTarget,cloudDt);' in w
checks['regional fog: lush flag per texel, not in the GPU texel']='out.lush[i]=!indoors&&lushZone(zone);' in rf and 'inline bool lushZone(uint32_t zone){return zone==10||zone==33||zone==215||zone==1519||forestZone(zone)||grassZone(zone);}' in rf and 'static_assert(sizeof(Texel)==16' in rf
checks['cpp: measurement line only on profile-sampled frames']='if(profileSampled())logf("WORLD fog clouds active=%d coverage=%.3f threshold=%.3f height=%.1f speed=%.2f dir=(%.2f %.2f) sigmaMax=%.4f lush=%.2f noiseReady=%d"' in w

# nothing on the game thread
import os
stream=[fp.src('stream_device.h'),fp.src('stream_proxies.h'),fp.src('game_snapshot.h')]
checks['game thread untouched: no FogClouds / fog_clouds in stream_device.h, stream_proxies.h, game_snapshot.h']=all(('FogClouds' not in t.read_text() and 'fog_clouds' not in t.read_text() and 'fogClouds' not in t.read_text()) for t in stream)
# review fix: derive() is inactive until the quantile table exists, so the generation request must not depend on cf.active (it never started)
req=[l for l in w.splitlines() if 'fogCloudNoise().request()' in l]
checks['noise request: gated by the settings only (never by cf.active), before derive']=(len(req)==1 and 'cf.active' not in req[0] and 'quality.fogClouds&&quality.fogCloudDensity&&effects.fog&&debug==0' in req[0]
    and w.index('fogCloudNoise().request()')<w.index('NorthlightFogClouds::derive('))
# game test fix: the banks take the game's fog colour (c26) when it is validated (c25.w), not the near-black ambient*.35 air radiance
checks['colour: cloud ambient is the brighter of the air radiance and the validated game fog colour (never darker than the fog it hides)']=('max(max(AmbientLight.rgb,0)*.35,LegacyFogColor.rgb*LegacyFog.w)*ambientWeight' in h.split('float4 FogClouds(',1)[1].split('\n}\n',1)[0])
# night game test: the banks' colour goes in c25.w/c26 for the cloud pass only and the bank's values come back before the lamp fog
cp=w.split('if(cf.active&&c[21][0]>=.5f){',1)[1].split('d->SetPixelShaderConstantF(17,c[17],2);',1)[0]
checks['colour: c25/c26 set for the cloud pass from NorthlightFogClouds::colour, restored from the bank (c[25], 2 registers) after it']=(
    'NorthlightFogClouds::colour(c[26],c[25][3]>=.5f,c[31][3],cloudColour)' in cp and 'd->SetPixelShaderConstantF(26,cloudColour,1)' in cp
    and cp.index('fog clouds raymarch')<cp.index('d->SetPixelShaderConstantF(25,c[25],2)'))
# Duskwood/lamp game test: the rain's extra air and the cloud density thin out in dense zones; the lamp glow ignores the clouds
checks['dense zones: airFloor = .0017f + rain extra x denseDamp, cloud density x denseDamp, lamps sigmaAt without clouds']=(
    'const float airFloor=.0017f+wx.airExtinction()*denseDamp;' in w and 'unsigned(std::lround(float(quality.fogCloudDensity)*denseDamp))' in w
    and 'localLights.position[i][2],false);' in w and 'ray.z*t,true)' in w and 'if(clouds&&cf.active)' in w)
# lamp glow game test: the glow fades in over the same near ramp as the air, at the ray's closest approach to the light
lf=h.split('float4 LocalFog(',1)[1].split('\n}\n',1)[0]
checks['LocalFog: smooth near ramp at the closest approach (no hard FogRange.x cut of the glow core)']=(
    'float nearGlow=saturate((clamp(-b,t0,t1)-FogRange.x)*FogRange.y);nearGlow*=nearGlow*(3-2*nearGlow);' in lf and 'integral*LocalLightFog[i].x*nearGlow' in lf)
for k,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+k)
sys.exit(0 if all(checks.values()) else 1)
