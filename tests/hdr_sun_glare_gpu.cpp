// Production celestial shaders and HDR presentation, with authored depth and
// generated wrong-depth/wrong-energy controls. No game assets or draw stream.
#undef NDEBUG
#pragma clang diagnostic ignored "-Wreturn-type"
#define main unused_hdr_sun_replay
#include "composite_distance_gpu.cpp"
#undef main
#include "celestial_disc_compiled_shaders.h"
#include "hdr_frame.h"

int main(int argc,char** argv){
    assert(argc==5);constexpr unsigned N=64;
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,N,N,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto library=LoadLibraryA(argv[1]);assert(library);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(library,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=N;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DSurface9 *target=nullptr,*output=nullptr,*staging=nullptr;
    ok(d->CreateRenderTarget(N,N,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateRenderTarget(N,N,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&output,nullptr));
    ok(d->CreateOffscreenPlainSurface(N,N,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    IDirect3DPixelShader9 *disc=nullptr,*visibility=nullptr,*veil=nullptr,*wrongDisc=nullptr,*wrongVisibility=nullptr,*wrongVeil=nullptr,*copy=nullptr;
    ok(d->CreatePixelShader(kCelestialDiscPSShader,&disc));ok(d->CreatePixelShader(kCelestialHaloVisibilityPSShader,&visibility));ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCopy),&copy));
    ok(d->CreatePixelShader(kCelestialVeilPSShader,&veil));
    auto code=read(argv[2]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&wrongDisc));code=read(argv[3]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&wrongVisibility));
    code=read(argv[4]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&wrongVeil));
    auto* white=texture(d,{1,1,D3DFMT_A8R8G8B8,4,{255,255,255,255}});
    float c[49][4]{};c[0][0]=c[0][1]=1.f/N;c[0][3]=1/.94f;c[1][0]=c[1][1]=5;c[1][2]=1;
    const float cs=std::cos(.35f),sn=std::sin(.35f);
    c[2][1]=1;c[3][0]=-sn;c[3][2]=cs;c[4][0]=cs;c[4][2]=sn;c[5][3]=1;
    c[6][0]=cs;c[6][2]=sn;c[6][3]=.04f;c[7][1]=1;c[8][0]=-sn;c[8][2]=cs;
    c[9][0]=c[9][1]=c[9][2]=c[9][3]=1;c[10][0]=2;c[10][1]=.2f;c[10][2]=1000;c[10][3]=1;
    c[11][2]=1.5f/16777215;c[11][3]=1;c[12][0]=1.1f;c[12][1]=20;c[12][2]=2.4f;c[12][3]=1;
    c[46][3]=2;c[47][0]=c[47][1]=c[47][2]=1;c[47][3]=1.8f;c[48][0]=c[48][1]=c[48][2]=1;
    for(unsigned k=0;k<32;++k){c[13+k][0]=c[13+k][1]=c[13+k][2]=c[13+k][3]=.5f;}
    auto pixels=[&](IDirect3DSurface9* surface){
        ok(d->GetRenderTargetData(surface,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::vector<float> result(N*N*4);for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N*4;++x){std::uint16_t bits;std::memcpy(&bits,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*2,2);result[y*N*4+x]=half(bits);}
        ok(staging->UnlockRect());return result;
    };
    auto render=[&](IDirect3DPixelShader9* shader,IDirect3DTexture9* depth,IDirect3DTexture9* halo=nullptr){
        ok(NorthlightHdr::quadState(d,target,N,N,6));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->SetPixelShader(shader));
        ok(d->SetTexture(0,depth));ok(d->SetTexture(1,white));ok(d->SetTexture(3,halo?halo:white));ok(d->SetTexture(5,halo?halo:white));ok(d->SetPixelShaderConstantF(0,c[0],49));
        ok(d->BeginScene());ok(NorthlightHdr::drawQuad(d,N,N));ok(d->EndScene());return pixels(target);
    };
    auto depthTexture=[&](float raw){Image im{N,N,D3DFMT_R32F,4,std::vector<unsigned char>(N*N*4)};for(unsigned i=0;i<N*N;++i)std::memcpy(im.pixels.data()+i*4,&raw,4);return texture(d,im);};
    unsigned cases=0,oldLeaks=0;
    for(float raw:{.5f,.93999f,.940001f,.9985f,.9999f,1.f}){
        auto* depth=depthTexture(raw);const auto result=render(disc,depth);
        float peak=0;for(unsigned i=0;i<N*N;++i){assert(std::isfinite(result[i*4]));peak=std::max(peak,result[i*4]);}
        assert(raw==1?peak>0:peak==0);++cases;
        c[45][0]=1;c[45][1]=.01f;c[45][2]=.01f;
        const auto source=render(visibility,depth);assert(raw==1?source[0]>.9f:source[0]==0);++cases;
        if(raw==.9985f){const auto wrong=render(wrongDisc,depth);const auto wrongSource=render(wrongVisibility,depth);assert(wrongSource[0]>.9f);for(unsigned i=0;i<N*N;++i)oldLeaks+=wrong[i*4]>0;assert(oldLeaks);}
        depth->Release();
    }
    auto* clear=depthTexture(1);const auto halo=render(disc,clear),overbright=render(wrongDisc,clear);
    float haloPeak=0,oldPeak=0;for(unsigned i=0;i<N*N;++i){haloPeak=std::max(haloPeak,halo[i*4]);oldPeak=std::max(oldPeak,overbright[i*4]);}
    assert(haloPeak>.5f&&haloPeak<=1.001f&&oldPeak>20);++cases;
    // The authored SDR halo is the transfer reference, including faint tails
    // and saturated tints. A shoulder of decoded tint is not equivalent.
    float maxTransferError=0;unsigned transferPixels=0;
    const float tints[][3]={{1,1,1},{1,.45f,.08f},{.15f,.45f,1}};
    for(const auto& tint:tints)for(float weight:{.02f,.2f,.9f,1.8f}){
        for(unsigned k=0;k<3;++k)c[47][k]=c[48][k]=tint[k];
        c[47][3]=weight;c[10][0]=0;const auto encoded=render(disc,clear);
        c[10][0]=2;const auto linear=render(disc,clear);
        for(unsigned i=0;i<N*N;++i)for(unsigned k=0;k<3;++k){
            const float x=encoded[i*4+k],expected=x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);
            const float error=std::fabs(linear[i*4+k]-expected);maxTransferError=std::max(maxTransferError,error);
            assert(error<.0025f);++transferPixels;
        }
        ++cases;
    }
    // The late veil must use the same authored shoulder/transfer order as the
    // sky halo. A fully hidden source contributes nothing even over geometry.
    auto* blocked=depthTexture(.9985f);
    auto* hidden=texture(d,{1,1,D3DFMT_A8R8G8B8,4,{0,0,0,0}});
    float maxVeilError=0,maxWrongVeilError=0;unsigned veilChannels=0;
    for(const auto& tint:tints)for(float weight:{.02f,.2f,.9f,1.8f}){
        for(unsigned k=0;k<3;++k)c[47][k]=c[48][k]=tint[k];
        c[47][3]=weight;c[10][0]=0;const auto encoded=render(veil,blocked);
        c[10][0]=2;const auto linear=render(veil,blocked),wrong=render(wrongVeil,blocked);
        for(unsigned i=0;i<N*N;++i)for(unsigned k=0;k<3;++k){
            const float x=encoded[i*4+k],expected=x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);
            const float error=std::fabs(linear[i*4+k]-expected);maxVeilError=std::max(maxVeilError,error);
            maxWrongVeilError=std::max(maxWrongVeilError,std::fabs(wrong[i*4+k]-expected));
            assert(error<.0025f);assert(linear[i*4+3]==encoded[i*4+3]);++veilChannels;
        }
        ++cases;
    }
    assert(maxWrongVeilError>.1f);
    for(auto* depth:{blocked,clear}){const auto result=render(veil,depth,hidden);for(float x:result)assert(x==0);++cases;}
    {const auto result=render(veil,clear);for(float x:result)assert(x==0);++cases;}
    blocked->Release();hidden->Release();
    c[47][3]=1.8f;for(unsigned k=0;k<3;++k)c[47][k]=c[48][k]=1;
    c[12][3]=0;c[12][0]=1;const auto body=render(disc,clear);
    float bodyPeak=0;for(unsigned i=0;i<N*N;++i)bodyPeak=std::max(bodyPeak,body[i*4]);assert(bodyPeak>7.9f&&bodyPeak<8.01f);++cases;
    NorthlightHdr::Frame frame;NorthlightHdr::Settings settings;settings.autoExposure=false;settings.bloom=0;settings.paperWhite=203;settings.maxNits=1500;
    ok(d->BeginScene());ok(frame.prepareLegacy(d,white,nullptr,nullptr,N,N,0,.94f,settings));ok(d->EndScene());
    unsigned bright[2]{};float presentedPeak[2]{};
    for(unsigned old=0;old<2;++old){
        Image im{N,N,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(N*N*16)};
        for(unsigned i=0;i<N*N;++i){float pixel[4]={0,0,0,1};for(unsigned k=0;k<3;++k)pixel[k]=.1f+body[i*4+k]*body[i*4+3]+(old?overbright:halo)[i*4+k];std::memcpy(im.pixels.data()+i*16,pixel,16);}
        auto* scene=texture(d,im);ok(NorthlightHdr::quadState(d,target,N,N));ok(d->SetTexture(0,scene));ok(d->SetPixelShader(copy));
        ok(d->BeginScene());ok(NorthlightHdr::drawQuad(d,N,N));ok(frame.finish(d,target,output,settings,0,true,false));ok(d->EndScene());
        const auto presented=pixels(output);for(unsigned i=0;i<N*N;++i){const float nits=80*presented[i*4];assert(std::isfinite(nits)&&nits<=1501);bright[old]+=nits>203*1.2f;presentedPeak[old]=std::max(presentedPeak[old],nits);}scene->Release();++cases;
    }
    assert(bright[0]>0&&bright[1]>bright[0]*4&&presentedPeak[0]>500);
    std::printf("HDR_SUN_GPU_PASS cases=%u transferPixels=%u transferMaxError=%g veilChannels=%u veilMaxError=%g wrongVeilMaxError=%g horizonCounterfactualPixels=%u glarePeak=%g oldGlarePeak=%g compactSourcePeak=%g brightArea=%u/%u presentedPeakNits=%g/%g\n",cases,transferPixels,maxTransferError,veilChannels,maxVeilError,maxWrongVeilError,oldLeaks,haloPeak,oldPeak,bodyPeak,bright[0],bright[1],presentedPeak[0],presentedPeak[1]);
    frame.reset();for(unsigned k=0;k<6;++k)d->SetTexture(k,nullptr);d->SetPixelShader(nullptr);
    clear->Release();white->Release();disc->Release();visibility->Release();veil->Release();wrongDisc->Release();wrongVisibility->Release();wrongVeil->Release();copy->Release();target->Release();output->Release();staging->Release();d->Release();api->Release();FreeLibrary(library);DestroyWindow(window);
}
