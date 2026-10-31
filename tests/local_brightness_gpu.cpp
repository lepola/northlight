// Hidden-device production compositor check; no game or timing queries.
#define main northlight_unused_composite_replay
#include "composite_distance_gpu.cpp"
#undef main
#include <array>
static Image solid(float r,float g=0,float b=0,float a=0){
    Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};
    const float p[]={r,g,b,a};std::memcpy(im.pixels.data(),p,16);return im;
}
int main(int argc,char** argv){
    assert(argc==3||argc==4);constexpr unsigned N=4;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightBrightnessOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,N,N,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[2]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=N;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* shaders[3]{};
    for(unsigned i=0;i<(argc==4?3u:2u);++i){auto code=read(i==2?argv[3]:std::string(argv[1])+(i?"/WorldUnlitComposite.bin":"/WorldComposite.bin"));ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shaders[i]));}
    for(unsigned s=0;s<16;++s){for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,k,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto k:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(s,k,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));}
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto k:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE})ok(d->SetRenderState(k,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(N,N,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(N,N,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->SetRenderTarget(0,target));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{N-.5f,-.5f,0,1,1,0},{-.5f,N-.5f,0,1,0,1},{N-.5f,N-.5f,0,1,1,1}};
    std::vector<IDirect3DTexture9*> owned;
    auto bind=[&](unsigned slot,Image im){auto t=texture(d,im);owned.push_back(t);ok(d->SetTexture(slot,t));};
    auto clearInputs=[&]{for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));for(auto t:owned)t->Release();owned.clear();};
    auto draw=[&](unsigned shader){ok(d->SetPixelShader(shaders[shader]));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::array<float,4> p{};for(unsigned k=0;k<4;++k){std::uint16_t h;std::memcpy(&h,static_cast<char*>(lock.pBits)+lock.Pitch+8+k*2,2);p[k]=half(h);}ok(staging->UnlockRect());return p;};
    unsigned cases=0,negativeControls=0,neutralControls=0;float maxError=0;
    // Explicit authored receiver colours, not reconstructed screenshot geometry.
    const float surfaces[][3]={{0,0,0},{.08f,.04f,.01f},{.72f,.30f,.035f},{.84f,.41f,.05f},{.95f,.9f,.75f},{1,1,1}};
    for(const auto& surface:surfaces)for(float baseline:{.15f,.6f})for(float transmittance:{0.f,.35f,1.f})for(float ao:{.4f,1.f})for(float correction:{-.1f,0.f,.2f})for(float power:{0.f,.001f,.1f,1.f,4.f,32.f,512.f}){
        clearInputs();const float nearZ=.2f,farZ=1000,nd=farZ/(farZ-nearZ)*(1-nearZ/10);
        float c[68][4]{};c[0][0]=c[0][1]=1.f/N;c[0][2]=nearZ;c[0][3]=farZ;c[1][0]=c[1][1]=c[1][2]=c[2][0]=1;c[25][1]=transmittance;c[25][2]=c[25][3]=1;
        for(unsigned k=0;k<3;++k)c[26][k]=.1f;c[33][0]=c[33][1]=c[33][2]=c[33][3]=N;ok(d->SetPixelShaderConstantF(0,c[0],68));
        bind(0,solid(surface[0],surface[1],surface[2],.375f));bind(1,solid(nd));bind(2,solid(0));bind(3,solid(power,power*.65f,power*.08f));bind(8,solid(correction,correction,correction));bind(9,solid(0,0,0,1));bind(10,solid(0,0,0,ao));bind(11,solid(0));bind(12,solid(baseline,baseline,baseline));bind(13,solid(0));bind(15,solid(10));
        float original[3],transported[3],color[3],room[3],addition[3],scale=1;
        for(unsigned k=0;k<3;++k){const float fog=std::min((1-transmittance)*.1f,surface[k]);original[k]=std::clamp((surface[k]-fog)*ao+fog,0.f,1.f);transported[k]=std::max(original[k]-fog,0.f);const float albedo=std::min(transported[k]/baseline,transmittance);color[k]=std::max(original[k]+albedo*correction,original[k]-.45f*transported[k]);room[k]=std::min(transported[k],.5f*std::max(1-color[k],0.f));addition[k]=albedo*power*(k==0?1.f:k==1?.65f:.08f);scale=std::max(scale,1+addition[k]/std::max(room[k],.00001f));}
        const auto p=draw(0);
        for(unsigned k=0;k<3;++k){const float expected=color[k]+std::min(addition[k]/scale,room[k]);maxError=std::max(maxError,std::fabs(p[k]-expected));assert(std::isfinite(p[k])&&std::fabs(p[k]-expected)<.0015f);assert(p[k]<=color[k]+room[k]+.001f&&p[k]>=color[k]-.001f);if(surface[k]==0)assert(p[k]==0);}
        assert(p[3]==.375f);++cases;
        if(argc==4&&power==0){assert(draw(2)==p);++neutralControls;}
        if(argc==4&&power==32&&surface[0]==.72f&&transmittance==1&&ao==1&&correction==0){const auto old=draw(2);assert(old[0]>1&&p[0]<.86f);++negativeControls;}
        // The same extreme source must leave unlit, sky and water receivers alone.
        if(power==512&&correction==0){
            const auto unlit=draw(1);for(unsigned k=0;k<3;++k)assert(std::fabs(unlit[k]-surface[k])<.001f);
            bind(1,solid(1));const auto sky=draw(0);for(unsigned k=0;k<3;++k)assert(std::fabs(sky[k]-surface[k])<.001f);
            bind(1,solid(nd));bind(11,solid(5,1));c[30][0]=1;ok(d->SetPixelShaderConstantF(30,c[30],1));const auto water=draw(0);for(unsigned k=0;k<3;++k)assert(std::fabs(water[k]-surface[k])<.001f);
        }
    }
    assert(argc!=4||(negativeControls==2&&neutralControls==216));
    std::printf("LOCAL_BRIGHTNESS_PASS cases=%u maxError=%g oldClippingControls=%u exactNoLampControls=%u gameLaunched=0 timingQueries=0\n",cases,maxError,negativeControls,neutralControls);
    clearInputs();d->SetPixelShader(nullptr);for(auto shader:shaders)if(shader)shader->Release();staging->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
