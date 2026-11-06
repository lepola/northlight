// Measure the production native-POM kernel; all waits are outside GPU intervals.
#define main distance_oracle_main
#include "composite_distance_gpu.cpp"
#undef main
#include <algorithm>
#include <array>

static double elapsed(IDirect3DDevice9* d,IDirect3DPixelShader9* shader,const void* quad,UINT stride){
    IDirect3DQuery9 *frequency=nullptr,*start=nullptr,*finish=nullptr,*disjoint=nullptr;
    ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));
    ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&finish));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
    d->SetPixelShader(shader);ok(d->BeginScene());
    for(unsigned i=0;i<4;++i)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,stride));
    ok(disjoint->Issue(D3DISSUE_BEGIN));ok(frequency->Issue(D3DISSUE_END));ok(start->Issue(D3DISSUE_END));
    constexpr unsigned repeats=32;for(unsigned i=0;i<repeats;++i)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,stride));
    ok(finish->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));ok(d->EndScene());
    auto wait=[](IDirect3DQuery9* q,void* result,DWORD size){const auto began=GetTickCount();HRESULT hr;
        while((hr=q->GetData(result,size,D3DGETDATA_FLUSH))==S_FALSE){assert(GetTickCount()-began<10000);Sleep(1);}ok(hr);};
    UINT64 hz=0,a=0,b=0;BOOL invalid=TRUE;wait(finish,&b,sizeof b);wait(start,&a,sizeof a);wait(frequency,&hz,sizeof hz);wait(disjoint,&invalid,sizeof invalid);
    assert(!invalid&&hz&&b>=a);frequency->Release();start->Release();finish->Release();disjoint->Release();
    return double(b-a)*1000/double(hz)/repeats;
}
int main(int argc,char** argv){
    assert(argc==3);const std::string folder=argv[2];
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightMaterialKernelTiming";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto make=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(make);auto api=make(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* shaders[2]={};for(unsigned i=0;i<2;++i){auto code=read(folder+(i?"after.bin":"before.bin"));ok(d->CreatePixelShader((DWORD*)code.data(),&shaders[i]));}
    auto source=image(folder+"source.fgr");auto* height=texture(d,source);d->SetTexture(14,height);
    for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(14,k,D3DTEXF_LINEAR);
    d->SetSamplerState(14,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(14,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(14,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
    d->SetDepthStencilSurface(nullptr);for(auto k:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})d->SetRenderState(k,FALSE);
    d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
    d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX2|D3DFVF_TEXCOORDSIZE4(1));
    const float controls[]={1,1,-1,0,.036f,16,16,0};ok(d->SetPixelShaderConstantF(220,controls,2));
    struct V{float x,y,z,w,u,v,px,py,pz,pw;};
    for(unsigned resolution=0;resolution<2;++resolution){
        const unsigned width=resolution?3840:256,heightPixels=resolution?2160:256;IDirect3DSurface9* target=nullptr;ok(d->CreateRenderTarget(width,heightPixels,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
        d->SetRenderTarget(0,target);D3DVIEWPORT9 vp={0,0,width,heightPixels,0,1};d->SetViewport(&vp);
        for(unsigned mode=0;mode<4;++mode){
            const float distance=mode==0?1:mode==1?16:24,scale=mode==3?512.f:1.f;
            V quad[4];for(unsigned i=0;i<4;++i){float u=i&1?1.f:0.f,v=i&2?1.f:0.f;quad[i]={u*width-.5f,v*heightPixels-.5f,0,1,u*scale,v*scale,u*2-1,v*2-1,0,distance};}
            if(resolution){std::array<double,3> times[2];for(unsigned repeat=0;repeat<3;++repeat)for(unsigned step=0;step<2;++step){unsigned i=step^(repeat&1);times[i][repeat]=elapsed(d,shaders[i],quad,sizeof(V));}
                for(auto& t:times)std::sort(t.begin(),t.end());std::printf("GPU_TIMING mode=%u width=%u height=%u beforeMs=%.6f afterMs=%.6f\n",mode,width,heightPixels,times[0][1],times[1][1]);
            }else for(unsigned i=0;i<2;++i){d->SetPixelShader(shaders[i]);ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(V)));ok(d->EndScene());
                IDirect3DSurface9* stage=nullptr;ok(d->CreateOffscreenPlainSurface(width,heightPixels,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&stage,nullptr));ok(d->GetRenderTargetData(target,stage));D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));
                std::ofstream output(folder+(i?"after-":"before-")+std::to_string(mode)+".f32",std::ios::binary);for(unsigned y=0;y<heightPixels;++y)output.write(static_cast<char*>(lock.pBits)+size_t(y)*lock.Pitch,width*16);stage->UnlockRect();stage->Release();}
        }target->Release();
    }
    d->SetTexture(14,nullptr);height->Release();for(auto* s:shaders)s->Release();d->Release();api->Release();DestroyWindow(window);std::puts("MATERIAL_KERNEL_TIMING_PASS");
}
