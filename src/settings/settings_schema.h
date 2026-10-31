#pragma once
#include <algorithm>
#include <cmath>
#include <iomanip>
#include "settings_store.h"
#include "quality_settings.h"
#include "celestial_profiles.h"
#include "regional_shadow_range.h"
#include "settings_defaults.h"
#include "hdr_settings_contract.h"

namespace NorthlightSettings {
using Images=std::array<FileImage,4>;
using Documents=std::array<Document,4>;
inline Documents documents(const Images& a){Documents d;for(unsigned i=0;i<4;++i)d[i]=Document(a[i].raw);return d;}
inline Documents factoryDocuments(){return {Document("[Quality]\nPreset=Quality\n"),Document("[Renderer]\nBackend=dxvk\n"),Document(kSettingsFactoryCelestial),Document(kSettingsFactoryShadowRange)};}
inline std::string number(float f){std::ostringstream s;s<<std::setprecision(6)<<f;return s.str();}
inline std::string migrateHdr(const std::filesystem::path& root){
    try{Store store(root,3000);const auto before=store.read();auto docs=documents(before);auto values=docs[1].values();
        if(values["renderer"]["hdrpipelineversion"]=="2")return "";
        for(const auto& section:values)for(const auto& key:section.second)
            if(section.first.rfind("hdr.",0)==0||(section.first=="renderer"&&(key.first.rfind("hdr",0)==0||key.first=="experimentalhdr")))docs[1].set(section.first,key.first,nullptr);
        for(unsigned i=0;i<NorthlightHdrSettingsContract::Count;++i){const auto& key=NorthlightHdrSettingsContract::Keys[i];const auto value=i==0?"1":NorthlightHdrSettingsContract::mode(key)?"Auto":number(key.value);docs[1].set("Renderer",key.name,&value);}
        const std::string version="2";docs[1].set("Renderer","HDRPipelineVersion",&version);auto after=before;after[1]={true,docs[1].bytes()};store.commit(before,after);return "";
    }catch(const std::exception& error){return std::string("HDR migration failed: ")+error.what();}
}
inline bool liveQuality(const std::string& key){
    static const std::set<std::string> restart={"CommandStream","StreamFramesAhead","StreamFrameSkip","StreamOcclusionLocal","GIThreads","GIFastBVH","GIRays","GIDistance","NameMask","SpellLayer"};
    return restart.count(key)==0;
}
inline NorthlightQuality::Settings liveQualityUpdate(NorthlightQuality::Settings current,const NorthlightQuality::Settings& next){for(const auto& k:NorthlightQuality::Keys)if(liveQuality(k.name))current.*k.field=next.*k.field;current.preset=next.preset;return NorthlightQuality::effective(current);}
struct Config{NorthlightQuality::Settings quality;NorthlightCelestialProfiles::Table celestial;std::array<float,NorthlightHdrSettingsContract::Count> hdr=NorthlightHdrSettingsContract::defaults();};
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
inline Config configOf(const Documents& d,const std::string& legacy,bool validate=false,const std::string& hdrProfile=""){
    Config c;c.quality=qualityOf(d[0],legacy);std::string error;std::istringstream celestial(d[2].text()),shadow(d[3].text());NorthlightRegionalShadow::Table ranges;
    if(!NorthlightCelestialProfiles::parse(celestial,c.celestial,error)&&validate)throw std::runtime_error("Celestial: "+error);
    if(!NorthlightRegionalShadow::parse(shadow,ranges,error)&&validate)throw std::runtime_error("Shadow range: "+error);
    auto renderer=d[1].values();for(unsigned i=0;i<c.hdr.size();++i){const auto& key=NorthlightHdrSettingsContract::Keys[i];const auto section=!hdrProfile.empty()&&NorthlightHdrSettingsContract::displayKey(i)?"hdr."+hdrProfile:"renderer";
        c.hdr[i]=NorthlightHdrSettingsContract::read(key,renderer[section][lower(key.name)]);}return c;
}
inline std::pair<std::string,uint32_t> region(const std::string& section){
    auto colon=section.find(':');if(colon==section.npos)throw std::runtime_error("Unknown region");auto map=lower(section.substr(0,colon)),id=section.substr(colon+1);
    static const std::map<std::string,std::string> maps={{"azeroth","Azeroth"},{"kalimdor","Kalimdor"},{"expansion01","Expansion01"},{"northrend","Northrend"}};
    if(!maps.count(map)||id.empty()||id.size()>6||id.find_first_not_of("0123456789")!=id.npos||std::stoul(id)==0)throw std::runtime_error("Invalid region");return {maps.at(map),uint32_t(std::stoul(id))};
}
inline std::string regionName(const std::string& s){auto r=region(s);return r.first+":"+std::to_string(r.second);}
inline const std::array<const char*,11> CelestialNames={"disc","disc_gain","light","light_mix","light_strength","fog_gain","fog_height_scale","horizon_haze","glow_strength","glow","glow_mix"};
inline std::string profileValue(NorthlightCelestialProfiles::Profile p,const std::string& key){unsigned n=0;float lo=0,hi=0;auto* v=NorthlightCelestialProfiles::keyTarget(p,key,n,lo,hi);std::string s;for(unsigned i=0;i<n;++i)s+=(i?",":"")+number(v[i]);return s;}
inline const NorthlightCelestialProfiles::Profile& profile(const NorthlightCelestialProfiles::Table& t,const std::string& s){if(lower(s)=="default")return t.fallback;auto r=region(s);return t.at(r.first,r.second);}
using Values=std::map<std::string,std::map<std::string,std::string>>;
inline std::string explicitValue(const Values& v,const std::string& section,const std::string& key){auto s=v.find(lower(section));if(s==v.end())return "~";auto k=s->second.find(lower(key));return k==s->second.end()?"~":k->second;}
inline std::string explicitValue(const Document& d,const std::string& section,const std::string& key){return explicitValue(d.values(),section,key);}
struct Field {
    unsigned file=0;std::string section,key,kind="slider",low="0",high="1",step="1",saved,active,factory,inherited,overrideValue="~",factoryOverride="~",apply="R",options,origin;
    bool available=true;
    std::string id()const{return std::string(1,"QRCS"[file])+"\t"+section+"\t"+key;}
};
// Case-insensitive match against a comma list of options; unknown values (and "~") stay as written so validation still reports them.
inline std::string canonicalOption(const std::string& value,const std::string& options){
    if(options=="Manual,Auto"){float mode=0;if(NorthlightHdrSettingsContract::parse(value,mode)&&(mode==0||mode==1))return NorthlightHdrSettingsContract::modeName(mode);}
    std::istringstream s(options);std::string o;while(std::getline(s,o,','))if(lower(o)==lower(value))return o;return value;
}
inline std::vector<Field> schema(const Documents& docs,const Documents& initial,const Config& active,const std::string& legacy,const std::string& backendOptions="native,dxvk,dxvk2,legacy",const std::string& hdrProfile=""){
    auto factory=factoryDocuments();const auto available=","+backendOptions+",";
    const std::string factoryBackend=available.find(",dxvk,")!=available.npos?"dxvk":available.find(",dxvk2,")!=available.npos?"dxvk2":"native";factory[1].set("Renderer","Backend",&factoryBackend);
    const auto saved=configOf(docs,legacy,false,hdrProfile),defaults=configOf(factory,legacy);std::vector<Field> out;
    std::array<Values,4> savedValues,factoryValues,initialValues;
    for(unsigned i=0;i<4;++i){savedValues[i]=docs[i].values();factoryValues[i]=factory[i].values();initialValues[i]=initial[i].values();}
    const auto legacyQuality=qualityOf(Document("[Quality]\nPreset=Quality\n"),legacy);
    auto add=[&](Field f){f.overrideValue=explicitValue(savedValues[f.file],f.section,f.key);f.factoryOverride=explicitValue(factoryValues[f.file],f.section,f.key);if(f.kind=="enum")for(auto* v:{&f.overrideValue,&f.factoryOverride,&f.saved,&f.active})*v=canonicalOption(*v,f.options); // readers accept any case; the menu compares exact spellings
        f.origin=f.overrideValue=="~"?"inherited":"override";out.push_back(std::move(f));};
    Field p;p.section="Quality";p.key="Preset";p.kind="enum";p.saved=NorthlightQuality::name(saved.quality.preset);p.active=NorthlightQuality::name(active.quality.preset);p.factory=p.inherited="Quality";p.options="Quality,Balanced,Performance";p.apply="F";add(p);
    unsigned index=0;
    for(const auto& k:NorthlightQuality::Keys){Field f;f.section="Quality";f.key=k.name;f.kind=k.low==0&&k.high==1?"check":"slider";f.low=std::to_string(k.low);f.high=std::to_string(k.high);
        f.saved=std::to_string(saved.quality.*k.field);f.active=std::to_string(active.quality.*k.field);f.factory=std::to_string(defaults.quality.*k.field);f.apply=liveQuality(k.name)?"F":"R";
        f.options=std::to_string(legacyQuality.*k.field)+","+std::to_string(k.preset[1])+","+std::to_string(k.preset[2]);f.inherited=std::to_string(saved.quality.preset==NorthlightQuality::Preset::Quality?legacyQuality.*k.field:k.preset[unsigned(saved.quality.preset)]);
        if(f.key=="AOMethod"){const char* names[]={"SSAO","HDAO","HBAOPlus","GTAO"};f.kind="enum";f.saved=names[saved.quality.aoMethod];f.active=names[active.quality.aoMethod];f.factory=f.inherited="SSAO";f.options="SSAO,HDAO,HBAOPlus,GTAO";}
        if(f.key=="AOQuality"){f.kind="enum";f.saved=NorthlightQuality::aoQualityName(saved.quality.aoQuality);f.active=NorthlightQuality::aoQualityName(active.quality.aoQuality);f.factory=f.inherited="High";f.options="High,Medium,Low";}
        add(f);out.back().origin=std::string(1,saved.quality.origin[index++]);}
    Field b;b.file=1;b.section="Renderer";b.key="Backend";b.kind="enum";b.saved=lower(explicitValue(savedValues[1],b.section,b.key));if(b.saved=="~")b.saved="legacy";b.active=lower(explicitValue(initialValues[1],b.section,b.key));if(b.active=="~")b.active="legacy";b.factory=factoryBackend;b.inherited="legacy";b.options=backendOptions;add(b);
    unsigned hdrIndex=0;for(const auto& key:NorthlightHdrSettingsContract::Keys){Field f;f.file=1;f.section="Renderer";f.key=key.name;f.kind=key.low==0&&key.high==1?"check":"slider";f.low=number(key.low);f.high=number(key.high);f.step=number(key.step);f.factory=f.inherited=number(key.value);f.apply="F";
        if(!hdrProfile.empty()&&NorthlightHdrSettingsContract::displayKey(hdrIndex))f.section="HDR."+hdrProfile;
        f.saved=number(NorthlightHdrSettingsContract::read(key,explicitValue(savedValues[1],f.section,f.key)));
        f.active=number(active.hdr[hdrIndex]);
        if(NorthlightHdrSettingsContract::mode(key)){f.kind="enum";f.options="Manual,Auto";f.saved=NorthlightHdrSettingsContract::modeName(saved.hdr[hdrIndex]);f.active=NorthlightHdrSettingsContract::modeName(active.hdr[hdrIndex]);f.factory=f.inherited=NorthlightHdrSettingsContract::modeName(key.value);}
        ++hdrIndex;add(f);}
    for(unsigned file=2;file<4;++file){std::set<std::string> sections;if(file==2)sections.insert("default");
        for(const auto* v:std::array<const Values*,2>{&savedValues[file],&factoryValues[file]})for(const auto& s:*v)if(!s.first.empty()&&s.first!="default")sections.insert(regionName(s.first));
        for(const auto& section:sections){
            if(file==2){for(auto body:{"sun_","moon_"})for(auto name:CelestialNames){Field f;f.file=2;f.section=section;f.key=std::string(body)+name;f.apply="F";unsigned n=0;float lo=0,hi=0;NorthlightCelestialProfiles::Profile dummy;
                NorthlightCelestialProfiles::keyTarget(dummy,f.key,n,lo,hi);f.kind=n==3?"rgb":"slider";f.low=number(lo);f.high=number(hi);f.step="0.01";
                f.saved=profileValue(profile(saved.celestial,section),f.key);f.active=profileValue(profile(active.celestial,section),f.key);f.factory=profileValue(profile(defaults.celestial,section),f.key);f.inherited=profileValue(section=="default"?NorthlightCelestialProfiles::Profile{}:saved.celestial.fallback,f.key);add(f);
            }}else{Field f;f.file=3;f.section=section;f.key="terrain_radius";f.low=number(NorthlightRegionalShadow::DefaultRadius);f.high=number(NorthlightRegionalShadow::MaximumRadius);
                auto value=[&](const Values& v){auto s=explicitValue(v,section,f.key);return s=="~"?f.low:s;};f.saved=value(savedValues[3]);f.active=value(initialValues[3]);f.factory=value(factoryValues[3]);f.inherited=f.low;add(f);}
        }
    }return out;
}
inline void validateValue(const Field& f,const std::string& value){
    if(value.size()>128||value.find_first_of("\t\r\n\0",0,4)!=value.npos)throw std::runtime_error("Invalid settings value");
    if(value=="~"){if(f.file==1&&f.key=="Backend"&&(","+f.options+",").find(",legacy,")==std::string::npos)throw std::runtime_error("Legacy backend is not installed; select an available backend");return;}
    if(f.kind=="enum"){std::istringstream s(f.options);std::string v;while(std::getline(s,v,','))if(v==value)return;throw std::runtime_error("Unknown option for "+f.key);}
    if(f.file==2){NorthlightCelestialProfiles::Profile p;if(auto e=NorthlightCelestialProfiles::assignKey(p,f.key,value))throw std::runtime_error(f.key+": "+e);return;}
    if(f.file==1&&std::stof(f.step)<1){float n=0;if(!NorthlightHdrSettingsContract::parse(value,n)||n<std::stof(f.low)||n>std::stof(f.high))throw std::runtime_error("Value outside allowed range for "+f.key);return;}
    if(value.empty()||value.find_first_not_of("0123456789")!=value.npos)throw std::runtime_error("Expected an integer for "+f.key);
    auto n=std::stoull(value);if(n<std::stoull(f.low)||n>std::stoull(f.high))throw std::runtime_error("Value outside allowed range for "+f.key);
}
inline Documents applyChanges(Documents docs,const std::vector<Field>& fields,const std::string& changes){
    if(changes.size()>131072)throw std::runtime_error("Settings request exceeds 128 KiB");std::map<std::string,const Field*> known;for(const auto& f:fields)known[f.id()]=&f;
    std::set<std::string> changed;std::istringstream in(changes);std::string line;unsigned count=0;
    while(std::getline(in,line)){if(line.empty())continue;if(++count>1024)throw std::runtime_error("Too many settings changes");auto at=line.rfind('\t');if(at==line.npos)throw std::runtime_error("Malformed settings change");
        auto id=line.substr(0,at),value=line.substr(at+1);if(!known.count(id)||!changed.insert(id).second)throw std::runtime_error("Unknown or duplicate setting");const auto& f=*known.at(id);
        if(!f.available)throw std::runtime_error(f.key+" is not supported by this renderer build");
        if(value=="@factory")value=f.factoryOverride;validateValue(f,value);docs[f.file].set(f.section,f.key,value=="~"?nullptr:&value);
        if(f.file==3&&value=="~"){
            auto values=docs[3].values();if(values[lower(f.section)].empty())for(auto i=docs[3].lines.begin();i!=docs[3].lines.end();++i)if(lower(Document::content(i->text))=="["+lower(f.section)+"]"){i->text.clear();break;}}
    }return docs;
}
inline std::string wire(const std::string& value){std::string out;for(auto c:value)switch(c){case '%':out+="%25";break;case '\t':out+="%09";break;case '\r':out+="%0d";break;case '\n':out+="%0a";break;default:out+=c;}return out;}
inline std::string serialize(const std::vector<Field>& fields,const std::string& revision,const std::string& activeRevision){
    std::string out="NLS1\t"+revision+"\t"+activeRevision+"\n";
    for(const auto& f:fields){bool first=true;for(const auto& cell:{std::string("F"),std::string(1,"QRCS"[f.file]),f.section,f.key,f.kind,f.low,f.high,f.step,f.saved,f.active,f.factory,f.overrideValue,f.apply,f.options,f.origin,f.inherited,f.factoryOverride,std::string(f.available?"1":"0")}){if(!first)out+='\t';first=false;out+=wire(cell);}out+='\n';}
    return out;
}
} // namespace NorthlightSettings
