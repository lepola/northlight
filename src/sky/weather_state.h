#pragma once
// 0.3.198 (rain): the smoothed weather state of Forever-style rain. Pure: no D3D, no allocation, no clock.
// The renderer feeds one Sample per frame (draws counted by the draw hook's single pointer comparison,
// see weather_detect.h) and the frame's dt; every consumer reads State (WorldRenderer::weather()):
// the later phases - world-space rain layer (intensity, blend), lamp-lit drops (intensity) and lens drops (blend, secondsSinceSeen) - take it as a plain POD and need nothing else.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NorthlightWeather {
enum class Kind:std::uint8_t{None,Rain,Snow,Sand}; /* Sand is reserved: never detected in this phase */
inline const char* kindName(Kind k){switch(k){case Kind::Rain:return "rain";case Kind::Snow:return "snow";case Kind::Sand:return "sand";default:return "none";}}
// What the draw hook counted in one frame for the hot candidate (kind = that candidate's kind).
struct Sample{Kind kind=Kind::None;std::uint32_t primitives=0,draws=0;};
// kind: the kind whose blend is non-zero (None only once fully faded out); intensity 0..1 (smoothed primitive
// count over kFullPrimitives); blend 0..1 (smoothstep fade of the layer in/out);
// secondsSinceSeen: clamped seconds since a weather draw;
// primitives: this frame's count for kind.
struct State{Kind kind=Kind::None;float intensity=0,blend=0,secondsSinceSeen=0;std::uint32_t primitives=0;};

constexpr float kMaxDt=0.1f;            /* Alt+Tab and loading screens are one step, never a jump */
constexpr float kHoldSeconds=2.5f;      /* no weather draws for this long keeps the state, then ramps out */
constexpr float kBlendInSeconds=4.0f,kBlendOutSeconds=6.0f;
constexpr float kIntensityTau=1.5f;     /* EMA time constant of primitives / kFullPrimitives */
constexpr float kFullPrimitives=42000.0f; /* intensity 1: heavy rain (.wchange 1 1) measured at 7 draws x 6143 = ~43k primitives per frame */
constexpr float kBlendLogLow=0.01f,kBlendLogHigh=0.99f;

inline float smoothstep01(float x){x=x<0?0:x>1?1:x;return x*x*(3-2*x);}

class Tracker{
public:
    void frame(const Sample& sample,float dtSeconds){
        const float dt=dtSeconds>0?std::min(dtSeconds,kMaxDt):0.0f; /* NaN and negatives are 0 */
        const bool seen=sample.kind!=Kind::None&&sample.kind!=Kind::Sand&&sample.draws>0;
        everSeen=everSeen||seen;
        s.secondsSinceSeen=seen||!everSeen?0.0f:std::min(s.secondsSinceSeen+dt,kHoldSeconds*4); /* 0 until the first weather draw */
        if(seen){
            if(kind_==Kind::None)begin(sample);
            else if(kind_!=sample.kind)pending=sample.kind; /* the old kind ramps fully out first */
            else{pending=Kind::None;const float raw=std::min(1.0f,float(sample.primitives)/kFullPrimitives);s.intensity+=(raw-s.intensity)*(1-std::exp(-dt/kIntensityTau));}
        }
        if(kind_!=Kind::None){
            if(pending!=Kind::None||s.secondsSinceSeen>kHoldSeconds)phase-=dt/kBlendOutSeconds;
            else if(seen)phase+=dt/kBlendInSeconds; /* a gap shorter than the hold freezes the ramp */
            phase=std::min(1.0f,phase);
            if(phase<=0){phase=0;kind_=Kind::None;s.intensity=0;const Kind next=pending;pending=Kind::None;if(next!=Kind::None&&seen&&sample.kind==next)begin(sample);}
        }
        s.kind=kind_;s.blend=kind_==Kind::None?0.0f:smoothstep01(phase);s.primitives=seen&&sample.kind==kind_?sample.primitives:0;
    }
    const State& state()const{return s;}
private:
    void begin(const Sample& sample){kind_=sample.kind;pending=Kind::None;phase=0;s.intensity=std::min(1.0f,float(sample.primitives)/kFullPrimitives);}
    State s;Kind kind_=Kind::None,pending=Kind::None;float phase=0;bool everSeen=false;
};
}
