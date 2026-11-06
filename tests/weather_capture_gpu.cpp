// Weather capture roundtrip on a private device, with actual sampled depth and stencil.
#define main northlight_lighting_capture_helpers
#include "world_lighting_capture_gpu.cpp"
#undef main
#include "weather_diagnostics.h"
#include <filesystem>
#include <limits>

int main(int argc,char** argv){
    assert(argc==4);const bool multisampleVerified=std::atoi(argv[3])!=0;const std::filesystem::path root=std::filesystem::u8path(argv[2]);constexpr unsigned W=37,H=23;
    HWND window=CreateWindowExW(0,L"STATIC",L"Northlight weather capture",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);
    Ref<IDirect3D9> api;api.p=create(D3D_SDK_VERSION);Ref<IDirect3DDevice9> device;
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    ok(api.p->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device.p));Meter meter(device.p);auto* d=&meter;
    const D3DRENDERSTATETYPE stencilStates[]={D3DRS_STENCILFUNC,D3DRS_STENCILREF,D3DRS_STENCILMASK,D3DRS_STENCILWRITEMASK,D3DRS_STENCILPASS,D3DRS_STENCILFAIL,D3DRS_STENCILZFAIL,D3DRS_TWOSIDEDSTENCILMODE};
    unsigned cases=0;
    for(unsigned samples:{0u,4u})for(auto format:{D3DFMT_A8R8G8B8,D3DFMT_A16B16G16R16F}){
        Ref<IDirect3DSurface9> rt[4],stencil;for(auto& surface:rt)ok(d->CreateRenderTarget(W,H,format,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&surface.p,nullptr));
        ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&stencil.p,nullptr));
        Ref<IDirect3DTexture9> depth;Ref<IDirect3DSurface9> depthLevel;
        ok(d->CreateTexture(W,H,1,D3DUSAGE_DEPTHSTENCIL,D3DFORMAT(MAKEFOURCC('I','N','T','Z')),D3DPOOL_DEFAULT,&depth.p,nullptr));ok(depth.p->GetSurfaceLevel(0,&depthLevel.p));
        Ref<IDirect3DSurface9> single;ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&single.p,nullptr));
        ok(d->SetDepthStencilSurface(nullptr));for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));ok(d->SetRenderTarget(0,single.p));ok(d->SetDepthStencilSurface(depthLevel.p));
        D3DVIEWPORT9 full{0,0,W,H,0,1};ok(d->SetViewport(&full));ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){D3DRECT rect{long(x),long(y),long(x+1),long(y+1)};ok(d->Clear(1,&rect,D3DCLEAR_ZBUFFER,0,float((x+3*y)%2),0));}
        ok(d->SetDepthStencilSurface(nullptr));for(unsigned i=0;i<4;++i)ok(d->SetRenderTarget(i,rt[i].p));ok(d->SetDepthStencilSurface(stencil.p));
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_STENCIL,0xff335577,1,0x40));D3DRECT left{0,0,long(W/2),long(H)};ok(d->Clear(1,&left,D3DCLEAR_STENCIL,0,1,0xc0));
        float vertex[256][4],pixel[224][4];for(unsigned i=0;i<256*4;++i)vertex[i/4][i%4]=float(i)/1024;for(unsigned i=0;i<224*4;++i)pixel[i/4][i%4]=-float(i)/2048;
        ok(d->SetVertexShaderConstantF(0,vertex[0],256));ok(d->SetPixelShaderConstantF(0,pixel[0],224));ok(d->SetTexture(3,depth.p));
        ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetRenderState(D3DRS_STENCILENABLE,TRUE));ok(d->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_GREATER));ok(d->SetRenderState(D3DRS_STENCILREF,37));ok(d->SetRenderState(D3DRS_STENCILWRITEMASK,15));
        ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_CW));ok(d->SetRenderState(D3DRS_FILLMODE,D3DFILL_WIREFRAME));
        D3DVIEWPORT9 unusual{2,1,W-4,H-2,.125f,.94f};RECT scissor{3,2,long(W-3),long(H-2)};ok(d->SetViewport(&unusual));ok(d->SetScissorRect(&scissor));
        DWORD stencilBefore[std::size(stencilStates)];for(unsigned i=0;i<std::size(stencilStates);++i)ok(d->GetRenderState(stencilStates[i],&stencilBefore[i]));State before(d);
        NorthlightSurfaceFrame constants{};constants.c[0][0]=1.f/W;constants.c[0][1]=1.f/H;constants.c[0][2]=.2f;constants.c[0][3]=1277;
        constants.c[1][0]=1.3f;constants.c[1][1]=1.7f;constants.c[1][2]=-1;constants.c[1][3]=7.25f;constants.c[2][1]=1.f/.94f;constants.c[3][0]=.8f;
        NorthlightQuality::Settings quality;quality.weatherWindDirection=270;quality.weatherWindStrength=175;quality.weatherCameraMotion=0;
        NorthlightWeather::State weather{NorthlightWeather::Kind::Rain,.8f,1,0,0};
        NorthlightWeatherDiagnostics::Frame frame;frame.constants=&constants;frame.quality=&quality;frame.weather=&weather;frame.rendererFrame=123;frame.width=W;frame.height=H;
        frame.layerGate=1;frame.skySeen=true;frame.linearHdr=format==D3DFMT_A16B16G16R16F;frame.nameMaskRequired=true;
        frame.sourceSamples=samples;frame.multisampleVerified=multisampleVerified;
        const auto folder=root/("case-"+std::to_string(samples)+"-"+std::to_string(unsigned(format)));std::filesystem::create_directory(folder);
        const bool expectedComplete=!samples||multisampleVerified;
        ok(d->BeginScene());assert(NorthlightWeatherDiagnostics::capture(d,folder.u8string(),frame,depth.p,rt[0].p,stencil.p)==expectedComplete);ok(d->EndScene());before.same(d);
        for(unsigned i=0;i<std::size(stencilStates);++i){DWORD after;ok(d->GetRenderState(stencilStates[i],&after));assert(after==stencilBefore[i]);}
        if(expectedComplete){
            const auto rawDepth=read((folder/"capture-1-weather-depth.fgr").u8string()),mask=read((folder/"capture-1-weather-name-mask.fgr").u8string());
            assert(rawDepth.size()==20+W*H*4&&mask.size()==20+W*H*4);
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){float value;std::memcpy(&value,rawDepth.data()+20+(y*W+x)*4,4);assert(value==float((x+3*y)%2));assert(mask[20+(y*W+x)*4+2]==(x<W/2?255:0));}
        }
        ++cases;
        if(!samples&&format==D3DFMT_A8R8G8B8){
            const auto missing=root/"missing-mask";std::filesystem::create_directory(missing);ok(d->BeginScene());assert(!NorthlightWeatherDiagnostics::capture(d,missing.u8string(),frame,depth.p,rt[0].p,nullptr));ok(d->EndScene());before.same(d);
            const auto failure=root/"failed-mask";std::filesystem::create_directory(failure);meter.arm("GetRenderTargetData",3);
            ok(d->BeginScene());assert(!NorthlightWeatherDiagnostics::capture(d,failure.u8string(),frame,depth.p,rt[0].p,stencil.p));ok(d->EndScene());assert(meter.triggered);before.same(d);
            frame.constants=nullptr;frame.unavailable="no-sky";const auto skipped=root/"skipped";std::filesystem::create_directory(skipped);meter.arm("CreateRenderTarget");
            assert(!NorthlightWeatherDiagnostics::capture(d,skipped.u8string(),frame,nullptr,nullptr,nullptr));assert(!meter.triggered);meter.fault=nullptr;
            frame.constants=&constants;frame.unavailable=nullptr;constants.c[9][3]=std::numeric_limits<float>::quiet_NaN();const auto invalid=root/"invalid-context";std::filesystem::create_directory(invalid);
            assert(!NorthlightWeatherDiagnostics::capture(d,invalid.u8string(),frame,nullptr,nullptr,nullptr));constants.c[9][3]=0;
        }
        for(unsigned slot=0;slot<16;++slot)ok(d->SetTexture(slot,nullptr));ok(d->SetDepthStencilSurface(nullptr));for(unsigned i=1;i<4;++i)ok(d->SetRenderTarget(i,nullptr));
    }
    std::printf("PASS weather capture: cases=%u exact raw depth/stencil, SDR/FP16, MSAA0/4, state/constants restored, incomplete/missing/no-sky/nonfinite truthful\n",cases);
}
