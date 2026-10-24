#!/usr/bin/env python3
# northlight-test:
"""0.3.200 (gpu budget): wiring of GpuBudgetMs - source checks, no compiler.
The frame timer brackets the effects only with GpuBudgetMs > 0 and is read back without ever blocking (GetData flags 0, no
D3DGETDATA_FLUSH, no wait loop); the controller runs once per frame on the replay thread and hands its level to the world; at level 0
the world's path and constants are the old ones (every change gated by the level); the shaders' intervals take a zero addend at full
level; the key is the last one and documented; nothing on the game thread (src/stream) or in the command stream knows about it."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re

w=fp.src('world_renderer.h').read_text()
r=fp.src('renderer.cpp').read_text()
t=fp.src('gpu_frame_timer.h').read_text()
b=fp.src('gpu_budget.h').read_text()
q=fp.src('quality_settings.h').read_text()
h=(fp.SHADERS/'world_effects.hlsl').read_text()
def code(text):return re.sub(r'/\*.*?\*/','',re.sub(r'//[^\n]*','',text),flags=re.S)
checks={}

# controller and timer headers
checks['gpu_budget.h: portable (no D3D, Win32, clock or allocation)']=all(x not in code(b) for x in ('d3d9','windows.h','chrono','QueryPerformance','new ','malloc','std::vector'))
checks['gpu_budget.h: level 0 is the full image (40 cloud / 48 fog intervals, the light limit, no alternation, zero interval constant)']=all(x in b for x in (
    'return level==0?40u:level==1?32u:24u;','return level>=3?40u:48u;','return std::max(limit,shadowReserve);','return steps==fullSteps||!steps||!fullSteps?0.f:'))
tc=code(t)
reads=re.findall(r'->GetData\(([^;]*?)\)',tc)
checks['timer: every GetData with flags 0, no D3DGETDATA_FLUSH, no wait loop, sleep or spin']=(len(reads)==4 and all(x.rstrip().endswith(',0') for x in reads)
    and 'D3DGETDATA_FLUSH' not in tc and 'while' not in tc and 'for(;;)' not in tc and 'Sleep' not in tc and 'SwitchToThread' not in tc)
checks['timer: S_FALSE leaves the slot pending (continue), never retried in the same poll']=tc.count('if(hr==S_FALSE)continue;')==4
checks['timer: a full ring skips the frame (begin returns false), disjoint / zero frequency / nonmonotonic dropped']=(
    'if(s.pending)continue;' in tc and 'if(disjoint){s.pending=false;continue;}' in tc and 'if(!frequency||t1<t0)continue;' in tc)

# renderer.cpp: timer bracket, poll, controller, logs
rc=code(r)
re_=rc[rc.index('void renderEffects() {'):rc.index('applied = true; ++appliedFrames;')]
# Surface effects finish after deferred spells; clearFrame also closes the timer on early exits.
checks['renderer: timer spans surface work through deferred spells and is closed on frame cleanup']=(
    'surfaceTimerOpen=world&&world->gpuBudgetMs()&&gpuTimer->begin();' in re_
    and re_.index('if (!saved.ok) return;')<re_.index('surfaceTimerOpen=')<re_.index('gpuProfile->beginFrame(')
    and 'if(surfaceTimerOpen){if(gpuTimer)gpuTimer->end();surfaceTimerOpen=false;}' in rc
    and rc.count('if(surfaceTimerOpen){gpuTimer->end();surfaceTimerOpen=false;}')==2)
def in_order(text,*tokens):
    positions=[text.find(token) for token in tokens]
    return all(pos>=0 for pos in positions) and all(a<b for a,b in zip(positions,positions[1:]))
constructor=rc[rc.index('Device(IDirect3DDevice9* d,'):rc.index('~Device() {')]
destructor=rc[rc.index('~Device() {'):rc.index('bool gamePresentation(')]
reset=rc[rc.index('HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pp)'):rc.index('void trimMemory(')]
# Intervening profiler cleanup is valid; query owners must still retire before the device.
checks['renderer: the timer is created, reset before a device Reset and destroyed with the profiler']=(
    'gpuTimer=std::make_unique<NorthlightGpuFrameTimer>(ext);' in constructor
    and in_order(reset,'gpuProfile->reset();','gpuSceneProfile->reset();','materialProfile->reset();','gpuFrameStarted=gpuSceneSplit=gpuEffectsSplit=false;','gpuTimer->reset();','HRESULT hr=hdr.settings.requested?hdr.reset(real,ext,pp):ext->Reset(pp);')
    and in_order(destructor,'gpuProfile.reset();','gpuSceneProfile.reset();','materialProfile.reset();','gpuTimer.reset();','ext->Release();'))
fin=rc[rc.index('void finishFrameImpl() {'):]
gb=rc[rc.index('void gpuBudgetFrame(){'):rc.index('void finishFrameImpl() {')]
checks['renderer: once per frame in finishFrameImpl, after the profile poll, ungated by Diagnostics']=(
    fin.index('if(diagnostics()){gpuProfile->poll();if(gpuSceneProfile)gpuSceneProfile->poll();}')<fin.index('gpuBudgetFrame();')<fin.index('bool modifiers=(GetAsyncKeyState(VK_CONTROL)') and rc.count('gpuBudgetFrame();')==1)
checks['renderer: GpuBudgetMs=0 resets the controller and sets level 0 before anything is polled']=(
    'if(!budget){gpuBudget.reset();gpuBudgetQpc=0;if(world)world->setGpuBudgetLevel(0);return;}' in gb and gb.index('return;}')<gb.index('poll(ms)'))
checks['renderer: only a finished reading updates the controller (dt from the previous reading), the level goes to the world']=(
    'if(gpuTimer&&gpuTimer->poll(ms)){' in gb and 'world->setGpuBudgetLevel(gpuBudget.update(float(budget),float(ms),dt,gpuLimited));' in gb)
checks['renderer: GPUBUDGET line on RenderProfile sample frames, level changes logged (first 32, then with Diagnostics)']=(
    'if(sampled()&&NorthlightRenderThreadProbe::profiling())logf("GPUBUDGET ms=%.3f smoothed=%.3f budget=%.1f level=%u gpuWait=%.2f gpuBound=%d"' in gb
    and 'if(gpuBudget.level!=before&&(gpuBudget.changes<=32||diagnostics()))' in gb and 'logf("GPUBUDGET level %u -> %u' in gb)

# world: level gated, full level unchanged
wc=code(w)
checks['world: the level is 0 whenever GpuBudgetMs is 0, clamped to MaxLevel']='void setGpuBudgetLevel(unsigned level){gpuBudgetLevel=quality.gpuBudgetMs?std::min(level,NorthlightGpuBudget::MaxLevel):0u;}' in wc
checks['world: the interval constants are written only at a reduced level (bank unchanged at level 0), c60.x only with the clouds active']=(
    'if(gpuBudgetLevel){if(cf.active)c[60][0]=NorthlightGpuBudget::spacingDelta(c[21][3],NorthlightGpuBudget::cloudSteps(gpuBudgetLevel),40);c[64][2]=NorthlightGpuBudget::spacingDelta(c[21][3],NorthlightGpuBudget::fogSteps(gpuBudgetLevel),48);}' in wc
    and len(re.findall(r'c\[64\]\[[0-2]\]=',wc))==1 and wc.index('c[21][3]=128;')<wc.index('if(gpuBudgetLevel){')<wc.index('d->SetPixelShaderConstantF(0,&c[0][0],68);'))
checks['world: the lamp limit passes through lightLimit (the setting at level 0)']='NorthlightGpuBudget::lightLimit(quality.localLightLimit,gpuBudgetLevel,effects.shadows&&quality.pointShadows?quality.pointShadowLimit:0)' in wc and wc.count('quality.localLightLimit,float(selectDt)')==0
cp=wc[wc.index('if(cf.active&&c[21][0]>=.5f){'):wc.index('d->SetPixelShaderConstantF(17,c[17],2);')]
checks['world: the clouds are one draw into the fog at every level (0.3.200: no amortised reuse, it lagged a frame in motion)']=(
    cp.count('const bool cloudsDrawn=check(quad(w/2,h/2),"fog clouds raymarch");')==1 and cp.count('SetPixelShader(fogCloudsPS)')==1
    and 'cloudBuffer' not in wc and 'cloudAlternate' not in wc and 'amortise' not in cp and 'if(!cloudsDrawn)return false;' in cp)

# shaders: zero addend at level 0
hc=code(h)
checks['hlsl: the intervals are mad(range,1/N,addend): c60.x clouds (40), c64.z WorldFog (48)']=(
    'float spacing=mad(FogInfo.w,1.0/40,CloudInfo[0].x);' in hc and 'float spacing=mad(FogInfo.w,1.0/48,FogStepInfo.z);' in hc and 'float4 FogStepInfo : register(c64);' in hc
    and 'FogInfo.w/40' not in hc and 'FogInfo.w/48' not in hc)
checks['hlsl: FogTemporal still passes s9 through at weight 0 (the clouds\' copy)']='if(FogTemporalInfo.y<=0)return current;' in h

# settings and docs
checks['settings: GpuBudgetMs (0..20, presets 4/3/2, default 4)']=(
    re.search(r'\{\s*"GpuBudgetMs"\s*,\s*&Settings::gpuBudgetMs\s*,\s*0\s*,\s*20\s*,\s*\{\s*4\s*,\s*3\s*,\s*2\s*\}\s*\}',q) is not None and 'unsigned gpuBudgetMs=4;' in q)

# task 45: GPU-bound gate and the CPU budget (ActorShadowCpuMs)
wl=fp.src('world_shadow_experiment.inl').read_text();wlc=code(wl)
checks['task 45 gate: the signal is the previous Present\'s latency wait, kept apart from the logging counters; known only with the latency limit']=(
    'latencyLastMs=0;' in rc and 'latencyLastMs=double(qpcNow()-t0)*perMs;++latencyWaits;latencyWaitMs+=latencyLastMs;' in rc
    and 'boundKnown=limitGpuLatency&&!latencyFailed;gpuLimited=!boundKnown||gpuBound.bound();' in gb
    and 'cpuLimited=(!replayKnown||replayWait.bound())&&(!boundKnown||!gpuBound.bound());' in gb
    and 'NorthlightStream::get(streamStats->presentNs)+NorthlightStream::get(streamStats->backpressureNs)' in gb and 'replayKnown=streamStats!=nullptr;' in gb
    and 'NorthlightStream::get(streamStats->skippedFrames)' in gb and 'replayWait.update(skippedNow?NorthlightGpuBudget::MaxWaitMs:' in gb
    and 'target->streamStats=&stream->streamCore().q.stats;' in r
    and 'gpuBound.update(float(latencyLastMs),' in gb and gb.index('gpuBound.update(')<gb.index('const unsigned budget=world?world->gpuBudgetMs():0;'))
checks['task 45 gate: BoundWaitMs and the lighten parameter default to the old behaviour']=(
    'constexpr float BoundWaitMs=.5f;' in b and 'unsigned update(float budgetMs,float ms,float dt,bool lighten=true){' in b
    and 'over=lighten&&smoothed>budgetMs?over+dt:0.f;' in code(b) and 'under=smoothed<budgetMs*UpFraction||!lighten?under+dt:0.f;' in code(b))
cb=rc[rc.index('void cpuBudgetFrame(){'):rc.index('void finishFrameImpl() {')]
checks['task 45 cpu: the effects span is always measured (two QPC reads, after the applied check), consumed once per frame, off at ActorShadowCpuMs=0']=(
    'struct EffectsSpan { std::int64_t& out; std::int64_t start; ~EffectsSpan(){out=qpcNow()-start;} } effectsSpan{cpuEffectsQpc,qpcNow()};' in re_
    and re_.index('if (applied || !enabled || failed || !projectionValid) return;')<re_.index('effectsSpan{')
    and 'if(!budget){cpuBudget.reset();cpuBudgetQpc=0;cpuEffectsQpc=0;if(world)world->setCpuBudgetLevel(0);return;}' in cb
    and 'world->setCpuBudgetLevel(cpuBudget.update(float(budget),float(ms),dt,cpuLimited));' in cb and 'cpuEffectsQpc=0;' in cb
    and 'gpuBudgetFrame();cpuBudgetFrame();' in fin and rc.count('cpuBudgetFrame();')==1)
checks['task 45 cpu: level changes and the sample line are logged']=(
    'if(cpuBudget.level!=before&&(cpuBudget.changes<=32||diagnostics()))' in cb and 'logf("CPUBUDGET level %u -> %u smoothed=%.3f budget=%u radius=%u changes=%u"' in cb
    and 'logf("CPUBUDGET ms=%.3f smoothed=%.3f budget=%.1f level=%u radius=%u gpuBound=%d replayWait=%.2f replayBound=%d"' in cb)
checks['task 45 radius: one accessor (the setting at level 0); admission and selection read it, not quality.actorShadowRadius']=(
    'unsigned effectiveActorShadowRadius()const{return NorthlightGpuBudget::actorShadowRadius(quality.actorShadowRadius,cpuBudgetLevel);}' in wc
    and 'void setCpuBudgetLevel(unsigned level){cpuBudgetLevel=quality.actorShadowCpuMs?std::min(level,NorthlightGpuBudget::MaxLevel):0u;}' in wc
    and 'centre.yards=float(effectiveActorShadowRadius());' in wc and len(re.findall(r'quality\.actorShadowRadius',wlc))==1 and 'quality.actorShadowRadius,stable.radiusDropped' in wlc # the selection log prints the setting
    and len(re.findall(r'quality\.actorShadowRadius',wc))==2) # the accessor and the SHADOW experiment log of the setting
checks['task 45 radius: leaving the radius path clears the radius identities']='if(!radius&&actorShadowHistory.radiusSize())actorShadowHistory.clearRadius();' in wlc and 'void clearRadius(){radius_.clear();}' in fp.src('actor_shadow_selection.h').read_text()
checks['task 45 settings: ActorShadowCpuMs (0..20, presets 3/3/2, default 3), documented in the ini and the package README']=(
    re.search(r'\{\s*"ActorShadowCpuMs"\s*,\s*&Settings::actorShadowCpuMs\s*,\s*0\s*,\s*20\s*,\s*\{\s*3\s*,\s*3\s*,\s*2\s*\}\s*\}',q) is not None and 'unsigned actorShadowCpuMs=3;' in q
    and ';ActorShadowCpuMs=3' in fp.src('windows-package/northlight-quality.ini').read_text() and 'ActorShadowCpuMs' in fp.src('windows-package/README.txt').read_text(encoding='utf-8'))

# nothing on the game thread
stream=[p for p in (fp.REPO/'src'/'stream').glob('*') if p.is_file()]
checks['game thread untouched: nothing in src/stream mentions the budget']=all(not re.search(r'gpuBudget|GpuBudget|gpu_budget|gpu_frame_timer',p.read_text(errors='ignore')) for p in stream)
for k,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+k)
sys.exit(0 if all(checks.values()) else 1)
