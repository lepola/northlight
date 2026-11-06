#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <vector>
#include "northlight_mem.h"
extern "C" int memcmp(const void* a,const void* b,size_t n){return NorthlightMem::compare(a,b,n);}
#include "replay_constants.h"
using NorthlightReplayConstants::DirtySpan;
static inline DirtySpan reference(const float* a,const float* b,unsigned count){
    if(!count||!std::memcmp(a,b,size_t(count)*16))return {};
    unsigned first=0,last=count;
    while(first<last&&!std::memcmp(a+first*4,b+first*4,16))++first;
    while(last>first&&!std::memcmp(a+(last-1)*4,b+(last-1)*4,16))--last;
    return {first,last-first};
}
__attribute__((noinline)) static DirtySpan oldSpan(const float* a,const float* b,unsigned count){return reference(a,b,count);}
__attribute__((noinline)) static DirtySpan newSpan(const float* a,const float* b,unsigned count){
    return count>=192?NorthlightReplayConstants::Detail::knownFloatSpan(a,b,count):oldSpan(a,b,count);
}
static volatile unsigned sink=0;
int main(int argc,char**){
    unsigned random=991;
    for(unsigned test=0;test<20000;++test){
        random=random*1664525+1013904223;const unsigned count=random%257,offset=(random>>16)%4;
        std::vector<float> a(count*4+offset),b(a.size());
        for(size_t i=0;i<a.size();++i){random=random*1664525+1013904223;std::memcpy(a.data()+i,&random,4);}
        b=a;
        if(count&&test%7){random=random*1664525+1013904223;reinterpret_cast<unsigned char*>(b.data()+offset)[random%(count*16)]^=1;}
        const auto expected=reference(a.data()+offset,b.data()+offset,count);
        const auto got=NorthlightReplayConstants::Detail::knownFloatSpan(a.data()+offset,b.data()+offset,count);
        assert(expected.first==got.first&&expected.count==got.count);
        std::vector<float> uploaded=a;
        if(got.count)std::memcpy(uploaded.data()+offset+got.first*4,b.data()+offset+got.first*4,got.count*16);
        if(!b.empty())assert(!std::memcmp(uploaded.data(),b.data(),b.size()*4));
    }
    assert(!NorthlightReplayConstants::Detail::knownFloatSpan(nullptr,nullptr,0).count);
    std::puts("PASS material spans: 20000 exact arbitrary-bit/alignment/range cases; identical uploaded banks");
    if(argc>1)return 0;
    for(unsigned count:{32u,64u,128u,192u,256u})for(unsigned kind=0;kind<5;++kind){
        std::vector<float> a(count*4),b(a);const unsigned changed=kind==0?0:kind==1?4:kind==2?31:count-1;
        if(kind<4)b[changed*4]=1;else for(unsigned i=0;i<count;++i)b[i*4]=1;
        std::vector<double> times[2];
        for(unsigned round=0;round<14;++round)for(unsigned turn=0;turn<2;++turn){
            const unsigned mode=(round+turn)&1;auto start=std::chrono::steady_clock::now();
            for(unsigned repeat=0;repeat<20000;++repeat){std::atomic_signal_fence(std::memory_order_seq_cst);const auto span=mode?newSpan(a.data(),b.data(),count):oldSpan(a.data(),b.data(),count);sink=span.first+span.count;}
            times[mode].push_back(std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/20000);
        }
        for(auto& v:times)std::sort(v.begin(),v.end());
        std::printf("registers=%u case=%u oldNs=%.3f newNs=%.3f ratio=%.4f\n",count,kind,times[0][7],times[1][7],times[1][7]/times[0][7]);
    }
}
