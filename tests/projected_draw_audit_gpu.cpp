// The reference probe must explain depth rejection without changing the image.
#undef NDEBUG
#define main composite_oracle_main
#include "composite_distance_gpu.cpp"
#undef main
#include "projected_draw_audit.h"

struct AuditVertex {float x,y,z,w;DWORD color;};
int main(int argc,char** argv){
    assert(argc==2);constexpr unsigned W=128,H=96;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightProjectedAudit";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,W,H,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DSurface9* readback=nullptr;ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    unsigned cases=0;
    for(unsigned msaa:{0u,2u}){
        IDirect3DSurface9 *target=nullptr,*depth=nullptr,*resolved=nullptr;
        ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_TYPE(msaa),0,FALSE,&target,nullptr));
        ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_TYPE(msaa),0,FALSE,&depth,nullptr));
        if(msaa)ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
        ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(depth));ok(d->SetVertexShader(nullptr));ok(d->SetPixelShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));
        ok(d->SetTexture(0,nullptr));ok(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
        ok(d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE));ok(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));
        ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_LIGHTING,FALSE));ok(d->SetRenderState(D3DRS_STENCILENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
        ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE));ok(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER));ok(d->SetRenderState(D3DRS_ALPHAREF,127));
        NorthlightProjectedDrawAudit audit;
        for(unsigned phase=0;phase<4;++phase)for(unsigned kind=0;kind<2;++kind)for(unsigned blocked=0;blocked<3;++blocked)for(bool alpha:{false,true}){
            const DWORD color=alpha?0xc0408040:0x40408040;
            AuditVertex vertices[]={{31.5f,23.5f,.6f,1,color},{95.5f,23.5f,.6f,1,color},{31.5f,71.5f,.6f,1,color},{95.5f,71.5f,.6f,1,color}};
            ok(d->SetRenderState(D3DRS_SRCBLEND,kind?D3DBLEND_DESTCOLOR:D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,kind?D3DBLEND_ZERO:D3DBLEND_ONE));
            std::vector<unsigned char> images[2];NorthlightProjectedDrawAudit::Result result{};unsigned results=0,calls=0;
            for(bool probing:{false,true}){
                ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff606060,blocked==1?.5f:.7f,0));
                if(blocked==2){D3DRECT rect={0,0,W/2,H};ok(d->Clear(1,&rect,D3DCLEAR_ZBUFFER,0,.5f,0));}
                ok(d->BeginScene());
                auto draw=[&]{++calls;return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(AuditVertex));};
                ok(probing?audit.draw(d,cases+1,phase,2,draw):draw());ok(d->EndScene());
                DWORD value=0;ok(d->GetRenderState(D3DRS_COLORWRITEENABLE,&value));assert(value==15);ok(d->GetRenderState(D3DRS_ZFUNC,&value));assert(value==D3DCMP_LESSEQUAL);ok(d->GetRenderState(D3DRS_ZWRITEENABLE,&value));assert(!value);
                IDirect3DVertexBuffer9* stream=nullptr;UINT offset=0,stride=0;ok(d->GetStreamSource(0,&stream,&offset,&stride));assert(!stream);
                if(msaa)ok(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(msaa?resolved:target,readback));
                D3DLOCKED_RECT lock{};ok(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));auto& bytes=images[probing];bytes.resize(W*H*4);for(unsigned y=0;y<H;++y)std::memcpy(bytes.data()+y*W*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,W*4);ok(readback->UnlockRect());
                audit.poll([&](const auto& r){result=r;++results;});
            }
            assert(images[0]==images[1]&&calls==3&&results==1&&SUCCEEDED(result.status));
            if(!alpha)assert(!result.visible&&!result.potential);
            else{assert(result.potential>0);if(blocked==0)assert(result.visible==result.potential);else if(blocked==1)assert(!result.visible);else assert(result.visible>0&&result.visible<result.potential);}
            ++cases;
        }
        ok(d->SetDepthStencilSurface(nullptr));depth->Release();target->Release();if(resolved)resolved->Release();
    }
    readback->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::printf("PROJECTED_AUDIT_GPU_PASS cases=%u exactColour=1 depthAndAlphaRejection=1 msaa0_2=1 restoredState=1 upSideEffects=1 noQueryFlush=1\n",cases);
}
