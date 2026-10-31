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
    IDirect3DPixelShader9 *lightingFadePS=nullptr,*fogFadePS=nullptr,*cloudFadePS=nullptr;
    bool shadowTransitionFailed=false;int shadowTransitionSource=-1;
    unsigned shadowTransitionStarts=0,shadowTransitionFailures=0;
    void resetShadowTransitions(){
        for(auto& f:shadowTransition){f.state.reset();f.workingValid=false;}
        shadowTransitionSource=-1;
    }
    void releaseShadowTransitions(){
        resetShadowTransitions();
        for(auto& f:shadowTransition){drop(f.historySurface);drop(f.history);drop(f.dynamicSurface);drop(f.dynamic);}
        drop(lightingFadePS);drop(fogFadePS);drop(cloudFadePS);shadowTransitionFailed=false;
    }
    bool optionalShadowTarget(IDirect3DTexture9*& texture,IDirect3DSurface9*& surface){
        if(texture&&surface)return true;
        IDirect3DTexture9* t=nullptr;IDirect3DSurface9* s=nullptr;
        if(FAILED(d->CreateTexture(1024,1024,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&t,nullptr))||
           !t||FAILED(t->GetSurfaceLevel(0,&s))||!s){drop(s);drop(t);return false;}
        texture=t;surface=s;return true;
    }
    bool shadowTransitionResources(int slot){
        if(shadowTransitionFailed)return false;
        const bool shaders=(lightingFadePS||SUCCEEDED(d->CreatePixelShader(kWorldLightingFadeShader,&lightingFadePS)))&&
            (fogFadePS||SUCCEEDED(d->CreatePixelShader(kWorldFogFadeShader,&fogFadePS)))&&
            (cloudFadePS||SUCCEEDED(d->CreatePixelShader(kFogCloudsFadeShader,&cloudFadePS)));
        auto& f=shadowTransition[slot];
        if(shaders&&optionalShadowTarget(f.history,f.historySurface)&&optionalShadowTarget(f.dynamic,f.dynamicSurface))return true;
        // Allocation failure must retain all active effects and baseline shadows.
        for(auto& item:shadowTransition){if(item.state.fading())item.state.cancel();drop(item.historySurface);drop(item.history);drop(item.dynamicSurface);drop(item.dynamic);}
        drop(lightingFadePS);drop(fogFadePS);drop(cloudFadePS);shadowTransitionFailed=true;++shadowTransitionFailures;
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
    void bindShadowTransition(int source){
        const bool active=shadowTransitionActive(source);
        if(!active)return;
        const int first=source*2;float info[4]={1,1,0,0};
        for(int cascade=0;cascade<2;++cascade){
            const auto& f=shadowTransition[first+cascade];const bool fade=f.state.fading();
            const float* previous=fade?f.previousMatrix:sourceMatrices[source][cascade];
            d->SetPixelShaderConstantF(80+cascade*4,previous,4);
            const auto& old=fade?f.previous:f.working;
            const auto oldDirection=fade?f.state.previous():f.state.current();
            float direction[4]={oldDirection.x,oldDirection.y,oldDirection.z,0};d->SetPixelShaderConstantF(cascade?98:88,direction,1);
            float reproject[16];ShadowTransitionMath::reproject(old,sourceMatrices[source][cascade],cascade?192.f:48.f,reproject);
            d->SetPixelShaderConstantF(90+cascade*4,reproject,4);
            d->SetTexture(6+cascade,fade?f.history:neutralWhite);d->SetTexture(8+cascade,fade?f.dynamic:neutralWhite);
            info[cascade]=f.state.weight();
        }
        d->SetPixelShaderConstantF(89,info,1);
        const auto farDirection=shadowTransition[first+1].state.current();
        float direction[4]={farDirection.x,farDirection.y,farDirection.z,0};d->SetPixelShaderConstantF(99,direction,1);
        float oldInfo[4]={NorthlightWorldMath::ShadowBiasWorld*NorthlightWorldMath::InverseShadowDepth,
            NorthlightWorldMath::ShadowBiasWorld*NorthlightWorldMath::InverseShadowDepth,
            NorthlightWorldMath::InverseShadowDepth,NorthlightWorldMath::InverseShadowDepth};
        d->SetPixelShaderConstantF(100,oldInfo,1);
        for(unsigned s=6;s<10;++s){d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
            d->SetSamplerState(s,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(s,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
            d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE);}
    }
