// Exercise the shipped LocalLighting bytecode, not a rewritten shader formula.
#define main unused_material_composite_main
#include "composite_distance_gpu.cpp"
#undef main
#include <array>
int main(int argc,char** argv){
    assert(argc==3);
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"LocalMaterialCorrection";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,32,32,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=32;pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    auto bytes=read(argv[2]);IDirect3DPixelShader9* shader=nullptr;ok(d->CreatePixelShader((DWORD*)bytes.data(),&shader));
    auto constantTexture=[&](std::array<float,4> value){Image image{4,4,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(256)};for(unsigned i=0;i<16;++i)std::memcpy(image.pixels.data()+i*16,value.data(),16);return texture(d,image);};
    const float nearZ=.1f,farZ=100,receiverZ=10;
    const float depthValue=farZ/(farZ-nearZ)*(1-nearZ/receiverZ);
    auto depth=constantTexture({depthValue,0,0,0}),normals=constantTexture({0,0,-1,1}),baseline=constantTexture({1,1,1,0}),neutral=constantTexture({0,0,0,0});
    IDirect3DCubeTexture9* cubes[2]{};
    for(unsigned visible=0;visible<2;++visible){ok(d->CreateCubeTexture(4,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&cubes[visible],nullptr));for(unsigned face=0;face<6;++face){D3DLOCKED_RECT lock{};ok(cubes[visible]->LockRect((D3DCUBEMAP_FACES)face,0,&lock,nullptr,0));for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x){float value=float(visible);std::memcpy((char*)lock.pBits+y*lock.Pitch+x*4,&value,4);}ok(cubes[visible]->UnlockRect((D3DCUBEMAP_FACES)face,0));}}
    IDirect3DTexture9* target=nullptr;IDirect3DSurface9 *surface=nullptr,*stage=nullptr;ok(d->CreateTexture(4,4,1,D3DUSAGE_RENDERTARGET,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&target,nullptr));ok(target->GetSurfaceLevel(0,&surface));ok(d->CreateOffscreenPlainSurface(4,4,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&stage,nullptr));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,surface));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetPixelShader(shader));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
    for(unsigned sampler=0;sampler<6;++sampler)for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(sampler,state,D3DTEXF_POINT));
    ok(d->SetTexture(0,depth));ok(d->SetTexture(2,neutral));ok(d->SetTexture(3,normals));ok(d->SetTexture(4,baseline));ok(d->SetTexture(5,neutral));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{3.5f,-.5f,0,1,1,0},{-.5f,3.5f,0,1,0,1},{3.5f,3.5f,0,1,1,1}};
    float c[13][4]{};c[0][0]=c[0][1]=.25f;c[0][2]=nearZ;c[0][3]=farZ;c[1][0]=c[1][1]=c[1][2]=1;c[2][0]=1;c[2][3]=.025f;c[3][0]=c[4][1]=c[5][2]=c[6][3]=1;c[7][2]=5;c[7][3]=1.f/18;c[8][3]=20;c[9][0]=.1f;c[9][1]=20;c[9][2]=.25f;c[9][3]=1;c[10][0]=1;c[10][2]=.85f;c[12][1]=48;
    unsigned cases=0;float maxError=0;
    for(unsigned color=0;color<2;++color)for(unsigned visible=0;visible<2;++visible)for(unsigned bump=0;bump<2;++bump)for(float gain:{0.f,.3f,1.f})for(float translation:{0.f,10000.f}){
        const std::array<float,3> rgb=color?std::array<float,3>{.01f,.05f,.3f}:std::array<float,3>{.3f,.08f,.01f};std::memcpy(c[8],rgb.data(),12);c[10][0]=gain;c[12][0]=float(bump);c[6][0]=translation;c[7][0]=translation;
        ok(d->SetTexture(1,cubes[visible]));ok(d->SetPixelShaderConstantF(0,c[0],13));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(surface,stage));D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));float value[4];std::memcpy(value,(char*)lock.pBits+2*lock.Pitch+2*16,16);ok(stage->UnlockRect());
        for(unsigned channel=0;channel<3;++channel){const float expected=visible?0:-rgb[channel]*(15.f/18)*(15.f/18)*gain;maxError=std::max(maxError,std::fabs(value[channel]-expected));assert(std::isfinite(value[channel])&&std::fabs(value[channel]-expected)<.0001f);assert(value[channel]<=.00001f);}
        assert(std::fabs(value[3])<.00001f);++cases;
    }
    std::printf("LOCAL_MATERIAL_CORRECTION_PASS cases=%u maxError=%.9g orange blue neutralDetail translation clear blocked gain alpha\n",cases,maxError);
    for(auto cube:cubes)cube->Release();for(auto tex:{depth,normals,baseline,neutral,target})tex->Release();stage->Release();surface->Release();shader->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
