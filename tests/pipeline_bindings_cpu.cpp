// Use the production stream and its existing fake target/lifetime checks.
#ifdef _WIN32
// The SDK stub uses the portable wait seam; keep the Windows C++ runtime.
#include <condition_variable>
#include <thread>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#include <unordered_set>
#include <queue>
#include <functional>
#include <memory>
#undef _WIN32
#endif
#define main commandStreamChecks
#include "test_command_stream.cpp"
#undef main
template<class T> [[gnu::noinline]] static ProxyBase* classifyBinding(T* p){return ProxyBase::of(p);}

int main(int argc,char** argv){
    if(argc>1)return commandStreamChecks(argc,argv);
    redundantFiltering();checkClean();
    Rig rig(true);auto* d=rig.dev;
    IDirect3DTexture9* tex=nullptr;IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;IDirect3DVertexDeclaration9* decl=nullptr;
    DWORD code[]={0xFFFE0200u,0x0000FFFFu};D3DVERTEXELEMENT9 el[2]={{0,0,2,0,0,0},{0xFF,0,17,0,0,0}};
    CHECK(d->CreateTexture(8,8,1,0,(D3DFORMAT)22,(D3DPOOL)1,&tex,nullptr)==D3D_OK);
    CHECK(d->CreateVertexBuffer(256,0,0,(D3DPOOL)0,&vb,nullptr)==D3D_OK);
    CHECK(d->CreateIndexBuffer(64,0,(D3DFORMAT)101,(D3DPOOL)0,&ib,nullptr)==D3D_OK);
    CHECK(d->CreateVertexShader(code,&vs)==D3D_OK&&d->CreatePixelShader(code,&ps)==D3D_OK&&d->CreateVertexDeclaration(el,&decl)==D3D_OK);
    auto bind=[&]{d->SetTexture(0,tex);d->SetStreamSource(0,vb,0,20);d->SetIndices(ib);d->SetVertexShader(vs);d->SetPixelShader(ps);d->SetVertexDeclaration(decl);};
    bind();rig.sync();gTrace.clear();
    constexpr unsigned iterations=300000;
    for(unsigned sample=0;sample<9;++sample){
        const auto count=get(rig.core().q.stats.filteredCalls),seq=rig.core().q.recordedSeq();
        const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<iterations;++i)bind();
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        CHECK(rig.core().q.recordedSeq()==seq&&get(rig.core().q.stats.filteredCalls)-count==6ull*iterations);
        std::printf("BINDINGS_CPU sample=%u ms=%.6f calls=%u filtered=%u recorded=0\n",sample,elapsed,6*iterations,6*iterations);
    }
    std::uintptr_t expected=0;
    for(auto* p:{static_cast<IUnknown*>(tex),static_cast<IUnknown*>(vb),static_cast<IUnknown*>(ib),static_cast<IUnknown*>(vs),static_cast<IUnknown*>(ps),static_cast<IUnknown*>(decl)})expected+=reinterpret_cast<std::uintptr_t>(ProxyBase::of(p));
    for(unsigned sample=0;sample<9;++sample){
        std::uintptr_t sum=0;const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<iterations;++i){sum+=reinterpret_cast<std::uintptr_t>(classifyBinding(tex));sum+=reinterpret_cast<std::uintptr_t>(classifyBinding(vb));sum+=reinterpret_cast<std::uintptr_t>(classifyBinding(ib));sum+=reinterpret_cast<std::uintptr_t>(classifyBinding(vs));sum+=reinterpret_cast<std::uintptr_t>(classifyBinding(ps));sum+=reinterpret_cast<std::uintptr_t>(classifyBinding(decl));}
        const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        CHECK(sum==expected*iterations);std::printf("CLASSIFY_CPU sample=%u ms=%.6f calls=%u identityExact=1\n",sample,elapsed,6*iterations);
    }
    tex->Release();vb->Release();ib->Release();vs->Release();ps->Release();decl->Release();rig.finish();checkClean();
    std::puts("PIPELINE_BINDINGS_CPU_PASS");
}
