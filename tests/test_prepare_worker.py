#!/usr/bin/env python3
# northlight-test: requires=cxx,client,stormlib
"""0.3.177 (r83 a1-prepare): the actor prepare worker. Native test of prepare_worker.h
(test_prepare_worker.cpp: handover at every index, the threaded worker and join with random delays and
stops, exception and watchdog takeover, cache independence, and the counterfactuals: state reset at the
handover, unfiltered records, the live declaration cache after an eviction, the camera at the join),
built O2, ASan+UBSan and TSan; 0.3.183: the quarantine bounded to the abandoned frame (its replays, one
arena, the programs pinned at the abandon) over 32 frames with the worker held, and its counterfactuals
under ASan (every replay held: the assert on prepareQuarantine.size(); only the published ones held: the
constant donor freed; no pins: the retired program freed); and a wiring audit of the renderer side: the
capture fill, stamp and publish, the join first in the selection, quiesce before recycling, the deferred
cache clear, one prepare function, the frozen camera, the enable rule, the quarantine of the abandoned
frame's replays, RenderProfile-only diagnostics. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import client_fixtures  # the real client programs, from the tester's client
import ast,re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
def literal(path,name):
    return next(ast.literal_eval(n.value) for n in ast.parse(path.read_text()).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id==name for t in n.targets))
stub=literal(HERE/'test_terrain_snapshot.py','stub')
deformation=literal(HERE/'test_actor_deformation.py','harness')
code=deformation[deformation.index('std::vector<Word> code(unsigned major){'):deformation.index('int main(')]
worker=re.sub(r'/\*.*?\*/','',re.sub(r'//[^\n]*','',fp.src('prepare_worker.h').read_text()),flags=re.S)
checks={
 'the worker header touches no renderer state (actorPrograms, declarationCache, replays, the device)':
    not any(n in worker for n in ('actorPrograms','declarationCache','replays','d->','IDirect3DDevice9','context.')),
}
w=fp.src('world_renderer.h').read_text();x=fp.src('world_shadow_experiment.inl').read_text();g=fp.src('world_memory_guard.inl').read_text()
def body(text,head):
    start=text.index(head);depth=0;i=text.index('{',start)
    while True:
        depth+={'{':1,'}':-1}.get(text[i],0)
        if depth==0:return text[start:i+1]
        i+=1
capture=body(w,'    void captureModel(D3DPRIMITIVETYPE type,')
select=body(x,'    void selectShadowReplays(){')
assert w.count('void endFrame(')==1
end=body(w,'    void endFrame(')
release=w[w.index('    void releaseGPU(bool keepMesh=false){'):];release=release[:release.index('\n')]
reset=w[w.index('    void reset(){'):];reset=reset[:reset.index('\n')]
trim=body(g,'    MemoryTrim trimMemory(){')
register=body(w,'    void registerShader(IDirect3DVertexShader9* shader,uint64_t hash){')
checks.update({
 'capture: the inputs filled just before a selected skinned draw joins replays, published right after':
    'if(handoff)prepareFill(*p,metadata); /* 0.3.177: the stable selection\'s inputs */\n        replays.emplace_back(p.release());preparePublish();' in capture,
 'the join is the first statement of selectShadowReplays':select.split('{',1)[1].lstrip().startswith('prepareJoin();'),
 'quiesce before recycling: endFrame, reset (endFrame, releaseGPU), releaseGPU, trimMemory':
    end.index('prepareQuiesce();prepareEndFrame();')<end.index('recycleReplay(')
    and release.index('prepareQuiesce();')<release.index('prepareCaches->sampled.clear()')<release.index('releaseReplayGPU();') and reset.index('endFrame();')<reset.index('releaseGPU(true);')
    and trim.index('prepareQuiesce();')<trim.index('freeReplays.clear()'),
 'registerShader defers the cache clear (the owner clears at its next open)':
    'prepareCachesStale=true;' in register and 'prepareCaches->sampled.clear()' not in register and 'prepareCaches->bones.clear()' not in register,
 'the caches are cleared by their owner only: at open (stale), releaseGPU after quiesce':
    w.count('prepareCaches->sampled.clear()')==1 and x.count('prepareCaches->sampled.clear()')==1
    and 'if(prepareCachesStale.exchange(false)){prepareCaches->sampled.clear();prepareCaches->bones.clear();' in x,
 'one prepare function: the worker (Prepare), the join tail, the self-check and inline all call prepareRecord':
    fp.src('prepare_worker.h').read_text().count('prepareRecord(s,p,index,c,f.inverseView,f.camera,out);')==1
    and x.count('NorthlightActorPrepare::prepareRecord(')==3 and 'NorthlightActorPrepare::prepareRecord(state,p,index,*prepareCaches,context.inverseView,context.camera,out);prepareConsume(out);' in x,
 'the stable loop body exists only in prepareRecord (the legacy loop has no stable identity branch)':
    'if(tuning.stableIdentity){' not in x and 'if(tuning.stableIdentity)prepareStable(distanceTests,distanceReused);' in x,
 'the worker frame copies the camera at open, and the join tail uses that copy':
    'std::memcpy(frame.inverseView,context.inverseView,sizeof frame.inverseView);std::memcpy(frame.camera,context.camera,sizeof frame.camera);' in x
    and 'prepareWorker.index(k),*prepareCaches,frame.inverseView,frame.camera,outputs[k]);' in x,
 'offload only with 6+ cores, ActorShadows=1, the stable tuning and no watchdog; RenderProfile A/B windows':
    'bool prepareOffload()const{return prepareCores>=6&&quality.actorShadows&&selectionTuning().stableIdentity&&!prepareWorker.abandoned();}' in x
    and 'const bool abInline=NorthlightRenderThreadProbe::profiling()&&(GetTickCount()/10000u)%2u==1u;' in x,
 'an abandoned worker: its frame\'s replays quarantined (recycle, resize, clear) until it settles; stamped at publish, cleared at release (0.3.183)':
    'if(prepareHeld(*raw)){try{prepareQuarantine.emplace_back(raw);}catch(...){} return;}' in w and 'prepareUnsettled()' not in body(w,'    void recycleReplay(Replay* raw){')
    and 'bool prepareHeld(const Replay& p)const{return prepareUnsettled()&&p.prepareStamp==prepareAbandonedEpoch;}' in x
    and body(x,'    void preparePublish(){').split('{',1)[1].lstrip().startswith('replays.back()->prepareStamp=prepareEpoch;')
    and 'std::uint64_t prepareStamp=0;' in w and 'program=nullptr;declCopy=nullptr;prepareStamp=0;' in body(w,'        void releaseResources(bool retainSnapshot=false){')
    and 'std::uint64_t prepareEpoch=1,prepareAbandonedEpoch=0;' in x and x.count('prepareEpoch++')==1
    and (lambda a:all(s in a for s in ('prepareQuarantinedPrograms.reserve(captureShaders.size()+actorPrograms.size()+retiredPrograms.size());prepareQuarantinedDecls.reserve(1);',
        'for(const auto& s:captureShaders)if(s.second.program)prepareQuarantinedPrograms.push_back(s.second.program);','for(const auto& a:actorPrograms)if(a.second)prepareQuarantinedPrograms.push_back(a.second);',
        'for(const auto& r:retiredPrograms)if(r)prepareQuarantinedPrograms.push_back(r);','prepareQuarantinedDecls.push_back(std::move(prepareDecls));try{prepareDecls=std::make_unique<NorthlightActorPrepare::DeclArena>();}catch(...){}',
        'prepareAbandonedEpoch=prepareEpoch++;')))(body(x,'    void prepareAbandon(){'))
    and 'if(prepareUnsettled())for(size_t i=fit;i<replays.size();++i)recycleReplay(replays[i].release());' in w
    and 'if(prepareUnsettled()){for(auto& p:replays)recycleReplay(p.release());for(auto& p:heldShadowReplays)recycleReplay(p.release());}' in w,
 'watchdog re-arm: only in endFrame, after the settled worker released its quarantine, arena, pinned programs and caches; 10 s after the abandon, at most 4 a session; counted in the log and the fields':
    (lambda e:e.index('recycleReplay(p.release());')<e.index('prepareQuarantinedDecls.clear();')<e.index('prepareQuarantinedPrograms.clear();')<e.index('prepareAbandonedCaches.reset();')<e.index('prepareWorker.rearm()'))(body(x,'    void prepareEndFrame(){'))
    and x.count('prepareQuarantinedDecls.clear()')==1 and x.count('prepareQuarantinedPrograms.clear()')==1
    and 'if(prepareRearms<PrepareRearms&&GetTickCount()-prepareAbandonTick>=PrepareRearmAfterMs&&prepareWorker.rearm()){' in x
    and 'static constexpr unsigned PrepareRearms=4;static constexpr DWORD PrepareRearmAfterMs=10000;' in x and x.count('prepareWorker.rearm()')==1
    and '(re-arms %u of %u)' in x and 'prepareRearms=%u' in x,
 'T1 (0.3.179): prepareFill takes the capture metadata\'s program: no actorPrograms lookup, no shared_ptr copy':
    (lambda f:'actorPrograms' not in f and 'std::shared_ptr' not in f and 'p.program=metadata.program.get();' in f)(body(x,'    void prepareFill(Replay& p,const CaptureShader& metadata){'))
    and 'const NorthlightActorDeformation::Program* program=nullptr;' in w and 'metadata.program=std::make_shared<const NorthlightActorDeformation::Program>(std::move(program));\n                actorPrograms.emplace(shader,metadata.program);' in w,
 'T1: registerShader retires erased programs; they are released only after the recycle (0.3.183: an abandoned worker reads the pinned copies)':
    'retireProgram(std::move(old->second.program));' in register and 'retireProgram(std::move(program->second));actorPrograms.erase(program);' in register
    and w.count('retiredPrograms.clear()')+x.count('retiredPrograms.clear()')==1
    and (lambda f:'retiredPrograms.clear();' in f and 'prepareUnsettled' not in f and 'Quarantine' not in f)(body(x,'    void prepareFrameRelease(){'))
    and end.index('recycleReplay(')<end.index('prepareFrameRelease(); /* 0.3.179: after the recycle */')
    and 'if(replays.empty()&&heldShadowReplays.empty())prepareFrameRelease();' in release,
 'T2 (0.3.179): prepareFill reads the declaration through the frame arena (the per-record copy when full); the arena is reset only in prepareFrameRelease, moved aside at the abandon (0.3.183)':
    'prepareDecls->find(p.decl,' in x and 'if(copy){p.declCopy=copy;p.declared=copy->declared;}' in x
    and x.count('prepareDecls->reset()')==1 and 'prepareQuarantinedDecls.push_back(std::move(prepareDecls));' in body(x,'    void prepareAbandon(){') and x.count('prepareQuarantinedDecls.push_back(')==1
    and 'const NorthlightActorPrepare::DeclCopy* declCopy=nullptr;' in w and 'program=nullptr;declCopy=nullptr;' in w
    and w.index('std::unique_ptr<NorthlightActorPrepare::DeclArena> prepareDecls=')<w.index('NorthlightActorPrepare::Worker<Replay> prepareWorker;'),
 'T3 (0.3.179): a release publish; the seq_cst fence only at the wake threshold, and before the worker sleeps':
    (lambda h:'slots_[n]={record,index};published_.store(n+1,std::memory_order_release);\n        if(n+1-done_.load(std::memory_order_relaxed)>=wakeBatch_){std::atomic_thread_fence(std::memory_order_seq_cst);if(wakeable_.exchange(false))notify();}' in h
        and 'blocked_=true;wakeable_.store(true,std::memory_order_relaxed);std::atomic_thread_fence(std::memory_order_seq_cst);\n            wake_.wait(lock,' in h
        and h.count('std::atomic_thread_fence(std::memory_order_seq_cst)')==2 and 'published_.store(n+1,std::memory_order_seq_cst)' not in h)(fp.src('prepare_worker.h').read_text()),
 'M1/M2 (0.3.179): one span per sampled record around fill and publish in captureModel; the meter and the warm re-run gated by profileSampled()':
    'const bool handoff=p->shadowSelected&&p->shadowSkinned,timedHandoff=handoff&&prepareHandoffSample();' in capture
    and capture.index('prepareMeter.start()')<capture.index('prepareFill(*p,metadata);')<capture.index('replays.emplace_back(p.release());preparePublish();if(timedHandoff)prepareMeter.stop(handoffStart);')
    and 'prepareMeter.beginFrame(profileSampled(),prepareFrameSerial);' in x and 'if(profileSampled()&&r.published)prepareWarmHandoff(r.published);' in x
    and 'steady_clock' not in body(x,'    void prepareFill(Replay& p,const CaptureShader& metadata){') and 'steady_clock' not in body(x,'    void preparePublish(){')
    and 'handoffUs=%.1f handoffRawUs=%.1f pairNs=%.1f handoffSamples=%u handoffWarmUs=%.1f' in x,
 'diagnostics only with RenderProfile (the fields and every clock)':
    'if(NorthlightRenderThreadProbe::profiling())std::snprintf(prepareFields,' in x and 'prepareTimed=profileSampled();' in x,
 'the worker is joined first in ~WorldRenderer and destroyed before the replays, caches, arenas, quarantine, pinned programs, actorPrograms, captureShaders and retiredPrograms it reads':
    w.count('NorthlightActorPrepare::Worker<Replay> prepareWorker;')==1
    and all(w.index(member)<w.index('NorthlightActorPrepare::Worker<Replay> prepareWorker;') for member in ('std::vector<std::unique_ptr<Replay>> replays,',
        'std::unique_ptr<NorthlightActorPrepare::Caches> prepareCaches=','std::unique_ptr<NorthlightActorPrepare::DeclArena> prepareDecls=','std::vector<std::unique_ptr<Replay>> prepareQuarantine;',
        'std::vector<std::shared_ptr<const NorthlightActorDeformation::Program>> prepareQuarantinedPrograms;',
        'std::shared_ptr<const NorthlightActorDeformation::Program>> actorPrograms;','std::unordered_map<IDirect3DVertexShader9*,CaptureShader> captureShaders;',
        'std::vector<std::shared_ptr<const NorthlightActorDeformation::Program>> retiredPrograms;'))
    and body(w,'    ~WorldRenderer(){').split('{',1)[1].lstrip().startswith('prepareWorker.join();'),
})
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-prepare-worker-') as tmp:
    p=Path(tmp);(p/'d3d9.h').write_text(stub);client_fixtures.actor_client_programs(p)
    xs=fp.src('world_shadow_experiment.inl').read_text()
    block=xs[xs.index('    // ---- 0.3.177 (r83 a1-prepare)'):xs.index('    // Stable per-actor quota (see actor_shadow_selection.h).')]
    wr=fp.src('world_renderer.h').read_text();retire=body(wr,'    void retireProgram(std::shared_ptr<const NorthlightActorDeformation::Program>&& program){')
    (p/'test_prepare_worker.cpp').write_text((HERE/'test_prepare_worker.cpp').read_text().replace('/*SYNTHETIC_PROGRAMS*/',code).replace('/*PREPARE_BLOCK*/',block).replace('/*RETIRE_PROGRAM*/',retire))
    four=str(client_fixtures.four_bone_vs3())
    for label,flags,mode in (('O2',['-O2'],'full'),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'],'full'),('tsan',['-O1','-g','-fsanitize=thread'],'tsan')):
        exe=p/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,'-I',str(p),*fp.test_include_flags(),str(p/'test_prepare_worker.cpp'),str(fp.src('world_gi.cpp')),'-o',str(exe)],check=True)
        out=subprocess.run([str(exe),four,mode],capture_output=True,text=True)
        if out.returncode or 'WARNING' in out.stderr or 'ERROR' in out.stderr:
            sys.exit(f'{label} failed ({out.returncode}):\n{out.stdout}{out.stderr[-6000:]}')
        print(f'[{label}]',out.stdout.strip(),flush=True)
        if label=='asan': # 0.3.179 (T1) counterfactual: a program freed at the re-register is read after it
            bad=subprocess.run([str(exe),four,'lifetime-counterfactual'],capture_output=True,text=True)
            assert bad.returncode!=0 and 'heap-use-after-free' in bad.stderr,('the freed-program counterfactual must fail under ASan',bad.returncode,bad.stderr[-2000:])
            print('[asan] counterfactual: a program freed at the re-register: heap-use-after-free, as expected',flush=True)
            bad=subprocess.run([str(exe),four,'shutdown-counterfactual'],capture_output=True,text=True) # 0.3.179: no join at destruction
            assert bad.returncode!=0 and 'heap-use-after-free' in bad.stderr,('the shutdown-order counterfactual must fail under ASan',bad.returncode,bad.stderr[-2000:])
            print('[asan] counterfactual: destruction without the join: heap-use-after-free, as expected',flush=True)
            # 0.3.183: the quarantine bounded to the abandoned frame; each weaker rule must fail.
            bad=subprocess.run([str(exe),four,'quarantine-all-counterfactual'],capture_output=True,text=True,timeout=300)
            assert bad.returncode!=0 and 'prepareQuarantine.size()' in bad.stderr,('the hold-everything counterfactual must fail the bound',bad.returncode,bad.stderr[-2000:])
            print('[asan] counterfactual: every replay held while unsettled: the quarantine grows past the abandoned frame, as expected',flush=True)
            bad=subprocess.run([str(exe),four,'quarantine-published-counterfactual'],capture_output=True,text=True,timeout=300)
            assert bad.returncode!=0 and 'heap-use-after-free' in bad.stderr,('the published-only counterfactual must fail under ASan',bad.returncode,bad.stderr[-2000:])
            print('[asan] counterfactual: only the published replays held: the constant donor freed, heap-use-after-free, as expected',flush=True)
            bad=subprocess.run([str(exe),four,'unpinned-counterfactual'],capture_output=True,text=True,timeout=300)
            assert bad.returncode!=0 and 'heap-use-after-free' in bad.stderr,('the unpinned counterfactual must fail under ASan',bad.returncode,bad.stderr[-2000:])
            print('[asan] counterfactual: programs not pinned at the abandon: the retired program freed, heap-use-after-free, as expected',flush=True)
print('PASS prepare worker: model, threads and counterfactuals')
