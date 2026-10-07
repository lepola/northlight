#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.149 render-thread instrumentation (RenderProfile). Native tests of
render_thread_probe.h and the RenderProfile/DiagReplayProbe keys at -O2 and under
ASan/UBSan, plus a source audit of the wiring: RenderProfile=0 (the default in every
preset, also with Diagnostics=1) leaves the 0.3.148 paths - the Off replay loop, the
frame%120 sample and self-check cadence, no extra clock read or counter - and the probe
cannot reach the image or fail the frame. Static analysis and native code only; no Wine,
Windows binary or game is run."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import hashlib,json,os,re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
OUT=fp.output_dir()
FILES=['render_thread_probe.h','quality_settings.h','renderer.cpp','world_renderer.h','world_replay_probe.inl','device_mirror.h',
       'extension_raw_methods.inl','generate_extension_raw_methods.py','gpu_profile.h','test_render_thread_probe.cpp','test_render_thread_probe.py']
runs=[]
with tempfile.TemporaryDirectory(prefix='northlight-render-thread-') as tmp:
    for label,flags in [('O2',['-O2']),('asan+ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(tmp)/'test'
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/'test_render_thread_probe.cpp'),'-o',str(exe)],check=True)
        out=subprocess.run([str(exe)],check=True,capture_output=True,text=True).stdout
        print(label+':\n'+out,end='',flush=True);runs.append({'flags':flags,'stdout':out})

read=lambda n:fp.tracked(n).read_text()
r,w,probe,probeH,mirror,raw,q=read('renderer.cpp'),read('world_renderer.h'),read('world_replay_probe.inl'),read('render_thread_probe.h'),read('device_mirror.h'),read('extension_raw_methods.inl'),read('quality_settings.h')
CLOCKS=('QueryPerformanceCounter','steady_clock','QpcClock','::now(')
loop=w[w.index('auto replayLoop=[&](auto& split)->bool{'):w.index('ReplaySplit split;split.timed=profiled;')]
submit=w[w.index('template<class Split,class Check> bool submitReplay('):w.index('    bool render(IDirect3DSurface9* targetSurface,')]
ticks=r[r.index('struct PresentTicks {'):r.index('static void finishDeviceFrame(IDirect3DDevice9* owner);')]
issue=probe[probe.index('NorthlightReplayDrawState::Cache probeBindings(d);'):probe.index('        if(draws==replayProbeList.size())return true;')]
probeBody=probe[probe.index('    template<class Mode> bool replayProbeIssue('):probe.index('    // endFrame(): a finished window')]
counted=[l for l in raw.splitlines() if 'm->countRaw(' in l and 'rawActive()){++m->rawCalls;' not in l]
checks={
 # gate
 'RenderProfile/DiagReplayProbe 0 in every preset; Diagnostics=0 wins; probe needs RenderProfile':'{"RenderProfile",&Settings::renderProfile,0,1,{0,0,0}},' in q
   and '{"DiagReplayProbe",&Settings::diagReplayProbe,0,1,{0,0,0}},' in q and 'inline bool renderProfile(const Settings& s){return s.diagnostics&&s.renderProfile;}' in q
   and 'inline bool replayProbe(const Settings& s){return renderProfile(s)&&s.diagReplayProbe;}' in q
   and 'NorthlightRenderThreadProbe::configure(NorthlightQuality::renderProfile(quality),NorthlightQuality::replayProbe(quality));' in w
   and 'probeFlag.store(profile&&probe,std::memory_order_relaxed);' in probeH,
 # functional cadences
 'self-check keeps frame%120==0; the diagnostic sample is a separate flag':r.count(',vs,frame%120==0,NorthlightRenderThreadProbe::sampleFrame(frame));captureWater(')==4
   and w.count('constantSelfCheck=selfCheck;sample=sample&&NorthlightDiagnostics::enabled();')==2 and 'frame%120!=60' in read('mirror_audit_schedule.h'),
 'sampler: RenderProfile=0 is frame%120==0, RenderProfile=1 the prime 127':'inline bool sampleFrame(unsigned frame,bool profile){return profile?profileFrame(frame):frame%SamplePeriod==0;}' in probeH
   and 'constexpr unsigned SamplePeriod=120,ProfilePeriod=127;' in probeH and 'bool sampled()const{return NorthlightRenderThreadProbe::sampleFrame(frame)&&NorthlightDiagnostics::enabled();}' in r
   and 'if(diagnostics())gpuProfile->beginFrame(frame,NorthlightRenderThreadProbe::sampleFrame(frame));' in r,
 # replay loop
 'replay loop: Off unless a RenderProfile sample or the probe':'const bool profiled=profileSampled(),probeRecord=replayProbeActive()&&slot==0&&captureMode==CaptureFresh&&!diagnosticCapture;' in w
   and 'if(profiled||probeRecord){if(!replayLoop(split))return false;}\n            else {NorthlightRenderThreadProbe::Off off;if(!replayLoop(off))return false;}' in w
   and 'ReplaySplit split;split.timed=profiled;' in w and 'bool profileSampled()const{return captureSampled&&NorthlightRenderThreadProbe::profiling();}' in probe
   and 'bool replayProbeActive()const{return NorthlightRenderThreadProbe::probing()&&!replayProbeDisabled;}' in probe,
 'Off hooks empty; probe modes drop calls at compile time':re.search(r'struct Off \{\s*static constexpr bool Draws=true,Calls=true;\s*void start\(\)\{\}\s*void mark\(Bucket\)\{\}\s*void constant\(Constant\)\{\}\s*template<class Packet> void drawn\(const Packet\*\)\{\}\s*\};',probeH) is not None
   and 'if constexpr(!Split::Calls)return poseConstants.prepare(*p,rows);' in submit and 'if constexpr(Split::Draws)hr=p->indexed?' in submit,
 'Split reads the clock only when timed':'void start(){if(timed)last=Clock::now();}' in probeH and 'void mark(Bucket b){if(!timed)return;const int64_t now=Clock::now();' in probeH,
 'no clock read in the replay loop or submitReplay':not any(c in loop or c in submit for c in CLOCKS),
 'whole-loop timing only when the probe records':'const int64_t loopStart=probeRecord?QpcClock::now():0;' in w and w.count('QpcClock::now()')==8, # 0.3.197: +2 for the local-light tracker: its frame time (functional, every frame) and selectUs (Diagnostics only)
 # 0.3.175: the celestial mask draw's pair, RenderProfile only
 'celestial mask timer only with RenderProfile':'const int64_t started=NorthlightRenderThreadProbe::profiling()?QpcClock::now():0;' in w and 'if(started&&captureFrequency.QuadPart>0){m.ms=double(QpcClock::now()-started)' in w,
 # 0.3.152: the other two reads are the upload window timer, RenderProfile sample frames only
 'upload window timer only on profile frames':'const int64_t uploadWindowStart=profileSampled()?QpcClock::now():0;' in w and 'if(uploadWindowStart)uploadWindowTicks=QpcClock::now()-uploadWindowStart;' in w
   and 'uploadWindowMs=%.3f' in probe and 'uploadWindowTicks<0?-1.0:double(uploadWindowTicks)*tick' in probe,
 'no log line inside the cascade: split/profile/probe lines from endFrame':'logf(' not in loop and 'logf(' not in submit and 'keepReplaySplit(slot,split,' in w
   and 'if(profileSampled())logRenderProfile();else{replayProfileUsed.clear();replayGiPacked.clear();}\n        frameStaging=frameProbeUpload=false;\n        logReplayProbeWindow();' in w
   and w.count('logRenderProfile()')==1 and w.count('logReplayProbeWindow()')==1,
 'profile logs gated':'if(profileSampled())logf("WORLD replay split ' in probe and 'if(profileSampled())logf("WORLD profile frame ' in probe
   and 'if(NorthlightRenderThreadProbe::profiling())logf("WORLD replay probe window=' in probe,
 'capture waste recorded only on profile frames':'if(sample&&NorthlightRenderThreadProbe::profiling())try{replayGiPacked.push_back(&replay);}catch(...){}' in w
   and 'if(pointUpdates!=pointUpdatesBefore&&profileSampled())profilePointUsed();' in w and 'if(profiled)split.used=&replayProfileUsed;' in w,
 # call counts
 'raw counts only on RenderProfile sample frames':'void countRaw(unsigned i){if(rawCounting)++rawMethodCalls[i];}' in mirror and r.count('mirrorState.rawCounting=')==1
   and 'mirrorState.rawCounting=sampled()&&NorthlightRenderThreadProbe::profiling();' in r and len(counted)==7
   and all('{ if(m->rawCounting&&rawActive())m->countRaw(' in l for l in counted) and raw.count('#ifndef NORTHLIGHT_DEVICE_MIRROR_TEST_API')==1,
 # per-frame times
 'Present ticks and frame cost only with RenderProfile':'bool on=NorthlightRenderThreadProbe::profiling();' in ticks and ticks.count('QueryPerformanceCounter(')==3 and ticks.count('if(on)QueryPerformanceCounter(')==3
   and 'CpuScope cpu(sampled()?&cpuEffects:nullptr);' in r and 'CpuScope frameCostScope(!sampled()&&NorthlightRenderThreadProbe::profiling()?&cpuEffects:nullptr);' in r
   and 'if(NorthlightRenderThreadProbe::profiling()&&cpuFrequency.QuadPart>0){namespace P=NorthlightRenderThreadProbe;' in r and 'if(t.on)frameCost.presented(' in r,
 'frame cost excludes sample and logging frames':'world&&world->lastFarDrawn()),sampledFrame);' in r and 'if(logged||previousDirty||pendingExcluded_){++excluded_;return;}' in probeH
   and 'static thread_local unsigned long long threadLogLines=0;' in r and '++logCost.calls;++threadLogLines;' in r,
 'both Present paths timed':r.count('PresentTicks ticks;')==2 and 'presentedDeviceFrame(owner,ticks)' in r and 'ticks.present();presented(ticks);' in r,
 # probe
 'probe after the bounds join and the sun near loop, before the union':w.index('replayBoundsJoin(); /* 0.3.143: first pointBounds reader */')<w.index('auto replayLoop=[&]')<w.index('replayProbe(rows,realLoopMs);')
   <w.index('if(!check(d->SetRenderTarget(0,shadowSurface[slot]),"shadow target"))return false;') and w.count('replayProbe(rows,realLoopMs)')==1,
 'probe GPU marks only in probing windows':'const bool marks=profile&&replayProbeMode!=NorthlightRenderThreadProbe::ProbeOff;\n                if(marks)profile->mark("SunNearLoop");replayProbe(rows,realLoopMs);if(marks)profile->mark("ReplayProbe");' in w,
 'probe target: cascade formats and size, private, restored':'static constexpr unsigned ReplayProbeSize=1024' in probe and 'D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&replayProbeTexture' in probe
   and 'D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&replayProbeDepth' in probe and all(x not in w for x in ('replayProbeSurface','replayProbeTexture','replayProbeDepth'))
   and 'SavedState save(d,&stateBlocks);' in probe and re.findall(r'SetRenderTarget\(([^)]*)\)',probe)==['0,replayProbeSurface']
   and 'SetTexture(' not in probe and 'SetSamplerState(' not in probe and 'SetRenderState(' not in probe and 'void releaseGPU(){replayBoundsAbandon();releaseReplayProbe();' in w,
 'probe never fails the frame (no check(), no failed flag; the call site ignores its result)':'check(' not in probe and 'failed=true' not in probe and 'void replayProbe(const float* rows,double realLoopMs){' in probe,
 'probe issues the same per-draw sequence, own counters':'submitReplay(p,probeBindings,probePoses,rows,mode,constantBytes,constantCalls,' in issue
   and all(x not in probe for x in ('replayDraws','replayConstantBytes','replayConstantCalls','culledReplayDraws','replaySlot','staticDedup','persistentSkipped','replayMeshKeys','replayPosePrepared')),
 'probe failures counted, disabled after 8':'ReplayProbeFailureLimit=8' in probe and 'if(++replayProbeFailures<ReplayProbeFailureLimit&&hr!=E_OUTOFMEMORY)return;' in probe,
 'sample lines tag the mirror audit frame (no skipping)':'mirrorAudit=%u counted=%d' in r and 'CPU timers frame=%u mirrorAudit=%u' in r and r.count('unsigned(sampleFrame%120==60)')==2,
 # version
 'version 0.3.197':'logf("Northlight renderer 0.3.197; reference sun look (sun glow hue from native/sunHalo band, soft-shoulder glare, veil, sun-tinted haze), native sun/moon suppressed (F1b), lamps dimmed to 30 pct in direct sun, native moon02 skipped by texture identity, no game bytes in the DLL, MEMREAD self-read profile (RenderProfile), soft sun removal in shadow, jump-stable shadow anchor, geometry coverage hold with travel lead, steadier animated shadow edges (near 5x5 tent, still-camera shadow history), native blob shadows kept at BlobShadowStrength (faint texture under modulate blend), bilinear lighting history, near capture reserve for the player and companions, remembered rigid prop shadows (drawn-by-game states, windowed held), AO and bloom folded into the world composite, ground normals reject object tops, both wide samples, batched celestial terrain mask, DXVK async left to the runtime, render-thread terrain upload and rigid bookkeeping trims, moon without the horizon stall, art layer bands retimed to the sun and moon, actor prepare on a worker, trimmed prepare handoff, in-place capture constants, gate thread census, predicted snapshot lookups, word-wise memcmp, owner-thread gate elision; abandoned-frame prepare quarantine; removal smoothing on matching normals in its own pass (35/50 degree gate); per-frame draw gates; translucent depth census; early depth for translucent actors; DXVK 3.1.1 default, dxvk2 (2.7.1) by choice only, no automatic fallback; AO depth texel snap; shadow cascades follow camera zoom and collision; reduced terrain shadow reach under address-space pressure; command-stream replay thread; draw-hook lookup caches; lighter replay retire; fresh texture shadows evict only stale keeps; soft local-light cap with fades; blended GI re-publication; backend=' in r,
}
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
print('PASS render-thread instrumentation wiring: RenderProfile=0 keeps the 0.3.148 paths; probe isolated and never fails the frame')
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'render-thread-probe-validation.json').write_text(json.dumps({'command':'python3 tests/test_render_thread_probe.py',
    'scope':'Native render_thread_probe.h + quality_settings.h with fake clocks; source audit of renderer.cpp/world_renderer.h/world_replay_probe.inl wiring. No game, Wine or D3D.',
    'runs':runs,'checks':{k:bool(v) for k,v in checks.items()},
    'source_sha256':{n:hashlib.sha256(fp.tracked(n).read_bytes()).hexdigest() for n in FILES}},indent=2)+'\n')
