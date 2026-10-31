// Offscreen real-device oracle: original client VS/PS, production capture and LocalDirect.
#define main distance_oracle_main
#include "composite_distance_gpu.cpp"
#undef main
#include <cstdarg>
#include <unordered_map>
template<class T> static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char* format,...){va_list args;va_start(args,format);std::vprintf(format,args);std::puts("");std::fflush(stdout);va_end(args);}
#include "saved_state.h"
#include "auto_bump_renderer.h"
#include "world_compiled_shaders.h"
#include "forwarders.h"
#ifdef NORTHLIGHT_MATERIAL_MIRROR
#include "device_mirror.h"
#endif
struct MaterialCallMeter:ForwardIDirect3DDevice9 {
    unsigned samplerGets=0,samplerSets=0,constantSets=0,renderGets=0,constantGets=0,vertexGets=0,pixelGets=0;
    int fault=-1;
    bool fail(int at){if(fault!=at)return false;fault=-1;return true;}
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** s)override{++vertexGets;if(fail(0)){*s=nullptr;return E_FAIL;}return real->GetVertexShader(s);}
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** s)override{++pixelGets;if(fail(1)){*s=nullptr;return E_FAIL;}return real->GetPixelShader(s);}
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* s)override{if(fail(2))return E_FAIL;return real->SetVertexShader(s);}
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* s)override{if(fail(3))return E_FAIL;return real->SetPixelShader(s);}
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE k,DWORD* v)override{++renderGets;return real->GetRenderState(k,v);}
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT a,float* v,UINT n)override{++constantGets;return real->GetVertexShaderConstantF(a,v,n);}
    explicit MaterialCallMeter(IDirect3DDevice9* device):ForwardIDirect3DDevice9(device){}
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT a,const float* v,UINT n)override{++constantSets;if(fail(4))return E_FAIL;return real->SetPixelShaderConstantF(a,v,n);}
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD n,D3DSAMPLERSTATETYPE k,DWORD* v) override{++samplerGets;return real->GetSamplerState(n,k,v);}
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD n,D3DSAMPLERSTATETYPE k,DWORD v) override{++samplerSets;return real->SetSamplerState(n,k,v);}
};
static constexpr unsigned W=256,H=256;
static Image solid(float x,float y,float z,float a){Image i{W,H,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(W*H*16)};float v[]={x,y,z,a};for(unsigned at=0;at<W*H;++at)std::memcpy(i.pixels.data()+at*16,v,16);return i;}
static IDirect3DTexture9* mipTexture(IDirect3DDevice9* d,const std::string& path){
    auto base=image(path);IDirect3DTexture9* t=nullptr;ok(d->CreateTexture(base.width,base.height,0,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&t,nullptr));
    for(unsigned level=0;level<t->GetLevelCount();++level){auto im=image(path+"."+std::to_string(level));D3DLOCKED_RECT lock{};ok(t->LockRect(level,&lock,nullptr,0));for(unsigned y=0;y<im.height;++y)std::memcpy((char*)lock.pBits+y*lock.Pitch,im.pixels.data()+y*im.width*4,im.width*4);ok(t->UnlockRect(level));}return t;
}
static std::vector<float> pixels(IDirect3DDevice9* d,IDirect3DSurface9* source){
    IDirect3DSurface9* staging=nullptr;ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->GetRenderTargetData(source,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> out(W*H*4);for(unsigned y=0;y<H;++y)std::memcpy(out.data()+y*W*4,(char*)lock.pBits+y*lock.Pitch,W*16);staging->UnlockRect();staging->Release();return out;
}
static std::array<unsigned,3> occludedPixels(IDirect3DDevice9* d,IDirect3DTexture9* texture){
    assert(texture);IDirect3DSurface9 *source=nullptr,*staging=nullptr;ok(texture->GetSurfaceLevel(0,&source));
    ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->GetRenderTargetData(source,staging));
    D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::array<unsigned,3> count{};
    unsigned covered=0;
    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){const auto* p=(unsigned char*)lock.pBits+y*lock.Pitch+x*4;if(p[3]>5){++covered;for(unsigned i=0;i<3;++i)count[i]+=unsigned(p[2-i])+2<unsigned(p[3]);}}
    std::printf("OCCLUSION covered=%u\n",covered);
    staging->UnlockRect();drop(staging);drop(source);return count;
}
int main(int argc,char** argv){
    assert(argc==4);WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightBumpOracle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    IDirect3DDevice9* rawDevice=d;MaterialCallMeter meter(d);d=&meter;
#ifdef NORTHLIGHT_MATERIAL_MIRROR
    DeviceMirror mirror;mirror.gate.exclusive=true;ExtensionDevice extension(&meter,&mirror);d=&extension;
#endif
    IDirect3DSurface9 *target=nullptr,*depth=nullptr;ok(d->CreateRenderTarget(W,H,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateDepthStencilSurface(W,H,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));
    ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(depth));D3DVIEWPORT9 vp={0,0,W,H,0,.94f};ok(d->SetViewport(&vp));
    std::string folder=argv[2];std::wstring root(folder.begin(),folder.end());if(root.back()!=L'/'&&root.back()!=L'\\')root+=L'/';
    NorthlightAutoBump bump(d);NorthlightQuality::Settings settings;settings.autoBumpTerrain=2;settings.autoBumpWalls=2;settings.autoBump=1;settings.autoBumpStrength=100;bump.configure(settings);
    auto capture=[&](auto&&... args){
#ifdef NORTHLIGHT_MATERIAL_RAW
        ExtensionDevice::RawScope raw(extension);
#endif
        bump.capture(std::forward<decltype(args)>(args)...);
    };
    auto native=[&](auto&&... args){
#ifdef NORTHLIGHT_MATERIAL_RAW
        ExtensionDevice::RawScope raw(extension);
#endif
        return bump.nativeDraw(std::forward<decltype(args)>(args)...);
    };
    struct V {float x,y,z,nx,ny,nz;DWORD color;float u,v;};
    const V mesh[]={{-1,1,-1,0,0,1,0xffffffff,0,0},{1,1,-1,0,0,1,0xffffffff,1,0},{-1,-1,-1,0,0,1,0xffffffff,0,1},{1,-1,-1,0,0,1,0xffffffff,1,1}};
    IDirect3DVertexBuffer9* retainedStream=nullptr;IDirect3DIndexBuffer9* retainedIndices=nullptr;
    ok(d->CreateVertexBuffer(sizeof(mesh),0,0,D3DPOOL_MANAGED,&retainedStream,nullptr));
    ok(d->CreateIndexBuffer(12,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&retainedIndices,nullptr));
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},{0,28,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    IDirect3DVertexDeclaration9* decl=nullptr;ok(d->CreateVertexDeclaration(elements,&decl));
    auto* diffuse=mipTexture(d,argv[3]);auto* white=texture(d,solid(1,1,1,1));
    auto* normals=texture(d,solid(0,0,1,1));const float normalizedZ=100.f/99.9f*(1-.1f);auto* zTexture=texture(d,solid(normalizedZ,0,0,0));
    IDirect3DPixelShader9 *local=nullptr,*oldLocal=nullptr;ok(d->CreatePixelShader(kLocalDirectShader,&local));auto oldLocalBytes=read(folder+"old-LocalDirect.bin");ok(d->CreatePixelShader((DWORD*)oldLocalBytes.data(),&oldLocal));
    IDirect3DPixelShader9* materialPS=nullptr;ok(d->CreatePixelShader(kMaterialCompositeShader,&materialPS));
    auto* flatColor=texture(d,solid(.3f,.3f,.3f,1));auto* zero=texture(d,solid(0,0,0,0));
    unsigned cases=0,totalChanged=0;
    for(const auto* testCase:{"terrain","terrain-layer","mapobjdiffuse_t1","detaildoodad","wall-composite","terrain-sm2","mapobjdiffuse_t1-sm2","detaildoodad-sm2"}){
        const bool layer=std::strcmp(testCase,"terrain-layer")==0,composite=std::strcmp(testCase,"wall-composite")==0;
        std::string familyName=layer?"terrain":composite?"mapobjspecular_t1":testCase;const bool sm2=familyName.size()>4&&familyName.substr(familyName.size()-4)=="-sm2";if(sm2)familyName.resize(familyName.size()-4);const auto* family=familyName.c_str();
        const bool terrain=std::strcmp(family,"terrain")==0,grass=std::strcmp(family,"detaildoodad")==0;
        const std::string pixel=composite?"mapobjcomposite":terrain?(layer?"terrain0":"terrain1"):grass?"detaildoodad":"mapobjdiffuse";
        auto vsBytes=read(folder+"shaders_vertex_"+(sm2?"vs_2_0_":"vs_3_0_")+family+".bls-"+(composite?"30":"0")+".bin"),psBytes=read(folder+"shaders_pixel_ps_3_0_"+pixel+".bls-0.bin");
        IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;ok(d->CreateVertexShader((DWORD*)vsBytes.data(),&vs));ok(d->CreatePixelShader((DWORD*)psBytes.data(),&ps));
        bump.registerVertex(vs,NorthlightWaterShaderPatch::fnv1a64((std::uint32_t*)vsBytes.data(),vsBytes.size()/4),(DWORD*)vsBytes.data(),vsBytes.size()/4);
        bump.registerPixel(ps,NorthlightWaterShaderPatch::fnv1a64((std::uint32_t*)psBytes.data(),psBytes.size()/4));
        ok(d->SetDepthStencilSurface(depth));ok(d->SetVertexDeclaration(decl));ok(d->SetVertexShader(vs));ok(d->SetPixelShader(ps));ok(d->SetViewport(&vp));
        float constants[34][4]={};
        if(terrain||grass){for(unsigned i=0;i<4;++i)constants[i][i]=1;constants[4][0]=1;constants[5][1]=1;constants[7][2]=.5f;constants[7][3]=1;constants[18][0]=constants[18][1]=.5f;constants[13][0]=constants[13][1]=.5f;constants[19][0]=constants[19][1]=.5f;constants[14][0]=constants[14][1]=.5f;}
        else{constants[31][0]=constants[32][1]=constants[33][2]=1;constants[2][0]=constants[3][1]=1;constants[4][3]=.5f;constants[5][3]=1;}
        // DetailDoodad's c13 is a DEF; its output UV is the vertex UV. Native fade is c8.
        constants[8][1]=1;constants[9][1]=1;constants[30][1]=1;constants[30][2]=1;ok(d->SetVertexShaderConstantF(0,constants[0],34));
        constants[25][0]=constants[25][1]=constants[25][2]=1;constants[12][1]=constants[12][2]=1;ok(d->SetVertexShaderConstantF(0,constants[0],34));
        for(unsigned st=0;st<2;++st){ok(d->SetTexture(st,st&&!composite?white:diffuse));for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER})d->SetSamplerState(st,state,D3DTEXF_LINEAR);d->SetSamplerState(st,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);d->SetSamplerState(st,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);}
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE})ok(d->SetRenderState(state,TRUE));for(auto state:{D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE})ok(d->SetRenderState(state,FALSE));d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL);d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
        const float layerMask[]={.5f,0,0,0};d->SetPixelShaderConstantF(0,layerMask,1);
        const float preservedConstant[]={.123f,.234f,.345f,.456f,.567f,.678f,.789f,.890f};ok(d->SetPixelShaderConstantF(20,preservedConstant,2));
        d->SetTexture(2,white);d->SetSamplerState(2,D3DSAMP_ADDRESSU,D3DTADDRESS_MIRROR);d->SetSamplerState(2,D3DSAMP_MINFILTER,D3DTEXF_POINT);
        ok(d->SetStreamSource(0,retainedStream,0,sizeof(V)));ok(d->SetIndices(retainedIndices));
        const D3DRENDERSTATETYPE restoreKeys[]={D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_COLORWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_BLENDOP,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_BLENDOPALPHA,D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA};
        DWORD preservedStates[13];for(unsigned i=0;i<13;++i)ok(d->GetRenderState(restoreKeys[i],&preservedStates[i]));
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff112233,1,0));
        if(grass){
            const float invisible[]={0,0,0,0};d->SetVertexShaderConstantF(9,invisible,1);ok(d->BeginScene());capture(vs,[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,mesh,sizeof(V));},true);ok(d->EndScene());assert(bump.texture());
            IDirect3DSurface9 *a=nullptr,*b=nullptr;ok(bump.texture()->GetSurfaceLevel(0,&a));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&b,nullptr));ok(d->GetRenderTargetData(a,b));D3DLOCKED_RECT lock{};ok(b->LockRect(&lock,nullptr,D3DLOCK_READONLY));for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x)assert(((std::uint16_t*)((char*)lock.pBits+y*lock.Pitch))[x*4+3]==0);b->UnlockRect();drop(a);drop(b);bump.endFrame();d->SetVertexShaderConstantF(9,constants[9],1);++cases;
        }
        ok(d->BeginScene());capture(vs,[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,mesh,sizeof(V));},true);ok(d->EndScene());assert(bump.texture());
        IDirect3DSurface9 *materialSurface=nullptr,*materialRead=nullptr;ok(bump.texture()->GetSurfaceLevel(0,&materialSurface));ok(d->CreateOffscreenPlainSurface(W,H,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&materialRead,nullptr));ok(d->GetRenderTargetData(materialSurface,materialRead));
        D3DLOCKED_RECT materialLock{};ok(materialRead->LockRect(&materialLock,nullptr,D3DLOCK_READONLY));std::vector<float> materialValues(W*H*4);unsigned covered=0;float minHeight=2,maxHeight=-1;
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){auto* p=(std::uint16_t*)((char*)materialLock.pBits+y*materialLock.Pitch)+x*4;for(unsigned k=0;k<4;++k)materialValues[(y*W+x)*4+k]=half(p[k]);if(half(p[3])>.01f){++covered;minHeight=std::min(minHeight,half(p[0]));maxHeight=std::max(maxHeight,half(p[0]));}}
        materialRead->UnlockRect();drop(materialRead);drop(materialSurface);std::printf("MATERIAL %s covered=%u height=%f..%f centerZ=%f\n",family,covered,minHeight,maxHeight,materialValues[(128*W+128)*4+1]);std::fflush(stdout);
        {std::ofstream file(folder+family+"-material.f32",std::ios::binary);file.write((char*)materialValues.data(),materialValues.size()*4);}
        // Capture must restore the original PS, RT and Z writes, and never colour the scene.
        for(unsigned i=0;i<13;++i){DWORD actual=0;ok(d->GetRenderState(restoreKeys[i],&actual));assert(actual==preservedStates[i]);}
        float restoredConstant[8];ok(d->GetPixelShaderConstantF(20,restoredConstant,2));assert(!std::memcmp(restoredConstant,preservedConstant,sizeof restoredConstant));
        IDirect3DBaseTexture9* restoredTexture=nullptr;ok(d->GetTexture(2,&restoredTexture));assert(restoredTexture==white);drop(restoredTexture);DWORD restoredAddress=0;ok(d->GetSamplerState(2,D3DSAMP_ADDRESSU,&restoredAddress));assert(restoredAddress==D3DTADDRESS_MIRROR);
        IDirect3DVertexShader9* restoredVS=nullptr;ok(d->GetVertexShader(&restoredVS));assert(restoredVS==vs);drop(restoredVS);
        IDirect3DSurface9 *restoredTarget=nullptr,*restoredDepth=nullptr;ok(d->GetRenderTarget(0,&restoredTarget));ok(d->GetDepthStencilSurface(&restoredDepth));assert(restoredTarget==target&&restoredDepth==depth);drop(restoredTarget);drop(restoredDepth);
        D3DVIEWPORT9 restoredViewport{};ok(d->GetViewport(&restoredViewport));assert(!std::memcmp(&vp,&restoredViewport,sizeof vp));
        IDirect3DVertexBuffer9* restoredStream=nullptr;IDirect3DIndexBuffer9* restoredIndices=nullptr;UINT restoredOffset=99,restoredStride=99;
        ok(d->GetStreamSource(0,&restoredStream,&restoredOffset,&restoredStride));ok(d->GetIndices(&restoredIndices));assert(restoredStream==retainedStream&&restoredIndices==retainedIndices&&restoredOffset==0&&restoredStride==sizeof(V));drop(restoredStream);drop(restoredIndices);
        IDirect3DPixelShader9* actualPS=nullptr;ok(d->GetPixelShader(&actualPS));assert(actualPS==ps);drop(actualPS);DWORD zwrite=0;ok(d->GetRenderState(D3DRS_ZWRITEENABLE,&zwrite));assert(zwrite);auto untouched=pixels(d,target);for(unsigned i=0;i<W*H;++i){assert(std::fabs(untouched[i*4]-17.f/255)<.0001f);assert(std::fabs(untouched[i*4+1]-34.f/255)<.0001f);}
        // Exercise the actual native diffuse draw, not only the normal replay.
        const float nativeConstants[4][4]={{.25f,.25f,.25f,.25f},{0,0,0,0},{0,0,0,0},{0,0,0,0}};d->SetPixelShaderConstantF(0,nativeConstants[0],4);
        const float preservedPOM[8]={7,8,9,10,11,12,13,14};d->SetPixelShaderConstantF(160,preservedPOM,2);d->SetTexture(14,white);d->SetSamplerState(14,D3DSAMP_MAXMIPLEVEL,3);d->SetSamplerState(14,D3DSAMP_ADDRESSU,D3DTADDRESS_MIRROR);
        std::vector<float> nativeBaseline;
        for(unsigned mode=0;mode<4;++mode){auto policy=settings;policy.autoBumpTerrain=policy.autoBumpWalls=mode==0?0:mode==1?1:2;policy.autoParallaxDepth=mode==3?0:100;bump.configure(policy);
            const auto vertexGets=meter.vertexGets,pixelGets=meter.pixelGets,constantSets=meter.constantSets;
            ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0,1,0));bool replaced=false;ok(d->BeginScene());ok(native([&]{IDirect3DPixelShader9* bound=nullptr;d->GetPixelShader(&bound);replaced=bound!=ps;drop(bound);return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,mesh,sizeof(V));}));ok(d->EndScene());
#ifndef NORTHLIGHT_MATERIAL_MIRROR
            // One classification read per shader, no repeat save read; the callback reads PS once.
            if(replaced){
#ifdef NORTHLIGHT_MATERIAL_STATE_BASELINE
                assert(meter.vertexGets-vertexGets==2&&meter.pixelGets-pixelGets==3&&meter.constantSets-constantSets==3);
#else
                assert(meter.vertexGets-vertexGets==1&&meter.pixelGets-pixelGets==2&&meter.constantSets-constantSets==2);
#endif
                std::printf("POM_STATE_CALLS family=%s vertexGets=%u pixelGets=%u constantSets=%u\n",testCase,meter.vertexGets-vertexGets,meter.pixelGets-pixelGets,meter.constantSets-constantSets);
            }
#endif
            assert(replaced==(!grass&&mode==2));auto values=pixels(d,target);unsigned difference=0;for(unsigned at=0;at<values.size();++at){assert(std::isfinite(values[at]));if(mode)difference+=std::fabs(values[at]-nativeBaseline[at])>.00001f;}
            {std::ofstream file(folder+testCase+"-native-"+std::to_string(mode)+".f32",std::ios::binary);file.write((char*)values.data(),values.size()*4);}
            if(!mode)nativeBaseline=values;else if(grass||mode!=2)assert(difference==0);else {assert(difference>100);totalChanged+=difference;}
            float restored[8];d->GetPixelShaderConstantF(160,restored,2);assert(!std::memcmp(restored,preservedPOM,sizeof restored));DWORD state=0;d->GetSamplerState(14,D3DSAMP_MAXMIPLEVEL,&state);assert(state==3);d->GetSamplerState(14,D3DSAMP_ADDRESSU,&state);assert(state==D3DTADDRESS_MIRROR);IDirect3DBaseTexture9* texture14=nullptr;d->GetTexture(14,&texture14);assert(texture14==white);drop(texture14);
            d->GetPixelShader(&actualPS);assert(actualPS==ps);drop(actualPS);++cases;
        }bump.configure(settings);
#ifndef NORTHLIGHT_MATERIAL_MIRROR
        // Every query/upload failure keeps one native callback with original bindings/constants.
        if(!grass)for(int fault=0;fault<5;++fault){meter.fault=fault;unsigned callbacks=0;
            ok(native([&]{++callbacks;IDirect3DVertexShader9* actual=nullptr;IDirect3DPixelShader9* pixel=nullptr;
                ok(d->GetVertexShader(&actual));ok(d->GetPixelShader(&pixel));assert(actual==vs&&pixel==ps);drop(actual);drop(pixel);
                float restored[8];ok(d->GetPixelShaderConstantF(160,restored,2));assert(!std::memcmp(restored,preservedPOM,sizeof restored));return S_OK;}));
            assert(callbacks==1&&meter.fault==-1);
        }
#endif
        if(terrain&&!sm2&&!layer){
            std::vector<std::uint32_t> shadowVS,shadowPS;NorthlightTerrainCoverage::PixelInfo info;
            assert(NorthlightTerrainCoverage::vertex(reinterpret_cast<std::uint32_t*>(vsBytes.data()),vsBytes.size()/4,shadowVS));
            assert(NorthlightTerrainCoverage::pixel(reinterpret_cast<std::uint32_t*>(psBytes.data()),psBytes.size()/4,shadowPS,info));
            IDirect3DVertexShader9* coveredVS=nullptr;IDirect3DPixelShader9* coveredPS=nullptr;
            ok(d->CreateVertexShader(reinterpret_cast<DWORD*>(shadowVS.data()),&coveredVS));const auto baselineShadow=d->CreatePixelShader(reinterpret_cast<DWORD*>(shadowPS.data()),&coveredPS);
            if(FAILED(baselineShadow)){std::printf("BASE_SHADOW_UNSUPPORTED hr=%08lx; original coverage shader, POM not applied\n",(unsigned long)baselineShadow);drop(coveredVS);}else{
            const float coverage[24]={};d->SetPixelShaderConstantF(info.constants,coverage,6);d->SetVertexShader(coveredVS);d->SetPixelShader(coveredPS);
            ok(d->BeginScene());ok(native([&]{IDirect3DPixelShader9* bound=nullptr;d->GetPixelShader(&bound);assert(bound!=coveredPS);drop(bound);return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,mesh,sizeof(V));},vs,ps));ok(d->EndScene());
            float restoredCoverage[24];d->GetPixelShaderConstantF(info.constants,restoredCoverage,6);assert(!std::memcmp(coverage,restoredCoverage,sizeof coverage));
            d->GetPixelShader(&actualPS);assert(actualPS==coveredPS);drop(actualPS);d->SetVertexShader(vs);d->SetPixelShader(ps);drop(coveredVS);drop(coveredPS);++cases;}
        }
        // Independent captured visibility: cavity, sunlight, moonlight, normal-only and zero-strength.
        for(unsigned mode=0;mode<7;++mode){
            bump.endFrame();auto policy=settings;policy.autoParallaxShadows=mode!=0&&mode!=1&&mode!=6;policy.autoCavity=mode==1||mode==6;
            if(mode==4)policy.autoBumpTerrain=policy.autoBumpWalls=1;
            if(mode==5)policy.autoParallaxShadowStrength=0;
            if(mode==6)policy.autoBumpStrength=0;
            float light[4]={.98f,0,.2f,mode==3?2.f:1.f};bump.configure(policy);bump.setLight(light);
            ok(d->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,1,0));ok(d->BeginScene());capture(vs,[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,mesh,sizeof(V));},true);ok(d->EndScene());
            assert(bump.texture());
            const bool cavity=mode==1||mode==6,shadow=!grass&&(mode==2||mode==3);
            // Normals-only and grass require no auxiliary pass without cavity.
            if(!cavity&&!shadow)assert(!bump.occlusionTexture());else{
                auto counts=occludedPixels(d,bump.occlusionTexture());
                std::printf("OCCLUSION %s mode=%u sun=%u moon=%u cavity=%u\n",family,mode,counts[0],counts[1],counts[2]);std::fflush(stdout);
                if(mode==1||mode==6){assert(counts[2]>100&&counts[0]==0&&counts[1]==0);if(mode==6)assert(bump.gain()==0&&bump.texture());}
                else if(mode==2)assert(counts[0]>0&&counts[1]==0&&counts[2]==0);
                else if(mode==3)assert(counts[1]>0&&counts[0]==0&&counts[2]==0);
            }
            IDirect3DSurface9* auxiliary=nullptr;d->GetRenderTarget(1,&auxiliary);assert(!auxiliary);++cases;
        }
        bump.endFrame();bump.configure(settings);const float light[4]={.8f,0,.6f,1};bump.setLight(light);ok(d->BeginScene());capture(vs,[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,mesh,sizeof(V));},true);ok(d->EndScene());
        ok(d->SetDepthStencilSurface(nullptr));d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetPixelShader(local);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);vp.MaxZ=1;d->SetViewport(&vp);
        float c[74][4]={};c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=.1f;c[0][3]=100;c[1][0]=c[1][1]=1;c[1][2]=-1;c[2][0]=1;for(unsigned i=0;i<4;++i)c[3+i][i]=1;c[36][0]=.8f;c[36][2]=.2f;c[36][3]=10;c[44][0]=.7f;c[44][1]=.6f;c[44][2]=.5f;c[44][3]=.1f;c[52][0]=1;c[52][1]=1;c[73][1]=48;d->SetPixelShaderConstantF(0,c[0],74);
        c[33][0]=W;c[33][1]=H;c[33][2]=W/2;c[33][3]=H/2;
        d->SetTexture(1,zTexture);d->SetTexture(9,bump.texture());d->SetTexture(12,white);d->SetTexture(14,normals);
        for(unsigned st:{1u,9u,12u,14u}){d->SetSamplerState(st,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(st,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(st,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(st,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(st,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);}
        const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{W-.5f,-.5f,0,1,1,0},{-.5f,H-.5f,0,1,0,1},{W-.5f,H-.5f,0,1,1,1}};
        std::vector<float> baseline,wrongDepth;
        for(unsigned mode=0;mode<5;++mode){c[73][0]=mode==1||mode==2?1:0;if(mode==2)c[0][2]=.2f;if(mode==4)c[0][2]=.1f; // wrong final depth must reject the stored material
            d->SetPixelShader(mode?local:oldLocal);
            d->SetPixelShaderConstantF(0,c[0],74);ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());auto out=pixels(d,target);
            unsigned changed=0;for(unsigned at=0;at<out.size();++at){assert(std::isfinite(out[at]));if(!mode)continue;changed+=std::fabs(out[at]-baseline[at])>.00001f;}
            if(!mode)baseline=out;else if(mode==1){assert(changed>100);totalChanged+=changed;std::ofstream file(folder+family+"-lit.f32",std::ios::binary);file.write((char*)out.data(),out.size()*4);}else if(mode==2)wrongDepth=out;else if(mode==3){for(unsigned at=0;at<out.size();++at)assert(std::fabs(out[at]-wrongDepth[at])<.000001f);}else{for(unsigned at=0;at<out.size();++at)assert(std::fabs(out[at]-baseline[at])<.000001f);}
            if(!mode){std::ofstream file(folder+family+"-flat.f32",std::ios::binary);file.write((char*)out.data(),out.size()*4);}++cases;
        }
        // Exercise the final full-resolution shader, not only the intermediate LocalDirect buffer.
        float full[79][4]={};std::memcpy(full,c,sizeof c);full[20][2]=1;full[30][2]=1;
        full[74][0]=.8f;full[74][2]=.6f;full[75][0]=-.8f;full[75][2]=.6f;
        std::vector<float> sunDetail;
        d->SetPixelShader(materialPS);d->SetTexture(0,flatColor);d->SetTexture(5,diffuse);d->SetTexture(10,white);
        for(unsigned sampler:{0u,4u,5u,8u,10u,12u,14u}){
            d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
            d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        }
        for(unsigned mode=0;mode<10;++mode){
            full[0][2]=mode==4?.2f:.1f;full[73][0]=mode==0?0.f:1.f;full[73][1]=mode==8?.5f:48.f;
            full[6][0]=mode==9?10000.f:0;full[6][1]=mode==9?-7000.f:0;
            full[76][0]=mode==2||mode==7?0.f:1.f;full[76][1]=full[76][0]*.8f;full[76][2]=full[76][0]*.6f;
            full[77][0]=mode==7?1.f:0;full[77][1]=full[77][0]*.8f;full[77][2]=full[77][0]*.6f;
            d->SetTexture(4,mode==5?zero:white);d->SetTexture(8,mode==3?zero:white);d->SetTexture(9,mode==6?zero:bump.texture());
            d->SetPixelShaderConstantF(0,full[0],79);ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());auto out=pixels(d,target);
            unsigned changed=0;for(unsigned at=0;at<out.size();++at){assert(std::isfinite(out[at]));changed+=std::fabs(out[at]-(at%4==3?1.f:.3f))>.00001f;}
            if(mode==1||mode==7||mode==9){assert(changed>100);if(mode==1)sunDetail=out;if(mode==9)assert(out==sunDetail);}
            else assert(changed==0);
            if(mode==0||mode==1){std::ofstream file(folder+family+(mode==0?"-full-flat.f32":"-full-lit.f32"),std::ios::binary);file.write((char*)out.data(),out.size()*4);}
            ++cases;
        }
        // Final shading must preserve inactive lights, fog, uncovered pixels and geometry behind the capture.
        auto* blockedSun=texture(d,solid(0,1,1,1));auto* blockedMoon=texture(d,solid(1,0,1,1));auto* cavity=texture(d,solid(1,1,.4f,1));
        float occFull[79][4]={};std::memcpy(occFull,full,sizeof full);occFull[0][2]=.1f;occFull[6][0]=occFull[6][1]=0;occFull[73][0]=0;occFull[73][1]=48;occFull[74][0]=0;occFull[74][2]=1;occFull[75][0]=0;occFull[75][2]=1;occFull[76][0]=occFull[76][1]=occFull[76][2]=.2f;occFull[77][0]=occFull[77][1]=occFull[77][2]=0;
        for(unsigned sampler:{6u}){d->SetSamplerState(sampler,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(sampler,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(sampler,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);}
        for(unsigned mode=0;mode<8;++mode){occFull[78][0]=mode==0?0.f:1.f;occFull[0][2]=mode==4?.2f:.1f;occFull[18][0]=occFull[18][1]=occFull[18][2]=mode==6||mode==7?.2f:0;occFull[18][3]=1;
            d->SetTexture(6,mode==1?blockedSun:mode==2?blockedMoon:mode==3?zero:mode==6||mode==7?cavity:blockedSun);d->SetTexture(4,mode==5?zero:white);d->SetTexture(8,mode==7?zero:white);d->SetTexture(9,bump.texture());d->SetTexture(14,normals);
            d->SetPixelShaderConstantF(0,occFull[0],79);ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());auto out=pixels(d,target);unsigned changed=0;
            for(unsigned at=0;at<out.size();++at){assert(std::isfinite(out[at]));if(at%4!=3){assert(out[at]<=.30001f);changed+=std::fabs(out[at]-.3f)>.00001f;}else assert(out[at]==1);}
            if(mode==1||mode==6||mode==7)assert(changed>100);else assert(changed==0);++cases;
        }
        d->SetTexture(6,nullptr);drop(blockedSun);drop(blockedMoon);drop(cavity);d->SetTexture(9,nullptr);bump.endFrame();drop(vs);drop(ps);
    }
    bump.reset();
#ifdef NORTHLIGHT_MATERIAL_MIRROR
    assert(!extension.audit());std::printf("MIRROR_CAPTURE_PASS raw=%u forwarded=%llu answered=%llu invalidations=%llu renderGets=%u constantGets=%u\n",unsigned(mirror.rawScopes),(unsigned long long)mirror.forwarded,(unsigned long long)mirror.answered,(unsigned long long)mirror.invalidations,meter.renderGets,meter.constantGets);
#endif
    std::printf("MATERIAL_CALLS samplerGets=%u samplerSets=%u constantSets=%u\n",meter.samplerGets,meter.samplerSets,meter.constantSets);std::printf("AUTOBUMP_GPU_PASS cases=%u changed_components=%u\n",cases,totalChanged);drop(materialPS);drop(flatColor);drop(zero);drop(retainedStream);drop(retainedIndices);drop(local);drop(oldLocal);drop(zTexture);drop(normals);drop(white);drop(diffuse);drop(decl);drop(depth);drop(target);rawDevice->Release();api->Release();DestroyWindow(window);return 0;
}
