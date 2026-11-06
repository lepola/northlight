#define NOMINMAX
#include "blp_mip_guard.h"
#include <d3d9.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include <atomic>
#include <thread>

namespace B=NorthlightBlpMipGuard;
// Reserve the audited addresses inside the stand's own PE image. This keeps
// the loader/CRT from placing mappings there; no remote allocation is needed.
alignas(4096) static unsigned char clientReserve[0x300000];
using Direct=int(__thiscall*)(void*,const char*,unsigned,uint32_t**,unsigned,int);
static void put(void* p,uint32_t v){std::memcpy(p,&v,4);}
static uint32_t word(const void* p){uint32_t v;std::memcpy(&v,p,4);return v;}
static std::vector<unsigned char> readFile(const char* name){
    FILE* f=std::fopen(name,"rb");assert(f);std::fseek(f,0,SEEK_END);long n=std::ftell(f);assert(n>0);std::rewind(f);
    std::vector<unsigned char> bytes(n);assert(std::fread(bytes.data(),1,bytes.size(),f)==bytes.size());std::fclose(f);return bytes;
}
static unsigned levels(const unsigned char* p){
    unsigned w=word(p+12),h=word(p+16);assert(w&&h);if(w==h*6)w/=6;
    if(!(p[11]&15))return 1;unsigned n=1;while(w>1||h>1){w=std::max(w/2,1u);h=std::max(h/2,1u);++n;}return n;
}
static DWORD protection(void* p){MEMORY_BASIC_INFORMATION info={};assert(VirtualQuery(p,&info,sizeof info));return info.Protect;}
static void writable(void* p,size_t size){DWORD old;assert(VirtualProtect(p,size,PAGE_EXECUTE_READWRITE,&old));}
static void executable(void* p,size_t size){DWORD old;assert(VirtualProtect(p,size,PAGE_EXECUTE_READ,&old));assert(FlushInstructionCache(GetCurrentProcess(),p,size));}

// An independently authored ABI harness around the replacement loop. Client
// code is read only from an explicitly provided local executable, never shipped.
static std::array<unsigned char,B::DirectSize> harness(){
    std::array<unsigned char,B::DirectSize> bytes;bytes.fill(0xcc);
    const unsigned char entry[]={0x55,0x89,0xe5,0x56,0x57,0x53,0x89,0xce,0xe9,0,0,0,0};
    std::memcpy(bytes.data(),entry,sizeof entry);put(bytes.data()+9,uint32_t(B::PatchOffset-13));
    const unsigned char leave[]={0x5b,0x5f,0xc7,0x86,0x98,0x04,0,0,0,0,0,0,0xb8,1,0,0,0,0x5e,0x5d,0xc2,0x14,0};
    static_assert(sizeof leave==22);std::memcpy(bytes.data()+B::PatchOffset-22,leave,sizeof leave);
    return bytes;
}
static unsigned invoke(Direct code,const std::vector<unsigned char>& source,unsigned count,uint32_t* table,unsigned first=0){
    std::array<unsigned char,4096> object{};assert(source.size()>=1172);std::memcpy(object.data()+4,source.data(),1172);
    put(object.data()+0x498,uint32_t(uintptr_t(source.data())));put(object.data()+0x4a0,count);auto* output=table;
    const unsigned result=code(object.data(),nullptr,0,&output,first,1);
    assert(output==table);if(result)assert(word(object.data()+0x498)==0);return result;
}
static unsigned cases=0;
static void check(Direct code,const std::vector<unsigned char>& source,unsigned count){
    const auto before=source;unsigned wanted=0;
    while(wanted<std::min(count,16u)&&word(source.data()+84+4*wanted))++wanted;
    SYSTEM_INFO sys={};GetSystemInfo(&sys);const size_t page=sys.dwPageSize;
    auto* block=static_cast<unsigned char*>(VirtualAlloc(nullptr,3*page,MEM_COMMIT|MEM_RESERVE,PAGE_NOACCESS));assert(block);
    DWORD old;assert(VirtualProtect(block+page,page,PAGE_READWRITE,&old));
    // Exactly the number of entries permitted by the client, then a guard page.
    const unsigned capacity=std::min(count,16u);auto* table=reinterpret_cast<uint32_t*>(block+2*page)-capacity;
    for(unsigned i=0;i<capacity;++i)table[i]=0xa5a5a5a5;
    assert(invoke(code,source,count,table)==1);
    for(unsigned i=0;i<wanted;++i)assert(table[i]==uint32_t(uintptr_t(source.data())+word(source.data()+20+4*i)));
    for(unsigned i=wanted;i<capacity;++i)assert(table[i]==0xa5a5a5a5);
    assert(source==before);assert(VirtualFree(block,0,MEM_RELEASE));++cases;
}
static unsigned flushCalls=0,protectCalls=0;
static BOOL WINAPI failFlushOnce(HANDLE process,LPCVOID address,SIZE_T size){
    if(++flushCalls==1)return FALSE;return FlushInstructionCache(process,address,size);
}
static BOOL WINAPI failRestoreOnce(LPVOID p,SIZE_T size,DWORD next,PDWORD previous){
    if(++protectCalls==2)return FALSE;return VirtualProtect(p,size,next,previous);
}
static BOOL WINAPI failProtect(LPVOID,SIZE_T,DWORD,PDWORD){return FALSE;}
static void equal(const char* actual,const char* expected){if(actual==expected)return;if(!actual||!expected||std::strcmp(actual,expected)){std::printf("status=%s expected=%s\n",actual?actual:"(null)",expected?expected:"(null)");std::fflush(stdout);assert(false);}}
static DWORD WINAPI exitThread(void*){return 0;}
template<class To,class From> static To pointer(From p){To result;static_assert(sizeof result==sizeof p);std::memcpy(&result,&p,sizeof result);return result;}
static void gpuSmoke(IDirect3D9* api){
    HWND window=CreateWindowExW(0,L"STATIC",L"BLP guard offscreen",WS_POPUP,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    D3DPRESENT_PARAMETERS pp={};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device=nullptr;
    assert(SUCCEEDED(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&device)));
    IDirect3DTexture9* texture=nullptr;assert(SUCCEEDED(device->CreateTexture(8,8,0,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr)));
    assert(texture->GetLevelCount()==4);
    for(unsigned level=0;level<4;++level){D3DLOCKED_RECT rect={};const unsigned side=8>>level;const uint32_t color=0xff123456+level;
        assert(SUCCEEDED(texture->LockRect(level,&rect,nullptr,0)));
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x)put(static_cast<unsigned char*>(rect.pBits)+y*rect.Pitch+4*x,color);
        assert(SUCCEEDED(texture->UnlockRect(level)));assert(SUCCEEDED(texture->LockRect(level,&rect,nullptr,D3DLOCK_READONLY)));
        for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x)assert(word(static_cast<unsigned char*>(rect.pBits)+y*rect.Pitch+4*x)==color);
        assert(SUCCEEDED(texture->UnlockRect(level)));
    }
    texture->Release();IDirect3DSurface9 *old=nullptr,*target=nullptr,*staging=nullptr;
    assert(SUCCEEDED(device->GetRenderTarget(0,&old)));
    assert(SUCCEEDED(device->CreateRenderTarget(32,32,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr)));
    assert(SUCCEEDED(device->CreateOffscreenPlainSurface(32,32,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&staging,nullptr)));
    assert(SUCCEEDED(device->SetRenderTarget(0,target)));assert(SUCCEEDED(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff3871a3,1,0)));
    assert(SUCCEEDED(device->GetRenderTargetData(target,staging)));D3DLOCKED_RECT rect={};assert(SUCCEEDED(staging->LockRect(&rect,nullptr,D3DLOCK_READONLY)));
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x)assert(word(static_cast<unsigned char*>(rect.pBits)+y*rect.Pitch+4*x)==0xff3871a3);
    assert(SUCCEEDED(staging->UnlockRect()));assert(SUCCEEDED(device->SetRenderTarget(0,old)));
    staging->Release();target->Release();old->Release();device->Release();DestroyWindow(window);
}
static void proxySmoke(unsigned char* image,int count,char** samples){
    const char* dll=std::getenv("NORTHLIGHT_BLP_PROXY_DLL");if(!dll)return;
    for(unsigned cycle=0;cycle<3;++cycle){
        HMODULE module=LoadLibraryA(dll);assert(module);
        using Create=IDirect3D9*(WINAPI*)(UINT);auto create=pointer<Create>(GetProcAddress(module,"Direct3DCreate9"));assert(create);
        IDirect3D9* api=create(D3D_SDK_VERSION);assert(api);
        assert(!std::memcmp(image+B::DirectRva+B::PatchOffset,B::BoundedLoop,sizeof B::BoundedLoop));
        gpuSmoke(api);api->Release();assert(FreeLibrary(module));
        HMODULE resident=nullptr;const bool pinned=GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(module),&resident)!=FALSE;
        // Other proxy/runtime features may retain the module. The standalone
        // module oracle below separately verifies a complete unload.
        for(int i=0;i<count;++i){auto source=readFile(samples[i]);check(reinterpret_cast<Direct>(image+B::DirectRva),source,levels(source.data()));}
        std::printf("BLP_GUARD_PROXY cycle=%u startupActive=1 gpuMipRoundtrip=1 gpuReadbackExact=1 callableAfterRelease=1 proxyResident=%u\n",cycle+1,unsigned(pinned));
    }
}
static void mechanics(){
    const auto authored=harness();unsigned char before[B::DirectSize];std::memcpy(before,authored.data(),sizeof before);
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));assert(code);
    std::memcpy(code,before,sizeof before);executable(code,4096);
    equal(B::install(),"client-layout-mismatch");
    equal(B::replace(code,before,{&failProtect,&FlushInstructionCache}),"code-protection-failed");
    assert(!std::memcmp(code,before,sizeof before)&&protection(code)==PAGE_EXECUTE_READ);
    flushCalls=0;equal(B::replace(code,before,{&VirtualProtect,&failFlushOnce}),"code-cache-flush-failed");
    assert(flushCalls==2&&!std::memcmp(code,before,sizeof before)&&protection(code)==PAGE_EXECUTE_READ);
    protectCalls=0;equal(B::replace(code,before,{&failRestoreOnce,&FlushInstructionCache}),"code-protection-restore-failed");
    assert(protectCalls==3&&!std::memcmp(code,before,sizeof before)&&protection(code)==PAGE_EXECUTE_READ);
    unsigned char mismatch[B::DirectSize];std::memcpy(mismatch,before,sizeof mismatch);mismatch[0]^=1;
    equal(B::replace(code,mismatch),"decoder-changed-concurrently");assert(!std::memcmp(code,before,sizeof before));

    // A thread executing inside the changed range makes the attempt refuse.
    writable(code,4096);code[B::PatchOffset]=0xeb;code[B::PatchOffset+1]=0xfe;executable(code,4096);
    std::memcpy(before,code,sizeof before);
    HANDLE busy=CreateThread(nullptr,0,reinterpret_cast<LPTHREAD_START_ROUTINE>(code+B::PatchOffset),nullptr,0,nullptr);assert(busy);
    bool inLoop=false;
    for(unsigned i=0;i<100&&!inLoop;++i){Sleep(1);assert(SuspendThread(busy)!=DWORD(-1));CONTEXT c={};c.ContextFlags=CONTEXT_CONTROL;assert(GetThreadContext(busy,&c));inLoop=c.Eip==uintptr_t(code+B::PatchOffset);assert(ResumeThread(busy)!=DWORD(-1));}
    assert(inLoop);equal(B::replace(code,before),"decoder-busy");assert(!std::memcmp(code,before,sizeof before));
    assert(SuspendThread(busy)!=DWORD(-1));writable(code,4096);code[B::PatchOffset]=0xc2;code[B::PatchOffset+1]=4;code[B::PatchOffset+2]=0;executable(code,4096);
    assert(ResumeThread(busy)!=DWORD(-1));assert(WaitForSingleObject(busy,10000)==WAIT_OBJECT_0);CloseHandle(busy);
    writable(code,4096);std::memcpy(code,authored.data(),authored.size());executable(code,4096);std::memcpy(before,code,sizeof before);

    // Hold allocator activity on other threads; installation must not acquire
    // a user-space lock or allocate after suspending any of them.
    std::atomic<bool> stop{false};std::atomic<unsigned> turns{0};std::vector<std::thread> workers;
    for(unsigned i=0;i<6;++i)workers.emplace_back([&]{while(!stop.load()){std::vector<unsigned> a(1024,turns.fetch_add(1));if(a[0]==UINT32_MAX)std::abort();}});
    while(turns.load()<100)std::this_thread::yield();
    const char* installed=B::replace(code,before);stop.store(true);for(auto& worker:workers)worker.join();equal(installed,"active");
    assert(!std::memcmp(code,before,B::PatchOffset)&&!std::memcmp(code+B::PatchOffset,B::BoundedLoop,sizeof B::BoundedLoop));
    assert(protection(code)==PAGE_EXECUTE_READ);
    std::vector<unsigned char> source(8192,0x55);put(source.data(),0x32504c42);put(source.data()+4,1);source[8]=2;source[11]=1;
    for(unsigned prefix=0;prefix<=16;++prefix){
        for(unsigned i=0;i<16;++i){put(source.data()+20+4*i,1172+4*i);put(source.data()+84+4*i,i<prefix?4:0);}
        for(unsigned count=0;count<=20;++count)check(reinterpret_cast<Direct>(code),source,count);
        check(reinterpret_cast<Direct>(code),source,UINT32_MAX);
    }
    // A pre-existing suspend count is preserved by the installer.
    HANDLE held=CreateThread(nullptr,0,&exitThread,nullptr,CREATE_SUSPENDED,nullptr);assert(held);
    B::PausedThreads parked;equal(parked.pause(uintptr_t(code+B::PatchOffset),sizeof B::BoundedLoop),nullptr);
    // Destruction is needed before checking the caller-owned suspend count.
    for(size_t i=parked.count;i>0;--i){auto& t=parked.threads[i-1];if(t.paused){assert(ResumeThread(t.handle)!=DWORD(-1));t.paused=false;}}
    assert(ResumeThread(held)==1);assert(WaitForSingleObject(held,10000)==WAIT_OBJECT_0);CloseHandle(held);
    assert(VirtualFree(code,0,MEM_RELEASE));
    std::printf("BLP_GUARD_MECHANICS_PASS cases=%u heapTurns=%u rollback=1 busyRefused=1 suspendCountsPreserved=1\n",cases,turns.load());
}
static void client(const char* file,int count,char** samples){
    auto bytes=readFile(file);IMAGE_DOS_HEADER dos;std::memcpy(&dos,bytes.data(),sizeof dos);
    assert(dos.e_magic==IMAGE_DOS_SIGNATURE);IMAGE_NT_HEADERS32 nt;std::memcpy(&nt,bytes.data()+dos.e_lfanew,sizeof nt);
    assert(nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_I386);
    auto* image=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));assert(image==reinterpret_cast<void*>(0x400000));
    const size_t sectionOffset=dos.e_lfanew+24+nt.FileHeader.SizeOfOptionalHeader;
    struct Region {size_t rva,size;};const Region regions[]={{B::DirectRva,B::DirectSize},{0x2ae900,144},{0x2ab700,96}};
    for(const auto& region:regions){
        assert(uintptr_t(image+region.rva)>=uintptr_t(clientReserve)&&uintptr_t(image+region.rva+region.size)<=uintptr_t(clientReserve+sizeof clientReserve));
        bool copied=false;
        for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i){IMAGE_SECTION_HEADER s;std::memcpy(&s,bytes.data()+sectionOffset+i*sizeof s,sizeof s);
            if(region.rva<s.VirtualAddress||region.rva+region.size>size_t(s.VirtualAddress)+s.SizeOfRawData)continue;
            const auto offset=s.PointerToRawData+region.rva-s.VirtualAddress;assert(offset+region.size<=bytes.size());
            writable(image+region.rva,region.size);std::memcpy(image+region.rva,bytes.data()+offset,region.size);executable(image+region.rva,region.size);copied=true;break;}
        assert(copied);
    }
    // Only three explicitly audited code fragments are copied to our reserved
    // storage. No client PE entrypoint, imports or game initialization are run.
    const auto original=std::vector<unsigned char>(clientReserve,clientReserve+sizeof clientReserve);
    unsigned char pristine[B::DirectSize];std::memcpy(pristine,image+B::DirectRva,sizeof pristine);
    const auto code=reinterpret_cast<Direct>(image+B::DirectRva);std::vector<std::array<uint32_t,512>> baseline(count);std::vector<uint32_t> bases(count);
    for(int i=0;i<count;++i){auto source=readFile(samples[i]);bases[i]=uint32_t(uintptr_t(source.data()));baseline[i].fill(0xa5a5a5a5);assert(invoke(code,source,levels(source.data()),baseline[i].data())==1);}
    // Unknown code and a stale object-layout signature must both refuse.
    writable(image+B::DirectRva,4096);image[B::DirectRva]^=1;executable(image+B::DirectRva,4096);
    equal(B::install(reinterpret_cast<HMODULE>(image)),"decoder-signature-mismatch");
    writable(image+B::DirectRva,4096);image[B::DirectRva]^=1;executable(image+B::DirectRva,4096);
    writable(image+0x2ae900,144);image[0x2ae900]^=1;executable(image+0x2ae900,144);
    equal(B::install(reinterpret_cast<HMODULE>(image)),"client-layout-mismatch");
    writable(image+0x2ae900,144);image[0x2ae900]^=1;executable(image+0x2ae900,144);
    equal(B::install(reinterpret_cast<HMODULE>(image)),"active");equal(B::install(reinterpret_cast<HMODULE>(image)),"already-active");
    for(size_t i=0;i<original.size();++i){const auto address=uintptr_t(clientReserve+i),start=uintptr_t(image+B::DirectRva+B::PatchOffset);
        assert(clientReserve[i]==(address>=start&&address-start<sizeof B::BoundedLoop?B::BoundedLoop[address-start]:original[i]));}
    using Count=unsigned(__cdecl*)(unsigned,unsigned);const auto clientCount=reinterpret_cast<Count>(image+0x2ab700);
    std::vector<unsigned char> synthetic(8192,0x55);put(synthetic.data(),0x32504c42);put(synthetic.data()+4,1);synthetic[8]=2;
    for(unsigned i=0;i<16;++i){put(synthetic.data()+20+4*i,1172+4*i);put(synthetic.data()+84+4*i,4);}
    const unsigned dimensions[]={1,2,3,4,7,16,128,1024,16384};unsigned geometryCases=0;
    for(unsigned w:dimensions)for(unsigned h:dimensions)for(unsigned mips=0;mips<2;++mips){
        put(synthetic.data()+12,w);put(synthetic.data()+16,h);synthetic[11]=mips;
        const unsigned n=levels(synthetic.data());if(mips)assert(clientCount(w,h)==n);check(code,synthetic,n);
        for(unsigned first=0;first<=n;++first){std::array<uint32_t,512> table;table.fill(0xa5a5a5a5);
            const bool allowed=!first||(mips&&first<n);assert(invoke(code,synthetic,n,table.data(),first)==unsigned(allowed));
            if(!allowed)for(auto value:table)assert(value==0xa5a5a5a5);}
        ++geometryCases;
    }
    for(unsigned side:dimensions){put(synthetic.data()+12,side*6);put(synthetic.data()+16,side);synthetic[11]=1;
        const auto n=levels(synthetic.data());assert(clientCount(side*6,side)==n);check(code,synthetic,n);++geometryCases;}
    std::printf("BLP_GUARD_GEOMETRY_PASS cases=%u cubeStrip=1 noMip=1 firstLevelBounds=1\n",geometryCases);
    unsigned removed=0;
    for(int i=0;i<count;++i){auto source=readFile(samples[i]);const auto before=source;const auto n=levels(source.data());check(code,source,n);
        std::array<uint32_t,512> after;after.fill(0xa5a5a5a5);assert(invoke(code,source,n,after.data())==1);
        unsigned valid=0;while(valid<n&&valid<16&&word(source.data()+84+4*valid))++valid;
        for(unsigned j=0;j<valid;++j)assert(baseline[i][j]-bases[i]==after[j]-uint32_t(uintptr_t(source.data())));assert(source==before);
        for(unsigned j=valid;j<after.size();++j)assert(after[j]==0xa5a5a5a5);
        removed+=valid<baseline[i].size()&&baseline[i][valid]!=0xa5a5a5a5;
        std::printf("BLP_GUARD_SAMPLE file=%s levels=%u validPointersIdentical=1 payloadUnchanged=1\n",samples[i],n);
    }
    // Exercise the actual production proxy's startup with pristine client code.
    writable(image+B::DirectRva,sizeof pristine);std::memcpy(image+B::DirectRva,pristine,sizeof pristine);executable(image+B::DirectRva,sizeof pristine);proxySmoke(image,count,samples);
    if(const char* name=std::getenv("NORTHLIGHT_BLP_ORACLE_MODULE")){
        writable(image+B::DirectRva,sizeof pristine);std::memcpy(image+B::DirectRva,pristine,sizeof pristine);executable(image+B::DirectRva,sizeof pristine);
        HMODULE module=LoadLibraryA(name);assert(module);using Apply=BOOL(*)(HMODULE);auto apply=pointer<Apply>(GetProcAddress(module,"ApplyBlpGuard"));assert(apply&&apply(reinterpret_cast<HMODULE>(image)));
        assert(FreeLibrary(module));HMODULE resident=nullptr;
        assert(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(module),&resident));
        for(int i=0;i<count;++i){auto source=readFile(samples[i]);check(code,source,levels(source.data()));}
        std::puts("BLP_GUARD_UNLOAD_PASS moduleResident=0 clientCodeCallable=1");
    }
    std::printf("BLP_GUARD_CLIENT_PASS samples=%d extraPointerCasesRemoved=%u changedBytes=47 idempotent=1\n",count,removed);
}
int main(int argc,char** argv){
    static_assert(sizeof(void*)==4);
    assert(GetModuleHandleW(nullptr)==reinterpret_cast<HMODULE>(0x400000));
    assert(uintptr_t(clientReserve)<=0x6ab700&&uintptr_t(clientReserve+sizeof clientReserve)>0x6b012e);
    std::setvbuf(stdout,nullptr,_IONBF,0);mechanics();if(argc>1)client(argv[1],argc-2,argv+2);std::puts("BLP_GUARD_PASS gameLaunched=0 filesWritten=0");
}
