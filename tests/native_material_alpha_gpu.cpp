// Grazing-angle material demonstration using the production cache, capture and final shader.
#define main distance_oracle_main
#include "composite_distance_gpu.cpp"
#undef main
#include <cstdarg>
#include <unordered_map>
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char* f,...){va_list a;va_start(a,f);std::vprintf(f,a);std::puts("");va_end(a);}
#include "saved_state.h"
#include "auto_bump_renderer.h"
#include "world_compiled_shaders.h"
#include "patch_terrain_shadow_coverage.h"
#ifndef NORTHLIGHT_OBLIQUE_WIDTH
#define NORTHLIGHT_OBLIQUE_WIDTH 1280
#define NORTHLIGHT_OBLIQUE_HEIGHT 800
#endif
static constexpr UINT W=NORTHLIGHT_OBLIQUE_WIDTH,H=NORTHLIGHT_OBLIQUE_HEIGHT;
static IDirect3DTexture9* solid(IDirect3DDevice9* d,float x,float y,float z,float a){Image i{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};float data[]={x,y,z,a};std::memcpy(i.pixels.data(),data,16);return texture(d,i);}
static void save(IDirect3DDevice9* d,IDirect3DSurface9* target,const std::string& path){
    D3DSURFACE_DESC desc{};target->GetDesc(&desc);IDirect3DSurface9* stage=nullptr;ok(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&stage,nullptr));ok(d->GetRenderTargetData(target,stage));
    const UINT bytes=desc.Format==D3DFMT_A32B32G32R32F?16:desc.Format==D3DFMT_A16B16G16R16F?8:4;std::ofstream f(path,std::ios::binary);UINT header[]={0x31524746,desc.Width,desc.Height,UINT(desc.Format),bytes};f.write((char*)header,20);D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));for(UINT y=0;y<desc.Height;++y)f.write((char*)lock.pBits+y*lock.Pitch,desc.Width*bytes);stage->UnlockRect();drop(stage);
}

static std::vector<float> alphaPixels(IDirect3DDevice9*d,IDirect3DSurface9*source){IDirect3DSurface9*stage=nullptr;ok(d->CreateOffscreenPlainSurface(32,32,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&stage,nullptr));ok(d->GetRenderTargetData(source,stage));D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float>a(1024);for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)std::memcpy(&a[y*32+x],(char*)lock.pBits+y*lock.Pitch+x*16+12,4);stage->UnlockRect();drop(stage);return a;}
static void sliceChecks(){
 using namespace NorthlightWaterShaderPatch;NorthlightTerrainCoverage::Program p;std::vector<Word> result;
 std::vector<Word> code={0xffff0300,0x0200001f,0x80000005,dst(1,0),0x02000001,dst(0,0),src(1,0),0x01000041,dst(0,0),0x02000001,dst(8,0),src(2,0),65535};
 assert(NorthlightTerrainCoverage::analyse(code.data(),code.size(),p));assert(NorthlightNativeMaterialCapture::alphaSlice(code.data(),p,false,result));
 NorthlightTerrainCoverage::Program sliced;assert(NorthlightTerrainCoverage::analyse(result.data(),result.size(),sliced)&&sliced.slots==2); // Keep the kill and all its initialized lanes.
 assert(result[5]==dst(0,0));code.insert(code.end()-1,{0x01000028,src(2,0)});p={};assert(NorthlightTerrainCoverage::analyse(code.data(),code.size(),p));assert(!NorthlightNativeMaterialCapture::alphaSlice(code.data(),p,true,result));
 std::puts("NATIVE_SLICE_PASS kill_dependencies unknown_flow_fallback");
}
int main(int argc,char**argv){std::setvbuf(stdout,nullptr,_IONBF,0);assert(argc>2);auto module=LoadLibraryA(argv[1]);assert(module);auto api=((IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9"))(D3D_SDK_VERSION);HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;IDirect3DDevice9*d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 IDirect3DSurface9*target=nullptr;ok(d->CreateRenderTarget(32,32,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));d->SetRenderTarget(0,target);d->SetDepthStencilSurface(nullptr);D3DVIEWPORT9 vp{0,0,32,32,0,1};d->SetViewport(&vp);for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_LIGHTING})d->SetRenderState(state,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
 Image source{32,32,D3DFMT_A8R8G8B8,4,std::vector<unsigned char>(4096)};for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){auto*p=source.pixels.data()+(y*32+x)*4;p[0]=64;p[1]=128;p[2]=200;p[3]=x<8?0:x<16?64:x<24?128:255;}
 auto*diffuse=texture(d,source);auto*normal=solid(d,128.f/255,128.f/255,.5f,1);auto*white=solid(d,1,1,1,1);for(unsigned i=0;i<16;++i){d->SetTexture(i,i==14?normal:i<2?diffuse:white);for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})d->SetSamplerState(i,key,D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);}
 struct V{float x,y,z,w;DWORD color;float uv[7][2],clip[4];};V q[4]={};for(unsigned i=0;i<4;++i){q[i].x=(i&1)?31.5f:-.5f;q[i].y=(i&2)?31.5f:-.5f;q[i].w=1;q[i].color=0x80ffffff;for(unsigned j=0;j<7;++j){q[i].uv[j][0]=(i&1)?1:0;q[i].uv[j][1]=(i&2)?1:0;}q[i].clip[0]=(i&1)?1:-1;q[i].clip[1]=(i&2)?-1:1;q[i].clip[3]=2;}
 sliceChecks();d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX8|D3DFVF_TEXCOORDSIZE4(7));float constants[224][4]={};d->SetPixelShaderConstantF(0,constants[0],224);std::ifstream manifest(std::string(argv[2])+"/files.txt");std::string file;unsigned tested=0,rejected=0,opaqueTested=0,nativeSlots=0,alphaSlots=0,opaqueSlots=0,sliceFallback=0;float worst=0;
 for(unsigned pattern=0;pattern<2;++pattern){manifest.clear();manifest.seekg(0);
 for(unsigned i=0;i<224;++i)for(unsigned c=0;c<4;++c)constants[i][c]=pattern?.125f*(1+(i+3*c)%8):0;
 while(std::getline(manifest,file)){d->SetPixelShaderConstantF(0,constants[0],224);auto code=read(std::string(argv[2])+"/"+file);IDirect3DPixelShader9*native=nullptr,*capture=nullptr;ok(d->CreatePixelShader((DWORD*)code.data(),&native));std::vector<uint32_t> patched;unsigned control;
  if(!NorthlightNativeMaterialCapture::patch((uint32_t*)code.data(),code.size()/4,patched,control)){++rejected;drop(native);continue;}HRESULT created=d->CreatePixelShader((DWORD*)patched.data(),&capture);if(FAILED(created)){std::fprintf(stderr,"Rejected patched %s: %08lx\n",file.c_str(),(unsigned long)created);std::abort();}
  float settings[5][4]={{.06f,48,.001f,1},{1,1,-1,0},{0,16,16,0},{0,8,0,0},{0,0,1,1}};d->SetPixelShaderConstantF(control,settings[0],5);
  auto draw=[&](IDirect3DPixelShader9*ps){d->SetPixelShader(ps);ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(V)));ok(d->EndScene());return alphaPixels(d,target);};auto a=draw(native),b=draw(capture);
  for(unsigned i=0;i<a.size();++i){assert(std::isfinite(a[i])&&std::isfinite(b[i]));float expected=std::clamp(a[i],0.f,1.f);if(expected<.001f)expected=0;worst=std::max(worst,std::fabs(expected-b[i]));if(std::fabs(expected-b[i])>.0001f){std::fprintf(stderr,"Alpha mismatch %s %u %g %g\n",file.c_str(),i,expected,b[i]);std::abort();}}
  NorthlightTerrainCoverage::Program original;assert(NorthlightTerrainCoverage::analyse(reinterpret_cast<const uint32_t*>(code.data()),code.size()/4,original));nativeSlots+=original.slots;
  for(bool alpha:{true,false}){std::vector<uint32_t> sliced;NorthlightTerrainCoverage::Program p;
   if(NorthlightNativeMaterialCapture::alphaSlice(reinterpret_cast<const uint32_t*>(code.data()),original,alpha,sliced)){assert(NorthlightTerrainCoverage::analyse(sliced.data(),sliced.size(),p));(alpha?alphaSlots:opaqueSlots)+=p.slots;}else{++sliceFallback;(alpha?alphaSlots:opaqueSlots)+=original.slots;}}
  // Force only the original output alpha to one; its native texkill still
  // decides coverage. This distinguishes a killed fragment from alpha zero.
  std::vector<float> coverage(a.size(),1.f);bool killed=false;for(const auto& op:original.ops)killed|=op.code==65;
  if(killed){using namespace NorthlightWaterShaderPatch;unsigned one=223;while(original.constants[one]){assert(one);--one;}
   std::vector<uint32_t> probe(reinterpret_cast<const uint32_t*>(code.data()),reinterpret_cast<const uint32_t*>(code.data())+code.size()/4-1);
   probe.insert(probe.begin()+1,{0x05000051,dst(2,one),0x3f800000,0x3f800000,0x3f800000,0x3f800000});probe.insert(probe.end(),{0x02000001,dst(8,0,8),src(2,one,0),65535});
   IDirect3DPixelShader9* mask=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(probe.data()),&mask));coverage=draw(mask);drop(mask);}
  drop(capture);assert(NorthlightNativeMaterialCapture::patch(reinterpret_cast<const uint32_t*>(code.data()),code.size()/4,patched,control,false));ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&capture));settings[0][3]=0;d->SetPixelShaderConstantF(control,settings[0],5);
  auto opaque=draw(capture);for(unsigned i=0;i<opaque.size();++i)assert(std::isfinite(opaque[i])&&std::fabs(opaque[i]-coverage[i])<.0001f);++opaqueTested;
  ++tested;drop(native);drop(capture);
 }
 }
 assert(tested>=64);std::printf("NATIVE_MATERIAL_ALPHA_PASS variants=%u preservedFallback=%u worstAlphaError=%g opaqueVariants=%u nativeSlots=%u alphaSlots=%u opaqueSlots=%u sliceFallback=%u\n",tested,rejected,worst,opaqueTested,nativeSlots,alphaSlots,opaqueSlots,sliceFallback);return 0;
}
