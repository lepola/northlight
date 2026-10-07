#!/usr/bin/env python3
"""0.3.198 (rain): wiring of the weather couplings - source checks, no compiler.
The new shader constants live in c59.yzw (LocalLightFog[0].yzw: only .x of c59..c66 is read anywhere) and are documented in the register
block; c23 (LightCenter, copied from the camera) and GridInfo.z (a divisor in LocalDirect and smoothRemoval) are untouched; the CPU copy of
the air extinction (sigmaAt) mirrors the shader's; the lighting alpha and the baseline alpha keep the true visibility; no new pass is added
to render() except the wet-ground pass; every coupling multiplies by a gain that is exactly 1 (or adds exactly 0) without weather; the settings reach the derivation."""
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

# c59.yzw
checks['hlsl: WeatherInfo at c59, documented (y wetness, z shadows, w air extinction), LocalLightFog still read only through .x']=(
    'float4 WeatherInfo : register(c59);' in h and 'y wetness (WorldWet), z direct shadow softening (WorldLighting), w the shared air extinction' in h
    and 'free: only .x is read' in h and re.findall(r'LocalLightFog\[[^\]]*\]\.(\w+)',re.sub(r'//[^\n]*','',h))==['x'] and 'LocalLightFog[i].x' in h)
checks['hlsl: no other register c59..c66 reader']=re.findall(r'register\(c6[0-6]\)',h)==['register(c60)'] and h.count('register(c59)')==2 # 0.3.199 (fog clouds): CloudInfo[4] at c60 (.yzw only), see test_fog_clouds_wiring
checks['hlsl: WorldLighting and WorldFog read WeatherInfo (z, w); y is read by WorldWet only']=all(
    ('WeatherInfo.'+c) in h for c in 'zw') and len(re.findall(r'WeatherInfo\.x',h))==0
checks['cpp: c59.yzw set in the bank, c59.x and c60..c66 not']=('c[59][1]=wx.wet;c[59][2]=wx.shadowSoften();c[59][3]=airFloor;' in w and 'c[59][0]' not in w and set(re.findall(r'c\[(6[0-6])\]',w))<={'60','61','62','63'}) # 0.3.199 (fog clouds): c60..c63 .yzw belong to CloudInfo
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
# wetness: its own pass after WorldGI (0.3.198 rebase onto task 13: WorldGI is at 31 temps, WorldLighting at 460/512 slots)
gi=h[h.index('float4 WorldGI('):h.index('float4 WorldWet(')]
wet=h[h.index('float4 WorldWet('):h.index('// Four POINT reads work')]
checks['hlsl WorldGI: no weather code (it is at 31 temps)']='WeatherInfo' not in gi
checks['hlsl WorldWet: reads WeatherInfo.y, and only y']=re.findall(r'WeatherInfo\.(\w)',wet)==['y']
checks['hlsl WorldWet: sky, water and no-depth return 0 like WorldGI']='if(d>=.99999||waterDistance(uv,d)>0)return 0;' in wet
checks['hlsl WorldWet: openness is the +Z moment of metadata-validated probes of the layer above, blended bilinearly over the 2x2 neighbours with the residency fade']=(
    '(wrapped.y+4*GridInfo.x+.5)/(GridInfo.x*6)' in wet and 'if(all(metadata.xyz==cell)&&metadata.w>=0){' in wet and 'saturate((above.x-24)/48)*above.z*weight' in wet
    and 'saturate((PassInfo.w-metadata.w)*(1/.45))' in wet and 'lerp(1-fraction,fraction,bit)' in wet and 'base.z+=1' in wet and 'GridInfo.w>=.5' in wet
    and 'open=total>.00001?open/total:0;' in wet)
checks['hlsl WorldWet: a probe counts only if its -Z first hit reaches the surface (low ceilings, walls), Chebyshev-weighted']=(
    '(wrapped.y+5*GridInfo.x+.5)/(GridInfo.x*6)' in wet and 'float need=cell.z*GridOrigin.w-p.z;' in wet and 'weight*=sees*sees*below.z;' in wet)
checks['hlsl WorldWet: wet clamped to 0..1, confidence-weighted, up-facing; darkens the ambient only (no direct term); rgb only (alpha 0); no irradiance read']=(
    'float wet=saturate(WeatherInfo.y*up*open*smoothNormal.w);' in wet and 'saturate((n.z-.55)/.35)' in wet and 'wet*AmbientLight.rgb*(fresnel*.15-.35)' in wet
    and 'LegacyDirect' not in wet and 'return float4(wet*AmbientLight.rgb*(fresnel*.15-.35),0);' in wet and 'ProbeR' not in wet and 'probeIrradiance' not in wet)
# no new passes beyond the wet pass
checks['render: exactly one pass added by 0.3.198 rain (WorldWet: quad count 14 -> 15, shader creations 19 -> 20; 0.3.199 fog clouds then 16 and 21)']=(w.count('quad(')==16 and w.count('CreatePixelShader')==21) # 0.3.199 (fog clouds): +1 pass (FogClouds), +1 shader
wd=w[w.index('if(wx.wet>0&&debug==0'):]
wd=wd[:wd.index('        if(localDirectCount')]
checks['cpp: the wet pass is drawn only when wx.wet>0, debug==0 and the probe grid is active (not gated on effects.gi)']=(
    'if(wx.wet>0&&debug==0&&c[20][3]>=.5f){' in wd and 'effects.gi' not in wd.split('{',1)[1] and 'SetPixelShader(wetPS)' in wd and 'world wet pass' in wd)
gp=w[w.index('d->SetPixelShader(giPS);'):w.index('if(wx.wet>0&&debug==0')]
checks['cpp: the wet pass follows the GI pass and inherits its additive RGB blend (ALPHABLENDENABLE TRUE, COLORWRITEENABLE 7)']=(
    'D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_COLORWRITEENABLE,7);d->SetPixelShader(giPS)' in w and 'world GI pass' in gp
    and 'D3DRS_ALPHABLENDENABLE' not in wd and 'D3DRS_COLORWRITEENABLE' not in wd)
checks['cpp: wetPS created and released with the other shaders']=('kWorldWetShader,&wetPS' in w and 'drop(wetPS);' in w)
# disc and veil
checks['discs: gain atomic, default 1, opacity and glare weights only, veil inherits']=(
    'std::atomic<float> weatherGain{1.f};' in dr and 'c[9][3]=glare?disc.opacity:disc.opacity*weatherGain.load(std::memory_order_relaxed);' in dr
    and 'c[12][2]*=gain;c[47][3]*=gain;' in dr and 'veilStrength(c[9][3],1,hazeAtSun,skyTransmittance)' in dr)
checks['renderer.cpp: the gain is set once per frame next to the disc render, from the world']='celestialDiscs->setWeatherGain(world->weatherEffects().discGain());' in rc and rc.count('setWeatherGain')==1
# settings
checks['WorldRenderer: settings accessors and the derived frame']=all(s in w for s in ('unsigned weatherSetting()const{return quality.weather;}','unsigned rainFogSetting()const{return quality.rainFog;}',
    'unsigned rainWetnessSetting()const{return quality.rainWetness;}','derive(weatherState,quality.weather,quality.rainFog,quality.rainWetness)'))
checks['weather_effects.h: coefficient table']=all(s in we for s in ('kSnowFog=0.6f','kHazeTau=0.75f','kAirExtinction=0.0012f','kShafts=0.75f','kDisc=0.85f','kShadowSoften=0.5f','kLampFog=0.f','kAmbientLift=0.25f'))
checks['probe radiance is not touched (no GI re-solve: skyRadiance sites as before)']='skyRadiance' not in we and w.count('skyRadiance')==3
for k,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+k)
sys.exit(0 if all(checks.values()) else 1)
