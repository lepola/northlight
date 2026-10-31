// Standalone DXVK regression: never loads WoW or Northlight.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
static std::vector<unsigned char> read(const std::string& path){std::ifstream f(path,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
struct ConstantBank {float values[79][4]{};unsigned count=0;};
static ConstantBank constantBank(const std::vector<unsigned char>& bytes){
    static_assert(sizeof(float)==4);
    if(bytes.size()!=68*16&&bytes.size()!=79*16)throw std::invalid_argument("Expected 68 or 79 float4 registers");
    ConstantBank bank;bank.count=unsigned(bytes.size()/16);std::memcpy(bank.values,bytes.data(),bytes.size());return bank;
}
#ifndef NORTHLIGHT_FGR_CONSTANT_BANK_ONLY
#include <windows.h>
#include <d3d9.h>
static void ok(HRESULT hr){if(FAILED(hr)){std::fprintf(stderr,"HRESULT=%08lx\n",(unsigned long)hr);std::abort();}}
struct Image {std::uint32_t width,height,format,bytes;std::vector<unsigned char> pixels;};
static Image image(const std::string& path){auto b=read(path);std::uint32_t h[5];assert(b.size()>=sizeof h);std::memcpy(h,b.data(),sizeof h);assert(h[0]==0x31524746&&b.size()==20+std::size_t(h[1])*h[2]*h[4]);return {h[1],h[2],h[3],h[4],{b.begin()+20,b.end()}};}
static IDirect3DTexture9* texture(IDirect3DDevice9* dev,const Image& im){IDirect3DTexture9* tex=nullptr;ok(dev->CreateTexture(im.width,im.height,1,0,(D3DFORMAT)im.format,D3DPOOL_MANAGED,&tex,nullptr));D3DLOCKED_RECT lock{};ok(tex->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<im.height;++y)std::memcpy((char*)lock.pBits+y*lock.Pitch,im.pixels.data()+std::size_t(y)*im.width*im.bytes,im.width*im.bytes);ok(tex->UnlockRect(0));return tex;}
static void wait(IDirect3DQuery9* event){ok(event->Issue(D3DISSUE_END));HRESULT hr;while((hr=event->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE)SwitchToThread();ok(hr);}
static float half(std::uint16_t h){const unsigned e=(h>>10)&31, m=h&1023;const float v=e==0?std::ldexp(float(m),-24):e==31?(m?NAN:INFINITY):std::ldexp(float(1024+m),int(e)-25);return h&32768?-v:v;}
struct Vertex{float x,y,z,w,u,v;};
int main(int argc,char** argv){
    assert(argc==8);const std::string prefix=argv[1];
    auto bank=constantBank(read(prefix+"constants.f32"));auto& c=bank.values;
    auto light=image(prefix+"light.fgr"),dist=image(prefix+"distance.fgr"),scene=image(prefix+"scene.fgr"),base=image(prefix+"baseline.fgr"),normals=image(prefix+"normals.fgr");
    assert(light.format==D3DFMT_A16B16G16R16F&&dist.format==D3DFMT_R32F&&dist.width==light.width&&dist.height==light.height);
    const bool composite=std::strstr(argv[6],"composite")!=nullptr;
    const unsigned fw=scene.width,fh=scene.height,W=composite?fw:light.width,H=composite?fh:light.height;assert(fw==unsigned(c[33][0])&&fh==unsigned(c[33][1]));
    // Saved view distances reconstruct depth unless the fixture supplies it;
    // AO uses the supplied image or a neutral texture.
    Image depth{fw,fh,D3DFMT_R32F,4,std::vector<unsigned char>(std::size_t(fw)*fh*4)};
    for(unsigned y=0;y<fh;++y)for(unsigned x=0;x<fw;++x){float z;const std::size_t i=std::size_t(std::min(y*dist.height/fh,dist.height-1))*dist.width+std::min(x*dist.width/fw,dist.width-1);std::memcpy(&z,dist.pixels.data()+i*4,4);const float nd=c[0][3]/(c[0][3]-c[0][2])*(1-c[0][2]/std::max(z,c[0][2]));const float d=nd/c[2][0]+c[1][3];std::memcpy(depth.pixels.data()+(std::size_t(y)*fw+x)*4,&d,4);}
    const bool depthReconstructed=!std::ifstream(prefix+"depth.fgr").good();
    if(!depthReconstructed)depth=image(prefix+"depth.fgr");
    if(composite)light=image(prefix+"temporal-light.fgr");
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightShaderReplay";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[7]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* dev=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&dev));
    const bool neutralAO=!std::ifstream(prefix+"ao.fgr").good();
    IDirect3DTexture9* textures[]={texture(dev,scene),texture(dev,depth),texture(dev,light),texture(dev,base),texture(dev,normals),texture(dev,neutralAO?Image{1,1,D3DFMT_A8R8G8B8,4,{0,0,0,255}}:image(prefix+"ao.fgr"))};
    const unsigned slots[]={0,1,8,12,14,10};for(unsigned i=0;i<6;++i){ok(dev->SetTexture(slots[i],textures[i]));for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(dev->SetSamplerState(slots[i],state,i==0||i==5?D3DTEXF_LINEAR:D3DTEXF_POINT));ok(dev->SetSamplerState(slots[i],D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(dev->SetSamplerState(slots[i],D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(dev->SetSamplerState(slots[i],D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    IDirect3DTexture9 *fog=nullptr,*zero=nullptr;
    if(composite){fog=texture(dev,image(prefix+"fog.fgr"));zero=texture(dev,{1,1,D3DFMT_A8R8G8B8,4,{0,0,0,0}});ok(dev->SetTexture(9,fog));for(unsigned slot:{2u,11u,13u})ok(dev->SetTexture(slot,zero));for(unsigned slot:{2u,9u,11u,13u}){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(dev->SetSamplerState(slot,state,slot==9?D3DTEXF_LINEAR:D3DTEXF_POINT));ok(dev->SetSamplerState(slot,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(dev->SetSamplerState(slot,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));ok(dev->SetSamplerState(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE));}}
    IDirect3DPixelShader9* shaders[2]{};for(unsigned i=0;i<2;++i){auto b=read(argv[i+2]);ok(dev->CreatePixelShader((const DWORD*)b.data(),&shaders[i]));}
    IDirect3DPixelShader9* normalShaders[2]{};
    for(unsigned i=0;i<2;++i){auto b=read(argv[i+4]);ok(dev->CreatePixelShader((const DWORD*)b.data(),&normalShaders[i]));}
    IDirect3DTexture9* normalTexture=nullptr;IDirect3DSurface9* normalSurface=nullptr;
    ok(dev->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&normalTexture,nullptr));ok(normalTexture->GetSurfaceLevel(0,&normalSurface));
    IDirect3DTexture9* distanceTexture=nullptr;IDirect3DSurface9* distanceSurface=nullptr;
    ok(dev->CreateTexture(W,H,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&distanceTexture,nullptr));ok(distanceTexture->GetSurfaceLevel(0,&distanceSurface));
    for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(dev->SetSamplerState(15,state,D3DTEXF_POINT));ok(dev->SetSamplerState(15,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(dev->SetSamplerState(15,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(dev->SetSamplerState(15,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(dev->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(dev->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(dev->SetRenderTarget(0,target));ok(dev->SetDepthStencilSurface(nullptr));ok(dev->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(dev->SetRenderState(D3DRS_ZENABLE,FALSE));ok(dev->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(dev->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(dev->SetPixelShaderConstantF(0,c[0],bank.count));D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(dev->SetViewport(&vp));
    const Vertex vertices[]={{-.5f,-.5f,0,1,0,0},{float(W)-.5f,-.5f,0,1,1,0},{-.5f,float(H)-.5f,0,1,0,1},{float(W)-.5f,float(H)-.5f,0,1,1,1}};
    IDirect3DQuery9 *event=nullptr,*start=nullptr,*end=nullptr,*freq=nullptr,*disjoint=nullptr;ok(dev->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&freq));ok(dev->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
    std::vector<unsigned char> output[2], normalOutput[2];double means[2]={};
    unsigned activeMode=0;
    auto draw=[&](unsigned n){ok(dev->BeginScene());for(unsigned j=0;j<n;++j){
        ok(dev->SetTexture(14,nullptr));ok(dev->SetTexture(15,nullptr));
        ok(dev->SetRenderTarget(0,normalSurface));ok(dev->SetRenderTarget(1,distanceSurface));
        ok(dev->SetPixelShader(normalShaders[activeMode]));ok(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)));
        ok(dev->SetRenderTarget(1,nullptr));ok(dev->SetRenderTarget(0,target));
        ok(dev->SetTexture(14,normalTexture));ok(dev->SetTexture(15,distanceTexture));
        ok(dev->SetPixelShader(shaders[activeMode]));ok(dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex)));
    }ok(dev->EndScene());};
    for(unsigned block=0;block<8;++block){const unsigned mode=block%2;activeMode=mode;ok(dev->SetPixelShader(shaders[mode]));draw(4);wait(event);ok(disjoint->Issue(D3DISSUE_BEGIN));ok(start->Issue(D3DISSUE_END));draw(16);ok(end->Issue(D3DISSUE_END));ok(freq->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);UINT64 a=0,b=0,f=0;BOOL bad=FALSE;assert(start->GetData(&a,sizeof a,0)==S_OK&&end->GetData(&b,sizeof b,0)==S_OK&&freq->GetData(&f,sizeof f,0)==S_OK&&disjoint->GetData(&bad,sizeof bad,0)==S_OK&&!bad&&b>=a&&f);means[mode]+=double(b-a)/double(f)*1000/16/4;
        if(block<2){ok(dev->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));output[mode].resize(std::size_t(W)*H*8);for(unsigned y=0;y<H;++y)std::memcpy(output[mode].data()+std::size_t(y)*W*8,(char*)lock.pBits+y*lock.Pitch,W*8);ok(staging->UnlockRect());
            ok(dev->GetRenderTargetData(normalSurface,staging));ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            normalOutput[mode].resize(std::size_t(W)*H*8);
            for(unsigned y=0;y<H;++y)std::memcpy(normalOutput[mode].data()+std::size_t(y)*W*8,(char*)lock.pBits+y*lock.Pitch,W*8);
            ok(staging->UnlockRect());}
    }
    std::size_t changed=0,big=0,alpha=0,nonfinite=0;double maxAbs=0,sumSq=0;for(std::size_t i=0;i<std::size_t(W)*H*4;++i){std::uint16_t a,b;std::memcpy(&a,output[0].data()+i*2,2);std::memcpy(&b,output[1].data()+i*2,2);const double error=std::fabs(double(half(a))-half(b));nonfinite+=!std::isfinite(error);changed+=a!=b;alpha+=i%4==3&&a!=b;big+=error>1.0/1024;maxAbs=std::max(maxAbs,error);sumSq+=error*error;}
    std::printf("{\"capture\":\"%s\",\"old_ms\":%.6f,\"native_ms\":%.6f,\"ratio\":%.4f,\"components\":%llu,\"changed\":%llu,\"over_1_1024\":%llu,\"alpha_changes\":%llu,\"nonfinite\":%llu,\"max_abs\":%.9g,\"rmse\":%.9g,\"depth_reconstructed\":%s,\"AO_neutral\":%s,\"constant_register_count\":%u,\"constant_bank_stage\":\"unversioned\",\"invented_fog_environment\":false,\"neutral_composite_auxiliary_textures\":%s,\"per_source_bank_missing\":true,\"material_inputs_complete\":false,\"authentic_source_replay\":false,\"whole_scene_replay\":false}\n",argv[6],means[0],means[1],means[1]/means[0],(unsigned long long)std::size_t(W)*H*4,(unsigned long long)changed,(unsigned long long)big,(unsigned long long)alpha,(unsigned long long)nonfinite,maxAbs,std::sqrt(sumSq/(std::size_t(W)*H*4)),depthReconstructed?"true":"false",neutralAO?"true":"false",bank.count,composite?"true":"false");std::fflush(stdout);
    assert(!nonfinite&&!alpha&&maxAbs<=1.0/1024);
    std::size_t normalChanged=0,normalAlpha=0;double normalMax=0;
    for(std::size_t i=0;i<normalOutput[0].size()/2;++i){std::uint16_t a,b;
        std::memcpy(&a,normalOutput[0].data()+i*2,2);std::memcpy(&b,normalOutput[1].data()+i*2,2);
        const double error=std::fabs(double(half(a))-half(b));assert(std::isfinite(error));
        normalChanged+=a!=b;normalAlpha+=(i%4==3&&a!=b);normalMax=std::max(normalMax,error);
    }
    std::printf("{\"normal_components\":%llu,\"changed\":%llu,\"max_abs\":%.9g}\n",
       (unsigned long long)(normalOutput[0].size()/2),(unsigned long long)normalChanged,normalMax);
    std::fflush(stdout);assert(normalMax<=1.0/1024&&!normalAlpha);
for(auto* shader:normalShaders)shader->Release();normalSurface->Release();normalTexture->Release();distanceSurface->Release();distanceTexture->Release();if(fog)fog->Release();if(zero)zero->Release();for(auto* shader:shaders)shader->Release();for(auto* tex:textures)tex->Release();for(auto* query:{event,start,end,freq,disjoint})query->Release();staging->Release();target->Release();dev->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
#endif
