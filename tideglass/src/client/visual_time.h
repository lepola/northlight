#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "client/client_call_patch.h"

namespace water
{
// Visual minutes only. Simulation ticks and the server's clock are never changed.
class VisualDayCycle
{
public:
    float Evaluate(float real,uint32_t now,float hour,float seconds)
    {
        if (!std::isfinite(hour) || hour>=24) hour=-1;
        if (!std::isfinite(seconds) || seconds<0) seconds=0;
        if (seconds>0) {
            if (!running || seconds!=period || hour!=startHour) {
                origin=hour>=0?hour/24:real;tick=now;period=seconds;startHour=hour;running=true;
            }
            const double fraction=origin+double(uint32_t(now-tick))*.001/period;
            return float(fraction-std::floor(fraction));
        }
        running=false;
        return hour>=0?hour/24:real;
    }
private:
    bool running=false;
    float origin=0,period=0,startHour=-1;
    uint32_t tick=0;
};

namespace client12340
{
// The two native lighting reads already used by standalone's day-fraction hook.
// Keep this small ownership transaction independent of OS page protection.
class DayFractionCalls : public NativeCalls<2>
{
public:
    DayFractionCalls():NativeCalls({0x7e2752,0x4f859c},0x76cff0){}
};
}
}
