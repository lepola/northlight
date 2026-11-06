// Reuse the existing full material pixel/binding oracle for normal invocations.
#define NORTHLIGHT_OBLIQUE_ENTRY material_pixel_oracle_main
#include "auto_normal_oblique_gpu.cpp"
#undef NORTHLIGHT_OBLIQUE_ENTRY

struct VariantDevice:ForwardIDirect3DDevice9 {
    unsigned creates=0;bool fail=false;
    explicit VariantDevice(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* caps)override{auto hr=real->GetDeviceCaps(caps);if(SUCCEEDED(hr))caps->VertexTextureFilterCaps=0;return hr;}
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* words,IDirect3DVertexShader9** out)override{
        ++creates;if(fail){*out=nullptr;return E_FAIL;}return real->CreateVertexShader(words,out);
    }
};
struct VariantSource:IDirect3DVertexShader9 {
    IDirect3DVertexShader9* real;ULONG refs=1;unsigned& destroyed;unsigned reads=0,failRead=0;
    VariantSource(IDirect3DVertexShader9* shader,unsigned& count):real(shader),destroyed(count){real->AddRef();}
    ~VariantSource(){real->Release();++destroyed;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** d)override{return real->GetDevice(d);}
    HRESULT STDMETHODCALLTYPE GetFunction(void* data,UINT* bytes)override{++reads;if(failRead==reads)return E_FAIL;return real->GetFunction(data,bytes);}
};

static int variantLifecycle(const char* backend,const char* file){
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"MaterialVariantLifecycle";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    const auto module=LoadLibraryA(backend);assert(module);const auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));VariantDevice device(raw);
    auto words=read(file);IDirect3DVertexShader9* original=nullptr;ok(raw->CreateVertexShader(reinterpret_cast<const DWORD*>(words.data()),&original));
    float constants[1024],after[1024];for(unsigned i=0;i<1024;++i)constants[i]=float(i)*.00017f;ok(raw->SetVertexShaderConstantF(0,constants,256));ok(raw->SetVertexShader(original));
    unsigned destroyed=0;
#ifdef NORTHLIGHT_TEST_LAZY_VARIANT_CURRENT
    {
        auto* source=new VariantSource(original,destroyed);NorthlightAutoDisplacement g(&device);
        g.registerVariant(source,reinterpret_cast<const DWORD*>(words.data()),words.size()/4,true);
        assert(device.creates==0&&source->reads==0&&source->refs==2);
        source->Release(); // The pending registry must retain its immutable source.
        assert(!destroyed&&DisplacementStateTestAccess::ensure(g,source)&&device.creates==1&&source->reads==2);
        for(unsigned i=0;i<8;++i)assert(DisplacementStateTestAccess::ensure(g,source));
        g.reset();assert(DisplacementStateTestAccess::ensure(g,source)&&device.creates==1&&source->reads==2);
        NorthlightQuality::Settings settings;settings.autoBump=0;g.configure(settings);settings.autoBump=1;g.configure(settings);
        assert(DisplacementStateTestAccess::ensure(g,source)&&device.creates==1);
        source->AddRef();g.registerVariant(source,reinterpret_cast<const DWORD*>(words.data()),words.size()/4,true);source->Release();
        assert(device.creates==1&&source->reads==2&&DisplacementStateTestAccess::ensure(g,source)&&device.creates==2&&source->reads==4);
    }
    assert(destroyed==1);
    // Failed bytecode reads and failed creation are cached once, then a new
    // registration can recover. Neither path changes current game bindings.
    for(unsigned failure:{1u,2u,3u}){
        auto* source=new VariantSource(original,destroyed);source->failRead=failure<3?failure:0;
        {NorthlightAutoDisplacement g(&device);g.registerVariant(source,reinterpret_cast<const DWORD*>(words.data()),words.size()/4,true);device.fail=failure==3;
            const auto before=device.creates;assert(!DisplacementStateTestAccess::ensure(g,source));const auto reads=source->reads;
            assert(!DisplacementStateTestAccess::ensure(g,source)&&source->reads==reads&&device.creates==before+unsigned(failure==3));
            device.fail=false;source->failRead=0;g.registerVariant(source,reinterpret_cast<const DWORD*>(words.data()),words.size()/4,true);assert(DisplacementStateTestAccess::ensure(g,source));}
        source->Release();
    }
    assert(destroyed==4);
#endif
    ok(raw->GetVertexShaderConstantF(0,after,256));assert(!std::memcmp(constants,after,sizeof constants));
    IDirect3DVertexShader9* bound=nullptr;ok(raw->GetVertexShader(&bound));assert(bound==original);drop(bound);
    std::printf("MATERIAL_VARIANT_LIFECYCLE_PASS creates=%u destroyed=%u constants=bit-exact binding=preserved pending-source=owned reset=retained failures=once recovery=registration\n",device.creates,destroyed);
    raw->SetVertexShader(nullptr);drop(original);raw->Release();api->Release();FreeLibrary(module);DestroyWindow(window);return 0;
}

static int variantRegistration(const char* backend,const char* folder){
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"MaterialVariantRegistration";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    const auto module=LoadLibraryA(backend);assert(module);const auto create=(IDirect3D9*(WINAPI*)(UINT))GetProcAddress(module,"Direct3DCreate9");assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));MaterialDepthMeter device(raw);
    std::ifstream input(std::string(folder)+"index.txt");assert(input);std::string name;unsigned family=0;
    struct Source {std::vector<unsigned char> words;IDirect3DVertexShader9* shader;unsigned family;};std::vector<Source> sources;
    while(input>>name>>family){auto words=read(std::string(folder)+name);IDirect3DVertexShader9* shader=nullptr;ok(raw->CreateVertexShader(reinterpret_cast<const DWORD*>(words.data()),&shader));sources.push_back({std::move(words),shader,family});}
    assert(sources.size()>=20);LARGE_INTEGER hz,a,b;QueryPerformanceFrequency(&hz);
    for(unsigned mode:{1u,3u}){
        NorthlightAutoBump bump(&device);NorthlightQuality::Settings settings;settings.autoBump=1;settings.autoBumpTerrain=settings.autoBumpWalls=settings.autoBumpGrass=settings.autoBumpModels=mode;bump.configure(settings);
        const auto before=device.vertexCreates;QueryPerformanceCounter(&a);
        for(auto& s:sources)bump.registerVertex(s.shader,NorthlightWaterShaderPatch::fnv1a64(reinterpret_cast<const uint32_t*>(s.words.data()),s.words.size()/4),reinterpret_cast<const DWORD*>(s.words.data()),s.words.size()/4,s.family);
        QueryPerformanceCounter(&b);
        std::printf("MATERIAL_VARIANT_REGISTRATION mode=%u sources=%zu creates=%u cpuMs=%.6f\n",mode,sources.size(),device.vertexCreates-before,double(b.QuadPart-a.QuadPart)*1000/double(hz.QuadPart));
    }
    for(auto& s:sources)drop(s.shader);raw->Release();api->Release();FreeLibrary(module);DestroyWindow(window);return 0;
}

int main(int argc,char** argv){
    if(argc==4&&!std::strcmp(argv[2],"--lifecycle"))return variantLifecycle(argv[1],argv[3]);
    if(argc==4&&!std::strcmp(argv[2],"--registration"))return variantRegistration(argv[1],argv[3]);
    return material_pixel_oracle_main(argc,argv);
}
