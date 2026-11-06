#undef NDEBUG
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include "texture_reset_cases.h"
static void ok(HRESULT hr){assert(SUCCEEDED(hr));}
int main(int argc,char** argv){
    assert(argc==2||argc==3);const bool legacy=argc==3;TextureResetCases::run();
    auto window=CreateWindowW(L"STATIC",L"Northlight texture Reset test",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    NorthlightParticleTextures::Cache particle;NorthlightMaterialTextures::Cache material;NorthlightParticleTextures::View a,b;
    const unsigned char pixels[]={20,80,160,255,20,80,160,255,20,80,160,255,20,80,160,255};
    int managedOwner=0,defaultOwner=0;
    auto texture=[&](D3DPOOL pool,const void* owner){IDirect3DTexture9* t=nullptr;ok(d->CreateTexture(2,2,1,pool==D3DPOOL_DEFAULT?D3DUSAGE_DYNAMIC:0,D3DFMT_A8R8G8B8,pool,&t,nullptr));
        D3DLOCKED_RECT lock{};ok(t->LockRect(0,&lock,nullptr,0));for(unsigned row=0;row<2;++row)std::memcpy(static_cast<char*>(lock.pBits)+row*lock.Pitch,pixels+row*8,8);ok(t->UnlockRect(0));
        D3DSURFACE_DESC desc{};ok(t->GetLevelDesc(0,&desc));particle.upload(t,owner,2,2,0,21,pixels,sizeof pixels,8,true,true,unsigned(desc.Pool));material.upload(t,owner,2,2,0,21,pixels,sizeof pixels,8,true,true,unsigned(desc.Pool));return t;};
    auto* managed=texture(D3DPOOL_MANAGED,&managedOwner);auto* temporary=texture(D3DPOOL_DEFAULT,&defaultOwner);
    assert(particle.lookup(managed,a)&&material.lookup(managed,b));const auto life=a.lifetime,revision=a.revision,hash=a.rgbaHash,materialLife=b.lifetime,materialRevision=b.revision;
    const auto decoded=particle.counts.decoded;
    const auto resetCaches=[&]{if(legacy){particle.clear();material.clear();}else{particle.reset();material.reset();}};
    resetCaches();
    if(!particle.lookup(managed,a)||!material.lookup(managed,b)){std::puts("TEXTURE_RESET_MANAGED_MISS old_clear_loses_once_uploaded_mips");return 1;}
    assert(!particle.lookup(temporary,a)&&!material.lookup(temporary,b));temporary->Release();temporary=nullptr;
    auto bad=pp;bad.SwapEffect=static_cast<D3DSWAPEFFECT>(0x7fffffff);assert(FAILED(d->Reset(&bad)));
    assert(particle.lookup(managed,a)&&material.lookup(managed,b));
    for(unsigned retry=0;retry<2;++retry){resetCaches();ok(d->Reset(&pp));assert(particle.lookup(managed,a)&&material.lookup(managed,b));
        assert(a.lifetime==life&&a.revision==revision&&a.rgbaHash==hash&&b.lifetime==materialLife&&b.revision==materialRevision&&particle.counts.decoded==decoded);
        D3DLOCKED_RECT lock{};ok(managed->LockRect(0,&lock,nullptr,D3DLOCK_READONLY));for(unsigned row=0;row<2;++row)assert(std::memcmp(static_cast<const char*>(lock.pBits)+row*lock.Pitch,pixels+row*8,8)==0);ok(managed->UnlockRect(0));}
    temporary=texture(D3DPOOL_DEFAULT,&defaultOwner);assert(particle.lookup(temporary,a)&&material.lookup(temporary,b));
    particle.erase(temporary,&defaultOwner);material.erase(temporary,&defaultOwner);temporary->Release();resetCaches();assert(particle.lookup(managed,a)&&material.lookup(managed,b));
    particle.invalidate(managed);material.invalidate(managed);resetCaches();assert(!particle.lookup(managed,a)&&!material.lookup(managed,b));
    managed->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
    std::puts("TEXTURE_RESET_GPU_PASS native_failed_reset_successful_retry managed_GPU_CPU_pixels_identity_revision_unchanged default_recreated writes_releases_invalidated no_readback_repopulation");return 0;
}
