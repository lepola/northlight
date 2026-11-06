// Original stock WMO shaders are loaded from the tester's client, never embedded.
#undef NDEBUG
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include "displacement_geometry.h"
#include "displacement_shader_patch.h"

using Mesh=NorthlightDrawSnapshot::Mesh;
using Layout=NorthlightWmoVertexColor::Layout;
static void checked(HRESULT h,unsigned line){if(FAILED(h))std::printf("D3D_FAILURE line=%u HRESULT=%08lx\n",line,(unsigned long)h);assert(SUCCEEDED(h));}
#define ok(expr) checked((expr),__LINE__)
template<class T>static void drop(T*& p){if(p)p->Release();p=nullptr;}
static void logf(const char*,...){}
#include "quality_settings.h"
#include "auto_displacement_renderer.h"
#include "forwarders.h"
static std::vector<uint32_t> words(const std::string& path){
    std::ifstream f(path,std::ios::binary);assert(f);std::vector<char> b((std::istreambuf_iterator<char>(f)),{});
    assert(b.size()%4==0);std::vector<uint32_t> w(b.size()/4);std::memcpy(w.data(),b.data(),b.size());return w;
}
struct Vertex {float p[3],n[3];DWORD color;float uv[2];};
static constexpr D3DVERTEXELEMENT9 elements[]={
    {0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},
    {0,24,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},{0,28,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
static Mesh quad(){
    const Vertex v[]={{{-8,-8,8},{0,0,-1},0x3f101080,{0,0}},{{8,-8,8},{.2f,0,-.98f},0x7fffffff,{1,0}},
                      {{-8,8,8},{0,.3f,-.95f},0xbfffffff,{0,1}},{{8,8,8},{-.1f,-.1f,-.99f},0xff000000,{1,1}}};
    Mesh m;m.topology=D3DPT_TRIANGLELIST;m.indexed=true;m.vertexCount=4;m.primitiveCount=2;m.streams[0].stride=sizeof(Vertex);
    m.streams[0].bytes.resize(sizeof v);std::memcpy(m.streams[0].bytes.data(),v,sizeof v);m.indices={0,1,2,2,1,3};return m;
}
static std::vector<Mesh> fixtures(const char* path){
    std::ifstream f(path,std::ios::binary);assert(f);uint32_t header[2];f.read((char*)header,sizeof header);assert(header[0]==0x314d5746&&header[1]==2);
    std::vector<Mesh> result{quad()};
    for(unsigned i=0;i<header[1];++i){Mesh m;m.topology=D3DPT_TRIANGLELIST;m.indexed=true;m.vertexCount=6;m.primitiveCount=2;m.streams[0].stride=sizeof(Vertex);
        m.streams[0].bytes.resize(6*sizeof(Vertex));f.read((char*)m.streams[0].bytes.data(),m.streams[0].bytes.size());assert(f);m.indices={0,1,2,3,4,5};result.push_back(std::move(m));}
    return result;
}
// Export the native FOG output as a test-only TEXCOORD for a pixel probe.
// Every arithmetic instruction and its constants remain unchanged.
static std::vector<uint32_t> fogProbe(std::vector<uint32_t> w){
    using namespace NorthlightWaterShaderPatch;const unsigned major=(w[0]>>8)&255;
    for(size_t at=1;at<w.size();){const unsigned op=w[at]&65535,n=op==65534?(w[at]>>16)&32767:(w[at]>>24)&15;if(op==65535)break;
        if(major==3&&op==31&&(w[at+1]&15)==11)w[at+1]=0x800f0005u;
        else if(major==2&&op!=65534&&op!=31&&op!=81&&n&&regType(w[at+1])==4&&regIndex(w[at+1])==1)w[at+1]=replaceReg(w[at+1],6,7);
        at+=n+1;
    }return w;
}
struct Harness {
    IDirect3DDevice9* d;IDirect3DSurface9 *target=nullptr,*read=nullptr;IDirect3DPixelShader9 *color=nullptr,*probe[2]={};IDirect3DTexture9* height=nullptr;
    explicit Harness(IDirect3DDevice9* device):d(device){
        const DWORD ps[]={0xffff0300,0x0200001f,0x8000000a,0x900f0000,0x02000001,0x800f0800,0x90e40000,0xffff};ok(d->CreatePixelShader(ps,&color));
        for(unsigned sm=0;sm<2;++sm){const DWORD p[]={0xffff0300,0x0200001f,0x8000000a,0x900f0000,0x0200001f,0x80000005,0x90030001,
            0x0200001f,0x80000005u|((sm?15u:7u)<<16),0x90010002,0x02000001,0x80010800,0x90000001,
            0x02000001,0x80020800,0x90ff0000,0x02000001,0x80040800,0x90000002,0x02000001,0x80080800,0x90ff0000,0xffff};ok(d->CreatePixelShader(p,&probe[sm]));}
        ok(d->CreateRenderTarget(256,256,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
        ok(d->CreateOffscreenPlainSurface(256,256,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr));
        ok(d->CreateTexture(2,2,1,0,D3DFMT_R32F,D3DPOOL_MANAGED,&height,nullptr));D3DLOCKED_RECT lock{};ok(height->LockRect(0,&lock,nullptr,0));
        for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)((float*)((char*)lock.pBits+y*lock.Pitch))[x]=.5f;ok(height->UnlockRect(0));
        ok(d->SetRenderTarget(0,target));ok(d->SetDepthStencilSurface(nullptr));
        for(const auto pair:{std::pair{D3DRS_ZENABLE,DWORD(FALSE)},std::pair{D3DRS_LIGHTING,DWORD(FALSE)},std::pair{D3DRS_FOGENABLE,DWORD(FALSE)},
            std::pair{D3DRS_ALPHABLENDENABLE,DWORD(FALSE)},std::pair{D3DRS_ALPHATESTENABLE,DWORD(FALSE)},std::pair{D3DRS_CULLMODE,DWORD(D3DCULL_NONE)},
            std::pair{D3DRS_SRGBWRITEENABLE,DWORD(FALSE)},std::pair{D3DRS_COLORWRITEENABLE,DWORD(15)}})ok(d->SetRenderState(pair.first,pair.second));
    }
    ~Harness(){drop(target);drop(read);drop(color);for(auto*& p:probe)drop(p);drop(height);}
    std::vector<unsigned char> render(const Mesh& m,IDirect3DVertexDeclaration9* decl,IDirect3DVertexShader9* vs,float (&bank)[256][4],
        unsigned probeMajor=0,const NorthlightDisplacementPatch::Binding* binding=nullptr,bool gpu=false){
        ok(d->SetVertexDeclaration(decl));ok(d->SetVertexShader(vs));ok(d->SetPixelShader(probeMajor?probe[probeMajor-2]:color));ok(d->SetVertexShaderConstantF(0,bank[0],256));
        if(binding){float controls[20]{};controls[0]=controls[5]=controls[10]=1;controls[15]=1;controls[16]=.5f;ok(d->SetVertexShaderConstantF(binding->constant,controls,binding->count));
            if(gpu){const unsigned sampler=D3DVERTEXTEXTURESAMPLER0+binding->sampler;ok(d->SetTexture(sampler,height));
                for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(sampler,state,D3DTEXF_LINEAR));ok(d->SetSamplerState(sampler,D3DSAMP_MIPFILTER,D3DTEXF_NONE));}}
        ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->BeginScene());ok(d->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,m.vertexCount,m.primitiveCount,
            m.indices.data(),D3DFMT_INDEX32,m.streams[0].bytes.data(),m.streams[0].stride));ok(d->EndScene());ok(d->GetRenderTargetData(target,read));
        D3DLOCKED_RECT lock{};ok(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> image(256*256*4);
        for(unsigned y=0;y<256;++y)std::memcpy(image.data()+y*256*4,(char*)lock.pBits+y*lock.Pitch,256*4);ok(read->UnlockRect());return image;
    }
};
static unsigned compared=0,maxError=0,negative=0;
static const char* currentShader=nullptr;static unsigned currentFixture=0,currentState=0;static bool currentProbe=false,currentGpu=false;
static void compare(const std::vector<unsigned char>& reference,const std::vector<unsigned char>& result){
    unsigned pixels=0;
    for(unsigned y=2;y<254;++y)for(unsigned x=2;x<254;++x){const unsigned p=4*(y*256+x);
        // Avoid only the one-pixel raster coverage edge; source attributes and
        // native clamp behavior are checked against the same-format GPU oracle.
        bool interior=true;for(int oy=-1;oy<=1;++oy)for(int ox=-1;ox<=1;++ox)interior&=reference[4*((y+oy)*256+x+ox)+3]>0;
        if(!interior)continue;++pixels;for(unsigned c=0;c<4;++c){const unsigned error=unsigned(std::abs(int(reference[p+c])-int(result[p+c])));maxError=std::max(maxError,error);
            if(error>1)std::printf("MISMATCH shader=%s fixture=%u state=%u probe=%u gpu=%u pixel=%u,%u lane=%u reference=%u actual=%u\n",currentShader,currentFixture,currentState,currentProbe,currentGpu,x,y,c,reference[p+c],result[p+c]);assert(error<=1);}
    }
    assert(pixels>128);++compared;
}
static void run(Harness& h,const std::string& file,const std::vector<Mesh>& meshes){
    currentShader=file.c_str();
    std::printf("WMO_SHADER_BEGIN %s\n",file.c_str());
    using namespace NorthlightWaterShaderPatch;const auto native=words(file);Layout layout;assert(NorthlightWmoVertexColor::analyse(native.data(),native.size(),layout));
    for(bool probe:{false,true}){
        const auto source=probe?fogProbe(native):native;IDirect3DVertexShader9* original=nullptr;ok(h.d->CreateVertexShader((DWORD*)source.data(),&original));
        for(bool gpu:{false,true}){if(gpu&&layout.color.major==2)continue;
            NorthlightDisplacementPatch::Binding binding;std::vector<uint32_t> displaced,preserved;
            assert(NorthlightDisplacementPatch::patch(source.data(),source.size(),displaced,binding,false,gpu));assert(NorthlightWmoVertexColor::patch(displaced,layout,preserved));
            IDirect3DVertexShader9 *fixed=nullptr,*old=nullptr;ok(h.d->CreateVertexShader((DWORD*)preserved.data(),&fixed));ok(h.d->CreateVertexShader((DWORD*)displaced.data(),&old));
            for(unsigned fixture=0;fixture<meshes.size();++fixture){const auto& m=meshes[fixture];std::vector<std::array<float,2>> uv;
                for(unsigned i=0;i<m.vertexCount;++i){Vertex v;std::memcpy(&v,m.streams[0].bytes.data()+i*sizeof(Vertex),sizeof v);uv.push_back({v.uv[0],v.uv[1]});}
                NorthlightDisplacementGeometry::Height height;height.width=height.height=2;height.values.assign(4,.5f);Mesh fixedMesh,oldMesh;
                assert(NorthlightDisplacementGeometry::build(m,elements,5,uv,height,16,false,true,true,fixedMesh,nullptr,nullptr,nullptr,0,gpu,&layout));
                assert(NorthlightDisplacementGeometry::build(m,elements,5,uv,height,16,false,true,true,oldMesh,nullptr,nullptr,nullptr,0,gpu));
                auto decl=std::vector<D3DVERTEXELEMENT9>(std::begin(elements),std::end(elements));decl.back()={0,sizeof(Vertex),D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_TEXCOORD,7};
                if(gpu)decl.push_back({0,sizeof(Vertex)+12,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,6});decl.push_back(D3DDECL_END());
                IDirect3DVertexDeclaration9 *coarseDecl=nullptr,*oldDecl=nullptr,*fixedDecl=nullptr;ok(h.d->CreateVertexDeclaration(elements,&coarseDecl));ok(h.d->CreateVertexDeclaration(decl.data(),&oldDecl));decl.pop_back();
                for(unsigned i=0;i<layout.attributes();++i)decl.push_back({0,WORD(sizeof(Vertex)+(gpu?20:12)+layout.offset(i)),BYTE(i+1==layout.attributes()?D3DDECLTYPE_FLOAT3:layout.type(i%layout.count)),0,D3DDECLUSAGE_TEXCOORD,BYTE(layout.semantic[i])});
                decl.push_back(D3DDECL_END());ok(h.d->CreateVertexDeclaration(decl.data(),&fixedDecl));
                // Current native constants change on this same cached mesh.
                for(unsigned state=0;state<4;++state){float bank[256][4]{};bank[2][0]=bank[3][1]=.035f;bank[4][3]=.5f;bank[5][3]=1;
                    currentFixture=fixture;currentState=state;currentProbe=probe;currentGpu=gpu;
                    bank[10][0]=.07f;bank[10][1]=.04f;bank[10][2]=.08f;bank[11][0]=.1f;bank[11][1]=.2f;bank[11][2]=.3f;bank[12][2]=1;
                    for(unsigned i=0;i<4;++i){bank[17+i][0]=state?1.f:.0f;bank[17+i][1]=state?.55f+.1f*i:0;bank[17+i][2]=state?.2f:0;
                        bank[21+i][0]=state==2?float(i)-4:float(i);bank[21+i][1]=state==2?3:float(i)*.5f;bank[21+i][2]=7-float(i)*.2f;
                        bank[25][i]=.2f;bank[26][i]=.7f;bank[27][i]=.03f;}
                    bank[30][0]=.04f;bank[30][1]=.2f;bank[30][2]=1.3f;bank[31][0]=bank[32][1]=bank[33][2]=1;
                    if(state==3){bank[31][0]=.75f;bank[31][1]=.125f;bank[32][1]=1.25f;bank[33][2]=.875f;bank[29][0]=.1f;bank[29][1]=-.06f;}
                    const unsigned major=probe?layout.color.major:0;auto reference=h.render(m,coarseDecl,original,bank,major);
                    if(probe){
                        // UV and FOG retain their existing displaced-geometry
                        // evaluation; native fog itself is nonlinear. Their
                        // oracle is the identical old subdivided native draw.
                        // COLOR0 alpha retains the coarse native corner oracle.
                        const auto displacedReference=h.render(oldMesh,oldDecl,old,bank,major,&binding,gpu);
                        for(size_t i=0;i<reference.size();i+=4){reference[i]=displacedReference[i];reference[i+2]=displacedReference[i+2];}
                    }
                    compare(reference,h.render(fixedMesh,fixedDecl,fixed,bank,major,&binding,gpu));
                    if(!probe&&state==1&&fixture==0){const auto baseline=h.render(oldMesh,oldDecl,old,bank,0,&binding,gpu);unsigned changed=0;
                        for(size_t i=0;i<baseline.size();i+=4)changed+=std::abs(int(baseline[i+2])-int(reference[i+2]))>8;negative+=changed>1024;}
                }
                drop(coarseDecl);drop(oldDecl);drop(fixedDecl);
            }
            drop(fixed);drop(old);
        }
        drop(original);
    }
    std::printf("WMO_SHADER_PASS %s inputs=%u packedBytes=%u comparisons=%u\n",file.c_str(),layout.count,layout.bytes(),compared);
}
struct DisplacementStateTestAccess {
    static auto current(NorthlightAutoDisplacement& g){return g.current;}
    static uint64_t builds(NorthlightAutoDisplacement& g){return g.builds;}
};
struct GeometryDevice:ForwardIDirect3DDevice9 {
    bool cpu=false;unsigned draws=0;
    explicit GeometryDevice(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* caps)override{auto hr=real->GetDeviceCaps(caps);if(SUCCEEDED(hr)&&cpu)caps->VertexTextureFilterCaps=0;return hr;}
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE t,INT b,UINT m,UINT v,UINT i,UINT n)override{++draws;return real->DrawIndexedPrimitive(t,b,m,v,i,n);}
};
static void integration(Harness& h,const std::vector<uint32_t>& code){
    auto mesh=quad();for(unsigned i=0;i<4;++i){const float n[]={0,0,-1};std::memcpy(mesh.streams[0].bytes.data()+i*sizeof(Vertex)+12,n,sizeof n);}
    std::vector<uint32_t> capture;assert(!NorthlightWaterShaderPatch::patch(code.data(),code.size(),capture));
    IDirect3DVertexShader9 *original=nullptr,*captured=nullptr,*lazy=nullptr;ok(h.d->CreateVertexShader((DWORD*)code.data(),&original));
    ok(h.d->CreateVertexShader((DWORD*)capture.data(),&captured));ok(h.d->CreateVertexShader((DWORD*)capture.data(),&lazy));
    IDirect3DVertexDeclaration9* declaration=nullptr;ok(h.d->CreateVertexDeclaration(elements,&declaration));
    IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;void* bytes=nullptr;
    ok(h.d->CreateVertexBuffer(mesh.streams[0].bytes.size(),0,0,D3DPOOL_MANAGED,&vb,nullptr));ok(vb->Lock(0,mesh.streams[0].bytes.size(),&bytes,0));std::memcpy(bytes,mesh.streams[0].bytes.data(),mesh.streams[0].bytes.size());ok(vb->Unlock());
    ok(h.d->CreateIndexBuffer(mesh.indices.size()*4,0,D3DFMT_INDEX32,D3DPOOL_MANAGED,&ib,nullptr));ok(ib->Lock(0,mesh.indices.size()*4,&bytes,0));std::memcpy(bytes,mesh.indices.data(),mesh.indices.size()*4);ok(ib->Unlock());
    IDirect3DTexture9* diffuse=nullptr;ok(h.d->CreateTexture(8,8,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&diffuse,nullptr));D3DLOCKED_RECT lock{};ok(diffuse->LockRect(0,&lock,nullptr,0));
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)((DWORD*)((char*)lock.pBits+y*lock.Pitch))[x]=0xff808080;ok(diffuse->UnlockRect(0));
    unsigned cases=0;
    for(bool cpu:{false,true}){GeometryDevice device(h.d);device.cpu=cpu;NorthlightAutoDisplacement geometry(&device);NorthlightQuality::Settings settings;
        settings.autoDisplacementStrength=100;settings.autoDisplacementDistance=12;settings.autoDisplacementDetail=4;geometry.configure(settings);
        geometry.tracking([](void* p,bool){return uint64_t(reinterpret_cast<uintptr_t>(p));},[](void*,bool){return uint64_t(1);},nullptr);
        geometry.registerMaterial(original,(DWORD*)code.data(),code.size(),2);geometry.registerVariant(captured,(DWORD*)capture.data(),capture.size(),false,true);
        for(unsigned variant=0;variant<3;++variant)for(unsigned state=0;state<3;++state){
            float bank[256][4]{};bank[2][0]=bank[3][1]=.08f;bank[4][3]=.5f;bank[5][3]=1;bank[10][0]=bank[10][1]=bank[10][2]=.05f;
            bank[17][0]=state?.9f:.1f;bank[17][1]=state?.4f:.8f;bank[17][2]=.2f;bank[21][0]=state==2?3:0;bank[21][2]=7;bank[25][0]=.2f;bank[26][0]=.7f;bank[27][0]=.03f;
            bank[30][1]=bank[30][2]=1;bank[31][0]=bank[32][1]=bank[33][2]=1;
            const auto reference=h.render(mesh,declaration,original,bank);h.d->SetTexture(0,diffuse);h.d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_WRAP);h.d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_WRAP);
            ok(h.d->SetStreamSource(0,vb,0,sizeof(Vertex)));ok(h.d->SetIndices(ib));ok(h.d->SetVertexDeclaration(declaration));ok(h.d->SetVertexShader(original));ok(h.d->SetVertexShaderConstantF(0,bank[0],256));
            h.d->SetRenderState(D3DRS_ZENABLE,TRUE);h.d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);h.d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
            geometry.beginDraw();NorthlightAutoDisplacement::Args args{{D3DPT_TRIANGLELIST,0,0,4,0,2,true}};assert(geometry.prepare(original,2,3,args));
            const auto* entry=DisplacementStateTestAccess::current(geometry);assert(entry&&entry->colorCount==3&&entry->vertices>4&&entry->bytes>=entry->vertices*entry->stride);
            assert(bool(entry->height)==!cpu);assert(DisplacementStateTestAccess::builds(geometry)==1);
            auto* bound=variant==0?original:variant==1?captured:lazy;ok(h.d->SetVertexShader(bound));h.d->SetRenderState(D3DRS_ZENABLE,FALSE);
            ok(h.d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(h.d->BeginScene());ok(geometry.draw([&]{return h.d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,4,0,2);},true));ok(h.d->EndScene());assert(device.draws==cases%9+1);
            ok(h.d->GetRenderTargetData(h.target,h.read));ok(h.read->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> result(256*256*4);
            for(unsigned y=0;y<256;++y)std::memcpy(result.data()+y*256*4,(char*)lock.pBits+y*lock.Pitch,256*4);ok(h.read->UnlockRect());
            currentShader="production prepare/drawBound";currentFixture=0;currentState=state;currentProbe=false;currentGpu=!cpu;compare(reference,result);
            IDirect3DVertexShader9* restored=nullptr;ok(h.d->GetVertexShader(&restored));assert(restored==bound);drop(restored);
            float after[256][4];ok(h.d->GetVertexShaderConstantF(0,after[0],256));assert(!std::memcmp(after,bank,sizeof bank));
            IDirect3DVertexBuffer9* source=nullptr;UINT offset=0,stride=0;ok(h.d->GetStreamSource(0,&source,&offset,&stride));assert(source==vb&&offset==0&&stride==sizeof(Vertex));drop(source);
            geometry.endFrame();++cases;
        }
    }
    drop(diffuse);drop(vb);drop(ib);drop(declaration);drop(original);drop(captured);drop(lazy);
    std::printf("WMO_PRODUCTION_INTEGRATION_PASS cases=%u cpuGpuHeight=1 nativeCaptureLazyVariants=1 cachedGeometryCurrentLights=1 stateRestored=1\n",cases);
}
int main(int argc,char** argv){
    assert(argc==4);std::setvbuf(stdout,nullptr,_IONBF,0);WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightWmoColor";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,256,256,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=256;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    {Harness harness(d);const auto meshes=fixtures(argv[3]);std::ifstream list(argv[2]);assert(list);std::string file,oneLight;unsigned count=0;while(std::getline(list,file)){const auto code=words(file);
            if(code[0]==0xfffe0300&&code.size()==170)oneLight=file;run(harness,file,meshes);++count;}
        assert(count==72&&compared==2592&&negative>=24);assert(!oneLight.empty());integration(harness,words(oneLight));
        assert(compared==2610);std::printf("WMO_VERTEX_COLOR_GPU_PASS shaders=%u comparisons=%u nativeColorAlphaUVFog=1 currentBanks=4 originalSeamPairs=2 maxByteError=%u baselineFailureControls=%u\n",count,compared,maxError,negative);}
    d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
