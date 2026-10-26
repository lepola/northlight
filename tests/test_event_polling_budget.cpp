#include <cassert>
#include <initializer_list>
#include <cstdint>
#include <cstdio>
using HRESULT=int;
constexpr HRESULT S_OK=0,S_FALSE=1,E_FAIL=-1;
#include "query_poll.h"
int main(){
    using namespace NorthlightStream;
    for(uint64_t duration:{4000000ull,8000000ull,14000000ull}){
        uint64_t tick=0;unsigned calls=0,yields=0;
        auto now=[&]{tick+=100;return tick;};auto poll=[&]{return tick>=duration?S_OK:S_FALSE;};
        assert(waitEventQuery(poll,now,[&]{++yields;},calls,kEventFlushWaitNs)==S_OK);
        assert(tick>=duration&&tick<duration+60000&&calls<400&&yields>0);
        tick=0;assert(waitEventQuery(poll,now,[]{},calls,2000000)==S_FALSE); // the short deadline reproduces premature pending
    }
    unsigned calls=0;uint64_t tick=0;auto now=[&]{return tick+=100;};
    assert(waitEventQuery([]{return S_FALSE;},now,[]{},calls,kEventFlushWaitNs)==S_FALSE&&tick>=kEventFlushWaitNs&&calls<400);
    unsigned clocks=0,yields=0;
    assert(waitEventQuery([]{return S_FALSE;},[&]{++clocks;return uint64_t(7);},[&]{++yields;},calls,kEventFlushWaitNs)==S_FALSE&&clocks<=65537&&yields<=65536);
    tick=5;assert(waitEventQuery([]{return S_FALSE;},[&]{return tick--;},[]{},calls,kEventFlushWaitNs)==S_FALSE&&calls==1);
    assert(waitEventQuery([]{return S_OK;},now,[]{assert(false);},calls,kEventFlushWaitNs)==S_OK&&calls==1);
    assert(waitEventQuery([]{return E_FAIL;},now,[]{assert(false);},calls,kEventFlushWaitNs)==E_FAIL&&calls==1);
    std::puts("EVENT_POLLING_PASS normal4_8_14ms=complete shortDeadline=pending stalledClock=bounded nonmonotonic=bounded immediateStatus=exact");
}
