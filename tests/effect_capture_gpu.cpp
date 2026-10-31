// Exercise production frame-boundary exports on a private backend device.
#undef NDEBUG
#define NORTHLIGHT_REPLAY_STAND 1
#include "../src/proxy/renderer.cpp"
#include <cassert>
#include <fstream>
struct EffectDiagnosticsTest {
    static void run(IDirect3DDevice9* d,IDirect3D9* api,const std::filesystem::path& root){
        auto* wrapper=new Device(d,api);
        wrapper->width=64;wrapper->height=32;wrapper->enabled=true;wrapper->frame=7;
        auto ok=[](HRESULT hr){assert(SUCCEEDED(hr));};
        ok(d->CreateTexture(32,16,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&wrapper->aoRaw,nullptr));
        ok(wrapper->aoRaw->GetSurfaceLevel(0,&wrapper->aoRawSurface));
        ok(d->ColorFill(wrapper->aoRawSurface,nullptr,0xffcc7733));
        wrapper->diagnosticAoRaw=true;
        IDirect3DSurface9* before=nullptr;ok(d->GetRenderTarget(0,&before));
        ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));
        NorthlightDiagnosticCapture::request(NorthlightDiagnosticCapture::AO|NorthlightDiagnosticCapture::Materials|NorthlightDiagnosticCapture::World|NorthlightDiagnosticCapture::Fog|NorthlightDiagnosticCapture::Atlas);
        wrapper->captureEffectDiagnostics();assert(wrapper->gpuDiagnosticFrame);
        unsigned current=0,skipped=0;std::filesystem::path first;
        auto check=[&](const std::filesystem::path& folder,bool positive){
            std::ifstream metadata(folder/"capture-1-products.jsonl");assert(metadata);std::string line;
            unsigned zeros=0,live=0;
            while(std::getline(metadata,line)){assert(line.find("\"fps_eligible\":false")!=std::string::npos);zeros+=line.find("\"state\":\"skipped\"")!=std::string::npos;live+=line.find("\"state\":\"current\"")!=std::string::npos;}
            assert(zeros>=10&&live==unsigned(positive));
            std::ifstream file(folder/"capture-1-ao-raw.fgr",std::ios::binary);assert(file);uint32_t header[5]{};file.read(reinterpret_cast<char*>(header),sizeof header);assert(header[0]==0x31524746);
            std::vector<unsigned char> pixels{std::istreambuf_iterator<char>(file),{}};assert(!pixels.empty());bool any=std::any_of(pixels.begin(),pixels.end(),[](auto b){return b!=0;});assert(any==positive);
            current+=live;skipped+=zeros;
        };
        for(const auto& entry:std::filesystem::directory_iterator(root/"northlight-diagnostics"))if(entry.is_directory()){first=entry.path();check(first,true);}
        assert(!first.empty());wrapper->clearFrame();wrapper->enabled=false;wrapper->frame=8;
        assert(!wrapper->diagnosticAoRaw&&!wrapper->diagnosticAo&&!wrapper->diagnosticWorld);
        NorthlightDiagnosticCapture::request(15);wrapper->captureEffectDiagnostics();unsigned fresh=0;
        for(const auto& entry:std::filesystem::directory_iterator(root/"northlight-diagnostics"))if(entry.is_directory()&&entry.path()!=first){check(entry.path(),false);++fresh;}
        assert(fresh==1&&current==1&&skipped>=20);
        assert(wrapper->atlasCaptureFrames==1);
        IDirect3DTexture9* atlas=nullptr;ok(d->CreateTexture(512,512,1,0,D3DFMT_DXT1,D3DPOOL_MANAGED,&atlas,nullptr));
        D3DLOCKED_RECT upload{};ok(atlas->LockRect(0,&upload,nullptr,0));std::memset(upload.pBits,0,128*upload.Pitch);ok(atlas->UnlockRect(0));
        ok(d->SetTexture(0,atlas));wrapper->atlasCaptureDraw();assert(wrapper->atlasCaptureTextures[0]==atlas);
        ok(d->SetTexture(0,nullptr));atlas->Release(); // the diagnostic pin must keep it alive until export
        wrapper->clearFrame();wrapper->frame=9;wrapper->captureEffectDiagnostics();assert(!wrapper->atlasCaptureFrames&&!NorthlightAtlasProof::enabled);
        for(auto* texture:wrapper->atlasCaptureTextures)assert(!texture);
        unsigned atlasFiles=0;for(const auto& entry:std::filesystem::recursive_directory_iterator(root/"northlight-diagnostics"))atlasFiles+=entry.path().extension()==".bc";assert(atlasFiles==1);

        IDirect3DSurface9* after=nullptr;ok(d->GetRenderTarget(0,&after));assert(after==before);after->Release();before->Release();DWORD blend=FALSE;ok(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend));assert(blend==TRUE);
        wrapper->Release();std::puts("EFFECT_CAPTURE_GPU_PASS actualDevice=1 currentAO=1 effectsOffZero=1 freshFrame=1 separateScopes=1 stateRestored=1");
    }
};
int main(int argc,char** argv){
    assert(argc==4);std::filesystem::path root=argv[2];std::string prefix=root.string()+"/";assert(MultiByteToWideChar(CP_UTF8,0,prefix.c_str(),-1,rootPath,MAX_PATH));
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightEffectCaptureTest";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,32,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.MultiSampleType=D3DMULTISAMPLE_TYPE(std::atoi(argv[3]));
    IDirect3DDevice9* d=nullptr;assert(SUCCEEDED(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d)));EffectDiagnosticsTest::run(d,api,root);
    api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
