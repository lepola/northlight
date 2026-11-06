// Paired shader replay: recorded receiver buffers, fixed camera and lighting.
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "world_compiled_shaders.h"
static void must(HRESULT h){assert(SUCCEEDED(h));}
static std::vector<unsigned char> read(const std::string& p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
static IDirect3DTexture9* texture(IDirect3DDevice9* d,const std::string& p){auto b=read(p);unsigned h[5];std::memcpy(h,b.data(),20);assert(h[0]==0x31524746&&b.size()==20+size_t(h[1])*h[2]*h[4]);IDirect3DTexture9* t=nullptr;must(d->CreateTexture(h[1],h[2],1,0,D3DFORMAT(h[3]),D3DPOOL_MANAGED,&t,nullptr));D3DLOCKED_RECT l;must(t->LockRect(0,&l,nullptr,0));for(unsigned y=0;y<h[2];++y)std::memcpy((char*)l.pBits+y*l.Pitch,b.data()+20+size_t(y)*h[1]*h[4],h[1]*h[4]);must(t->UnlockRect(0));return t;}
int main(int argc,char** argv){assert(argc==9||argc==10);const std::string prefix=argv[2],out=argv[3],group="lighting-input-g"+std::string(argv[7])+"-s";const bool material=std::string(argv[4])=="material",hdr=std::atoi(argv[8])!=0;
 auto dimensions=read(prefix+group+"0.fgr");unsigned imageHeader[5];std::memcpy(imageHeader,dimensions.data(),20);const unsigned width=imageHeader[1],height=imageHeader[2];
 WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"LampComposite";assert(RegisterClassW(&wc));auto hwnd=CreateWindowW(wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");auto api=create(D3D_SDK_VERSION);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=hwnd;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
 IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 auto bank=read(prefix+"lighting-pass-"+argv[5]+"-constants.f32");assert(bank.size()==116*16);float constants[224][4]{};std::memcpy(constants,bank.data(),bank.size());
 const auto exists=[](const std::string& p){return bool(std::ifstream(p,std::ios::binary));};
 // Older journals did not export the final effect/rain masks. A null D3D9
 // sampler reads white, which would protect every pixel and erase the entire
 // paired lighting delta. Explicitly limit those replays to unmasked receivers.
 const bool effectMask=exists(prefix+group+"7.fgr"),rainMask=exists(prefix+group+"13.fgr"),waterMask=exists(prefix+"lighting-input-g0-s11.fgr");
 if(!effectMask)constants[79][0]=0;
 if(!waterMask)constants[30][0]=0;
 must(d->SetPixelShaderConstantF(0,constants[0],224));
 std::vector<IDirect3DTexture9*> owned;auto bind=[&](unsigned slot,const std::string& path,bool linear=false){auto* t=texture(d,path);owned.push_back(t);must(d->SetTexture(slot,t));for(auto s:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})must(d->SetSamplerState(slot,s,linear?D3DTEXF_LINEAR:D3DTEXF_POINT));must(d->SetSamplerState(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE));must(d->SetSamplerState(slot,D3DSAMP_SRGBTEXTURE,FALSE));must(d->SetSamplerState(slot,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));must(d->SetSamplerState(slot,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));};
 bind(1,prefix+"depth.fgr");bind(8,prefix+group+"8.fgr");bind(10,prefix+group+"10.fgr",true);bind(12,prefix+"lighting-input-g0-s12.fgr",true);bind(14,prefix+"normals.fgr");bind(15,prefix+"distance.fgr");
 IDirect3DTexture9* zero=nullptr;must(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&zero,nullptr));D3DLOCKED_RECT z;must(zero->LockRect(0,&z,nullptr,0));*(DWORD*)z.pBits=0;must(zero->UnlockRect(0));owned.push_back(zero);
 if(effectMask)bind(7,prefix+group+"7.fgr");else must(d->SetTexture(7,zero));
 if(waterMask)bind(11,prefix+"lighting-input-g0-s11.fgr");else must(d->SetTexture(11,zero));
 if(rainMask)bind(13,prefix+group+"13.fgr");else must(d->SetTexture(13,zero));
 std::printf("CAPTURE_COMPOSITE effect_mask=%u rain_mask=%u water_mask=%u unmasked_receivers_only=%u\n",unsigned(effectMask),unsigned(rainMask),unsigned(waterMask),unsigned(!effectMask||!rainMask));
 if(hdr&&!material){
  std::ifstream base(prefix+"hdr-base.fgr",std::ios::binary);unsigned h[5]{};base.read(reinterpret_cast<char*>(h),sizeof h);
  assert(base.gcount()==sizeof h&&h[0]==0x31524746&&(h[3]==D3DFMT_A16B16G16R16F||h[3]==D3DFMT_A8R8G8B8||h[3]==D3DFMT_X8R8G8B8));
  const bool srgb=h[3]!=D3DFMT_A16B16G16R16F;
  if(argc==10){assert((std::strcmp(argv[9],"0")==0||std::strcmp(argv[9],"1")==0)&&bool(std::atoi(argv[9]))==srgb);}
  bind(2,prefix+"hdr-base.fgr");must(d->SetSamplerState(2,D3DSAMP_SRGBTEXTURE,srgb));
  std::printf("CAPTURE_HDR_BASE format=%u srgb=%u\n",h[3],unsigned(srgb));
 }
 if(material){bind(4,prefix+group+"4.fgr",true);bind(5,prefix+group+"5.fgr",true);bind(6,prefix+group+"6.fgr");bind(9,prefix+"lighting-input-g0-s9.fgr");}
 else{bind(0,prefix+group+"5.fgr",true);bind(9,prefix+group+"4.fgr",true);}
 IDirect3DPixelShader9* ps=nullptr;if(material){auto code=read(prefix+"lighting-shader-"+argv[6]+".bin");must(d->CreatePixelShader((DWORD*)code.data(),&ps));}else must(d->CreatePixelShader(hdr?kWorldCompositeHdrShader:kWorldCompositeShader,&ps));must(d->SetPixelShader(ps));
 IDirect3DSurface9 *target=nullptr,*copy=nullptr;must(d->CreateRenderTarget(width,height,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));must(d->CreateOffscreenPlainSurface(width,height,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&copy,nullptr));must(d->SetRenderTarget(0,target));must(d->SetDepthStencilSurface(nullptr));
 for(const auto& s:std::initializer_list<std::pair<D3DRENDERSTATETYPE,DWORD>>{{D3DRS_ZENABLE,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_FOGENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_COLORWRITEENABLE,15}})must(d->SetRenderState(s.first,s.second));
 struct V{float x,y,z,w,u,v;};const V quad[]={{-.5f,-.5f,0,1,0,0},{width-.5f,-.5f,0,1,1,0},{-.5f,height-.5f,0,1,0,1},{width-.5f,height-.5f,0,1,1,1}};must(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));D3DVIEWPORT9 vp{0,0,width,height,0,1};must(d->SetViewport(&vp));
 for(unsigned mode=0;mode<2;++mode){
  if(material)bind(0,mode?out+"/material-input-fixed.fgr":prefix+group+"0.fgr",true);
  else bind(3,mode?out+"/current-local-fixed.fgr":prefix+"current-local-light.fgr");
  must(d->BeginScene());must(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(V)));must(d->EndScene());must(d->GetRenderTargetData(target,copy));D3DLOCKED_RECT l;must(copy->LockRect(&l,nullptr,D3DLOCK_READONLY));
  std::ofstream f(out+(material?"/material-":"/world-")+(mode?"after.fgr":"before.fgr"),std::ios::binary);unsigned h[]={0x31524746,width,height,D3DFMT_A16B16G16R16F,8};f.write((const char*)h,20);for(unsigned y=0;y<height;++y)f.write((char*)l.pBits+y*l.Pitch,width*8);assert(f);must(copy->UnlockRect());
 }
 for(auto* t:owned)t->Release();ps->Release();target->Release();copy->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(hwnd);
 return 0;
}
