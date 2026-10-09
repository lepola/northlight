#!/usr/bin/env python3
"""0.3.198 (rain): wiring of the weather couplings - source checks, no compiler.
The new shader constants live in c59.zw (LocalLightFog[0].zw: only .x of c59..c66 is read anywhere) and are documented in the register
block; c23 (LightCenter, copied from the camera) and GridInfo.z (a divisor in LocalDirect and smoothRemoval) are untouched; the CPU copy of
the air extinction (sigmaAt) mirrors the shader's; the lighting alpha and the baseline alpha keep the true visibility; no new pass is added
to render() beyond the fog-cloud pass; every coupling multiplies by a gain that is exactly 1 (or adds exactly 0) without weather; the settings reach the derivation."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re

w=fp.src('world_renderer.h').read_text()
h=(fp.SHADERS/'world_effects.hlsl').read_text()
hz=fp.src('horizon_haze.h').read_text()
dr=fp.src('celestial_disc_renderer.h').read_text()
rc=fp.src('renderer.cpp').read_text()
we=fp.src('weather_effects.h').read_text()
checks={}

# c59.zw
checks['hlsl: WeatherInfo at c59, documented (z shadows, w air extinction), LocalLightFog still read only through .x']=(
    'float4 WeatherInfo : register(c59);' in h and 'z direct shadow softening (WorldLighting), w the shared air extinction' in h
    and 'free: only .x is read' in h and re.findall(r'LocalLightFog\[[^\]]*\]\.(\w+)',re.sub(r'//[^\n]*','',h))==['x'] and 'LocalLightFog[i].x' in h)
checks['hlsl: no other register c59..c66 reader']=re.findall(r'register\(c6[0-6]\)',h)==['register(c60)','register(c64)','register(c64)'] and h.count('register(c59)')==2 # 0.3.199 (fog clouds): CloudInfo[4] at c60 (.yzw only), see test_fog_clouds_wiring; (fog temporal): FogTemporalInfo at c64 (.y only), see test_fog_temporal_wiring; 0.3.200 (gpu budget): FogStepInfo at c64 (.z, WorldFog), see test_gpu_budget_wiring
checks['hlsl: WorldLighting and WorldFog read WeatherInfo (z, w)']=all(
    ('WeatherInfo.'+c) in h for c in 'zw') and len(re.findall(r'WeatherInfo\.x',h))==0
checks['cpp: c59.zw set in the bank, c59.x and c60..c66 not']=('c[59][2]=wx.shadowSoften();c[59][3]=airFloor;' in w and 'c[59][0]' not in w and 'c[59][1]' not in w and set(re.findall(r'c\[(6[0-6])\]',w))<={'60','61','62','63','64'}) # 0.3.199 (fog clouds): c60..c63 .yzw belong to CloudInfo; 0.3.200 (gpu budget): c60.x and c64.z the march intervals
checks['cpp: c23 untouched']=w.count('memcpy(c[23],context.camera,12);')==1 and 'c[23][' not in w
checks['cpp: GridInfo.z (c20.z) unchanged']='c[20][2]=.85f;' in w and re.findall(r'c\[20\]\[2\]=[^;]*;',w)==['c[20][2]=.85f;']
# identity at zero weather
checks['cpp: every coupling is a gain (x1) or an addend (+0) from the one derived Frame']=all(s in w for s in (
    'const auto wx=weatherEffects();','c[58][2]=10.f*lampGain*wx.lampFogGain();','c[58][3]=.12f*lampGain*wx.lampFogGain();','c[18][3]+=wx.ambientLift();',
    'c[21][1]=1.2f*wx.shaftGain();','c[21][1]=1.2f*volumePalette.fogGain[source]*wx.shaftGain();','hazeLift,wx.hazeTauScale());','const float airFloor=.0017f+wx.airExtinction()*denseDamp;'))
checks['cpp: no other 1.2f volume gain site']=w.count('c[21][1]=')==2
checks['haze: tauScale defaults to 1 and multiplies the optical depth only']=('float tauScale=1.f)' in hz and '*.01f*scale*(std::isfinite(tauScale)&&tauScale>0?tauScale:1.f);' in hz)
checks['hlsl: WorldFog takes the air floor from c59.w (the old literal .0017 moved into the constant)']=('airBase=WeatherInfo.w+airBase*saturate(mad(field.w,1.6,-1));' in h and '.0017+airBase' not in h)
checks['cpp: sigmaAt mirrors the extra extinction through the same airFloor']=('airBase=airFloor+airBase*std::clamp((t.height-.625f)/.625f,0.f,1.f);' in w and '.0017f+airBase' not in w and w.index('const float airFloor=')<w.index('auto sigmaAt='))
# shadows
lt=h[h.index('LightingOutput WorldLighting('):h.index('// Second pass to the same FP16 lighting buffer')]
checks['hlsl WorldLighting: only the direct replacement is softened, behind a branch that is off when dry']=(
    'if(WeatherInfo.z>0){moonShadow=lerp(1,shadow,1-WeatherInfo.z);sunVisibility=lerp(1,shadow,GridInfo.z*(1-WeatherInfo.z));}' in lt
    and 'float3 moon=DirectLight.rgb*saturate(dot(n,SunDirection.xyz))*moonShadow;' in lt and 'float3 sun=DirectLight.rgb*saturate(dot(n,LegacyDirection.xyz))*sunVisibility;' in lt)
checks['hlsl WorldLighting: correction.a and baseline.a keep the true visibility']=(
    'o.correction=float4(replacement-painted,lerp(visibility,shadow,SunDirection.w)*SourcePolicy.y);' in lt
    and 'o.baseline=float4(max(old+AmbientLight.rgb,.15),lerp(visibility,shadow,SunDirection.w)*known);' in lt and 'float visibility=lerp(1,shadow,GridInfo.z);' in lt)
checks['hlsl: GridInfo.z still the divisor of the removal']=('/GridInfo.z' in h and 'max(GridInfo.z,.001)' in h)
checks['hlsl: c24.y (softness) still unread']='PassInfo.y' not in h.replace('softness,debug','')
# WorldGI carries no weather code (0.3.198 rebase onto task 13: it is at 31 temps)
gi=h[h.index('float4 WorldGI('):h.index('// Four POINT reads work')]
checks['hlsl WorldGI: no weather code (it is at 31 temps)']='WeatherInfo' not in gi
checks['hlsl: no WorldWet shader; WeatherInfo.y only the fog clouds\' folded bank top (0.3.199 optimisation)']='WorldWet' not in h and h.count('WeatherInfo.y')==1 and 'mad(nL,CloudInfo[2].z,WeatherInfo.y)' in h
checks['render: the weather adds no pass of its own (quad count 16, shader creations 22 with the 0.3.199 fog clouds and fog temporal and the 0.3.203 debug composite)']=(w.count('quad(')==16 and w.count('CreatePixelShader')==22) and 'wetPS' not in w
# disc and veil
checks['discs: gain atomic, default 1, opacity and glare weights only, veil inherits']=(
    'std::atomic<float> weatherGain{1.f};' in dr and 'c[9][3]=glare?disc.opacity:disc.opacity*weatherGain.load(std::memory_order_relaxed);' in dr
    and 'c[12][2]*=gain;c[47][3]*=gain;' in dr and 'veilStrength(c[9][3],1,hazeAtSun,skyTransmittance)' in dr)
checks['renderer.cpp: the gain is set once per frame next to the disc render, from the world']='celestialDiscs->setWeatherGain(world->weatherEffects().discGain());' in rc and rc.count('setWeatherGain')==1
# settings
checks['WorldRenderer: settings accessors and the derived frame']=all(s in w for s in ('unsigned weatherSetting()const{return quality.weather;}','unsigned rainFogSetting()const{return quality.rainFog;}',
    'derive(weatherState,quality.weather,quality.rainFog)')) and 'rainWetness' not in w
checks['weather_effects.h: coefficient table']=all(s in we for s in ('kSnowFog=0.6f','kHazeTau=0.75f','kAirExtinction=0.0012f','kShafts=0.75f','kDisc=0.85f','kShadowSoften=0.5f','kLampFog=0.f','kAmbientLift=0.25f'))
checks['probe radiance is not touched (no GI re-solve: skyRadiance sites as before)']='skyRadiance' not in we and w.count('skyRadiance')==3
for k,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+k)
sys.exit(0 if all(checks.values()) else 1)
