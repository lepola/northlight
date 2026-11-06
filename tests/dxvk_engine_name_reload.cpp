#undef NDEBUG
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>

// WoW probes D3D9, releases its factory, unloads the DLL and loads it again.
int main(){
    SetErrorMode(SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS);
    for(unsigned cycle=0;cycle<3;++cycle){
        HMODULE module=LoadLibraryW(L"d3d9.dll");assert(module);
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const char*>(module)+dos->e_lfanew);
        const SIZE_T imageSize=nt->OptionalHeader.SizeOfImage;
        auto factory=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(factory);
        IDirect3D9* api=factory(D3D_SDK_VERSION);assert(api&&api->GetAdapterCount());
        api->Release();assert(FreeLibrary(module));
        // Another DLL can occupy the old range between WoW's probe and reload.
        // Keep it reserved when the proxy really unloads, forcing a different base.
        const auto reserved=VirtualAlloc(module,imageSize,MEM_RESERVE,PAGE_NOACCESS);
        std::printf("factory release + FreeLibrary cycle=%u completed oldBase=%p reserved=%d resident=%d\n",cycle+1,module,int(reserved!=nullptr),int(GetModuleHandleW(L"d3d9.dll")!=nullptr));std::fflush(stdout);
    }
    std::puts("PASS repeated factory creation and proxy unload/reload");
}
