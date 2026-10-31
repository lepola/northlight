// Real production bytecode on an independent, hidden D3D9 device.
#define main northlight_unused_composite_main
#include "composite_distance_gpu.cpp"
#undef main
#include <functional>
#include "quality_settings.h"

static Image constant(float r,float g,float b,float a){
    Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};
    const float v[]={r,g,b,a};std::memcpy(im.pixels.data(),v,16);return im;
}
static void settingsCheck(){
    using namespace NorthlightQuality;
    for(unsigned method=0;method<4;++method){
        const char* names[]={"SSAO","hdao","HBAO+","GTAO"};
        for(const auto& text:{std::string(names[method]),std::to_string(method)}){
            std::istringstream in("[Quality]\nAOMethod="+text+"\nAOStrength=125\nAORadius=350\n");
            std::vector<std::string> problems;auto s=load(&in,nullptr,problems);
            assert(problems.empty()&&s.aoMethod==method&&s.aoStrength==125&&s.aoRadius==350);
        }
    }
    for(const char* value:{"VXAO","4","-1","GTAOabc"}){
        std::istringstream in(std::string("[Quality]\nAOMethod=")+value+"\nAOStrength=201\nAORadius=0\n");
        std::vector<std::string> problems;assert(load(&in,nullptr,problems)==Settings{}&&problems.size()==3);
    }
    assert(preset(Preset::Quality).aoMethod==0&&preset(Preset::Balanced).aoMethod==0&&preset(Preset::Performance).aoMethod==0);
}
int main(int argc,char** argv){
    assert(argc==4||argc==5);settingsCheck();
    const std::string quality=argc==5?argv[4]:"";
    assert(quality.empty()||quality=="Medium"||quality=="Low");
    constexpr unsigned W=256,H=192,AW=W/2,AH=H/2;
    const char* names[]={"AO","HDAO","HBAOPlus","GTAO"};
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightAOMethods";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[2]);assert(module);
    auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);
    auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* shaders[4][2]{};
    for(unsigned m=0;m<4;++m)for(unsigned variant=0;variant<2;++variant){
        std::string path=std::string(argv[1])+"/"+(variant?std::string(names[m])+"ContactBloom"+(m?quality:""):"AO")+".bin";
        auto code=read(path);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shaders[m][variant]));
    }
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;
    ok(d->CreateRenderTarget(AW,AH,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    ok(d->CreateOffscreenPlainSurface(AW,AH,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
    D3DVIEWPORT9 vp={0,0,AW,AH,0,1};ok(d->SetViewport(&vp));
    for(unsigned s=0;s<4;++s){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    auto scene=texture(d,constant(1,1,1,1));ok(d->SetTexture(0,scene));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{AW-.5f,-.5f,0,1,1,0},{-.5f,AH-.5f,0,1,0,1},{AW-.5f,AH-.5f,0,1,1,1}};
    unsigned failures=0,cases=0;std::vector<float> corner[4];
    FILE* report=std::fopen(argv[3],"w");assert(report);std::fprintf(report,"{\"cases\":[");
    for(unsigned shape=0;shape<6;++shape)for(unsigned strength=0;strength<2;++strength){
        const float clipNear=.2f,clipFar=1000.f,sx=1.3f,sy=1.9f;
        Image depths{W,H,D3DFMT_R32F,4,std::vector<unsigned char>(W*H*4)};
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
            float rx=(2*(x+.5f)/W-1)/sx,ry=(1-2*(y+.5f)/H)/sy,z=4;
            if(shape==1)z=4/(1+.4f*rx+.25f*ry);
            if(shape==2)z=4/(1+std::fabs(rx)*1.4f);
            if(shape==3&&x>W/3&&x<W*2/3&&y>H/3&&y<H*2/3)z=1;
            float depth=shape==4?1:clipFar/(clipFar-clipNear)*(1-clipNear/z);
            std::memcpy(depths.pixels.data()+(y*W+x)*4,&depth,4);
        }
        auto depth=texture(d,depths),water=texture(d,constant(1,1,0,0));ok(d->SetTexture(1,depth));ok(d->SetTexture(3,water));
        float c[7][4]{};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=clipNear;c[0][3]=clipFar;
        c[1][0]=sx;c[1][1]=sy;c[1][2]=strength?.6f:0;c[1][3]=.12f;c[2][0]=.08f;c[2][1]=2;
        c[3][1]=c[3][2]=1;c[4][0]=shape==5?1:0;ok(d->SetPixelShaderConstantF(0,c[0],7));
        for(unsigned m=0;m<4;++m)for(unsigned variant=0;variant<2;++variant){
            ok(d->SetPixelShader(shaders[m][variant]));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));
            if(!variant&&m){ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_ALPHA));ok(d->SetPixelShader(shaders[m][1]));ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));}
            ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));
            D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));double sum=0;float minimum=1,maximum=0;unsigned count=0;
            for(unsigned y=4;y<AH-4;++y)for(unsigned x=4;x<AW-4;++x){
                float pixel[4];std::memcpy(pixel,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,16);
                bool bad=false;for(float v:pixel)bad|=!std::isfinite(v);
                bad|=pixel[3]<-1e-5f||pixel[3]>1.00001f;
                if(!strength||shape==0||shape==4||shape==5)bad|=std::fabs(pixel[3]-1)>1e-4f;
                if(shape==1)bad|=pixel[3]<.98f;
                if(variant)for(unsigned k=0;k<3;++k)bad|=std::fabs(pixel[k]-.08f)>1e-5f;
                if(bad&&failures++<6)std::printf("FAIL method=%s shape=%u strength=%u variant=%u xy=%u,%u alpha=%f\n",names[m],shape,strength,variant,x,y,pixel[3]);
                minimum=std::min(minimum,pixel[3]);maximum=std::max(maximum,pixel[3]);sum+=pixel[3];++count;
            }
            if(shape==2&&strength&&variant){corner[m].resize(AW*AH);for(unsigned y=0;y<AH;++y)for(unsigned x=0;x<AW;++x){float a;std::memcpy(&a,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16+12,4);corner[m][y*AW+x]=a;}}
            ok(staging->UnlockRect());if(shape==2&&strength&&minimum>.995f){std::printf("FAIL no corner AO method=%s min=%f\n",names[m],minimum);++failures;}
            std::fprintf(report,"%s{\"method\":\"%s\",\"shape\":%u,\"strength\":%u,\"variant\":%u,\"min\":%.6f,\"max\":%.6f,\"mean\":%.6f}",cases?",":"",names[m],shape,strength,variant,minimum,maximum,sum/count);++cases;
        }
        ok(d->SetTexture(1,nullptr));ok(d->SetTexture(3,nullptr));depth->Release();water->Release();
    }
    {
        // Regression cases for the HBAO+ receiver orientation and the GTAO screen-radius clamp.
        const float clipNear=.2f,clipFar=1000.f;
        auto run=[&](float sx,float sy,unsigned method,const std::function<float(float,float)>& zOf,std::vector<float>& alpha){
            Image depths{W,H,D3DFMT_R32F,4,std::vector<unsigned char>(W*H*4)};
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){
                float z=zOf((2*(x+.5f)/W-1)/sx,(1-2*(y+.5f)/H)/sy);if(!(z>clipNear&&z<clipFar))z=clipFar;
                float depth=clipFar/(clipFar-clipNear)*(1-clipNear/z);std::memcpy(depths.pixels.data()+(y*W+x)*4,&depth,4);
            }
            auto depth=texture(d,depths),water=texture(d,constant(1,1,0,0));ok(d->SetTexture(1,depth));ok(d->SetTexture(3,water));
            float c[7][4]{};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=clipNear;c[0][3]=clipFar;c[1][0]=sx;c[1][1]=sy;c[1][2]=.6f;c[1][3]=.12f;c[2][0]=.08f;c[2][1]=2;c[3][1]=c[3][2]=1;
            ok(d->SetPixelShaderConstantF(0,c[0],7));ok(d->SetPixelShader(shaders[method][1]));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));
            D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));alpha.resize(AW*AH);
            for(unsigned y=0;y<AH;++y)for(unsigned x=0;x<AW;++x)std::memcpy(&alpha[y*AW+x],static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16+12,4);
            ok(staging->UnlockRect());ok(d->SetTexture(1,nullptr));ok(d->SetTexture(3,nullptr));depth->Release();water->Release();
        };
        // 1. HBAO+ on a wall to the right of the view axis, nearly parallel to it: the visible face points forward in
        // view Z (n.z>0) while facing the eye. A convex crease must stay open, a concave one must keep its occlusion.
        {
            const float angle[2]={6*3.14159265f/180,-24*3.14159265f/180},q[2]={1.2f,3};
            float n[2][2],e[2];for(unsigned i=0;i<2;++i){n[i][0]=-std::cos(angle[i]);n[i][1]=std::sin(angle[i]);e[i]=n[i][0]*q[0]+n[i][1]*q[1];}
            assert(n[0][1]>0&&e[0]<0&&e[1]<0);
            for(unsigned fold=0;fold<2;++fold){
                std::vector<float> alpha;
                auto hit=[&](float rx,unsigned& face){
                    float t[2];for(unsigned i=0;i<2;++i){float dn=n[i][0]*rx+n[i][1];t[i]=dn<0?e[i]/dn:(fold?1e9f:-1e9f);}
                    face=(fold?t[0]<t[1]:t[0]>t[1])?0:1;return t[face]; // concave: union of the solids, convex: intersection
                };
                run(1.3f,1.9f,2,[&](float rx,float){unsigned face;return hit(rx,face);},alpha);
                float minimum=1;unsigned count=0;
                for(unsigned y=4;y<AH-4;++y)for(unsigned x=4;x<AW-4;++x){
                    const float rx=(2*(x+.5f)/AW-1)/1.3f;unsigned face;hit(rx,face);
                    if(std::fabs(rx-q[0]/q[1])>.07f||face)continue; // receivers on the forward-facing wall only
                    minimum=std::min(minimum,alpha[y*AW+x]);++count;
                }
                assert(count);
                std::fprintf(report,",{\"case\":\"hbao_forward_normal_%s\",\"min\":%.6f}",fold?"concave":"convex",minimum);
                const bool bad=fold?minimum>.97f:minimum<.98f;
                if(bad){std::printf("FAIL HBAO+ forward-facing %s min=%f\n",fold?"concave crease lost its occlusion":"convex crease darkened",minimum);++failures;}
                ++cases;
            }
        }
        // 2. GTAO close to the camera with the game's anisotropic projection, where the radius clamp is active:
        // the same V groove along view X and along view Y must shade alike (the sample reach is isotropic in view space).
        {
            const float sx=1.2686f,sy=1.9626f;double mean[2]{};
            for(unsigned axis=0;axis<2;++axis){
                std::vector<float> alpha;
                run(sx,sy,3,[&](float rx,float ry){const float r=axis?ry:rx;return 3/(1+1.4f*std::fabs(r));},alpha); // z+1.4|u|=3
                unsigned count=0;
                for(unsigned y=4;y<AH-4;++y)for(unsigned x=4;x<AW-4;++x){
                    const float rx=(2*(x+.5f)/AW-1)/sx,ry=(1-2*(y+.5f)/AH)/sy,r=axis?ry:rx,z=3/(1+1.4f*std::fabs(r)),u=std::fabs(r)*z;
                    if(u>.25f)continue;mean[axis]+=alpha[y*AW+x];++count;
                }
                assert(count);mean[axis]/=count;
            }
            std::fprintf(report,",{\"case\":\"gtao_clamp_isotropy\",\"grooveX\":%.6f,\"grooveY\":%.6f}",mean[0],mean[1]);
            if(mean[0]>.97||mean[1]>.97||std::fabs(mean[0]-mean[1])>.04){std::printf("FAIL GTAO anisotropic clamp: groove along Y %f vs along X %f\n",mean[1],mean[0]);++failures;}
            ++cases;
        }
    }
    for(unsigned a=0;a<4;++a)for(unsigned b=a+1;b<4;++b){double difference=0;for(unsigned i=0;i<AW*AH;++i)difference+=std::fabs(corner[a][i]-corner[b][i]);if(difference/(AW*AH)<1e-5){std::printf("FAIL identical methods %s %s\n",names[a],names[b]);++failures;}}
    {std::ofstream image(std::string(argv[3])+".pgm",std::ios::binary);image<<"P5\n"<<AW*4<<" "<<AH<<"\n255\n";
     for(unsigned y=0;y<AH;++y)for(unsigned m=0;m<4;++m)for(unsigned x=0;x<AW;++x){unsigned char v=static_cast<unsigned char>(std::max(0.f,std::min(1.f,corner[m][y*AW+x]))*255+.5f);image.write(reinterpret_cast<const char*>(&v),1);}}
    std::fprintf(report,"],\"case_count\":%u,\"failures\":%u,\"settings\":\"PASS\",\"game_launched\":false}\n",cases,failures);std::fclose(report);
    std::printf("AO GPU: %u cases, failures=%u; settings PASS; four distinct kernels\n",cases,failures);
    ok(d->SetTexture(0,nullptr));scene->Release();d->SetPixelShader(nullptr);for(auto& method:shaders)for(auto shader:method)shader->Release();staging->Release();target->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
