#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace NorthlightStream {
// Boundary observations, not an attribution of the entire interval to a thread
// or a blocking call. Producer counters can include work outside the interval.
struct IdlePoint {
    std::uint64_t recorded=0,published=0,replayed=0,waitSeq=0;
    std::uint64_t syncNs=0,presentNs=0,backpressureNs=0,recordNs=0,lockNs=0,snapshotNs=0,publishes=0;
    std::uint64_t frame=0,presents=0;std::uint32_t epoch=0;std::int32_t cooperative=0;
    bool known=false,consistent=false,backpressure=false,express=false,contextKnown=false;
    unsigned waits()const{return (waitSeq?1u:0u)|(backpressure?2u:0u)|(express?4u:0u);}
    const char* queueState()const{
        if(!known||!consistent||replayed>published||published>recorded)return "unknown";
        return published>replayed?"published":recorded>published?"unpublished":"empty";
    }
};
struct IdleGap {
    IdlePoint first,last;
    std::uint64_t startNs=0,endNs=0,waitNs=0;unsigned segments=0,waitsSeen=0;
    std::uint16_t before=0xffff,after=0xffff;bool interrupted=false;
    std::uint64_t wallNs()const{return endNs>=startNs?endNs-startNs:0;}
};
struct IdleReport {unsigned frames=0,gaps=0,count=0;std::array<IdleGap,4> worst{};};
class IdleGapProfile {
    bool enabled_=false,open_=false;IdleGap current_;IdleReport window_;
public:
    static constexpr std::uint64_t ThresholdNs=20000000;
    static constexpr unsigned WindowFrames=120;
    void enable(bool on){if(enabled_==on)return;enabled_=on;open_=false;current_={};window_={};}
    bool open()const{return enabled_&&open_;}
    void wait(std::uint64_t start,std::uint64_t end,const IdlePoint& first,const IdlePoint& last,std::uint16_t before){
        if(!enabled_)return;
        if(end<start||(open_&&start<current_.endNs)){open_=false;current_={};return;}
        if(!open_){current_={};current_.first=first;current_.startNs=start;current_.before=before;open_=true;}
        current_.last=last;current_.endNs=end;current_.waitNs+=end-start;++current_.segments;
        current_.waitsSeen|=first.waits()|last.waits();
    }
    void finish(std::uint64_t end,const IdlePoint& last,std::uint16_t after,bool interrupted=false){
        if(!open())return;
        open_=false;
        if(end<current_.endNs)return;
        current_.last=last;current_.endNs=end;current_.after=after;current_.interrupted=interrupted;current_.waitsSeen|=last.waits();
        if(current_.wallNs()<ThresholdNs)return;
        ++window_.gaps;
        if(window_.count<window_.worst.size())window_.worst[window_.count++]=current_;
        else{
            auto least=std::min_element(window_.worst.begin(),window_.worst.end(),[](const IdleGap& a,const IdleGap& b){return a.wallNs()<b.wallNs();});
            if(current_.wallNs()>least->wallNs())*least=current_;
        }
    }
    bool presented(){return enabled_&&++window_.frames>=WindowFrames;}
    IdleReport take(){auto result=window_;window_={};return result;}
    static long long delta(std::uint64_t first,std::uint64_t last){
        return last>=first&&last-first<=std::uint64_t(std::numeric_limits<long long>::max())?static_cast<long long>(last-first):-1;
    }
};
}
