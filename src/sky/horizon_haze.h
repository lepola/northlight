#pragma once
#include <algorithm>
#include <cmath>
#include <string>

// Horizon haze (WorldComposite): CPU policy only, no D3D object or game address.
// Terrain haze starts at a fraction of the GAME's own fog end (T=0 of the
// proven terrain c12 / WMO c30 fog), never at a fixed distance, so it only
// reshapes the last stretch before the stock fog wall. The sky band follows
// world elevation. One optical depth serves both, so a far ridge meets the
// sky without a step. Strength 0 or fog switched off uploads optical depth 0:
// the shader then returns the scene colour unchanged.
namespace NorthlightHorizonHaze {
inline constexpr float MaxOpticalDepth=1.4f; // HorizonHaze=100; the default 50 gives .7
inline constexpr float Log2e=1.44269504f;
inline constexpr float MinimumEnd=100;       // yards; never haze nearer terrain
inline constexpr float SmoothingSeconds=1;   // terrain <-> WMO fog contexts differ
inline constexpr float SunLobe=.06f;         // x HG(g=.6) azimuth lobe; the shader caps the lift at .25 (.03 -> .06)
// Shader literals of horizonHaze(): lift=min(|lobe|*HG,LiftCap) with HG(g=.6) peaking at
// .64/.4^3 = 10 toward the sun (+.1% for the GPU pow), tinting the fog colour by the lift colour
// (c35.yzw since 0.3.163: the glow hue; Warm is today's colour and the default).
// constants() limits the lobe so that no haze channel exceeds 1.
inline constexpr float LiftCap=.25f,LobePeak=10.01f;
inline constexpr float Warm[3]={1,.8f,.55f};
inline constexpr float ColourJump=1.3f;      // fog end grows by more than 30%: new fog regime

// T=(y+x*z*Pz)^p reaches 0 at signed view Z z=y/(-x*Pz). Fog start can be
// negative (Tanaris), so only the end is used. 0 = unknown, disabled or no end.
inline float fogEnd(const float* parameters,float projectedForward){
    if(!parameters||!(parameters[3]>.5f)||!std::isfinite(projectedForward))return 0;
    const float slope=-parameters[0]*projectedForward;
    if(!(slope>0)||!(parameters[1]>0))return 0;
    const float end=parameters[1]/slope;
    return std::isfinite(end)?end:0;
}

// Per-map fog end and colour. Unknown fog on the same map holds the last
// value; a new map snaps. Before any known fog the end is the far clip and the
// colour is unknown (no haze until the game's fog colour is seen). Asymmetric:
// a LONGER fog end (surfacing, leaving an interior, clearer zone) snaps, so
// the start never lags inside the mid landscape; a longer end by more than 30%
// also snaps the colour. A shorter end approaches over about 1 s.
struct State {
    bool valid=false,colorKnown=false;std::string map;double time=0;
    float end=0,color[3]={0,0,0};
    void update(const std::string& nextMap,const float* parameters,const float* fogColor,float projectedForward,double now){
        const float measured=fogEnd(parameters,projectedForward);
        const bool reset=!valid||map!=nextMap||!(now>=time);
        const float t=reset?1.f:float(1-std::exp(-std::min(now-time,.25)/SmoothingSeconds));
        if(reset){end=0;colorKnown=false;}
        const bool jump=measured>0&&!(end*ColourJump>=measured);
        if(measured>0)end=measured>end?measured:end+(measured-end)*t;
        if(measured>0&&fogColor){
            for(unsigned i=0;i<3;++i){
                const float c=std::isfinite(fogColor[i])?std::clamp(fogColor[i],0.f,1.f):0.f;
                color[i]=colorKnown&&!jump?color[i]+(c-color[i])*t:c;
            }
            colorKnown=true;
        }
        valid=true;map=nextMap;time=now;
    }
    // Clamped to [100, far clip]; the far clip before any known fog end.
    float effectiveEnd(float farZ)const{
        const float limit=std::isfinite(farZ)&&farZ>0?farZ:MinimumEnd; /* no `far`: a Windows macro */
        const float e=end>0?end:limit;
        return std::clamp(e,std::min(MinimumEnd,limit),limit);
    }
};

struct Settings { unsigned strength=50,startPercent=75,bandDegrees=6,terrain=1; };
// c34 (final pass only): haze RGB, optical depth*log2(e).
// c57.yzw: terrain start view Z, 1/(ramp length) (0 = sky only), log2(e)/sin(band).
// c67.zw: horizontal sun direction x lobe strength (length includes cos(sun elevation)).
// c35.yzw: lift colour, the glow hue (sun_hue.h horizonLift); Warm when none is given.
struct Constants { float haze[4]={0,0,0,0};float shape[3]={0,0,0};float sun[2]={0,0};float lift[3]={Warm[0],Warm[1],Warm[2]}; };
inline Constants constants(const State& s,const Settings& q,float farZ,float zoneScale,bool fogOn,
                           bool sunValid,const float* sunDirection,float sunWeight,const float* liftColour=nullptr,float tauScale=1.f){ /* 0.3.198 (rain): tauScale x1 = unchanged */
    Constants c;
    if(liftColour)for(unsigned i=0;i<3;++i)c.lift[i]=std::isfinite(liftColour[i])?std::clamp(liftColour[i],0.f,1.f):Warm[i];
    const float end=s.effectiveEnd(farZ);
    const float start=end*float(std::clamp(q.startPercent,50u,95u))*.01f;
    c.shape[0]=start;c.shape[1]=q.terrain?1.f/std::max(end-start,1.f):0.f;
    const float band=float(std::clamp(q.bandDegrees,2u,15u))*3.14159265f/180;
    c.shape[2]=Log2e/std::sin(band);
    const float scale=std::isfinite(zoneScale)?std::clamp(zoneScale,0.f,4.f):1.f;
    const float tau=MaxOpticalDepth*float(std::min(q.strength,100u))*.01f*scale*(std::isfinite(tauScale)&&tauScale>0?tauScale:1.f);
    if(!fogOn||!s.colorKnown||!(tau>0))return c; /* optical depth 0: exact scene colour */
    for(unsigned i=0;i<3;++i)c.haze[i]=s.color[i];
    c.haze[3]=tau*Log2e;
    if(sunValid&&sunDirection&&std::isfinite(sunWeight)&&std::isfinite(sunDirection[0])&&std::isfinite(sunDirection[1])){
        // Haze colour <= 1: fog*(1+lift*colour) per channel. Fog up to .8 never needs it
        // (the shader cap suffices); brighter fog limits |lobe|*LobePeak instead.
        float liftLimit=LiftCap;
        for(unsigned i=0;i<3;++i)if(s.color[i]>0&&c.lift[i]>0)liftLimit=std::min(liftLimit,(1/s.color[i]-1)/c.lift[i]);
        const float sunHorizontal=std::sqrt(sunDirection[0]*sunDirection[0]+sunDirection[1]*sunDirection[1]);
        float k=SunLobe*std::clamp(sunWeight,0.f,1.f);
        if(liftLimit<LiftCap&&k*sunHorizontal*LobePeak>liftLimit)k=liftLimit/(sunHorizontal*LobePeak);
        c.sun[0]=sunDirection[0]*k;c.sun[1]=sunDirection[1]*k;
    }
    return c;
}
}
