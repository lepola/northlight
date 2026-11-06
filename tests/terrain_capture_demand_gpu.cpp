// Exercise the production terrain capture entry points; no game/cache/timings.
#undef NDEBUG
#include "renderer.cpp"
#include <cassert>

static void checked(HRESULT hr){assert(SUCCEEDED(hr));}
struct TerrainMeter:ForwardIDirect3DDevice9 {
    unsigned reads=0;
    explicit TerrainMeter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT first,float* p,UINT n)override{++reads;return real->GetVertexShaderConstantF(first,p,n);}
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** p)override{++reads;return real->GetVertexDeclaration(p);}
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT s,IDirect3DVertexBuffer9** p,UINT* o,UINT* n)override{++reads;return real->GetStreamSource(s,p,o,n);}
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT s,UINT* p)override{++reads;return real->GetStreamSourceFreq(s,p);}
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** p)override{++reads;return real->GetIndices(p);}
};
struct TerrainCaptureDemandTest {
    static void run(TerrainMeter& d){
        WorldRenderer w(&d);w.valid=true;w.active=std::make_shared<WorldRenderer::Snapshot>();w.active->bvh=std::make_shared<NorthlightGI::BVH>();w.meshGeneration=1;
        // Only the identity classification is used by capture; keep a real shader object.
        const DWORD code[]={0xfffe0300,0x0200001f,0x80000000,0x900f0000,0x0200001f,0x80000000,0xe00f0000,0x02000001,0xe00f0000,0x90e40000,0xffff};
        IDirect3DVertexShader9* shader=nullptr;checked(d.CreateVertexShader(code,&shader));w.terrainShaders.insert(shader);
        D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
        IDirect3DVertexDeclaration9* declaration=nullptr;checked(d.CreateVertexDeclaration(elements,&declaration));checked(d.SetVertexDeclaration(declaration));
        using Position=NorthlightTerrainCapture::Position;Position vertices[]={{1,1,3},{2,1,4},{1,2,5}};WORD indices[]={0,1,2};
        IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
        checked(d.CreateVertexBuffer(sizeof vertices,0,0,D3DPOOL_MANAGED,&vb,nullptr));checked(d.CreateIndexBuffer(sizeof indices,0,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib,nullptr));
        auto write=[&]{void* p=nullptr;checked(vb->Lock(0,sizeof vertices,&p,0));std::memcpy(p,vertices,sizeof vertices);checked(vb->Unlock());};write();
        void* p=nullptr;checked(ib->Lock(0,sizeof indices,&p,0));std::memcpy(p,indices,sizeof indices);checked(ib->Unlock());
        checked(d.SetStreamSource(0,vb,0,sizeof(Position)));checked(d.SetStreamSourceFreq(0,1));checked(d.SetIndices(ib));
        for(unsigned i=0;i<16;++i)w.context.view[i]=i%5==0?1.f:0.f;
        float constants[32]{};std::memcpy(constants,w.context.view,64);constants[16]=constants[21]=constants[27]=1;
        checked(d.SetVertexShaderConstantF(0,constants,8));w.projection[0]=w.projection[1]=w.projection[2]=1;
        auto capture=[&](NorthlightCaptureMetadata::PositionBounds* bounds=nullptr){w.capture(D3DPT_TRIANGLELIST,0,0,3,0,1,true,shader,false,false,bounds);};
        auto frame=[&]{w.endFrame();w.valid=true;};
        auto switches=[&](bool shadows){auto q=w.quality;q.gi=q.worldFog=q.localLights=q.actorShadows=q.contactAO=q.autoBump=0;q.weather=1;q.worldShadows=shadows;
            w.quality=q;w.setEffects({false,shadows,false});assert(w.effectsEnabled()&&w.pipelineActive&&w.effects.shadows==shadows);};
        switches(true);capture();assert(w.frameTerrain.size()==1&&w.terrainAttempts==1&&d.reads>0);
        const auto original=w.frameTerrain.front();assert(original->positions.size()==3&&original->positions[0].z==3);
        const auto entries=w.terrainBoundsCache.persistentEntries();const auto bytes=w.terrainBoundsCache.persistentBytes();assert(entries&&bytes);
        assert(w.uploadLiveTerrain());auto* cachedVB=w.liveTerrainGPU.vertices();auto* cachedIB=w.liveIndicesGPU;assert(cachedVB&&cachedIB);
        const auto cachedBytes=w.liveTerrainGPU.bytes();assert(cachedBytes&&w.uploadedTerrain.size()==1);
        frame();switches(false);const auto attempts=w.terrainAttempts;
        for(unsigned f=0;f<8;++f){d.reads=0;const auto sourceBytes=w.terrainBoundsCache.bytesRead();
            capture();
            w.captureUP(D3DPT_TRIANGLELIST,0,3,1,indices,D3DFMT_INDEX16,vertices,sizeof(Position),true,shader,false,false);
            w.captureUP(D3DPT_TRIANGLELIST,0,3,1,nullptr,D3DFMT_INDEX16,vertices,sizeof(Position),false,shader,false,false);
            assert(d.reads==0&&w.terrainAttempts==attempts&&w.frameTerrain.empty()&&w.liveTerrainChunks.empty()&&w.pointLiveBatches.empty());
            assert(w.terrainBoundsCache.bytesRead()==sourceBytes&&w.terrainBoundsCache.persistentEntries()==entries&&w.terrainBoundsCache.persistentBytes()==bytes);
            assert(w.liveTerrainGPU.vertices()==cachedVB&&w.liveIndicesGPU==cachedIB&&w.liveTerrainGPU.bytes()==cachedBytes&&w.uploadedTerrain.front()==original);
            frame();
        }
        // Same-frame bounds remain available with shadows disabled (AutoBump handoff).
        NorthlightCaptureMetadata::PositionBounds bounds;d.reads=0;capture(&bounds);
        assert(bounds.known&&bounds.low[0]==1&&bounds.low[1]==1&&bounds.low[2]==3&&bounds.high[0]==2&&bounds.high[1]==2&&bounds.high[2]==5);
        assert(d.reads>0&&w.frameTerrain.size()==1&&w.frameTerrain.front()==original);frame();
        // No cache rebuild or upload is required when the unchanged source returns.
        switches(true);capture();assert(w.frameTerrain.size()==1&&w.frameTerrain.front()==original);
        assert(w.uploadLiveTerrain()&&w.terrainUploadReused&&!w.terrainUploadBytes&&w.liveTerrainGPU.vertices()==cachedVB&&w.liveIndicesGPU==cachedIB);frame();
        // Dormancy is not permission to reuse stale bytes after a native buffer write.
        switches(false);vertices[0].z=9;write();capture();assert(w.frameTerrain.empty());frame();switches(true);capture();
        assert(w.frameTerrain.size()==1&&w.frameTerrain.front()!=original&&w.frameTerrain.front()->positions[0].z==9);frame();
        // Both UP entry points resume their original complete capture behavior.
        for(bool indexed:{false,true}){w.captureUP(D3DPT_TRIANGLELIST,0,3,1,indices,D3DFMT_INDEX16,vertices,sizeof(Position),indexed,shader,false,false);
            assert(w.frameTerrain.size()==1&&w.frameTerrain.front()->positions[0].z==9&&w.frameTerrain.front()->indices.size()==3);frame();}
        // A non-terrain shader still reaches model capture diagnostics while shadows are off.
        switches(false);w.captureMode=WorldRenderer::CaptureFresh;NorthlightDiagnostics::configure(true);const auto unknown=w.unknownCaptureCalls;
        w.capture(D3DPT_TRIANGLELIST,0,0,3,0,1,true,nullptr,false,true);
        w.captureUP(D3DPT_TRIANGLELIST,0,3,1,indices,D3DFMT_INDEX16,vertices,sizeof(Position),true,nullptr,false,true);
        assert(w.unknownCaptureCalls==unknown+2);NorthlightDiagnostics::configure(false);
        shader->Release();declaration->Release();vb->Release();ib->Release();
        std::puts("TERRAIN_CAPTURE_DEMAND_PASS dormantFrames=8 disabledPaths=3 boundsHandoff=1 unchangedReentry=1 mutatedReentry=1 upReentry=2 modelRouting=2 cacheRetained=1");
    }
};
int main(int argc,char** argv){
    assert(argc==3);std::setvbuf(stdout,nullptr,_IONBF,0);assert(MultiByteToWideChar(CP_UTF8,0,argv[2],-1,rootPath,MAX_PATH));
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightTerrainDemand";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* device=nullptr;checked(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device));
    {TerrainMeter meter(device);TerrainCaptureDemandTest::run(meter);}
    device->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
