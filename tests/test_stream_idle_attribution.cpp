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
struct CpuApiFixture {
    static inline unsigned opens=0,reads=0,closes=0;static inline bool openOk=true,readOk=true;
    static const IdleProducerCpu::Api& api(){static const IdleProducerCpu::Api a{
        []()->void*{++opens;return openOk?reinterpret_cast<void*>(std::uintptr_t(1)):nullptr;},
        [](void* h,IdleCpuSample& p){CHECK(h==reinterpret_cast<void*>(std::uintptr_t(1)));++reads;p.kernelNs=reads*100;p.userNs=reads*200;return readOk;},
        [](void* h){CHECK(h==reinterpret_cast<void*>(std::uintptr_t(1)));++closes;}};return a;}
};
static void cpuLifetime(){
    using F=CpuApiFixture;F::opens=F::reads=F::closes=0;F::openOk=F::readOk=true;
    {IdleProducerCpu clock;clock.start(false,&F::api());CHECK(!clock.sample().known&&F::opens==0&&F::reads==0);
     F::openOk=false;clock.start(true,&F::api());CHECK(!clock.sample().known&&F::opens==1&&F::reads==0&&F::closes==0);
     F::openOk=true;clock.start(true,&F::api());const auto a=clock.sample();CHECK(a.known&&a.kernelNs==100&&a.userNs==200);
     F::readOk=false;CHECK(!clock.sample().known);F::readOk=true;const auto b=clock.sample();CHECK(b.known&&b.kernelNs==300);
     clock.start(true,&F::api());CHECK(F::closes==1);clock.reset();clock.reset();CHECK(F::closes==2&&!clock.sample().known);}
    CHECK(F::closes==2&&F::reads==3);
    IdleGapProfile profile;profile.enable(true);IdlePoint point;IdleCpuSample first,last;
    first.known=last.known=true;first.kernelNs=100;last.kernelNs=500;
    profile.wait(100,10000000,point,point,1,first);profile.wait(10000000,30000100,point,point,1,{});
    profile.finish(30000100,point,2,false,last);const auto gap=profile.take().worst[0];
    CHECK(gap.cpuFirst.known&&gap.cpuFirst.kernelNs==100&&gap.cpuLast.kernelNs==500&&gap.startNs==100&&gap.endNs==30000100);++groups;
}
static IdleImageLock imageRecord(std::uint64_t key=1,std::uint64_t entry=1){
    IdleImageLock p;p.key=key;p.entryNs=entry;p.caller=0x123456;p.w=64;p.h=32;p.d=1;p.fmt=22;p.flags=0x2000;p.pool=1;p.usage=0x200;p.level=2;p.face=3;p.route=2;p.frame=99;p.epoch=7;return p;
}
static void imageLifecycle(){
    IdleImageLocks locks;const auto p=imageRecord();CHECK(locks.begin(p)==0&&locks.select(1,40000000).lock.id==0);
    locks.enable(true);const auto id=locks.begin(p);CHECK(id);
    auto selected=locks.select(1,30000001);CHECK(selected.stable&&selected.lock.id==id&&selected.lock.mode==0&&selected.lock.returnedNs==0);
    locks.returned(id,1001,3);selected=locks.select(1,30000001);
    CHECK(selected.lock.mode==3&&selected.lock.caller==p.caller&&selected.lock.fmt==22&&selected.lock.w==64&&selected.lock.flags==p.flags&&selected.lock.epoch==7);
    locks.unlock(p.key,30000001);selected=locks.select(1001,30000001);
    CHECK(selected.stable&&selected.lock.id==id&&selected.lock.returnedNs==1001&&selected.lock.unlockNs==30000001&&selected.overlapNs==30000001u-1001u);
    CHECK(locks.select(1,30000001).overlapNs==30000000); // The full API+fill span is longer than the clipped fill span.
    CHECK(locks.select(30000001,60000001).lock.id==0); // A completed lock cannot explain a later gap.
    const auto failed=locks.begin(imageRecord(2,40000001));locks.returned(failed,70000001,0);
    selected=locks.select(40000001,70000001);CHECK(selected.lock.id==failed&&selected.lock.mode==4&&selected.lock.returnedNs==selected.lock.unlockNs);
    const auto retired=locks.begin(imageRecord(3,80000001));locks.returned(retired,80000101,1);locks.retire(3);
    selected=locks.select(80000001,110000001);CHECK(selected.lock.id==0&&!selected.stable);
    const auto reused=locks.begin(imageRecord(3,120000001));CHECK(reused!=retired);locks.returned(reused,120000101,2);
    selected=locks.select(120000001,150000001);CHECK(selected.stable&&selected.lock.id==reused&&selected.lock.mode==2);
    locks.enable(false);locks.enable(true);CHECK(locks.select(1,200000000).lock.id==0);++groups;
}
static void imageBounds(){
    IdleImageLocks locks;locks.enable(true);std::array<std::uint64_t,IdleImageLocks::Active> ids{};
    for(unsigned i=0;i<ids.size();++i){ids[i]=locks.begin(imageRecord(i+1));CHECK(ids[i]);}
    CHECK(locks.begin(imageRecord(99))==0&&locks.select(1,30000001).overflow==1);
    for(unsigned i=0;i<ids.size();++i){locks.returned(ids[i],1001,1);locks.unlock(i+1,30000001);}
    for(unsigned i=0;i<IdleImageLocks::Completed+2;++i){const auto key=100+i;const auto entry=std::uint64_t(i+2)*40000000;const auto id=locks.begin(imageRecord(key,entry));CHECK(id);locks.returned(id,entry+100,3);locks.unlock(key,entry+30000000);}
    const auto selected=locks.select(240000000,270000000);CHECK(selected.lock.key==104&&selected.overlapNs==30000000);
    CHECK(locks.select(1,30000001).lock.id==0); // Bounded history is not a completeness guarantee.
    ++groups;
}
static void imagePublicationRace(){
    IdleImageLocks locks;locks.enable(true);std::atomic<bool> done{false};constexpr unsigned Count=10000;
    std::thread producer([&]{for(unsigned i=1;i<=Count;++i){auto p=imageRecord(i,std::uint64_t(i)*40000000);p.w=i;p.caller=i;p.fmt=i^0x1234;
        const auto id=locks.begin(p);CHECK(id);locks.returned(id,p.entryNs+100,3);locks.unlock(i,p.entryNs+30000000);}done.store(true);});
    while(!done.load()){const auto s=locks.select(1,std::uint64_t(Count+1)*40000000);if(s.stable&&s.lock.id){CHECK(s.lock.w==s.lock.caller&&s.lock.fmt==(s.lock.w^0x1234));CHECK(s.lock.returnedNs==0||s.lock.returnedNs==s.lock.entryNs+100);}}
    producer.join();CHECK(locks.select(1,std::uint64_t(Count+1)*40000000).stable);++groups;
}
static void actualImageLocks(){
    for(bool profile:{false,true}){
        gTrace.clear();StreamDevice::Options options;options.diagnostics=&always;options.idleProfiling=profile?&always:&never;Rig rig(true,options);rig.sync();
        IDirect3DTexture9* texture=nullptr;CHECK(rig.dev->CreateTexture(64,32,1,0,(D3DFORMAT)22,(D3DPOOL)1,&texture,nullptr)==D3D_OK);rig.sync();
        auto* proxy=static_cast<StreamTexture*>(ProxyBase::of(texture));D3DLOCKED_RECT locked{};void* caller=reinterpret_cast<void*>(std::uintptr_t(0xABCDEF));
        const auto start=nowNs();CHECK(lockImage(*proxy,*proxy,proxy->subs[0],RouteTexture,0,0,64,32,1,&locked,nullptr,nullptr,nullptr,0,caller)==D3D_OK);
        std::memset(locked.pBits,0x57,std::size_t(locked.Pitch)*32);auto selected=rig.core().idleImages.select(start,nowNs());
        if(profile){CHECK(selected.stable&&selected.lock.id&&selected.lock.returnedNs>=selected.lock.entryNs&&selected.lock.caller==0xABCDEF&&selected.lock.w==64&&selected.lock.h==32);}
        else CHECK(!selected.lock.id);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));CHECK(texture->UnlockRect(0)==D3D_OK);rig.sync();
        selected=rig.core().idleImages.select(start,nowNs());if(profile)CHECK(selected.lock.id&&selected.lock.unlockNs>selected.lock.returnedNs);
        CHECK(texture->LockRect(0,&locked,nullptr,D3::kLockReadOnly)==D3D_OK);CHECK(static_cast<unsigned char*>(locked.pBits)[7]==0x57);CHECK(texture->UnlockRect(0)==D3D_OK);
        const auto invalid=nowNs();CHECK(texture->LockRect(0,nullptr,nullptr,0)==D3DERR_INVALIDCALL);CHECK(!rig.core().idleImages.select(invalid,nowNs()).lock.id);
        texture->Release();rig.sync();
        IDirect3DTexture9* abandoned=nullptr;CHECK(rig.dev->CreateTexture(8,8,1,0,(D3DFORMAT)22,(D3DPOOL)1,&abandoned,nullptr)==D3D_OK);rig.sync();
        const auto abandonedStart=nowNs();CHECK(abandoned->LockRect(0,&locked,nullptr,0)==D3D_OK);abandoned->Release();rig.sync();
        selected=rig.core().idleImages.select(abandonedStart,nowNs());if(profile)CHECK(!selected.lock.id&&!selected.stable);
        rig.finish();checkClean();
    }
    ++groups;
}
static unsigned callerFormats=0;
static const char* imageCaller(void* p){CHECK(reinterpret_cast<std::uintptr_t>(p)==0x123456);++callerFormats;return "fixture-module+0x123456";}
static void boundedProducerLog(){
    StreamCore core;std::vector<std::string> lines;std::function<void(const char*)> sink=[&](const char* line){lines.emplace_back(line);};
    core.idleLog=&sink;core.callerModule=&imageCaller;auto p=imageRecord();p.id=1;p.returnedNs=100;p.unlockNs=30000001;
    callerFormats=0;core.reportIdleImage(p);CHECK(lines.empty()&&callerFormats==0);
    core.idleImages.enable(true);for(unsigned i=0;i<7;++i)core.reportIdleImage(p);
    CHECK(lines.size()==4&&callerFormats==4);for(const auto& line:lines)CHECK(line.find("caller=fixture-module+0x123456")!=std::string::npos&&line.find("scope=observed-lock-interval-not-cpu-work")!=std::string::npos);
    core.frameNo=120;core.reportIdleImage(p);CHECK(lines.size()==5&&callerFormats==5);
    core.idleLog=nullptr;core.reportIdleImage(p);CHECK(lines.size()==5&&callerFormats==5);
    core.idleLog=&sink;core.callerModule=nullptr;core.reportIdleImage(p);CHECK(lines.size()==5&&callerFormats==5);++groups;
}
int main(){
    views();publicationABA();producerWaits();boundedTracker();concurrentPublication();
    replayerGap(false,false,false);replayerGap(true,false,false);replayerGap(true,true,false);replayerGap(true,false,true);
    cpuLifetime();imageLifecycle();imageBounds();imagePublicationRace();actualImageLocks();boundedProducerLog();
    std::printf("STREAM_IDLE_ATTRIBUTION_PASS groups=%u actualQueue=1 actualReplayer=1 noGame=1 noGPU=1\n",groups);
}
