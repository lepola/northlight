// Genuine old/new production material consumers on a hidden D3D9 device.
#define main northlight_unused_distance_main
#include "composite_distance_gpu.cpp"
#undef main

static Image halfPixel(std::uint16_t r,std::uint16_t g,std::uint16_t b,std::uint16_t a) {
    Image im{1,1,D3DFMT_A16B16G16R16F,8,std::vector<unsigned char>(8)};
    const std::uint16_t values[]={r,g,b,a};std::memcpy(im.pixels.data(),values,8);return im;
}
static Image scalar(float value) {
    Image im{1,1,D3DFMT_R32F,4,std::vector<unsigned char>(4)};std::memcpy(im.pixels.data(),&value,4);return im;
}
int main(int argc,char** argv) {
    assert(argc==6||argc==7);const std::string folder=argv[1],entry=argv[2];
    const unsigned W=std::strtoul(argv[4],nullptr,10),H=std::strtoul(argv[5],nullptr,10);
    const unsigned samples=argc>6?std::strtoul(argv[6],nullptr,10):0;
    const bool local=entry=="LocalLighting",helper=entry=="BumpHelper",audit=entry=="MaterialAudit",bench=W>256;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightMaterialPostRejection";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[3]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* shaders[2]{};for(unsigned v=0;v<2;++v){auto code=read(folder+"/"+(v?"new/":"old/")+entry+".bin");ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shaders[v]));}
    IDirect3DCubeTexture9* cube=nullptr;ok(d->CreateCubeTexture(4,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&cube,nullptr));for(unsigned face=0;face<6;++face){D3DLOCKED_RECT l{};ok(cube->LockRect(D3DCUBEMAP_FACES(face),0,&l,nullptr,0));for(unsigned y=0;y<4;++y)std::memset(static_cast<char*>(l.pBits)+y*l.Pitch,0,16);ok(cube->UnlockRect(D3DCUBEMAP_FACES(face),0));}
    for(unsigned s=0;s<16;++s){for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,k,s==0||s==4||s==5||s==10||s==12?D3DTEXF_LINEAR:D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto k:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW})ok(d->SetSamplerState(s,k,D3DTADDRESS_CLAMP));}
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));for(auto k:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(k,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    IDirect3DQuery9 *event=nullptr,*start=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
    auto draw=[&](unsigned count){ok(d->BeginScene());for(unsigned n=0;n<count;++n)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());};
    for(unsigned shape=0;shape<12;++shape){
        const float z=shape==2||shape==11?96.f:shape==6?42.f:shape==7?48.f:4.f,clipNear=.2f,clipFar=1277.f;
        float c[116][4]{};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=clipNear;c[0][3]=clipFar;c[1][0]=1.3f;c[1][1]=1.9f;c[1][2]=1;c[2][0]=1;
        const float angle=float(shape%3)*.7f;c[3][0]=std::cos(angle);c[3][1]=std::sin(angle);c[4][0]=-std::sin(angle);c[4][1]=std::cos(angle);c[5][2]=c[6][3]=1;
        c[16][0]=.6f;c[16][1]=.8f;c[16][3]=1;c[17][0]=.4f;c[17][1]=.6f;c[17][2]=.8f;c[18][0]=c[18][1]=c[18][2]=.2f;c[18][3]=.8f;c[19][0]=c[19][1]=c[19][2]=.04f;
        c[20][2]=1;c[20][3]=1;c[25][1]=1;c[25][2]=1;c[27][0]=1;c[27][1]=1;c[28][0]=.8f;c[28][1]=.6f;c[29][0]=c[29][1]=c[29][2]=.4f;
        c[30][0]=shape==1;c[30][2]=1;c[33][0]=W;c[33][1]=H;c[33][2]=std::max(1u,W/2);c[33][3]=std::max(1u,H/2);c[35][0]=1.f/1000;
        c[52][1]=.8f;c[73][0]=shape==8||shape==9?0.f:.75f;c[73][1]=48;c[73][3]=audit?1.f:0.f;c[78][0]=shape==9||shape==10;c[78][1]=8;
        c[74][0]=c[75][1]=1;c[76][0]=.8f;c[76][1]=.5f;c[76][2]=.3f;c[77][0]=.3f;c[77][1]=.5f;c[77][2]=.8f;
        for(unsigned i=0;i<16;++i){float p[4]={-1.f+float(i%4),3.f+float(i%3),1.f,200.f};float rgb[4]={.05f,.07f,.09f,1.f/199};if(i<8){std::memcpy(c[36+i],p,16);std::memcpy(c[44+i],rgb,16);}std::memcpy(c[84+i],p,16);std::memcpy(c[100+i],rgb,16);}
        const float depth=shape==0?1.f:clipFar/(clipFar-clipNear)*(1-clipNear/z);
        std::vector<IDirect3DTexture9*> textures;auto bind=[&](unsigned slot,const Image& im){auto t=texture(d,im);textures.push_back(t);ok(d->SetTexture(slot,t));};
        const auto normal=halfPixel(shape==11?0x38c0:0x38cd,0x3a66,0,0x3c00);
        const std::uint16_t zbits=z==96?0x5600:z==48?0x5200:z==42?0x5140:0x4400;
        const auto detail=halfPixel(0x3800,0x38cd,shape==5?0x4000:zbits,shape==4?0:0x3c00),water=halfPixel(0x4000,0x3800,0,0);
        bind(0,halfPixel(0x4200,0x3800,0x3400,0x3c00));bind(1,scalar(depth));bind(2,scalar(1));bind(3,scalar(1));bind(4,halfPixel(0,0,0,0x3800));bind(5,halfPixel(0x4200,0x3c00,0x3800,0x3c00));bind(6,halfPixel(0x3800,0x3a00,0x3800,0x3c00));bind(8,halfPixel(0,0,0,0x3c00));bind(9,detail);bind(10,halfPixel(0,0,0,0x3a66));bind(11,water);bind(12,halfPixel(0x3800,0x3a00,0x3400,0x3c00));bind(14,normal);
        if(local){float bank[13][4]{};std::memcpy(bank,c,7*16);bank[2][1]=c[30][0];bank[2][3]=.025f;bank[7][0]=-1;bank[7][1]=3;bank[7][2]=1;bank[7][3]=1.f/199;bank[8][0]=.5f;bank[8][1]=.7f;bank[8][2]=.9f;bank[8][3]=200;bank[9][0]=.1f;bank[9][1]=200;bank[9][2]=.25f;bank[9][3]=1;bank[10][0]=.8f;bank[10][2]=1;std::memcpy(bank[12],c[73],16);std::memcpy(c,bank,sizeof bank);bind(0,scalar(depth));ok(d->SetTexture(1,cube));bind(2,water);bind(3,normal);bind(4,halfPixel(0,0,0,0x3800));bind(5,detail);}
        ok(d->SetPixelShaderConstantF(0,c[0],116));
        for(auto format:{D3DFMT_A16B16G16R16F,D3DFMT_A32B32G32R32F,D3DFMT_A8R8G8B8}){
            if(bench&&format!=D3DFMT_A16B16G16R16F)continue;
            if(format==D3DFMT_A8R8G8B8&&entry!="MaterialComposite")continue;
            if(samples&&FAILED(api->CheckDeviceMultiSampleType(0,D3DDEVTYPE_HAL,format,TRUE,D3DMULTISAMPLE_TYPE(samples),nullptr)))continue;
            IDirect3DSurface9 *target=nullptr,*staging=nullptr,*resolved=nullptr;ok(d->CreateRenderTarget(W,H,format,D3DMULTISAMPLE_TYPE(samples),0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(W,H,format,D3DPOOL_SYSTEMMEM,&staging,nullptr));if(samples)ok(d->CreateRenderTarget(W,H,format,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp={0,0,W,H,0,1};ok(d->SetViewport(&vp));
            const unsigned bytes=format==D3DFMT_A16B16G16R16F?8:format==D3DFMT_A8R8G8B8?4:16;std::vector<unsigned char> output[2];std::vector<double> timing[2];
            for(unsigned block=0;block<(bench?16u:2u);++block){const unsigned mode=(block/2)%2?1-block%2:block%2;ok(d->SetPixelShader(shaders[mode]));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff808080,1,0));draw(2);wait(event);
                ok(disjoint->Issue(D3DISSUE_BEGIN));ok(start->Issue(D3DISSUE_END));draw(bench?8:1);ok(end->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);UINT64 a=0,b=0,f=0;BOOL bad=FALSE;assert(start->GetData(&a,sizeof a,0)==S_OK&&end->GetData(&b,sizeof b,0)==S_OK&&frequency->GetData(&f,sizeof f,0)==S_OK&&disjoint->GetData(&bad,sizeof bad,0)==S_OK&&!bad&&b>=a&&f);if(block>=4)timing[mode].push_back(double(b-a)/double(f)*1000/8);
                if(block<2){if(resolved)ok(d->StretchRect(target,nullptr,resolved,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(resolved?resolved:target,staging));D3DLOCKED_RECT l{};ok(staging->LockRect(&l,nullptr,D3DLOCK_READONLY));output[mode].resize(std::size_t(W)*H*bytes);for(unsigned y=0;y<H;++y)std::memcpy(output[mode].data()+std::size_t(y)*W*bytes,static_cast<char*>(l.pBits)+y*l.Pitch,W*bytes);ok(staging->UnlockRect());}
            }
            std::size_t changes=0,nonfinite=0;for(std::size_t i=0;i<output[0].size();++i)changes+=output[0][i]!=output[1][i];for(std::size_t i=0;i<std::size_t(W)*H*4;++i){float x;if(bytes==8){std::uint16_t bits;std::memcpy(&bits,output[1].data()+i*2,2);x=half(bits);}else if(bytes==4)x=output[1][i]/255.f;else std::memcpy(&x,output[1].data()+i*4,4);nonfinite+=!std::isfinite(x);}
            double median[2]{};for(unsigned v=0;v<2;++v)if(!timing[v].empty()){std::sort(timing[v].begin(),timing[v].end());median[v]=timing[v][timing[v].size()/2];}
            std::printf("{\"entry\":\"%s\",\"width\":%u,\"height\":%u,\"shape\":%u,\"format\":%u,\"msaa\":%u,\"changed_bytes\":%llu,\"nonfinite\":%llu,\"old_ms\":%.6f,\"new_ms\":%.6f}\n",entry.c_str(),W,H,shape,unsigned(format),samples,(unsigned long long)changes,(unsigned long long)nonfinite,median[0],median[1]);std::fflush(stdout);assert(!changes&&!nonfinite);if(resolved)resolved->Release();staging->Release();target->Release();
        }
        for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));for(auto t:textures)t->Release();
    }
    for(auto shader:shaders)shader->Release();for(auto query:{event,start,end,frequency,disjoint})query->Release();cube->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
