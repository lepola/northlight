    // 0.3.172 rigid memory (rigid_memory.h): render-thread side. Observation of this capture
    // frame's rigid groups (before retainSelected: every captured group, selected or not, is a
    // body or a candidate), copies of remembered replays, and their injection after it. The copy
    // owns its constant bank and holds the immutable snapshot, never a game VB/IB (uploadReplay
    // binds the renderer's own streams); it holds the replacement and original shaders, the
    // declaration and, for cutout draws only, the texture: released on clear().
    template<class T> struct RigidRef {
        T* p=nullptr;RigidRef()=default;explicit RigidRef(T* x):p(x){if(p)p->AddRef();}
        RigidRef(RigidRef&& o)noexcept:p(o.p){o.p=nullptr;}
        RigidRef& operator=(RigidRef&& o)noexcept{if(this!=&o){if(p)p->Release();p=o.p;o.p=nullptr;}return *this;}
        RigidRef(const RigidRef&)=delete;RigidRef& operator=(const RigidRef&)=delete;~RigidRef(){if(p)p->Release();}
        void reset(T* x){if(x==p)return;if(x)x->AddRef();if(p)p->Release();p=x;} /* 0.3.176 (S3'): a held pointer keeps its reference */
    };
    struct RigidDraw {
        RigidRef<IDirect3DVertexShader9> shader,originalShader;RigidRef<IDirect3DVertexDeclaration9> decl;RigidRef<IDirect3DBaseTexture9> texture;
        std::shared_ptr<const NorthlightDrawSnapshot::Mesh> shared;std::vector<float> constants;BOOL bools[16]={};int ints[64]={};
        NorthlightShaderConstants::Usage usage;unsigned projectionKind=2;float cutoff=-1;unsigned alphaMode=0;DWORD addressU=D3DTADDRESS_WRAP,addressV=D3DTADDRESS_WRAP;
        D3DPRIMITIVETYPE type=D3DPT_TRIANGLELIST;UINT count=0;bool indexed=false;
    };
    struct RigidPayload {std::vector<RigidDraw> draws;unsigned bone=0;};
    NorthlightRigidMemory::Registry<RigidPayload> rigidMemory;
    std::vector<NorthlightRigidMemory::Observation> rigidObservations;std::vector<float> rigidBodies;
    std::vector<const Replay*> rigidGroupDraws;std::vector<std::pair<size_t,unsigned>> rigidGroups; /* per observation: first copy source in rigidGroupDraws, bone */
    std::unordered_map<IDirect3DVertexShader9*,bool> rigidAudited;
    NorthlightRigidGeometry::PlacementIndex rigidIndex;static constexpr size_t RigidIndexStep=2048; /* ~0.1 ms */
    std::string rigidMap;DWORD rigidMapMs=0;unsigned rigidInjected=0;double rigidObserveMs=0;
    // 0.3.176 (D2): placements the index stepped this frame and (RenderProfile sample frames) its time; -1: not measured.
    size_t rigidIndexItems=0;double rigidIndexMs=-1;
    NorthlightRigidMemory::DrawKeySet rigidDrawKeys; /* (original shader, primitives) of every remembered draw: the drawn test at capture */
    std::vector<NorthlightRigidMemory::Event> rigidEvents;unsigned rigidEventTokens=0,rigidEventLines=0,rigidEventSuppressed=0;DWORD rigidEventRefillMs=0;
    static constexpr unsigned RigidEventsPerSecond=20,RigidEventLines=2000; /* RIGID event lines: rate and session cap */
    void rigidMemoryClear(){rigidMemory.clear();rigidAudited.clear();rigidIndex.reset(nullptr,0);rigidGroupDraws.clear();rigidDrawKeys.clear();}
    void rigidDrawKeysRebuild(){rigidDrawKeys.clear();for(const auto& e:rigidMemory.entries())for(const auto& c:e.payload.draws)rigidDrawKeys.add(c.originalShader.p,c.count);}
    // captureModel, before any rejection: a draw whose (shader, primitives) is remembered. Bone 0's
    // world matrix W0 (the mirror-answered rows of paletteRows()) against the entries' W0.
    void rigidMemoryDrawn(IDirect3DVertexShader9* shader,UINT count){
        float rows[12],w0[12];const auto* program=paletteRows(shader,rows);
        if(!program||program->paletteBase!=31||!NorthlightRigidGeometry::worldBone(rows,context.inverseView,w0))return;
        rigidMemory.markDrawn(w0,[&](const auto& e){for(const auto& c:e.payload.draws)if(c.originalShader.p==shader&&c.count==count)return true;return false;});
    }
    // Audited palette programs (c31.. rows, 3 per bone): the four-weight blend (SkinEnvelope)
    // and the one-influence program (oneBoneTemplate). Cached per original shader.
    const NorthlightActorDeformation::Program* rigidProgram(IDirect3DVertexShader9* shader){
        auto program=actorPrograms.find(shader);if(program==actorPrograms.end())return nullptr;
        auto audited=rigidAudited.find(shader);const auto& p=*program->second;
        if(audited==rigidAudited.end())audited=rigidAudited.emplace(shader,(NorthlightReplayBounds::SkinEnvelope::supports(p)&&p.paletteBase==31)||NorthlightRigidGeometry::oneBoneTemplate(p)).first;
        return audited->second?&p:nullptr;
    }
    static std::uint64_t rigidSceneRevision(const StaticShadow::Snapshot& scene){return scene.revision*1000003ull^scene.mapGeneration;}
    // Static-cache placements of this map's scene, indexed RigidIndexStep per frame while a settled
    // track waits for the screen (rigidMemory.screening()).
    void rigidIndexStep(){
        if(!staticScene||staticScene->map!=lastRequest.map)return;auto& x=rigidIndex;const auto& all=staticScene->placements;
        const bool timed=profileSampled();const auto started=timed?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
        if(x.scene!=staticScene.get()||x.revision!=rigidSceneRevision(*staticScene))x.reset(staticScene.get(),rigidSceneRevision(*staticScene),all.size());
        const size_t first=x.next;
        for(const size_t end=std::min(all.size(),x.next+RigidIndexStep);x.next<end;++x.next){const auto& place=all[x.next];
            if(place.category==1||place.category==3)x.add(place.translation.x,place.translation.y,place.translation.z,std::uint32_t(x.next));}
        x.complete=x.next==all.size();rigidIndexItems+=x.next-first;
        if(timed)rigidIndexMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    }
    // The index is complete for this map's current static scene.
    bool rigidIndexCurrent()const{
        return staticScene&&staticScene->map==lastRequest.map&&rigidIndex.scene==staticScene.get()&&rigidIndex.revision==rigidSceneRevision(*staticScene)&&rigidIndex.complete;}
    // 1: the static cache draws it (a placement at W's origin with W's axes), 0: not (or no scene
    // for this map after 15 s), -1: not known yet.
    int rigidStaticCovered(const NorthlightRigidMemory::Observation& o,DWORD now){
        if(!staticScene||staticScene->map!=lastRequest.map)return now-rigidMapMs<15000?-1:0;const auto& x=rigidIndex;
        if(x.scene!=staticScene.get()||x.revision!=rigidSceneRevision(*staticScene)||!x.complete)return -1;
        return x.find(o.world+9,.05f,[&](std::uint32_t i){const auto& place=staticScene->placements[i];const float t[3]={place.translation.x,place.translation.y,place.translation.z};
            return NorthlightRigidGeometry::staticPlacement(o.world+9,o.world,t,place.matrix);})?1:0;
    }
    // One constant group [first,end) of the captured replays: a non-rigid group gives a body
    // root, a rigid one (every skinned draw shared, audited, one bone) an observation of its
    // selected draws, or (0.3.173 F4) of its captured non-small draws when selection kept none:
    // LiveUnselected, matched only (no copy).
    void rigidObserveGroup(size_t first,size_t end){
        const Replay* head=nullptr;const Replay* selectedHead=nullptr;const Replay* unselectedHead=nullptr;float bone=NAN;bool rigid=true;
        NorthlightRigidMemory::Observation o,u;std::uint64_t shape=NorthlightRigidMemory::ShapeSeed,unselectedShape=NorthlightRigidMemory::ShapeSeed;
        const size_t copies=rigidGroupDraws.size();
        for(size_t k=first;k<end;++k){const auto& p=*replays[k];if(!p.shadowSkinned)continue;if(!head)head=&p;
            if(rigid){float b=NAN;const auto* program=p.shared?rigidProgram(p.originalShader):nullptr;const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
                // 0.3.176 (S2): selection's value when it tested this draw (same program, snapshot and
                // declaration: the same answer); the audit gate (shared, audited program) still applies.
                if(program&&p.boneKnown)b=p.bone;
                else if(program&&declarationCache.get(p.decl,elements,count))b=prepareCaches->bones.bone(*program,p.mesh(),p.shared,p.decl,elements,count);
                rigid=!std::isnan(b)&&(std::isnan(bone)||b==bone);bone=b;}
            if(!p.shadowSelected){if(p.shadowSmall)continue; /* small at capture: never observed */
                if(!unselectedHead)unselectedHead=&p;++u.draws;u.triangles+=p.count;u.bytes+=p.mesh().byteSize();
                NorthlightRigidMemory::mixShape(unselectedShape,p.originalShader,p.decl,p.mesh().vertexCount,p.mesh().primitiveCount,p.mesh().byteSize());continue;}
            if(!selectedHead)selectedHead=&p;
            ++o.draws;o.triangles+=p.count;o.bytes+=p.mesh().byteSize();
            NorthlightRigidMemory::mixShape(shape,p.originalShader,p.decl,p.mesh().vertexCount,p.mesh().primitiveCount,p.mesh().byteSize());
            if(o.draws<=rigidMemory.tuning().maxDraws)rigidGroupDraws.push_back(&p);}
        if(!head){rigidGroupDraws.resize(copies);return;}
        if(!rigid){rigidGroupDraws.resize(copies);auto program=actorPrograms.find(head->originalShader);float root[3];
            if(program!=actorPrograms.end()&&NorthlightActorDeformation::rootWorld(*program->second,head->constants,context.inverseView,root))rigidBodies.insert(rigidBodies.end(),root,root+3);
            return;}
        if(!(bone>=0&&bone<=74&&std::floor(bone)==bone)){rigidGroupDraws.resize(copies);return;}
        if(selectedHead){
            if(!rigidMemory.small(o)||!NorthlightRigidGeometry::worldBone(selectedHead->constants+4*(31+3*unsigned(bone)),context.inverseView,o.world)){rigidGroupDraws.resize(copies);return;}
            o.hasWorld0=NorthlightRigidGeometry::worldBone(selectedHead->constants+4*31,context.inverseView,o.world0);
            o.shape=shape;rigidObservations.push_back(o);rigidGroups.push_back({copies,unsigned(bone)});}
        else if(unselectedHead&&rigidMemory.small(u)&&NorthlightRigidGeometry::worldBone(unselectedHead->constants+4*(31+3*unsigned(bone)),context.inverseView,u.world)){
            u.shape=unselectedShape;u.selected=false;rigidObservations.push_back(u);rigidGroups.push_back({copies,unsigned(bone)});}
    }
    static void rigidFill(const Replay& p,RigidDraw& c){
        c.shader.reset(p.shader);c.originalShader.reset(p.originalShader);c.decl.reset(p.decl);
        c.texture.reset(p.cutoff>=0?p.texture:nullptr); /* opaque: stage 0 is irrelevant (replay_draw_state.h) */
        c.shared=p.shared;c.constants.assign(p.constants,p.constants+1024);std::memcpy(c.bools,p.bools,sizeof c.bools);std::memcpy(c.ints,p.ints,sizeof c.ints);
        c.usage=p.constantUsage;c.projectionKind=p.projectionKind;c.cutoff=p.cutoff;c.alphaMode=p.alphaMode;c.addressU=p.addressU;c.addressV=p.addressV;c.type=p.type;c.count=p.count;c.indexed=p.indexed;
    }
    size_t rigidGroupEnd(size_t n)const{return n+1<rigidGroups.size()?rigidGroups[n+1].first:rigidGroupDraws.size();}
    RigidPayload rigidCopy(size_t n){
        RigidPayload out;out.bone=rigidGroups[n].second;const size_t end=rigidGroupEnd(n);
        out.draws.reserve(end-rigidGroups[n].first);
        for(size_t k=rigidGroups[n].first;k<end;++k){out.draws.emplace_back();rigidFill(*rigidGroupDraws[k],out.draws.back());}
        return out;
    }
    // 0.3.176 (S3'): a Refresh rewrites the entry's copy in place when it has as many draws (the
    // constant banks into their capacity, references taken only for changed pointers: no allocation);
    // otherwise a fresh copy replaces it. Either way the payload equals rigidCopy(n).
    void rigidRefresh(size_t n,RigidPayload& out){
        const size_t first=rigidGroups[n].first,end=rigidGroupEnd(n);
        if(out.draws.size()!=end-first){RigidPayload fresh=rigidCopy(n);out=std::move(fresh);return;}
        out.bone=rigidGroups[n].second;for(size_t k=first;k<end;++k)rigidFill(*rigidGroupDraws[k],out.draws[k-first]);
    }
    // A body root on a static-cache placement origin is a doodad (a lantern, a banner): it holds
    // nothing. Unknown (no scene for this map, index incomplete): it counts as a body.
    bool rigidStaticBody(const float* root){
        if(!rigidIndexCurrent())return false;const auto& x=rigidIndex;
        return x.find(root,rigidMemory.tuning().staticBodyTolerance,[](std::uint32_t){return true;}); /* the index holds categories 1 and 3 only */
    }
    // Before retainSelected: observe, decide, copy the replays of remembered groups.
    void rigidMemoryObserve(){
        rigidInjected=0;rigidObserveMs=0;rigidIndexItems=0;rigidIndexMs=-1;
        if(!effects.shadows){rigidMemoryClear();return;}
        const auto started=std::chrono::steady_clock::now();const DWORD now=GetTickCount();
        if(lastRequest.map!=rigidMap){rigidMemoryClear();rigidMemory.resetStats();admission.reset();rigidMap=lastRequest.map;rigidMapMs=now;}
        rigidMemory.events(NorthlightDiagnostics::enabled());
        rigidObservations.clear();rigidBodies.clear();rigidGroupDraws.clear();rigidGroups.clear();
        for(size_t i=0;i<replays.size();){const unsigned group=replays[i]->constantGroup;size_t j=i;
            while(j<replays.size()&&replays[j]->constantGroup==group)++j;
            rigidObserveGroup(i,j);i=j;}
        rigidBodies.insert(rigidBodies.end(),gatedBodies.begin(),gatedBodies.end()); /* 0.3.211 (task 31): bodies the radius gate did not capture still hold their props */
        const float* pivot=actorShadowOriginValid?actorShadowOrigin:context.camera;
        rigidMemory.frame(rigidObservations,rigidBodies.data(),rigidBodies.size()/3,now,pivot,context.inverseView,projection,!captureShortfall,
            [&](size_t n){return rigidStaticCovered(rigidObservations[n],now);},[&](const float* root){return rigidStaticBody(root);});
        for(size_t n=0;n<rigidObservations.size();++n){const auto& o=rigidObservations[n];
            if(o.action==NorthlightRigidMemory::Observation::Refresh)rigidMemory.refresh(o,[&](RigidPayload& payload){rigidRefresh(n,payload);});
            else if(o.action!=NorthlightRigidMemory::Observation::None)rigidMemory.store(o,rigidCopy(n),now);}
        rigidDrawKeysRebuild(); /* after store: the drawn test of the next capture frame */
        // The index is stepped while a settled track waits for the screen, and (0.3.173) while it is
        // not complete for this scene and tracks exist (all within range): the doodad-body test of
        // held needs it too, also after a static-scene revision change.
        if(rigidMemory.screening()||(rigidMemory.stats().tracks&&!rigidIndexCurrent()))rigidIndexStep();
        rigidGroupDraws.clear();
        rigidObserveMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    }
    // After retainSelected: remembered groups the game did not draw this capture frame join the
    // replays, outside quota, radius and fate, with their bone's rows rebased to this view.
    void rigidMemoryInject(){
        const auto started=std::chrono::steady_clock::now();
        if(effects.shadows&&!rigidMemory.entries().empty()){
            unsigned group=0;for(const auto& p:replays)group=std::max(group,p->constantGroup);
            rigidMemory.forAbsent([&](auto& e){
                float rows[12];if(replays.size()+e.payload.draws.size()>=4096||!NorthlightRigidMemory::rebase(e.world,context.inverseView,rows))return;
                for(const auto& c:e.payload.draws){std::unique_ptr<Replay,ReplayRecycle> p(acquireReplay().release(),ReplayRecycle{this});
                    NorthlightReplayCaptureConstants::reset(*p);
                    p->shader=c.shader.p;p->shader->AddRef();p->originalShader=c.originalShader.p;p->originalShader->AddRef();p->decl=c.decl.p;p->decl->AddRef();
                    p->texture=c.texture.p;if(p->texture)p->texture->AddRef();p->shared=c.shared;
                    std::memcpy(p->constantStorage,c.constants.data(),sizeof p->constantStorage);std::memcpy(p->boolStorage,c.bools,sizeof p->boolStorage);std::memcpy(p->intStorage,c.ints,sizeof p->intStorage);
                    std::memcpy(p->constantStorage+4*(31+3*e.payload.bone),rows,sizeof rows);
                    p->constantUsage=c.usage;p->projectionKind=c.projectionKind;p->cutoff=c.cutoff;p->alphaMode=c.alphaMode;p->addressU=c.addressU;p->addressV=c.addressV;
                    p->type=c.type;p->base=0;p->min=0;p->vertices=p->mesh().vertexCount;p->start=0;p->count=c.count;p->indexed=c.indexed;
                    p->captureRootValid=false;p->selfShadow=false;std::memcpy(p->anchor,e.world+9,sizeof p->anchor);p->anchorValid=true;p->pointBounds={};p->gpuCached=false;p->shadowSkinned=p->shadowSelected=true;p->shadowSmall=false;p->fateSlot=-1;p->fateClass=NorthlightShadowFate::NotRanked;p->fateDistance=0;
                    p->staticProofMask=p->staticM2Mask=0;p->staticM2Model=nullptr;p->boneKnown=false;p->bone=NAN;p->constantGroup=++group;
                    replays.emplace_back(p.release());++rigidInjected;}});
        }
        if(captureSampled){const auto& s=rigidMemory.stats();const double ms=rigidObserveMs+std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            deferLogf("RIGID memory tracks=%zu entries=%zu injected=%u seen=%zu held=%zu static=%zu mobile=%zu droppedInView=%llu droppedRange=%llu droppedTeleport=%llu droppedMoved=%llu droppedUnseen=%llu evicted=%llu remembered=%llu indexComplete=%d bytes=%zu ms=%.3f drawn=%zu drawnNotCaptured=%zu liveUnselected=%zu notDrawn=%zu drawnMoved=%llu rememberGateFar=%llu tracksForgotten=%llu refreshSkipped=%llu sortMs=%.3f eventsSuppressed=%u indexMs=%.3f indexItems=%zu",
                s.tracks,s.entries,rigidInjected,s.seen,s.held,s.statics,s.mobile,(unsigned long long)s.droppedInView,(unsigned long long)s.droppedRange,(unsigned long long)s.droppedTeleport,
                (unsigned long long)s.droppedMoved,(unsigned long long)s.droppedUnseen,(unsigned long long)s.evicted,(unsigned long long)s.remembered,int(rigidIndex.complete),s.bytes,ms,
                s.drawn,s.drawnNotCaptured,s.liveUnselected,s.notDrawn,(unsigned long long)s.droppedDrawnMoved,(unsigned long long)s.rememberGateFar,(unsigned long long)s.tracksForgotten,(unsigned long long)s.refreshSkipped,s.sortMs,rigidEventSuppressed,rigidIndexMs,rigidIndexItems);}
        // F6 (Diagnostics=1): RIGID event lines, at most RigidEventsPerSecond, RigidEventLines a session.
        if(NorthlightDiagnostics::enabled()){rigidMemory.takeEvents(rigidEvents);const DWORD now=GetTickCount();
            for(const auto& v:rigidEvents){if(now-rigidEventRefillMs>=1000){rigidEventRefillMs=now;rigidEventTokens=RigidEventsPerSecond;}
                if(!rigidEventTokens||rigidEventLines>=RigidEventLines){++rigidEventSuppressed;continue;}--rigidEventTokens;++rigidEventLines;
                float eye=0,pivot=0;const float* p=actorShadowOriginValid?actorShadowOrigin:context.camera;
                for(unsigned k=0;k<3;++k){eye+=(v.at[k]-context.camera[k])*(v.at[k]-context.camera[k]);pivot+=(v.at[k]-p[k])*(v.at[k]-p[k]);}
                deferLogf("RIGID event %s reason=%s state=%s shape=%016llx at=(%.1f %.1f %.1f) eye=%.1f pivot=%.1f body=%d bodyAt=(%.1f %.1f %.1f) bodyDistance=%.1f ratio=%.2f",
                    NorthlightRigidMemory::eventName(v.kind),NorthlightRigidMemory::reasonName(v.reason),NorthlightRigidMemory::stateName(v.state),(unsigned long long)v.shape,
                    v.at[0],v.at[1],v.at[2],std::sqrt(eye),std::sqrt(pivot),int(v.hasBody),v.body[0],v.body[1],v.body[2],v.bodyDistance,v.ratio);}}
    }
