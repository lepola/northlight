// Reproduce the untextured sky and color-write-disabled draws from the .4 log.
#undef NDEBUG
#define main composite_unused
#include "composite_distance_gpu.cpp"
#undef main
#include "hdr_color.h"
static constexpr unsigned W=64,H=64;
static void fill(IDirect3DDevice9* d,IDirect3DSurface9* target,IDirect3DPixelShader9* shader,const float (&color)[4]){
    ok(NorthlightHdr::quadState(d,target,W,H));ok(d->SetPixelShader(shader));ok(d->SetPixelShaderConstantF(0,color,1));
    ok(d->BeginScene());ok(NorthlightHdr::drawQuad(d,W,H));ok(d->EndScene());
}
template<class Draw> static double timed(IDirect3DDevice9* d,Draw draw){
    IDirect3DQuery9 *start=nullptr,*end=nullptr,*frequency=nullptr;
    ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));
    for(unsigned i=0;i<16;++i)draw();ok(frequency->Issue(D3DISSUE_END));ok(start->Issue(D3DISSUE_END));
    for(unsigned i=0;i<32;++i)draw();ok(end->Issue(D3DISSUE_END));UINT64 a=0,b=0,hz=0;HRESULT hr;
    while((hr=end->GetData(&b,sizeof b,D3DGETDATA_FLUSH))==S_FALSE)SwitchToThread();ok(hr);ok(start->GetData(&a,sizeof a,0));ok(frequency->GetData(&hz,sizeof hz,0));
    start->Release();end->Release();frequency->Release();assert(hz&&b>a);return double(b-a)*1000/double(hz)/32;
}

int main(int argc,char** argv){
    assert(argc==2||argc==3);std::setvbuf(stdout,nullptr,_IONBF,0);
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,W,H,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* constant=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCalibration),&constant));
    IDirect3DSurface9 *resolved=nullptr,*staging=nullptr;
    ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
    ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    D3DMATRIX identity{};for(unsigned i=0;i<4;++i)identity.m[i][i]=1;
    ok(d->SetTransform(D3DTS_WORLD,&identity));ok(d->SetTransform(D3DTS_VIEW,&identity));ok(d->SetTransform(D3DTS_PROJECTION,&identity));
    auto pixels=[&](IDirect3DSurface9* target){
        if(target!=resolved)ok(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(resolved,staging));
        D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> result(W*H*4);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W*4;++x)result[y*W*4+x]=half(reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(lock.pBits)+y*lock.Pitch)[x]);
        ok(staging->UnlockRect());return result;
    };
    auto state=[&](IDirect3DSurface9* target,unsigned width,unsigned height,unsigned blend,unsigned opacity,bool dormant,bool gradient){
        ok(NorthlightHdr::quadState(d,target,width,height));ok(d->SetPixelShader(nullptr));ok(d->SetTexture(0,nullptr));
        ok(d->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE));ok(d->SetRenderState(D3DRS_LIGHTING,FALSE));ok(d->SetRenderState(D3DRS_SPECULARENABLE,FALSE));
        ok(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_DISABLE));ok(d->SetTextureStageState(0,D3DTSS_ALPHAOP,dormant?D3DTOP_MODULATE:D3DTOP_DISABLE));
        ok(d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,dormant?D3DTSS_TCI_CAMERASPACEPOSITION:0));
        ok(d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,dormant?D3DTTFF_COUNT3|D3DTTFF_PROJECTED:D3DTTFF_DISABLE));
        ok(d->SetTextureStageState(1,D3DTSS_COLOROP,dormant?D3DTOP_ADD:D3DTOP_DISABLE));
        ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,blend!=0));ok(d->SetRenderState(D3DRS_SRCBLEND,blend==3?D3DBLEND_ONE:D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,blend==1?D3DBLEND_ONE:D3DBLEND_INVSRCALPHA));
    };
    struct V{float x,y,z;DWORD c;};
    unsigned cases=0;float maxRgb=0,maxAlpha=0;
    for(auto samples:{D3DMULTISAMPLE_NONE,D3DMULTISAMPLE_2_SAMPLES})for(unsigned blend=0;blend<4;++blend)
    for(unsigned opacity:{0u,64u,160u,255u})for(bool dormant:{false,true})for(bool gradient:{false,true}){
        IDirect3DSurface9* target=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,samples,0,FALSE,&target,nullptr));
        NorthlightHdr::Color transfer;std::vector<float> reference;
        for(bool direct:{false,true}){
            const float background[]={.3f,.7f,1.4f,.8f};fill(d,target,constant,background);ok(d->BeginScene());state(target,W,H,blend,opacity,dormant,gradient);
            const DWORD c=D3DCOLOR_ARGB(opacity,200,180,240),other=gradient?D3DCOLOR_ARGB(opacity,40,100,20):c;
            const V q[]={{-1,1,.5f,c},{1,1,.5f,other},{-1,-1,.5f,other},{.7f,-.8f,.5f,c}};
            const D3DRENDERSTATETYPE checked[]={D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA,D3DRS_BLENDOPALPHA,D3DRS_SRGBWRITEENABLE,D3DRS_COLORWRITEENABLE};
            DWORD saved[6]{};for(unsigned i=0;i<6;++i)ok(d->GetRenderState(checked[i],saved+i));
            float constants[8]{},restored[8]{};ok(d->GetPixelShaderConstantF(0,constants,2));
            auto draw=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V));};
            const auto before=transfer.layers;if(direct)ok(transfer.draw(d,80,draw));else ok(transfer.legacyLayer(d,80,draw));
            assert(transfer.layers==before+(direct&&blend==1&&!dormant?0:1));
            DWORD op=0;ok(d->GetTextureStageState(0,D3DTSS_COLOROP,&op));assert(op==D3DTOP_DISABLE);
            IDirect3DPixelShader9* ps=nullptr;ok(d->GetPixelShader(&ps));assert(!ps);
            for(unsigned i=0;i<6;++i){DWORD value=0;ok(d->GetRenderState(checked[i],&value));assert(value==saved[i]);}
            ok(d->GetPixelShaderConstantF(0,restored,2));assert(!std::memcmp(constants,restored,sizeof constants));
            ok(d->EndScene());auto output=pixels(target);
            if(!direct)reference=output;else{
                float rgb=0,alpha=0;for(unsigned i=0;i<output.size();++i){assert(std::isfinite(output[i]));auto& error=i%4==3?alpha:rgb;error=std::max(error,std::fabs(output[i]-reference[i]));}
                maxRgb=std::max(maxRgb,rgb);maxAlpha=std::max(maxAlpha,alpha);
                std::printf("case=%u samples=%u blend=%u opacity=%u dormant=%u gradient=%u rgb=%g alpha=%g\n",cases,unsigned(samples),blend,opacity,unsigned(dormant),unsigned(gradient),rgb,alpha);
                assert(rgb<.015f&&alpha<.005f);
            }
        }
        target->Release();++cases;
    }
    // No-color draws must still execute once: preserve native alpha test,
    // depth and stencil updates, as observed by a following diagnostic draw.
    for(bool programmable:{false,true})for(unsigned opacity:{64u,192u}){
        IDirect3DSurface9* depth=nullptr;ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));
        std::vector<float> reference;NorthlightHdr::Color transfer;
        for(bool direct:{false,true}){
            const float background[]={.3f,.7f,1.4f,.8f};fill(d,resolved,constant,background);ok(d->BeginScene());state(resolved,W,H,1,opacity,false,false);
            ok(d->SetDepthStencilSurface(depth));ok(d->Clear(0,nullptr,D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,1,0));
            ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESS));
            ok(d->SetRenderState(D3DRS_STENCILENABLE,TRUE));ok(d->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_ALWAYS));ok(d->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_INCRSAT));
            ok(d->SetRenderState(D3DRS_STENCILFAIL,D3DSTENCILOP_KEEP));ok(d->SetRenderState(D3DRS_STENCILZFAIL,D3DSTENCILOP_KEEP));ok(d->SetRenderState(D3DRS_STENCILMASK,255));ok(d->SetRenderState(D3DRS_STENCILWRITEMASK,255));
            ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE));ok(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER));ok(d->SetRenderState(D3DRS_ALPHAREF,128));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,0));
            const float color[]={.2f,.4f,.6f,float(opacity)/255};if(programmable){ok(d->SetPixelShader(constant));ok(d->SetPixelShaderConstantF(0,color,1));}
            const V q[]={{-1,1,.25f,D3DCOLOR_ARGB(opacity,200,180,240)},{0,1,.25f,D3DCOLOR_ARGB(opacity,200,180,240)},{-1,-1,.25f,D3DCOLOR_ARGB(opacity,200,180,240)},{0,-1,.25f,D3DCOLOR_ARGB(opacity,200,180,240)}};
            unsigned calls=0;auto draw=[&]{++calls;return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V));};
            if(direct)ok(transfer.draw(d,80,draw));else ok(draw());assert(calls==1&&transfer.layers==0);
            DWORD writes=15;ok(d->GetRenderState(D3DRS_COLORWRITEENABLE,&writes));assert(!writes);
            ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
            ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_EQUAL));ok(d->SetRenderState(D3DRS_STENCILREF,1));ok(d->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_KEEP));
            ok(d->SetPixelShader(constant));const float red[]={1,0,0,1};ok(d->SetPixelShaderConstantF(0,red,1));
            const V front[]={{-1,1,.1f,~0u},{1,1,.1f,~0u},{-1,-1,.1f,~0u},{1,-1,.1f,~0u}};
            ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,front,sizeof(V)));ok(d->SetRenderState(D3DRS_STENCILENABLE,FALSE));
            const float green[]={0,1,0,1};ok(d->SetPixelShaderConstantF(0,green,1));const V back[]={{-1,1,.5f,~0u},{1,1,.5f,~0u},{-1,-1,.5f,~0u},{1,-1,.5f,~0u}};ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,back,sizeof(V)));
            ok(d->EndScene());auto output=pixels(resolved);if(!direct)reference=output;else assert(output==reference);
            assert(output[(H/2*W+W/4)*4]==(opacity>128?1.f:0.f));assert(output[(H/2*W+W/4)*4+1]==(opacity>128?0.f:1.f));
            ok(d->SetDepthStencilSurface(nullptr));
        }
        depth->Release();++cases;
    }
    std::printf("HDR_DIFFUSE_PASS cases=%u maxRgbError=%g maxAlphaError=%g depth_stencil_alpha_test_preserved=1\n",cases,maxRgb,maxAlpha);
    if(argc==3){
        constexpr unsigned width=3840,height=2160;IDirect3DSurface9* target=nullptr;ok(d->CreateRenderTarget(width,height,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_2_SAMPLES,0,FALSE,&target,nullptr));
        NorthlightHdr::Color transfer;
        for(bool full:{false,true}){
            auto frame=[&](bool direct){ok(d->BeginScene());state(target,width,height,1,160,false,false);
                const float x=full?1.f:-.8f,y=full?-1.f:.8f;const V q[]={{-1,1,.5f,0xa0c8b4f0},{x,1,.5f,0xa0c8b4f0},{-1,y,.5f,0xa0c8b4f0},{x,y,.5f,0xa0c8b4f0}};
                auto draw=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V));};
                if(direct)ok(transfer.draw(d,80,draw));else ok(transfer.legacyLayer(d,80,draw));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,0));
                if(direct)ok(transfer.draw(d,80,draw));else ok(transfer.legacyLayer(d,80,draw));ok(d->EndScene());};
            double before[5],after[5];for(unsigned i=0;i<5;++i){if(i%2){after[i]=timed(d,[&]{frame(true);});before[i]=timed(d,[&]{frame(false);});}else{before[i]=timed(d,[&]{frame(false);});after[i]=timed(d,[&]{frame(true);});}}
            std::sort(before,before+5);std::sort(after,after+5);std::printf("HDR_DIFFUSE_BENCH width=%u height=%u samples=2 full=%u legacyMs=%.6f directMs=%.6f savedMs=%.6f scope=two_sky_draws_not_whole_game\n",width,height,unsigned(full),before[2],after[2],before[2]-after[2]);
        }
        transfer.reset();target->Release();
    }
    resolved->Release();staging->Release();constant->Release();d->Release();api->Release();DestroyWindow(window);
}
