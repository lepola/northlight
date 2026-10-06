#pragma once
// 0.3.192 (CS): the record gate. The queue's producer side (reserve/commit, StreamState, the proxies' lock state) is single-threaded by design: the game
// thread. Every game-facing entry that records, answers from StreamState or syncs holds a RecordGuard; the owner (the thread that created the device) costs
// a thread-id compare, a plain store and a load (no mutex, no locked instruction); any other thread (never expected; correctness must not depend on that)
// is serialized behind a mutex and counted in foreignEntries. The protocol is the owner fast path of MirrorGate (mirror_guard.h) in its asymmetric
// form: the owner stores inside=1 and then reads foreignActive; a foreign thread announces itself (foreignActive++), forces a barrier on every core
// (FlushProcessWriteBuffers, where it exists; else the owner pays a real fence) and then waits for inside==0 under the mutex. Either the owner sees the
// announcement (it resets inside and takes the mutex too) or the foreign thread sees inside==1 (it waits for the call to end).
// Guards nest per thread (a message handler dispatched by a pumped wait enters again; Present calls other gated code): the thread-local `held` makes an
// inner entry on the thread that already holds the gate a no-op. The replay thread is exempt: it only runs while the game thread waits for it, and a
// spin on `inside` there would deadlock against the owner's pumped wait. One gate per process, like the stream itself (a global: a guard exit must be
// able to run after the last Release has destroyed the device).
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include "stream_stats.h"
#include "stream_wait.h"

namespace NorthlightStream {
class RecordGate {
    struct Tls {const RecordGate* held=nullptr;std::uint32_t id=0;};
    static Tls& tls(){static thread_local Tls t;return t;}
    static std::uint32_t idOf(Tls& t){
        if(!t.id){static std::atomic<std::uint32_t> next{1};t.id=next.fetch_add(1,std::memory_order_relaxed);}
        return t.id;
    }
    std::atomic<std::uint32_t> owner_{0},exempt_{0};   // thread ids (see idOf); 0 = none
    std::atomic<unsigned> inside_{0},foreignActive_{0};
    std::atomic<Counter*> foreign_{nullptr};
    std::mutex mutex_;
    bool fenceFree_=false;   // the barrier exists (Windows): the owner's entry needs no hardware fence
    [[gnu::cold]] [[gnu::noinline]] void barrier(){
#ifdef _WIN32
        if(auto fn=flushFn())fn();
#endif
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }
#ifdef _WIN32
    static void (WINAPI* flushFn())(void){
        static void (WINAPI* fn)(void)=[]{const HMODULE k=GetModuleHandleW(L"kernel32.dll");
            return k?reinterpret_cast<void (WINAPI*)(void)>(reinterpret_cast<void*>(GetProcAddress(k,"FlushProcessWriteBuffers"))):nullptr;}();
        return fn;
    }
#endif
public:
    RecordGate(){
#ifdef _WIN32
        fenceFree_=flushFn()!=nullptr;
#endif
    }
    // The calling thread becomes the owner (the device's creator); `counter` receives the foreign entries (null: none counted).
    void setOwner(Counter* counter){owner_.store(idOf(tls()),std::memory_order_relaxed);foreign_.store(counter,std::memory_order_relaxed);}
    void clearOwner(){owner_.store(0,std::memory_order_relaxed);foreign_.store(nullptr,std::memory_order_relaxed);}
    void setExempt(){exempt_.store(idOf(tls()),std::memory_order_relaxed);}   // the replay thread, from its start
    void clearExempt(){exempt_.store(0,std::memory_order_relaxed);}
    bool isOwnerThread(){return idOf(tls())==owner_.load(std::memory_order_relaxed);}

    class Guard {
        RecordGate* g_;const RecordGate* prev_;enum Mode:unsigned char{Nested,Elided,Locked,Foreign} mode_=Nested;
    public:
        explicit Guard(RecordGate& g):g_(&g),prev_(tls().held){
            Tls& t=tls();if(prev_==&g)return;
            const std::uint32_t id=idOf(t);
            if(id==g.owner_.load(std::memory_order_relaxed)){
                g.inside_.store(1,std::memory_order_relaxed);
                if(g.fenceFree_)std::atomic_signal_fence(std::memory_order_seq_cst);else std::atomic_thread_fence(std::memory_order_seq_cst);
                if(!g.foreignActive_.load(std::memory_order_acquire))mode_=Elided;
                else{g.inside_.store(0,std::memory_order_release);g.mutex_.lock();mode_=Locked;}   // a foreign call is in flight: take the mutex like it does
            }else if(id==g.exempt_.load(std::memory_order_relaxed)||!g.owner_.load(std::memory_order_relaxed))return;   // the replay thread; or no stream owner at all
            else{g.enterForeign();mode_=Foreign;}
            t.held=&g;
        }
        ~Guard(){
            if(mode_==Nested)return;
            tls().held=prev_;
            if(mode_==Elided)g_->inside_.store(0,std::memory_order_release);
            else{g_->mutex_.unlock();if(mode_==Foreign)g_->foreignActive_.fetch_sub(1,std::memory_order_release);}
        }
        Guard(const Guard&)=delete;Guard& operator=(const Guard&)=delete;
    };
private:
    [[gnu::cold]] [[gnu::noinline]] void enterForeign(){
        if(Counter* c=foreign_.load(std::memory_order_relaxed))add(*c);
        foreignActive_.fetch_add(1,std::memory_order_seq_cst);
        barrier();
        mutex_.lock();
        while(inside_.load(std::memory_order_acquire))std::this_thread::yield();   // the owner's elided call ends; its next entry sees foreignActive_ and takes the mutex
    }
};
inline RecordGate recordGate;
using RecordGuard=RecordGate::Guard;
}
