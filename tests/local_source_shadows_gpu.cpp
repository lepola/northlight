// Hidden device oracle: real production cube rasterization and local-light bytecode.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main northlight_unused_composite_oracle
#include "composite_distance_gpu.cpp"
#undef main
#pragma clang diagnostic pop
#include "point_light_shadow.h"
#include <array>

static Image rgba(float r,float g,float b,float a){Image v{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};const float f[]={r,g,b,a};std::memcpy(v.pixels.data(),f,16);return v;}
static Image red(float value){Image v{1,1,D3DFMT_R32F,4,std::vector<unsigned char>(4)};std::memcpy(v.pixels.data(),&value,4);return v;}
static float smooth(float a,float b,float x){float t=std::clamp((x-a)/(b-a),0.f,1.f);return t*t*(3-2*t);}
int main(int argc,char** argv){
    assert(argc==3);const std::string dir=argv[1];constexpr unsigned size=32,full=64,cubeSize=256;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightLocalShadowOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[2]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DPixelShader9 *direct=nullptr,*lighting=nullptr,*casterPS=nullptr;IDirect3DVertexShader9* casterVS=nullptr;
    auto code=read((dir+"/LocalDirect.bin").c_str());ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&direct));
    code=read((dir+"/LocalLighting.bin").c_str());ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&lighting));
    code=read((dir+"/LocalShadowPS.bin").c_str());ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&casterPS));
    code=read((dir+"/LocalShadowVS.bin").c_str());ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()),&casterVS));
    constexpr unsigned maximumLights=NorthlightPointShadow::MaxLights;IDirect3DCubeTexture9* cubes[maximumLights]{};IDirect3DSurface9* depth=nullptr;ok(d->CreateDepthStencilSurface(cubeSize,cubeSize,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&depth,nullptr));
    float positions[maximumLights][3]={};const float basePositions[4][3]={{-2,0,5},{2,0,5},{0,-3,5},{0,3,5}};
    float colors[maximumLights][3]={};const float baseColors[4][3]={{1,.2f,.1f},{.1f,.4f,1},{.2f,1,.1f},{.7f,.2f,1}};
    for(unsigned i=0;i<maximumLights;++i)for(unsigned k=0;k<3;++k){positions[i][k]=basePositions[i%4][k]+(k<2?.002f*(i/4):0);colors[i][k]=baseColors[i%4][k]*(.75f+.25f*float(i%7)/6);}
    struct Caster{float x,y,z,u,v;};const Caster blocker[]={{-.5f,-1.5f,7.5f,0,0},{.5f,-1.5f,7.5f,1,0},{-.5f,1.5f,7.5f,0,1},{.5f,1.5f,7.5f,1,1}};
    for(unsigned light=0;light<maximumLights;++light){
        ok(d->CreateCubeTexture(cubeSize,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&cubes[light],nullptr));
        for(unsigned face=0;face<6;++face){
            IDirect3DSurface9* surface=nullptr;ok(cubes[light]->GetCubeMapSurface(D3DCUBEMAP_FACES(face),0,&surface));
            ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,surface));ok(d->SetDepthStencilSurface(depth));D3DVIEWPORT9 vp={0,0,cubeSize,cubeSize,0,1};ok(d->SetViewport(&vp));
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
            ok(d->SetFVF(D3DFVF_XYZ|D3DFVF_TEX1));ok(d->SetVertexShader(casterVS));ok(d->SetPixelShader(casterPS));
            float matrix[16];assert(NorthlightPointShadow::faceMatrix(positions[light],.1f,16,face,matrix,cubeSize));ok(d->SetVertexShaderConstantF(0,matrix,4));
            const float material[]={0,0,0,-1},sphere[]={256,1.f/cubeSize,0,0};ok(d->SetPixelShaderConstantF(0,material,1));ok(d->SetPixelShaderConstantF(1,sphere,1));
            ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,blocker,sizeof(Caster)));ok(d->EndScene());surface->Release();
        }
    }
    IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(size,size,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(size,size,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp={0,0,size,size,0,1};ok(d->SetViewport(&vp));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
    const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{size-.5f,-.5f,0,1,1,0},{-.5f,size-.5f,0,1,0,1},{size-.5f,size-.5f,0,1,1,1}};
    for(unsigned s=0;s<16;++s){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW})ok(d->SetSamplerState(s,state,D3DTADDRESS_CLAMP));}
    unsigned cases=0,checks=0,failures=0,shadowPixels[maximumLights]={};float maximum=0;
    for(unsigned count:{4u,8u,16u,32u,64u,128u})for(float sun:{0.f,.8f,8.f})for(float visibility:{0.f,1.f})for(float gain:{.25f,.8f})for(bool shadows:{false,true}){
        std::vector<IDirect3DTexture9*> textures;const auto bind=[&](unsigned s,const Image& image){auto t=texture(d,image);textures.push_back(t);ok(d->SetTexture(s,t));};
        const float nearZ=.2f,farZ=1000,z=10,dz=farZ/(farZ-nearZ)*(1-nearZ/z),cut=sun>0?.7f:0.f;
        float c[68][4]{};c[0][0]=c[0][1]=1.f/full;c[0][2]=nearZ;c[0][3]=farZ;c[1][0]=c[1][1]=c[1][2]=1;c[2][0]=1;
        c[3][0]=c[4][1]=c[5][2]=c[6][3]=1;c[20][2]=1;c[52][0]=count;c[52][1]=gain;c[52][2]=cut;
        c[33][0]=c[33][1]=full;c[33][2]=c[33][3]=size;
        bind(1,red(dz));bind(11,rgba(0,0,0,0));bind(12,rgba(0,0,0,visibility));bind(14,rgba(0,0,-1,1));ok(d->SetPixelShader(direct));
        ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
        for(unsigned first=0;first<count;first+=8){
            c[52][0]=std::min(8u,count-first);
            for(unsigned j=0;j<8;++j){for(unsigned k=0;k<4;++k)c[36+j][k]=c[44+j][k]=0;if(first+j>=count)continue;
                std::memcpy(c[36+j],positions[first+j],12);c[36+j][3]=16;std::memcpy(c[44+j],colors[first+j],12);c[44+j][3]=1.f/15;}
            ok(d->SetPixelShaderConstantF(0,c[0],68));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,first!=0));
            ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());
        }
        if(shadows)for(unsigned i=0;i<count;++i){
            float local[12][4]{};std::memcpy(local,c,7*16);std::memcpy(local[7],positions[i],12);local[7][3]=1.f/15;std::memcpy(local[8],colors[i],12);local[8][3]=16;local[2][3]=.025f;
            local[9][0]=.1f;local[9][1]=16;local[9][2]=1.f/cubeSize;local[9][3]=1;local[10][0]=gain;local[10][1]=cut;local[10][2]=1;local[10][3]=sun;local[11][2]=-1;
            bind(0,red(dz));ok(d->SetTexture(1,cubes[i]));bind(2,rgba(0,0,0,0));bind(3,rgba(0,0,-1,1));bind(4,rgba(0,0,0,visibility));ok(d->SetPixelShader(lighting));ok(d->SetPixelShaderConstantF(0,local[0],12));
            ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
            ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());
        }
        ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        for(unsigned y=1;y<size-1;++y)for(unsigned x=1;x<size-1;++x){
            // Both shaders snap their full-resolution depth sample before reconstruction.
            const float px=(float(2*x)+1-full*.5f)/float(full*.5f)*z,py=-(float(2*y)+1-full*.5f)/float(full*.5f)*z;
            float expected[3]={};bool edge=false;
            for(unsigned i=0;i<count;++i){const float dx=px-positions[i][0],dy=py-positions[i][1],distance=std::sqrt(dx*dx+dy*dy+25);float attenuation=std::clamp((16-distance)/15,0.f,1.f);float contribution[3];
                for(unsigned k=0;k<3;++k)contribution[k]=colors[i][k]*(attenuation*attenuation*(5/distance)*gain*(1-cut*visibility));
                const float hx=positions[i][0]+.5f*dx,hy=positions[i][1]+.5f*dy;bool blocked=std::fabs(hx)<.5f&&std::fabs(hy)<1.5f;
                if(std::fabs(std::fabs(hx)-.5f)<.15f&&std::fabs(hy)<1.7f)edge=true;if(std::fabs(std::fabs(hy)-1.5f)<.15f&&std::fabs(hx)<.7f)edge=true;
                float energy=std::max({contribution[0],contribution[1],contribution[2]}),relevance=smooth(.15f,.5f,energy/std::max(sun*visibility,.02f))*smooth(.002f,.01f,energy);
                if(shadows&&blocked&&relevance>.9f)++shadowPixels[i];
                for(unsigned k=0;k<3;++k)expected[k]+=contribution[k]*(shadows&&blocked?1-relevance:1);
            }
            if(edge)continue;float actual[4];std::memcpy(actual,static_cast<char*>(lock.pBits)+y*lock.Pitch+x*16,16);
            for(unsigned k=0;k<3;++k){float error=std::fabs(actual[k]-expected[k]);maximum=std::max(maximum,error);if(!std::isfinite(actual[k])||error>5e-4f){if(failures++<5)std::printf("FAIL count=%u sun=%.1f vis=%.0f gain=%.2f shadows=%d xy=%u,%u channel=%u actual=%.6f expected=%.6f\n",count,sun,visibility,gain,int(shadows),x,y,k,actual[k],expected[k]);}}++checks;
        }
        ok(staging->UnlockRect());for(unsigned s=0;s<16;++s)ok(d->SetTexture(s,nullptr));for(auto t:textures)t->Release();++cases;
    }
    unsigned verified=0;for(unsigned i=0;i<maximumLights;++i){if(!shadowPixels[i])++failures;else ++verified;}
    std::printf("local shadows GPU cases=%u checkedPixels=%u maxError=%.8g failures=%u verifiedShadowSources=%u\n",cases,checks,maximum,failures,verified);
    d->SetPixelShader(nullptr);d->SetVertexShader(nullptr);for(auto cube:cubes)cube->Release();depth->Release();staging->Release();target->Release();direct->Release();lighting->Release();casterPS->Release();casterVS->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);return failures?1:0;
}
