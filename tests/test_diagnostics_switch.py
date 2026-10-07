#!/usr/bin/env python3
# northlight-test:
"""Diagnostics=0 source audit (0.3.141). Every production logf() is either behind a
diagnostics gate (sampled()/diagnostics()/NorthlightDiagnostics::enabled()/captureSampled,
itself false when Diagnostics=0) or in the labelled allow-list below (start-up,
settings, errors/warnings, capped first-N rejects, one-off events, user-triggered,
or reachable only from a gated caller). A new ungated periodic line fails this test.
Also checks the non-log diagnostic work is gated and the functional mirror audit is
not. Prints the full inventory. Static source analysis only; nothing is run."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import re,sys
HERE=Path(__file__).resolve().parent
FILES=['renderer.cpp','world_renderer.h','world_shadow_experiment.inl','world_point_rendering.inl','celestial_disc_renderer.h',
       'shadow_blob_filter.h','water_renderer.h','gpu_profile.h','world_diagnostics.h','world_replay_probe.inl','world_rigid_memory.inl']
# 0.3.149: profiling()/profileSampled() = RenderProfile, which requires Diagnostics=1 (NorthlightQuality::renderProfile).
GATES=('NorthlightDiagnostics::enabled()','diagnostics()','sampled()','captureSampled','if(diagnostics)','shadowFate.active()','sampledFrame','profiling()','profileSampled()')
# Ungated lines that stay with Diagnostics=0: format prefix -> label.
KEEP={
 'LOGGER intervalMs':'indirect: reportLogCost() runs only in the gated MIRROR block',
 'EXTENSION fault':'error','DISABLED:':'error','Resources ':'one-off: resource (re)creation',
 'LOCK METER':'one-off startup line and ProcessVertices warning; the interval line runs only from the Diagnostics-gated block of WorldRenderer::endFrame (0.3.192)',
 'VIEWPORT GATE':'capped: first 8','WORLD skipped frame':'capped: first 8 (periodic tail gated)',
 'WORLD skip episode':'capped: first 32 runs of skipped world frames (tail gated; 0.3.169)','WORLD coverage hold':'capped: first 32 hold/retire episodes (tail gated; 0.3.169)',
 'FIRST EFFECT FRAME':'one-off','GI probe blend texture unavailable':'one-off warning: the 0.3.197 blend texture failed to allocate (static once flag)','MIRROR mismatch':'error (the audit itself is functional and ungated)',
 'WORLD non-caster draw rejected':'capped: first 4 (periodic tail gated)','Projection rejected':'capped: first',
 'D3D9 device wrapped':'start-up','MEMORY async sampler':'error','MEMORY guard':'warning: low address space (pressure/trim/after-trim/recovery, cooldown-limited)',
 'LOG previous session':'start-up: previous log rotation result','DEVICE lifetime':'one-off: device create/destroy',
 'Reset HRESULT':'one-off: Reset','Effects components':'user-triggered: Ctrl+Shift+F7..F9','Effects %s':'user-triggered: Ctrl+Shift+F10',
 'World debug':'user-triggered: Ctrl+Shift+F12','MEMORY frame':'gated: diagnostics() in the same condition','MEMORY sample failed':'gated: diagnostics() in the same condition',
 'Effects retry':'user-triggered','MIRROR fallback':'one-off error','SHADOWBLOB draw signature':'capped: first 4',
 'Backend capabilities':'start-up','UNSUPPORTED BACKEND':'error','CreateDevice HRESULT':'start-up','DXVK compatibility':'start-up',
 'Northlight renderer':'start-up version line','Direct3D9Ex requested':'start-up',
 'BACKEND candidate':'start-up: one line per backend load attempt','BACKEND selected':'start-up: loaded backend','BACKEND SELF-LOAD REFUSED':'start-up error',
 'BACKEND RECURSION':'one-off: first re-entered export','HOST exe':'start-up: wow.exe identity','PROXY module':'start-up: proxy location',
 'PROXY WARNING':'start-up warning','GAME d3d9.dll':'start-up: game-folder d3d9.dll identity','GAME WARNING':'start-up warning',
 'WORLD shadow cache VERIFY MISMATCH':'error (debug verify)','GEOMETRY MEMORY':'warning: allocation deferral',
 'STATIC SHADOW request deferred':'warning: allocation failure','STATIC SHADOW upload deferred':'warning: allocation failure',
 'CSTREAM active':'start-up one-off: the replay thread runs (0.3.192)','CSTREAM disabled':'start-up one-off: why the command stream is off (0.3.192)',
 'QUALITY':'settings','WORLD replacement deferred':'warning','WORLD pending mesh released':'event: orphaned staged upload released (0.3.156), at most once per geometry snapshot','WORLD geometry stalled':'warning: once per generation-admission stall episode (0.3.156 watchdog)','WORLD terrain allocation requestMiB':'warning: allocation deferred',
 'WORLD shadow terrain reach':'warning: terrain shadow reach reduced/restored under address-space pressure (0.3.190), at most one pair per 30 s backoff','WORLD geometry memory stall':'warning: one begin/end pair per geometry-memory stall episode (0.3.190)',
 'GI actor BVH rejected':'warning','WORLD DISABLED':'error','WORLD streaming retry':'capped: first 12','SHADOW experiment':'settings / error',
 'CELESTIAL profiles loaded':'start-up settings','SHADOW regional terrain loaded':'start-up settings','WORLD explicit recovery':'user-triggered',
 'WORLD worker stopped':'error','WORLD context validated':'one-off','WORLD cache: %s':'worker error message',
 'TERRAIN SHADOW patched':'capped: first 4','TERRAIN UP snapshot rejected':'capped: first 12','TERRAIN snapshot rejected':'capped: first 12',
 'TERRAIN projection':'capped: first 12','STATIC SHADOW draw retry':'capped: first 8','MODEL snapshot rejected':'capped: first 12',
 'WORLD GPU diagnostic':'user-triggered GPU capture (F12 debug)','WORLD slow submission':'capped: first 12','POINT pass skipped':'capped: first 12',
 'CELESTIAL disabled':'error','CELESTIAL native texture identity':'error','CELESTIAL early draw skipped':'capped: first 4',
 'SHADOWBLOB candidate':'capped: first 4','SHADOWBLOB identified':'capped: first 8','SHADOWBLOB draw states':'capped: first 2','SHADOWBLOB faint texture unavailable':'capped: first 3 failures (the creation is retried every 600 frames)','WATER disabled':'error','WATER explicit recovery':'user-triggered',
 'PREPARE worker':'warning/error: the prepare worker watchdog (at most 5 a session), its re-arm (at most 4) or a record exception (first 4); 0.3.177', 'WATER registered':'one-off: shader registration','WATER mask patch skipped':'capped: first 8 (patch rejected or patched hash mismatch; that shader only)','GPU profile':'gated: no sample opens when off (beginFrame/poll gated)','%s':'gpu_profile report: gated as above; 0.3.176 flushDeferredLogs(): lines formatted at their gated deferLogf sites',
}
def conditions(s,pos):
    out=[];j=max(s.rfind(';',0,pos),s.rfind('{',0,pos),s.rfind('}',0,pos));out.append(s[j+1:pos])
    depth=0;k=pos
    while k>0:
        k-=1;c=s[k]
        if c=='}':depth+=1
        elif c=='{':
            if depth==0:j=max(s.rfind(';',0,k),s.rfind('{',0,k),s.rfind('}',0,k));out.append(s[j+1:k])
            else:depth-=1
    return out
gated,kept,unknown=[],[],[]
for f in FILES:
    s=fp.src(f).read_text()
    for m in re.finditer(r'\b(?:logf|deferLogf)\(',s):
        if re.search(r'(void|Include after the renderer\'s|Include after)\s*$',s[max(0,m.start()-40):m.start()]):continue
        fmt=re.match(r'(?:logf|deferLogf)\("([^"]{0,60})',s[m.start():]);fmt=fmt.group(1) if fmt else s[m.start():m.start()+30].replace('\n',' ')
        if fmt.startswith('const char*'):continue # comment text
        where=f'{f}:{s.count(chr(10),0,m.start())+1}'
        if any(g in c for c in conditions(s,m.start())[:6] for g in GATES):gated.append((where,fmt));continue
        label=next((v for k,v in KEEP.items() if fmt.startswith(k)),None)
        (kept if label else unknown).append((where,fmt,label))
print(f'GATED (off with Diagnostics=0): {len(gated)} log sites');[print('  LOG ',w,f) for w,f in gated]
print(f'KEPT with Diagnostics=0: {len(kept)}');[print('  ',w,f,'--',l) for w,f,l in kept]
assert not unknown,'ungated, unlabelled log sites:\n'+'\n'.join(f'{w} {f}' for w,f,_ in unknown)
# Non-log diagnostic work (MEASUREMENT / COUNTER) must be gated; functional work must not be.
r=fp.src('renderer.cpp').read_text();w=fp.src('world_renderer.h').read_text()
checks={
 'CpuScope timings use sampled()':all('CpuScope' not in l or 'sampled' in l or 'diagnostics()' in l for l in r.splitlines() if 'CpuScope ' in l and '(' in l and 'struct' not in l),
 'world capture: raw sample frame in, diagnostics gated inside':r.count(',vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame));captureWater(')==4 and w.count('constantSelfCheck=selfCheck;sample=sample&&NorthlightDiagnostics::enabled();')==2,
 'constant self-check keeps its 0.3.140 cadence (0.3.180 C0: its own flag and serial; the profile Stats only on diagnostic samples)':'},sample?&constantEpochProfile:nullptr,constantSelfCheck,constantSelfCheckState))return;' in w,
 'GPU timestamp queries only when on':'if(diagnostics())gpuProfile->beginFrame(frame,NorthlightRenderThreadProbe::sampleFrame(frame))' in r and 'if(diagnostics())gpuProfile->poll();' in r,
 'async memory sampler always on (memory guard), periodic line only when on':'try{memoryDiagnostics=std::make_unique<NorthlightMemoryDiagnostics::Sampler>(&queryAddressSpace);}' in r and 'if(diagnostics())try{memoryDiagnostics=' not in r and 'if(decision.report&&diagnostics())logf("MEMORY frame=' in r and 'else if(diagnostics())logf("MEMORY sample failed' in r,
 'frame interval sampling only when on':'if(diagnostics()&&QueryPerformanceCounter(&intervalTick)&&frameIntervals.sample(' in r,
 'mirror audit stays functional (ungated)':'mirrorAuditSchedule.afterWorldCapture(frame,' in r and not re.search(r'diagnostics\(\)[^;]*mirrorAuditSchedule',r),
 'command stream: the periodic CSTREAM line and the state audit only when on; the other CSTREAM lines are capped errors (via the options.log lambda, whose logf is labelled %s)':(lambda t:
    'if(audit&&diag&&frames%sampleEvery==1)runAudit();' in t and 'if(diag&&log&&frames%sampleEvery==0)cstreamLine(frames);' in t and t.count('cstreamLine(')==2
    and 'const bool diag=diagnostics&&diagnostics();' in t and 'options.diagnostics=&NorthlightDiagnostics::enabled;' in r and 'deadLogged_<8' in t and 'AttachThreadInput(' not in t)(fp.src('replay_thread.h').read_text()),
 'Diagnostics read once at quality load':'NorthlightDiagnostics::configure(quality.diagnostics!=0);' in w,
 'fate tracker: Diagnostics=0 wins':'shadowFateDiagnostics=NorthlightQuality::shadowFate(quality);' in w,
 'streaming phase clocks gated':'const bool on=NorthlightDiagnostics::enabled();' in fp.src('streaming_phase_profile.h').read_text(),
 'no PERSISTENT lines left (the persistent casters retired in 0.3.172)':all('PERSISTENT' not in fp.src(f).read_text() for f in FILES),
 'RenderProfile needs Diagnostics (its log gates count as diagnostics gates)':'inline bool renderProfile(const Settings& s){return s.diagnostics&&s.renderProfile;}' in fp.src('quality_settings.h').read_text(),
 'near capture reserve counters only on the sampled MODEL frame capture line':(lambda t:t.count('nearAdmitted=%u nearBytes=%zu nearRefused=%u nearSelf=%d nearReserve=%zu')==1
   and 'if(captureSampled)logf("MODEL frame capture skinnedCandidates=' in t[t.rindex('\n',0,t.index('nearAdmitted=%u')):t.index('nearAdmitted=%u')])(w),
 'RIGID event lines (0.3.173): Diagnostics only, rate-limited and capped; recording off otherwise':(lambda t:t.count('deferLogf("RIGID event ')==1
   and 'if(NorthlightDiagnostics::enabled()){rigidMemory.takeEvents(rigidEvents);' in t and 'rigidMemory.events(NorthlightDiagnostics::enabled());' in t
   and 'if(!rigidEventTokens||rigidEventLines>=RigidEventLines){++rigidEventSuppressed;continue;}' in t)(fp.src('world_rigid_memory.inl').read_text()),
 'celestial mask counters (0.3.175): on the gated CELESTIAL line, the clock only with RenderProfile':(lambda t:'const int64_t started=NorthlightRenderThreadProbe::profiling()?QpcClock::now():0;' in t
   and 'if(NorthlightDiagnostics::enabled()&&(!m.lastLog||now-m.lastLog>=10000)){m.lastLog=now;' in t)(w),
 'point/envelope diagnostics gated':'const bool diagnostics=NorthlightDiagnostics::enabled()&&(frames==0||frames%120==0);' in fp.src('world_point_rendering.inl').read_text(),
}
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
print('PASS Diagnostics=0 audit: every periodic line gated, only start-up/settings/error/capped/event/user-triggered lines remain')

# 0.3.176 (U0/S0): no log write inside the selection and upload effects buckets. From the start of
# WorldRenderer::render() to its Upload mark (the selection span, then the upload span), every
# function reachable by an unqualified call (member functions of the world renderer's files; calls on
# other objects and namespaces excluded) is searched for logf(. Early exits (blocks ending in
# return false) are excluded: they never reach the Upload mark, so their time rolls into a later
# bucket. What remains may only be error or one-off event lines; the sampled lines (MODEL GPU
# cache/bulk/policy/clears, MODEL shadow actors/selection, RIGID memory/event, WORLD actor packets)
# are deferLogf, written by flushDeferredLogs() from endFrame(). Counterfactual: with deferLogf
# turned back into logf the audit must find those lines.
SPAN_FILES=['world_renderer.h','world_shadow_experiment.inl','world_point_rendering.inl','world_replay_probe.inl','world_rigid_memory.inl','world_diagnostics.h']
SPAN_ALLOWED={'PREPARE worker':'0.3.177: watchdog (at most 5 a session) / record exception (first 4)','WORLD DISABLED':'error (check())','GEOMETRY MEMORY':'warning: allocation deferral','WORLD pending mesh released':'event (0.3.156)',
 'SHADOW experiment selection allocation failed':'error','WORLD streaming retry':'capped error','WORLD staged mesh committed':'event: one per commit',
 'GI probe blend texture unavailable':'one-off warning (0.3.197: static once flag)'}
SPAN_DEFERRED=['MODEL GPU cache','MODEL bulk sharing','MODEL GPU policy','MODEL GPU clears','MODEL shadow actors','MODEL shadow selection','RIGID memory','RIGID event','WORLD actor packets']
KEYWORDS={'if','for','while','switch','return','catch','sizeof','defined','decltype','static_assert','alignof','noexcept','do','else','try','new','delete'}
def uncomment(t):return re.sub(r'/\*.*?\*/','',re.sub(r'//[^\n]*','',t),flags=re.S)
def span_logs(texts):
    defs={}
    for t in texts:
        for m in re.finditer(r'\b([A-Za-z_]\w*)\s*\(([^;{}()]*(?:\([^;{}()]*\)[^;{}()]*)*)\)\s*(?:const\s*)?(?:noexcept\s*)?(?:->\s*[\w:<>*&\s]+)?\{',t):
            pre=t[max(0,m.start()-200):m.start()]
            if m.group(1) in KEYWORDS or not re.search(r'[\w>*&]\s*$',pre) or re.search(r'(=|return|,|\()\s*$',pre):continue
            i=m.end()-1;depth=0
            for k in range(i,len(t)):
                depth+=(t[k]=='{')-(t[k]=='}')
                if depth==0:break
            defs.setdefault(m.group(1),[]).append(t[i:k+1])
    w=texts[0];a=w.index('{',w.index('bool render(IDirect3DSurface9* targetSurface'));b=w.index('bucket(NorthlightEffectsBuckets::Upload);',a)
    root=re.sub(r'\{[^{}]*return false;\s*\}','{}',w[a:b])
    seen=set();stack=[root];logs=set()
    while stack:
        body=stack.pop()
        logs.update(re.findall(r'\blogf\(\s*"([^"]{0,60})',body))
        for m in re.finditer(r'(?<![\w.>:])([A-Za-z_]\w*)\s*\(',body):
            if m.group(1) in defs and m.group(1) not in seen:seen.add(m.group(1));stack.extend(defs[m.group(1)])
    return logs,seen
texts=[uncomment(fp.src(f).read_text()) for f in SPAN_FILES]
logs,reached=span_logs(texts)
print(f'U0/S0 span audit: {len(reached)} functions reachable in the selection and upload spans')
for fmt in sorted(logs):print('  SPAN LOG',fmt,'--',next((v for k,v in SPAN_ALLOWED.items() if fmt.startswith(k)),'NOT ALLOWED'))
bad=[fmt for fmt in logs if not any(fmt.startswith(k) for k in SPAN_ALLOWED)]
counter,_=span_logs([t.replace('deferLogf(','logf(') for t in texts])
missed=[k for k in SPAN_DEFERRED if not any(fmt.startswith(k) for fmt in counter)]
ra=fp.src('renderer.cpp').read_text();ao=ra.index('effectsBuckets.mark(Bucket::AO);')
span_checks={
 'U0/S0: no logf( reachable between the selection/upload bucket marks except errors and one-off events':not bad,
 'U0/S0 counterfactual: the sampled lines are reachable there when not deferred':not missed and {'selectShadowReplays','uploadReplay','rigidMemoryInject','finishActorScene','uploadLiveTerrain'}<=reached,
 'U0/S0: the proxy has no log line from the AO mark to world->render()':'logf(' not in ra[ao:ra.index('world->render(',ao)],
 'U0/S0: deferred lines are written from endFrame() before anything else':'void endFrame(bool retainPool=true){\n        flushDeferredLogs();' in fp.src('world_renderer.h').read_text(),
}
for name,ok in span_checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(span_checks.values()),(bad,missed)

