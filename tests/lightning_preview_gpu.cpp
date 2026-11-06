#define main composite_fixture_main
#include "composite_distance_gpu.cpp"
#undef main
#include "local_light_selection.h"
#include "particle_lights.h"
#include <cwchar>
struct SkyScreen {float x,y,z,w;DWORD colour;float u,v,u1,v1;};
struct CasterVertex {float x,y,z,nx,ny,nz,u,v;};
static void saveSurface(IDirect3DDevice9* d,IDirect3DSurface9* target,IDirect3DSurface9* staging,unsigned w,unsigned h,const std::string& file){
    ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
    std::ofstream out(file,std::ios::binary);const unsigned header[]={0x31524746,w,h,D3DFMT_A32B32G32R32F,16};out.write(reinterpret_cast<const char*>(header),20);
    for(unsigned y=0;y<h;++y)out.write(static_cast<const char*>(lock.pBits)+y*lock.Pitch,w*16);
    ok(staging->UnlockRect());assert(out);
}
int main(int argc,char** argv){
    assert(argc==4||argc==5);const bool live=argc==5,visible=live&&std::string(argv[4])=="--interactive";
    const std::string prefix=argv[1];const auto depth=image(prefix+"depth.fgr"),normals=image(prefix+"normals.fgr");const unsigned W=depth.width,H=depth.height;
    auto bytes=read(prefix+"constants.f32");assert(bytes.size()==84*16);float c[84][4];std::memcpy(c,bytes.data(),bytes.size());
    bytes=read(prefix+"source.bin");assert(bytes.size()==48);NorthlightLocalLights::Light light;std::memcpy(&light,bytes.data(),48);assert(NorthlightLocalLights::valid(light));
    const auto sprite=image(prefix+"bolt0.fgr");const bool mask=std::ifstream(prefix+"bolt1.fgr").good();
    bytes=read(prefix+"bolt-vertices.bin");assert(bytes.size()%sizeof(SkyScreen)==0);std::vector<SkyScreen> sky(bytes.size()/sizeof(SkyScreen));std::memcpy(sky.data(),bytes.data(),bytes.size());
    auto info=read(prefix+"bolt-info.bin");assert(info.size()==8);unsigned mode,tint;std::memcpy(&mode,info.data(),4);std::memcpy(&tint,info.data()+4,4);
    unsigned period=0,clipStart=0,clipEnd=0;float reference[4]{};std::vector<float> timeline;
    if(live){bytes=read(prefix+"timeline.bin");assert(bytes.size()>=36);unsigned header[5];std::memcpy(header,bytes.data(),20);assert(header[0]==0x31464c4e);period=header[1];clipStart=header[2];clipEnd=header[3];
        assert(period&&clipStart<clipEnd&&clipEnd<=period&&bytes.size()==36+size_t(period)*16);std::memcpy(reference,bytes.data()+20,16);timeline.resize(size_t(period)*4);std::memcpy(timeline.data(),bytes.data()+36,timeline.size()*4);}
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightLightningPreview";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    RECT rect{0,0,LONG(W),LONG(H)};const DWORD style=visible?WS_OVERLAPPEDWINDOW:WS_OVERLAPPED;AdjustWindowRect(&rect,style,FALSE);
    auto window=CreateWindowExW(0,wc.lpszClassName,L"Northlight lightning stand",style,CW_USEDEFAULT,CW_USEDEFAULT,live?rect.right-rect.left:64,live?rect.bottom-rect.top:64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[3]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=live?W:64;pp.BackBufferHeight=live?H:64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=visible?D3DPRESENT_INTERVAL_ONE:D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DSurface9 *target=nullptr,*staging=nullptr,*backbuffer=nullptr,*boltSurface=nullptr,*displayRead=nullptr;
    IDirect3DTexture9 *lightTexture=nullptr,*boltTexture=nullptr,*baseTexture=nullptr,*albedoTexture=nullptr;IDirect3DPixelShader9* compose=nullptr;
    if(live){
        ok(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&lightTexture,nullptr));ok(lightTexture->GetSurfaceLevel(0,&target));
        ok(d->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&boltTexture,nullptr));ok(boltTexture->GetSurfaceLevel(0,&boltSurface));ok(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer));
        baseTexture=texture(d,image(prefix+"base.fgr"));albedoTexture=texture(d,image(prefix+"albedo.fgr"));auto shader=read(prefix+"Compose.bin");ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(shader.data()),&compose));
        if(!visible)ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&displayRead,nullptr));
    }else ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    auto dt=texture(d,depth),nt=texture(d,normals),zero=texture(d,{1,1,D3DFMT_A8R8G8B8,4,{0,0,0,0}}),bolt=texture(d,sprite);
    auto mt=mask?texture(d,image(prefix+"bolt1.fgr")):nullptr;
    const std::string shaderDirectory=std::string(argv[2]).substr(0,std::string(argv[2]).find_last_of("/\\")+1);
    auto code=read(argv[2]);IDirect3DPixelShader9* shader=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shader));
    IDirect3DPixelShader9 *correction=nullptr,*casterPS=nullptr;IDirect3DVertexShader9* casterVS=nullptr;
    code=read(shaderDirectory+"FlashShadow.bin");ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&correction));
    code=read(shaderDirectory+"ShadowPS.bin");ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&casterPS));
    code=read(shaderDirectory+"ShadowVS.bin");ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()),&casterVS));
    bytes=read(prefix+"casters.bin");assert(bytes.size()%sizeof(CasterVertex)==0);std::vector<CasterVertex> casters(bytes.size()/sizeof(CasterVertex));std::memcpy(casters.data(),bytes.data(),bytes.size());
    IDirect3DTexture9* shadow[2]{};IDirect3DSurface9 *shadowSurface[2]{},*shadowDepth=nullptr;
    ok(d->CreateDepthStencilSurface(1024,1024,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&shadowDepth,nullptr));
    for(unsigned s=0;s<16;++s){ok(d->SetTexture(s,zero));for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,k,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto k:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(s,k,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));}
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));ok(d->SetVertexShader(nullptr));
    for(auto k:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_LIGHTING})ok(d->SetRenderState(k,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    for(unsigned cascade=0;cascade<2;++cascade){
        ok(d->CreateTexture(1024,1024,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,shadow+cascade,nullptr));ok(shadow[cascade]->GetSurfaceLevel(0,shadowSurface+cascade));
        ok(d->SetRenderTarget(0,shadowSurface[cascade]));ok(d->SetDepthStencilSurface(shadowDepth));D3DVIEWPORT9 sv{0,0,1024,1024,0,1};ok(d->SetViewport(&sv));
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
        ok(d->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1));ok(d->SetVertexShader(casterVS));ok(d->SetVertexShaderConstantF(0,c[7+cascade*4],4));ok(d->SetPixelShader(casterPS));const float opaque[]={0,0,0,-1};ok(d->SetPixelShaderConstantF(0,opaque,1));
        ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,UINT(casters.size()/3),casters.data(),sizeof(CasterVertex)));ok(d->EndScene());
    }
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));ok(d->SetVertexShader(nullptr));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
    D3DVIEWPORT9 vp{0,0,W,H,0,1};ok(d->SetViewport(&vp));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
    bool paused=false,slow=false,shadows=true,fullCycle=false,capturedDark=false,capturedFlash=false;double tick=clipStart;
    LARGE_INTEGER frequency{},last{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&last);
    if(visible){ShowWindow(window,SW_SHOW);UpdateWindow(window);}
    for(unsigned phase=0;live?(visible||phase<180):phase<3;++phase){
        unsigned activeTint=phase==1?tint:tint&0xffffff;float energy=0;
        if(live){
            MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}if(!IsWindow(window))break;
            if(visible&&GetForegroundWindow()==window){
                if(GetAsyncKeyState(VK_ESCAPE)&1)break;
                if(GetAsyncKeyState(VK_SPACE)&1)paused=!paused;if(GetAsyncKeyState('S')&1)slow=!slow;if(GetAsyncKeyState('L')&1)shadows=!shadows;
                if(GetAsyncKeyState('T')&1)fullCycle=!fullCycle;if(GetAsyncKeyState('R')&1)tick=fullCycle?0:clipStart;
            }
            LARGE_INTEGER now;QueryPerformanceCounter(&now);const double dt=visible?std::min(250.,1000.*double(now.QuadPart-last.QuadPart)/frequency.QuadPart):1000./60;last=now;
            if(!paused)tick+=dt*(slow?.25:1);const unsigned begin=fullCycle?0:clipStart,end=fullCycle?period:clipEnd;
            if(tick>=end||tick<begin)tick=begin+std::fmod(std::max(0.,tick-begin),double(end-begin));
            const float* rgba=timeline.data()+4*unsigned(tick);activeTint=0;
            for(unsigned k=0;k<4;++k){const auto byte=DWORD(std::lround(std::clamp(rgba[k],0.f,1.f)*255));activeTint|=byte<<(k==3?24:16-8*k);}
            for(unsigned k=0;k<3;++k){c[44][k]=reference[k]*reference[3]>0?light.diffuse[k]*rgba[k]*rgba[3]/(reference[k]*reference[3]):0;energy=std::max(energy,c[44][k]);}
            if(energy<NorthlightParticleLights::MinEnergy)for(unsigned k=0;k<3;++k)c[44][k]=0;
            if(visible&&phase%6==0){wchar_t title[320];std::swprintf(title,320,L"Northlight lightning | %ls | %ls | Shadows %ls | %ls | %.3fs | Space: pause   S: slow   L: shadows   T: full cycle   R: restart   Esc: exit",paused?L"PAUSED":L"PLAY",slow?L"0.25x":L"1x",shadows?L"ON":L"OFF",fullCycle?L"native cycle":L"fragment repeat",tick*.001);SetWindowTextW(window,title);}
            for(unsigned s=0;s<4;++s)ok(d->SetTexture(s,nullptr));ok(d->SetRenderTarget(0,target));
        }
        ok(d->SetTexture(1,dt));ok(d->SetTexture(14,nt));ok(d->SetTexture(11,zero));ok(d->SetTexture(9,zero));ok(d->SetTexture(12,zero));
        for(unsigned k=0;k<3;++k){c[36][k]=light.position[k];if(!live)c[44][k]=phase==1?light.diffuse[k]:0;}
        c[36][3]=light.attenuationEnd;c[44][3]=NorthlightLocalLightSelection::transport(light);
        ok(d->SetPixelShaderConstantF(0,c[0],84));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetPixelShader(shader));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
        ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());
        if(!live)saveSurface(d,target,staging,W,H,prefix+"unshadowed-"+std::to_string(phase)+".fgr");
        if(shadows){
        ok(d->SetTexture(2,shadow[0]));ok(d->SetTexture(3,shadow[1]));ok(d->SetPixelShader(correction));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));
        ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
        ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());
        }
        if(!live)saveSurface(d,target,staging,W,H,prefix+"light-"+std::to_string(phase)+".fgr");
        else ok(d->SetRenderTarget(0,boltSurface));
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->SetPixelShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX2));
        ok(d->SetTexture(0,bolt));ok(d->SetTexture(1,mt));
        for(unsigned slot=0;slot<2;++slot){for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(slot,k,D3DTEXF_LINEAR));
            ok(d->SetTextureStageState(slot,D3DTSS_TEXCOORDINDEX,slot));ok(d->SetTextureStageState(slot,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));
            ok(d->SetTextureStageState(slot,D3DTSS_COLOROP,slot==1&&!mask?D3DTOP_DISABLE:D3DTOP_MODULATE));
            ok(d->SetTextureStageState(slot,D3DTSS_COLORARG1,D3DTA_TEXTURE));ok(d->SetTextureStageState(slot,D3DTSS_COLORARG2,slot?D3DTA_CURRENT:D3DTA_DIFFUSE));
            ok(d->SetTextureStageState(slot,D3DTSS_ALPHAOP,slot||!mode?D3DTOP_MODULATE:D3DTOP_SELECTARG2));ok(d->SetTextureStageState(slot,D3DTSS_ALPHAARG1,D3DTA_TEXTURE));ok(d->SetTextureStageState(slot,D3DTSS_ALPHAARG2,slot?D3DTA_CURRENT:D3DTA_DIFFUSE));
        }
        ok(d->SetTextureStageState(2,D3DTSS_COLOROP,D3DTOP_DISABLE));
        for(auto& v:sky)v.colour=activeTint;
        ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
        ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,UINT(sky.size()/3),sky.data(),sizeof(SkyScreen)));ok(d->EndScene());
        if(!live)saveSurface(d,target,staging,W,H,prefix+"bolt-"+std::to_string(phase)+".fgr");
        else{
            ok(d->SetRenderTarget(0,backbuffer));ok(d->SetPixelShader(compose));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
            IDirect3DTexture9* inputs[]={baseTexture,albedoTexture,lightTexture,boltTexture};for(unsigned s=0;s<4;++s){ok(d->SetTexture(s,inputs[s]));for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,k,D3DTEXF_POINT));}
            ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());
            const bool atPeak=energy>=.9f*std::max({light.diffuse[0],light.diffuse[1],light.diffuse[2]});
            if(!visible&&((!capturedDark&&energy==0)||(!capturedFlash&&atPeak))){
                const bool bright=atPeak;ok(d->GetRenderTargetData(backbuffer,displayRead));D3DLOCKED_RECT lock{};ok(displayRead->LockRect(&lock,nullptr,D3DLOCK_READONLY));
                std::ofstream out(prefix+(bright?"live-flash.bgra":"live-dark.bgra"),std::ios::binary);for(unsigned y=0;y<H;++y)out.write(static_cast<const char*>(lock.pBits)+y*lock.Pitch,W*4);ok(displayRead->UnlockRect());
                if(bright)capturedFlash=true;else capturedDark=true;
            }
            ok(d->Present(nullptr,nullptr,nullptr,nullptr));if(visible&&IsIconic(window))Sleep(50);
        }
    }
    if(live&&!visible)assert(capturedDark&&capturedFlash);
    for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));ok(d->SetPixelShader(nullptr));if(compose)compose->Release();for(auto p:{backbuffer,boltSurface,displayRead})if(p)p->Release();for(auto p:{lightTexture,boltTexture,baseTexture,albedoTexture})if(p)p->Release();shader->Release();correction->Release();casterPS->Release();casterVS->Release();for(auto p:shadowSurface)p->Release();for(auto p:shadow)p->Release();shadowDepth->Release();if(mt)mt->Release();bolt->Release();zero->Release();nt->Release();dt->Release();staging->Release();target->Release();d->Release();api->Release();FreeLibrary(module);if(IsWindow(window))DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::puts(live?"PASS interactive lightning stand":"PASS offline GPU lightning frames: before/peak/after");
}
