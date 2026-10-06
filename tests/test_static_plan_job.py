#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.152 static shadow plans on a worker (static_plan_job.h, GpuCache::kickPlans) and per-model
plan storage. Native clang++ only; never runs the game, Wine or the DLL, writes no records.
Builds and runs the static plan proofs (plan equality, plans, incremental, prebuild) and the
randomized synchronous-vs-worker proof (test_static_plan_job.cpp) at -O2, ASan+UBSan and TSan,
then audits the joins: every non-const public GpuCache method settles the worker first."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import platform,re,subprocess,sys,tempfile
HERE=Path(__file__).resolve().parent
gpu=fp.src('static_shadow_gpu.h').read_text()
job=fp.src('static_plan_job.h').read_text()
w=fp.src('world_renderer.h').read_text()
results=[]
def check(name,ok,detail=''):
    results.append((name,bool(ok)));print(('PASS ' if ok else 'FAIL ')+name+(' | '+detail if detail else ''),flush=True)

# --- source audit ---------------------------------------------------------------------------
cls=gpu[gpu.index('class GpuCache {'):gpu.rindex('} // namespace StaticShadow')]
public=[];mode='private';depth=0;i=0
# Public member functions at class depth: name, const, body (methods of nested classes excluded).
for m in re.finditer(r'\{|\}|\n(public|private):',cls):
    tok=m.group(0)
    if tok=='{':depth+=1
    elif tok=='}':depth-=1
    elif depth==1:mode=m.group(1)
    if tok=='{' and depth==2 and mode=='public':
        head=cls[cls.rfind('\n',0,m.start())+1:m.start()]
        f=re.match(r'\s*(?:static\s+)?(?:[\w:<>,\*&\s]+?\s)?[&\*]?(\w+)\((.*)\)\s*(const)?\s*$',head)
        if not f or f.group(1) in ('GpuCache','if','for','while','switch') or head.lstrip().startswith(('struct','class','bool contains','~')):continue
        public.append((f.group(1),bool(f.group(3)),head.strip().startswith('static'),cls[m.end():m.end()+40]))
mutators=[(n,b) for n,c,st,b in public if not c and not st]
names=sorted({n for n,_ in mutators})
# draw() only reads its own matrix's plan (prepare joins that item) and writes D3D state and
# counters the worker never reads (instancing is snapshotted at the kick): it must not join all.
unjoined=[n for n,b in mutators if n!='draw' and not re.match(r'\s*(mutate\(|settle\()',b)]
check('every non-const public GpuCache method settles the plan worker first',not unjoined and {'update','reset','setCoveredOwners','setCoveredPlacements','discardPlan','kickPlans'}<=set(names),f'{len(names)} methods: {" ".join(names)}; unjoined: {unjoined}')
check('draw joins only its matrix (prepare), never the whole job','const auto& plan=prepare(matrix);stats_.culled=plan.culled;' in gpu and 'const bool installed=asyncCount_&&joinMatrix(matrix);' in gpu)
check('reset drops finished builds (exact old identity); others install',"void reset(){mutate(true);" in gpu and 'else if(discard){slot=Plan{};slot.occupied=item.savedOccupied;slot.valid=item.savedValid;' in gpu)
check('worker never touches plans_/stats_/D3D',all(x not in gpu[gpu.index('    void runItem(unsigned i)const{'):gpu.index('    bool joinMatrix(')] for x in ('plans_','stats_','->Lock','SetRenderState','device_','canInstance_','epoch_')))
check('pinned slots are never LRU victims','if(plan.pinned)continue;' in gpu and 'if(!plan.pinned)return plan;' in gpu)
check('two cores or fewer: always synchronous','bool asyncPlans_=NorthlightStaticPlanJob::Async&&NorthlightStream::cores()>2;' in gpu and 'if(!asyncPlans_||!lruPlans_||' in gpu)
check('failed or stolen items get their exact old entry back','if(state==NorthlightStaticPlanJob::State::Stolen||item.failed){slot=std::move(item.saved);if(item.evicted)--stats_.planEvictions;' in gpu
      and 'catch(...){returnNodes(ctx,previous,next);for(const auto& move:ctx.chunkMoves)previous.chunks[move.from]=std::move(next.chunks[move.to]);' in gpu)
check('worker: one thread, claims under the mutex, steal-back of pending items','if(states_[i]==State::Pending){states_[i]=State::Stolen;return State::Stolen;}' in job and 'states_[i]=State::Running;}' in job and job.count('std::thread(')==1)
check('worker: an undequeued job with nothing pending is retracted, not awaited','if(queued_){bool pending=false;for(unsigned i=0;i<count_;++i)pending|=states_[i]==State::Pending;if(!pending){queued_=false;++retractions_;}}' in job)
check('renderer: slot matrices once per frame, kick before the loop, RAII join before any return',
      w.count('NorthlightWorldMath::shadowCachePlacement(')==1 and 'const float* cachedMatrix=staticSlotMatrix[slot];' in w
      and w.index('struct StaticPlanJoin {WorldRenderer& r;~StaticPlanJoin(){try{r.staticCasters.settle();}catch(...){}}} staticPlanJoin{*this};')<w.index('staticPlanKick(slotPlacement,sourceActive);staticKickMs+=')<w.index('for(int source=0;source<2;++source){\n          if(!effects.shadows||!sourceActive[source])continue;'))
check('renderer: settled before static updates, device release, key invalidation and mesh clears',
      'staticCasters.settle(); /* 0.3.152: no plan job across the static cache update */' in w and 'void releaseGPU(){replayBoundsAbandon();releaseReplayProbe();staticCasters.settle();' in w
      and 'void invalidateShadowCache(){staticCasters.settle();dropStaticDirtyJobs();' in w and 'void clearMesh(){staticCasters.settle();dropStaticDirtyJobs();' in w)
check('renderer: worker dirty rects for eligible slots, used only when current',
      'StaticCacheDirtyRects&&staticScissorCaps==1&&!placements[slot].reason&&key.valid&&key.staticContent.valid' in w
      and 'if(job.ready&&job.frame==staticFrame&&staticCasters.kickedModeCurrent()&&' in w and 'dirtyRectsFrom(job.work(),shadowCacheKey[slot],staticSlotMatrix[slot],true,false,Traced{view,threw})' in w)
check('PLAN COST log: split and worker fields','reuseMs=%.3f rebuildMs=%.3f walkMs=%.3f asyncKicks=%llu asyncBuilds=%llu stolen=%llu asyncFailures=%llu waitMs=%.3f workerMs=%.3f asyncDirtyRects=%llu kickMs=%.3f' in w)

# --- native proofs --------------------------------------------------------------------------
configs=[('O2',['-O2']),('asan+ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']),('tsan',['-O1','-g','-fsanitize=thread'])]
proofs=[('plan_equality','test_static_shadow_plan_equality.cpp','plan equality:'),('plans','test_static_shadow_plans.cpp','shadow plans:'),
        ('incremental','test_static_shadow_incremental.cpp','incremental static plans:'),('prebuild','test_static_shadow_prebuild.cpp','steps=256:'),
        ('plan_job','test_static_plan_job.cpp','static plan job:')]
with tempfile.TemporaryDirectory(prefix='northlight-0.3.152-') as tmp:
    for label,flags in configs:
        for name,source,marker in proofs:
            exe=Path(tmp)/f'{name}-{label.replace("+","_")}'
            defines=[] if '#define STATIC_SHADOW_GPU_TEST' in fp.tracked(source).read_text() else ['-DSTATIC_SHADOW_GPU_TEST']
            b=subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*defines,*fp.test_include_flags(),'-I',str(fp.SUPPORT),str(HERE/source),str(fp.src('static_shadow_scene.cpp')),str(fp.src('world_gi.cpp')),'-o',str(exe)],capture_output=True,text=True)
            if b.returncode:
                if label=='tsan':check(f'{name} [{label}] build','',f'TSan unavailable on this toolchain? {(b.stdout+b.stderr).strip().splitlines()[-1:]}')
                else:check(f'{name} [{label}] build','',(b.stdout+b.stderr).strip()[-400:])
                continue
            r=subprocess.run([str(exe)],capture_output=True,text=True)
            out=(r.stdout+r.stderr).strip()
            ok=r.returncode==0 and marker in r.stdout and 'ThreadSanitizer' not in out and 'AddressSanitizer' not in out and 'runtime error' not in out
            check(f'{name} [{label}]',ok,[l for l in out.splitlines() if marker in l or 'Sanitizer' in l or 'FAIL' in l][-1:][0][:300] if out else '')
    # R5: identity to 0.3.151. The equality proof's random stream, hashing every plan digest and
    # every fake-D3D draw record (instance bytes), must give the hash base 0.3.151 gives with
    # this toolchain (Apple clang 21; the per-arch values differ by FP contraction only).
    BASE_0_3_151={'arm64':3166779223232308360,'x86_64':7905305124395154475}
    src=(HERE/'test_static_shadow_plan_equality.cpp').read_text()
    edits=[('static uint32_t rng=0x5eed137;','static uint32_t rng=0x5eed137;\nstatic uint64_t H=1469598103934665603ull;static void hs(const std::string& x){for(unsigned char c:x)H=(H^c)*1099511628211ull;}'),
           ('assert(x==y);','assert(x==y);hs(x);'),
           ('drawRecords+=pair.fd.drawnRecords.size();','drawRecords+=pair.fd.drawnRecords.size();for(const auto& r:pair.fd.drawnRecords)hs(std::string(reinterpret_cast<const char*>(r.transform.data()),48)+std::to_string(r.firstIndex)+"/"+std::to_string(r.pixel)+"/"+std::to_string(r.primitives));'),
           ('std::cout<<"plan equality: "','std::cout<<"HASH "<<H<<"\\n";std::cout<<"plan equality: "')]
    for a,b in edits:
        assert src.count(a)==1,f'equality proof changed: {a}';src=src.replace(a,b)
    (Path(tmp)/'plan_hash.cpp').write_text(src)
    for arch in ['arm64','x86_64'] if platform.machine()=='arm64' else [platform.machine()]:
        exe=Path(tmp)/f'plan_hash-{arch}'
        b=subprocess.run(['clang++','-arch',arch,'-std=c++17','-O2',*fp.test_include_flags(),'-I',str(fp.SUPPORT),str(Path(tmp)/'plan_hash.cpp'),str(fp.src('static_shadow_scene.cpp')),str(fp.src('world_gi.cpp')),'-o',str(exe)],capture_output=True,text=True)
        got=re.search(r'HASH (\d+)',subprocess.run([str(exe)],capture_output=True,text=True).stdout) if not b.returncode else None
        want=BASE_0_3_151.get(arch)
        check(f'plan bytes and draw records identical to base 0.3.151 [{arch}]',got and want is not None and int(got.group(1))==want,f'hash {got.group(1) if got else b.stderr.strip()[-200:]} base {want}')
# 0.3.192 (DXVK3): the GpuCache lifecycle test also proves the Instances LOCK METER charge (instanceDiscards x buffer bytes).
check('instance DISCARD is metered at the whole buffer',"if(flags==D3DLOCK_DISCARD)NorthlightLockMeter::discard(NorthlightLockMeter::Instances,std::uint64_t(instanceCapacity_)*sizeof(Instance));" in gpu)
with tempfile.TemporaryDirectory(prefix='northlight-static-gpu-') as tmp:
    exe=Path(tmp)/'static_shadow_gpu'
    b=subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG','-O2',*fp.test_include_flags(),'-I',str(fp.SUPPORT),str(HERE/'test_static_shadow_gpu.cpp'),str(fp.src('world_gi.cpp')),'-o',str(exe)],capture_output=True,text=True)
    r=subprocess.run([str(exe)],capture_output=True,text=True) if not b.returncode else b
    check('static shadow GPU cache incl. Instances meter == instanceDiscards x capacity x 48',r.returncode==0 and 'tests passed' in r.stdout,(r.stdout+r.stderr).strip()[-300:])
failed=[n for n,ok in results if not ok]
print(f'\n{len(results)} checks, {len(failed)} failed')
if failed:sys.exit(1)
print('PASS static plan worker: synchronous-identical plans, joins and failure paths')
