#include <windows.h>
#include "command_queue.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>
using namespace NorthlightStream;
static unsigned pumped=0;
static LRESULT CALLBACK windowProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_APP){assert(inPumpedWait);++pumped;return 7;}
    return DefWindowProcW(h,m,w,l);
}
int main(int argc,char** argv){
    {Queue idle;assert(!idle.nextTimed(0));assert(!idle.nextTimed(1));assert(get(idle.stats.consumerSleeps)==2);
     idle.interrupt();assert(!idle.nextTimed(10000));assert(get(idle.stats.consumerSleeps)==2);}
    WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.lpszClassName=L"NorthlightQueueWaitTest";wc.hInstance=GetModuleHandleW(nullptr);
    assert(RegisterClassW(&wc));
    HWND window=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);assert(window);
    Queue q;unsigned result=0;std::atomic<bool> ready{false};
    bool timed=false,seen=false;
    for(int i=1;i<argc;++i){timed|=!std::strcmp(argv[i],"timed");seen|=!std::strcmp(argv[i],"seen");}
    std::thread consumer([&]{
        ready.store(true);
        for(;;){
            const auto* h=timed?q.nextTimed(1):q.next(true);if(!h)continue;
            unsigned value;std::memcpy(&value,h+1,sizeof value);
            if(h->id==2){Sleep(2);assert(SendMessageW(window,WM_APP,0,0)==7);}
            result=value;const bool stop=h->id==3;q.retire(h);if(stop)return;
        }
    });
    while(!ready.load())std::this_thread::yield();
    auto run=[&](unsigned id,unsigned value){
        std::memcpy(q.reserve(id,sizeof value,kFlagWaitTarget),&value,sizeof value);q.commit();
        if(seen&&id==2){
            q.publish();
            const auto deadline=GetTickCount64()+5000;
            DWORD status;
            do{status=GetQueueStatus(QS_SENDMESSAGE);if(HIWORD(status)&QS_SENDMESSAGE)break;Sleep(1);}while(GetTickCount64()<deadline);
            assert(HIWORD(status)&QS_SENDMESSAGE); // marks the pending SendMessage as seen, without dispatching it
        }
        q.waitReplayed(q.recordedSeq());assert(result==value);
    };
    for(unsigned i=0;i<1000;++i)run(1,i);
    std::vector<std::uint64_t> ns;ns.reserve(10000);const auto start=nowNs();
    for(unsigned i=0;i<10000;++i){const auto t=nowNs();run(1,i);ns.push_back(nowNs()-t);}
    const auto elapsed=nowNs()-start;
    for(unsigned i=0;i<8;++i)run(2,i); // The consumer is blocked in SendMessage until the producer pumps.
    run(3,0);consumer.join();assert(pumped==8);
    DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
    std::sort(ns.begin(),ns.end());
    std::printf("{\"timed_consumer\":%s,\"seen_sendmessages\":%s,\"roundtrips\":10000,\"mean_us\":%.3f,\"p50_us\":%.3f,\"p95_us\":%.3f,\"p99_us\":%.3f,\"max_us\":%.3f,\"pumped_sendmessages\":%u}\n",timed?"true":"false",seen?"true":"false",double(elapsed)/10000/1000,double(ns[5000])/1000,double(ns[9500])/1000,double(ns[9900])/1000,double(ns.back())/1000,pumped);
}
