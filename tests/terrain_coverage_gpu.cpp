// Compare the production handoff patch against a frozen pre-optimization patch.
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "patch_terrain_shadow_coverage.h"
#include "terrain_coverage_before.h"
using namespace NorthlightTerrainCoverage;
static void checked(HRESULT result,unsigned line){if(FAILED(result)){std::printf("line=%u HRESULT=%08lx\n",line,(unsigned long)result);std::abort();}}
#define ok(operation) checked((operation),__LINE__)
template<class T>static void drop(T*& value){if(value)value->Release();value=nullptr;}
static Word instruction(unsigned code,unsigned count){return code|(count<<24);}
static std::vector<Word> original(){
    std::vector<Word> code{0xffff0300,instruction(81,5),dst(2,0),bits(.3f),bits(.7f),bits(1),bits(0)};
    code.insert(code.end(),{instruction(31,2),0x8000000au,dst(1,0),instruction(31,2),0x80050005u,dst(1,1,7),
        instruction(1,2),dst(0,0),src(1,0),instruction(4,4),dst(0,1,1),src(0,0,0xff),src(2,0,0x00),src(2,0,0x55),
        instruction(5,3),dst(0,1,7),src(1,0),src(0,1,0x00),instruction(1,2),dst(8,0,7),src(0,1),
        instruction(1,2),dst(8,0,8),src(2,0,0xaa),65535});
    return code;
}
struct Vertex{float x,y,z,w;DWORD color;float unused[10];float position[4];};
static void wait(IDirect3DQuery9* query,void* output,DWORD bytes){
    const DWORD start=GetTickCount();HRESULT result;
    while((result=query->GetData(output,bytes,D3DGETDATA_FLUSH))==S_FALSE){assert(GetTickCount()-start<10000);Sleep(1);}ok(result);
}
static double measure(IDirect3DDevice9* device,IDirect3DPixelShader9* shader,const Vertex* quad){
    IDirect3DQuery9 *frequency=nullptr,*start=nullptr,*finish=nullptr,*disjoint=nullptr;
    ok(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));
    ok(device->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&finish));ok(device->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
    ok(device->SetPixelShader(shader));ok(device->BeginScene());
    for(unsigned n=0;n<4;++n)ok(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));
    ok(disjoint->Issue(D3DISSUE_BEGIN));ok(frequency->Issue(D3DISSUE_END));ok(start->Issue(D3DISSUE_END));
    constexpr unsigned repeats=32;for(unsigned n=0;n<repeats;++n)ok(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));
    ok(finish->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));ok(device->EndScene());
    UINT64 hz=0,a=0,b=0;BOOL invalid=TRUE;wait(finish,&b,sizeof b);wait(start,&a,sizeof a);wait(frequency,&hz,sizeof hz);wait(disjoint,&invalid,sizeof invalid);
    assert(!invalid&&hz&&b>=a);for(auto* query:{frequency,start,finish,disjoint})query->Release();return double(b-a)*1000/double(hz)/repeats;
}
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    assert(argc==3);const bool timing=std::strcmp(argv[2],"benchmark")==0;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightTerrainCoverage";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);
    auto* api=create(D3D_SDK_VERSION);assert(api);IDirect3DDevice9* device=nullptr;
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device));
    ok(device->SetDepthStencilSurface(nullptr));ok(device->SetVertexShader(nullptr));
    ok(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX6|D3DFVF_TEXCOORDSIZE4(5)));
    for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})ok(device->SetRenderState(state,FALSE));
    ok(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(device->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    unsigned cases=0;const auto native=original();
    IDirect3DPixelShader9* nativeShader=nullptr;ok(device->CreatePixelShader(reinterpret_cast<const DWORD*>(native.data()),&nativeShader));drop(nativeShader);
    for(unsigned sources:{1u,2u}){
        std::vector<Word> code[2];NorthlightTerrainCoverageBefore::PixelInfo oldInfo;PixelInfo newInfo;
        assert(NorthlightTerrainCoverageBefore::pixel(native.data(),native.size(),code[0],oldInfo,sources));assert(pixel(native.data(),native.size(),code[1],newInfo,sources));assert(oldInfo.constants==newInfo.constants);
        IDirect3DPixelShader9* shaders[2]{};for(unsigned i=0;i<2;++i){
            const auto name=std::to_string(sources)+"-"+std::to_string(i)+".bin";FILE* output=std::fopen(name.c_str(),"wb");assert(output);std::fwrite(code[i].data(),4,code[i].size(),output);std::fclose(output);
            HRESULT created=device->CreatePixelShader(reinterpret_cast<const DWORD*>(code[i].data()),&shaders[i]);
            if(i==0&&created==D3DERR_INVALIDCALL){
                // The native runtime rejects raw SUB in the old patch. Keep
                // every old operation, encoding subtraction as ADD/NEG only.
                Program parsed;assert(analyse(code[i].data(),code[i].size(),parsed));unsigned normalized=0;
                for(const auto& op:parsed.ops)if(op.code==3){assert(op.size==3&&!(code[i][op.at+3]&0x0f000000u));code[i][op.at]=(code[i][op.at]&~65535u)|2u;code[i][op.at+3]|=0x01000000u;++normalized;}
                assert(normalized==3*sources);std::printf("REFERENCE_SUB_NORMALIZED sources=%u instructions=%u\n",sources,normalized);
                created=device->CreatePixelShader(reinterpret_cast<const DWORD*>(code[i].data()),&shaders[i]);
            }
            ok(created);}
        for(unsigned samples:{0u,2u})for(D3DFORMAT format:{D3DFMT_A16B16G16R16F,D3DFMT_A32B32G32R32F}){
            if(samples&&format==D3DFMT_A32B32G32R32F)continue;
            const UINT width=timing?3840:257,height=timing?2160:129;const unsigned bytes=format==D3DFMT_A16B16G16R16F?8:16;
            IDirect3DSurface9 *target=nullptr,*resolved=nullptr,*staging=nullptr;
            ok(device->CreateRenderTarget(width,height,format,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&target,nullptr));
            ok(device->CreateRenderTarget(width,height,format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
            ok(device->CreateOffscreenPlainSurface(width,height,format,D3DPOOL_SYSTEMMEM,&staging,nullptr));
            ok(device->SetRenderTarget(0,target));D3DVIEWPORT9 viewport={0,0,width,height,0,1};ok(device->SetViewport(&viewport));
            const unsigned poses=timing?3:39;
            for(unsigned pose=0;pose<poses;++pose){
                float rows[6][4]{};for(unsigned source=0;source<sources;++source){for(unsigned axis=0;axis<3;++axis)rows[source*3+axis][axis]=1;
                    rows[source*3][3]=source?.47f:0;rows[source*3+1][3]=source?-.29f:0;}
                Vertex quad[4]{};
                for(unsigned i=0;i<4;++i){const float u=i&1?1.f:0.f,v=i&2?1.f:0.f;quad[i].x=u*width-.5f;quad[i].y=v*height-.5f;quad[i].w=1;quad[i].color=0x5799bbddu;
                    quad[i].position[0]=(u*2-1)*(pose%3==0?.8f:1.2f);quad[i].position[1]=(v*2-1)*1.2f;quad[i].position[2]=pose%3==2?1.2f:.5f;quad[i].position[3]=1;}
                // Uniform exact bounds and their adjacent floats; the other
                // source is moved outside so it cannot hide a wrong predicate.
                if(!timing&&pose>=3){unsigned index=pose-3,axis=index/12;const float limits[][2]={{-.9949765625f,.9930234375f},{-.9930234375f,.9949765625f},{0,1}};
                    float boundary=limits[axis][index/6%2];const unsigned neighbor=index%3;float value=neighbor==0?std::nextafter(boundary,-INFINITY):neighbor==1?boundary:std::nextafter(boundary,INFINITY);
                    for(auto& vertex:quad){vertex.position[0]=vertex.position[1]=0;vertex.position[2]=.5f;vertex.position[axis]=value;}
                    for(unsigned source=1;source<sources;++source)rows[source*3][3]=3;
                }
                ok(device->SetPixelShaderConstantF(newInfo.constants,rows[0],sources*3));
                if(timing){std::array<double,6> time[2];for(unsigned block=0;block<6;++block)for(unsigned step=0;step<2;++step){const unsigned mode=step^(block&1);time[mode][block]=measure(device,shaders[mode],quad);}
                    for(auto& values:time)std::sort(values.begin(),values.end());std::printf("TIMING sources=%u msaa=%u bits=%u pose=%u beforeMs=%.6f afterMs=%.6f\n",sources,samples,bytes*2,pose,(time[0][2]+time[0][3])*.5,(time[1][2]+time[1][3])*.5);
                }else{std::vector<unsigned char> image[2];for(unsigned mode=0;mode<2;++mode){ok(device->SetPixelShader(shaders[mode]));ok(device->BeginScene());ok(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(device->EndScene());
                        if(samples)ok(device->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));ok(device->GetRenderTargetData(samples?resolved:target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
                        image[mode].resize(size_t(width)*height*bytes);for(unsigned y=0;y<height;++y)std::memcpy(image[mode].data()+size_t(y)*width*bytes,(char*)lock.pBits+size_t(y)*lock.Pitch,width*bytes);ok(staging->UnlockRect());}
                    size_t changed=0;for(size_t at=0;at<image[0].size();++at)changed+=image[0][at]!=image[1][at];
                    if(changed)std::printf("IMAGE_MISMATCH sources=%u msaa=%u bits=%u pose=%u changedBytes=%zu\n",sources,samples,bytes*2,pose,changed);
                    assert(!changed);
                    if(pose<2){bool varied=false;for(size_t at=bytes;at<image[0].size();at+=bytes)varied|=std::memcmp(image[0].data(),image[0].data()+at,bytes)!=0;assert(varied);}
                    ++cases;}
            }
            drop(staging);drop(resolved);drop(target);
        }
        for(auto*& shader:shaders)drop(shader);
    }
    device->Release();api->Release();DestroyWindow(window);std::printf("TERRAIN_COVERAGE_GPU_PASS cases=%u timing=%u\n",cases,unsigned(timing));
}
