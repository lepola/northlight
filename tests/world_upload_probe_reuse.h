#pragma once
inline void WorldColdUploadTest::probeReuse(UploadFaultDevice& d,D3DPRESENT_PARAMETERS& parameters){
    const bool wasSpooling=Spooling;Spooling=true;
    auto renderer=std::make_unique<WorldRenderer>(&d);auto& w=*renderer;w.quality.actorShadows=w.quality.actorShadowDetail=1;w.quality.pointShadowLimit=128;w.valid=true;
    assert(w.resources(128,128,D3DFMT_A16B16G16R16F));
    auto snapshot=scene(0);
    auto populate=[&](unsigned generation){
        snapshot->serial=generation;snapshot->atlas.assign(NorthlightGI::probeLayout().atlasSize(),{});
        const unsigned n=NorthlightGI::probeLayout().atlas;
        for(size_t i=0;i<snapshot->atlas.size();++i){auto& e=snapshot->atlas[i];e.key={int32_t(i%n),int32_t(i/n%n),int32_t(i/n/n)};
            if(i%(generation==1?3:11)==0)continue;e.occupied=e.probe.valid=true;
            for(unsigned k=0;k<4;++k)e.probe.sh[k]={float(generation)*.125f+float(k)*.015625f,float(i%7)*.03125f,.25f+float(k)*.015625f};
            for(unsigned a=0;a<6;++a){e.probe.moments[a].mean=float(a+1);e.probe.moments[a].meanSquare=float((a+1)*(a+1));}
        }
    };
    auto texture=[&](unsigned c){return c<5?w.probe[c]:w.probePrev;};
    auto poison=[&]{for(unsigned c=0;c<6;++c){auto* t=texture(c);assert(t);D3DSURFACE_DESC desc{};assert(SUCCEEDED(t->GetLevelDesc(0,&desc)));D3DLOCKED_RECT lock{};assert(SUCCEEDED(t->LockRect(0,&lock,nullptr,0)));
        for(unsigned y=0;y<desc.Height;++y){auto* row=reinterpret_cast<float*>(static_cast<char*>(lock.pBits)+size_t(y)*lock.Pitch);std::fill(row,row+size_t(desc.Width)*4,12345.f);}assert(SUCCEEDED(t->UnlockRect(0)));}};
    auto exact=[&]{
        const unsigned n=NorthlightGI::probeLayout().atlas;assert(w.uploadedSerial==snapshot->serial&&w.geometryTargetsReady);
        for(unsigned c=0;c<6;++c){auto* t=texture(c);assert(t);D3DSURFACE_DESC desc{};assert(SUCCEEDED(t->GetLevelDesc(0,&desc)));assert(desc.Pool==D3DPOOL_MANAGED&&desc.Format==D3DFMT_A32B32G32R32F&&desc.Width==n*n*(c==5?3:1)&&desc.Height==n*(c==3?6:1));
            D3DLOCKED_RECT lock{};assert(SUCCEEDED(t->LockRect(0,&lock,nullptr,D3DLOCK_READONLY)));
            for(unsigned y=0;y<desc.Height;++y){const auto* row=reinterpret_cast<const float*>(static_cast<const char*>(lock.pBits)+size_t(y)*lock.Pitch);
                for(unsigned x=0;x<desc.Width;++x){const unsigned localX=c==5?x%(n*n):x;const size_t index=(size_t(localX/n)*n+y%n)*n+localX%n;const auto& e=snapshot->atlas[index];
                    for(unsigned k=0;k<4;++k){float expected=0;bool activation=false;
                        if(c<3||c==5){const unsigned channel=c==5?x/(n*n):c;const auto& sh=e.probe.sh[k];expected=channel==0?sh.x:channel==1?sh.y:sh.z;}
                        else if(c==3){const unsigned axis=y/n;expected=k==0?e.probe.moments[axis].mean:k==1?e.probe.moments[axis].meanSquare:k==2?float(e.occupied&&e.probe.valid):NorthlightProbeBlend::None;}
                        else if(k<3)expected=k==0?float(e.key.x):k==1?float(e.key.y):float(e.key.z);
                        else if(e.occupied&&e.probe.valid)activation=true;else expected=-1;
                        if(activation)assert(std::isfinite(row[x*4+k])&&row[x*4+k]>=0&&row[x*4+k]!=12345.f);else assert(row[x*4+k]==expected);
                    }
                }
            }assert(SUCCEEDED(t->UnlockRect(0)));t->PreLoad();
        }
    };
    populate(1);complete(w,snapshot);exact();
    std::array<IDirect3DTexture9*,6> retained{};for(unsigned c=0;c<6;++c){retained[c]=texture(c);retained[c]->AddRef();}
    poison();populate(2);w.reset();assert(w.keepPressureProbes&&w.resetReplayPending&&w.uploadedSerial==0);
    for(unsigned c=0;c<6;++c)assert(texture(c)==retained[c]);assert(SUCCEEDED(d.Reset(&parameters)));w.valid=true;assert(w.resources(128,128,D3DFMT_A16B16G16R16F));
    complete(w,snapshot);assert(!w.keepPressureProbes&&!w.resetReplayPending);for(unsigned c=0;c<6;++c)assert(texture(c)==retained[c]);exact();
    // A Reset can arrive after targets succeeded, either before page creation
    // or during a staged page copy. Neither interruption may drop the managed
    // probes or retain partially written candidate claims.
    for(unsigned phase=0;phase<2;++phase){
        poison();populate(3+phase);w.reset();assert(w.pressureReplayAllocation());assert(SUCCEEDED(d.Reset(&parameters)));
        w.valid=true;assert(w.resources(128,128,D3DFMT_A16B16G16R16F));assert(w.geometryResources(128,128));
        assert(!w.keepPressureProbes&&w.meshStore.table().empty()&&w.pressureReplayAllocation());
        if(phase){
            const auto started=GetTickCount();
            while(!w.pendingMesh&&DWORD(GetTickCount()-started)<5000){NorthlightStreaming::Budget budget(32,2);w.upload(budget);assert(SUCCEEDED(d.Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0)));assert(SUCCEEDED(d.Present(nullptr,nullptr,nullptr,nullptr)));Sleep(1);}
            assert(w.pendingMesh&&w.meshStore.staging().open&&w.meshStore.table().empty());
        }else assert(!w.pendingMesh);
        w.reset();assert(w.keepPressureProbes&&w.resetReplayPending&&!w.pendingMesh&&!w.meshStore.staging().open);
        for(unsigned c=0;c<6;++c)assert(texture(c)==retained[c]);assert(SUCCEEDED(d.Reset(&parameters)));
        w.valid=true;assert(w.resources(128,128,D3DFMT_A16B16G16R16F));complete(w,snapshot);
        for(unsigned c=0;c<6;++c)assert(texture(c)==retained[c]);exact();
    }
    // An incompatible retained descriptor must be replaced while compatible
    // channels keep their identity. Hold the old texture to rule out pointer reuse.
    poison();populate(5);w.reset();assert(w.keepPressureProbes);assert(SUCCEEDED(d.Reset(&parameters)));
    drop(w.probe[0]);IDirect3DTexture9* wrong=nullptr;assert(SUCCEEDED(d.CreateTexture(1,1,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&wrong,nullptr)));w.probe[0]=wrong;wrong->AddRef();
    w.valid=true;assert(w.resources(128,128,D3DFMT_A16B16G16R16F));complete(w,snapshot);assert(w.probe[0]!=wrong&&w.probe[0]!=retained[0]);
    for(unsigned c=1;c<6;++c)assert(texture(c)==retained[c]);exact();wrong->Release();for(auto* t:retained)t->Release();
    assert(!w.failed);std::puts("COLD_PROBE_REUSE_PASS realReset=6 interruptedBeforePage=1 interruptedDuringCopy=1 managedIdentity=1 fullAtlasRewrite=1 invalidCellsCleared=1 historyCleared=1 incompatibleDescriptorRecreated=1 uploadedSerial=5");Spooling=wasSpooling;
}
