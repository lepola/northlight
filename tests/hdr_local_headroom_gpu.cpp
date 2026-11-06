// HDR light is bounded by the receiver's transported radiance, without an SDR ceiling.
#define main unused_composite_distance_main
#include "composite_distance_gpu.cpp"
#undef main
#include <array>
static Image solid(float r,float g=0,float b=0,float a=0){Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};const float value[]={r,g,b,a};std::memcpy(im.pixels.data(),value,16);return im;}
static float linear(float x){return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);}

int main(int argc,char** argv){
 assert(argc==3);constexpr unsigned N=4;
 auto window=CreateWindowW(L"STATIC",L"",WS_POPUP,0,0,N,N,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
 auto module=LoadLibraryA(argv[2]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));auto api=create(D3D_SDK_VERSION);assert(api);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=N;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
 IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(N,N,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(N,N,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
 ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
 for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_FOGENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_STENCILENABLE})ok(d->SetRenderState(state,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
 const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{N-.5f,-.5f,0,1,1,0},{-.5f,N-.5f,0,1,0,1},{N-.5f,N-.5f,0,1,1,1}};
 const char* entries[]={"WorldComposite","WorldPrelitComposite","WorldCompositeHdr","WorldPrelitCompositeHdr","WorldMaterialCompositeHdr"};unsigned cases=0;float worst=0;
 for(unsigned variant=0;variant<5;++variant){
  const bool hdr=variant>=2;auto code=read(std::string(argv[1])+"/"+entries[variant]+".bin");IDirect3DPixelShader9* shader=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shader));ok(d->SetPixelShader(shader));
  for(const auto& hdrBase:std::array<std::array<float,3>,3>{{{{1.2f,1.2f,1.2f}},{{2.f,.8f,.4f}},{{.1f,.2f,.3f}}}})for(bool lamp:{false,true}){
   const float native[]={.5f,.6f,.7f},local[]={lamp?.2f:0.f,lamp?.11f:0.f,lamp?.04f:0.f};
   std::vector<IDirect3DTexture9*> owned;auto bind=[&](unsigned slot,Image im){auto* t=texture(d,im);owned.push_back(t);ok(d->SetTexture(slot,t));};
   for(unsigned slot=0;slot<16;++slot){bind(slot,solid(0));for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(slot,state,D3DTEXF_POINT));ok(d->SetSamplerState(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(slot,D3DSAMP_SRGBTEXTURE,FALSE));for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(slot,state,D3DTADDRESS_CLAMP));}
   bind(0,solid(native[0],native[1],native[2],.8f));bind(1,solid(.5f));bind(2,hdr?solid(hdrBase[0],hdrBase[1],hdrBase[2]):solid(0));bind(3,solid(local[0],local[1],local[2]));
   bind(4,solid(0,0,0,1));bind(5,solid(native[0],native[1],native[2]));bind(9,solid(0,0,0,1));bind(10,solid(0,0,0,1));bind(12,solid(.5f,.5f,.5f,1));bind(15,solid(.2f*10/(10+.2f)));
   float c[80][4]{};c[0][0]=c[0][1]=1.f/N;c[0][2]=.1f;c[0][3]=10;c[1][0]=c[1][1]=c[1][2]=c[2][0]=1;
   for(unsigned i=0;i<4;++i)c[3+i][i]=1;c[25][1]=c[25][2]=1;for(unsigned i=0;i<4;++i)c[33][i]=N;
   ok(d->SetPixelShaderConstantF(0,c[0],80));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock;ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));const auto* actual=static_cast<const float*>(lock.pBits);
   float add[3],room[3],ratio=0;for(unsigned k=0;k<3;++k){const float source=hdr?hdrBase[k]:native[k];add[k]=std::min((hdr?linear(native[k]):native[k])/.5f,1.f)*local[k];room[k]=hdr?source:std::min(source,.5f*std::max(1-source,0.f));ratio=std::max(ratio,add[k]/std::max(room[k],.00001f));}
   for(unsigned k=0;k<3;++k){const float source=hdr?hdrBase[k]:native[k],expected=source+std::min(add[k]/(1+ratio),room[k]),error=std::fabs(actual[k]-expected);worst=std::max(worst,error);
    if(!(error<.00003f)){std::fprintf(stderr,"HDR_LOCAL_HEADROOM_FAIL shader=%s base=%g,%g,%g lamp=%u channel=%u actual=%g expected=%g error=%g\n",entries[variant],hdrBase[0],hdrBase[1],hdrBase[2],unsigned(lamp),k,actual[k],expected,error);return 1;}}
   assert(std::fabs(actual[3]-.8f)<.00001f);ok(staging->UnlockRect());for(unsigned slot=0;slot<16;++slot)ok(d->SetTexture(slot,nullptr));for(auto* t:owned)t->Release();++cases;
  }shader->Release();
 }
 std::printf("HDR_LOCAL_HEADROOM_PASS cases=%u worst=%g above_one colored_HDR low_energy SDR_one_stop_cap alpha_no_lamp_control\n",cases,worst);
 staging->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
 return 0;
}
