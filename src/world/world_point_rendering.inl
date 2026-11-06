// Included inside WorldRenderer. Each source owns a complete cube and its static cache.
    static constexpr UINT PointResolution=256;
    struct PointContentMemo {const NorthlightLocalShadowSignature::Records* records=nullptr;uint64_t generation=0;float position[3]={},range=0;bool valid=false;NorthlightLocalShadowSignature::Digest faces[6];};
    struct PointSlot {
        IDirect3DCubeTexture9* cube=nullptr;
        IDirect3DSurface9* faces[6]={};
        IDirect3DTexture9 *cacheFace[6]={};
        IDirect3DSurface9 *cacheSurface[6]={};
        unsigned staticOnlyFaces=0;
        bool cacheValid=false,ready=false;float selectionGain=1;uint64_t cacheSource=0;
        NorthlightLocalShadowSignature::Digest faceContent[6];bool faceKnown[6]={};uint64_t faceSerial[6]={};
        mutable PointContentMemo contentMemo;
        NorthlightPointShadow::FaceCycle faceCycle;unsigned faceWeights[6]={};
        NorthlightPointShadow::RefreshSchedule schedule;
        NorthlightLocalLights::Light selected{};std::string map;DWORD retryAt=0;
    };
    std::array<PointSlot,NorthlightPointShadow::MaxLights> pointSlots;
    struct PointKeep {PointSlot slot;uint64_t age=0;bool wanted=false;};
    std::array<PointKeep,8> pointKeeps;uint64_t pointKeepAge=0;
    // Sources render sequentially; only their completed cubes and static caches persist.
    IDirect3DSurface9 *pointDepth=nullptr,*pointScratchSurface=nullptr;
    IDirect3DTexture9 *pointScratch=nullptr,*pointEmpty=nullptr;
    IDirect3DVertexShader9* pointShadowVS=nullptr;
    IDirect3DPixelShader9 *pointShadowPS=nullptr,*pointLightingPS=nullptr;
    IDirect3DPixelShader9 *pointSoftResolvePS=nullptr,*pointSoftLightingPS=nullptr;bool pointSoftTried=false;
    NorthlightPointShadow::SelectionTracker pointSelection;std::string pointSelectionMap;
    unsigned pointIndex=0;bool pointReady=false;
    PointSlot& pointSlot(){return pointSlots[pointIndex];}
    const PointSlot& pointSlot()const{return pointSlots[pointIndex];}
    unsigned pointFacesRebuilt=0,pointFacesKept=0,pointFacesReused=0,pointFailures=0,pointDraws=0,pointCulled=0,pointBoundsValid=0;
    unsigned pointUpdates=0,pointReuses=0,pointReportedLimit=0,pointBatchesMerged=0;
    using PointCandidates=std::array<std::vector<size_t>,6>;
    PointCandidates pointStaticCandidates,pointTerrainCandidates,pointLiveCandidates,pointReplayCandidates;
    std::vector<unsigned char> pointBatchMasks;
    UINT pointPrimitiveLimit=0;
    struct PointWork {unsigned selected=0,statics=0,terrain=0,live=0,replay=0,replayMerged=0,split=0,unions=0,rebuilt=0,restored=0;uint64_t triangles=0,bridged=0;} pointWork;
    size_t pointBoundVertices=0,pointBoundOperations=0;
    struct PointLiveBatch {UINT start,count;V low,high;};std::vector<PointLiveBatch> pointLiveBatches;

    static void releasePointSlot(PointSlot& p){
        p.schedule.invalidate();
        for(auto& f:p.faces)drop(f);drop(p.cube);
        for(auto& t:p.cacheFace)drop(t);for(auto& f:p.cacheSurface)drop(f);p=PointSlot{};
    }
    void releasePointSlot(){releasePointSlot(pointSlot());}
    size_t clearPointKeeps(){
        size_t bytes=0;for(auto& keep:pointKeeps){for(auto* t:keep.slot.cacheFace)if(t)bytes+=size_t(PointResolution)*PointResolution*4;releasePointSlot(keep.slot);keep.age=0;keep.wanted=false;}return bytes;
    }
    void keepPointSlot(PointSlot& p,unsigned capacity){
        bool complete=p.cacheValid&&active&&p.map==active->map&&p.cacheSource==p.selected.sourceId;
        for(unsigned face=0;face<6;++face)complete=complete&&(bool(p.cacheFace[face])==bool(p.cacheSurface[face]));
        if(!capacity||!complete){releasePointSlot(p);return;}
        unsigned victim=capacity;
        for(unsigned i=0;i<capacity;++i){const auto& keep=pointKeeps[i];
            if(!keep.slot.cacheValid||keep.slot.cacheSource==p.cacheSource){victim=i;break;}
            if(!keep.wanted&&(victim==capacity||keep.age<pointKeeps[victim].age))victim=i;
        }
        if(victim==capacity){releasePointSlot(p);return;}
        auto& keep=pointKeeps[victim];releasePointSlot(keep.slot);
        // Retain static faces only. A new cube must render every dynamic face
        // before use, even if the source returns within the ordinary refresh period.
        for(auto& f:p.faces)drop(f);drop(p.cube);p.ready=false;p.staticOnlyFaces=0;
        p.schedule.invalidate();p.faceCycle.reset();p.contentMemo.valid=false;p.retryAt=0;
        std::swap(keep.slot,p);keep.age=++pointKeepAge;keep.wanted=false;
    }
    void restorePointSlot(const NorthlightLocalLights::Light& light){
        auto& p=pointSlot();if(p.cacheValid&&p.cacheSource==light.sourceId&&p.map==active->map)return;
        for(auto& keep:pointKeeps)if(keep.slot.cacheValid&&keep.slot.cacheSource==light.sourceId&&keep.slot.map==active->map){
            releasePointSlot(p);std::swap(p,keep.slot);keep.age=0;keep.wanted=false;++pointWork.restored;return;
        }
    }
    void releasePointGPU(){
        ++nativeCubeRevision;
        if(nativeDirectRenderer)nativeDirectRenderer->reset();nativeDirectScene.clear();nativeDirectFrame=~0u;nativeDirectBindingCount=0;
        for(pointIndex=0;pointIndex<pointSlots.size();++pointIndex)releasePointSlot();pointIndex=0;
        clearPointKeeps();pointKeepAge=0;
        drop(pointDepth);drop(pointScratchSurface);drop(pointScratch);drop(pointEmpty);
        drop(pointShadowVS);drop(pointShadowPS);drop(pointLightingPS);
        drop(pointSoftResolvePS);drop(pointSoftLightingPS);pointSoftTried=false;
        pointReady=false;pointUpdates=pointReuses=pointReportedLimit=pointBatchesMerged=0;localLightTracker.reset();localLightQpc=0;
        pointSelection.reset();pointSelectionMap.clear();
        for(auto* lists:{&pointStaticCandidates,&pointTerrainCandidates,&pointLiveCandidates,&pointReplayCandidates})
            for(auto& list:*lists)std::vector<size_t>().swap(list);
        std::vector<unsigned char>().swap(pointBatchMasks);
    }
    bool pointCheck(HRESULT hr,const char* operation){
        if(SUCCEEDED(hr))return true;
        ++nativeCubeRevision;
        pointSlot().schedule.invalidate();pointSlot().cacheValid=false;pointSlot().staticOnlyFaces=0;
        pointSlot().ready=false;pointSlot().retryAt=GetTickCount()+1000;
        if(pointFailures++<12)logf("POINT pass skipped: %s hr=%08lx (world effects retained)",operation,(unsigned long)hr);
        return false;
    }
    bool pointResources(){
        if(pointSlot().retryAt&&LONG(GetTickCount()-pointSlot().retryAt)<0)return false;
        if(!pointPrimitiveLimit){D3DCAPS9 caps{};pointPrimitiveLimit=SUCCEEDED(d->GetDeviceCaps(&caps))&&caps.MaxPrimitiveCount?caps.MaxPrimitiveCount:1;}
        if(!pointScratch&&!pointCheck(d->CreateTexture(PointResolution,PointResolution,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&pointScratch,nullptr),"cube scratch"))return false;
        if(!pointScratch)return pointCheck(E_FAIL,"empty cube scratch");
        if(!pointScratchSurface&&!pointCheck(pointScratch->GetSurfaceLevel(0,&pointScratchSurface),"cube scratch surface"))return false;
        if(!pointDepth&&!pointCheck(d->CreateDepthStencilSurface(PointResolution,PointResolution,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&pointDepth,nullptr),"cube depth"))return false;
        if(!pointShadowVS&&!pointCheck(d->CreateVertexShader(kLocalShadowVSShader,&pointShadowVS),"cube vertex shader"))return false;
        if(!pointShadowPS&&!pointCheck(d->CreatePixelShader(kLocalShadowPSShader,&pointShadowPS),"cube pixel shader"))return false;
        if(!pointLightingPS&&!pointCheck(d->CreatePixelShader(kLocalLightingShader,&pointLightingPS),"local lighting shader"))return false;
        if(!pointDepth||!pointScratchSurface||!pointShadowVS||!pointShadowPS||!pointLightingPS)return pointCheck(E_FAIL,"empty optional resource");
        if(!pointEmpty){
            IDirect3DTexture9* texture=nullptr;HRESULT hr=d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr);
            if(SUCCEEDED(hr)&&texture){D3DLOCKED_RECT lock{};hr=texture->LockRect(0,&lock,nullptr,0);
                if(SUCCEEDED(hr)){if(lock.pBits)*static_cast<DWORD*>(lock.pBits)=0xffffffffu;const HRESULT unlocked=texture->UnlockRect(0);hr=lock.pBits?unlocked:E_FAIL;}}
            if(FAILED(hr)||!texture){drop(texture);return pointCheck(FAILED(hr)?hr:E_FAIL,"empty static face texture");}
            pointEmpty=texture;
        }
        if(pointSlot().cube){
            bool faces=true;for(auto* face:pointSlot().faces)faces=faces&&face;if(faces){pointSlot().retryAt=0;return true;}
        }
        // Static faces may have survived an offscreen interval without a cube.
        // Complete partial allocations on retry; pointCheck already invalidates their contents.
        if(!pointSlot().cube&&!pointCheck(d->CreateCubeTexture(PointResolution,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&pointSlot().cube,nullptr),"create R32F cube"))return false;
        if(!pointSlot().cube)return pointCheck(E_FAIL,"empty cube");
        for(unsigned i=0;i<6;++i){if(!pointSlot().faces[i]&&!pointCheck(pointSlot().cube->GetCubeMapSurface(D3DCUBEMAP_FACES(i),0,&pointSlot().faces[i]),"cube surface"))return false;
            if(!pointSlot().faces[i])return pointCheck(E_FAIL,"empty cube face");}
        pointSlot().retryAt=0;return true;
    }
    bool pointCacheResources(unsigned face){
        auto& p=pointSlot();
        if(!p.cacheFace[face]){
            drop(p.cacheSurface[face]);
            if(!pointCheck(d->CreateTexture(PointResolution,PointResolution,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&p.cacheFace[face],nullptr),"cube cache face"))return false;
        }
        if(!p.cacheFace[face])return pointCheck(E_FAIL,"empty cube cache face");
        if(!p.cacheSurface[face]&&!pointCheck(p.cacheFace[face]->GetSurfaceLevel(0,&p.cacheSurface[face]),"cube cache surface"))return false;
        return p.cacheSurface[face]?true:pointCheck(E_FAIL,"empty cube cache surface");
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
    Batch pointBatchRun(const std::vector<size_t>& candidates,size_t& at,unsigned face=6){
        size_t last=candidates[at++];Batch run=batches[last];uint64_t selected=run.count,bridged=0;
        const auto opaque=[&](unsigned material){return material<materials.size()&&material<uploadedAlphaCutoffs.size()&&uploadedAlphaCutoffs[material]<=0;};
        const auto append=[&](Batch& sum,const Batch& next){
            if(next.page!=sum.page||uint64_t(sum.start)+uint64_t(sum.count)*3!=next.start)return false;
            const auto low=std::min(sum.minVertex,next.minVertex);
            const uint64_t high=std::max(uint64_t(sum.minVertex)+sum.vertexCount,uint64_t(next.minVertex)+next.vertexCount);
            const uint64_t count=uint64_t(sum.count)+next.count;
            if(high>UINT_MAX||count>UINT_MAX||(pointPrimitiveLimit&&count>pointPrimitiveLimit))return false;
            sum.count=UINT(count);sum.minVertex=low;sum.vertexCount=UINT(high-low);return true;
        };
        // Preserve index order and coverage. A gap may bridge only batches
        // proven outside this light/face; excluded or unknown batches stay barriers.
        while(at<candidates.size()){
            const size_t index=candidates[at];const auto& next=batches[index];
            if(index<=last)break;
            // Cached world materials are UNORM: cutoff zero cannot reject any
            // alpha. The draw below canonicalizes these to no alpha sampling.
            if((next.material!=run.material||next.pointOnly!=run.pointOnly)&&!(opaque(run.material)&&opaque(next.material)))break;
            Batch combined=run;uint64_t extra=0;size_t gap=last+1;
            for(;gap<index;++gap){
                if(face>=6||pointBatchMasks.size()!=batches.size()||(pointBatchMasks[gap]&(1u<<face))||!append(combined,batches[gap]))break;
                extra+=batches[gap].count;
            }
            // ponytail: at most twice the selected triangle work; compact indices
            // are the next step only if sparse, large gaps dominate a real capture.
            if(gap!=index||bridged+extra>selected+next.count||!append(combined,next))break;
            run=combined;last=index;selected+=next.count;bridged+=extra;++at;++pointBatchesMerged;
        }
        pointWork.bridged+=bridged;
        return run;
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
                pointUpdates,pointReuses,unsigned(reused),unsigned(pointReplayCount>=256),NorthlightPointShadow::RefreshSchedule::period(pointReplayCount,quality.pointShadowRefreshMs),replays.size(),pointDraws,pointCulled);
            pointUpdates=pointReuses=0;
            if(NorthlightQuality::renderProfile(quality))logf("POINT static faces rebuilt=%u keptByContent=%u mergedBatches=%u worldFrame=%llu",pointFacesRebuilt,pointFacesKept,pointBatchesMerged,(unsigned long long)frames);
            pointFacesRebuilt=pointFacesKept=pointBatchesMerged=0;
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
        pointBoundsValid=0;replayBoundsCache.beginFrame(diagnostics,replays.size());replayBoundsMetadata.beginFrame();
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
        pointBoundsValid=0;replayBoundsCache.beginFrame(diagnostics,replays.size());replayBoundsMetadata.beginFrame();
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
        replayBoundsCache.lendToBuild(buildBudget);
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
            deferLogf("WORLD replay bounds total=%zu valid=%u envelopeValid=%u fallbackValid=%u deferred=%u buildVertices=%zu operations=%zu envelopeEntries=%zu envelopeBytes=%zu envelopeMs=%.3f readyMs=%.3f heavyMs=%.3f buildMs=%.3f classifyMs=%.3f wallMs=%.3f skinValid=%zu coldVisits=%zu buildIndices=%zu completed=%zu timeDeferred=%zu vertexDeferred=%zu operationDeferred=%zu unsupported=%zu tupleLimit=%zu invalid=%zu evictions=%zu readyEvictions=%zu expiredEvictions=%zu releasedGroupBytes=%zu buildSkippedReady=%zu cheapValid=%zu heavyAttempts=%zu heavyValid=%zu cheapSliceDeferred=%zu async=%u mainMs=%.3f workerMs=%.3f waitMs=%.3f startLagMs=%.3f fallbackCapped=%zu sourceIdentityHits=%zu sourceReconstructedHits=%zu",
                replays.size(),pointBoundsValid,replayBoundsCache.envelopeValid(),replayBoundsCache.fallbackValid(),replayBoundsCache.deferred(),stats.buildVertices,pointBoundOperations,
                replayBoundsCache.envelopeEntries(),replayBoundsCache.envelopeBytes(),replayBoundsCache.envelopeMilliseconds(),
                replayBoundsCache.envelopeReadyMilliseconds(),replayBoundsCache.envelopeHeavyMilliseconds(),replayBoundsCache.envelopeBuildMilliseconds(),
                replayBoundsCache.envelopeClassifyMilliseconds(),wallMs,stats.skinValid,stats.cold,stats.buildIndices,stats.completed,
                stats.timeDeferred,stats.vertexDeferred,stats.operationDeferred,stats.unsupported,stats.tupleLimit,stats.invalid,stats.evictions,stats.readyEvictions,stats.expiredEvictions,stats.releasedGroupBytes,stats.buildSkippedReady,
                stats.cheapValid,stats.heavyAttempts,stats.heavyValid,stats.cheapSliceDeferred,unsigned(async),mainMs,workerMs,waitMs,startLagMs,async?replayBoundsCapped:size_t(0),stats.sourceIdentityHits,stats.sourceReconstructedHits);
            deferLogf("WORLD replay priority cheapVisited=%zu heavyVisited=%zu buildVisited=%zu cheap=%zu heavy=%zu cold=%zu unsupported=%zu unclassified=%zu",
                cheapVisited,heavyVisited,buildVisited,cheap,heavy,cold,unsupported,unclassified);
            deferLogf("WORLD shadow bounds coverage total=%zu valid=%u unbounded=%zu readyBudgetMs=%.3f buildBudgetMs=%.3f shared=directional-and-point",
                replays.size(),pointBoundsValid,replays.size()-pointBoundsValid,double(replayBoundsCache.phaseLimit(0))/1000000.,double(replayBoundsCache.phaseLimit(2))/1000000.);
            deferLogf("WORLD replay build pending=%u pendingVertices=%zu pendingIndices=%zu resumes=%zu admissionDeferred=%zu ownerExpired=%zu abandoned=%zu",
                unsigned(replayBoundsCache.hasPendingBuild()),replayBoundsCache.pendingBuildVertices(),replayBoundsCache.pendingBuildIndices(),
                stats.pendingResumes,stats.pendingDeferred,stats.pendingOwnerExpired,stats.pendingAbandoned);
            deferLogf("WORLD replay memo tests=%zu hits=%zu misses=%zu stored=%zu evictions=%zu rejected=%zu entries=%zu bytes=%zu comparedBytes=%zu avoidedOperations=%zu budgetDeferred=%zu effectiveCopiedBytes=%zu",
                stats.memoTests,stats.memoHits,stats.memoMisses,stats.memoStored,stats.memoEvictions,stats.memoRejected,
                replayBoundsCache.memoEntries(),replayBoundsCache.memoBytes(),stats.memoComparedBytes,stats.memoAvoidedOperations,stats.memoBudgetDeferred,stats.effectiveCopiedBytes);
            const auto& memoPolicy=replayBoundsCache.memoPolicy();
            const auto memoTotals=replayBoundsCache.memoTotals();
            deferLogf("WORLD replay memoPolicy enabled=%u cooldownFrames=%u pauses=%llu retries=%llu coldMisses=%zu replacedMisses=%zu cameraMisses=%zu constantMisses=%zu bypassed=%zu storeBypassed=%zu protected=%zu copiedBytes=%zu totalTests=%llu totalHits=%llu totalCold=%llu totalReplaced=%llu totalCamera=%llu totalConstants=%llu totalBypassed=%llu totalStoreBypassed=%llu totalProtected=%llu totalCopiedBytes=%llu",
                unsigned(memoPolicy.enabled()),memoPolicy.remaining(),(unsigned long long)memoPolicy.pauses(),(unsigned long long)memoPolicy.retries(),
                stats.memoColdMisses,stats.memoReplacedMisses,stats.memoCameraMisses,stats.memoConstantMisses,
                stats.memoBypassed,stats.memoStoreBypassed,stats.memoProtected,stats.memoCopiedBytes,
                (unsigned long long)memoTotals.tests,(unsigned long long)memoTotals.hits,(unsigned long long)memoTotals.cold,
                (unsigned long long)memoTotals.replaced,(unsigned long long)memoTotals.camera,(unsigned long long)memoTotals.constants,
                (unsigned long long)memoTotals.bypassed,(unsigned long long)memoTotals.storeBypassed,
                (unsigned long long)memoTotals.protectedStores,(unsigned long long)memoTotals.copiedBytes);
            const auto& metadata=replayBoundsMetadata.stats();
            deferLogf("WORLD replay adaptive fastValid=%zu borrowedUs=%.3f extraCheapVisited=%zu preparedHits=%zu preparedMisses=%zu preparedNew=%zu preparedFallbacks=%zu preparedEvictions=%zu preparedEntries=%zu preparedBytes=%zu diagnosticsSampled=1",
                stats.fastValid,replayBoundsCache.borrowedMicroseconds(),cheapVisited-reservedCheapVisited,metadata.hits,metadata.misses,metadata.prepared,metadata.fallbacks,metadata.evictions,replayBoundsMetadata.entries(),replayBoundsMetadata.bytes());
            const auto& slow=replayBoundsCache.lastExpensiveEnvelope();
            if(slow.valid){
                const char* stage=slow.stage==NorthlightReplayBounds::EnvelopeCache::Stage::Cheap?"cheap":"heavy";
                const char* result=slow.status==NorthlightReplayBounds::Status::Valid?"valid":slow.status==NorthlightReplayBounds::Status::Budget?"budget":slow.status==NorthlightReplayBounds::Status::Invalid?"invalid":"unsupported";
                deferLogf("WORLD replay expensive cacheFrame=%llu program=%016llx groups=%zu operations=%zu elapsedUs=%.3f stage=%s result=%s skin=%u",
                    (unsigned long long)slow.frame,(unsigned long long)slow.programHash,slow.groups,slow.operations,slow.elapsedUs,stage,result,unsigned(slow.skin));
            }
        }
    }
    // Capture-skip prediction: would renderPointShadow() refresh the cube this frame?
    // Same selection and schedule, no state change; unpredictable counts as due.
    bool pointRefreshPredicted()const{
        if(!effects.shadows||lastRenderDebug!=0||!quality.pointShadows||!active)return false;
        // Conservative: a new lamp or changing selection also needs current-frame casters.
        return NorthlightPointShadow::selectMany(active->localLights.data(),active->localLights.size(),context.camera,quality.pointShadowLimit,{},float(quality.pointShadowDistance),true).count!=0||
            NorthlightPointShadow::selectMany(equipmentLights.output.data(),equipmentLights.outputCount,context.camera,quality.pointShadowLimit,{},float(quality.pointShadowDistance),true).count!=0;
    }
    using PointReplayRuns=std::vector<std::pair<UINT,UINT>>;
    struct NativeShellMemo {
        std::weak_ptr<const NorthlightDrawSnapshot::Mesh> mesh;
        std::shared_ptr<const NorthlightActorDeformation::Program> program;
        std::array<float,1024> constants{};std::array<float,16> inverse{};
        std::array<float,3> point{};std::uint64_t declaration=0;unsigned tag=0,source=0;
        NorthlightNativeFixtureShadow::Prepared prepared;
        NorthlightDrawSnapshot::Mesh ownedMesh;
        Batch::LampTriangles local,worldTriangles;
        std::vector<NorthlightActorDeformation::Position> world;
        bool matched=false;PointReplayRuns runs;
    };
    std::array<NativeShellMemo,32> nativeShellMemo;unsigned nativeShellNext=0,nativeShellProofs=0,nativeShellMemoHits=0,nativeShellPoseChecks=0;
    bool pointOwnNativeShell(const Replay& p,const NorthlightLocalLights::Light& light,PointReplayRuns& remaining,const NorthlightEquipmentLights::Tracker::Entry* resolved=nullptr){
        const auto* entry=resolved?resolved:equipmentLights.fixture(light.sourceId);
        if(!entry||!nativeFixtures||entry->fixtureTag>nativeFixtures->records.size()||(p.cutoff>=0&&p.cutoff!=.5f)||!p.count||p.count>4096||
           p.mesh().vertexCount>12288||p.count!=p.mesh().primitiveCount||(p.type!=D3DPT_TRIANGLELIST&&p.type!=D3DPT_TRIANGLESTRIP))return false;
        if(!p.pointBounds.valid)return false;
        for(unsigned k=0;k<3;++k)if(light.position[k]<p.pointBounds.low[k]-.003f||light.position[k]>p.pointBounds.high[k]+.003f)return false;
        const auto program=actorPrograms.find(p.originalShader);const D3DVERTEXELEMENT9* elements=nullptr;UINT elementCount=0;
        if(program==actorPrograms.end()||!program->second||!declarationCache.get(p.decl,elements,elementCount))return false;
        const auto declaration=NorthlightNativeFixtures::hash(elements,elementCount*sizeof(*elements));
        const auto source=unsigned((entry->nativeId-1)&63);
        NativeShellMemo transient,*memo=nullptr;
        if(p.shared)for(auto& cached:nativeShellMemo)if(cached.tag==entry->fixtureTag&&cached.source==source&&cached.program==program->second&&cached.declaration==declaration&&cached.mesh.lock()==p.shared){memo=&cached;break;}
        if(memo){++nativeShellMemoHits;if(!memo->prepared.valid())return false;
            if(!std::memcmp(memo->constants.data(),p.constants,sizeof memo->constants)&&!std::memcmp(memo->inverse.data(),context.inverseView,sizeof memo->inverse)&&!std::memcmp(memo->point.data(),light.position,sizeof memo->point)){
                remaining=memo->runs;return memo->matched;}}
        else{
        memo=p.shared?&nativeShellMemo[nativeShellNext++%nativeShellMemo.size()]:&transient;*memo=NativeShellMemo{};
        memo->mesh=p.shared;memo->program=program->second;memo->declaration=declaration;memo->tag=entry->fixtureTag;memo->source=source;
        const auto& record=nativeFixtures->records[entry->fixtureTag-1];
        const NorthlightNativeFixtures::ShellView shell{nativeFixtures->shells.data()+record.shellFirst,record.shellCount};
        const D3DVERTEXELEMENT9* position=nullptr;
        for(UINT i=0;i<elementCount&&elements[i].Stream!=0xff;++i)if(elements[i].Usage==D3DDECLUSAGE_POSITION&&!elements[i].UsageIndex){if(position)return false;position=elements+i;}
        if(!position||position->Stream>=4||position->Method!=D3DDECLMETHOD_DEFAULT)return false;
        const auto& stream=p.mesh().streams[position->Stream];const unsigned width=NorthlightDrawSnapshot::declarationBytes(position->Type);
        if(!width||unsigned(position->Offset)+width>stream.stride||!p.mesh().vertexCount||std::uint64_t(p.mesh().vertexCount-1)*stream.stride+position->Offset+width>stream.bytes.size())return false;
        ++nativeShellProofs;std::vector<NorthlightActorDeformation::Position> raw(p.mesh().vertexCount);
        for(UINT i=0;i<p.mesh().vertexCount;++i){NorthlightActorDeformation::Four value;
            if(!NorthlightActorDeformation::decodeElement(stream.bytes.data()+std::size_t(i)*stream.stride+position->Offset,position->Type,value))return false;
            raw[i]={value[0],value[1],value[2]};}
        if(!StaticShadowDedup::makeTriangles(raw,p.mesh().indices,p.mesh().indexed,p.count,p.type==D3DPT_TRIANGLESTRIP,memo->local,4096))return false;
        memo->prepared=NorthlightNativeFixtureShadow::prepare(shell,source,memo->local);if(!memo->prepared.valid())return false;
        const auto& excluded=memo->prepared.excluded;
        for(UINT first=0;first<excluded.size();){if(excluded[first]){++first;continue;}UINT end=first+1;while(end<excluded.size()&&!excluded[end])++end;memo->runs.push_back({first,end-first});first=end;}
        NorthlightActorDeformation::Binding bindings[16];std::size_t bindingCount=0;unsigned provided=0,streams=0;
        if(!NorthlightActorDeformation::transientBindings(*program->second,p.mesh(),elements,elementCount,bindings,bindingCount,provided)){memo->prepared={};return false;}
        for(std::size_t i=0;i<bindingCount;++i)streams|=1u<<bindings[i].element.Stream;
        if(!NorthlightNativeFixtureShadow::compact(p.mesh(),p.type==D3DPT_TRIANGLESTRIP,streams,memo->prepared,memo->local,memo->ownedMesh)){memo->prepared={};return false;}
        }
        // Membership survives camera/pose changes. Only the current rigid fit,
        // source anchor and all owned vertices are checked again, in linear work.
        ++nativeShellPoseChecks;
        if(!NorthlightActorDeformation::worldPositions(*program->second,memo->ownedMesh,elements,elementCount,p.constants,context.inverseView,memo->world)||
           !StaticShadowDedup::makeTriangles(memo->world,memo->ownedMesh.indices,true,memo->ownedMesh.primitiveCount,false,memo->worldTriangles,4096))return false;
        const bool matched=NorthlightNativeFixtureShadow::verify(memo->prepared,memo->local,memo->worldTriangles,light.position);memo->matched=matched;
        remaining.clear();if(matched)remaining=memo->runs;
        std::copy_n(p.constants,1024,memo->constants.begin());std::copy_n(context.inverseView,16,memo->inverse.begin());std::copy_n(light.position,3,memo->point.begin());
        return matched;
    }
    bool pointOwnShell(const Replay& p,const Batch& shell,PointReplayRuns& remaining){
        if(!shell.lampTriangles||shell.count!=shell.lampTriangles->size()||(p.cutoff>=0&&p.cutoff!=.5f)||!p.count||p.count>4096||
           p.mesh().vertexCount>12288||p.count!=p.mesh().primitiveCount||(p.type!=D3DPT_TRIANGLELIST&&p.type!=D3DPT_TRIANGLESTRIP))return false;
        if(p.pointBounds.valid){const auto& b=p.pointBounds;const float lo[]={shell.boundsLow.x,shell.boundsLow.y,shell.boundsLow.z},hi[]={shell.boundsHigh.x,shell.boundsHigh.y,shell.boundsHigh.z};
            for(unsigned k=0;k<3;++k)if(b.high[k]<lo[k]-.003f||b.low[k]>hi[k]+.003f)return false;}
        const auto program=actorPrograms.find(p.originalShader);const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
        if(program==actorPrograms.end()||!program->second||!declarationCache.get(p.decl,elements,count))return false;
        std::vector<NorthlightActorDeformation::Position> positions;Batch::LampTriangles triangles;
        if(!NorthlightActorDeformation::worldPositions(*program->second,p.mesh(),elements,count,p.constants,context.inverseView,positions)||
           !StaticShadowDedup::makeTriangles(positions,p.mesh().indices,p.mesh().indexed,p.count,p.type==D3DPT_TRIANGLESTRIP,triangles,4096))return false;
        const auto excluded=NorthlightWmoLampShadow::matching(*shell.lampTriangles,triangles);
        if(excluded.empty()||std::none_of(excluded.begin(),excluded.end(),[](bool hit){return hit;}))return false;
        remaining.clear();
        for(UINT first=0;first<excluded.size();){if(excluded[first]){++first;continue;}
            UINT end=first+1;while(end<excluded.size()&&!excluded[end])++end;
            remaining.push_back({first,end-first});first=end;}
        return true;
    }
    struct PointReplayRun {UINT start,count,min,vertices;};
    PointReplayRun pointReplayRun(const std::vector<size_t>& candidates,size_t& at,const std::map<size_t,PointReplayRuns>& splits,bool opaque){
        const auto& first=*replays[candidates[at++]];PointReplayRun run{first.start,first.count,first.min,first.vertices};
        if(!first.indexed||first.type!=D3DPT_TRIANGLELIST)return run;
        while(at<candidates.size()){
            const size_t index=candidates[at];const auto& next=*replays[index];
            if(splits.count(index)||next.type!=first.type||next.indexed!=first.indexed||next.base!=first.base||
               next.decl!=first.decl||next.shader!=first.shader||next.originalShader!=first.originalShader||next.index!=first.index||
               next.constantGroup!=first.constantGroup||next.alphaMode!=first.alphaMode||std::memcmp(&next.cutoff,&first.cutoff,sizeof(float))||
               std::memcmp(next.stream,first.stream,sizeof first.stream)||std::memcmp(next.offset,first.offset,sizeof first.offset)||std::memcmp(next.stride,first.stride,sizeof first.stride)||
               uint64_t(run.start)+uint64_t(run.count)*3!=next.start)break;
            if(!(opaque&&first.cutoff<=-1.f&&next.cutoff<=-1.f)&&
               (next.texture!=first.texture||next.addressU!=first.addressU||next.addressV!=first.addressV))break;
            // Reuse the existing exact pose proof, including bool/int banks and
            // projection kind. Group zero is a valid first captured pose.
            if(!NorthlightReplayPoses::sameCaptured(first,next))break;
            const UINT low=std::min(run.min,next.min);const uint64_t high=std::max(uint64_t(run.min)+run.vertices,uint64_t(next.min)+next.vertices);
            const uint64_t count=uint64_t(run.count)+next.count;
            if(high>UINT_MAX||count>UINT_MAX||(pointPrimitiveLimit&&count>pointPrimitiveLimit))break;
            run.count=UINT(count);run.min=low;run.vertices=UINT(high-low);++at;++pointBatchesMerged;++pointWork.replayMerged;
        }
        return run;
    }
    // fresh=false: model capture was skipped. Reuse a compatible complete cube
    // or defer; publishing a static-only cube would erase character shadows.
    // withReplays=false (0.3.158 ActorShadows=0): static cache, terrain and union only; the
    // cube is complete without replays (fresh=true) and the schedule sees none.
    bool renderPointShadow(const NorthlightLocalLights::Light& light,bool fresh=true,bool withReplays=true){
        pointSlot().ready=false;pointDraws=pointCulled=pointFacesReused=0;
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
        // Terrain is never cached (its live set follows the view), so the cache key ignores it.
        bool lightChanged=pointSlot().schedule.light.sourceId!=pointSlot().selected.sourceId||pointSlot().schedule.light.attenuationEnd!=pointSlot().selected.attenuationEnd||pointSlot().schedule.light.attenuationStart!=pointSlot().selected.attenuationStart||pointSlot().schedule.light.flags!=pointSlot().selected.flags;
        for(unsigned i=0;i<3;++i)lightChanged=lightChanged||pointSlot().schedule.light.position[i]!=pointSlot().selected.position[i];
        const unsigned rebuildMask=pointStaticRebuild(pointSlot().selected,lightChanged);const bool rebuild=rebuildMask!=0;
        const DWORD updateAt=GetTickCount();if(fresh)pointReplayCount=withReplays?replays.size():0;
        if(!pointSlot().schedule.due(updateAt,pointSlot().selected,meshGeneration,pointReplayCount,rebuild,quality.pointShadowRefreshMs)){
            pointSlot().ready=true;++pointReuses;return true;
        }
        if(!fresh){
            captureDemand=true;++captureDeferrals;
            if(!pointSlot().schedule.usable||lightChanged)return false;
            pointSlot().ready=true;++pointReuses;return true;
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
        std::vector<size_t> ownGlass;Batch ownShell{};
        std::map<size_t,PointReplayRuns> replayRuns;
        try {
            pointBatchMasks.assign(batches.size(),63);
            ownGlass=NorthlightWmoLampShadow::parts(light,batches);
            if(!ownGlass.empty()){
                ownShell=batches[ownGlass.front()];
                if(ownGlass.size()>1){auto proof=std::make_shared<Batch::LampTriangles>();ownShell.count=0;
                    for(size_t i:ownGlass){const auto& b=batches[i];proof->insert(proof->end(),b.lampTriangles->begin(),b.lampTriangles->end());ownShell.count+=b.count;
                        ownShell.boundsLow={std::min(ownShell.boundsLow.x,b.boundsLow.x),std::min(ownShell.boundsLow.y,b.boundsLow.y),std::min(ownShell.boundsLow.z,b.boundsLow.z)};
                        ownShell.boundsHigh={std::max(ownShell.boundsHigh.x,b.boundsHigh.x),std::max(ownShell.boundsHigh.y,b.boundsHigh.y),std::max(ownShell.boundsHigh.z,b.boundsHigh.z)};}
                    ownShell.lampTriangles=std::move(proof);}
            }
            for(auto* lists:{&pointStaticCandidates,&pointTerrainCandidates,&pointLiveCandidates,&pointReplayCandidates})for(auto& list:*lists)list.clear();
            // Reuse the mesh's terrain list shared with the directional passes.
            // Camera-visible native chunks may contain only part of a caster.
            const auto terrainCandidate=[&](size_t i){const auto& batch=batches[i];
                const unsigned mask=pointMask(batch.boundsLow,batch.boundsHigh,clip);pointBatchMasks[i]=static_cast<unsigned char>(mask);
                pointAppendCandidates(pointTerrainCandidates,i,mask,faces);};
            if(terrainBatchListGeneration==meshGeneration&&terrainBatchListSize==batches.size()){
                for(size_t i:terrainBatchList)terrainCandidate(i);
            }else for(size_t i=0;i<batches.size();++i)if(batches[i].terrain)terrainCandidate(i);
            if(rebuildMask&faces){
                for(size_t i=0;i<batches.size();++i){const auto& batch=batches[i];
                    if(!batch.terrain&&std::find(ownGlass.begin(),ownGlass.end(),i)==ownGlass.end()){
                        const unsigned mask=pointMask(batch.boundsLow,batch.boundsHigh,clip);pointBatchMasks[i]=static_cast<unsigned char>(mask);
                        pointAppendCandidates(pointStaticCandidates,i,mask,rebuildMask&faces);}
                }
            }
            for(size_t i=0;i<pointLiveBatches.size();++i){const auto& batch=pointLiveBatches[i];
                pointAppendCandidates(pointLiveCandidates,i,pointMask(batch.low,batch.high,clip),faces);}
            const auto* nativeFixture=ownGlass.empty()?equipmentLights.fixture(light.sourceId):nullptr;
            replayBoundsJoin();
            if(withReplays)for(size_t i=0;i<replays.size();++i){const auto& bounds=replays[i]->pointBounds;
                {PointReplayRuns runs;const bool owned=!ownGlass.empty()?pointOwnShell(*replays[i],ownShell,runs):(nativeFixture&&pointOwnNativeShell(*replays[i],light,runs,nativeFixture));if(owned){
                    if(runs.empty())continue;replayRuns.emplace(i,std::move(runs));}}
                const unsigned mask=NorthlightReplayBounds::outsideSphere(bounds,pointSlot().selected.position,pointSlot().selected.attenuationEnd)?0:
                    clip.mask(bounds.low,bounds.high,bounds.valid);
                pointAppendCandidates(pointReplayCandidates,i,mask,faces);
            }
        }catch(...){return pointCheck(E_OUTOFMEMORY,"cube candidate allocation");}
        bool bound=false;
        for(unsigned face=0;face<6;++face){
            if(!(faces&(1u<<face)))continue; /* a later frame of this face cycle */
            const unsigned bit=1u<<face;
            const bool staticOnly=pointTerrainCandidates[face].empty()&&
                (!liveTerrainIndexCount||pointLiveCandidates[face].empty())&&pointReplayCandidates[face].empty();
            if(!(rebuildMask&bit)){
                if(pointSlot().faceKnown[face]&&pointSlot().faceSerial[face]!=meshGeneration){pointSlot().faceSerial[face]=meshGeneration;++pointFacesKept;}
                if(staticOnly&&(pointSlot().staticOnlyFaces&bit)){
                    pointSlot().faceWeights[face]=0;++pointFacesReused;continue;
                }
            }
            // A wholly reused cube needs no device state changes. Unbind its
            // samplers only when a face will actually become a render target.
            if(!bound){
                for(unsigned i=0;i<13;++i)if(!pointCheck(d->SetTexture(i,nullptr),"unbind cube samplers"))return false;
                for(unsigned i=1;i<4;++i)if(!pointCheck(d->SetRenderTarget(i,nullptr),"unbind cube MRT"))return false;
                for(unsigned i=0;i<4;++i)if(!pointCheck(d->SetStreamSourceFreq(i,1),"cube stream frequency"))return false;
                if(!pointCheck(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE),"cube blend")||
                   !pointCheck(d->SetRenderState(D3DRS_COLORWRITEENABLE,15),"cube color mask"))return false;
                bound=true;
            }
            const float* matrix=faceMatrices[face];
            D3DVIEWPORT9 viewport={0,0,PointResolution,PointResolution,0,1};
            const float sphere[4]={pointSlot().selected.attenuationEnd*pointSlot().selected.attenuationEnd,1.f/PointResolution,0,0}; /* LocalShadowPS: casters within range only */
            bool materialKnown=false;IDirect3DTexture9* materialTexture=nullptr;float materialCutoff=0;
            const auto bindMaterial=[&](unsigned index){
                const float cutoff=uploadedAlphaCutoffs[index]<=0?-1.f:uploadedAlphaCutoffs[index];
                auto* texture=cutoff<0?nullptr:materials[index];
                if(!materialKnown||materialTexture!=texture)if(!pointCheck(d->SetTexture(0,texture),"cube alpha texture"))return false;
                if(!materialKnown||materialCutoff!=cutoff){const float value[]={0,0,0,cutoff};
                    if(!pointCheck(d->SetPixelShaderConstantF(0,value,1),"cube material"))return false;}
                materialKnown=true;materialTexture=texture;materialCutoff=cutoff;return true;
            };
            auto bindFace=[&](IDirect3DSurface9* target,const char* what){
                return pointCheck(d->SetDepthStencilSurface(nullptr),"unbind cube depth")&&pointCheck(d->SetRenderTarget(0,target),what)&&
                    pointCheck(d->SetDepthStencilSurface(pointDepth),"cube depth target")&&pointCheck(d->SetViewport(&viewport),"cube viewport")&&
                    pointCheck(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0),"clear cube")&&
                    pointCheck(d->SetVertexDeclaration(shadowDecl),"cube declaration")&&pointCheck(d->SetVertexShader(pointShadowVS),"cube static VS")&&
                    pointCheck(d->SetVertexShaderConstantF(0,matrix,4),"cube face matrix")&&pointCheck(d->SetPixelShader(pointShadowPS),"cube static PS")&&pointCheck(d->SetPixelShaderConstantF(1,sphere,1),"cube sphere")&&
                    pointCheck(d->SetStreamSource(0,vertices,0,sizeof(NorthlightGI::WorldVertex)),"cube static vertices")&&pointCheck(d->SetIndices(indices),"cube static indices")&&
                    pointCheck(d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP),"cube alpha U")&&pointCheck(d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP),"cube alpha V");
            };
            if(rebuildMask&(1u<<face)){
            if(pointStaticCandidates[face].empty()){drop(pointSlot().cacheSurface[face]);drop(pointSlot().cacheFace[face]);}
            else {
            if(!pointCacheResources(face)||!bindFace(pointSlot().cacheSurface[face],"cube cache target"))return false;
            UINT boundPage=UINT_MAX;
            for(size_t at=0;at<pointStaticCandidates[face].size();){const auto batch=pointBatchRun(pointStaticCandidates[face],at,face);
                if(batch.material>=materials.size()||batch.material>=uploadedAlphaCutoffs.size())return pointCheck(E_FAIL,"cube material range");
                const bool opaque=uploadedAlphaCutoffs[batch.material]<=0;
                NorthlightPointTransmissionFilter filter;
                if(!pointCheck(filter.begin(d,batch.pointOnly&&!opaque),"cube transmission filter"))return false;
                // The static VS has no COLOR output; coverage comes from the texture alone.
                if(!bindMaterial(batch.material)||
                   !bindMeshPage(batch,boundPage)||!pointCheck(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,batch.minVertex,batch.vertexCount,batch.start,batch.count),"cube static draw"))return false;
                if(!pointCheck(filter.restore(),"restore cube alpha filter"))return false;
                ++pointDraws;++pointWork.statics;pointWork.triangles+=batch.count;
            }
            }
            const auto* digests=pointFaceDigests(pointSlot().selected);
            pointSlot().faceContent[face]=digests?digests[face]:NorthlightLocalShadowSignature::Digest{};pointSlot().faceKnown[face]=digests!=nullptr;pointSlot().faceSerial[face]=meshGeneration;}
            if(rebuildMask&(1u<<face)){++pointFacesRebuilt;++pointWork.rebuilt;}
            pointSlot().staticOnlyFaces&=~bit;
            const unsigned faceDraws=pointDraws,faceMerged=pointBatchesMerged;
            // Dynamic casters of this frame into the scratch face, plus cached-mesh
            // terrain, including parts absent from the camera-culled native draws.
            if(!staticOnly){
            if(!bindFace(pointScratchSurface,"cube scratch target"))return false;
            UINT terrainBoundPage=UINT_MAX;
            for(size_t at=0;at<pointTerrainCandidates[face].size();){const auto batch=pointBatchRun(pointTerrainCandidates[face],at,face);
                if(batch.material>=materials.size()||batch.material>=uploadedAlphaCutoffs.size())return pointCheck(E_FAIL,"cube material range");
                if(!bindMaterial(batch.material)||
                   !bindMeshPage(batch,terrainBoundPage)||!pointCheck(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,batch.minVertex,batch.vertexCount,batch.start,batch.count),"cube terrain draw"))return false;
                ++pointDraws;++pointWork.terrain;pointWork.triangles+=batch.count;
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
                    if(!pointCheck(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,liveTerrainGPU.vertexCapacity(),UINT(liveIndexBase+batch.start),batch.count),"cube live draw"))return false;++pointDraws;++pointWork.live;pointWork.triangles+=batch.count;
                }
                if(!pointCheck(d->SetVertexDeclaration(shadowDecl),"cube restore terrain declaration"))return false;
            }
            if(withReplays){
            float rows[16];NorthlightWorldMath::replayProjection(context.inverseView,matrix,rows);
            NorthlightReplayPoses::Pass<BOOL> poseConstants;
            NorthlightReplayDrawState::Cache replayBindings(d);
            // LocalShadowPS reads patched TEXCOORD7 and writes perspective z/w;
            // the directional replay PS assumes orthographic w=1 and is invalid here.
            for(size_t at=0;at<pointReplayCandidates[face].size();){const size_t index=pointReplayCandidates[face][at];const auto& p=replays[index];
                if(!pointCheck(replayBindings.geometry(*p),"cube replay geometry state"))return false;
                if(!poseConstants.prepare(*p,rows,matrix))return false;
                const float* desired=poseConstants.desired();
                auto f=poseConstants.floats;auto b=poseConstants.booleans;auto i=poseConstants.integers;
                if(f.count&&!pointCheck(d->SetVertexShaderConstantF(f.first,desired+4*f.first,f.count),"cube replay float constants"))return false;
                if(b.count&&!pointCheck(d->SetVertexShaderConstantB(b.first,p->bools+b.first,b.count),"cube replay bool constants"))return false;
                if(i.count&&!pointCheck(d->SetVertexShaderConstantI(i.first,p->ints+4*i.first,i.count),"cube replay integer constants"))return false;
                if(!pointCheck(replayBindings.material(*p),"cube replay material state"))return false;
                const auto split=replayRuns.find(index);
                if(split!=replayRuns.end()){
                    ++at;
                    for(const auto& run:split->second){const UINT start=p->start+run.first*(p->type==D3DPT_TRIANGLELIST?3:1);
                        const HRESULT hr=p->indexed?d->DrawIndexedPrimitive(p->type,p->base,p->min,p->vertices,start,run.second):d->DrawPrimitive(p->type,start,run.second);
                        if(!pointCheck(hr,"cube split emitter draw"))return false;++pointDraws;++pointWork.split;pointWork.triangles+=run.second;}
                }else{
                    const auto run=pointReplayRun(pointReplayCandidates[face],at,replayRuns,replayBindings.skipping());
                    const HRESULT hr=p->indexed?d->DrawIndexedPrimitive(p->type,p->base,run.min,run.vertices,run.start,run.count):d->DrawPrimitive(p->type,run.start,run.count);
                    if(!pointCheck(hr,"cube animated draw"))return false;++pointDraws;++pointWork.replay;pointWork.triangles+=run.count;
                }
            }
            } /* withReplays */
            } /* dynamic candidates */
            // Preserve the original logical workload weights: coalescing must
            // not change the configured partial-face refresh groups/order.
            pointSlot().faceWeights[face]=pointDraws-faceDraws+pointBatchesMerged-faceMerged;
            // With no dynamic casters, min(cache, cache) copies the static face
            // without clearing or rendering a scratch face. Retain it until it changes.
            if(!pointCheck(d->SetDepthStencilSurface(nullptr),"unbind cube depth")||!pointCheck(d->SetRenderTarget(0,pointSlot().faces[face]),"cube target")||
               !pointCheck(d->SetViewport(&viewport),"cube union viewport"))return false;
            d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
            d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetStreamSourceFreq(0,1);
            d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
            auto* staticFace=pointSlot().cacheFace[face]?pointSlot().cacheFace[face]:pointEmpty;
            if(!pointCheck(d->SetTexture(0,staticOnly?staticFace:pointScratch),"cube union scratch")||!pointCheck(d->SetTexture(1,staticFace),"cube union cache"))return false;
            for(unsigned sampler=0;sampler<2;++sampler){d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(sampler,D3DSAMP_SRGBTEXTURE,FALSE);}
            float unionConstants[2][4]={{0,0,0,float(PointResolution)},{1.f/PointResolution,1.f/PointResolution,0,0}};
            if(!pointCheck(d->SetPixelShaderConstantF(0,&unionConstants[0][0],2),"cube union constants")||!pointCheck(d->SetPixelShader(unionPS),"cube union shader")||
               !pointCheck(quad(PointResolution,PointResolution),"cube union"))return false;
            ++pointWork.unions;
            if(staticOnly)pointSlot().staticOnlyFaces|=bit;
            d->SetTexture(0,nullptr);d->SetTexture(1,nullptr);
            d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
            d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        }
        if(rebuild){pointSlot().cacheValid=true;pointSlot().cacheSource=pointSlot().selected.sourceId;} /* cycling: already valid, its remaining faces wait for their frame */
        if(cycling&&!pointSlot().faceCycle.advance()){pointSlot().ready=true;++pointUpdates;return true;} /* stale until the last group */
        if(cycling)pointSlot().schedule.commit(pointSlot().faceCycle.startedAt,pointSlot().selected,pointSlot().faceCycle.generation);
        else pointSlot().schedule.commit(updateAt,pointSlot().selected,meshGeneration);
        pointSlot().ready=true;++pointUpdates;
        if(frames==0||(frames%600==0&&NorthlightDiagnostics::enabled()))logf("POINT source=%llu kind=%u range=%.2f faces=%u size=256 draws=%u reusedFaces=%u culled=%u replayBounds=%u boundVertices=%zu boundOperations=%zu",(unsigned long long)pointSlot().selected.sourceId,pointSlot().selected.kind,pointSlot().selected.attenuationEnd,unsigned(__builtin_popcount(faces)),pointDraws,pointFacesReused,pointCulled,pointBoundsValid,pointBoundVertices,pointBoundOperations);
        return true;
    }
    void renderPointShadows(const NorthlightLocalLightSelection::Selection& lights,bool fresh,bool withReplays){
        ++nativeCubeRevision;
        pointReady=false;pointWork={};
        if(!active)return;
        if(pointSelectionMap!=active->map){pointSelection.reset();clearPointKeeps();pointSelectionMap=active->map;}
        const auto picks=pointSelection.update(lights.sources.data(),lights.count,context.camera,quality.pointShadowLimit,GetTickCount(),&localScissorView,float(quality.pointShadowDistance));
        unsigned selected=0,totalDraws=0,totalCulled=0;const unsigned updatesBefore=pointUpdates,reusesBefore=pointReuses;
        unsigned wanted=0;for(const auto& pick:picks)wanted+=pick.index>=0;
        const unsigned limit=std::min(quality.pointShadowLimit,NorthlightPointShadow::MaxLights);
        // Every keep uses a spare configured slot, never an extra light budget;
        // eight static-only keeps cap idle targets at 12 MiB, zero under pressure.
        const unsigned keepLimit=memoryPressure?0:std::min(unsigned(pointKeeps.size()),limit-std::min(limit,wanted));
        for(unsigned i=0;i<pointKeeps.size();++i){auto& keep=pointKeeps[i];keep.wanted=false;
            if(i>=keepLimit){releasePointSlot(keep.slot);keep.age=0;continue;}
            if(keep.slot.cacheValid)for(const auto& pick:picks)if(pick.index>=0&&lights.sources[pick.index].sourceId==keep.slot.cacheSource){keep.wanted=true;break;}
        }
        // Release outgoing resources before allocating replacements, even when
        // the free slot occurs earlier in the array than the retired slot.
        for(pointIndex=0;pointIndex<pointSlots.size();++pointIndex){pointSlot().ready=false;const auto& pick=picks[pointIndex];
            if(pick.index<0||pointSlot().map!=active->map||pointSlot().selected.sourceId!=lights.sources[pick.index].sourceId)keepPointSlot(pointSlot(),keepLimit);}
        for(pointIndex=0;pointIndex<pointSlots.size();++pointIndex){const auto& pick=picks[pointIndex];if(pick.index<0)continue;
            ++selected;
            restorePointSlot(lights.sources[pick.index]);pointSlot().selectionGain=pick.gain;
            renderPointShadow(lights.sources[pick.index],fresh,withReplays);pointReady|=pointSlot().ready;
            totalDraws+=pointDraws;totalCulled+=pointCulled;
            if(!pointSlot().ready)pointSelection.defer(pointIndex);
        }
        pointIndex=0;
        pointDraws=totalDraws;pointCulled=totalCulled;pointLogSchedule(pointUpdates==updatesBefore&&pointReuses!=reusesBefore);
        pointWork.selected=selected;
        if(pointReportedLimit!=quality.pointShadowLimit||(frames%600==0&&NorthlightDiagnostics::enabled())){
            unsigned ready=0,resident=0,kept=0,staticFaces=0;for(const auto& p:pointSlots){ready+=p.ready;resident+=p.cube!=nullptr;for(auto* t:p.cacheFace)staticFaces+=t!=nullptr;}for(const auto& k:pointKeeps){kept+=k.slot.cacheValid;for(auto* t:k.slot.cacheFace)staticFaces+=t!=nullptr;}
            logf("POINT pool limit=%u selected=%u ready=%u cubeSlots=%u estimatedMiB=%.1f staticKeeps=%u staticFaces=%u",quality.pointShadowLimit,selected,ready,resident,resident*1.5+staticFaces*.25+(pointScratch?.25:0)+(pointDepth?.25:0),kept,staticFaces);
            pointReportedLimit=quality.pointShadowLimit;
        }
    }
    bool pointSoftShaders(){
        if(!pointSoftTried){
            pointSoftTried=true;
            if(FAILED(d->CreatePixelShader(kLocalShadowResolveShader,&pointSoftResolvePS))||FAILED(d->CreatePixelShader(kLocalLightingSoftShader,&pointSoftLightingPS))){
                drop(pointSoftResolvePS);drop(pointSoftLightingPS);logf("POINT PCSS unavailable; ordinary filtering retained");
            }
        }
        return pointSoftResolvePS&&pointSoftLightingPS;
    }
    bool renderPointLighting(IDirect3DTexture9* depth,IDirect3DTexture9* waterMask,UINT w,UINT h,float nearZ,float farZ,float minZ,float maxZ,float strength,float sunlitCut,float shadowStrength,float sunEnergy){
        if(!pointSlot().ready)return false;
        float constants[12][4]={};constants[0][0]=1.f/w;constants[0][1]=1.f/h;constants[0][2]=nearZ;constants[0][3]=farZ;
        constants[1][0]=projection[0];constants[1][1]=projection[1];constants[1][2]=projection[2];constants[1][3]=minZ;
        constants[2][0]=1.f/(maxZ-minZ);constants[2][1]=waterMask?1.f:0.f;constants[2][2]=pointSlot().selectionGain;constants[2][3]=.025f;
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
           !pointCheck(d->SetPixelShader(pointLightingPS),"local lighting PS")||
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
        if(quality.softShadows&&pointSoftShaders()&&softShadowResources(w,h)){
            IDirect3DSurface9* destination=nullptr;
            if(SUCCEEDED(d->GetRenderTarget(0,&destination))&&destination){
                // s1 holds the cube here, not the shared resolve target. Every
                // pixel consumed below is written, including water/sky rejects.
                bool resolved=SUCCEEDED(d->SetRenderTarget(0,softVisibilitySurface))&&
                    SUCCEEDED(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE))&&
                    SUCCEEDED(d->SetRenderState(D3DRS_COLORWRITEENABLE,15))&&
                    SUCCEEDED(d->SetPixelShader(pointSoftResolvePS))&&SUCCEEDED(quad(w/2,h/2));
                if(FAILED(d->SetRenderTarget(0,destination))){drop(destination);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);return false;}
                d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_COLORWRITEENABLE,7);
                if(resolved)resolved=SUCCEEDED(d->SetTexture(1,softVisibility))&&SUCCEEDED(d->SetPixelShader(pointSoftLightingPS));
                if(!resolved){d->SetTexture(1,pointSlot().cube);d->SetPixelShader(pointLightingPS);}
                // Optional failures never invalidate the completed cube/cache.
            }
            drop(destination);
        }
        const bool drawn=pointCheck(quad(w/2,h/2),"local lighting pass");
        d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);return drawn;
    }
