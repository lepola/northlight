#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.200 (jobs) job system (job_system.h) and its renderer wiring. Native clang++ only (-O2, ASan+UBSan,
TSan when the toolchain has it): worker count, correctness with the inline fallback, wait-help (the waiting
thread runs a queued job while the only worker is held), exception safety, nested waits, no allocation per
job, shutdown draining, interleaved counters. Then a source audit: the header is portable (no D3D, no Win32),
ReplayJobs=0 never touches the pool, and the jobs read only frozen inputs and never call the device."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
h=fp.src('job_system.h').read_text()
w=fp.src('world_renderer.h').read_text()
r=fp.src('renderer.cpp').read_text()
results=[]
def check(name,ok,detail=''):
    results.append((name,bool(ok)));print(('PASS ' if ok else 'FAIL ')+name+(' | '+detail if detail else ''),flush=True)

# --- header audit ---------------------------------------------------------------------------
check('portable header: no D3D, Win32 or allocation per job',all(x not in h for x in ('d3d9','windows.h','IDirect3D','std::function','new ','malloc','std::vector')))
check('worker count clamp(cores-2,1,4) = clamp(hw-3,1,4) with the stream (cores: NorthlightStream::cores()), capacity a power of two','inline unsigned workerCount(unsigned cores){return cores<=3?1u:std::min(cores-2,4u);}' in h
      and 'hardware_concurrency()' not in re.sub(r'//[^\n]*','',h)
      and re.search(r'Capacity=(\d+);',h) and (lambda n:n&(n-1)==0)(int(re.search(r'Capacity=(\d+);',h).group(1))))
check('the counter decrement is the job\'s last touch; a throw marks it failed','try{job.fn(job.arg);}catch(...){job.counter->failed_.store(true,std::memory_order_release);}' in h
      and h.index('job.counter->pending_.fetch_sub(1,std::memory_order_acq_rel);')>h.index('jobs_.fetch_add(1,std::memory_order_relaxed);'))
check('wait helps (pops and runs) instead of sleeping','while(!counter.done()){Job job;if(pop(job))run(job,1);else std::this_thread::yield();}' in h)

# --- renderer wiring ------------------------------------------------------------------------
render=w[w.index('    bool render(IDirect3DSurface9* targetSurface,'):]
check('lazy start on the renderer thread inside render() after the upload, never at construction',w.count('replayJobs_.start(')==1 and w.count('jobsStart()')==2
      and 'const unsigned cores=NorthlightStream::cores();' in w and 'if(replayJobs_.start(cores))logf("JOBS workers=%u cores=%u"' in w and 'const bool jobs=jobsStart();' in render
      and render.index('const bool jobs=jobsStart();')>render.index('bucket(NorthlightEffectsBuckets::Upload);'))
check('ReplayJobs=0 or no worker: nothing kicked','bool jobsOn()const{return quality.replayJobs!=0&&replayJobs_.started();}' in w
      and w.count('replayJobs_.kick(')==3 and all('if(jobs)replayJobs_.kick(' in l or 'job.kicked=true;replayJobs_.kick(' in l for l in w.splitlines() if 'replayJobs_.kick(' in l)
      and 'if(jobs&&!replayCullsKicked)' in render)
kicked=sorted(m.group(1) for m in re.finditer(r'replayJobs_\.kick\([\w.]+,(\w+)\)',w))
check('three jobs: atmosphere, terrain candidates, replay cull',kicked==['atmosphereJob','job','terrainJob'],' '.join(kicked))
check('each body inline at its old place with ReplayJobs=0, joined there (or at the first reader) with 1',
      'if(jobs)replayJobs_.wait(atmosphereDone);else atmosphereWork(atmosphere,nearZ,w,h,debug);' in render
      and 'if(!jobs)terrainJob();' in render and render.index('if(!jobs)terrainJob();')<render.index('bucket(NorthlightEffectsBuckets::TerrainSelection);')
      and render.index('if(jobs)replayJobs_.wait(terrainDone);')<render.index('if(!terrainCandidates.forEach(')
      and 'if(jobs&&!replayCullsKicked){replayCullsKicked=true;replayCullKick(slot,sourceActive);}' in render
      and render.index('replayBoundsJoin(); /* 0.3.143: first pointBounds reader */')<render.index('replayCullKick(slot,sourceActive)')
      and '(cull?(cull[index]&1)!=0:StaticShadow::containsBounds(cachedMatrix,p->staticProofLow,p->staticProofHigh))' in render
      and '(cull?(cull[index]&2)!=0:NorthlightShadowBounds::clipReject(' in render)
check('a later slot uses its job only when both matrices still match',
      '!std::memcmp(job.matrix,matrix,sizeof job.matrix)&&!std::memcmp(job.cached,cached,sizeof job.cached)?job.flags.data():nullptr' in w)
check('the veil keeps both sums; the bank picks by the final activity (ensureCloudNoise on the renderer thread)',
      'skyTransmittanceFrame=cf.active?atmosphere.veil[1]:atmosphere.veil[0];' in render and 'cf.active=cf.active&&effects.fog&&debug==0&&fogCloudsPS&&ensureCloudNoise();' in render
      and 'a.veil[0]=std::exp(-depth);a.veil[1]=std::exp(-cloudyDepth);' in w and 'cloudy+=NorthlightFogClouds::sigmaAt(' in w)
for name in ('void atmosphereWork(AtmosphereFrame& a,float nearZ,UINT w,UINT h,int debug){','void replayCullWork(ReplayCull& job){'):
    start=w.index(name);body=re.sub(r'/\*.*?\*/','',re.sub(r'//[^\n]*','',w[start:w.index('\n    }\n',start)]),flags=re.S)
    check(f'no device call or log in {name.split("(")[0].split()[-1]}','d->' not in body and 'check(' not in body and 'ensureCloud' not in body and 'logf(' not in body and 'Lock' not in body)
check('every kicked job is joined before render() returns (guards)',w.count('struct JobJoin')==1 and render.count('JobJoin ')==2
      and 'struct CullSettle {WorldRenderer& r;~CullSettle(){r.replayCullSettle();}} cullSettle{*this};' in render)
jl=r.index('logf("JOBS frame=%u jobs=%u jobMs=%.3f joinWaitMs=%.3f replayMs=%.3f"')
check('JOBS line on RenderProfile sample frames only',r.count('logf("JOBS frame=')==1 and r.rindex('if(sampledFrame&&cpuFrequency.QuadPart>0){',0,jl)<r.rindex('if(NorthlightRenderThreadProbe::profiling()){',0,jl)<jl<r.index('logf("D3D calls frame=',jl))

# --- native proof ---------------------------------------------------------------------------
configs=[('O2',['-O2']),('asan+ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']),('tsan',['-O1','-g','-fsanitize=thread'])]
with tempfile.TemporaryDirectory(prefix='northlight-jobs-') as tmp:
    for label,flags in configs:
        exe=Path(tmp)/f'jobs-{label.replace("+","_")}'
        b=subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),str(HERE/'test_job_system.cpp'),'-o',str(exe)],capture_output=True,text=True)
        if b.returncode:
            if label=='tsan':print(f'SKIP job system [{label}] build: TSan unavailable on this toolchain? {(b.stdout+b.stderr).strip().splitlines()[-1:]}');continue
            check(f'job system [{label}] build','',(b.stdout+b.stderr).strip()[-600:]);continue
        run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=300)
        out=(run.stdout+run.stderr).strip()
        check(f'job system [{label}]',run.returncode==0 and 'PASS job system' in run.stdout and 'Sanitizer' not in out and 'runtime error' not in out,out.splitlines()[-1][:300] if out else '')
assert all(ok for _,ok in results),'failed: '+', '.join(n for n,ok in results if not ok)
print('PASS job system: native proof and wiring')
