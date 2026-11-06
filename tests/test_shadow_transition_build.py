#!/usr/bin/env python3
# northlight-test:
"""Check validated crossfade payloads and the current renderer integration contracts."""
import hashlib,json,re,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=json.loads((fp.SHADERS/'shadow-transition-shader-build.json').read_text())
header=fp.src('shadow_transition_compiled_shaders.h').read_text()
for name,digest in manifest['sources'].items():assert sha(fp.SHADERS/name)==digest,name
assert len(manifest['shaders'])==8
for entry,record in manifest['shaders'].items():
 data=(fp.COMPILED/(entry+'.bin')).read_bytes();assert sha(fp.COMPILED/(entry+'.bin'))==record['sha256']
 limit=768
 assert record['target']=='ps_3_0' and record['static_instruction_slots']<=limit and record['temporary_registers']<=32
 body=re.search(r'k'+entry+r'Shader\[\] = \{(.*?)\};',header,re.S)[1]
 words=[int(word,16) for word in re.findall(r'0x[0-9a-fA-F]+',body)]
 assert struct.pack('<'+'I'*len(words),*words)==data
print('PASS eight fresh compiled/embedded optional extended SM3 variants <=768 slots and 32 temps; baseline shader gates unchanged')
w=fp.src('world_renderer.h').read_text();h=fp.src('world_shadow_transition.inl').read_text()
assert 'void invalidateShadowCache(){resetShadowTransitions();' in w
assert 'releaseGeometryTargets(){\n        releaseShadowTransitions();releaseNearEnvironment();' in w
assert 'releaseGPU(bool keepMesh=false){releaseShadowTransitions();' in w
assert 'if(!next.shadows)releaseShadowTransitions();' in w
assert 'if(memoryPressure||geometryMemoryDemand.load(std::memory_order_relaxed))releaseShadowTransitions();' in w
assert 'item.history,ShadowMaps' not in w and 'inventory.texture(transition.history,ShadowMaps)' in w and 'inventory.texture(transition.dynamic,ShadowMaps)' in w
assert 'slotDirections[slot]=' in w and 'shadowFrame(cascadePivot,slotDirections[source*2+cascade],radius)' in w
assert 'NorthlightActorShadowMap::matrix(actorCenter[source],pivot,rawSourceDirections[source]' in w
assert 'sliceBand||wasSlicing||slices.active||(reason&&!directionStep)' in w
assert 'localSame&&staticSame' in w and 'staticSignature(oldMatrix)' in w
assert w.index('historyReady=preserveShadowStatic(slot)')<w.index('renderCache(shadowCacheSurface[slot],partialStatic,true)')
commit=w.index('transition.state.commit(');assert w.index('unionShadow(shadowSurface[slot]')<commit
assert 'if(!environmentReuse){\n            const V selected=' in w
assert 'if(!complete)owner->resetShadowTransitions()' in w
start=w.index('const auto action=NorthlightQuality::cascadeAction(');predicate=w[start:w.index('cascadeActions[slot]=',start)]
assert '!transition.state.fading()' in predicate and 'workingValid' not in predicate
assert 'if(!historyReady&&!transition.state.fading())' in w
assert 'dynamicShadow=(historyReady||transition.state.fading())?transition.dynamic:shadowScratch' in w
assert 'dynamicSurface=(historyReady||transition.state.fading())?transition.dynamicSurface:shadowScratchSurface' in w
assert 'SetRenderTarget(0,dynamicSurface)' in w and 'unionShadow(shadowSurface[slot],dynamicShadow,' in w
assert 'StretchRect(' not in w[w.index('bool historyReady=false;'):w.index('transitionFrame.complete=true;')]
print('PASS static-only history, success-only exact map/matrix publication, content/reset/pressure invalidation, baseline fallback cadence and current terrain/actor copy')
assert h.index('shadowTransitionFailed||memoryPressure||geometryMemoryDemand')<h.index('if(shadowTransition[slot].history')
assert 'MaxPixelShader30InstructionSlots<required' in h and 'dynamicBudget(8ull<<20)' in h
assert 'std::max({kDirectionalShadowTransitionCurrentShaderSlots,kDirectionalShadowTransitionPreviousShaderSlots' in h
assert 'SetTexture(0,neutralWhite)' in h and 'SetTexture(1,shadowCache[slot])' in h
assert 'check(' not in h and 'target(' not in h
assert 'quality.softShadows||anyShadowTransition' in w
assert 'if(anyShadowTransition){bindShadowTransition(source);d->SetPixelShader(transitionCurrentPS);' in w
assert 'bindShadowTransition(source,true);d->SetPixelShader(transitionPreviousPS)' in w
assert 'if(anyShadowTransition&&!softResolved)releaseShadowTransitions();' in w
assert w.count('if(anyShadowTransition)for(unsigned sampler=6;sampler<10;++sampler)')==2
assert w.index('if(anyShadowTransition)for(unsigned sampler=6;sampler<10;++sampler)')<w.index('d->SetPixelShader(softResolved?lightingSoftPS:lightingPS)')
for selector in ['fogActorTiledCurrent?fogActorTiledFadePS:fogActorCurrent?fogActorFadePS:fogFadePS','cloudActorTiledFadePS:cloudActorFadePS','cloudFade=geometryReady&&shadowVolumeTransitionActive(firstSource)']:
 assert selector in w,selector
assert 'if(fading)bindShadowTransition(source)' in w and 'if(cloudFade)bindShadowTransition(firstSource)' in w
print('PASS optional cap/budget/failure fallback; two-epoch resolve only during fades; GI/material sampler restore and current actor/tiled/HDR volume consumers')

assert 'SetPixelShaderConstantF(113,currentNear,1)' in h and 'SetPixelShaderConstantF(114,currentFar,1)' in h
