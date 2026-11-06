#pragma once
#include "blp_mip_guard.h"

// Build 12340: retain a UI_ backdrop's own visible point lights before filling
// its four native slots with nearby equipment. Native positions, colours,
// directional/ambient lighting and non-UI models retain their original path.
// The thunk has no DLL callbacks and stays callable after a loader probe unload.
namespace NorthlightGlueLighting {
inline constexpr unsigned char Selection[]={
    // SetupLighting also visits async models before their M2 data exists.
    // Match the native AnimateST loaded-bit gate before reading shared data.
    0x55, // push ebp
    0x89,0xe5, // mov ebp, esp
    0x83,0xec,0x30, // sub esp, 0x30
    0x53, // push ebx
    0x56, // push esi
    0x57, // push edi
    0x8b,0x7d,0x08, // mov edi, dword ptr [ebp + 8]
    0x57, // push edi
    0xb8,0x00,0xe4,0x81,0x00, // mov eax, 0x81e400
    0xff,0xd0, // call eax
    0xf6,0x46,0x10,0x01, // test byte ptr [esi + 0x10], 1
    0x0f,0x84,0x29,0x01,0x00,0x00, // je 0x147
    0x8b,0x46,0x2c, // mov eax, dword ptr [esi + 0x2c]
    0x8b,0x80,0x50,0x01,0x00,0x00, // mov eax, dword ptr [eax + 0x150]
    0x83,0x78,0x08,0x04, // cmp dword ptr [eax + 8], 4
    0x0f,0x82,0x16,0x01,0x00,0x00, // jb 0x147
    0x8b,0x50,0x0c, // mov edx, dword ptr [eax + 0xc]
    0x81,0xfa,0x00,0x00,0x01,0x00, // cmp edx, 0x10000
    0x0f,0x82,0x07,0x01,0x00,0x00, // jb 0x147
    0x81,0xfa,0xfc,0xff,0xef,0xff, // cmp edx, 0xffeffffc
    0x0f,0x87,0xfb,0x00,0x00,0x00, // ja 0x147
    0x8b,0x0a, // mov ecx, dword ptr [edx]
    0x81,0xc9,0x20,0x20,0x00,0x00, // or ecx, 0x2020
    0x81,0xe1,0xff,0xff,0xff,0x00, // and ecx, 0xffffff
    0x81,0xf9,0x75,0x69,0x5f,0x00, // cmp ecx, 0x5f6975
    0x0f,0x85,0xe1,0x00,0x00,0x00, // jne 0x147
    0x8b,0x98,0x08,0x01,0x00,0x00, // mov ebx, dword ptr [eax + 0x108]
    0x83,0xfb,0x04, // cmp ebx, 4
    0x0f,0x87,0xd2,0x00,0x00,0x00, // ja 0x147
    0x85,0xdb, // test ebx, ebx
    0x0f,0x84,0xca,0x00,0x00,0x00, // je 0x147
    0x8b,0x8f,0xa4,0x00,0x00,0x00, // mov ecx, dword ptr [edi + 0xa4]
    0x83,0xf9,0x04, // cmp ecx, 4
    0x0f,0x87,0xbb,0x00,0x00,0x00, // ja 0x147
    0x89,0x4d,0xfc, // mov dword ptr [ebp - 4], ecx
    0x31,0xd2, // xor edx, edx
    0x39,0xca, // cmp edx, ecx
    0x73,0x19, // jae 0xae
    0x8b,0x84,0x97,0x84,0x00,0x00,0x00, // mov eax, dword ptr [edi + edx*4 + 0x84]
    0x89,0x44,0xd5,0xd0, // mov dword ptr [ebp + edx*8 - 0x30], eax
    0x8b,0x84,0x97,0x94,0x00,0x00,0x00, // mov eax, dword ptr [edi + edx*4 + 0x94]
    0x89,0x44,0xd5,0xd4, // mov dword ptr [ebp + edx*8 - 0x2c], eax
    0x42, // inc edx
    0xeb,0xe3, // jmp 0x91
    0xc7,0x87,0xa4,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // mov dword ptr [edi + 0xa4], 0
    0x8b,0xb6,0xd0,0x01,0x00,0x00, // mov esi, dword ptr [esi + 0x1d0]
    0x83,0xc6,0x68, // add esi, 0x68
    0x83,0x7e,0x08,0x01, // cmp dword ptr [esi + 8], 1
    0x75,0x34, // jne 0xfb
    0x83,0x7e,0x60,0x00, // cmp dword ptr [esi + 0x60], 0
    0x74,0x2e, // je 0xfb
    0x8b,0x46,0x3c, // mov eax, dword ptr [esi + 0x3c]
    0x0b,0x46,0x40, // or eax, dword ptr [esi + 0x40]
    0x0b,0x46,0x44, // or eax, dword ptr [esi + 0x44]
    0x25,0xff,0xff,0xff,0x7f, // and eax, 0x7fffffff
    0x74,0x1e, // je 0xfb
    0x8b,0x8f,0xa4,0x00,0x00,0x00, // mov ecx, dword ptr [edi + 0xa4]
    0x89,0xb4,0x8f,0x84,0x00,0x00,0x00, // mov dword ptr [edi + ecx*4 + 0x84], esi
    0xc7,0x84,0x8f,0x94,0x00,0x00,0x00,0x00,0x00,0x80,0xbf, // mov dword ptr [edi + ecx*4 + 0x94], 0xbf800000
    0xff,0x87,0xa4,0x00,0x00,0x00, // inc dword ptr [edi + 0xa4]
    0x81,0xc6,0xd4,0x00,0x00,0x00, // add esi, 0xd4
    0x4b, // dec ebx
    0x75,0xbd, // jne 0xc1
    0x31,0xdb, // xor ebx, ebx
    0x3b,0x5d,0xfc, // cmp ebx, dword ptr [ebp - 4]
    0x73,0x3c, // jae 0x147
    0x8b,0x97,0xa4,0x00,0x00,0x00, // mov edx, dword ptr [edi + 0xa4]
    0x83,0xfa,0x04, // cmp edx, 4
    0x73,0x31, // jae 0x147
    0x8b,0x44,0xdd,0xd0, // mov eax, dword ptr [ebp + ebx*8 - 0x30]
    0x31,0xc9, // xor ecx, ecx
    0x39,0xd1, // cmp ecx, edx
    0x73,0x0c, // jae 0x12c
    0x3b,0x84,0x8f,0x84,0x00,0x00,0x00, // cmp eax, dword ptr [edi + ecx*4 + 0x84]
    0x74,0x1b, // je 0x144
    0x41, // inc ecx
    0xeb,0xf0, // jmp 0x11c
    0x89,0x84,0x97,0x84,0x00,0x00,0x00, // mov dword ptr [edi + edx*4 + 0x84], eax
    0x8b,0x44,0xdd,0xd4, // mov eax, dword ptr [ebp + ebx*8 - 0x2c]
    0x89,0x84,0x97,0x94,0x00,0x00,0x00, // mov dword ptr [edi + edx*4 + 0x94], eax
    0xff,0x87,0xa4,0x00,0x00,0x00, // inc dword ptr [edi + 0xa4]
    0x43, // inc ebx
    0xeb,0xbf, // jmp 0x106
    0x5f, // pop edi
    0x5e, // pop esi
    0x5b, // pop ebx
    0x89,0xec, // mov esp, ebp
    0x5d, // pop ebp
    0xc2,0x04,0x00, // ret 4
};
inline constexpr size_t NativeTargetOffset=14;
inline constexpr uintptr_t SetupRva=0x431af0,CallOffset=0x68;
inline constexpr size_t SetupSize=0xb3;

inline const char* install(HMODULE image=GetModuleHandleW(nullptr)){
#if defined(__i386__) || defined(_M_IX86)
    using namespace NorthlightBlpMipGuard;
    const auto base=reinterpret_cast<uintptr_t>(image);
    if(base!=0x400000)return "unsupported-image-base";
    unsigned char setup[SetupSize],select[400],layout[166];
    if(!read(base+SetupRva,setup,sizeof setup)||!read(base+0x41e400,select,sizeof select)||
       !read(base+0x4333d0,layout,sizeof layout))return "unsupported-client";
    if(!digest(select,sizeof select,"0d28ec8c69bda0bb2e0a6ff8ef04d6271d898b5be15636a1d4ab70fdef71aa32")||
       !digest(layout,sizeof layout,"b910edc9143293226bc9ca203642a7f7cb85f4645e07f6842fa9e14d17b4fded"))return "client-layout-mismatch";
    auto* call=reinterpret_cast<unsigned char*>(base+SetupRva+CallOffset);
    int32_t original=int32_t((base+0x41e400)-(reinterpret_cast<uintptr_t>(call)+5));
    unsigned char expected[5]={0xe8};std::memcpy(expected+1,&original,4);
    const bool changed=std::memcmp(setup+CallOffset,expected,5)!=0;
    if(changed){
        if(setup[CallOffset]!=0xe8)return "call-site-mismatch";
        int32_t relative;std::memcpy(&relative,setup+CallOffset+1,4);
        unsigned char current[sizeof Selection];
        if(!read(reinterpret_cast<uintptr_t>(call)+5+relative,current,sizeof current)||
           std::memcmp(current,Selection,sizeof current))return "call-site-mismatch";
        std::memcpy(setup+CallOffset,expected,5);
    }
    if(!digest(setup,sizeof setup,"50a8b1fd89da572ac8d871c185509e28da71da92488363ea2027b404392878f5"))return "setup-signature-mismatch";
    if(changed)return "already-active";
    auto* thunk=static_cast<unsigned char*>(VirtualAlloc(nullptr,sizeof Selection,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!thunk)return "allocation-failed";
    std::memcpy(thunk,Selection,sizeof Selection);
    DWORD ignored=0;
    if(!VirtualProtect(thunk,sizeof Selection,PAGE_EXECUTE_READ,&ignored)||!FlushInstructionCache(GetCurrentProcess(),thunk,sizeof Selection)){
        VirtualFree(thunk,0,MEM_RELEASE);return "thunk-publication-failed";
    }
    PausedThreads threads;
    if(const auto* error=threads.pause(base+SetupRva,SetupSize)){VirtualFree(thunk,0,MEM_RELEASE);return error;}
    unsigned char current[SetupSize];
    if(!read(base+SetupRva,current,sizeof current)||std::memcmp(current,setup,sizeof current)){
        VirtualFree(thunk,0,MEM_RELEASE);return "setup-changed-concurrently";
    }
    DWORD old=0;
    if(!VirtualProtect(call,5,PAGE_EXECUTE_READWRITE,&old)){VirtualFree(thunk,0,MEM_RELEASE);return "code-protection-failed";}
    unsigned char replacement[5]={0xe8};
    const int32_t relative=int32_t(reinterpret_cast<uintptr_t>(thunk)-(reinterpret_cast<uintptr_t>(call)+5));
    std::memcpy(replacement+1,&relative,4);std::memcpy(call,replacement,5);
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),call,5)!=FALSE;
    if(flushed&&VirtualProtect(call,5,old,&ignored))return "active";
    std::memcpy(call,expected,5);
    const bool restored=FlushInstructionCache(GetCurrentProcess(),call,5)!=FALSE;
    const bool protectedAgain=VirtualProtect(call,5,old,&ignored)!=FALSE;
    VirtualFree(thunk,0,MEM_RELEASE);
    return !restored?"rollback-cache-failed":!protectedAgain?"rollback-protection-failed":"call-publication-failed";
#else
    (void)image;return "requires-x86";
#endif
}
} // namespace NorthlightGlueLighting
