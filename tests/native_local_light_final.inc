// Complete production renderer direct + point correction + final HDR composite,
// driven by the actual native endpoints produced above (no inferred native term).
static void finalContract(IDirect3DDevice9* d,const std::string& compiled,const std::vector<std::vector<float>>& native,unsigned size){
    IDirect3DPixelShader9 *direct=nullptr,*correction=nullptr,*final=nullptr;
    auto code=words((compiled+"/LocalDirect.bin").c_str());must(d->CreatePixelShader(code.data(),&direct));
    must(d->CreatePixelShader(kLocalLightingShader,&correction));code=words((compiled+"/WorldCompositeHdr.bin").c_str());must(d->CreatePixelShader(code.data(),&final));
    auto tex=[&](const std::vector<float>& data,unsigned width,unsigned height){IDirect3DTexture9* t=nullptr;must(d->CreateTexture(width,height,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&t,nullptr));D3DLOCKED_RECT lock;must(t->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<height;++y)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,data.data()+y*width*4,width*16);must(t->UnlockRect(0));return t;};
    auto one=[&](float x,float y,float z,float w){return tex({x,y,z,w},1,1);};
    std::vector<IDirect3DTexture9*> owned;auto keep=[&](IDirect3DTexture9* t){owned.push_back(t);return t;};
    auto *zero=keep(one(0,0,0,0)),*fog=keep(one(0,0,0,1)),*base=keep(one(.3f,.3f,.3f,1)),*normal=keep(one(0,0,-1,1)),*distance=keep(one(8,0,0,0));
    const float depth=64.f/63.9f*(1-.1f/8);auto* depthTexture=keep(one(depth,0,0,0));
    IDirect3DCubeTexture9* cube=nullptr;must(d->CreateCubeTexture(256,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&cube,nullptr));
    IDirect3DTexture9 *local=nullptr;must(d->CreateTexture(size,size,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&local,nullptr));owned.push_back(local);
    IDirect3DSurface9 *localSurface=nullptr,*target=nullptr;must(local->GetSurfaceLevel(0,&localSurface));must(d->CreateRenderTarget(size,size,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    struct Screen {float x,y,z,w,u,v;};const Screen quad[]={{-.5f,-.5f,0,1,0,0},{size-.5f,-.5f,0,1,1,0},{-.5f,size-.5f,0,1,0,1},{size-.5f,size-.5f,0,1,1,1}};
    must(d->SetVertexShader(nullptr));must(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));must(d->SetDepthStencilSurface(nullptr));D3DVIEWPORT9 viewport{0,0,size,size,0,1};must(d->SetViewport(&viewport));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SEPARATEALPHABLENDENABLE})must(d->SetRenderState(state,FALSE));must(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    for(unsigned slot=0;slot<16;++slot){must(d->SetTexture(slot,zero));for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})must(d->SetSamplerState(slot,key,D3DTEXF_POINT));for(auto key:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})must(d->SetSamplerState(slot,key,D3DTADDRESS_CLAMP));must(d->SetSamplerState(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE));must(d->SetSamplerState(slot,D3DSAMP_SRGBTEXTURE,FALSE));}
    auto draw=[&]{must(d->BeginScene());must(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Screen)));must(d->EndScene());};
    float world[224][4]{};world[0][0]=world[0][1]=1.f/size;world[0][2]=.1f;world[0][3]=64;world[1][0]=world[1][1]=world[1][2]=1;world[2][0]=1;for(unsigned i=0;i<4;++i)world[3+i][i]=1;
    world[20][2]=1;world[25][1]=world[25][2]=1;world[33][0]=world[33][1]=world[33][2]=world[33][3]=float(size);world[36][2]=4;world[36][3]=12;world[44][0]=1;world[44][1]=.55f;world[44][2]=.2f;world[44][3]=1.f/11;world[52][0]=world[52][1]=1;
    float point[13][4]{};std::memcpy(point,world,7*16);point[2][2]=1;point[2][3]=.025f;point[7][2]=4;point[7][3]=1.f/11;std::memcpy(point[8],world[44],12);point[8][3]=12;point[9][0]=.1f;point[9][1]=12;point[9][2]=1.f/256;point[9][3]=1;point[10][0]=1;point[10][2]=1;
    std::vector<std::vector<float>> result;unsigned positive=0,partial=0;float maxBlockedError=0;
    for(unsigned mode=0;mode<native.size();++mode){ // Baseline, clear, blocked, edge, no-slot reference, zero source, gain0, gainHalf.
        const unsigned visibility=mode<2?0:mode==3?2:1;
        for(unsigned face=0;face<6;++face){D3DLOCKED_RECT lock;must(cube->LockRect(D3DCUBEMAP_FACES(face),0,&lock,nullptr,0));for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x)reinterpret_cast<float*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=visibility==0?1.f:visibility==1?0.f:x<128?1.f:0.f;must(cube->UnlockRect(D3DCUBEMAP_FACES(face),0));}
        must(d->SetTexture(3,zero));must(d->SetRenderTarget(0,localSurface));must(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));must(d->SetTexture(1,depthTexture));must(d->SetTexture(14,normal));must(d->SetTexture(12,base));must(d->SetTexture(9,zero));must(d->SetTexture(11,zero));must(d->SetPixelShaderConstantF(0,world[0],224));must(d->SetPixelShader(direct));draw();
        if(!mode){const auto image=pixels(d,localSurface,size);for(unsigned i=0;i<image.size();i+=4)positive+=image[i]>.01f;assert(positive>size*size/2);}
        point[2][2]=mode==6?0.f:mode==7?.5f:1.f;must(d->SetPixelShader(correction));must(d->SetPixelShaderConstantF(0,point[0],13));must(d->SetTexture(0,depthTexture));must(d->SetTexture(1,cube));must(d->SetTexture(3,normal));must(d->SetTexture(4,base));must(d->SetTexture(5,zero));must(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));must(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));must(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));must(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));draw();
        const auto corrected=pixels(d,localSurface,size);if(mode==2)for(unsigned i=0;i<corrected.size();i+=4)maxBlockedError=std::max(maxBlockedError,std::fabs(corrected[i]));
        auto* scene=tex(native[mode],size,size);must(d->SetRenderTarget(0,target));must(d->SetPixelShader(final));must(d->SetPixelShaderConstantF(0,world[0],224));must(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));must(d->SetTexture(0,scene));must(d->SetTexture(1,depthTexture));must(d->SetTexture(2,scene));must(d->SetTexture(3,local));must(d->SetTexture(7,zero));must(d->SetTexture(8,zero));must(d->SetTexture(9,fog));must(d->SetTexture(10,fog));must(d->SetTexture(12,base));must(d->SetTexture(14,normal));must(d->SetTexture(15,distance));draw();result.push_back(pixels(d,target,size));must(d->SetTexture(0,zero));must(d->SetTexture(2,zero));scene->Release();
    }
    assert(result[0]==result[1]&&result[0]==result[6]&&result[4]==result[5]);
    for(unsigned y=80;y<176;++y)for(unsigned x=80;x<176;++x)for(unsigned c=0;c<4;++c){unsigned i=(y*size+x)*4+c;assert(result[2][i]==result[4][i]);assert(result[2][i]<=result[7][i]+.003f&&result[7][i]<=result[0][i]+.003f);assert(result[2][i]<=result[3][i]+.003f&&result[3][i]<=result[0][i]+.003f);partial+=result[3][i]>result[2][i]+.003f&&result[3][i]<result[0][i]-.003f;}
    std::printf("NATIVE_FINAL_CHECK partialPixels=%u rendererBlockedMax=%g\n",partial,maxBlockedError);
    assert(maxBlockedError<.01f&&partial);std::printf("NATIVE_FINAL_PASS clear=exact-baseline blocked=exact-no-native-slot zero=exact gain0=exact-baseline gainHalf=bounded partialPixels=%u rendererDirectPositive=%u rendererBlockedMax=%g originalRendererPath=unchanged\n",partial,positive,maxBlockedError);
    for(unsigned slot=0;slot<16;++slot)must(d->SetTexture(slot,nullptr));for(auto* t:owned)t->Release();cube->Release();localSurface->Release();target->Release();direct->Release();correction->Release();final->Release();
}
