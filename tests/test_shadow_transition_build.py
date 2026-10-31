#!/usr/bin/env python3
"""Issue 107: unchanged baseline, verified SM3 payloads and real consumer wiring."""
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
baseline = json.loads((Path(__file__).parent / 'fixtures/shadow_transition_baseline.json').read_text())
assert sha(fp.SHADERS / 'world_effects.hlsl') == baseline['world_effects_sha256']
for entry, digest in baseline['shaders'].items():
    assert sha(fp.COMPILED / f'{entry}.bin') == digest, entry
print(f"PASS all {len(baseline['shaders'])} baseline world shaders and HLSL unchanged")

manifest = json.loads((fp.SHADERS / 'shadow-transition-shader-build.json').read_text())
header = fp.src('shadow_transition_compiled_shaders.h').read_text()
for name in ('world_effects.hlsl', 'shadow_transition.hlsl'):
    assert manifest['sources'][name] == sha(fp.SHADERS / name)
for entry in ('WorldLightingFade', 'WorldFogFade', 'FogCloudsFade'):
    data = (fp.COMPILED / f'{entry}.bin').read_bytes()
    record = manifest['shaders'][entry]
    assert record['sha256'] == hashlib.sha256(data).hexdigest()
    assert record['target'] == 'ps_3_0' and record['static_instruction_slots'] <= 512 and record['temporary_registers'] <= 32
    body = re.search(r'k' + entry + r'Shader\[\] = \{(.*?)\};', header, re.S)[1]
    words = [int(word, 16) for word in re.findall(r'0x[0-9a-fA-F]+', body)]
    assert struct.pack('<' + 'I'*len(words), *words) == data
print('PASS all three transition sources, bytecode and embedded SM3 payloads')

w = fp.src('world_renderer.h').read_text()
h = fp.src('world_shadow_transition.inl').read_text()
render = w[w.index('    bool render(IDirect3DSurface9* targetSurface'):]
select = render.index('f.state.request(')
assert select < render.index('slotDirections[slot]=') < render.index('NorthlightActorShadowMap::matrix(')
assert select < render.index('cascadeFrames[source][cascade]=') < render.index('staticPlanKick(')
assert 'shadowFrame(cascadePivot,key.direction,radius)' in render
assert 'anchorNewMap||handover||teleported||travel||jump||!effects.shadows||!sourceActive[source]||!key.valid||shadowTransitionFailed' in render
assert 'wasSlicing=slices.active;if(reason)slices.reset();' in render
assert 'sliceBand||wasSlicing||slices.active||(reason&&!directionStep)' in render
assert render.index('if(action!=NorthlightQuality::CascadeAction::Render)') < render.index('preserveShadowStatic(slot)')
prepare = render.index('if(!transition.workingValid&&!transition.history&&!shadowTransitionFailed)shadowTransitionResources(slot);')
assert render.index('reuse.begin(interval);') < prepare < render.index('if(directionStep&&transition.state.canPreserveHistory()')
assert render.index('historyReady=preserveShadowStatic(slot)') < render.index('renderCache(shadowCacheSurface[slot],partialStatic,true)')
assert 'localSame&&staticSame' in render and 'staticSignature(oldMatrix)' in render
assert render.index('"shadow union"') < render.index('transition.state.commit(')
assert 'historyReady&&key.valid' in render and 'transition.working=cascadeFrames[source][cascade]' in render
assert 'for(auto& t:shadow)drop(t);for(auto& s:shadowSurface)drop(s);releaseShadowTransitions();' in w
assert 'void invalidateShadowCache(){resetShadowTransitions();staticCasters.settle();' in w
assert 'if(!next.shadows)releaseShadowTransitions();' in w
print('PASS direction selection, content/slice/travel/reset cancellation and successful-render commit ordering')

assert 'SetTexture(0,neutralWhite)' in h and 'SetTexture(1,shadowCache[slot])' in h
assert 'D3DPOOL_DEFAULT' in h and 'D3DFMT_R32F' in h
assert 'target(' not in h and 'check(' not in h  # Optional failure must not disable the world renderer.
assert 'if(!active)return;' in h
assert 'dynamicShadow=(historyReady||transition.state.fading())?transition.dynamic:shadowScratch' in render
assert 'SetTexture(0,dynamicShadow)' in render
assert render.count('if(anyShadowTransition)for(unsigned s=6;s<10;++s)d->SetTexture(s,textures[s]);') == 1
assert render.count('if(anyShadowVolumeTransition)for(unsigned s=6;s<10;++s)d->SetTexture(s,textures[s]);') == 1
fog_restore = render.index('/* restore the bank\'s direct/ambient before lamp fog, blur and composite */')
linear = render.index('if(anyShadowVolumeTransition){d->SetSamplerState(9,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);', fog_restore)
assert linear < render.index('if(localDirectCount&&debug==0)', linear)
for selector in ('shadowTransitionActive(source)?lightingFadePS:lightingPS', 'shadowVolumeTransitionActive(source)?fogFadePS:fogPS', 'shadowVolumeTransitionActive(firstSource)?cloudFadePS:fogCloudsPS'):
    assert selector in render
assert render.count('if(anyShadowTransition){bindShadowTransition(source);d->SetPixelShader') == 1
assert render.count('if(anyShadowVolumeTransition){bindShadowTransition(source);d->SetPixelShader') == 1
assert 'shadowTransitionActive(source)&&shadowTransition[source*2+1].state.fading()' in h
assert 'sourceTransition=shadowTransition[source*2+(volume?1:0)]' in render
for label in ('WorldLightingFade', 'FogMarchFade', 'FogCloudsFade', 'ShadowHistory'):
    assert label in render
assert 'reservedMiB=%u' in render and 'allocationFailures=%u' in render
print('PASS static-only history, retained current dynamics, unchanged idle dispatch and GI/fog sampler contracts')
