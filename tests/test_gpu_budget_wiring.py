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
    'return level==0?40u:level==1?32u:24u;','return level>=2;','return level>=3?40u:48u;','return level>=3&&limit>16?16u:limit;','return steps==fullSteps||!steps||!fullSteps?0.f:'))
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
checks['renderer: timer opened only with GpuBudgetMs > 0, right after the SavedState, closed by a scope guard (every return path)']=(
    'struct TimerEnd { NorthlightGpuFrameTimer* t; bool open; ~TimerEnd(){if(open)t->end();} } timerEnd{gpuTimer.get(),world&&world->gpuBudgetMs()&&gpuTimer->begin()};' in re_
    and re_.index('if (!saved.ok) return;')<re_.index('timerEnd{')<re_.index('gpuProfile->beginFrame(')<re_.index('profileEnd{'))
checks['renderer: the timer is created, reset before a device Reset and destroyed with the profiler']=(
    'gpuTimer=std::make_unique<NorthlightGpuFrameTimer>(ext);' in rc and 'gpuProfile->reset();gpuTimer->reset();' in rc and 'gpuProfile.reset();gpuTimer.reset();' in rc)
fin=rc[rc.index('void finishFrameImpl() {'):]
gb=rc[rc.index('void gpuBudgetFrame(){'):rc.index('void finishFrameImpl() {')]
checks['renderer: once per frame in finishFrameImpl, after the profile poll, ungated by Diagnostics']=(
    fin.index('if(diagnostics())gpuProfile->poll();')<fin.index('gpuBudgetFrame();')<fin.index('GetCreationParameters') and rc.count('gpuBudgetFrame();')==1)
checks['renderer: GpuBudgetMs=0 resets the controller and sets level 0 before anything is polled']=(
    'if(!budget){gpuBudget.reset();gpuBudgetQpc=0;if(world)world->setGpuBudgetLevel(0);return;}' in gb and gb.index('return;}')<gb.index('poll(ms)'))
checks['renderer: only a finished reading updates the controller (dt from the previous reading), the level goes to the world']=(
    'if(gpuTimer&&gpuTimer->poll(ms)){' in gb and 'world->setGpuBudgetLevel(gpuBudget.update(float(budget),float(ms),dt));' in gb)
checks['renderer: GPUBUDGET line on RenderProfile sample frames, level changes logged (first 32, then with Diagnostics)']=(
    'if(sampled()&&NorthlightRenderThreadProbe::profiling())logf("GPUBUDGET ms=%.3f smoothed=%.3f budget=%.1f level=%u"' in gb
    and 'if(gpuBudget.level!=before&&(gpuBudget.changes<=32||diagnostics()))' in gb and 'logf("GPUBUDGET level %u -> %u' in gb)

# world: level gated, full level unchanged
wc=code(w)
checks['world: the level is 0 whenever GpuBudgetMs is 0, clamped to MaxLevel']='void setGpuBudgetLevel(unsigned level){gpuBudgetLevel=quality.gpuBudgetMs?std::min(level,NorthlightGpuBudget::MaxLevel):0u;}' in wc
checks['world: the interval constants are written only at a reduced level (bank unchanged at level 0), c60.x only with the clouds active']=(
    'if(gpuBudgetLevel){if(cf.active)c[60][0]=NorthlightGpuBudget::spacingDelta(c[21][3],NorthlightGpuBudget::cloudSteps(gpuBudgetLevel),40);c[64][2]=NorthlightGpuBudget::spacingDelta(c[21][3],NorthlightGpuBudget::fogSteps(gpuBudgetLevel),48);}' in wc
    and len(re.findall(r'c\[64\]\[\d\]=',wc))==1 and wc.index('c[21][3]=128;')<wc.index('if(gpuBudgetLevel){')<wc.index('d->SetPixelShaderConstantF(0,&c[0][0],68);'))
checks['world: the lamp limit passes through lightLimit (the setting at level 0)']='NorthlightGpuBudget::lightLimit(quality.localLightLimit,gpuBudgetLevel)' in wc and wc.count('quality.localLightLimit,float(selectDt)')==0
cp=wc[wc.index('if(cf.active&&c[21][0]>=.5f){'):wc.index('d->SetPixelShaderConstantF(17,c[17],2);')]
checks['world: amortisation gated by cloudAlternate(level), FogTemporal and the buffer; else the old single draw into the fog']=(
    'const bool amortise=NorthlightGpuBudget::cloudAlternate(gpuBudgetLevel)&&fogTemporalPS&&ensureCloudBuffer(w,h);' in cp
    and 'else cloudBufferValid=false;' in cp and 'bool cloudsDrawn=!march||((!amortise||cloudTarget)&&check(quad(w/2,h/2),"fog clouds raymarch"));' in cp
    and cp.count('"fog clouds raymarch"')==1 and cp.count('SetPixelShader(fogCloudsPS)')==1)
am=cp[cp.index('if(amortise){'):]
checks['world: amortised: march into the buffer without blending every other frame (or when the buffer is stale / history broken), then the kept result through FogTemporal at weight 0 with s9 POINT, everything put back']=all(x in am for x in (
    'march=!cloudBufferValid||!useHistory||cloudMarchPhase;','D3DRS_ALPHABLENDENABLE,FALSE','SetRenderTarget(0,cloudBufferSurface)','SetRenderTarget(0,fogSurface)','D3DRS_ALPHABLENDENABLE,TRUE',
    'const float copy[4]={0,0,0,0};d->SetPixelShaderConstantF(64,copy,1);','SetTexture(14,nullptr);d->SetTexture(9,cloudBuffer);','SetPixelShader(fogTemporalPS)','"fog clouds reuse"',
    'd->SetTexture(9,nullptr);d->SetSamplerState(9,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(9,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);','d->SetPixelShaderConstantF(64,c[64],1);'))
checks['world: the buffer is invalid on frames without the clouds and after a failure; released with the device resources']=(
    '}else cloudBufferValid=false;' in wc and 'if(!cloudsDrawn){cloudBufferValid=false;return false;}' in wc and 'drop(cloudBufferSurface);drop(cloudBuffer);cloudBufferValid=false;cloudBufferFailed=false;' in wc)
checks['world: the buffer is created lazily (never at level 0), honours the memory guard, a failure keeps the direct path']=(
    'if(cloudBufferFailed||memoryPressure)return false;' in wc and 'cloudBufferFailed=true;' in wc and wc.count('ensureCloudBuffer(')==2)

# shaders: zero addend at level 0
hc=code(h)
checks['hlsl: the intervals are mad(range,1/N,addend): c60.x clouds (40), c64.z WorldFog (48)']=(
    'float spacing=mad(FogInfo.w,1.0/40,CloudInfo[0].x);' in hc and 'float spacing=mad(FogInfo.w,1.0/48,FogStepInfo.z);' in hc and 'float4 FogStepInfo : register(c64);' in hc
    and 'FogInfo.w/40' not in hc and 'FogInfo.w/48' not in hc)
checks['hlsl: FogTemporal still passes s9 through at weight 0 (the clouds\' copy)']='if(FogTemporalInfo.y<=0)return current;' in h

# settings and docs
checks['settings: GpuBudgetMs the last key (0..20, presets 4/3/2, default 4)']=(
    '{"GpuBudgetMs",&Settings::gpuBudgetMs,0,20,{4,3,2}},\n    {"StreamFramesAhead",' in q and 'unsigned gpuBudgetMs=4;' in q and 'char origin[42]=' in q)
ini=(fp.REPO/'renderer'/'windows-package'/'northlight-quality.ini').read_text()
readme=(fp.REPO/'README.md').read_text();txt=(fp.REPO/'renderer'/'windows-package'/'README.txt').read_text()
checks['docs: ini template, README.md and README.txt']=(';GpuBudgetMs=4' in ini and 'GpuBudgetMs' in readme and re.search(r'^  GpuBudgetMs +4 / 3 / 2 ',txt,re.M) is not None)

# nothing on the game thread
stream=[p for p in (fp.REPO/'src'/'stream').glob('*') if p.is_file()]
checks['game thread untouched: nothing in src/stream mentions the budget']=all(not re.search(r'gpuBudget|GpuBudget|gpu_budget|gpu_frame_timer',p.read_text(errors='ignore')) for p in stream)
for k,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+k)
sys.exit(0 if all(checks.values()) else 1)
