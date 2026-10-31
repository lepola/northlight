// Exercises the production relative-normal codec under camera rotations.
#define main northlight_unused_composite_main
#include "composite_distance_gpu.cpp"
#undef main
#include <array>
using Vec=std::array<float,3>;
static Vec unit(Vec v){float l=std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);for(auto& x:v)x/=l;return v;}
static Vec view(Vec v,float a,float b){Vec w={std::cos(a)*v[0]+std::sin(a)*v[2],v[1],-std::sin(a)*v[0]+std::cos(a)*v[2]};return {w[0],std::cos(b)*w[1]+std::sin(b)*w[2],-std::sin(b)*w[1]+std::cos(b)*w[2]};}
int main(int argc,char** argv){
    assert(argc==3);const std::string folder=argv[2];
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightMaterialRotation";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,32,32,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=32;pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* shaders[3]{};unsigned i=0;for(const char* file:{"capture-legacy.bin","capture-world.bin","decode.bin"}){auto bytes=read(folder+file);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(bytes.data()),&shaders[i++]));}
    IDirect3DTexture9 *capture=nullptr,*result=nullptr;IDirect3DSurface9 *captureSurface=nullptr,*resultSurface=nullptr,*stage=nullptr;
    ok(d->CreateTexture(4,4,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&capture,nullptr));ok(capture->GetSurfaceLevel(0,&captureSurface));
    ok(d->CreateTexture(4,4,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&result,nullptr));ok(result->GetSurfaceLevel(0,&resultSurface));ok(d->CreateOffscreenPlainSurface(4,4,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&stage,nullptr));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{3.5f,-.5f,0,1,1,0},{-.5f,3.5f,0,1,0,1},{3.5f,3.5f,0,1,1,1}};
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(9,key,D3DTEXF_POINT));ok(d->SetSamplerState(9,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
    float c[96][4]{};c[20][2]=.001f;c[82][0]=.75f;c[82][1]=48;
    float maximum[2]{},neutralError=0,matchedError=0;unsigned cases=0;
    for(Vec direction:std::vector<Vec>{{.7f,.2f,1},{1,.2f,0},{0,1,.1f},{.2f,.1f,-1},{0,0,-1},{.001f,0,-1}}){
    const Vec reference=unit(direction),base=unit({reference[0]-.15f,reference[1]+.1f,reference[2]}),mapped=unit({reference[0]+.2f,reference[1]-.15f,reference[2]});
    for(unsigned kind=0;kind<3;++kind)for(unsigned codec=0;codec<2;++codec){
        Vec first{};for(unsigned angle=0;angle<41;++angle){
            const float a=-2.f+angle*.1f,b=.3f*std::sin(a);const auto ref=view(reference,a,b),detail=view(kind==1?reference:mapped,a,b),scene=kind==2?reference:base;
            std::memcpy(c[80],ref.data(),12);std::memcpy(c[81],detail.data(),12);std::memcpy(c[83],scene.data(),12);
            float inverse[3][4]={};for(unsigned j=0;j<3;++j){Vec axis{};axis[j]=1;const auto column=view(axis,a,b);for(unsigned k=0;k<3;++k)inverse[k][j]=column[k];}
            std::memcpy(c[26],inverse,sizeof inverse);std::memcpy(c[84],inverse,sizeof inverse);ok(d->SetPixelShaderConstantF(0,c[0],96));
            ok(d->SetTexture(9,nullptr));ok(d->SetRenderTarget(0,captureSurface));ok(d->SetPixelShader(shaders[codec]));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());
            ok(d->SetRenderTarget(0,resultSurface));ok(d->SetTexture(9,capture));ok(d->SetPixelShader(shaders[2]));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(resultSurface,stage));
            D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));Vec value{};std::memcpy(value.data(),lock.pBits,12);ok(stage->UnlockRect());
            if(!angle)first=value;for(unsigned k=0;k<3;++k){assert(std::isfinite(value[k]));if(!kind)maximum[codec]=std::max(maximum[codec],std::fabs(value[k]-first[k]));if(kind==1)neutralError=std::max(neutralError,std::fabs(value[k]-scene[k]));
                if(kind==2&&codec==1){auto expected=unit({reference[0]*.25f+mapped[0]*.75f,reference[1]*.25f+mapped[1]*.75f,reference[2]*.25f+mapped[2]*.75f});matchedError=std::max(matchedError,std::fabs(value[k]-expected[k]));}}
            ++cases;
        }
    }
    }
    std::printf("MATERIAL_FRAME_ROTATION cases=%u legacyCameraDelta=%.9g worldCameraDelta=%.9g neutralError=%.9g matchedError=%.9g\n",cases,maximum[0],maximum[1],neutralError,matchedError);
    std::fflush(stdout);
    assert(maximum[0]>.02f&&maximum[1]<.0015f&&neutralError<.0015f&&matchedError<.0015f);
    std::puts("MATERIAL_FRAME_ROTATION_PASS");
    for(auto* p:shaders)p->Release();stage->Release();resultSurface->Release();captureSurface->Release();result->Release();capture->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
