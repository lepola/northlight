// Real native draws and production compositors on an independent hidden device.
#define main northlight_unused_composite_replay
#include "composite_distance_gpu.cpp"
#undef main
#include "effect_coverage.h"
#include <array>
using namespace NorthlightEffectCoverage;
#include "effect_coverage_benchmark.h"
static Image solid(float r,float g=0,float b=0,float a=0){
    Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};
    const float p[]={r,g,b,a};std::memcpy(im.pixels.data(),p,16);return im;
}
static float linear(float x){return x<.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);}
int main(int argc,char** argv){
    assert(argc==5);constexpr unsigned N=8;const bool allowMsaa=argv[4][0]=='1';
    assert(classify(false,true,false,0,0,0,15,true)==Write::None);
    assert(classify(true,true,false,0,0,0,15,true)==Write::Clear);
    assert(classify(false,false,true,5,6,1,15,true)==Write::Alpha);
    assert(classify(false,false,true,2,6,1,15,true)==Write::Premultiplied);
    assert(classify(false,false,true,5,2,1,15,true)==Write::Additive);
    assert(classify(true,false,true,5,6,2,15,true)==Write::None);
    // Lit M2/WMO and projected alpha receivers have the same native blend as
    // portals. Blend/depth alone cannot grant an exemption from world lighting.
    for(bool zWrite:{false,true})for(bool overlay:{false,true}){
        const bool protect=overlay&&!zWrite;
        assert(classify(false,zWrite,true,5,6,1,15,overlay)==(protect?Write::Alpha:Write::None));
        assert(classify(true,zWrite,true,5,6,1,15,overlay)==(protect?Write::Alpha:Write::Clear));
        assert(classify(true,zWrite,true,2,6,1,15,overlay)==(protect?Write::Premultiplied:Write::Clear));
        assert(classify(false,zWrite,false,0,0,0,15,overlay)==(protect?Write::Replace:Write::None));
    }
    assert(classify(false,false,true,9,3,1,15,false)==Write::None); // Projected modulate shadow.
    assert(classify(true,false,true,9,3,1,15,false)==Write::Modulate);
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightEffectCoverage";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,N,N,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[2]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=N;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    auto shader=[&](const std::string& path){auto code=read(path);IDirect3DPixelShader9* ps=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&ps));return ps;};
    IDirect3DPixelShader9* native[]={shader(std::string(argv[3])+"/native2.bin"),shader(std::string(argv[3])+"/native3.bin")};
    IDirect3DPixelShader9* nativeMrt[]={shader(std::string(argv[3])+"/native-mrt2.bin"),shader(std::string(argv[3])+"/native-mrt3.bin")};
    {D3DCAPS9 caps{};ok(d->GetDeviceCaps(&caps));std::printf("caps mrt=%lu misc=%08lx\n",caps.NumSimultaneousRTs,caps.PrimitiveMiscCaps);std::fflush(stdout);
     for(unsigned sm=2;sm<=3;++sm){auto bytes=read(std::string(argv[3])+"/native"+std::to_string(sm)+".bin");for(auto write:{Write::Alpha,Write::Premultiplied,Write::Replace,Write::Clear}){std::vector<std::uint32_t> patched;assert(patch(reinterpret_cast<const std::uint32_t*>(bytes.data()),bytes.size()/4,write,patched));IDirect3DPixelShader9* p=nullptr;const HRESULT hr=d->CreatePixelShader(reinterpret_cast<const DWORD*>(patched.data()),&p);if(FAILED(hr)){std::printf("patch sm=%u write=%u\n",sm,unsigned(write));for(auto word:patched)std::printf("%08x ",word);std::puts("");std::fflush(stdout);}ok(hr);p->Release();}}}
    const char* entries[]={"WorldComposite","WorldUnlitComposite","WorldCompositeHdr","WorldUnlitCompositeHdr","MaterialComposite","MaterialCompositeHdr","WorldMaterialCompositeHdr","Composite","HdrLegacyComposite","WorldPrelitComposite","WorldPrelitCompositeHdr"};
    IDirect3DPixelShader9* composites[11]{};for(unsigned i=0;i<11;++i)composites[i]=shader(std::string(argv[1])+"/"+entries[i]+".bin");
    IDirect3DTexture9* scene=nullptr;IDirect3DSurface9 *surface=nullptr,*stage=nullptr,*output=nullptr,*outputStage=nullptr;
    ok(d->CreateTexture(N,N,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&scene,nullptr));ok(scene->GetSurfaceLevel(0,&surface));
    ok(d->CreateOffscreenPlainSurface(N,N,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&stage,nullptr));
    ok(d->CreateRenderTarget(N*2,N*11,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&output,nullptr));
    ok(d->CreateOffscreenPlainSurface(N*2,N*11,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&outputStage,nullptr));
    auto pixels=[&](IDirect3DSurface9* from){ok(d->GetRenderTargetData(from,stage));D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> p(N*N*4);for(unsigned y=0;y<N;++y)std::memcpy(p.data()+y*N*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,N*4);ok(stage->UnlockRect());return p;};
    auto quad=[&](float z,bool left=false,unsigned offsetX=0,unsigned offsetY=0){const float x=float(offsetX)-.5f,y=float(offsetY)-.5f,w=float(left?N/2:N);const Vertex v[]={{x,y,z,1,0,0},{x+w,y,z,1,1,0},{x,y+N,z,1,0,1},{x+w,y+N,z,1,1,1}};return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(Vertex));};
    auto baseState=[&]{ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));for(auto k:{D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_STENCILENABLE,D3DRS_SEPARATEALPHABLENDENABLE})ok(d->SetRenderState(k,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));};
    Capture capture;capture.allowMultisample(allowMsaa);unsigned cases=0,compositeCases=0,negativeControls=0,bannerCounterfactuals=0,projectedCounterfactuals=0;float worst=0,bannerLift=0;
    std::array<std::vector<float>,11> blanketReceiver;
    for(unsigned i=0;i<10000;++i)ok(capture.draw(nullptr,classify(false,true,false,0,0,0,15,false),[]{return D3D_OK;}));
    assert(!capture.active()&&capture.draws==0&&capture.bindings==0); // No device call/allocation for an opaque-only frame.
    for(unsigned ms:{0u,2u}){
        IDirect3DSurface9 *aux=nullptr,*auxResolve=nullptr;ok(d->CreateRenderTarget(N,N,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_TYPE(ms),0,FALSE,&aux,nullptr));ok(d->CreateRenderTarget(N,N,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&auxResolve,nullptr));
        IDirect3DSurface9 *target=surface,*depth=nullptr;if(ms)ok(d->CreateRenderTarget(N,N,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_TYPE(ms),0,FALSE,&target,nullptr));
        ok(d->CreateDepthStencilSurface(N,N,D3DFMT_D24S8,D3DMULTISAMPLE_TYPE(ms),0,FALSE,&depth,nullptr));
        for(unsigned sm=0;sm<2;++sm)for(unsigned blend=0;blend<4;++blend)for(float alpha:{0.f,.25f,.8f,1.f})for(unsigned layers:{1u,3u})for(unsigned control=0;control<8;++control){
            const bool premult=blend==1||blend==3,additive=blend>=2;
            const auto dest=additive?D3DBLEND_ONE:D3DBLEND_INVSRCALPHA;
            capture.endFrame();std::vector<unsigned char> reference,actual,coverage,auxReference;unsigned submits=0;
            auto* nativeShader=control==3?nativeMrt[sm]:native[sm];
            for(bool tracked:{false,true}){
                for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));baseState();ok(d->SetRenderTarget(0,target));ok(d->SetRenderTarget(1,control==3?aux:nullptr));ok(d->SetDepthStencilSurface(depth));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));ok(d->SetPixelShader(nativeShader));
                ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));
                const bool black=additive&&!premult&&alpha==0;
                ok(d->BeginScene());const float background[]={black?0.f:.2f,black?0.f:.25f,black?0.f:.3f,1};ok(d->SetPixelShaderConstantF(0,background,1));ok(quad(.5f));
                ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,premult?D3DBLEND_ONE:D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,dest));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
                if(control==2){ok(d->SetRenderState(D3DRS_ALPHATESTENABLE,TRUE));ok(d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER));ok(d->SetRenderState(D3DRS_ALPHAREF,128));}
                const float scale=blend==1?alpha:1;const bool banner=control>=5;
                const float portal[]={(banner?.025f:.1f)*scale,(banner?.015f:.8f)*scale,(banner?.005f:.4f)*scale,alpha};ok(d->SetPixelShaderConstantF(0,portal,1));
                // 5: the old all-alpha admission on a dark lit receiver.
                // 6/7: the same no-Z-write alpha draw, classified as lit M2/WMO
                // or a projected alpha shadow; both remain below world fog.
                for(unsigned i=0;i<layers;++i){auto draw=[&]{if(tracked)++submits;return quad(control==1?.75f:.25f);};
                    const auto write=classify(capture.active(),false,true,premult?D3DBLEND_ONE:D3DBLEND_SRCALPHA,dest,D3DBLENDOP_ADD,15,control<6);
                    ok(tracked?capture.draw(d,write,draw,layers>1):draw());}
                capture.endBatch();
                if(control==3){ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetPixelShaderConstantF(0,background,1));auto draw=[&]{if(tracked)++submits;return quad(.1f,true);};ok(tracked?capture.draw(d,Write::Clear,draw):draw());}
                if(control==4){ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));const float litReceiver[]={.03f,.02f,.01f,.6f};ok(d->SetPixelShaderConstantF(0,litReceiver,1));
                    auto draw=[&]{if(tracked)++submits;return quad(.1f,true);};
                    const auto write=classify(capture.active(),false,true,premult?D3DBLEND_ONE:D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA,D3DBLENDOP_ADD,15,false);
                    ok(tracked?capture.draw(d,write,draw):draw());}
                ok(d->EndScene());
                IDirect3DPixelShader9* restored=nullptr;ok(d->GetPixelShader(&restored));assert(restored==nativeShader);restored->Release();IDirect3DSurface9* rt2=nullptr;assert(d->GetRenderTarget(2,&rt2)==D3DERR_NOTFOUND);DWORD cw=0;ok(d->GetRenderState(D3DRS_COLORWRITEENABLE2,&cw));assert(cw==15);
                if(control==3){ok(d->StretchRect(aux,nullptr,auxResolve,nullptr,D3DTEXF_NONE));const auto a=pixels(auxResolve);if(tracked)assert(a==auxReference);else auxReference=a;}ok(d->SetRenderTarget(1,nullptr));
                if(ms)ok(d->StretchRect(target,nullptr,surface,nullptr,D3DTEXF_NONE));if(tracked)actual=pixels(surface);else reference=pixels(surface);
            }
            if(actual!=reference){for(unsigned i=0;i<actual.size();++i)if(actual[i]!=reference[i]){std::printf("native mismatch sm=%u ms=%u premult=%u alpha=%g layers=%u control=%u byte=%u before=%u after=%u\n",sm+2,ms,unsigned(premult),alpha,layers,control,i,unsigned(reference[i]),unsigned(actual[i]));std::fflush(stdout);break;}}
            assert(actual==reference&&submits==layers+(control==3||control==4));auto* mask=capture.texture(d,target);
            if(ms&&!allowMsaa){assert(!mask&&!capture.active()&&capture.draws==0&&capture.bindings==0);++cases;continue;}
            if(!mask&&control<6){std::fprintf(stderr,"missing mask sm=%u ms=%u blend=%u alpha=%g layers=%u control=%u draws=%u bindings=%u active=%u emission=%u\n",sm+2,ms,blend,alpha,layers,control,capture.draws,capture.bindings,unsigned(capture.active()),unsigned(capture.hasEmission()));}
            assert(mask||control>=6);
            assert(control>=6?!capture.active():capture.active());
            assert(capture.draws==(control>=6?0:layers));
            if(!additive)assert(capture.bindings==(control>=6?0:1+(control==3||control==4))); // Three-layer early drain binds once.
            if(mask){IDirect3DSurface9* maskSurface=nullptr;ok(mask->GetSurfaceLevel(0,&maskSurface));coverage=pixels(maskSurface);maskSurface->Release();}
            else coverage.assign(N*N*4,0);
            if(!additive&&control==4)for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){const float value=coverage[(y*N+x)*4+2]/255.f;
                const float portalCoverage=1-std::pow(1-alpha,float(layers)),expected=portalCoverage*(x<N/2?.4f:1.f);assert(std::fabs(value-expected)<.012f);}
            float a=0;for(unsigned i=0;i<layers;++i)a=std::round(255*alpha+(1-alpha)*a*255)/255;
            if(control==1||(control==2&&alpha<=128.f/255))a=0;
            for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){
                if(!additive){const float expected=control>=6||(control==3&&x<N/2)?0:control==4&&x<N/2?a*.4f:a;assert(std::fabs(coverage[(y*N+x)*4+2]/255.f-expected)<.012f);}
                else for(unsigned k=0;k<3;++k){
                    const float glow[]={control>=5?.025f:.1f,control>=5?.015f:.8f,control>=5?.005f:.4f};
                    float expected=0;for(unsigned i=0;i<layers;++i)expected=std::min(1.f,std::round((expected+glow[k]*(premult?1:alpha))*255)/255);
                    if(control>=6||control==1||(control==2&&alpha<=128.f/255)||(control==3&&x<N/2))expected=0;
                    if(control==4&&x<N/2)expected*=.4f;
                    assert(std::fabs(coverage[(y*N+x)*4+2-k]/255.f-expected)<.012f);
                    assert(coverage[(y*N+x)*4+3]==0);
                }
            }
            ++cases;
            // Alpha coverage attenuates every correction. Additive radiance protects
            // emission but leaves atmospheric in-scattering intact. Unmasked output
            // is a negative control; fully covered portal pixels retain Scene.
            baseState();ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,output));
            std::vector<IDirect3DTexture9*> owned;
            auto bind=[&](unsigned slot,Image im){auto* t=texture(d,im);owned.push_back(t);ok(d->SetTexture(slot,t));};
            Image lin{N,N,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(N*N*16)};
            for(unsigned i=0;i<N*N;++i){float p[4]={linear(actual[i*4+2]/255.f),linear(actual[i*4+1]/255.f),linear(actual[i*4]/255.f),actual[i*4+3]/255.f};std::memcpy(lin.pixels.data()+i*16,p,16);}
            auto* linearScene=texture(d,lin);owned.push_back(linearScene);
            for(unsigned s=0;s<16;++s){for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,k,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));for(auto k:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(s,k,D3DTADDRESS_CLAMP));}
            for(unsigned variant=0;variant<11;++variant){
                for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));const bool hdr=variant==2||variant==3||variant==5||variant==6||variant==8||variant==10;
                float c[80][4]{};const float nearZ=.2f,farZ=1000,nd=farZ/(farZ-nearZ)*(1-nearZ/10);
                c[0][0]=c[0][1]=1.f/N;c[0][2]=nearZ;c[0][3]=farZ;c[1][0]=c[1][1]=c[1][2]=c[2][0]=1;c[25][1]=c[25][2]=c[25][3]=1;
                c[33][0]=c[33][1]=c[33][2]=c[33][3]=N;c[73][0]=1;c[73][1]=48;c[74][0]=1;c[76][0]=c[76][1]=c[76][2]=.3f;c[20][2]=c[30][2]=1;
                bind(1,solid(nd));bind(2,solid(0));bind(3,solid(.2f,.1f,.05f));bind(4,solid(.05f,.02f,0,.7f));bind(6,solid(1,1,1,1));bind(8,solid(.2f,.1f,.15f,1));bind(9,solid(.05f,.02f,0,.7f));bind(10,solid(0,0,0,.5f));bind(11,solid(0));bind(12,solid(.6f,.6f,.6f));bind(13,solid(0));bind(14,solid(0,0,1,1));bind(15,solid(10));
                if(control>=5){bind(4,solid(.4f,.35f,.3f,.8f));bind(9,solid(.4f,.35f,.3f,.8f));}
                ok(d->SetTexture(0,(variant==5||variant==8)?linearScene:scene));ok(d->SetTexture(5,scene));if(hdr&&variant!=8)ok(d->SetTexture(2,linearScene));
                if(variant==4||variant==5||variant==6)bind(9,solid(.9f,.5f,-10,1));
                if(variant==7){c[1][2]=.5f;c[2][0]=0;c[2][1]=3;c[3][1]=1;c[3][2]=1;bind(2,solid(0,0,0,.5f));bind(3,solid(0));}
                if(variant==8){bind(2,solid(0,0,0,.5f));ok(d->SetTexture(3,scene));}
                ok(d->SetPixelShaderConstantF(0,c[0],80));ok(d->SetPixelShader(composites[variant]));
                auto composite=[&](bool protected_){Capture::bind(d,protected_?mask:nullptr,capture.hasEmission());ok(d->BeginScene());ok(quad(0,false,protected_?N:0,variant*N));ok(d->EndScene());};
                composite(false);composite(true);
            }
            // Keep the native capture/state-order matrix intact. Only its
            // independent compositor outputs share one readback per case.
            ok(d->GetRenderTargetData(output,outputStage));D3DLOCKED_RECT lock{};ok(outputStage->LockRect(&lock,nullptr,D3DLOCK_READONLY));
            auto compositePixels=[&](unsigned variant,bool protected_){std::vector<float> p(N*N*4);for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N*4;++x){std::uint16_t h;std::memcpy(&h,static_cast<char*>(lock.pBits)+(variant*N+y)*lock.Pitch+((protected_?N*4:0)+x)*2,2);p[y*N*4+x]=half(h);}return p;};
            for(unsigned variant=0;variant<11;++variant){
                const bool hdr=variant==2||variant==3||variant==5||variant==6||variant==8||variant==10;
                const auto unmasked=compositePixels(variant,false),masked=compositePixels(variant,true);
                if(control==5)blanketReceiver[variant]=masked;
                if(control>=6){assert(masked==unmasked);assert(blanketReceiver[variant].size()==masked.size());
                    if(!additive&&variant==0&&alpha==1){const float lift=masked[0]-blanketReceiver[variant][0];assert(lift>.05f);bannerLift=std::max(bannerLift,lift);if(control==6)++bannerCounterfactuals;else ++projectedCounterfactuals;}}
                for(unsigned i=0;i<N*N;++i)for(unsigned k=0;k<3;++k){float original=actual[i*4+2-k]/255.f;
                    float cover=coverage[i*4+2]/255.f;
                    if(additive){float remaining=std::max(original-coverage[i*4+2-k]/255.f,0.f);cover=std::clamp(((hdr?linear(original):original)-(hdr?linear(remaining):remaining))/std::max(hdr?linear(original):original,.000001f),0.f,1.f);}
                    if(hdr)original=linear(original);
                    const bool world=variant<4||variant==6||variant>=9;
                    const float fogRgb[3]={control>=5?.4f:.05f,control>=5?.35f:.02f,control>=5?.3f:0.f};
                    const float expected=unmasked[i*4+k]*(1-cover)+original*cover+(additive&&world?fogRgb[k]*cover:0);
                    const float error=std::fabs(masked[i*4+k]-expected);worst=std::max(worst,error);if(!(error<.003f)){std::fprintf(stderr,"variant=%s sm=%u ms=%u a=%g cover=%g actual=%g expected=%g\n",entries[variant],sm+2,ms,alpha,cover,masked[i*4+k],expected);std::abort();}
                    if(cover==1&&std::fabs(unmasked[i*4+k]-original)>.05f)++negativeControls;
                }
                ++compositeCases;
            }
            ok(outputStage->UnlockRect());
            for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));for(auto* t:owned)t->Release();
            capture.colorWrite(output);assert(control>=6?!capture.active():capture.active());capture.colorWrite(target);assert(!capture.active()&&!capture.texture(d,target));
        }
        capture.endFrame();depth->Release();aux->Release();auxResolve->Release();if(ms)target->Release();
    }
    unsigned mixed=0;
    for(unsigned sm=0;sm<2;++sm)for(bool alphaFirst:{false,true})for(bool batch:{false,true})for(unsigned modulate=0;modulate<4;++modulate){
        capture.endFrame();std::vector<unsigned char> reference;float expected[3]={};
        for(bool tracked:{false,true}){
            for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));baseState();ok(d->SetRenderTarget(0,surface));for(unsigned s=1;s<4;++s)ok(d->SetRenderTarget(s,nullptr));ok(d->SetDepthStencilSurface(nullptr));
            ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));ok(d->SetPixelShader(native[sm]));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0x202020,1,0));
            ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));ok(d->BeginScene());
            for(unsigned layer=0;layer<2;++layer){const bool alpha=(layer==0)==alphaFirst;const float rgba[]={alpha?.2f:.3f,alpha?.1f:.15f,alpha?.05f:.04f,alpha?.4f:.5f};ok(d->SetPixelShaderConstantF(0,rgba,1));ok(d->SetRenderState(D3DRS_DESTBLEND,alpha?D3DBLEND_INVSRCALPHA:D3DBLEND_ONE));
                auto draw=[&]{return quad(.5f);};ok(tracked?capture.draw(d,alpha?Write::Alpha:Write::Additive,draw,batch):draw());}
            if(modulate){const float tint[]={.25f,.5f,.75f,.6f};ok(d->SetPixelShaderConstantF(0,tint,1));
                ok(d->SetRenderState(D3DRS_SRCBLEND,modulate==2?D3DBLEND_ZERO:D3DBLEND_DESTCOLOR));ok(d->SetRenderState(D3DRS_DESTBLEND,modulate==1?D3DBLEND_ZERO:D3DBLEND_SRCCOLOR));
                auto draw=[&]{return quad(.5f);};ok(tracked?capture.draw(d,Write::Modulate,draw,batch):draw());}
            capture.endBatch();ok(d->EndScene());const auto actual=pixels(surface);if(tracked)assert(actual==reference);else reference=actual;
        }
        auto* mask=capture.texture(d,surface);assert(mask&&capture.hasEmission());IDirect3DSurface9* ms=nullptr;ok(mask->GetSurfaceLevel(0,&ms));const auto value=pixels(ms);ms->Release();
        const float glow[]={.3f,.15f,.04f},tint[]={.25f,.5f,.75f};
        for(unsigned k=0;k<3;++k){expected[k]=std::round(glow[k]*.5f*255)/255;if(!alphaFirst)expected[k]=std::round(expected[k]*.6f*255)/255;
            if(modulate)expected[k]=std::min(1.f,std::round(expected[k]*tint[k]*(modulate==3?2:1)*255)/255);}
        for(unsigned pixel=0;pixel<N*N;++pixel){if(std::fabs(value[pixel*4+3]/255.f-.4f)>=.012f)std::fprintf(stderr,"mixed alpha sm=%u alphaFirst=%u batch=%u modulate=%u pixel=%u got=%u\n",sm+2,unsigned(alphaFirst),unsigned(batch),modulate,pixel,unsigned(value[pixel*4+3]));assert(std::fabs(value[pixel*4+3]/255.f-.4f)<.012f);for(unsigned k=0;k<3;++k)assert(std::fabs(value[pixel*4+2-k]/255.f-expected[k])<.012f);}
        // Removing an original program invalidates variants chained through its
        // coverage shader before their COM addresses can be reused.
        capture.forget(native[sm]);++mixed;
    }
    assert(negativeControls>0&&bannerCounterfactuals>0&&projectedCounterfactuals>0);std::printf("EFFECT_COVERAGE_PASS native=%u composites=%u mixedOrder=%u negativeControls=%u maxError=%g mrtMsaa=%u bannerCounterfactuals=%u projectedCounterfactuals=%u bannerLift=%g sm2 sm3 msaa0 msaa2 alpha premultiplied additive depth alphaTest opaqueOverwrite modulate modulate2x litAlphaErasesProportionally litBannerReceivesFog oldBlanketAlphaDarkControl projectedShadowBelowFog blackZeroEmission nativePixelsExact oneDrawPerInput batchedReplay opaqueOnlyNoDeviceCalls\n",cases,compositeCases,mixed,negativeControls,worst,unsigned(allowMsaa),bannerCounterfactuals,projectedCounterfactuals,bannerLift);
    capture.reset();coverageBenchmark(d,native[1]);for(auto* p:native)p->Release();for(auto* p:nativeMrt)p->Release();for(auto* p:composites)p->Release();surface->Release();stage->Release();output->Release();outputStage->Release();scene->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
