// Included inside WorldRenderer. Weather flashes use the directional caster path,
// independently of the nearby lamp cube budget. Maps are current-frame only.
    struct FlashSlot {
        IDirect3DTexture9* map[2]={};IDirect3DSurface9* surface[2]={};
        float matrix[2][16]={};V direction{};
        NorthlightLocalLights::Light source{};bool ready=false;
    };
    std::array<FlashSlot,2> flashSlots;
    IDirect3DPixelShader9* flashShadowPS=nullptr;
    unsigned flashFailures=0;DWORD flashRetryAt=0;
    void releaseFlashGPU(){
        for(auto& s:flashSlots){for(auto& p:s.surface)drop(p);for(auto& p:s.map)drop(p);s=FlashSlot{};}
        drop(flashShadowPS);flashRetryAt=0;
    }
    bool flashCheck(HRESULT hr,const char* operation){
        if(SUCCEEDED(hr))return true;
        if(flashFailures++<8)logf("FLASH shadow skipped: %s hr=%08lx",operation,(unsigned long)hr);
        flashRetryAt=GetTickCount()+1000;return false;
    }
    bool flashResources(FlashSlot& s){
        if(flashRetryAt&&LONG(GetTickCount()-flashRetryAt)<0)return false;
        if(!flashShadowPS&&!flashCheck(d->CreatePixelShader(kFlashShadowShader,&flashShadowPS),"lighting shader"))return false;
        for(unsigned c=0;c<2;++c){
            if(!s.map[c]&&!flashCheck(d->CreateTexture(1024,1024,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&s.map[c],nullptr),"map"))return false;
            if(!s.surface[c]&&!flashCheck(s.map[c]->GetSurfaceLevel(0,&s.surface[c]),"map surface"))return false;
        }
        return flashShadowPS&&shadowDepth&&shadowVS&&shadowPS&&replayPS;
    }
    bool renderFlashMap(FlashSlot& s,unsigned cascade,bool withReplays){
        const float* matrix=s.matrix[cascade];
        for(unsigned i=0;i<16;++i)if(!flashCheck(d->SetTexture(i,nullptr),"unbind sampler"))return false;
        for(unsigned i=1;i<4;++i)if(!flashCheck(d->SetRenderTarget(i,nullptr),"unbind MRT"))return false;
        D3DVIEWPORT9 vp={0,0,1024,1024,0,1};
        if(!flashCheck(d->SetDepthStencilSurface(nullptr),"unbind depth")||!flashCheck(d->SetRenderTarget(0,s.surface[cascade]),"target")||
           !flashCheck(d->SetDepthStencilSurface(shadowDepth),"depth target")||!flashCheck(d->SetViewport(&vp),"viewport")||
           !flashCheck(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0),"clear"))return false;
        // Loaded offscreen WMO/M2 placements participate, as they do for the sun.
        if(!drawStaticCasters(matrix))return false;
        for(unsigned i=0;i<4;++i)d->SetStreamSourceFreq(i,1);
        d->SetVertexDeclaration(shadowDecl);d->SetVertexShader(shadowVS);d->SetVertexShaderConstantF(0,matrix,4);d->SetPixelShader(shadowPS);
        d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
        d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
        d->SetRenderState(D3DRS_DEPTHBIAS,0);d->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS,0);d->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
        d->SetRenderState(D3DRS_FOGENABLE,FALSE);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
        for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(0,k,D3DTEXF_LINEAR);
        d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
        d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
        UINT page=UINT_MAX;
        for(const auto& b:batches){
            if(b.pointOnly||NorthlightShadowBounds::clipRejectAffine(b.boundsLow,b.boundsHigh,matrix))continue;
            if(b.material>=materials.size()||b.material>=uploadedAlphaCutoffs.size())return false;
            const float material[]={0,0,0,uploadedAlphaCutoffs[b.material]};
            if(!flashCheck(d->SetPixelShaderConstantF(0,material,1),"material")||!flashCheck(d->SetTexture(0,materials[b.material]),"alpha texture")||
               !bindMeshPage(b,page)||!flashCheck(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,b.minVertex,b.vertexCount,b.start,b.count),"world caster"))return false;
        }
        if(liveTerrainIndexCount||liveDirectionalIndexCount){
            const float opaque[]={0,0,0,-1};d->SetPixelShaderConstantF(0,opaque,1);d->SetTexture(0,nullptr);
            d->SetVertexDeclaration(liveTerrainDecl);d->SetStreamSource(0,liveTerrainGPU.vertices(),0,sizeof(StaticShadow::Vertex));d->SetIndices(liveIndicesGPU);
            NorthlightDynamicRing::touch(liveIndexRing,fence());
            if(!flashCheck(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,liveTerrainGPU.vertexCapacity(),UINT(liveIndexBase),UINT((liveTerrainIndexCount+liveDirectionalIndexCount)/3)),"live terrain"))return false;
        }
        if(withReplays){
            replayBoundsJoin();float rows[16];NorthlightWorldMath::replayProjection(context.inverseView,matrix,rows);
            d->SetPixelShader(replayPS);NorthlightReplayPoses::Pass<BOOL> poses;NorthlightReplayDrawState::Cache bindings(d);
            NorthlightRenderThreadProbe::Off split;size_t bytes=0,calls=0;
            for(const auto& p:replays){const auto& b=p->pointBounds;
                if(b.valid&&NorthlightShadowBounds::clipRejectAffine(vec(b.low),vec(b.high),matrix))continue;
                if(!submitReplay(p.get(),bindings,poses,rows,matrix,split,bytes,calls,[&](HRESULT h,const char* what){return flashCheck(h,what);}))return false;
            }
        }
        return true;
    }
    void renderFlashShadows(const NorthlightLocalLightSelection::Selection& lights,bool fresh,bool withReplays){
        for(auto& s:flashSlots)s.ready=false;
        unsigned slot=0;
        for(unsigned i=0;i<lights.count&&slot<flashSlots.size();++i){const auto& light=lights.sources[i];
            if(!(light.flags&NorthlightLocalLights::Flash))continue;
            // Never retain an older actor pose during a one-frame discharge.
            if(!fresh){captureDemand=true;continue;}
            const V pivot=staticPivotReady?staticFramePivot:shadowPivot();V delta=vec(light.position)-pivot;if(NorthlightGI::dot(delta,delta)<1)continue;
            auto& s=flashSlots[slot++];s.source=light;s.direction=NorthlightGI::normalized(delta);
            if(!flashResources(s))continue;
            // Sky models have no physical distance. As with sun/moon, the
            // distant discharge direction drives orthographic caster cascades;
            // receiver brightness still uses the finite source's distance.
            NorthlightWorldMath::shadowMatrix(pivot,s.direction,48,s.matrix[0]);
            NorthlightWorldMath::shadowMatrix(pivot,s.direction,192,s.matrix[1]);
            s.ready=renderFlashMap(s,0,withReplays)&&renderFlashMap(s,1,withReplays);
        }
    }
    bool renderFlashLighting(){
        bool drawn=false;
        for(const auto& s:flashSlots)if(s.ready){
            float position[]={s.source.position[0],s.source.position[1],s.source.position[2],s.source.attenuationEnd};
            float color[]={s.source.diffuse[0],s.source.diffuse[1],s.source.diffuse[2],NorthlightLocalLightSelection::transport(s.source)};
            const float direction[]={s.direction.x,s.direction.y,s.direction.z,0};
            if(!flashCheck(d->SetPixelShader(flashShadowPS),"shadow correction")||!flashCheck(d->SetTexture(2,s.map[0]),"near map")||
               !flashCheck(d->SetTexture(3,s.map[1]),"far map")||!flashCheck(d->SetPixelShaderConstantF(7,s.matrix[0],8),"map matrices")||
               !flashCheck(d->SetPixelShaderConstantF(16,direction,1),"direction")||!flashCheck(d->SetPixelShaderConstantF(36,position,1),"position")||
               !flashCheck(d->SetPixelShaderConstantF(44,color,1),"color"))continue;
            for(unsigned sampler:{2u,3u}){
                d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
                d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
                d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(sampler,D3DSAMP_SRGBTEXTURE,FALSE);
            }
            drawn=true;flashCheck(quad(width/2,height/2),"shadow correction draw");
        }
        return drawn;
    }
