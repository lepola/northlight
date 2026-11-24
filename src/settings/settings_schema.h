#pragma once
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include "appearance_presets.h"
#include "settings_store.h"
#include "quality_settings.h"
#include "celestial_profiles.h"
#include "regional_shadow_range.h"
#include "settings_defaults.h"
#include "hdr_settings_contract.h"

namespace NorthlightSettings {
using Images=std::array<FileImage,FileCount>;
using Documents=std::array<Document,FileCount>;
inline Documents documents(const Images& a){Documents d;for(unsigned i=0;i<FileCount;++i)d[i]=Document(a[i].raw);return d;}
inline Documents factoryDocuments(){return {Document("[Quality]\nPreset=Quality\n"),Document("[Renderer]\nBackend=dxvk\n"),Document(kSettingsFactoryCelestial),Document(kSettingsFactoryShadowRange),Document(kSettingsFactoryWater),Document(kSettingsFactoryWaterHost)};}
inline std::string number(float f){std::ostringstream s;s<<std::setprecision(6)<<f;return s.str();}
inline bool liveQuality(const std::string& key){
    static const std::set<std::string> restart={"CommandStream","StreamFramesAhead","StreamFrameSkip","StreamOcclusionLocal","GIThreads","GIFastBVH","GIRays","GIDistance","NameMask","SpellLayer","EquipmentLights"};
    return restart.count(key)==0;
}
inline NorthlightQuality::Settings liveQualityUpdate(NorthlightQuality::Settings current,const NorthlightQuality::Settings& next){for(const auto& k:NorthlightQuality::Keys)if(liveQuality(k.name))current.*k.field=next.*k.field;current.preset=next.preset;return NorthlightQuality::effective(current);}
struct Config{NorthlightQuality::Settings quality;NorthlightCelestialProfiles::Table celestial;std::string palette="Default";std::array<float,std::size(NorthlightHdrSettingsContract::Keys)> hdr={0,203,160,1000,1,0};};
// Only these live inputs can change the worker's GI Request. Display settings
// still apply immediately without cancelling an otherwise identical solve.
inline bool giRequestChanged(const NorthlightQuality::Settings& before,const NorthlightQuality::Settings& after,
                             const NorthlightCelestialProfiles::Table& oldProfiles,const NorthlightCelestialProfiles::Table& newProfiles){
    if(before.gi!=after.gi||before.giBounces!=after.giBounces||before.giDynamicProbes!=after.giDynamicProbes||
       before.giProbeMoveStep!=after.giProbeMoveStep||before.giProbeAhead!=after.giProbeAhead||before.shadowDirectionSteps!=after.shadowDirectionSteps)return true;
    const auto sameLight=[](const NorthlightCelestialProfiles::Profile& a,const NorthlightCelestialProfiles::Profile& b){
        for(unsigned s=0;s<2;++s){if(a.mix[s]!=b.mix[s]||a.strength[s]!=b.strength[s])return false;
            for(unsigned c=0;c<3;++c)if(a.light[s][c]!=b.light[s][c])return false;}return true;
    };
    if(!sameLight(oldProfiles.fallback,newProfiles.fallback))return true;
    for(const auto& entry:oldProfiles.zones)if(!sameLight(entry.second,newProfiles.at(entry.first.first,entry.first.second)))return true;
    for(const auto& entry:newProfiles.zones)if(!sameLight(entry.second,oldProfiles.at(entry.first.first,entry.first.second)))return true;
    return false;
}
inline NorthlightQuality::Settings qualityOf(const Document& d,const std::string& legacy){std::istringstream q(d.text()),l(legacy);std::vector<std::string> warnings;return NorthlightQuality::load(&q,&l,warnings);}
inline Config configOf(const Documents& d,const std::string& legacy,bool validate=false,const NorthlightAppearancePresets::Catalog* presets=nullptr){
    Config c;c.quality=qualityOf(d[0],legacy);std::string error;std::istringstream celestial(d[2].text()),shadow(d[3].text());NorthlightRegionalShadow::Table ranges;
    if(!NorthlightCelestialProfiles::parse(celestial,c.celestial,error)&&validate)throw std::runtime_error("Celestial: "+error);
    c.palette=c.celestial.palette.empty()?"Default":c.celestial.palette;
    if(presets)if(const auto* preset=presets->find(c.palette))if(const auto* defaults=presets->find("Default"))c.celestial=NorthlightAppearancePresets::compose(c.celestial,preset->table,defaults->table);
    if(!NorthlightRegionalShadow::parse(shadow,ranges,error)&&validate)throw std::runtime_error("Shadow range: "+error);
    auto renderer=d[1].values();for(unsigned i=0;i<c.hdr.size();++i){const auto& key=NorthlightHdrSettingsContract::Keys[i];c.hdr[i]=NorthlightHdrSettingsContract::read(key,renderer["renderer"][lower(key.name)]);}return c;
}
inline std::pair<std::string,uint32_t> region(const std::string& section){
    auto suffix=section.find("@inside");
    if(suffix!=section.npos&&suffix+7==section.size())return region(section.substr(0,suffix));
    auto colon=section.find(':');if(colon==section.npos)throw std::runtime_error("Unknown region");auto map=lower(section.substr(0,colon)),id=section.substr(colon+1);
    static const std::map<std::string,std::string> maps={{"azeroth","Azeroth"},{"kalimdor","Kalimdor"},{"expansion01","Expansion01"},{"northrend","Northrend"}};
    if(id=="default"){const auto exact=section.substr(0,colon);if(!NorthlightCelestialProfiles::safeMapName(exact))throw std::runtime_error("Invalid map fallback: "+section);return {exact,0};}
    if(id.empty()||id.size()>6||id.find_first_not_of("0123456789")!=id.npos||std::stoul(id)==0)throw std::runtime_error("Invalid region: "+section);
    if(maps.count(map))return {maps.at(map),uint32_t(std::stoul(id))};
    const auto exact=section.substr(0,colon);if(!NorthlightCelestialProfiles::safeMapName(exact))throw std::runtime_error("Invalid region: "+section);return {exact,uint32_t(std::stoul(id))};
}
inline std::string parentSection(const std::string& s){
    if(s.size()>8&&s.compare(s.size()-8,8,":default")==0)return "default";
    const auto slash=s.find('/');if(slash!=s.npos)return s.substr(0,slash);
    const auto colon=s.find(':');return colon==s.npos?"default":s.substr(0,colon)+":default";
}
inline std::string regionName(const std::string& s){
    if(s.size()>8&&s.compare(s.size()-8,8,":default")==0){const auto map=s.substr(0,s.size()-8);if(!NorthlightCelestialProfiles::safeMapName(map))throw std::runtime_error("Invalid region");return map+":default";}
    auto slash=s.find('/');auto root=region(s.substr(0,slash));
    auto name=root.first+":"+std::to_string(root.second);
    if(slash!=s.npos)name+="/"+std::to_string(region(root.first+":"+s.substr(slash+1)).second);
    if(s.size()>7&&s.compare(s.size()-7,7,"@inside")==0)name+="@inside";
    return name;
}
inline std::string liquidAreaKey(const std::string& section){
    if(section=="default")return section;
    auto slash=section.find('/');
    return slash==section.npos?std::to_string(region(section).second):section.substr(slash+1);
}
inline const std::vector<std::string> AppearanceKeys=NorthlightAppearancePresets::keys();
inline const std::array<const char*,11> CelestialNames={"disc","disc_gain","light","light_mix","light_strength","fog_gain","fog_height_scale","horizon_haze","glow_strength","glow","glow_mix"};
inline std::string profileValue(NorthlightCelestialProfiles::Profile p,const std::string& key){unsigned n=0;float lo=0,hi=0;auto* v=NorthlightCelestialProfiles::keyTarget(p,key,n,lo,hi);std::string s;for(unsigned i=0;i<n;++i)s+=(i?",":"")+number(v[i]);return s;}
inline const NorthlightCelestialProfiles::Profile& profile(const NorthlightCelestialProfiles::Table& t,const std::string& s){if(lower(s)=="default")return t.fallback;
    if(s.size()>8&&s.compare(s.size()-8,8,":default")==0){const auto map=s.substr(0,s.size()-8);const auto i=t.mapFallbacks.find(map);return i==t.mapFallbacks.end()?t.fallback:i->second;}
    auto slash=s.find('/');
    auto r=region(s.substr(0,slash));
    if(slash!=s.npos){auto child=region(r.first+":"+s.substr(slash+1));if(t.zones.count(child))return t.at(child.first,child.second);}
    return t.at(r.first,r.second);}
using Values=std::map<std::string,std::map<std::string,std::string>>;
inline std::string explicitValue(const Values& v,const std::string& section,const std::string& key){auto s=v.find(lower(section));if(s==v.end())return "~";auto k=s->second.find(lower(key));return k==s->second.end()?"~":k->second;}
inline std::string explicitValue(const Document& d,const std::string& section,const std::string& key){return explicitValue(d.values(),section,key);}
struct Field {
    unsigned file=0;std::string section,key,kind="slider",low="0",high="1",step="1",saved,active,factory,inherited,overrideValue="~",factoryOverride="~",apply="R",options,origin,parentKey,unconfiguredValue;
    bool available=true;
    std::map<std::string,std::pair<std::string,bool>> paletteValues;
    std::string id()const{return std::string(1,FileIds[file])+"\t"+section+"\t"+key;}
};
// Case-insensitive match against a comma list of options; unknown values (and "~") stay as written so validation still reports them.
inline std::string canonicalOption(const std::string& value,const std::string& options){
    std::istringstream s(options);std::string o;while(std::getline(s,o,','))if(lower(o)==lower(value))return o;return value;
}
inline std::vector<Field> schema(const Documents& docs,const Documents& initial,const Config& active,const std::string& legacy,const std::string& backendOptions="native,dxvk,dxvk2,legacy",bool waterInstalled=false,const NorthlightAppearancePresets::Catalog* presets=nullptr){
    auto factory=factoryDocuments();const auto available=","+backendOptions+",";
    const std::string factoryBackend=available.find(",dxvk,")!=available.npos?"dxvk":available.find(",dxvk2,")!=available.npos?"dxvk2":"native";factory[1].set("Renderer","Backend",&factoryBackend);
    const auto saved=configOf(docs,legacy,false,presets),defaults=configOf(factory,legacy);std::vector<Field> out;
    NorthlightCelestialProfiles::Table localCelestial;{std::istringstream input(docs[2].text());std::string error;NorthlightCelestialProfiles::parse(input,localCelestial,error);}
    const auto* appearanceDefault=presets?presets->find("Default"):nullptr;
    std::array<Values,FileCount> savedValues,factoryValues,initialValues;
    for(unsigned i=0;i<FileCount;++i){savedValues[i]=docs[i].values();factoryValues[i]=factory[i].values();initialValues[i]=initial[i].values();}
    const auto legacyQuality=qualityOf(Document("[Quality]\nPreset=Quality\n"),legacy);
    auto add=[&](Field f){f.overrideValue=explicitValue(savedValues[f.file],f.section,f.key);f.factoryOverride=explicitValue(factoryValues[f.file],f.section,f.key);if(f.kind=="enum")for(auto* v:{&f.overrideValue,&f.factoryOverride,&f.saved,&f.active})*v=canonicalOption(*v,f.options); // readers accept any case; the menu compares exact spellings
        f.origin=f.overrideValue=="~"?"inherited":"override";out.push_back(std::move(f));};
    Field p;p.section="Quality";p.key="Preset";p.kind="enum";p.saved=NorthlightQuality::name(saved.quality.preset);p.active=NorthlightQuality::name(active.quality.preset);p.factory=p.inherited="Quality";p.options="Quality,Balanced,Performance";p.apply="F";add(p);
    unsigned index=0;
    for(const auto& k:NorthlightQuality::Keys){Field f;f.section="Quality";f.key=k.name;f.kind=k.low==0&&k.high==1?"check":"slider";f.low=std::to_string(k.low);f.high=std::to_string(k.high);
        f.saved=std::to_string(saved.quality.*k.field);f.active=std::to_string(active.quality.*k.field);f.factory=std::to_string(defaults.quality.*k.field);f.apply=liveQuality(k.name)?"F":"R";
        f.options=std::to_string(legacyQuality.*k.field)+","+std::to_string(k.preset[1])+","+std::to_string(k.preset[2]);f.inherited=std::to_string(saved.quality.preset==NorthlightQuality::Preset::Quality?legacyQuality.*k.field:k.preset[unsigned(saved.quality.preset)]);
        if(f.key=="AOMethod"){const char* names[]={"SSAO","HDAO","HBAOPlus","GTAO"};f.kind="enum";f.saved=names[std::min(3u,unsigned(std::stoul(f.saved)))];f.active=names[std::min(3u,unsigned(std::stoul(f.active)))];f.factory=f.inherited="SSAO";f.options="SSAO,HDAO,HBAOPlus,GTAO";}
        add(f);out.back().origin=std::string(1,saved.quality.origin[index++]);}
    Field b;b.file=1;b.section="Renderer";b.key="Backend";b.kind="enum";b.saved=lower(explicitValue(savedValues[1],b.section,b.key));if(b.saved=="~")b.saved="legacy";b.active=lower(explicitValue(initialValues[1],b.section,b.key));if(b.active=="~")b.active="legacy";b.factory=factoryBackend;b.inherited="legacy";b.options=backendOptions;add(b);
    Field appearance;appearance.file=2;appearance.section="Appearance";appearance.key="Palette";appearance.kind="enum";appearance.saved=saved.palette;appearance.active=active.palette;appearance.factory=appearance.inherited="Default";appearance.apply="F";appearance.options="Default";
    std::set<std::string> paletteOptions={"default"};auto option=[&](const std::string& name){if(!name.empty()&&paletteOptions.insert(lower(name)).second)appearance.options+=","+name;};
    if(presets)for(const auto& name:presets->names())option(name);option(appearance.saved);option(appearance.active);
    add(appearance);
    unsigned hdrIndex=0;for(const auto& key:NorthlightHdrSettingsContract::Keys){Field f;f.file=1;f.section="Renderer";f.key=key.name;f.kind=key.low==0&&key.high==1?"check":"slider";f.low=number(key.low);f.high=number(key.high);f.step=number(key.step);f.factory=f.inherited=number(key.value);f.apply=hdrIndex?"F":"R";f.available=false; /* no HDR consumer in this build: kept in the snapshot so the files are preserved, shown disabled, never writable */
        f.saved=number(NorthlightHdrSettingsContract::read(key,explicitValue(savedValues[1],f.section,f.key)));
        f.active=number(hdrIndex?active.hdr[hdrIndex]:NorthlightHdrSettingsContract::read(key,explicitValue(initialValues[1],f.section,f.key)));++hdrIndex;add(f);}
    for(unsigned file=2;file<4;++file){std::set<std::string> sections;if(file==2)sections.insert("default");
        // Keep an existing player's section spelling when the packaged profile
        // adds a parent or interior qualifier for that same AreaTable entry.
        std::set<std::pair<std::string,std::string>> areas;
        if(file==3)for(const auto* values:{&savedValues[file],&factoryValues[file]}){
            for(const auto& entry:*values){
                if(entry.first.empty()||entry.first=="default")continue;
                const auto name=regionName(entry.first);
                if(areas.emplace(name.substr(0,name.find(':')),liquidAreaKey(name)).second)sections.insert(name);
            }
        }
        if(file==2){
            for(const auto& row:saved.celestial.zones)sections.insert(NorthlightAppearancePresets::section(saved.celestial,row.first));
            for(const auto& row:defaults.celestial.zones){
                const auto name=NorthlightAppearancePresets::section(defaults.celestial,row.first);
                if(presets||name.find("@inside")==name.npos)sections.insert(name);
            }
            for(const auto& row:active.celestial.zones)sections.insert(NorthlightAppearancePresets::section(active.celestial,row.first));
            for(const auto& row:saved.celestial.mapFallbacks)sections.insert(row.first+":default");
            if(presets)for(const auto& row:defaults.celestial.mapFallbacks)sections.insert(row.first+":default");
            for(const auto& row:active.celestial.mapFallbacks)sections.insert(row.first+":default");
            if(presets){
                for(const auto& preset:presets->entries){
                    for(const auto& row:preset.table.zones)sections.insert(NorthlightAppearancePresets::section(preset.table,row.first));
                    for(const auto& row:preset.table.mapFallbacks)sections.insert(row.first+":default");
                }
            }
        }
        for(const auto& section:sections){
            if(file==2&&section!="Appearance"){
                std::vector<std::string> keys(std::begin(AppearanceKeys),std::end(AppearanceKeys));
                for(auto body:{"sun_","moon_"})for(auto name:CelestialNames)keys.emplace_back(std::string(body)+name);
                for(const auto& key:keys){Field f;f.file=2;f.section=section;f.key=key;f.apply="F";unsigned n=0;float lo=0,hi=0;NorthlightCelestialProfiles::Profile dummy;
                NorthlightCelestialProfiles::keyTarget(dummy,f.key,n,lo,hi);f.kind=n==3?"rgb":"slider";f.low=number(lo);f.high=number(hi);f.step="0.01";
                f.saved=profileValue(profile(saved.celestial,section),f.key);f.active=profileValue(profile(active.celestial,section),f.key);f.factory=profileValue(profile(defaults.celestial,section),f.key);
                f.inherited=profileValue(section=="default"?profile(saved.celestial,"default"):profile(saved.celestial,parentSection(section)),f.key);add(f);
                if(file==2&&NorthlightAppearancePresets::visualKey(f.key)){
                    if(presets)for(const auto& item:presets->entries)out.back().paletteValues[item.name]={profileValue(profile(item.table,section),f.key),NorthlightAppearancePresets::hasDirectValue(item.table,section,f.key)};
                    if(out.back().overrideValue!="~"&&appearanceDefault&&NorthlightAppearancePresets::hasDirectValue(localCelestial,section,f.key)&&
                       NorthlightAppearancePresets::hasDirectValue(appearanceDefault->table,section,f.key)&&
                       NorthlightAppearancePresets::sameValue(profile(localCelestial,section),profile(appearanceDefault->table,section),f.key)){
                        out.back().overrideValue="~";out.back().origin="inherited";
                    }
                }
            }}else{Field f;f.file=3;f.section=section;f.key="terrain_radius";f.low=number(NorthlightRegionalShadow::DefaultRadius);f.high=number(NorthlightRegionalShadow::MaximumRadius);
                auto value=[&](const Values& v){auto s=explicitValue(v,section,f.key);return s=="~"?f.low:s;};f.saved=value(savedValues[3]);f.active=value(initialValues[3]);f.factory=value(factoryValues[3]);f.inherited=f.low;add(f);}
        }
    }
    if(waterInstalled){
        // "S" values are picked up by Tideglass's existing water.ini watcher after Save.
        // Inherited values are core defaults; factory overrides come from the packaged INIs.
        struct WaterControl { const char* key; float fallback,low,high,step; };
        static constexpr WaterControl controls[]={
            {"WaveStrength",1,0,4,.05f},{"WaveSpeed",1,0,10,.05f},
            {"WaveCrestShape",0,0,1,.05f},{"WaveCrestSharpness",0,0,1,.05f},
            {"RippleStrength",1.6f,0,5,.05f},{"FoamStrength",1,0,4,.05f},{"OpenWaterCrestFoam",1,0,1,.05f},
            {"ReflectionResolution",100,50,100,25},{"ReflectionDistance",0,0,2,1},{"FineHighlights",0,0,1,1},{"ReflectionSceneryDetails",0,0,1,1},{"ReflectionScenery",0,0,1,1},{"Clarity",1,.05f,10,.05f},{"SurfaceBlur",1,0,4,.05f},
            {"Reflection",.7f,0,4,.05f},{"NearReflection",.55f,0,1,.05f},
            {"Underwater",1,0,1,1},{"UnderwaterVisibility",35,2,500,1},{"UnderwaterScenery",0,0,1,1},
            {"Slime",1,0,1,1},{"Lava",1,0,1,1},
        };
        auto addWater=[&](unsigned file,const WaterControl& control){
            Field f;f.file=file;f.section=file==WaterFile?"Water":"WaterIntegration";f.key=control.key;
            f.low=number(control.low);f.high=number(control.high);f.step=number(control.step);
            f.kind=control.high==1&&control.step==1?"check":"slider";
            f.apply=file==WaterFile?"S":"R";f.inherited=number(control.fallback);
            if(f.key=="ReflectionResolution"){f.kind="enum";f.options="100,75,50";}
            if(f.key=="ReflectionDistance"){f.kind="enum";f.options="1,0,2";}
            if(f.key=="ReflectionScenery"){f.kind="enum";f.options="0,1";}
            if(f.key=="ReflectionScenery" && explicitValue(savedValues[file],"Water","ReflectionProducer")=="3")f.inherited="1";
            auto value=[&](const Values& values){
                auto raw=explicitValue(values,f.section,f.key);
                return raw=="~"?f.inherited:raw;
            };
            f.saved=value(savedValues[file]);f.factory=value(factoryValues[file]);
            f.active=file==WaterFile?f.saved:value(initialValues[file]);
            const bool legacyOff=f.key=="ReflectionScenery" && explicitValue(savedValues[file],"Water","ScreenReflections")=="0";
            add(std::move(f));
            // Expose the old Boolean as an explicit override until the player edits distance.
            if(out.back().key=="ReflectionDistance" && out.back().overrideValue=="~"){
                const auto legacy=explicitValue(savedValues[file],"Water","ShortReflectionDistance");
                if(legacy!="~"){
                    char* end=nullptr;
                    const auto digits=legacy.find_first_not_of("+-");
                    const bool hex=digits!=legacy.npos && lower(legacy.substr(digits,2))=="0x";
                    errno=0;
                    const auto value=std::strtol(legacy.c_str(),&end,hex?16:10);
                    if(end!=legacy.c_str() && !*end && !errno && value>=INT_MIN && value<=INT_MAX){
                        auto& distance=out.back();
                        distance.saved=distance.active=distance.overrideValue=value?"1":"0";
                        distance.origin="override";
                    }
                }
            }
            if(legacyOff){
                Field& reflection=out.back();
                reflection.saved=reflection.active=reflection.overrideValue="0";
                reflection.origin="override";
            }
        };
        addWater(WaterHostFile,{"Enable",0,0,1,1});
        for(const auto& control:controls)addWater(WaterFile,control);
        std::map<std::string,std::string> slimeParents;
        for(const auto& field:out){
            if(field.file==2&&field.key=="fog_tint_strength"&&
               !(field.section.size()>8&&field.section.compare(field.section.size()-8,8,":default")==0)){
                const auto id=liquidAreaKey(field.section);
                const auto parent=parentSection(field.section);
                const auto parentId=parent=="default"||(parent.size()>8&&parent.compare(parent.size()-8,8,":default")==0)?"default":liquidAreaKey(parent);
                slimeParents.try_emplace(id,parentId);
            }
        }
        const auto& water=savedValues[WaterFile];
        const auto general=explicitValue(water,"RegionalSlime","default");
        const auto legacy=explicitValue(water,"Water","RegionalSlimeStrength");
        for(const auto& [id,parent]:slimeParents){
            Field f;f.file=WaterFile;f.section="RegionalSlime";f.key=id;f.step="0.01";f.apply="S";
            f.parentKey=parent;
            f.unconfiguredValue=id=="1497"?(legacy=="~"?"1":legacy):"0";
            f.inherited=f.unconfiguredValue;
            if(id!="default"){
                if(general!="~")f.inherited=general;
                const auto localParent=explicitValue(water,f.section,parent);
                if(localParent!="~")f.inherited=localParent;
            }
            auto local=explicitValue(water,f.section,id);
            f.saved=f.active=local=="~"?f.inherited:local;
            f.factory=id=="1497"?"1":"0";add(f);
        }
    }
    return out;
}
inline void validateValue(const Field& f,const std::string& value){
    if(value.size()>128||value.find_first_of("\t\r\n\0",0,4)!=value.npos)throw std::runtime_error("Invalid settings value");
    if(value=="~"){if(f.file==1&&f.key=="Backend"&&(","+f.options+",").find(",legacy,")==std::string::npos)throw std::runtime_error("Legacy backend is not installed; select an available backend");return;}
    if(f.kind=="enum"){std::istringstream s(f.options);std::string v;while(std::getline(s,v,','))if(v==value)return;throw std::runtime_error("Unknown option for "+f.key);}
    if(f.file==2){NorthlightCelestialProfiles::Profile p;if(auto e=NorthlightCelestialProfiles::assignKey(p,f.key,value))throw std::runtime_error(f.key+": "+e);return;}
    if((f.file==1&&std::stof(f.step)<1)||(f.file==WaterFile&&f.kind=="slider")){float n=0;if(!NorthlightHdrSettingsContract::parse(value,n)||n<std::stof(f.low)||n>std::stof(f.high))throw std::runtime_error("Value outside allowed range for "+f.key);return;}
    if(value.empty()||value.find_first_not_of("0123456789")!=value.npos)throw std::runtime_error("Expected an integer for "+f.key);
    auto n=std::stoull(value);if(n<std::stoull(f.low)||n>std::stoull(f.high))throw std::runtime_error("Value outside allowed range for "+f.key);
}
inline Documents applyChanges(Documents docs,const std::vector<Field>& fields,const std::string& changes){
    if(changes.size()>131072)throw std::runtime_error("Settings request exceeds 128 KiB");std::map<std::string,const Field*> known;for(const auto& f:fields)known[f.id()]=&f;
    std::set<std::string> changed;std::istringstream in(changes);std::string line;unsigned count=0;
    while(std::getline(in,line)){if(line.empty())continue;if(++count>1024)throw std::runtime_error("Too many settings changes");auto at=line.rfind('\t');if(at==line.npos)throw std::runtime_error("Malformed settings change");
        auto id=line.substr(0,at),value=line.substr(at+1);if(!known.count(id)||!changed.insert(id).second)throw std::runtime_error("Unknown or duplicate setting");const auto& f=*known.at(id);
        if(!f.available)throw std::runtime_error(f.key+" is not supported by this renderer build");
        if(value=="@factory")value=f.factoryOverride;validateValue(f,value);
        if(f.file==2&&f.section=="default"&&value!="~"&&!docs[2].values().count("default"))
            docs[2].lines.insert(docs[2].lines.begin(),{"[default]",docs[2].newline});
        docs[f.file].set(f.section,f.key,value=="~"?nullptr:&value);
        if(f.file==WaterFile && f.key=="ReflectionDistance")
            docs[WaterFile].set("Water","ShortReflectionDistance",nullptr);
        // A player who explicitly enables scenery can also restore it on older INIs
        // whose master reflection switch was off. Leave that key alone on unrelated saves.
        if(f.file==WaterFile && f.key=="ReflectionScenery" && (value=="1" || value=="~") &&
           explicitValue(docs[WaterFile],"Water","ScreenReflections")=="0")
            docs[WaterFile].set("Water","ScreenReflections",nullptr);
        if(f.file==3&&value=="~"){
            auto values=docs[3].values();if(values[lower(f.section)].empty())for(auto i=docs[3].lines.begin();i!=docs[3].lines.end();++i)if(lower(Document::content(i->text))=="["+lower(f.section)+"]"){i->text.clear();break;}}
    }return docs;
}
inline std::string wire(const std::string& value){std::string out;for(auto c:value)switch(c){case '%':out+="%25";break;case '\t':out+="%09";break;case '\r':out+="%0d";break;case '\n':out+="%0a";break;default:out+=c;}return out;}
inline std::string serialize(const std::vector<Field>& fields,const std::string& revision,const std::string& activeRevision){
    std::string out="NLS1\t"+revision+"\t"+activeRevision+"\n";
    for(const auto& f:fields){bool first=true;for(const auto& cell:{std::string("F"),std::string(1,FileIds[f.file]),f.section,f.key,f.kind,f.low,f.high,f.step,f.saved,f.active,f.factory,f.overrideValue,f.apply,f.options,f.origin,f.inherited,f.factoryOverride,std::string(f.available?"1":"0")}){if(!first)out+='\t';first=false;out+=wire(cell);}out+='\n';}
    return out;
}
} // namespace NorthlightSettings
