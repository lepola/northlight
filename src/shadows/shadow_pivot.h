#pragma once
// 0.3.190 shadow pivot distance correction. The directional cascades are centred on
// shadowPivot() = eye + forward*distance, and the distance is only re-estimated while the camera
// orbits (12 degree turns, <= 2 yd per update). A mouse-wheel zoom or a camera collision (a tree or
// wall pushes the camera toward the player) moves the eye ALONG the view ray and leaves the distance
// stale: the pivot lands far past the player, the near/far hand-over (0.72..0.9 of the near radius)
// comes close to the player and his surroundings fall into the 4x coarser far cascade.
// This corrects the distance after the orbit estimator; the pivot always stays on the view ray, so
// orbiting still leaves every cascade where it was. Two sources:
//  snap: frame to frame the forward is nearly unchanged (dot > ForwardDot), the eye's move across the
//   ray is small (<= SnapAcrossRatio of its move along it, at most SnapAcrossMax yd), and its move along
//   the ray is at least SnapAlong yd: the distance loses that move (zoom/collision in: shorter, zoom
//   out: longer). Walking (~.12 yd/frame) and flying (~.7 yd/frame, camera pitched down: .57 along,
//   .4 across) never reach SnapAlong. A faster straight flight along the ray cannot be told from a
//   zoom by the camera alone, so at most SnapRun frames in a row correct (a zoom animation spans a few);
//   the run restarts after a frame under SnapAlong. A move over SnapJump yd is a teleport, not a snap.
//  self: only while the actor-shadow self was CAPTURED in the latest selection (the caller passes null
//   otherwise: during a capture gap selfAt only follows the camera). The self's axis midpoint (selfAt +
//   SelfAxis/2 up) must lie within SelfRay of the ray at SelfMinT..SelfMaxT (actor_shadow_selection.h);
//   its distance t along the ray then steers the distance: |t - distance| < SelfDeadband: unchanged,
//   below SelfJump: SelfGain of the difference, at most SelfStep yd per frame, from SelfJump up: t at
//   once. The self may only refine the distance along the ray (it feeds the pivot that ranks it).
// Result clamped to Min..Max. Non-finite input changes nothing. Portable and allocation-free.
#include "actor_shadow_selection.h"
#include <algorithm>
#include <cmath>
namespace NorthlightShadowPivot {
constexpr float ForwardDot=.9995f,SnapAlong=1.f,SnapAcrossRatio=.35f,SnapAcrossMax=1.5f,SnapJump=40.f,Min=.5f,Max=80.f;
constexpr unsigned SnapRun=4;
constexpr float SelfDeadband=1.f,SelfJump=8.f,SelfGain=.3f,SelfStep=1.5f;
enum Source : unsigned {Orbit=0,Snap=1,Self=2};
inline const char* name(Source s){return s==Snap?"snap":s==Self?"self":"orbit";}
struct State {
    float eye[3]={},forward[3]={};bool valid=false;unsigned run=0;
    unsigned snapCorrections=0;Source source=Orbit;
    float selfDistance=-1,selfPoint[3]={};bool hasSelf=false,selfAccepted=false; /* the latest captured self */
    void reset(){*this=State{};}
    // eye, forward (any length): the camera; distance: the orbit estimator's value; selfAt: the captured self's
    // position (null: none captured in the latest selection). Returns the corrected distance.
    float update(const float* eyeIn,const float* forwardIn,float distance,const float* selfAt){
        auto finite=[](const float* v){return v&&std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2]);};
        source=Orbit;
        if(!finite(eyeIn)||!finite(forwardIn)||!std::isfinite(distance))return distance;
        const float length=std::sqrt(forwardIn[0]*forwardIn[0]+forwardIn[1]*forwardIn[1]+forwardIn[2]*forwardIn[2]);
        if(!(length>1e-6f))return distance;
        float f[3],e[3];for(unsigned k=0;k<3;++k){f[k]=forwardIn[k]/length;e[k]=eyeIn[k];}
        float now=std::min(Max,std::max(Min,distance));
        if(valid){float d[3],m=0,along=0,turn=0;
            for(unsigned k=0;k<3;++k){d[k]=e[k]-eye[k];m+=d[k]*d[k];along+=d[k]*f[k];turn+=f[k]*forward[k];}
            const float across=std::sqrt(std::max(0.f,m-along*along)),size=std::fabs(along);
            if(size<SnapAlong)run=0;
            else if(turn>ForwardDot&&m<=SnapJump*SnapJump&&across<=std::min(SnapAcrossRatio*size,SnapAcrossMax)&&run<SnapRun){
                now=std::min(Max,std::max(Min,now-along));++run;++snapCorrections;source=Snap;}}
        for(unsigned k=0;k<3;++k){eye[k]=e[k];forward[k]=f[k];}valid=true;
        hasSelf=selfAccepted=false;selfDistance=-1;
        if(finite(selfAt)){
            for(unsigned k=0;k<3;++k)selfPoint[k]=selfAt[k];selfPoint[2]+=.5f*NorthlightActorShadowSelection::SelfAxis;hasSelf=true;
            float v[3],m=0,t=0;for(unsigned k=0;k<3;++k){v[k]=selfPoint[k]-e[k];m+=v[k]*v[k];t+=v[k]*f[k];}
            const float miss=std::sqrt(std::max(0.f,m-t*t));
            if(miss<=NorthlightActorShadowSelection::SelfRay&&t>=NorthlightActorShadowSelection::SelfMinT&&t<=NorthlightActorShadowSelection::SelfMaxT){
                selfAccepted=true;selfDistance=t;source=Self;const float diff=t-now;
                if(std::fabs(diff)>=SelfJump)now=t;
                else if(std::fabs(diff)>=SelfDeadband)now+=std::max(-SelfStep,std::min(SelfStep,diff*SelfGain));
                now=std::min(Max,std::max(Min,now));}}
        return now;
    }
    // Near cascade blend weight at the latest captured self (shader: saturate((max(|q.x|,|q.y|)-.72)/.18)),
    // for the row-major 4x4 near matrix m of directionalShadow; -1 without a captured self.
    float nearBlendAtSelf(const float* m)const{
        if(!hasSelf||!m)return -1;
        const float* p=selfPoint;
        const float x=p[0]*m[0]+p[1]*m[4]+p[2]*m[8]+m[12],y=p[0]*m[1]+p[1]*m[5]+p[2]*m[9]+m[13];
        return std::min(1.f,std::max(0.f,(std::max(std::fabs(x),std::fabs(y))-.72f)/.18f));
    }
};
// ShadowPivotCorrection=0: the distance returned is the input, bit for bit, and nothing is remembered.
inline float correct(bool enabled,State& state,const float* eye,const float* forward,float distance,const float* selfAt){
    if(!enabled){state.reset();return distance;}
    return state.update(eye,forward,distance,selfAt);
}
}
