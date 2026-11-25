#pragma once
// Client-local artistic palettes, selected from authored ADT zone IDs. Portable
// policy only: file loading at startup; region loading belongs to the worker.
#include "regional_fog.h"
#include "world_tone_curves.h"
#include "celestial_context.h"
#include "quality_settings.h"
#include <fstream>
#include <sstream>
#include <map>
#include <set>

namespace NorthlightCelestialProfiles {
constexpr size_t MaxProfileBytes=4*1024*1024;
constexpr unsigned MaxProfileRows=65536;
struct Profile {
    // Fog colour per time of day, at the grade's Daylight, Dusk and Night stops and blended by the same clock. The game's
    // fog colour changes through the day, so one tint cannot suit every hour. Brightness scales the fog a tint leaves alone.
    float fogTint[NorthlightTone::StopCount][3]={{1,1,1},{1,1,1},{1,1,1}},fogTintStrength[NorthlightTone::StopCount]={0,0,0};
    float fogBrightness[NorthlightTone::StopCount]={1,1,1},shadowContrast=0;
    float toneStrength=0;
    NorthlightTone::Timeline grade{};
    float disc[2][3]={{1,.96f,.88f},{.94f,.97f,1}};
    float gain[2]={3.5f,1.1f};
    float light[2][3]={{1,1,1},{1,1,1}};
    float mix[2]={0,0},strength[2]={1,1};
    // Independent artistic volume controls. Identity defaults retain all
    // existing zones; neither control changes surface light or the sun disc.
    float fogGain[2]={1,1},fogHeightScale[2]={1,1};
    // Horizon haze optical-depth multiplier (horizon_haze.h), day/night. 1 = the global strength.
    float horizonHaze[2]={1,1};
    // Glow (fog shafts / aureole) colour of the source, independent of light_strength: its own strength, and an optional hue
    // (glowMix 0 = follows the light hue and mix, 1 = the glow hue at the authored luminance).
    float glowStrength[2]={1,1},glow[2][3]={{1,1,1},{1,1,1}},glowMix[2]={0,0};
};
inline Profile blend(const Profile& a,const Profile& b,float t){
    Profile p; t=std::clamp(t,0.f,1.f);
    for(unsigned s=0;s<NorthlightTone::StopCount;++s){
        for(unsigned c=0;c<3;++c)p.fogTint[s][c]=a.fogTint[s][c]+(b.fogTint[s][c]-a.fogTint[s][c])*t;
        p.fogTintStrength[s]=a.fogTintStrength[s]+(b.fogTintStrength[s]-a.fogTintStrength[s])*t;
        p.fogBrightness[s]=a.fogBrightness[s]+(b.fogBrightness[s]-a.fogBrightness[s])*t;
    }
    p.shadowContrast=a.shadowContrast+(b.shadowContrast-a.shadowContrast)*t;
    p.toneStrength=a.toneStrength+(b.toneStrength-a.toneStrength)*t;
    for(unsigned s=0;s<NorthlightTone::StopCount;++s)p.grade[s]=NorthlightTone::blend(a.grade[s],b.grade[s],t);
    for(unsigned s=0;s<2;++s){
        for(unsigned c=0;c<3;++c){p.disc[s][c]=a.disc[s][c]+(b.disc[s][c]-a.disc[s][c])*t;p.light[s][c]=a.light[s][c]+(b.light[s][c]-a.light[s][c])*t;p.glow[s][c]=a.glow[s][c]+(b.glow[s][c]-a.glow[s][c])*t;}
        p.glowStrength[s]=a.glowStrength[s]+(b.glowStrength[s]-a.glowStrength[s])*t;p.glowMix[s]=a.glowMix[s]+(b.glowMix[s]-a.glowMix[s])*t;
        p.gain[s]=a.gain[s]+(b.gain[s]-a.gain[s])*t;
        p.mix[s]=a.mix[s]+(b.mix[s]-a.mix[s])*t;p.strength[s]=a.strength[s]+(b.strength[s]-a.strength[s])*t;
        p.fogGain[s]=a.fogGain[s]+(b.fogGain[s]-a.fogGain[s])*t;
        p.fogHeightScale[s]=a.fogHeightScale[s]+(b.fogHeightScale[s]-a.fogHeightScale[s])*t;
        p.horizonHaze[s]=a.horizonHaze[s]+(b.horizonHaze[s]-a.horizonHaze[s])*t;
    }return p;
}
inline NorthlightTone::Grade gradeAt(const Profile& p,float nightFactor,bool evening){
    return NorthlightTone::sample(p.grade,nightFactor,evening);
}
// The change to existing mist as an offset on its colour (fog x (1 + offset)): a hue shift that keeps its brightness,
// then the brightness itself. Optical depth and the scene's black point are untouched. w is the shadow contrast.
inline std::array<float,4> appearance(const Profile& p,const float* fog,bool fogOn,const NorthlightTone::TimeWeights& weights={}){
    std::array<float,4> result{};
    const float weight[]={weights.day,weights.dusk,weights.night};
    const float total=weight[0]+weight[1]+weight[2];
    if(fogOn&&total>0&&std::isfinite(total))for(unsigned s=0;s<NorthlightTone::StopCount;++s){
        if(!(weight[s]>0))continue;
        float channels[3],offset[3];
        for(unsigned c=0;c<3;++c)channels[c]=(0.5f+0.5f*std::clamp(fog[c],0.f,1.f))*p.fogTint[s][c];
        const float mean=.2126f*channels[0]+.7152f*channels[1]+.0722f*channels[2];
        for(unsigned c=0;c<3;++c)offset[c]=p.fogTintStrength[s]*(channels[c]-mean);
        // Keep the fog multiplier nonnegative even for a saturated swatch at high gain.
        const float lowest=std::min({offset[0],offset[1],offset[2]});
        if(lowest < -1.f)for(unsigned c=0;c<3;++c)offset[c]/=-lowest;
        for(unsigned c=0;c<3;++c)result[c]+=weight[s]/total*(offset[c]+(p.fogBrightness[s]-1.f)*(1.f+offset[c]));
    }
    result[3]=.35f*p.shadowContrast;
    // A smoothed transition can leave a sub-pixel residue for many frames.
    if(std::all_of(result.begin(),result.end(),[](float v){return std::abs(v)<.0001f;}))return {};
    return result;
}
// Slopes of the change in luminance. The endpoints stay fixed at black and white.
inline std::array<float,4> toneCoefficients(const Profile& p,float night,bool evening=false){
    if(p.toneStrength==0)return {};
    const auto grade=gradeAt(p,night,evening);
    float delta[5]={};
    for(unsigned i=0;i<3;++i){
        delta[i+1]=p.toneStrength*(grade.curves[0].levels[i]-.25f*(i+1));
    }
    std::array<float,4> result{};
    float slope=(delta[1]-delta[0])*4;
    result[3]=slope;
    for(unsigned i=0;i<3;++i){
        const float next=(delta[i+2]-delta[i+1])*4;
        result[i]=next-slope;
        slope=next;
    }
    if(std::all_of(result.begin(),result.end(),[](float v){return std::abs(v)<.0001f;}))return {};
    return result;
}
inline float fogHeightScale(const Profile& p,float night){
    return p.fogHeightScale[0]+(p.fogHeightScale[1]-p.fogHeightScale[0])*std::clamp(night,0.f,1.f);
}
inline float horizonHaze(const Profile& p,float night){
    return p.horizonHaze[0]+(p.horizonHaze[1]-p.horizonHaze[0])*std::clamp(night,0.f,1.f);
}
inline std::string trim(const std::string& s){auto a=s.find_first_not_of(" \t\r\n"),b=s.find_last_not_of(" \t\r\n");return a==std::string::npos?"":s.substr(a,b-a+1);}
// Map.dbc Directory is used verbatim as a cache directory basename by the
// verified world-context reader. Keep the same bounded ASCII alphabet here;
// spaces and apostrophes are real names (for example "Stratholme Raid" and
// "Zul'gurub"), while path components are never accepted.
inline bool safeMapName(const std::string& map){
    if(map.empty()||map.size()>63)return false;
    return std::all_of(map.begin(),map.end(),[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='\''||c==' ';});
}
inline bool hasRegionalAreaData(const std::string& map){return map=="Azeroth"||map=="Kalimdor"||map=="Expansion01"||map=="Northrend";}
inline bool safePaletteName(const std::string& name){
    if(name.empty()||name.size()>64)return false;
    return std::all_of(name.begin(),name.end(),[](char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c==' ';});
}
struct Table {
    Profile fallback;
    std::string palette;
    std::map<std::pair<std::string,uint32_t>,Profile> zones;
    std::map<std::string,Profile> mapFallbacks;
    std::set<std::pair<std::string,uint32_t>> interiors;
    std::map<std::pair<std::string,uint32_t>,uint32_t> parents;
    // Keys each zone section set itself (the rest were copied from [default]); lets an overlay move [default] under them.
    std::map<std::pair<std::string,uint32_t>,std::set<std::string>> explicitKeys;
    // Keep authored fallback keys separate so a palette overlay can retain
    // global and map-specific defaults without confusing either with a zone.
    std::set<std::string> explicitFallbackKeys;
    std::map<std::string,std::set<std::string>> explicitMapFallbackKeys;
    const Profile& fallbackFor(const std::string& map)const {auto i=mapFallbacks.find(map);return i==mapFallbacks.end()?fallback:i->second;}
    bool needsLiveZone()const{
        if(zones.empty())return false;
        auto tuned=[](const Profile& p){
            for(unsigned s=0;s<NorthlightTone::StopCount;++s)if(p.fogTintStrength[s]>0||p.fogBrightness[s]!=1)return true;
            return p.shadowContrast>0||p.toneStrength>0;
        };
        if(tuned(fallback))return true;
        if(std::any_of(zones.begin(),zones.end(),[&](const auto& row){return tuned(row.second);}))return true;
        // Instance map-zone profiles have no regional ADT lookup in the
        // shipped cache; the verified live AreaTable ID selects them.
        return std::any_of(zones.begin(),zones.end(),[&](const auto& row){
            const auto authored=explicitKeys.find(row.first);
            return !hasRegionalAreaData(row.first.first)&&authored!=explicitKeys.end()&&!authored->second.empty();
        });
    }
    const Profile& at(const std::string& map,uint32_t zone)const {auto i=zones.find({map,zone});return i==zones.end()?fallbackFor(map):i->second;}
};
// Instance map-zone IDs come from the verified live context because the
// shipped regional cache has no ADT data for those maps. Returning only an
// authored map+ID key prevents a zone remembered on another map from matching.
inline uint32_t liveInstanceZone(const Table& table,const std::string& map,uint32_t zone,uint32_t subzone){
    if(!safeMapName(map)||hasRegionalAreaData(map)||!zone)return 0;
    const auto parent=table.parents.find({map,subzone});
    if(parent!=table.parents.end()&&parent->second==zone&&table.zones.count({map,subzone}))return subzone;
    return table.zones.count({map,zone})?zone:0;
}
// One "sun_/moon_<name> = value" assignment with its range checks; nullptr on success, else the reason.
// Shared by the ini parser and the dev tuning overlay (zone_tuning.h).
inline float* keyTarget(Profile& profile,const std::string& key,unsigned& count,float& minimum,float& maximum){
    count=1;minimum=0;maximum=1;
    for(unsigned s=0;s<NorthlightTone::StopCount;++s){
        const std::string time=NorthlightTone::timeName(static_cast<NorthlightTone::TimeStop>(s));
        if(key.compare(0,time.size()+5,time+"_fog_")!=0)continue;
        const auto name=key.substr(time.size()+5);
        if(name=="tint"){count=3;return profile.fogTint[s];}
        if(name=="tint_strength"){maximum=2;return &profile.fogTintStrength[s];}
        if(name=="brightness"){maximum=2;return &profile.fogBrightness[s];}
    }
    if(key=="tone_curve_strength")return &profile.toneStrength;
    const auto binding=NorthlightTone::gradeBindings().find(key);
    if(binding!=NorthlightTone::gradeBindings().end()){
        const auto& b=binding->second;
        return NorthlightTone::parameter(profile.grade[static_cast<unsigned>(b.time)],b,minimum,maximum);
    }
    if(key=="shadow_contrast")return &profile.shadowContrast;
    Profile* p=&profile;
    unsigned s;if(key.rfind("sun_",0)==0)s=0;else if(key.rfind("moon_",0)==0)s=1;else return nullptr;
    auto name=key.substr(s?5:4);float* target=nullptr;count=1;minimum=0;maximum=1;
    if(name=="disc"){target=p->disc[s];count=3;}
    else if(name=="disc_gain"){target=&p->gain[s];maximum=4;}
    else if(name=="light"){target=p->light[s];count=3;}
    else if(name=="light_mix")target=&p->mix[s];
    else if(name=="light_strength"){target=&p->strength[s];maximum=4;}
    else if(name=="fog_gain"){target=&p->fogGain[s];maximum=8;}
    else if(name=="fog_height_scale"){target=&p->fogHeightScale[s];maximum=4;}
    else if(name=="horizon_haze"){target=&p->horizonHaze[s];maximum=4;}
    else if(name=="glow_strength"){target=&p->glowStrength[s];maximum=4;}
    else if(name=="glow"){target=p->glow[s];count=3;}
    else if(name=="glow_mix")target=&p->glowMix[s];
    else return nullptr;
    if(name=="fog_height_scale")minimum=.25f;
    return target;
}
// A look written before fog had a value per time of day sets one for all hours. "fog_tint" and "fog_tint_strength"
// are still read, as the three per-time keys of the same section; a per-time key in that section wins.
inline bool allHoursFogKey(const std::string& key){return key=="fog_tint"||key=="fog_tint_strength";}
inline std::array<std::string,NorthlightTone::StopCount> fogTimeKeys(const std::string& allHoursKey){
    return {"day_"+allHoursKey,"dusk_"+allHoursKey,"night_"+allHoursKey};
}
// The all-hours key a per-time fog key replaces, or empty.
inline std::string allHoursFogKeyOf(const std::string& key){
    const auto at=key.find("_fog_tint");if(at==key.npos)return {};
    const auto rest=key.substr(at+1);
    return allHoursFogKey(rest)&&(key=="day_"+rest||key=="dusk_"+rest||key=="night_"+rest)?rest:std::string();
}
inline const char* assignKey(Profile& profile,const std::string& key,std::string value){
    unsigned count=0;float minimum=0,maximum=0;
    float* target=keyTarget(profile,key,count,minimum,maximum);if(!target)return "unknown key";
    const auto name=key.substr(key.rfind("sun_",0)==0?4:5);
    for(char& c:value)if(c==',')c=' ';std::istringstream values(value);
    for(unsigned i=0;i<count;++i)if(!(values>>target[i])||!std::isfinite(target[i])||target[i]<minimum||target[i]>maximum)return "invalid numeric value";
    std::string extra;if(values>>extra)return "too many values";
    if(name=="light"&&target[0]+target[1]+target[2]<.01f)return "light hue cannot be black; use light_strength=0";
    if(name=="glow"&&target[0]+target[1]+target[2]<.01f)return "glow hue cannot be black; use glow_strength=0";
    return nullptr;
}
inline void inheritParent(Table& table,const std::pair<std::string,uint32_t>& child,uint32_t parent){
    auto& local=table.zones.at(child);
    Profile resolved=table.at(child.first,parent);
    for(const auto& key:table.explicitKeys.at(child)){
        unsigned count;float lo,hi;
        auto* src=keyTarget(local,key,count,lo,hi);
        auto* dst=keyTarget(resolved,key,count,lo,hi);
        std::copy_n(src,count,dst);
    }
    local=resolved;
}
inline bool hasExplicitDusk(const Table& table,const std::string& map,uint32_t zone,const std::string& key){
    if(table.explicitFallbackKeys.count(key))return true;
    const auto mapKeys=table.explicitMapFallbackKeys.find(map);
    if(mapKeys!=table.explicitMapFallbackKeys.end()&&mapKeys->second.count(key))return true;
    std::set<uint32_t> visited;
    while(zone&&visited.insert(zone).second){
        const auto section=std::make_pair(map,zone);const auto local=table.explicitKeys.find(section);
        if(local!=table.explicitKeys.end()&&local->second.count(key))return true;
        const auto parent=table.parents.find(section);if(parent==table.parents.end())break;zone=parent->second;
    }
    return false;
}
inline void resolveLegacyDusk(Table& table){
    using namespace NorthlightTone;
    auto resolve=[&](Profile& profile,const std::string& map,uint32_t zone){
        for(const auto& row:gradeBindings())if(static_cast<unsigned>(row.second.time)==static_cast<unsigned>(TimeStop::Dusk)&&!hasExplicitDusk(table,map,zone,row.first)){
            const auto& key=row.first;
            const auto suffix=key.substr(5);unsigned count=0;float lo=0,hi=0;
            auto* dusk=keyTarget(profile,key,count,lo,hi);auto* day=keyTarget(profile,"day_"+suffix,count,lo,hi);auto* night=keyTarget(profile,"night_"+suffix,count,lo,hi);
            for(unsigned i=0;i<count;++i)dusk[i]=day[i]+(night[i]-day[i])*DuskNightWeight;
        }
    };
    resolve(table.fallback,"",0);
    for(auto& row:table.mapFallbacks)resolve(row.second,row.first,0);
    for(auto& row:table.zones)resolve(row.second,row.first.first,row.first.second);
}
// Build the entire table before publishing it so invalid edits retain the saved appearance.
inline bool parse(std::istream& input,Table& out,std::string& error){
    Table t;Profile* p=nullptr;std::set<std::string>* explicitSet=nullptr;std::string line;unsigned number=0;bool defaultSeen=false,appearanceSeen=false;
    std::map<std::string,bool> keys;
    auto fail=[&](const char* why){error="line "+std::to_string(number)+": "+why;return false;};
    while(std::getline(input,line)){
        ++number;if(number>MaxProfileRows)return fail("file too large");line=trim(line.substr(0,line.find('#')));if(line.empty())continue;
        if(line.front()=='['&&line.back()==']'){
            std::string section=trim(line.substr(1,line.size()-2));keys.clear();
            if(section=="default") {if(defaultSeen||!t.zones.empty()||!t.mapFallbacks.empty())return fail("default must appear once, before map and zone profiles");defaultSeen=true;p=&t.fallback;explicitSet=&t.explicitFallbackKeys;continue;}
            if(section=="Appearance") {if(appearanceSeen)return fail("duplicate Appearance section");appearanceSeen=true;p=nullptr;explicitSet=nullptr;continue;}
            auto colon=section.find(':');if(colon==std::string::npos)return fail("expected [Map:ZoneID]");
            std::string map=trim(section.substr(0,colon)),id=trim(section.substr(colon+1));
            if(!safeMapName(map))return fail("invalid map name");
            if(id=="default"){
                if(t.zones.count({map,0})||t.mapFallbacks.count(map))return fail("duplicate map fallback");
                if(std::any_of(t.zones.begin(),t.zones.end(),[&](const auto& row){return row.first.first==map;}))return fail("map fallback must precede its zones");
                auto entry=t.mapFallbacks.emplace(map,t.fallback);p=&entry.first->second;explicitSet=&t.explicitMapFallbackKeys[map];continue;
            }
            auto slash=id.find('/');uint32_t parent=0;
            if(slash!=id.npos){
                auto root=id.substr(0,slash);
                if(root.empty()||root.size()>6||root.find_first_not_of("0123456789")!=root.npos||!(parent=uint32_t(std::stoul(root))))return fail("invalid parent zone");
                id=id.substr(slash+1);
            }
            const bool interior=id.size()>7&&id.compare(id.size()-7,7,"@inside")==0;
            if(interior)id.resize(id.size()-7);
            if(id.empty()||id.find_first_not_of("0123456789")!=std::string::npos||id.size()>6)return fail("invalid zone ID");
            uint32_t zone=uint32_t(std::stoul(id));if(!zone)return fail("zone ID must be positive");
            if(interior&&parent)return fail("interior requires a live zone");
            if(interior)t.interiors.emplace(map,zone);
            if(parent==zone)return fail("subzone equals parent");
            if(parent)t.parents[{map,zone}]=parent;
            auto entry=t.zones.emplace(std::make_pair(map,zone),t.fallbackFor(map));if(!entry.second)return fail("duplicate zone");p=&entry.first->second;explicitSet=&t.explicitKeys[entry.first->first];continue;
        }
        auto eq=line.find('=');if(eq==std::string::npos)return fail("expected key=value in section");
        std::string key=trim(line.substr(0,eq)),value=trim(line.substr(eq+1));if(!keys.emplace(key,true).second)return fail("duplicate key");
        if(appearanceSeen&&p==nullptr){
            if(key!="Palette"||!safePaletteName(value))return fail("Appearance accepts only a safe Palette name");
            t.palette=value;continue;
        }
        if(!p)return fail("expected key=value in section");
        if(allHoursFogKey(key)){
            for(const auto& timeKey:fogTimeKeys(key)){
                Profile checked;if(const char* why=assignKey(checked,timeKey,value))return fail(why);
                if(keys.count(timeKey))continue;
                assignKey(*p,timeKey,value);if(explicitSet)explicitSet->insert(timeKey);
            }
            continue;
        }
        if(const char* why=assignKey(*p,key,value))return fail(why);
        if(explicitSet)explicitSet->insert(key);
    }
    for(const auto& child:t.parents){
        const auto root=std::make_pair(child.first.first,child.second);
        if(t.parents.count(root))return fail("expected a top-level parent zone");
        inheritParent(t,child.first,child.second);
    }
    resolveLegacyDusk(t);
    if(input.bad()){error="read failed";return false;}out=std::move(t);error.clear();return true;
}
inline bool readIniText(std::istream& input,std::string& text,std::string& error){
    std::string raw;char buffer[4096];while(input){input.read(buffer,sizeof buffer);const auto n=size_t(input.gcount());if(raw.size()+n>MaxProfileBytes){error="INI exceeds 4 MiB";return false;}raw.append(buffer,n);}
    if(!input.eof()){error="INI read failed";return false;}
    if(NorthlightQuality::bigEndianUtf16(raw)||(raw.rfind("\xff\xfe",0)==0&&raw.size()%2)){error="unsupported or truncated INI encoding";return false;}
    text=NorthlightQuality::narrow(raw);if(text.rfind("\xef\xbb\xbf",0)==0)text.erase(0,3);return true;
}
inline bool load(const std::string& path,Table& out,std::string& error){
    std::ifstream f(path,std::ios::binary);if(!f){error="file unavailable; neutral defaults retained";return false;}
    std::string text;if(!readIniText(f,text,error))return false;std::istringstream decoded(text);return parse(decoded,out,error);
}

// Bilinear palettes at ADT chunk centers: continuous across authored chunk and
// tile boundaries, about 33 world units wide. No camera-anchored pattern or I/O.
inline Profile sample(const Table& table,const NorthlightRegionalFog::Region& region,const std::string& map,float x,float y){
    using namespace NorthlightRegionalFog;
    if(!std::isfinite(x)||!std::isfinite(y)||std::fabs(x)>100000||std::fabs(y)>100000)return table.fallbackFor(map);
    double gx=(WorldZero-y)/ChunkSize-.5,gy=(WorldZero-x)/ChunkSize-.5;
    double ix=std::floor(gx),iy=std::floor(gy);Profile p[4];
    for(unsigned row=0;row<2;++row)for(unsigned col=0;col<2;++col){
        float px=float(WorldZero-(iy+row+.5)*ChunkSize),py=float(WorldZero-(ix+col+.5)*ChunkSize);
        const auto area=areaAt(region,px,py);
        auto child=table.parents.find({map,area.subzone});
        p[row*2+col]=table.at(map,child!=table.parents.end()&&child->second==area.zone?area.subzone:area.zone);
    }
    return blend(blend(p[0],p[1],float(gx-ix)),blend(p[2],p[3],float(gx-ix)),float(gy-iy));
}
class Transition {
    bool valid=false;std::string map;float x=0,y=0;double time=0;Profile value;
public:
    Profile update(const Profile& target,const std::string& nextMap,float nx,float ny,double now){
        bool reset=!valid||map!=nextMap||(nx-x)*(nx-x)+(ny-y)*(ny-y)>256*256||now<time;
        value=reset?target:blend(value,target,float(1-std::exp(-std::min(now-time,.25)/1.25)));
        valid=true;map=nextMap;x=nx;y=ny;time=now;return value;
    }
};
inline void apply(const Profile& p,const float* direct,NorthlightCelestial::Context& sky){
    NorthlightCelestial::Body* bodies[]={&sky.sun,&sky.moon};float* colors[]={sky.sunColor,sky.moonColor};float* glows[]={sky.sunGlowColor,sky.moonGlowColor};
    const float weight[]={sky.sunWeight,sky.moonWeight};
    const float energy=direct[0]*.2126f+direct[1]*.7152f+direct[2]*.0722f;
    for(unsigned s=0;s<2;++s){
        bodies[s]->emission=p.gain[s];
        float luma=p.light[s][0]*.2126f+p.light[s][1]*.7152f+p.light[s][2]*.0722f,glowLuma=p.glow[s][0]*.2126f+p.glow[s][1]*.7152f+p.glow[s][2]*.0722f;
        for(unsigned c=0;c<3;++c){
            bodies[s]->tint[c]=p.disc[s][c];
            float tinted=p.light[s][c]*(energy/std::max(luma,.001f));
            const float lit=direct[c]+(tinted-direct[c])*p.mix[s];
            colors[s][c]=lit*p.strength[s]*weight[s];
            // The glow starts from the same lit colour (strength 1: exactly today's look) and moves toward its own hue.
            const float tintedGlow=p.glow[s][c]*(energy/std::max(glowLuma,.001f));
            glows[s][c]=(lit+(tintedGlow-lit)*p.glowMix[s])*p.glowStrength[s]*weight[s];
        }
    }
}
} // namespace NorthlightCelestialProfiles
