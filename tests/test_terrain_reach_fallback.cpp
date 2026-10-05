#include "terrain_reach_fallback.h"
#include "geometry_memory.h"
#include <cassert>
#include <iostream>
using namespace NorthlightTerrainReach;
namespace GM=NorthlightGeometryMemory;
int main(){
    constexpr float Ext=4096;
    // Observed Bael Modan sample (aggregate 760 MiB, largest block 63 MiB), synthetic growth checks.
    const GM::Sample observed{760*GM::MiB,63*GM::MiB,true},unfragmented{800*GM::MiB,63*GM::MiB,true};
    for(double mib:{1.0,23.58,37.92,64.0}){
        const uint64_t bytes=uint64_t(mib*GM::MiB);
        assert(!GM::admits(observed,GM::buildBudget(bytes)));   // full-reach sequence refused
        if(mib>31)assert(!GM::admits(unfragmented,GM::buildBudget(bytes))); // needs bytes+32 MiB contiguous
    }
    // Reduced reach: allocations stay well below the 96 MiB block requirement of the 64 MiB one.
    for(double mib:{1.0,8.0,16.0,24.0})assert(GM::admits(unfragmented,GM::buildBudget(uint64_t(mib*GM::MiB))));
    // Aggregate margin still applies to the reduced build (no margin was relaxed).
    assert(!GM::admits(observed,GM::buildBudget(24*GM::MiB)));
    assert(GM::buildBudget().available==768*GM::MiB&&GM::ContiguousMargin==32*GM::MiB&&GM::ProcessReserve==512*GM::MiB);

    // Fallback decisions.
    {State s;auto c=choose(s,Ext,1000);assert(c.reach==Ext&&!c.reduced&&!c.restored);
     assert(memoryRefused(s,c.reach,1000));                         // reduce on first refusal
     c=choose(s,Ext,1001);assert(c.reach==BaseReach&&c.reduced&&!c.restored);
     assert(!memoryRefused(s,c.reach,1001));                        // reduced build: normal retry path
     c=choose(s,Ext,1000+BackoffMs-1);assert(c.reduced&&c.reach==BaseReach); // backoff respected
     c=choose(s,Ext,1000+BackoffMs);assert(c.reach==Ext&&!c.reduced&&c.restored); // restored after it
     c=choose(s,Ext,1000+BackoffMs+1);assert(c.reach==Ext&&!c.restored);        // restored only once
     assert(memoryRefused(s,Ext,50000));                            // refused again: clock restarts
     assert(choose(s,Ext,50000+BackoffMs-1).reduced&&!choose(s,Ext,50000+BackoffMs).reduced);}
    {State s;assert(!memoryRefused(s,BaseReach,5));assert(!s.refused);       // not extended: no reduce
     auto c=choose(s,BaseReach,6);assert(c.reach==BaseReach&&!c.reduced&&!c.restored);
     assert(!memoryRefused(s,0,5));}
    {State s;auto c=choose(s,Ext,10);assert(c.reach==Ext&&!s.refused);}       // non-memory error never touches state
    {State s;memoryRefused(s,Ext,1);auto c=choose(s,BaseReach,2);assert(c.reach==BaseReach&&!c.reduced&&!c.restored); // other zone
     assert(!s.holding(2)==false);c=choose(s,BaseReach,1+BackoffMs);assert(!s.refused);
     c=choose(s,Ext,1+BackoffMs+1);assert(c.reach==Ext&&!c.restored);}
    {State s;const uint32_t t=0xFFFFFFF0u;assert(memoryRefused(s,Ext,t));    // DWORD tick wrap
     assert(choose(s,Ext,t+5).reduced);assert(choose(s,Ext,t+BackoffMs-1).reduced);
     auto c=choose(s,Ext,t+BackoffMs);assert(c.reach==Ext&&c.restored);}
    {State s;const uint32_t t=0xFFFFFFF0u;memoryRefused(s,Ext,t);
     assert(choose(s,Ext,uint32_t(t+0x20)).reduced);}                         // now wrapped past zero, still held
    // Episode.
    {Episode e;uint32_t ms=0;unsigned n=0;assert(!e.end(5,ms,n));
     assert(e.refused(100));assert(!e.refused(1100));assert(!e.refused(2100));e.markReduced();
     assert(e.end(0xFFFFFFFFu+1u+400u,ms,n)==true&&ms==uint32_t(400u-100u)&&n==3&&e.reduced);
     assert(!e.end(1,ms,n));assert(e.refused(9)&&!e.reduced);}
    std::cout<<"terrain reach fallback PASS\n";
}
