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

static std::vector<unsigned char> rgbaPixels(IDirect3DDevice9* d,IDirect3DTexture9* texture){IDirect3DSurface9* src=nullptr,*stage=nullptr;ok(texture->GetSurfaceLevel(0,&src));D3DSURFACE_DESC desc{};ok(src->GetDesc(&desc));ok(d->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&stage,nullptr));ok(d->GetRenderTargetData(src,stage));D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> result(desc.Width*desc.Height*4);for(unsigned y=0;y<desc.Height;++y)std::memcpy(result.data()+y*desc.Width*4,(char*)lock.pBits+y*lock.Pitch,desc.Width*4);ok(stage->UnlockRect());drop(src);drop(stage);return result;}
int main(int argc,char**argv){
 std::setvbuf(stdout,nullptr,_IONBF,0);assert(argc>=3);auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");auto api=create(D3D_SDK_VERSION);
 HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.BackBufferFormat=D3DFMT_X8R8G8B8;IDirect3DDevice9*d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
 Image fixture{128,128,D3DFMT_A8R8G8B8,4,std::vector<unsigned char>(128*128*4)};
 for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x){auto*p=fixture.pixels.data()+(y*128+x)*4;const bool leaf=x>=40&&x<88&&y>=40&&y<88,crack=x>=16&&x<24;p[0]=leaf?16:crack?25:160;p[1]=leaf?85:crack?25:160;p[2]=leaf?169:crack?25:160;p[3]=255;}
 IDirect3DTexture9* source=texture(d,fixture);NorthlightMaterialHeight::Image im;assert(NorthlightMaterialHeight::read(source,im));NorthlightAutoNormalCache cache(d);NorthlightQuality::Settings settings;
 float leaf[3],crack[3];
 for(unsigned mode=0;mode<3;++mode){settings.autoHeightPolarity=mode;cache.endFrame();cache.configure(settings);ok(d->BeginScene());auto* generated=cache.resolve(source);assert(generated);ok(d->EndScene());auto pixels=rgbaPixels(d,generated);const auto profile=NorthlightMaterialHeight::profile(im,mode,75,50);float error=0;
  for(unsigned y=0;y<128;y+=4)for(unsigned x=0;x<128;x+=4){auto*p=fixture.pixels.data()+(y*128+x)*4;const float cpu=NorthlightMaterialHeight::value(p[2]/255.f,p[1]/255.f,p[0]/255.f,profile);error=std::max(error,std::fabs(cpu-pixels[(y*128+x)*4]/255.f));}assert(error<2.f/255);
  leaf[mode]=pixels[(64*128+64)*4]/255.f;crack[mode]=pixels[(64*128+20)*4]/255.f;std::printf("HEIGHT_PROFILE mode=%u leaf=%g crack=%g cpuGpuError=%g\n",mode,leaf[mode],crack[mode],error);
 }
 assert(leaf[0]>.5f&&crack[0]<.5f&&leaf[0]>crack[0]);assert(leaf[1]<.5f&&leaf[2]>.5f);

 // The actual Silvermoon cobble mip with its painted orange leaves.
 auto silvermoon=image(std::string(argv[2])+"/silvermoon-source.fgr");auto* silvermoonTexture=texture(d,silvermoon);settings.autoHeightPolarity=0;cache.configure(settings);cache.endFrame();ok(d->BeginScene());auto* silvermoonMap=cache.resolve(silvermoonTexture);assert(silvermoonMap);ok(d->EndScene());auto silvermoonPixels=rgbaPixels(d,silvermoonMap);unsigned raised=0,orange=0;float minLeaf=1;
 for(unsigned i=0;i<silvermoon.width*silvermoon.height;++i){auto*p=silvermoon.pixels.data()+i*4;float r=p[2]/255.f,g=p[1]/255.f,b=p[0]/255.f;if(r>g*1.4f&&g>b*1.4f&&r>.12f){++orange;float h=silvermoonPixels[i*4]/255.f;if(h>.5f)++raised;minLeaf=std::min(minLeaf,h);}}assert(orange>10&&raised==orange);IDirect3DSurface9* silvermoonSurface=nullptr;ok(silvermoonMap->GetSurfaceLevel(0,&silvermoonSurface));save(d,silvermoonSurface,std::string(argv[2])+"/silvermoon-height.fgr");drop(silvermoonSurface);std::printf("SILVERMOON_LEAVES_PASS orangePixels=%u raised=%u minHeight=%g authoredPlane=.5\n",orange,raised,minLeaf);

 // Smooth painted skin must not become a swollen body. Fine clothing seams
 // remain signed local detail, under the model-specific CPU/GPU height policy.
 Image face{128,128,D3DFMT_A8R8G8B8,4,std::vector<unsigned char>(128*128*4)};
 for(unsigned y=0;y<128;++y)for(unsigned x=0;x<128;++x){auto* p=face.pixels.data()+(y*128+x)*4;p[0]=x<64?160:70+y/4;p[1]=x<64?160:110+y/4;p[2]=x<64?160:180+y/4;p[3]=255;if(x==20)p[0]=p[1]=p[2]=30;}
 auto* skin=texture(d,face);NorthlightMaterialHeight::Image faceImage;assert(NorthlightMaterialHeight::read(skin,faceImage));settings.autoHeightPolarity=0;cache.configure(settings);cache.endFrame();ok(d->BeginScene());auto* skinMap=cache.resolve(skin,true);assert(skinMap);ok(d->EndScene());auto skinPixels=rgbaPixels(d,skinMap);
 const float skinHeight=skinPixels[(64*128+96)*4]/255.f,seamHeight=skinPixels[(64*128+20)*4]/255.f;
 assert(std::fabs(skinHeight-.5f)<2.f/255&&seamHeight<.4f);assert(std::fabs(NorthlightMaterialHeight::modelValue(faceImage,96,64,0)-.5f)<.001f);
 std::printf("MODEL_HEIGHT_PASS paintedSkin=%g seam=%g no_colour_accent_inflation\n",skinHeight,seamHeight);

 // Actual composited equipment path: a CPU atlas is copied to a DEFAULT-pool
 // single-level texture; a later clothing rectangle changes the same resource.
 IDirect3DTexture9 *stagingAtlas=nullptr,*atlas=nullptr;ok(d->CreateTexture(512,512,1,0,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&stagingAtlas,nullptr));ok(d->CreateTexture(512,512,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&atlas,nullptr));
 std::vector<unsigned char> atlasPixels(512*512*4,255);for(unsigned y=0;y<512;++y)for(unsigned x=0;x<512;++x){auto*p=atlasPixels.data()+(y*512+x)*4;bool leaf=x>=64&&x<128&&y>=64&&y<128;p[0]=leaf?16:160;p[1]=leaf?85:160;p[2]=leaf?169:160;}
 auto uploadAtlas=[&]{D3DLOCKED_RECT lock{};ok(stagingAtlas->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<512;++y)std::memcpy((char*)lock.pBits+y*lock.Pitch,atlasPixels.data()+y*512*4,512*4);ok(stagingAtlas->UnlockRect(0));ok(d->UpdateTexture(stagingAtlas,atlas));NorthlightMaterialHeight::copiedTexture(stagingAtlas,atlas,true);};
 NorthlightMaterialTextures::cache().upload(stagingAtlas,stagingAtlas,512,512,0,D3DFMT_A8R8G8B8,atlasPixels.data(),atlasPixels.size(),512*4,true,true);{NorthlightParticleTextures::View v;bool found=NorthlightMaterialTextures::cache().lookup(stagingAtlas,v);std::printf("ATLAS_UPLOAD mirror=%d width=%u enabled=%d failed=%llu retained=%llu\n",int(found),v.width,int(NorthlightMaterialTextures::enabled.load()),(unsigned long long)NorthlightMaterialTextures::cache().counts.failed,(unsigned long long)NorthlightMaterialTextures::cache().counts.retained);}uploadAtlas();{NorthlightParticleTextures::View v;bool found=NorthlightMaterialTextures::cache().lookup(atlas,v);std::printf("ATLAS_COPY mirror=%d width=%u\n",int(found),v.width);}NorthlightMaterialHeight::Image atlasImage;assert(NorthlightMaterialHeight::read(atlas,atlasImage)&&atlasImage.width==128);const auto oldVersion=NorthlightMaterialHeight::version(atlas);uploadAtlas();assert(NorthlightMaterialHeight::version(atlas)==oldVersion);
 settings.autoHeightPolarity=0;cache.configure(settings);cache.endFrame();ok(d->BeginScene());auto* firstAtlas=cache.resolve(atlas);assert(firstAtlas);ok(d->EndScene());auto firstPixels=rgbaPixels(d,firstAtlas);float firstLeaf=firstPixels[(96*512+96)*4]/255.f;assert(firstLeaf>.5f);
 std::vector<unsigned char> rectangle(64*64*4,255);for(unsigned y=64;y<128;++y)for(unsigned x=64;x<128;++x){auto*p=atlasPixels.data()+(y*512+x)*4;p[0]=p[1]=p[2]=25;}for(size_t i=0;i<rectangle.size();i+=4)rectangle[i]=rectangle[i+1]=rectangle[i+2]=25;
 assert(NorthlightMaterialTextures::cache().updateRect(stagingAtlas,stagingAtlas,512,512,0,D3DFMT_A8R8G8B8,rectangle.data(),rectangle.size(),64*4,64,64,128,128,true));uploadAtlas();assert(NorthlightMaterialHeight::version(atlas)!=oldVersion);
 cache.endFrame();ok(d->BeginScene());auto* secondAtlas=cache.resolve(atlas);assert(secondAtlas);ok(d->EndScene());auto secondPixels=rgbaPixels(d,secondAtlas);float secondLeaf=secondPixels[(96*512+96)*4]/255.f;assert(secondLeaf<.5f&&secondLeaf<firstLeaf-.1f);std::printf("EQUIPMENT_ATLAS_PASS before=%g after=%g updated_same_texture_without_gpu_readback\n",firstLeaf,secondLeaf);
 // The centered POM ray must give opposite UV displacements for a cavity and
 // protrusion, while a flat .5 field leaves the authored UV unchanged.
 auto bytes=read(std::string(argv[2])+"/SignedPOM.bin");IDirect3DPixelShader9* shader=nullptr;ok(d->CreatePixelShader((DWORD*)bytes.data(),&shader));IDirect3DSurface9* target=nullptr,*stage=nullptr;ok(d->CreateRenderTarget(64,64,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(64,64,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&stage,nullptr));
 ok(d->SetRenderTarget(0,target));d->SetDepthStencilSurface(nullptr);D3DVIEWPORT9 vp{0,0,64,64,0,1};d->SetViewport(&vp);d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetPixelShader(shader);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);
 const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{63.5f,-.5f,0,1,1,0},{-.5f,63.5f,0,1,0,1},{63.5f,63.5f,0,1,1,1}};float shifted[3];
 for(unsigned mode=0;mode<3;++mode){float h=.2f+mode*.3f;Image plane{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};float data[4]={0,0,h,1};std::memcpy(plane.pixels.data(),data,16);auto* map=texture(d,plane);d->SetTexture(0,map);d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,stage));D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::memcpy(&shifted[mode],(char*)lock.pBits+32*lock.Pitch+32*16,4);ok(stage->UnlockRect());drop(map);}
 assert(shifted[0]<shifted[1]-.005f&&shifted[2]>shifted[1]+.005f);assert(std::fabs(shifted[1]-(32.5f/64))<.0001f);
 std::printf("SIGNED_POM_PASS cavity=%g flat=%g raised=%g\n",shifted[0],shifted[1],shifted[2]);std::puts("MATERIAL_HEIGHT_PASS raised_leaves negative_crevices same_cpu_gpu_height signed_pom");return 0;
}
