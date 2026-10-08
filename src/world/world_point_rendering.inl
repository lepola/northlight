// Included inside WorldRenderer. Optional point pass owns no game state and
// never calls check(): its resource/draw failures must not disable world effects.
    static constexpr UINT PointResolution=256;
    IDirect3DCubeTexture9* pointCube=nullptr;
    IDirect3DSurface9* pointFaces[6]={},*pointDepth=nullptr;
    // Static casters per face are cached (light and geometry are static); per
    // frame only live terrain and in-range replays are drawn and united.
    IDirect3DTexture9 *pointCacheFace[6]={},*pointScratch=nullptr;
    IDirect3DSurface9 *pointCacheSurface[6]={},*pointScratchSurface=nullptr;
    bool pointCacheValid=false;uint64_t pointCacheSource=0,pointCacheChunks=0;
    // 0.3.151: each static face is keyed on its content (NorthlightPointShadow::faceContent over the
    // mesh plan's records), so a mesh commit away from the lamp redraws nothing. Unknown records
    // (null: the bounded fallback) keep the old rule: any mesh generation change redraws.
    NorthlightLocalShadowSignature::Digest pointFaceContent[6];bool pointFaceKnown[6]={};uint64_t pointFaceSerial[6]={};
    struct PointContentMemo {const NorthlightLocalShadowSignature::Records* records=nullptr;uint64_t generation=0;float position[3]={},range=0;bool valid=false;NorthlightLocalShadowSignature::Digest faces[6];};
    mutable PointContentMemo pointContentMemo;
    // 0.3.151 PointShadowFacesPerFrame<6: the faces of one refresh over several fresh frames.
    NorthlightPointShadow::FaceCycle pointFaceCycle;unsigned pointFaceWeights[6]={};
    unsigned pointFacesRebuilt=0,pointFacesKept=0; /* RenderProfile: static faces redrawn / kept across a mesh commit by content */
    IDirect3DVertexShader9* pointShadowVS=nullptr;
    IDirect3DPixelShader9* pointShadowPS=nullptr,*pointLightingPS=nullptr;
    NorthlightLocalLights::Light pointSelected{};
    std::string pointMap;
    bool pointReady=false;
    DWORD pointRetryAt=0;
    unsigned pointFailures=0,pointDraws=0,pointCulled=0,pointBoundsValid=0;
    NorthlightPointShadow::RefreshSchedule pointSchedule;
    unsigned pointUpdates=0,pointReuses=0;
    using PointCandidates=std::array<std::vector<size_t>,6>;
    PointCandidates pointStaticCandidates,pointTerrainCandidates,pointLiveCandidates,pointReplayCandidates;
    size_t pointBoundVertices=0,pointBoundOperations=0;
    struct PointLiveBatch {UINT start,count;V low,high;};
    std::vector<PointLiveBatch> pointLiveBatches;

    void releasePointGPU(){
        pointSchedule.invalidate();pointUpdates=pointReuses=0;
        localLightTracker.reset();localLightQpc=0; /* 0.3.197: releaseGPU's only caller; a rebuilt device restarts the local light fades */
        for(auto* lists:{&pointStaticCandidates,&pointTerrainCandidates,&pointLiveCandidates,&pointReplayCandidates})
            for(auto& list:*lists)std::vector<size_t>().swap(list);
        pointReady=false;for(auto& face:pointFaces)drop(face);drop(pointCube);drop(pointDepth);
        for(auto& t:pointCacheFace)drop(t);for(auto& sf:pointCacheSurface)drop(sf);drop(pointScratch);drop(pointScratchSurface);pointCacheValid=false;
        drop(pointShadowVS);drop(pointShadowPS);drop(pointLightingPS);pointRetryAt=0;pointSelected={};pointMap.clear();
    }
    bool pointCheck(HRESULT hr,const char* operation){
        if(SUCCEEDED(hr))return true;
        pointSchedule.invalidate();pointCacheValid=false;
        pointReady=false;pointRetryAt=GetTickCount()+1000;
        if(pointFailures++<12)logf("POINT pass skipped: %s hr=%08lx (world effects retained)",operation,(unsigned long)hr);
        return false;
    }
    bool pointResources(){
        if(pointRetryAt&&LONG(GetTickCount()-pointRetryAt)<0)return false;
        if(pointCube&&pointDepth&&pointShadowVS&&pointShadowPS&&pointLightingPS&&pointScratch&&pointScratchSurface){
            bool faces=true;for(auto* face:pointFaces)faces=faces&&face;for(auto* face:pointCacheSurface)faces=faces&&face;if(faces){pointRetryAt=0;return true;}
        }
        // Release partial allocations before a retry; selection hysteresis is
        // retained across ordinary allocation retries.
        for(auto& face:pointFaces)drop(face);drop(pointCube);drop(pointDepth);
        for(auto& t:pointCacheFace)drop(t);for(auto& sf:pointCacheSurface)drop(sf);drop(pointScratch);drop(pointScratchSurface);pointCacheValid=false;
        drop(pointShadowVS);drop(pointShadowPS);drop(pointLightingPS);
        for(unsigned i=0;i<6;++i){if(!pointCheck(d->CreateTexture(PointResolution,PointResolution,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&pointCacheFace[i],nullptr),"cube cache face")||!pointCacheFace[i]||
            !pointCheck(pointCacheFace[i]->GetSurfaceLevel(0,&pointCacheSurface[i]),"cube cache surface")||!pointCacheSurface[i])return pointCheck(E_FAIL,"cube cache face");}
        if(!pointCheck(d->CreateTexture(PointResolution,PointResolution,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&pointScratch,nullptr),"cube scratch")||!pointScratch||
           !pointCheck(pointScratch->GetSurfaceLevel(0,&pointScratchSurface),"cube scratch surface")||!pointScratchSurface)return pointCheck(E_FAIL,"cube scratch");
        if(!pointCheck(d->CreateCubeTexture(PointResolution,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&pointCube,nullptr),"create R32F cube"))return false;
        if(!pointCube)return pointCheck(E_FAIL,"empty cube");
        for(unsigned i=0;i<6;++i){if(!pointCheck(pointCube->GetCubeMapSurface(D3DCUBEMAP_FACES(i),0,&pointFaces[i]),"cube surface"))return false;
            if(!pointFaces[i])return pointCheck(E_FAIL,"empty cube face");}
        if(!pointCheck(d->CreateDepthStencilSurface(PointResolution,PointResolution,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&pointDepth,nullptr),"cube depth"))return false;
        if(!pointCheck(d->CreateVertexShader(kLocalShadowVSShader,&pointShadowVS),"cube vertex shader")||
           !pointCheck(d->CreatePixelShader(kLocalShadowPSShader,&pointShadowPS),"cube pixel shader")||
           !pointCheck(d->CreatePixelShader(kLocalLightingShader,&pointLightingPS),"local lighting shader"))return false;
        if(!pointDepth||!pointShadowVS||!pointShadowPS||!pointLightingPS)return pointCheck(E_FAIL,"empty optional resource");
        pointRetryAt=0;return true;
    }
    void pointRecordTerrain(UINT start,UINT count,const NorthlightTerrainCapture::Bounds& bounds){
        // Reuse the snapshot's already validated, referenced-triangle bounds;
        // do not rescan hundreds of thousands of terrain vertices each frame.
        if(count)pointLiveBatches.push_back({start,count,vec(bounds.low),vec(bounds.high)});
    }
    unsigned pointMask(V low,V high,const float (&matrices)[6][16])const{
        float lo[]={low.x,low.y,low.z},hi[]={high.x,high.y,high.z};
        // Invalid bounds must draw. The generic sphere helper rejects invalid
        // inputs, so use the replay helper's explicit fail-open semantics.
        NorthlightReplayBounds::Bounds bounds;std::memcpy(bounds.low,lo,12);std::memcpy(bounds.high,hi,12);bounds.valid=true;
        if(NorthlightReplayBounds::outsideSphere(bounds,pointSelected.position,pointSelected.attenuationEnd))return 0;
        return NorthlightPointShadow::faceMask(lo,hi,true,matrices);
    }
    void pointAppendCandidates(PointCandidates& lists,size_t index,unsigned mask,unsigned faces=63){
        for(unsigned face=0;face<6;++face){if(!(faces&(1u<<face)))continue;if(mask&(1u<<face))lists[face].push_back(index);else ++pointCulled;}
    }
    // Content digests of the six faces for `light`; null when the records are unknown.
    // Recomputed only when the records, the mesh generation or the light's cube change.
    const NorthlightLocalShadowSignature::Digest* pointFaceDigests(const NorthlightLocalLights::Light& light)const{
        const auto* records=uploadedLocalShadowRecords.get();if(!records)return nullptr;
        auto& memo=pointContentMemo;
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
        if(!pointCacheValid||lightChanged||pointCacheSource!=light.sourceId)return 63;
        return NorthlightPointShadow::staleFaces(pointFaceContent,pointFaceKnown,pointFaceSerial,pointFaceDigests(light),meshGeneration);
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
        const bool diagnostics=NorthlightDiagnostics::enabled()&&(frames==0||frames%120==0); /* logs and envelope timing fields only */
        pointBoundsValid=0;replayBoundsCache.beginFrame(diagnostics);replayBoundsMetadata.beginFrame();
        // Program/declaration lookups stay on the render thread.
        replayBoundsFallbacks.clear();replayBoundsCapped=0;
        NorthlightReplayBoundsJob::gather(items,replays,[&](IDirect3DVertexShader9* shader,IDirect3DVertexDeclaration9* decl){return replayBoundsPrepared(shader,decl);});
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
        const bool diagnostics=NorthlightDiagnostics::enabled()&&(frames==0||frames%120==0); /* logs and envelope timing fields only */
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
        if(!effects.shadows||lastRenderDebug!=0||!(sourceWeights[0]<.5f)||!quality.pointShadows||!active)return false;
        if(!vertices||!indices||uploadedMap!=active->map||uploaded.lock()!=active->bvh)return true;
        const int selected=NorthlightPointShadow::select(active->localLights,context.camera,pointMap==active->map?pointSelected.sourceId:0,48);
        if(selected<0)return false;
        const auto& light=active->localLights[size_t(selected)];
        bool changed=pointMap!=active->map||pointSchedule.light.sourceId!=light.sourceId||pointSchedule.light.attenuationEnd!=light.attenuationEnd;
        for(unsigned i=0;i<3;++i)changed=changed||pointSchedule.light.position[i]!=light.position[i];
        // An incomplete face cycle leaves the schedule stale (due): the next frame captures.
        const bool rebuild=pointStaticRebuild(light,changed)!=0;
        return pointSchedule.due(GetTickCount(),light,meshGeneration,pointReplayCount,rebuild,quality.pointShadowRefreshMs);
    }
    // fresh=false: this frame's model capture was skipped (no replays). A due refresh
    // then keeps a complete cube of the same light one more frame, or draws a cube
    // without replays that is never committed; either way the next frame captures.
    // withReplays=false (0.3.158 ActorShadows=0): static cache, terrain and union only; the
    // cube is complete without replays (fresh=true) and the schedule sees none.
    bool renderPointShadow(bool fresh=true,bool withReplays=true){
        pointReady=false;pointDraws=pointCulled=0;
        if(!active||!vertices||!indices||uploadedMap!=active->map||uploaded.lock()!=active->bvh)return false;
        const uint64_t previous=pointMap==active->map?pointSelected.sourceId:0;
        int selected=NorthlightPointShadow::select(active->localLights,context.camera,previous,48);
        if(selected<0){pointSchedule.invalidate();return false;}
        if(pointMap!=active->map){pointSchedule.invalidate();pointCacheValid=false;}
        pointSelected=active->localLights[size_t(selected)];pointMap=active->map;
        if(!pointResources())return false;
        const uint64_t chunkHash=liveChunkHash();
        // Terrain is never cached (its live set follows the view), so the cache key ignores it.
        bool lightChanged=pointSchedule.light.sourceId!=pointSelected.sourceId||pointSchedule.light.attenuationEnd!=pointSelected.attenuationEnd;
        for(unsigned i=0;i<3;++i)lightChanged=lightChanged||pointSchedule.light.position[i]!=pointSelected.position[i];
        const unsigned rebuildMask=pointStaticRebuild(pointSelected,lightChanged);const bool rebuild=rebuildMask!=0;
        const DWORD updateAt=GetTickCount();if(fresh)pointReplayCount=withReplays?replays.size():0;
        if(!pointSchedule.due(updateAt,pointSelected,meshGeneration,pointReplayCount,rebuild,quality.pointShadowRefreshMs)){
            pointReady=true;++pointReuses;pointLogSchedule(true);return true;
        }
        // PointShadowFacesPerFrame<6: a refresh of the same light whose six faces are all complete
        // draws only this frame's group; a new, moved or resized light draws all six at once.
        const bool cycling=quality.pointShadowFacesPerFrame<6&&pointSchedule.usable&&!lightChanged&&pointCacheValid&&pointCacheSource==pointSelected.sourceId;
        if(!fresh&&(pointSchedule.complete||cycling)&&!lightChanged&&pointCacheValid&&pointCacheSource==pointSelected.sourceId){
            pointReady=true;++pointReuses;captureDemand=true;++captureDeferrals;pointLogSchedule(true);return true;} /* a cycle never advances without replays */
        unsigned faces=63;
        if(cycling){if(!pointFaceCycle.active())pointFaceCycle.start(quality.pointShadowFacesPerFrame,pointFaceWeights,updateAt,meshGeneration);
            faces=pointFaceCycle.mask();pointSchedule.stale();} /* a face failure below invalidates the whole cube */
        else{
            // From here a failure may leave a partially overwritten cube. It cannot
            // be reused until every face has been completed successfully.
            pointFaceCycle.reset();pointSchedule.invalidate();}
        float faceMatrices[6][16];
        for(unsigned face=0;face<6;++face)
            if(!NorthlightPointShadow::faceMatrix(pointSelected.position,.1f,pointSelected.attenuationEnd,face,faceMatrices[face],PointResolution))return pointCheck(E_FAIL,"cube projection");
        try {
            for(auto* lists:{&pointStaticCandidates,&pointTerrainCandidates,&pointLiveCandidates,&pointReplayCandidates})for(auto& list:*lists)list.clear();
            for(size_t i=0;i<batches.size();++i){const auto& batch=batches[i];
                if(batch.terrain){if(!liveTerrainChunks.count({batch.chunkX,batch.chunkY}))
                    pointAppendCandidates(pointTerrainCandidates,i,pointMask(batch.boundsLow,batch.boundsHigh,faceMatrices),faces);}
                else if(rebuildMask&faces)pointAppendCandidates(pointStaticCandidates,i,pointMask(batch.boundsLow,batch.boundsHigh,faceMatrices),rebuildMask&faces);
            }
            for(size_t i=0;i<pointLiveBatches.size();++i){const auto& batch=pointLiveBatches[i];
                pointAppendCandidates(pointLiveCandidates,i,pointMask(batch.low,batch.high,faceMatrices),faces);}
            replayBoundsJoin();
            if(withReplays)for(size_t i=0;i<replays.size();++i){const auto& bounds=replays[i]->pointBounds;
                const unsigned mask=NorthlightReplayBounds::outsideSphere(bounds,pointSelected.position,pointSelected.attenuationEnd)?0:
                    NorthlightPointShadow::faceMask(bounds.low,bounds.high,bounds.valid,faceMatrices);
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
            const float sphere[4]={pointSelected.attenuationEnd*pointSelected.attenuationEnd,1.f/PointResolution,0,0}; /* LocalShadowPS: casters within range only */
            auto bindFace=[&](IDirect3DSurface9* target,const char* what){
                return pointCheck(d->SetDepthStencilSurface(nullptr),"unbind cube depth")&&pointCheck(d->SetRenderTarget(0,target),what)&&
                    pointCheck(d->SetDepthStencilSurface(pointDepth),"cube depth target")&&pointCheck(d->SetViewport(&viewport),"cube viewport")&&
                    pointCheck(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0),"clear cube")&&
                    pointCheck(d->SetVertexDeclaration(shadowDecl),"cube declaration")&&pointCheck(d->SetVertexShader(pointShadowVS),"cube static VS")&&
                    pointCheck(d->SetVertexShaderConstantF(0,matrix,4),"cube face matrix")&&pointCheck(d->SetPixelShader(pointShadowPS),"cube static PS")&&pointCheck(d->SetPixelShaderConstantF(1,sphere,1),"cube sphere")&&
                    pointCheck(d->SetStreamSource(0,vertices,0,sizeof(NorthlightGI::WorldVertex)),"cube static vertices")&&pointCheck(d->SetIndices(indices),"cube static indices")&&
                    pointCheck(d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP),"cube alpha U")&&pointCheck(d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP),"cube alpha V");
            };
            if(rebuildMask&(1u<<face)){if(!bindFace(pointCacheSurface[face],"cube cache target"))return false;
            UINT boundPage=UINT_MAX;
            for(size_t index:pointStaticCandidates[face]){const auto& batch=batches[index];
                if(batch.material>=materials.size()||batch.material>=uploadedAlphaCutoffs.size())return pointCheck(E_FAIL,"cube material range");
                float material[]={1,1,1,uploadedAlphaCutoffs[batch.material]};
                if(!pointCheck(d->SetTexture(0,materials[batch.material]),"cube static alpha")||
                   !pointCheck(d->SetPixelShaderConstantF(0,material,1),"cube material")||
                   !bindMeshPage(batch,boundPage)||!pointCheck(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,batch.minVertex,batch.vertexCount,batch.start,batch.count),"cube static draw"))return false;
                ++pointDraws;
            }
            const auto* digests=pointFaceDigests(pointSelected);
            pointFaceContent[face]=digests?digests[face]:NorthlightLocalShadowSignature::Digest{};pointFaceKnown[face]=digests!=nullptr;pointFaceSerial[face]=meshGeneration;}
            else if(pointFaceKnown[face]&&pointFaceSerial[face]!=meshGeneration){pointFaceSerial[face]=meshGeneration;++pointFacesKept;} /* same content: the old rule redrew it */
            if(rebuildMask&(1u<<face))++pointFacesRebuilt;
            const unsigned faceDraws=pointDraws;
            // Dynamic casters of this frame into the scratch face, plus cached-mesh
            // terrain chunks not drawn live (terrain is never part of the cache).
            if(!bindFace(pointScratchSurface,"cube scratch target"))return false;
            UINT terrainBoundPage=UINT_MAX;
            for(size_t index:pointTerrainCandidates[face]){const auto& batch=batches[index];
                if(batch.material>=materials.size()||batch.material>=uploadedAlphaCutoffs.size())return pointCheck(E_FAIL,"cube material range");
                float material[]={1,1,1,uploadedAlphaCutoffs[batch.material]};
                if(!pointCheck(d->SetTexture(0,materials[batch.material]),"cube terrain alpha")||
                   !pointCheck(d->SetPixelShaderConstantF(0,material,1),"cube terrain material")||
                   !bindMeshPage(batch,terrainBoundPage)||!pointCheck(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,batch.minVertex,batch.vertexCount,batch.start,batch.count),"cube terrain draw"))return false;
                ++pointDraws;
            }
            if(liveTerrainIndexCount){
                float opaque[]={1,1,1,-1};
                if(!pointCheck(d->SetPixelShaderConstantF(0,opaque,1),"cube live material")||
                   !pointCheck(d->SetTexture(0,nullptr),"cube live alpha")||
                   !pointCheck(d->SetStreamSource(0,liveTerrainGPU.vertices(),0,sizeof(NorthlightGI::WorldVertex)),"cube live vertices")||
                   !pointCheck(d->SetIndices(liveIndicesGPU),"cube live indices"))return false;
                NorthlightDynamicRing::touch(liveIndexRing,fence()); /* 0.3.192 (DXVK3): drawn this frame: the slice stays pending until this frame's fence */
                for(size_t index:pointLiveCandidates[face]){const auto& batch=pointLiveBatches[index];
                    if(!pointCheck(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,liveTerrainGPU.vertexCapacity(),UINT(liveIndexBase+batch.start),batch.count),"cube live draw"))return false;++pointDraws;
                }
            }
            if(withReplays){
            float rows[16];NorthlightWorldMath::replayProjection(context.inverseView,matrix,rows);
            NorthlightReplayPoses::Pass<BOOL> poseConstants;
            NorthlightReplayDrawState::Cache replayBindings(d);
            // LocalShadowPS reads patched TEXCOORD7 and writes perspective z/w;
            // the directional replay PS assumes orthographic w=1 and is invalid here.
            for(size_t index:pointReplayCandidates[face]){const auto& p=replays[index];
                if(!pointCheck(replayBindings.geometry(*p),"cube replay geometry state"))return false;
                if(!poseConstants.prepare(*p,rows))return false;
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
            pointFaceWeights[face]=pointDraws-faceDraws; /* the next face cycle's balance */
            // Union: min(scratch, cached static face) into the cube face.
            if(!pointCheck(d->SetDepthStencilSurface(nullptr),"unbind cube depth")||!pointCheck(d->SetRenderTarget(0,pointFaces[face]),"cube target"))return false;
            d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
            d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetStreamSourceFreq(0,1);
            d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
            if(!pointCheck(d->SetTexture(0,pointScratch),"cube union scratch")||!pointCheck(d->SetTexture(1,pointCacheFace[face]),"cube union cache"))return false;
            for(unsigned sampler=0;sampler<2;++sampler){d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(sampler,D3DSAMP_SRGBTEXTURE,FALSE);}
            float unionConstants[2][4]={{0,0,0,float(PointResolution)},{1.f/PointResolution,1.f/PointResolution,0,0}};
            if(!pointCheck(d->SetPixelShaderConstantF(0,&unionConstants[0][0],2),"cube union constants")||!pointCheck(d->SetPixelShader(unionPS),"cube union shader")||
               !pointCheck(quad(PointResolution,PointResolution),"cube union"))return false;
            d->SetTexture(0,nullptr);d->SetTexture(1,nullptr);
            d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
            d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        }
        if(rebuild){pointCacheValid=true;pointCacheSource=pointSelected.sourceId;pointCacheChunks=chunkHash;} /* cycling: already valid, its remaining faces wait for their frame */
        if(!fresh){pointReady=true;captureDemand=true;++captureDeferrals;return true;} /* schedule stays incomplete: redrawn with replays next frame */
        if(cycling&&!pointFaceCycle.advance()){pointReady=true;++pointUpdates;pointLogSchedule(false);return true;} /* stale until the last group */
        if(cycling)pointSchedule.commit(pointFaceCycle.startedAt,pointSelected,pointFaceCycle.generation);
        else pointSchedule.commit(updateAt,pointSelected,meshGeneration);
        pointReady=true;++pointUpdates;pointLogSchedule(false);
        if(frames==0||(frames%600==0&&NorthlightDiagnostics::enabled()))logf("POINT source=%llu kind=%u range=%.2f faces=%u size=256 draws=%u culled=%u replayBounds=%u boundVertices=%zu boundOperations=%zu",(unsigned long long)pointSelected.sourceId,pointSelected.kind,pointSelected.attenuationEnd,unsigned(__builtin_popcount(faces)),pointDraws,pointCulled,pointBoundsValid,pointBoundVertices,pointBoundOperations);
        return true;
    }
    bool renderPointLighting(IDirect3DTexture9* depth,IDirect3DTexture9* waterMask,UINT w,UINT h,float nearZ,float farZ,float minZ,float maxZ){
        if(!pointReady)return false;
        float constants[10][4]={};constants[0][0]=1.f/w;constants[0][1]=1.f/h;constants[0][2]=nearZ;constants[0][3]=farZ;
        constants[1][0]=projection[0];constants[1][1]=projection[1];constants[1][2]=projection[2];constants[1][3]=minZ;
        constants[2][0]=1.f/(maxZ-minZ);constants[2][1]=waterMask?1.f:0.f;constants[2][2]=1;constants[2][3]=.025f;
        std::memcpy(constants[3],context.inverseView,64);std::memcpy(constants[7],pointSelected.position,12);constants[7][3]=pointSelected.attenuationStart;
        std::memcpy(constants[8],pointSelected.diffuse,12);constants[8][3]=pointSelected.attenuationEnd;
        constants[9][0]=.1f;constants[9][1]=pointSelected.attenuationEnd;constants[9][2]=1.f/PointResolution;constants[9][3]=1;
        if(!pointCheck(d->SetPixelShaderConstantF(0,&constants[0][0],10),"local constants")||
           !pointCheck(d->SetPixelShader(pointLightingPS),"local lighting PS")||
           !pointCheck(d->SetTexture(0,depth),"local depth")||!pointCheck(d->SetTexture(1,pointCube),"local cube")||
           !pointCheck(d->SetTexture(2,waterMask),"local water")||
           !pointCheck(d->SetRenderTarget(1,nullptr),"local baseline unbind")||
           !pointCheck(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE),"local additive blend")||
           !pointCheck(d->SetRenderState(D3DRS_COLORWRITEENABLE,7),"local RGB mask"))return false;
        for(unsigned sampler=0;sampler<3;++sampler){
            const struct{D3DSAMPLERSTATETYPE state;DWORD value;} states[]={{D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSW,D3DTADDRESS_CLAMP},{D3DSAMP_MINFILTER,D3DTEXF_POINT},{D3DSAMP_MAGFILTER,D3DTEXF_POINT},{D3DSAMP_MIPFILTER,D3DTEXF_NONE},{D3DSAMP_SRGBTEXTURE,FALSE}};
            for(const auto& state:states)if(!pointCheck(d->SetSamplerState(sampler,state.state,state.value),"local sampler"))return false;
        }
        // The correction is exactly zero beyond attenuationEnd (LocalColor.w).
        const std::array<NorthlightLocalLightSelection::Constant,1> sphere={{{pointSelected.position[0],pointSelected.position[1],pointSelected.position[2],pointSelected.attenuationEnd}}};
        // Enable first, as in the batch loops: localScissor may fail open by disabling it.
        d->SetRenderState(D3DRS_SCISSORTESTENABLE,NorthlightLocalLightScissor::Enabled);
        if(!localScissor(sphere,1)){d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);return true;}
        const bool drawn=pointCheck(quad(w/2,h/2),"local lighting pass");
        d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);return drawn;
    }
