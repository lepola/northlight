#pragma once
#include "celestial_profiles.h"
#include "celestial_time_warp.h"

// Immutable startup configuration. Extended coverage is TERRAIN ONLY; model
// streaming, GI, shadow resolution/depth and upload budgets remain unchanged.
namespace NorthlightRegionalShadow {
constexpr float DefaultRadius=928.f,MaximumRadius=4096.f;
struct Table {
    std::map<std::pair<std::string,uint32_t>,float> zones;
    float at(const std::string& map,uint32_t zone)const {
        auto i=zones.find({map,zone});return i==zones.end()?DefaultRadius:i->second;
    }
};
inline bool parse(std::istream& input,Table& out,std::string& error){
    Table next;std::string line,map;uint32_t zone=0;unsigned number=0;bool assigned=true;
    auto fail=[&](){error="invalid shadow range at line "+std::to_string(number);return false;};
    while(std::getline(input,line)){
        if(++number>1024)return fail();
        line=NorthlightCelestialProfiles::trim(line.substr(0,line.find('#')));if(line.empty())continue;
        if(line.front()=='['&&line.back()==']'){
            if(!assigned)return fail();
            auto section=line.substr(1,line.size()-2);auto colon=section.find(':');
            if(colon==std::string::npos)return fail();
            map=section.substr(0,colon);auto id=section.substr(colon+1);
            if((map!="Azeroth"&&map!="Kalimdor"&&map!="Expansion01"&&map!="Northrend")||id.empty()||id.size()>6||id.find_first_not_of("0123456789")!=std::string::npos)return fail();
            zone=uint32_t(std::stoul(id));if(!zone||next.zones.count({map,zone}))return fail();assigned=false;
        }else{
            auto eq=line.find('=');if(assigned||eq==std::string::npos||NorthlightCelestialProfiles::trim(line.substr(0,eq))!="terrain_radius")return fail();
            float radius;std::string extra;std::istringstream value(line.substr(eq+1));
            if(!(value>>radius)||!std::isfinite(radius)||radius<DefaultRadius||radius>MaximumRadius||(value>>extra))return fail();
            next.zones[{map,zone}]=radius;assigned=true;
        }
    }
    if(input.bad()||!assigned)return fail();out=std::move(next);error.clear();return true;
}
inline bool load(const std::string& path,Table& out,std::string& error){
    std::ifstream f(path,std::ios::binary);if(!f){error="no override; 928-unit defaults";return false;}
    std::string text;if(!NorthlightCelestialProfiles::readIniText(f,text,error))return false;std::istringstream decoded(text);return parse(decoded,out,error);
}
// Keep the entire original cube. Outside it, retain a vertical corridor toward
// the shared OWNED orbital azimuth for ALL elevations, day and night. 512u
// half-width covers far cached radius240 + orbit80 + publication96 + diagonal
// receiver extent. No camera direction or instantaneous altitude dependency.
// Test whole tile/model bounds: a retained terrain tile is never sparsely cut,
// so fixedTerrainChunks still denotes complete chunks.
inline bool selected(NorthlightGI::Vec3 center,NorthlightGI::Vec3 low,NorthlightGI::Vec3 high){
    if(high.x>=center.x-DefaultRadius&&low.x<=center.x+DefaultRadius&&
       high.y>=center.y-DefaultRadius&&low.y<=center.y+DefaultRadius)return true;
    const double ax=std::cos(NorthlightCelestialOrbit::kAzimuthRadians),ay=std::sin(NorthlightCelestialOrbit::kAzimuthRadians);
    const double dx=(double(low.x)+high.x)*.5-center.x,dy=(double(low.y)+high.y)*.5-center.y;
    const double ex=(double(high.x)-low.x)*.5,ey=(double(high.y)-low.y)*.5;
    const double side=-ay*dx+ax*dy,sideExtent=std::fabs(ay)*ex+std::fabs(ax)*ey;
    const double along=ax*dx+ay*dy,alongExtent=std::fabs(ax)*ex+std::fabs(ay)*ey;
    return std::fabs(side)<=512+sideExtent&&along+alongExtent>=-512;
}
inline bool selectedChunk(NorthlightGI::Vec3 center,NorthlightGI::Vec3 low,NorthlightGI::Vec3 high){
    // Select complete ADT chunks, not individual triangles: this preserves the
    // fixed-chunk ownership contract at the corridor boundary. Unexpected
    // cross-chunk triangles are conservatively kept, never guessed away.
    const double zero=NorthlightRegionalFog::WorldZero,size=NorthlightRegionalFog::ChunkSize;
    const double gx=std::floor((zero-(double(low.x)+high.x)*.5)/size);
    const double gy=std::floor((zero-(double(low.y)+high.y)*.5)/size);
    NorthlightGI::Vec3 lo(float(zero-(gx+1)*size),float(zero-(gy+1)*size),low.z);
    NorthlightGI::Vec3 hi(float(zero-gx*size),float(zero-gy*size),high.z);
    if(low.x<lo.x-.03f||low.y<lo.y-.03f||high.x>hi.x+.03f||high.y>hi.y+.03f)return true;
    return selected(center,lo,hi);
}
} // namespace NorthlightRegionalShadow
