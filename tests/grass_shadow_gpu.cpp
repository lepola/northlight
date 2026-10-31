// Functional production-capture and shadow-mask oracle. No GPU timestamps.
#undef NDEBUG
#include "../src/proxy/renderer.cpp"
#include <cassert>
#include <fstream>
static void must(HRESULT hr){assert(SUCCEEDED(hr));}
static std::vector<DWORD> code(const std::string& path){std::ifstream f(path,std::ios::binary|std::ios::ate);assert(f);auto n=f.tellg();assert(n>0&&size_t(n)%4==0);std::vector<DWORD> v(size_t(n)/4);f.seekg(0);f.read(reinterpret_cast<char*>(v.data()),n);assert(f);return v;}
struct GrassShadowTest {
 static void dense(WorldRenderer& w,IDirect3DDevice9* d,const std::string& prefix,IDirect3DTexture9* texture,IDirect3DVertexDeclaration9* decl,IDirect3DPixelShader9* shadow,IDirect3DPixelShader9* local,IDirect3DSurface9* target,IDirect3DSurface9* staging){
  struct Vertex {float x,y,z,nx,ny,nz;DWORD color;float u,v;};std::vector<Vertex> vertices(1024);std::vector<WORD> indices;
  for(unsigned q=0;q<256;++q)for(unsigned i:{0u,1u,2u,2u,1u,3u})indices.push_back(WORD(4*q+i));
  auto vs=code(prefix+"vs-0.bin"),ps=code(prefix+"ps-0.bin");IDirect3DVertexShader9* native=nullptr;IDirect3DPixelShader9* pixel=nullptr;
  must(d->CreateVertexShader(vs.data(),&native));must(d->CreatePixelShader(ps.data(),&pixel));w.registerShader(native,NorthlightShaderTags::fnv1a(vs.data(),vs.size()*4));w.registerPixelShader(pixel);
  float constants[256][4]={};for(unsigned i=0;i<4;++i)constants[i][i]=1;constants[4][0]=constants[5][1]=constants[6][2]=constants[6][3]=1;constants[9][1]=1;
  std::vector<float> reference;unsigned masks=0;
  for(unsigned order=0;order<4;++order){
   w.replays.clear();w.detailBudget={};w.replaySnapshots.clearFrame();w.snapshotFrameBegun=false;w.captureMode=WorldRenderer::CaptureFresh;w.quality.grassShadows=1;
   must(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));must(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));must(d->SetRenderState(D3DRS_ZENABLE,TRUE));must(d->SetVertexDeclaration(decl));must(d->SetVertexShader(native));must(d->SetPixelShader(pixel));must(d->SetTexture(0,texture));must(d->SetVertexShaderConstantF(0,constants[0],256));
   for(unsigned draw=0;draw<64;++draw){const unsigned id=order==0?draw:order==1?63-draw:order==2?(draw*17)%64:(draw*23+11)%64;const float x=-.875f+.25f*(id%8),y=-.875f+.25f*(id/8);
    for(unsigned i=0;i<1024;++i){const unsigned v=i%4;vertices[i]={x+((v&1)?.1f:-.1f),y+((v&2)?-.1f:.1f),.25f,0,0,1,0xffffffff,float(v&1),float((v>>1)&1)};}
    w.captureUP(D3DPT_TRIANGLELIST,0,1024,512,indices.data(),D3DFMT_INDEX16,vertices.data(),sizeof(Vertex),true,native,false,false);
   }
   assert(w.replays.size()==64&&w.detailBudget.draws==64&&w.detailBudget.vertices==65536&&w.detailBudget.bytes<=NorthlightDetailShadow::Budget::MaxBytes);
   for(auto shader:{shadow,local}){
    must(d->SetRenderTarget(0,target));must(d->SetDepthStencilSurface(nullptr));D3DVIEWPORT9 viewport{0,0,64,64,0,1};must(d->SetViewport(&viewport));must(d->SetRenderState(D3DRS_ZENABLE,FALSE));must(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));must(d->SetPixelShader(shader));must(d->SetTexture(0,texture));float material[]={3,0,0,0},sphere[]={100,0,0,0};must(d->SetPixelShaderConstantF(0,material,1));must(d->SetPixelShaderConstantF(1,sphere,1));must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0));must(d->BeginScene());
    for(const auto& owned:w.replays){const auto& replay=*owned;float c[256][4];std::memcpy(c,replay.constants,sizeof c);for(unsigned i=4;i<8;++i)for(unsigned j=0;j<4;++j)c[i][j]=float(i-4==j);must(d->SetVertexDeclaration(replay.decl));must(d->SetVertexShader(replay.shader));must(d->SetVertexShaderConstantF(0,c[0],256));must(d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,replay.mesh().vertexCount,replay.count,replay.mesh().indices.data(),D3DFMT_INDEX32,replay.mesh().streams[0].bytes.data(),sizeof(Vertex)));}
    must(d->EndScene());must(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};must(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> image(64*64);for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)std::memcpy(&image[y*64+x],static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,4);must(staging->UnlockRect());
    for(unsigned id=0;id<64;++id){const unsigned x=6+8*(id%8),y=59-8*(id/8);assert(image[y*64+x]<.9f&&image[y*64+x-4]==1.f);}if(reference.empty())reference=image;else assert(image==reference);++masks;
   }
  }
  std::printf("GRASS_DENSE_GPU_PASS batches=64 vertices=65536 snapshotBytes=%zu drawOrders=4 directionalLocalMasks=%u exact=1 bounded=1 timings=0\n",w.detailBudget.bytes,masks);native->Release();pixel->Release();
 }
 static void run(IDirect3DDevice9* d,const std::string& prefix){
  WorldRenderer w(d);w.valid=true;w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->bvh=std::make_shared<NorthlightGI::BVH>();
  w.quality.actorShadows=w.quality.actorShadowDetail=w.quality.grassShadows=1;w.effects.shadows=true;w.captureMode=WorldRenderer::CaptureFresh;
  for(unsigned i=0;i<16;++i)w.context.view[i]=w.context.inverseView[i]=(i%5==0);w.projection[0]=w.projection[1]=w.projection[2]=1;
  float constants[256][4]={};for(unsigned i=0;i<4;++i)constants[i][i]=1;
  constants[4][0]=constants[5][1]=constants[6][2]=constants[6][3]=1;constants[9][1]=1;
  must(d->SetVertexShaderConstantF(0,constants[0],256));
  struct Vertex {float x,y,z,nx,ny,nz;DWORD color;float u,v;};
  Vertex vertices[]={{-1,1,.25f,0,0,1,0xffffffff,0,0},{1,1,.25f,0,0,1,0xffffffff,1,0},{-1,-1,.25f,0,0,1,0xffffffff,0,1},{1,-1,.25f,0,0,1,0xffffffff,1,1}};
  const WORD indices[]={0,1,2,2,1,3};D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},{0,28,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
  IDirect3DVertexDeclaration9* decl=nullptr;must(d->CreateVertexDeclaration(elements,&decl));must(d->SetVertexDeclaration(decl));
  IDirect3DTexture9* texture=nullptr;must(d->CreateTexture(16,16,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr));
  D3DLOCKED_RECT lock{};must(texture->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){DWORD p=x<8?0x00ffffff:0xffffffff;std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4,&p,4);}must(texture->UnlockRect(0));must(d->SetTexture(0,texture));
  for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})must(d->SetSamplerState(0,state,D3DTADDRESS_CLAMP));
  for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})must(d->SetSamplerState(0,state,D3DTEXF_POINT));
  must(d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
  for(auto state:{D3DRS_FOGENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_SRGBWRITEENABLE})must(d->SetRenderState(state,FALSE));
  must(d->SetRenderState(D3DRS_ZENABLE,TRUE));must(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));must(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));must(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
  must(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));must(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));must(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));must(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
  IDirect3DPixelShader9 *shadow=nullptr,*local=nullptr;must(d->CreatePixelShader(kShadowReplayPSShader,&shadow));must(d->CreatePixelShader(kLocalShadowPSShader,&local));
  IDirect3DSurface9 *target=nullptr,*staging=nullptr;must(d->CreateRenderTarget(64,64,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));must(d->CreateOffscreenPlainSurface(64,64,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));must(d->SetRenderTarget(0,target));must(d->SetDepthStencilSurface(nullptr));D3DVIEWPORT9 viewport{0,0,64,64,0,1};must(d->SetViewport(&viewport));
  unsigned total=0;
  for(unsigned variant=0;variant<16;++variant){auto vs=code(prefix+"vs-"+std::to_string(variant%12)+".bin"),ps=code(prefix+"ps-"+std::to_string(variant%16)+".bin");
   IDirect3DVertexShader9* native=nullptr;IDirect3DPixelShader9* pixel=nullptr;must(d->CreateVertexShader(vs.data(),&native));must(d->CreatePixelShader(ps.data(),&pixel));w.registerShader(native,NorthlightShaderTags::fnv1a(vs.data(),vs.size()*4));w.registerPixelShader(pixel);
   const auto lookup=w.lookupShader(native);assert(lookup.capture&&lookup.capture->detailDoodad);
   auto capture=[&]{must(d->SetVertexShader(native));must(d->SetPixelShader(pixel));must(d->SetVertexShaderConstantF(0,constants[0],256));w.captureUP(D3DPT_TRIANGLELIST,0,4,2,indices,D3DFMT_INDEX16,vertices,sizeof(Vertex),true,native,false,false);};
   capture();assert(w.replays.size()==variant+1&&w.detailBudget.draws==variant+1);const auto& p=*w.replays.back();assert(p.alphaMode==3&&p.cutoff==0&&p.texture==texture&&p.mesh().vertexCount==4);
   DWORD blend=0;must(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend));assert(blend==TRUE);IDirect3DVertexShader9* after=nullptr;must(d->GetVertexShader(&after));assert(after==native);after->Release();
   for(auto shader:{shadow,local})for(unsigned mode:{0u,1u,2u,3u})for(float fade:{0.f,.5f,1.f}){
    float replay[256][4];std::memcpy(replay,constants,sizeof replay);for(unsigned i=4;i<8;++i)for(unsigned j=0;j<4;++j)replay[i][j]=float(i-4==j);replay[9][1]=fade;
    must(d->SetVertexShader(p.shader));must(d->SetVertexShaderConstantF(0,replay[0],256));must(d->SetPixelShader(shader));float material[]={float(mode),0,0,mode==3?0.f:.5f};must(d->SetPixelShaderConstantF(0,material,1));float sphere[]={100,0,0,0};must(d->SetPixelShaderConstantF(1,sphere,1));
    must(d->SetRenderState(D3DRS_ZENABLE,FALSE));must(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));must(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0));must(d->BeginScene());must(d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,4,2,p.mesh().indices.data(),D3DFMT_INDEX32,p.mesh().streams[0].bytes.data(),sizeof(Vertex)));must(d->EndScene());
    must(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT result{};must(staging->LockRect(&result,nullptr,D3DLOCK_READONLY));unsigned transparent=0,covered=0;for(unsigned y=1;y<63;++y)for(unsigned x=1;x<63;++x){float value;std::memcpy(&value,static_cast<char*>(result.pBits)+y*result.Pitch+x*16,4);if(x<30)transparent+=value<.9f;if(x>34)covered+=value<.9f;}must(staging->UnlockRect());if(mode==3){assert(transparent==0);if(fade==0)assert(covered==0);else if(fade==1)assert(covered>1500);else assert(covered>600&&covered<1100);}
    else{const bool opaque=mode==0||fade>=.5f;assert(covered==(opaque?1736u:0u));assert(transparent==((mode==2&&opaque)?1798u:0u));}++total;
   }
   must(d->SetRenderState(D3DRS_ZENABLE,TRUE));must(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));must(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));capture();assert(w.replays.size()==variant+1);must(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
   native->Release();pixel->Release();
  }
  assert(w.detailBudget.draws==16&&w.detailBlendRejected==16);
  IDirect3DVertexShader9* native=nullptr;must(d->GetVertexShader(&native));assert(native);
  must(d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE));must(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER));must(d->SetRenderState(D3DRS_ALPHAREF,127));
  auto captureLast=[&]{w.captureUP(D3DPT_TRIANGLELIST,0,4,2,indices,D3DFMT_INDEX16,vertices,sizeof(Vertex),true,native,false,false);};
  captureLast();assert(w.replays.size()==17&&w.replays.back()->alphaMode==1&&w.replays.back()->cutoff==.5f);
  must(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));captureLast();assert(w.replays.size()==18&&w.replays.back()->alphaMode==1);
  must(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));captureLast();assert(w.replays.size()==18&&w.detailMaskRejected==1);
  must(d->SetPixelShader(shadow));captureLast();assert(w.replays.size()==18&&w.detailMaskRejected==2);
  w.quality.grassShadows=0;captureLast();assert(w.replays.size()==18&&w.detailBudgetRejected==1);native->Release();
  std::printf("GRASS_CAPTURE_GPU_PASS nativeVertexVariants=12 nativePixelVariants=16 masks=%u additiveRejected=16 transparentSafe=1 fade=0,.5,1 legacyModes=0,1,2 cutoutCaptured=2 unknownRejected=1 disabledRejected=1 captureStateUnchanged=1 timings=0\n",total);
  dense(w,d,prefix,texture,decl,shadow,local,target,staging);
  must(d->SetTexture(0,nullptr));must(d->SetVertexShader(nullptr));must(d->SetPixelShader(nullptr));must(d->SetVertexDeclaration(nullptr));texture->Release();decl->Release();shadow->Release();local->Release();target->Release();staging->Release();
 }
};
int main(int argc,char** argv){assert(argc==4);SetEnvironmentVariableA("NORTHLIGHT_QUALITY_FILE",argv[2]);auto path=std::wstring(argv[2],argv[2]+std::strlen(argv[2]));assert(GetFullPathNameW(path.c_str(),MAX_PATH,rootPath,nullptr));WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"NorthlightGrassOracle";assert(RegisterClassW(&wc));auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(hwnd);auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED,&pp,&d));GrassShadowTest::run(d,argv[3]);d->Release();api->Release();FreeLibrary(module);DestroyWindow(hwnd);}
