#pragma once
#include "settings_transport.h"
#ifdef _WIN32
#include <windows.h>
#endif

namespace NorthlightSettings {
inline void (*bridgeLog)(const char*)=nullptr;
#if defined(_WIN32) && (defined(__i386__) || defined(_M_IX86))
inline bool clientBytes(std::uintptr_t address,const void* expected,size_t count){unsigned char actual[32];SIZE_T got=0;return count<=sizeof actual&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),actual,count,&got)&&got==count&&!std::memcmp(expected,actual,count);}
inline bool cvarSetterSupported(std::uintptr_t address,const unsigned char* expected,size_t count){
    if(clientBytes(address,expected,count))return true;
    // wow_optimize detours the public setter after startup. Keep calling that
    // setter (including its hook), with the original ABI and CVar flag checks.
    unsigned char jump[5];SIZE_T got=0;
    if(count<5||!clientBytes(address+5,expected+5,count-5)||
       !ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),jump,sizeof jump,&got)||got!=sizeof jump||jump[0]!=0xe9)return false;
    std::int32_t relative=0;std::memcpy(&relative,jump+1,sizeof relative);
    const auto destination=address+5+relative;
    const auto optimizer=GetModuleHandleW(L"wow_optimize.dll");MEMORY_BASIC_INFORMATION page{};
    static const unsigned char optimizerSetter[]={0x55,0x8b,0xec,0x51,0x53,0x8b,0xd9,0x89,0x55,0xfc,0x56,0x8b,0x75,0x08,0x8b,0xce};
    return optimizer&&VirtualQuery(reinterpret_cast<void*>(destination),&page,sizeof page)&&page.AllocationBase==optimizer&&page.Type==MEM_IMAGE&&
        (page.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))&&clientBytes(destination,optimizerSetter,sizeof optimizerSetter);
}
inline bool cvarClientSupported(){
    // Verified against the local 3.3.5a build 12340. Calls only; no detours, patches or Lua registration.
    static const unsigned char lookup[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x85,0xc0,0x74,0x1a,0x50,0xb9,0xfc,0x19,0xca,0x00};
    static const unsigned char setter[]={0x55,0x8b,0xec,0x53,0x8b,0x5d,0x0c,0x84,0xdb,0x56,0x57,0x8b,0x7d,0x08,0x8b,0xf1};
    auto base=GetModuleHandleW(nullptr);if(reinterpret_cast<std::uintptr_t>(base)!=0x400000)return false;
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS32 nt{};SIZE_T got=0;
    if(!ReadProcessMemory(GetCurrentProcess(),base,&dos,sizeof dos,&got)||got!=sizeof dos||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<64||dos.e_lfanew>4096)return false;
    if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<char*>(base)+dos.e_lfanew,&nt,sizeof nt,&got)||got!=sizeof nt)return false;
    return nt.Signature==IMAGE_NT_SIGNATURE&&nt.FileHeader.Machine==IMAGE_FILE_MACHINE_I386&&nt.FileHeader.TimeDateStamp==0x4c2452fe&&nt.OptionalHeader.SizeOfImage==0x9fd000&&clientBytes(0x767460,lookup,sizeof lookup)&&cvarSetterSupported(0x7668c0,setter,sizeof setter);
}
inline std::string cvarValue(void* cvar){
    if(!cvar)return "";const char* value=nullptr;SIZE_T got=0;
    if(!ReadProcessMemory(GetCurrentProcess(),static_cast<char*>(cvar)+0x28,&value,sizeof value,&got)||got!=sizeof value||!value)return "";
    MEMORY_BASIC_INFORMATION region{};if(!VirtualQuery(value,&region,sizeof region)||region.State!=MEM_COMMIT||(region.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return "";
    const size_t n=std::min<size_t>(1024,reinterpret_cast<std::uintptr_t>(region.BaseAddress)+region.RegionSize-reinterpret_cast<std::uintptr_t>(value));char data[1024];
    if(!ReadProcessMemory(GetCurrentProcess(),value,data,n,&got)||got!=n)return "";auto* end=static_cast<const char*>(std::memchr(data,0,n));return end?std::string(data,size_t(end-data)):"";
}
inline bool unprotectedCvar(void* cvar){DWORD flags=0;SIZE_T got=0;return cvar&&ReadProcessMemory(GetCurrentProcess(),static_cast<char*>(cvar)+0x1c,&flags,sizeof flags,&got)&&got==sizeof flags&&(flags&0x14c)==0;}
inline void gameThreadPump(){
    static const bool supported=[](){const bool value=cvarClientSupported();if(bridgeLog)bridgeLog(value?"SETTINGS bridge ready (native / verified wow_optimize CVar setter)":"SETTINGS bridge unavailable: unsupported client/CVar setter signature");return value;}();if(!supported)return;
    static DWORD owner=0,last=0;auto now=GetTickCount();if(owner&&owner!=GetCurrentThreadId())return;if(last&&now-last<10)return;owner=GetCurrentThreadId();last=now;
    auto service=peek();if(!service)return;
    using Lookup=void*(__cdecl*)(const char*);using Set=bool(__thiscall*)(void*,const char*,int,int,int,int);
    const auto lookup=reinterpret_cast<Lookup>(0x767460);const auto set=reinterpret_cast<Set>(0x7668c0);
    void* request=lookup("northlightSettingsRequest");void* reply=lookup("northlightSettingsReply");if(!request||!reply)return;
    if(!unprotectedCvar(request)||!unprotectedCvar(reply))return;
    static Transport transport;
    try{const auto output=transport.exchange(*service,cvarValue(request));if(!output.empty()&&output!=cvarValue(reply))set(reply,output.c_str(),1,0,0,1);}catch(...){/* Fail closed: no arbitrary Lua or alternate integration path. */}
}
#else
inline void gameThreadPump(){}
#endif
} // namespace NorthlightSettings
