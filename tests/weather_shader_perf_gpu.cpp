// Isolated GPU timestamp comparison of frozen and specialized precipitation shaders.
#define NORTHLIGHT_SURFACE_HELPERS_ONLY
#include "surface_effects_gpu.cpp"
#undef NORTHLIGHT_SURFACE_HELPERS_ONLY
int main(int argc,char** argv){
 assert(argc==4);const unsigned requested=unsigned(std::strtoul(argv[2],nullptr,10));assert(requested==0||requested==4);const bool half=std::strtoul(argv[3],nullptr,10)!=0;requireNoGame();WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightWeatherTiming";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
 HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 IDirect3DPixelShader9* candidate[4]{};const DWORD* binaries[]={kRainQualityShader,kSnowQualityShader,kRainPerformanceShader,kSnowPerformanceShader};for(unsigned i=0;i<4;++i)ok(d->CreatePixelShader(binaries[i],&candidate[i]));
 IDirect3DPixelShader9* baseline[2]{};ok(d->CreatePixelShader(kPrecipitationQualityShader,&baseline[0]));ok(d->CreatePixelShader(kPrecipitationPerformanceShader,&baseline[1]));
 const unsigned W=half?960:1920,H=half?540:1080;Image depth{W,H,D3DFMT_R32F,4,std::vector<unsigned char>(size_t(W)*H*4)};const float raw=1;for(unsigned i=0;i<W*H;++i)std::memcpy(depth.pixels.data()+i*4,&raw,4);auto input=texture(d,depth);
 ok(d->SetDepthStencilSurface(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetTexture(3,input));
 for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(3,state,D3DTADDRESS_CLAMP));for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(3,state,D3DTEXF_POINT));ok(d->SetSamplerState(3,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
 for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(state,0));
 ok(d->SetRenderState(D3DRS_MULTISAMPLEANTIALIAS,TRUE));ok(d->SetRenderState(D3DRS_MULTISAMPLEMASK,0xffffffff));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,7));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));ok(d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE));
 float c[10][4]{};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=.1f;c[0][3]=1000;c[1][0]=1.3f;c[1][1]=1.7f;c[1][2]=-1;c[1][3]=.31f;c[2][1]=1;c[4][1]=1;c[5][2]=1;c[6][0]=-1;c[7][2]=3;c[7][3]=1;c[8][0]=16;c[8][1]=2;c[8][2]=9;
 const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
 for(unsigned samples:{4u,0u}){
  if(samples!=requested)continue;
  requireNoGame();IDirect3DSurface9* target=nullptr;const HRESULT hr=d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&target,nullptr);if(FAILED(hr)){std::printf("UNSUPPORTED samples=%u hr=%08lx\n",samples,(unsigned long)hr);continue;}
  D3DSURFACE_DESC desc{};ok(target->GetDesc(&desc));assert(unsigned(desc.MultiSampleType)==samples);
  ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x00143250,1,0));
  for(unsigned entry=0;entry<2;++entry)for(unsigned snow=0;snow<2;++snow)for(unsigned strength=0;strength<2;++strength){
   c[3][0]=strength?1.f:.35f;c[3][1]=float(snow);ok(d->SetPixelShaderConstantF(0,c[0],10));
   for(unsigned block=0;block<10;++block)for(unsigned order=0;order<2;++order){requireNoGame();const unsigned variant=(block+order)&1;ok(d->SetPixelShader(variant?candidate[entry*2+snow]:baseline[entry]));
    IDirect3DQuery9 *event=nullptr,*start=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;
    ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
    ok(disjoint->Issue(D3DISSUE_BEGIN));ok(start->Issue(D3DISSUE_END));ok(d->BeginScene());for(unsigned i=0;i<4;++i)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(end->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);
    UINT64 a=0,b=0,f=0;BOOL bad=FALSE;assert(start->GetData(&a,sizeof a,0)==S_OK&&end->GetData(&b,sizeof b,0)==S_OK&&frequency->GetData(&f,sizeof f,0)==S_OK&&disjoint->GetData(&bad,sizeof bad,0)==S_OK&&!bad&&b>=a&&f);
    if(block>=2)std::printf("TIMING samples=%u entry=%u snow=%u strength=%u block=%u variant=%s ms=%.8f\n",samples,entry,snow,strength,block,variant?"candidate":"baseline",double(b-a)*1000./double(f)/4.);
    for(auto* q:{event,start,end,frequency,disjoint})q->Release();
   }
  }
  target->Release();
 }
 d->SetTexture(3,nullptr);input->Release();for(auto* s:candidate)s->Release();for(auto* s:baseline)s->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
