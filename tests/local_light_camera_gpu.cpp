// Static, animated-bone and attached lamps illuminate a fixed receiver offscreen.
// Use Dalaran-scale coordinates and the production shader/scissor together.
#define main northlight_unused_composite_replay
#include "composite_distance_gpu.cpp"
#undef main
#include "local_light_scissor.h"
#include "native_scene_light_fixture.h"
#include "equipment_lights.h"
#include "particle_lights.h"

static Image solid(float r,float g=0,float b=0,float a=0){
    Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};
    const float values[]={r,g,b,a};std::memcpy(im.pixels.data(),values,16);return im;
}
int main(int argc,char** argv){
    assert(argc==3||argc==4);const bool badView=argc==4&&std::strcmp(argv[3],"wrong-view")==0;
    const bool badPose=argc==4&&std::strcmp(argv[3],"stale-pose")==0;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightLampCamera";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,4,4,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[2]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=4;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(1,1,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    ok(d->CreateOffscreenPlainSurface(1,1,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));
    D3DVIEWPORT9 vp{0,0,1,1,0,1};ok(d->SetViewport(&vp));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(state,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    for(unsigned s=0;s<16;++s){for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,key,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto key:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(s,key,D3DTADDRESS_CLAMP));}
    const float nearZ=.2f,farZ=1277.00488f,distance=5,world[]={5766,573,650};
    const float depth=farZ/(farZ-nearZ)*(1-nearZ/distance)*.94f;
    auto z=texture(d,solid(depth)),normal=texture(d,solid(0,1,0,1)),baseline=texture(d,solid(0,0,0,1)),zero=texture(d,solid(0)),detail=texture(d,solid(.5f,.5f,-distance,1));
    ok(d->SetTexture(1,z));ok(d->SetTexture(9,detail));ok(d->SetTexture(11,zero));ok(d->SetTexture(12,baseline));ok(d->SetTexture(14,normal));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{.5f,-.5f,0,1,1,0},{-.5f,.5f,0,1,0,1},{.5f,.5f,0,1,1,1}};
    unsigned cases=0,failures=0,bulbOffscreen=0;float maxError=0;
    for(unsigned scenario=0;scenario<3;++scenario){
        auto blob=modelAsset();authoredTrack(blob,648+76,4,{{.75f},{.75f},{.75f},{.75f}});
        if(scenario)authoredTrack(blob,320,12,{{0,0,0,2,0,0},{0,0,0,2,0,0}},3);
        Fixture fixture(blob);const auto root=fixture.add(5765.8f);auto& placement=fixture.memory.object(root);
        put(placement,0xe8,578.75f);put(placement,0xec,651.7f);
        if(scenario==2){
            fixture.memory.set(fixture.lights[0],0x64,0u);
            const auto child=fixture.add();auto& attached=fixture.memory.object(child);
            put(attached,0x48,root);put(attached,0x54,0xffffu);put(attached,16,1u|0x40000u);
        }
        std::copy_n(world,3,fixture.camera);fixture.update();assert(fixture.output.count==1);
        NorthlightEquipmentLights::Tracker native;NorthlightParticleLights::Frame particles;NorthlightLocalLightSelection::Tracker selector;
        native.observeScene(fixture.output,1000,{},fixture.camera);native.publish(1000);
        native.observeScene(fixture.output,1100,{},fixture.camera);native.publish(1100);assert(native.outputCount==1);
        const auto reference=native.output[0];
        const float rgb[]={reference.diffuse[0],reference.diffuse[1],reference.diffuse[2],NorthlightLocalLightSelection::transport(reference)};
    for(const char* entry:{"LocalDirect1","LocalDirect","LocalDirect16"}){
        auto code=read(std::string(argv[1])+"/"+entry+".bin");IDirect3DPixelShader9* ps=nullptr;ok(d->CreatePixelShader((const DWORD*)code.data(),&ps));ok(d->SetPixelShader(ps));
        for(float pitch:{0.f,10.f,41.f,66.f,88.f})for(float yaw:{-150.f,-20.f,75.f})for(float sign:{-1.f,1.f})for(float sunCut:{0.f,.7f})for(float bump:{0.f,.75f}){
            const float p=pitch*.01745329252f,y=yaw*.01745329252f;
            const float forward[]={std::cos(y)*std::cos(p),std::sin(y)*std::cos(p),-std::sin(p)};
            const float right[]={std::sin(y),-std::cos(y),0},up[]={std::cos(y)*std::sin(p),std::sin(y)*std::sin(p),std::cos(p)};
            float c[116][4]{};c[0][0]=c[0][1]=.5f;c[0][2]=nearZ;c[0][3]=farZ;c[1][0]=1.10396826f;c[1][1]=1.96261036f;c[1][2]=sign;c[2][0]=1.f/.94f;
            for(unsigned k=0;k<3;++k){c[3][k]=right[k];c[4][k]=up[k];c[5][k]=forward[k]*sign;c[6][k]=world[k]-forward[k]*distance;}c[6][3]=1;
            c[20][2]=.85f;c[33][0]=c[33][1]=2;c[33][2]=c[33][3]=1;c[52][1]=.648f;c[52][2]=sunCut;c[73][0]=bump;c[73][1]=48;
            const unsigned phase=(cases%5)*250;put(fixture.memory.object(fixture.scene),12,100u+phase);
            std::copy_n(c[6],3,fixture.camera);fixture.update();assert(fixture.output.count==1);
            if(badPose&&scenario)fixture.output.lights[0].position[0]=5765.8f;
            const float lamp[]={5765.8f+float(scenario*2)*float(phase)*.001f,578.75f,652.7f,reference.attenuationEnd};
            for(unsigned k=0;k<3;++k)if(std::fabs(fixture.output.lights[0].position[k]-lamp[k])>.003f){
                std::printf("FAIL moving pose scenario=%u phase=%u axis=%u actual=%g expected=%g\n",scenario,phase,k,fixture.output.lights[0].position[k],lamp[k]);return 1;
            }
            float delta[3],length=0;for(unsigned k=0;k<3;++k){delta[k]=lamp[k]-world[k];length+=delta[k]*delta[k];}length=std::sqrt(length);
            const float reach=(lamp[3]-length)*rgb[3],lambert=delta[1]/length;
            const unsigned now=1150+cases*50;native.observeScene(fixture.output,now,{},fixture.camera);native.publish(now);
            particles.publish(native.output.data(),native.outputCount,{},fixture.camera,selector);
            const auto selected=selector.update({},fixture.camera,32,.05f,false,nullptr,particles.output.data(),particles.outputCount);
            assert(selected.count==1);
            std::memcpy(c[36],selected.position[0].data(),16);std::memcpy(c[44],selected.color[0].data(),16);
            std::memcpy(c[84],c[36],16);std::memcpy(c[100],c[44],16);
            float center[3]{};for(unsigned k=0;k<3;++k){const float delta=lamp[k]-c[6][k];center[0]+=right[k]*delta;center[1]+=up[k]*delta;center[2]+=forward[k]*delta;}
            bulbOffscreen+=center[2]<=0||std::fabs(center[0]*c[1][0])>center[2]||std::fabs(center[1]*c[1][1])>center[2];
            auto view=NorthlightLocalLightScissor::view(c[3],c[1],nearZ,3.5f,2,2);assert(view.valid);NorthlightLocalLightScissor::Bounds bounds;NorthlightLocalLightScissor::add(view,lamp,bounds);auto r=NorthlightLocalLightScissor::rect(view,bounds);
            assert(r.area()==1);RECT rect{r.left,r.top,r.right,r.bottom};ok(d->SetScissorRect(&rect));ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));
            if(badView){c[5][0]=0;c[5][1]=0;c[5][2]=sign;}
            ok(d->SetPixelShaderConstantF(0,c[0],116));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());
            ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));float actual[4];std::memcpy(actual,lock.pBits,16);ok(staging->UnlockRect());
            for(unsigned k=0;k<3;++k){const float expected=rgb[k]*reach*reach*lambert*.648f*(1-sunCut),error=std::fabs(actual[k]-expected);maxError=std::max(maxError,error);
                if(!std::isfinite(actual[k])||error>std::max(.00008f,expected*.005f)){if(failures++<3)std::printf("FAIL %s pitch=%g yaw=%g sign=%g channel=%u actual=%g expected=%g\n",entry,pitch,yaw,sign,k,actual[k],expected);}}
            assert(actual[3]==0);++cases;
        }
        ok(d->SetPixelShader(nullptr));ps->Release();
    }
    }
    assert(bulbOffscreen>0);std::printf("LOCAL_LIGHT_CAMERA cases=%u failures=%u bulbOffscreen=%u nativeDraws=0 poseScenarios=3 maxError=%g\n",cases,failures,bulbOffscreen,maxError);
    unsigned rangeCases=0;
    // The actual scaled bonfire's old FGL values must supply the same compact
    // constants as a live light, with no GPU energy beyond twelve yards.
    for(const char* entry:{"LocalDirect1","LocalDirect","LocalDirect16"}){
        auto code=read(std::string(argv[1])+"/"+entry+".bin");IDirect3DPixelShader9* ps=nullptr;ok(d->CreatePixelShader((const DWORD*)code.data(),&ps));ok(d->SetPixelShader(ps));
        for(float separation:{.1f,.5f,1.f,4.f,6.f,11.99f,12.f,16.f,100.f,214.718948f}){
            const NorthlightLocalLights::Light legacy{{0,separation,5},{4,1.882352948f,.627451003f},85.88758f,214.718948f,1,2,2};
            const auto lamp=NorthlightLocalLights::nativeModelTransport(legacy);const float camera[3]{};
            const auto selected=NorthlightLocalLightSelection::select({lamp},camera);assert(selected.count==1);
            float c[116][4]{};c[0][0]=c[0][1]=.5f;c[0][2]=nearZ;c[0][3]=farZ;c[1][0]=c[1][1]=c[1][2]=1;c[2][0]=1.f/.94f;
            c[3][0]=c[4][1]=c[5][2]=c[6][3]=1;c[33][0]=c[33][1]=2;c[33][2]=c[33][3]=1;c[52][1]=.648f;
            std::memcpy(c[36],selected.position[0].data(),16);std::memcpy(c[44],selected.color[0].data(),16);std::memcpy(c[84],c[36],16);std::memcpy(c[100],c[44],16);
            ok(d->SetPixelShaderConstantF(0,c[0],116));ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
            ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));
            D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));float actual[4];std::memcpy(actual,lock.pBits,16);ok(staging->UnlockRect());
            const float fade=std::clamp((12.f-separation)/11.f,0.f,1.f);
            for(unsigned k=0;k<3;++k){const float expected=selected.color[0][k]*fade*fade*.648f;
                if(!std::isfinite(actual[k])||std::fabs(actual[k]-expected)>.00008f){if(failures++<3)std::printf("FAIL native range %s distance=%g actual=%g expected=%g\n",entry,separation,actual[k],expected);}}
            if(separation>=12)assert(actual[0]==0&&actual[1]==0&&actual[2]==0);++rangeCases;
        }
        ok(d->SetPixelShader(nullptr));ps->Release();
    }
    std::printf("NATIVE_STATIC_RANGE_GPU cases=%u failures=%u cutoff=12 oldRadius=214.718948 peak=1\n",rangeCases,failures);
    for(unsigned s=0;s<16;++s)d->SetTexture(s,nullptr);z->Release();normal->Release();baseline->Release();zero->Release();detail->Release();staging->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
