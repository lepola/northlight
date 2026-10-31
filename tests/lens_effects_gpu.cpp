// Offscreen authored lens optics: production draw path, target resolve and state restoration.
#define NORTHLIGHT_SURFACE_HELPERS_ONLY
#define NORTHLIGHT_TEST_REAL_NAME_MASK
#include "surface_effects_gpu.cpp"
#undef NORTHLIGHT_SURFACE_HELPERS_ONLY
struct NorthlightWeatherLensTestAccess {static auto& state(NorthlightSurfaceRenderer& renderer){return renderer.lensState;}};
static std::vector<unsigned char> readPixels(IDirect3DSurface9* stage,unsigned w,unsigned h,unsigned bytes){D3DLOCKED_RECT l{};ok(stage->LockRect(&l,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> out(size_t(w)*h*bytes);for(unsigned y=0;y<h;++y)std::memcpy(out.data()+size_t(y)*w*bytes,static_cast<const char*>(l.pBits)+size_t(y)*l.Pitch,size_t(w)*bytes);ok(stage->UnlockRect());return out;}
static void writePixels(const char* path,const std::vector<unsigned char>& data){std::ofstream f(path,std::ios::binary);assert(f);f.write(reinterpret_cast<const char*>(data.data()),data.size());assert(f.good());}
int main(int argc,char** argv){
 assert(argc==3);requireNoGame();WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightLensTest";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
 HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));const auto src=image(argv[2]);assert(src.format==D3DFMT_A8R8G8B8&&src.bytes==4);const unsigned w=src.width,h=src.height;auto input=texture(d,src);auto shifted=src;for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)std::memcpy(shifted.pixels.data()+(size_t(y)*w+x)*4,src.pixels.data()+(size_t(y)*w+(x+43)%w)*4,4);auto movedInput=texture(d,shifted);
 {
 NorthlightSurfaceRenderer lens(d);
 for(D3DFORMAT format:{D3DFMT_A8R8G8B8,D3DFMT_A16B16G16R16F,D3DFMT_A32B32G32R32F})for(unsigned samples:{0u,4u}){requireNoGame();const unsigned bytes=format==D3DFMT_A8R8G8B8?4:format==D3DFMT_A16B16G16R16F?8:16,channel=bytes/4;IDirect3DSurface9 *target=nullptr,*resolved=nullptr,*stage=nullptr,*scratchSurface=nullptr;IDirect3DTexture9* scratch=nullptr;
  ok(d->CreateRenderTarget(w,h,format,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&target,nullptr));ok(d->CreateRenderTarget(w,h,format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));ok(d->CreateOffscreenPlainSurface(w,h,format,D3DPOOL_SYSTEMMEM,&stage,nullptr));ok(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,format,D3DPOOL_DEFAULT,&scratch,nullptr));ok(scratch->GetSurfaceLevel(0,&scratchSurface));
  auto read=[&](){ok(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(resolved,stage));return readPixels(stage,w,h,bytes);};
  auto background=[&](IDirect3DTexture9* tex=nullptr){ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp{0,0,w,h,0,1};ok(d->SetViewport(&vp));ok(d->SetVertexShader(nullptr));ok(d->SetPixelShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetTexture(0,tex?tex:input));ok(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE));ok(d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));ok(d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE));ok(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT));ok(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));const Vertex v[]={{-.5f,-.5f,0,1,0,0},{w-.5f,-.5f,0,1,1,0},{-.5f,h-.5f,0,1,0,1},{w-.5f,h-.5f,0,1,1,1}};ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(Vertex)));ok(d->EndScene());};
  background();const auto base=read();lens.lens(target,scratch,scratchSurface,w,h,false,1.f,0.f,nullptr);assert(read()==base);
  for(bool snow:{false,true}){requireNoGame();lens.reset();background();ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));D3DVIEWPORT9 before{},after{};ok(d->GetViewport(&before));IDirect3DBaseTexture9* beforeTexture=nullptr;ok(d->GetTexture(0,&beforeTexture));
   ok(d->BeginScene());lens.lens(target,scratch,scratchSurface,w,h,snow,1.f,.6f,nullptr);ok(d->EndScene());ok(d->GetViewport(&after));assert(!std::memcmp(&before,&after,sizeof before));DWORD blend=0,srcBlend=0;ok(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend));ok(d->GetRenderState(D3DRS_SRCBLEND,&srcBlend));assert(blend&&srcBlend==D3DBLEND_SRCALPHA);IDirect3DBaseTexture9* afterTexture=nullptr;ok(d->GetTexture(0,&afterTexture));assert(beforeTexture==afterTexture);drop(beforeTexture);drop(afterTexture);
   const auto first=read();unsigned changed=0;for(size_t i=0;i<first.size();i+=bytes){changed+=std::memcmp(first.data()+i,base.data()+i,3*channel)!=0;assert(!std::memcmp(first.data()+i+3*channel,base.data()+i+3*channel,channel));if(bytes==8)for(unsigned c=0;c<4;++c){unsigned short q;std::memcpy(&q,first.data()+i+2*c,2);assert((q&0x7c00)!=0x7c00);}if(bytes==16)for(unsigned c=0;c<4;++c){float q;std::memcpy(&q,first.data()+i+4*c,4);assert(std::isfinite(q));}}assert(changed>100);if(samples==0&&bytes==4)writePixels(snow?"snow.raw":"rain.raw",first);
   background();ok(d->BeginScene());lens.lens(target,scratch,scratchSurface,w,h,snow,5.f,.6f,nullptr);ok(d->EndScene());const auto later=read();assert(first!=later);if(samples==0&&bytes==4)writePixels(snow?"snow-later.raw":"rain-later.raw",later);
   std::printf("PASS lens=%s format=%u samples=%u changedPixels=%u state=restored alpha=preserved animation=visible\n",snow?"snow":"rain",unsigned(format),samples,changed);
  }
  IDirect3DSurface9* mask=nullptr;ok(d->CreateDepthStencilSurface(w,h,D3DFMT_D24S8,D3DMULTISAMPLE_TYPE(samples),0,TRUE,&mask,nullptr));background();ok(d->SetDepthStencilSurface(mask));ok(d->Clear(0,nullptr,D3DCLEAR_STENCIL,0,1,0));D3DRECT left{0,0,LONG(w/2),LONG(h)};ok(d->Clear(1,&left,D3DCLEAR_STENCIL,0,1,0x80));ok(d->SetDepthStencilSurface(nullptr));ok(d->BeginScene());lens.lens(target,scratch,scratchSurface,w,h,false,1.f,.6f,mask);ok(d->EndScene());const auto protectedImage=read();unsigned unmaskedChanges=0;for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){size_t at=(size_t(y)*w+x)*bytes;if(x<w/2)assert(!std::memcmp(base.data()+at,protectedImage.data()+at,bytes));else unmaskedChanges+=std::memcmp(base.data()+at,protectedImage.data()+at,3*channel)!=0;}assert(unmaskedChanges>100);drop(mask);std::printf("PASS name stencil format=%u samples=%u preserved left, changed right=%u\n",unsigned(format),samples,unmaskedChanges);
  background(movedInput);const auto movedBase=read();assert(movedBase!=base);ok(d->BeginScene());lens.lens(target,scratch,scratchSurface,w,h,false,1.f,.6f,nullptr);ok(d->EndScene());assert(read()!=movedBase);std::printf("PASS lens on moved scene samples=%u\n",samples);
  if(format==D3DFMT_A8R8G8B8&&samples==0){
   lens.reset();auto& model=NorthlightWeatherLensTestAccess::state(lens);model.primed=true;model.aspect=float(w)/h;model.count=2;
   for(unsigned i=0;i<2;++i){auto& p=model.drops[i];p.x=p.previousX=model.aspect*.5f+(i?-.008f:.008f);p.y=p.previousY=.5f;p.water=.000144f;p.vy=.18f;p.age=1;p.life=100;}
   background();ok(d->BeginScene());lens.lens(target,scratch,scratchSurface,w,h,false,0,1,nullptr,0);ok(d->EndScene());
   assert(model.count==1&&model.merges==1);auto stream=read();writePixels("rivulet.raw",stream);
   unsigned streamPixels=0;for(unsigned y=unsigned(h*.46f);y<unsigned(h*.485f);++y)for(unsigned x=w/2-6;x<w/2+6;++x){const auto offset=(size_t(y)*w+x)*4;streamPixels+=std::memcmp(stream.data()+offset,base.data()+offset,3)!=0;}
   assert(streamPixels>20);
   lens.reset();auto& frozen=NorthlightWeatherLensTestAccess::state(lens);frozen.primed=true;frozen.aspect=float(w)/h;frozen.count=1;
   auto& flake=frozen.drops[0];flake.x=flake.previousX=frozen.aspect*.5f;flake.y=flake.previousY=.5f;flake.snow=.000324f;flake.age=1;flake.life=100;
   background();ok(d->BeginScene());lens.lens(target,scratch,scratchSurface,w,h,true,0,1,nullptr,0,0);ok(d->EndScene());auto cold=read();writePixels("snow-cold.raw",cold);
   for(unsigned f=1;f<=30;++f){background();ok(d->BeginScene());lens.lens(target,scratch,scratchSurface,w,h,true,float(f)*.1f,1,nullptr,0,1);ok(d->EndScene());}
   auto melted=read();writePixels("snow-melted.raw",melted);assert(frozen.count==1&&frozen.drops[0].snow<.0000004f&&frozen.drops[0].water>.000323f&&cold!=melted);
   std::printf("PASS rivulet pixels=%u; fire melts existing snow into a moving water bead, mass conserved\n",streamPixels);
  }
  d->SetTexture(0,nullptr);d->SetRenderTarget(0,nullptr);drop(stage);drop(resolved);drop(target);drop(scratchSurface);drop(scratch);
 }
 }
 input->Release();movedInput->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
