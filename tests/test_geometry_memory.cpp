#include <string>
#include "geometry_memory.h"
#include "world_streaming.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace NorthlightGeometryMemory;
int main(){
    static_assert(ProcessReserve==256*MiB&&EntryHeadroom==256*MiB);
    auto build=buildBudget();
    // Replay the original measured-shortfall scenario at the proposed reserve:
    // 515 entry + (384-362) shortfall = 537 MiB. Discarding that estimate
    // admits 516 MiB in a different region, while 492 MiB or a 95 MiB block
    // still fails. Every original admission/refusal assertion remains below.
    EntryLift oldRegion;
    oldRegion.refused(515*MiB,384*MiB,362*MiB,1000,96*MiB);
    assert(oldRegion.apply(buildBudget(64*MiB),1100).available==537*MiB);
    assert(!admits({516*MiB,332*MiB,true},oldRegion.apply(buildBudget(64*MiB),1100)));
    oldRegion.clear();
    assert(admits({516*MiB,332*MiB,true},oldRegion.apply(buildBudget(64*MiB),1101)));
    assert(!admits({492*MiB,332*MiB,true},oldRegion.apply(buildBudget(64*MiB),1101)));
    assert(!admits({516*MiB,95*MiB,true},oldRegion.apply(buildBudget(64*MiB),1101)));
    // Many small retargets must compare against the measured attempt's fixed
    // anchor, not just the previous request; camera/GI retargets inside its
    // coverage retain the anti-rebuild backoff.
    const NorthlightGI::Vec3 refusedCenter{0,0,0};
    oldRegion.refused(515*MiB,384*MiB,362*MiB,2000,96*MiB);
    for(int step=1;step<=13;++step){
        const NorthlightGI::Vec3 eye{float(step*8),0,0};
        if(!NorthlightWorldStreaming::applicable("Northrend",refusedCenter,"Northrend",eye))oldRegion.clear();
        assert(oldRegion.armed==(step<=12));
    }
    assert(admits({516*MiB,332*MiB,true},oldRegion.apply(buildBudget(64*MiB),2100)));
    assert(!NorthlightWorldStreaming::applicable("Northrend",refusedCenter,"UlduarRaid",refusedCenter));
    assert(build.available==512*MiB);
    assert(build.largest==32*MiB);
    assert(admits({512*MiB,32*MiB,true},build));
    assert(!admits({512*MiB,32*MiB-1,true},build));
    assert(!admits({512*MiB-1,512*MiB,true},build));
    assert(!admits({UINT64_MAX,UINT64_MAX,false},build));
    assert(!admits({UINT64_MAX,UINT64_MAX,true},{}));
    assert(!admits({512*MiB,32*MiB-1,true},buildBudget(32*MiB)));
    assert(!admits({512*MiB,64*MiB-1,true},buildBudget(32*MiB)));
    assert(admits({512*MiB,64*MiB,true},buildBudget(32*MiB)));
    assert(!buildBudget(UINT64_MAX).valid);
    // In-build budget: unchanged contiguous rule, 384 MiB (256 reserve + 128 working guard).
    assert(InBuildHeadroom==128*MiB&&ProcessReserve+InBuildHeadroom==384*MiB);
    auto inBuild=inBuildBudget();
    assert(inBuild.valid&&inBuild.available==384*MiB&&inBuild.largest==32*MiB);
    assert(admits({384*MiB,32*MiB,true},inBuild));
    assert(!admits({384*MiB-1,32*MiB,true},inBuild));
    assert(!admits({384*MiB,32*MiB-1,true},inBuild));
    assert(admits({384*MiB,64*MiB,true},inBuildBudget(32*MiB)));
    assert(!admits({384*MiB,64*MiB-1,true},inBuildBudget(32*MiB)));
    assert(inBuildBudget(32*MiB).largest==buildBudget(32*MiB).largest);
    assert(!inBuildBudget(UINT64_MAX).valid&&!inBuildBudget(UINT64_MAX-ContiguousMargin+1).valid);
    assert(!inBuildBudget(UINT64_MAX-ProcessReserve+1).valid&&inBuildBudget(UINT64_MAX-ProcessReserve).valid);
    // A request above the guard keeps the process reserve after the allocation: floor = reserve + request.
    assert(inBuildBudget(200*MiB).available==456*MiB&&inBuildBudget(200*MiB).largest==232*MiB);
    assert(admits({456*MiB,232*MiB,true},inBuildBudget(200*MiB)));
    assert(!admits({456*MiB-1,232*MiB,true},inBuildBudget(200*MiB)));
    assert(!admits({456*MiB,232*MiB-1,true},inBuildBudget(200*MiB)));
    assert(inBuildBudget(128*MiB).available==384*MiB&&inBuildBudget(129*MiB).available==385*MiB);
    // Entry and interior checks use separate headroom; the build does not pay entry headroom twice.
    assert(admits({622*MiB,477*MiB,true},buildBudget(64*MiB))&&!admits({511*MiB,477*MiB,true},buildBudget(1*MiB)));
    assert(admits({511*MiB,477*MiB,true},inBuildBudget(1*MiB)));
    assert(buildBudget().available==512*MiB&&buildBudget().largest==32*MiB);
    auto upload=uploadBudget(40*MiB,8*MiB,16*MiB,4*MiB);
    assert(upload.available==448*MiB&&upload.largest==96*MiB);
    assert(admits({448*MiB,96*MiB,true},upload));
    assert(!admits({448*MiB-1,96*MiB,true},upload));
    assert(!admits({448*MiB,96*MiB-1,true},upload));
    assert(uploadBudget(1,2,30,25).largest==50+16*MiB);
    assert(!uploadBudget(1,2,3,4).valid);
    assert(!uploadBudget(UINT64_MAX,1,0,0).valid);
    assert(!uploadBudget(UINT64_MAX/2,0,1,0).valid);
    assert(!uploadBudget((UINT64_MAX-ProcessReserve-64*MiB)/2+1,0,0,0).valid);
    const auto boundary=(UINT64_MAX-ProcessReserve-64*MiB)/2;
    assert(uploadBudget(boundary,0,0,0).valid&&uploadBudget(boundary,0,0,0).largest==2*boundary+ChunkMargin);
    assert(!uploadBudget(boundary+1,0,0,0).valid);
    auto dynamic=dynamicBudget(8*MiB);
    assert(dynamic.valid&&dynamic.largest==32*MiB&&dynamic.available==ProcessReserve+24*MiB);
    assert(admits({ProcessReserve+24*MiB,32*MiB,true},dynamic));
    assert(!admits({ProcessReserve+24*MiB,32*MiB-1,true},dynamic));
    assert(!dynamicBudget(UINT64_MAX/2).valid);
    const uint64_t pageCapacity=SmallPageCapacityLimit;
    assert(admitsSmallPageGrowth(true,ProcessReserve+3*pageCapacity,pageCapacity));
    assert(!admitsSmallPageGrowth(true,ProcessReserve+3*pageCapacity-1,pageCapacity));
    assert(admitsSmallPageGrowth(true,ProcessReserve+3,1));
    assert(!admitsSmallPageGrowth(true,ProcessReserve+2,1));
    assert(!admitsSmallPageGrowth(false,UINT64_MAX,pageCapacity));
    assert(!admitsSmallPageGrowth(true,UINT64_MAX,0));
    assert(!admitsSmallPageGrowth(true,UINT64_MAX,pageCapacity+1));
    assert(!admitsSmallPageGrowth(true,UINT64_MAX,UINT64_MAX));
    // The smaller admission deliberately makes no contiguous-block assertion.
    // Every actual allocation still has to succeed before its bytes are debited.
    Sample pageFrame{ProcessReserve+6*pageCapacity,0,true};
    assert(admitsSmallPageGrowth(pageFrame.valid,pageFrame.available,pageCapacity));
    debitGrowth(pageFrame,pageCapacity);
    assert(admitsSmallPageGrowth(pageFrame.valid,pageFrame.available,pageCapacity));
    debitGrowth(pageFrame,pageCapacity);
    assert(pageFrame.available==ProcessReserve&&pageFrame.largest==0);
    assert(!admitsSmallPageGrowth(pageFrame.valid,pageFrame.available,1));
    Sample transaction{ProcessReserve+96*MiB,128*MiB,true};
    assert(admits(transaction,dynamicBudget(32*MiB)));
    debitGrowth(transaction,32*MiB);
    assert(transaction.available==ProcessReserve&&transaction.largest==32*MiB&&transaction.valid);
    // The first allocation must reserve its memory before the second growth.
    assert(!admits(transaction,dynamicBudget(8*MiB)));
    transaction={2048*MiB,1024*MiB,true};
    debitGrowth(transaction,32*MiB);
    assert(admits(transaction,dynamicBudget(8*MiB)));
    debitGrowth(transaction,8*MiB);
    assert(transaction.available==1928*MiB&&transaction.largest==904*MiB);
    Sample small{3,2,true};debitGrowth(small,1);
    assert(small.available==0&&small.largest==0&&small.valid);
    Sample overflow{UINT64_MAX,UINT64_MAX,true};debitGrowth(overflow,UINT64_MAX/3+1);
    assert(!overflow.valid&&overflow.available==0&&overflow.largest==0);
    Sample invalid{100,100,false};debitGrowth(invalid,1);
    assert(!invalid.valid&&invalid.available==0&&invalid.largest==0);
    Sample unchanged{2048*MiB,1024*MiB,true};debitGrowth(unchanged,0);
    assert(unchanged.available==2048*MiB&&unchanged.largest==1024*MiB);
    assert(roundUp(1,8*MiB)==8*MiB&&roundUp(8*MiB,8*MiB)==8*MiB&&roundUp(8*MiB+1,8*MiB)==16*MiB&&roundUp(5,0)==5);
    Generations<int> generations;
    auto a=std::make_shared<int>(1),b=std::make_shared<int>(2),c=std::make_shared<int>(3);
    assert(generations.canAdmit()&&generations.track(a));
    auto extraOwner=a;
    assert(generations.track(extraOwner)&&generations.live()==1);
    assert(generations.track(b)&&!generations.canAdmit());
    assert(!generations.track(c)&&!generations.track({}));
    a.reset();assert(!generations.canAdmit());
    extraOwner.reset();assert(generations.canAdmit()&&generations.track(c));
    b.reset();c.reset();assert(generations.live()==0);
    struct Pair {int x=1,y=2;};
    auto pair=std::make_shared<Pair>();
    auto x=std::shared_ptr<int>(pair,&pair->x),y=std::shared_ptr<int>(pair,&pair->y);
    assert(generations.track(x)&&generations.track(y)&&generations.live()==1);
    std::weak_ptr<Pair> observer=pair;
    pair.reset();x.reset();y.reset();
    assert(observer.expired()&&generations.live()==0);
    // A committed GPU mesh keeps only identity and the tiny material metadata.
    // Simulate retaining GPU resources while CPU geometry A is superseded.
    struct Geometry {std::vector<float> alpha;explicit Geometry(float value):alpha{value}{}};
    Generations<Geometry> scenes;
    auto active=std::make_shared<Geometry>(.37f);
    assert(scenes.track(active));
    std::weak_ptr<Geometry> uploaded=active;
    std::vector<float> committedAlpha=active->alpha;
    bool gpuBuffersReady=true;
    auto replacement=std::make_shared<Geometry>(.62f);
    assert(scenes.track(replacement));
    active=replacement;
    assert(uploaded.expired());
    assert(gpuBuffersReady&&committedAlpha[0]==.37f);
    assert(scenes.live()==1); // Old GPU ownership did not keep old BVH alive.

    // Worker construction C must wait while render-active B and stale pending
    // upload A retain distinct generations. Cancel A to admit latest C.
    auto pending=std::make_shared<Geometry>(.12f);
    assert(scenes.track(pending)&&scenes.live()==2&&!scenes.canAdmit());
    auto latest=std::make_shared<Geometry>(.85f);
    assert(!scenes.track(latest));
    std::weak_ptr<Geometry> pendingObserver=pending;
    pending.reset();
    assert(pendingObserver.expired()&&scenes.canAdmit()&&scenes.track(latest));
    // Extra shared owners still count as one and must prevent premature expiry.
    auto workerOwner=latest;
    latest.reset();assert(scenes.live()==2&&!scenes.canAdmit());
    workerOwner.reset();assert(scenes.canAdmit());
    // Commit the replacement's own alpha metadata and identity together.
    committedAlpha=active->alpha;uploaded=active;
    assert(uploaded.lock()==active&&committedAlpha[0]==.62f);
    replacement.reset();active.reset();
    assert(uploaded.expired()&&scenes.live()==0&&committedAlpha[0]==.62f);
    {   // Log fields: the refusing limit is named; a non-exact largest prints as a lower bound.
        using namespace NorthlightGeometryMemory;
        Budget b;b.available=640*MiB;b.largest=96*MiB;b.valid=true;
        Sample s;s.available=639*MiB;s.largest=63*MiB;s.valid=true;
        auto why=refusal(s,b);assert(std::string(why.limit)=="available"&&why.need==640*MiB);
        s.available=900*MiB;why=refusal(s,b);assert(std::string(why.limit)=="largest"&&why.need==96*MiB);
        s.largest=96*MiB;why=refusal(s,b);assert(std::string(why.limit)=="none"&&why.need==0&&admits(s,b));
        Sample bad;assert(std::string(refusal(bad,b).limit)=="sample");
        char text[32];formatLargestMiB(text,sizeof text,63*MiB,false);assert(std::string(text)==">=63");
        formatLargestMiB(text,sizeof text,63*MiB,true);assert(std::string(text)=="63");
    }
    std::cout<<"Geometry generation and memory admission tests passed\n";
}
