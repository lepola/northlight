#pragma once
// Portable whole-world appearance palettes. Presets use celestial-profiles.ini's
// region syntax, but accept only appearance keys and never sun/moon settings.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include "settings_store.h"
#include "celestial_profiles.h"

namespace NorthlightAppearancePresets {
using Table=NorthlightCelestialProfiles::Table;
using Profile=NorthlightCelestialProfiles::Profile;
inline constexpr char NameRuleText[]="Use 1 to 40 letters, numbers, spaces, underscores or hyphens; Default is reserved.";

inline const std::vector<std::string>& keys(){
    static const std::vector<std::string> value=[] {
        std::vector<std::string> out={"fog_tint","fog_tint_strength","shadow_contrast","tone_curve_strength"};
        for(const auto& field:NorthlightTone::gradeBindings())out.push_back(field.first);
        return out;
    }();
    return value;
}
inline bool visualKey(const std::string& key){const auto& all=keys();return std::find(all.begin(),all.end(),key)!=all.end();}
inline bool hasDirectValue(const Table& table,const std::string& section,const std::string& key){
    if(section=="default")return table.explicitFallbackKeys.count(key)!=0;
    if(section.size()>8&&section.compare(section.size()-8,8,":default")==0){
        const auto map=section.substr(0,section.size()-8);const auto at=table.explicitMapFallbackKeys.find(map);
        return at!=table.explicitMapFallbackKeys.end()&&at->second.count(key);
    }
    const auto colon=section.find(':');if(colon==section.npos)return false;
    const auto map=section.substr(0,colon);auto id=section.substr(colon+1);const auto slash=id.find('/');if(slash!=id.npos)id=id.substr(slash+1);
    if(id.size()>7&&id.compare(id.size()-7,7,"@inside")==0)id.resize(id.size()-7);
    if(id.empty()||id.find_first_not_of("0123456789")!=id.npos)return false;
    const auto zone=uint32_t(std::stoul(id));const auto at=table.explicitKeys.find({map,zone});
    return at!=table.explicitKeys.end()&&at->second.count(key);
}

struct Entry {std::string name;Table table;};
struct Catalog {
    std::vector<Entry> entries;
    std::vector<std::string> errors;
    const Entry* find(const std::string& name)const{
        const auto wanted=NorthlightSettings::lower(name);
        for(const auto& item:entries)if(NorthlightSettings::lower(item.name)==wanted)return &item;
        return nullptr;
    }
    std::vector<std::string> names()const{std::vector<std::string> out;for(const auto& e:entries)out.push_back(e.name);return out;}
};

// The looks the installer ships. A player may edit Vibrant's file by hand, but the menu neither replaces nor deletes
// these: an update restores them, so a deletion would not last and a replacement would be overwritten.
inline bool packaged(const std::string& name){
    const auto wanted=NorthlightSettings::lower(name);return wanted=="default"||wanted=="vibrant";
}
inline bool validName(const std::string& name,std::string& error){
    auto invalid=[&]{error=NameRuleText;return false;};
    if(name.empty()||name.size()>40||name=="."||name==".."||NorthlightSettings::lower(name)=="default")return invalid();
    if(std::isspace(static_cast<unsigned char>(name.front()))||std::isspace(static_cast<unsigned char>(name.back())))return invalid();
    for(unsigned char c:name)if(!std::isalnum(c)&&c!=' '&&c!='_'&&c!='-')return invalid();
    // Avoid Windows device names even when this code runs on a case-sensitive host.
    const auto base=NorthlightSettings::lower(name.substr(0,name.find('.')));
    if(base=="con"||base=="prn"||base=="aux"||base=="nul"||
       (base.size()==4&&(base.compare(0,3,"com")==0||base.compare(0,3,"lpt")==0)&&base[3]>='1'&&base[3]<='9'))return invalid();
    error.clear();return true;
}

inline bool parse(const std::string& raw,Table& out,std::string& error){
    try{
        NorthlightSettings::Document doc(raw);
        const auto values=doc.values();
        bool hasDefault=false,hasVisual=false;
        for(const auto& section:values){
            if(section.first=="default")hasDefault=true;
            for(const auto& field:section.second){
                if(!visualKey(field.first)){error="preset contains a non-appearance setting: "+field.first;return false;}
                hasVisual=true;
            }
        }
        if(!hasDefault||!hasVisual){error="preset needs a [default] section with appearance values";return false;}
        std::istringstream input(doc.text());
        if(!NorthlightCelestialProfiles::parse(input,out,error))return false;
        if(!out.palette.empty()){error="preset files cannot select another palette";return false;}
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}

using PresetPaths=std::map<std::string,std::vector<std::filesystem::path>>;
inline bool indexPresetFiles(const std::filesystem::path& folder,PresetPaths& paths,std::string& error){
    paths.clear();std::error_code ec;
    for(std::filesystem::directory_iterator it(folder,ec),end;!ec&&it!=end;it.increment(ec)){
        if(!it->is_regular_file(ec)||ec)continue;
        if(NorthlightSettings::lower(it->path().extension().string())==".ini")
            paths[NorthlightSettings::lower(it->path().stem().string())].push_back(it->path());
    }
    if(ec){error="Cannot list appearance-presets: "+ec.message();return false;}
    for(auto& row:paths)std::sort(row.second.begin(),row.second.end(),[](const auto& a,const auto& b){return a.filename().string()<b.filename().string();});
    error.clear();return true;
}
inline bool readPresetFile(const std::filesystem::path& path,Entry& entry,std::string& error){
    NorthlightSettings::FileImage image;
    try{image=NorthlightSettings::readImage(path);}catch(const std::exception& e){error=e.what();return false;}
    if(!image.exists){error="cannot read file";return false;}
    Table table;if(!parse(image.raw,table,error))return false;
    entry={path.stem().string(),std::move(table)};error.clear();return true;
}

inline Catalog load(const std::filesystem::path& root){
    Catalog out;const auto folder=root/"appearance-presets";std::error_code ec;
    if(!std::filesystem::exists(folder,ec)){if(ec)out.errors.push_back("Cannot inspect appearance-presets: "+ec.message());return out;}
    if(!std::filesystem::is_directory(folder,ec)){out.errors.push_back("appearance-presets is not a directory");return out;}
    PresetPaths paths;std::string error;
    if(!indexPresetFiles(folder,paths,error)){out.errors.push_back(error);return out;}
    for(const auto& row:paths){
        if(row.second.size()!=1){
            for(const auto& path:row.second)out.errors.push_back(path.filename().string()+": duplicate preset name");
            continue;
        }
        const auto& path=row.second.front();const auto name=path.stem().string();
        const bool packagedDefault=NorthlightSettings::lower(name)=="default"&&name=="Default";
        if(!packagedDefault&&!validName(name,error)){out.errors.push_back(path.filename().string()+": "+error);continue;}
        Entry entry;
        if(!readPresetFile(path,entry,error)){out.errors.push_back(path.filename().string()+": "+error);continue;}
        out.entries.push_back(std::move(entry));
    }
    return out;
}

inline Catalog loadSelected(const std::filesystem::path& root,const std::string& selected){
    Catalog out;
    if(NorthlightSettings::lower(selected)=="default")return out;
    const auto folder=root/"appearance-presets";std::error_code ec;
    if(!std::filesystem::is_directory(folder,ec)){
        if(ec)out.errors.push_back("Cannot inspect appearance-presets: "+ec.message());
        else out.errors.push_back("appearance-presets is not a directory");
        return out;
    }
    PresetPaths paths;std::string error;
    if(!indexPresetFiles(folder,paths,error)){out.errors.push_back(error);return out;}
    auto read=[&](const std::string& name){
        const auto found=paths.find(NorthlightSettings::lower(name));
        if(found==paths.end()){out.errors.push_back(name+".ini is missing from appearance-presets");return;}
        if(found->second.size()!=1){out.errors.push_back("Duplicate preset name: "+name);return;}
        Entry entry;
        if(!readPresetFile(found->second.front(),entry,error)){out.errors.push_back(found->second.front().filename().string()+": "+error);return;}
        out.entries.push_back(std::move(entry));
    };
    read("Default");
    read(selected);
    return out;
}

inline std::string value(const Profile& profile,const std::string& key){
    Profile copy=profile;unsigned count=0;float lo=0,hi=0;const float* target=NorthlightCelestialProfiles::keyTarget(copy,key,count,lo,hi);
    if(!target)throw std::runtime_error("Unknown appearance key: "+key);
    std::ostringstream out;out<<std::setprecision(6);for(unsigned i=0;i<count;++i){if(i)out<<',';out<<target[i];}return out.str();
}
inline Profile mapBase(const Table& table,const std::string& map){
    const auto i=table.mapFallbacks.find(map);return i==table.mapFallbacks.end()?table.fallback:i->second;
}
inline bool sameValue(const Profile& a,const Profile& b,const std::string& key){
    Profile x=a,y=b;unsigned n=0;float lo=0,hi=0;auto* p=NorthlightCelestialProfiles::keyTarget(x,key,n,lo,hi);auto* q=NorthlightCelestialProfiles::keyTarget(y,key,n,lo,hi);
    if(!p||!q)return false;for(unsigned i=0;i<n;++i)if(std::fabs(p[i]-q[i])>1e-6f)return false;return true;
}
inline Table compose(const Table& local,const Table& palette,const Table& defaults){
    Table out=local;out.palette=local.palette;
    for(const Table* source:{&palette,&defaults}){
        out.explicitFallbackKeys.insert(source->explicitFallbackKeys.begin(),source->explicitFallbackKeys.end());
        for(const auto& row:source->explicitMapFallbackKeys)out.explicitMapFallbackKeys[row.first].insert(row.second.begin(),row.second.end());
    }
    for(const auto& key:keys()){
        const bool localDirect=local.explicitFallbackKeys.count(key)!=0;
        const bool baselineDirect=defaults.explicitFallbackKeys.count(key)!=0;
        const bool personal=localDirect&&(!baselineDirect||!sameValue(local.fallback,defaults.fallback,key));
        if(personal)continue;
        Profile next=out.fallback;unsigned n=0;float lo=0,hi=0;auto* dst=NorthlightCelestialProfiles::keyTarget(next,key,n,lo,hi);
        Profile source=palette.fallback;auto* src=NorthlightCelestialProfiles::keyTarget(source,key,n,lo,hi);
        if(dst&&src)std::copy_n(src,n,dst);out.fallback=next;
    }
    std::set<std::string> maps;
    for(const auto& row:local.mapFallbacks)maps.insert(row.first);
    for(const auto& row:palette.mapFallbacks)maps.insert(row.first);
    for(const auto& map:maps){
        Profile base=mapBase(local,map),theme=mapBase(palette,map);
        Profile original=mapBase(local,map),factory=mapBase(defaults,map);
        const auto localMapKeys=local.explicitMapFallbackKeys.find(map),defaultMapKeys=defaults.explicitMapFallbackKeys.find(map);
        for(const auto& key:keys()){
            const bool localMapDirect=localMapKeys!=local.explicitMapFallbackKeys.end()&&localMapKeys->second.count(key);
            const bool defaultMapDirect=defaultMapKeys!=defaults.explicitMapFallbackKeys.end()&&defaultMapKeys->second.count(key);
            const bool localGlobalDirect=local.explicitFallbackKeys.count(key)!=0;
            const bool defaultGlobalDirect=defaults.explicitFallbackKeys.count(key)!=0;
            const bool personalGlobal=localGlobalDirect&&(!defaultGlobalDirect||!sameValue(local.fallback,defaults.fallback,key));
            const bool personalMap=localMapDirect?(!defaultMapDirect||!sameValue(original,factory,key)):personalGlobal;
            if(personalMap)continue;
            unsigned n=0;float lo=0,hi=0;auto* dst=NorthlightCelestialProfiles::keyTarget(base,key,n,lo,hi);auto* src=NorthlightCelestialProfiles::keyTarget(theme,key,n,lo,hi);
            if(dst&&src)std::copy_n(src,n,dst);
        }
        out.mapFallbacks[map]=base;
    }
    std::set<std::pair<std::string,uint32_t>> zones;
    for(const auto& row:local.zones)zones.insert(row.first);
    for(const auto& row:palette.zones)zones.insert(row.first);
    for(const auto& row:palette.parents)out.parents.emplace(row.first,row.second);
    for(const auto& row:defaults.parents)out.parents.emplace(row.first,row.second);
    for(const auto& id:palette.interiors)out.interiors.insert(id);
    for(const auto& id:defaults.interiors)out.interiors.insert(id);
    for(const auto& row:palette.explicitKeys)out.explicitKeys[row.first].insert(row.second.begin(),row.second.end());
    for(const auto& row:defaults.explicitKeys)out.explicitKeys[row.first].insert(row.second.begin(),row.second.end());
    for(const auto& row:defaults.zones)zones.insert(row.first);
    std::map<std::pair<std::string,uint32_t>,Profile> resolved;
    std::set<std::pair<std::string,uint32_t>> visiting;
    std::function<Profile(const std::pair<std::string,uint32_t>&)> composeZone=[&](const auto& id)->Profile{
        if(const auto found=resolved.find(id);found!=resolved.end())return found->second;
        if(!visiting.insert(id).second)return out.fallbackFor(id.first);
        Profile base=out.fallbackFor(id.first);
        if(const auto parent=out.parents.find(id);parent!=out.parents.end())base=composeZone({id.first,parent->second});
        const Profile original=local.at(id.first,id.second),factory=defaults.at(id.first,id.second),theme=palette.at(id.first,id.second);
        const auto localKeys=local.explicitKeys.find(id),themeKeys=palette.explicitKeys.find(id);
        const auto defaultKeys=defaults.explicitKeys.find(id);
        if(localKeys!=local.explicitKeys.end())for(const auto& key:localKeys->second)if(!visualKey(key)){
            Profile source=original;unsigned n=0;float lo=0,hi=0;auto* src=NorthlightCelestialProfiles::keyTarget(source,key,n,lo,hi);auto* dst=NorthlightCelestialProfiles::keyTarget(base,key,n,lo,hi);
            if(src&&dst)std::copy_n(src,n,dst);
        }
        for(const auto& key:keys()){
            const bool localDirect=localKeys!=local.explicitKeys.end()&&localKeys->second.count(key);
            const bool baselineDirect=defaultKeys!=defaults.explicitKeys.end()&&defaultKeys->second.count(key);
            const bool personal=localDirect&&(!baselineDirect||!sameValue(original,factory,key));
            const bool themed=themeKeys!=palette.explicitKeys.end()&&themeKeys->second.count(key);
            const Profile& source=personal?original:themed?theme:base;
            unsigned n=0;float lo=0,hi=0;auto* dst=NorthlightCelestialProfiles::keyTarget(base,key,n,lo,hi);Profile value=source;auto* src=NorthlightCelestialProfiles::keyTarget(value,key,n,lo,hi);
            if(dst&&src)std::copy_n(src,n,dst);
        }
        visiting.erase(id);resolved[id]=base;out.zones[id]=base;return base;
    };
    for(const auto& id:zones)composeZone(id);
    NorthlightCelestialProfiles::resolveLegacyDusk(out);
    return out;
}

inline std::string section(const Table& table,const std::pair<std::string,uint32_t>& id){
    if(table.interiors.count(id))return id.first+":"+std::to_string(id.second)+"@inside";
    const auto parent=table.parents.find(id);
    if(parent!=table.parents.end())return id.first+":"+std::to_string(parent->second)+"/"+std::to_string(id.second);
    return id.first+":"+std::to_string(id.second);
}
inline std::string serialize(const Table& table){
    std::ostringstream out;out<<"# Northlight appearance preset. Only regional appearance values are shared.\n[default]\n";
    auto write=[&](const Profile& p,const Profile* inherited){for(const auto& key:keys())if(!inherited||!sameValue(p,*inherited,key))out<<key<<"="<<value(p,key)<<'\n';};
    auto diff=[&](const Profile& p,const Profile& inherited){std::ostringstream body;for(const auto& key:keys())if(!sameValue(p,inherited,key))body<<key<<"="<<value(p,key)<<'\n';return body.str();};
    write(table.fallback,nullptr);
    for(const auto& row:table.mapFallbacks){
        const auto body=diff(row.second,table.fallback);
        if(body.empty())continue;out<<'\n'<<'['<<row.first<<":default]\n"<<body;
    }
    for(const auto& row:table.zones){
        Profile base=mapBase(table,row.first.first);
        const auto parent=table.parents.find(row.first);if(parent!=table.parents.end())base=table.at(row.first.first,parent->second);
        const auto body=diff(row.second,base);
        // Empty zone sections still carry subzone and interior lookup relationships.
        out<<'\n'<<'['<<section(table,row.first)<<"]\n"<<body;
    }
    return out.str();
}

inline void writeNew(const std::filesystem::path& path,const std::string& bytes){
#ifdef _WIN32
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Preset already exists or cannot be created");
    DWORD written=0;const bool ok=WriteFile(file,bytes.data(),DWORD(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file);CloseHandle(file);
    if(!ok){DeleteFileW(path.c_str());throw std::runtime_error("Preset write failed");}
#else
    const int file=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
    if(file<0)throw std::runtime_error(errno==EEXIST?"Preset already exists":"Preset cannot be created");
    size_t at=0;while(at<bytes.size()){const auto n=::write(file,bytes.data()+at,bytes.size()-at);if(n<=0){::close(file);::unlink(path.c_str());throw std::runtime_error("Preset write failed");}at+=size_t(n);}
    if(::fsync(file)){::close(file);::unlink(path.c_str());throw std::runtime_error("Preset flush failed");}::close(file);
#endif
}
// The whole new file is on disk before it takes the old one's place, so a failed write leaves the old look intact.
inline void writeReplacing(const std::filesystem::path& path,const std::string& bytes){
    auto staged=path;staged+=".new";std::error_code ec;std::filesystem::remove(staged,ec);
    writeNew(staged,bytes);
    try{NorthlightSettings::replaceFile(staged,path);}catch(...){std::filesystem::remove(staged,ec);throw;}
}
} // namespace NorthlightAppearancePresets
