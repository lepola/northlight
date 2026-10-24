// SPDX-License-Identifier: MIT
// Hidden SDK window only; no game process is created or controlled.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "stream_device.h"
#include "atlas_gpu_diagnostic.h"

static void check(HRESULT hr,const char* call){if(FAILED(hr)){std::printf("FAIL %s hr=%08lx\n",call,(unsigned long)hr);std::exit(20);}}
static constexpr unsigned Size=512,Tile=64;
static constexpr unsigned short colors[]={0xf800,0x07e0,0x001f,0xffe0,0xf81f,0x07ff,0xffff,0};
static unsigned rgb(unsigned short c){const unsigned r=c>>11,g=(c>>5)&63,b=c&31;return (((r<<3)|(r>>2))<<16)|(((g<<2)|(g>>4))<<8)|((b<<3)|(b>>2));}
static void fillBlock(unsigned char* p,unsigned fmt,unsigned short c){
    if(fmt!=D3DFMT_DXT1){std::memset(p,0xff,8);if(fmt==D3DFMT_DXT5){p[0]=p[1]=255;std::memset(p+2,0,6);}p+=8;}
    std::memcpy(p,&c,2);std::memcpy(p+2,&c,2);std::memset(p+4,0,4);
}
static void fillInterpolatedBlock(unsigned char* p,unsigned fmt,unsigned variant){
    if(fmt!=D3DFMT_DXT1){
        if(fmt==D3DFMT_DXT3)for(unsigned n=0;n<8;++n)p[n]=static_cast<unsigned char>((2*n+1)*16+2*n);
        else{p[0]=variant%2?0:255;p[1]=variant%2?255:0;std::uint64_t bits=0;for(unsigned n=0;n<16;++n)bits|=std::uint64_t(n%8)<<(3*n);for(unsigned n=0;n<6;++n)p[n+2]=static_cast<unsigned char>(bits>>(8*n));}
        p+=8;
    }
    constexpr unsigned short endpoints[][2]={{0xfff2,0x2046},{0x07e0,0},{0xffff,0},{0x1234,0xdddd},{0,0},{0xffff,0xffff},{0x0821,0},{1,0}};
    std::memcpy(p,&endpoints[variant%8][0],2);std::memcpy(p+2,&endpoints[variant%8][1],2);
    const std::uint32_t indices=0xe4e4e4e4;std::memcpy(p+4,&indices,4);
}
static void writeTile(IDirect3DTexture9* texture,unsigned fmt,unsigned cell,unsigned variant,bool dirty,bool interpolate=false){
    IDirect3DSurface9* surface=nullptr;check(texture->GetSurfaceLevel(0,&surface),"GetSurfaceLevel");
    const RECT rect={LONG(cell%8*Tile),LONG(cell/8*Tile),LONG((cell%8+1)*Tile),LONG((cell/8+1)*Tile)};
    D3DLOCKED_RECT lock{};check(surface->LockRect(&lock,&rect,dirty?D3DLOCK_NO_DIRTY_UPDATE:0),"Lock tile");
    const unsigned block=fmt==D3DFMT_DXT1?8:16;
    std::vector<unsigned char> mip(16*16*block);
    for(unsigned i=0;i<256;++i){if(interpolate)fillInterpolatedBlock(mip.data()+i*block,fmt,i);else fillBlock(mip.data()+i*block,fmt,colors[(cell+variant)%8]);}
    for(unsigned row=0;row<16;++row)std::memcpy(static_cast<unsigned char*>(lock.pBits)+row*lock.Pitch,mip.data()+row*16*block,16*block);
    check(surface->UnlockRect(),"Unlock tile");surface->Release();
    if(dirty)check(texture->AddDirtyRect(&rect),"AddDirtyRect");
    std::fill(mip.begin(),mip.end(),0xee);
}
static void draw(IDirect3DDevice9* d,IDirect3DTexture9* texture,IDirect3DSurface9* target){
    check(d->SetRenderTarget(0,target),"SetRenderTarget");check(d->SetDepthStencilSurface(nullptr),"SetDepth");
    D3DVIEWPORT9 viewport{0,0,Size,Size,0,1};check(d->SetViewport(&viewport),"SetViewport");
    check(d->SetVertexShader(nullptr),"Set VS");check(d->SetPixelShader(nullptr),"Set PS");
    check(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1),"Set FVF");
    for(auto pair:{std::pair<D3DRENDERSTATETYPE,DWORD>{D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_LIGHTING,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_SRGBWRITEENABLE,FALSE}})check(d->SetRenderState(pair.first,pair.second),"Set render state");
    check(d->SetTexture(0,texture),"Set texture");
    check(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1),"Set colorop");check(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE),"Set colorarg");
    check(d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1),"Set alphaop");check(d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE),"Set alphaarg");
    check(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE),"Disable stage1");
    for(auto pair:{std::pair<D3DSAMPLERSTATETYPE,DWORD>{D3DSAMP_MINFILTER,D3DTEXF_POINT},{D3DSAMP_MAGFILTER,D3DTEXF_POINT},{D3DSAMP_MIPFILTER,D3DTEXF_NONE},{D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},{D3DSAMP_SRGBTEXTURE,FALSE},{D3DSAMP_MAXMIPLEVEL,0},{D3DSAMP_MIPMAPLODBIAS,0}})check(d->SetSamplerState(0,pair.first,pair.second),"Set sampler");
    struct V{float x,y,z,rhw,u,v;};const V vertices[]={{-.5f,-.5f,0,1,0,0},{Size-.5f,-.5f,0,1,1,0},{-.5f,Size-.5f,0,1,0,1},{Size-.5f,Size-.5f,0,1,1,1}};
    check(d->BeginScene(),"BeginScene");check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(V)),"Draw atlas");check(d->EndScene(),"EndScene");
}
static unsigned readPixels(IDirect3DDevice9* d,IDirect3DSurface9* target,IDirect3DSurface9* readback,const std::string& path,unsigned variant){
    check(d->GetRenderTargetData(target,readback),"GetRenderTargetData");
    D3DLOCKED_RECT lock{};check(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY),"Read pixels");
    std::vector<unsigned char> pixels(Size*Size*4);unsigned errors=0,firstX=0,firstY=0,firstHave=0,firstWant=0;
    for(unsigned y=0;y<Size;++y){auto* src=static_cast<const unsigned char*>(lock.pBits)+y*lock.Pitch;std::memcpy(pixels.data()+y*Size*4,src,Size*4);
        for(unsigned x=0;x<Size;++x){unsigned p=0;std::memcpy(&p,src+x*4,4);const unsigned want=rgb(colors[(y/Tile*8+x/Tile+variant)%8]);if((p&0xffffff)!=want){if(!errors){firstX=x;firstY=y;firstHave=p;firstWant=want;}++errors;}}}
    check(readback->UnlockRect(),"Unlock pixels");
    FILE* f=std::fopen(path.c_str(),"wb");if(f){std::fwrite(pixels.data(),1,pixels.size(),f);std::fclose(f);}
    if(errors)std::printf("PIXEL_FAIL file=%s count=%u first=%u,%u have=%08x want=%06x\n",path.c_str(),errors,firstX,firstY,firstHave,firstWant);
    return errors;
}
static void sync(NorthlightStream::StreamDevice* stream){if(stream&&!NorthlightStream::runTask(*NorthlightStream::activeCore.load(),[](NorthlightStream::StreamCore&){},NorthlightStream::Cmd::SyncInit))std::exit(21);}
static std::vector<IDirect3DTexture9*> pressure(IDirect3DDevice9* d){
    std::vector<IDirect3DTexture9*> result;
    for(unsigned n=0;n<20;++n){IDirect3DTexture9* t=nullptr;check(d->CreateTexture(512,512,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&t,nullptr),"Create eviction pressure");
        for(unsigned twice=0;twice<2;++twice){IDirect3DSurface9* s=nullptr;check(t->GetSurfaceLevel(0,&s),"Pressure surface");RECT rect{0,0,8,8};D3DLOCKED_RECT lock{};check(s->LockRect(&lock,&rect,0),"Pressure lock");for(unsigned y=0;y<8;++y)std::memset(static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch,n,32);check(s->UnlockRect(),"Pressure unlock");s->Release();}
        result.push_back(t);}
    return result;
}
#include "ui_draw_diagnostic_scenarios.h"

int main(int argc,char** argv){
    if(argc!=3&&argc!=4)return 2;
    HMODULE module=LoadLibraryExA(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!module)return 3;
    IDirect3D9*(WINAPI*create)(UINT)=nullptr;auto proc=GetProcAddress(module,"Direct3DCreate9");std::memcpy(&create,&proc,sizeof(create));
    IDirect3D9* d3d=create?create(D3D_SDK_VERSION):nullptr;if(!d3d)return 4;
    WNDCLASSA cls{};cls.lpfnWndProc=DefWindowProcA;cls.hInstance=GetModuleHandleA(nullptr);cls.lpszClassName="NorthlightUiAtlasValidation";RegisterClassA(&cls);
    HWND window=CreateWindowExA(0,cls.lpszClassName,"hidden-atlas-validation",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,cls.hInstance,nullptr);
    unsigned errors=0;NorthlightAtlasProof::enabled=true;
    for(unsigned streamed:{0u,1u}){
        if(argc==4&&streamed)break; // The loaded Northlight DLL already owns its exact production stream.
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=Size;pp.BackBufferHeight=Size;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
        IDirect3DDevice9* target=nullptr;check(d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&target),"CreateDevice");
        IDirect3DDevice9* d=target;NorthlightStream::StreamDevice* stream=nullptr;
        if(streamed){NorthlightStream::StreamDevice::Options options;options.framesAhead=2;options.frameSkip=0;options.directReplay=true;options.extension=target;options.rawOf=[](IUnknown* e,unsigned){return e;};options.diagnostics=[](){return true;};options.log=[](const char* s){std::printf("STREAM %s\n",s);};const char* reason=nullptr;
            stream=NorthlightStream::StreamDevice::make(target,d3d,&pp,options,&reason);if(!stream){std::printf("STREAM_MAKE_FAIL %s\n",reason);return 5;}d=stream;}
        IDirect3DSurface9 *render=nullptr,*readback=nullptr;
        check(d->CreateRenderTarget(Size,Size,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&render,nullptr),"Create RT");
        check(d->CreateOffscreenPlainSurface(Size,Size,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr),"Create readback");
        for(unsigned fmt:{unsigned(D3DFMT_DXT1),unsigned(D3DFMT_DXT3),unsigned(D3DFMT_DXT5)}){
            IDirect3DTexture9* texture=nullptr;check(d->CreateTexture(Size,Size,1,0,(D3DFORMAT)fmt,D3DPOOL_MANAGED,&texture,nullptr),"Create managed atlas");
            for(unsigned pass=0;pass<3;++pass){
                std::vector<IDirect3DTexture9*> evictions;if(pass==1)evictions=pressure(d);
                if(stream&&pass){sync(stream);auto* proxy=static_cast<NorthlightStream::StreamTexture*>(NorthlightStream::ProxyBase::of(texture));NorthlightStream::dropSubShadow(*NorthlightStream::activeCore.load(),proxy->subs[0]);}
                for(unsigned n=0;n<64;++n)writeTile(texture,fmt,pass==1?63-n:n,pass,pass==2);
                draw(d,texture,render);
                const std::string path=std::string(argv[2])+"-"+(streamed?"stream":"direct")+"-"+std::to_string(fmt)+"-"+std::to_string(pass)+".bgra";
                unsigned bad=readPixels(d,render,readback,path,pass);errors+=bad;
                std::printf("ATLAS mode=%s fmt=%u pass=%u badPixels=%u\n",streamed?"stream":"direct",fmt,pass,bad);
                if(stream&&pass==2){
                    NorthlightAtlasDiagnostic::Result diagnostic;
                    if(!NorthlightStream::runTask(*NorthlightStream::activeCore.load(),[&](NorthlightStream::StreamCore&){
                        auto* raw=NorthlightStream::ProxyBase::of(texture)->raw;
                        auto* entry=NorthlightAtlasProof::cache().find(raw);if(!entry){++diagnostic.failed;return;}
                        DWORD before=0,after=0;target->SetSamplerState(0,D3DSAMP_MIPMAPLODBIAS,0x3f800000);target->GetSamplerState(0,D3DSAMP_MIPMAPLODBIAS,&before);
                        if(NorthlightAtlasDiagnostic::capture(target,*entry,std::string(argv[2])+"-diagnostic-"+std::to_string(fmt),diagnostic))++diagnostic.captured;else ++diagnostic.failed;
                        target->GetSamplerState(0,D3DSAMP_MIPMAPLODBIAS,&after);if(before!=after)++diagnostic.failed;
                        auto mismatch=*entry;mismatch.tiles[0]^=1;NorthlightAtlasDiagnostic::Result negative;
                        if(!NorthlightAtlasDiagnostic::capture(target,mismatch,std::string(argv[2])+"-negative-"+std::to_string(fmt),negative)||negative.rawMismatches!=1||negative.pixelMismatches)++diagnostic.failed;
                    },NorthlightStream::Cmd::SyncInit))return 22;
                    std::printf("DIAGNOSTIC fmt=%u captured=%u failed=%u known=%u sourceRawMismatch=%u rawGpuBadPixels=%u\n",fmt,diagnostic.captured,diagnostic.failed,diagnostic.knownTiles,diagnostic.rawMismatches,diagnostic.pixelMismatches);
                    errors+=diagnostic.failed+diagnostic.rawMismatches+diagnostic.pixelMismatches+(diagnostic.knownTiles!=64);
                }
                for(auto* t:evictions)t->Release();
            }
            // Exercise all color/alpha selectors and contrast endpoints. Solid
            // endpoint colors above cannot reveal valid hardware BC differences.
            for(unsigned cell=0;cell<64;++cell)writeTile(texture,fmt,cell,0,false,true);
            draw(d,texture,render);sync(stream);
            NorthlightAtlasDiagnostic::Result interpolation;
            auto captureInterpolation=[&](){
                NorthlightAtlasProof::Entry entry{};entry.format=fmt;entry.raw=texture;
                if(stream){auto* raw=NorthlightStream::ProxyBase::of(texture)->raw;auto* known=NorthlightAtlasProof::cache().find(raw);if(!known){++interpolation.failed;return;}entry=*known;}
                if(NorthlightAtlasDiagnostic::capture(target,entry,std::string(argv[2])+"-interpolation-"+(streamed?"stream-":"direct-")+std::to_string(fmt),interpolation))++interpolation.captured;else ++interpolation.failed;
            };
            if(stream){if(!NorthlightStream::runTask(*NorthlightStream::activeCore.load(),[&](NorthlightStream::StreamCore&){captureInterpolation();},NorthlightStream::Cmd::SyncInit))return 22;}
            else captureInterpolation();
            errors+=interpolation.failed+interpolation.rawMismatches+interpolation.pixelMismatches;
            std::printf("INTERPOLATION mode=%s fmt=%u captured=%u failed=%u sourceRawMismatch=%u outsideEnvelopePixels=%u\n",streamed?"stream":"direct",fmt,interpolation.captured,interpolation.failed,interpolation.rawMismatches,interpolation.pixelMismatches);
            // Source SYSTEMMEM atlas -> DEFAULT atlas via GPU update, then shader sampling.
            IDirect3DTexture9 *system=nullptr,*gpu=nullptr;
            check(d->CreateTexture(Size,Size,1,0,(D3DFORMAT)fmt,D3DPOOL_SYSTEMMEM,&system,nullptr),"Create system atlas");
            check(d->CreateTexture(Size,Size,1,0,(D3DFORMAT)fmt,D3DPOOL_DEFAULT,&gpu,nullptr),"Create GPU atlas");
            for(unsigned cell=0;cell<64;++cell)writeTile(system,fmt,cell,3,false);
            check(d->UpdateTexture(system,gpu),"UpdateTexture GPU atlas");draw(d,gpu,render);
            const std::string path=std::string(argv[2])+"-"+(streamed?"stream":"direct")+"-"+std::to_string(fmt)+"-gpu.bgra";
            unsigned bad=readPixels(d,render,readback,path,3);errors+=bad;std::printf("ATLAS mode=%s fmt=%u pass=gpu badPixels=%u\n",streamed?"stream":"direct",fmt,bad);
            system->Release();gpu->Release();texture->Release();
        }
        uiDrawScenarios(d,target,stream,std::string(argv[2])+"-uidraw-"+(streamed?"stream":"direct"));
        d->SetTexture(0,nullptr);render->Release();readback->Release();d->Release();
    }
    d3d->Release();DestroyWindow(window);FreeLibrary(module);
    std::printf("RESULT badPixels=%u hiddenWindow=1 gameProcess=0\n",errors);return errors?10:0;
}
