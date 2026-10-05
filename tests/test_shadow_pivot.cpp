// 0.3.190 shadow pivot distance correction (shadow_pivot.h): a collision snap or wheel zoom moves the
// distance with the eye, walking, running and flying do not, orbiting leaves the pivot point put, a
// captured self steers the distance with hysteresis (a capture gap does not), non-finite input and the
// switch change nothing.
#include "shadow_pivot.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace NorthlightShadowPivot;
struct Cam {float eye[3],f[3];};
static void forwardOf(float yawDeg,float pitchDeg,float* f){const float y=yawDeg*3.14159265f/180,p=pitchDeg*3.14159265f/180;f[0]=std::cos(p)*std::cos(y);f[1]=std::cos(p)*std::sin(y);f[2]=std::sin(p);}
static bool same(float a,float b){return std::memcmp(&a,&b,sizeof a)==0;}
static float dist3(const float* a,const float* b){float q=0;for(unsigned k=0;k<3;++k)q+=(a[k]-b[k])*(a[k]-b[k]);return std::sqrt(q);}
int main(){
    float f[3];forwardOf(30,-12,f);
    const float player[3]={-8886.2f,572.2f,100.4f};
    auto eyeAt=[&](float t,float* e){for(unsigned k=0;k<3;++k)e[k]=player[k]-f[k]*t;}; /* t yd behind the player on the view ray */
    // Collision snap: estimate 30 yd, the eye jumps 25 yd toward the player in one frame -> about 5 yd.
    {State s;float e[3];eyeAt(30,e);float d=30;for(int i=0;i<10;++i)d=s.update(e,f,d,nullptr);assert(d==30&&s.source==Orbit&&s.snapCorrections==0);
     eyeAt(5,e);d=s.update(e,f,d,nullptr);assert(std::fabs(d-5)<.01f&&s.source==Snap&&s.snapCorrections==1);
     for(int i=0;i<10;++i)d=s.update(e,f,d,nullptr);assert(std::fabs(d-5)<.01f&&s.source==Orbit);
     eyeAt(30,e);d=s.update(e,f,d,nullptr);assert(std::fabs(d-30)<.01f&&s.source==Snap);} /* the camera swings back out */
    // Wheel zoom in: several 1.5-3 yd steps, idle frames between; the pivot stays on the player.
    {State s;float e[3];float t=30,d=30;eyeAt(t,e);s.update(e,f,d,nullptr);
     const float steps[]={1.5f,3.f,2.f,2.5f,1.5f};
     for(float step:steps){for(int i=0;i<6;++i)d=s.update(e,f,d,nullptr);t-=step;eyeAt(t,e);d=s.update(e,f,d,nullptr);assert(s.source==Snap&&std::fabs(d-t)<.01f);}
     assert(s.snapCorrections==5);}
    // A smooth wheel zoom (.33 yd/frame) stays under SnapAlong: no snap (the self path follows it).
    {State s;float e[3];float t=30,d=30;eyeAt(t,e);s.update(e,f,d,nullptr);
     for(int i=0;i<30;++i){t-=.33f;eyeAt(t,e);d=s.update(e,f,d,nullptr);}assert(d==30&&s.snapCorrections==0);}
    // Zoom out: longer; clamped to 80 and, in, to .5.
    {State s;float e[3];float d=10;eyeAt(10,e);s.update(e,f,d,nullptr);eyeAt(16,e);d=s.update(e,f,d,nullptr);assert(std::fabs(d-16)<.01f&&s.source==Snap);
     float far=78;eyeAt(70,e);State z;z.update(e,f,far,nullptr);eyeAt(79,e);far=z.update(e,f,far,nullptr);assert(far==Max);
     State c;float near=3;eyeAt(3,e);c.update(e,f,near,nullptr);eyeAt(.1f,e);near=c.update(e,f,near,nullptr);assert(near==Min);}
    // Walking and running forward (the camera follows the player: the eye moves along the ray), 60 and 20 FPS: none of it counts.
    {for(float speed:{.12f,.35f,.6f,.95f}){State s;float e[3];eyeAt(12,e);float d=12;float walked=0;
        for(int i=0;i<600;++i){walked+=speed;float q[3];for(unsigned k=0;k<3;++k)q[k]=e[k]+f[k]*walked;d=s.update(q,f,d,nullptr);}
        assert(d==12&&s.snapCorrections==0&&s.source==Orbit);}}
    // Flying .7 yd/frame horizontally with the camera pitched down 35 degrees: .57 along, .4 across.
    {float g[3];forwardOf(30,-35,g);float dir[3]={std::cos(30*3.14159265f/180),std::sin(30*3.14159265f/180),0};
     State s;float d=14,e[3]={0,0,300};s.update(e,g,d,nullptr);
     for(int i=0;i<600;++i){for(unsigned k=0;k<3;++k)e[k]+=dir[k]*.7f;d=s.update(e,g,d,nullptr);assert(s.source==Orbit);}
     assert(d==14&&s.snapCorrections==0);
     // Faster (1.6 yd/frame): the across share (.92) is over 35% of the along share (1.3): still none.
     for(int i=0;i<100;++i){for(unsigned k=0;k<3;++k)e[k]+=dir[k]*1.6f;d=s.update(e,g,d,nullptr);}assert(d==14&&s.snapCorrections==0);
     }
    // Sustained straight motion along the ray, no self: flight 1.35 yd/call, taxi 3 yd/call (200 calls), also from a ramp-up: the distance stays.
    {for(float speed:{1.35f,3.f}){State fast;float q[3]={0,0,300};float d=12;fast.update(q,f,d,nullptr);
        for(int i=0;i<200;++i){for(unsigned k=0;k<3;++k)q[k]+=f[k]*speed;d=fast.update(q,f,d,nullptr);if(i>0)assert(fast.source==Orbit);}
        assert(std::fabs(d-12)<.01f+(fast.snapCorrections?speed:0)&&fast.snapCorrections<=1);}
     State ramp;float q[3]={0,0,300};float d=12;ramp.update(q,f,d,nullptr);
     const float moves[]={.3f,.9f,1.5f};for(float m:moves){for(unsigned k=0;k<3;++k)q[k]+=f[k]*m;d=ramp.update(q,f,d,nullptr);}
     for(int i=0;i<200;++i){for(unsigned k=0;k<3;++k)q[k]+=f[k]*1.5f;d=ramp.update(q,f,d,nullptr);}assert(d==12&&ramp.snapCorrections==0);
     // The same ramp at double the per-call move (capture intervals): still none.
     State ramp2;float r[3]={0,0,300};d=12;ramp2.update(r,f,d,nullptr);const float moves2[]={.6f,1.8f,3.f};
     for(float m:moves2){for(unsigned k=0;k<3;++k)r[k]+=f[k]*m;d=ramp2.update(r,f,d,nullptr);}
     for(int i=0;i<200;++i){for(unsigned k=0;k<3;++k)r[k]+=f[k]*3.f;d=ramp2.update(r,f,d,nullptr);}assert(d==12&&ramp2.snapCorrections==0);}
    // Collision impulse after standing still, and while walking slowly sideways: corrected.
    {State s;float e[3];eyeAt(20,e);float d=20;for(int i=0;i<30;++i)d=s.update(e,f,d,nullptr);eyeAt(6,e);d=s.update(e,f,d,nullptr);assert(s.source==Snap&&std::fabs(d-6)<.01f);
     State w;eyeAt(20,e);d=20;const float side[3]={f[1],-f[0],0};w.update(e,f,d,nullptr);
     for(int i=0;i<30;++i){for(unsigned k=0;k<3;++k)e[k]+=side[k]*.12f;d=w.update(e,f,d,nullptr);}
     for(unsigned k=0;k<3;++k)e[k]+=f[k]*14+side[k]*.12f;d=w.update(e,f,d,nullptr);assert(w.source==Snap&&std::fabs(d-6)<.05f&&w.snapCorrections==1);}
    // Orbit: the eye circles a point 20 yd ahead (1.2 degrees/frame, the estimator's distance right): the pivot never moves.
    {State s;float d=20,pivot[3];float e[3];float yaw=30;float g[3];forwardOf(yaw,-12,g);for(unsigned k=0;k<3;++k)e[k]=player[k]-g[k]*d;
     for(unsigned k=0;k<3;++k)pivot[k]=e[k]+g[k]*d;s.update(e,g,d,nullptr);
     for(int i=0;i<300;++i){yaw+=1.2f;forwardOf(yaw,-12,g);for(unsigned k=0;k<3;++k)e[k]=pivot[k]-g[k]*20;
        d=s.update(e,g,d,nullptr);assert(s.source==Orbit&&same(d,20));float p[3];for(unsigned k=0;k<3;++k)p[k]=e[k]+g[k]*d;assert(dist3(p,pivot)<1e-3f);}
     assert(s.snapCorrections==0);}
    // Self refinement: a captured self on the ray steers the distance with a rate limit; a jump over 8 yd is immediate.
    {State s;float e[3];eyeAt(20,e);const float axis=NorthlightActorShadowSelection::SelfAxis;
     float root[3]={player[0],player[1],player[2]-axis*.5f}; /* the axis midpoint is the player */
     float d=26;d=s.update(e,f,d,root);assert(s.source==Self&&s.selfAccepted&&std::fabs(s.selfDistance-20)<.01f&&d<26&&d>=24.4f); /* diff 6: .3*6=1.8 limited to 1.5 */
     for(int i=0;i<40;++i)d=s.update(e,f,d,root);assert(std::fabs(d-20)<1.01f);
     const float settled=d;for(int i=0;i<10;++i)d=s.update(e,f,d,root);assert(d==settled);
     float big=35;State j;j.update(e,f,big,root);big=j.update(e,f,big,root);assert(std::fabs(big-20)<.01f);}
    // A self whose axis is off the ray (miss > SelfRay) or outside SelfMinT..SelfMaxT is ignored.
    {State s;float e[3];eyeAt(20,e);float side[3]={f[1],-f[0],0};float root[3];for(unsigned k=0;k<3;++k)root[k]=player[k]+side[k]*2.5f;root[2]-=NorthlightActorShadowSelection::SelfAxis*.5f;
     float d=26;d=s.update(e,f,d,root);assert(d==26&&!s.selfAccepted&&s.source==Orbit&&s.hasSelf);
     float behind[3];for(unsigned k=0;k<3;++k)behind[k]=e[k]-f[k]*3;behind[2]-=NorthlightActorShadowSelection::SelfAxis*.5f;d=s.update(e,f,d,behind);assert(d==26&&!s.selfAccepted);}
    // Capture gap (the caller passes no self): the distance is left alone and nothing is accepted.
    {State s;float e[3];eyeAt(20,e);float d=26;for(int i=0;i<30;++i)d=s.update(e,f,d,nullptr);assert(d==26&&!s.hasSelf&&s.selfDistance==-1&&s.nearBlendAtSelf(e)==-1);}
    // Hysteresis: a jittery self (+-.4 yd) never moves a settled distance and the source never flaps; larger jitter stays bounded.
    {State s;float e[3];eyeAt(20,e);float root[3]={player[0],player[1],player[2]-NorthlightActorShadowSelection::SelfAxis*.5f};
     float d=20;unsigned changes=0;Source last=Orbit;bool first=true;
     for(int i=0;i<400;++i){const float jitter=(i%2?.4f:-.4f)*(1+(i%3)*.2f)*.8f;float r[3]={root[0]+f[0]*jitter,root[1]+f[1]*jitter,root[2]+f[2]*jitter};
        const float before=d;d=s.update(e,f,d,r);assert(same(d,before));if(!first&&s.source!=last)++changes;last=s.source;first=false;}
     assert(changes==0&&s.source==Self);
     float wide=20;State w;for(int i=0;i<400;++i){const float jitter=(i%2?1.6f:-1.6f);float r[3]={root[0]+f[0]*jitter,root[1]+f[1]*jitter,root[2]+f[2]*jitter};wide=w.update(e,f,wide,r);assert(std::fabs(wide-20)<1.6f);}}
    // Snap then self in one frame: self has the last word; the source is Self.
    {State s;float e[3];eyeAt(30,e);float root[3]={player[0],player[1],player[2]-NorthlightActorShadowSelection::SelfAxis*.5f};float d=30;s.update(e,f,d,nullptr);
     eyeAt(5,e);d=s.update(e,f,d,root);assert(s.source==Self&&std::fabs(d-5)<.01f&&s.snapCorrections==1);}
    // Non-finite input changes nothing (and does not poison the state).
    {State s;float e[3];eyeAt(20,e);float d=20;s.update(e,f,d,nullptr);
     const float nan[3]={NAN,0,0},inf[3]={0,INFINITY,0},zero[3]={0,0,0};
     assert(same(s.update(nan,f,d,nullptr),d)&&same(s.update(e,nan,d,nullptr),d)&&same(s.update(e,inf,d,nullptr),d)&&same(s.update(e,zero,d,nullptr),d)&&s.update(e,f,NAN,nullptr)!=s.update(e,f,NAN,nullptr));
     assert(s.update(e,f,INFINITY,nullptr)==INFINITY&&s.snapCorrections==0);
     eyeAt(8,e);d=s.update(e,f,d,nullptr);assert(std::fabs(d-8)<.01f); /* the state survived: the last good eye was 20 yd back */
     d=s.update(e,f,d,nan);assert(same(d,d)&&!s.hasSelf);}
    // A teleport (over SnapJump) is not a snap; neither is a turn of the forward.
    {State s;float e[3];eyeAt(20,e);float d=20;s.update(e,f,d,nullptr);float far[3];eyeAt(20,far);for(unsigned k=0;k<3;++k)far[k]+=f[k]*60;d=s.update(far,f,d,nullptr);assert(d==20&&s.snapCorrections==0);
     float g[3];forwardOf(33,-12,g);State t;t.update(e,f,d,nullptr);float h[3];eyeAt(5,h);d=t.update(h,g,d,nullptr);assert(d==20&&t.snapCorrections==0);}
    // ShadowPivotCorrection=0: the old value, bit for bit, nothing remembered; 1 corrects.
    {State s;float e[3];eyeAt(30,e);float d=30;d=correct(false,s,e,f,d,nullptr);eyeAt(5,e);const float root[3]={player[0],player[1],player[2]-1.5f};
     const float old=30.123456f;assert(same(correct(false,s,e,f,old,root),old)&&!s.valid&&!s.hasSelf&&s.snapCorrections==0);
     State on;eyeAt(30,e);correct(true,on,e,f,30,nullptr);eyeAt(5,e);assert(std::fabs(correct(true,on,e,f,30,nullptr)-5)<.01f);}
    // Near blend at the self: the shader's saturate((max(|q.x|,|q.y|)-.72)/.18) with the near matrix.
    {State s;float e[3]={0,0,0};const float ff[3]={1,0,0};const float self[3]={20,0,-1.5f};s.update(e,ff,20,self);
     float m[16]={};m[0]=1.f/48;m[5]=1.f/48;m[10]=1;m[15]=1; /* near cascade, 48 yd radius, unrotated: q.x=x/48 */
     const float nb=s.nearBlendAtSelf(m);assert(nb==0); /* 20/48=.42 */
     State t;const float far[3]={40,0,-1.5f};t.update(e,ff,20,far);assert(std::fabs(t.nearBlendAtSelf(m)-((40.f/48-.72f)/.18f))<1e-5f);
     State u;const float edge[3]={47,0,-1.5f};u.update(e,ff,20,edge);assert(u.nearBlendAtSelf(m)==1&&u.nearBlendAtSelf(nullptr)==-1);}
    std::puts("PASS shadow pivot: collision snap 30->5 yd, wheel zoom steps and animation, zoom out and clamps 0.5..80, walking/running/flying never correct, sustained flight/taxi and ramp-ups never correct, smooth zoom does not snap, impulses after stillness or sideways walking do, orbit leaves the pivot point put, captured self steers with 1 yd deadband / 1.5 yd steps / 8 yd jump, capture gap and off-ray self ignored, no source flapping over a jittery self, non-finite input unchanged, switch off = input bit for bit");
}
