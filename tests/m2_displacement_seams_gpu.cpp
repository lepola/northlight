// Hidden-device regression: skinned seams, native eye depth and CPU/VTF parity.
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include "draw_snapshot.h"
#include "displacement_geometry.h"
#include "displacement_shader_patch.h"

template<class T> static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void ok(HRESULT hr){assert(SUCCEEDED(hr));}
static std::vector<DWORD> shader(const std::string& path){std::ifstream f(path,std::ios::binary|std::ios::ate);assert(f);const auto n=f.tellg();assert(n>0&&n%4==0);std::vector<DWORD> b(size_t(n)/4);f.seekg(0);f.read((char*)b.data(),n);assert(f);return b;}
using namespace NorthlightDisplacementGeometry;
struct V {float x,y,z;unsigned char weights[4],bones[4];float nx,ny,nz,u,v,u1,v1;};
static_assert(sizeof(V)==48);
static const D3DVERTEXELEMENT9 elements[]={
    {0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},
    {0,12,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},
    {0,16,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},
    {0,20,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},
    {0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
static Mesh mesh(const std::vector<V>& vertices,const std::vector<uint32_t>& indices){Mesh m;m.vertexCount=unsigned(vertices.size());m.primitiveCount=unsigned(indices.size()/3);m.indexed=true;m.indices=indices;m.streams[0].stride=sizeof(V);m.streams[0].bytes.resize(vertices.size()*sizeof(V));std::memcpy(m.streams[0].bytes.data(),vertices.data(),m.streams[0].bytes.size());return m;}
static std::vector<std::array<float,2>> coordinates(const Mesh& m){std::vector<std::array<float,2>> uv;for(unsigned i=0;i<m.vertexCount;++i){V v;std::memcpy(&v,m.streams[0].bytes.data()+i*sizeof(V),sizeof v);uv.push_back({v.u,v.v});}return uv;}
static Mesh buildMesh(const Mesh& source,unsigned factor,bool gpu,bool safe=true){Height h;h.width=h.height=8;h.values.assign(64,.8f);Mesh out;assert(build(source,elements,6,coordinates(source),h,factor,false,true,true,out,nullptr,nullptr,nullptr,safe?4:0,gpu));return out;}
static unsigned moved(const Mesh& m){unsigned n=0;for(unsigned i=0;i<m.vertexCount;++i){float d[3];std::memcpy(d,m.streams[0].bytes.data()+size_t(i)*m.streams[0].stride+sizeof(V),12);n+=d[0]!=0||d[1]!=0||d[2]!=0;}return n;}
static bool allBoundary(const Mesh& m){std::unordered_map<uint64_t,unsigned> edges;for(unsigned i=0;i<m.indices.size();i+=3)for(unsigned k=0;k<3;++k)++edges[edge(m.indices[i+k],m.indices[i+(k+1)%3])];std::vector<bool> boundary(m.vertexCount);for(auto e:edges)if(e.second!=2){boundary[unsigned(e.first>>32)]=true;boundary[unsigned(e.first)]=true;}return std::all_of(boundary.begin(),boundary.end(),[](bool b){return b;});}

int main(int argc,char** argv){
    assert(argc>=3);const std::string folder=argv[2];
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightM2Seams";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    constexpr unsigned W=160,H=96;IDirect3DSurface9 *target=nullptr,*depth=nullptr,*stage=nullptr;
    ok(d->CreateRenderTarget(W,H,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&stage,nullptr));
    ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(depth));D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));
    for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE})ok(d->SetRenderState(key,TRUE));for(auto key:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_LIGHTING,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(key,FALSE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    const auto code=shader(folder+"SeamVS.bin");IDirect3DVertexShader9 *original=nullptr,*variants[2]={};NorthlightDisplacementPatch::Binding bindings[2];ok(d->CreateVertexShader(code.data(),&original));
    for(unsigned gpu=0;gpu<2;++gpu){std::vector<uint32_t> words;assert(NorthlightDisplacementPatch::patch(reinterpret_cast<const uint32_t*>(code.data()),code.size(),words,bindings[gpu],true,gpu!=0));ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(words.data()),&variants[gpu]));}
    auto psCode=shader(folder+"SeamPS.bin");IDirect3DPixelShader9* ps=nullptr;ok(d->CreatePixelShader(psCode.data(),&ps));ok(d->SetPixelShader(ps));
    IDirect3DVertexDeclaration9 *nativeDecl=nullptr,*decls[2]={};ok(d->CreateVertexDeclaration(elements,&nativeDecl));
    for(unsigned gpu=0;gpu<2;++gpu){std::vector<D3DVERTEXELEMENT9> e(elements,elements+5);e.push_back({0,sizeof(V),D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_TEXCOORD,7});if(gpu)e.push_back({0,sizeof(V)+12,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,6});e.push_back(D3DDECL_END());ok(d->CreateVertexDeclaration(e.data(),&decls[gpu]));}
    IDirect3DTexture9* height=nullptr;ok(d->CreateTexture(8,8,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&height,nullptr));D3DLOCKED_RECT lock{};ok(height->LockRect(0,&lock,nullptr,0));for(unsigned y=0;y<8;++y)std::fill_n((float*)((char*)lock.pBits+y*lock.Pitch),8,.8f);ok(height->UnlockRect(0));
    auto vertex=[](float x,float y,float z){return V{x,y,z,{255,0,0,0},{0,0,0,0},0,0,1,(x+1)*.5f,(y+1)*.5f,0,0};};
    const auto shell=mesh({vertex(-.9f,-.8f,.5f),vertex(-.1f,-.8f,.5f),vertex(-.9f,.8f,.5f),vertex(-.1f,.8f,.5f)},{0,1,2,2,1,3});
    const auto interior=mesh({vertex(.1f,-.8f,.5f),vertex(.9f,-.8f,.5f),vertex(.1f,.8f,.5f),vertex(.9f,.8f,.5f),vertex(.5f,0,.5f)},{0,1,4,1,3,4,3,2,4,2,0,4});
    const V eye[]={vertex(-.8f,-.6f,.505f),vertex(-.2f,-.6f,.505f),vertex(-.8f,.6f,.505f),vertex(-.2f,.6f,.505f)};
    auto draw=[&](const Mesh& m,unsigned gpu,float strength){const auto& b=bindings[gpu];float controls[8]={strength,0,0,0,.5f,0,0,0};ok(d->SetVertexShader(variants[gpu]));ok(d->SetVertexShaderConstantF(b.constant,controls,b.count));ok(d->SetVertexDeclaration(decls[gpu]));if(gpu){const unsigned stage=D3DVERTEXTEXTURESAMPLER0+b.sampler;ok(d->SetTexture(stage,height));for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(stage,key,D3DTEXF_LINEAR));ok(d->SetSamplerState(stage,D3DSAMP_MIPFILTER,D3DTEXF_NONE));}return d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,m.vertexCount,m.primitiveCount,m.indices.data(),D3DFMT_INDEX32,m.streams[0].bytes.data(),m.streams[0].stride);};
    auto render=[&](unsigned gpu,unsigned factor,float strength,bool safe){auto a=buildMesh(shell,factor,gpu!=0,safe),b=buildMesh(interior,factor,gpu!=0);assert(moved(b)>0);if(safe)assert(moved(a)==0);
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));ok(d->BeginScene());float color[]={.25f,.25f,.25f,1};ok(d->SetPixelShaderConstantF(0,color,1));ok(draw(a,gpu,strength));ok(draw(b,gpu,strength));
        ok(d->SetVertexShader(original));ok(d->SetVertexDeclaration(nativeDecl));color[0]=1;color[1]=color[2]=0;ok(d->SetPixelShaderConstantF(0,color,1));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,eye,sizeof(V)));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->EndScene());
        ok(d->GetRenderTargetData(target,stage));ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> pixels(W*H*4);for(unsigned y=0;y<H;++y)std::memcpy(pixels.data()+size_t(y)*W*4,(char*)lock.pBits+y*lock.Pitch,W*4);ok(stage->UnlockRect());return pixels;};
    auto bleed=[](const std::vector<unsigned char>& pixels){unsigned n=0;for(size_t i=0;i<pixels.size();i+=4)n+=pixels[i+2]>pixels[i+1]+100;return n;};
    unsigned cases=0;for(unsigned factor:{2u,4u,16u})for(float strength:{.03f,.12f}){auto cpu=render(0,factor,strength,true),gpu=render(1,factor,strength,true);assert(cpu==gpu&&bleed(cpu)==0);for(unsigned path=0;path<2;++path){const auto bad=bleed(render(path,factor,strength,false));assert(bad>0);std::printf("M2_SEAM_GPU factor=%u strength=%.2f gpu=%u bleed=0 negativeControl=%u\n",factor,strength,path,bad);++cases;std::fflush(stdout);}}
    // Authentic client meshes remain local. Check actual thin head/eye geosets
    // and CPU/VTF masks without inferring race from runtime shader identities.
    for(int arg=3;arg<argc;++arg){std::ifstream f(argv[arg],std::ios::binary);uint32_t header[3];f.read((char*)header,sizeof header);assert(f&&header[0]==0x314d3246&&header[1]<=32768&&header[2]<=24576);std::vector<V> vertices(header[1]);std::vector<uint32_t> indices(header[2]);f.read((char*)vertices.data(),vertices.size()*sizeof(V));f.read((char*)indices.data(),indices.size()*4);assert(f);auto source=mesh(vertices,indices);const auto cpu=buildMesh(source,4,false),gpu=buildMesh(source,4,true);assert(cpu.indices==gpu.indices&&cpu.vertexCount==gpu.vertexCount);for(unsigned i=0;i<cpu.vertexCount;++i){float a[3],b[3];std::memcpy(a,cpu.streams[0].bytes.data()+size_t(i)*cpu.streams[0].stride+sizeof(V),12);std::memcpy(b,gpu.streams[0].bytes.data()+size_t(i)*gpu.streams[0].stride+sizeof(V),12);for(unsigned c=0;c<3;++c)assert(std::fabs(a[c]-b[c]*.3f)<1e-6f);}if(allBoundary(source))assert(moved(cpu)==0);std::printf("M2_CLIENT_SEAM mesh=%s vertices=%u output=%u moved=%u\n",argv[arg],header[1],cpu.vertexCount,moved(cpu));}
    std::printf("M2_DISPLACEMENT_SEAMS_PASS cases=%u cpu_vtf_exact eye_depth_preserved negative_controls_rejected\n",cases);
    drop(height);drop(original);drop(ps);for(auto& p:variants)drop(p);for(auto& p:decls)drop(p);drop(nativeDecl);drop(stage);drop(target);drop(depth);drop(d);drop(api);FreeLibrary(module);DestroyWindow(window);
}
