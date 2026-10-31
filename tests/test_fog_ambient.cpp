#include "fog_ambient.h"
#include <cassert>
#include <limits>
#include <cstdio>
int main(){
    using NorthlightFogAmbient::eligible;
    float rgb[3]={};assert(eligible(rgb,1));rgb[0]=.2f;
    assert(!eligible(rgb,1)&&eligible(rgb,0)&&eligible(rgb,-1));
    for(unsigned k=0;k<3;++k){
        float channels[3]={};channels[k]=.00000001f;assert(!eligible(channels,1));
        channels[k]=std::numeric_limits<float>::infinity();assert(!eligible(channels,0));
        channels[k]=std::numeric_limits<float>::quiet_NaN();assert(!eligible(channels,0));
    }
    assert(!eligible(nullptr,0)&&!eligible(rgb,std::numeric_limits<float>::infinity()));
    assert(!eligible(rgb,std::numeric_limits<float>::quiet_NaN()));
    float negative[3]={-1,-.2f,-.01f};assert(eligible(negative,1));
    std::puts("PASS fog ambient eligibility: exact zero, every positive channel, disabled gain, nonfinite fail-open");
}
