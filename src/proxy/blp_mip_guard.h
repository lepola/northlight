#pragma once
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstring>
#include "backend_loader.h"

// The audited 12340 BLP direct-view loop is bounded in memory only. All jump
// targets stay in the client, so a probe/unload of d3d9 cannot leave a callback
// pointing into an unloaded DLL. No client instruction bytes are embedded.
namespace NorthlightBlpMipGuard {
inline constexpr uintptr_t DirectRva=0x2affd0,PatchOffset=0x12f;
inline constexpr size_t DirectSize=350;
inline constexpr unsigned char BoundedLoop[]={
    0x8b,0x8e,0xa0,0x04,0x00,0x00, // mov ecx,[esi+4a0h]
    0x83,0xf9,0x10,0x76,0x03,0x6a,0x10,0x59, // min(count,16)
    0x8b,0x96,0x98,0x04,0x00,0x00,0x56,0x83,0xc6,0x18,
    0x8b,0x7d,0x10,0x8b,0x3f,0xe3,0x0c,
    0x83,0x7e,0x40,0x00,0x74,0x06, // stop on the first zero size too
    0xad,0x01,0xd0,0xab,0xe2,0xf4, // offset + source -> output; loop
    0x5e,0xeb,0xbc,0x90            // restore self; original success epilogue
};
static_assert(PatchOffset+sizeof BoundedLoop==DirectSize);

inline bool read(uintptr_t address,void* out,size_t size){
    SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,size,&got)&&got==size;
}
inline bool digest(const void* data,size_t size,const char* expected){
    NorthlightBackendLoader::Sha256 hash;hash.update(data,size);return hash.hex()==expected;
}

#if defined(__i386__) || defined(_M_IX86)
// Resolve and census before stopping threads. While stopped, use only kernel
// operations and stack storage: no heap, logger, loader or C++ static locks.
// The native iterator also visits threads created during the initial census.
struct PausedThreads {
    struct Thread {HANDLE handle=nullptr;DWORD id=0;bool paused=false;};
    Thread threads[1024]={};DWORD census[1024]={};size_t count=0,censusCount=0;
    using Next=LONG(NTAPI*)(HANDLE,HANDLE,ACCESS_MASK,ULONG,ULONG,PHANDLE);
    const char* pause(uintptr_t begin,size_t size){
        const auto raw=GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtGetNextThread");
        Next next=nullptr;static_assert(sizeof next==sizeof raw);std::memcpy(&next,&raw,sizeof next);
        if(!next)return "thread-iterator-unavailable";
        const DWORD process=GetCurrentProcessId(),caller=GetCurrentThreadId();
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD,0);
        if(snapshot==INVALID_HANDLE_VALUE)return "thread-census-failed";
        THREADENTRY32 item={};item.dwSize=sizeof item;
        BOOL more=Thread32First(snapshot,&item);bool complete=more!=FALSE;
        while(more){
            if(item.th32OwnerProcessID==process){
                if(censusCount==1024){complete=false;break;}
                census[censusCount++]=item.th32ThreadID;
            }
            more=Thread32Next(snapshot,&item);
        }
        if(complete&&GetLastError()!=ERROR_NO_MORE_FILES)complete=false;
        CloseHandle(snapshot);
        if(!complete||!censusCount)return "thread-census-failed";
        HANDLE cursor=nullptr;
        for(;;){
            HANDLE handle=nullptr;
            // MAXIMUM_ALLOWED avoids silently skipping a thread just because
            // suspend/context rights were denied. The census is checked below.
            const LONG status=next(GetCurrentProcess(),cursor,MAXIMUM_ALLOWED,0,0,&handle);
            if(status==LONG(0x8000001au))break; // STATUS_NO_MORE_ENTRIES
            if(status<0)return "thread-enumeration-failed";
            if(count==1024){CloseHandle(handle);return "too-many-threads";}
            auto& thread=threads[count++];thread.handle=handle;cursor=handle;
            thread.id=GetThreadId(handle);
            if(!thread.id)return "thread-identity-failed";
            if(thread.id==caller)continue;
            if(SuspendThread(handle)==DWORD(-1))return "thread-suspend-failed";
            thread.paused=true;
            CONTEXT context={};context.ContextFlags=CONTEXT_CONTROL;
            if(!GetThreadContext(handle,&context))return "thread-context-failed";
            if(uintptr_t(context.Eip)>=begin&&uintptr_t(context.Eip)-begin<size)return "decoder-busy";
        }
        // A thread that exited or could not be enumerated is a reason to skip
        // this attempt, rather than guessing that every executable PC is safe.
        for(size_t i=0;i<censusCount;++i){
            bool found=false;for(size_t j=0;j<count;++j)if(threads[j].id==census[i]){found=true;break;}
            if(!found)return "thread-set-changed";
        }
        return nullptr;
    }
    ~PausedThreads(){
        for(size_t i=count;i>0;--i){auto& t=threads[i-1];if(t.paused)ResumeThread(t.handle);CloseHandle(t.handle);}
    }
};
struct MemoryOps {
    decltype(&VirtualProtect) protect=&VirtualProtect;
    decltype(&FlushInstructionCache) flush=&FlushInstructionCache;
};
inline const char* replace(unsigned char* function,const unsigned char (&before)[DirectSize],MemoryOps ops={}){
    PausedThreads threads;
    auto* target=function+PatchOffset;
    if(const auto* error=threads.pause(reinterpret_cast<uintptr_t>(target),sizeof BoundedLoop))return error;
    if(std::memcmp(function,before,DirectSize))return "decoder-changed-concurrently";
    DWORD old=0;
    if(!ops.protect(target,sizeof BoundedLoop,PAGE_EXECUTE_READWRITE,&old))return "code-protection-failed";
    std::memcpy(target,BoundedLoop,sizeof BoundedLoop);
    const bool flushed=ops.flush(GetCurrentProcess(),target,sizeof BoundedLoop)!=FALSE;
    DWORD ignored=0;
    if(flushed&&ops.protect(target,sizeof BoundedLoop,old,&ignored))return "active";
    // Restore the exact saved code before any thread resumes if publication
    // failed. A failed protection restore leaves the page writable here.
    std::memcpy(target,before+PatchOffset,sizeof BoundedLoop);
    const bool rollbackFlush=ops.flush(GetCurrentProcess(),target,sizeof BoundedLoop)!=FALSE;
    const bool rollbackProtect=ops.protect(target,sizeof BoundedLoop,old,&ignored)!=FALSE;
    if(!rollbackFlush)return "rollback-cache-failed";
    if(!rollbackProtect)return "rollback-protection-failed";
    return flushed?"code-protection-restore-failed":"code-cache-flush-failed";
}
#endif

inline const char* install(HMODULE image=GetModuleHandleW(nullptr)){
#if defined(__i386__) || defined(_M_IX86)
    const auto base=reinterpret_cast<uintptr_t>(image);
    if(base!=0x400000)return "unsupported-image-base";
    IMAGE_DOS_HEADER dos={};IMAGE_NT_HEADERS32 nt={};
    if(!read(base,&dos,sizeof dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>1048576||
       !read(base+dos.e_lfanew,&nt,sizeof nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||
       nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||nt.OptionalHeader.ImageBase!=0x400000||
       nt.OptionalHeader.SizeOfImage<DirectRva+DirectSize)return "unsupported-client";
    unsigned char direct[DirectSize],attach[144],levels[96];
    if(!read(base+DirectRva,direct,sizeof direct)||!read(base+0x2ae900,attach,sizeof attach)||!read(base+0x2ab700,levels,sizeof levels))return "client-code-unreadable";
    if(!digest(attach,sizeof attach,"d0e0b60eaec5970f40cb7a1942eb997ccd5293ad0f272ca3585d20e38f1b54a1")||
       !digest(levels,sizeof levels,"54b9266c7fa1b3c636c9c8f27f8ced76730c10bd43d0bc157353e6b341d5cebc"))return "client-layout-mismatch";
    if(digest(direct,sizeof direct,"019e36b156bbca51383f29635599008ff4ec8cfbfc1233207fd9b088818809c6"))return "already-active";
    if(!digest(direct,sizeof direct,"f74ce8ec7778092011d97da5cd9b8e3a38347cd7aead0fbfaf6da05dbe318409"))return "decoder-signature-mismatch";
    return replace(reinterpret_cast<unsigned char*>(base+DirectRva),direct);
#else
    (void)image;return "requires-x86";
#endif
}
} // namespace NorthlightBlpMipGuard
