#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// CPU arrival intervals at Present, not GPU timings. Fixed storage, no waits.
// Reset across device resets and effect-mode changes so A/B windows do not mix.
namespace NorthlightFrameIntervals {
struct Report { unsigned count=0;double meanMs=0,p50Ms=0,p95Ms=0,maxMs=0; };
class Window {
    std::array<double,120> samples_{};
    unsigned count_=0;
    int64_t previous_=0;
    bool primed_=false;
public:
    void reset(){count_=0;previous_=0;primed_=false;}
    // Preserve short A/B segments before resetting at a settings boundary.
    // The next sample primes a new interval, so no interval crosses modes.
    bool takePartial(Report& out){
        out={};
        if(!count_){reset();return false;}
        out.count=count_;
        for(unsigned i=0;i<count_;++i)out.meanMs+=samples_[i];
        out.meanMs/=count_;
        std::sort(samples_.begin(),samples_.begin()+count_);
        out.p50Ms=(samples_[(count_-1)/2]+samples_[count_/2])*.5;
        out.p95Ms=samples_[(count_*95+99)/100-1];out.maxMs=samples_[count_-1];
        reset();return true;
    }
    bool sample(int64_t tick,int64_t frequency,Report& out){
        out={};
        if(frequency<=0){reset();return false;}
        if(!primed_){previous_=tick;primed_=true;return false;}
        const auto old=previous_;previous_=tick;
        if(tick<=old){count_=0;return false;}
        const double ms=(double(tick)-double(old))*1000.0/double(frequency);
        if(!std::isfinite(ms)||ms<=0){count_=0;return false;}
        samples_[count_++]=ms;
        if(count_!=samples_.size())return false;
        out.count=count_;
        for(double value:samples_)out.meanMs+=value;
        out.meanMs/=count_;
        std::sort(samples_.begin(),samples_.end());
        out.p50Ms=(samples_[59]+samples_[60])*.5;
        out.p95Ms=samples_[113];out.maxMs=samples_.back();
        count_=0;return true;
    }
};
}
