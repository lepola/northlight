// Manual hidden DXVK replay. Inputs: capture prefix, old/new shader binaries, pass name, backend DLL.

// Hidden standalone DXVK replay of fog and AO; reuses the existing image/device helpers.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main northlight_removal_replay_unused
#include "removal_distance_gpu.cpp"
#undef main
#pragma clang diagnostic pop
int main(int argc,char** argv){
 assert(argc==6||argc==7);const std::string prefix=argv[1],mode=argv[4];
 // Optional fog-only cases use exact image comparison; the existing invocation is unchanged.
 const int scenario=argc==7?std::atoi(argv[6]):-1;assert(scenario>=-1&&scenario<=12);
 const bool ao=mode=="AO"||mode=="AOContactBloom",blur=mode=="AOBlur",cloud=mode=="FogClouds";
 auto bytes=read(prefix+"constants.f32");assert(bytes.size()==68*16);float c[68][4];std::memcpy(c,bytes.data(),sizeof c);
 auto scene=image(prefix+"scene.fgr");const unsigned fw=scene.width,fh=scene.height,W=fw/2,H=fh/2;
 Image depth{fw,fh,D3DFMT_R32F,4,std::vector<unsigned char>(std::size_t(fw)*fh*4)};
 if(std::ifstream(prefix+"depth.fgr").good())depth=image(prefix+"depth.fgr");
 else{auto dist=image(prefix+"distance.fgr");for(unsigned y=0;y<fh;++y)for(unsigned x=0;x<fw;++x){float z;size_t k=size_t(std::min(y*dist.height/fh,dist.height-1))*dist.width+std::min(x*dist.width/fw,dist.width-1);std::memcpy(&z,dist.pixels.data()+4*k,4);float d=c[0][3]/(c[0][3]-c[0][2])*(1-c[0][2]/std::max(z,c[0][2]));d=d/c[2][0]+c[1][3];std::memcpy(depth.pixels.data()+4*(size_t(y)*fw+x),&d,4);}}
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightScheduleReplay";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
 HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
 HMODULE module=LoadLibraryA(argv[5]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
 IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));std::vector<IDirect3DTexture9*> owned;
 auto bind=[&](unsigned slot,const Image& im,DWORD filter=D3DTEXF_POINT){auto* t=texture(d,im);owned.push_back(t);ok(d->SetTexture(slot,t));for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(slot,state,filter));ok(d->SetSamplerState(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(slot,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(slot,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));};
 if(scenario>=7)for(unsigned y=0;y<fh;++y)for(unsigned x=0;x<fw;++x){float sky=c[1][3]+1/c[2][0];std::memcpy(depth.pixels.data()+4*(size_t(y)*fw+x),&sky,4);}
 bind(0,scene,D3DTEXF_LINEAR);bind(1,depth);Image zero{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};
 Image water=std::ifstream(prefix+"water.fgr").good()?image(prefix+"water.fgr"):zero;bind(3,water);bind(11,water);
 if(ao||blur){
  float minZ=c[1][3],inv=c[2][0],px=c[1][0],py=c[1][1];c[1][0]=px;c[1][1]=py;c[1][2]=.7f;c[1][3]=0;c[2][0]=.2f;c[2][1]=6;c[2][2]=c[2][3]=0;
  c[3][0]=minZ;c[3][1]=inv;c[3][2]=minZ+1/inv;c[3][3]=0;c[4][0]=std::ifstream(prefix+"water.fgr").good()?1.f:0.f;
  if(blur){Image raw{W,H,D3DFMT_A8R8G8B8,4,std::vector<unsigned char>(size_t(W)*H*4)};for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){auto k=4*(size_t(y)*W+x);raw.pixels[k]=17;raw.pixels[k+1]=21;raw.pixels[k+2]=23;raw.pixels[k+3]=(x/19+y/13)%4?255:static_cast<unsigned char>(40+(x*11+y*7)%170);}if(std::ifstream(prefix+"ao.fgr").good())raw=image(prefix+"ao.fgr");bind(0,raw,D3DTEXF_LINEAR);bind(2,raw);}
 }else{
  Image field{64,64,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(64*64*16)};
  for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){float v[4]={0,.012f,.004f,(x/7+y/9)%5?5.f:0.f};if(scenario==10)v[0]=float(int((x*11+y*7)%29)-14);std::memcpy(field.pixels.data()+16*(y*64+x),v,16);}if(scenario==3)std::fill(field.pixels.begin(),field.pixels.end(),0);bind(13,field);
  Image shadow{64,64,D3DFMT_R32F,4,std::vector<unsigned char>(64*64*4)};for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){float v=(x/3+y/5)%2?.72f:.21f;std::memcpy(shadow.pixels.data()+4*(y*64+x),&v,4);}bind(3,shadow);
  float visible=.65f;Image source{1,1,D3DFMT_R32F,4,std::vector<unsigned char>(4)};std::memcpy(source.pixels.data(),&visible,4);bind(15,source);
  for(unsigned i=3;i<7;++i){std::fill(c[i],c[i]+4,0);c[i][i-3]=1;}
  c[15][0]=c[15][1]=0;c[15][2]=4;c[16][0]=.57735026f;c[16][1]=.57735026f;c[16][2]=.57735026f;
  for(unsigned i=11;i<15;++i)std::fill(c[i],c[i]+4,0);c[11][0]=c[12][1]=c[13][2]=1.f/192;c[14][2]=.2f;c[14][3]=1;
  c[2][2]=192;c[2][3]=1.f/64;c[17][0]=.6f;c[17][1]=.8f;c[17][2]=.9f;c[18][0]=.2f;c[18][1]=.3f;c[18][2]=.4f;
  c[21][0]=c[21][1]=1;c[21][2]=.4f;c[21][3]=128;c[22][0]=c[22][1]=c[22][2]=1;c[22][3]=.008f;
  c[27][0]=1;c[31][0]=c[31][1]=-128;c[31][2]=1.f/256;c[31][3]=.5f;c[32][0]=.01f;c[32][1]=.004f;c[32][2]=64;c[32][3]=32;
  c[24][0]=.001f;c[58][0]=20;c[58][1]=1.f/15;c[59][3]=.0017f;c[64][2]=0;
  c[59][1]=-30;c[60][0]=0;c[61][0]=.25f;c[62][0]=66;c[62][1]=-3.2f;c[62][2]=96;c[62][3]=.045f;c[63][0]=2.8f;c[63][1]=1.f/48;c[63][2]=1.f/12;c[63][3]=5.2f;
 }
 if(mode=="FogBlur"||mode=="FogBlurV"){
   bind(9,image(prefix+"fog.fgr"),D3DTEXF_LINEAR);
   c[34][0]=mode=="FogBlur"?2.f:0.f;c[34][1]=mode=="FogBlurV"?2.f:0.f;
 }
 if(scenario==1)c[17][0]=c[17][1]=c[17][2]=0;
 if(scenario==2)c[21][1]=0;
 if(scenario==4)c[15][2]=-50;
 if(scenario==5)c[15][0]=c[15][1]=10000;
 if(scenario==6)c[58][0]=96;
 if(scenario>=7){std::fill(c[4],c[4]+4,0);std::fill(c[5],c[5]+4,0);c[4][2]=1;c[5][1]=1;c[15][2]=2;c[58][0]=3.5f;c[61][0]=-.5f;c[62][1]=1;}
 if(scenario==8)c[17][0]=c[17][1]=c[17][2]=0;
 if(scenario==9)c[21][1]=0;
 if(scenario==11){c[60][0]=128.f*(1.f/12-1.f/40);c[64][2]=128.f*(1.f/12-1.f/48);}
 if(scenario==12){c[30][0]=1;const float value[4]={32,.25f,0,0};Image liquid{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};std::memcpy(liquid.pixels.data(),value,16);bind(11,liquid);}
 IDirect3DVolumeTexture9* noise=nullptr;if(cloud){ok(d->CreateVolumeTexture(32,32,32,1,0,D3DFMT_L8,D3DPOOL_MANAGED,&noise,nullptr));D3DLOCKED_BOX box{};ok(noise->LockBox(0,&box,nullptr,0));uint32_t seed=77;for(unsigned z=0;z<32;++z)for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){seed=seed*1664525u+1013904223u;static_cast<unsigned char*>(box.pBits)[z*box.SlicePitch+y*box.RowPitch+x]=static_cast<unsigned char>(seed>>24);}ok(noise->UnlockBox(0));ok(d->SetTexture(14,noise));for(auto st:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(14,st,D3DTEXF_LINEAR));for(auto st:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW})ok(d->SetSamplerState(14,st,D3DTADDRESS_WRAP));ok(d->SetSamplerState(14,D3DSAMP_MIPFILTER,D3DTEXF_NONE));}
 IDirect3DPixelShader9* ps[2]{};for(unsigned i=0;i<2;++i){auto v=read(argv[2+i]);ok(d->CreatePixelShader((const DWORD*)v.data(),&ps[i]));}
 IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetPixelShaderConstantF(0,c[0],68));D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));
 const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{float(W)-.5f,-.5f,0,1,1,0},{-.5f,float(H)-.5f,0,1,0,1},{float(W)-.5f,float(H)-.5f,0,1,1,1}};
 IDirect3DQuery9 *event=nullptr,*start=nullptr,*end=nullptr,*freq=nullptr,*disjoint=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&freq));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
 auto draw=[&](unsigned n){ok(d->BeginScene());for(unsigned i=0;i<n;++i)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());};
 std::vector<unsigned char> output[2];std::vector<double> times[2];
 for(unsigned block=0;block<(scenario<0?16u:20u);++block){unsigned modeIndex=((block+1)/2)%2;ok(d->SetPixelShader(ps[modeIndex]));draw(64);wait(event);ok(disjoint->Issue(D3DISSUE_BEGIN));ok(start->Issue(D3DISSUE_END));draw(64);ok(end->Issue(D3DISSUE_END));ok(freq->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);UINT64 a=0,b=0,f=0;BOOL bad=FALSE;assert(start->GetData(&a,sizeof a,0)==S_OK&&end->GetData(&b,sizeof b,0)==S_OK&&freq->GetData(&f,sizeof f,0)==S_OK&&disjoint->GetData(&bad,sizeof bad,0)==S_OK&&!bad&&b>=a&&f);times[modeIndex].push_back(double(b-a)/double(f)*1000/64);
  if(block<2){ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));output[modeIndex].resize(size_t(W)*H*8);for(unsigned y=0;y<H;++y)std::memcpy(output[modeIndex].data()+size_t(y)*W*8,(char*)lock.pBits+y*lock.Pitch,W*8);ok(staging->UnlockRect());}}
 size_t changed=0,alpha=0,nonfinite=0;double maximum=0;for(size_t i=0;i<size_t(W)*H*4;++i){uint16_t a,b;std::memcpy(&a,output[0].data()+2*i,2);std::memcpy(&b,output[1].data()+2*i,2);double e=std::fabs(double(half(a))-half(b));maximum=std::max(maximum,e);nonfinite+=!std::isfinite(e);changed+=a!=b;alpha+=i%4==3&&a!=b;}
 for(auto& t:times)std::sort(t.begin(),t.end());const size_t middle=times[0].size()/2;std::printf("{\"shader\":\"%s\",\"old_ms\":%.6f,\"new_ms\":%.6f,\"ratio\":%.4f,\"components\":%llu,\"changed\":%llu,\"alpha_changes\":%llu,\"nonfinite\":%llu,\"max_abs\":%.9g}\n",mode.c_str(),times[0][middle],times[1][middle],times[1][middle]/times[0][middle],(unsigned long long)size_t(W)*H*4,(unsigned long long)changed,(unsigned long long)alpha,(unsigned long long)nonfinite,maximum);std::fflush(stdout);assert(!alpha&&!nonfinite&&maximum<=1.0/1024);
 if(scenario>=0)assert(!changed&&maximum==0);
 d->SetPixelShader(nullptr);for(unsigned i=0;i<16;++i)d->SetTexture(i,nullptr);for(auto* t:owned)t->Release();if(noise)noise->Release();for(auto* p:ps)p->Release();for(auto* q:{event,start,end,freq,disjoint})q->Release();target->Release();staging->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);return 0;
}
