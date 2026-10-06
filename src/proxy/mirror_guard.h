#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#else
#include <thread>
#endif

// One real gate acquisition per game-facing call. Nested layers (Device ->
// resource unwrap -> MirrorDevice) see that an enclosing MirrorGuard on this
// thread already owns the SAME gate and skip the recursive lock. Invariant:
// held==&gate only while this thread owns the gate through a live MirrorGuard
// (the mutex, or from 0.3.182 the owner's elided entry); guards are strictly
// scoped, so restore-before-release keeps it exact.
// 0.3.180 (D0, r88): every lock site of a device gate is a MirrorGuard (device,
// registry, resource proxies and forwarders, state blocks, swap chains, RawScope),
// so their nested re-locks are skipped too, and each real acquisition feeds the
// thread census: foreign (not the CreateDevice caller) entries per site class.
// 0.3.182 (D1, r88 §3.3): the owner thread elides the mutex with a seq_cst Dekker pair,
// per call and never sticky. The owner exchanges inside=1 and elides only while no
// foreign call is in flight (foreignActive==0); otherwise it resets inside and locks.
// A foreign thread announces itself (foreignActive++), waits until the owner is not
// inside, then locks. Each guard's exit acts on the mode its entry recorded.
static constexpr bool kMirrorSingleGate=true;
#ifndef NORTHLIGHT_MIRROR_OWNER_FAST_PATH
#define NORTHLIGHT_MIRROR_OWNER_FAST_PATH 1
#endif
static constexpr bool kMirrorOwnerFastPath=NORTHLIGHT_MIRROR_OWNER_FAST_PATH!=0;
// Test counterfactual only: 1 keys "foreign" on the TLS held pointer instead of the thread id.
#ifndef NORTHLIGHT_GATE_CENSUS_BY_HELD
#define NORTHLIGHT_GATE_CENSUS_BY_HELD 0
#endif
// Test counterfactuals only, each must fail its test: 1 a relaxed Dekker (plain store instead of the
// xchg, relaxed loads and increment), 2 the foreign thread skips the wait on inside, 3 elision leaves
// held unset, 4 the exit acts on the current state instead of the recorded mode.
#ifndef NORTHLIGHT_GATE_ELISION_COUNTERFACTUAL
#define NORTHLIGHT_GATE_ELISION_COUNTERFACTUAL 0
#endif
// 0.3.192 (CS): exclusive-owner mode, set by the command stream while one thread (the replay thread) runs every
// Device call. The owner's entry then replaces the seq_cst exchange (a full barrier on every call, ~25 ns) by a
// plain store: the Dekker pair is made asymmetric. A foreign thread (never expected; correctness does not depend on
// that) announces itself and then forces a barrier on every core before it looks at `inside`
// (FlushProcessWriteBuffers: the standard asymmetric-fence / biased-lock pairing), so either the owner's store is
// visible to it or the owner's later load sees the announcement. Where no such barrier exists (hosts of the tests)
// the owner keeps a real fence in this mode: the protocol and its races stay testable, only the saving is absent.
#ifndef NORTHLIGHT_GATE_ASYMMETRIC
#ifdef _WIN32
#define NORTHLIGHT_GATE_ASYMMETRIC 1
#else
#define NORTHLIGHT_GATE_ASYMMETRIC 0
#endif
#endif
// Tests substitute a mutex that asserts ownership on unlock and counts acquisitions.
#ifndef NORTHLIGHT_GATE_MUTEX
#define NORTHLIGHT_GATE_MUTEX std::recursive_mutex
#endif
enum class MirrorSite:unsigned {Device,Registry,Resource,StateBlock,SwapChain,Raw,Buffer,Count};
inline const char* mirrorSiteName(unsigned site){
    static const char* const names[]={"device","registry","resource","stateblock","swapchain","raw","buffer"};
    return site<unsigned(MirrorSite::Count)?names[site]:"none";
}
struct MirrorGate {
    static constexpr unsigned Sites=unsigned(MirrorSite::Count);
    NORTHLIGHT_GATE_MUTEX mutex;
    // The CreateDevice caller: the constructing thread (DeviceMirror is a Device member; the Device
    // constructor also sets it explicitly). Written before the gate is shared, read-only afterwards.
    // 0.3.192 (CS): with CommandStream=1 the Device is constructed on the game thread but every call into it runs on
    // the replay thread, which is therefore the real owner: the replay thread must store its own MirrorGuard::threadId()
    // here before it executes its first command (the game thread never calls the Device, so nothing races the write).
    // Left as constructed, every replayed call would count as foreign and take the mutex.
    std::uint32_t ownerTid;
    // 0.3.182 (D1): 1 while the owner runs an elided call (written by the owner only), and the number of
    // foreign calls between their announcement and their exit.
    std::atomic<unsigned> inside{0},foreignActive{0};
    // Owner entries that took the mutex because a foreign call was in flight (owner-written, relaxed).
    std::atomic<std::uint32_t> ownerLocked{0};
    // 0.3.192 (CS): exclusive-owner mode (see NORTHLIGHT_GATE_ASYMMETRIC). Set before the owner thread starts calling,
    // cleared only after it is joined; foreign entries seen while it is on are counted in foreignExclusive.
    std::atomic<bool> exclusive{false};
    std::atomic<std::uint32_t> foreignExclusive{0};
    // Census (relaxed): foreign entries per site class; the first foreign {tid,site,frame}, claimed by
    // CAS and published by firstReady; the first Present/SwapChain::Present/draw callers.
    std::atomic<std::uint32_t> foreign[Sites]={};
    std::atomic<bool> firstClaimed{false},firstReady{false};
    std::uint32_t firstTid=0,firstSite=Sites,firstFrame=0;
    std::atomic<std::uint32_t> frame{0},presentTid{0},swapPresentTid{0},drawTid{0};
    // Owner outer entries (elided or locked since 0.3.182) per site class while counting (RenderProfile
    // sample frames). Written, read and cleared by the owner while it holds the gate (DRAWGATE ab).
    std::atomic<bool> counting{false};
    std::atomic<std::uint32_t> acquired[Sites]={};
    // Called once per site class at its first foreign entry (the gate is held for gated sites).
    void(*report)(void*,unsigned site,std::uint32_t tid)noexcept=nullptr;void* reportContext=nullptr;
    MirrorGate();
    MirrorGate(const MirrorGate&)=delete;
    MirrorGate& operator=(const MirrorGate&)=delete;
    // An outer entry (MirrorGuard's elided, locked or foreign branch). Nested entries never reach it:
    // they run on the thread that holds the gate already.
    void entered(MirrorSite site,bool foreignThread);
    // Tracked vertex/index buffers take no gate: Lock/Unlock/Release report their thread here.
    void noteBuffer();
    void noteFirst(std::atomic<std::uint32_t>& slot);
    std::uint32_t takeAcquired(MirrorSite site){return acquired[unsigned(site)].exchange(0,std::memory_order_relaxed);}
    [[gnu::cold]] [[gnu::noinline]] void foreignEntry(MirrorSite site,std::uint32_t tid);
    // 0.3.182 (D1): the owner's entry. Elide: no foreign call is in flight. Reenter: this thread's elided
    // entry of this gate is further up its stack under another gate's guard (A -> B -> A; only the owner
    // writes inside), so it keeps the gate and this entry's exit leaves inside alone. Lock: a foreign
    // call is in flight; inside is reset and the caller takes the mutex.
    enum OwnerEntry:unsigned char {Lock,Elide,Reenter};
    OwnerEntry enterOwner(){
        if(exclusive.load(std::memory_order_relaxed))return enterOwnerExclusive();
#if NORTHLIGHT_GATE_ELISION_COUNTERFACTUAL==1
        if(inside.load(std::memory_order_relaxed))return Reenter;
        inside.store(1,std::memory_order_relaxed);
        if(!foreignActive.load(std::memory_order_relaxed))return Elide;
#else
        if(inside.exchange(1,std::memory_order_seq_cst))return Reenter;
        if(!foreignActive.load(std::memory_order_seq_cst))return Elide;
#endif
        inside.store(0,std::memory_order_release);
        ownerLocked.store(ownerLocked.load(std::memory_order_relaxed)+1,std::memory_order_relaxed);
        return Lock;
    }
    // 0.3.192 (CS): the same decision with a plain store of inside (only the owner writes it, so a nested entry is
    // detected by a plain load). The compiler fence orders the store before the load of foreignActive in program
    // order; the hardware order is supplied by the foreign side's barrier (asymmetric build) or by a real fence here.
    OwnerEntry enterOwnerExclusive(){
        if(inside.load(std::memory_order_relaxed))return Reenter;
        inside.store(1,std::memory_order_relaxed);
#if NORTHLIGHT_GATE_ASYMMETRIC
        std::atomic_signal_fence(std::memory_order_seq_cst);
#else
        std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
        if(!foreignActive.load(std::memory_order_acquire))return Elide; /* acquire: pairs with exitForeign's release (free on x86) */
        inside.store(0,std::memory_order_release);
        ownerLocked.store(ownerLocked.load(std::memory_order_relaxed)+1,std::memory_order_relaxed);
        return Lock;
    }
    // True when the mode is now as requested. Asymmetric builds need the process-wide barrier (looked up once; a
    // runtime without it, which no supported one lacks, keeps the symmetric protocol and reports false).
    bool setExclusive(bool on){
#if NORTHLIGHT_GATE_ASYMMETRIC
        if(on&&!barrier()){return false;}
#endif
        exclusive.store(on,std::memory_order_seq_cst);return true;
    }
#if NORTHLIGHT_GATE_ASYMMETRIC
    static void (WINAPI*& barrier())(void){
        static void (WINAPI* fn)(void)=nullptr;static bool looked=false;
        if(!looked){looked=true;const HMODULE kernel=GetModuleHandleW(L"kernel32.dll");
            if(kernel)fn=reinterpret_cast<void (WINAPI*)(void)>(reinterpret_cast<void*>(GetProcAddress(kernel,"FlushProcessWriteBuffers")));}
        return fn;
    }
#endif
    void exitOwner(){inside.store(0,std::memory_order_release);}
    [[gnu::cold]] [[gnu::noinline]] void enterForeign();
    // A barrier executed on every running thread (Windows FlushProcessWriteBuffers); a plain fence elsewhere.
    [[gnu::cold]] [[gnu::noinline]] static void processBarrier(){
#if NORTHLIGHT_GATE_ASYMMETRIC
        if(auto fn=barrier())fn(); /* present: setExclusive(true) only succeeds with it */
#endif
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }
    void exitForeign(){mutex.unlock();foreignActive.fetch_sub(1,std::memory_order_release);}
};
// One TLS block per thread: the gate its innermost owning MirrorGuard holds, and its cached thread id.
struct MirrorThread {const MirrorGate* held=nullptr;std::uint32_t tid=0;};
class MirrorGuard {
    using Thread=MirrorThread;
    static inline thread_local Thread thread_;
    // Nested: the innermost enclosing guard on this thread holds the gate. Elided: the owner's fast path.
    // Reentered: the owner's elided entry further up (A -> B -> A). Locked: the owner while a foreign
    // call is in flight. Foreign: any other thread.
    enum class Mode:unsigned char {Nested,Elided,Reentered,Locked,Foreign};
    MirrorGate* gate_;
    const MirrorGate* previous_;
    Mode mode_;
public:
    // The calling thread's id, from TLS after its first use (no GetCurrentThreadId call per entry).
    static std::uint32_t threadId(){
        Thread& t=thread_;
        if(!t.tid){
#ifdef _WIN32
            t.tid=GetCurrentThreadId();
#else
            static std::atomic<std::uint32_t> next{1};t.tid=next.fetch_add(1,std::memory_order_relaxed);
#endif
        }
        return t.tid;
    }
    explicit MirrorGuard(MirrorGate& gate,MirrorSite site=MirrorSite::Device):gate_(&gate),previous_(thread_.held),mode_(Mode::Nested){
        if(kMirrorSingleGate&&previous_==&gate)return;
        const bool owner=threadId()==gate.ownerTid;
        if(!owner){gate.enterForeign();mode_=Mode::Foreign;}
        else switch(kMirrorOwnerFastPath?gate.enterOwner():MirrorGate::Lock){
        case MirrorGate::Elide:mode_=Mode::Elided;break;
        case MirrorGate::Reenter:mode_=Mode::Reentered;break;
        case MirrorGate::Lock:gate.mutex.lock();mode_=Mode::Locked;break;
        }
#if NORTHLIGHT_GATE_ELISION_COUNTERFACTUAL==3
        if(mode_!=Mode::Elided)
#endif
        thread_.held=&gate;
        gate.entered(site,NORTHLIGHT_GATE_CENSUS_BY_HELD?previous_!=nullptr:!owner);
    }
    ~MirrorGuard(){
        if(mode_==Mode::Nested)return;
        thread_.held=previous_;
#if NORTHLIGHT_GATE_ELISION_COUNTERFACTUAL==4
        mode_=threadId()!=gate_->ownerTid?Mode::Foreign:gate_->foreignActive.load(std::memory_order_seq_cst)?Mode::Locked:Mode::Elided;
#endif
        switch(mode_){
        case Mode::Elided:gate_->exitOwner();break;
        case Mode::Locked:gate_->mutex.unlock();break;
        case Mode::Foreign:gate_->exitForeign();break;
        case Mode::Reentered:case Mode::Nested:break;
        }
    }
    MirrorGuard(const MirrorGuard&)=delete;
    MirrorGuard& operator=(const MirrorGuard&)=delete;
    bool elided()const{return mode_==Mode::Elided;}
    static bool heldByThisThread(const MirrorGate& gate){return thread_.held==&gate;}
};
inline MirrorGate::MirrorGate():ownerTid(MirrorGuard::threadId()){}
inline void MirrorGate::entered(MirrorSite site,bool foreignThread){
    if(foreignThread){foreignEntry(site,MirrorGuard::threadId());return;}
    if(counting.load(std::memory_order_relaxed)){auto& n=acquired[unsigned(site)];n.store(n.load(std::memory_order_relaxed)+1,std::memory_order_relaxed);}
}
inline void MirrorGate::noteBuffer(){const std::uint32_t tid=MirrorGuard::threadId();if(tid!=ownerTid)foreignEntry(MirrorSite::Buffer,tid);}
inline void MirrorGate::noteFirst(std::atomic<std::uint32_t>& slot){if(!slot.load(std::memory_order_relaxed))slot.store(MirrorGuard::threadId(),std::memory_order_relaxed);}
// 0.3.182 (D1): announce, wait until the owner is not inside an elided call, then lock. The owner
// never waits for a foreign thread inside a call, so the wait ends when its current call returns.
inline void MirrorGate::enterForeign(){
#if NORTHLIGHT_GATE_ELISION_COUNTERFACTUAL==1
    foreignActive.fetch_add(1,std::memory_order_relaxed);
    while(inside.load(std::memory_order_relaxed)){
#else
    foreignActive.fetch_add(1,std::memory_order_seq_cst);
    if(exclusive.load(std::memory_order_seq_cst)){foreignExclusive.fetch_add(1,std::memory_order_relaxed);processBarrier();} /* 0.3.192 (CS): pairs with the owner's plain store */
    while(NORTHLIGHT_GATE_ELISION_COUNTERFACTUAL!=2&&inside.load(std::memory_order_seq_cst)){
#endif
        for(unsigned i=0;i<64&&inside.load(std::memory_order_relaxed);++i){
#if defined(__i386__)||defined(__x86_64__)
            __builtin_ia32_pause();
#elif defined(__aarch64__)
            __asm__ __volatile__("yield");
#endif
        }
#ifdef _WIN32
        SwitchToThread();
#else
        std::this_thread::yield();
#endif
    }
    mutex.lock();
}
inline void MirrorGate::foreignEntry(MirrorSite site,std::uint32_t tid){
    const std::uint32_t before=foreign[unsigned(site)].fetch_add(1,std::memory_order_relaxed);
    bool expected=false;
    if(firstClaimed.compare_exchange_strong(expected,true,std::memory_order_relaxed)){
        firstTid=tid;firstSite=unsigned(site);firstFrame=frame.load(std::memory_order_relaxed);firstReady.store(true,std::memory_order_release);}
    if(!before&&report)report(reportContext,unsigned(site),tid);
}
