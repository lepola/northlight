#pragma once
// The settings window's edit draft over a snapshot's fields (settings_panel.cpp). No ImGui: tested natively.
// A draft maps a field id to "~" (inherit), "@factory" or a literal value. What the window shows for a field is
// its effective value: the draft, else the saved override, else what it inherits (the preset's value for a Quality
// key, the default profile's value for a regional Sun/Moon key, otherwise the schema's inherited value).
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "settings_schema.h"

namespace NorthlightSettingsDraft {
using NorthlightSettings::Field;
enum File : unsigned { Quality=0, Renderer=1, Celestial=2, ShadowRange=3 };

inline std::vector<std::string> split(const std::string& text){
    std::vector<std::string> out;std::size_t start=0;
    if(text.empty())return out;
    for(;;){const auto at=text.find(',',start);out.push_back(text.substr(start,at==text.npos?text.npos:at-start));if(at==text.npos)return out;start=at+1;}
}
inline bool numeric(const std::string& s,double& out){
    if(s.empty())return false;char* end=nullptr;out=std::strtod(s.c_str(),&end);return end&&*end==0&&std::isfinite(out);
}
// Numbers compare by value ("1.50" == "1.5"); lists ("r,g,b") element by element.
inline bool same(const Field& f,const std::string& a,const std::string& b){
    if(a==b)return true;
    if(f.kind=="enum")return false;
    const auto x=split(a),y=split(b);if(x.size()!=y.size()||x.empty())return false;
    for(std::size_t i=0;i<x.size();++i){double p=0,q=0;if(!numeric(x[i],p)||!numeric(y[i],q)||std::fabs(p-q)>1e-6*std::max(1.0,std::fabs(q)))return false;}
    return true;
}
// AutoBumpTerrain/Walls/Grass are 0..2 integers in the schema but read as three named modes.
inline bool modeField(const Field& f){return f.file==Quality&&(f.key=="AutoBumpTerrain"||f.key=="AutoBumpWalls"||f.key=="AutoBumpGrass");}
inline std::vector<std::string> modeOptions(const Field& f){return std::atoi(f.high.c_str())>=2?std::vector<std::string>{"0","1","2"}:std::vector<std::string>{"0","1"};}
// Quality keys other than Preset and the named enums carry their per-preset values in options ("quality,balanced,performance").
inline bool presetValued(const Field& f){return f.file==Quality&&f.key!="Preset"&&f.kind!="enum";}

class Draft {
    const std::vector<Field>* fields=nullptr;
    std::map<std::string,std::size_t> byId;
    std::map<std::string,std::string> values;
    const Field* preset=nullptr;
    const Field* palette=nullptr;
    const Field* find(unsigned file,const std::string& section,const std::string& key)const{
        auto i=byId.find(std::string(1,NorthlightSettings::FileIds[file])+"\t"+section+"\t"+key);return i==byId.end()?nullptr:&(*fields)[i->second];
    }
    std::string resolve(const Field& f,bool useDraft,unsigned depth)const{
        std::string v=f.overrideValue;
        if(useDraft){auto d=values.find(f.id());if(d!=values.end())v=d->second;}
        if(v=="@factory")v=f.factoryOverride;
        if(v!="~")return v;
        if(depth>4)return f.inherited;
        if(f.file==Celestial&&NorthlightAppearancePresets::visualKey(f.key)){
            const auto selected=palette?resolve(*palette,useDraft,depth+1):"Default";
            const auto values=f.paletteValues.find(selected);
            if(values!=f.paletteValues.end()&&values->second.second)return values->second.first;
        }
        if(f.file==Celestial&&f.section!="default"){
            const auto parent=NorthlightSettings::parentSection(f.section);
            if(const Field* base=find(Celestial,parent,f.key))return resolve(*base,useDraft,depth+1);
            if(parent!="default"){
                const auto colon=f.section.find(':');if(colon!=f.section.npos){const auto mapBase=f.section.substr(0,colon)+":default";
                    if(mapBase!=parent)if(const Field* base=find(Celestial,mapBase,f.key))return resolve(*base,useDraft,depth+1);}
            }
            if(parent!="default")if(const Field* base=find(Celestial,"default",f.key))return resolve(*base,useDraft,depth+1);
        }
        if(f.section=="RegionalSlime"&&f.key!="default"){
            if(f.parentKey!="default")if(const Field* parent=find(f.file,f.section,f.parentKey))return resolve(*parent,useDraft,depth+1);
            if(const Field* base=find(f.file,f.section,"default")){
                if(useDraft?overrides(*base):base->overrideValue!="~")return resolve(*base,useDraft,depth+1);
                return f.unconfiguredValue;
            }
        }
        if(presetValued(f)&&preset){
            const auto p=resolve(*preset,useDraft,depth+1);const auto options=split(f.options);
            const std::size_t index=p=="Performance"?2:p=="Balanced"?1:0;
            if(index<options.size()&&!options[index].empty())return options[index];
        }
        return f.inherited;
    }
public:
    // Adopts new fields; draft entries whose field no longer exists are dropped.
    void bind(const std::vector<Field>* next){
        fields=next;byId.clear();preset=nullptr;palette=nullptr;
        if(!fields){values.clear();return;}
        for(std::size_t i=0;i<fields->size();++i){byId[(*fields)[i].id()]=i;}
        preset=find(Quality,"Quality","Preset");
        palette=find(Celestial,"Appearance","Palette");
        for(auto i=values.begin();i!=values.end();)i=byId.count(i->first)?std::next(i):values.erase(i);
    }
    const Field* field(unsigned file,const std::string& section,const std::string& key)const{return find(file,section,key);}
    const Field* quality(const std::string& key)const{return find(Quality,"Quality",key);}
    std::string effective(const Field& f)const{return resolve(f,true,0);}
    std::string saved(const Field& f)const{return resolve(f,false,0);}
    bool edited(const Field& f)const{return values.count(f.id())!=0;}
    // The field writes its own value (not inherited) once the draft is saved.
    bool overrides(const Field& f)const{
        auto d=values.find(f.id());std::string v=d==values.end()?f.overrideValue:d->second;
        if(v=="@factory")v=f.factoryOverride;return v!="~";
    }
    std::size_t size()const{return values.size();}
    bool empty()const{return values.empty();}
    void clear(){values.clear();}
    // Records value unless it equals what the field shows without this edit (which removes the edit instead).
    void set(const Field& f,const std::string& value){
        if(!fields)return;const auto id=f.id();
        values.erase(id);
        if(!same(f,value,effective(f)))values[id]=value;
    }
    void pin(const Field& f,const std::string& value){
        if(f.overrideValue!="~"&&same(f,f.overrideValue,value))values.erase(f.id());
        else values[f.id()]=value;
    }
    // Right-click: back to the inherited value. Fields whose saved file never overrides need no draft entry.
    void inherit(const Field& f){if(f.overrideValue=="~")values.erase(f.id());else values[f.id()]="~";}
    void factory(const Field& f){if(f.overrideValue==f.factoryOverride)values.erase(f.id());else values[f.id()]="@factory";}
    std::vector<std::pair<std::string,std::string>> changes()const{return {values.begin(),values.end()};}
    // A restart-only field whose shown value differs from what the running renderer uses.
    bool restartPending(const Field& f)const{return f.apply=="R"&&f.available&&!same(f,effective(f),f.active);}
};

inline bool canInherit(const Field& f){return f.available&&!(f.file==Renderer&&f.key=="Backend"&&(","+f.options+",").find(",legacy,")==std::string::npos);}

struct WaterTier { const char* name; const char* scenery; const char* resolution; const char* distance; const char* fineHighlights; const char* sceneryDetails; const char* depthBlur; };
inline constexpr WaterTier WaterTiers[]={
    {"Ultra","1","100","2","1","1","1"}, {"Quality","1","100","0","0","0","1"},
    {"Balanced","1","75","0","0","0","1"}, {"Performance","0","50","1","0","0","0"}
};
inline const char* waterPreset(const std::string& scenery,const std::string& resolution,const std::string& distance,const std::string& fineHighlights,const std::string& sceneryDetails,const std::string& depthBlur){
    double blur;
    if(!numeric(depthBlur,blur))return "Custom";
    for(const auto& tier:WaterTiers)
        if(blur==std::atof(tier.depthBlur) && fineHighlights==tier.fineHighlights && scenery==tier.scenery && (scenery=="0" || (resolution==tier.resolution && distance==tier.distance && sceneryDetails==tier.sceneryDetails)))return tier.name;
    return "Custom";
}

// Keys a Quality field needs switched on (the old menu's dependencies; every GI* key needs GI).
inline std::vector<std::string> dependencies(const Field& f){
    static const std::map<std::string,std::vector<std::string>> table={
        {"AutoBumpStrength",{"AutoBump"}},{"AutoBumpDistance",{"AutoBump"}},{"AutoBumpTerrain",{"AutoBump"}},{"AutoBumpWalls",{"AutoBump"}},{"AutoBumpGrass",{"AutoBump"}},
        {"AutoParallaxDepth",{"AutoBump"}},{"AutoParallaxDistance",{"AutoBump"}},{"AutoParallaxSteps",{"AutoBump"}},
        {"AutoParallaxShadows",{"AutoBump"}},{"AutoParallaxShadowStrength",{"AutoBump","AutoParallaxShadows"}},{"AutoParallaxShadowSteps",{"AutoBump","AutoParallaxShadows"}},
        {"AutoCavity",{"AutoBump"}},{"AutoCavityStrength",{"AutoBump","AutoCavity"}},
        {"AOMethod",{"ContactAO"}},{"AOStrength",{"ContactAO"}},{"AORadius",{"ContactAO"}},
        {"ActorShadowBudgetMiB",{"ActorShadows"}},{"ActorShadowRadius",{"ActorShadows"}},{"ActorShadowCpuMs",{"ActorShadows"}},{"ActorShadowDetail",{"ActorShadows"}},
        {"PointShadowLimit",{"PointShadows"}},{"PointShadowRefreshMs",{"PointShadows"}},{"PointShadowFacesPerFrame",{"PointShadows"}},
        {"FogCloudDensity",{"FogClouds"}},{"RainFog",{"Weather"}},{"RainOpacity",{"Weather"}},{"RenderProfile",{"Diagnostics"}},
        {"ShadowFateDiagnostics",{"Diagnostics","ActorShadows"}},{"DiagReplayProbe",{"Diagnostics","RenderProfile","ActorShadows"}},
        {"SpellLayerClasses",{"SpellLayer"}},{"SpellTrace",{"SpellLayer"}},{"SpellFog",{"SpellLayer"}},{"NameMaskDebug",{"NameMask"}},
        {"StreamFramesAhead",{"CommandStream"}},{"StreamFrameSkip",{"CommandStream"}},{"StreamOcclusionLocal",{"CommandStream"}},
    };
    if(f.file!=Quality)return {};
    auto i=table.find(f.key);if(i!=table.end())return i->second;
    if(f.key.size()>2&&f.key.compare(0,2,"GI")==0)return {"GI"};
    return {};
}
// Why the field cannot be edited now, or "" (the missing key is named by its id; the panel words it).
enum class Block { None, Unavailable, NeedsHdr, NeedsKey };
inline Block blocked(const Draft& d,const Field& f,std::string& missing){
    missing.clear();
    if(!f.available)return Block::Unavailable;
    if(f.file==Renderer&&f.key!="Backend"&&f.key!="ExperimentalHDR")
        if(const Field* hdr=d.field(Renderer,"Renderer","ExperimentalHDR"))if(d.effective(*hdr)=="0")return Block::NeedsHdr;
    if(f.file==NorthlightSettings::WaterFile && (f.key=="ReflectionResolution" || f.key=="ReflectionDistance" || f.key=="ReflectionSceneryDetails")){
        const auto* scenery=d.field(f.file,"Water","ReflectionScenery");
        if(scenery && d.effective(*scenery)=="0"){missing="ReflectionScenery";return Block::NeedsKey;}
    }
    for(const auto& key:dependencies(f))if(const Field* parent=d.quality(key))if(d.effective(*parent)=="0"){missing=key;return Block::NeedsKey;}
    return Block::None;
}
} // namespace NorthlightSettingsDraft
