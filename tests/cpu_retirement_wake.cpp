#include "cpu_retirement.h"
#include <atomic>
#include <cassert>
#include <cstdio>
struct Owner {
    std::atomic<bool>* entered=nullptr;std::atomic<bool>* release=nullptr;
    std::atomic<unsigned>* completed=nullptr;std::thread::id producer;
    ~Owner(){assert(std::this_thread::get_id()!=producer);if(entered){entered->store(true);while(!release->load())std::this_thread::yield();}completed->fetch_add(1);}
};
int main(){
    unsigned cases=0;
    for(bool generation:{false,true}){
        NorthlightStreaming::CpuRetirement queue;
        std::atomic<unsigned> completed{0};
        for(unsigned burst=0;burst<40;++burst){
            std::atomic<bool> entered{false},release{false};
            auto first=std::make_shared<Owner>();first->entered=&entered;first->release=&release;first->completed=&completed;first->producer=std::this_thread::get_id();
            assert(generation?queue.retireGeneration(first,1):queue.retire(first,1));assert(!first);
            while(!entered.load())std::this_thread::yield();
            for(unsigned i=0;i<16;++i){auto p=std::make_shared<Owner>();p->completed=&completed;p->producer=std::this_thread::get_id();assert(generation?queue.retireGeneration(p,1):queue.retire(p,1));assert(!p);}
            release.store(true);assert(queue.waitIdle(5000));assert(completed.load()==(burst+1)*17);++cases;
        }
    }
    std::printf("CPU_RETIREMENT_WAKE_PASS bursts=%u owners=%u noLostWake=1 offProducer=1\n",cases,cases*17);
}
