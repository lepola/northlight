    // Optional static-only history. Current moving casters are never retained in
    // history; their existing draw is redirected into a per-slot dynamic target.
    struct ShadowTransitionGPU {
        NorthlightShadowTransition::Slot state;
        NorthlightWorldMath::ShadowFrame working,previous;
        float previousMatrix[16]={};
        bool workingValid=false;
        IDirect3DTexture9 *history=nullptr,*dynamic=nullptr;
        IDirect3DSurface9 *historySurface=nullptr,*dynamicSurface=nullptr;
    } shadowTransition[4];
    IDirect3DPixelShader9 *transitionCurrentPS=nullptr,*transitionPreviousPS=nullptr,*fogFadePS=nullptr,*cloudFadePS=nullptr;
    IDirect3DPixelShader9 *fogActorFadePS=nullptr,*fogActorTiledFadePS=nullptr,*cloudActorFadePS=nullptr,*cloudActorTiledFadePS=nullptr;
    bool shadowTransitionFailed=false;int shadowTransitionSource=-1;
    unsigned shadowTransitionStarts=0,shadowTransitionFailures=0;
    void resetShadowTransitions(){
        for(auto& f:shadowTransition){f.state.reset();f.workingValid=false;}
        shadowTransitionSource=-1;
    }
    void releaseShadowTransitions(){
        resetShadowTransitions();
        for(auto& f:shadowTransition){drop(f.historySurface);drop(f.history);drop(f.dynamicSurface);drop(f.dynamic);}
        drop(transitionCurrentPS);drop(transitionPreviousPS);drop(fogFadePS);drop(cloudFadePS);drop(fogActorFadePS);drop(fogActorTiledFadePS);drop(cloudActorFadePS);drop(cloudActorTiledFadePS);shadowTransitionFailed=false;
    }
    bool optionalShadowTarget(IDirect3DTexture9*& texture,IDirect3DSurface9*& surface){
        if(texture&&surface)return true;
        IDirect3DTexture9* t=nullptr;IDirect3DSurface9* s=nullptr;
        if(FAILED(d->CreateTexture(1024,1024,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&t,nullptr))||
           !t||FAILED(t->GetSurfaceLevel(0,&s))||!s){drop(s);drop(t);return false;}
        texture=t;surface=s;return true;
    }
    bool shadowTransitionResources(int slot){
        if(shadowTransitionFailed||memoryPressure||geometryMemoryDemand.load(std::memory_order_relaxed))return false;
        if(shadowTransition[slot].history&&shadowTransition[slot].dynamic&&transitionCurrentPS&&transitionPreviousPS)return true;
        D3DCAPS9 caps{};
        const unsigned required=std::max({kDirectionalShadowTransitionCurrentShaderSlots,kDirectionalShadowTransitionPreviousShaderSlots,kWorldFogTransitionShaderSlots,
            kWorldFogActorTransitionShaderSlots,kWorldFogActorTiledTransitionShaderSlots,kFogCloudsTransitionShaderSlots,
            kFogCloudsActorTransitionShaderSlots,kFogCloudsActorTiledTransitionShaderSlots});
        if(FAILED(d->GetDeviceCaps(&caps))||caps.MaxPixelShader30InstructionSlots<required)return false;
        const auto budget=NorthlightGeometryMemory::dynamicBudget(8ull<<20);
        if(!NorthlightGeometryMemory::admits(geometryAdmission(budget),budget))return false;
        const bool shaders=(transitionCurrentPS||SUCCEEDED(d->CreatePixelShader(kDirectionalShadowTransitionCurrentShader,&transitionCurrentPS)))&&
            (transitionPreviousPS||SUCCEEDED(d->CreatePixelShader(kDirectionalShadowTransitionPreviousShader,&transitionPreviousPS)))&&
            (fogFadePS||SUCCEEDED(d->CreatePixelShader(kWorldFogTransitionShader,&fogFadePS)))&&
            (cloudFadePS||SUCCEEDED(d->CreatePixelShader(kFogCloudsTransitionShader,&cloudFadePS)))&&
            (fogActorFadePS||SUCCEEDED(d->CreatePixelShader(kWorldFogActorTransitionShader,&fogActorFadePS)))&&
            (fogActorTiledFadePS||SUCCEEDED(d->CreatePixelShader(kWorldFogActorTiledTransitionShader,&fogActorTiledFadePS)))&&
            (cloudActorFadePS||SUCCEEDED(d->CreatePixelShader(kFogCloudsActorTransitionShader,&cloudActorFadePS)))&&
            (cloudActorTiledFadePS||SUCCEEDED(d->CreatePixelShader(kFogCloudsActorTiledTransitionShader,&cloudActorTiledFadePS)));
        auto& f=shadowTransition[slot];
        if(shaders&&optionalShadowTarget(f.history,f.historySurface)&&optionalShadowTarget(f.dynamic,f.dynamicSurface))return true;
        // Allocation failure must retain all active effects and baseline shadows.
        for(auto& item:shadowTransition){if(item.state.fading())item.state.cancel();drop(item.historySurface);drop(item.history);drop(item.dynamicSurface);drop(item.dynamic);}
        drop(transitionCurrentPS);drop(transitionPreviousPS);drop(fogFadePS);drop(cloudFadePS);drop(fogActorFadePS);drop(fogActorTiledFadePS);drop(cloudActorFadePS);drop(cloudActorTiledFadePS);shadowTransitionFailed=true;++shadowTransitionFailures;
        logf("WORLD shadow transition unavailable: retaining instantaneous cache updates");return false;
    }
    // Save the exact old 1024 working crop, excluding its old dynamic casters.
    // The existing union is used with a white dynamic input: no new caster draw.
    bool preserveShadowStatic(int slot){
        auto& f=shadowTransition[slot];const auto& key=shadowCacheKey[slot];
        long x=0,y=0;float dz=0;
        if(!f.workingValid||!key.valid||!NorthlightWorldMath::cacheOffset(f.working,key.frame,x,y,dz)||
           std::labs(x)>long(ShadowCacheMargin)||std::labs(y)>long(ShadowCacheMargin)||!shadowTransitionResources(slot))return false;
        bool ok=SUCCEEDED(d->SetDepthStencilSurface(nullptr));
        auto set=[&](HRESULT result){ok=SUCCEEDED(result)&&ok;};
        for(unsigned s=0;s<16;++s)set(d->SetTexture(s,nullptr));
        set(d->SetRenderTarget(0,f.historySurface));
        set(d->SetRenderState(D3DRS_ZENABLE,FALSE));set(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
        set(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));set(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
        set(d->SetVertexShader(nullptr));set(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));set(d->SetStreamSourceFreq(0,1));
        set(d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0));set(d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));
        set(d->SetTexture(0,neutralWhite));set(d->SetTexture(1,shadowCache[slot]));
        for(unsigned s=0;s<2;++s){set(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));set(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
            set(d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT));set(d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
            set(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));set(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));}
        float c[2][4]={{float(long(ShadowCacheMargin)+x),float(long(ShadowCacheMargin)-y),dz,float(ShadowCacheSize)},
                       {1.f/1024,1.f/float(ShadowCacheSize),0,0}};
        ok=ok&&SUCCEEDED(d->SetPixelShaderConstantF(0,&c[0][0],2))&&SUCCEEDED(d->SetPixelShader(unionPS))&&SUCCEEDED(quad(1024,1024));
        set(d->SetTexture(0,nullptr));set(d->SetTexture(1,nullptr));
        set(d->SetRenderState(D3DRS_ZENABLE,TRUE));set(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));
        set(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR));set(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR));
        if(ok){f.previous=f.working;NorthlightWorldMath::shadowMatrixFrom(f.previous,slot%2?192.f:48.f,f.previousMatrix);}
        return ok;
    }
    bool shadowTransitionActive(int source)const{
        return effects.shadows&&!shadowTransitionFailed&&(shadowTransition[source*2].state.fading()||shadowTransition[source*2+1].state.fading());
    }
    bool shadowVolumeTransitionActive(int source)const{
        return shadowTransitionActive(source)&&shadowTransition[source*2+1].state.fading();
    }
    // c80..100 and s6..9 belong only to the transition variants. Other passes
    // keep their original sampler bindings, including the GI probe atlas.
    void bindShadowTransition(int source,bool previousEpoch=false){
        const bool active=shadowTransitionActive(source);
        if(!active&&!shadowTransitionActive(1-source))return;
        const int first=source*2;float info[4]={1,1,0,0};
        for(int cascade=0;cascade<2;++cascade){
            const auto& f=shadowTransition[first+cascade];const bool fade=f.state.fading();
            const float* previous=fade?f.previousMatrix:sourceMatrices[source][cascade];
            d->SetPixelShaderConstantF(80+cascade*4,previous,4);
            const NorthlightShadowTransition::Direction fallback{sourceDirections[source].x,sourceDirections[source].y,sourceDirections[source].z};
            const auto oldDirection=fade?f.state.previous():f.state.valid()?f.state.current():fallback;
            float direction[4]={oldDirection.x,oldDirection.y,oldDirection.z,0};d->SetPixelShaderConstantF(cascade?98:88,direction,1);
            float reproject[16]={};
            if(fade)ShadowTransitionMath::reproject(f.previous,sourceMatrices[source][cascade],cascade?192.f:48.f,reproject);
            else for(unsigned i=0;i<4;++i)reproject[i*5]=1;
            d->SetPixelShaderConstantF(90+cascade*4,reproject,4);
            d->SetTexture(6+cascade,fade?f.history:neutralWhite);d->SetTexture(8+cascade,fade?f.dynamic:neutralWhite);
            info[cascade]=f.state.weight();
        }
        info[2]=quality.softShadows?1.f:0.f;info[3]=previousEpoch?1.f:0.f;
        d->SetPixelShaderConstantF(89,info,1);
        d->SetPixelShaderConstantF(105,sourceMatrices[source][0],4);
        d->SetPixelShaderConstantF(109,sourceMatrices[source][1],4);
        const NorthlightShadowTransition::Direction fallback{sourceDirections[source].x,sourceDirections[source].y,sourceDirections[source].z};
        const auto nearDirection=shadowTransition[first].state.valid()?shadowTransition[first].state.current():fallback;
        const float currentNear[4]={nearDirection.x,nearDirection.y,nearDirection.z,0};d->SetPixelShaderConstantF(113,currentNear,1);
        if(previousEpoch){
            d->SetPixelShaderConstantF(7,shadowTransition[first].state.fading()?shadowTransition[first].previousMatrix:sourceMatrices[source][0],4);
            d->SetPixelShaderConstantF(11,shadowTransition[first+1].state.fading()?shadowTransition[first+1].previousMatrix:sourceMatrices[source][1],4);
            const auto oldNear=shadowTransition[first].state.fading()?shadowTransition[first].state.previous():nearDirection;
            const float old[4]={oldNear.x,oldNear.y,oldNear.z,source==1?1.f:0.f};d->SetPixelShaderConstantF(16,old,1);
        }
        const auto currentFarDirection=shadowTransition[first+1].state.valid()?shadowTransition[first+1].state.current():fallback;
        const float currentFar[4]={currentFarDirection.x,currentFarDirection.y,currentFarDirection.z,0};d->SetPixelShaderConstantF(114,currentFar,1);
        const auto farDirection=previousEpoch&&shadowTransition[first+1].state.fading()?shadowTransition[first+1].state.previous():shadowTransition[first+1].state.valid()?shadowTransition[first+1].state.current():fallback;
        float direction[4]={farDirection.x,farDirection.y,farDirection.z,0};d->SetPixelShaderConstantF(99,direction,1);
        float oldInfo[4]={NorthlightWorldMath::ShadowBiasWorld*NorthlightWorldMath::InverseShadowDepth,
            NorthlightWorldMath::ShadowBiasWorld*NorthlightWorldMath::InverseShadowDepth,
            NorthlightWorldMath::InverseShadowDepth,NorthlightWorldMath::InverseShadowDepth};
        d->SetPixelShaderConstantF(100,oldInfo,1);
        const auto& farSlot=shadowTransition[first+1];float inverse[16]={};
        if(farSlot.state.fading())ShadowTransitionMath::reproject(farSlot.working,farSlot.previousMatrix,192.f,inverse);
        else for(unsigned i=0;i<4;++i)inverse[i*5]=1;
        d->SetPixelShaderConstantF(101,inverse,4);
        for(unsigned s=6;s<10;++s){d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
            d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
            d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE);}
    }
