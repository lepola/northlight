#include "wmo_lamp_shadow.h"
#include "point_light_shadow.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>
struct Batch{unsigned material=0,count=8;bool terrain=false;NorthlightGI::Vec3 boundsLow,boundsHigh;uint64_t lampSourceId=0;};
int main(){
    using namespace NorthlightLocalLights;using NorthlightWmoLampShadow::unique;
    Light source{};source.position[0]=10;source.diffuse[0]=.3f;source.attenuationStart=.8f;source.attenuationEnd=5.f;source.kind=3;source.flags=CastShadow|GlassEmitter;source.sourceId=1;
    assert(valid(source));auto invalid=source;invalid.kind=1;assert(!valid(invalid));invalid=source;invalid.flags=GlassEmitter;assert(!valid(invalid));
    std::vector<Batch> batches={{0,8,false,{9.5f,-.5f,-.5f},{10.5f,.5f,.5f},1},
                                {1,8,false,{9.5f,-.5f,-.5f},{10.5f,.5f,.5f},0},
                                {0,8,false,{12.5f,-.5f,-.5f},{13.5f,.5f,.5f},2}};
    assert(unique(source,batches)==0); // ordinary glass, wall and other fixtures stay
    auto other=source;other.flags=CastShadow;assert(unique(other,batches)==size_t(-1));
    other=source;other.sourceId=2;other.position[0]=13;assert(unique(other,batches)==2);
    auto native=source;native.kind=1;native.flags|=Baked;native.attenuationStart=0.f;
    assert(valid(native)&&unique(native,batches)==0);
    native.position[0]=11.f;assert(unique(native,batches)==size_t(-1));
    native=source;native.kind=2;assert(!valid(native));
    batches[0].count=7;assert(unique(source,batches)==size_t(-1));batches[0].count=8;
    batches[0].terrain=true;assert(unique(source,batches)==size_t(-1));batches[0].terrain=false;
    batches[0].boundsHigh.x=12;assert(unique(source,batches)==size_t(-1));batches[0].boundsHigh.x=10.5f;
    batches.push_back(batches[0]);assert(unique(source,batches)==size_t(-1));batches.pop_back();
    batches[0].lampSourceId=2;assert(unique(source,batches)==size_t(-1));batches[0].lampSourceId=1; // coincident other owner cannot match
    batches[0].boundsLow.x=std::numeric_limits<float>::quiet_NaN();assert(unique(source,batches)==size_t(-1));
    NorthlightPointShadow::RefreshSchedule schedule;schedule.commit(5,source,1);assert(!schedule.due(6,source,1,256,false));
    other=source;other.attenuationStart+=.1f;assert(schedule.due(6,other,1,256,false));other=source;other.flags=CastShadow;assert(schedule.due(6,other,1,256,false));
}
