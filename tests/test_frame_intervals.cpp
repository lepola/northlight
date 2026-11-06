#include "frame_intervals.h"
#include <cassert>
#include <cstdio>
int main(){
    NorthlightFrameIntervals::Window w;NorthlightFrameIntervals::Report r;
    assert(!w.sample(0,1000,r));
    for(int i=1;i<=120;++i)assert(w.sample(i*10,1000,r)==(i==120));
    assert(r.count==120&&r.meanMs==10&&r.p50Ms==10&&r.p95Ms==10&&r.maxMs==10);
    // A/B reset must discard the partial old-mode interval window.
    for(int i=1;i<=50;++i)assert(!w.sample(1200+i*10,1000,r));
    w.reset();assert(!w.sample(5000,1000,r));
    for(int i=1;i<=120;++i)assert(w.sample(5000+i*20,1000,r)==(i==120));
    assert(r.meanMs==20&&r.p95Ms==20);
    // Device/clock discontinuities never turn into huge unsigned intervals.
    assert(!w.sample(0,1000,r));assert(!w.sample(10,0,r));
    assert(!w.sample(0,1000,r));
    int64_t tick=0;
    for(int i=1;i<=120;++i){tick+=i;assert(w.sample(tick,1000,r)==(i==120));}
    assert(r.meanMs==60.5&&r.p50Ms==60.5&&r.p95Ms==114&&r.maxMs==120);
    // Brief OFF segments must be observable without mixing the next ON frame.
    assert(!w.sample(tick+2,1000,r));assert(!w.sample(tick+8,1000,r));
    assert(!w.sample(tick+18,1000,r));assert(w.takePartial(r));
    assert(r.count==3&&r.meanMs==6&&r.p50Ms==6&&r.p95Ms==10&&r.maxMs==10);
    assert(!w.takePartial(r)&&r.count==0);
    assert(!w.sample(100000,1000,r));assert(!w.sample(100007,1000,r));
    assert(w.takePartial(r)&&r.count==1&&r.meanMs==7&&r.p50Ms==7&&r.p95Ms==7);
    assert(!w.sample(200000,1000,r));
    for(int i=1;i<=120;++i)assert(w.sample(200000+i*4,1000,r)==(i==120));
    assert(r.count==120&&r.meanMs==4);assert(!w.takePartial(r));
    std::puts("PASS Present interval statistics, mode/reset isolation, clock discontinuity");
}
