// Real production bytecode: split 8-lamp vs merged 16-lamp accumulation.
#define main northlight_unused_composite_replay
#include "composite_distance_gpu.cpp"
#undef main
#include <array>
#include <limits>
static Image solid(float r,float g=0,float b=0,float a=0){Image im{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};const float p[]={r,g,b,a};std::memcpy(im.pixels.data(),p,16);return im;}
int main(int argc,char** argv){
    assert(argc>=3);const std::string compiled=argv[1];constexpr unsigned N=16,full=32;
    const bool badBank=argc>3&&std::strcmp(argv[3],"bad-bank")==0;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightLampBatchOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,N,N,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[2]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=N;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9* shaders[3]{};const char* entries[]={"LocalDirect.bin","LocalDirect16.bin","LocalLighting.bin"};
    for(unsigned i=0;i<3;++i){auto code=read(i==1&&argc>3&&!badBank?argv[3]:compiled+"/"+entries[i]);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shaders[i]));}
    IDirect3DCubeTexture9* cube=nullptr;ok(d->CreateCubeTexture(4,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&cube,nullptr));
    for(unsigned face=0;face<6;++face){D3DLOCKED_RECT lock{};ok(cube->LockRect(D3DCUBEMAP_FACES(face),0,&lock,nullptr,0));for(unsigned y=0;y<4;++y)std::memset(static_cast<char*>(lock.pBits)+y*lock.Pitch,0,16);ok(cube->UnlockRect(D3DCUBEMAP_FACES(face),0));}
    for(unsigned s=0;s<16;++s){for(auto key:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,key,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto key:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW})ok(d->SetSamplerState(s,key,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));}
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(key,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{N-.5f,-.5f,0,1,1,0},{-.5f,N-.5f,0,1,0,1},{N-.5f,N-.5f,0,1,1,1}};
    float position[24][4]{},color[24][4]{};
    for(unsigned i=0;i<24;++i){position[i][0]=(int(i%5)-2)*1.3f;position[i][1]=(int(i%3)-1)*1.1f;position[i][2]=5;position[i][3]=20;for(unsigned k=0;k<3;++k)color[i][k]=.2f+.8f*float((i*7+k*3)%17)/16;color[i][3]=1.f/19;}
    unsigned cases=0,failures=0,checked=0,bumpChanges=0,shadowChanges=0,rejected=0;double max32=0,max16=0;
    for(auto format:{D3DFMT_A32B32G32R32F,D3DFMT_A16B16G16R16F}){
        const bool fp16=format==D3DFMT_A16B16G16R16F;IDirect3DSurface9 *target=nullptr,*staging=nullptr;
        ok(d->CreateRenderTarget(N,N,format,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(N,N,format,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp={0,0,N,N,0,1};ok(d->SetViewport(&vp));
        auto pixels=[&]{ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT l{};ok(staging->LockRect(&l,nullptr,D3DLOCK_READONLY));std::vector<float> out(N*N*4);for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N*4;++x){const auto* row=static_cast<char*>(l.pBits)+y*l.Pitch;if(fp16){std::uint16_t h;std::memcpy(&h,row+x*2,2);out[y*N*4+x]=half(h);}else std::memcpy(&out[y*N*4+x],row+x*4,4);}ok(staging->UnlockRect());return out;};
        for(unsigned count:{9u,16u,24u})for(unsigned detailMode=0;detailMode<4;++detailMode)for(float strength:{0.f,.4f,1.f})for(unsigned domain=0;domain<4;++domain)for(bool shadow:{false,true}){
            const float nearZ=.2f,farZ=1000,z=10,nd=domain==2?1.f:farZ/(farZ-nearZ)*(1-nearZ/z);
            float c[116][4]{};c[0][0]=c[0][1]=1.f/full;c[0][2]=nearZ;c[0][3]=farZ;c[1][0]=c[1][1]=c[1][2]=1;c[2][0]=1;
            c[3][0]=c[4][1]=c[5][2]=c[6][3]=1;c[20][2]=.6f;c[30][0]=domain==1||domain==3;c[33][0]=c[33][1]=full;c[33][2]=c[33][3]=N;
            c[52][1]=.8f;c[52][2]=detailMode%2?.7f:0;c[73][0]=strength;c[73][1]=detailMode==1?12.f:40.f;c[73][2]=1;c[73][3]=.125f;
            for(unsigned reg=74;reg<=78;++reg)for(unsigned k=0;k<4;++k)c[reg][k]=float(reg)+float(k)*.125f;
            std::vector<IDirect3DTexture9*> owned;auto make=[&](const Image& im){auto t=texture(d,im);owned.push_back(t);return t;};
            auto depth=make(solid(nd)),normal=make(solid(0,0,-1,1)),base=make(solid(0,0,0,.7f)),water=make(solid(domain==3?11.f:9.f,1));
            Image material{full,full,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(full*full*16)};
            for(unsigned y=0;y<full;++y)for(unsigned x=0;x<full;++x){const float coverage=detailMode==1?.5f:detailMode==3?.01f:1.f;const float octX=x%4<2?1.f:0.f;const float f[]={octX*coverage,(11.f/14)*coverage,(detailMode==2?2.f:z)*coverage,coverage};std::memcpy(material.pixels.data()+(y*full+x)*16,f,16);}
            auto detail=make(material);
            auto draw=[&]{ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());};
            std::vector<float> unshadowed(N*N*4);
            auto render=[&](bool wide,float bump,bool point){
                for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));ok(d->SetTexture(1,depth));ok(d->SetTexture(9,detail));ok(d->SetTexture(11,water));ok(d->SetTexture(12,base));ok(d->SetTexture(14,normal));
                c[73][0]=bump;ok(d->SetPixelShaderConstantF(0,c[0],116));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));
                for(unsigned first=0;first<count;){const unsigned batch=wide&&first==0?16:8;float pos[16][4]{},rgb[16][4]{};for(unsigned i=0;i<batch&&first+i<count;++i){std::memcpy(pos[i],position[first+i],16);std::memcpy(rgb[i],color[first+i],16);}
                    ok(d->SetPixelShader(shaders[batch==16?1:0]));ok(d->SetPixelShaderConstantF(batch==16?(badBank?68:84):36,pos[0],batch));ok(d->SetPixelShaderConstantF(batch==16?(badBank?84:100):44,rgb[0],batch));
                    float preserved[6][4];ok(d->GetPixelShaderConstantF(73,preserved[0],6));assert(!std::memcmp(preserved,c[73],sizeof preserved));
                    draw();first+=batch;
                }
                if(point){const auto lit=pixels();for(unsigned at=0;at<lit.size();++at)unshadowed[at]=std::max(unshadowed[at],std::fabs(lit[at]));float local[13][4]{};std::memcpy(local,c,7*16);std::memcpy(local[7],position[0],16);local[7][3]=NORTHLIGHT_POINT_MATERIAL?color[0][3]:1.f;std::memcpy(local[8],color[0],16);local[8][3]=position[0][3];local[2][1]=c[30][0];local[2][2]=1;local[2][3]=.025f;
                    local[9][0]=.1f;local[9][1]=20;local[9][2]=.25f;local[9][3]=1;local[10][0]=c[52][1];local[10][1]=c[52][2];local[10][2]=c[20][2];std::memcpy(local[12],c[73],16);
                    ok(d->SetPixelShaderConstantF(0,local[0],13));ok(d->SetTexture(0,depth));ok(d->SetTexture(1,cube));ok(d->SetTexture(2,water));ok(d->SetTexture(3,normal));ok(d->SetTexture(4,base));ok(d->SetTexture(5,detail));ok(d->SetPixelShader(shaders[2]));draw();
                }
                return pixels();
            };
            const auto split=render(false,strength,shadow),wide=render(true,strength,shadow);
            for(unsigned at=0;at<wide.size();++at){const double error=std::fabs(double(wide[at])-split[at]);const float scale=std::max(std::fabs(wide[at]),std::fabs(split[at]));
                // 16 nonnegative lamp terms: regrouped additions/scales fit 64 float eps.
                // FP16 rounds split passes at the blend target: two pre-shadow
                // half ULPs, plus one final ULP after the identical signed correction.
                // Use pre-correction energy so cancellation cannot shrink its error bound.
                const float energy=std::max(scale,unshadowed[at]);int exponent=0,finalExponent=0;std::frexp(energy,&exponent);std::frexp(scale,&finalExponent);
                const double ulp=std::max(std::ldexp(1.,-24),std::ldexp(1.,exponent-11)),finalUlp=std::max(std::ldexp(1.,-24),std::ldexp(1.,finalExponent-11));
                const double bound=64*std::numeric_limits<float>::epsilon()*std::max(1.f,energy)+(fp16?2*ulp+(shadow?finalUlp:0):0);
                (fp16?max16:max32)=std::max(fp16?max16:max32,error);
                if(!std::isfinite(wide[at])||error>bound){if(failures++<4)std::printf("FAIL format=%u count=%u detail=%u strength=%g domain=%u point=%u at=%u split=%g wide=%g error=%g bound=%g\n",unsigned(format),count,detailMode,strength,domain,unsigned(shadow),at,split[at],wide[at],error,bound);}
                if(domain==1||domain==2){assert(split[at]==0&&wide[at]==0);++rejected;}++checked;
            }
            if(!fp16&&count==16&&detailMode==0&&strength==1&&domain==0){const auto flat=render(true,0,shadow);for(unsigned at=0;at<wide.size();++at)bumpChanges+=std::fabs(wide[at]-flat[at])>.001f;}
            if(!fp16&&count==16&&detailMode==0&&strength==0&&domain==0&&shadow){const auto lit=render(true,0,false);for(unsigned at=0;at<wide.size();++at)shadowChanges+=lit[at]-wide[at]>.001f;}
            for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));for(auto t:owned)t->Release();++cases;
        }
        staging->Release();target->Release();
    }
    if(!bumpChanges||!shadowChanges)++failures;std::printf("local direct batches GPU cases=%u components=%u rejected=%u bumpChanges=%u shadowChanges=%u maxFp32=%.9g maxFp16=%.9g failures=%u\n",cases,checked,rejected,bumpChanges,shadowChanges,max32,max16,failures);
    d->SetPixelShader(nullptr);for(auto shader:shaders)shader->Release();cube->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
