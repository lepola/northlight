    // 0.3.149 render-thread instrumentation of the directional replay loop
    // (render_thread_probe.h). RenderProfile=0: none of this runs; the cascade
    // loop instantiates Off (the 0.3.148 loop) and nothing is recorded or logged.
    struct QpcClock {static int64_t now(){LARGE_INTEGER t;QueryPerformanceCounter(&t);return t.QuadPart;}};
    using ReplaySplit=NorthlightRenderThreadProbe::Split<QpcClock,Replay>;
    // Per frame, each directional slot (sun near, sun far, moon near, moon far):
    // R rendered, U reused, D deferred (capture skipped), - not drawn (source off, shadows off).
    char cascadeActions[4]={'-','-','-','-'},lastCascadeActions[4]={'-','-','-','-'};
    bool lastNearDrawn()const{return lastCascadeActions[0]=='R'||lastCascadeActions[2]=='R';}
    bool lastFarDrawn()const{return lastCascadeActions[1]=='R'||lastCascadeActions[3]=='R';}
    // RenderProfile sample frame (the renderer's sample flag reaches captureSampled).
    bool profileSampled()const{return captureSampled&&NorthlightRenderThreadProbe::profiling();}
    // Results are kept here and logged by logRenderProfile() from endFrame(): no log
    // line inside the cascade or renderEffects spans.
    struct SplitResult {bool valid=false;uint64_t draws=0;unsigned stateCalls=0,clockReads=0;int64_t ticks[3]={};unsigned marks[3]={},constants[3]={};};
    SplitResult splitResults[4];int64_t uploadWindowTicks=-1; /* 0.3.152: -1 not measured this frame */
    std::vector<const Replay*> replayProfileUsed,replayGiPacked; /* drawn by any pass / packed for GI, this profile frame */
    void keepReplaySplit(int slot,const ReplaySplit& s,uint64_t draws,unsigned stateCalls){
        auto& r=splitResults[slot];r.valid=true;r.draws=draws;r.stateCalls=stateCalls;r.clockReads=s.clockReads();
        for(unsigned b=0;b<3;++b){r.ticks[b]=s.ticks[b];r.marks[b]=s.marks[b];r.constants[b]=s.constants[b];}
    }
    // The point cube drew these replays this frame (a committed fresh update).
    void profilePointUsed(){try{for(const auto& face:pointReplayCandidates)for(size_t index:face)if(index<replays.size())replayProfileUsed.push_back(replays[index].get());}catch(...){}}
    // endFrame(), profile sample frames: split per rendered slot, cascade actions, capture waste.
    // 0.3.160: game self-read cost since the previous profile sample (all frames in between).
    unsigned long long selfReadCalls0=0,selfReadBytes0=0,selfReadFailures0=0;long long selfReadTicks0=0;unsigned selfReadFrames0=0;
    void logSelfReads(double tick){
        using S=NorthlightWorldContext::SelfReadStats;
        const unsigned long long calls=S::calls.load(std::memory_order_relaxed),bytes=S::bytes.load(std::memory_order_relaxed),failures=S::failures.load(std::memory_order_relaxed);
        const long long ticks=S::ticks.load(std::memory_order_relaxed);
        const unsigned n=frames-selfReadFrames0;const double ms=double(ticks-selfReadTicks0)*tick;
        if(profileSampled()&&selfReadFrames0&&n)logf("MEMREAD frames=%u calls=%llu bytes=%llu failures=%llu ms=%.3f callsPerFrame=%.1f msPerFrame=%.4f usPerCall=%.3f",
            n,calls-selfReadCalls0,bytes-selfReadBytes0,failures-selfReadFailures0,ms,double(calls-selfReadCalls0)/n,ms/n,
            calls>selfReadCalls0?ms*1000.0/double(calls-selfReadCalls0):0.0);
        selfReadCalls0=calls;selfReadBytes0=bytes;selfReadFailures0=failures;selfReadTicks0=ticks;selfReadFrames0=frames;
    }
    void logRenderProfile(){
        namespace P=NorthlightRenderThreadProbe;
        const double tick=captureFrequency.QuadPart>0?1000.0/double(captureFrequency.QuadPart):0.0; /* ms per tick */
        logSelfReads(tick);
        const double clock=P::clockCostMin<QpcClock>(32,8)*tick; /* 0.3.150: 8 runs, the cheapest */
        for(int slot=0;slot<4;++slot){auto& r=splitResults[slot];if(!r.valid)continue;r.valid=false;
            const double own=double(r.ticks[P::Own])*tick,state=double(r.ticks[P::State])*tick,draw=double(r.ticks[P::Draw])*tick;
            const double calls=double(r.stateCalls)+double(r.constants[0]+r.constants[1]+r.constants[2]);
            auto per=[&](double ms,unsigned reads,double n){return n>0?std::max(0.0,ms-double(reads)*clock)*1000.0/n:0.0;};
            if(profileSampled())logf("WORLD replay split source=%d cascade=%d draws=%llu stateCalls=%.0f constantF=%u constantB=%u constantI=%u ownMs=%.3f stateMs=%.3f drawMs=%.3f ownUsPerDraw=%.3f stateUsPerCall=%.3f drawUsPerDraw=%.3f stateRawUsPerCall=%.3f clockReads=%u clockNs=%.1f timerMs=%.3f (secondary: per-draw clock reads)",
                slot/2,slot%2,(unsigned long long)r.draws,calls,r.constants[0],r.constants[1],r.constants[2],own,state,draw,
                per(own,r.marks[P::Own],double(r.draws)),per(state,r.marks[P::State],calls),per(draw,r.marks[P::Draw],double(r.draws)),calls>0?state*1000.0/calls:0.0,r.clockReads,clock*1e6,double(r.clockReads)*clock);
        }
        // Captured replays (selected and unselected) that no directional pass or cube face
        // drew and that were not packed for GI: what a lighter capture path could skip.
        size_t captured=0,unused=0,unusedBytes=0;
        if(captureMode!=CaptureSkipped){
            std::sort(replayProfileUsed.begin(),replayProfileUsed.end());std::sort(replayGiPacked.begin(),replayGiPacked.end());
            auto count=[&](const std::vector<std::unique_ptr<Replay>>& list){for(const auto& p:list){++captured;
                if(std::binary_search(replayProfileUsed.begin(),replayProfileUsed.end(),p.get())||std::binary_search(replayGiPacked.begin(),replayGiPacked.end(),p.get()))continue;
                ++unused;unusedBytes+=p->mesh().byteSize();}};
            count(replays);count(heldShadowReplays);
        }
        const double perCandidateUs=replayCaptureCalls?double(replayCaptureTicks)*tick*1000.0/replayCaptureCalls:0.0;
        // 0.3.150: model capture net of its timers (per candidate one CpuScope, plus the capture-phase subset's reads),
        // per candidate and per accepted draw (captured: replays + held; a rejected candidate costs about nothing).
        const unsigned captureReads=replayCaptureCalls+capturePhases.clockReads+actorPhases.clockReads;
        const double captureNetMs=std::max(0.0,double(replayCaptureTicks)*tick-double(captureReads)*clock);
        if(profileSampled())logf("WORLD profile frame worldFrame=%u capture=%s cascades=%c%c%c%c (sun near,far moon near,far: R render U reuse D defer - off) probe=%s probeRan=%u captured=%zu unused=%zu unusedBytes=%zu heldUnselected=%zu giPacked=%zu captureUsPerCandidate=%.3f unusedCaptureMsEstimate=%.3f captureClockReads=%u captureNetMs=%.3f captureNetUsPerCandidate=%.3f captureNetUsPerAccepted=%.3f uploadWindowMs=%.3f staging=%d probeUpload=%d",
            frames,captureMode==CaptureSkipped?"skipped":captureMode==CaptureFresh?"fresh":"none",cascadeActions[0],cascadeActions[1],cascadeActions[2],cascadeActions[3],
            NorthlightRenderThreadProbe::probeModeName(replayProbeMode),unsigned(replayProbeRanThisFrame),captured,unused,unusedBytes,heldShadowReplays.size(),replayGiPacked.size(),perCandidateUs,double(unused)*perCandidateUs/1000.0,
            captureReads,captureNetMs,replayCaptureCalls?captureNetMs*1000.0/replayCaptureCalls:0.0,captured?captureNetMs*1000.0/double(captured):0.0,uploadWindowTicks<0?-1.0:double(uploadWindowTicks)*tick,int(frameStaging),int(frameProbeUpload));
        replayProfileUsed.clear();replayGiPacked.clear();uploadWindowTicks=-1;
    }

    // DiagReplayProbe (RenderProfile=1 and DiagReplayProbe=1; render_thread_probe.h
    // ProbeSchedule). In 10 s windows cycling off/full/off/state/off/logic, every
    // frame that renders the sun near map with fresh replays issues its drawn
    // packets once more, in order, through the same submitReplay(), into a private
    // 1024x1024 R32F target with its own D24S8 depth (the cascade's formats and size).
    // full: the complete call stream; state: without the draw calls; logic: pose
    // preparation only, no D3D9 call. Nothing samples or binds that target elsewhere,
    // and the pass runs inside its own SavedState (state block, targets, depth,
    // viewport), so every later pass sees the pre-probe device state: image unchanged.
    // Called after replayBoundsJoin() and the sun near replay loop, before the union.
    // Never fails the frame: probe errors count toward disabling the probe only.
    static constexpr unsigned ReplayProbeSize=1024,ReplayProbeFailureLimit=8;
    NorthlightRenderThreadProbe::ProbeSchedule replayProbeSchedule;
    std::vector<const Replay*> replayProbeList;
    IDirect3DTexture9* replayProbeTexture=nullptr;IDirect3DSurface9 *replayProbeSurface=nullptr,*replayProbeDepth=nullptr;
    bool replayProbeDisabled=false,replayProbeRanThisFrame=false;unsigned replayProbeFailures=0;
    NorthlightRenderThreadProbe::ProbeMode replayProbeMode=NorthlightRenderThreadProbe::ProbeOff;
    uint64_t replayProbeWindow=0;bool replayProbeWindowOpen=false;
    struct ProbeWindowStats {
        NorthlightRenderThreadProbe::Samples<2048> realLoop,issue,save,restore,draws;unsigned runs=0,incomplete=0,frames=0;
        void reset(){realLoop.reset();issue.reset();save.reset();restore.reset();draws.reset();runs=incomplete=frames=0;}
    } replayProbeStats,replayProbeDone;
    bool replayProbeReport=false;uint64_t replayProbeReportWindow=0;NorthlightRenderThreadProbe::ProbeMode replayProbeReportMode=NorthlightRenderThreadProbe::ProbeOff;
    bool replayProbeActive()const{return NorthlightRenderThreadProbe::probing()&&!replayProbeDisabled;}
    // Renderer side: the current window (its FRAME cost lines follow the same windows).
    uint64_t probeWindow()const{return replayProbeWindow;}
    NorthlightRenderThreadProbe::ProbeMode probeMode()const{return replayProbeMode;}
    void dropReplayProbeTargets(){drop(replayProbeSurface);drop(replayProbeTexture);drop(replayProbeDepth);}
    void releaseReplayProbe(){dropReplayProbeTargets();replayProbeList.clear();}
    // Start of render(): the window of this frame. A finished window is logged by endFrame().
    void replayProbeFrame(){
        replayProbeRanThisFrame=false;
        if(!replayProbeActive()){replayProbeMode=NorthlightRenderThreadProbe::ProbeOff;return;}
        const uint64_t window=replayProbeSchedule.window(GetTickCount());
        if(replayProbeWindowOpen&&window!=replayProbeWindow){
            replayProbeDone=replayProbeStats;replayProbeStats.reset();replayProbeReport=true;replayProbeReportWindow=replayProbeWindow;replayProbeReportMode=replayProbeMode;}
        else if(!replayProbeWindowOpen)replayProbeStats.reset();
        replayProbeWindow=window;replayProbeWindowOpen=true;replayProbeMode=NorthlightRenderThreadProbe::ProbeSchedule::mode(window);
        ++replayProbeStats.frames;
    }
    void replayProbeFailed(const char* stage,HRESULT hr){
        ++replayProbeStats.incomplete;
        if(++replayProbeFailures<ReplayProbeFailureLimit&&hr!=E_OUTOFMEMORY)return;
        replayProbeDisabled=true;
        if(NorthlightRenderThreadProbe::profiling())logf("WORLD replay probe disabled: %s HRESULT=%08lx after %u consecutive incomplete runs (rendering unaffected)",stage,(unsigned long)hr,replayProbeFailures);
    }
    template<class Mode> bool replayProbeIssue(const float* rows,const float* worldMatrix,size_t& draws){
        size_t constantBytes=0,constantCalls=0;const char* failedStage=nullptr;HRESULT failedHR=S_OK;
        NorthlightReplayPoses::Pass<BOOL> probePoses;
        NorthlightReplayDrawState::Cache probeBindings(d);
        Mode mode;
        for(const Replay* p:replayProbeList){
            if(!submitReplay(p,probeBindings,probePoses,rows,worldMatrix,mode,constantBytes,constantCalls,[&](HRESULT h,const char* s){if(SUCCEEDED(h))return true;failedStage=s;failedHR=h;return false;}))break;
            ++draws;
        }
        if(draws==replayProbeList.size())return true;
        replayProbeFailed(failedStage?failedStage:"pose constants",failedStage?failedHR:E_FAIL);return false;
    }
    // realLoopMs: this frame's sun near replay loop (coarse, two clock reads, every probe frame).
    void replayProbe(const float* rows,const float* worldMatrix,double realLoopMs){
        replayProbeStats.realLoop.add(realLoopMs);
        const auto mode=replayProbeMode;if(mode==NorthlightRenderThreadProbe::ProbeOff||captureFrequency.QuadPart<=0)return;
        if(!replayProbeSurface||!replayProbeDepth){
            dropReplayProbeTargets();
            if(FAILED(d->CreateTexture(ReplayProbeSize,ReplayProbeSize,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&replayProbeTexture,nullptr))||
               FAILED(replayProbeTexture->GetSurfaceLevel(0,&replayProbeSurface))||
               FAILED(d->CreateDepthStencilSurface(ReplayProbeSize,ReplayProbeSize,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&replayProbeDepth,nullptr))){
                dropReplayProbeTargets();replayProbeFailed("probe targets",E_OUTOFMEMORY);return;}
        }
        const double ms=1000.0/double(captureFrequency.QuadPart);
        const int64_t t0=QpcClock::now();int64_t t1=0,t2=0;size_t draws=0;bool complete=false;
        {
            SavedState save(d,&stateBlocks);if(!save.ok){replayProbeFailed("probe state save",E_FAIL);return;}
            const D3DVIEWPORT9 vp={0,0,ReplayProbeSize,ReplayProbeSize,0,1};HRESULT hr=d->SetDepthStencilSurface(nullptr);
            if(SUCCEEDED(hr))hr=d->SetRenderTarget(0,replayProbeSurface);if(SUCCEEDED(hr))hr=d->SetDepthStencilSurface(replayProbeDepth);
            if(SUCCEEDED(hr))hr=d->SetViewport(&vp);if(SUCCEEDED(hr))hr=d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0);
            if(FAILED(hr)){replayProbeFailed("probe target binding",hr);return;}
            t1=QpcClock::now();
            complete=mode==NorthlightRenderThreadProbe::ProbeFull?replayProbeIssue<NorthlightRenderThreadProbe::Off>(rows,worldMatrix,draws):
                     mode==NorthlightRenderThreadProbe::ProbeState?replayProbeIssue<NorthlightRenderThreadProbe::StateOnly>(rows,worldMatrix,draws):
                     replayProbeIssue<NorthlightRenderThreadProbe::LogicOnly>(rows,worldMatrix,draws);
            t2=QpcClock::now();
        } /* SavedState restores targets, depth, viewport and the full state block here */
        const int64_t t3=QpcClock::now();
        replayProbeRanThisFrame=true;if(!complete)return;
        replayProbeFailures=0;++replayProbeStats.runs;
        replayProbeStats.save.add(double(t1-t0)*ms);replayProbeStats.issue.add(double(t2-t1)*ms);replayProbeStats.restore.add(double(t3-t2)*ms);replayProbeStats.draws.add(double(draws));
    }
    // endFrame(): a finished window, logged after every measured span of the frame.
    void logReplayProbeWindow(){
        if(!replayProbeReport)return;replayProbeReport=false;
        auto& w=replayProbeDone;
        const auto real=w.realLoop.take(),issue=w.issue.take(),save=w.save.take(),restore=w.restore.take(),drawn=w.draws.take();
        if(NorthlightRenderThreadProbe::profiling())logf("WORLD replay probe window=%llu mode=%s seconds=%u frames=%u runs=%u incomplete=%u target=%ux%u draws=%.0f,%.0f,%.0f realLoopMs=%.3f,%.3f,%.3f,%.3f,%.3f issueMs=%.3f,%.3f,%.3f,%.3f,%.3f saveMs=%.3f,%.3f restoreMs=%.3f,%.3f usPerDraw=%.3f (mean,p50,p95,p99,max; draws mean,p50,max; save/restore mean,p95)",
            (unsigned long long)replayProbeReportWindow,NorthlightRenderThreadProbe::probeModeName(replayProbeReportMode),unsigned(NorthlightRenderThreadProbe::ProbeSchedule::WindowMs/1000),w.frames,w.runs,w.incomplete,ReplayProbeSize,ReplayProbeSize,
            drawn.mean,drawn.p50,drawn.max,real.mean,real.p50,real.p95,real.p99,real.max,issue.mean,issue.p50,issue.p95,issue.p99,issue.max,save.mean,save.p95,restore.mean,restore.p95,
            drawn.mean>0?issue.mean*1000.0/drawn.mean:0.0);
        w.reset();
    }
