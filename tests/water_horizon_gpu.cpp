// Production WorldComposite: smooth water range, unchanged dry/sky policy and ownership.
#define main northlight_unused_composite_replay
#include "composite_distance_gpu.cpp"
#undef main
#include "world_compiled_shaders.h"
int main(int argc,char** argv){
    assert(argc==2);constexpr unsigned width=128,height=4;constexpr float clipNear=.5f,clipFar=1277,end=360;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightWaterHorizonTest";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,width,height,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=width;pp.BackBufferHeight=height;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    const auto constant=[&](float r,float g,float b,float a){Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};float v[]={r,g,b,a};std::memcpy(im.pixels.data(),v,16);return texture(d,im);};
    auto zero=constant(0,0,0,0),neutral=constant(0,0,0,1),scene=constant(.2f,.3f,.4f,1),distance=constant(clipFar,0,0,0);
    Image depth{width,height,D3DFMT_R32F,4,std::vector<unsigned char>(width*height*4)},mask{width,height,D3DFMT_G32R32F,8,std::vector<unsigned char>(width*height*8)};
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        const float z=5+float(x)*500/(width-1),nd=y==0||y==2?1:clipFar/(clipFar-clipNear)*(1-clipNear/z);
        const float water[]={z+(y==3?100:0),y==0||y==3?1.f:0.f};
        std::memcpy(depth.pixels.data()+(y*width+x)*4,&nd,4);std::memcpy(mask.pixels.data()+(y*width+x)*8,water,8);
    }
    auto depths=texture(d,depth),waters=texture(d,mask);
    for(unsigned slot=0;slot<16;++slot){ok(d->SetTexture(slot,zero));for(auto filter:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(slot,filter,D3DTEXF_POINT));ok(d->SetSamplerState(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(slot,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(slot,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    ok(d->SetTexture(0,scene));ok(d->SetTexture(1,depths));ok(d->SetTexture(9,neutral));ok(d->SetTexture(10,neutral));ok(d->SetTexture(11,waters));ok(d->SetTexture(15,distance));
    IDirect3DPixelShader9 *ordinary=nullptr,*owned=nullptr;ok(d->CreatePixelShader(kWorldCompositeShader,&ordinary));ok(d->CreatePixelShader(kWorldCompositeWaterProbeShader,&owned));
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(width,height,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(width,height,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE})ok(d->SetRenderState(state,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));D3DVIEWPORT9 viewport={0,0,width,height,0,1};ok(d->SetViewport(&viewport));
    Vertex quad[]={{-.5f,-.5f,0,1,0,0},{width-.5f,-.5f,0,1,1,0},{-.5f,height-.5f,0,1,0,1},{width-.5f,height-.5f,0,1,1,1}};
    float c[68][4]{};c[0][0]=1.f/width;c[0][1]=1.f/height;c[0][2]=clipNear;c[0][3]=clipFar;c[1][0]=c[1][1]=100;c[1][2]=1;c[2][0]=1;
    c[3][0]=-1;c[4][1]=1;c[5][2]=-1;c[6][3]=1;c[25][1]=c[25][2]=1;c[30][0]=1;
    c[33][0]=c[33][2]=width;c[33][1]=c[33][3]=height;
    c[34][0]=.6f;c[34][1]=.7f;c[34][2]=.8f;c[57][1]=.75f*end;c[57][3]=10;
    unsigned failures=0,nearWaterChanged=0,pixels=0;
    // Normal -> off -> sky-only -> core-owned -> normal return.
    std::vector<float> first;
    for(unsigned mode=0;mode<5;++mode){
        c[34][3]=mode==1?0:1;c[57][2]=mode==2?0:1/(.25f*end);c[30][1]=mode==3?1:0;
        ok(d->SetPixelShaderConstantF(0,c[0],68));ok(d->SetPixelShader(mode==3?owned:ordinary));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));
        D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
            const float z=5+float(x)*500/(width-1);float nd;std::memcpy(&nd,depth.pixels.data()+(y*width+x)*4,4);
            const float actualZ=clipNear*clipFar/(clipFar-nd*(clipFar-clipNear));
            float range=y==2?1:y==0?z/end:(actualZ-.75f*end)/(.25f*end);
            range=std::clamp(range,0.f,1.f);if(mode==2&&y!=2)range=0;if(mode==3&&y==0)range=0;
            const float amount=mode==1?0:.5f*range*range*(3-2*range);
            for(unsigned k=0;k<4;++k){float got;std::memcpy(&got,static_cast<char*>(lock.pBits)+y*lock.Pitch+(x*4+k)*4,4);
                const float expected=k==3?1:.2f+.1f*k+.4f*amount;
                if(!std::isfinite(got)||std::fabs(got-expected)>.0002f)++failures;
                const unsigned i=(y*width+x)*4+k;if(!mode)first.push_back(got);if(mode==4&&got!=first[i])++failures;
                if(!mode&&!y&&!k&&z<.75f*end&&got>.201f)++nearWaterChanged;
            }++pixels;
        }
        ok(staging->UnlockRect());
    }
    // A completed refracted surface must not be relit using its unrefracted scene depth.
    // Rows: shallow water, dry scene, foreground above water, shallow water again.
    // Strong known lighting makes an incorrect ownership branch visibly fail.
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        const float z=y==2?5.f:10.f,nd=clipFar/(clipFar-clipNear)*(1-clipNear/z);
        const float water[]={9.f,y==1?0.f:1.f};
        std::memcpy(depth.pixels.data()+(y*width+x)*4,&nd,4);
        std::memcpy(mask.pixels.data()+(y*width+x)*8,water,8);
    }
    auto closeDepth=texture(d,depth),closeWater=texture(d,mask);
    auto bright=constant(.1f,.1f,.1f,1),base=constant(.5f,.5f,.5f,1),closeDistance=constant(9,0,0,0);
    ok(d->SetTexture(1,closeDepth));ok(d->SetTexture(11,closeWater));
    ok(d->SetTexture(8,bright));ok(d->SetTexture(12,base));ok(d->SetTexture(15,closeDistance));
    c[34][3]=0;c[30][1]=0;
    unsigned nativeLit=0,foregroundLit=0,shadedChanged=0,returnedChanged=0;
    std::vector<float> native;
    for(unsigned mode=0;mode<3;++mode){
        c[30][2]=mode==1?1:0;
        ok(d->SetPixelShaderConstantF(0,c[0],68));ok(d->SetPixelShader(owned));
        ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());
        ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned k=0;k<3;++k){
            float got;std::memcpy(&got,static_cast<char*>(lock.pBits)+y*lock.Pitch+(x*4+k)*4,4);
            const float original=.2f+.1f*k;const bool wet=y==0||y==3;
            if(!std::isfinite(got))++failures;
            if(!mode){native.push_back(got);if(wet&&got>original+.005f)++nativeLit;}
            if(mode==1){if(wet&&std::fabs(got-original)>.0002f)++shadedChanged;if(!wet&&got>original+.005f)++foregroundLit;}
            if(mode==2&&got!=native[(y*width+x)*3+k])++returnedChanged;
        }
        ok(staging->UnlockRect());
    }
    std::printf("water material ownership: native lit=%u foreground lit=%u shaded changed=%u return changed=%u\n",
        nativeLit,foregroundLit,shadedChanged,returnedChanged);
    failures+=nativeLit<width||foregroundLit<width||shadedChanged||returnedChanged;
    // The early pass also corrects dry source pixels that a wave can sample. The final
    // scene has AO, bloom and haze. Both shallow and deep scene receivers keep
    // full lighting before material absorption; emissive lava stays excluded.
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        const float z=y==2?5.f:y==3?(x<width/2?50.f:9.f):10.f;
        const float nd=clipFar/(clipFar-clipNear)*(1-clipNear/z);
        const float water[]={9.f,y==1?0.f:y==3&&x>=width/2?2.f:1.f};
        std::memcpy(depth.pixels.data()+(y*width+x)*4,&nd,4);
        std::memcpy(mask.pixels.data()+(y*width+x)*8,water,8);
    }
    auto materialDepth=texture(d,depth),materialWater=texture(d,mask);
    auto dark=constant(-.15f,-.15f,-.15f,0),ambient=constant(.3f,.2f,.1f,1.f);
    auto fog=constant(.1f,.2f,.3f,.5f),dither=constant(.01f,.01f,.01f,0);
    IDirect3DPixelShader9* prelight=nullptr;ok(d->CreatePixelShader(kWorldWaterLightingShader,&prelight));
    ok(d->SetTexture(1,materialDepth));ok(d->SetTexture(11,materialWater));ok(d->SetTexture(8,dark));
    ok(d->SetTexture(9,fog));ok(d->SetTexture(10,ambient));ok(d->SetTexture(2,dither));
    c[34][3]=1;c[30][0]=2;c[30][2]=0;
    unsigned submergedChanged=0,otherChanged=0,prelightReturnChanged=0;
    std::vector<float> shaded;
    for(unsigned mode=0;mode<3;++mode){
        // Removing the light must undo the early correction; restoring it is exact.
        ok(d->SetTexture(8,mode==1?zero:dark));
        ok(d->SetPixelShaderConstantF(0,c[0],68));ok(d->SetPixelShader(prelight));
        ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());
        ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned k=0;k<4;++k){
            float got;std::memcpy(&got,static_cast<char*>(lock.pBits)+y*lock.Pitch+(x*4+k)*4,4);
            const float original=k==3?1:.2f+.1f*k;
            const bool submerged=y==0||(y==3&&x<width/2);
            const bool receiver=!(y==3&&x>=width/2);
            const float expected=original*((!mode||mode==2)&&receiver&&k<3?.7f:1);
            if(!std::isfinite(got)||std::fabs(got-expected)>.0002f)++failures;
            if(!mode){shaded.push_back(got);if(submerged&&k<3&&got<original-.005f)++submergedChanged;
                if(!submerged&&receiver&&k<3&&got<original-.005f)++otherChanged;}
            if(mode==2&&got!=shaded[(y*width+x)*4+k])++prelightReturnChanged;
        }
        ok(staging->UnlockRect());
    }
    std::printf("water scene lighting: submerged changed=%u other changed=%u return changed=%u\n",
        submergedChanged,otherChanged,prelightReturnChanged);
    failures+=submergedChanged<width||otherChanged<width||prelightReturnChanged;
    // Feed the lit scene to the final ownership pass. It must not add a second
    // correction at the original, unrefracted receiver positions.
    Image lit{width,height,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(width*height*16)};
    std::memcpy(lit.pixels.data(),shaded.data(),lit.pixels.size());auto litScene=texture(d,lit);
    ok(d->SetTexture(0,litScene));ok(d->SetTexture(9,neutral));ok(d->SetTexture(10,neutral));ok(d->SetTexture(2,zero));
    c[34][3]=0;c[30][0]=1;c[30][2]=1;
    ok(d->SetPixelShaderConstantF(0,c[0],68));ok(d->SetPixelShader(owned));
    ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());
    ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT finalLock{};ok(staging->LockRect(&finalLock,nullptr,D3DLOCK_READONLY));
    unsigned doubled=0;
    for(unsigned x=0;x<width;++x)for(unsigned k=0;k<4;++k){float got;
        std::memcpy(&got,static_cast<char*>(finalLock.pBits)+(x*4+k)*4,4);
        if(!std::isfinite(got)||std::fabs(got-shaded[x*4+k])>.0002f)++doubled;}
    ok(staging->UnlockRect());failures+=doubled;
    std::printf("water scene lighting: second wet correction=%u\n",doubled);
    // A sloping liquid crosses a fixed blocker. Constant opaque depth cannot
    // reproduce this result: visibility must follow the native water receiver.
    IDirect3DPixelShader9* waterVisibility=nullptr;
    ok(d->CreatePixelShader(kWorldWaterVisibilityShader,&waterVisibility));
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        const float water[]={4.f+4.f*x/(width-1),y==0?0.f:1.f};
        std::memcpy(mask.pixels.data()+(y*width+x)*8,water,8);
    }
    auto slopingWater=texture(d,mask),blocker=constant(.5f,.5f,.5f,1),emptyMap=constant(1,1,1,1);
    ok(d->SetTexture(11,slopingWater));
    c[2][1]=48;c[2][2]=192;c[2][3]=1.f/1024;
    for(unsigned row=7;row<15;++row)for(float& value:c[row])value=0;
    c[9][2]=c[13][2]=-.05f;c[10][2]=c[14][2]=.2f;
    c[16][0]=c[16][1]=c[16][3]=0;c[16][2]=1;c[20][2]=1;
    c[24][0]=0;c[35][0]=0;c[59][2]=0;
    unsigned visibilityErrors=0;
    for(unsigned mode=0;mode<3;++mode){
        ok(d->SetTexture(2,mode==1?emptyMap:blocker));ok(d->SetTexture(3,mode==1?emptyMap:blocker));
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0));
        ok(d->SetPixelShaderConstantF(0,c[0],68));ok(d->SetPixelShader(waterVisibility));
        ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());
        ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
            float got;std::memcpy(&got,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,4);
            const float expected=mode==1||y==0||x<width/2?1.f:0.f;
            if(!std::isfinite(got)||std::fabs(got-expected)>.001f)++visibilityErrors;
        }
        ok(staging->UnlockRect());
    }
    failures+=visibilityErrors;
    std::printf("water surface visibility: sloping receiver/off/return errors=%u\n",visibilityErrors);
    waterVisibility->Release();for(auto* t:{slopingWater,blocker,emptyMap})t->Release();
    prelight->Release();for(auto* t:{materialDepth,materialWater,dark,ambient,fog,dither,litScene})t->Release();
    for(auto* t:{closeDepth,closeWater,bright,base,closeDistance})t->Release();
    assert(nearWaterChanged>40);std::printf("water horizon GPU pixels=%u nearWaterChanged=%u failures=%u\n",pixels,nearWaterChanged,failures);
    d->SetPixelShader(nullptr);for(unsigned s=0;s<16;++s)d->SetTexture(s,nullptr);for(auto* t:{zero,neutral,scene,distance,depths,waters})t->Release();ordinary->Release();owned->Release();staging->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
