#include "northlight_mem.h"
extern "C" __attribute__((noinline)) int memcmp(const void* a,const void* b,size_t n){return NorthlightMem::compare(a,b,n);}
#include "replay_constant_ranges.h"
#include "range_reference.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
static unsigned rng=98713;
static unsigned randomWord(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
template<class T,size_t N,size_t W> static void check(){
    NorthlightReplayConstants::BoundedDirtyRegisters<T,N,W> now;ReplayRangeReference::BoundedDirtyRegisters<T,N,W> old;
    T data[N*W]={},patch[N*W]={},backend[N*W]={};bool known[N]={};
    for(unsigned test=0;test<30000;++test){if(test%317==0){now.reset();old.reset();std::fill(std::begin(known),std::end(known),false);}
        for(unsigned j=0;j<test%9;++j){unsigned bits=randomWord(),at=randomWord()%(N*W);std::memcpy(data+at,&bits,4);bits=randomWord();std::memcpy(patch+at,&bits,4);}
        unsigned first=randomWord()%N,count=randomWord()%(N-first+1);if(test%17==0){first=0;count=N;}
        unsigned overlay=first+(count?randomWord()%(count+1):0),length=randomWord()%(first+count-overlay+1);
        NorthlightReplayConstants::DirtySpan a,b;const bool useOverlay=test%2;
        if(useOverlay){a=old.updateOverlay(data,{first,count},{overlay,length},patch);b=now.updateOverlay(data,{first,count},{overlay,length},patch);}
        else{a=old.update(data,{first,count});b=now.update(data,{first,count});}
        assert(a.first==b.first&&a.count==b.count);
        size_t expectedFirst=N,expectedLast=0;
        for(unsigned reg=first;reg<first+count;++reg){const T* source=useOverlay&&reg>=overlay&&reg<overlay+length?patch+(reg-overlay)*W:data+reg*W;
            if(!known[reg]||std::memcmp(backend+reg*W,source,W*sizeof(T))){expectedFirst=std::min(expectedFirst,size_t(reg));expectedLast=reg+1;}}
        if(expectedFirst==N)assert(!b.count);else assert(b.first==expectedFirst&&b.count==expectedLast-expectedFirst);
        for(unsigned reg=b.first;reg<b.first+b.count;++reg){known[reg]=true;std::memcpy(backend+reg*W,now.data()+reg*W,W*sizeof(T));}
        for(unsigned reg=0;reg<N;++reg)if(known[reg])assert(!std::memcmp(backend+reg*W,old.data()+reg*W,W*sizeof(T))&&!std::memcmp(backend+reg*W,now.data()+reg*W,W*sizeof(T)));
    }
    std::printf("PASS replay validity: count=%zu width=%zu cases=30000 exactSpans=1 unknownAndKnownBytes=1 reset=1 overlay=1\n",N,W);
}
static volatile unsigned sink=0;
int main(){check<float,256,4>();check<float,193,4>();check<int,16,4>();check<int,16,1>();
    for(unsigned overlay=0;overlay<2;++overlay){double ns[2]={};unsigned calls[2]={};float data[1024]={},patch[16]={};
        NorthlightReplayConstants::BoundedDirtyRegisters<float,256,4> now;ReplayRangeReference::BoundedDirtyRegisters<float,256,4> old;now.update(data,{0,256});old.update(data,{0,256});
        for(unsigned round=0;round<24;++round)for(unsigned turn=0;turn<2;++turn){unsigned mode=(round+turn)&1;auto start=std::chrono::steady_clock::now();unsigned sum=0;
            for(unsigned i=0;i<10000;++i){std::atomic_signal_fence(std::memory_order_seq_cst);data[128]=float(i/4);patch[0]=float(i/7);NorthlightReplayConstants::DirtySpan s;
                if(overlay)s=mode?now.updateOverlay(data,{0,256},{2,4},patch):old.updateOverlay(data,{0,256},{2,4},patch);
                else s=mode?now.update(data,{0,256}):old.update(data,{0,256});sum+=s.count;}
            calls[mode]+=sum;sink=sum;ns[mode]+=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/10000;}
        assert(calls[0]==calls[1]);std::printf("replayValidity overlay=%u oldNs=%.3f newNs=%.3f ratio=%.4f sameUploads=1\n",overlay,ns[0]/24,ns[1]/24,ns[1]/ns[0]);}
}
