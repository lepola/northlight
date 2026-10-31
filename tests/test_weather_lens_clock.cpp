#include "weather_lens_clock.h"
#include <cassert>
#include <cstdio>
#include <limits>

int main(){
    for(float intensity:{0.f,.1f,.5f,1.f})for(unsigned fps:{30u,60u,120u,240u}){
        NorthlightWeatherLensClock clock;clock.advance(0,intensity);
        for(unsigned f=1;f<=fps*10;++f)clock.advance(float(f)/fps,intensity);
        assert(std::abs(clock.elapsed-10*(.45+1.55*double(intensity)))<1e-5);
    }
    NorthlightWeatherLensClock light,heavy;
    light.advance(0,.1f);heavy.advance(0,.9f);
    for(unsigned f=1;f<=1200;++f){light.advance(float(f)/120,.1f);heavy.advance(float(f)/120,.9f);}
    assert(heavy.elapsed>light.elapsed*3);
    const double before=light.elapsed;
    light.advance(10,1);assert(light.elapsed==before); // An intensity edit alone never changes position.
    light.advance(10.01f,1);assert(light.elapsed>before&&light.elapsed<before+.021);
    const double paused=light.elapsed;
    light.advance(1000,1);assert(light.elapsed<=paused+.200001); // Shelter/Alt+Tab is one bounded step.
    const auto reset=light.elapsed;light.advance(0,1);assert(light.elapsed==reset);
    for(float bad:{-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        const auto saved=light.elapsed;light.advance(bad,.5f);assert(light.elapsed==saved);
    }
    NorthlightWeatherLensClock a,b;
    for(unsigned i=0;i<1000;++i){a.advance(float(i)/120,.6f);b.advance(float(i)/120,.6f);}
    assert(a.elapsed==b.elapsed); // Rain/snow clocks and visibility settings cannot affect one another.
    NorthlightWeatherLensClock longSession;longSession.advance(1e8,.5f);longSession.advance(1e8+1./120,.5f);
    assert(longSession.elapsed>.01&&longSession.elapsed<.011); // A large session time retains sub-frame precision.
    std::puts("PASS weather lens clock: FPS-independent speed, intensity response, continuous changes, pauses, resets and independent state");
}
