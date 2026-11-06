#pragma once
#include <algorithm>
#include <cmath>
#include "surface_frame.h"

namespace NorthlightWeatherMotion {
struct Flow {float x=.35f,y=1,z=0,shiftX=0,shiftY=0;};
struct Tracker {
    Flow flow;
    float previousForward[3]={};
    double previous=0;bool started=false;
    unsigned windKind=~0u,windHeading=~0u,windStrength=~0u;
    float windVelocity[3]={};
    static float dot(const float* a,const float* b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
    void update(const NorthlightSurfaceFrame& frame,unsigned kind,unsigned heading,unsigned strength,bool respond,double time){
        if(!std::isfinite(time))return;
        const float dt=started?float(std::clamp(time-previous,0.,.1)):0;
        const float fall=kind==1?19.f:kind==2?1.3f:.25f;
        const unsigned normalized=heading%360,limited=std::min(strength,300u);
        if(windKind!=kind||windHeading!=normalized||windStrength!=limited){
            const float angle=float(normalized)*.01745329252f;
            const float speed=(kind==1?7.5f:kind==2?1.2f:6.f)*limited*.01f;
            windVelocity[0]=std::cos(angle)*speed;windVelocity[1]=std::sin(angle)*speed;windVelocity[2]=-fall;
            windKind=kind;windHeading=normalized;windStrength=limited;
        }
        const float* velocity=windVelocity;
        const float scale=kind==3?6.f:fall;
        const float sign=frame.c[1][2]<0?-1.f:1.f;
        const float forward[3]={frame.c[6][0]*sign,frame.c[6][1]*sign,frame.c[6][2]*sign};
        flow.x=dot(velocity,frame.c[4])/scale;flow.y=-dot(velocity,frame.c[5])/scale;flow.z=dot(velocity,forward)/scale;
        flow.shiftX=flow.shiftY=0;
        // Large view cuts and missing frames rebase the history instead of kicking the lens.
        if(respond&&started&&dt>0&&time-previous<.25&&dot(previousForward,forward)>.6f){
            flow.shiftX=std::clamp(dot(previousForward,frame.c[4])*frame.c[1][0]*.5f,-.12f,.12f)*.22f;
            flow.shiftY=std::clamp(-dot(previousForward,frame.c[5])*frame.c[1][1]*.5f,-.12f,.12f)*.22f;
        }
        for(unsigned i=0;i<3;++i)previousForward[i]=forward[i];
        previous=time;started=true;
    }
};
}
