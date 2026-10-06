#pragma once
// 0.3.192 (CS): waits of the game thread. Win32: an event waited with MsgWaitForMultipleObjectsEx(QS_SENDMESSAGE) and a
// PeekMessage(PM_NOREMOVE|PM_QS_SENDMESSAGE) pump, so a cross-thread SendMessage that DXVK's replay-thread SetWindowPos
// (Reset, fullscreen) triggers is dispatched here instead of deadlocking. Elsewhere (host tests): a condvar, with an
// optional pumpHook standing in for message dispatch. Every wait returns its elapsed nanoseconds for the diagnostics.
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#ifndef PM_QS_SENDMESSAGE
#define PM_QS_SENDMESSAGE 0x00400000   // QS_SENDMESSAGE<<16; older MinGW headers lack it
#endif
#endif

namespace NorthlightStream {
constexpr std::uint32_t kWaitInfinite=0xFFFFFFFFu;
inline std::uint64_t nowNs(){return (std::uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}

// True while this thread is inside waitPumped(): a recorder entry made from there (a message handler that DXVK's pump
// dispatched) is nested and must never block on the replay thread (see runSync in command_stream.inl).
inline thread_local bool inPumpedWait=false;
// Test seam, non-Win32 only: called by waitPumped() between 1 ms slices, where Win32 would dispatch sent messages.
inline void (*pumpHook)()=nullptr;

struct WaitResult {bool signaled;std::uint64_t ns;};
struct PumpScope {bool saved;PumpScope():saved(inPumpedWait){inPumpedWait=true;}~PumpScope(){inPumpedWait=saved;}PumpScope(const PumpScope&)=delete;PumpScope& operator=(const PumpScope&)=delete;};

#ifdef _WIN32
class Event {
    HANDLE h_;
public:
    explicit Event(bool manualReset=false):h_(CreateEventW(nullptr,manualReset?TRUE:FALSE,FALSE,nullptr)){}
    ~Event(){if(h_)CloseHandle(h_);}
    Event(const Event&)=delete;Event& operator=(const Event&)=delete;
    void set(){if(h_)SetEvent(h_);}
    void reset(){if(h_)ResetEvent(h_);}
    WaitResult wait(std::uint32_t ms=kWaitInfinite){
        const auto t0=nowNs();
        if(!h_){Sleep(1);return {false,nowNs()-t0};}   // no event object: degrade to polling, callers re-check their condition
        const bool ok=WaitForSingleObject(h_,ms)==WAIT_OBJECT_0;
        return {ok,nowNs()-t0};
    }
    WaitResult waitPumped(std::uint32_t ms=kWaitInfinite){
        const auto t0=nowNs();
        if(!h_){Sleep(1);return {false,nowNs()-t0};}
        PumpScope scope;bool ok=false;
        for(;;){
            std::uint32_t left=ms;
            if(ms!=kWaitInfinite){const auto spent=(nowNs()-t0)/1000000u;if(spent>=ms)break;left=(std::uint32_t)(ms-spent);}
            const DWORD r=MsgWaitForMultipleObjectsEx(1,&h_,left,QS_SENDMESSAGE,0);
            if(r==WAIT_OBJECT_0){ok=true;break;}
            if(r==WAIT_OBJECT_0+1){MSG m;PeekMessageW(&m,nullptr,0,0,PM_NOREMOVE|PM_QS_SENDMESSAGE);continue;}   // dispatches sent messages only
            break;   // timeout or failure
        }
        return {ok,nowNs()-t0};
    }
};
#else
class Event {
    std::mutex m_;std::condition_variable cv_;bool flag_=false;const bool manual_;
    bool take(){if(!flag_)return false;if(!manual_)flag_=false;return true;}
public:
    explicit Event(bool manualReset=false):manual_(manualReset){}
    Event(const Event&)=delete;Event& operator=(const Event&)=delete;
    void set(){{std::lock_guard<std::mutex> l(m_);flag_=true;}cv_.notify_all();}
    void reset(){std::lock_guard<std::mutex> l(m_);flag_=false;}
    WaitResult wait(std::uint32_t ms=kWaitInfinite){
        const auto t0=nowNs();std::unique_lock<std::mutex> l(m_);bool ok;
        if(ms==kWaitInfinite){cv_.wait(l,[&]{return flag_;});ok=take();}
        else{cv_.wait_for(l,std::chrono::milliseconds(ms),[&]{return flag_;});ok=take();}
        return {ok,nowNs()-t0};
    }
    WaitResult waitPumped(std::uint32_t ms=kWaitInfinite){
        const auto t0=nowNs();PumpScope scope;bool ok=false;
        for(;;){
            {std::unique_lock<std::mutex> l(m_);cv_.wait_for(l,std::chrono::milliseconds(1),[&]{return flag_;});if(take()){ok=true;break;}}
            if(pumpHook)pumpHook();
            if(ms!=kWaitInfinite&&(nowNs()-t0)/1000000u>=ms)break;
        }
        return {ok,nowNs()-t0};
    }
};
#endif
}
