// Native host test for the DXVK 3.1.1 lock-flag rules our uploads rely on (0.3.192). No game, Wine or GPU.
// A fake buffer models LockBuffer of DXVK 3.1.1 (d3d9_device.cpp):
//   :5493  READONLY is stripped for every non-MANAGED pool;
//   NOOVERWRITE and DISCARD are stripped for every non-DEFAULT pool;
//   :5564  skipWait = (!needsReadback && (readOnly || !direct)) || noOverwrite, with direct = DEFAULT|DYNAMIC;
//   :5526  a DISCARD of a direct buffer adds the FULL buffer size to the discard counter (allocation throttle).
// Fake event queries complete a controllable number of frames after they are issued, and a byte-level buffer
// model (a DISCARD renames the buffer: in-flight draws keep the old slice) checks the fence-checked rings of
// dynamic_ring.h against races and against the expected flag decision.
#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <d3d9.h>
#include "upload_lock.h"
#include "dynamic_ring.h"
#include "backend_policy.h"
namespace Ring=NorthlightDynamicRing;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"CHECK failed %s:%d: %s\n",__FILE__,__LINE__,#c);std::abort();}}while(0)
constexpr std::uint64_t KiB=1024,MiB=1024*1024;

// ---- the DXVK 3.1.1 LockBuffer rules -------------------------------------------------------------------------
enum Pool {Default,Managed};
constexpr DWORD UsageDynamic=0x200;
struct LockOutcome {bool waited=false;DWORD effective=0;};
struct Dxvk311Buffer {
    Pool pool=Default;DWORD usage=0;UINT size=0;bool needsReadback=false;std::uint64_t discardCounter=0;
    bool direct()const{return pool==Default&&(usage&UsageDynamic);}
    LockOutcome lock(DWORD flags){
        if(pool!=Managed)flags&=~DWORD(D3DLOCK_READONLY);
        if(pool!=Default)flags&=~DWORD(D3DLOCK_NOOVERWRITE|D3DLOCK_DISCARD);
        const bool readOnly=flags&D3DLOCK_READONLY,noOverwrite=flags&D3DLOCK_NOOVERWRITE,discard=flags&D3DLOCK_DISCARD;
        const bool skipWait=(!needsReadback&&(readOnly||!direct()))||noOverwrite;
        if(discard&&direct())discardCounter+=size;
        LockOutcome out;out.effective=flags;out.waited=!skipWait&&!discard; // a DISCARD renames the slice and never waits
        return out;
    }
};

// ---- (a) the read-back flag ------------------------------------------------------------------------------------
static void readBackFlags(){
    struct Kind {const char* name;Pool pool;DWORD usage;};
    const Kind kinds[]={{"DEFAULT|DYNAMIC",Default,UsageDynamic},{"DEFAULT static",Default,0},{"MANAGED",Managed,0},{"MANAGED|DYNAMIC",Managed,UsageDynamic}};
    // Gate on (a loaded DXVK >= 3): READONLY|NOOVERWRITE; no read-back of any buffer class waits and none is charged.
    NorthlightUpload::ReadBackNoOverwrite.store(true);
    CHECK(NorthlightUpload::readBackLock()==(DWORD(D3DLOCK_READONLY)|DWORD(D3DLOCK_NOOVERWRITE)));
    for(const auto& k:kinds){Dxvk311Buffer b;b.pool=k.pool;b.usage=k.usage;b.size=4*MiB;
        const auto r=b.lock(NorthlightUpload::readBackLock());CHECK(!r.waited&&!b.discardCounter);
        if(k.pool==Default&&(k.usage&UsageDynamic))CHECK(r.effective==DWORD(D3DLOCK_NOOVERWRITE)); // READONLY is stripped, NOOVERWRITE is what skips the wait
    }
    // Gate off (2.7.1, 1.10.3, native, fallback): plain READONLY. On the 3.1.1 rules a DYNAMIC DEFAULT buffer then waits (READONLY is
    // stripped and nothing else skips the wait): the reason the NOOVERWRITE flag exists; every other class keeps skipping the wait.
    NorthlightUpload::ReadBackNoOverwrite.store(false);
    CHECK(NorthlightUpload::readBackLock()==DWORD(D3DLOCK_READONLY));
    for(const auto& k:kinds){Dxvk311Buffer b;b.pool=k.pool;b.usage=k.usage;b.size=4*MiB;
        const auto r=b.lock(NorthlightUpload::readBackLock());CHECK(!b.discardCounter);
        CHECK(r.waited==(k.pool==Default&&(k.usage&UsageDynamic)));
    }
    // The caveat that keeps the flag off elsewhere: NOOVERWRITE would also skip the needsReadback wait of a buffer a ProcessVertices wrote.
    for(bool gate:{false,true}){NorthlightUpload::ReadBackNoOverwrite.store(gate);
        Dxvk311Buffer b;b.pool=Default;b.usage=UsageDynamic;b.size=MiB;b.needsReadback=true;CHECK(b.lock(NorthlightUpload::readBackLock()).waited==!gate);}
    NorthlightUpload::ReadBackNoOverwrite.store(false);
    std::puts("PASS read-back lock flag: gate on = no wait on DEFAULT|DYNAMIC, gate off = plain READONLY (waits on 3.1.1), no class charged");
}

// ---- (c) the version parse -----------------------------------------------------------------------------------
static void majors(){
    using NorthlightBackend::dxvkMajor;
    CHECK(dxvkMajor("v3.1.1")==3&&dxvkMajor("3.1.1")==3&&dxvkMajor("V3.0")==3&&dxvkMajor("2.7.1")==2&&dxvkMajor("v2.7.1")==2&&dxvkMajor("1.10.3")==1&&dxvkMajor("10.0")==10);
    CHECK(dxvkMajor("")==0&&dxvkMajor("garbage")==0&&dxvkMajor("v")==0&&dxvkMajor("  ")==0&&dxvkMajor("DXVK 3.1.1")==0&&dxvkMajor("99999.1")==0);
    std::puts("PASS dxvkMajor: v3.1.1/3.1.1 -> 3, 2.7.1 -> 2, 1.10.3 -> 1, empty/garbage -> 0");
}

// ---- (b) the rings against a byte-level buffer model -----------------------------------------------------------
struct Slice {std::vector<std::uint8_t> bytes;explicit Slice(UINT n):bytes(n){}};
static inline std::uint64_t word(std::uint64_t seed,std::size_t k){return seed*0x9E3779B97F4A7C15ull+(k+1)*0xD1B54A32D192ED03ull;}
struct Draw {std::shared_ptr<Slice> slice;UINT offset=0,bytes=0;std::uint64_t seed=0;long doneFrame=LONG_MAX;};
static void fill(Slice& s,UINT offset,UINT bytes,std::uint64_t seed){for(std::size_t k=0;k<bytes/8;++k){const std::uint64_t v=word(seed,k);std::memcpy(&s.bytes[offset+8*k],&v,8);}}
static bool readsBack(const Draw& d){ // byte-exact for small regions; head, tail and a stride through the middle of big ones
    const std::size_t words=d.bytes/8,stride=d.bytes<=MiB?1:251;
    auto same=[&](std::size_t k){std::uint64_t v;std::memcpy(&v,&d.slice->bytes[d.offset+8*k],8);return v==word(d.seed,k);};
    for(std::size_t k=0;k<words;k+=(k<512||k+512>=words)?1:stride)if(!same(k))return false;
    return true;
}
struct Sim {long frame=0;std::mt19937 rng{20260};std::mt19937 latencyRng{7};int fixedLatency=-1;bool failQueries=false;long liveQueries=0;} sim;
struct FakeQuery final:IDirect3DQuery9 {
    long doneFrame=0;
    HRESULT Issue(DWORD)override{return S_OK;}HRESULT GetData(void*,DWORD,DWORD)override{return S_OK;}unsigned long Release()override{return 0;}
};
struct Stream {
    const char* name="";UINT align=16;Ring::Ring ring;UINT capacity=0;std::shared_ptr<Slice> slice;Dxvk311Buffer dev;
    std::vector<std::shared_ptr<Draw>> draws,flight; // draws on the CURRENT slice (race model); every draw not yet verified-complete
    std::shared_ptr<Draw> open;                      // the previous call's draw: the next place() issues its query
    unsigned discards=0,fenceReuse=0,wraps=0,shrinks=0,grows=0,creates=0,calls=0;std::uint64_t discardBytes=0,frameCharge=0,maxFrameCharge=0;
};
struct Hooks { // the ring's query hooks: the fake event completes `latency` frames after it was issued; the draw it follows completes with it
    Stream* st;
    IDirect3DQuery9* issue(){
        if(sim.failQueries)return nullptr; // CreateQuery/Issue failed: the span stays pending until a DISCARD
        const long latency=sim.fixedLatency>=0?sim.fixedLatency:long(sim.latencyRng()%4);
        if(st->open)st->open->doneFrame=sim.frame+latency;
        auto* q=new FakeQuery;q->doneFrame=sim.frame+latency;++sim.liveQueries;return q;
    }
    bool done(IDirect3DQuery9* q){return static_cast<FakeQuery*>(q)->doneFrame<=sim.frame;}
    void release(IDirect3DQuery9* q){--sim.liveQueries;delete static_cast<FakeQuery*>(q);}
};
static UINT roundUp(UINT v,UINT a){return (v+a-1)/a*a;}
// The buffer is dropped or renamed: draws whose query was never issued are assumed finished within a few frames (they still read the old slice).
static void orphan(Stream& st){for(auto& d:st.draws)if(d->doneFrame==LONG_MAX)d->doneFrame=sim.frame+4;st.draws.clear();st.open.reset();}
struct Outcome {Ring::Slot slot;bool shrunk=false,grew=false;};
static void checkDecision(Stream& st,const Ring::Slot& slot,UINT cursorBefore,UINT bytes,bool freshBefore){
    const UINT need=roundUp(bytes,st.align),at=roundUp(cursorBefore,st.align);
    auto vacant=[&](UINT b,UINT e){for(const auto& d:st.draws)if(d->doneFrame>sim.frame&&d->offset<e&&b<d->offset+d->bytes)return false;return true;};
    if(freshBefore)CHECK(slot.flags==DWORD(D3DLOCK_NOOVERWRITE)&&slot.offset==0&&!slot.wrapped&&!slot.discarded); // a fresh buffer has nothing in flight
    if(std::uint64_t(at)+need<=st.capacity&&vacant(at,at+need)){CHECK(slot.offset==at&&slot.flags==DWORD(D3DLOCK_NOOVERWRITE)&&!slot.wrapped&&!slot.discarded);}
    else{
        CHECK(slot.wrapped);
        if(need<=st.capacity&&vacant(0,need)){CHECK(slot.offset==0&&slot.flags==DWORD(D3DLOCK_NOOVERWRITE)&&slot.fenceReuse&&!slot.discarded);} // fence reuse: every overlapping query is done
        else{CHECK(slot.offset==0&&slot.flags==DWORD(D3DLOCK_DISCARD)&&slot.discarded&&!slot.fenceReuse);}                                       // DISCARD only while a pending span overlaps
    }
    CHECK(slot.offset%st.align==0&&std::uint64_t(slot.offset)+bytes<=st.capacity);
}
static Outcome upload(Stream& st,UINT bytes,std::uint64_t seed){
    Hooks h{&st};Outcome out;
    auto recreate=[&](UINT capacity){orphan(st);st.slice=std::make_shared<Slice>(capacity);st.capacity=capacity;st.dev=Dxvk311Buffer{Default,UsageDynamic,capacity,false,st.dev.discardCounter};++st.creates;};
    if(st.capacity<bytes){Ring::reset(st.ring,0,h);recreate(Ring::ringCapacity(bytes));Ring::reset(st.ring,st.capacity,h);++st.grows;out.grew=true;}
    UINT cursorBefore=st.ring.cursor;bool freshBefore=st.ring.fresh;
    Ring::Slot slot=Ring::place(st.ring,bytes,st.align,h);
    checkDecision(st,slot,cursorBefore,bytes,freshBefore);
    if(slot.discarded&&Ring::oversized(st.ring,st.ring.peak)){ // production: a right-sized new buffer charges nothing instead of DISCARDing an oversized one
        const UINT smaller=Ring::ringCapacity(st.ring.peak);CHECK(smaller<st.capacity&&smaller>=bytes);
        Ring::reset(st.ring,0,h);recreate(smaller);Ring::reset(st.ring,smaller,h);
        slot=Ring::place(st.ring,bytes,st.align,h);checkDecision(st,slot,0,bytes,true);CHECK(!slot.discarded&&!slot.wrapped);++st.shrinks;out.shrunk=true;
    }
    out.slot=slot;++st.calls;st.wraps+=slot.wrapped;st.fenceReuse+=slot.fenceReuse;
    // The lock: the DXVK 3.1.1 model never waits for a ring lock, and only a DISCARD is charged (the whole buffer).
    const std::uint64_t before=st.dev.discardCounter;const auto lock=st.dev.lock(slot.flags);
    CHECK(!lock.waited&&lock.effective==slot.flags);
    if(slot.discarded){
        CHECK(st.dev.discardCounter==before+st.capacity);++st.discards;st.discardBytes+=st.capacity;st.frameCharge+=st.capacity;
        orphan(st);st.slice=std::make_shared<Slice>(st.capacity); // renamed: draws in flight keep the old slice
    }else{
        CHECK(st.dev.discardCounter==before);
        for(const auto& d:st.draws) // the race check: no NOOVERWRITE write over a region whose draw may still be reading it
            CHECK(d->doneFrame<=sim.frame||d->offset+d->bytes<=slot.offset||slot.offset+bytes<=d->offset);
    }
    fill(*st.slice,slot.offset,bytes,seed);
    auto d=std::make_shared<Draw>();d->slice=st.slice;d->offset=slot.offset;d->bytes=bytes;d->seed=seed;
    st.draws.push_back(d);st.flight.push_back(d);st.open=d;
    return out;
}
// End of frame: every draw still in flight reads back what was written; finished draws leave the list.
static void verifyFlight(Stream& st){
    for(auto it=st.flight.begin();it!=st.flight.end();){
        if((*it)->doneFrame<=sim.frame)it=st.flight.erase(it);
        else{CHECK(readsBack(**it));++it;}
    }
}
static void release(Stream& st){Hooks h{&st};Ring::reset(st.ring,0,h);st.draws.clear();st.flight.clear();st.open.reset();st.slice.reset();}

// 400-frame blocks, four kinds: a peak (12-20 MiB; 20 MiB at frame 0) at the start of each, then
//   0: small sizes only (1-49 KiB), a long stretch after the peak;
//   1: 150 calls log-uniform 1 KiB .. 6 MiB, then small;
//   2: 100 calls of 25-60% of the ring capacity (a ring holding 2 sets: DISCARDs while queries are late), then small;
//   3: 200 calls log-uniform 1 KiB .. 20 MiB, then small.
static UINT sizeFor(unsigned f,UINT capacity){
    const unsigned block=f/400,p=f%400,kind=block%4;auto r=[&](std::uint64_t n){return std::uint64_t(sim.rng())%n;};
    auto logUniform=[&](double lo,double hi){const double u=double(sim.rng())/double(std::mt19937::max());return roundUp(UINT(lo*std::pow(hi/lo,u)),16);};
    if(f==0)return UINT(20*MiB);
    if(p==0)return roundUp(UINT(12*MiB+r(8*MiB)),16);
    if(kind==1&&p<150)return logUniform(double(KiB),6.0*MiB);
    if(kind==2&&p<100&&capacity)return roundUp(UINT(double(capacity)*(0.25+0.35*double(r(1000))/1000.0)),16);
    if(kind==3&&p<200)return logUniform(double(KiB),20.0*MiB);
    return roundUp(UINT(KiB+r(48*KiB)),16);
}
static void longRun(unsigned frames){
    sim=Sim();Stream vb,ib;vb.name="vb";vb.align=16;ib.name="ib";ib.align=4;
    std::uint64_t oldCharge=0,newCharge=0;UINT runningMax[2]={};unsigned oversizedSeen=0,simulatedShrinks=0,shrinkWhenSmall=0;std::uint64_t maxCharge=0;
    for(unsigned f=0;f<frames;++f){
        sim.frame=long(f);vb.frameCharge=ib.frameCharge=0;
        const UINT vbBytes=sizeFor(f,vb.capacity),ibBytes=std::max<UINT>(1024,roundUp(vbBytes/3,16));
        // 0.3.192 evidence: before the peak the ring is right-sized; right after a peak the decayed peak keeps it from looking oversized.
        if(f%400==1)CHECK(!Ring::oversized(vb.ring,vb.ring.peak));
        const auto a=upload(vb,vbBytes,0x1000+f),b=upload(ib,ibBytes,0x900000+f);(void)a;(void)b;
        // At most one DISCARD per ring and call, charged at the buffer size: the frame never pays more than the two buffers.
        CHECK(vb.frameCharge<=vb.capacity+0u||vb.shrinks||vb.grows);CHECK(vb.frameCharge+ib.frameCharge<=std::uint64_t(vb.capacity)+ib.capacity+(28*MiB));
        CHECK(vb.frameCharge<=Ring::ringCapacity(20*MiB)&&ib.frameCharge<=Ring::ringCapacity(20*MiB));
        const std::uint64_t charge=vb.frameCharge+ib.frameCharge;newCharge+=charge;maxCharge=std::max(maxCharge,charge);
        // The old scheme: every call DISCARDed an 8 MiB-rounded buffer per stream.
        runningMax[0]=std::max(runningMax[0],vbBytes);runningMax[1]=std::max(runningMax[1],ibBytes);
        for(unsigned s=0;s<2;++s)oldCharge+=std::uint64_t(8*MiB)*((runningMax[s]+8*MiB-1)/(8*MiB));
        verifyFlight(vb);verifyFlight(ib);
        // A peak decays: 300 small calls after it the ring is either already shrunk (a DISCARD wrap shrinks) or reports oversized,
        // and then a shrink to ringCapacity(peak) gives a fresh, right-sized, NOOVERWRITE-first buffer.
        if(f%400==350&&(f/400)%2==0){
            const bool over=Ring::oversized(vb.ring,vb.ring.peak);oversizedSeen+=over;
            CHECK(over||vb.capacity<=4*MiB||vb.capacity<=2*std::uint64_t(Ring::ringCapacity(vb.ring.peak)));
            if(over){
                const UINT peak=vb.ring.peak,old=vb.capacity,smaller=Ring::ringCapacity(peak);CHECK(smaller<old);
                Hooks h{&vb};Ring::reset(vb.ring,0,h);orphan(vb);vb.slice=std::make_shared<Slice>(smaller);vb.capacity=smaller;vb.dev=Dxvk311Buffer{Default,UsageDynamic,smaller,false,vb.dev.discardCounter};
                Ring::reset(vb.ring,smaller,h);CHECK(vb.ring.fresh&&vb.ring.cursor==0&&vb.ring.capacity==smaller&&!Ring::oversized(vb.ring,peak));++simulatedShrinks;
            }else ++shrinkWhenSmall;
        }
    }
    CHECK(sim.liveQueries>=0);release(vb);release(ib);CHECK(sim.liveQueries==0); // every query is released
    const unsigned total=vb.discards+ib.discards;
    CHECK(newCharge<oldCharge/3); // the point of the rings: well under the old 8 MiB-per-stream-per-call charge
    CHECK(vb.fenceReuse>0&&ib.fenceReuse>0&&vb.wraps>=vb.fenceReuse&&vb.shrinks+simulatedShrinks>0);
    if(frames>=800)CHECK(oversizedSeen+shrinkWhenSmall>=frames/800); // every decayed peak was observed one way or the other
    std::printf("PASS ring model, %u frames: vb discards=%u fenceReuse=%u wraps=%u shrinks=%u(+%u simulated, oversized seen %u) grows=%u | ib discards=%u fenceReuse=%u | discard charge new %.1f MiB total (max %.1f MiB/frame) vs old scheme %.1f MiB, %u discards\n",
        frames,vb.discards,vb.fenceReuse,vb.wraps,vb.shrinks,simulatedShrinks,oversizedSeen,vb.grows,ib.discards,ib.fenceReuse,double(newCharge)/MiB,double(maxCharge)/MiB,double(oldCharge)/MiB,total);
}

// Constant per-frame size, queries done `late` frames after they were issued: a span is reusable late+1 frames after its call, so a ring holding
// at least late+1 sets never needs a DISCARD, not even while it fills. A ring holding fewer (ringCapacity = bytes + 2x bytes headroom capped
// at 1..8 MiB, so 3 sets up to 4 MiB, 2 sets up to 8 MiB and 1 set above) can only DISCARD at a wrap, and only while the old span is still pending.
static unsigned steady(UINT bytes,int late,unsigned frames,unsigned* setsOut=nullptr){
    sim=Sim();sim.fixedLatency=late;Stream vb,ib;vb.align=16;ib.align=4;
    for(unsigned f=0;f<frames;++f){sim.frame=long(f);upload(vb,bytes,0x77+f);upload(ib,std::max<UINT>(1024,roundUp(bytes/3,16)),0x99+f);verifyFlight(vb);verifyFlight(ib);}
    const unsigned sets=Ring::ringCapacity(bytes)/bytes;if(setsOut)*setsOut=sets;
    const unsigned discards=vb.discards;
    if(int(sets)>=late+1)CHECK(vb.discards==0&&ib.discards==0&&vb.grows==1&&vb.shrinks==0);
    else CHECK(vb.discards<=frames/std::max(1u,sets)+1&&vb.discards>0);
    release(vb);release(ib);CHECK(sim.liveQueries==0);return discards;
}
static void steadyStates(unsigned frames){
    // Small sets: no DISCARD at all, queries 2 frames late (after warm-up and during it).
    for(UINT bytes:{4*KiB,64*KiB,256*KiB,512*KiB,640*KiB}){unsigned sets=0;CHECK(steady(bytes,2,frames,&sets)==0&&sets>=3);}
    for(int late:{0,1,3})CHECK(steady(100*KiB,late,frames)==0);
    // Larger sets: up to 4 MiB the ring holds 3 sets (no DISCARD with queries 2 frames late); above, 2 sets (1 above 8 MiB): with a late
    // query every wrap DISCARDs, but never more than one per wrap and only while a span overlaps (the model in upload() checks both); with
    // instantly completing queries there is none.
    unsigned sets=0;for(UINT bytes:{MiB,2*MiB,4*MiB,6*MiB,20*MiB}){CHECK(steady(bytes,0,frames,&sets)==0);const unsigned late=steady(bytes,2,frames);
        CHECK(bytes<=4*MiB?(sets>=3&&late==0):(late>0&&late<=frames/sets+1));
        std::printf("  steady %2u MiB: ring holds %u set(s); discards over %u frames: 0 (instant queries), %u (2 frames late)\n",unsigned(bytes/MiB),sets,frames,late);}
    std::puts("PASS steady state: constant sizes, queries 0-3 frames late: zero DISCARDs whenever the ring holds more sets than the query latency");
}
static void failedQueries(){
    // CreateQuery/Issue failing makes every span pending forever: the old DISCARD-on-wrap behaviour, and still never a NOOVERWRITE over a pending region.
    sim=Sim();sim.failQueries=true;Stream vb;vb.align=16;const UINT bytes=300*KiB;
    for(unsigned f=0;f<200;++f){sim.frame=long(f);upload(vb,bytes,f);verifyFlight(vb);}
    CHECK(vb.fenceReuse==0&&vb.discards>=200*bytes/Ring::ringCapacity(bytes)-1&&vb.discards<=vb.wraps&&sim.liveQueries==0);release(vb);
    std::puts("PASS failed queries: spans stay pending, every wrap DISCARDs, no write over an in-flight region");
}
int main(int argc,char** argv){
    readBackFlags();majors();
    longRun(argc>1?unsigned(std::atoi(argv[1])):2000);
    steadyStates(argc>2?unsigned(std::atoi(argv[2])):500);failedQueries();
    std::puts("dxvk3 lock flags: read-back gate, version parse, fence-checked rings vs the DXVK 3.1.1 lock model passed");
}
