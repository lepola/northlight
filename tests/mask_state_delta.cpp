#include <d3d9.h>
#include "name_stencil.h"
#include "unlit_receiver.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
struct Backend {
    IDirect3DDevice9* real=nullptr;std::array<DWORD,256> state{};unsigned sets=0,gets=0;int failRead=-1;
    __attribute__((noinline)) HRESULT SetRenderState(D3DRENDERSTATETYPE t,DWORD value){++sets;state[t]=value;return real?real->SetRenderState(t,value):D3D_OK;}
    __attribute__((noinline)) HRESULT GetRenderState(D3DRENDERSTATETYPE t,DWORD* value){++gets;if(int(t)==failRead)return E_FAIL;if(real)return real->GetRenderState(t,value);*value=state[t];return D3D_OK;}
};
struct Reader {
    Backend* ext;bool mirrored=false;
    bool readDrawRenderState(D3DRENDERSTATETYPE t,DWORD& value){if(mirrored){value=ext->state[t];return int(t)!=ext->failRead;}return SUCCEEDED(ext->GetRenderState(t,&value));}
};
struct Before:Reader {
    template<size_t N> bool maskApply(const NorthlightNameStencil::Set (&sets)[N],DWORD (&previous)[N]){
        for(size_t i=0;i<N;++i)if(!readDrawRenderState(D3DRENDERSTATETYPE(sets[i].state),previous[i]))return false;
        for(size_t i=0;i<N;++i)ext->SetRenderState(D3DRENDERSTATETYPE(sets[i].state),sets[i].value);return true;
    }
    template<size_t N> void maskRestore(const NorthlightNameStencil::Set (&sets)[N],const DWORD (&previous)[N]){for(size_t i=0;i<N;++i)ext->SetRenderState(D3DRENDERSTATETYPE(sets[i].state),previous[i]);}
};
struct After:Reader {
#include "mask_state_methods.inc"
};
template<size_t N>static void check(const NorthlightNameStencil::Set (&sets)[N]){
    for(bool mirrored:{false,true})for(unsigned bits=0;bits<(1u<<N);++bits){Backend a,b;
        for(size_t i=0;i<N;++i)a.state[sets[i].state]=b.state[sets[i].state]=sets[i].value^((bits>>i)&1);
        const auto original=a.state;Before old;old.ext=&a;old.mirrored=mirrored;After next;next.ext=&b;next.mirrored=mirrored;DWORD x[N],y[N];
        assert(old.maskApply(sets,x)&&next.maskApply(sets,y));assert(a.state==b.state&&a.sets==N&&b.sets==size_t(__builtin_popcount(bits))&&a.gets==b.gets);
        // Even an unexpected nested writer/failure retains unconditional restoration.
        for(size_t i=0;i<N;++i)a.state[sets[i].state]=b.state[sets[i].state]=0xdeadbeefu;
        old.maskRestore(sets,x);next.maskRestore(sets,y);assert(a.state==original&&b.state==original);
        for(size_t fail=0;fail<N;++fail){a.state=b.state=original;a.failRead=b.failRead=int(sets[fail].state);a.sets=b.sets=0;
            assert(!old.maskApply(sets,x)&&!next.maskApply(sets,y));assert(a.state==original&&b.state==original&&!a.sets&&!b.sets);}
    }
}
struct Measurement {double ns=0;unsigned sets=0,gets=0;};
template<class Mask>static Measurement bench(Backend& backend,bool mirrored){
    for(const auto& s:NorthlightNameStencil::kMark)assert(SUCCEEDED(backend.SetRenderState(D3DRENDERSTATETYPE(s.state),s.value)));
    assert(SUCCEEDED(backend.SetRenderState(D3DRS_STENCILENABLE,FALSE)));Mask mask;mask.ext=&backend;mask.mirrored=mirrored;
    const unsigned sets=backend.sets,gets=backend.gets;const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<20000;++i){DWORD previous[8];assert(mask.maskApply(NorthlightUnlitReceiver::Unmark,previous));mask.maskRestore(NorthlightUnlitReceiver::Unmark,previous);}
    return {std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/20000,(backend.sets-sets)/20000,(backend.gets-gets)/20000};
}
int main(int argc,char** argv){
    check(NorthlightNameStencil::kMark);check(NorthlightNameStencil::kUnmark);check(NorthlightNameStencil::kAlphaGuard);check(NorthlightNameStencil::kComposite);
    check(NorthlightUnlitReceiver::Mark);check(NorthlightUnlitReceiver::Unmark);
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightMaskState";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,8,8,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    assert(argc==2);auto module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    Backend backend;assert(SUCCEEDED(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&backend.real)));
    for(bool mirrored:{false,true}){double old=0,next=0;Measurement before,after;
        for(unsigned round=0;round<12;++round)for(unsigned turn=0;turn<2;++turn){if((round+turn)&1){after=bench<After>(backend,mirrored);next+=after.ns;}else{before=bench<Before>(backend,mirrored);old+=before.ns;}}
        assert(before.sets==16&&after.sets==11&&before.gets==after.gets);
        std::printf("PASS mask state delta: mirrored=%u oldNs=%.3f newNs=%.3f ratio=%.4f commonOldSets=%u commonNewSets=%u backendGets=%u exactState=1 failedReadNoWrites=1 nestedMutationRestored=1 actualUnlitTables=1\n",unsigned(mirrored),old/12,next/12,next/old,before.sets,after.sets,after.gets);}
    backend.real->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
