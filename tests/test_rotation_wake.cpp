// Production queue comparison; test-only sleep control is inserted by the runner.
#define NORTHLIGHT_STREAM_CHECK_WAIT 1
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#include "command_queue.h"
using namespace NorthlightStream;

static void put(Queue& q,std::uint64_t seq){std::memcpy(q.reserve(7,8),&seq,8);q.commit();}
static void take(Queue& q,const CommandHeader* h,std::uint64_t seq){
    assert(h&&h->id==7&&h->size==16);std::uint64_t value=0;std::memcpy(&value,h+1,8);assert(value==seq);q.retire(h);
}
static std::uint64_t cpuNs(){
    FILETIME a{},b{},kernel{},user{};assert(GetThreadTimes(GetCurrentThread(),&a,&b,&kernel,&user));
    return (((std::uint64_t(kernel.dwHighDateTime)<<32)|kernel.dwLowDateTime)+
            ((std::uint64_t(user.dwHighDateTime)<<32)|user.dwLowDateTime))*100;
}
static std::uint64_t cycles(){ULONG64 value=0;assert(QueryThreadCycleTime(GetCurrentThread(),&value));return value;}
template<class F> static void until(F&& predicate){
    const auto deadline=nowNs()+5000000000ull;
    while(!predicate()){assert(nowNs()<deadline);std::this_thread::yield();}
}
static void checks(){
    static_assert(BudgetBytes==(16u<<20)&&kMaxBudgetBytes==(32u<<20),"queue caps unchanged");
    for(unsigned n:{64u,128u,192u,256u}){
        Queue q;q.trackIdlePublications(true);q.rotationArmSleep();
        const auto clocks=rotationClockReads;
        for(unsigned i=0;i<n;++i)put(q,i);
        assert(rotationClockReads==clocks);
        const unsigned expected=NORTHLIGHT_ROTATION_FIRST_WAKE||n>=256?1:0;
        assert(q.wakeSignals()==expected);
        const auto diagnostics=q.wakePublicationDiagnostics();
        const auto held=NORTHLIGHT_ROTATION_FIRST_WAKE?0u:std::min(n/64,3u);
        assert(diagnostics.heldBatches==held&&diagnostics.heldCommands==held*64&&diagnostics.heldBytes==held*1024);
        assert(diagnostics.wakeSignals==expected);
        const auto point=q.idlePoint();assert(point.consistent&&point.recorded==n&&point.published==n&&!point.replayed);
        q.publish();q.publish();assert(q.wakeSignals()==1); // one signal while that sleeper has not run
        for(unsigned i=0;i<n;++i)take(q,q.next(false),i);
        assert(!q.next(false)&&q.depth()==0&&q.budget()==BudgetBytes);
        q.setPressure(true);assert(q.budget()==BudgetBytes/2);q.setPressure(false);
        q.rotationArmSleep();for(unsigned i=0;i<256;++i)put(q,n+i);
        assert(q.wakeSignals()==2);for(unsigned i=0;i<256;++i)take(q,q.next(false),n+i);
    }
    { // Busy replay has no sleeper to notify, even across automatic publications/chunks.
        Queue q;for(unsigned i=0;i<100000;++i)put(q,i);
        assert(!q.wakeSignals());q.publish();
        for(unsigned i=0;i<100000;++i)take(q,q.next(false),i);
        assert(!q.depth()&&!q.wakeSignals()&&q.budget()==BudgetBytes);
    }
    { // The byte-driven automatic publication uses the same sleeper policy.
        Queue q;q.trackIdlePublications(true);q.rotationArmSleep();std::uint64_t value=123;
        std::memcpy(q.reserve(7,kAutoPublishBytes-8),&value,8);q.commit();
        const auto diagnostics=q.wakePublicationDiagnostics();
        assert(diagnostics.heldBatches==(NORTHLIGHT_ROTATION_FIRST_WAKE?0:1));
        assert(diagnostics.heldCommands==(NORTHLIGHT_ROTATION_FIRST_WAKE?0:1));
        assert(diagnostics.heldBytes==(NORTHLIGHT_ROTATION_FIRST_WAKE?0:kAutoPublishBytes));
        q.publish();const auto* h=q.next(false);assert(h&&h->size==kAutoPublishBytes);q.retire(h);
        assert(!q.depth()&&q.wakeSignals()==1);
    }
    { // A release/publish racing the sleeper handshake cannot strand a sparse batch.
        Queue q;std::atomic<unsigned> ack{0};constexpr unsigned rounds=128;
        std::thread consumer([&]{for(unsigned r=0;r<rounds;++r){for(unsigned i=0;i<64;++i)take(q,q.next(true),r*64+i);ack.store(r+1);}});
        for(unsigned r=0;r<rounds;++r){for(unsigned i=0;i<64;++i)put(q,r*64+i);q.publish();until([&]{return ack.load()==r+1;});}
        consumer.join();assert(!q.depth()&&q.replayedSeq()==rounds*64);
    }
    { // Profiling off changes no counters and neither publication path reads a clock.
        Queue q;q.rotationArmSleep();const auto clocks=rotationClockReads;
        for(unsigned i=0;i<256;++i)put(q,i);q.publish();assert(rotationClockReads==clocks);
        const auto diagnostics=q.wakePublicationDiagnostics();
        assert(!diagnostics.heldBatches&&!diagnostics.heldCommands&&!diagnostics.heldBytes&&!diagnostics.wakeSignals);
        for(unsigned i=0;i<256;++i)take(q,q.next(false),i);
    }
    std::puts("CHECK_PASS sparse=64,128,192,256 order=exact signals=one-per-sleep caps=unchanged handshake=128");
}
static void sparse(unsigned n){
    Queue q;std::atomic<std::uint64_t> first{0},last{0};
    std::thread consumer([&]{for(unsigned i=0;i<n;++i){const auto* h=q.next(true);if(!i)first.store(nowNs());take(q,h,i);}last.store(nowNs());});
    until([&]{return get(q.stats.consumerSleeps)>0;});std::this_thread::sleep_for(std::chrono::milliseconds(3));
    const auto start=nowNs(),cpu=cpuNs(),cycle=cycles();for(unsigned i=0;i<n;++i)put(q,i);const auto recorded=nowNs();
    // Simulate independent producer work after the sparse publication; this is not a game FPS estimate.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));q.publish();consumer.join();
    const auto firstAt=first.load();
    std::printf("{\"case\":\"sparse%u\",\"wallNs\":%llu,\"firstNs\":%llu,\"recordNs\":%llu,\"producerCpuNs\":%llu,\"producerCycles\":%llu,\"wakes\":%u}\n",
        n,(unsigned long long)(last.load()-start),(unsigned long long)(firstAt-start),(unsigned long long)(recorded-start),
        (unsigned long long)(cpuNs()-cpu),(unsigned long long)(cycles()-cycle),q.wakeSignals());
}
static void throughput(const char* name,unsigned work,bool producerOnly){
    constexpr unsigned n=262144;Queue q(kMaxBudgetBytes);std::atomic<bool> go{false};
    std::uint64_t consumerCpu=0,consumerCycles=0,checksum=1;
    auto consume=[&]{until([&]{return go.load();});const auto cpu=cpuNs(),cycle=cycles();
        for(unsigned i=0;i<n;++i){take(q,q.next(true),i);for(unsigned k=0;k<work;++k){checksum^=checksum<<13;checksum^=checksum>>7;checksum^=checksum<<17;}}
        consumerCpu=cpuNs()-cpu;consumerCycles=cycles()-cycle;};
    std::thread consumer;if(!producerOnly)consumer=std::thread(consume);
    constexpr unsigned preload=4096;for(unsigned i=0;i<preload;++i)put(q,i);q.publish();
    const auto start=nowNs(),cpu=cpuNs(),cycle=cycles();go.store(true);
    for(unsigned i=preload;i<n;++i)put(q,i);q.publish();const auto produced=nowNs(),producerCpu=cpuNs()-cpu,producerCycles=cycles()-cycle;
    if(producerOnly){for(unsigned i=0;i<n;++i)take(q,q.next(false),i);}else consumer.join();
    assert(!q.depth());
    std::printf("{\"case\":\"%s\",\"wallNs\":%llu,\"producerWallNs\":%llu,\"producerCpuNs\":%llu,\"producerCycles\":%llu,\"consumerCpuNs\":%llu,\"consumerCycles\":%llu,\"wakes\":%u,\"sleeps\":%llu,\"reservedBytes\":%llu,\"checksum\":%llu}\n",
        name,(unsigned long long)(nowNs()-start),(unsigned long long)(produced-start),(unsigned long long)producerCpu,(unsigned long long)producerCycles,
        (unsigned long long)consumerCpu,(unsigned long long)consumerCycles,q.wakeSignals(),(unsigned long long)get(q.stats.consumerSleeps),
        (unsigned long long)q.reservedBytes(),(unsigned long long)checksum);
}
static void publications(){
    Queue q;std::uint64_t total=0,worst=0;constexpr unsigned batches=1024;
    for(unsigned r=0;r<batches;++r){for(unsigned i=0;i<63;++i)put(q,r*64+i);
        std::uint64_t value=r*64+63;std::memcpy(q.reserve(7,8),&value,8);
        const auto start=nowNs();q.commit();const auto elapsed=nowNs()-start;total+=elapsed;worst=std::max(worst,elapsed);}
    q.publish();for(unsigned i=0;i<batches*64;++i)take(q,q.next(false),i);
    std::printf("{\"case\":\"publication\",\"meanNs\":%llu,\"worstNs\":%llu,\"wakes\":%u}\n",(unsigned long long)(total/batches),(unsigned long long)worst,q.wakeSignals());
}
static void paced(){
    constexpr unsigned batches=1024,n=batches*64;Queue q;std::vector<std::uint64_t> sent(batches),latencies(batches);
    std::uint64_t consumerCpu=0,consumerCycles=0;
    std::thread consumer([&]{const auto cpu=cpuNs(),cycle=cycles();for(unsigned i=0;i<n;++i){
        const auto* h=q.next(true);if(i%64==0)latencies[i/64]=nowNs()-sent[i/64];take(q,h,i);}
        consumerCpu=cpuNs()-cpu;consumerCycles=cycles()-cycle;});
    until([&]{return get(q.stats.consumerSleeps)>0;});std::this_thread::sleep_for(std::chrono::milliseconds(3));
    const auto start=nowNs(),cpu=cpuNs(),cycle=cycles();
    for(unsigned batch=0;batch<batches;++batch){const auto began=nowNs();sent[batch]=began;
        for(unsigned i=0;i<64;++i)put(q,batch*64+i);
        while(nowNs()-began<100000)__builtin_ia32_pause();}
    q.publish();const auto producerCpu=cpuNs()-cpu,producerCycles=cycles()-cycle;consumer.join();
    std::sort(latencies.begin(),latencies.end());
    std::printf("{\"case\":\"paced100us\",\"wallNs\":%llu,\"producerCpuNs\":%llu,\"producerCycles\":%llu,\"consumerCpuNs\":%llu,\"consumerCycles\":%llu,\"latencyP50Ns\":%llu,\"latencyP95Ns\":%llu,\"latencyMaxNs\":%llu,\"wakes\":%u,\"sleeps\":%llu}\n",
        (unsigned long long)(nowNs()-start),(unsigned long long)producerCpu,(unsigned long long)producerCycles,(unsigned long long)consumerCpu,
        (unsigned long long)consumerCycles,(unsigned long long)latencies[batches/2],(unsigned long long)latencies[batches*95/100],
        (unsigned long long)latencies.back(),q.wakeSignals(),(unsigned long long)get(q.stats.consumerSleeps));
}
int main(int argc,char** argv){
    if(argc==1||!std::strcmp(argv[1],"checks")){checks();return 0;}
    if(!std::strcmp(argv[1],"sparse")){for(unsigned n:{64u,128u,192u,256u})sparse(n);return 0;}
    if(!std::strcmp(argv[1],"dense")){throughput("dense",0,false);return 0;}
    if(!std::strcmp(argv[1],"lagging")){throughput("lagging",64,false);return 0;}
    if(!std::strcmp(argv[1],"producer")){throughput("producer",0,true);return 0;}
    if(!std::strcmp(argv[1],"publication")){publications();return 0;}
    if(!std::strcmp(argv[1],"paced")){paced();return 0;}
    return 2;
}
