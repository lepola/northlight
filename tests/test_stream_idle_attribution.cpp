// Reuse the real queue/replayer, SDK stub and resource/lifetime controls.
#define main unused_stream_main
#include "test_command_stream.cpp"
#undef main
#include <mutex>

static unsigned groups=0;
static void command(Queue& q,unsigned value=7,std::uint16_t flags=0){
    auto* payload=static_cast<unsigned*>(q.reserve(7,sizeof(unsigned),flags));*payload=value;q.commit();
}
static void retireOne(Queue& q){const auto* h=q.next(false);CHECK(h);q.retire(h);}
static void views(){
    Queue q;command(q);q.publish();CHECK(!q.idlePoint().known);retireOne(q);
    q.trackIdlePublications(true);auto p=q.idlePoint();CHECK(p.known&&p.consistent&&std::string(p.queueState())=="empty");
    command(q);p=q.idlePoint();CHECK(p.recorded==2&&p.published==1&&p.replayed==1&&std::string(p.queueState())=="unpublished");
    CHECK(!q.next(false));q.publish();p=q.idlePoint();CHECK(p.recorded==2&&p.published==2&&p.replayed==1&&std::string(p.queueState())=="published");
    // Published commands are executable, never admitted as a new empty wait.
    CHECK(q.next(false));retireOne(q);CHECK(std::string(q.idlePoint().queueState())=="empty");
    p.known=false;CHECK(std::string(p.queueState())=="unknown");p.known=true;p.consistent=false;CHECK(std::string(p.queueState())=="unknown");
    p.consistent=true;p.published=p.recorded+1;CHECK(std::string(p.queueState())=="unknown");
    q.trackIdlePublications(false);CHECK(!q.idlePoint().known);++groups;
}
static void publicationABA(){
    Queue q;q.trackIdlePublications(true);command(q);const auto before=q.recordedSeq();
    q.testIdleSnapshotHook([](void* context){auto& queue=*static_cast<Queue*>(context);queue.publish();retireOne(queue);},&q);
    auto p=q.idlePoint();CHECK(q.recordedSeq()==before&&q.replayedSeq()==before&&!p.consistent&&std::string(p.queueState())=="unknown");
    q.testIdleSnapshotHook(nullptr,nullptr);p=q.idlePoint();CHECK(p.consistent&&std::string(p.queueState())=="empty");++groups;
}
static IdleGap gap(IdlePoint first,IdlePoint last){
    IdleGapProfile profile;profile.enable(true);profile.wait(1,30000001,first,last,7);profile.finish(30000001,last,8);
    auto report=profile.take();CHECK(report.gaps==1&&report.count==1);return report.worst[0];
}
static void producerWaits(){
    {Queue q;q.trackIdlePublications(true);command(q,7,kFlagWaitTarget);std::thread producer([&]{q.waitReplayed(1);});
     CHECK(waitFor([&]{return q.idlePoint().waitSeq==1;}));const auto first=q.idlePoint();retireOne(q);producer.join();const auto last=q.idlePoint();
     const auto report=gap(first,last);CHECK(report.waitsSeen&1);CHECK(last.waitSeq==0&&last.syncNs>=first.syncNs);CHECK(q.unflaggedWaits()==0);}
    {Queue q(2*ChunkBytes);q.trackIdlePublications(true);Block* firstBlock=q.tryAllocBlock(ChunkBytes);CHECK(firstBlock);
     q.reserveWithBlock(7,0,firstBlock);q.commit();std::atomic<bool> finished{false};
     std::thread producer([&]{Block* block=q.tryAllocBlock(4096);CHECK(block);q.freeBlock(block);finished.store(true);});
     CHECK(waitFor([&]{return q.idlePoint().backpressure;}));const auto first=q.idlePoint();CHECK(!finished.load());retireOne(q);producer.join();const auto last=q.idlePoint();
     CHECK(gap(first,last).waitsSeen&2);CHECK(!last.backpressure&&finished.load());}
    {Queue q;q.trackIdlePublications(true);std::atomic<unsigned> calls{0};std::thread producer([&]{q.runExpress([](void* p){static_cast<std::atomic<unsigned>*>(p)->fetch_add(1);},&calls);});
     CHECK(waitFor([&]{return q.idlePoint().express;}));const auto first=q.idlePoint();q.serveExpress(kExpressIdle);producer.join();const auto last=q.idlePoint();
     CHECK(gap(first,last).waitsSeen&4);CHECK(calls.load()==1&&!last.express&&q.recordedSeq()==0);}
    ++groups;
}
static void boundedTracker(){
    IdlePoint a;a.known=a.consistent=true;IdlePoint b=a;b.syncNs=100;b.recordNs=200;
    IdleGapProfile off;off.wait(0,100000000,a,b,1);off.finish(100000000,b,2);CHECK(!off.presented()&&off.take().count==0);
    IdleGapProfile p;p.enable(true);
    p.wait(0,10000000,a,b,1);p.wait(15000000,25000000,b,b,1);p.finish(30000000,b,2);
    auto r=p.take();CHECK(r.count==1&&r.worst[0].wallNs()==30000000&&r.worst[0].waitNs==20000000&&r.worst[0].segments==2);
    CHECK(IdleGapProfile::delta(20,10)==-1&&IdleGapProfile::delta(10,20)==10);
    CHECK(IdleGapProfile::delta(0,std::numeric_limits<std::uint64_t>::max())==-1);
    for(unsigned i=0;i<10;++i){p.wait(0,20000000+i*1000000,a,b,1);p.finish(20000000+i*1000000,b,2);}
    r=p.take();CHECK(r.gaps==10&&r.count==4);for(unsigned i=0;i<r.count;++i)CHECK(r.worst[i].wallNs()>=26000000);
    p.wait(0,100,a,b,1);p.finish(99,b,2);CHECK(p.take().count==0&&!p.open());
    p.wait(10,9,a,b,1);CHECK(!p.open());
    p.wait(0,30000000,a,b,1);p.enable(false);p.enable(true);p.finish(30000000,b,2);CHECK(p.take().count==0);
    a.contextKnown=b.contextKnown=true;a.epoch=4;b.epoch=5;b.cooperative=D3DERR_DEVICELOST;
    const auto changed=gap(a,b);CHECK(changed.first.epoch==4&&changed.last.epoch==5&&changed.last.cooperative==D3DERR_DEVICELOST);
    for(unsigned i=1;i<IdleGapProfile::WindowFrames;++i)CHECK(!p.presented());CHECK(p.presented());CHECK(p.take().frames==IdleGapProfile::WindowFrames);
    ++groups;
}
static bool always(){return true;}
static bool never(){return false;}
static std::vector<std::string> replayerGap(bool profile,bool lost,bool interrupt){
    std::mutex mutex;std::vector<std::string> logs;auto sink=[&](const char* line){std::lock_guard<std::mutex> lock(mutex);logs.emplace_back(line);};
    gTrace.clear();gKnobs.coop.store(D3D_OK);
    if(interrupt){
        StreamCore core;auto* target=new TargetDevice;core.target=target;Replayer replay(core);replay.diagnostics=&always;replay.idleProfiling=profile?&always:&never;replay.log=sink;
        CHECK(replay.start());CHECK(waitFor([&]{return get(core.q.stats.consumerSleeps)>0;}));std::this_thread::sleep_for(std::chrono::milliseconds(30));replay.join();target->Release();
    }else{
        StreamDevice::Options options;options.diagnostics=&always;options.idleProfiling=profile?&always:&never;options.log=sink;Rig rig(true,options);rig.sync();
        CHECK(rig.core().q.idlePoint().known==profile);
        if(lost){gKnobs.coop.store(D3DERR_DEVICELOST);rig.dev->Present(nullptr,nullptr,nullptr,nullptr);rig.sync();CHECK(rig.core().coopState.load()==D3DERR_DEVICELOST);}
        const auto sleeps=get(rig.core().q.stats.consumerSleeps);CHECK(waitFor([&]{return get(rig.core().q.stats.consumerSleeps)>sleeps;}));
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        if(!lost){const auto epoch=rig.core().queryEpoch.load();D3DPRESENT_PARAMETERS pp{};CHECK(rig.dev->Reset(&pp)==D3D_OK);CHECK(rig.core().queryEpoch.load()==epoch+1);}
        rig.finish();
    }
    gKnobs.coop.store(D3D_OK);checkClean();
    std::vector<std::string> gaps;for(const auto& line:logs)if(line.rfind("CSTREAM IDLE_GAP ",0)==0)gaps.push_back(line);
    if(profile){CHECK(!gaps.empty()&&gaps.size()<=4);for(const auto& line:gaps){CHECK(line.find("producerCpuMs=unknown applicationPhase=unknown")!=std::string::npos);CHECK(line.find("producerDeltaNonexclusiveNs[")!=std::string::npos);}}
    else CHECK(gaps.empty());
    if(interrupt&&profile)CHECK(std::any_of(gaps.begin(),gaps.end(),[](const std::string& line){return line.find("interrupted=1")!=std::string::npos;}));
    if(lost&&profile){char value[32];std::snprintf(value,sizeof value,"%08lx",(unsigned long)std::uint32_t(D3DERR_DEVICELOST));CHECK(std::any_of(gaps.begin(),gaps.end(),[&](const std::string& line){return line.find(value)!=std::string::npos;}));}
    ++groups;return gaps;
}
static void concurrentPublication(){
    Queue q;q.trackIdlePublications(true);constexpr unsigned Count=10000,Prefill=8000;std::atomic<bool> done{false},prefilled{false},consume{false};
    std::thread producer([&]{for(unsigned i=1;i<=Count;++i){auto* data=static_cast<unsigned*>(q.reserve(7,256));data[0]=i;data[63]=i^0xA5A5A5A5u;q.commit();if(i%257==0)q.publish();
        if(i==Prefill){prefilled.store(true);while(!consume.load())std::this_thread::yield();}}q.publish();done.store(true);});
    // Prove three physical chunks before recycling can make allocation count
    // depend on scheduling; the remaining commands still run concurrently.
    CHECK(waitFor([&]{return prefilled.load();}));CHECK(q.recordedSeq()==Prefill&&q.replayedSeq()==0&&get(q.stats.chunkAllocs)>=3);consume.store(true);
    unsigned consumed=0;std::uint64_t sum=0;
    while(consumed<Count){const auto point=q.idlePoint();if(point.consistent)CHECK(point.replayed<=point.published&&point.published<=point.recorded);
        if(const auto* h=q.next(false)){const auto* data=reinterpret_cast<const unsigned*>(h+1);CHECK(data[63]==(data[0]^0xA5A5A5A5u));sum+=data[0];q.retire(h);++consumed;}else std::this_thread::yield();}
    producer.join();CHECK(done.load()&&sum==std::uint64_t(Count)*(Count+1)/2&&q.idlePoint().consistent&&get(q.stats.chunkAllocs)>=3);++groups;
}
int main(){
    views();publicationABA();producerWaits();boundedTracker();concurrentPublication();
    replayerGap(false,false,false);replayerGap(true,false,false);replayerGap(true,true,false);replayerGap(true,false,true);
    std::printf("STREAM_IDLE_ATTRIBUTION_PASS groups=%u actualQueue=1 actualReplayer=1 noGame=1 noGPU=1\n",groups);
}
