#pragma once
#include <filesystem>
#include "world_local_lights.h"
#include "rigid_geometry.h"
#include <array>
#include <map>
#include <memory>

namespace NorthlightLightAttachments {
struct Attachment {
    uint64_t sourceId=0;uint32_t bone=0,reference=0;
    std::array<float,9> witness{};
    float point[3]{},offset[3]{},placement[12]{};
};
static_assert(sizeof(Attachment)==128,"FGA1 record layout");
using Records=std::vector<Attachment>;
inline bool readFile(const std::string& path,Records& output){
    std::ifstream file(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);if(!file)return false;
    const auto length=file.tellg();if(length<16)return false;file.seekg(0);
    char magic[4];uint32_t header[3];file.read(magic,4);file.read(reinterpret_cast<char*>(header),12);
    if(!file||std::memcmp(magic,"FGA1",4)||header[0]!=1||header[1]!=sizeof(Attachment)||header[2]>1000000||
       uint64_t(length)!=16+uint64_t(header[2])*sizeof(Attachment))return false;
    Records records(header[2]);if(!records.empty())file.read(reinterpret_cast<char*>(records.data()),records.size()*sizeof(Attachment));
    if(!file)return false;
    for(size_t i=0;i<records.size();++i){const auto& a=records[i];if(a.bone>=75||a.reference>=75)return false;
        for(float v:a.witness)if(!std::isfinite(v)||std::fabs(v)>=100000)return false;
        for(const auto* values:{a.point,a.offset,a.placement}){const unsigned count=values==a.placement?12:3;
            for(unsigned k=0;k<count;++k)if(!std::isfinite(values[k])||std::fabs(values[k])>=100000)return false;}
        if(i&&a.witness<records[i-1].witness)return false;
    }
    output.swap(records);return true;
}
class Cache {
    std::string root_,map_;std::shared_ptr<const Records> records_;
public:
    explicit Cache(std::string root):root_(std::move(root)){}
    std::shared_ptr<const Records> load(const std::string& map){
        if(!NorthlightLocalLights::safeMap(map))return {};
        if(map!=map_){Records candidate;readFile(root_+"/"+map+".fga",candidate);
            records_=std::make_shared<const Records>(std::move(candidate));map_=map;}
        return records_;
    }
};
struct Tracker {
    std::shared_ptr<const Records> records;
    std::map<uint64_t,std::array<float,3>> positions;
    void reset(){records.reset();positions.clear();}
    void bind(const std::shared_ptr<const Records>& next){if(next!=records){reset();records=next;}}
    bool active()const{return records&&!records->empty();}
    void prune(const std::vector<NorthlightLocalLights::Light>& lights,const float* camera){
        for(auto at=positions.begin();at!=positions.end();){
            auto light=std::lower_bound(lights.begin(),lights.end(),at->first,[](const auto& l,uint64_t id){return l.sourceId<id;});
            bool keep=light!=lights.end()&&light->sourceId==at->first;
            if(keep){float d2=0;for(unsigned k=0;k<3;++k){const float d=at->second[k]-camera[k];d2+=d*d;}
                const float reach=256+light->attenuationEnd;keep=d2<reach*reach;}
            if(keep)++at;else at=positions.erase(at);
        }
    }
    // A fixed reference bone plus the exact skin triangle identifies a placed
    // source even while its emitting bone rotates, translates or scales.
    void observe(const std::array<float,9>& witness,const float* constants,unsigned paletteBase,const float* inverseView){
        if(!active()||!constants||!inverseView)return;
        for(float v:witness)if(!std::isfinite(v))return;
        auto at=std::lower_bound(records->begin(),records->end(),witness,[](const Attachment& a,const auto& key){return a.witness<key;});
        for(;at!=records->end()&&at->witness==witness;++at){const auto& a=*at;
            if(paletteBase+3*std::max(a.bone,a.reference)+2>=256)continue;
            float reference[12],source[12];
            if(!NorthlightRigidGeometry::worldBone(constants+4*(paletteBase+3*a.reference),inverseView,reference)||
               !NorthlightRigidGeometry::worldBone(constants+4*(paletteBase+3*a.bone),inverseView,source))continue;
            for(unsigned k=0;k<3;++k)for(unsigned j=0;j<3;++j)reference[9+k]-=reference[3*j+k]*a.offset[j];
            if(!NorthlightRigidGeometry::staticPlacement(reference+9,reference,a.placement+9,a.placement))continue;
            std::array<float,3> point{};bool valid=true;
            for(unsigned k=0;k<3;++k){point[k]=source[9+k];for(unsigned j=0;j<3;++j)point[k]+=source[3*j+k]*a.point[j];
                valid=valid&&std::isfinite(point[k])&&std::fabs(point[k])<100000;}
            if(valid)positions[a.sourceId]=point;
        }
    }
    const std::vector<NorthlightLocalLights::Light>& apply(const std::vector<NorthlightLocalLights::Light>& fixed,std::vector<NorthlightLocalLights::Light>& output)const{
        if(positions.empty())return fixed;
        output=fixed;for(auto& light:output){auto at=positions.find(light.sourceId);if(at!=positions.end())std::copy(at->second.begin(),at->second.end(),light.position);}
        return output;
    }
};
} // namespace NorthlightLightAttachments
