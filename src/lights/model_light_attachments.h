#pragma once
#include <filesystem>
#include "world_local_lights.h"
#include "rigid_geometry.h"
#include "equipment_lights.h"
#include <array>
#include <map>
#include <memory>

namespace NorthlightLightAttachments {
inline constexpr uint32_t NativeSource=1;
struct Attachment {
    uint64_t sourceId=0;uint32_t bone=0,reference=0;
    std::array<float,9> witness{};
    float point[3]{},offset[3]{},placement[12]{};uint32_t flags=0;
};
static_assert(sizeof(Attachment)==128,"FGA1 record layout");
using Records=std::vector<Attachment>;
inline bool readFile(const std::string& path,Records& output){
    std::ifstream file(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);if(!file)return false;
    const auto length=file.tellg();if(length<16)return false;file.seekg(0);
    char magic[4];uint32_t header[3];file.read(magic,4);file.read(reinterpret_cast<char*>(header),12);
    if(!file||std::memcmp(magic,"FGA1",4)||(header[0]!=1&&header[0]!=2)||header[1]!=sizeof(Attachment)||header[2]>1000000||
       uint64_t(length)!=16+uint64_t(header[2])*sizeof(Attachment))return false;
    Records records(header[2]);if(!records.empty())file.read(reinterpret_cast<char*>(records.data()),records.size()*sizeof(Attachment));
    if(!file)return false;
    for(size_t i=0;i<records.size();++i){const auto& a=records[i];if((a.flags&~NativeSource)||(header[0]==1&&a.flags)||
            (a.flags&NativeSource?(a.bone>=64||a.reference):a.bone>=75||a.reference>=75))return false;
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
    struct NativeBinding {std::uint64_t id=0;bool present=false,required=false;NorthlightLocalLights::Light light{};};
    std::map<uint64_t,NativeBinding> nativeBindings;
    std::map<uint64_t,const Attachment*> nativeProofs;
    std::vector<uint64_t> retiredNative;
    std::vector<std::uint64_t> sourceIds;
    std::vector<std::size_t> candidates;
    std::array<uint64_t,NorthlightNativeLights::Capacity> claimedNative{};unsigned claimedNativeCount=0;
    const std::vector<NorthlightLocalLights::Light>* checkedSources=nullptr;bool requiresNative=false;
    void reset(){records.reset();positions.clear();nativeBindings.clear();nativeProofs.clear();retiredNative.clear();sourceIds.clear();candidates.clear();claimedNativeCount=0;checkedSources=nullptr;requiresNative=false;}
    void bind(const std::shared_ptr<const Records>& next){if(next!=records){std::vector<uint64_t> retired;for(const auto& binding:nativeBindings)if(binding.second.required)retired.push_back(binding.first);reset();retiredNative=std::move(retired);records=next;
        if(records){for(const auto& record:*records)sourceIds.push_back(record.sourceId);
            for(const auto& record:*records)if(record.flags&NativeSource)nativeProofs.emplace(record.sourceId,&record);
            std::sort(sourceIds.begin(),sourceIds.end());sourceIds.erase(std::unique(sourceIds.begin(),sourceIds.end()),sourceIds.end());}}}
    // Match a root emitter's original world point against the unmodified FGL
    // source, then keep its existing range, shadow flags and identity in motion.
    void observeScene(const NorthlightNativeLights::Snapshot& scene,const std::vector<NorthlightLocalLights::Light>& fixed){
        claimedNativeCount=0;
        if(!active())return;
        for(auto& entry:nativeBindings)entry.second.present=false;
        if(!scene.valid||!NorthlightNativeLights::wellFormed(scene))return;
        bool candidatesReady=false;
        for(unsigned i=0;i<scene.count;++i){const auto& point=scene.lights[i];if(!(point.flags&NorthlightNativeLights::RootAnchor))continue;
            auto binding=std::find_if(nativeBindings.begin(),nativeBindings.end(),[&](const auto& entry){return entry.second.id==point.id;});
            if(binding==nativeBindings.end()){
                if(!candidatesReady){candidates.clear();
                    if(std::is_sorted(fixed.begin(),fixed.end(),[](const auto& a,const auto& b){return a.sourceId<b.sourceId;})){
                        auto id=sourceIds.begin();for(std::size_t k=0;k<fixed.size();++k){while(id!=sourceIds.end()&&*id<fixed[k].sourceId)++id;
                            if(id!=sourceIds.end()&&*id==fixed[k].sourceId&&fixed[k].kind==2)candidates.push_back(k);}
                    }else for(std::size_t k=0;k<fixed.size();++k)if(fixed[k].kind==2&&std::binary_search(sourceIds.begin(),sourceIds.end(),fixed[k].sourceId))candidates.push_back(k);
                    candidatesReady=true;}
                const auto matches=[&](const auto index){const auto& candidate=fixed[index];
                    const auto proof=nativeProofs.find(candidate.sourceId);
                    const bool required=candidate.flags&NorthlightLocalLights::RequiresNative;
                    if(required&&proof==nativeProofs.end())return false;
                    // Reader reserves 64 IDs per model; low six bits identify
                    // the authored native record even for coincident bulbs.
                    if(proof!=nativeProofs.end()&&((point.id>>52)!=0xfea||((point.id-1)&63)!=proof->second->bone))return false;
                    const float* anchor=proof!=nativeProofs.end()?proof->second->offset:candidate.position;
                    if(NorthlightEquipmentLights::distance2(point.anchor,anchor)>=.25f*.25f||
                       !NorthlightEquipmentLights::hueClose(point.color,candidate.diffuse))return false;
                    const auto used=nativeBindings.find(candidate.sourceId);return used==nativeBindings.end()||!used->second.present;};
                const auto light=std::find_if(candidates.begin(),candidates.end(),matches);
                if(light==candidates.end())continue;
                if(nativeProofs.count(fixed[*light].sourceId)&&std::find_if(light+1,candidates.end(),matches)!=candidates.end())continue;
                binding=nativeBindings.insert_or_assign(fixed[*light].sourceId,NativeBinding{point.id,false,bool(fixed[*light].flags&NorthlightLocalLights::RequiresNative),fixed[*light]}).first;
            }
            binding->second.present=true;std::copy_n(point.position,3,positions[binding->first].begin());
            const auto proof=nativeProofs.find(binding->first);
            if(proof!=nativeProofs.end()){
                auto light=binding->second.light;
                if(binding->second.required){std::copy_n(point.color,3,light.diffuse);light=NorthlightLocalLights::nativeTransport(light,point.attenuation);}
                light.flags&=~NorthlightLocalLights::RequiresNative;
                for(unsigned k=0;k<3;++k)light.position[k]=point.position[k]+proof->second->point[k];
                if(!NorthlightLocalLights::valid(light)){binding->second.present=false;continue;}
                binding->second.light=light;std::copy_n(light.position,3,positions[binding->first].begin());
                if(claimedNativeCount<claimedNative.size())claimedNative[claimedNativeCount++]=point.id;
            }
        }
        std::sort(claimedNative.begin(),claimedNative.begin()+claimedNativeCount);
    }
    void releaseScene(){nativeBindings.clear();claimedNativeCount=0;}
    bool active()const{return records&&!records->empty();}
    bool needsDrawPose()const{return positions.size()>nativeBindings.size();}
    void forgetInactive(NorthlightLocalLightSelection::Tracker& selection){
        for(unsigned i=0;i<selection.count;){const auto id=selection.entries[i].id;
            const auto proof=nativeProofs.find(id);const auto binding=nativeBindings.find(id);
            if((proof!=nativeProofs.end()&&binding!=nativeBindings.end()&&binding->second.required&&!binding->second.present)||
               (proof==nativeProofs.end()&&std::binary_search(retiredNative.begin(),retiredNative.end(),id)))selection.forget(id);
            else ++i;
        }
        retiredNative.clear();
    }
    void prune(const std::vector<NorthlightLocalLights::Light>& lights,const float* camera){
        auto light=lights.begin(); // Both source IDs and position keys are sorted.
        for(auto at=positions.begin();at!=positions.end();){
            while(light!=lights.end()&&light->sourceId<at->first)++light;
            bool keep=light!=lights.end()&&light->sourceId==at->first;
            if(keep){float d2=0;for(unsigned k=0;k<3;++k){const float d=at->second[k]-camera[k];d2+=d*d;}
                const float reach=256+light->attenuationEnd;keep=d2<reach*reach;}
            if(keep)++at;else{nativeBindings.erase(at->first);at=positions.erase(at);}
        }
    }
    // A fixed reference bone plus the exact skin triangle identifies a placed
    // source even while its emitting bone rotates, translates or scales.
    void observe(const std::array<float,9>& witness,const float* constants,unsigned paletteBase,const float* inverseView){
        if(!active()||!constants||!inverseView)return;
        for(float v:witness)if(!std::isfinite(v))return;
        auto at=std::lower_bound(records->begin(),records->end(),witness,[](const Attachment& a,const auto& key){return a.witness<key;});
        unsigned previousReference=75,previousBone=75;float baseReference[12],source[12];bool referenceValid=false,sourceValid=false;
        for(;at!=records->end()&&at->witness==witness;++at){const auto& a=*at;
            if(a.flags&NativeSource)continue;
            if(paletteBase+3*std::max(a.bone,a.reference)+2>=256)continue;
            if(previousReference!=a.reference){previousReference=a.reference;referenceValid=NorthlightRigidGeometry::worldBone(constants+4*(paletteBase+3*a.reference),inverseView,baseReference);}
            if(!referenceValid)continue;
            if(previousBone!=a.bone){previousBone=a.bone;sourceValid=NorthlightRigidGeometry::worldBone(constants+4*(paletteBase+3*a.bone),inverseView,source);}
            if(!sourceValid)continue;
            float reference[12];std::copy_n(baseReference,12,reference);
            for(unsigned k=0;k<3;++k)for(unsigned j=0;j<3;++j)reference[9+k]-=reference[3*j+k]*a.offset[j];
            if(!NorthlightRigidGeometry::staticPlacement(reference+9,reference,a.placement+9,a.placement))continue;
            std::array<float,3> point{};bool valid=true;
            for(unsigned k=0;k<3;++k){point[k]=source[9+k];for(unsigned j=0;j<3;++j)point[k]+=source[3*j+k]*a.point[j];
                valid=valid&&std::isfinite(point[k])&&std::fabs(point[k])<100000;}
            if(valid)positions[a.sourceId]=point;
        }
    }
    const std::vector<NorthlightLocalLights::Light>& apply(const std::vector<NorthlightLocalLights::Light>& fixed,std::vector<NorthlightLocalLights::Light>& output){
        if(checkedSources!=&fixed){checkedSources=&fixed;requiresNative=std::any_of(fixed.begin(),fixed.end(),[](const auto& l){return l.flags&NorthlightLocalLights::RequiresNative;});}
        if(!nativeBindings.empty()||!nativeProofs.empty()||requiresNative){
            output.clear();output.reserve(fixed.size());
            for(auto light:fixed){const auto binding=nativeBindings.find(light.sourceId);
                if(light.flags&NorthlightLocalLights::RequiresNative){
                    if(binding!=nativeBindings.end()&&binding->second.present&&nativeProofs.count(light.sourceId))output.push_back(binding->second.light);
                    continue;
                }
                if(binding!=nativeBindings.end()&&!binding->second.present&&nativeProofs.find(light.sourceId)==nativeProofs.end())continue;
                const auto position=positions.find(light.sourceId);if(position!=positions.end())std::copy(position->second.begin(),position->second.end(),light.position);
                output.push_back(light);}
            return output;
        }
        if(positions.empty())return fixed;
        if(positions.size()>8&&std::is_sorted(fixed.begin(),fixed.end(),[](const auto& a,const auto& b){return a.sourceId<b.sourceId;})){
            output=fixed;auto at=positions.begin();for(auto& light:output){while(at!=positions.end()&&at->first<light.sourceId)++at;if(at!=positions.end()&&at->first==light.sourceId)std::copy(at->second.begin(),at->second.end(),light.position);}return output;
        }
        output=fixed;for(auto& light:output){auto at=positions.find(light.sourceId);if(at!=positions.end())std::copy(at->second.begin(),at->second.end(),light.position);}
        return output;
    }
};
} // namespace NorthlightLightAttachments
