// Issue #101: stationary particle publication reaches both shipped direct and fog shaders.
#define main northlight_unused_composite_replay
#include "composite_distance_gpu.cpp"
#undef main
#include "local_light_batches.h"
#include "particle_lights.h"
static Image solid(float r,float g=0,float b=0,float a=0){Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};const float p[]={r,g,b,a};std::memcpy(im.pixels.data(),p,16);return im;}
int main(int argc,char** argv){
 assert(argc==3||argc==4);constexpr unsigned W=66,H=50,N=W/2,M=H/2;
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightParticleDrawProbe";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
 auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,W,H,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
 auto module=LoadLibraryA(argv[2]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=W;pp.BackBufferHeight=H;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
 IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 IDirect3DPixelShader9* ps[2]{};for(unsigned i=0;i<2;++i){auto code=read(std::string(argv[1])+"/"+(i?"LocalFog.bin":"LocalDirect.bin"));ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&ps[i]));}
 IDirect3DPixelShader9* single=nullptr;if(argc==4){auto code=read(argv[3]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&single));}
 IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(N,M,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(N,M,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
 for(unsigned s=0;s<16;++s){for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,k,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto k:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(s,k,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));}
 ok(d->SetDepthStencilSurface(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
 for(auto k:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SEPARATEALPHABLENDENABLE})ok(d->SetRenderState(k,FALSE));
 ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
 std::vector<IDirect3DTexture9*> owned;auto make=[&](Image im){auto t=texture(d,im);owned.push_back(t);return t;};
 const float nearZ=.2f,farZ=1000,z=30,nd=farZ/(farZ-nearZ)*(1-nearZ/z);
 auto depth=make(solid(nd)),normal=make(solid(0,0,-1,1)),zero=make(solid(0)),white=make(solid(1,1,1,1));
 float c[84][4]{};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=nearZ;c[0][3]=farZ;c[1][0]=1.25f;c[1][1]=1.65f;c[1][2]=1;c[2][0]=1;
 c[3][0]=c[4][1]=c[5][2]=c[6][3]=1;c[6][0]=-12400;c[6][1]=220;c[6][2]=50;std::memcpy(c[15],c[6],12);
 c[20][2]=1;c[21][3]=128;c[33][0]=W;c[33][1]=H;c[33][2]=N;c[33][3]=M;c[52][1]=.8f;c[58][0]=3.5f;c[58][1]=1.f/12;c[58][2]=8;c[58][3]=.1f;
 const auto view=NorthlightLocalLightScissor::view(c[3],c[1],nearZ,3.5f,W,H);assert(view.valid);
 const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{N-.5f,-.5f,0,1,1,0},{-.5f,M-.5f,0,1,0,1},{N-.5f,M-.5f,0,1,1,1}};
 auto pixels=[&]{ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT l{};ok(staging->LockRect(&l,nullptr,D3DLOCK_READONLY));std::vector<float> out(N*M*4);for(unsigned y=0;y<M;++y)std::memcpy(out.data()+y*N*4,static_cast<char*>(l.pBits)+y*l.Pitch,N*16);ok(staging->UnlockRect());return out;};
 unsigned cases=0,hits=0,failures=0;
 std::vector<unsigned> counts={1,4,8,9,12,14,16,17,24,32,128,136};if(single)counts.insert(counts.end(),{2,3,5,6});
 for(unsigned count:counts)for(unsigned slot:{0u,count-1})for(unsigned kind=0;kind<2;++kind)for(float x:{-18.f,0.f,18.f}){
  NorthlightParticleLights::Frame frame;NorthlightLocalLightSelection::Tracker tracker;
  NorthlightLocalLightSelection::Selection published;std::vector<NorthlightLocalLights::Light> fixed;
  const NorthlightParticleLights::Sample sample{{c[6][0]+x,c[6][1],c[6][2]+26},{.04f,.12f,.7f}};
  std::uint64_t id=0;
  for(unsigned tick=0;tick<64;++tick){frame.nextFrame();frame.observe(sample,c[15]);frame.publish(nullptr,0,fixed,c[15],tracker,1.f/120);
   published=tracker.update(fixed,c[15],32,1.f/120,true,&view,frame.output.data(),frame.outputCount,frame.immediate.data(),frame.immediateCount);
   assert(frame.outputCount==1&&frame.immediateCount==1&&published.count==1&&published.color[0][2]>.48f);
   if(tick)assert(frame.output[0].sourceId==id);id=frame.output[0].sourceId;
  }
  NorthlightLocalLightSelection::Selection lights;lights.count=count;
  for(unsigned i=0;i<count;++i){lights.position[i]={c[6][0]+2000,c[6][1],c[6][2]+26,5};lights.color[i]={0,0,0,.25f};}
  lights.position[slot]=published.position[0];lights.color[slot]=published.color[0];lights.fog[slot][0]=.006f;
  auto render=[&](bool scissor){
   ok(d->SetRenderTarget(0,target));ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetPixelShaderConstantF(0,c[0],84));
   ok(d->SetTexture(1,depth));ok(d->SetTexture(9,zero));ok(d->SetTexture(11,zero));ok(d->SetTexture(12,white));ok(d->SetTexture(14,normal));ok(d->SetPixelShader(ps[kind]));
   const unsigned batchSize=kind?4:8;
   for(unsigned first=0;first<count;first+=batchSize){
    float pos[8][4]{},color[8][4]{},fog[8][4]{};for(unsigned i=0;i<batchSize&&first+i<count;++i){std::memcpy(pos[i],lights.position[first+i].data(),16);std::memcpy(color[i],lights.color[first+i].data(),16);std::memcpy(fog[i],lights.fog[first+i].data(),16);}
    ok(d->SetPixelShaderConstantF(36,pos[0],batchSize));ok(d->SetPixelShaderConstantF(44,color[0],batchSize));if(kind)ok(d->SetPixelShaderConstantF(59,fog[0],batchSize));
    // Only the candidate side selects the optional one-light shader. All fog and multi-light controls retain their original shader.
    ok(d->SetPixelShader(single&&scissor&&!kind&&count-first==1?single:ps[kind]));
    NorthlightLocalLightScissor::Bounds bounds;for(unsigned i=0;i<batchSize&&first+i<count;++i)NorthlightLocalLightScissor::add(view,pos[i],bounds);const auto r=NorthlightLocalLightScissor::rect(view,bounds);
    ok(d->SetRenderState(D3DRS_SCISSORTESTENABLE,scissor));if(scissor){if(!r.area())continue;const RECT rr{r.left,r.top,r.right,r.bottom};ok(d->SetScissorRect(&rr));}
    D3DVIEWPORT9 vp{0,0,N,M,0,1};ok(d->SetViewport(&vp));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());
   }return pixels();
  };
  const auto a=render(false),b=render(true);bool hit=false;
  for(unsigned i=0;i<a.size();++i){hit|=a[i]>1e-6f;if(!std::isfinite(a[i])||a[i]!=b[i]){if(failures++<5)std::printf("FAIL count=%u slot=%u kind=%u x=%g at=%u baseline=%g clipped=%g\n",count,slot,kind,x,i,a[i],b[i]);}}
  hits+=hit;++cases;
 }
 std::printf("particle direct/fog GPU cases=%u lit=%u failures=%u stationaryFrames=64 actualItemGeometry=0\n",cases,hits,failures);assert(hits==cases);
 for(unsigned sampler=0;sampler<16;++sampler)ok(d->SetTexture(sampler,nullptr));ok(d->SetPixelShader(nullptr));for(auto t:owned)t->Release();for(auto shader:ps)shader->Release();if(single)single->Release();staging->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
