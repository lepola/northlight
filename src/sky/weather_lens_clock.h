#pragma once
#include <algorithm>
#include <cmath>

// Integrate speed, rather than multiplying absolute time by changing intensity.
// Shelter, pauses and clock resets cannot teleport a partially visible droplet.
struct NorthlightWeatherLensClock {
    double elapsed=0;
    double previous=0;
    bool started=false;
    float advance(double seconds,float intensity){
        if(!std::isfinite(seconds)||seconds<0)return float(elapsed);
        const double dt=started?std::clamp(double(seconds)-previous,0.,.1):0.;
        previous=seconds;started=true;
        const double amount=std::isfinite(intensity)?std::clamp(double(intensity),0.,1.):0.;
        elapsed+=dt*(.45+1.55*amount);
        return float(elapsed);
    }
};
