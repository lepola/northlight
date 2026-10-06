    // Selection runs on capture frames only; with model capture skipped between them
    // (both shadow intervals >1) its frame counts are rescaled to rendered frames.
    NorthlightActorShadowSelection::Tuning selectionTuning()const{
        return NorthlightActorShadowSelection::atCadence(NorthlightActorShadowSelection::active(),
            effects.shadows&&NorthlightQuality::captureSkipPossible(quality,true)?quality.nearShadowInterval:1);}
    void selectShadowReplays(){
        prepareJoin(); /* 0.3.177 (r83): first; every render-thread cache use of the frame follows it */
        if(shadowSelectionDone)return;shadowSelectionDone=true;
        auto finishFate=[this]{finishShadowFate();};struct FateGuard {decltype(finishFate)& finish;~FateGuard(){finish();}} fateGuard{finishFate};
        const auto start=captureSampled?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        const size_t captured=replays.size(),budget=size_t(actorShadowBudgetMiB)*1048576;
        size_t actorBytes=0,actors=0,small=0,distanceTests=0,distanceReused=0;
        for(const auto& p:replays){small+=!p->shadowSelected;
            if(p->shadowSelected&&p->shadowSkinned){actorBytes+=p->mesh().byteSize();++actors;}}
        NorthlightReplayShadowPolicy::Result result;result.kept=actors;result.keptBytes=actorBytes;NorthlightActorShadowSelection::Result stable;bool ranked=false,stableRan=false;
        auto transition=[this](bool over){actorShadowTransitions+=over!=actorShadowOver;actorShadowOver=over;}; /* over<->under budget crossings per log window */
        const bool radius=NorthlightActorShadowSelection::Enabled&&quality.actorShadowRadius;
        if(!radius)transition(budget&&actorBytes>budget);
        try{
            // Ranking is necessary only if the captured actor geometry exceeds
            // this optional replay quota. Capture safety still uses Frame's
            // independent byte/draw limits, including on immutable cache hits.
            // ActorShadowRadius>0 runs the stable selection on every frame (grouping,
            // attachments, radius identities); the quota then ranks, as before, but on
            // the bytes inside the radius only (choose() decides with shouldRank).
            if(radius){stableRan=true;stable=selectStableActors(budget,distanceTests,distanceReused);ranked=stable.ranked;actorShadowRadiusToggles+=stable.radiusToggles;actorShadowRadiusFlicker+=stable.radiusFlicker;actorShadowRadiusRekeyed+=stable.radiusRekeyed;
                transition(budget&&stable.radiusInsideBytes>budget);
                if(ranked){actorShadowToggles+=stable.toggles;++actorShadowFrames;actorShadowCapBinds+=unsigned(stable.exemptCapBinds);}}
            else if((ranked=NorthlightActorShadowSelection::Enabled&&budget&&actorShadowHistory.shouldRank(actorBytes,budget,selectionTuning()))){stableRan=true;stable=selectStableActors(budget,distanceTests,distanceReused);actorShadowToggles+=stable.toggles;++actorShadowFrames;actorShadowCapBinds+=unsigned(stable.exemptCapBinds);}
            else if(budget&&actorBytes>budget){
                shadowCandidates.clear();shadowCandidates.reserve(actors);
                unsigned previousGroup=UINT_MAX;IDirect3DVertexShader9* previousShader=nullptr;
                IDirect3DVertexDeclaration9* previousDecl=nullptr;float previousDistance=0;bool previousKnown=false;
                for(size_t index=0;index<replays.size();++index){const auto& p=*replays[index];
                    if(!p.shadowSelected||!p.shadowSkinned)continue;
                    NorthlightReplayShadowPolicy::Candidate item;item.index=index;item.bytes=p.mesh().byteSize();
                    if(p.constantGroup==previousGroup&&p.originalShader==previousShader&&p.decl==previousDecl){
                        item.known=previousKnown;item.distanceSquared=previousDistance;++distanceReused;
                    }else{
                        ++distanceTests;const auto program=actorPrograms.find(p.originalShader);
                        const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
                        item.known=program!=actorPrograms.end()&&declarationCache.get(p.decl,elements,count)&&
                            prepareCaches->sampled.distance(*program->second,p.mesh(),p.shared,p.decl,elements,count,
                                p.constants,context.inverseView,context.camera,item.distanceSquared);
                    }
                    previousGroup=p.constantGroup;previousShader=p.originalShader;previousDecl=p.decl;
                    previousKnown=item.known;previousDistance=item.distanceSquared;shadowCandidates.push_back(item);
                }
                result=NorthlightReplayShadowPolicy::choose(shadowCandidates,budget);
                for(const auto& item:shadowCandidates)replays[item.index]->shadowSelected=item.keep;
            }
            else if(NorthlightActorShadowSelection::Enabled){if(budget)actorShadowHistory.keptAll();else actorShadowHistory.clear();}
            if(stableRan){pivotSelfCaptured=stable.radiusSelf==1;pivotSelfFrame=frames;} /* 0.3.190: the self was captured in this selection (not merely held) */
            if(stable.actors+stable.rigidActors){result.kept=stable.kept;result.dropped=stable.dropped;result.keptBytes=stable.keptBytes;result.droppedBytes=stable.droppedBytes;result.unknown=stable.unknown;}
            // Do not destroy excluded constant-bank owners. GI has already
            // copied its packets; shadows alone see this reduced, ordered list.
            if(shadowFate.active())for(const auto& p:replays)shadowFate.record(p->fateSlot,NorthlightShadowFate::Fate(p->fateClass),p->shadowSelected,p->fateDistance);
            if(!stableRan)for(auto& p:replays)p->boneKnown=false; /* 0.3.176 (S2): no selection bones this frame */
            rigidMemoryObserve(); /* 0.3.172 rigid memory: every captured group, before the unselected leave */
            NorthlightReplayShadowPolicy::retainSelected(replays,heldShadowReplays);
            rigidMemoryInject(); /* remembered groups the game did not draw: after selection, before bounds and upload */
        }catch(...){
            for(auto& p:replays)p->shadowSelected=true;actorShadowHistory.clear();
            logf("SHADOW experiment selection allocation failed; current captured shadows retained");return;
        }
        if(captureSampled){const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            // 0.3.177 (r83): the prepare worker's fields, RenderProfile only.
            char prepareFields[400]="";
            if(NorthlightRenderThreadProbe::profiling())std::snprintf(prepareFields,sizeof prepareFields," prepareMode=%s published=%u workerRecords=%u joinInlineRecords=%u workerPrepareMs=%.3f joinWaitMs=%.3f joinInlineMs=%.3f wakes=%u notifyUs=%.1f handoffUs=%.1f handoffRawUs=%.1f pairNs=%.1f handoffSamples=%u handoffWarmUs=%.1f prepareMismatch=%u cachesStaleClears=%u prepareResync=%u prepareRearms=%u",
                prepareStats.mode,prepareStats.published,prepareStats.workerRecords,prepareStats.joinInline,prepareStats.workerMs,prepareStats.joinWaitMs,prepareStats.joinInlineMs,prepareStats.wakes,prepareStats.notifyUs,prepareMeter.on()?prepareMeter.correctedUs():-1.0,prepareMeter.on()?prepareMeter.rawUs():-1.0,prepareMeter.pairNs(),prepareMeter.samples(),prepareStats.handoffWarmUs,prepareStats.mismatch,prepareStats.staleClears,prepareStats.resync,prepareRearms);
            if(stable.actors+stable.rigidActors){deferLogf("MODEL shadow actors ranked=%zu kept=%zu toggles=%zu togglesSinceLog=%zu rankedFramesSinceLog=%u matched=%zu retained=%zu rigidActors=%zu rigidDraws=%zu rigidBytes=%zu attached=%zu attachedBytes=%zu orphans=%zu freeNearBody=%zu attachRadius=%.1f rigidHits=%u rigidMisses=%u rigidScannedVertices=%zu history=%zu cut=prefix margin=%.2f origin=%s rigidNew=%zu prepareMs=%.3f chooseMs=%.3f exemptActors=%zu exemptDraws=%zu exemptBytes=%zu cappedExempt=%zu stillRankedSmall=%zu exemptCapBindsSinceLog=%u rooted=%zu rootFallback=%zu rootRejected=%zu locked=%zu exemptNpcLike=%zu%s",
                stable.actors,stable.actorsKept,stable.toggles,actorShadowToggles,actorShadowFrames,stable.matched,stable.retained,stable.rigidActors,stable.rigidDraws,stable.rigidBytes,stable.attached,stable.attachedBytes,stable.orphans,stable.rigidStuck,double(NorthlightActorShadowSelection::AttachRadius),prepareCaches->bones.hits,prepareCaches->bones.misses,prepareCaches->bones.scannedVertices,actorShadowHistory.size(),double(NorthlightActorShadowSelection::active().margin),NorthlightActorShadowSelection::FlickerFixes&&actorShadowOriginValid?"pivot":"camera",stable.rigidNew,actorShadowPrepareMs,actorShadowChooseMs,stable.exemptActors,stable.exemptDraws,stable.exemptBytes,stable.cappedExempt,stable.stillRankedSmall,actorShadowCapBinds,stable.rooted,stable.rootFallback,stable.rootRejected,stable.locked,stable.exemptNpcLike,prepareFields);actorShadowCapBinds=0;
                actorShadowToggles=0;actorShadowFrames=0;}
            deferLogf("MODEL shadow selection captured=%zu selected=%zu smallGIExcluded=%zu actorCandidates=%zu actorKept=%zu actorDropped=%zu actorBytes=%zu keptActorBytes=%zu droppedActorBytes=%zu budgetBytes=%zu unknownDistance=%zu distanceTests=%zu distanceReused=%zu selectionMs=%.3f inputCacheHits=%u inputCacheMisses=%u scope=captured-actors distance=sampled-pose-vertex captureBudgetMiB=%u budgetTransitionsSinceLog=%u ranking=%d radius=%u radiusDropped=%zu radiusDroppedDraws=%zu radiusDroppedBytes=%zu radiusTogglesSinceLog=%zu radiusFlickerSinceLog=%zu radiusRekeyedSinceLog=%zu radiusCharacters=%zu radiusSelf=%zu radiusCompanions=%zu radiusInsideBytes=%zu radiusNoPivot=%zu",
                captured,replays.size(),small,actors,result.kept+stable.rigidDraws,result.dropped,actorBytes,result.keptBytes+stable.rigidBytes,result.droppedBytes,budget,result.unknown,distanceTests,distanceReused,ms,prepareCaches->sampled.hits,prepareCaches->sampled.misses,captureBudgetMiB,actorShadowTransitions,int(ranked&&stable.actors+stable.rigidActors>0),
                quality.actorShadowRadius,stable.radiusDropped,stable.radiusDroppedDraws,stable.radiusDroppedBytes,actorShadowRadiusToggles,actorShadowRadiusFlicker,actorShadowRadiusRekeyed,stable.radiusCharacters,stable.radiusSelf,stable.radiusCompanions,stable.radiusInsideBytes,stable.radiusUnreferenced);
            actorShadowTransitions=0;actorShadowRadiusToggles=actorShadowRadiusFlicker=actorShadowRadiusRekeyed=0;
        }
    }
    // Called once per frame after selection: closes the fate frame and opens the next.
    void finishShadowFate(){
        if(shadowFate.active()){shadowFate.endFrame();if(shadowFate.windowEnded())shadowFate.report([](const char* format,auto... args){logf(format,args...);});}
        // ShadowFateDiagnostics=1 only: 0.2-0.5 ms per sampled window otherwise.
        shadowFate.beginFrame(actorShadowBudgetMiB>0&&shadowFateDiagnostics);
    }
    // ---- 0.3.177 (r83 a1-prepare): the stable selection's prepare on a worker while the game draws.
    // A frame opens at its first selected skinned capture: the camera is frozen and the caches pass to
    // the worker. Each such draw is published right after it joins replays; the join (the first statement
    // of selectShadowReplays) stops the worker and prepares the rest inline from the last output's state,
    // and the caches return. Inline instead (the 0.3.176 path through the same prepareRecord): fewer than
    // 6 cores, no thread, ActorShadows=0, a legacy tuning, after a watchdog, and the RenderProfile A/B
    // inline windows (every other 10 s).
    enum class PrepareFrame : unsigned char {None,Worker,Inline,Joined};
    PrepareFrame prepareFrame=PrepareFrame::None;bool prepareOutputsReady=false,prepareOpened=false,prepareTimed=false;std::uint32_t prepareCount=0;
    const unsigned prepareCores=NorthlightStream::cores(); /* 0.3.192 (CS): one core fewer while the replay thread runs */
    std::atomic<bool> prepareCachesStale{false}; /* registerShader(): the owner clears the caches at its next open */
    // RenderProfile diagnostics of the frame (appended to MODEL shadow actors); -1: not measured.
    struct PrepareStats {const char* mode="inline";std::uint32_t published=0,workerRecords=0,joinInline=0,wakes=0,mismatch=0,staleClears=0,resync=0;
        double workerMs=-1,joinWaitMs=-1,joinInlineMs=-1,notifyUs=-1,handoffWarmUs=-1;} prepareStats;
    unsigned prepareFaults=0;
    // 0.3.179 (M1): the capture-side handoff, RenderProfile sample frames only; the phase from the frame serial.
    NorthlightActorPrepare::HandoffMeter<std::chrono::steady_clock> prepareMeter;bool prepareMeterBegun=false;std::uint32_t prepareFrameSerial=0;
    bool prepareHandoffSample(){if(!prepareMeterBegun){prepareMeterBegun=true;prepareMeter.beginFrame(profileSampled(),prepareFrameSerial);}return prepareMeter.sample();}
    std::atomic<std::uint32_t> prepareWarmCounter{0};std::atomic<std::uintptr_t> prepareWarmSink{0}; /* M2's publish and result stand-ins */
    // Watchdog re-arm: a settled abandoned worker is taken back PrepareRearmAfterMs after its abandon, at
    // most PrepareRearms times a session (a one-off scheduler stall must not cost the whole session).
    static constexpr unsigned PrepareRearms=4;static constexpr DWORD PrepareRearmAfterMs=10000;
    unsigned prepareRearms=0;DWORD prepareAbandonTick=0;
    // 0.3.183: the prepare epoch advances only at an abandon; a replay stamped with the abandoned epoch was
    // captured in the abandoned frame before the abandon (the only replays the abandoned worker can reach).
    std::uint64_t prepareEpoch=1,prepareAbandonedEpoch=0;
    bool prepareOffload()const{return prepareCores>=6&&quality.actorShadows&&selectionTuning().stableIdentity&&!prepareWorker.abandoned();}
    bool prepareUnsettled()const{return prepareWorker.abandoned()&&!prepareWorker.settled();}
    bool prepareHeld(const Replay& p)const{return prepareUnsettled()&&p.prepareStamp==prepareAbandonedEpoch;}
    // The caches' owner opens them: a deferred clear (registerShader), then this frame's statistics.
    void prepareCachesOpen(){
        prepareOpened=true;
        if(prepareCachesStale.exchange(false)){prepareCaches->sampled.clear();prepareCaches->bones.clear();++prepareStats.staleClears;}
        prepareCaches->sampled.beginFrame();prepareCaches->bones.beginFrame();
    }
    // The frame's first selected skinned capture: worker or inline.
    void prepareOpen(){
        prepareTimed=profileSampled();prepareFrame=PrepareFrame::Inline;
        const bool abInline=NorthlightRenderThreadProbe::profiling()&&(GetTickCount()/10000u)%2u==1u;
        if(!prepareOffload()){prepareStats.mode="inline";return;}
        if(abInline){prepareStats.mode="ab-inline";return;}
        NorthlightActorPrepare::Frame frame;std::memcpy(frame.inverseView,context.inverseView,sizeof frame.inverseView);std::memcpy(frame.camera,context.camera,sizeof frame.camera);
        frame.timed=prepareTimed;prepareCachesOpen();
        if(prepareWorker.begin(frame,*prepareCaches)){prepareFrame=PrepareFrame::Worker;prepareStats.mode="worker";}
        else prepareStats.mode="inline";
    }
    // captureModel, right after a draw joined replays: publish it if selected and skinned.
    void preparePublish(){
        replays.back()->prepareStamp=prepareEpoch; /* 0.3.183: every accepted capture (constant donors too) */
        if(prepareFrame==PrepareFrame::Joined)return; /* captured after the selection: never selected */
        const Replay& p=*replays.back();if(!p.shadowSelected||!p.shadowSkinned)return;
        if(prepareFrame==PrepareFrame::None)prepareOpen();
        if(prepareFrame!=PrepareFrame::Worker)return;
        prepareWorker.publish(&p,replays.size()-1); /* false (arena full): the join's check resyncs inline */
    }
    // The worker did not acknowledge within the watchdog: inline until it is re-armed (prepareEndFrame).
    // It may still be in one record: until it settles, its caches are set aside, and (0.3.183) only this
    // frame's replays captured so far (stamped with the abandoned epoch) are quarantined, its arena is
    // moved aside and every program a record can point at is pinned.
    void prepareAbandon(){
        std::unique_ptr<NorthlightActorPrepare::Caches> fresh;
        try{fresh=std::make_unique<NorthlightActorPrepare::Caches>();prepareQuarantinedPrograms.reserve(captureShaders.size()+actorPrograms.size()+retiredPrograms.size());prepareQuarantinedDecls.reserve(1);}catch(...){fresh.reset();}
        if(fresh){prepareAbandonedCaches=std::move(prepareCaches);prepareCaches=std::move(fresh);prepareCachesOpen();
            for(const auto& s:captureShaders)if(s.second.program)prepareQuarantinedPrograms.push_back(s.second.program); /* reserved: no throw */
            for(const auto& a:actorPrograms)if(a.second)prepareQuarantinedPrograms.push_back(a.second);
            for(const auto& r:retiredPrograms)if(r)prepareQuarantinedPrograms.push_back(r);
            if(prepareDecls){prepareQuarantinedDecls.push_back(std::move(prepareDecls));try{prepareDecls=std::make_unique<NorthlightActorPrepare::DeclArena>();}catch(...){}} /* none: per-record copies */
        }else while(!prepareWorker.settled())NorthlightActorPrepare::pause(); /* no memory: wait for the old ones, hold nothing */
        prepareAbandonedEpoch=prepareEpoch++;
        prepareStats.mode="watchdog";
        prepareAbandonTick=GetTickCount();
        logf("PREPARE worker watchdog: no acknowledgement within %.0f ms; actor prepare inline %s (re-arms %u of %u)",NorthlightActorPrepare::Worker<Replay>::WatchdogMs,
            prepareRearms<PrepareRearms?"until the worker settles, at least 10 s":"for this session",prepareRearms,PrepareRearms);
    }
    // Stops an open worker frame; its outputs are discarded (endFrame, reset, releaseGPU, trimMemory).
    void prepareQuiesce(){
        if(prepareFrame==PrepareFrame::Worker){const auto r=prepareWorker.stop();if(r.timedOut)prepareAbandon();}
        if(prepareFrame!=PrepareFrame::None)prepareFrame=PrepareFrame::Joined;prepareOutputsReady=false;
    }
    // endFrame (after prepareQuiesce): the next frame starts closed; a settled abandoned worker frees
    // what it held and, within the re-arm budget, returns to use.
    void prepareEndFrame(){
        prepareFrame=PrepareFrame::None;prepareOutputsReady=false;prepareOpened=false;prepareTimed=false;prepareCount=0;prepareMeterBegun=false;prepareMeter.beginFrame(false,0);++prepareFrameSerial;prepareStats=PrepareStats{};
        if(prepareWorker.abandoned()&&prepareWorker.settled()){
            if(!prepareQuarantine.empty()){auto held=std::move(prepareQuarantine);prepareQuarantine.clear();for(auto& p:held)recycleReplay(p.release());}
            prepareQuarantinedDecls.clear();prepareQuarantinedPrograms.clear(); /* 0.3.183: the abandoned frame's arena and pins */
            prepareAbandonedCaches.reset();
            if(prepareRearms<PrepareRearms&&GetTickCount()-prepareAbandonTick>=PrepareRearmAfterMs&&prepareWorker.rearm()){
                ++prepareRearms;logf("PREPARE worker re-armed after a watchdog (re-arms %u of %u)",prepareRearms,PrepareRearms);}}
    }
    // The join, the first statement of selectShadowReplays: stop the worker (at most its record in
    // flight; a sleeping one at once), prepare [done, published) inline with the frozen camera and the
    // returned caches, from the state the last output carries.
    void prepareJoin(){
        if(prepareFrame!=PrepareFrame::Worker){if(!prepareOpened)prepareCachesOpen();prepareFrame=PrepareFrame::Joined;return;}
        prepareFrame=PrepareFrame::Joined;
        const auto r=prepareWorker.stop();
        prepareStats.published=r.published;prepareStats.wakes=prepareWorker.wakes();
        if(prepareTimed){prepareStats.joinWaitMs=r.waitMs;prepareStats.notifyUs=prepareWorker.notifyUs();prepareStats.workerMs=prepareWorker.busyMs();}
        if(r.timedOut){prepareAbandon();return;} /* the selection prepares inline with the new caches */
        if(r.failed&&prepareFaults++<4)logf("PREPARE worker record failed (exception); the join prepared it inline");
        const auto started=prepareTimed?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        try{
            auto* outputs=prepareWorker.outputs();const auto& frame=prepareWorker.frame();
            for(std::uint32_t k=r.done;k<r.published;++k){NorthlightActorPrepare::State state=k?outputs[k-1].after:NorthlightActorPrepare::State{};
                NorthlightActorPrepare::prepareRecord(state,*prepareWorker.record(k),prepareWorker.index(k),*prepareCaches,frame.inverseView,frame.camera,outputs[k]);}
            prepareCount=r.published;prepareOutputsReady=true;
        }catch(...){prepareOutputsReady=false;} /* the selection prepares inline */
        prepareStats.workerRecords=r.done;prepareStats.joinInline=r.published-r.done;
        if(prepareTimed){prepareStats.joinInlineMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();prepareSelfCheck();}
        if(profileSampled()&&r.published)prepareWarmHandoff(r.published); /* M2 */
    }
    // RenderProfile sample frames: every 16th output recomputed inline (scratch caches, the carried state
    // of the output before it) and compared bit for bit; the frozen camera compared with the context.
    void prepareSelfCheck(){
        if(!prepareOutputsReady)return;
        if(!prepareCheckCaches)try{prepareCheckCaches=std::make_unique<NorthlightActorPrepare::Caches>();}catch(...){return;}
        const auto* outputs=prepareWorker.outputs();const auto& frame=prepareWorker.frame();
        if(std::memcmp(frame.inverseView,context.inverseView,sizeof frame.inverseView)||std::memcmp(frame.camera,context.camera,sizeof frame.camera))++prepareStats.mismatch;
        for(std::uint32_t k=0;k<prepareCount;k+=16){NorthlightActorPrepare::State state=k?outputs[k-1].after:NorthlightActorPrepare::State{};NorthlightActorPrepare::Output check;
            NorthlightActorPrepare::prepareRecord(state,*prepareWorker.record(k),prepareWorker.index(k),*prepareCheckCaches,frame.inverseView,frame.camera,check);
            prepareStats.mismatch+=!NorthlightActorPrepare::same(check,outputs[k]);}
    }
    // 0.3.179: end of endFrame, after the recycle: what the frame's replays pointed at may go. 0.3.183: an
    // abandoned worker reads only its frame's arena and pinned programs (prepareAbandon; released at settle).
    void prepareFrameRelease(){
        retiredPrograms.clear();
        if(prepareDecls)prepareDecls->reset();else try{prepareDecls=std::make_unique<NorthlightActorPrepare::DeclArena>();}catch(...){}
    }
    // 0.3.177 (r83): the prepare inputs of a selected skinned draw, at capture (just before it joins
    // replays): its program (0.3.179: the capture metadata's) and a copy of its declaration; declared:
    // program && the declaration was read (the stable selection's declared()).
    void prepareFill(Replay& p,const CaptureShader& metadata){
        p.program=metadata.program.get(); /* 0.3.179 (T1): the capture metadata's (the program map's object): no lookup, no reference */
        p.declCopy=nullptr;p.elementCount=0;
        // 0.3.179 (T2): the frame's copy of the declaration, read once a frame; the per-record copy when full.
        const NorthlightActorPrepare::DeclCopy* copy=p.program&&prepareDecls?prepareDecls->find(p.decl,[this](IDirect3DVertexDeclaration9* decl,const D3DVERTEXELEMENT9*& elements,UINT& count){
            return declarationCache.get(decl,elements,count);}):nullptr;
        if(copy){p.declCopy=copy;p.declared=copy->declared;}
        else{const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
            p.declared=p.program&&declarationCache.get(p.decl,elements,count)&&count<=p.elements.size();
            p.elementCount=p.declared?count:0;if(p.declared)std::copy(elements,elements+count,p.elements.begin());}
    }
    // 0.3.179 (M2): a warm lower bound of the handoff. At the join of a RenderProfile sample frame, the
    // fill's work for every published record again, into a scratch value (no replay written, no reference
    // taken), with a release store standing in for the publish; one clock pair.
    void prepareWarmHandoff(std::uint32_t published){
        const auto start=std::chrono::steady_clock::now();std::uintptr_t sink=0;
        for(std::uint32_t k=0;k<published;++k){const Replay& p=*prepareWorker.record(k);
            struct {const NorthlightActorDeformation::Program* program;const NorthlightActorPrepare::DeclCopy* decl;bool declared;} scratch{p.program,nullptr,false};
            if(scratch.program&&prepareDecls){scratch.decl=prepareDecls->find(p.decl,[this](IDirect3DVertexDeclaration9* decl,const D3DVERTEXELEMENT9*& elements,UINT& count){return declarationCache.get(decl,elements,count);});
                scratch.declared=scratch.decl&&scratch.decl->declared;}
            sink+=reinterpret_cast<std::uintptr_t>(scratch.program)^reinterpret_cast<std::uintptr_t>(scratch.decl)^std::uintptr_t(scratch.declared);
            prepareWarmCounter.store(k+1,std::memory_order_release);}
        prepareWarmSink.store(sink,std::memory_order_relaxed);
        prepareStats.handoffWarmUs=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
    }
    // One prepared draw into the selection: its draw, and (0.3.176 S2) the rigid bone it tested.
    void prepareConsume(const NorthlightActorPrepare::Output& out){
        actorShadowDraws.push_back(out.item);Replay& stored=*replays[out.item.index];stored.boneKnown=out.tested;stored.bone=out.item.bone;
    }
    // The stable selection's prepare: the joined worker's outputs when they are exactly this frame's
    // selected skinned draws in replays order (else a resync), otherwise prepareRecord over them inline.
    void prepareStable(size_t& distanceTests,size_t& distanceReused){
        if(prepareOutputsReady){prepareOutputsReady=false;
            const auto* outputs=prepareWorker.outputs();std::uint32_t k=0;bool aligned=true;
            for(size_t index=0;index<replays.size()&&aligned;++index){const Replay& p=*replays[index];
                if(!p.shadowSelected||!p.shadowSkinned)continue;aligned=k<prepareCount&&outputs[k].item.index==index;++k;}
            if(aligned&&k==prepareCount){for(k=0;k<prepareCount;++k)prepareConsume(outputs[k]);
                if(prepareCount){distanceTests+=outputs[prepareCount-1].after.distanceTests;distanceReused+=outputs[prepareCount-1].after.distanceReused;}
                return;}
            ++prepareStats.resync;}
        NorthlightActorPrepare::State state;NorthlightActorPrepare::Output out;
        for(size_t index=0;index<replays.size();++index){const Replay& p=*replays[index];
            if(!p.shadowSelected||!p.shadowSkinned)continue;
            NorthlightActorPrepare::prepareRecord(state,p,index,*prepareCaches,context.inverseView,context.camera,out);prepareConsume(out);}
        distanceTests+=state.distanceTests;distanceReused+=state.distanceReused;
    }
    // Stable per-actor quota (see actor_shadow_selection.h). Distances reuse the
    // same sampled-vertex rule as the legacy ranking; rigid palette tests are
    // cached per immutable snapshot owner.
    NorthlightActorShadowSelection::Result selectStableActors(size_t budget,size_t& distanceTests,size_t& distanceReused){
        const auto started=captureSampled?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        actorShadowDraws.clear();actorShadowDraws.reserve(replays.size());
        const auto tuning=selectionTuning();
        // 0.3.177 (r83): the stable identity path is prepareRecord (prepare_worker.h) per selected skinned
        // draw in replays order, the same function the prepare worker runs.
        if(tuning.stableIdentity)prepareStable(distanceTests,distanceReused);
        else{ /* the legacy tuning (FlickerFixes=false): the 0.3.139 loop */
        unsigned previousGroup=UINT_MAX;IDirect3DVertexShader9* previousShader=nullptr;
        IDirect3DVertexDeclaration9* previousDecl=nullptr;float previousDistance=0,previousAt[3]={};bool previousKnown=false,groupRigid=true,groupStationary=false;unsigned groupDraw=0;
        for(size_t index=0;index<replays.size();++index){const auto& p=*replays[index];
            if(!p.shadowSelected||!p.shadowSkinned)continue;
            NorthlightActorShadowSelection::Draw item;item.index=index;item.bytes=p.mesh().byteSize();item.group=p.constantGroup;
            // Program and declaration lookups only for draws that test a distance
            // or a palette (reused-distance draws of multi-bone groups need none).
            auto program=actorPrograms.end();const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;int declaredState=-1;
            auto declared=[&]{if(declaredState<0){program=actorPrograms.find(p.originalShader);declaredState=program!=actorPrograms.end()&&declarationCache.get(p.decl,elements,count);}return declaredState==1;};
            if(p.constantGroup==previousGroup&&p.originalShader==previousShader&&p.decl==previousDecl){
                item.known=previousKnown;item.distanceSquared=previousDistance;std::memcpy(item.at,previousAt,sizeof item.at);++distanceReused;
            }else{
                ++distanceTests;
                item.known=declared()&&prepareCaches->sampled.distance(*program->second,p.mesh(),p.shared,p.decl,elements,count,
                    p.constants,context.inverseView,context.camera,item.distanceSquared,item.at);
            }
            // A group is rigid only if every draw is: after its first multi-bone
            // draw the remaining draws need no palette test. Only a group's first
            // draw supplies the actor identity key.
            const bool first=p.constantGroup!=previousGroup;if(first)groupRigid=true;
            item.bone=groupRigid&&declared()?prepareCaches->bones.bone(*program->second,p.mesh(),p.shared,p.decl,elements,count):NAN;item.rigid=!std::isnan(item.bone);
            {Replay& stored=*replays[index];stored.boneKnown=groupRigid&&declared();stored.bone=item.bone;} /* 0.3.176 (S2): rigidObserveGroup reuses it */
            groupRigid=item.rigid;
            if(first){std::uint64_t key=14695981039346656037ull;auto mix=[&](uint64_t n){key=(key^n)*1099511628211ull;};
                mix(reinterpret_cast<uintptr_t>(p.originalShader));mix(reinterpret_cast<uintptr_t>(p.decl));mix(p.mesh().vertexCount);mix(p.mesh().primitiveCount);mix(item.bytes);
                item.key=key;groupDraw=0;
                // Two extra world samples per draw (first two draws) only for actors
                // the history marks as possibly stationary: the idle-pose check.
                groupStationary=tuning.stationary&&!item.rigid&&item.known&&actorShadowHistory.stationaryHint(key,item.at);}
            if(groupStationary&&groupDraw<2&&declared())for(unsigned x=0;x<2;++x)
                if(NorthlightActorDeformation::sampledExtraWorld(*program->second,p.mesh(),elements,count,x,p.constants,context.inverseView,item.extra[item.extras]))++item.extras;
            ++groupDraw;
            previousGroup=p.constantGroup;previousShader=p.originalShader;previousDecl=p.decl;
            previousKnown=item.known;previousDistance=item.distanceSquared;std::memcpy(previousAt,item.at,sizeof previousAt);actorShadowDraws.push_back(item);
        }}
        // Stable identity judges stillness by the palette root: no extra vertex samples.
        const auto prepared=captureSampled?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        // Radius: the eye and the pivot (origin) find the player on the centre ray; the quota
        // ranks (shouldRank) on the bytes inside the radius.
        const NorthlightActorShadowSelection::Radius radius{float(quality.actorShadowRadius),context.camera,true,true};
        const auto result=NorthlightActorShadowSelection::choose(actorShadowDraws,actorShadowScratch,budget,actorShadowHistory,captureSampled,
            NorthlightActorShadowSelection::FlickerFixes&&actorShadowOriginValid?actorShadowOrigin:nullptr,tuning,radius.active()?&radius:nullptr);
        if(captureSampled){const auto chosen=std::chrono::steady_clock::now();
            actorShadowPrepareMs=std::chrono::duration<double,std::milli>(prepared-started).count();actorShadowChooseMs=std::chrono::duration<double,std::milli>(chosen-prepared).count();}
        for(const auto& item:actorShadowDraws)replays[item.index]->shadowSelected=item.keep;
        if(shadowFate.active())for(const auto& a:actorShadowScratch.actors){using namespace NorthlightShadowFate;
            const Fate f=a.rigid?Rigid:(a.body!=SIZE_MAX||a.orphan)?Attached:a.exempt?Exempt:a.waiting?Waiting:a.keep?Kept:Dropped;
            for(size_t i=a.first;i<a.first+a.count;++i){auto& r=*replays[actorShadowDraws[i].index];r.fateClass=f;r.fateDistance=std::sqrt(std::max(0.f,actorShadowDraws[i].distanceSquared));}}
        return result;
    }
