// Recorded final composition through the production HDR bloom/grade/shoulder.
#define main unused_composite_replay_main
#include "lamp_capture_composite_gpu.cpp"
#undef main
#include "hdr_frame.h"

int main(int argc,char** argv){
 assert(argc==3);const std::string out=argv[2];
 auto input=read(out+"/final-shadowed.fgr");unsigned header[5];std::memcpy(header,input.data(),20);
 const unsigned width=header[1],height=header[2];assert(header[0]==0x31524746&&header[3]==D3DFMT_A16B16G16R16F);
 auto window=CreateWindowW(L"STATIC",L"",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
 auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");auto api=create(D3D_SDK_VERSION);assert(api);
 D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
 IDirect3DDevice9* d=nullptr;must(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 IDirect3DPixelShader9* copyShader=nullptr;must(d->CreatePixelShader(reinterpret_cast<const DWORD*>(kHdrCopy),&copyShader));
 IDirect3DSurface9 *target=nullptr,*staging=nullptr;must(d->CreateRenderTarget(width,height,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));must(d->CreateOffscreenPlainSurface(width,height,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
 for(int ev:{-2,0,1})for(const char* mode:{"shadowed","unshadowed"}){
  NorthlightHdr::Frame frame;auto* source=texture(d,out+"/final-"+mode+".fgr");must(d->BeginScene());must(frame.ensure(d,width,height));
  must(NorthlightHdr::quadState(d,frame.surface(),width,height));must(d->SetPixelShader(copyShader));must(d->SetTexture(0,source));must(NorthlightHdr::drawQuad(d,width,height));
  NorthlightHdr::Settings settings;settings.paperWhite=203;settings.maxNits=1000;settings.contrast=1;settings.exposure=float(ev);settings.autoExposure=false;settings.bloom=1;
  must(frame.finish(d,nullptr,target,settings,1.f/60,true,false));must(d->EndScene());must(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock;must(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
  std::ofstream output(out+"/present-"+std::to_string(ev)+"-"+mode+".fgr",std::ios::binary);output.write((const char*)header,20);for(unsigned y=0;y<height;++y)output.write((char*)lock.pBits+y*lock.Pitch,width*8);assert(output);must(staging->UnlockRect());source->Release();
 }
 std::puts("CAPTURE_HDR_PRESENT_PASS exposures=-2,0,1 paperWhite=203 maxNits=1000 contrast=1 bloom=1 automatic_history_not_captured=1");
 target->Release();staging->Release();copyShader->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
