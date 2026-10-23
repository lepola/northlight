// Production shader replay: screen-space source occlusion is not air shadowing.
#define main northlight_unused_composite_replay
#include "composite_distance_gpu.cpp"
#undef main
#include "world_compiled_shaders.h"
#include "celestial_disc_compiled_shaders.h"

int main(int argc,char** argv){
    assert(argc==4);constexpr unsigned N=16;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightFogOcclusionTest";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,N,N,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=N;pp.BackBufferHeight=N;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9 *oldFog=nullptr,*fog=nullptr,*oldHalo=nullptr,*halo=nullptr;
    auto code=read(argv[2]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&oldFog));code=read(argv[3]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&oldHalo));
    ok(d->CreatePixelShader(kWorldFogShader,&fog));ok(d->CreatePixelShader(kCelestialHaloVisibilityPSShader,&halo));
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(N,N,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(N,N,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE})ok(d->SetRenderState(state,FALSE));
    D3DVIEWPORT9 vp={0,0,N,N,0,1};ok(d->SetViewport(&vp));
    for(unsigned s=0;s<16;++s){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    auto scalar=[&](float value){Image im{1,1,D3DFMT_R32F,4,std::vector<unsigned char>(4)};std::memcpy(im.pixels.data(),&value,4);return texture(d,im);};
    auto zero=scalar(0),one=scalar(1),nearDepth=scalar(1000.f/999.8f*(1-.2f/2));
    Image field{64,64,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(64*64*16)};
    const float node[]={0,0,0,1.25f};for(unsigned p=0;p<64*64;++p)std::memcpy(field.pixels.data()+p*16,node,16);auto regional=texture(d,field);
    Image striped{64,64,D3DFMT_R32F,4,std::vector<unsigned char>(64*64*4)};
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){float v=y<32?0.f:1.f;std::memcpy(striped.pixels.data()+(y*64+x)*4,&v,4);}auto shadow=texture(d,striped);
    const Vertex q[]={{-.5f,-.5f,0,1,0,0},{N-.5f,-.5f,0,1,1,0},{-.5f,N-.5f,0,1,0,1},{N-.5f,N-.5f,0,1,1,1}};
    auto capture=[&](IDirect3DPixelShader9* ps,float (*c)[4]){
        ok(d->SetPixelShaderConstantF(0,c[0],68));ok(d->SetPixelShader(ps));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));
        D3DLOCKED_RECT l{};ok(staging->LockRect(&l,nullptr,D3DLOCK_READONLY));std::vector<float> out(N*N*4);for(unsigned y=0;y<N;++y)std::memcpy(out.data()+y*N*4,static_cast<char*>(l.pBits)+y*l.Pitch,N*16);ok(staging->UnlockRect());return out;
    };
    float c[68][4]{};c[0][0]=c[0][1]=1.f/N;c[0][2]=.2f;c[0][3]=1000;c[1][0]=c[1][1]=5;c[1][2]=1;c[2][0]=1;c[2][2]=192;c[2][3]=1.f/64;
    c[3][1]=1;c[4][2]=1;c[5][0]=1;c[6][2]=20;c[6][3]=1;c[11][0]=c[12][1]=1.f/192;c[14][2]=.5f;c[15][2]=20;
    c[17][0]=.6f;c[17][1]=.5f;c[17][2]=.4f;for(unsigned k=0;k<3;++k)c[18][k]=.04f;
    c[21][0]=1;c[21][1]=1.2f;c[21][2]=1.5f;c[21][3]=128;c[22][0]=.7f;c[22][1]=.8f;c[22][2]=1;c[22][3]=.0035f;
    c[27][0]=1;c[31][0]=c[31][1]=-256;c[31][2]=1.f/512;c[32][0]=.0055f;c[32][1]=.0037f;c[32][2]=c[32][3]=128;c[33][0]=c[33][1]=N;c[58][1]=1.f/8;c[59][3]=.0017f;
    ok(d->SetTexture(13,regional));unsigned failures=0,cases=0,baselineDim=0;double maxError=0,shaftContrast=0;
    for(float angle:{0.f,.6f,1.57f,3.14f})for(auto depth:{one,nearDepth})for(auto map:{one,zero,shadow}){
        c[16][0]=std::cos(angle);c[16][1]=std::sin(angle);ok(d->SetTexture(1,depth));ok(d->SetTexture(3,map));ok(d->SetTexture(15,one));const auto visible=capture(fog,c),oldVisible=capture(oldFog,c);
        ok(d->SetTexture(15,zero));const auto hidden=capture(fog,c),oldHidden=capture(oldFog,c);
        for(unsigned p=0;p<N*N;++p)for(unsigned k=0;k<4;++k){const auto i=p*4+k;double err=std::fabs(hidden[i]-visible[i]);maxError=std::max(maxError,err);
            if(!std::isfinite(hidden[i])||err>2e-6||std::fabs(visible[i]-oldVisible[i])>2e-5)++failures;
            if(k<3&&oldVisible[i]>oldHidden[i]+.01f)++baselineDim;
            if(map==zero&&k<3){const float expected=c[18][k]*c[22][k]*.35f*(1-hidden[p*4+3]);if(std::fabs(hidden[i]-expected)>2e-6)++failures;}
            if(depth==nearDepth&&(hidden[p*4+3]<.99f||hidden[i]>.01f&&k<3))++failures;
        }
        if(angle==0&&depth==one&&map==shadow)shaftContrast=std::fabs(hidden[(8*N+3)*4]-hidden[(8*N+13)*4]);
        ++cases;
    }
    // Full source occlusion hides the halo/ring; fog air shadowing above remains independent.
    std::memset(c,0,sizeof c);const float cs=std::cos(.4f),sn=std::sin(.4f);
    c[0][0]=c[0][1]=1.f/N;c[0][3]=1/.94f;c[1][0]=c[1][1]=5;c[1][2]=1;c[2][1]=1;c[3][0]=-sn;c[3][2]=cs;c[4][0]=cs;c[4][2]=sn;
    c[6][0]=cs;c[6][2]=sn;c[6][3]=.04f;
    for(unsigned j=13;j<45;++j)c[j][0]=c[j][1]=c[j][2]=c[j][3]=.5f;
    ok(d->SetTexture(3,zero));ok(d->SetTexture(4,zero));ok(d->SetTexture(0,one));const auto clear=capture(halo,c);
    ok(d->SetTexture(0,nearDepth));const auto blocked=capture(halo,c),oldBlocked=capture(oldHalo,c);
    for(unsigned p=0;p<N*N;++p)if(clear[p*4]<.999f||blocked[p*4]!=0||oldBlocked[p*4]!=0)++failures;
    c[46][0]=1;ok(d->SetTexture(4,one));const auto mountain=capture(halo,c);for(unsigned p=0;p<N*N;++p)if(mountain[p*4]!=0)++failures;
    if(!baselineDim||shaftContrast<.02)++failures;
    std::printf("fog/halo GPU cases=%u baselineDimChannels=%u shaftContrast=%g maxOcclusionError=%g failures=%u\n",cases+3,baselineDim,shaftContrast,maxError,failures);
    for(unsigned s=0;s<16;++s)d->SetTexture(s,nullptr);d->SetPixelShader(nullptr);zero->Release();one->Release();nearDepth->Release();regional->Release();shadow->Release();oldFog->Release();fog->Release();oldHalo->Release();halo->Release();target->Release();staging->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
