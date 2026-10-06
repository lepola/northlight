#pragma once
// 0.3.192 (MEMMAP): one-shot address-space snapshots at device lifecycle points (destroy-begin/-end, create,
// frame 300), always on (rare events). Pure part (host-testable, no Win32): Collector groups a VirtualQuery
// walk's regions by AllocationBase, picks the top allocations, diffs against the previous snapshot of the process
// (new / gone / grew-or-shrank, >= 1 MiB) and formats MEMMAP lines through a sink. Win32 part (#ifdef _WIN32):
// the walk, the process heaps, thread/module counts, process memory counters. Only kernel32/toolhelp APIs;
// HeapSummary and K32GetProcessMemoryInfo via GetProcAddress (Wine may lack them). Storage is reserved once
// (a few thousand entries); nothing here allocates after the first snapshot, nothing throws to the caller.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#endif
namespace NorthlightMemMap {
enum State:unsigned {Free=0,Commit=1,Reserve=2};
enum Type:unsigned {NoType=0,Image=1,Mapped=2,Private=3};
struct Region {std::uint64_t base=0,allocBase=0,size=0;unsigned state=Free,type=NoType,protect=0;};
struct Alloc {std::uint64_t base=0,total=0,committed=0;unsigned type=NoType,protect=0;};
struct Totals {std::uint64_t commit[4]={},reserve[4]={},freeBytes=0,largestFree=0,regions=0,allocations=0,dropped=0;};
constexpr std::size_t MaxAllocs=4096,TopCount=12,DiffCap=24;
constexpr std::uint64_t DiffMinBytes=1u<<20;
inline const char* typeName(unsigned t){return t==Image?"IMAGE":t==Mapped?"MAPPED":t==Private?"PRIVATE":"NONE";}
inline double mib(std::uint64_t b){return double(b)/1048576.0;}

struct DiffEntry {Alloc a;std::uint64_t before=0;};
struct Diff {
    DiffEntry added[DiffCap],gone[DiffCap],changed[DiffCap];
    unsigned nAdded=0,nGone=0,nChanged=0;                 // stored (<= DiffCap)
    unsigned addedCount=0,goneCount=0,changedCount=0;     // all
    std::uint64_t addedBytes=0,goneBytes=0,changedDelta=0,changedGrew=0;
};

// Regions arrive in address order, so one AllocationBase's regions are adjacent.
class Collector {
    std::vector<Alloc> allocs_;Totals totals_;std::uint64_t cur_=~0ull;bool curDropped_=false;
public:
    Collector(){allocs_.reserve(MaxAllocs);}
    void begin(){allocs_.clear();totals_=Totals();cur_=~0ull;curDropped_=false;}
    void add(const Region& r){
        ++totals_.regions;
        if(r.state==Free){totals_.freeBytes+=r.size;if(r.size>totals_.largestFree)totals_.largestFree=r.size;cur_=~0ull;return;}
        const unsigned t=r.type<4?r.type:0;
        (r.state==Commit?totals_.commit:totals_.reserve)[t]+=r.size;
        if(r.allocBase!=cur_||cur_==~0ull){
            cur_=r.allocBase;curDropped_=false;
            if(allocs_.size()>=allocs_.capacity()){curDropped_=true;++totals_.dropped;return;}
            Alloc a;a.base=r.allocBase;a.type=t;a.protect=r.protect;allocs_.push_back(a);++totals_.allocations;
        }
        if(curDropped_)return;
        Alloc& a=allocs_.back();a.total+=r.size;if(r.state==Commit)a.committed+=r.size;
    }
    const std::vector<Alloc>& allocs()const{return allocs_;}
    const Totals& totals()const{return totals_;}
    void swap(Collector& o){allocs_.swap(o.allocs_);std::swap(totals_,o.totals_);std::swap(cur_,o.cur_);std::swap(curDropped_,o.curDropped_);}
    // The n largest by total, descending (n <= TopCount); returns how many.
    std::size_t top(Alloc* out,std::size_t n)const{
        std::size_t have=0;
        for(const Alloc& a:allocs_){
            std::size_t i;
            if(have<n)i=have++;else if(n&&a.total>out[n-1].total)i=n-1;else continue;
            while(i>0&&out[i-1].total<a.total){out[i]=out[i-1];--i;}
            out[i]=a;
        }
        return have;
    }
};
// Both lists are in ascending base order (a VirtualQuery walk).
inline void diff(const std::vector<Alloc>& prev,const std::vector<Alloc>& cur,Diff& d){
    d=Diff();
    auto push=[](DiffEntry* list,unsigned& n,unsigned& all,const Alloc& a,std::uint64_t before){++all;if(n<DiffCap)list[n++]={a,before};};
    std::size_t i=0,j=0;
    while(i<prev.size()||j<cur.size()){
        if(j>=cur.size()||(i<prev.size()&&prev[i].base<cur[j].base)){
            if(prev[i].total>=DiffMinBytes){push(d.gone,d.nGone,d.goneCount,prev[i],prev[i].total);d.goneBytes+=prev[i].total;}
            ++i;
        }else if(i>=prev.size()||cur[j].base<prev[i].base){
            if(cur[j].total>=DiffMinBytes){push(d.added,d.nAdded,d.addedCount,cur[j],0);d.addedBytes+=cur[j].total;}
            ++j;
        }else{
            const std::uint64_t a=prev[i].total,b=cur[j].total,delta=a>b?a-b:b-a;
            if(delta>=DiffMinBytes){push(d.changed,d.nChanged,d.changedCount,cur[j],a);if(b>a)d.changedGrew+=delta;else d.changedDelta+=delta;}
            ++i;++j;
        }
    }
}
// name(base,type,buf,size) fills buf with a module/file name or "" (may be null).
using NameFn=void(*)(std::uint64_t,unsigned,char*,std::size_t);
using Sink=void(*)(const char*);
inline void formatAlloc(char* out,std::size_t size,const Alloc& a,NameFn name,std::uint64_t before=~0ull){
    char n[96]="";if(name&&a.type==Image)name(a.base,a.type,n,sizeof n);
    int w=std::snprintf(out,size,"base=0x%08llx sizeMiB=%.1f commitMiB=%.1f type=%s prot=0x%02x",(unsigned long long)a.base,mib(a.total),mib(a.committed),typeName(a.type),a.protect);
    if(w>0&&std::size_t(w)<size&&before!=~0ull)w+=std::snprintf(out+w,size-std::size_t(w)," wasMiB=%.1f",mib(before));
    if(w>0&&std::size_t(w)<size&&n[0])std::snprintf(out+w,size-std::size_t(w)," module=%s",n);
}
inline void emitList(Sink sink,const char* kind,const char* label,const DiffEntry* list,unsigned stored,unsigned all,std::uint64_t bytes,NameFn name,bool showBefore){
    char line[512];
    std::snprintf(line,sizeof line,"MEMMAP %s %s count=%u totalMiB=%.1f shown=%u",kind,label,all,mib(bytes),stored);sink(line);
    for(unsigned i=0;i<stored;++i){
        char one[256];formatAlloc(one,sizeof one,list[i].a,name,showBefore?list[i].before:~0ull);
        std::snprintf(line,sizeof line,"MEMMAP %s %s #%u %s",kind,label,i,one);sink(line);
    }
}
// Summary, top and diff lines for what `cur` holds, diffed against `prev` (null on the first snapshot).
inline void report(Sink sink,const char* label,const Collector& cur,const Collector* prev,NameFn name,double walkMs){
    char line[640];const Totals& t=cur.totals();
    const std::uint64_t commit=t.commit[1]+t.commit[2]+t.commit[3],reserve=t.reserve[1]+t.reserve[2]+t.reserve[3];
    std::snprintf(line,sizeof line,"MEMMAP summary %s commitMiB=%.1f reserveOnlyMiB=%.1f image=%.1f/%.1f mapped=%.1f/%.1f private=%.1f/%.1f freeMiB=%.1f largestFreeMiB=%.1f regions=%llu allocations=%llu dropped=%llu walkMs=%.2f (type=commit/reserve MiB)",
        label,mib(commit),mib(reserve),mib(t.commit[Image]),mib(t.reserve[Image]),mib(t.commit[Mapped]),mib(t.reserve[Mapped]),mib(t.commit[Private]),mib(t.reserve[Private]),
        mib(t.freeBytes),mib(t.largestFree),(unsigned long long)t.regions,(unsigned long long)t.allocations,(unsigned long long)t.dropped,walkMs);sink(line);
    Alloc best[TopCount];const std::size_t n=cur.top(best,TopCount);
    for(std::size_t i=0;i<n;++i){char one[256];formatAlloc(one,sizeof one,best[i],name);std::snprintf(line,sizeof line,"MEMMAP top %s #%u %s",label,unsigned(i),one);sink(line);}
    if(!prev){std::snprintf(line,sizeof line,"MEMMAP diff %s previous=none",label);sink(line);return;}
    Diff d;diff(prev->allocs(),cur.allocs(),d);
    emitList(sink,"diff-new",label,d.added,d.nAdded,d.addedCount,d.addedBytes,name,false);
    emitList(sink,"diff-gone",label,d.gone,d.nGone,d.goneCount,d.goneBytes,name,false);
    emitList(sink,"diff-changed",label,d.changed,d.nChanged,d.changedCount,d.changedGrew,name,true);
    std::snprintf(line,sizeof line,"MEMMAP diff %s netMiB=%.1f (new+grew %.1f, gone+shrank %.1f; allocations >= 1 MiB or changed by >= 1 MiB)",label,
        mib(d.addedBytes+d.changedGrew)-mib(d.goneBytes+d.changedDelta),mib(d.addedBytes+d.changedGrew),mib(d.goneBytes+d.changedDelta));sink(line);
}

#ifdef _WIN32
namespace Detail {
inline Collector& current(){static Collector c;return c;}
inline Collector& previous(){static Collector c;return c;}
inline bool& havePrevious(){static bool h=false;return h;}
inline std::atomic_flag& busy(){static std::atomic_flag f=ATOMIC_FLAG_INIT;return f;}
inline double ms(LARGE_INTEGER a,LARGE_INTEGER b,LARGE_INTEGER f){return f.QuadPart?1000.0*double(b.QuadPart-a.QuadPart)/double(f.QuadPart):0.0;}
inline void moduleName(std::uint64_t base,unsigned,char* out,std::size_t size){
    out[0]=0;char full[MAX_PATH]="";
    if(!GetModuleFileNameA(reinterpret_cast<HMODULE>(std::uintptr_t(base)),full,sizeof full))return;
    const char* slash=std::strrchr(full,'\\');const char* fwd=std::strrchr(full,'/');if(fwd>slash)slash=fwd;
    std::snprintf(out,size,"%s",slash?slash+1:full);
}
struct HeapSummaryT {DWORD cb;SIZE_T cbAllocated,cbCommitted,cbReserved,cbMaxReserve;};
}
// One snapshot. `northlight` is a preformatted tally string (may be empty). Skips when another snapshot is
// running (replay thread vs render thread); never throws.
inline void snapshot(Sink sink,const char* label,const char* northlight){
    using namespace Detail;
    if(busy().test_and_set(std::memory_order_acquire))return;
    try{
        LARGE_INTEGER freq={},t0={},t1={},t2={};QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&t0);
        Collector& cur=current();cur.begin();
        SYSTEM_INFO si={};GetSystemInfo(&si);
        std::uint64_t at=std::uint64_t(reinterpret_cast<std::uintptr_t>(si.lpMinimumApplicationAddress));if(at<0x10000)at=0x10000;
        const std::uint64_t end=std::uint64_t(reinterpret_cast<std::uintptr_t>(si.lpMaximumApplicationAddress))+1;
        for(unsigned guard=0;at<end&&guard<400000;++guard){
            MEMORY_BASIC_INFORMATION m={};
            if(VirtualQuery(reinterpret_cast<const void*>(std::uintptr_t(at)),&m,sizeof m)!=sizeof m)break;
            Region r;r.base=std::uint64_t(reinterpret_cast<std::uintptr_t>(m.BaseAddress));r.size=m.RegionSize;if(!r.size)break;
            r.allocBase=std::uint64_t(reinterpret_cast<std::uintptr_t>(m.AllocationBase));r.protect=m.State==MEM_FREE?0:m.Protect;
            r.state=m.State==MEM_FREE?Free:m.State==MEM_COMMIT?Commit:Reserve;
            r.type=m.Type==MEM_IMAGE?Image:m.Type==MEM_MAPPED?Mapped:m.Type==MEM_PRIVATE?Private:NoType;
            cur.add(r);at=r.base+r.size;
        }
        QueryPerformanceCounter(&t1);
        report(sink,label,cur,havePrevious()?&previous():nullptr,&moduleName,ms(t0,t1,freq));
        char line[640];
        // Process memory counters (psapi's K32 forwarders live in kernel32 on Windows 7+ and in Wine).
        {using Fn=BOOL(WINAPI*)(HANDLE,PROCESS_MEMORY_COUNTERS*,DWORD);
         HMODULE k=GetModuleHandleA("kernel32.dll");Fn fn=k?reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(k,"K32GetProcessMemoryInfo"))):nullptr;
         PROCESS_MEMORY_COUNTERS pmc={};pmc.cb=sizeof pmc;
         if(fn&&fn(GetCurrentProcess(),&pmc,sizeof pmc))std::snprintf(line,sizeof line,"MEMMAP process %s workingSetMiB=%.1f peakWorkingSetMiB=%.1f pagefileMiB=%.1f peakPagefileMiB=%.1f",label,mib(pmc.WorkingSetSize),mib(pmc.PeakWorkingSetSize),mib(pmc.PagefileUsage),mib(pmc.PeakPagefileUsage));
         else std::snprintf(line,sizeof line,"MEMMAP process %s unavailable",label);
         sink(line);}
        // Heaps: HeapSummary only (no HeapWalk), 8 ms total.
        {HANDLE heaps[64];const DWORD count=GetProcessHeaps(64,heaps);const DWORD have=count<64?count:64;
         using Fn=BOOL(WINAPI*)(HANDLE,DWORD,HeapSummaryT*);
         HMODULE k=GetModuleHandleA("kernel32.dll");Fn fn=k?reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(k,"HeapSummary"))):nullptr;
         std::uint64_t committed=0,allocated=0;unsigned summarized=0,listed=0;bool capped=false;const HANDLE process=GetProcessHeap();
         std::snprintf(line,sizeof line,"MEMMAP heaps %s count=%lu heapSummary=%d",label,(unsigned long)count,fn?1:0);sink(line);
         for(DWORD i=0;fn&&i<have;++i){
             QueryPerformanceCounter(&t2);if(ms(t1,t2,freq)>8.0){capped=true;break;}
             HeapSummaryT hs={};hs.cb=sizeof hs;if(!fn(heaps[i],0,&hs))continue;
             ++summarized;committed+=hs.cbCommitted;allocated+=hs.cbAllocated;
             if(hs.cbCommitted>=(1u<<20)&&listed<12){++listed;std::snprintf(line,sizeof line,"MEMMAP heap %s #%lu handle=0x%08llx%s allocatedMiB=%.1f committedMiB=%.1f reservedMiB=%.1f",label,(unsigned long)i,(unsigned long long)reinterpret_cast<std::uintptr_t>(heaps[i]),heaps[i]==process?" default=1":"",mib(hs.cbAllocated),mib(hs.cbCommitted),mib(hs.cbReserved));sink(line);}
         }
         if(fn){std::snprintf(line,sizeof line,"MEMMAP heaps %s summarized=%u allocatedMiB=%.1f committedMiB=%.1f capped=%d",label,summarized,mib(allocated),mib(committed),capped?1:0);sink(line);}}
        // Threads (ours only) and modules.
        {const DWORD pid=GetCurrentProcessId();unsigned threads=0,modules=0;bool ok=false;
         HANDLE s=CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD|TH32CS_SNAPMODULE,pid);
         if(s!=INVALID_HANDLE_VALUE){ok=true;
             THREADENTRY32 te={};te.dwSize=sizeof te;if(Thread32First(s,&te))do{if(te.th32OwnerProcessID==pid)++threads;}while(Thread32Next(s,&te));
             MODULEENTRY32 me={};me.dwSize=sizeof me;if(Module32First(s,&me))do{++modules;}while(Module32Next(s,&me));
             CloseHandle(s);}
         if(ok)std::snprintf(line,sizeof line,"MEMMAP threads %s threads=%u modules=%u",label,threads,modules);else std::snprintf(line,sizeof line,"MEMMAP threads %s unavailable",label);
         sink(line);}
        if(northlight&&northlight[0]){std::snprintf(line,sizeof line,"MEMMAP northlight %s %s",label,northlight);sink(line);}
        QueryPerformanceCounter(&t2);
        std::snprintf(line,sizeof line,"MEMMAP done %s totalMs=%.2f",label,ms(t0,t2,freq));sink(line);
        current().swap(previous());havePrevious()=true;
    }catch(...){}
    busy().clear(std::memory_order_release);
}
#endif
}
