#!/usr/bin/env python3
# northlight-test: requires=cxx,client,stormlib,world-cache
"""0.3.172 rigid memory: native model test of rigid_memory.h and rigid_geometry.h (clang++, plain and
ASan/UBSan): settling, sticky mobile/held/static, identity without the snapshot pointer, absence and the
in-view despawn test, caps, the rebase round trip, the real client one-influence program with a Stormwind
sign end to end and the static-doodad flood on real world-cache placements. Wiring audit of the renderer
side (world_rigid_memory.inl): observed before retainSelected, injected after it and before the bounds
kick and upload, copies own their constant banks and hold no texture when opaque, cleared on device
loss/reset/trim/map change/shadows off, never touches the static cache. 0.3.176: the flat placement index against the 0.3.175 map index (S1),
and rigidObserveGroup reusing selection's bone (S2, test_rigid_observe.cpp) against the 0.3.175 path, with
the audit-gate counterfactual, and the in-place refresh (S3', test_rigid_refresh.cpp). No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import client_fixtures  # the real client programs and placements, from the tester's client and world cache
import ast,re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
def literal(path,name):
 return next(ast.literal_eval(n.value) for n in ast.parse(path.read_text()).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id==name for t in n.targets))
stub=literal(HERE/'test_terrain_snapshot.py','stub')
m=fp.src('world_rigid_memory.inl').read_text();w=fp.src('world_renderer.h').read_text();x=fp.src('world_shadow_experiment.inl').read_text();g=fp.src('world_memory_guard.inl').read_text()
checks={}
select=x[x.index('    void selectShadowReplays(){'):x.index('    // Called once per frame after selection')]
checks['observed before retainSelected, injected right after it, both inside the selection try']=(select.count('rigidMemoryObserve();')==1 and select.count('rigidMemoryInject();')==1
    and select.index('try{')<select.index('rigidMemoryObserve();')<select.index('NorthlightReplayShadowPolicy::retainSelected(replays,heldShadowReplays);')<select.index('rigidMemoryInject();')<select.index('}catch(...){'))
render=w[w.index('    bool render(IDirect3DSurface9* targetSurface'):]
checks['injection before the bounds kick and the replay upload']=render.index('if(replayShadows)selectShadowReplays();')<render.index('replayBoundsKick();')<render.index('return uploadReplay();')
inject=m[m.index('    void rigidMemoryInject(){'):]
checks['copies own their constant bank (constants==constantStorage), rebased bone rows, unique groups']=('NorthlightReplayCaptureConstants::reset(*p);' in inject
    and 'std::memcpy(p->constantStorage,c.constants.data(),sizeof p->constantStorage);' in inject and 'std::memcpy(p->constantStorage+4*(31+3*e.payload.bone),rows,sizeof rows);' in inject
    and 'NorthlightRigidMemory::rebase(e.world,context.inverseView,rows)' in inject and 'p->constantGroup=++group;' in inject and 'p->constantStamp' not in inject.replace('reset(*p)',''))
checks['opaque copies hold no texture; no game VB/IB is held']=('c.texture.reset(p.cutoff>=0?p.texture:nullptr);' in m and re.search(r'(p|c)(\.|->)(stream|index)\b',m) is None)
checks['at most 4096 replays, outside quota/radius/fate']=('if(replays.size()+e.payload.draws.size()>=4096' in inject and 'p->fateSlot=-1;' in inject
    and 'p->shadowSkinned=p->shadowSelected=true;' in inject)
checks['cleared in releaseGPU (reset() calls it), trimMemory, on a map change and with shadows off']=('staticCasters.settle();rigidMemoryClear();' in w
    and re.search(r'void reset\(\)\{[^\n]*releaseGPU\(\);\}',w) is not None and 'rigidMemoryClear(); /* 0.3.172' in g[g.index('MemoryTrim trimMemory(){'):g.index('void setMemoryPressure')]
    and 'if(lastRequest.map!=rigidMap){rigidMemoryClear();' in m and 'if(!effects.shadows){rigidMemoryClear();return;}' in m)
checks['never the static cache']=all(n not in m for n in ('staticCasters','shadowCacheKey','invalidateShadowCache','staticSignature'))
checks['observes every captured skinned group; bodies are the non-rigid ones; identity by mixShape']=('if(!p.shadowSkinned)continue;' in m and 'rigidBodies.insert(' in m
    and 'NorthlightRigidMemory::mixShape(shape,p.originalShader,p.decl,p.mesh().vertexCount,p.mesh().primitiveCount,p.mesh().byteSize());' in m and 'shared.get()' not in m)
checks['shortfall frames record and draw; only the despawn test needs a complete frame']='!captureShortfall,' in m and m.count('captureShortfall')==1
checks['logged on sampled frames']='if(captureSampled){const auto& s=rigidMemory.stats();' in inject and 'deferLogf("RIGID memory tracks=%zu entries=%zu injected=%u seen=%zu held=%zu static=%zu mobile=%zu droppedInView=' in inject
capture=w[w.index('    void captureModel(D3DPRIMITIVETYPE type,'):w.index('    // One directional replay draw in the 0.3.142 order')]
drawn='if(!rigidDrawKeys.empty()&&rigidDrawKeys.contains(current,count))rigidMemoryDrawn(current,count);'
# 0.3.196 (task 12): captureModel's shader lookup goes through lookupShader(), so metadata is `*found`.
checks['0.3.173 drawn test: only with entries, right after the shader lookup, before the 4096 cap, budget, blend and projection checks']=(capture.count(drawn)==1
    and capture.index('const auto& metadata=*found;')<capture.index(drawn)<capture.index('if(replays.size()>=4096)')<capture.index('if(replaySnapshots.captureExhausted(priority))')
    and capture.index(drawn)<capture.index('D3DRS_ALPHABLENDENABLE')<capture.index('kind==1?4:2,q,4'))
checks['drawn test reuses the mirror-answered palette rows of drawRoot (one read site), bone 0 at c31']=(w.count('GetVertexShaderConstantF(UINT(program->second->paletteBase),rows,3)')==1
    and 'float rows[12];const auto* program=paletteRows(shader,rows);if(!program)return false;' in w and 'const auto* program=paletteRows(shader,rows);' in m and 'program->paletteBase!=31' in m)
observe=m[m.index('    void rigidMemoryObserve(){'):m.index('    void rigidMemoryInject(){')]
checks['key set rebuilt after store every capture frame, cleared with the memory; drawn marks per frame']=(observe.index('rigidMemory.store(o,rigidCopy(n),now);')<observe.index('rigidDrawKeysRebuild();')
    and 'rigidDrawKeys.clear();}' in m and 'rigidMemory.clearDrawn(); /* 0.3.173' in w[w.index('    void endFrame('):])
checks['LiveUnselected: captured non-small draws only (small at capture never observed), no copy']=('if(!p.shadowSelected){if(p.shadowSmall)continue;' in m and 'u.selected=false;' in m
    and 'p->shadowSmall=smallShadow;' in w)
checks['RIGID event lines: Diagnostics only, 20 per second, 2000 a session']=('if(NorthlightDiagnostics::enabled()){rigidMemory.takeEvents(rigidEvents);' in m and 'RigidEventsPerSecond=20,RigidEventLines=2000;' in m
    and 'if(!rigidEventTokens||rigidEventLines>=RigidEventLines){++rigidEventSuppressed;continue;}' in m and 'rigidMemory.events(NorthlightDiagnostics::enabled());' in m)
checks['counters reset on a map change; doodad bodies from the static index']=('if(lastRequest.map!=rigidMap){rigidMemoryClear();rigidMemory.resetStats();' in m and 'return rigidStaticBody(root);' in m)
checks['placement index stepped while incomplete with tracks (doodad bodies after a revision change), same per-frame step']=(
    'if(rigidMemory.screening()||(rigidMemory.stats().tracks&&!rigidIndexCurrent()))rigidIndexStep();' in observe
    and observe.index('rigidMemory.frame(')<observe.index('rigidIndexStep();') and 'static constexpr size_t RigidIndexStep=2048;' in m
    and m.count('rigidIndexStep()')==2 and 'if(!rigidIndexCurrent())return false;const auto& x=rigidIndex;' in m
    and re.search(r'bool rigidIndexCurrent\(\)const\{\s*return staticScene&&staticScene->map==lastRequest.map&&rigidIndex.scene==staticScene.get\(\)&&rigidIndex.revision==rigidSceneRevision\(\*staticScene\)&&rigidIndex.complete;\}',m) is not None)
sel=x[x.index('    NorthlightActorShadowSelection::Result selectStableActors('):]
checks['S2 (0.3.176): selection stores exactly the draws it tested; reset at capture and without the stable selection; observe uses it only behind the audit gate, else the 0.3.175 call']=(
    # 0.3.177 (r83): the stable path's prepareRecord reports the tested bone; the selection writes it back.
    'out.tested=s.groupRigid&&declared;\n    item.bone=out.tested?caches.bones.bone(*program,p.mesh(),p.shared,p.decl,elements,count):NAN;' in fp.src('prepare_worker.h').read_text()
    and 'actorShadowDraws.push_back(out.item);Replay& stored=*replays[out.item.index];stored.boneKnown=out.tested;stored.bone=out.item.bone;' in x
    and 'item.bone=groupRigid&&declared()?prepareCaches->bones.bone(*program->second,p.mesh(),p.shared,p.decl,elements,count):NAN;item.rigid=!std::isnan(item.bone);\n            {Replay& stored=*replays[index];stored.boneKnown=groupRigid&&declared();stored.bone=item.bone;}' in sel
    and 'p->shadowSkinned=priority;p->shadowSelected=!smallShadow;p->shadowSmall=smallShadow;p->boneKnown=false;' in capture
    and 'if(!stableRan)for(auto& p:replays)p->boneKnown=false;' in select and select.index('if(!stableRan)')<select.index('rigidMemoryObserve();')
    and select.count('stableRan=true;stable=selectStableActors(')==2 and select.count('selectStableActors(')==2
    and 'const auto* program=p.shared?rigidProgram(p.originalShader):nullptr;' in m
    and 'if(program&&p.boneKnown)b=p.bone;\n                else if(program&&declarationCache.get(p.decl,elements,count))b=prepareCaches->bones.bone(*program,p.mesh(),p.shared,p.decl,elements,count);' in m)
checks["S3' (0.3.176): Refresh rewrites the entry's copy in place (Registry::refresh), Remember stores a fresh copy; store's Refresh goes through refresh"]=(
    "if(o.action==NorthlightRigidMemory::Observation::Refresh)rigidMemory.refresh(o,[&](RigidPayload& payload){rigidRefresh(n,payload);});\n            else if(o.action!=NorthlightRigidMemory::Observation::None)rigidMemory.store(o,rigidCopy(n),now);}" in observe
    and 'if(o.action==Observation::Refresh)return refresh(o,[&](Payload& p){p=std::move(payload);});' in fp.src('rigid_memory.h').read_text()
    and 'auto& e=entries_[std::size_t(t->entry)];fill(e.payload);stats_.bytes=' in fp.src('rigid_memory.h').read_text())
hdr=fp.src('rigid_memory.h').read_text()
checks['tracks: new ones sorted and merged into the ordered survivors (no full sort), partial reindex']=('std::inplace_merge(tracks_.begin(),middle,tracks_.end(),trackBefore);' in hdr
    and 'std::sort(middle,tracks_.end(),trackBefore);' in hdr and 'std::sort(tracks_.begin(),tracks_.end()' not in hdr and 'for(std::size_t i=start;i<tracks_.size();++i)trackIndex_[tracks_[i].serial]=i;' in hdr)
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-rigid-memory-') as tmp:
 p=Path(tmp);(p/'d3d9.h').write_text(stub)
 client_fixtures.actor_client_programs(p);client_fixtures.rigid_placements(p)
 for label,flags in (('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])):
  exe=p/('test-'+label)
  subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-UNDEBUG','-I',str(p),*fp.test_include_flags(),str(HERE/'test_rigid_memory.cpp'),str(fp.src('world_gi.cpp')),'-o',str(exe)],check=True)
  print(f'[{label}]',flush=True);subprocess.run([str(exe),str(client_fixtures.four_bone_vs3())],check=True)
 # 0.3.176 (S2): the production rigidProgram()/rigidObserveGroup() in a harness, with and without the audit gate.
 def method(text,head):
  start=text.index(head);depth=0;i=text.index('{',start)
  while True:
   depth+={'{':1,'}':-1}.get(text[i],0)
   if depth==0:return text[start:i+1]
   i+=1
 gated=method(m,'    const NorthlightActorDeformation::Program* rigidProgram(')+'\n'+method(m,'    void rigidObserveGroup(size_t first,size_t end){')
 gate='if(program&&p.boneKnown)b=p.bone;'
 assert gated.count(gate)==1
 ungated=gated.replace(gate,'if(p.boneKnown)b=p.bone;')
 harness=(HERE/'test_rigid_observe.cpp').read_text().replace('/*OBSERVE_METHODS*/','struct Gated:Base{using Base::Base;\n'+gated+'\nOBSERVE_LOOP};\nstruct Ungated:Base{using Base::Base;\n'+ungated+'\nOBSERVE_LOOP};')
 (p/'test_rigid_observe.cpp').write_text(harness)
 for label,flags in (('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])):
  exe=p/('observe-'+label)
  subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-UNDEBUG','-I',str(p),*fp.test_include_flags(),str(p/'test_rigid_observe.cpp'),str(fp.src('world_gi.cpp')),'-o',str(exe)],check=True)
  print(f'[observe {label}]',flush=True);subprocess.run([str(exe),str(client_fixtures.four_bone_vs3())],check=True)
 # 0.3.176 (S3'): the production copy types and rigidFill/rigidCopy/rigidRefresh in a harness.
 def block(text,head):
  start=text.index(head);depth=0;i=text.index('{',start)
  while True:
   depth+={'{':1,'}':-1}.get(text[i],0)
   if depth==0:return text[start:text.index(';',i)+1] if head.lstrip().startswith(('template<class T> struct','struct')) else text[start:i+1]
   i+=1
 copies='\n'.join(block(m,h) for h in ('    template<class T> struct RigidRef {','    struct RigidDraw {','    struct RigidPayload {',
  '    static void rigidFill(const Replay& p,RigidDraw& c){','    size_t rigidGroupEnd(size_t n)const{','    RigidPayload rigidCopy(size_t n){','    void rigidRefresh(size_t n,RigidPayload& out){'))
 (p/'test_rigid_refresh.cpp').write_text((HERE/'test_rigid_refresh.cpp').read_text().replace('/*REFRESH_METHODS*/',copies))
 for label,flags in (('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])):
  exe=p/('refresh-'+label)
  subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,'-UNDEBUG','-I',str(p),*fp.test_include_flags(),str(p/'test_rigid_refresh.cpp'),'-o',str(exe)],check=True)
  print(f'[refresh {label}]',flush=True);subprocess.run([str(exe)],check=True)
print('PASS rigid memory: model and wiring')
