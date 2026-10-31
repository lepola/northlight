// Measures the post-composition HDR passes, not whole-client frame time.
#undef NDEBUG
#pragma clang diagnostic ignored "-Wreturn-type"
#define main northlight_composite_unused
#include "composite_distance_gpu.cpp"
#undef main
#include "hdr_frame.h"
#include "hdr_color.h"
#include <functional>

static double submissionMs=0;
static double timed(IDirect3DDevice9* device,const std::function<void()>& frame){
    IDirect3DQuery9 *start=nullptr,*end=nullptr,*frequency=nullptr;
    ok(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));
    for(unsigned i=0;i<16;++i)frame();
    LARGE_INTEGER a{},b{},clock{};QueryPerformanceFrequency(&clock);
    ok(frequency->Issue(D3DISSUE_END));ok(start->Issue(D3DISSUE_END));QueryPerformanceCounter(&a);for(unsigned i=0;i<48;++i)frame();QueryPerformanceCounter(&b);ok(end->Issue(D3DISSUE_END));
    submissionMs=double(b.QuadPart-a.QuadPart)*1000/clock.QuadPart/48;
    UINT64 first=0,last=0,hz=0;
    while(end->GetData(&last,sizeof last,D3DGETDATA_FLUSH)==S_FALSE)SwitchToThread();ok(start->GetData(&first,sizeof first,0));ok(frequency->GetData(&hz,sizeof hz,0));
    start->Release();end->Release();frequency->Release();assert(hz&&last>first);return double(last-first)*1000/double(hz)/48;
}
int main(int argc,char** argv){
    assert(argc==2||argc==3);HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device));
    IDirect3DPixelShader9* constant=nullptr;ok(device->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCalibration),&constant));
    for(const auto dimensions:{std::pair<unsigned,unsigned>{1920,1080},{2560,1440},{3840,2160}}){
        const unsigned width=dimensions.first,height=dimensions.second;
        IDirect3DTexture9 *encoded=nullptr,*copy=nullptr;IDirect3DSurface9 *encodedSurface=nullptr,*copySurface=nullptr,*output=nullptr;
        ok(device->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&encoded,nullptr));ok(encoded->GetSurfaceLevel(0,&encodedSurface));
        ok(device->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&copy,nullptr));ok(copy->GetSurfaceLevel(0,&copySurface));
        ok(device->CreateRenderTarget(width,height,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&output,nullptr));ok(device->ColorFill(encodedSurface,nullptr,0xff808080));
        NorthlightHdr::Color previous;NorthlightHdr::Frame current;NorthlightHdr::Settings settings;settings.bloom=1;settings.autoExposure=true;
        const auto old=[&]{ok(device->BeginScene());ok(previous.convertWorld(device,encodedSurface,output,copy,copySurface,width,height,203,1000));ok(device->EndScene());};
        const auto next=[&]{ok(device->BeginScene());ok(current.prepareLegacy(device,encoded,nullptr,nullptr,width,height,0,1,settings));ok(current.finish(device,nullptr,output,settings,1.f/60,true,false,.18f,nullptr,nullptr,nullptr,nullptr,nullptr,encodedSurface));ok(device->EndScene());};
        // Interleaving reduces bias from clock changes during an isolated microbenchmark.
        double oldMs[3],newMs[3],cpuMs[3];for(unsigned i=0;i<3;++i){oldMs[i]=timed(device,old);newMs[i]=timed(device,next);cpuMs[i]=submissionMs;}
        std::sort(oldMs,oldMs+3);std::sort(newMs,newMs+3);
        std::sort(cpuMs,cpuMs+3);
        std::printf("{\"width\":%u,\"height\":%u,\"old_transfer_ms\":%.6f,\"linear_post_ms\":%.6f,\"linear_cpu_submit_ms\":%.6f,\"scope\":\"post_only_no_game_or_world_lighting\"}\n",width,height,oldMs[1],newMs[1],cpuMs[1]);std::fflush(stdout);
        if(width==3840){
            IDirect3DSurface9* msaa=nullptr;
            ok(device->CreateRenderTarget(width,height,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_8_SAMPLES,0,FALSE,&msaa,nullptr));
            IDirect3DSurface9* legacyMsaa=nullptr;
            ok(device->CreateRenderTarget(width,height,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_8_SAMPLES,0,FALSE,&legacyMsaa,nullptr));
            auto msaaPost=[&](bool mirror){ok(device->BeginScene());ok(current.prepareLegacy(device,encoded,nullptr,nullptr,width,height,0,1,settings));ok(current.finish(device,nullptr,msaa,settings,1.f/60,true,false,.18f,nullptr,nullptr,nullptr,nullptr,nullptr,mirror?legacyMsaa:nullptr));ok(device->EndScene());};
            const double mirrored=timed(device,[&]{msaaPost(true);}),withoutMirror=timed(device,[&]{msaaPost(false);});
            std::printf("{\"width\":%u,\"height\":%u,\"samples\":8,\"hdr_post_mrt_ms\":%.6f,\"hdr_post_without_mirror_ms\":%.6f}\n",width,height,mirrored,withoutMirror);
            legacyMsaa->Release();
            NorthlightHdr::Color transfer;const float white[]={1,1,1,1};
            auto screen=[&](bool before){
                ok(device->BeginScene());ok(NorthlightHdr::quadState(device,msaa,width,height));
                device->SetPixelShader(constant);device->SetPixelShaderConstantF(0,white,1);
                device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_INVDESTCOLOR);device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);
                // The draw covers a small sprite; the old fallback shaded and
                // resolved the entire 4K target for every sprite regardless.
                struct V{float x,y,z,w,u,v;};const V q[]={{0,0,0,1,0,0},{64,0,0,1,1,0},{0,64,0,1,0,1},{64,64,0,1,1,1}};
                for(unsigned n=0;n<32;++n){auto draw=[&]{return device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V));};
                    if(before)ok(transfer.legacyLayer(device,80,draw));else ok(transfer.draw(device,80,draw));}
                ok(device->EndScene());
            };
            double layer[3],direct[3];for(unsigned i=0;i<3;++i){layer[i]=timed(device,[&]{screen(true);});direct[i]=timed(device,[&]{screen(false);});}
            std::sort(layer,layer+3);std::sort(direct,direct+3);
            std::printf("{\"width\":%u,\"height\":%u,\"samples\":8,\"sprites\":32,\"legacy_screen_layers_ms\":%.6f,\"direct_screen_ms\":%.6f,\"scope\":\"screen_blend_compatibility_only_not_client_fps\"}\n",width,height,layer[1],direct[1]);
            for(bool full:{false,true}){
                auto twoTextures=[&](bool before){
                    ok(device->BeginScene());ok(NorthlightHdr::quadState(device,msaa,width,height));ok(device->SetPixelShader(nullptr));
                    ok(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1));
                    for(unsigned stage=0;stage<2;++stage){
                        ok(device->SetTexture(stage,encoded));
                        for(auto state:{D3DTSS_COLOROP,D3DTSS_ALPHAOP})ok(device->SetTextureStageState(stage,state,D3DTOP_MODULATE));
                        for(auto state:{D3DTSS_COLORARG1,D3DTSS_ALPHAARG1})ok(device->SetTextureStageState(stage,state,D3DTA_TEXTURE));
                        for(auto state:{D3DTSS_COLORARG2,D3DTSS_ALPHAARG2})ok(device->SetTextureStageState(stage,state,D3DTA_CURRENT));
                        ok(device->SetTextureStageState(stage,D3DTSS_TEXCOORDINDEX,0));ok(device->SetTextureStageState(stage,D3DTSS_RESULTARG,D3DTA_CURRENT));
                        ok(device->SetTextureStageState(stage,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE));
                    }
                    ok(device->SetTextureStageState(2,D3DTSS_COLOROP,D3DTOP_DISABLE));
                    ok(device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
                    const float w=full?float(width):64.f,h=full?float(height):64.f;
                    struct V{float x,y,z,w;DWORD color;float u,v;};const V q[]={{0,0,0,1,0xa0ffffff,0,0},{w,0,0,1,0xa0ffffff,1,0},{0,h,0,1,0xa0ffffff,0,1},{w,h,0,1,0xa0ffffff,1,1}};
                    for(unsigned n=0;n<2;++n){auto draw=[&]{return device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V));};
                        if(before)ok(transfer.legacyLayer(device,160,draw));else ok(transfer.draw(device,160,draw));}
                    ok(device->EndScene());
                };
                for(unsigned i=0;i<3;++i){layer[i]=timed(device,[&]{twoTextures(true);});direct[i]=timed(device,[&]{twoTextures(false);});}
                std::sort(layer,layer+3);std::sort(direct,direct+3);
                std::printf("{\"width\":%u,\"height\":%u,\"samples\":8,\"draws\":2,\"fullCoverage\":%u,\"two_texture_layers_ms\":%.6f,\"two_texture_direct_ms\":%.6f,\"scope\":\"fixed_function_compatibility_only_not_client_fps\"}\n",width,height,unsigned(full),layer[1],direct[1]);
            }
            for(bool blend:{false,true}){
                ok(device->SetRenderState(D3DRS_ALPHABLENDENABLE,blend));
                // Isolate state validation/substitution from draw/GPU stalls.
                // The fixed-function two-texture state above remains active.
                double cpu[7];LARGE_INTEGER a{},b{},clock{};QueryPerformanceFrequency(&clock);
                for(unsigned block=0;block<7;++block){
                    QueryPerformanceCounter(&a);for(unsigned n=0;n<2048;++n)ok(transfer.draw(device,160,[]{return S_OK;}));QueryPerformanceCounter(&b);
                    cpu[block]=double(b.QuadPart-a.QuadPart)*1000/clock.QuadPart/1024;
                }
                std::sort(cpu,cpu+7);std::printf("{\"two_texture_cpu_ms_per_2_draws\":%.9f,\"blend\":%s,\"scope\":\"validation_and_state_only_no_draw_or_game\"}\n",cpu[3],blend?"true":"false");
            }
            transfer.reset();msaa->Release();
        }
        current.reset();previous.reset();encodedSurface->Release();encoded->Release();copySurface->Release();copy->Release();output->Release();
    }
    if(argc==3){
        const auto im=image(argv[2]);assert(im.format==D3DFMT_A16B16G16R16F&&im.bytes==8);
        auto* input=texture(device,im);IDirect3DPixelShader9* copyShader=nullptr;
        ok(device->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCopy),&copyShader));
        for(auto samples:{D3DMULTISAMPLE_NONE,D3DMULTISAMPLE_8_SAMPLES}){
            IDirect3DSurface9 *composed=nullptr,*output=nullptr,*resolved=nullptr,*readback=nullptr;
            for(auto** target:{&composed,&output})ok(device->CreateRenderTarget(im.width,im.height,D3DFMT_A16B16G16R16F,samples,0,FALSE,target,nullptr));
            ok(device->CreateRenderTarget(im.width,im.height,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
            ok(device->CreateOffscreenPlainSurface(im.width,im.height,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&readback,nullptr));
            // The capture is already resolved. Replicate each captured color to
            // the MSAA samples; this cannot reconstruct original sample edges.
            ok(device->BeginScene());ok(NorthlightHdr::quadState(device,composed,im.width,im.height));
            ok(device->SetPixelShader(copyShader));ok(device->SetTexture(0,input));ok(NorthlightHdr::drawQuad(device,im.width,im.height));ok(device->EndScene());
            NorthlightHdr::Frame frame;NorthlightHdr::Settings settings;settings.autoExposure=true;settings.bloom=1;
            ok(device->BeginScene());ok(frame.prepareLegacy(device,input,nullptr,nullptr,im.width,im.height,0,1,settings,nullptr,false));ok(device->EndScene());
            auto post=[&]{ok(device->BeginScene());ok(frame.finish(device,composed,output,settings,1.f/60,true,true));ok(device->EndScene());};
            double gpu[3],cpu[3];for(unsigned i=0;i<3;++i){gpu[i]=timed(device,post);cpu[i]=submissionMs;}
            std::sort(gpu,gpu+3);std::sort(cpu,cpu+3);
            ok(device->StretchRect(output,nullptr,resolved,nullptr,D3DTEXF_NONE));ok(device->GetRenderTargetData(resolved,readback));
            D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            const std::string name="capture-output-"+std::to_string(unsigned(samples))+".rgba16f";
            std::ofstream bytes(name,std::ios::binary);assert(bytes);
            for(unsigned y=0;y<im.height;++y)bytes.write(static_cast<const char*>(lock.pBits)+size_t(y)*lock.Pitch,im.width*8);
            bytes.close();assert(bytes);ok(readback->UnlockRect());
            std::printf("{\"width\":%u,\"height\":%u,\"samples\":%u,\"captured_linear_post_ms\":%.6f,\"captured_cpu_submit_ms\":%.6f,\"scope\":\"resolved_capture_post_only_no_game_ui_lighting_generation_or_presentation\",\"white_nits\":203,\"peak_nits\":1000,\"bloom\":1,\"exposure_history_reset\":true,\"output\":\"%s\"}\n",im.width,im.height,unsigned(samples),gpu[1],cpu[1],name.c_str());std::fflush(stdout);
            frame.reset();composed->Release();output->Release();resolved->Release();readback->Release();
        }
        ok(device->SetTexture(0,nullptr));input->Release();copyShader->Release();
    }
    constant->Release();device->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
