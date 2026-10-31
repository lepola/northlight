#pragma once
// Task 34 SpellFog: replayed spell draws (they are drawn after Northlight's fog composite) get Northlight's fog through the game's own fog constants, at the cost of
// changing one or two constants of the record, not a pass. The M2 effect vertex shaders write fog = min(pow(max(z*c30.x + c30.y, 0), c30.z), 1) and the ps_3_0 combiners mix
// lerp(cF, X, fog) (cF a PS constant); so a draw replayed with fog' = fog * T_N(z) (T_N: Northlight's transmittance along the view ray) is fogged by both. The fit F maps the game's
// (c30.x, c30.y, c30.z) to (a, b, p) with pow(max(z*a + b, 0), p) ~ g(z) * T_N(z) on [0, min(game fog end, 128)] (Northlight's fog reaches 128 units); it is computed once per distinct
// game constant and frame, not per entry. Pure CPU maths: no D3D, no game data.
// Approximations, by design: the camera's heading stands for every pixel of the effect (the optical depth is taken on rays from the eye to a point at the heading, at the height of an effect
// above the ground there; off-axis pixels are farther per unit of view depth); the optical depth is sampled at six distances
// and interpolated, Northlight's fog reaches 128 units; the fit is one power law (a game ramp that starts late with a high power is followed less well); the horizon haze that
// Northlight adds to the picture is not folded; the in-scatter colour is the ambient sky source only (no direct-light term); the estimate follows the camera with a 0.25 s smoothing and
// is the previous frame's. Fixed-function effects and the ps_2_0 fixed-function fog colour are not touched.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace NorthlightSpellFog {
constexpr unsigned kSamples=6;
constexpr float kDistance[kSamples]={6,12,24,48,80,128};   // view distances (world units) the frame's optical depth is sampled at
constexpr float kRange=128;                                  // Northlight's fog is integrated to 128 units (FogInfo.w)
// A precomputed fit of the publish (Hold::update): the record path then looks fits up and never fits. flat[bare]: the fit for every flat game constant (it does not depend on the constant),
// ramp[i]: the fit for a recently seen falling-ramp constant (the layer reports those, Hold::addHint); valid: computed, ok: the fit succeeded.
struct Fit{bool valid=false,ok=false,bare=false,bareSkip=false;float key[3]={},fit[3]={};}; // bareSkip: not ok because no bare-ramp fit was good enough (the record folds nothing, counted apart)
constexpr unsigned kRamps=4;
struct Frame{
    bool active=false;
    float tau[kSamples]={};        // optical depth from the eye to kDistance[i] along the view ray (nondecreasing)
    float scatter[3]={};           // radiance of the fog's in-scatter per unit of (1 - T): the neutral sky source
    unsigned revision=0;           // changes when the numbers moved by more than ~1 % since the last change (Hold::update): the fit caches' key, not a per-frame counter
    Fit flat[2],ramp[kRamps];      // the fits of this revision (empty: the layer fits itself, cached per revision)
    unsigned stamp=0;              // counts the publishes (Hold::update): a frame that never reached the publish leaves it unchanged (see Freshness)
    // T_N(z): exp(-tau(z)) with tau interpolated linearly through (0,0) and the samples, constant beyond the last
    float transmittance(float z)const{
        if(!active||!(z>0))return 1;
        if(z>=kDistance[kSamples-1])return std::exp(-tau[kSamples-1]);
        float z0=0,t0=0;
        for(unsigned i=0;i<kSamples;++i){
            if(z<=kDistance[i]){const float u=(z-z0)/(kDistance[i]-z0);return std::exp(-(t0+(tau[i]-t0)*u));}
            z0=kDistance[i];t0=tau[i];
        }
        return std::exp(-tau[kSamples-1]);
    }
};
// The clamped and bare shader variants refine the same six candidates.
// Share only that search within one refit; acceptance and fallback stay separate.
struct FitWork{
    struct Candidate{float a,b,p,e;};
    Candidate refined[6];unsigned count=0;bool ready=false;
    float game[3]={},tau[kSamples]={};
    bool matches(const Frame& f,const float g[3])const{
        return ready&&!std::memcmp(game,g,sizeof game)&&!std::memcmp(tau,f.tau,sizeof tau);
    }
};
inline bool flatConstant(const float game[3]);
inline bool emitFog(const Frame& f,const float game[3],float out[3],bool bareBase=false,bool* bareSkip=nullptr,FitWork* work=nullptr);
// The frame's numbers move toward the raw estimate with a time constant (no pumping when the camera crosses a fog bank), fade to nothing when the effect is off, and the frame is
// inactive (identity) below a negligible optical depth. `revision` moves only when a sample's optical depth, the scatter colour or `active` changed by more than ~1 % relative to the values at
// the last move; then the precomputed fits are redone (here, on the thread of the world render: the record path of the layer does no fitting for them).
struct Hold{
    Frame out;
    float fitTau[kSamples]={},fitScatter[3]={};bool fitActive=false;
    float hints[kRamps][4]={};unsigned hintN=0,hintNext=0;bool hintsNew=false; // falling-ramp constants the layer saw (x, y, power, bare) and whether one has no fit yet
    // The layer reports a falling-ramp constant it had to fit; the next update fits it too (a ramp constant is the same for many frames).
    void addHint(const float game[3],bool bare){
        for(unsigned i=0;i<hintN;++i)if(!std::memcmp(hints[i],game,12)&&(hints[i][3]!=0)==bare)return;
        const unsigned at=hintN<kRamps?hintN++:(hintNext++%kRamps);
        std::memcpy(hints[at],game,12);hints[at][3]=bare?1.f:0.f;hintsNew=true;
    }
    static void fitOne(const Frame& f,const float game[3],bool bare,Fit& slot,FitWork* work=nullptr){
        slot=Fit{};slot.valid=true;slot.bare=bare;std::memcpy(slot.key,game,12);slot.ok=emitFog(f,game,slot.fit,bare,&slot.bareSkip,work);
    }
    void refit(bool all){
        static const float kFlat[3]={0,1,1};
        if(all){out.flat[0]=Fit{};out.flat[1]=Fit{};for(auto& r:out.ramp)r=Fit{};}
        if(!out.active)return;
        FitWork work[kRamps+1];
        if(all)for(int bare=0;bare<2;++bare)fitOne(out,kFlat,bare!=0,out.flat[bare],&work[0]);
        for(unsigned i=0;i<hintN;++i){
            bool have=false;for(const Fit& r:out.ramp)if(r.valid&&r.bare==(hints[i][3]!=0)&&!std::memcmp(r.key,hints[i],12))have=true;
            if(have)continue;
            unsigned slot=0;while(slot<kRamps&&out.ramp[slot].valid)++slot;if(slot==kRamps)slot=i;
            unsigned shared=0;while(shared<i&&std::memcmp(hints[shared],hints[i],12))++shared;
            fitOne(out,hints[i],hints[i][3]!=0,out.ramp[slot],&work[shared+1]);
        }
    }
    unsigned resets=0;                            // non-finite input or state: the hold was reset and went on (the world renderer logs the first)
    void update(const Frame& raw,bool wanted,float dt){
        // An untouched, inactive hold only advances its publication stamp.
        if(!wanted&&!out.active&&!fitActive&&!hintsNew&&std::isfinite(dt)){
            static const float zero[kSamples]={};
            if(!std::memcmp(out.tau,zero,sizeof out.tau)&&!std::memcmp(fitTau,zero,sizeof fitTau)&&!std::memcmp(out.scatter,zero,sizeof out.scatter)&&!std::memcmp(fitScatter,zero,sizeof fitScatter)){++out.stamp;return;}
        }
        bool bad=!std::isfinite(dt);
        if(wanted&&raw.active){for(unsigned i=0;i<kSamples;++i)bad|=!std::isfinite(raw.tau[i]);for(unsigned c=0;c<3;++c)bad|=!std::isfinite(raw.scatter[c]);}
        for(unsigned i=0;i<kSamples;++i)bad|=!std::isfinite(out.tau[i]);for(unsigned c=0;c<3;++c)bad|=!std::isfinite(out.scatter[c]);
        if(bad){ // a NaN would stay in the smoothed numbers for good: start over from nothing (identity) and carry on with the next frame's estimate
            ++resets;for(float& t:out.tau)t=0;for(float& v:out.scatter)v=0;wanted=false;dt=0;
        }
        const float k=1-std::exp(-std::clamp(dt,0.f,.5f)/.25f);
        for(unsigned i=0;i<kSamples;++i){
            const float target=wanted&&raw.active?raw.tau[i]:0.f;
            out.tau[i]+=(target-out.tau[i])*k;
        }
        if(wanted&&raw.active)for(unsigned c=0;c<3;++c)out.scatter[c]=raw.scatter[c];
        const bool active=out.tau[kSamples-1]>.004f;
        bool moved=active!=fitActive;
        for(unsigned i=0;i<kSamples&&!moved;++i)if(std::fabs(out.tau[i]-fitTau[i])>.01f*std::max(fitTau[i],.01f))moved=true;
        for(unsigned c=0;c<3&&!moved;++c)if(std::fabs(out.scatter[c]-fitScatter[c])>.01f*std::max(fitScatter[c],.01f))moved=true;
        out.active=active;
        if(moved){
            ++out.revision;fitActive=active;
            std::memcpy(fitTau,out.tau,sizeof fitTau);std::memcpy(fitScatter,out.scatter,sizeof fitScatter);
            refit(true);hintsNew=false;
        }else if(hintsNew){refit(false);hintsNew=false;}
        ++out.stamp;
    }
};
// A frame whose world render did not publish (an early return, the effects bailing out) leaves the estimate frozen: the integration asks once per frame whether the stamp moved since
// the last frame and tells the layer; a frozen estimate is not folded (identity) until the next publish.
struct Freshness{
    unsigned seen=0;
    bool next(unsigned stamp){const bool moved=stamp!=seen;seen=stamp;return moved;}
};
// bareBase: the vertex shader raises the bare ramp to the power (vs_3_0 Model/Projected variants) instead of max(ramp, 0) or lit; a fit for it must not end before the game's ramp does.
// Close to the camera Northlight's fog is not folded: none within kClearNear units, fading in with a smoothstep over the next kClearFade units, full beyond. The fit target is
// g(z) * (1 - (1 - T_N(z)) * smoothstep(kClearNear, kClearNear + kClearFade, z)); the ramp form min(pow(max(a z + b, 0), p), 1) represents the plateau with b > 1 (1 up to z = (b-1)/-a).
constexpr float kClearNear=20,kClearFade=15;
inline float clearFade(float z){const float u=std::min(1.f,std::max(0.f,(z-kClearNear)/kClearFade));return u*u*(3-2*u);}
inline float foldedTransmittance(const Frame& f,float z){return 1-(1-f.transmittance(z))*clearFade(z);}
// Positive powers of a base >= 1 are clamped to 1 by every fit sample.
// Unit power preserves the clamped base, including signed zero; other inputs keep pow.
inline float clampedPower(float base,float power){
    const float w=std::max(base,0.f);
    if(power==1.f)return std::min(1.f,w);
    return w>=1.f&&power>0?1.f:std::min(1.f,std::pow(w,power));
}
// The game's visibility at view distance z for its fog constant (x, y, power).
inline float gameVisibility(const float g[3],float z){return clampedPower(z*g[0]+g[1],g[2]);}
// A flat game constant: visibility 1 at every distance (|x| ~ 0 and min(pow(max(y, 0), p), 1) = 1), how the game switches an effect's own fog off. Northlight's fog is then folded alone: the fit
// target is T_N(z) over 0..kRange. A bare-ramp shader (the pow of the ramp without a max) must not reach 0 before kFlatBareEnd then, so the fog does not come back after the zero crossing
// (D3D pow takes |base|); powers up to kFlatMaxPower are searched for a flat constant, so thick fog within kRange stays reachable.
constexpr float kFlatBareEnd=512,kFlatMaxPower=32;
inline bool flatConstant(const float game[3]){
    for(unsigned i=0;i<3;++i)if(!std::isfinite(game[i]))return false;
    return std::fabs(game[0])<=1e-7f&&game[2]>.05f&&game[2]<=16.f&&std::min(1.f,std::pow(std::max(game[1],0.f),game[2]))>=1.f;
}
// The fit F: out = (a, b, p). False: identity (leave the game's constant): no fog, a constant the shader would not use as a fog ramp (non-finite, an exponent outside .05..16, a ramp that
// rises and stays below 1), or a range too short to matter.
inline bool emitFog(const Frame& f,const float game[3],float out[3],bool bareBase,bool* bareSkip,FitWork* work){
    if(bareSkip)*bareSkip=false;
    if(!f.active||!(f.tau[kSamples-1]>=.004f))return false;                 // no fog (transmittance 1 everywhere): the game's constant
    for(unsigned i=0;i<3;++i)if(!std::isfinite(game[i]))return false;
    const bool flat=flatConstant(game);
    if(!flat&&(!(game[0]<-1e-7f)||!(game[2]>.05f&&game[2]<=16.f)))return false;   // a ramp that falls with distance, a sane exponent (or a flat constant)
    const float end=flat?kFlatBareEnd/.98f:-game[1]/game[0];                // where the game's visibility reaches 0 (a flat constant: where a bare ramp must at least reach it)
    const float hi=std::min(end,kRange);
    if(!(hi>2.f))return false;
    // sample points: the near plateau and its fade (0..20 every 4, then 25, 30, 35) and twelve across the fitted range; the near ones weigh more (the fit stays at 1 there)
    constexpr unsigned M=24;float z[M],h[M],wt[M];unsigned N=0;
    for(float q:{0.f,4.f,8.f,12.f,16.f,20.f,25.f,30.f,35.f})if(q<hi*.9f){z[N]=q;wt[N]=q<=kClearNear?16.f:1.f;++N;}
    for(unsigned i=0;i<12;++i){z[N]=hi*(float(i)+.5f)/12.f;wt[N]=1.f;++N;}
    for(unsigned i=0;i<N;++i)h[i]=gameVisibility(game,z[i])*foldedTransmittance(f,z[i]);
    static const float scale[]={.6f,.8f,1.f,1.25f,1.6f,2.f,2.6f,3.5f};
    float best=1e30f,bestFit[3]={game[0],game[1],game[2]};bool found=false;
    auto error=[&](float a,float b,float p,float limit=INFINITY){float e=0;for(unsigned i=0;i<N;++i){const float v=clampedPower(a*z[i]+b,p),d=v-h[i];e+=wt[i]*d*d;if(e>limit)break;}return e;};
    // A bare-ramp shader (D3D pow takes |base|) must be usable as a whole: visible near the eye (below kClearNear within .02 of the game's own where that is 1, else within .06) and, past its zero crossing, not coming back
    // above the game's own bare ramp times the transmittance (+0.15) out to the game's end (1024 for a flat constant). Thick fog cannot satisfy both with one power law of at most 32: then the record folds nothing (bareSkip), not a bad fit.
    auto bareGood=[&](float a,float b,float p){
        for(unsigned i=0;i<N&&z[i]<=kClearNear;++i){const float g0=gameVisibility(game,z[i]);if(clampedPower(a*z[i]+b,p)<g0-(g0>=.99f?.02f:.06f))return false;}
        for(float zf:{64.f,128.f,256.f,512.f,768.f,1024.f,end})   // the view distances that exist: up to the game's own end (a flat constant: 1024)
            if(zf<=(flat?1024.f:end))
            if(clampedPower(std::fabs(a*zf+b),p)>clampedPower(std::fabs(game[0]*zf+game[1]),flat?1.f:game[2])*f.transmittance(zf)+.15f)return false;   // against the game's own bare ramp (it too comes back after its end)
        return true;
    };
    const float gp=flat?1.f:game[2];                                         // a flat constant's own exponent says nothing: the fit does not depend on it (one fit per frame serves them all)
    float powers[32];unsigned np=0;
    for(float s:scale)powers[np++]=gp*s;
    for(float p:{3.f,4.f,6.f,8.f,12.f,16.f,24.f,32.f})if(flat||p>gp*3.5f)powers[np++]=p;   // a ramp gets the steeper powers the plateau asks for
    // two starts per power: the line through h^(1/p) over every sample, and over the samples beyond the fade only (it then rises above 1 near the eye: the plateau). Only the kRefine best starts
    // by error are refined (damped Gauss-Newton on the weighted error in visibility itself: the clamp at 1 and the plateau of a game ramp that starts late): a fit costs tens of microseconds.
    FitWork local;FitWork& search=work?*work:local;
    if(!search.matches(f,game)){
        search.count=0;
        using Start=FitWork::Candidate;Start starts[64];unsigned ns=0;
        constexpr unsigned kRefine=sizeof search.refined/sizeof search.refined[0];
        float bestErrors[kRefine]={};unsigned bestCount=0;
        bool bounded=true;for(unsigned i=0;i<N;++i)bounded&=std::isfinite(h[i])&&h[i]>=0&&h[i]<=1;
        for(unsigned pi=0;pi<np&&ns<62;++pi){
            const float p=std::min(powers[pi],flat?kFlatMaxPower:1e30f);if(!(p>.05f))continue;
            float roots[M];
            for(unsigned i=0;i<N;++i)roots[i]=h[i]==1.f?1.f:std::pow(std::max(h[i],0.f),1.f/p);
            for(int start=0;start<2;++start){
                float sz=0,sy=0,szz=0,szy=0,sn=0;
                for(unsigned i=0;i<N;++i){if(start&&z[i]<kClearNear+kClearFade)continue;const float y=roots[i];sz+=z[i];sy+=y;szz+=z[i]*z[i];szy+=z[i]*y;sn+=1;}
                const float den=sn*szz-sz*sz;if(!(sn>=2)||!(std::fabs(den)>1e-9f))continue;
                float a=(sn*szy-sz*sy)/den,b=(sy-a*sz)/sn;
                if(!(b>0))b=1;if(!(a<-1e-7f))a=-1.f/hi;
                // Positive squared terms only grow. Once this exceeds the best
                // six errors already seen, the start cannot reach the six refined.
                const float e=error(a,b,p,bounded&&bestCount==kRefine?bestErrors[kRefine-1]:INFINITY);
                starts[ns++]=Start{a,b,p,e};
                if(bounded&&(bestCount<kRefine||e<bestErrors[kRefine-1])){
                    unsigned at=std::min(bestCount,kRefine-1);
                    while(at&&e<bestErrors[at-1]){bestErrors[at]=bestErrors[at-1];--at;}
                    bestErrors[at]=e;if(bestCount<kRefine)++bestCount;
                }
            }
        }
        for(unsigned r=0;r<kRefine&&r<ns;++r){
            unsigned m=r;for(unsigned i=r+1;i<ns;++i)if(starts[i].e<starts[m].e)m=i;
            std::swap(starts[r],starts[m]);
            float a=starts[r].a,b=starts[r].b;const float p=starts[r].p;
            // Reuse accepted visibility values at the next refinement step.
            float e=starts[r].e,lambda=1e-3f;float cached[M]={},candidateValues[M];bool cachedValid=false;
            for(int it=0;it<10;++it){
                float jaa=0,jab=0,jbb=0,ga=0,gb=0;
                for(unsigned i=0;i<N;++i){
                    const float w=a*z[i]+b;if(!(w>0)||w>=1)continue;
                    const float v=cachedValid?cached[i]:std::pow(w,p);cached[i]=v;if(v>=1)continue;
                    const float d=p*v/w,r2=v-h[i],k=wt[i];
                    jaa+=k*d*z[i]*d*z[i];jab+=k*d*z[i]*d;jbb+=k*d*d;ga+=k*d*z[i]*r2;gb+=k*d*r2;
                }
                cachedValid=true;
                const float m00=jaa*(1+lambda)+1e-12f,m11=jbb*(1+lambda)+1e-12f,det=m00*m11-jab*jab;
                if(!(std::fabs(det)>1e-18f))break;
                const float da=-(m11*ga-jab*gb)/det,db=-(m00*gb-jab*ga)/det;
                const float na=a+da,nb=b+db;
                if(!(na<-1e-7f)||!(nb>0)){lambda*=10;continue;}
                float ne=0;for(unsigned i=0;i<N;++i){const float v=clampedPower(na*z[i]+nb,p),d=v-h[i];candidateValues[i]=v;ne+=wt[i]*d*d;}
                if(ne<e){a=na;b=nb;e=ne;for(unsigned i=0;i<N;++i)cached[i]=candidateValues[i];lambda=std::max(lambda*.3f,1e-6f);}else lambda*=10;
            }
            search.refined[search.count++]=Start{a,b,p,e};
        }
        std::memcpy(search.game,game,sizeof search.game);std::memcpy(search.tau,f.tau,sizeof search.tau);search.ready=true;
    }
    for(unsigned r=0;r<search.count;++r){
        const auto& candidate=search.refined[r];
        const float a=candidate.a,b=candidate.b,p=candidate.p,e=candidate.e;
        if(!(a<-1e-7f)||!(b>0)||!(e<best))continue;                         // nonincreasing, visible and capable of improving the fit
        if(bareBase&&(-b/a<end*.98f||!bareGood(a,b,p)))continue;           // a shader that takes the pow of the bare ramp (no max): the ramp must not reach 0 before the game's own, a negative base is not 0 there and the fog would come back
        best=e;bestFit[0]=a;bestFit[1]=b;bestFit[2]=p;found=true;
    }
    if(!found&&bareBase){ // no free fit reaches 0 late enough (thick fog and a far game end): pin the end at the game's and fit the slope and the power only
        const float E=end*.98f;
        for(unsigned pi=0;pi<np;++pi){
            const float p=std::min(powers[pi],flat?kFlatMaxPower:1e30f);float num=0,dd=0;if(!(p>.05f))continue;
            for(unsigned i=0;i<N;++i){if(z[i]<kClearNear+kClearFade)continue;const float y=std::pow(std::max(h[i],0.f),1.f/p),w=E-z[i];num+=y*w;dd+=w*w;}   // beyond the fade; the line rises above 1 nearer (the plateau)
            if(!(dd>0))continue;
            const float a=-num/dd,b=-a*E;if(!(a<-1e-7f)||!(b>0))continue;
            if(!bareGood(a,b,p))continue;
            const float e=error(a,b,p);if(e<best){best=e;bestFit[0]=a;bestFit[1]=b;bestFit[2]=p;found=true;}
        }
    }
    if(!found){if(bareSkip)*bareSkip=bareBase;return false;}
    for(unsigned i=0;i<3;++i)out[i]=bestFit[i];
    return true;
}
// The fit's visibility at z (what the replayed shader will compute).
inline float fitVisibility(const float fit[3],float z){return std::min(1.f,std::pow(std::max(z*fit[0]+fit[1],0.f),fit[2]));}
// The alpha sub-class mixes lerp(cF, X, fog'): the fog colour that makes the mix at a representative distance equal what Northlight's fog does to the game's
// result, X g T + cF (1-g) T + S (1-T), with fog' = g T. cF' = (cF (1-g) T + S (1-T)) / (1 - g T) at z = half of the fitted range. Where the fit leaves the denominator small there is little
// fog to add: the colour moves continuously (smoothstep over den 0..0.08) to the game's cF, or to the in-scatter colour S for a flat game constant, whose cF is stale (the game does not
// maintain it with its fog off).
inline void emitColor(const Frame& f,const float game[3],const float fit[3],const float cF[3],float out[3]){
    const bool flat=flatConstant(game);
    const float hi=game[0]<-1e-7f?std::min(-game[1]/game[0],kRange):kRange,zm=hi*.5f;
    const float g=gameVisibility(game,zm),T=foldedTransmittance(f,zm),gp=fitVisibility(fit,zm),den=1-gp;
    const float u=std::clamp(den/.08f,0.f,1.f),w=u*u*(3-2*u);
    for(unsigned c=0;c<3;++c){
        const float rest=flat?f.scatter[c]:cF[c];                          // what the colour tends to where there is no fog to add
        const float v=den>1e-4f?std::clamp((cF[c]*(1-g)*T+f.scatter[c]*(1-T))/den,0.f,std::max(cF[c],4.f)):rest;
        out[c]=rest+(v-rest)*w;
    }
}
}
