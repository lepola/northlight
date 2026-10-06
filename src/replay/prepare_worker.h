#pragma once
// 0.3.177 (r83 a1-prepare): the stable actor selection's per-record prepare (distance, rigid bone,
// identity key, palette root) runs on a worker while the game still issues its draws. Portable: no
// D3D calls, no renderer state. The renderer publishes each selected skinned record right after
// capture (in replays order, with its replays index); the worker prepares them in order with the
// frame's frozen camera and the caches it owns while the frame is open; the join (the first
// statement of the selection) stops it and finishes the rest inline with the same prepareRecord(),
// resuming from the state the last output carries. Inline (no worker) is prepareRecord() over the
// same records in the same order: every output is bit-identical either way.
// 0.3.192 (CS): "the render thread" below is the thread that runs the Device; with the command stream on that is the
// replay thread, which also owns the 5 ms watchdog's clock. Nothing here depends on it being the game thread.
#include "sampled_vertex_cache.h"
#include "replay_bounds.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <memory>
#include <mutex>
#include <thread>

namespace NorthlightActorPrepare {
using Draw=NorthlightActorShadowSelection::Draw;
static_assert(!NorthlightActorShadowSelection::FlickerFixes||NorthlightActorShadowSelection::Tuning{}.stableIdentity,
    "the offloaded prepare is the stableIdentity path; any other tuning runs the selection loop inline");
// Owned by the worker from a frame's open to the join's acknowledgement, by the render thread otherwise.
struct Caches {NorthlightActorDeformation::SampledVertexCache sampled;NorthlightActorDeformation::RigidBoneCache bones;};
// The loop state carried from one selected skinned record to the next (and the distance counters).
struct State {
    unsigned previousGroup=~0u;const void* previousShader=nullptr;const void* previousDecl=nullptr;
    float previousDistance=0,previousAt[3]={};bool previousKnown=false,groupRigid=true;
    std::size_t distanceTests=0,distanceReused=0;
};
// One record's result: the selection draw, whether its rigid bone was tested (the replay's boneKnown,
// written back at the join), and the state after it (the carried state of the next record).
struct Output {Draw item;bool tested=false;State after;};
// The per-record body of selectStableActors (stableIdentity). Record: the renderer's Replay (or a test
// record) with constantGroup, originalShader, decl, mesh(), shared, constants and, filled at capture,
// program (the program handle, null: not an actor program), elements/elementCount (a copy of the
// declaration) and declared (program && the declaration was read).
// 0.3.179 (T2): a frame's declarations, each read once through the declaration cache into an entry that
// is never changed afterwards. Keyed by the declaration pointer alone: every record holds a reference to
// its declaration for the whole frame, so the pointer cannot be reused within the frame, and the arena
// is reset per frame (after the recycle, once the prepare worker has settled). Full (more than Capacity
// distinct declarations): the caller keeps the per-record copy.
struct DeclCopy {const void* key=nullptr;UINT count=0;bool declared=false;D3DVERTEXELEMENT9 elements[MAXD3DDECLLENGTH+1]={};};
class DeclArena {
public:
    static constexpr unsigned Capacity=128;
private:
    std::unique_ptr<DeclCopy[]> entries_{new DeclCopy[Capacity]};unsigned used_=0;const DeclCopy* last_=nullptr;
public:
    void reset(){used_=0;last_=nullptr;}
    unsigned used()const{return used_;}
    // get(key,elements,count): the declaration cache's read. declared: it succeeded and the elements fit.
    template<class Decl,class Get> const DeclCopy* find(Decl* key,Get&& get){
        if(last_&&last_->key==key)return last_;
        for(unsigned i=0;i<used_;++i)if(entries_[i].key==key)return last_=&entries_[i];
        if(used_==Capacity)return nullptr;
        DeclCopy& e=entries_[used_];const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
        e.key=key;e.declared=get(key,elements,count)&&count<=MAXD3DDECLLENGTH+1;e.count=e.declared?count:0;
        if(e.declared)std::copy(elements,elements+count,e.elements);
        ++used_;return last_=&e;
    }
};
// The record's program: a handle (tests of the 0.3.177 layout) or the raw pointer (0.3.179 T1).
inline const NorthlightActorDeformation::Program* programOf(const std::shared_ptr<const NorthlightActorDeformation::Program>& p){return p.get();}
inline const NorthlightActorDeformation::Program* programOf(const NorthlightActorDeformation::Program* p){return p;}
// Record: see prepareRecord; its declaration through declarationElements()/declarationCount().
template<class Record> void prepareRecord(State& s,const Record& p,std::size_t index,Caches& caches,const float* inverseView,const float* camera,Output& out){
    Draw& item=out.item;item=Draw{};item.index=index;item.bytes=p.mesh().byteSize();item.group=p.constantGroup;
    const NorthlightActorDeformation::Program* program=programOf(p.program);const bool declared=p.declared&&program;
    const D3DVERTEXELEMENT9* elements=declared?p.declarationElements():nullptr;const UINT count=declared?p.declarationCount():0;
    const void* shader=p.originalShader;const void* decl=p.decl;
    if(p.constantGroup==s.previousGroup&&shader==s.previousShader&&decl==s.previousDecl){
        item.known=s.previousKnown;item.distanceSquared=s.previousDistance;std::memcpy(item.at,s.previousAt,sizeof item.at);++s.distanceReused;
    }else{
        ++s.distanceTests;
        item.known=declared&&caches.sampled.distance(*program,p.mesh(),p.shared,p.decl,elements,count,
            p.constants,inverseView,camera,item.distanceSquared,item.at);
    }
    // A group is rigid only if every draw is: after its first multi-bone draw the remaining draws
    // need no palette test. Only a group's first draw supplies the actor identity key's root.
    const bool first=p.constantGroup!=s.previousGroup;if(first)s.groupRigid=true;
    out.tested=s.groupRigid&&declared;
    item.bone=out.tested?caches.bones.bone(*program,p.mesh(),p.shared,p.decl,elements,count):NAN;item.rigid=!std::isnan(item.bone);
    s.groupRigid=item.rigid;
    // Stable per-draw identity: the snapshot-cache entry (VB/IB identity, range, base, declaration)
    // with the shader; a shape hash only for uncached draws.
    std::uint64_t key=14695981039346656037ull;auto mix=[&](std::uint64_t n){key=(key^n)*1099511628211ull;};
    mix(reinterpret_cast<std::uintptr_t>(shader));mix(reinterpret_cast<std::uintptr_t>(decl));
    if(p.shared)mix(reinterpret_cast<std::uintptr_t>(p.shared.get()));else{mix(p.mesh().vertexCount);mix(p.mesh().primitiveCount);mix(item.bytes);}
    item.key=key;
    // One palette root per constant group: only the audited palette template (c31.. row-major 3x4
    // bones, translation in w: the skin-envelope specialization) has a provable root.
    if(first)item.hasRoot=program&&NorthlightReplayBounds::SkinEnvelope::supports(*program)&&program->paletteBase==31&&
        NorthlightActorDeformation::rootWorld(*program,p.constants,inverseView,item.root);
    s.previousGroup=p.constantGroup;s.previousShader=shader;s.previousDecl=decl;
    s.previousKnown=item.known;s.previousDistance=item.distanceSquared;std::memcpy(s.previousAt,item.at,sizeof s.previousAt);
    out.after=s;
}
// Bitwise equality (floats by their bits): the runtime self-check and the tests.
inline bool sameBits(const void* a,const void* b,std::size_t n){return std::memcmp(a,b,n)==0;}
inline bool same(const State& a,const State& b){
    return a.previousGroup==b.previousGroup&&a.previousShader==b.previousShader&&a.previousDecl==b.previousDecl&&sameBits(&a.previousDistance,&b.previousDistance,4)&&
        sameBits(a.previousAt,b.previousAt,12)&&a.previousKnown==b.previousKnown&&a.groupRigid==b.groupRigid&&a.distanceTests==b.distanceTests&&a.distanceReused==b.distanceReused;}
inline bool same(const Output& x,const Output& y){
    const Draw& a=x.item;const Draw& b=y.item;
    return x.tested==y.tested&&same(x.after,y.after)&&a.index==b.index&&a.bytes==b.bytes&&a.group==b.group&&sameBits(&a.distanceSquared,&b.distanceSquared,4)&&
        sameBits(a.at,b.at,12)&&a.known==b.known&&a.rigid==b.rigid&&sameBits(&a.bone,&b.bone,4)&&a.key==b.key&&a.keep==b.keep&&
        sameBits(a.extra,b.extra,sizeof a.extra)&&a.extras==b.extras&&sameBits(a.root,b.root,12)&&a.hasRoot==b.hasRoot;}
// The frame's inputs frozen at its first publish.
struct Frame {float inverseView[16]={},camera[3]={};bool timed=false;};
// The worker's per-record call: prepareRecord (tests substitute one that throws or waits).
struct Prepare {template<class Record> void operator()(State& s,const Record& p,std::size_t index,Caches& c,const Frame& f,Output& out)const{
    prepareRecord(s,p,index,c,f.inverseView,f.camera,out);}};

// 0.3.179 (M1): the capture-side handoff (fill + publish) timed as one span per sampled record, 1 in
// Stride from a per-frame phase, minus the cost of the clock read pair itself (pairNs: the median delta
// of 33 back-to-back reads, taken once per measured frame at its first sampled record). rawUs and
// correctedUs are frame estimates (x Stride). Off: no clock read.
template<class Clock> class HandoffMeter {
    bool on_=false;unsigned phase_=0,count_=0,samples_=0;double pairNs_=-1,rawNs_=0,correctedNs_=0;
    static double ns(typename Clock::duration d){return std::chrono::duration<double,std::nano>(d).count();}
    void calibrate(){typename Clock::time_point t[33];for(auto& x:t)x=Clock::now();double d[32];
        for(unsigned i=0;i<32;++i)d[i]=ns(t[i+1]-t[i]);std::nth_element(d,d+16,d+32);pairNs_=d[16];}
public:
    static constexpr unsigned Stride=16;
    void beginFrame(bool on,unsigned phase){on_=on;phase_=phase%Stride;count_=samples_=0;pairNs_=-1;rawNs_=correctedNs_=0;}
    bool on()const{return on_;}
    // Per selected skinned record, in capture order: whether it is timed.
    bool sample(){if(!on_)return false;const bool s=(count_++ +phase_)%Stride==0;if(s&&pairNs_<0)calibrate();return s;}
    typename Clock::time_point start()const{return Clock::now();}
    void stop(typename Clock::time_point t){const double span=ns(Clock::now()-t);rawNs_+=span;correctedNs_+=std::max(0.,span-pairNs_);++samples_;}
    double rawUs()const{return Stride*rawNs_/1000;}
    double correctedUs()const{return Stride*correctedNs_/1000;}
    double pairNs()const{return pairNs_;}
    unsigned samples()const{return samples_;}
};
inline void pause(){
#if defined(__i386__)||defined(__x86_64__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    __asm__ __volatile__("yield");
#endif
}
// One persistent thread; one frame at a time; single producer (the render thread: open, publish,
// stop), single consumer (the worker). SPSC without wrap: the arena restarts at every open.
template<class Record,class Process=Prepare> class Worker {
public:
    static constexpr std::uint32_t Capacity=4096; /* the replay cap */
    static constexpr std::uint32_t WakeBatch=48;  /* a sleeping worker is woken once this many records wait */
    static constexpr double SpinUs=30,WatchdogMs=5; /* the default; tests may raise it (scheduler stalls of a loaded test host) */
    struct Stop {std::uint32_t published=0,done=0;bool failed=false,timedOut=false;double waitMs=-1;};
private:
    using Clock=std::chrono::steady_clock;
    enum Phase : std::uint64_t {Idle=0,Open=1,Stopping=2,Stopped=3};
    static std::uint64_t word(std::uint64_t serial,Phase phase){return serial<<2|phase;}
    struct Slot {const Record* record=nullptr;std::size_t index=0;};
    std::unique_ptr<Slot[]> slots_;std::unique_ptr<Output[]> out_;
    std::atomic<std::uint64_t> word_{0}; /* frame serial << 2 | phase */
    std::atomic<std::uint32_t> published_{0},done_{0};
    std::atomic<bool> wakeable_{false},failed_{false};std::atomic<std::uint64_t> busyNs_{0};
    std::mutex mutex_;std::condition_variable wake_;bool blocked_=false,shutdown_=false; /* under mutex_ */
    std::thread thread_;
    Frame frame_;Caches* caches_=nullptr;Process process_;
    std::uint64_t serial_=0;bool open_=false,abandoned_=false,joined_=false;double watchdogMs_=WatchdogMs;std::uint32_t wakeBatch_=WakeBatch; /* render thread */
    // Producer-side counters of the open frame (render thread).
    std::uint32_t wakes_=0;double notifyUs_=0;
    void notify(){
        const auto start=frame_.timed?Clock::now():Clock::time_point{};
        {std::lock_guard<std::mutex> lock(mutex_);wake_.notify_one();}++wakes_;
        if(frame_.timed)notifyUs_+=std::chrono::duration<double,std::micro>(Clock::now()-start).count();
    }
    // Worker: one frame. Returns when the frame ends (acknowledged or claimed) or at shutdown.
    void run(std::uint64_t mine){
        const std::uint64_t open=word(mine,Open),stopping=word(mine,Stopping);
        State s;std::uint32_t d=0;bool failed=false;const bool timed=frame_.timed;std::uint64_t busy=0;
        for(;;){
            if(word_.load(std::memory_order_acquire)!=open)break;
            const std::uint32_t n=failed?d:published_.load(std::memory_order_acquire);
            if(d<n){
                const Slot slot=slots_[d];const auto start=timed?Clock::now():Clock::time_point{};
                try{process_(s,*slot.record,slot.index,*caches_,frame_,out_[d]);}
                catch(...){failed=true;failed_.store(true,std::memory_order_relaxed);continue;} /* the joiner prepares record d on */
                if(timed){busy+=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count());busyNs_.store(busy,std::memory_order_relaxed);}
                done_.store(++d,std::memory_order_release);continue;
            }
            // No record: spin SpinUs (a clock read every 64 pauses), then sleep until a record, the
            // stop or shutdown.
            bool arrived=false;
            if(!failed){const auto spinStart=Clock::now();
                for(unsigned i=1;;++i){pause();
                    if(word_.load(std::memory_order_acquire)!=open||published_.load(std::memory_order_acquire)>d){arrived=true;break;}
                    if(!(i&63)&&std::chrono::duration<double,std::micro>(Clock::now()-spinStart).count()>=SpinUs)break;}}
            if(arrived)continue;
            std::unique_lock<std::mutex> lock(mutex_);
            // 0.3.179 (T3): the fence pairs with publish()'s at the wake threshold (fence-fence Dekker): either
            // this re-check sees the record or the producer sees wakeable_ and notifies.
            blocked_=true;wakeable_.store(true,std::memory_order_relaxed);std::atomic_thread_fence(std::memory_order_seq_cst);
            wake_.wait(lock,[&]{return shutdown_||word_.load(std::memory_order_seq_cst)!=open||(!failed&&published_.load(std::memory_order_seq_cst)>d);});
            blocked_=false;wakeable_.store(false,std::memory_order_relaxed);
            if(shutdown_)return;
        }
        // Acknowledge a stop of this frame; a claimed or newer frame is not ours to touch.
        std::uint64_t expected=stopping;word_.compare_exchange_strong(expected,word(mine,Stopped),std::memory_order_acq_rel);
    }
    void loop(){
        std::uint64_t mine=0;
        for(;;){
            // A newer frame, open or already stopping (stopped before this thread took the lock: run()
            // acknowledges it at once, having touched nothing).
            {std::unique_lock<std::mutex> lock(mutex_);blocked_=true;
             wake_.wait(lock,[&]{const auto w=word_.load(std::memory_order_seq_cst);return shutdown_||(((w&3)==Open||(w&3)==Stopping)&&(w>>2)!=mine);});
             blocked_=false;if(shutdown_)return;mine=word_.load(std::memory_order_acquire)>>2;}
            run(mine);
        }
    }
public:
    Worker():slots_(new Slot[Capacity]),out_(new Output[Capacity]){}
    Worker(const Worker&)=delete;Worker& operator=(const Worker&)=delete;
    ~Worker(){join();}
    // Shutdown: the thread finishes what it is doing (at most the rest of an open frame, or an abandoned
    // worker's record in flight) and exits. Afterwards nothing is read: settled, no new frame.
    void join(){{std::lock_guard<std::mutex> lock(mutex_);shutdown_=true;}wake_.notify_all();if(thread_.joinable())thread_.join();open_=false;joined_=true;}
    // false: no thread (creation failed) or abandoned after a watchdog: the caller runs inline.
    bool ready(){
        if(abandoned_||joined_)return false;
        if(!thread_.joinable()){try{thread_=std::thread([this]{loop();});}catch(...){abandoned_=true;return false;}}
        return true;
    }
    bool abandoned()const{return abandoned_;}
    // After a watchdog: once the abandoned worker has settled (it touches nothing of its last frame), the
    // caller may take it back into use. false: not abandoned, not settled, or no thread.
    bool rearm(){if(!abandoned_||!thread_.joinable()||!settled())return false;abandoned_=false;return true;}
    void setWatchdogMs(double ms){watchdogMs_=ms;}
    void setWakeBatch(std::uint32_t n){wakeBatch_=n?n:1;} /* tests (the wake stress at 1) */
    std::uint32_t done()const{return done_.load(std::memory_order_acquire);} /* records the worker finished (tests) */
    bool open()const{return open_;}
    Process& process(){return process_;}
    // Opens a frame: the caches belong to the worker until stop() returns.
    bool begin(const Frame& frame,Caches& caches){
        if(open_||!ready())return false;
        published_.store(0,std::memory_order_relaxed);done_.store(0,std::memory_order_relaxed);failed_.store(false,std::memory_order_relaxed);busyNs_.store(0,std::memory_order_relaxed);
        frame_=frame;caches_=&caches;wakes_=0;notifyUs_=0;
        word_.store(word(++serial_,Open),std::memory_order_seq_cst);open_=true;
        notify(); /* once per frame at open */
        return true;
    }
    // A record, after begin(). false: the arena is full (the caller stops publishing; the join
    // prepares the rest inline).
    bool publish(const Record* record,std::size_t index){
        const std::uint32_t n=published_.load(std::memory_order_relaxed);if(!open_||n>=Capacity)return false;
        // 0.3.179 (T3): a release store (a plain store on x86 TSO), the slot written before it; the full
        // fence only at the wake threshold, paired with the worker's before it sleeps.
        slots_[n]={record,index};published_.store(n+1,std::memory_order_release);
        if(n+1-done_.load(std::memory_order_relaxed)>=wakeBatch_){std::atomic_thread_fence(std::memory_order_seq_cst);if(wakeable_.exchange(false))notify();}
        return true;
    }
    std::uint32_t published()const{return published_.load(std::memory_order_relaxed);}
    // Ends the frame: the worker finishes at most its record in flight (a blocked one is claimed at
    // once). After it returns the caches and out[0, done) belong to the caller. timedOut: the worker
    // did not acknowledge within the watchdog; it is abandoned for the session and may still be inside
    // one record: until settled(), the caller keeps every published record (and what it points to)
    // alive and never uses the arena, the frame or these caches again. Never blocks longer.
    Stop stop(){
        Stop r;if(!open_)return r;open_=false;
        const std::uint64_t stopping=word(serial_,Stopping),stopped=word(serial_,Stopped);
        const auto start=frame_.timed?Clock::now():Clock::time_point{};
        word_.store(stopping,std::memory_order_seq_cst);
        {std::lock_guard<std::mutex> lock(mutex_);
         if(blocked_){std::uint64_t expected=stopping;word_.compare_exchange_strong(expected,stopped,std::memory_order_acq_rel);wake_.notify_one();}}
        Clock::time_point watch{};bool watching=false;
        for(unsigned i=1;word_.load(std::memory_order_acquire)!=stopped;++i){pause();
            if(!(i&255)){const auto now=Clock::now();if(!watching){watch=now;watching=true;}
                else if(std::chrono::duration<double,std::milli>(now-watch).count()>=watchdogMs_){abandoned_=true;r.timedOut=true;break;}}}
        r.published=published_.load(std::memory_order_relaxed);r.done=r.timedOut?0:done_.load(std::memory_order_acquire);r.failed=failed_.load(std::memory_order_relaxed);
        if(frame_.timed)r.waitMs=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        return r;
    }
    // The last stopped frame is acknowledged: the worker touches none of its records any more.
    bool settled()const{return joined_||(!open_&&(serial_==0||word_.load(std::memory_order_acquire)==word(serial_,Stopped)));}
    const Output* outputs()const{return out_.get();}
    Output* outputs(){return out_.get();}
    const Frame& frame()const{return frame_;}
    const Record* record(std::uint32_t k)const{return slots_[k].record;}
    std::size_t index(std::uint32_t k)const{return slots_[k].index;}
    // The last frame's counters (read after stop()).
    std::uint32_t wakes()const{return wakes_;}
    double notifyUs()const{return notifyUs_;}
    double busyMs()const{return double(busyNs_.load(std::memory_order_relaxed))/1e6;}
};
}
