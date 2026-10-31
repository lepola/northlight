// Included inside WorldRenderer. Each source owns a complete cube and its static cache.
    static constexpr UINT PointResolution=256;
    struct PointContentMemo {const NorthlightLocalShadowSignature::Records* records=nullptr;uint64_t generation=0;float position[3]={},range=0;bool valid=false;NorthlightLocalShadowSignature::Digest faces[6];};
    struct PointSlot {
        IDirect3DCubeTexture9* cube=nullptr;
        IDirect3DSurface9* faces[6]={},*depth=nullptr;
        IDirect3DTexture9 *cacheFace[6]={},*scratch=nullptr;
        IDirect3DSurface9 *cacheSurface[6]={},*scratchSurface=nullptr;
        IDirect3DVertexShader9* shadowVS=nullptr;
        IDirect3DPixelShader9 *shadowPS=nullptr,*lightingPS=nullptr;
        bool cacheValid=false,ready=false;uint64_t cacheSource=0,cacheChunks=0;
        NorthlightLocalShadowSignature::Digest faceContent[6];bool faceKnown[6]={};uint64_t faceSerial[6]={};
        mutable PointContentMemo contentMemo;
        NorthlightPointShadow::FaceCycle faceCycle;unsigned faceWeights[6]={};
        NorthlightPointShadow::RefreshSchedule schedule;
        NorthlightLocalLights::Light selected{};std::string map;DWORD retryAt=0;
    };
    std::array<PointSlot,NorthlightPointShadow::MaxLights> pointSlots;
    unsigned pointIndex=0;bool pointReady=false;
    PointSlot& pointSlot(){return pointSlots[pointIndex];}
    const PointSlot& pointSlot()const{return pointSlots[pointIndex];}
    unsigned pointFacesRebuilt=0,pointFacesKept=0,pointFailures=0,pointDraws=0,pointCulled=0,pointBoundsValid=0;
    unsigned pointUpdates=0,pointReuses=0,pointReportedLimit=0;
    using PointCandidates=std::array<std::vector<size_t>,6>;
    PointCandidates pointStaticCandidates,pointTerrainCandidates,pointLiveCandidates,pointReplayCandidates;
    size_t pointBoundVertices=0,pointBoundOperations=0;
    struct PointLiveBatch {UINT start,count;V low,high;};std::vector<PointLiveBatch> pointLiveBatches;

    void releasePointSlot(){
        auto& p=pointSlot();p.schedule.invalidate();
        for(auto& f:p.faces)drop(f);drop(p.cube);drop(p.depth);
        for(auto& t:p.cacheFace)drop(t);for(auto& f:p.cacheSurface)drop(f);drop(p.scratch);drop(p.scratchSurface);
        drop(p.shadowVS);drop(p.shadowPS);drop(p.lightingPS);p=PointSlot{};
    }
    void releasePointGPU(){
        for(pointIndex=0;pointIndex<pointSlots.size();++pointIndex)releasePointSlot();pointIndex=0;
        pointReady=false;pointUpdates=pointReuses=pointReportedLimit=0;localLightTracker.reset();localLightQpc=0;
        for(auto* lists:{&pointStaticCandidates,&pointTerrainCandidates,&pointLiveCandidates,&pointReplayCandidates})
            for(auto& list:*lists)std::vector<size_t>().swap(list);
    }
    bool pointCheck(HRESULT hr,const char* operation){
        if(SUCCEEDED(hr))return true;
        pointSlot().schedule.invalidate();pointSlot().cacheValid=false;
        pointSlot().ready=false;pointSlot().retryAt=GetTickCount()+1000;
        if(pointFailures++<12)logf("POINT pass skipped: %s hr=%08lx (world effects retained)",operation,(unsigned long)hr);
        return false;
    }
    bool pointResources(){
        if(pointSlot().retryAt&&LONG(GetTickCount()-pointSlot().retryAt)<0)return false;
        if(pointSlot().cube&&pointSlot().depth&&pointSlot().shadowVS&&pointSlot().shadowPS&&pointSlot().lightingPS&&pointSlot().scratch&&pointSlot().scratchSurface){
            bool faces=true;for(auto* face:pointSlot().faces)faces=faces&&face;for(auto* face:pointSlot().cacheSurface)faces=faces&&face;if(faces){pointSlot().retryAt=0;return true;}
        }
        // Release partial allocations before a retry; selection hysteresis is
        // retained across ordinary allocation retries.
        for(auto& face:pointSlot().faces)drop(face);drop(pointSlot().cube);drop(pointSlot().depth);
        for(auto& t:pointSlot().cacheFace)drop(t);for(auto& sf:pointSlot().cacheSurface)drop(sf);drop(pointSlot().scratch);drop(pointSlot().scratchSurface);pointSlot().cacheValid=false;
        drop(pointSlot().shadowVS);drop(pointSlot().shadowPS);drop(pointSlot().lightingPS);
        for(unsigned i=0;i<6;++i){if(!pointCheck(d->CreateTexture(PointResolution,PointResolution,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&pointSlot().cacheFace[i],nullptr),"cube cache face")||!pointSlot().cacheFace[i]||
            !pointCheck(pointSlot().cacheFace[i]->GetSurfaceLevel(0,&pointSlot().cacheSurface[i]),"cube cache surface")||!pointSlot().cacheSurface[i])return pointCheck(E_FAIL,"cube cache face");}
        if(!pointCheck(d->CreateTexture(PointResolution,PointResolution,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&pointSlot().scratch,nullptr),"cube scratch")||!pointSlot().scratch||
           !pointCheck(pointSlot().scratch->GetSurfaceLevel(0,&pointSlot().scratchSurface),"cube scratch surface")||!pointSlot().scratchSurface)return pointCheck(E_FAIL,"cube scratch");
        if(!pointCheck(d->CreateCubeTexture(PointResolution,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&pointSlot().cube,nullptr),"create R32F cube"))return false;
        if(!pointSlot().cube)return pointCheck(E_FAIL,"empty cube");
        for(unsigned i=0;i<6;++i){if(!pointCheck(pointSlot().cube->GetCubeMapSurface(D3DCUBEMAP_FACES(i),0,&pointSlot().faces[i]),"cube surface"))return false;
            if(!pointSlot().faces[i])return pointCheck(E_FAIL,"empty cube face");}
        if(!pointCheck(d->CreateDepthStencilSurface(PointResolution,PointResolution,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&pointSlot().depth,nullptr),"cube depth"))return false;
        if(!pointCheck(d->CreateVertexShader(kLocalShadowVSShader,&pointSlot().shadowVS),"cube vertex shader")||
           !pointCheck(d->CreatePixelShader(kLocalShadowPSShader,&pointSlot().shadowPS),"cube pixel shader")||
           !pointCheck(d->CreatePixelShader(kLocalLightingShader,&pointSlot().lightingPS),"local lighting shader"))return false;
        if(!pointSlot().depth||!pointSlot().shadowVS||!pointSlot().shadowPS||!pointSlot().lightingPS)return pointCheck(E_FAIL,"empty optional resource");
        pointSlot().retryAt=0;return true;
    }
    void pointRecordTerrain(UINT start,UINT count,const NorthlightTerrainCapture::Bounds& bounds){
        // Reuse the snapshot's already validated, referenced-triangle bounds;
        // do not rescan hundreds of thousands of terrain vertices each frame.
        if(count)pointLiveBatches.push_back({start,count,vec(bounds.low),vec(bounds.high)});
    }
    unsigned pointMask(V low,V high,const NorthlightPointShadow::FaceClip& clip)const{
        float lo[]={low.x,low.y,low.z},hi[]={high.x,high.y,high.z};
        // Invalid bounds must draw. The generic sphere helper rejects invalid
        // inputs, so use the replay helper's explicit fail-open semantics.
        NorthlightReplayBounds::Bounds bounds;std::memcpy(bounds.low,lo,12);std::memcpy(bounds.high,hi,12);bounds.valid=true;
        if(NorthlightReplayBounds::outsideSphere(bounds,pointSlot().selected.position,pointSlot().selected.attenuationEnd))return 0;
        return clip.mask(lo,hi,true);
    }
    void pointAppendCandidates(PointCandidates& lists,size_t index,unsigned mask,unsigned faces=63){
        for(unsigned face=0;face<6;++face){if(!(faces&(1u<<face)))continue;if(mask&(1u<<face))lists[face].push_back(index);else ++pointCulled;}
    }
    // Content digests of the six faces for `light`; null when the records are unknown.
    // Recomputed only when the records, the mesh generation or the light's cube change.
    const NorthlightLocalShadowSignature::Digest* pointFaceDigests(const NorthlightLocalLights::Light& light)const{
        const auto* records=uploadedLocalShadowRecords.get();if(!records)return nullptr;
        auto& memo=pointSlot().contentMemo;
        if(!memo.valid||memo.records!=records||memo.generation!=meshGeneration||memo.range!=light.attenuationEnd||std::memcmp(memo.position,light.position,sizeof memo.position)){
            float matrices[6][16];memo.valid=false;
            for(unsigned face=0;face<6;++face)if(!NorthlightPointShadow::faceMatrix(light.position,.1f,light.attenuationEnd,face,matrices[face],PointResolution))return nullptr;
            NorthlightPointShadow::faceContent(*records,light,matrices,memo.faces);
            memo.records=records;memo.generation=meshGeneration;memo.range=light.attenuationEnd;std::memcpy(memo.position,light.position,sizeof memo.position);memo.valid=true;
        }
        return memo.faces;
    }
    // Static cache faces to redraw for `light`: all six for a new/changed light or an invalid
    // cache, else each face whose content changed (unknown records: the generation rule).
    unsigned pointStaticRebuild(const NorthlightLocalLights::Light& light,bool lightChanged)const{
        if(!pointSlot().cacheValid||lightChanged||pointSlot().cacheSource!=light.sourceId)return 63;
        return NorthlightPointShadow::staleFaces(pointSlot().faceContent,pointSlot().faceKnown,pointSlot().faceSerial,pointFaceDigests(light),meshGeneration);
    }
    void pointLogSchedule(bool reused){
        if(NorthlightDiagnostics::enabled()&&(frames==0||frames%120==0)){
            logf("POINT schedule updates=%u reuses=%u reused=%u crowd=%u periodMs=%u replayPackets=%zu draws=%u culled=%u",
                pointUpdates,pointReuses,unsigned(reused),unsigned(replays.size()>=256),replays.size()>=256?33u:0u,replays.size(),pointDraws,pointCulled);
            pointUpdates=pointReuses=0;
            if(NorthlightQuality::renderProfile(quality))logf("POINT static faces rebuilt=%u keptByContent=%u",pointFacesRebuilt,pointFacesKept);
            pointFacesRebuilt=pointFacesKept=0;
        }
    }
    // 0.3.143 (NorthlightReplayBoundsJob::Async): render() starts this pass on the
    // bounds worker right after shadow selection and the first pointBounds reader
    // joins it. Until then packets hold cleared bounds: draw, never cull.
    NorthlightReplayBoundsJob::Worker replayBoundsWorker;
    bool replayBoundsDiagnostics=false;double replayBoundsKickMs=0;
    unsigned replayBoundsLogFrame=0;bool replayBoundsLogSeen=false;
    bool replayBoundsDiagnosticSample(){
        // frames counts successful world composites and can stay at zero (or
        // a multiple of 120) while geometry is absent. Sample that value once.
        if(!NorthlightDiagnostics::enabled()||(frames&&frames%120)||(replayBoundsLogSeen&&replayBoundsLogFrame==frames))return false;
        replayBoundsLogSeen=true;replayBoundsLogFrame=frames;return true;
    }
    // A metadata fallback (per-frame preparation cap) keeps the synchronous
    // pass' full-key evaluation as an unshared snapshot of the same program,
    // at most once per pair and FallbackPreparations pairs per frame. Beyond
    // that a pair is Unsupported this frame: drawn, never culled.
    static constexpr size_t FallbackPreparations=16;
    struct BoundsFallback {IDirect3DVertexShader9* shader;IDirect3DVertexDeclaration9* decl;std::shared_ptr<const NorthlightReplayBounds::Prepared> prepared;};
    std::vector<BoundsFallback> replayBoundsFallbacks;size_t replayBoundsCapped=0;
    std::shared_ptr<const NorthlightReplayBounds::Prepared> replayBoundsPrepared(IDirect3DVertexShader9* shader,IDirect3DVertexDeclaration9* decl){
        bool built=false;
        const auto build=[&]()->std::shared_ptr<const NorthlightReplayBounds::Prepared>{
            built=true;auto program=actorPrograms.find(shader);
            if(program==actorPrograms.end())return {};
            const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
            if(!declarationCache.get(decl,elements,count))return {};
            return NorthlightReplayBounds::EnvelopeCache::prepareProgram(*program->second,elements,count);
        };
        auto prepared=replayBoundsMetadata.get(shader,decl,build);
        if(prepared||built)return prepared; /* built and still null: no program/declaration */
        for(const auto& f:replayBoundsFallbacks)if(f.shader==shader&&f.decl==decl)return f.prepared;
        if(replayBoundsFallbacks.size()>=FallbackPreparations){++replayBoundsCapped;return {};}
        prepared=build();
        try{replayBoundsFallbacks.push_back({shader,decl,prepared});}catch(...){}
        return prepared;
    }
    void replayBoundsKick(){
        if(!NorthlightReplayBoundsJob::Async||replayBoundsWorker.pending())return;
        const auto start=std::chrono::steady_clock::now();
        // Allocation or thread failure leaves the frame to the synchronous pass,
        // before any per-frame cache state has advanced.
        auto& items=replayBoundsWorker.items();
        try{items.reserve(replays.size());replayBoundsFallbacks.reserve(FallbackPreparations);}catch(...){return;}
        if(!replayBoundsWorker.ready())return;
        const bool diagnostics=replayBoundsDiagnosticSample(); /* logs and envelope timing fields only */
        pointBoundsValid=0;replayBoundsCache.beginFrame(diagnostics);replayBoundsMetadata.beginFrame();
        // Program/declaration lookups stay on the render thread.
        replayBoundsFallbacks.clear();replayBoundsCapped=0;
        replayBoundsWorker.nearList().begin(actorMatrices[0],actorValid[0],actorMatrices[1],actorValid[1]); /* the actor maps as last drawn: casters that can be wholly inside one are visited first, the self's own packets before all */
        NorthlightReplayBoundsJob::gather(items,replays,[&](IDirect3DVertexShader9* shader,IDirect3DVertexDeclaration9* decl){return replayBoundsPrepared(shader,decl);},&replayBoundsWorker.nearList());
        replayBoundsFallbacks.clear(); /* pins now live in the items only */
        replayBoundsWorker.start(replayBoundsCache,context.inverseView);
        replayBoundsDiagnostics=diagnostics;
        replayBoundsKickMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
    // First pointBounds reader; also when render() returns.
    void replayBoundsJoin(){
        if(!replayBoundsWorker.pending())return;
        const auto start=std::chrono::steady_clock::now();
        const auto result=replayBoundsWorker.finish();
        const double waitMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        NorthlightReplayBoundsJob::apply(replayBoundsWorker.items(),replays,!result.failed);
        pointBoundsValid=result.failed?0:result.totals.valid;pointBoundVertices=result.totals.boundVertices;pointBoundOperations=result.totals.boundOperations;
        if(NorthlightDiagnostics::enabled()){replayNearSize=unsigned(replayBoundsWorker.nearList().size());replayNearStops+=result.totals.nearStops;}
        if(replayBoundsDiagnostics){
            const double mainMs=replayBoundsKickMs+std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            pointLogReplayBounds(result.totals,result.workerMs,mainMs,result.workerMs,waitMs,result.startLagMs,true);
        }
    }
    // Before the bounds cache or the packets change outside render(): the job's
    // results are discarded (the packets still hold cleared bounds).
    void replayBoundsAbandon(){
        if(!replayBoundsWorker.pending())return;
        replayBoundsWorker.finish(true);
        for(auto& item:replayBoundsWorker.items())item=NorthlightReplayBoundsJob::Item{};
    }
    void pointCalculateReplayBounds(){
        if(replayBoundsWorker.pending())return; /* 0.3.143: in flight since selection, joined by the first reader */
        const auto start=std::chrono::steady_clock::now();
        using WorkKind=NorthlightReplayBounds::WorkKind;
        NorthlightReplayBounds::Budget cheapBudget,heavyBudget,buildBudget;
        cheapBudget.maxVertices=0;cheapBudget.maxOperations=163840;
        heavyBudget.maxVertices=0;heavyBudget.maxOperations=32768;
        buildBudget.maxVertices=2048;buildBudget.maxOperations=65536;
        const bool diagnostics=replayBoundsDiagnosticSample(); /* logs and envelope timing fields only */
        pointBoundsValid=0;replayBoundsCache.beginFrame(diagnostics);replayBoundsMetadata.beginFrame();
        // Hints contain no entry/declaration pointers. Current pose bounds and
        // classifications never survive to another frame or recycled packet.
        for(auto& p:replays){p->pointBounds={};p->boundsWork={};p->boundsPrepared.reset();}
        size_t cheapVisited=0,heavyVisited=0,buildVisited=0;
        const auto visit=[&](size_t index,unsigned phase){
            auto& p=replays[index];if(p->pointBounds.valid)return false;
            // This check precedes declaration/program/cache lookups. In a warm
            // crowd the cold pass consequently performs no repeated lookups.
            if(phase==1&&p->boundsWork.kind!=WorkKind::Heavy)return false;
            if(phase==2&&p->boundsWork.kind!=WorkKind::Cold)return false;
            // The borrowed cheap turn cannot spend already-closed heavy/build
            // allowances. Newly built cold packets evaluate on a later frame.
            if(phase==0&&(p->boundsWork.kind==WorkKind::Heavy||p->boundsWork.kind==WorkKind::Cold||p->boundsWork.kind==WorkKind::Unsupported))return false;
            if(!p->shared){p->boundsWork.kind=WorkKind::Unsupported;return false;}
            if(phase==0&&!p->boundsPrepared){
                p->boundsPrepared=replayBoundsMetadata.get(p->originalShader,p->decl,[&]()->std::shared_ptr<const NorthlightReplayBounds::Prepared>{
                    // Key preparation used to run inside each cache lookup.
                    // Charge misses to the same budget; hits need no new timer.
                    struct PreparationCost {
                        NorthlightReplayBounds::Cache& cache;
                        std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
                        ~PreparationCost(){cache.chargeCheapPreparation(std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count()));}
                    } cost{replayBoundsCache};
                    auto program=actorPrograms.find(p->originalShader);
                    if(program==actorPrograms.end())return {};
                    const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
                    if(!declarationCache.get(p->decl,elements,count))return {};
                    return NorthlightReplayBounds::EnvelopeCache::prepareProgram(*program->second,elements,count);
                });
            }
            NorthlightReplayBounds::Status status;
            if(p->boundsPrepared){
                const auto& prepared=*p->boundsPrepared;
                if(phase==0){++cheapVisited;status=replayBoundsCache.calculateCheap(prepared,p->mesh(),p->shared,p->constants,context.inverseView,cheapBudget,p->pointBounds,p->boundsWork);}
                else if(phase==1){++heavyVisited;status=replayBoundsCache.evaluateHeavy(prepared,p->mesh(),p->shared,p->constants,context.inverseView,heavyBudget,p->pointBounds,p->boundsWork);}
                else{++buildVisited;status=replayBoundsCache.buildEnclosed(prepared,p->mesh(),p->shared,p->constants,context.inverseView,buildBudget,p->pointBounds);}
            }else{
                // Allocation/metadata-cap failures retain the audited full key
                // path and do not turn missing metadata into missing shadows.
                auto program=actorPrograms.find(p->originalShader);
                if(program==actorPrograms.end()){p->boundsWork.kind=WorkKind::Unsupported;return false;}
                const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
                if(!declarationCache.get(p->decl,elements,count)){p->boundsWork.kind=WorkKind::Unsupported;return false;}
                if(phase==0){++cheapVisited;status=replayBoundsCache.calculateCheap(*program->second,p->mesh(),p->shared,elements,count,p->constants,context.inverseView,cheapBudget,p->pointBounds,p->boundsWork);}
                else if(phase==1){++heavyVisited;status=replayBoundsCache.evaluateHeavy(*program->second,p->mesh(),p->shared,elements,count,p->constants,context.inverseView,heavyBudget,p->pointBounds,p->boundsWork);}
                else{++buildVisited;status=replayBoundsCache.buildEnclosed(*program->second,p->mesh(),p->shared,elements,count,p->constants,context.inverseView,buildBudget,p->pointBounds);}
            }
            if(status==NorthlightReplayBounds::Status::Valid)++pointBoundsValid;
            return status==NorthlightReplayBounds::Status::Budget;
        };
        replayBoundsCache.readyResume(NorthlightReplaySchedule::pass(replays.size(),replayBoundsCache.readyStart(replays.size()),
            [&](){return replayBoundsCache.canCheap()&&cheapBudget.operations<cheapBudget.maxOperations;},[&](size_t index){return visit(index,0);}));
        replayBoundsCache.heavyResume(NorthlightReplaySchedule::pass(replays.size(),replayBoundsCache.heavyStart(replays.size()),
            [&](){return replayBoundsCache.canHeavy()&&heavyBudget.operations<heavyBudget.maxOperations;},[&](size_t index){return visit(index,1);}));
        replayBoundsCache.continuePendingBuild(buildBudget);
        replayBoundsCache.buildResume(NorthlightReplaySchedule::pass(replays.size(),replayBoundsCache.buildStart(replays.size()),
            [&](){return replayBoundsCache.canBuild()&&buildBudget.vertices<buildBudget.maxVertices&&buildBudget.operations<buildBudget.maxOperations;},[&](size_t index){return visit(index,2);}));
        const size_t reservedCheapVisited=cheapVisited;
        replayBoundsCache.finishReservedTurnsAndLend();
        cheapBudget.maxOperations=262144-heavyBudget.operations-buildBudget.operations;
        // Preserve the first turn's cursor for the next frame. Advancing it
        // through this bonus turn could strand cold/heavy packets exclusively
        // after their reserved phases on every frame of a periodic draw list.
        NorthlightReplaySchedule::pass(replays.size(),replayBoundsCache.readyStart(replays.size()),
            [&](){return replayBoundsCache.canCheap()&&cheapBudget.operations<cheapBudget.maxOperations;},[&](size_t index){return visit(index,0);});
        replayBoundsCache.settleClock();
        pointBoundVertices=buildBudget.vertices;pointBoundOperations=cheapBudget.operations+heavyBudget.operations+buildBudget.operations;
        const double wallMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        if(diagnostics)pointLogReplayBounds({cheapVisited,heavyVisited,buildVisited,reservedCheapVisited,pointBoundVertices,pointBoundOperations,pointBoundsValid},wallMs,wallMs,0,0,0,false);
    }
    void pointLogReplayBounds(const NorthlightReplayBoundsJob::Totals& visits,double wallMs,double mainMs,double workerMs,double waitMs,double startLagMs,bool async){
        using WorkKind=NorthlightReplayBounds::WorkKind;
        const size_t cheapVisited=visits.cheapVisited,heavyVisited=visits.heavyVisited,buildVisited=visits.buildVisited,reservedCheapVisited=visits.reservedCheapVisited;
        if(NorthlightDiagnostics::enabled()){ /* callers pass only sampled frames */
            size_t unclassified=0,cheap=0,heavy=0,cold=0,unsupported=0;
            for(const auto& p:replays)switch(p->boundsWork.kind){
                case WorkKind::Unknown:++unclassified;break;case WorkKind::Cheap:++cheap;break;
                case WorkKind::Heavy:++heavy;break;case WorkKind::Cold:++cold;break;case WorkKind::Unsupported:++unsupported;break;
            }
            const auto& stats=replayBoundsCache.envelopeStats();
            logf("WORLD replay bounds total=%zu valid=%u envelopeValid=%u fallbackValid=%u deferred=%u buildVertices=%zu operations=%zu envelopeEntries=%zu envelopeBytes=%zu envelopeMs=%.3f readyMs=%.3f heavyMs=%.3f buildMs=%.3f classifyMs=%.3f wallMs=%.3f skinValid=%zu coldVisits=%zu buildIndices=%zu completed=%zu timeDeferred=%zu vertexDeferred=%zu operationDeferred=%zu unsupported=%zu tupleLimit=%zu invalid=%zu evictions=%zu readyEvictions=%zu expiredEvictions=%zu releasedGroupBytes=%zu buildSkippedReady=%zu cheapValid=%zu heavyAttempts=%zu heavyValid=%zu cheapSliceDeferred=%zu async=%u mainMs=%.3f workerMs=%.3f waitMs=%.3f startLagMs=%.3f fallbackCapped=%zu",
                replays.size(),pointBoundsValid,replayBoundsCache.envelopeValid(),replayBoundsCache.fallbackValid(),replayBoundsCache.deferred(),stats.buildVertices,pointBoundOperations,
                replayBoundsCache.envelopeEntries(),replayBoundsCache.envelopeBytes(),replayBoundsCache.envelopeMilliseconds(),
                replayBoundsCache.envelopeReadyMilliseconds(),replayBoundsCache.envelopeHeavyMilliseconds(),replayBoundsCache.envelopeBuildMilliseconds(),
                replayBoundsCache.envelopeClassifyMilliseconds(),wallMs,stats.skinValid,stats.cold,stats.buildIndices,stats.completed,
                stats.timeDeferred,stats.vertexDeferred,stats.operationDeferred,stats.unsupported,stats.tupleLimit,stats.invalid,stats.evictions,stats.readyEvictions,stats.expiredEvictions,stats.releasedGroupBytes,stats.buildSkippedReady,
                stats.cheapValid,stats.heavyAttempts,stats.heavyValid,stats.cheapSliceDeferred,unsigned(async),mainMs,workerMs,waitMs,startLagMs,async?replayBoundsCapped:size_t(0));
            logf("WORLD replay priority cheapVisited=%zu heavyVisited=%zu buildVisited=%zu cheap=%zu heavy=%zu cold=%zu unsupported=%zu unclassified=%zu",
                cheapVisited,heavyVisited,buildVisited,cheap,heavy,cold,unsupported,unclassified);
            logf("WORLD replay build pending=%u pendingVertices=%zu pendingIndices=%zu resumes=%zu admissionDeferred=%zu ownerExpired=%zu abandoned=%zu",
                unsigned(replayBoundsCache.hasPendingBuild()),replayBoundsCache.pendingBuildVertices(),replayBoundsCache.pendingBuildIndices(),
                stats.pendingResumes,stats.pendingDeferred,stats.pendingOwnerExpired,stats.pendingAbandoned);
            logf("WORLD replay memo tests=%zu hits=%zu misses=%zu stored=%zu evictions=%zu rejected=%zu entries=%zu bytes=%zu comparedBytes=%zu avoidedOperations=%zu budgetDeferred=%zu effectiveCopiedBytes=%zu",
                stats.memoTests,stats.memoHits,stats.memoMisses,stats.memoStored,stats.memoEvictions,stats.memoRejected,
                replayBoundsCache.memoEntries(),replayBoundsCache.memoBytes(),stats.memoComparedBytes,stats.memoAvoidedOperations,stats.memoBudgetDeferred,stats.effectiveCopiedBytes);
            const auto& memoPolicy=replayBoundsCache.memoPolicy();
            const auto memoTotals=replayBoundsCache.memoTotals();
            logf("WORLD replay memoPolicy enabled=%u cooldownFrames=%u pauses=%llu retries=%llu coldMisses=%zu replacedMisses=%zu cameraMisses=%zu constantMisses=%zu bypassed=%zu storeBypassed=%zu protected=%zu copiedBytes=%zu totalTests=%llu totalHits=%llu totalCold=%llu totalReplaced=%llu totalCamera=%llu totalConstants=%llu totalBypassed=%llu totalStoreBypassed=%llu totalProtected=%llu totalCopiedBytes=%llu",
                unsigned(memoPolicy.enabled()),memoPolicy.remaining(),(unsigned long long)memoPolicy.pauses(),(unsigned long long)memoPolicy.retries(),
                stats.memoColdMisses,stats.memoReplacedMisses,stats.memoCameraMisses,stats.memoConstantMisses,
                stats.memoBypassed,stats.memoStoreBypassed,stats.memoProtected,stats.memoCopiedBytes,
                (unsigned long long)memoTotals.tests,(unsigned long long)memoTotals.hits,(unsigned long long)memoTotals.cold,
                (unsigned long long)memoTotals.replaced,(unsigned long long)memoTotals.camera,(unsigned long long)memoTotals.constants,
                (unsigned long long)memoTotals.bypassed,(unsigned long long)memoTotals.storeBypassed,
                (unsigned long long)memoTotals.protectedStores,(unsigned long long)memoTotals.copiedBytes);
            const auto& metadata=replayBoundsMetadata.stats();
            logf("WORLD replay adaptive fastValid=%zu borrowedUs=%.3f extraCheapVisited=%zu preparedHits=%zu preparedMisses=%zu preparedNew=%zu preparedFallbacks=%zu preparedEvictions=%zu preparedEntries=%zu preparedBytes=%zu diagnosticsSampled=1",
                stats.fastValid,replayBoundsCache.borrowedMicroseconds(),cheapVisited-reservedCheapVisited,metadata.hits,metadata.misses,metadata.prepared,metadata.fallbacks,metadata.evictions,replayBoundsMetadata.entries(),replayBoundsMetadata.bytes());
            const auto& slow=replayBoundsCache.lastExpensiveEnvelope();
            if(slow.valid){
                const char* stage=slow.stage==NorthlightReplayBounds::EnvelopeCache::Stage::Cheap?"cheap":"heavy";
                const char* result=slow.status==NorthlightReplayBounds::Status::Valid?"valid":slow.status==NorthlightReplayBounds::Status::Budget?"budget":slow.status==NorthlightReplayBounds::Status::Invalid?"invalid":"unsupported";
                logf("WORLD replay expensive cacheFrame=%llu program=%016llx groups=%zu operations=%zu elapsedUs=%.3f stage=%s result=%s skin=%u",
                    (unsigned long long)slow.frame,(unsigned long long)slow.programHash,slow.groups,slow.operations,slow.elapsedUs,stage,result,unsigned(slow.skin));
            }
        }
    }
    // Capture-skip prediction: would renderPointShadow() refresh the cube this frame?
    // Same selection and schedule, no state change; unpredictable counts as due.
    bool pointRefreshPredicted()const{
        if(!effects.shadows||lastRenderDebug!=0||!quality.pointShadows||!active)return false;
        // Conservative: a new lamp or changing selection also needs current-frame casters.
        return NorthlightPointShadow::selectMany(active->localLights.data(),active->localLights.size(),context.camera,quality.pointShadowLimit,{},48,true).count!=0;
    }
    // fresh=false: model capture was skipped. Reuse a compatible complete cube
    // or defer; publishing a static-only cube would erase character shadows.
    // withReplays=false (0.3.158 ActorShadows=0): static cache, terrain and union only; the
    // cube is complete without replays (fresh=true) and the schedule sees none.
    bool renderPointShadow(const NorthlightLocalLights::Light& light,bool fresh=true,bool withReplays=true){
        pointSlot().ready=false;pointDraws=pointCulled=0;
        // Publication can lead the bounded upload. Its committed same-map
        // pages, materials and meshGeneration remain drawable until the swap.
        if(!active||!vertices||!indices||uploadedMap!=active->map)return false;
        if(pointSlot().map!=active->map){pointSlot().schedule.invalidate();pointSlot().cacheValid=false;}
        pointSlot().selected=light;pointSlot().map=active->map;
        // LocalLighting is zero outside this same conservative sphere scissor.
        // Keep the static cache, but require fresh dynamic faces on re-entry.
        if(NorthlightLocalLightScissor::Enabled&&localScissorView.valid){
            const std::array<NorthlightLocalLightSelection::Constant,1> sphere={{{light.position[0],light.position[1],light.position[2],light.attenuationEnd}}};
            if(!NorthlightLocalLightScissor::batchRect(localScissorView,sphere,1).area()){
                pointSlot().schedule.invalidate();pointSlot().faceCycle.reset();return false;
            }
        }
        if(!pointResources())return false;
        const uint64_t chunkHash=liveChunkHash();
        // Terrain is never cached (its live set follows the view), so the cache key ignores it.
        bool lightChanged=pointSlot().schedule.light.sourceId!=pointSlot().selected.sourceId||pointSlot().schedule.light.attenuationEnd!=pointSlot().selected.attenuationEnd||pointSlot().schedule.light.attenuationStart!=pointSlot().selected.attenuationStart||pointSlot().schedule.light.flags!=pointSlot().selected.flags;
        for(unsigned i=0;i<3;++i)lightChanged=lightChanged||pointSlot().schedule.light.position[i]!=pointSlot().selected.position[i];
        const unsigned rebuildMask=pointStaticRebuild(pointSlot().selected,lightChanged);const bool rebuild=rebuildMask!=0;
        const DWORD updateAt=GetTickCount();if(fresh)pointReplayCount=withReplays?replays.size():0;
        if(!pointSlot().schedule.due(updateAt,pointSlot().selected,meshGeneration,pointReplayCount,rebuild,quality.pointShadowRefreshMs)){
            pointSlot().ready=true;++pointReuses;pointLogSchedule(true);return true;
        }
        if(!fresh){
            captureDemand=true;++captureDeferrals;
            if(!pointSlot().schedule.usable||lightChanged)return false;
            pointSlot().ready=true;++pointReuses;pointLogSchedule(true);return true;
        }
        // PointShadowFacesPerFrame<6: a refresh of the same light whose six faces are all complete
        // draws only this frame's group; a new, moved or resized light draws all six at once.
        const bool cycling=quality.pointShadowFacesPerFrame<6&&pointSlot().schedule.usable&&!lightChanged&&pointSlot().cacheValid&&pointSlot().cacheSource==pointSlot().selected.sourceId;
        unsigned faces=63;
        if(cycling){if(!pointSlot().faceCycle.active())pointSlot().faceCycle.start(quality.pointShadowFacesPerFrame,pointSlot().faceWeights,updateAt,meshGeneration);
            faces=pointSlot().faceCycle.mask();pointSlot().schedule.stale();} /* a face failure below invalidates the whole cube */
        else{
            // From here a failure may leave a partially overwritten cube. It cannot
            // be reused until every face has been completed successfully.
            pointSlot().faceCycle.reset();pointSlot().schedule.invalidate();}
        float faceMatrices[6][16];
        for(unsigned face=0;face<6;++face)
            if(!NorthlightPointShadow::faceMatrix(pointSlot().selected.position,.1f,pointSlot().selected.attenuationEnd,face,faceMatrices[face],PointResolution))return pointCheck(E_FAIL,"cube projection");
        const NorthlightPointShadow::FaceClip clip(faceMatrices);
        // Only one complete, uniquely matched enclosure can be omitted, and
        // only from this source's point-shadow cube. Shared geometry is intact.
        const size_t ownGlass=NorthlightWmoLampShadow::unique(light,batches);
        try {
            for(auto* lists:{&pointStaticCandidates,&pointTerrainCandidates,&pointLiveCandidates,&pointReplayCandidates})for(auto& list:*lists)list.clear();
            for(size_t i=0;i<batches.size();++i){const auto& batch=batches[i];
                if(i==ownGlass)continue;
                // A visible native chunk can contain only a camera-culled subset.
                // Keep its cached terrain in the light's shadow volume; the
                // depth union with native geometry is order-independent.
                if(batch.terrain){
                    pointAppendCandidates(pointTerrainCandidates,i,pointMask(batch.boundsLow,batch.boundsHigh,clip),faces);}
                else if(rebuildMask&faces)pointAppendCandidates(pointStaticCandidates,i,pointMask(batch.boundsLow,batch.boundsHigh,clip),rebuildMask&faces);
            }
            for(size_t i=0;i<pointLiveBatches.size();++i){const auto& batch=pointLiveBatches[i];
                pointAppendCandidates(pointLiveCandidates,i,pointMask(batch.low,batch.high,clip),faces);}
            replayBoundsJoin();
            if(withReplays)for(size_t i=0;i<replays.size();++i){const auto& bounds=replays[i]->pointBounds;
                const unsigned mask=NorthlightReplayBounds::outsideSphere(bounds,pointSlot().selected.position,pointSlot().selected.attenuationEnd)?0:
                    clip.mask(bounds.low,bounds.high,bounds.valid);
                pointAppendCandidates(pointReplayCandidates,i,mask,faces);
            }
        }catch(...){return pointCheck(E_OUTOFMEMORY,"cube candidate allocation");}
        // No sampler may still hold the cube while one face is its render target.
        for(unsigned i=0;i<13;++i)if(!pointCheck(d->SetTexture(i,nullptr),"unbind cube samplers"))return false;
        for(unsigned i=1;i<4;++i)if(!pointCheck(d->SetRenderTarget(i,nullptr),"unbind cube MRT"))return false;
        for(unsigned i=0;i<4;++i)if(!pointCheck(d->SetStreamSourceFreq(i,1),"cube stream frequency"))return false;
        if(!pointCheck(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE),"cube blend")||
           !pointCheck(d->SetRenderState(D3DRS_COLORWRITEENABLE,15),"cube color mask"))return false;
        for(unsigned face=0;face<6;++face){
            if(!(faces&(1u<<face)))continue; /* a later frame of this face cycle */
            const float* matrix=faceMatrices[face];
            D3DVIEWPORT9 viewport={0,0,PointResolution,PointResolution,0,1};
            const float sphere[4]={pointSlot().selected.attenuationEnd*pointSlot().selected.attenuationEnd,1.f/PointResolution,0,0}; /* LocalShadowPS: casters within range only */
            auto bindFace=[&](IDirect3DSurface9* target,const char* what){
                return pointCheck(d->SetDepthStencilSurface(nullptr),"unbind cube depth")&&pointCheck(d->SetRenderTarget(0,target),what)&&
                    pointCheck(d->SetDepthStencilSurface(pointSlot().depth),"cube depth target")&&pointCheck(d->SetViewport(&viewport),"cube viewport")&&
                    pointCheck(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0),"clear cube")&&
                    pointCheck(d->SetVertexDeclaration(shadowDecl),"cube declaration")&&pointCheck(d->SetVertexShader(pointSlot().shadowVS),"cube static VS")&&
                    pointCheck(d->SetVertexShaderConstantF(0,matrix,4),"cube face matrix")&&pointCheck(d->SetPixelShader(pointSlot().shadowPS),"cube static PS")&&pointCheck(d->SetPixelShaderConstantF(1,sphere,1),"cube sphere")&&
                    pointCheck(d->SetStreamSource(0,vertices,0,sizeof(NorthlightGI::WorldVertex)),"cube static vertices")&&pointCheck(d->SetIndices(indices),"cube static indices")&&
                    pointCheck(d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP),"cube alpha U")&&pointCheck(d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP),"cube alpha V");
            };
            if(rebuildMask&(1u<<face)){if(!bindFace(pointSlot().cacheSurface[face],"cube cache target"))return false;
            UINT boundPage=UINT_MAX;
            for(size_t index:pointStaticCandidates[face]){const auto& batch=batches[index];
                if(batch.material>=materials.size()||batch.material>=uploadedAlphaCutoffs.size())return pointCheck(E_FAIL,"cube material range");
                NorthlightPointTransmissionFilter filter;
                if(!pointCheck(filter.begin(d,batch.pointOnly),"cube transmission filter"))return false;
                // The static VS has no COLOR output; coverage comes from the texture alone.
                float material[]={0,0,0,uploadedAlphaCutoffs[batch.material]};
                if(!pointCheck(d->SetTexture(0,materials[batch.material]),"cube static alpha")||
                   !pointCheck(d->SetPixelShaderConstantF(0,material,1),"cube material")||
                   !bindMeshPage(batch,boundPage)||!pointCheck(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,batch.minVertex,batch.vertexCount,batch.start,batch.count),"cube static draw"))return false;
                if(!pointCheck(filter.restore(),"restore cube alpha filter"))return false;
                ++pointDraws;
            }
            const auto* digests=pointFaceDigests(pointSlot().selected);
            pointSlot().faceContent[face]=digests?digests[face]:NorthlightLocalShadowSignature::Digest{};pointSlot().faceKnown[face]=digests!=nullptr;pointSlot().faceSerial[face]=meshGeneration;}
            else if(pointSlot().faceKnown[face]&&pointSlot().faceSerial[face]!=meshGeneration){pointSlot().faceSerial[face]=meshGeneration;++pointFacesKept;} /* same content: the old rule redrew it */
            if(rebuildMask&(1u<<face))++pointFacesRebuilt;
            const unsigned faceDraws=pointDraws;
            // Dynamic casters of this frame into the scratch face, plus cached-mesh
            // terrain, including parts absent from the camera-culled native draws.
            if(!bindFace(pointSlot().scratchSurface,"cube scratch target"))return false;
            UINT terrainBoundPage=UINT_MAX;
            for(size_t index:pointTerrainCandidates[face]){const auto& batch=batches[index];
                if(batch.material>=materials.size()||batch.material>=uploadedAlphaCutoffs.size())return pointCheck(E_FAIL,"cube material range");
                float material[]={0,0,0,uploadedAlphaCutoffs[batch.material]};
                if(!pointCheck(d->SetTexture(0,materials[batch.material]),"cube terrain alpha")||
                   !pointCheck(d->SetPixelShaderConstantF(0,material,1),"cube terrain material")||
                   !bindMeshPage(batch,terrainBoundPage)||!pointCheck(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,batch.minVertex,batch.vertexCount,batch.start,batch.count),"cube terrain draw"))return false;
                ++pointDraws;
            }
            if(liveTerrainIndexCount){
                float opaque[]={1,1,1,-1};
                if(!pointCheck(d->SetPixelShaderConstantF(0,opaque,1),"cube live material")||
                   !pointCheck(d->SetTexture(0,nullptr),"cube live alpha")||
                   !pointCheck(d->SetVertexDeclaration(liveTerrainDecl),"cube live declaration")||
                   !pointCheck(d->SetStreamSource(0,liveTerrainGPU.vertices(),0,sizeof(StaticShadow::Vertex)),"cube live vertices")||
                   !pointCheck(d->SetIndices(liveIndicesGPU),"cube live indices"))return false;
                NorthlightDynamicRing::touch(liveIndexRing,fence()); /* 0.3.192 (DXVK3): drawn this frame: the slice stays pending until this frame's fence */
                for(size_t index:pointLiveCandidates[face]){const auto& batch=pointLiveBatches[index];
                    if(!pointCheck(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,liveTerrainGPU.vertexCapacity(),UINT(liveIndexBase+batch.start),batch.count),"cube live draw"))return false;++pointDraws;
                }
                if(!pointCheck(d->SetVertexDeclaration(shadowDecl),"cube restore terrain declaration"))return false;
            }
            if(withReplays){
            float rows[16];NorthlightWorldMath::replayProjection(context.inverseView,matrix,rows);
            NorthlightReplayPoses::Pass<BOOL> poseConstants;
            NorthlightReplayDrawState::Cache replayBindings(d);
            // LocalShadowPS reads patched TEXCOORD7 and writes perspective z/w;
            // the directional replay PS assumes orthographic w=1 and is invalid here.
            for(size_t index:pointReplayCandidates[face]){const auto& p=replays[index];
                if(!pointCheck(replayBindings.geometry(*p),"cube replay geometry state"))return false;
                if(!poseConstants.prepare(*p,rows,matrix))return false;
                const float* desired=poseConstants.desired();
                auto f=poseConstants.floats;auto b=poseConstants.booleans;auto i=poseConstants.integers;
                if(f.count&&!pointCheck(d->SetVertexShaderConstantF(f.first,desired+4*f.first,f.count),"cube replay float constants"))return false;
                if(b.count&&!pointCheck(d->SetVertexShaderConstantB(b.first,p->bools+b.first,b.count),"cube replay bool constants"))return false;
                if(i.count&&!pointCheck(d->SetVertexShaderConstantI(i.first,p->ints+4*i.first,i.count),"cube replay integer constants"))return false;
                if(!pointCheck(replayBindings.material(*p),"cube replay material state"))return false;
                HRESULT hr=p->indexed?d->DrawIndexedPrimitive(p->type,p->base,p->min,p->vertices,p->start,p->count):d->DrawPrimitive(p->type,p->start,p->count);
                if(!pointCheck(hr,"cube animated draw"))return false;++pointDraws;
            }
            } /* withReplays */
            pointSlot().faceWeights[face]=pointDraws-faceDraws; /* the next face cycle's balance */
            // Union: min(scratch, cached static face) into the cube face.
            if(!pointCheck(d->SetDepthStencilSurface(nullptr),"unbind cube depth")||!pointCheck(d->SetRenderTarget(0,pointSlot().faces[face]),"cube target"))return false;
            d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
            d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetStreamSourceFreq(0,1);
            d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
            if(!pointCheck(d->SetTexture(0,pointSlot().scratch),"cube union scratch")||!pointCheck(d->SetTexture(1,pointSlot().cacheFace[face]),"cube union cache"))return false;
            for(unsigned sampler=0;sampler<2;++sampler){d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(sampler,D3DSAMP_SRGBTEXTURE,FALSE);}
            float unionConstants[2][4]={{0,0,0,float(PointResolution)},{1.f/PointResolution,1.f/PointResolution,0,0}};
            if(!pointCheck(d->SetPixelShaderConstantF(0,&unionConstants[0][0],2),"cube union constants")||!pointCheck(d->SetPixelShader(unionPS),"cube union shader")||
               !pointCheck(quad(PointResolution,PointResolution),"cube union"))return false;
            d->SetTexture(0,nullptr);d->SetTexture(1,nullptr);
            d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
            d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        }
        if(rebuild){pointSlot().cacheValid=true;pointSlot().cacheSource=pointSlot().selected.sourceId;pointSlot().cacheChunks=chunkHash;} /* cycling: already valid, its remaining faces wait for their frame */
        if(cycling&&!pointSlot().faceCycle.advance()){pointSlot().ready=true;++pointUpdates;pointLogSchedule(false);return true;} /* stale until the last group */
        if(cycling)pointSlot().schedule.commit(pointSlot().faceCycle.startedAt,pointSlot().selected,pointSlot().faceCycle.generation);
        else pointSlot().schedule.commit(updateAt,pointSlot().selected,meshGeneration);
        pointSlot().ready=true;++pointUpdates;pointLogSchedule(false);
        if(frames==0||(frames%600==0&&NorthlightDiagnostics::enabled()))logf("POINT source=%llu kind=%u range=%.2f faces=%u size=256 draws=%u culled=%u replayBounds=%u boundVertices=%zu boundOperations=%zu",(unsigned long long)pointSlot().selected.sourceId,pointSlot().selected.kind,pointSlot().selected.attenuationEnd,unsigned(__builtin_popcount(faces)),pointDraws,pointCulled,pointBoundsValid,pointBoundVertices,pointBoundOperations);
        return true;
    }
    void renderPointShadows(const NorthlightLocalLightSelection::Selection& lights,bool fresh,bool withReplays){
        pointReady=false;std::array<uint64_t,NorthlightPointShadow::MaxLights> previous{};
        for(unsigned i=0;i<pointSlots.size();++i){auto& p=pointSlots[i];p.ready=false;if(active&&p.map==active->map)previous[i]=p.selected.sourceId;}
        const auto picks=NorthlightPointShadow::selectMany(lights.sources.data(),lights.count,context.camera,quality.pointShadowLimit,previous);
        bool used[NorthlightPointShadow::MaxLights]={};unsigned slots[NorthlightPointShadow::MaxLights]={};
        // Reserve every surviving source before reassigning free slots; rank changes keep their caches.
        for(unsigned i=0;i<picks.count;++i){slots[i]=NorthlightPointShadow::MaxLights;
            for(unsigned j=0;j<pointSlots.size();++j)if(!used[j]&&previous[j]==lights.sources[picks.index[i]].sourceId){slots[i]=j;used[j]=true;break;}}
        for(unsigned i=0;i<picks.count;++i){if(slots[i]==NorthlightPointShadow::MaxLights){
                unsigned j=0;while(used[j])++j;slots[i]=j;used[j]=true;}
            pointIndex=slots[i];renderPointShadow(lights.sources[picks.index[i]],fresh,withReplays);pointReady|=pointSlot().ready;}
        for(pointIndex=0;pointIndex<pointSlots.size();++pointIndex)if(!used[pointIndex])releasePointSlot();pointIndex=0;
        if(pointReportedLimit!=quality.pointShadowLimit||(frames%600==0&&NorthlightDiagnostics::enabled())){
            unsigned ready=0,resident=0;for(const auto& p:pointSlots){ready+=p.ready;resident+=p.cube!=nullptr;}
            logf("POINT pool limit=%u selected=%u ready=%u cubeSlots=%u estimatedMiB=%.1f",quality.pointShadowLimit,picks.count,ready,resident,resident*3.5);
            pointReportedLimit=quality.pointShadowLimit;
        }
    }
    bool renderPointLighting(IDirect3DTexture9* depth,IDirect3DTexture9* waterMask,UINT w,UINT h,float nearZ,float farZ,float minZ,float maxZ,float strength,float sunlitCut,float shadowStrength,float sunEnergy){
        if(!pointSlot().ready)return false;
        float constants[12][4]={};constants[0][0]=1.f/w;constants[0][1]=1.f/h;constants[0][2]=nearZ;constants[0][3]=farZ;
        constants[1][0]=projection[0];constants[1][1]=projection[1];constants[1][2]=projection[2];constants[1][3]=minZ;
        constants[2][0]=1.f/(maxZ-minZ);constants[2][1]=waterMask?1.f:0.f;constants[2][2]=1;constants[2][3]=.025f;
        std::memcpy(constants[3],context.inverseView,64);std::memcpy(constants[7],pointSlot().selected.position,12);constants[7][3]=1.f/std::max(pointSlot().selected.attenuationEnd-std::min(pointSlot().selected.attenuationStart,pointSlot().selected.attenuationEnd*.9f),.05f);
        std::memcpy(constants[8],pointSlot().selected.diffuse,12);constants[8][3]=pointSlot().selected.attenuationEnd;
        constants[9][0]=.1f;constants[9][1]=pointSlot().selected.attenuationEnd;constants[9][2]=1.f/PointResolution;constants[9][3]=1;
        constants[10][0]=strength;constants[10][1]=sunlitCut;constants[10][2]=shadowStrength;constants[10][3]=sunEnergy;
        constants[11][0]=sourceDirections[0].x;constants[11][1]=sourceDirections[0].y;constants[11][2]=sourceDirections[0].z;
        const float bumpControl[]={materialDetail?materialBumpGain:0.f,materialBumpDistance,0,0};
        d->SetPixelShaderConstantF(12,bumpControl,1);d->SetTexture(5,materialDetail);
        for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(5,state,D3DTEXF_POINT);
        d->SetSamplerState(5,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(5,D3DSAMP_SRGBTEXTURE,FALSE);
        d->SetSamplerState(5,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(5,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        if(!pointCheck(d->SetPixelShaderConstantF(0,&constants[0][0],12),"local constants")||
           !pointCheck(d->SetPixelShader(pointSlot().lightingPS),"local lighting PS")||
           !pointCheck(d->SetTexture(0,depth),"local depth")||!pointCheck(d->SetTexture(1,pointSlot().cube),"local cube")||
           !pointCheck(d->SetTexture(2,waterMask),"local water")||
           !pointCheck(d->SetTexture(3,normalBuffer),"local normals")||!pointCheck(d->SetTexture(4,baselineLight),"local sun visibility")||
           !pointCheck(d->SetRenderTarget(1,nullptr),"local baseline unbind")||
           !pointCheck(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE),"local additive blend")||
           !pointCheck(d->SetRenderState(D3DRS_COLORWRITEENABLE,7),"local RGB mask"))return false;
        for(unsigned sampler=0;sampler<5;++sampler){
            const struct{D3DSAMPLERSTATETYPE state;DWORD value;} states[]={{D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSW,D3DTADDRESS_CLAMP},{D3DSAMP_MINFILTER,D3DTEXF_POINT},{D3DSAMP_MAGFILTER,D3DTEXF_POINT},{D3DSAMP_MIPFILTER,D3DTEXF_NONE},{D3DSAMP_SRGBTEXTURE,FALSE}};
            for(const auto& state:states)if(!pointCheck(d->SetSamplerState(sampler,state.state,state.value),"local sampler"))return false;
        }
        // The correction is exactly zero beyond attenuationEnd (LocalColor.w).
        const std::array<NorthlightLocalLightSelection::Constant,1> sphere={{{pointSlot().selected.position[0],pointSlot().selected.position[1],pointSlot().selected.position[2],pointSlot().selected.attenuationEnd}}};
        // Enable first, as in the batch loops: localScissor may fail open by disabling it.
        d->SetRenderState(D3DRS_SCISSORTESTENABLE,NorthlightLocalLightScissor::Enabled);
        if(!localScissor(sphere,1)){d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);return true;}
        const bool drawn=pointCheck(quad(w/2,h/2),"local lighting pass");
        d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);return drawn;
    }
