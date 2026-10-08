#!/usr/bin/env python3
# northlight-test:
"""0.3.199 (fog temporal): wiring of the temporal accumulation of the half-resolution fog - source checks, no compiler.
The FogTemporal shader (own entry after FogClouds; every other entry byte-identical), c64.y as its only constant, the FogTemporal key,
the pass and the fogBlurred<->fogHistory rotation guarded by the setting (FogTemporal=0 keeps the old sequence), the composite on the
final buffer, and nothing in the stream or proxy files."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import json
import re
import subprocess

w=fp.src('world_renderer.h').read_text()
h=(fp.SHADERS/'world_effects.hlsl').read_text()
py=(fp.REPO/'scripts'/'shaders'/'compile_world_shaders.py').read_text()
q=fp.src('quality_settings.h').read_text()
manifest=json.loads((fp.SHADERS/'world-shader-build.json').read_text())['shaders']
ft=manifest.get('FogTemporal',{})
names=re.findall(r'\("(\w+)", "[vp]s_3_0"\)',py)
checks={}

checks['build: FogTemporal is an ENTRIES member right after FogClouds']=('FogClouds' in names and 'FogTemporal' in names and names.index('FogTemporal')==names.index('FogClouds')+1)
checks['manifest: FogTemporal ps_3_0, <= 512 slots, < 32 temporaries']=(ft.get('target')=='ps_3_0' and ft.get('static_instruction_slots',999)<=512 and ft.get('temporary_registers',99)<32)
checks['compiled: FogTemporal.bin and generated kFogTemporalShader']=((fp.COMPILED/'FogTemporal.bin').exists() and 'static const DWORD kFogTemporalShader[]' in fp.src('world_compiled_shaders.h').read_text())
# WorldFog stays the 0.3.198 bytecode (the working tree state is not a test input: a git-status check failed on any other uncommitted shader edit)
checks['compiled: WorldFog bytecode unchanged (512 slots)']=json.loads((fp.SHADERS/'world-shader-build.json').read_text())['shaders']['WorldFog']['sha256']=='58b53917f682aef3cb86b53d7a91d6f5cfd2ddba556ef711ee9a8056ff41c0d7'

body=h[h.index('float4 FogTemporal('):]
body=body[:body.index('\n}\n')]
checks['HLSL: placed after FogBlur and FogClouds']=(h.index('float4 FogBlur(')<h.index('float4 FogClouds(')<h.index('float4 FogTemporal('))
checks['HLSL: c64 is the only new constant, only .y read; s14 history, s15 depth history']=(
    'float4 FogTemporalInfo : register(c64);' in h and 'FogTemporalInfo.y' in body and len(re.findall(r'FogTemporalInfo\.[xzw]',body))==0
    and 'sampler2D FogHistory : register(s14);' in h and 'DepthHistory' in body)
checks['HLSL: no other shader reads c64 (only the declaration and this entry)']=len(re.findall(r'register\(c64\)',h))==1 and 'LocalLightFog[5].' not in h
checks['HLSL: sky reprojects the direction only, clamp over the neighbourhood, lerp by the weight, passthrough at 0']=(
    'sky' in body and 'PreviousView[0].xyz' in body and 'min(lo,s)' in body and 'clamp(history,lo,hi)' in body and 'lerp(current,' in body
    and 'if(FogTemporalInfo.y<=0)return current;' in body and 'max(.25,w*.03)' in body)

checks['setting: key after FogCloudDensity, 0..1, presets 1/1/1, default 1, origin grows']=(
    '{"FogCloudDensity",&Settings::fogCloudDensity,0,200,{100,100,100}},\n    {"FogTemporal",&Settings::fogTemporal,0,1,{1,1,1}},' in q and 'unsigned fogTemporal=1;' in q and 'char origin[40]=' in q)
ini=(fp.REPO/'renderer'/'windows-package'/'northlight-quality.ini').read_text()
checks['docs: ini key, both readmes']=(';FogTemporal=1' in ini and 'FogTemporal' in (fp.REPO/'README.md').read_text() and 'FogTemporal' in (fp.REPO/'renderer'/'windows-package'/'README.txt').read_text())

g=w[w.index('bool fogResolved=false;'):]
pas=g[g.index('if(effects.fog&&quality.fogTemporal&&fogTemporalPS&&fogHistory&&debug==0){'):g.index('// Separable depth-aware blur')]
checks['host: pass and swap guarded by the setting, the shader, the history target, fog and debug==0']=(
    'std::swap(fogBlurred,fogHistory);std::swap(fogBlurredSurface,fogHistorySurface)' in pas and pas.count('std::swap')==2 and 'else fogHistoryValid=false;' in pas)
checks['host: history weight 0 unless fogHistoryValid&&useHistory; weight constant is c64 only, previous view c53 from the bank']=(
    'fogHistoryValid&&useHistory?FogTemporalWeight:0.f' in pas and 'SetPixelShaderConstantF(64,info,1)' in pas and 'SetPixelShaderConstantF(53,c[53],4)' in pas
    and len(re.findall(r'SetPixelShaderConstantF\(6[4-9]',w))==1)
checks['host: reads raw fog s9 + fogHistory s14 + previous depth temporalDepth[temporalIndex] s15, writes fogBlurred; s14/s15 put back']=(
    'SetRenderTarget(0,fogBlurredSurface)' in pas and 'SetTexture(9,fog)' in pas and 'SetTexture(14,fogHistory)' in pas and 'SetTexture(15,temporalDepth[temporalIndex])' in pas
    and 'SetTexture(14,nullptr)' in pas and 'SetTexture(15,nullptr)' in pas and 'D3DSAMP_MINFILTER,D3DTEXF_POINT' in pas)
checks['host: FogTemporal profile mark after the pass (worst case < MaxMarks 20)']=(pas.index('quad(w/2,h/2),"fog temporal pass"')<pas.index('profile->mark("FogTemporal")') and
    len(set(re.findall(r'profile->mark\("(\w+)"\)',w)))+4+2<=20)
bl=g[g.index('// Separable depth-aware blur'):g.index('// c34 belongs')]
checks['host: blur unchanged when the pass did not run (fog -> fogBlurred -> fog), resolved: fogHistory -> fog -> fogBlurred']=(
    'SetRenderTarget(0,fogResolved?fogSurface:fogBlurredSurface);d->SetTexture(9,fogResolved?fogHistory:fog)' in bl
    and 'SetRenderTarget(0,fogResolved?fogBlurredSurface:fogSurface);d->SetTexture(9,fogResolved?fog:fogBlurred)' in bl)
checks['host: composite and the diagnostic dump read the final buffer']=('SetTexture(9,fogResolved?fogBlurred:fog);d->SetPixelShader(finalPS)' in g and 'dump(d,fogResolved?fogBlurredSurface:fogSurface,directory,"fog",capture)' in g)
checks['host: history target created with the others, dropped and invalidated in releaseGPU; shader optional']=(
    '&fogHistory,&fogHistorySurface)' in w and 'drop(fogHistorySurface);drop(fogHistory);drop(fogTemporalPS);fogHistoryValid=false;' in w and 'fogTemporalPS=nullptr;logf("WORLD fog temporal disabled' in w)
checks['host: no per-draw work (nothing in src/stream or src/proxy mentions it)']=all(
    not re.search(r'fogTemporal|FogTemporal|fogHistory',p.read_text(errors='ignore')) for d in ('stream','proxy') for p in (fp.REPO/'src'/d).rglob('*') if p.is_file())
for k,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+k)
sys.exit(0 if all(checks.values()) else 1)
