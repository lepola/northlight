#include <cstdint>
#include <cassert>
#include <cstdio>
#include <algorithm>
using HRESULT=std::int32_t;
constexpr HRESULT S_OK=0,S_FALSE=1,E_FAIL=-1;
#include "query_poll.h"
#include "stream_stats.h"
void replayAttribution(){
    NorthlightStream::ReplayFrameProfile p;
    p.begin(false,127);p.record(4,900);assert(!p.enabled&&!p.calls&&!p.totalNs);
    p.recordUpload(512,10,20,30,false,false,4);assert(!p.upload.calls&&!p.upload.clockReads);
    p.begin(true,126);p.record(4,900);assert(!p.enabled&&!p.calls);
    p.begin(true,127);assert(p.enabled&&p.frame==127);
    p.record(4,300);p.record(7,200);p.record(4,400);p.record(512,10000);
    assert(p.calls==3&&p.totalNs==900&&p.maxNs==400&&p.maxId==4);
    assert(p.entries[4].calls==2&&p.entries[4].ns==700&&p.entries[7].ns==200);
    p.recordUpload(256,10,20,30,false,false,4);p.recordUpload(128,40,0,0,true,false,2);p.recordUpload(64,15,0,0,false,true,2);
    assert(p.upload.calls==3&&p.upload.bytes==448&&p.upload.skipped==1&&p.upload.failed==1&&p.upload.clockReads==8);
    assert(p.upload.lockNs==65&&p.upload.copyNs==20&&p.upload.unlockNs==30&&p.upload.maxLockNs==40&&p.upload.maxCopyNs==20&&p.upload.maxUnlockNs==30);
    p.begin(true,254);assert(p.enabled&&!p.calls&&!p.totalNs&&!p.maxNs);
    assert(!p.upload.calls&&!p.upload.bytes&&!p.upload.clockReads);
    for(const auto& e:p.entries)assert(!e.calls&&!e.ns);
    p.record(511,1);assert(p.calls==1&&p.maxId==511);
    unsigned coverage[256]={};
    for(unsigned frame=1;frame<=16;++frame){
        p.begin(true,127*frame);unsigned timed=0;
        for(unsigned call=0;call<264;++call){const bool take=p.sample(4,true);timed+=take;if(call<8)assert(take);else coverage[call-8]+=take;if(take)p.record(4,10);}
        for(unsigned call=0;call<100;++call){assert(p.sample(7,false));p.record(7,20);}
        assert(timed==24&&p.calls==124&&p.observed==364&&p.entries[4].observed==264&&p.entries[4].calls==24);
        assert(p.totalNs==2240); // Actual measured subset, never extrapolated.
    }
    for(auto count:coverage)assert(count==1);
    p.begin(false,127);assert(!p.sample(4,true)&&!p.sample(7,false));
    p.begin(true,127);assert(!p.sample(512,true)&&p.observed==0);
    std::puts("REPLAY_PROFILE_PASS prefix=8 stride=16 rotatingCoverage=exact drawsUploadsWaits=full measuredSubset=unscaled");
}
void waitAttribution(){
    // Operational GPU attribution excludes the queue trip, immediate answers
    // and failed queries; it also works without QueryWaitProfile diagnostics.
    std::uint64_t pending=99,clock=1000000;unsigned calls=0;
    const auto now=[&]{clock+=1000;return clock;};
    assert(NorthlightStream::waitEventQuery([]{return S_OK;},now,[]{},calls,2000000,&pending)==S_OK&&pending==0&&clock==1000000);
    assert(NorthlightStream::waitEventQuery([&]{return clock>=1400000?S_OK:S_FALSE;},now,[]{},calls,2000000,&pending)==S_OK);
    assert(pending>=400000&&pending<452000);
    clock=0;
    assert(NorthlightStream::waitEventQuery([]{return S_FALSE;},now,[]{},calls,2000000,&pending)==S_FALSE&&pending>=2000000&&pending<2002000);
    clock=0;
    assert(NorthlightStream::waitEventQuery([&]{return clock>=400000?E_FAIL:S_FALSE;},now,[]{},calls,2000000,&pending)==E_FAIL&&pending==0);
    assert(NorthlightStream::waitEventQuery([]{return S_FALSE;},[]{return std::uint64_t(0);},[]{},calls,2000000,&pending)==S_FALSE&&pending==0);
    NorthlightStream::QueryWaitProfile s;
    s.record(1,false,true,S_OK,780,100,200,800,1000);
    assert(s.calls==1&&s.flush==1&&!s.errors&&!s.pending&&!s.unissued&&s.waitNs==780);
    assert(s.beforeNs==100&&s.replayNs==600&&s.afterNs==200&&s.tripNs==900&&s.maxTripNs==900);
    s.record(0,true,true,S_FALSE,40,2000,2005,2040,2050);
    assert(s.calls==2&&s.flush==1&&s.unissued==1&&s.pending==1&&!s.errors&&s.waitNs==820);
    assert(s.beforeNs+s.replayNs+s.afterNs==s.tripNs&&s.tripNs==950&&s.maxTripNs==900);
    // Nested/refused tasks have no replay timestamps. A backwards clock cannot
    // turn into unsigned multi-year latency; status/call counts remain visible.
    s.record(1,false,false,E_FAIL,0,3000,0,0,3010);
    s.record(1,false,true,E_FAIL,12,4000,3999,4010,4020);
    s.record(1,false,true,S_OK,0,5000,5002,5001,5003);
    s.record(1,false,true,S_OK,0,6000,6001,6002,6000);
    assert(s.calls==6&&s.errors==2&&s.tripNs==950&&s.maxTripNs==900);
    s={};assert(!s.calls&&!s.waitNs&&!s.tripNs&&!s.maxTripNs);
}
int main(){
    {
        NorthlightStream::EventFlushCadence cadence;
        unsigned flushes=0;
        for(std::uint64_t now=0;now<16000000;now+=50000)flushes+=cadence.flags(now);
        assert(flushes==16); // Previously 320 driver flush requests.
        assert(cadence.flags(0)==1&&cadence.flags(0)==0); // clock rollback/stall
        NorthlightStream::EventFlushCadence fresh;
        assert(fresh.flags(0)==1); // retry/new generation starts with a flush
    }
    replayAttribution();
    waitAttribution();
    std::uint64_t worst=0;unsigned maxCalls=0;
    for(std::uint64_t ready=0;ready<1700000;ready+=1000){
        std::uint64_t clock=0;unsigned calls=0;
        const auto hr=NorthlightStream::waitEventQuery([&]{return clock>=ready?S_OK:S_FALSE;},[&]{clock+=1000;return clock;},[]{},calls);
        assert(hr==S_OK&&clock>=ready);worst=std::max(worst,clock-ready);maxCalls=std::max(maxCalls,calls);
    }
    std::uint64_t clock=0;unsigned calls=0;
    assert(NorthlightStream::waitEventQuery([]{return S_FALSE;},[&]{clock+=1000;return clock;},[]{},calls)==S_FALSE);
    assert(clock>=2000000&&clock<2002000&&calls<=45);
#ifndef NORTHLIGHT_QUERY_LATENCY_BASELINE
    assert(worst<=52000);
#endif
    assert(NorthlightStream::waitEventQuery([]{return E_FAIL;},[]{return std::uint64_t(0);},[]{},calls)==E_FAIL&&calls==1);
    assert(NorthlightStream::waitEventQuery([]{return S_FALSE;},[]{return std::uint64_t(0);},[]{},calls)==S_FALSE&&calls==1);
    std::printf("QUERY_POLL_PASS readySweep=1700 worstDetectionNs=%llu maxDriverPolls=%u pendingBudgetMs=2 error=preserved stalledClock=bounded\n",static_cast<unsigned long long>(worst),maxCalls);
    // Normal GPU completion after the old 2 ms deadline must not escape into
    // the game's Sleep(0) loop. Exercise the production FLUSH budget separately.
    worst=0;maxCalls=0;
    for(std::uint64_t ready=0;ready<15900000;ready+=1000){
        clock=0;
        const auto hr=NorthlightStream::waitEventQuery([&]{return clock>=ready?S_OK:S_FALSE;},[&]{clock+=1000;return clock;},[]{},calls,NorthlightStream::kEventFlushWaitNs);
        assert(hr==S_OK&&clock>=ready);worst=std::max(worst,clock-ready);maxCalls=std::max(maxCalls,calls);
    }
    assert(worst<=52000&&maxCalls<=322);
    clock=0;
    assert(NorthlightStream::waitEventQuery([&]{return clock>=12000000?S_OK:S_FALSE;},[&]{clock+=100;return clock;},[]{},calls,NorthlightStream::kEventFlushWaitNs)==S_OK);
    assert(clock>=12000000&&clock<12050200&&calls<=245); // More than 65,536 live clock checks must not expire the wait.
    clock=0;
    assert(NorthlightStream::waitEventQuery([]{return S_FALSE;},[&]{clock+=1000;return clock;},[]{},calls,NorthlightStream::kEventFlushWaitNs)==S_FALSE);
    assert(clock>=16000000&&clock<16002000&&calls<=325);
    assert(NorthlightStream::waitEventQuery([]{return S_FALSE;},[]{return std::uint64_t(0);},[]{},calls,NorthlightStream::kEventFlushWaitNs)==S_FALSE&&calls==1);
    clock=0;
    assert(NorthlightStream::waitEventQuery([&]{return clock>=6000000?E_FAIL:S_FALSE;},[&]{clock+=1000;return clock;},[]{},calls,NorthlightStream::kEventFlushWaitNs)==E_FAIL);
    assert(clock>=6000000&&clock<6052000);
    std::printf("QUERY_FLUSH_PASS readySweep=15900 worstDetectionNs=%llu maxDriverPolls=%u pendingBudgetMs=16 delayedError=preserved stalledClock=bounded\n",static_cast<unsigned long long>(worst),maxCalls);
}
