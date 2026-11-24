#pragma once
// Developer-only zone tuning (NORTHLIGHT_DEV_TUNING builds): the overlay file northlight-tuning.ini,
// applied on top of the shipped celestial / shadow-range tables, and the state JSON the tuner reads.
// Portable policy only: no D3D, no file polling (that lives in world_renderer.h under the define).
#include "celestial_profiles.h"
#include "regional_shadow_range.h"
#include <array>
#include <cstdio>
#include <cstdlib>

namespace NorthlightZoneTuning {
// The dev DLL embeds this (logged at startup); renderer/build_packages.py refuses any DLL containing it.
constexpr char Marker[]="NORTHLIGHT DEV TUNING BUILD";
// FogAddMax: the policy's own peak is Duskwood night (.003+.015+.008)*1.2 = .031 extinction per unit; .05 leaves headroom above it.
constexpr float RadiusMin=NorthlightRegionalShadow::DefaultRadius,RadiusMax=NorthlightRegionalShadow::MaximumRadius,FogScaleMax=4,FogAddMax=.05f;
struct ZoneOverride {
    std::vector<std::pair<std::string,std::string>> celestial; // sun_/moon_ keys, validated by NorthlightCelestialProfiles::assignKey, in file order
    float radius=-1,fogDay=-1,fogNight=-1,addDay=-1,addNight=-1; // <0 = not overridden; add* is density added after the scale
    unsigned count()const {return unsigned(celestial.size())+(radius>=0)+(fogDay>=0)+(fogNight>=0)+(addDay>=0)+(addNight>=0);}
    bool worker()const {return radius>=0||fogDay>=0||fogNight>=0||addDay>=0||addNight>=0;}
    bool sameWorker(const ZoneOverride& o)const {return radius==o.radius&&fogDay==o.fogDay&&fogNight==o.fogNight&&addDay==o.addDay&&addNight==o.addNight;}
};
struct Overrides {
    bool enabled=true;unsigned revision=0; // revision: set by the renderer when it publishes a parse
    ZoneOverride fallback;                                      // [default]: celestial keys only
    std::map<std::pair<std::string,uint32_t>,ZoneOverride> zones;
    unsigned count()const {unsigned n=fallback.count();for(const auto& z:zones)n+=z.second.count();return n;}
    // The terrain_radius / ground fog parts the geometry worker consumes (a change needs a rebuild).
    bool sameWorker(const Overrides& o)const {
        if(enabled!=o.enabled)return false;
        auto worker=[](const ZoneOverride& z){return z.worker();}; // celestial-only zones are no worker difference
        for(const auto& z:zones){auto i=o.zones.find(z.first);if(i==o.zones.end()?worker(z.second):!z.second.sameWorker(i->second))return false;}
        for(const auto& z:o.zones)if(!zones.count(z.first)&&worker(z.second))return false;
        return true;
    }
};
inline bool parseNumber(const std::string& text,float lo,float hi,float& out){
    std::istringstream in(text);std::string extra;float v;
    if(!(in>>v)||!std::isfinite(v)||v<lo||v>hi||(in>>extra))return false;out=v;return true;
}
// Atomic like the shipped parser: any error keeps `out` untouched. Sections: [tuning], [default], [Map:ZoneID], each at most once.
inline bool parse(std::istream& input,Overrides& out,std::string& error){
    Overrides next;ZoneOverride* zone=nullptr;enum {None,Tuning,Default,Zone} kind=None;
    std::set<std::string> keys,sections;std::string line;unsigned number=0;
    auto fail=[&](const char* why){error="line "+std::to_string(number)+": "+why;return false;};
    while(std::getline(input,line)){
        if(++number>8192)return fail("file too large");
        line=NorthlightCelestialProfiles::trim(line.substr(0,line.find('#')));if(line.empty())continue;
        if(line.front()=='['&&line.back()==']'){
            std::string section=NorthlightCelestialProfiles::trim(line.substr(1,line.size()-2));keys.clear();zone=nullptr;
            if(!sections.insert(section).second)return fail("duplicate section");
            if(section=="tuning"){kind=Tuning;continue;}
            if(section=="default"){kind=Default;zone=&next.fallback;continue;}
            auto colon=section.find(':');if(colon==std::string::npos)return fail("unknown section");
            std::string map=NorthlightCelestialProfiles::trim(section.substr(0,colon)),id=NorthlightCelestialProfiles::trim(section.substr(colon+1));
            if(map!="Azeroth"&&map!="Kalimdor"&&map!="Expansion01"&&map!="Northrend")return fail("unsupported map");
            if(id.empty()||id.find_first_not_of("0123456789")!=std::string::npos||id.size()>6||!std::stoul(id))return fail("invalid zone ID");
            kind=Zone;zone=&next.zones[{map,uint32_t(std::stoul(id))}];continue;
        }
        auto eq=line.find('=');if(kind==None||eq==std::string::npos)return fail("expected key=value in section");
        std::string key=NorthlightCelestialProfiles::trim(line.substr(0,eq)),value=NorthlightCelestialProfiles::trim(line.substr(eq+1));
        if(!keys.insert(key).second)return fail("duplicate key");
        float v;
        if(kind==Tuning){
            if(key!="enabled")return fail("unknown key");
            if(!parseNumber(value,0,1,v)||(v!=0&&v!=1))return fail("enabled must be 0 or 1");
            next.enabled=v==1;continue;
        }
        float* zoneKey=key=="terrain_radius"?&zone->radius:key=="ground_fog_day_scale"?&zone->fogDay:key=="ground_fog_night_scale"?&zone->fogNight:
            key=="ground_fog_day_add"?&zone->addDay:key=="ground_fog_night_add"?&zone->addNight:nullptr;
        if(zoneKey){
            if(kind==Default)return fail("zone-only key in [default]");
            const bool radius=zoneKey==&zone->radius,add=zoneKey==&zone->addDay||zoneKey==&zone->addNight;
            if(!parseNumber(value,radius?RadiusMin:0.f,radius?RadiusMax:add?FogAddMax:FogScaleMax,v))return fail("invalid numeric value");
            *zoneKey=v;continue;
        }
        NorthlightCelestialProfiles::Profile scratch;
        if(const char* why=NorthlightCelestialProfiles::assignKey(scratch,key,value))return fail(why);
        zone->celestial.emplace_back(key,value);
    }
    if(input.bad()){error="read failed";return false;}
    out=std::move(next);error.clear();return true;
}
inline void applyKeys(NorthlightCelestialProfiles::Profile& p,const ZoneOverride& o,const std::set<std::string>* skip=nullptr){
    for(const auto& kv:o.celestial)if(!skip||!skip->count(kv.first))NorthlightCelestialProfiles::assignKey(p,kv.first,kv.second); // validated at parse
}
// The effective celestial table. A shipped zone section is a copy of [default] taken at parse time, so a [default]
// override lands on the fallback and on every zone value whose key that zone did not set itself (Table::explicitKeys);
// zone overrides then go on top. A zone without a shipped section starts from the effective fallback.
inline NorthlightCelestialProfiles::Table applyCelestial(const NorthlightCelestialProfiles::Table& shipped,const Overrides& o){
    auto out=shipped;if(!o.enabled)return out;
    applyKeys(out.fallback,o.fallback);
    for(auto& z:out.zones){auto e=shipped.explicitKeys.find(z.first);applyKeys(z.second,o.fallback,e==shipped.explicitKeys.end()?nullptr:&e->second);}
    for(const auto& z:o.zones){
        auto it=out.zones.find(z.first);
        if(it==out.zones.end())it=out.zones.emplace(z.first,out.fallback).first;
        applyKeys(it->second,z.second);
    }
    for(const auto& child:out.parents){
        NorthlightCelestialProfiles::inheritParent(out,child.first,child.second);
        auto local=o.zones.find(child.first);
        if(local!=o.zones.end())applyKeys(out.zones.at(child.first),local->second);
    }
    return out;
}
inline const ZoneOverride* zoneOverride(const Overrides& o,const std::string& map,uint32_t zone){
    if(!o.enabled)return nullptr;auto i=o.zones.find({map,zone});return i==o.zones.end()?nullptr:&i->second;
}
inline float terrainRadius(const Overrides& o,float shipped,const std::string& map,uint32_t zone){
    auto z=zoneOverride(o,map,zone);return z&&z->radius>=0?z->radius:shipped;
}
// Ground fog density of an outdoor cell: policy value * scale + add (the hook of NorthlightRegionalFog::buildField).
inline void fogScale(const Overrides& o,const std::string& map,uint32_t zone,float& day,float& nightExtra){
    auto z=zoneOverride(o,map,zone);if(!z)return;
    if(z->fogDay>=0)day*=z->fogDay;if(z->fogNight>=0)nightExtra*=z->fogNight;
    if(z->addDay>=0)day+=z->addDay;if(z->addNight>=0)nightExtra+=z->addNight;
}
struct Values {NorthlightCelestialProfiles::Profile profile;float terrainRadius=NorthlightRegionalShadow::DefaultRadius,fogDay=1,fogNight=1,fogDayAdd=0,fogNightAdd=0;};
inline Values baseValues(const NorthlightCelestialProfiles::Table& shipped,const NorthlightRegionalShadow::Table& ranges,const std::string& map,uint32_t zone){
    Values v;v.profile=shipped.at(map,zone);v.terrainRadius=ranges.at(map,zone);return v;
}
inline Values effectiveValues(const Values& base,const NorthlightCelestialProfiles::Table& effective,const Overrides& o,const std::string& map,uint32_t zone){
    Values v=base;v.profile=effective.at(map,zone);v.terrainRadius=terrainRadius(o,base.terrainRadius,map,zone);
    auto z=zoneOverride(o,map,zone);if(z){v.fogDay=z->fogDay>=0?z->fogDay:base.fogDay;v.fogNight=z->fogNight>=0?z->fogNight:base.fogNight;v.fogDayAdd=z->addDay>=0?z->addDay:0;v.fogNightAdd=z->addNight>=0?z->addNight:0;}
    return v;
}
// Can the regional ground fog policy add any density in this zone at all? (False: the ground fog scales are no-ops.)
inline bool groundFogActive(uint32_t zone){auto t=NorthlightRegionalFog::policy(zone,0.f,1.f,1.f,false);return t.day>0||t.nightExtra>0;}
struct State {
    std::string build,map,error;unsigned tick=0,revision=0,builtRevision=0,loaded=0;bool inWorld=false,enabled=true,groundFogActive=false;
    uint32_t zone=0;float camera[3]={},night=0,terrainReach=0;Values base,effective;
};
inline std::string jsonNumber(float v){char b[32];std::snprintf(b,sizeof b,"%.6g",std::isfinite(v)?double(v):0.);return b;}
inline std::string jsonString(const std::string& s){
    std::string o="\"";
    for(unsigned char c:s){if(c=='"'||c=='\\'){o+='\\';o+=char(c);}else if(c<0x20){char b[8];std::snprintf(b,sizeof b,"\\u%04x",c);o+=b;}else o+=char(c);}
    return o+"\"";
}
inline std::string valuesJson(const Values& v){
    const auto& p=v.profile;std::string o="{";
    auto triple=[&](const char* k,const float* c){o+=std::string("\"")+k+"\":["+jsonNumber(c[0])+","+jsonNumber(c[1])+","+jsonNumber(c[2])+"],";};
    auto one=[&](const char* k,float x){o+=std::string("\"")+k+"\":"+jsonNumber(x)+",";};
    for(unsigned s=0;s<2;++s){
        const std::string b=s?"moon_":"sun_";
        triple((b+"disc").c_str(),p.disc[s]);one((b+"disc_gain").c_str(),p.gain[s]);triple((b+"light").c_str(),p.light[s]);
        one((b+"light_mix").c_str(),p.mix[s]);one((b+"light_strength").c_str(),p.strength[s]);one((b+"fog_gain").c_str(),p.fogGain[s]);
        one((b+"fog_height_scale").c_str(),p.fogHeightScale[s]);one((b+"horizon_haze").c_str(),p.horizonHaze[s]);
        one((b+"glow_strength").c_str(),p.glowStrength[s]);triple((b+"glow").c_str(),p.glow[s]);one((b+"glow_mix").c_str(),p.glowMix[s]);
    }
    one("terrain_radius",v.terrainRadius);one("ground_fog_day_scale",v.fogDay);one("ground_fog_night_scale",v.fogNight);one("ground_fog_day_add",v.fogDayAdd);o+="\"ground_fog_night_add\":"+jsonNumber(v.fogNightAdd)+"}";
    return o;
}
inline std::string stateJson(const State& s){
    auto b=[](bool v){return v?"true":"false";};
    return "{\"version\":1,\"build\":"+jsonString(s.build)+",\"tick\":"+std::to_string(s.tick)+",\"inWorld\":"+b(s.inWorld)+",\"map\":"+jsonString(s.map)+
        ",\"zone\":"+std::to_string(s.zone)+",\"camera\":["+jsonNumber(s.camera[0])+","+jsonNumber(s.camera[1])+","+jsonNumber(s.camera[2])+"],\"night\":"+jsonNumber(s.night)+
        ",\"overridesEnabled\":"+b(s.enabled)+",\"overridesLoaded\":"+std::to_string(s.loaded)+",\"overridesError\":"+jsonString(s.error)+
        ",\"overridesRevision\":"+std::to_string(s.revision)+",\"builtRevision\":"+std::to_string(s.builtRevision)+",\"base\":"+valuesJson(s.base)+",\"effective\":"+valuesJson(s.effective)+
        ",\"terrainReach\":"+jsonNumber(s.terrainReach)+",\"groundFogActive\":"+b(s.groundFogActive)+"}\n";
}
} // namespace NorthlightZoneTuning
