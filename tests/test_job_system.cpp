// 0.3.200 (jobs): native test of job_system.h (built by test_job_system.py at -O2, ASan+UBSan and TSan).
#include "job_system.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>

static std::atomic<long> allocations{0};
void* operator new(std::size_t n){allocations.fetch_add(1,std::memory_order_relaxed);if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}

using NorthlightJobs::System;using NorthlightJobs::Counter;
struct Add {std::atomic<long>* sum;long value;void operator()(){sum->fetch_add(value,std::memory_order_relaxed);}};

int main(){
    // worker count: clamp(hw-3,1,4); 0 (unknown) -> 1
    // worker count from the cores left after the replay thread (hardware-1 with the stream): clamp(cores-2,1,4) = clamp(hw-3,1,4); 0 (unknown) -> 1
    assert(NorthlightJobs::workerCount(0)==1&&NorthlightJobs::workerCount(1)==1&&NorthlightJobs::workerCount(3)==1&&NorthlightJobs::workerCount(4)==2
        &&NorthlightJobs::workerCount(5)==3&&NorthlightJobs::workerCount(6)==4&&NorthlightJobs::workerCount(63)==4);
    {   // no workers: every kick runs inline, results identical
        System s;Counter c;std::atomic<long> sum{0};Add a{&sum,3};
        for(int i=0;i<10;++i)assert(!s.kick(c,a));
        assert(c.done()&&s.wait(c)&&sum==30);const auto st=s.take();assert(st.jobs==10&&st.inlineJobs==10);
    }
    {   // correctness: many rounds, each result written by exactly one job; a full queue falls back inline
        System s;assert(s.start(7)&&s.workers()==4&&s.started());
        std::vector<long> out(1000,0);std::vector<Add> adds;std::atomic<long> sum{0};
        struct Slot {std::vector<long>* out;std::size_t i;void operator()(){(*out)[i]=long(i)*long(i);}};
        std::vector<Slot> slots;for(std::size_t i=0;i<out.size();++i)slots.push_back({&out,i});
        unsigned inlined=0;
        for(int round=0;round<50;++round){Counter c;
            for(auto& slot:slots)inlined+=!s.kick(c,slot);
            assert(s.wait(c)&&c.done());
            for(std::size_t i=0;i<out.size();++i){assert(out[i]==long(i)*long(i));out[i]=0;}
        }
        assert(inlined>0); /* 1000 kicks into 64 cells: some ran inline */
        Counter c;Add a{&sum,1};for(int i=0;i<200;++i)s.kick(c,a);s.wait(c);assert(sum==200);
        std::printf("correctness ok inlined=%u\n",inlined);
    }
    {   // wait-help: the only worker is held by a blocking job; the waiter runs the other job itself
        System s;assert(s.start(3)&&s.workers()==1);
        std::atomic<bool> started{false},release{false};std::thread::id ranOn;
        auto block=[&]{started=true;while(!release.load())std::this_thread::yield();};
        auto other=[&]{ranOn=std::this_thread::get_id();release=true;};
        Counter a,b;assert(s.kick(a,block));while(!started.load())std::this_thread::yield();
        assert(s.kick(b,other));s.timing(true);assert(s.wait(b));
        assert(ranOn==std::this_thread::get_id());assert(s.wait(a));
        const auto st=s.take();assert(st.jobs==2&&st.helpedNs>0&&st.waitNs>0);
        std::printf("wait-help ok\n");
    }
    {   // exception safety: a throwing job completes its counter as failed; the pool keeps working
        System s;s.start(7);Counter c;std::atomic<long> sum{0};Add a{&sum,1};
        auto boom=[]{throw std::runtime_error("job");};
        for(int i=0;i<8;++i){s.kick(c,a);s.kick(c,boom);}
        assert(!s.wait(c)&&c.done()&&c.failed()&&sum==8);
        c.clearFailure();for(int i=0;i<8;++i)s.kick(c,a);assert(s.wait(c)&&sum==16);
        System none;Counter d;assert(!none.kick(d,boom)&&d.done()&&!none.wait(d)); /* inline throw: the same contract */
        std::printf("exceptions ok\n");
    }
    {   // nested: a job kicks and waits on its own sub-jobs (help on a worker), no deadlock
        System s;s.start(7);std::atomic<long> sum{0};
        struct Parent {System* s;std::atomic<long>* sum;void operator()(){Counter sub;Add a{sum,1};for(int i=0;i<16;++i)s->kick(sub,a);s->wait(sub);}};
        std::vector<Parent> parents(16,Parent{&s,&sum});Counter c;for(auto& p:parents)s.kick(c,p);
        assert(s.wait(c)&&sum==256);std::printf("nested ok\n");
    }
    {   // no allocation per job in the steady state
        System s;s.start(7);std::atomic<long> sum{0};Add a{&sum,1};Counter warm;s.kick(warm,a);s.wait(warm);
        const long before=allocations.load();
        for(int round=0;round<200;++round){Counter c;for(int i=0;i<16;++i)s.kick(c,a);s.timing(round&1);s.wait(c);s.take();}
        assert(allocations.load()==before&&sum==1+200*16);std::printf("allocation-free ok\n");
    }
    {   // shutdown: queued jobs still run, threads joined; later kicks run inline; idempotent
        std::atomic<long> sum{0};
        {System s;s.start(7);Counter c;
         auto slow=[&]{std::this_thread::sleep_for(std::chrono::microseconds(200));sum.fetch_add(1);};
         for(int i=0;i<40;++i)s.kick(c,slow);
         s.shutdown();assert(c.done()&&sum==40&&s.workers()==0&&!s.started());
         Add a{&sum,1};assert(!s.kick(c,a)&&sum==41);s.shutdown();}
        {System s;s.start(7);} /* destructor with idle workers */
        {Counter c;Add a{&sum,1};System s;s.start(7);for(int i=0;i<30;++i)s.kick(c,a);} /* destructor drains (the job's objects outlive the pool) */
        assert(sum==71);std::printf("shutdown ok\n");
    }
    {   // stress: several owners' counters interleaved, results exact (TSan: no race)
        System s;s.start(7);
        for(int round=0;round<300;++round){std::atomic<long> x{0},y{0};Counter cx,cy;Add ax{&x,2},ay{&y,3};
            for(int i=0;i<20;++i){s.kick(cx,ax);s.kick(cy,ay);}
            s.wait(cy);assert(y==60);s.wait(cx);assert(x==40);}
        std::printf("stress ok\n");
    }
    std::printf("PASS job system\n");
    return 0;
}
