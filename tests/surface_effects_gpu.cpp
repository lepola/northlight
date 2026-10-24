// Retained precipitation production shaders on an independent hidden D3D9 device.
#define main northlight_unused_distance_replay
#include "composite_distance_gpu.cpp"
#undef main
#include "surface_compiled_shaders.h"
#include <array>
static std::vector<unsigned char> pixels(IDirect3DSurface9* staging,unsigned w,unsigned h,unsigned bytes){
 D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> out(w*h*bytes);
 for(unsigned y=0;y<h;++y)std::memcpy(out.data()+y*w*bytes,static_cast<char*>(lock.pBits)+y*lock.Pitch,w*bytes);ok(staging->UnlockRect());return out;
}
int main(int argc,char** argv){
 assert(argc==2);WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightPrecipitationOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
 HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 IDirect3DPixelShader9* shaders[2]{};ok(d->CreatePixelShader(kPrecipitationQualityShader,&shaders[0]));ok(d->CreatePixelShader(kPrecipitationPerformanceShader,&shaders[1]));
 unsigned cases=0,targets=0,unsupported=0;
 for(unsigned odd:{0u,1u})for(D3DFORMAT format:{D3DFMT_A8R8G8B8,D3DFMT_A16B16G16R16F,D3DFMT_A32B32G32R32F})for(unsigned samples:{0u,2u,4u,8u}){
  const unsigned W=160+odd,H=128+odd,bytes=format==D3DFMT_A8R8G8B8?4:format==D3DFMT_A16B16G16R16F?8:16;
  IDirect3DSurface9 *target=nullptr,*resolved=nullptr,*staging=nullptr;
  if(FAILED(d->CreateRenderTarget(W,H,format,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&target,nullptr))){++unsupported;continue;}
  ++targets;ok(d->CreateRenderTarget(W,H,format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));ok(d->CreateOffscreenPlainSurface(W,H,format,D3DPOOL_SYSTEMMEM,&staging,nullptr));
  auto read=[&](){ok(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(resolved,staging));return pixels(staging,W,H,bytes);};
  ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));
  d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})d->SetRenderState(state,0);
  d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE);d->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_COLORWRITEENABLE,7);d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
  for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})d->SetSamplerState(3,state,D3DTADDRESS_CLAMP);for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(3,state,D3DTEXF_POINT);d->SetSamplerState(3,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(3,D3DSAMP_SRGBTEXTURE,FALSE);
  float c[10][4]{};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=.1f;c[0][3]=1000;c[1][0]=1.3f;c[1][1]=1.7f;c[1][2]=-1;c[2][1]=1;c[4][1]=1;c[5][2]=1;c[6][0]=-1;c[7][2]=3;c[7][3]=1;c[8][0]=16;c[8][1]=2;c[8][2]=9;
  const Vertex v[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
  ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x00143250,1,0));const auto clear=read();
  for(unsigned entry=0;entry<2;++entry)for(unsigned snow=0;snow<2;++snow){unsigned changed=0;
   for(unsigned mode=0;mode<3;++mode)for(unsigned phase=0;phase<8;++phase){
    Image depth{W,H,D3DFMT_R32F,4,std::vector<unsigned char>(W*H*4)};const float raw=mode==1?1000.f/(1000.f-.1f)*(1-.1f):1.f;for(unsigned at=0;at<W*H;++at)std::memcpy(depth.pixels.data()+at*4,&raw,4);auto input=texture(d,depth);d->SetTexture(3,input);
    c[1][3]=phase*.31f;c[3][0]=mode==0?0.f:1.f;c[3][1]=float(snow);ok(d->SetPixelShaderConstantF(0,c[0],10));ok(d->SetPixelShader(shaders[entry]));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x00143250,1,0));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(Vertex)));ok(d->EndScene());const auto result=read();
    if(mode<2)assert(result==clear);else changed+=result!=clear;
    if(bytes==8)for(unsigned at=0;at<result.size();at+=2){unsigned short bits;std::memcpy(&bits,result.data()+at,2);assert((bits&0x7c00)!=0x7c00);}
    if(bytes==16)for(unsigned at=0;at<result.size();at+=4){float value;std::memcpy(&value,result.data()+at,4);assert(std::isfinite(value));}
    ++cases;d->SetTexture(3,nullptr);input->Release();
   }assert(changed>0);
  }
  d->SetRenderTarget(0,nullptr);staging->Release();resolved->Release();target->Release();
 }
 std::printf("PASS precipitation: cases=%u targets=%u unsupportedTargets=%u; zero weather, foreground occlusion, rain/snow, odd grids, UNORM/FP16/FP32 HDR, MSAA0/2/4/8\n",cases,targets,unsupported);
 d->SetPixelShader(nullptr);for(auto* s:shaders)s->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
