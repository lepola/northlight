// Production AO -> FP16 bilateral filter -> both full-resolution composites.
#define main northlight_unused_composite_main
#include "composite_distance_gpu.cpp"
#undef main
#include <iomanip>

struct Target {
    IDirect3DTexture9* texture=nullptr;
    IDirect3DSurface9 *surface=nullptr,*staging=nullptr;
    unsigned width,height;
    Target(IDirect3DDevice9* d,unsigned w,unsigned h):width(w),height(h){
        ok(d->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&texture,nullptr));
        ok(texture->GetSurfaceLevel(0,&surface));
        ok(d->CreateOffscreenPlainSurface(w,h,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    }
    ~Target(){staging->Release();surface->Release();texture->Release();}
    void draw(IDirect3DDevice9* d,IDirect3DPixelShader9* shader){
        ok(d->SetRenderTarget(0,surface));D3DVIEWPORT9 vp={0,0,width,height,0,1};ok(d->SetViewport(&vp));
        const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{width-.5f,-.5f,0,1,1,0},{-.5f,height-.5f,0,1,0,1},{width-.5f,height-.5f,0,1,1,1}};
        ok(d->SetPixelShader(shader));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());
    }
    void save(IDirect3DDevice9* d,const std::string& path,unsigned component,float scale=1){
        ok(d->GetRenderTargetData(surface,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::ofstream out(path,std::ios::binary);assert(out);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
            std::uint16_t bits;std::memcpy(&bits,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*8+component*2,2);
            float v=half(bits)*scale;assert(std::isfinite(v)&&v>=0&&v<=1.001f);out.write(reinterpret_cast<char*>(&v),4);
        }
        ok(staging->UnlockRect());assert(out.good());
    }
};

int main(int argc,char** argv){
    assert(argc==4);const std::string compiled=std::string(argv[1])+"/";
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightAOPipeline";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[2]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
    for(unsigned s=0;s<16;++s){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,s==0||s==10?D3DTEXF_LINEAR:D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    auto shader=[&](std::string name){auto code=read(compiled+name+".bin");IDirect3DPixelShader9* s=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&s));return s;};
    auto blur=shader("AOBlur"),legacy=shader("Composite"),world=shader("WorldComposite");
    const char* methods[]={"AO","HDAO","HBAOPlus","GTAO","ASSAO"};const char* qualities[]={"","Medium","Low"};
    IDirect3DPixelShader9* kernels[5][3]{};for(unsigned m=0;m<5;++m)for(unsigned q=0;q<3;++q)kernels[m][q]=shader(std::string(methods[m])+"Only"+(m?qualities[q]:""));
    auto constant=[&](float r,float g,float b,float a){Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};const float v[]={r,g,b,a};std::memcpy(im.pixels.data(),v,16);return texture(d,im);};
    auto scene=constant(.5,.5,.5,1),zero=constant(0,0,0,0),neutral=constant(0,0,0,1);
    std::ifstream cases(argv[3]);assert(cases);std::string prefix;unsigned W,H,water;float radius,strength;
    unsigned count=0;
    while(cases>>std::quoted(prefix)>>W>>H>>radius>>strength>>water){
        const unsigned AW=W/2,AH=H/2;const float nearZ=.2f,farZ=1000,part=.94f,sx=1.3f,sy=1.9f;
        auto pixels=read(prefix+".depth.f32");assert(pixels.size()==size_t(W)*H*4);
        Image depthImage{W,H,D3DFMT_R32F,4,pixels},distanceImage{AW,AH,D3DFMT_R32F,4,std::vector<unsigned char>(size_t(AW)*AH*4)};
        for(unsigned y=0;y<AH;++y)for(unsigned x=0;x<AW;++x){
            unsigned dx=std::min(unsigned((x+.5f)/AW*W),W-1),dy=std::min(unsigned((y+.5f)/AH*H),H-1);float raw;std::memcpy(&raw,pixels.data()+(size_t(dy)*W+dx)*4,4);
            float z=nearZ*farZ/std::max(farZ-std::min(raw/part,1.f)*(farZ-nearZ),.00001f);std::memcpy(distanceImage.pixels.data()+(size_t(y)*AW+x)*4,&z,4);
        }
        auto depth=texture(d,depthImage),distance=texture(d,distanceImage),mask=constant(1,1,0,0);
        Target raw(d,AW,AH),filtered(d,AW,AH),full(d,W,H);
        for(unsigned m=0;m<5;++m)for(unsigned q=0;q<3;++q){
            const std::string name=prefix+"."+methods[m]+(q?qualities[q]:"High");
            float c[80][4]{};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=nearZ;c[0][3]=farZ;c[1][0]=sx;c[1][1]=sy;c[1][2]=strength;c[2][1]=radius;c[3][1]=1/part;c[3][2]=part;c[4][0]=float(water);
            ok(d->SetPixelShaderConstantF(0,c[0],80));for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));
            ok(d->SetTexture(0,scene));ok(d->SetTexture(1,depth));ok(d->SetTexture(3,mask));raw.draw(d,kernels[m][q]);raw.save(d,name+".raw.f32",3);
            ok(d->SetTexture(0,raw.texture));ok(d->SetTexture(2,raw.texture));filtered.draw(d,blur);filtered.save(d,name+".blur.f32",3);
            ok(d->SetTexture(0,scene));ok(d->SetTexture(2,filtered.texture));c[2][2]=1;ok(d->SetPixelShaderConstantF(2,c[2],1));full.draw(d,legacy);full.save(d,name+".legacy.f32",0);
            std::memset(c,0,sizeof c);c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=nearZ;c[0][3]=farZ;c[1][0]=sx;c[1][1]=sy;c[1][2]=1;c[2][0]=1/part;c[30][0]=float(water);c[33][0]=float(W);c[33][1]=float(H);c[33][2]=float(AW);c[33][3]=float(AH);
            for(unsigned i=0;i<4;++i)c[3+i][i]=1;
            ok(d->SetPixelShaderConstantF(0,c[0],80));for(unsigned s=2;s<16;++s)ok(d->SetTexture(s,zero));
            ok(d->SetTexture(9,neutral));ok(d->SetTexture(10,filtered.texture));ok(d->SetTexture(11,mask));ok(d->SetTexture(15,distance));full.draw(d,world);full.save(d,name+".world.f32",0,2);
            ++count;
        }
        for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));depth->Release();distance->Release();mask->Release();
    }
    assert(cases.eof()&&count);std::printf("AO_PIPELINE_GPU_PASS cases=%u stages=4 game_launched=false\n",count);
    d->SetPixelShader(nullptr);for(auto& method:kernels)for(auto s:method)s->Release();blur->Release();legacy->Release();world->Release();scene->Release();zero->Release();neutral->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
