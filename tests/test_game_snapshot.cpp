// 0.3.192 (CS): native test of the game-memory snapshot (snapshot_store.h, game_snapshot.h), the VS tag function
// (shader_tags.h) and the small stream hooks (stream_hooks.h). Host build, no Windows, no game: a byte-map fake
// memory stands in for the client and a counting live reader for ReadProcessMemory.
#include "game_snapshot.h"
#include "stream_hooks.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>
using namespace NorthlightStream;

// Fake client memory. Unmapped bytes fail the read, exactly like an invalid address.
struct Memory {
    std::map<std::uintptr_t,unsigned char> bytes;unsigned live=0;bool tearSky=false;
    void put(std::uintptr_t a,const void* p,std::size_t n){for(std::size_t i=0;i<n;++i)bytes[a+i]=static_cast<const unsigned char*>(p)[i];}
    void put32(std::uintptr_t a,std::uint32_t v){put(a,&v,4);}
    bool read(std::uintptr_t a,void* out,std::size_t n){
        ++live;
        for(std::size_t i=0;i<n;++i){auto it=bytes.find(a+i);if(it==bytes.end())return false;static_cast<unsigned char*>(out)[i]=it->second;}
        // A moving sky block: every read of the whole block leaves a different byte behind (a torn double read).
        if(tearSky&&a==0xd38b00&&n==0x388)++bytes[0xd38b00+4];
        return true;
    }
    void camera(){ // the pointer chain readCurrent / readCurrentBasis walk; the values only have to be readable
        put32(0xb7436c,0x1000000);put32(0x1000000+0x7e20,0x2000000);put32(0x2000000,0xa1ea54);
        const std::uint32_t zero[2]={0,0};put(0x2000000+0xa0,zero,8);
        float data[12];for(int i=0;i<12;++i)data[i]=float(i)+.5f;put(0x2000000+8,data,sizeof data);
        put32(0xc5df88,0x3000000);put32(0x3000000,0xa2e718);put32(0x3000000+0x1af8,0);
        float view[16];for(int i=0;i<16;++i)view[i]=float(i);put(0x3000000+0x1b00,view,sizeof view);
        unsigned char sky[0x388];for(unsigned i=0;i<sizeof sky;++i)sky[i]=(unsigned char)(i*7+1);put(0xd38b00,sky,sizeof sky);
    }
    void clobber(){for(auto& b:bytes)b.second=(unsigned char)(b.second^0x5a);} // the game moves on
};
struct Spy {std::uintptr_t address;std::size_t size;bool ok;std::uint64_t hash;
    bool operator==(const Spy& o)const{return address==o.address&&size==o.size&&ok==o.ok&&hash==o.hash;}};
static std::uint64_t fnv(const void* p,std::size_t n){return NorthlightShaderTags::fnv1a(p,n);}

static std::unique_ptr<GameSnapshot> fresh(){return std::make_unique<GameSnapshot>();}

static void testFifo() {
    auto s=fresh();std::uint32_t v;
    for(std::uint32_t i=1;i<=3;++i){v=i;assert(s->record(0x1000,&v,4,true));}
    v=9;assert(s->record(0x1000,&v,2,true)); // another size is another key
    assert(s->keys()==2&&s->copies()==4&&s->usable());
    s->rewind();
    for(std::uint32_t want:{1u,2u,3u,3u,3u}){std::uint32_t got=0;assert(s->serve(0x1000,&got,4)==GameSnapshot::Serve::Ok&&got==want);}
    std::uint32_t got=0;assert(s->serve(0x1004,&got,4)==GameSnapshot::Serve::Miss&&s->serve(0x1000,&got,8)==GameSnapshot::Serve::Miss);
    s->rewind();assert(s->serve(0x1000,&got,4)==GameSnapshot::Serve::Ok&&got==1); // rewind restarts the FIFO
    // a failed read replays as a failure, in its place in the FIFO
    s->clear();v=7;assert(s->record(0x2000,&v,4,true)&&s->record(0x2000,nullptr,4,false)&&s->record(0x2000,&v,4,true));
    s->rewind();assert(s->serve(0x2000,&got,4)==GameSnapshot::Serve::Ok&&s->serve(0x2000,&got,4)==GameSnapshot::Serve::Failed&&s->serve(0x2000,&got,4)==GameSnapshot::Serve::Ok);
    assert(!GameSnapshot().usable()); // nothing recorded is not usable
}
static void testOverflow() {
    auto s=fresh();unsigned char big[4096]={};
    unsigned n=0;while(s->record(0x1000+n*8,big,4096,true))++n;
    assert(n==GameSnapshot::ArenaBytes/4096&&s->overflowed()&&!s->usable());
    const auto before=SnapshotStats::overflow.load();assert(!s->record(0x1,big,1,true));assert(SnapshotStats::overflow.load()==before); // counted once per snapshot
    s->clear();assert(!s->overflowed()&&s->record(0x1,big,4,true)&&s->usable()); // clear() reuses the storage
    s->clear();for(unsigned i=0;i<GameSnapshot::MaxKeys;++i)assert(s->record(0x10000+i*16,big,4,true));
    assert(!s->record(0x90000,big,4,true)&&s->overflowed()); // key table
    s->clear();for(unsigned i=0;i<GameSnapshot::MaxCopies;++i)assert(s->record(0x500,big,4,true));
    assert(!s->record(0x500,big,4,true)&&s->overflowed()); // copy table
    s->clear();assert(!s->record(0x1,big,GameSnapshot::ArenaBytes+1,true)&&s->overflowed());
}
static void testPlayback() {
    Memory m;m.camera();m.tearSky=true;auto s=fresh();const CodeRange range;
    auto live=[&](std::uintptr_t a,void* o,std::size_t n){return m.read(a,o,n);};
    std::vector<Spy> captured,replayed;
    const auto run=[&](std::vector<Spy>& log){return [&log,&s,&live,&range](std::uintptr_t a,void* o,std::size_t n){
        const bool ok=snapshotRead(*s,a,o,n,live,range);log.push_back({a,n,ok,ok?fnv(o,n):0});return ok;};};
    { ScopedRecording rec(*s);assert(activeSnapshot==s.get()&&s->mode==GameSnapshot::Mode::Recording);captureReaders(run(captured)); }
    assert(activeSnapshot==nullptr&&s->mode==GameSnapshot::Mode::Idle&&s->usable());
    // the readers walked the whole chain, the double reads and the glare/body identities
    assert(captured.size()>=24); unsigned frameReads=0;for(auto& r:captured)frameReads+=r.address==0xb7436c;assert(frameReads==4); /* readCurrent before+after, readCurrentBasis before+after */
    unsigned skyReads=0;for(auto& r:captured)skyReads+=r.address==0xd38b00&&r.size==0x388;
    assert(skyReads==2);
    assert(s->has(0xb7436c,4)&&s->has(0xd38b00,0x388)&&s->has(0xd38b00,0x1b8)&&s->has(0xc5df88,4));
    // the torn pair is in the snapshot as two different copies
    {std::uint64_t h[2]={};unsigned i=0;for(auto& r:captured)if(r.address==0xd38b00&&r.size==0x388)h[i++]=r.hash;assert(h[0]!=h[1]);}
    // the game moves on; replay still sees the captured moment, in the same order, with zero live reads
    m.clobber();m.live=0;const auto hits0=SnapshotStats::hits.load(),misses0=SnapshotStats::misses.load();
    { ScopedPlayback play(*s);assert(activeSnapshot==s.get());captureReaders(run(replayed));assert(s->misses==0&&s->hits==captured.size()); }
    assert(activeSnapshot==nullptr&&m.live==0);
    assert(replayed==captured);
    assert(SnapshotStats::hits.load()-hits0==captured.size()&&SnapshotStats::misses.load()==misses0);
    // reading more often than the capture did: the last copy, never live
    { ScopedPlayback play(*s);std::uint32_t v=0;for(int i=0;i<6;++i)assert(snapshotRead(*s,0xb7436c,&v,4,live,range)&&v==0x1000000);assert(s->misses==0&&m.live==0); }
    // a miss reads live and counts
    { ScopedPlayback play(*s);std::uint32_t v=0;m.put32(0x5000000,0x1234);
      assert(snapshotRead(*s,0x5000000,&v,4,live,range)&&v==0x1234&&s->misses==1&&m.live==1&&s->hits==0); }
    assert(SnapshotStats::misses.load()==misses0+1);
    // a read the capture saw fail fails again, even though the byte exists now
    { auto t=fresh();Memory e;auto lv=[&](std::uintptr_t a,void* o,std::size_t n){return e.read(a,o,n);};std::uint32_t v=0;
      { ScopedRecording rec(*t);assert(!snapshotRead(*t,0x7000,&v,4,lv,range)); }
      e.put32(0x7000,5);{ ScopedPlayback play(*t);assert(!snapshotRead(*t,0x7000,&v,4,lv,range)&&t->hits==1); } }
}
static void testCodeRangeAndInactive() {
    Memory m;m.put32(0x401000,0xdeadbeef);m.put32(0x9ac790,0xcafef00d);m.put32(0xb7436c,1);const CodeRange range;auto s=fresh();
    auto live=[&](std::uintptr_t a,void* o,std::size_t n){return m.read(a,o,n);};std::uint32_t v=0;
    assert(range.contains(0x401000)&&range.contains(0x9ac790)&&!range.contains(0xa00000)&&!range.contains(0xb7436c)&&!range.contains(0x3fffff));
    { ScopedRecording rec(*s);assert(snapshotRead(*s,0x401000,&v,4,live,range)&&v==0xdeadbeef);assert(snapshotRead(*s,0xb7436c,&v,4,live,range));assert(s->keys()==1); } // code never recorded
    m.put32(0x401000,0x11111111);m.live=0;
    { ScopedPlayback play(*s);assert(snapshotRead(*s,0x401000,&v,4,live,range)&&v==0x11111111&&m.live==1&&s->hits==0&&s->misses==0); } // code is live, not a hit or a miss
    // Idle: straight to the live reader, nothing counted
    m.live=0;const auto h=SnapshotStats::hits.load(),ms=SnapshotStats::misses.load();
    assert(s->mode==GameSnapshot::Mode::Idle&&snapshotRead(*s,0xb7436c,&v,4,live,range)&&m.live==1&&SnapshotStats::hits.load()==h&&SnapshotStats::misses.load()==ms);
    assert(activeSnapshot==nullptr); // the direct path: readSelf's single null check
    // thread_local: another thread never sees this thread's active snapshot
    { ScopedPlayback play(*s);bool other=true;std::thread t([&]{other=activeSnapshot!=nullptr;});t.join();assert(!other&&activeSnapshot==s.get()); }
    // nesting restores the previous snapshot
    auto t=fresh();{ ScopedRecording a(*s);{ ScopedRecording b(*t);assert(activeSnapshot==t.get()); }assert(activeSnapshot==s.get()); }assert(activeSnapshot==nullptr);
}
static void testPeParse() {
    unsigned char img[0x400]={};auto p16=[&](std::size_t a,std::uint16_t v){std::memcpy(img+a,&v,2);};auto p32=[&](std::size_t a,std::uint32_t v){std::memcpy(img+a,&v,4);};
    p16(0,0x5a4d);p32(0x3c,0x80);p32(0x80,0x4550);p16(0x86,3);p16(0x94,0xe0);
    const std::size_t table=0x80+24+0xe0;
    auto section=[&](int i,std::uint32_t vsize,std::uint32_t rva,std::uint32_t flags){p32(table+i*40+8,vsize);p32(table+i*40+12,rva);p32(table+i*40+36,flags);};
    section(0,0x5a0000,0x1000,0x60000020); // .text
    section(1,0x20000,0x5a1000,0x40000040); // .rdata
    section(2,0x10000,0x5c1000,0xc0000040); // .data
    CodeRange r;assert(parseImageCode(img,sizeof img,0x400000,r)&&r.lo==0x400000&&r.hi==0x400000+0x1000+0x5a0000);
    CodeRange none;img[0]=0;assert(!parseImageCode(img,sizeof img,0x400000,none)&&none.lo==0x400000&&none.hi==0xA00000); // untouched on failure
    img[0]='M';assert(!parseImageCode(img,0x90,0x400000,none)&&!parseImageCode(nullptr,0,0,none));
    section(0,0x5a0000,0x1000,0x40000040);assert(!parseImageCode(img,sizeof img,0x400000,none)); // no executable section
}
static void testTriggers() {
    using NorthlightShaderTags::kTerrain;using NorthlightShaderTags::kUi;using NorthlightShaderTags::kWmo;using NorthlightShaderTags::kWorld;
    TriggerPolicy t;
    assert(t.onDraw(0,300,true)==Trigger::None&&t.onDraw(kWorld,300,false)==Trigger::None); // untagged world model draws are not the trigger
    assert(t.onDraw(kTerrain|kWorld,500,false)==Trigger::World&&t.onDraw(kTerrain,500,false)==Trigger::None&&t.onDraw(kWmo,50,false)==Trigger::None); // first terrain/WMO only
    for(unsigned i=0;i<TriggerPolicy::MaxSky;++i)assert(t.onDraw(0,2,true)==Trigger::Sky);
    assert(t.onDraw(0,2,true)==Trigger::None); // capped at 8
    assert(t.onDraw(0,5,true)==Trigger::None&&t.onDraw(0,2,false)==Trigger::None&&t.onDraw(0,4,true)==Trigger::None); // count<=4 and full viewport only (cap already hit)
    assert(t.onDraw(kUi,2,true)==Trigger::Ui&&t.onDraw(kUi,2,true)==Trigger::None&&t.onDraw(kUi,500,false)==Trigger::None); // first UI draw; later UI quads are never sky
    assert(t.onPresent()==Trigger::Present);
    // a new frame starts clean
    assert(t.onDraw(kWmo|kWorld,10,false)==Trigger::World&&t.onDraw(0,4,true)==Trigger::Sky&&t.onDraw(kUi,3,true)==Trigger::Ui);
    t.beginFrame();assert(t.sky==0&&!t.world&&!t.ui);
    // a world draw that is also a full-viewport quad takes the world trigger once, then counts as sky
    assert(t.onDraw(kTerrain,2,true)==Trigger::World&&t.onDraw(kTerrain,2,true)==Trigger::Sky);
}
// Reference copy of the expression Device::CreateVertexShader computed inline before 0.3.192.
static int oldTag(std::uint64_t h){
    for(auto x:kTerrainVS)if(x==h)return 1;
    for(auto x:kUiVS)if(x==h)return 2;
    return 0;
}
static void testShaderTags() {
    using namespace NorthlightShaderTags;
    std::vector<std::uint64_t> all;
    for(auto x:kTerrainVS)all.push_back(x);for(auto x:kUiVS)all.push_back(x);
    for(auto& x:kWorldShaderSignatures)all.push_back(x.hash);for(auto& x:kWmoShaderSignatures)all.push_back(x.hash);
    std::mt19937_64 rng(7);for(int i=0;i<20000;++i)all.push_back(rng());
    unsigned terrain=0,ui=0,world=0,wmo=0;
    for(auto h:all){
        assert(deviceTag(h)==oldTag(h)); // the Device's tag, unchanged
        bool inWorld=false;for(auto& x:kWorldShaderSignatures)inWorld|=x.hash==h;
        assert(inWorldSignatures(h)==inWorld);
        assert(inWmoSignatures(h)==(NorthlightWmoContext::signature(h)!=nullptr));
        const unsigned tags=triggerTags(h);
        assert((tags&3u)==unsigned(oldTag(h))&&bool(tags&kWorld)==inWorld&&bool(tags&kWmo)==inWmoSignatures(h)&&!(tags&~(3u|kWorld|kWmo)));
        terrain+=oldTag(h)==1;ui+=oldTag(h)==2;world+=inWorld;wmo+=inWmoSignatures(h);
    }
    assert(terrain>0&&ui>0&&world>0&&wmo>0);
    for(auto x:kTerrainVS)assert(deviceTag(x)==kTerrain);
    // FNV-1a: the algorithm renderer.cpp hashed with, over exactly `size` bytes
    assert(fnv1a("",0)==14695981039346656037ULL&&fnv1a("a",1)==0xaf63dc4c8601ec8cULL&&fnv1a("foobar",6)==0x85944171f73967e8ULL);
    unsigned char buf[300];for(unsigned i=0;i<sizeof buf;++i)buf[i]=(unsigned char)rng();
    for(std::size_t n:{1u,2u,3u,4u,63u,64u,299u,300u}){std::uint64_t h=14695981039346656037ULL;for(std::size_t i=0;i<n;++i)h=(h^buf[i])*1099511628211ULL;
        assert(fnv1a(buf,n)==h&&triggerTagsOfBytecode(buf,n)==triggerTags(h));}
}
static void testPoolAndHooks() {
    SnapshotPool pool(3);auto* a=pool.acquire();auto* b=pool.acquire();auto* c=pool.acquire();
    assert(a&&b&&c&&a!=b&&pool.acquire()==nullptr&&pool.allocated()==3);
    std::uint32_t v=1;a->record(1,&v,4,true);pool.release(a);auto* again=pool.acquire();
    assert(again==a&&again->copies()==0&&pool.allocated()==3); // reused and cleared, no new allocation
    pool.release(b);pool.release(c);pool.release(again);pool.release(nullptr);assert(pool.idle()==3&&pool.allocated()==3);
    for(int i=0;i<100;++i){auto* s=pool.acquire();assert(s);pool.release(s);}assert(pool.allocated()==3);
    // hooks inert by default
    assert(upIdentity==nullptr&&innerOf==nullptr&&!streamActive.load()&&gameTid.load()==0&&replayTid.load()==0);
    int x=0;assert(inner(&x)==&x&&inner(nullptr)==nullptr);
    innerOf=[](const void* p){return p?static_cast<const void*>(static_cast<const char*>(p)+1):p;};
    assert(inner(&x)==reinterpret_cast<const char*>(&x)+1&&inner(nullptr)==nullptr);innerOf=nullptr;
    const unsigned hw=std::thread::hardware_concurrency();
    assert(cores()==hw);streamActive=true;assert(cores()==(hw>1?hw-1:hw));streamActive=false;assert(cores()==hw);
    upIdentity=&x;bool other=true;std::thread t([&]{other=upIdentity!=nullptr;});t.join();assert(!other&&upIdentity==&x);upIdentity=nullptr;
    // CommandStream from the ini text: the file value, the default when absent, 0 on anything not parsed as 1
    assert(commandStreamFromText("",false)&&commandStreamFromText("[Quality]\nPreset=Quality\n")&&commandStreamFromText("[Quality]\nCommandStream=1\n"));
    assert(!commandStreamFromText("[Quality]\nCommandStream=0\n")&&!commandStreamFromText("\xEF\xBB\xBF[Quality]\r\nCommandStream = 0\r\n")&&!commandStreamFromText("[Quality]\nPreset=Performance\nCommandStream=0\n"));
    assert(commandStreamFromText("[Quality]\nCommandStream=2\n")); // out of range: the default, with a warning in the real loader
}
int main(){
    testFifo();testOverflow();testPlayback();testCodeRangeAndInactive();testPeParse();testTriggers();testShaderTags();testPoolAndHooks();
    std::printf("game snapshot: FIFO record/playback incl. torn double reads, miss->live, failed reads, code-range bypass, inactive passthrough, thread-local, overflow, PE code range, trigger policy, VS tags == pre-0.3.192 tags, pool, hooks\n");
}
