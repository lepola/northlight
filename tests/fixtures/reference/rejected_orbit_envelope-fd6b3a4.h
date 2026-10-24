#pragma once
// Rejected prototype, retained only for the physical-quality regression.
// Full transport exposes worse SH error; this must never be wired into runtime.
#include "world_streaming.h"
#include <limits>
namespace NorthlightWorldStreaming {
// An optional stationary-camera envelope. The caller must supply a recently
// captured self, retain eye-based coverage tests, and admit the enlarged crop.
// Merely moving the old crop to the pivot would lose GI transport geometry.
struct OrbitEnvelope {
    static constexpr float Radius=60.f,StationaryDistance=2.f;
    static constexpr uint32_t StableMs=1000,GapMs=500;
    NorthlightGI::Vec3 candidate;std::string map;uint32_t since=0,at=0;bool valid=false;
    void reset(){valid=false;map.clear();}
    bool select(const std::string& currentMap,NorthlightGI::Vec3 eye,
                const NorthlightGI::Vec3* capturedSelf,uint32_t now,NorthlightGI::Vec3& center){
        if(!capturedSelf||!within(eye,eye,0)||!within(*capturedSelf,*capturedSelf,0)){
            reset();return false;
        }
        if(!valid||map!=currentMap||uint32_t(now-at)>GapMs||!within(candidate,*capturedSelf,StationaryDistance)){
            candidate=*capturedSelf;map=currentMap;since=now;valid=true;
        }
        at=now;
        if(uint32_t(now-since)<StableMs||!within(candidate,eye,Radius))return false;
        center=candidate;return true;
    }
    static NorthlightGI::Vec3 low(NorthlightGI::Vec3 center){return bounds(center,false);}
    static NorthlightGI::Vec3 high(NorthlightGI::Vec3 center){return bounds(center,true);}
private:
    static NorthlightGI::Vec3 bounds(NorthlightGI::Vec3 center,bool high){
        const float infinity=std::numeric_limits<float>::infinity(),direction=high?infinity:-infinity,sign=high?1.f:-1.f;
        return {std::nextafter(center.x+sign*(288.f+Radius),direction),
                std::nextafter(center.y+sign*(288.f+Radius),direction),
                std::nextafter(center.z+sign*(320.f+Radius),direction)};
    }
};
}
