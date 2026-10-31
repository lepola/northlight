#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include "world_math.h"
#include "model_normal_renderer.h"
using NorthlightGI::Vec3;
static void checked(HRESULT h,int line){if(FAILED(h)){std::printf("line=%d HRESULT=%08lx\n",line,(unsigned long)h);std::abort();}}
#define ok(x) checked((x),__LINE__)
static std::vector<unsigned char> read(const std::string& p){std::ifstream f(p,std::ios::binary);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
static float half(unsigned h){unsigned e=(h>>10)&31,m=h&1023;float v=e==0?std::ldexp(float(m),-24):e==31?(m?NAN:INFINITY):std::ldexp(float(1024+m),int(e)-25);return h&32768?-v:v;}
struct Vertex {float p[3],n[3],uv[2];};
struct Quad {float x,y,z,w,u,v;};
struct Target {IDirect3DTexture9* t=nullptr;IDirect3DSurface9 *s=nullptr,*read=nullptr;unsigned w=0,h=0,bytes=0;D3DFORMAT format;};
static IDirect3DDevice9* d;
static Target target(unsigned w,unsigned h,D3DFORMAT f,unsigned bytes){Target t;t.w=w;t.h=h;t.bytes=bytes;t.format=f;ok(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,f,D3DPOOL_DEFAULT,&t.t,nullptr));ok(t.t->GetSurfaceLevel(0,&t.s));ok(d->CreateOffscreenPlainSurface(w,h,f,D3DPOOL_SYSTEMMEM,&t.read,nullptr));return t;}
static std::vector<unsigned char> pixels(Target& t,const std::string& file=""){ok(d->GetRenderTargetData(t.s,t.read));D3DLOCKED_RECT lock{};ok(t.read->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> b(size_t(t.w)*t.h*t.bytes);for(unsigned y=0;y<t.h;++y)std::memcpy(b.data()+size_t(y)*t.w*t.bytes,(char*)lock.pBits+y*lock.Pitch,t.w*t.bytes);ok(t.read->UnlockRect());if(!file.empty()){std::ofstream f(file,std::ios::binary);unsigned header[]={0x31524746,t.w,t.h,unsigned(t.format),t.bytes};f.write((char*)header,sizeof header);f.write((char*)b.data(),b.size());}return b;}
static void bind(unsigned slot,IDirect3DBaseTexture9* t){ok(d->SetTexture(slot,t));for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(slot,k,D3DTEXF_POINT));for(auto k:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(slot,k,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(slot,D3DSAMP_MIPFILTER,D3DTEXF_NONE));}
static void viewport(Target& t){D3DVIEWPORT9 vp{0,0,t.w,t.h,0,1};ok(d->SetViewport(&vp));}
static void quad(Target& t){viewport(t);ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));Quad v[]={{-.5f,-.5f,0,1,0,0},{float(t.w)-.5f,-.5f,0,1,1,0},{-.5f,float(t.h)-.5f,0,1,0,1},{float(t.w)-.5f,float(t.h)-.5f,0,1,1,1}};ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(Quad)));ok(d->EndScene());}
static IDirect3DPixelShader9* ps(const std::string& p){auto b=read(p);IDirect3DPixelShader9* s=nullptr;ok(d->CreatePixelShader((DWORD*)b.data(),&s));return s;}
static IDirect3DVertexShader9* vs(const std::string& p){auto b=read(p);IDirect3DVertexShader9* s=nullptr;ok(d->CreateVertexShader((DWORD*)b.data(),&s));return s;}
static std::vector<unsigned char> capturedPixels(IDirect3DTexture9* texture,unsigned w,unsigned h){Target t;t.t=texture;t.w=w;t.h=h;t.bytes=8;t.format=D3DFMT_A16B16G16R16F;ok(texture->GetSurfaceLevel(0,&t.s));ok(d->CreateOffscreenPlainSurface(w,h,t.format,D3DPOOL_SYSTEMMEM,&t.read,nullptr));auto data=pixels(t);t.read->Release();t.s->Release();return data;}
static void nearImage(const std::vector<unsigned char>& a,const std::vector<unsigned char>& b){assert(a.size()==b.size());for(size_t i=0;i<a.size();i+=2){unsigned short x,y;std::memcpy(&x,a.data()+i,2);std::memcpy(&y,b.data()+i,2);assert(std::fabs(half(x)-half(y))<.002f);}}
int main(int argc,char**argv){
    assert(argc==5);std::setvbuf(stdout,nullptr,_IONBF,0);std::string dir=argv[2],tag=argv[3];
    auto bytes=read(dir+"terrain.bin");unsigned count,samples;Vec3 center;std::memcpy(&count,bytes.data(),4);std::memcpy(&samples,bytes.data()+4,4);std::memcpy(&center,bytes.data()+8,12);assert(bytes.size()==20+count*sizeof(Vertex)+samples*12);auto vertices=(const Vertex*)(bytes.data()+20);auto points=(const Vec3*)(bytes.data()+20+count*sizeof(Vertex));
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightOfflineShadowCamera";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);assert(api);D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    auto shadowVS=vs(dir+"ShadowVS.bin"),rasterVS=vs(dir+"RasterVS.bin");auto shadowPS=ps(dir+"ShadowPS.bin"),normalPS=ps(dir+"WorldNormals.bin"),fixedNormalPS=ps(dir+"WorldNormalsFixed.bin"),lightingPS=ps(dir+"ActorShadow.bin"),tilePS=ps(dir+"ActorTiles.bin"),rasterPS=ps(dir+"RasterPS.bin");
    D3DVERTEXELEMENT9 decl[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};IDirect3DVertexDeclaration9* declaration=nullptr;ok(d->CreateVertexDeclaration(decl,&declaration));
    auto modelVS=vs(dir+argv[4]);auto modelPS=ps(dir+"ModelPS.bin");auto alphaModelPS=ps(dir+"ModelAlphaPS.bin");
    auto noDistancePS=std::ifstream(dir+"WorldNormalsNoDistance.bin").good()?ps(dir+"WorldNormalsNoDistance.bin"):nullptr;
    auto sourceMesh=read(dir+"human-0.mesh");unsigned meshVertices,meshIndices;std::memcpy(&meshVertices,sourceMesh.data()+4,4);std::memcpy(&meshIndices,sourceMesh.data()+8,4);assert(sourceMesh.size()==12+meshVertices*48+meshIndices*4);
    D3DVERTEXELEMENT9 modelElements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},{0,16,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},{0,20,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    IDirect3DVertexDeclaration9* modelDecl=nullptr;ok(d->CreateVertexDeclaration(modelElements,&modelDecl));
    auto modelNormals=std::make_unique<NorthlightModelNormals>(d);
    IDirect3DVertexBuffer9* sentinelVB=nullptr;IDirect3DIndexBuffer9* sentinelIB=nullptr;ok(d->CreateVertexBuffer(48,0,0,D3DPOOL_MANAGED,&sentinelVB,nullptr));ok(d->CreateIndexBuffer(6,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&sentinelIB,nullptr));
    constexpr unsigned W=768,H=576;auto depth=target(W,H,D3DFMT_R32F,4),geometric=target(W,H,D3DFMT_A16B16G16R16F,8),normals=target(W/2,H/2,D3DFMT_A16B16G16R16F,8),lighting=target(W/2,H/2,D3DFMT_A16B16G16R16F,8);auto tileMid=target(256,256,D3DFMT_R32F,4),tiles=target(64,64,D3DFMT_R32F,4);auto fixedNormals=target(W/2,H/2,D3DFMT_A16B16G16R16F,8);Target shadows[]={target(1024,1024,D3DFMT_R32F,4),target(1024,1024,D3DFMT_R32F,4)};
    IDirect3DSurface9 *sceneDepth=nullptr,*shadowDepth=nullptr;ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&sceneDepth,nullptr));ok(d->CreateDepthStencilSurface(1024,1024,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&shadowDepth,nullptr));
    IDirect3DTexture9* white=nullptr;ok(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&white,nullptr));D3DLOCKED_RECT lock{};ok(white->LockRect(0,&lock,nullptr,0));*(DWORD*)lock.pBits=0xffffffff;ok(white->UnlockRect(0));
    for(auto k:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE})ok(d->SetRenderState(k,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE1,15));
    auto guardNormals=target(W/2,H/2,D3DFMT_A16B16G16R16F,8);
    FILE* csv=std::fopen((dir+tag+"-samples.csv").c_str(),"w");assert(csv);std::fprintf(csv,"elevation,frame,sample,x,y,z,original,geometric,fixed\n");
    for(unsigned elevation=0;elevation<3;++elevation){
        const float z=elevation==0?.15f:elevation==1?.45f:.8f;const Vec3 sun=NorthlightGI::normalized(Vec3(.3f,-.9f,z));float light[2][16];
        bind(12,nullptr);bind(13,nullptr);for(unsigned cascade=0;cascade<2;++cascade){NorthlightWorldMath::shadowMatrix(center,sun,12.f,light[cascade]);bind(2,nullptr);bind(3,nullptr);ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,shadows[cascade].s));ok(d->SetDepthStencilSurface(shadowDepth));viewport(shadows[cascade]);ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetVertexDeclaration(declaration));ok(d->SetVertexShader(shadowVS));ok(d->SetVertexShaderConstantF(0,light[cascade],4));ok(d->SetPixelShader(shadowPS));float material[]={1,1,1,-1};ok(d->SetPixelShaderConstantF(0,material,1));bind(0,white);ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,count/3,vertices,sizeof(Vertex)));ok(d->EndScene());}
        ok(d->SetDepthStencilSurface(nullptr));ok(d->SetPixelShader(tilePS));
        float tileInfo[]={1,1.f/1024,12,0};ok(d->SetPixelShaderConstantF(72,tileInfo,1));bind(12,shadows[0].t);ok(d->SetRenderTarget(0,tileMid.s));quad(tileMid);
        tileInfo[1]=1.f/256;ok(d->SetPixelShaderConstantF(72,tileInfo,1));bind(12,tileMid.t);ok(d->SetRenderTarget(0,tiles.s));quad(tiles);bind(12,nullptr);
        for(unsigned frame=0;frame<17;++frame){
            float angle=(float(frame)-8)*.025f;const Vec3 look=center+Vec3(0,0,.3f),eye=look+Vec3(std::cos(angle)*1.25f,std::sin(angle)*1.25f,.0f),forward=NorthlightGI::normalized(look-eye),right=NorthlightGI::normalized(NorthlightGI::cross(Vec3(0,0,1),forward)),up=NorthlightGI::cross(forward,right);float view[16]={right.x,up.x,forward.x,0,right.y,up.y,forward.y,0,right.z,up.z,forward.z,0,-NorthlightGI::dot(eye,right),-NorthlightGI::dot(eye,up),-NorthlightGI::dot(eye,forward),1};float inverse[16]={right.x,right.y,right.z,0,up.x,up.y,up.z,0,forward.x,forward.y,forward.z,0,eye.x,eye.y,eye.z,1};const float nearZ=.5f,farZ=500,sy=1.7f,sx=sy*H/W;float lens[]={sx,sy,farZ/(farZ-nearZ),nearZ*farZ/(farZ-nearZ)};
            for(unsigned s:{1u,12u,13u,14u})bind(s,nullptr);ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,depth.s));ok(d->SetRenderTarget(1,geometric.s));ok(d->SetDepthStencilSurface(sceneDepth));viewport(depth);ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetVertexDeclaration(declaration));ok(d->SetVertexShader(rasterVS));ok(d->SetPixelShader(rasterPS));ok(d->SetVertexShaderConstantF(100,view,4));ok(d->SetVertexShaderConstantF(104,lens,1));ok(d->SetPixelShaderConstantF(104,lens,1));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,count/3,vertices,sizeof(Vertex)));ok(d->EndScene());ok(d->SetRenderTarget(1,nullptr));ok(d->SetDepthStencilSurface(nullptr));
            modelNormals->endFrame();assert(!modelNormals->texture());bind(13,nullptr);viewport(depth);
            ok(d->SetRenderTarget(0,depth.s));ok(d->SetDepthStencilSurface(sceneDepth));
            ok(d->SetVertexDeclaration(modelDecl));ok(d->SetVertexShader(modelVS));ok(d->SetPixelShader(modelPS));bind(0,white);
            ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
            float native[256][4]={};native[2][0]=sx;native[3][1]=sy;native[4][2]=lens[2];native[4][3]=-lens[3];native[5][2]=1;native[28][3]=1;
            Vec3 placement(center.x,center.y,0),relative=placement-eye;
            for(unsigned bone=0;bone<75;++bone){unsigned r=31+bone*3;native[r][0]=right.x;native[r][1]=right.y;native[r][2]=right.z;native[r][3]=NorthlightGI::dot(relative,right);
                native[r+1][0]=up.x;native[r+1][1]=up.y;native[r+1][2]=up.z;native[r+1][3]=NorthlightGI::dot(relative,up);
                native[r+2][0]=forward.x;native[r+2][1]=forward.y;native[r+2][2]=forward.z;native[r+2][3]=NorthlightGI::dot(relative,forward);}
            ok(d->SetVertexShaderConstantF(0,native[0],256));
            ok(d->SetStreamSource(0,sentinelVB,0,48));ok(d->SetIndices(sentinelIB));
            ok(d->SetRenderState(D3DRS_WRAP5,7));ok(d->SetRenderState(D3DRS_WRAP7,15));
            auto captureModel=[&]{ok(d->BeginScene());modelNormals->capture(modelVS,[&]{return d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,meshVertices,meshIndices/3,sourceMesh.data()+12+48*meshVertices,D3DFMT_INDEX32,sourceMesh.data()+12,48);},true);ok(d->EndScene());assert(modelNormals->texture());};
            float before[224][4],after[224][4];ok(d->GetPixelShaderConstantF(0,before[0],224));captureModel();ok(d->GetPixelShaderConstantF(0,after[0],224));assert(!std::memcmp(before,after,sizeof before));
            IDirect3DVertexShader9* restoredVS=nullptr;IDirect3DPixelShader9* restoredPS=nullptr;ok(d->GetVertexShader(&restoredVS));ok(d->GetPixelShader(&restoredPS));assert(restoredVS==modelVS&&restoredPS==modelPS);restoredVS->Release();restoredPS->Release();
            IDirect3DSurface9 *restoredRT=nullptr,*restoredDS=nullptr;ok(d->GetRenderTarget(0,&restoredRT));ok(d->GetDepthStencilSurface(&restoredDS));assert(restoredRT==depth.s&&restoredDS==sceneDepth);restoredRT->Release();restoredDS->Release();
            IDirect3DVertexBuffer9* restoredVB=nullptr;IDirect3DIndexBuffer9* restoredIB=nullptr;UINT offset=0,stride=0;ok(d->GetStreamSource(0,&restoredVB,&offset,&stride));ok(d->GetIndices(&restoredIB));assert(restoredVB==sentinelVB&&restoredIB==sentinelIB&&stride==48&&offset==0);restoredVB->Release();restoredIB->Release();
            DWORD wrap5=0,wrap7=0;ok(d->GetRenderState(D3DRS_WRAP5,&wrap5));ok(d->GetRenderState(D3DRS_WRAP7,&wrap7));assert(wrap5==7&&wrap7==15);
            if(frame==8&&elevation==0&&SUCCEEDED(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,D3DFMT_A8R8G8B8,TRUE,D3DMULTISAMPLE_2_SAMPLES,nullptr))){
                auto single=capturedPixels(modelNormals->texture(),W,H);IDirect3DSurface9 *msaa=nullptr,*msaaDepth=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_2_SAMPLES,0,FALSE,&msaa,nullptr));ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_2_SAMPLES,0,FALSE,&msaaDepth,nullptr));
                modelNormals->endFrame();ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,msaa));ok(d->SetDepthStencilSurface(msaaDepth));captureModel();assert(single==capturedPixels(modelNormals->texture(),W,H));
                ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,depth.s));ok(d->SetDepthStencilSurface(sceneDepth));msaa->Release();msaaDepth->Release();std::puts("MODEL_NORMALS_MSAA_PASS samples=2 capture_exact");
            }
            if(frame==8&&elevation==0){
                auto opaque=capturedPixels(modelNormals->texture(),W,H);IDirect3DTexture9* transparent=nullptr;ok(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&transparent,nullptr));D3DLOCKED_RECT lock{};ok(transparent->LockRect(0,&lock,nullptr,0));*static_cast<DWORD*>(lock.pBits)=0x00ffffff;ok(transparent->UnlockRect(0));
                ok(d->SetPixelShader(alphaModelPS));ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE));ok(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER));ok(d->SetRenderState(D3DRS_ALPHAREF,128));bind(0,transparent);modelNormals->endFrame();captureModel();auto empty=capturedPixels(modelNormals->texture(),W,H);for(size_t at=6;at<empty.size();at+=8){unsigned short z;std::memcpy(&z,empty.data()+at,2);assert(z==0);}
                bind(0,white);modelNormals->endFrame();captureModel();assert(opaque==capturedPixels(modelNormals->texture(),W,H));ok(d->SetPixelShader(modelPS));ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));transparent->Release();std::puts("MODEL_NORMALS_ALPHA_PASS native_cutout transparent_rejected opaque_retained");
            }
            ok(d->SetDepthStencilSurface(nullptr));
            float c[105][4]={};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=nearZ;c[0][3]=farZ;c[1][0]=sx;c[1][1]=sy;c[1][2]=1;c[2][0]=1;c[2][1]=48;c[2][2]=192;c[2][3]=1.f/1024;std::memcpy(c[3],inverse,64);std::memcpy(c[7],light[0],64);std::memcpy(c[11],light[1],64);std::memcpy(c[15],&eye,12);std::memcpy(c[16],&sun,12);c[16][3]=0;c[17][0]=c[17][1]=c[17][2]=.6f;c[18][0]=c[18][1]=c[18][2]=.2f;c[20][2]=1;c[24][0]=NorthlightWorldMath::ShadowBiasWorld/1280;c[27][1]=1;std::memcpy(c[28],&sun,12);c[29][0]=c[29][1]=c[29][2]=.6f;c[33][0]=W;c[33][1]=H;c[33][2]=W/2;c[33][3]=H/2;c[35][0]=1.f/1280;
            std::memcpy(c[68],light[0],64);c[72][0]=1;c[72][1]=1.f/1024;c[72][2]=12;ok(d->SetPixelShaderConstantF(0,c[0],105));bind(0,white);bind(1,depth.t);ok(d->SetRenderTarget(0,normals.s));ok(d->SetPixelShader(normalPS));quad(normals);if(frame==8)pixels(normals,dir+tag+"-normals.fgr");std::vector<unsigned char> result[3];
            ok(d->SetPixelShader(fixedNormalPS));float modelInfo[]={1,0,0,0};ok(d->SetPixelShaderConstantF(79,modelInfo,1));bind(13,modelNormals->texture());ok(d->SetRenderTarget(0,fixedNormals.s));quad(fixedNormals);bind(13,nullptr);
            if(frame==8)pixels(fixedNormals,dir+tag+"-fixed-normals.fgr");
            if(frame==8&&elevation==0){
                auto baseline=pixels(normals),fixed=pixels(fixedNormals);ok(d->SetRenderTarget(0,guardNormals.s));
                if(noDistancePS){ok(d->SetPixelShader(noDistancePS));bind(13,modelNormals->texture());quad(guardNormals);nearImage(fixed,pixels(guardNormals));}ok(d->SetPixelShader(fixedNormalPS));
                modelInfo[0]=0;ok(d->SetPixelShaderConstantF(79,modelInfo,1));quad(guardNormals);nearImage(baseline,pixels(guardNormals));modelInfo[0]=1;ok(d->SetPixelShaderConstantF(79,modelInfo,1));bind(13,nullptr);
                auto captured=capturedPixels(modelNormals->texture(),W,H);
                for(unsigned invalid=0;invalid<2;++invalid){auto data=captured;for(size_t at=0;at<data.size();at+=8){unsigned short lanes[4];std::memcpy(lanes,data.data()+at,8);if(lanes[3]){if(invalid==0)lanes[3]+=16;else lanes[0]=lanes[1]=lanes[2]=0;std::memcpy(data.data()+at,lanes,8);}}
                    IDirect3DTexture9* stale=nullptr;ok(d->CreateTexture(W,H,1,0,D3DFMT_A16B16G16R16F,D3DPOOL_MANAGED,&stale,nullptr));D3DLOCKED_RECT lock{};ok(stale->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<H;++y)std::memcpy(static_cast<char*>(lock.pBits)+size_t(y)*lock.Pitch,data.data()+size_t(y)*W*8,W*8);ok(stale->UnlockRect(0));bind(13,stale);quad(guardNormals);nearImage(baseline,pixels(guardNormals));bind(13,nullptr);stale->Release();
                }
                std::puts("MODEL_NORMALS_VALIDATION_PASS stale_depth zero_normal disabled");
            }
            for(unsigned mode=0;mode<3;++mode){bind(2,shadows[0].t);bind(3,shadows[1].t);bind(12,shadows[0].t);bind(13,tiles.t);bind(14,mode==0?normals.t:mode==1?geometric.t:fixedNormals.t);ok(d->SetRenderTarget(0,lighting.s));ok(d->SetPixelShader(lightingPS));quad(lighting);result[mode]=pixels(lighting,(frame==0||frame==8||frame==16)?dir+tag+"-e"+std::to_string(elevation)+"-f"+std::to_string(frame)+"-m"+std::to_string(mode)+".fgr":"");}
            if(frame==8){pixels(geometric,dir+tag+"-model-normals.fgr");pixels(depth,dir+tag+"-depth.fgr");}auto scene=pixels(depth);unsigned valid=0,bad=0;float worst=0;
            for(unsigned i=0;i<samples;++i){Vec3 p=points[i]-eye;float vz=NorthlightGI::dot(p,forward);if(vz<=nearZ)continue;float u=.5f+.5f*sx*NorthlightGI::dot(p,right)/vz,v=.5f-.5f*sy*NorthlightGI::dot(p,up)/vz;if(u<.01f||u>.99f||v<.01f||v>.99f)continue;unsigned px=unsigned(u*W),py=unsigned(v*H);float raw;std::memcpy(&raw,scene.data()+4*(py*W+px),4);float hz=nearZ*farZ/(farZ-raw*(farZ-nearZ));if(std::fabs(hz-vz)>.01f)continue;unsigned x=unsigned(u*(W/2)),y=unsigned(v*(H/2));float a[3];for(unsigned m=0;m<3;++m){unsigned short h;std::memcpy(&h,result[m].data()+8*(y*(W/2)+x),2);a[m]=half(h);}float difference=a[1]-a[0];++valid;bad+=difference>.1f;worst=std::max(worst,difference);std::fprintf(csv,"%u,%u,%u,%.6f,%.6f,%.6f,%.8f,%.8f,%.8f\n",elevation,frame,i,points[i].x,points[i].y,points[i].z,a[0],a[1],a[2]);}
            std::printf("elevation=%u frame=%u samples=%u smoothedDarker=%u worst=%.4f\n",elevation,frame,valid,bad,worst);
        }
    }
    modelNormals->endFrame();assert(!modelNormals->texture());modelNormals->reset();assert(!modelNormals->texture());modelNormals.reset();sentinelVB->Release();sentinelIB->Release();std::printf("MODEL_NORMALS_GPU_PASS native_skin_capture_state_restore\n");
    std::fclose(csv);d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);return 0;
}
