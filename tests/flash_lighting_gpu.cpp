#define main composite_fixture_main
#include "composite_distance_gpu.cpp"
#undef main
#include "local_light_selection.h"
static Image constantImage(float r,float g=0,float b=0,float a=0){Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};const float values[]={r,g,b,a};std::memcpy(im.pixels.data(),values,16);return im;}
int main(int argc,char** argv){
    assert(argc==3);constexpr unsigned W=32,H=32;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightFlashProof";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,W,H,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[2]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* shaders[2]{};for(unsigned i=0;i<2;++i){auto code=read(std::string(argv[1])+"/"+(i?"LocalDirect.bin":"LocalFlash.bin"));ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),shaders+i));}
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    std::vector<IDirect3DTexture9*> owned;auto make=[&](Image im){auto t=texture(d,im);owned.push_back(t);return t;};
    const float nearZ=.2f,farZ=1000,z=30,nd=farZ/(farZ-nearZ)*(1-nearZ/z);
    auto depth=make(constantImage(nd)),normal=make(constantImage(0,0,-1,1)),zero=make(constantImage(0)),white=make(constantImage(1,1,1,1));
    for(unsigned s=0;s<16;++s){ok(d->SetTexture(s,zero));for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,k,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto k:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(s,k,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));}
    ok(d->SetTexture(1,depth));ok(d->SetTexture(12,white));ok(d->SetTexture(14,normal));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto k:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_SCISSORTESTENABLE})ok(d->SetRenderState(k,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    float c[84][4]{};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=nearZ;c[0][3]=farZ;c[1][0]=c[1][1]=100000;c[1][2]=c[2][0]=1;
    c[3][0]=c[4][1]=c[5][2]=c[6][3]=1;c[20][2]=1;c[33][0]=W;c[33][1]=H;c[33][2]=W;c[33][3]=H;c[52][0]=1;c[52][1]=1;
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
    auto render=[&](unsigned shader){ok(d->SetPixelShader(shaders[shader]));ok(d->SetPixelShaderConstantF(0,c[0],84));D3DVIEWPORT9 vp{0,0,W,H,0,1};ok(d->SetViewport(&vp));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));float result;std::memcpy(&result,static_cast<char*>(lock.pBits)+(H/2)*lock.Pitch+(W/2)*16,4);ok(staging->UnlockRect());return result;};
    unsigned cases=0;
    for(float distance:{25.f,50.f,100.f,300.f,1100.f})for(float intensity:{0.f,.25f,1.f})for(float sunlight:{0.f,.7f}){
        NorthlightLocalLights::Light l{};l.kind=2;l.flags=NorthlightLocalLights::Flash;l.sourceId=0xfed0000000000001ull;l.diffuse[0]=intensity;l.attenuationStart=.5f;l.attenuationEnd=1000;
        c[36][2]=z-distance;c[36][3]=l.attenuationEnd;c[44][0]=intensity;c[44][3]=NorthlightLocalLightSelection::transport(l);c[52][2]=sunlight;
        const float expected=intensity?intensity*NorthlightLocalLights::attenuation(l,distance):0,actual=render(0);
        assert(std::isfinite(actual)&&std::fabs(actual-expected)<.0001f);++cases;
    }
    c[36][2]=26;c[36][3]=8;c[44][0]=.3f;c[44][3]=1/7.5f;
    for(float sunlight:{0.f,.7f}){c[52][2]=sunlight;const float specialized=render(0),ordinary=render(1);assert(std::fabs(specialized-ordinary)<1e-6f);++cases;}
    std::printf("PASS flash GPU: %u inverse-square/intensity/daylight/extinction and ordinary-lamp parity cases\n",cases);
    for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));ok(d->SetPixelShader(nullptr));for(auto t:owned)t->Release();for(auto p:shaders)p->Release();staging->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
