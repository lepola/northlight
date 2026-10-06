// Native tests of src/stream/command_queue.h, stream_wait.h and the generated src/generated/command_stream.inl.
// Built by test_command_stream.py against a generated D3D9 stub (d3d9_stub.h) and generated cases (cs_generated.inc).
// Modes: no argument = everything; "threads" = only the threaded cases (the TSan build).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
#include "d3d9_stub.h"
#include "command_queue.h"
#include "command_stream.inl"
using namespace NorthlightStream;

#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"CHECK failed %s:%d: %s\n",__FILE__,__LINE__,#c);std::abort();}}while(0)

// ---- trace helpers shared by the generated formatters, fakes and cases ----
static std::vector<std::string> gTrace;
static std::atomic<unsigned> gSyncDelayMs{0};
template<class T> static std::string fv(T v){if constexpr(std::is_pointer<T>::value)return std::to_string((std::uintptr_t)v);else return std::to_string((double)v);}
static std::string fb(const void* p,std::size_t n){if(!p)return "null";std::string s="[";char b[4];for(std::size_t i=0;i<n;++i){std::snprintf(b,sizeof b,"%02x",((const unsigned char*)p)[i]);s+=b;}return s+"]";}
template<bool Inner,class T> static std::string fi(T* p){return p?std::to_string((std::uintptr_t)p-(Inner?0x10000u:0u)):"null";}
template<class T> static std::string fa(T* p){return p?std::to_string((std::uintptr_t)p):"null";}
template<class T> static std::string fo(T* p){return p?"out":"null";}
static const std::string dummy_unused;

template<class T> struct SelfOf{static T* proxy(){return nullptr;}static T* fake(){return nullptr;}};

// What the replay thread's translator does, reduced to arithmetic: proxy -> inner = +0x10000 (the receiver marker maps
// to its fake object), toProxy = +0x20000, so a wrong or missing translation shows in the trace.
struct TestTr {
    std::vector<int32_t> results;unsigned skips=0;std::set<std::uintptr_t> dead;
    IDirect3DDevice9* device();
    template<class T> T* inner(T* p){
        if(!p||dead.count((std::uintptr_t)p))return nullptr;
        if(p==SelfOf<T>::proxy())return SelfOf<T>::fake();
        return reinterpret_cast<T*>((std::uintptr_t)p+0x10000);
    }
    template<class T> T* toProxy(T* p){return p?reinterpret_cast<T*>((std::uintptr_t)p+0x20000):nullptr;}
    void result(Cmd,HRESULT hr){results.push_back(hr);}
    void skipped(Cmd){++skips;}
};
// The class hosting NORTHLIGHT_STREAM_*_METHODS in the real code provides these; here the minimum that records its use.
struct HostBase {
    Queue q;unsigned observed=0,answered=0;bool answerYes=false;
    Queue& streamQueue(){return q;}
    template<Cmd C,class... A> void observe(CmdTag<C>,A&&...){++observed;}
    template<Cmd C,class... A> bool answer(CmdTag<C>,A&&...){++answered;return answerYes;}
    template<Cmd C,class... A> typename MethodTraits<C>::Ret local(CmdTag<C>,A&&...){return typename MethodTraits<C>::Ret();}
    template<Cmd C,class... A> typename MethodTraits<C>::Ret syncCall(CmdTag<C> t,A... a){return runSync(q,t,a...);}
    template<Cmd C,class... A> typename MethodTraits<C>::Ret syncGet(CmdTag<C> t,A... a){return runSync(q,t,a...);}
};

// The consumer: executes generated commands against TestTr until a Stop command.
struct ReplayThread {
    Queue& q;TestTr& tr;std::thread t;
    ReplayThread(Queue& q_,TestTr& tr_):q(q_),tr(tr_),t([this]{loop();}){}
    void loop(){
        for(;;){
            const CommandHeader* h=q.next(true);
            if(!h)continue;
            if(h->id==(std::uint16_t)Cmd::Stop){q.retire(h);return;}
            if(h->id==(std::uint16_t)Cmd::Sync){
                SyncCall* sc;std::memcpy(&sc,h+1,sizeof sc);
                if(auto ms=gSyncDelayMs.load())std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                CHECK(executeSync(*sc,tr));
            }else CHECK(dispatchGenerated(h,tr));
            q.retire(h);
        }
    }
    void stop(){q.reserve((std::uint16_t)Cmd::Stop,0);q.commit();q.publish();t.join();}
};

#include "cs_generated.inc"
IDirect3DDevice9* TestTr::device(){return SelfOf<IDirect3DDevice9>::fake();}

// ---- queue unit tests (plain bytes) ----
static std::uint64_t mix(std::uint64_t x){x^=x<<13;x^=x>>7;x^=x<<17;return x;}
static void fillPayload(void* p,std::size_t n,std::uint64_t seq){auto* b=static_cast<unsigned char*>(p);std::memcpy(b,&seq,8);for(std::size_t i=8;i<n;++i)b[i]=(unsigned char)(seq*31+i);}
static bool checkPayload(const void* p,std::size_t n,std::uint64_t seq){auto* b=static_cast<const unsigned char*>(p);std::uint64_t s;std::memcpy(&s,b,8);if(s!=seq)return false;for(std::size_t i=8;i<n;++i)if(b[i]!=(unsigned char)(seq*31+i))return false;return true;}
static void record(Queue& q,std::uint16_t id,std::uint32_t n,std::uint64_t seq){void* p=q.reserve(id,n);CHECK(((std::uintptr_t)p&7)==0);fillPayload(p,n,seq);q.commit();}
static bool waitFor(const std::function<bool()>& c,int ms=10000){for(int i=0;i<ms;++i){if(c())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}return false;}

static void orderingAcrossChunks(){
    Queue q;std::vector<std::uint32_t> sizes;std::uint64_t r=88172645463325252ull;
    for(int i=0;i<3000;++i){r=mix(r);sizes.push_back(8+std::uint32_t(r%3000));}
    for(std::size_t i=0;i<sizes.size();++i){record(q,(std::uint16_t)(100+i%7),sizes[i],i);if(i%251==0)q.publish();}
    q.publish();
    CHECK(get(q.stats.chunkAllocs)>=4);   // ~4.5 MB recorded: the chain crossed several chunk boundaries
    for(std::size_t i=0;i<sizes.size();++i){
        const CommandHeader* h=q.next(false);CHECK(h);
        CHECK(h->id==100+i%7&&h->flags==0&&h->size==8+((sizes[i]+7u)&~7u));
        CHECK(checkPayload(h+1,sizes[i],i));
        for(std::size_t k=sizes[i];k<((sizes[i]+7u)&~7u);++k)CHECK(((const unsigned char*)(h+1))[k]==0);   // deterministic padding
        q.retire(h);CHECK(q.replayedSeq()==i+1);
    }
    CHECK(!q.next(false)&&q.recordedSeq()==sizes.size()&&q.depth()==0);
    CHECK(get(q.stats.commands)==sizes.size()&&get(q.stats.chunksLive)==1);   // consumed chunks went back to the pool
}
static void publishRules(){
    {Queue q;for(int i=0;i<10;++i)record(q,1,16,i);CHECK(!q.next(false));   // unpublished commands are invisible
     q.publish();for(int i=0;i<10;++i){auto* h=q.next(false);CHECK(h);q.retire(h);}CHECK(!q.next(false));}
    {Queue q;for(int i=0;i<63;++i)record(q,1,8,i);CHECK(!q.next(false));record(q,1,8,63);CHECK(q.next(false));}   // 64 commands auto-publish
    {Queue q;for(int i=0;i<7;++i)record(q,1,8184,i);CHECK(!q.next(false));record(q,1,8184,7);CHECK(q.next(false));}   // 64 KiB auto-publish
    {   // a chunk switch publishes the closed chunk; the command being reserved stays invisible until published
        Queue q;for(int i=0;i<3;++i)record(q,1,(std::uint32_t)MaxInlinePayload,i);
        for(int i=0;i<10;++i)record(q,2,16,i);
        for(int i=0;i<3;++i){auto* h=q.next(false);CHECK(h&&h->id==1);q.retire(h);}
        CHECK(!q.next(false));
        q.reserve(3,(std::uint32_t)MaxInlinePayload);   // does not fit the first chunk's remainder: closes it
        for(int i=0;i<10;++i){auto* h=q.next(false);CHECK(h&&h->id==2);q.retire(h);}
        CHECK(!q.next(false));q.commit();q.publish();{auto* h=q.next(false);CHECK(h&&h->id==3);q.retire(h);}
    }
    {   // the GetData rule: a spin on a result the replay thread produces ends because the spinner publishes each round
        Queue q;std::atomic<bool> done{false};
        std::thread consumer([&]{auto* h=q.next(true);CHECK(h);done.store(true);q.retire(h);});
        record(q,1,8,0);int rounds=0;
        while(!done.load()){q.publish();++rounds;std::this_thread::yield();}
        consumer.join();CHECK(rounds>=1);
    }
}
static void blocksAndInline(){
    Queue q;
    {void* p=q.reserve(5,(std::uint32_t)MaxInlinePayload);std::memset(p,0xAB,MaxInlinePayload);q.commit();q.publish();auto* h=q.next(false);CHECK(h&&h->size==8+MaxInlinePayload);q.retire(h);}
    {void* p=q.reserve(5,0);(void)p;q.commit();q.publish();auto* h=q.next(false);CHECK(h&&h->size==8);q.retire(h);}
    Block* b=q.tryAllocBlock(300*1024);CHECK(b&&b->capacity==512*1024&&((std::uintptr_t)b->data()&7)==0);
    CHECK(get(q.stats.blocksLive)==1&&get(q.stats.blockBytes)==512*1024);
    fillPayload(b->data(),300*1024,77);b->used=300*1024;
    auto* extra=static_cast<std::uint32_t*>(q.reserveWithBlock(6,8,b));extra[0]=0xC0FFEE;extra[1]=300*1024;q.commit();q.publish();
    auto* h=q.next(false);CHECK(h&&h->id==6&&(h->flags&kFlagBlock));
    CHECK(Queue::blockOf(h)==b&&checkPayload(Queue::blockOf(h)->data(),300*1024,77));
    CHECK(((const std::uint32_t*)Queue::payload(h))[0]==0xC0FFEE);
    q.retire(h);CHECK(get(q.stats.blocksLive)==0&&get(q.stats.blockBytes)==0);   // retire released the block
    Block* c=q.tryAllocBlock(400*1024);CHECK(c==b&&get(q.stats.blockReuses)==1&&get(q.stats.blockAllocs)==1);   // same class: pooled
    q.freeBlock(c);
    q.trim();Block* d=q.tryAllocBlock(400*1024);CHECK(get(q.stats.blockAllocs)==2);q.freeBlock(d);   // trim released the pool
    Block* tiny=q.tryAllocBlock(1);CHECK(tiny&&tiny->capacity==4096);q.freeBlock(tiny);
    CHECK(q.tryAllocBlock(std::size_t(1)<<31)==nullptr);
}
static void budgetRules(){
    {   // a request that alone exceeds the budget, and one that does not fit once drained: nullptr
        Queue q(2u<<20);
        CHECK(q.tryAllocBlock(3u<<20)==nullptr&&get(q.stats.blockRefused)==1);
        Block* a=q.tryAllocBlock(1u<<20);CHECK(a);   // 1 chunk + 1 MiB = budget
        CHECK(q.tryAllocBlock(1u<<20)==nullptr&&get(q.stats.blockRefused)==2);   // nothing queued to wait for: refused, not blocked
        q.freeBlock(a);CHECK(q.canAdmit(1u<<20));q.addShadowBytes(1u<<20);CHECK(!q.canAdmit(1));q.addShadowBytes(-(1<<20));
    }
    {   // memory pressure halves the budget
        Queue q(4u<<20);Block* a=q.tryAllocBlock(1500*1024);CHECK(a);q.freeBlock(a);
        q.setPressure(true);CHECK(q.budget()==2u<<20);CHECK(q.tryAllocBlock(1500*1024)==nullptr);q.setPressure(false);
        a=q.tryAllocBlock(1500*1024);CHECK(a);q.freeBlock(a);
    }
    {   // backpressure on chunks: the producer blocks at the budget until the consumer retires
        Queue q(3u<<20);std::atomic<bool> finished{false};const int N=5000;
        std::thread producer([&]{for(int i=0;i<N;++i)record(q,1,1000,i);q.publish();finished.store(true);});
        CHECK(waitFor([&]{return get(q.stats.backpressureWaits)>=1;}));
        std::this_thread::sleep_for(std::chrono::milliseconds(30));CHECK(!finished.load());   // still blocked: nobody consumes
        for(int i=0;i<N;++i){auto* h=q.next(true);CHECK(h&&checkPayload(h+1,1000,i));q.retire(h);}
        producer.join();CHECK(finished.load()&&q.recordedSeq()==N&&get(q.stats.highWaterBytes)<=3u<<20&&get(q.stats.backpressureNs)>0);
    }
    {   // backpressure on blocks
        Queue q(6u<<20);std::atomic<bool> finished{false};std::atomic<int> got{0};
        std::thread producer([&]{for(int i=0;i<20;++i){Block* b=q.tryAllocBlock(900*1024);CHECK(b);b->used=8;fillPayload(b->data(),8,i);q.reserveWithBlock(7,0,b);q.commit();got.fetch_add(1);}q.publish();finished.store(true);});
        CHECK(waitFor([&]{return get(q.stats.backpressureWaits)>=1;}));
        std::this_thread::sleep_for(std::chrono::milliseconds(30));CHECK(!finished.load()&&got.load()<20);
        for(int i=0;i<20;++i){auto* h=q.next(true);CHECK(h&&checkPayload(Queue::blockOf(h)->data(),8,i));q.retire(h);}
        producer.join();CHECK(get(q.stats.blocksLive)==0&&get(q.stats.blockBytes)==0&&get(q.stats.blockAllocs)<=6);
    }
}
static void reuseAfterWarmup(){
    Queue q;std::uint64_t chunks=0,blocks=0;
    for(int round=0;round<4;++round){
        for(int i=0;i<700;++i)record(q,1,4000,i);
        Block* b=q.tryAllocBlock(1u<<20);CHECK(b);q.reserveWithBlock(2,0,b);q.commit();q.publish();
        while(auto* h=q.next(false))q.retire(h);
        if(round==1){chunks=get(q.stats.chunkAllocs);blocks=get(q.stats.blockAllocs);CHECK(chunks>=3);}   // warm-up: the first two frames
        else if(round>1)CHECK(get(q.stats.chunkAllocs)==chunks&&get(q.stats.blockAllocs)==blocks);   // no growth after warm-up
    }
    CHECK(get(q.stats.chunksLive)==1&&get(q.stats.blocksLive)==0&&get(q.stats.blockReuses)>=3);
}
static void counters(){
    Queue q;record(q,1,8,0);record(q,1,24,1);q.publish();
    CHECK(get(q.stats.commands)==2&&get(q.stats.bytes)==16+32&&get(q.stats.publishes)==1&&get(q.stats.highWaterDepth)==2);
    char line[600];CHECK(formatCounters(line,sizeof line,q.stats)>0&&std::strstr(line,"cmds=2"));
    CHECK(std::string(cmdName(Cmd::Sync))=="Sync"&&std::string(cmdName(Cmd::Device_Clear))=="Device::Clear"&&std::string(cmdName(Cmd::Count))=="?");
}
static void interruptAndEvents(){
    {Queue q;std::atomic<int> state{0};
     std::thread t([&]{CHECK(q.next(true)==nullptr);state.store(1);q.clearInterrupt();auto* h=q.next(true);CHECK(h&&h->id==9);state.store(2);q.retire(h);});
     std::this_thread::sleep_for(std::chrono::milliseconds(20));CHECK(state.load()==0);q.interrupt();CHECK(waitFor([&]{return state.load()==1;}));
     record(q,9,8,0);q.publish();t.join();CHECK(state.load()==2&&get(q.stats.consumerSleeps)>=1);}
    {Event e;CHECK(!e.wait(1).signaled);e.set();CHECK(e.wait(1).signaled&&!e.wait(1).signaled);   // auto-reset
     Event m(true);m.set();CHECK(m.wait(1).signaled&&m.wait(1).signaled);m.reset();CHECK(!m.wait(1).signaled);}
    {static int hooks;static bool sawFlag;hooks=0;sawFlag=false;pumpHook=[]{++hooks;sawFlag=inPumpedWait;};
     Event e;CHECK(!inPumpedWait);auto r=e.waitPumped(15);CHECK(!r.signaled&&r.ns>=10000000&&hooks>=1&&sawFlag&&!inPumpedWait);pumpHook=nullptr;
     e.set();CHECK(e.waitPumped(1000).signaled);}
}
static void spscStress(){
    Queue q;const std::uint64_t N=300000;
    std::thread consumer([&]{
        for(std::uint64_t i=0;i<N;++i){
            auto* h=q.next(true);CHECK(h&&h->id==1);
            const std::uint32_t n=h->size-8;CHECK(n>=8&&checkPayload(h+1,n>16?16:n,i));
            if(n>16){std::uint32_t tail;std::memcpy(&tail,reinterpret_cast<const unsigned char*>(h+1)+n-4,4);CHECK(tail==(std::uint32_t)(i^n));}
            q.retire(h);
        }
    });
    std::uint64_t r=1234567;
    for(std::uint64_t i=0;i<N;++i){
        r=mix(r);std::uint32_t n=8+std::uint32_t(r%200);if(r%97==0)n=8+std::uint32_t(r%30000);n=(n+7)&~7u;
        auto* p=static_cast<unsigned char*>(q.reserve(1,n));fillPayload(p,n>16?16:n,i);if(n>16){const std::uint32_t tail=(std::uint32_t)(i^n);std::memcpy(p+n-4,&tail,4);}q.commit();
        if(i%997==0)q.publish();
        if(i%20011==0){const auto seq=q.recordedSeq();q.waitReplayed(seq);CHECK(q.replayedSeq()>=seq);}
    }
    q.publish();consumer.join();CHECK(q.replayedSeq()==N&&q.depth()==0);
}

// ---- generated code, game-facing methods through the macros ----
static void gameFacing(){
    ProxyDevice dev;TestTr tr;
    dev.SetRenderState((D3DRENDERSTATETYPE)7,9);CHECK(dev.observed==1);
    dev.SetCursorPosition(1,2,3);
    CHECK(!dev.q.next(false));   // plain records are not published yet
    dev.Clear(0,nullptr,1,2,3.0f,4);
    auto* h=dev.q.next(false);CHECK(h&&h->id==(std::uint16_t)Cmd::Device_SetRenderState);   // Clear published the lot
    gTrace.clear();
    while((h=dev.q.next(false))){CHECK(dispatchGenerated(h,tr));dev.q.retire(h);}
    CHECK(gTrace.size()==3&&gTrace[0]=="Device::SetRenderState 7.000000 9.000000 "&&gTrace[2].rfind("Device::Clear 0.000000 null ",0)==0);
    dev.BeginScene();CHECK(dev.q.next(false));h=dev.q.next(false);dev.q.retire(h);
    // get: answered locally, or a sync call
    IDirect3DSurface9* rt=nullptr;dev.answerYes=true;CHECK(dev.GetRenderTarget(0,&rt)==D3D_OK);CHECK(dev.answered==1&&!dev.q.next(false));
    dev.answerYes=false;
    {ReplayThread rp(dev.q,tr);gTrace.clear();
     CHECK(dev.GetRenderTarget(2,&rt)==5&&rt==(IDirect3DSurface9*)(uintptr_t)(0x5000+1+0x20000));
     DWORD passes=0;CHECK(dev.ValidateDevice(&passes)==5&&dev.TestCooperativeLevel()==5);
     D3DGAMMARAMP ramp;dev.GetGammaRamp(0,&ramp);   // a void sync call
     CHECK(gTrace.size()==4&&get(dev.q.stats.syncCalls)==4);
     rp.stop();}
    // receiver translation and the dead-proxy path
    Queue q2;TestTr t2;t2.dead.insert(0x1234);
    record_Texture_PreLoad(q2,(IDirect3DTexture9*)(uintptr_t)0x1234);record_Texture_PreLoad(q2,SelfOf<IDirect3DTexture9>::proxy());q2.publish();
    gTrace.clear();for(int i=0;i<2;++i){auto* x=q2.next(false);CHECK(x&&dispatchGenerated(x,t2));q2.retire(x);}
    CHECK(t2.skips==1&&gTrace.size()==1&&gTrace[0]=="Texture::PreLoad ");
    // an id the generated switch does not own (a hand-written command) is left to the caller
    {void* p=q2.reserve((std::uint16_t)Cmd::Present,0);(void)p;q2.commit();q2.publish();auto* x=q2.next(false);CHECK(x&&!dispatchGenerated(x,t2));q2.retire(x);}
    // an absurd count is dropped, not recorded
    {Queue q3;D3DRECT rects[1]={};record_Device_Clear(q3,100000,rects,0,0,0.f,0);CHECK(q3.recordedSeq()==0&&get(q3.stats.oversizeDrops)==1);}
}
static void nestedSync(){
    Queue q;
    inPumpedWait=true;CHECK(runSync(q,NORTHLIGHT_STREAM_TAG(Device_TestCooperativeLevel))==D3DERR_INVALIDCALL);inPumpedWait=false;
    CHECK(get(q.stats.nestedSyncs)==1&&q.recordedSeq()==0&&get(q.stats.syncCalls)==0);
    // a recorder entry made from the pump of a real sync wait never blocks on the replay thread
    TestTr tr;ReplayThread rp(q,tr);gSyncDelayMs=40;
    static HRESULT nested;static Queue* qq;qq=&q;nested=12345;
    pumpHook=[]{nested=runSync(*qq,NORTHLIGHT_STREAM_TAG(Device_TestCooperativeLevel));};
    CHECK(runSync(q,NORTHLIGHT_STREAM_TAG(Device_TestCooperativeLevel))==5);
    pumpHook=nullptr;gSyncDelayMs=0;CHECK(nested==D3DERR_INVALIDCALL&&get(q.stats.nestedSyncs)>=2);
    rp.stop();
}

int main(int argc,char** argv){
    const bool threadsOnly=argc>1&&std::string(argv[1])=="threads";
    spscStress();
    budgetRules();interruptAndEvents();publishRules();nestedSync();
    generatedSyncCases();
    if(!threadsOnly){
        orderingAcrossChunks();blocksAndInline();reuseAfterWarmup();counters();generatedRecordCases();gameFacing();
    }
    std::puts("test_command_stream: all passed");
    return 0;
}
