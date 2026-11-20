// 0.3.175 (S3) effects buckets (effects_buckets.h): contiguous spans sum to the whole, an unreached
// mark rolls into the next bucket, draw calls are billed like time, and off reads no clock.
#include "effects_buckets.h"
#include <cassert>
#include <cstdio>
using namespace NorthlightEffectsBuckets;
static std::int64_t now=0;static unsigned calls=0;
static std::int64_t fake(){++calls;return now;}
int main(){
    std::uint32_t dip=0,dp=0;const std::uint32_t* counters[4]={&dip,&dp,nullptr,nullptr};
    Frame f;
    // Off: no clock read at all.
    calls=0;f.begin(false,&fake,counters,4);for(unsigned b=0;b<Count;++b)f.mark(Bucket(b));f.end();assert(calls==0&&!f.on());
    // On: every span billed to the mark that closes it; the tail after the last mark; draws likewise.
    now=1000;calls=0;f.begin(true,&fake,counters,4);
    now+=5;dip+=2;f.mark(Setup);
    now+=7;dip+=3700;f.mark(Celestial);
    now+=1;f.mark(AO);
    now+=20;dip+=90;dp+=4;f.mark(SunNear); /* Selection..TerrainSelection not reached: rolled into SunNear */
    now+=3;dip+=1;f.mark(Composite);
    now+=2;f.end();
    assert(f.ticks(Setup)==5&&f.ticks(Celestial)==7&&f.ticks(AO)==1&&f.ticks(Selection)==0&&f.ticks(SunNear)==20&&f.ticks(Composite)==3&&f.ticks(Tail)==2);
    std::int64_t sum=0;std::uint32_t draws=0;for(unsigned b=0;b<Count;++b){sum+=f.ticks(b);draws+=f.drawCalls(b);}
    assert(sum==f.total()&&f.total()==38&&draws==3797&&f.drawCalls(Celestial)==3700&&f.drawCalls(SunNear)==94);
    assert(calls==7&&f.reads()==7&&f.reads()<=MaxReads&&!f.on());
    const auto saved=f.snapshot(1000);
    assert(saved.valid&&saved.totalMs==38&&saved.reads==7&&saved.ms[SunNear]==20);
    assert(!f.snapshot(0).valid);
    f.begin(false,&fake);assert(!f.snapshot(1000).valid&&calls==7);
    f.begin(true,&fake);assert(!f.snapshot(1000).valid);f.end();
    assert(saved.totalMs==38&&saved.ms[SunNear]==20); // owns its original frame
    const auto readsBefore=calls;
    // A mark after end() is ignored (no read); begin() resets.
    f.mark(Veil);assert(calls==readsBefore);f.begin(true,&fake,counters,4);assert(f.ticks(SunNear)==0&&f.drawCalls(Celestial)==0);f.end();
    for(unsigned b=0;b<Count;++b)assert(name(b)[0]!='?');
    std::puts("PASS effects buckets: contiguous spans sum to the whole, unreached marks roll into the next bucket, draw calls billed alike, off reads no clock");
}
