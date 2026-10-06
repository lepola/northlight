// Host test of the pure MEMMAP logic (address_space_snapshot.h) with fake VirtualQuery region lists.
#include "address_space_snapshot.h"
#include <cassert>
#include <string>
#include <vector>
using namespace NorthlightMemMap;
static std::vector<std::string> lines;
static void sink(const char* l){lines.emplace_back(l);}
static void name(std::uint64_t base,unsigned,char* out,std::size_t n){std::snprintf(out,n,"mod%llx.dll",(unsigned long long)base);}
static const std::uint64_t M=1u<<20;
static Region R(std::uint64_t base,std::uint64_t alloc,std::uint64_t size,unsigned st,unsigned type,unsigned prot=4){Region r;r.base=base;r.allocBase=alloc;r.size=size;r.state=st;r.type=type;r.protect=prot;return r;}
static bool has(const char* needle){for(auto& l:lines)if(l.find(needle)!=std::string::npos)return true;return false;}
int main(){
    Collector a;a.begin();
    a.add(R(0x10000,0,0x1000,Free,NoType));
    a.add(R(0x400000,0x400000,0x1000,Commit,Image,2));a.add(R(0x401000,0x400000,3*M,Commit,Image,0x20));      // image 0x400000: 3 MiB + 4 KiB, all committed
    a.add(R(0x1000000,0x1000000,10*M,Commit,Private));a.add(R(0x1A00000,0x1000000,30*M,Reserve,Private));    // private 40 MiB, 10 committed
    a.add(R(0x4000000,0x4000000,50*M,Free,NoType));
    a.add(R(0x8000000,0x8000000,5*M,Commit,Mapped));
    a.add(R(0x9000000,0x9000000,2*M,Reserve,Private));
    const Totals& t=a.totals();
    assert(t.regions==8&&t.allocations==4&&t.freeBytes==50*M+0x1000&&t.largestFree==50*M);
    assert(t.commit[Image]==3*M+0x1000&&t.commit[Private]==10*M&&t.reserve[Private]==32*M&&t.commit[Mapped]==5*M);
    // regions of one AllocationBase around a free gap are two allocations
    Alloc best[TopCount];std::size_t n=a.top(best,TopCount);assert(n==4&&best[0].base==0x1000000&&best[0].total==40*M&&best[0].committed==10*M&&best[1].base==0x8000000);
    n=a.top(best,2);assert(n==2&&best[0].total>=best[1].total&&best[1].base==0x8000000);
    // second snapshot: private heap shrank by 20 MiB (changed), mapped gone, a 8 MiB private new, a 512 KiB new (ignored)
    Collector b;b.begin();
    b.add(R(0x400000,0x400000,3*M+0x1000,Commit,Image));
    b.add(R(0x1000000,0x1000000,20*M,Commit,Private));
    b.add(R(0x4000000,0x4000000,8*M,Commit,Private));
    b.add(R(0x5000000,0x5000000,M/2,Commit,Private));
    b.add(R(0x9000000,0x9000000,2*M,Reserve,Private));
    Diff d;diff(a.allocs(),b.allocs(),d);
    assert(d.addedCount==1&&d.added[0].a.base==0x4000000&&d.addedBytes==8*M);
    assert(d.goneCount==1&&d.gone[0].a.base==0x8000000&&d.goneBytes==5*M);
    assert(d.changedCount==1&&d.changed[0].a.base==0x1000000&&d.changed[0].before==40*M&&d.changedDelta==20*M&&d.changedGrew==0);
    report(sink,"device=2 point=create frame=0 tick=1",b,&a,&name,0.5);
    assert(has("MEMMAP summary device=2 point=create")&&has("MEMMAP top ")&&has("module=mod400000.dll")&&has("MEMMAP diff-new ")&&has("MEMMAP diff-gone ")&&has("MEMMAP diff-changed ")&&has("wasMiB=40.0"));
    assert(has("netMiB=-17.0"));
    lines.clear();report(sink,"x",a,nullptr,nullptr,0.0);assert(has("previous=none")&&!has("module="));
    // identical maps: nothing
    Diff same;diff(a.allocs(),a.allocs(),same);assert(!same.addedCount&&!same.goneCount&&!same.changedCount);
    // diff list cap: counts and bytes stay exact while stored entries are capped
    Collector many,none;many.begin();none.begin();for(unsigned i=0;i<100;++i)many.add(R(0x10000000+std::uint64_t(i)*0x1000000,0x10000000+std::uint64_t(i)*0x1000000,2*M,Commit,Private));
    diff(none.allocs(),many.allocs(),d);assert(d.addedCount==100&&d.nAdded==DiffCap&&d.addedBytes==200*M);
    // capacity: past MaxAllocs the allocations are dropped, regions of a dropped allocation do not leak into the previous one
    Collector full;full.begin();for(unsigned i=0;i<MaxAllocs+10;++i){full.add(R(0x10000+std::uint64_t(i)*0x20000,0x10000+std::uint64_t(i)*0x20000,0x1000,Commit,Private));full.add(R(0x11000+std::uint64_t(i)*0x20000,0x10000+std::uint64_t(i)*0x20000,0x1000,Reserve,Private));}
    assert(full.allocs().size()<=full.allocs().capacity()&&full.totals().dropped>=10&&full.allocs().back().total==0x2000);
    Collector swapA,swapB;swapA.begin();swapA.add(R(0x10000,0x10000,0x1000,Commit,Private));swapA.swap(swapB);assert(swapB.allocs().size()==1&&swapA.allocs().empty());
    std::puts("PASS memmap logic: grouping by AllocationBase, totals per type, top-N, new/gone/changed diff with caps, formats, capacity guard.");
}
