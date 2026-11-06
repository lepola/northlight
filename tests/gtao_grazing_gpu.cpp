// Production AO bytecode on an independent hidden device; no game process.
#define main northlight_unused_composite_main
#include "composite_distance_gpu.cpp"
#undef main

int main(int argc,char** argv){
    assert(argc==4);
    constexpr unsigned W=640,H=360,AW=W/2,AH=H/2;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightGtaoGrazing";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[2]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* shader=nullptr;const auto code=read(argv[1]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shader));
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(AW,AH,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(AW,AH,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetPixelShader(shader));
    D3DVIEWPORT9 vp={0,0,AW,AH,0,1};ok(d->SetViewport(&vp));
    for(unsigned s=0;s<4;++s){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    auto scene=texture(d,{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)});ok(d->SetTexture(0,scene));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{AW-.5f,-.5f,0,1,1,0},{-.5f,AH-.5f,0,1,0,1},{AW-.5f,AH-.5f,0,1,1,1}};
    FILE* report=std::fopen(argv[3],"w");assert(report);std::fprintf(report,"{\"cases\":[");unsigned cases=0,failures=0;
    for(unsigned shape=0;shape<4;++shape)for(float height:{.5f,2.f,8.f})for(float slope:{-.15f,0.f,.05f,.15f}){
        const float clipNear=.2f,clipFar=1000.f,sx=1.3f,sy=1.9f,part=.94f;
        Image depths{W,H,D3DFMT_R32F,4,std::vector<unsigned char>(W*H*4)};std::vector<float> zs(W*H);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            const float rx=(2*(x+.5f)/W-1)/sx,ry=(1-2*(y+.5f)/H)/sy;
            float z=clipFar;
            // Convex terrain is an intersection of lower half-spaces. All
            // surface points lie below every face's tangent: no local AO.
            const unsigned faces=shape==0?1:5;
            for(unsigned face=0;face<faces;++face){
                float mx=shape==2?(int(face)-2)*.035f:0;
                float my=-slope-(shape==1?face*.035f:0);
                float b=-height+(shape==1?face*(face+1)*.035f*2:shape==2?float((int(face)-2)*(int(face)-2))*.10f:0);
                const float denom=ry-my-mx*rx;
                const float hit=b/denom;
                bool visible=hit>clipNear&&hit<clipFar;
                for(unsigned other=0;other<faces&&visible;++other){
                    float ox=shape==2?(int(other)-2)*.035f:0;
                    float oy=-slope-(shape==1?other*.035f:0);
                    float ob=-height+(shape==1?other*(other+1)*.035f*2:shape==2?float((int(other)-2)*(int(other)-2))*.10f:0);
                    visible=ry*hit<=ob+oy*hit+ox*rx*hit+1e-4f;
                }
                if(visible)z=std::min(z,hit);
            }
            if(shape==3){
                // A genuine concave seam must retain contact shading.
                const float den=ry+slope-.45f*std::fabs(rx);
                z=den<0?-height/den:clipFar;
            }
            if(!(z>clipNear&&z<clipFar))z=clipFar;zs[y*W+x]=z;
            const float nd=clipFar/(clipFar-clipNear)*(1-clipNear/z);
            const float depth=std::round(double(nd)*part*16777215.0)/16777215.0;
            std::memcpy(depths.pixels.data()+(y*W+x)*4,&depth,4);
        }
        auto depth=texture(d,depths);ok(d->SetTexture(1,depth));
        float c[7][4]{};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=clipNear;c[0][3]=clipFar;c[1][0]=sx;c[1][1]=sy;c[1][2]=.6f;c[2][1]=2;c[3][1]=1/part;c[3][2]=part;ok(d->SetPixelShaderConstantF(0,c[0],7));
        ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));
        D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));float minimum=1,maximum=0;double sum=0;unsigned count=0,dark=0,wx=0,wy=0;
        for(unsigned y=AH/2+8;y<AH-8;++y)for(unsigned x=8;x<AW-8;++x){
            const float z=zs[(y*2+1)*W+x*2+1];if(z>150||z<clipNear)continue;
            // Mixed sky/terrain texels are silhouettes, not planar receivers.
            const unsigned i=(y*2+1)*W+x*2+1;
            if(zs[i-1]>=clipFar||zs[i+1]>=clipFar||zs[i-W]>=clipFar||zs[i+W]>=clipFar)continue;
            float a;std::memcpy(&a,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16+12,4);assert(std::isfinite(a));if(a<minimum){minimum=a;wx=x;wy=y;}maximum=std::max(maximum,a);sum+=a;++count;dark+=a<.99f;
        }
        assert(count);ok(staging->UnlockRect());
        std::fprintf(report,"%s{\"shape\":%u,\"height\":%.2f,\"slope\":%.2f,\"min\":%.7f,\"max\":%.7f,\"mean\":%.7f,\"dark\":%u,\"pixels\":%u,\"worstX\":%u,\"worstY\":%u}",cases?",":"",shape,height,slope,minimum,maximum,sum/count,dark,count,wx,wy);++cases;
        if(shape<3&&minimum<.98f)++failures;
        ok(d->SetTexture(1,nullptr));depth->Release();
    }
    std::fprintf(report,"],\"case_count\":%u,\"failures\":%u}\n",cases,failures);std::fclose(report);std::printf("AO grazing GPU: %u cases, convex/plane failures=%u\n",cases,failures);
    ok(d->SetTexture(0,nullptr));scene->Release();d->SetPixelShader(nullptr);shader->Release();staging->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
