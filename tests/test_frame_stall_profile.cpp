#include "frame_stall_profile.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace NorthlightFrameStalls;
int main(){
    Queries q;QuerySpan span;
    span.record(q,8,1,1,0,10);span.record(q,8,1,1,50,60);span.record(q,8,0,0,100,110);
    assert(q.calls==3&&q.flush==2&&q.pending==2&&q.events==3&&q.completed==1&&q.wallNs==30&&q.maxCallNs==10&&q.pendingNs==110&&!span.waiting);
    span.record(q,9,0,1,200,210);span.reset();span.record(q,9,0,0,300,320);
    assert(q.occlusion==2&&q.completed==1);
    span.record(q,8,1,1,400,410);span.record(q,8,1,-3,500,520);
    assert(q.errors==1&&!span.waiting&&q.completed==1);
    span.record(q,8,1,1,700,600);assert(!span.waiting&&q.errors==1);

    Tracker tracker;Frame f;int64_t done=1000000;unsigned long long logs=0;
    assert(!tracker.presented(f,done-600,done-500,done,1000000,0,logs));
    const int spans[]={25000,50000,30000,60000,40000};
    for(unsigned i=0;i<120;++i){
        const auto previous=done;done+=i<5?spans[i]:10000;
        f={};f.id=i;f.effectsMs=2;f.applied=true;f.focus=true;f.queries.events=i;f.sampled=i==3;
        const bool full=tracker.presented(f,done-600,done-500,done,1000000,previous+1000,logs);
        assert(full==(i==119));
    }
    auto report=tracker.take();assert(report.frames==120&&report.slow==5&&report.count==4);
    std::vector<double> times;
    for(unsigned i=0;i<report.count;++i){
        const auto& row=report.worst[i];times.push_back(row.totalMs);
        assert(row.phases&&std::fabs(row.sceneMs+row.effectsMs+row.tailMs+row.finishMs+row.presentMs-row.totalMs)<1e-9);
        assert(row.queries.events==row.id);
    }
    std::sort(times.begin(),times.end());assert((times==std::vector<double>{30,40,50,60}));
    assert(tracker.take().count==0);

    tracker.reset();done=1000000;tracker.presented({},done,done,done,1000000,0,0);
    for(unsigned i=0;i<120;++i){
        done+=25000;f={};f.id=i;f.effectsMs=30; // impossible effects span: report phases as unknown.
        if(i==4)++logs;
        tracker.presented(f,done-1000,done-500,done,1000000,done-2000,logs);
    }
    report=tracker.take();assert(report.slow==120&&report.count==4);
    for(const auto& row:report.worst)assert(!row.phases&&row.sceneMs==-1&&row.tailMs==-1);
    assert(!tracker.presented({},done,done-1,done,1000000,0,logs));
    assert(tracker.take().frames==0);
    assert(!tracker.presented({},0,0,0,1000000,0,logs)); // zero ticks prime correctly.
    tracker.presented({},25000,25000,25000,1000000,0,logs);
    assert(tracker.take().slow==1);
    assert(!tracker.presented({},25000,25000,25000,0,0,logs));
    assert(tracker.take().frames==0);
    tracker.reset();f={};f.effectsMs=1;
    tracker.presented({},0,0,0,1000000,0,0,true,0,1000000);
    tracker.presented(f,24000,24500,25000,1000000,20000,0,true,18000000,21000000);
    auto row=tracker.take().worst[0];assert(row.phases&&row.sceneIdleMs==17&&row.frameIdleMs==20);
    tracker.presented(f,49000,49500,50000,1000000,45000,0,true,100,200); // counter reset
    row=tracker.take().worst[0];assert(row.sceneIdleMs==-1&&row.frameIdleMs==-1);
    tracker.presented(f,74000,74500,75000,1000000,70000,0); // direct rendering has no queue clock
    row=tracker.take().worst[0];assert(row.sceneIdleMs==-1&&row.frameIdleMs==-1);
    tracker.presented(f,99000,99500,100000,1000000,95000,0,true,200,300);
    row=tracker.take().worst[0];assert(row.sceneIdleMs==-1&&row.frameIdleMs==-1); // prime after unknown
    tracker.presented(f,124000,124500,125000,1000000,120000,0,true,40000300,50000300);
    row=tracker.take().worst[0];assert(row.sceneIdleMs==-1&&row.frameIdleMs==-1); // impossible wait span
    tracker.reset();tracker.presented({},0,0,0,1000000,0,0,true,0,0);
    tracker.presented(f,24000,24500,25000,1000000,20000,0,true,20020000,25020000);
    row=tracker.take().worst[0];assert(row.sceneIdleMs==20&&row.frameIdleMs==25); // bound clock-read skew
    // Recorded material-off rotation frame 2457, 2026-10-23. Queue idle and
    // pending queries overlap the phase partition; adding them invents work.
    tracker.reset();tracker.presented({},1000000,1000000,1000000,1000000,0,0,true,0,0);
    f={};f.id=2457;f.effectsMs=.767;f.applied=f.focus=true;
    f.queries.pendingNs=2685000;f.queries.wallNs=10000;
    tracker.presented(f,1248084,1248298,1248312,1000000,1221665,0,true,217807000,242411000);
    row=tracker.take().worst[0];assert(row.id==2457&&row.phases);
    assert(std::fabs(row.totalMs-248.312)<1e-9&&std::fabs(row.sceneIdleMs-217.807)<1e-9);
    assert(std::fabs(row.frameIdleMs-242.411)<1e-9&&std::fabs(row.sceneMs-row.sceneIdleMs-3.858)<1e-9);
    assert(std::fabs(row.sceneMs+row.effectsMs+row.tailMs+row.finishMs+row.presentMs-row.totalMs)<1e-9);
    assert(row.queries.pendingNs==2685000&&row.queries.wallNs==10000);
    std::puts("PASS frame-stall profile: exact phase partition, top four of 120, query waits, queue-idle deltas, direct path and counter/clock resets; no GPU/game.");
}
