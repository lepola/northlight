// Production screen effects must survive an unavailable geometry publication.
#undef NDEBUG
#define NORTHLIGHT_REPLAY_STAND 1
#include "../src/proxy/renderer.cpp"
#include <cassert>
#include <fstream>
struct WorldScreenEffectsTest {
    static void retryHdrCombined(WorldRenderer& w){drop(w.hdrCombinedPS);w.hdrCombineTried=false;}
    static bool hdrCombined(const WorldRenderer& w){return w.hdrCombinedPS!=nullptr;}
    static void fixtureContext(WorldRenderer& w){
        // A real frame reacquires the camera and lighting from a world draw after endFrame().
        w.valid=true;w.traceContext=2;
        for(unsigned i=0;i<16;++i)w.context.view[i]=w.context.inverseView[i]=(i%5==0)?1.f:0.f;
        for(unsigned i=0;i<3;++i){w.context.camera[i]=0;w.context.direct[i]=.6f;w.context.ambient[i]=.25f;}
        w.context.lightDirection[0]=.4f;w.context.lightDirection[1]=.2f;w.context.lightDirection[2]=-.8944272f;
        w.projection[0]=w.projection[1]=w.projection[2]=1;
        w.sourceDirections[0]={.4f,.2f,-.8944272f};w.sourceDirections[1]={0,0,1};
        w.sourceColors[0]=w.sourceGlowColors[0]={.6f,.5f,.4f};w.sourceColors[1]=w.sourceGlowColors[1]={0,0,0};w.sourceWeights[0]=1;w.sourceWeights[1]=0;
    }
    static void sceneOwnership(WorldRenderer& w,IDirect3DDevice9* d){
        auto scene=w.gpuScene;const auto originalLimit=w.captureSources().sharedSourceLimit();
        assert(scene->device==d&&scene->decodedSources&&scene->captureSources==w.captureSources().sharedSources());
        assert(&w.staticCasters==&scene->staticCasters&&&w.meshStore==&scene->meshStore&&&w.meshBuffers==&scene->meshBuffers&&&w.liveTerrainGPU==&scene->terrain&&&w.terrainBoundsCache==&scene->terrainSources);
        struct Views {IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;NorthlightReplayGPU::Placement at;~Views(){for(auto& p:vb)drop(p);drop(ib);}} material,shadow;
        auto mesh=std::make_shared<NorthlightDrawSnapshot::Mesh>();mesh->streams[0].stride=12;mesh->streams[0].bytes.resize(36);mesh->indices={0,1,2};mesh->vertexCount=3;mesh->primitiveCount=1;
        mesh->sourceIdentity=NorthlightSourceIdentity::make({0x7363656e65,1});const auto bytes=mesh->byteSize();
        auto admit=[](size_t){return true;};
        {
            NorthlightAutoBump materials(d,scene);
            assert(scene->normalMaps&&scene->displacement&&materials.captureSources().sharedSources()==scene->captureSources);
            assert(materials.captureSources().sharedSourceParticipants()==3&&materials.captureSources().sharedSourceLimit()==originalLimit+(48u<<20));
            scene->setFrame(20);assert(!scene->materials.bind(d,mesh,material.vb,material.ib,admit,material.at));
            scene->setFrame(21);assert(!scene->materials.bind(d,mesh,material.vb,material.ib,admit,material.at));assert(scene->materials.commitPending());
            assert(scene->materials.bind(d,mesh,material.vb,material.ib,admit,material.at)&&scene->shadows.bind(d,mesh,shadow.vb,shadow.ib,admit,shadow.at));
            assert(scene->native.bytes()==bytes&&material.vb[0]==shadow.vb[0]&&material.ib==shadow.ib);
            materials.reset();assert(!scene->materials.bytes()&&scene->shadows.bytes()==bytes&&scene->native.bytes()==bytes);
            auto reconstructed=std::make_shared<NorthlightDrawSnapshot::Mesh>(*mesh);mesh.reset();
            assert(scene->shadows.bind(d,reconstructed,shadow.vb,shadow.ib,admit,shadow.at)&&shadow.vb[0]==material.vb[0]);
        }
        assert(w.captureSources().sharedSourceLimit()==originalLimit&&w.captureSources().sharedSourceParticipants()==1);
        assert(scene->native.bytes()==bytes);scene->shadows.clear();assert(!scene->native.bytes()&&!scene->native.chargedBytes());
        bool rejected=false;try{NorthlightGpuScene::forDevice(reinterpret_cast<IDirect3DDevice9*>(uintptr_t(1)),scene);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
        std::puts("GPU_SCENE_OWNERSHIP_PASS actual_world_materials_displacement one_source_bank unchanged_caps one_upload consumer_reset reconstructed_source typed_views foreign_device_rejected");
    }
    static void pageIndexImage(WorldRenderer& w,IDirect3DDevice9* d){
        struct Vertex {float x,y,z,rhw;DWORD color;};
        const Vertex v[]={{5,5,.5f,1,0xffff0000},{58,5,.5f,1,0xff00ff00},{58,58,.5f,1,0xff0000ff},{5,58,.5f,1,0xffffffff}};
        const uint16_t narrow[]={0,1,2,0,2,3};const uint32_t wide[]={0,1,2,0,2,3};
        NorthlightMeshPageKey::Key key;key.lo=123;key.hi=456;key.vertexBytes=sizeof(v);key.indexBytes=sizeof(narrow);
        assert(w.meshStore.beginStaging({key}));w.syncMeshBuffers();const auto id=w.meshStore.staging().uploadBuffer[0];assert(w.meshStore.markWriting(id));
        assert(w.acquireMeshPage(id,false,sizeof(narrow)));auto* ib16=w.meshBuffers[id].ib;D3DINDEXBUFFER_DESC desc{};assert(SUCCEEDED(ib16->GetDesc(&desc))&&desc.Format==D3DFMT_INDEX16);
        void* ptr=nullptr;assert(SUCCEEDED(ib16->Lock(0,sizeof(narrow),&ptr,0))&&ptr);std::memcpy(ptr,narrow,sizeof(narrow));assert(SUCCEEDED(ib16->Unlock()));
        IDirect3DIndexBuffer9* ib32=nullptr;IDirect3DVertexBuffer9* vb=nullptr;IDirect3DSurface9 *target=nullptr,*read=nullptr,*old=nullptr,*oldDepth=nullptr;IDirect3DStateBlock9* state=nullptr;
        assert(SUCCEEDED(d->CreateStateBlock(D3DSBT_ALL,&state))&&SUCCEEDED(state->Capture()));assert(SUCCEEDED(d->GetRenderTarget(0,&old)));d->GetDepthStencilSurface(&oldDepth);
        assert(SUCCEEDED(d->CreateIndexBuffer(sizeof(wide),0,D3DFMT_INDEX32,D3DPOOL_MANAGED,&ib32,nullptr)));assert(SUCCEEDED(ib32->Lock(0,sizeof(wide),&ptr,0))&&ptr);std::memcpy(ptr,wide,sizeof(wide));assert(SUCCEEDED(ib32->Unlock()));
        assert(SUCCEEDED(d->CreateVertexBuffer(sizeof(v),0,0,D3DPOOL_MANAGED,&vb,nullptr)));assert(SUCCEEDED(vb->Lock(0,sizeof(v),&ptr,0))&&ptr);std::memcpy(ptr,v,sizeof(v));assert(SUCCEEDED(vb->Unlock()));
        assert(SUCCEEDED(d->CreateRenderTarget(64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr)));assert(SUCCEEDED(d->CreateOffscreenPlainSurface(64,64,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr)));
        assert(SUCCEEDED(d->SetRenderTarget(0,target))&&SUCCEEDED(d->SetDepthStencilSurface(nullptr)));assert(SUCCEEDED(d->SetVertexShader(nullptr))&&SUCCEEDED(d->SetPixelShader(nullptr))&&SUCCEEDED(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE)));
        assert(SUCCEEDED(d->SetRenderState(D3DRS_ZENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE))&&SUCCEEDED(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_LIGHTING,FALSE)));
        assert(SUCCEEDED(d->SetTexture(0,nullptr))&&SUCCEEDED(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1))&&SUCCEEDED(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE)));assert(SUCCEEDED(d->SetStreamSource(0,vb,0,sizeof(Vertex))));
        std::vector<unsigned char> images[2];for(unsigned mode=0;mode<2;++mode){assert(SUCCEEDED(d->SetIndices(mode?ib16:ib32)));assert(SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0)));assert(SUCCEEDED(d->BeginScene()));assert(SUCCEEDED(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2)));assert(SUCCEEDED(d->EndScene()));assert(SUCCEEDED(d->GetRenderTargetData(target,read)));D3DLOCKED_RECT locked{};assert(SUCCEEDED(read->LockRect(&locked,nullptr,D3DLOCK_READONLY)));images[mode].resize(64*64*4);for(unsigned y=0;y<64;++y)std::memcpy(images[mode].data()+y*64*4,static_cast<const char*>(locked.pBits)+y*locked.Pitch,64*4);assert(SUCCEEDED(read->UnlockRect()));}
        assert(images[0]==images[1]&&std::any_of(images[0].begin(),images[0].end(),[](unsigned char x){return x!=0;}));assert(SUCCEEDED(d->SetRenderTarget(0,old)));assert(SUCCEEDED(d->SetDepthStencilSurface(oldDepth)));assert(SUCCEEDED(state->Apply()));drop(oldDepth);drop(old);drop(target);drop(read);drop(vb);drop(ib32);drop(state);w.meshStore.cancel();w.releaseMeshPool();
        std::puts("WORLD_PAGE_INDEX16_IMAGE_PASS equal32=1 productionBuffer=1 activeUnchanged=1");
    }
    static void coldGpuAtAddressPressure(WorldRenderer& w,bool legacyBudget,bool inject,unsigned& indexCreates,unsigned& failIndexAt){
        using namespace NorthlightGeometryMemory;namespace P=NorthlightWorldMeshPages;using NorthlightGI::WorldVertex;
        auto pages=std::make_shared<P::Plan>();constexpr unsigned count=60,vertices=10000,indices=30000;
        for(unsigned n=0;n<count;++n){P::Page page;page.vertices.resize(vertices);page.indices.resize(indices);for(unsigned i=0;i<vertices;++i){page.vertices[i].position={float(i),float(n),0};page.vertices[i].normal={0,0,1};}for(unsigned i=0;i<indices;++i)page.indices[i]=uint16_t((i*7+n)%vertices);
            page.key=NorthlightMeshPageKey::keyOf(page.vertices.data(),page.vertices.size()*sizeof(WorldVertex),page.indices.data(),page.indices.size()*sizeof(uint16_t));pages->vertexBytes+=page.key.vertexBytes;pages->indexBytes+=page.key.indexBytes;pages->pages.push_back(std::move(page));
            P::Batch batch;batch.page=n;batch.start=0;batch.count=indices/3;batch.material=0;batch.minVertex=0;batch.vertexCount=vertices;pages->batches.push_back(batch);}
        auto plan=std::make_shared<NorthlightWorldMesh::WorldMeshUploadPlan>();plan->vertexCount=count*vertices;plan->triangleCount=count*indices/3;plan->vertexBytes=uint64_t(plan->vertexCount)*sizeof(WorldVertex);plan->indexBytes=uint64_t(plan->triangleCount)*3*sizeof(uint32_t);plan->textureBytes=4;plan->pagesSealed=true;
        auto payload=std::make_shared<NorthlightWorldMesh::UploadPayload>();payload->pages=pages;payload->materials.resize(1);payload->materials[0].bgra={255,255,255,255};payload->bytes=pages->cpuBytes()+4;plan->payload.set(payload);assert(!P::validateSealed(*plan));
        w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->bvh=std::make_shared<NorthlightGI::BVH>();w.active->meshPlan=plan;w.active->map="pressure-test";w.active->serial=0;assert(!w.geometryTargetsReady&&w.meshStore.table().empty());payload.reset();pages.reset();
        struct Ballast {std::vector<void*> blocks;~Ballast(){for(auto p:blocks)assert(VirtualFree(p,0,MEM_RELEASE));}} ballast;MEMORYSTATUSEX status{};status.dwLength=sizeof status;assert(GlobalMemoryStatusEx(&status));
        while(status.ullAvailVirtual>395*MiB+65536){uint64_t amount=std::min<uint64_t>(16*MiB,(status.ullAvailVirtual-395*MiB)&~uint64_t(65535));assert(amount);auto p=VirtualAlloc(nullptr,size_t(amount),MEM_RESERVE,PAGE_NOACCESS);assert(p);ballast.blocks.push_back(p);status.dwLength=sizeof status;assert(GlobalMemoryStatusEx(&status));}
        uint64_t minimum=status.ullAvailVirtual;unsigned frames=0;bool completed=false,sawFailure=false;
        if(inject)failIndexAt=indexCreates+3;
        for(;frames<1000&&!completed;++frames){NorthlightStreaming::Budget budget(legacyBudget?2u<<20:4u<<20,legacyBudget?.5:2.0);completed=w.upload(budget);status.dwLength=sizeof status;assert(GlobalMemoryStatusEx(&status));minimum=std::min<uint64_t>(minimum,status.ullAvailVirtual);assert(minimum>=ProcessReserve+16*MiB);
            if(w.meshRetry.waiting&&!w.pendingMesh){assert(inject&&!sawFailure&&!completed&&w.meshStore.table().empty()&&plan->payload.live());sawFailure=true;failIndexAt=0;w.meshRetry.failedAt-=1000;}}
        assert(sawFailure==inject&&completed&&w.meshStore.table().size()==count&&!plan->payload.live()&&!w.geometryTargetsReady);
        for(auto id:w.meshStore.table()){D3DINDEXBUFFER_DESC desc{};assert(SUCCEEDED(w.meshBuffers[id].ib->GetDesc(&desc))&&desc.Format==D3DFMT_INDEX16);}
        assert(w.geometryResources(256,160));status.dwLength=sizeof status;assert(GlobalMemoryStatusEx(&status));minimum=std::min<uint64_t>(minimum,status.ullAvailVirtual);assert(minimum>=ProcessReserve+16*MiB);
        NorthlightStreaming::Budget finalBudget;assert(w.upload(finalBudget));
        std::printf("WORLD_COLD_GPU_PRESSURE_PASS entryMiB=395 minimumMiB=%llu pages=%u frames=%u budget=%s injectedFailure=%u recovered=1 sceneTargetsAfterMesh=1 reserveIntact=1\n",(unsigned long long)(minimum>>20),count,frames,legacyBudget?"previous":"cold",unsigned(inject));
        w.active.reset();w.clearMesh();w.releaseMeshPool();w.releaseGeometryTargets();w.meshRetry.clear();
    }
    static void reuseAtAddressPressure(WorldRenderer& w){
        using namespace NorthlightGeometryMemory;
        using NorthlightMeshPageKey::Key;
        auto key=[](unsigned n){Key k;k.lo=n+1;k.hi=~uint64_t(n);k.vertexBytes=300000;k.indexBytes=100000;return k;};
        std::vector<Key> first,next;for(unsigned n=0;n<64;++n){first.push_back(key(n));next.push_back(key(n+1000));}
        auto create=[&](const std::vector<Key>& keys){
            assert(w.meshStore.beginStaging(keys));w.syncMeshBuffers();
            for(size_t i=0;i<w.meshStore.staging().upload.size();++i){const auto id=w.meshStore.staging().uploadBuffer[i];const auto& k=w.meshStore.staging().keys[w.meshStore.staging().upload[i]];
                assert(w.meshStore.markWriting(id));assert(w.acquireMeshPage(id,true,k.vertexBytes));assert(w.acquireMeshPage(id,false,k.indexBytes));}
            w.meshStore.commit();
        };
        create(first);create({key(999)});assert(w.meshStore.beginStaging(next));w.syncMeshBuffers();
        uint64_t vb=0,ib=0;auto has=[&](uint32_t id,bool vertex){return id<w.meshBuffers.size()&&(vertex?w.meshBuffers[id].vb!=nullptr:w.meshBuffers[id].ib!=nullptr);};
        assert(w.meshStore.stagingGrowth(has,vb,ib)&&vb==0&&ib==0);
        struct Ballast {std::vector<void*> blocks;~Ballast(){for(auto p:blocks)assert(VirtualFree(p,0,MEM_RELEASE));}} ballast;
        // This fixture tests aggregate admission, not accidental driver VA
        // fragmentation. Preserve the required contiguous witness while the
        // ballast consumes the other free regions, then return it to the scan.
        auto* witness=VirtualAlloc(nullptr,18*MiB,MEM_RESERVE,PAGE_NOACCESS);assert(witness);
        MEMORYSTATUSEX status{};status.dwLength=sizeof status;assert(GlobalMemoryStatusEx(&status));
        while(status.ullAvailVirtual>325*MiB+65536){uint64_t bytes=std::min<uint64_t>(16*MiB,(status.ullAvailVirtual-325*MiB)&~uint64_t(65535));assert(bytes);auto p=VirtualAlloc(nullptr,size_t(bytes),MEM_RESERVE,PAGE_NOACCESS);assert(p);ballast.blocks.push_back(p);status.dwLength=sizeof status;assert(GlobalMemoryStatusEx(&status));}
        assert(VirtualFree(witness,0,MEM_RELEASE));
        auto oldBudget=uploadBudget(64ull*300000,64ull*100000,MiB,MiB);oldBudget.largest=18*MiB;
        auto newBudget=uploadBudget(vb,ib,MiB,MiB);newBudget.largest=18*MiB;
        bool exact=false;auto sample=w.geometryAdmission(newBudget,&exact);assert(sample.valid&&!admits(sample,oldBudget)&&admits(sample,newBudget));
        auto original=w.meshStore.table();uint64_t minimum=sample.available;
        for(size_t i=0;i<w.meshStore.staging().upload.size();++i){const auto id=w.meshStore.staging().uploadBuffer[i];const auto& k=w.meshStore.staging().keys[w.meshStore.staging().upload[i]];
            auto* v=w.meshBuffers[id].vb;auto* index=w.meshBuffers[id].ib;assert(v&&index&&w.meshStore.markWriting(id));
            assert(w.acquireMeshPage(id,true,k.vertexBytes)&&w.acquireMeshPage(id,false,k.indexBytes));assert(v==w.meshBuffers[id].vb&&index==w.meshBuffers[id].ib);
            void* bytes=nullptr;assert(SUCCEEDED(v->Lock(0,k.vertexBytes,&bytes,0))&&bytes);std::memset(bytes,int(i),k.vertexBytes);assert(SUCCEEDED(v->Unlock()));
            bytes=nullptr;assert(SUCCEEDED(index->Lock(0,k.indexBytes,&bytes,0))&&bytes);std::memset(bytes,int(i),k.indexBytes);assert(SUCCEEDED(index->Unlock()));v->PreLoad();index->PreLoad();
            status.dwLength=sizeof status;assert(GlobalMemoryStatusEx(&status));minimum=std::min<uint64_t>(minimum,status.ullAvailVirtual);assert(minimum>=ProcessReserve+16*MiB);
        }
        assert(w.meshStore.table()==original);w.meshStore.cancel();assert(w.meshStore.table()==original);
        std::printf("WORLD_REUSED_PAGES_PRESSURE_PASS availableMiB=%llu minimumMiB=%llu pages=64 newBytes=0 oldBudgetMiB=%llu newBudgetMiB=%llu activePreserved=1\n",(unsigned long long)(sample.available>>20),(unsigned long long)(minimum>>20),(unsigned long long)(oldBudget.available>>20),(unsigned long long)(newBudget.available>>20));
        w.releaseMeshPool();
    }
    static void setup(WorldRenderer& w,unsigned width,unsigned height){
        assert(w.resources(width,height,D3DFMT_A16B16G16R16F));
        assert(!w.color&&!w.colorSurface); // No consumer has requested full-size scratch.
        fixtureContext(w);w.frames=599;NorthlightDiagnostics::configure(true);w.lastRequest.map="Northrend";
        w.quality.equipmentLights=0;w.quality.horizonHaze=0;w.quality.fogClouds=0;
        w.effects.gi=true;w.effects.shadows=true;
        auto field=std::make_shared<NorthlightRegionalFog::Field>();field->originX=field->originY=-256;field->fogCells=field->airCells=4096;
        for(auto& t:field->texels){t.ground=-2;t.day=.015f;t.height=2.5f;}
        auto region=std::make_shared<WorldRenderer::FogRegion>();region->map=w.lastRequest.map;region->center={0,0,0};region->field=field;
        {std::lock_guard<std::mutex> lock(w.mutex);w.publishedFogRegion=std::move(region);}
        assert(!w.uploadedFogField&&!w.regionalFogTexture); // render() must upload without an active BVH
        assert(!w.ready()&&w.screenReady()&&!w.active&&!w.vertices&&!w.indices);
    }
    static bool resources(WorldRenderer& w,unsigned width,unsigned height){return w.resources(width,height,D3DFMT_A16B16G16R16F);}
    static void screenRetry(WorldRenderer& w){assert(w.screenResourceRetry&&!w.failed&&!w.width&&!w.height&&!w.fogPS&&!w.fog&&!w.liveTerrainDecl);w.screenResourceRetryAt-=1000;}
    static bool uploadFog(WorldRenderer& w){return w.uploadRegionalFog(w.publishedFogRegion->field,w.lastRequest.map);}
    static void fogRetry(WorldRenderer& w){assert(w.fogUploadRetry&&!w.failed&&!w.regionalFogTexture&&!w.uploadedFogField&&w.screenReady());w.fogUploadRetryAt-=1000;}
    static void fogUploaded(WorldRenderer& w){assert(!w.fogUploadRetry&&w.uploadedFogField==w.publishedFogRegion->field&&w.regionalFogTexture);D3DLOCKED_RECT l{};assert(SUCCEEDED(w.regionalFogTexture->LockRect(0,&l,nullptr,D3DLOCK_READONLY)));for(unsigned y=0;y<NorthlightRegionalFog::N;++y)assert(!std::memcmp(static_cast<const char*>(l.pBits)+y*l.Pitch,w.uploadedFogField->texels.data()+y*NorthlightRegionalFog::N,NorthlightRegionalFog::N*sizeof(NorthlightRegionalFog::Texel)));assert(SUCCEEDED(w.regionalFogTexture->UnlockRect(0)));}
    static void consumers(WorldRenderer& w){
        w.quality.nearShadowInterval=w.quality.farShadowInterval=1;
        w.captureDemand=false;w.actorCaptureDecided=true;w.lastRenderDebug=0;
        for(unsigned frame=0;frame<1000;++frame){
            w.captureMode=WorldRenderer::CaptureUndecided;w.actorCaptureDue=frame%20==0;
            assert(w.modelCaptureSkipped()==!w.actorCaptureDue);
        }
        w.actorCaptureDue=false;w.captureMode=WorldRenderer::CaptureUndecided;w.captureDemand=true;assert(!w.modelCaptureSkipped());
        w.captureMode=WorldRenderer::CaptureUndecided;w.lastRenderDebug=1;assert(!w.modelCaptureSkipped());w.lastRenderDebug=0;
        w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->bvh=std::make_shared<NorthlightGI::BVH>();
        w.captureMode=WorldRenderer::CaptureUndecided;assert(w.ready()&&!w.modelCaptureSkipped());w.active.reset();
        w.captureMode=WorldRenderer::CaptureUndecided;w.actorCaptureDecided=false;
        std::puts("WORLD_SCREEN_CONSUMERS_PASS frames=1000 GI=50 unusedShadowCaptures=0 demand=preserved diagnostic=preserved readyGeometry=preserved");
    }
    static void fog(WorldRenderer& w,bool on){w.effects.fog=on;w.fogHistoryValid=false;}
    static void cloudPressure(WorldRenderer& w){
        auto& noise=w.fogCloudNoise();noise.data=NorthlightFogClouds::generate();noise.quantiles=NorthlightFogClouds::quantiles(noise.data.data());noise.ready.store(true,std::memory_order_release);
        w.memoryPressure=true;assert(w.ensureCloudNoise()&&w.cloudNoise);w.memoryPressure=false;
        std::puts("WORLD_CLOUD_PRESSURE_PASS demandPressure=1 fullNoiseTexture=1");
    }
    static void ambientVariant(WorldRenderer& w){
        assert(w.fogAmbientPS&&w.fogVolumeShader(true,true)==w.fogAmbientPS);
        assert(w.fogVolumeShader(false,false)==w.fogPS);
        // Optional variant refusal must retain the complete shader and avoid retries.
        drop(w.fogAmbientPS);w.fogAmbientRefused=true;
        assert(w.fogVolumeShader(true,false)==w.fogPS&&!w.fogAmbientPS);
        w.fogAmbientRefused=false;assert(w.fogVolumeShader(true,false)==w.fogAmbientPS&&w.fogAmbientPS);
        std::puts("WORLD_FOG_AMBIENT_VARIANT_PASS zeroSource=1 residentReuse=1 optionalFallback=1");
        assert(w.localDirectShader(2)==w.localDirectPS&&!w.localDirectSinglePS);
        auto* single=w.localDirectShader(1);assert(single&&single!=w.localDirectPS&&w.localDirectShader(1)==single);
        drop(w.localDirectSinglePS);w.localDirectSingleRefused=true;
        assert(w.localDirectShader(1)==w.localDirectPS&&!w.localDirectSinglePS);
        w.localDirectSingleRefused=false;assert(w.localDirectShader(1)==w.localDirectSinglePS&&w.localDirectSinglePS);
        std::puts("WORLD_LOCAL_SINGLE_VARIANT_PASS exactCount=1 residentReuse=1 optionalFallback=1");
    }
    static void noSceneTargets(const WorldRenderer& w){
        assert(!w.geometryTargetsReady&&!w.shadowDepth&&!w.shadowCacheDepth&&!w.shadowScratch&&!w.probePrev&&!w.actorVisibility);
        for(auto* t:w.shadow)assert(!t);for(auto* t:w.shadowCache)assert(!t);for(auto* t:w.probe)assert(!t);for(auto* t:w.actorShadow)assert(!t);for(auto* t:w.actorTiles)assert(!t);assert(!w.actorTilesMid);
    }
    static uint64_t sceneBytes(const WorldRenderer& w){
        using namespace NorthlightResourceMemory;Inventory i;
        for(auto* t:w.shadow)i.texture(t,ShadowMaps);for(auto* t:w.shadowCache)i.texture(t,ShadowMaps);i.texture(w.shadowScratch,ShadowMaps);
        for(auto* t:w.actorShadow)i.texture(t,ShadowMaps);for(auto* t:w.actorTiles)i.texture(t,ShadowMaps);i.texture(w.actorTilesMid,ShadowMaps);i.texture(w.actorVisibility,ShadowMaps);
        i.surface(w.shadowDepth,ShadowMaps);i.surface(w.shadowCacheDepth,ShadowMaps);for(auto* t:w.probe)i.texture(t,ProbeTextures);i.texture(w.probePrev,ProbeTextures);
        assert(i.complete());return i.groups[ShadowMaps]+i.groups[ProbeTextures];
    }
    static void fakeScene(WorldRenderer& w,IDirect3DVertexBuffer9* vb,IDirect3DIndexBuffer9* ib){
        w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->map=w.lastRequest.map;w.active->bvh=std::make_shared<NorthlightGI::BVH>();
        w.active->serial=1;w.active->atlas.resize(NorthlightGI::probeLayout().atlasSize());auto& entry=w.active->atlas[0];entry.occupied=true;entry.probe.valid=true;entry.probe.sh[0]={.3f,.4f,.5f};
        w.uploaded=w.active->bvh;w.uploadedMap=w.active->map;w.vertices=vb;w.indices=ib;w.effects.shadows=w.effects.gi=false;
    }
    static void retryScene(WorldRenderer& w){w.geometryTargetsRetryAt-=1000;}
    static bool sceneDrawn(const WorldRenderer& w){return w.fogGeometryReady;}
    static IDirect3DTexture9* screenFog(const WorldRenderer& w){return w.fog;}
    static void pressure(WorldRenderer& w,bool on){w.memoryPressure=on;}
    static void expireFakeScene(WorldRenderer& w){w.active.reset();w.expiredMeshPending=true;}
    static std::vector<unsigned char> probeImage(WorldRenderer& w){
        assert(w.uploadedSerial==1&&w.probe[0]);D3DSURFACE_DESC desc{};assert(SUCCEEDED(w.probe[0]->GetLevelDesc(0,&desc)));D3DLOCKED_RECT lock{};
        assert(SUCCEEDED(w.probe[0]->LockRect(0,&lock,nullptr,D3DLOCK_READONLY)));std::vector<unsigned char> image(size_t(desc.Width)*desc.Height*16);
        for(unsigned y=0;y<desc.Height;++y)std::memcpy(image.data()+size_t(y)*desc.Width*16,static_cast<const char*>(lock.pBits)+size_t(y)*lock.Pitch,size_t(desc.Width)*16);
        assert(SUCCEEDED(w.probe[0]->UnlockRect(0)));return image;
    }
    static void probeRetired(const WorldRenderer& w){assert(!w.uploadedSerial);}
    static void removeFakeScene(WorldRenderer& w){
        w.vertices=nullptr;w.indices=nullptr;w.uploaded.reset();w.uploadedMap.clear();w.active.reset();w.releaseGeometryTargets();w.effects.shadows=w.effects.gi=true;
    }
    static bool composed(const WorldRenderer& w){return w.composited&&!w.shadowsComposited&&!w.shadowFrameReady;}
    static void scratch(const WorldRenderer& w,bool expected){assert(bool(w.color)==expected&&bool(w.colorSurface)==expected);}
    static void direct(WorldRenderer& w,bool on){w.sourceColors[0]=on?WorldRenderer::V(.6f,.5f,.4f):WorldRenderer::V();w.sourceColors[1]={};}
    static void resetResources(WorldRenderer& w){w.releaseGPU(true);assert(w.publishedFogRegion&&!w.uploadedFogField&&!w.active);}
    static void diagnosticFrames(WorldRenderer& w,unsigned width,unsigned height){
        const auto directory=NorthlightWorldDiagnostics::directory(w.root);assert(!directory.empty());
        auto pixels=[&](const std::string& folder,const char* name,unsigned capture,bool zero){
            std::ifstream f(folder+"/capture-"+std::to_string(capture)+"-"+name+".fgr",std::ios::binary);assert(f);
            uint32_t header[5];f.read(reinterpret_cast<char*>(header),sizeof header);assert(header[0]==0x31524746&&header[1]&&header[2]);
            std::vector<unsigned char> bytes(size_t(header[1])*header[2]*header[4]);f.read(reinterpret_cast<char*>(bytes.data()),bytes.size());assert(f&&f.peek()==EOF);
            const bool empty=std::all_of(bytes.begin(),bytes.end(),[](auto byte){return byte==0;});assert(empty==zero);
        };
        w.captureProducts(directory,1,900,width,height,true,true);pixels(directory,"fog",1,false);
        const auto before=fogImage(w);
        // Populated targets are deliberately retained, but the next frame has
        // no producer. Export zeros without any device access or target clear.
        assert(NorthlightWorldDiagnostics::product(nullptr,w.fogSurface,directory,"ao-raw",2,false,width/2,height/2,D3DFMT_A16B16G16R16F,901));
        assert(NorthlightWorldDiagnostics::textureProduct(nullptr,nullptr,directory,"materials-normal-height",2,false,width,height,D3DFMT_A16B16G16R16F,901));
        pixels(directory,"ao-raw",2,true);pixels(directory,"materials-normal-height",2,true);assert(before==fogImage(w));
        w.captureProducts(directory,2,901,width,height,false);pixels(directory,"fog",2,true);pixels(directory,"normals",2,true);
        w.composited=false;assert(w.requestNormalGpuDiagnosticCapture());assert(w.captureSkippedDiagnostic(902,width,height));
        assert(w.diagnosticCaptureInLastRender()&&!w.gpuNormalDiagnosticRequested);pixels(w.gpuDiagnosticDirectory,"fog",1,true);pixels(w.gpuDiagnosticDirectory,"scene",1,true);
        NorthlightDiagnosticCapture::request(NorthlightDiagnosticCapture::AO);NorthlightDiagnosticCapture::request(NorthlightDiagnosticCapture::Materials);
        assert(NorthlightDiagnosticCapture::take()==(NorthlightDiagnosticCapture::AO|NorthlightDiagnosticCapture::Materials)&&!NorthlightDiagnosticCapture::take());
        std::puts("DIAGNOSTIC_FRAME_PASS staleTargetsRetained=1 skippedImagesZero=1 noDeviceAccessForEmpty=1 worldSkippedCapture=1 scopeQueue=1");
    }
    static void emptyDebugViews(WorldRenderer& world,IDirect3DSurface9* out,IDirect3DSurface9* resolved,IDirect3DSurface9* read,IDirect3DTexture9* depth,unsigned width,unsigned height){
        for(unsigned debug:{1u,2u,3u,1u,2u}){
            world.effects.shadows=world.effects.gi=debug<3?false:true;world.effects.fog=false;
            if(debug<3&&resolved==nullptr)world.effects.shadows=world.effects.gi=true; // also exercise missing geometry with enabled components
            assert(SUCCEEDED(world.d->ColorFill(out,nullptr,0xffffffff)));
            assert(SUCCEEDED(world.d->BeginScene()));const bool rendered=world.render(out,depth,width,height,D3DFMT_A16B16G16R16F,1,1000,0,1,debug,nullptr,nullptr);assert(SUCCEEDED(world.d->EndScene()));assert(rendered);
            if(resolved)assert(SUCCEEDED(world.d->StretchRect(out,nullptr,resolved,nullptr,D3DTEXF_NONE)));
            assert(SUCCEEDED(world.d->GetRenderTargetData(resolved?resolved:out,read)));D3DLOCKED_RECT lock{};assert(SUCCEEDED(read->LockRect(&lock,nullptr,D3DLOCK_READONLY)));
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width*8;++x)assert(static_cast<const unsigned char*>(lock.pBits)[y*lock.Pitch+x]==0);
            assert(SUCCEEDED(read->UnlockRect()));
        }
        std::puts("DIAGNOSTIC_VIEW_PASS disabledComponentsAndMissingGeometryBlack=1 noStaleImage=1");
    }
    static std::vector<unsigned char> fogImage(WorldRenderer& w){
        assert(w.uploadedFogField==w.publishedFogRegion->field&&!w.active&&!w.fogGeometryReady);
        D3DSURFACE_DESC desc{};assert(SUCCEEDED(w.fogSurface->GetDesc(&desc)));
        IDirect3DSurface9* read=nullptr;assert(SUCCEEDED(w.d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&read,nullptr)));
        assert(SUCCEEDED(w.d->GetRenderTargetData(w.fogSurface,read)));D3DLOCKED_RECT lock{};assert(SUCCEEDED(read->LockRect(&lock,nullptr,D3DLOCK_READONLY)));
        std::vector<unsigned char> image(size_t(desc.Width)*desc.Height*8);
        for(unsigned y=0;y<desc.Height;++y)std::memcpy(image.data()+size_t(y)*desc.Width*8,static_cast<const char*>(lock.pBits)+y*lock.Pitch,desc.Width*8);
        assert(SUCCEEDED(read->UnlockRect()));read->Release();return image;
    }
};
struct WorldScratchMeter:ForwardIDirect3DDevice9 {
    unsigned shaderSlotLimit=0,combinedDraws=0;bool refuseCombined=false,combinedBound=false;
    IDirect3DPixelShader9* combinedShader=nullptr;
    unsigned width,height,creates=0,sceneCreates=0,failSceneAt=0,screenCreates=0,failScreenAt=0,shaderCreates=0,failShaderAt=0,fogCreates=0,failFogAt=0;float fogWeight=-1;
    unsigned indexCreates=0,failIndexAt=0;
    WorldScratchMeter(IDirect3DDevice9* d,unsigned w,unsigned h):ForwardIDirect3DDevice9(d),width(w),height(h){}
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* caps)override{const auto hr=real->GetDeviceCaps(caps);if(SUCCEEDED(hr)&&shaderSlotLimit)caps->MaxPixelShader30InstructionSlots=shaderSlotLimit;return hr;}
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* shader)override{combinedBound=shader&&shader==combinedShader;return real->SetPixelShader(shader);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT n,const void* vertices,UINT stride)override{combinedDraws+=combinedBound;return real->DrawPrimitiveUP(type,n,vertices,stride);}
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT length,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DIndexBuffer9** out,HANDLE* shared)override{
        if(++indexCreates==failIndexAt){if(out)*out=nullptr;return E_OUTOFMEMORY;}return real->CreateIndexBuffer(length,usage,format,pool,out,shared);
    }
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w,UINT h,UINT levels,DWORD usage,D3DFORMAT format,D3DPOOL pool,IDirect3DTexture9** t,HANDLE* shared)override{
        if(w==width&&h==height&&(usage&D3DUSAGE_RENDERTARGET)&&format==D3DFMT_A16B16G16R16F)++creates;
        if(w==width/2&&h==height/2&&(usage&D3DUSAGE_RENDERTARGET)&&format==D3DFMT_A16B16G16R16F&&++screenCreates==failScreenAt)return E_OUTOFMEMORY;
        if(w==NorthlightRegionalFog::N&&h==NorthlightRegionalFog::N&&format==D3DFMT_A32B32G32R32F&&++fogCreates==failFogAt)return E_OUTOFMEMORY;
        if(w==1024&&h==1024&&format==D3DFMT_R32F&&(usage&D3DUSAGE_RENDERTARGET)&&++sceneCreates==failSceneAt)return E_OUTOFMEMORY;
        return real->CreateTexture(w,h,levels,usage,format,pool,t,shared);
    }
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code,IDirect3DPixelShader9** shader)override{
        if(++shaderCreates==failShaderAt)return E_OUTOFMEMORY;
        if(code==kWorldMaterialCompositeHdrShader&&refuseCombined){*shader=nullptr;return D3DERR_NOTAVAILABLE;}
        const auto hr=real->CreatePixelShader(code,shader);if(SUCCEEDED(hr)&&code==kWorldMaterialCompositeHdrShader)combinedShader=*shader;return hr;
    }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT start,const float* values,UINT count)override{
        if(start==64&&count==1)fogWeight=values[1];return real->SetPixelShaderConstantF(start,values,count);
    }
};
static void screenOk(HRESULT h){if(FAILED(h)){std::fprintf(stderr,"screen HRESULT=%08lx\n",(unsigned long)h);std::abort();}}
static bool renderChecked(WorldRenderer& world,IDirect3DDevice9* device,IDirect3DSurface9* out,IDirect3DTexture9* depth,UINT w,UINT h,D3DFORMAT format,float nearZ,float farZ,float minZ,float maxZ,int debug,NorthlightGpuProfile* profile,IDirect3DTexture9* water,IDirect3DTexture9* scene=nullptr,IDirect3DTexture9* ao=nullptr,unsigned frame=0,IDirect3DTexture9* hdrBase=nullptr,bool hdrCombineAllowed=true){
    screenOk(device->BeginScene());const bool result=world.render(out,depth,w,h,format,nearZ,farZ,minZ,maxZ,debug,profile,water,scene,ao,frame,hdrBase,false,hdrCombineAllowed);screenOk(device->EndScene());
    if(!result)std::fprintf(stderr,"WORLD_SCREEN_RENDER_FAILED reason=%s screenReady=%u geometryReady=%u debug=%d frame=%u\n",world.lastSkipReason(),unsigned(world.screenReady()),unsigned(world.ready()),debug,frame);
    return result;
}
static float screenHalf(unsigned short h){unsigned e=h>>10&31,m=h&1023;float v=e==0?std::ldexp(float(m),-24):e==31?(m?NAN:INFINITY):std::ldexp(float(1024+m),int(e)-25);return h&32768?-v:v;}
static IDirect3DTexture9* screenTexture(IDirect3DDevice9* d,unsigned w,unsigned h,const float* value,unsigned channels=4){
    IDirect3DTexture9* t=nullptr;screenOk(d->CreateTexture(w,h,1,0,channels==4?D3DFMT_A32B32G32R32F:D3DFMT_R32F,D3DPOOL_MANAGED,&t,nullptr));D3DLOCKED_RECT l{};screenOk(t->LockRect(0,&l,nullptr,0));
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)std::memcpy(static_cast<char*>(l.pBits)+y*l.Pitch+x*channels*4,value,channels*4);screenOk(t->UnlockRect(0));return t;
}
int main(int argc,char** argv){
    assert(argc==3);constexpr unsigned W=256,H=160;const unsigned msaa=std::getenv("NORTHLIGHT_TEST_MSAA")?unsigned(std::atoi(std::getenv("NORTHLIGHT_TEST_MSAA"))):0;assert(msaa==0||msaa==2);
    std::setvbuf(stdout,nullptr,_IONBF,0);logFile=stdout;
    std::string root=std::string(argv[2])+"/";assert(MultiByteToWideChar(CP_UTF8,0,root.c_str(),-1,rootPath,MAX_PATH));
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightScreenEffectsOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,W,H,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;screenOk(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DSurface9 *out=nullptr,*readback=nullptr,*resolved=nullptr;screenOk(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_TYPE(msaa),0,FALSE,&out,nullptr));screenOk(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    if(msaa)screenOk(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
    // Capture stores positive-hemisphere normal detail relative to its reference.
    const float rgb[]={.4f,.4f,.4f,1},ambient[]={0,0,0,1},normal[]={.7f,.6f,20,1},z[]={1000.f/999.f-1000.f/(999.f*20)};
    auto* scene=screenTexture(d,W,H,rgb);auto* ao=screenTexture(d,W/2,H/2,ambient);auto* detail=screenTexture(d,W,H,normal);auto* depth=screenTexture(d,W,H,z,1);
    std::vector<float> images[4];
    {
        WorldScratchMeter meter(d,W,H);
        auto world=std::make_unique<WorldRenderer>(&meter);
        meter.failShaderAt=3;assert(!WorldScreenEffectsTest::resources(*world,W,H));
        const auto shaderAttempts=meter.shaderCreates;assert(!WorldScreenEffectsTest::resources(*world,W,H)&&meter.shaderCreates==shaderAttempts);WorldScreenEffectsTest::screenRetry(*world);
        meter.failShaderAt=0;meter.failScreenAt=3;assert(!WorldScreenEffectsTest::resources(*world,W,H));
        const auto targetAttempts=meter.screenCreates;assert(!WorldScreenEffectsTest::resources(*world,W,H)&&meter.screenCreates==targetAttempts);WorldScreenEffectsTest::screenRetry(*world);
        const unsigned beforeRetry=meter.screenCreates;assert(WorldScreenEffectsTest::resources(*world,W,H));assert(meter.screenCreates>beforeRetry);
        std::puts("WORLD_SCREEN_RESOURCE_RETRY_PASS shaderOom=1 targetOom=1 partialReleased=1 recovered=1");
        WorldScreenEffectsTest::sceneOwnership(*world,&meter);WorldScreenEffectsTest::reuseAtAddressPressure(*world);WorldScreenEffectsTest::pageIndexImage(*world,d);WorldScreenEffectsTest::coldGpuAtAddressPressure(*world,true,false,meter.indexCreates,meter.failIndexAt);WorldScreenEffectsTest::coldGpuAtAddressPressure(*world,false,false,meter.indexCreates,meter.failIndexAt);WorldScreenEffectsTest::coldGpuAtAddressPressure(*world,false,true,meter.indexCreates,meter.failIndexAt);meter.sceneCreates=0;WorldScreenEffectsTest::setup(*world,W,H);
        meter.fogCreates=0;meter.failFogAt=1;assert(!WorldScreenEffectsTest::uploadFog(*world));
        const auto fogAttempts=meter.fogCreates;assert(!WorldScreenEffectsTest::uploadFog(*world)&&meter.fogCreates==fogAttempts);WorldScreenEffectsTest::fogRetry(*world);
        meter.failFogAt=0;assert(WorldScreenEffectsTest::uploadFog(*world));WorldScreenEffectsTest::fogUploaded(*world);
        std::puts("WORLD_FOG_UPLOAD_RETRY_PASS allocationOom=1 throttled=1 recoveredFieldExact=1");
        WorldScreenEffectsTest::consumers(*world);WorldScreenEffectsTest::cloudPressure(*world);
        WorldScreenEffectsTest::noSceneTargets(*world);assert(WorldScreenEffectsTest::sceneBytes(*world)==0&&meter.sceneCreates==0);
        for(unsigned mode=0;mode<4;++mode){
            WorldScreenEffectsTest::fog(*world,mode>=2);world->setMaterialDetail(mode&1?detail:nullptr,1,48,nullptr,8);
            screenOk(d->SetRenderTarget(0,out));screenOk(d->SetDepthStencilSurface(nullptr));screenOk(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
            assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,mode));assert(WorldScreenEffectsTest::composed(*world));
            WorldScreenEffectsTest::scratch(*world,bool(mode&1));assert(meter.creates==(mode+1)/2);
            if(msaa)screenOk(d->StretchRect(out,nullptr,resolved,nullptr,D3DTEXF_NONE));screenOk(d->GetRenderTargetData(msaa?resolved:out,readback));D3DLOCKED_RECT l{};screenOk(readback->LockRect(&l,nullptr,D3DLOCK_READONLY));
            auto& image=images[mode];image.resize(W*H*3);
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)for(unsigned k=0;k<3;++k){unsigned short h;std::memcpy(&h,static_cast<char*>(l.pBits)+y*l.Pitch+x*8+k*2,2);float value=screenHalf(h);assert(std::isfinite(value)&&value>=0);image[(y*W+x)*3+k]=value;}
            screenOk(readback->UnlockRect());
            IDirect3DSurface9* restored=nullptr;screenOk(d->GetRenderTarget(0,&restored));assert(restored==out);restored->Release();DWORD blend=TRUE;screenOk(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend));assert(!blend);
            if(mode==1){
                // A camera turn may hide all captures without disabling materials.
                world->setMaterialDetail(nullptr,1,48,nullptr,8,true);
                assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
                WorldScreenEffectsTest::scratch(*world,true);assert(meter.creates==1);
            }
        }

        WorldScreenEffectsTest::fog(*world,true);
        for(unsigned frame=0;frame<2;++frame){
            meter.fogWeight=-1;assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
            assert(meter.fogWeight==(frame?.85f:0.f));
        }
        std::puts("WORLD_FOG_HISTORY_PASS noGeometry=1 firstWeight=0 nextWeight=0.85");
        WorldScreenEffectsTest::ambientVariant(*world);
        IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
        screenOk(d->CreateVertexBuffer(32,0,0,D3DPOOL_MANAGED,&vb,nullptr));screenOk(d->CreateIndexBuffer(12,0,D3DFMT_INDEX32,D3DPOOL_MANAGED,&ib,nullptr));
        auto* screenFog=WorldScreenEffectsTest::screenFog(*world);WorldScreenEffectsTest::fakeScene(*world,vb,ib);meter.failSceneAt=3;
        meter.fogWeight=-1;
        assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
        assert(!WorldScreenEffectsTest::sceneDrawn(*world)&&meter.fogWeight==.85f&&WorldScreenEffectsTest::screenFog(*world)==screenFog);WorldScreenEffectsTest::noSceneTargets(*world);assert(world->screenReady());
        WorldScreenEffectsTest::retryScene(*world);meter.failSceneAt=0;
        meter.fogWeight=-1;
        assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
        const auto sceneBytes=WorldScreenEffectsTest::sceneBytes(*world);assert(sceneBytes>0&&world->ready()&&WorldScreenEffectsTest::sceneDrawn(*world)&&meter.fogWeight==0&&WorldScreenEffectsTest::screenFog(*world)==screenFog);
        const auto creates=meter.sceneCreates;assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));assert(meter.sceneCreates==creates);
        const auto probeBefore=WorldScreenEffectsTest::probeImage(*world);
        WorldScreenEffectsTest::pressure(*world,true);world->endFrame();assert(WorldScreenEffectsTest::sceneBytes(*world)==sceneBytes); // Active BVH still owns these targets.
        WorldScreenEffectsTest::expireFakeScene(*world);world->endFrame();WorldScreenEffectsTest::noSceneTargets(*world);WorldScreenEffectsTest::probeRetired(*world);
        assert(WorldScreenEffectsTest::sceneBytes(*world)==0&&WorldScreenEffectsTest::screenFog(*world)==screenFog);
        WorldScreenEffectsTest::fixtureContext(*world);
        assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
        WorldScreenEffectsTest::pressure(*world,false);WorldScreenEffectsTest::fakeScene(*world,vb,ib);
        assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
        assert(WorldScreenEffectsTest::sceneBytes(*world)==sceneBytes&&WorldScreenEffectsTest::screenFog(*world)==screenFog&&WorldScreenEffectsTest::probeImage(*world)==probeBefore);
        WorldScreenEffectsTest::removeFakeScene(*world);vb->Release();ib->Release();
        std::printf("WORLD_SCENE_LAZY_PASS noBvhBytes=0 partialFailureKeptScreen=1 retryWithoutReset=1 pressuredExpiryReclaimed=1 probesRestored=1 sceneBytes=%llu\n",(unsigned long long)sceneBytes);
        // The unfurled legacy path still gets scratch; the next folded frame retires it.
        WorldScreenEffectsTest::fog(*world,false);world->setMaterialDetail(nullptr,0,48);
        for(bool folded:{false,true}){
            screenOk(d->SetRenderTarget(0,out));screenOk(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff666666,1,0));
            assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,folded?scene:nullptr,ao));
            WorldScreenEffectsTest::scratch(*world,!folded);assert(meter.creates==2);
        }
        std::puts("WORLD_SCRATCH_PASS folded_off=0 materials_on=1 invisible_reused=1 legacy_on=1 retired_on_disable=1");
        std::vector<unsigned char> ambientOnly[2];
        for(unsigned direct=0;direct<2;++direct){
            if(direct)WorldScreenEffectsTest::resetResources(*world);
            WorldScreenEffectsTest::direct(*world,direct!=0);WorldScreenEffectsTest::fog(*world,true);
            screenOk(d->SetRenderTarget(0,out));
            assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
            ambientOnly[direct]=WorldScreenEffectsTest::fogImage(*world);
        }
        assert(ambientOnly[0]==ambientOnly[1]); // missing shadow geometry disables direct shafts, never ambient extinction
        float maximumScatter=0,minimumTransmission=1;
        for(size_t pixel=0;pixel<ambientOnly[0].size()/8;++pixel)for(unsigned channel=0;channel<4;++channel){unsigned short bits;std::memcpy(&bits,ambientOnly[0].data()+pixel*8+channel*2,2);const float value=screenHalf(bits);assert(std::isfinite(value)&&value>=0);if(channel<3)maximumScatter=std::max(maximumScatter,value);else minimumTransmission=std::min(minimumTransmission,value);}
        assert(maximumScatter>.00001f&&minimumTransmission<.999f);
        std::printf("WORLD_AMBIENT_FOG_PASS noBvh=1 independentUpload=1 resetRestored=1 sunZero=1 moonZero=1 scatter=%g transmission=%g unshadowedDirectDisabled=1 msaa=%u\n",maximumScatter,minimumTransmission,msaa);
        WorldScreenEffectsTest::diagnosticFrames(*world,W,H);
        WorldScreenEffectsTest::emptyDebugViews(*world,out,resolved,readback,depth,W,H);
        {
            auto world=std::make_unique<WorldRenderer>(&meter);WorldScreenEffectsTest::setup(*world,W,H);
        // Three adjacent receiver classes share the existing native stencil:
        // name (keep native target), self-lit M2 (atmosphere only), ordinary.
        IDirect3DSurface9* receiverDepth=nullptr;
        screenOk(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_TYPE(msaa),0,FALSE,&receiverDepth,nullptr));
        float unlitPixels[4][3]={};
        for(unsigned mode=0;mode<4;++mode){
            WorldScreenEffectsTest::fixtureContext(*world);WorldScreenEffectsTest::fog(*world,mode>=2);
            world->setMaterialDetail(mode&1?detail:nullptr,1,48,nullptr,8);
            // Establish the same material configuration before measuring mask cost.
            assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
            screenOk(d->SetRenderTarget(0,out));screenOk(d->SetDepthStencilSurface(receiverDepth));
            D3DVIEWPORT9 viewport{0,0,W,H,0,1};screenOk(d->SetViewport(&viewport));
            screenOk(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
            screenOk(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_STENCIL,0xff00ff00,1,0));
            D3DRECT name{0,0,LONG(W/3),H},unlit{LONG(W/3),0,LONG(W*2/3),H};
            screenOk(d->Clear(1,&name,D3DCLEAR_STENCIL,0,1,NorthlightNameStencil::kBit));
            screenOk(d->Clear(1,&unlit,D3DCLEAR_STENCIL,0,1,NorthlightUnlitReceiver::Bit));
            screenOk(d->SetDepthStencilSurface(nullptr));
            world->setNameMask(receiverDepth);world->setUnlitMask(receiverDepth);
            const unsigned targets=meter.creates;
            assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao));
            assert(meter.creates==targets); // no additional full-size texture
            if(msaa)screenOk(d->StretchRect(out,nullptr,resolved,nullptr,D3DTEXF_NONE));
            screenOk(d->GetRenderTargetData(msaa?resolved:out,readback));D3DLOCKED_RECT lock{};screenOk(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            for(unsigned channel=0;channel<3;++channel){
                unsigned short nameValue,unlitValue;
                const auto* row=static_cast<char*>(lock.pBits)+(H/2)*lock.Pitch;
                std::memcpy(&nameValue,row+(W/6)*8+channel*2,2);std::memcpy(&unlitValue,row+(W/2)*8+channel*2,2);
                assert(std::abs(screenHalf(nameValue)-(channel==1?1.f:0.f))<.002f);
                unlitPixels[mode][channel]=screenHalf(unlitValue);
                if(mode<2)assert(std::abs(unlitPixels[mode][channel]-.4f)<.003f);
            }
            screenOk(readback->UnlockRect());
        }
        float fogDelta=0;
        for(unsigned k=0;k<3;++k){assert(std::abs(unlitPixels[0][k]-unlitPixels[1][k])<.001f);assert(std::abs(unlitPixels[2][k]-unlitPixels[3][k])<.001f);fogDelta=std::max(fogDelta,std::abs(unlitPixels[2][k]-unlitPixels[0][k]));}
        assert(fogDelta>.002f);
        // Exercise the actual optional HDR selector, not only its bytecode:
        // low advertised caps, failed shader creation, combined draw and a
        // diagnostic capture retaining the original two-pass input ownership.
        const float linear[]={.132868f,.132868f,.132868f,1};auto* hdrBase=screenTexture(d,W,H,linear);
        std::vector<unsigned short> hdrReference;WorldScreenEffectsTest::fog(*world,false);
        world->setMaterialDetail(detail,1,48,nullptr,8);
        for(unsigned mode=0;mode<5;++mode){
            WorldScreenEffectsTest::fixtureContext(*world);
            if(mode<3){WorldScreenEffectsTest::retryHdrCombined(*world);meter.combinedShader=nullptr;}
            meter.shaderSlotLimit=mode==0?512:0;meter.refuseCombined=mode==1;
            if(mode==3)assert(world->requestNormalGpuDiagnosticCapture());
            screenOk(d->ColorFill(out,nullptr,0xff00ff00));world->setNameMask(receiverDepth);world->setUnlitMask(receiverDepth);
            const auto combinedBefore=meter.combinedDraws;
            assert(renderChecked(*world,d,out,depth,W,H,D3DFMT_A16B16G16R16F,1,1000,0,1,0,nullptr,nullptr,scene,ao,100+mode,hdrBase,mode!=4));
            assert(WorldScreenEffectsTest::hdrCombined(*world)==(mode>=2));
            assert(meter.combinedDraws-combinedBefore==(mode==2?1u:0u));
            if(msaa)screenOk(d->StretchRect(out,nullptr,resolved,nullptr,D3DTEXF_NONE));screenOk(d->GetRenderTargetData(msaa?resolved:out,readback));
            D3DLOCKED_RECT lock{};screenOk(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            std::vector<unsigned short> image(W*H*4);for(unsigned y=0;y<H;++y)std::memcpy(image.data()+y*W*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,W*8);screenOk(readback->UnlockRect());
            if(!mode)hdrReference=image;
            for(unsigned i=0;i<image.size();++i){assert(std::isfinite(screenHalf(image[i])));assert(std::abs(screenHalf(image[i])-screenHalf(hdrReference[i]))<.004f);}
        }
        meter.shaderSlotLimit=0;meter.refuseCombined=false;hdrBase->Release();receiverDepth->Release();
        std::printf("HDR_COMBINED_WORLD_GPU_PASS capFallback=1 creationFallback=1 actualCombinedDraw=1 diagnosticFallback=1 expansionFallback=1 namesUnlitPreserved=1 msaa=%u\n",msaa);
        std::printf("UNLIT_RECEIVER_GPU_PASS nativeRadiance=1 atmosphereRetained=1 materialInvariant=1 namesRetained=1 extraFullSizeTextures=0 msaa=%u\n",msaa);
        }

    }
    float bump=0,fog=0;for(unsigned i=0;i<W*H*3;++i){bump=std::max(bump,std::abs(images[1][i]-images[0][i]));fog=std::max(fog,std::abs(images[2][i]-images[0][i]));}
    std::printf("WORLD_SCREEN_GPU bumpDelta=%g fogDelta=%g noGeometry=1 noStaleShadows=1 stateRestored=1 msaa=%u\n",bump,fog,msaa);std::fflush(stdout);assert(bump>.002f&&fog>.002f);
    scene->Release();ao->Release();detail->Release();depth->Release();out->Release();if(resolved)resolved->Release();readback->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::puts("WORLD_SCREEN_GPU_PASS");
}
