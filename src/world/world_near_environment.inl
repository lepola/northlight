// Included inside WorldRenderer. Only the environment is held between near
// updates; moving casters and the final union always use the current frame.
    IDirect3DTexture9* nearEnvironment[2]={};
    IDirect3DSurface9* nearEnvironmentSurface[2]={};
    DWORD nearEnvironmentRetry[2]={};
    void releaseNearEnvironment(){
        for(unsigned s=0;s<2;++s){drop(nearEnvironmentSurface[s]);drop(nearEnvironment[s]);nearEnvironmentRetry[s]=0;nearShadow[s].invalidate();}
    }
    bool nearEnvironmentResources(unsigned source){
        if(nearEnvironmentSurface[source])return true;
        if(nearEnvironmentRetry[source]&&LONG(GetTickCount()-nearEnvironmentRetry[source])<0)return false;
        const auto budget=NorthlightGeometryMemory::defaultTargetBudget();
        if(!NorthlightGeometryMemory::admits(geometryAdmission(budget),budget)){nearEnvironmentRetry[source]=GetTickCount()+1000;return false;}
        HRESULT hr=d->CreateTexture(1024,1024,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&nearEnvironment[source],nullptr);
        if(SUCCEEDED(hr))hr=nearEnvironment[source]->GetSurfaceLevel(0,&nearEnvironmentSurface[source]);
        nearShadow[source].invalidate();
        if(SUCCEEDED(hr))return true;
        drop(nearEnvironmentSurface[source]);drop(nearEnvironment[source]);nearEnvironmentRetry[source]=GetTickCount()+1000;
        return false; // Allocation refusal keeps full, fresh near shadows.
    }
    bool unionShadow(IDirect3DSurface9* target,IDirect3DTexture9* live,IDirect3DTexture9* cached,float x,float y,float dz,UINT size){
        d->SetDepthStencilSurface(nullptr);if(!check(d->SetRenderTarget(0,target),"shadow target"))return false;
        d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
        d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetStreamSourceFreq(0,1);
        d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        d->SetTexture(0,live);d->SetTexture(1,cached);
        for(unsigned sampler=0;sampler<2;++sampler){d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(sampler,D3DSAMP_SRGBTEXTURE,FALSE);}
        float constants[2][4]={{x,y,dz,float(size)},{1.f/1024,1.f/float(size),0,0}};
        d->SetPixelShaderConstantF(0,constants[0],2);d->SetPixelShader(unionPS);
        if(!check(quad(1024,1024),"shadow union"))return false;
        d->SetTexture(0,nullptr);d->SetTexture(1,nullptr);
        d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);
        d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        return true;
    }
