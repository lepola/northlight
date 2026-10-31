#pragma once
// Portable FGL1 reader. Offline builder retains real asset source coordinates,
// intensities and authored attenuation ranges. No D3D/client APIs are used here.
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace NorthlightLocalLights {
inline constexpr uint32_t Baked=1,CastShadow=2,FirePulse=4,CandlePulse=8,GlassEmitter=16;
struct Light {
    float position[3],diffuse[3],attenuationStart,attenuationEnd;
    uint64_t sourceId;
    uint32_t kind,flags; // kind 1=WMO, 2=M2, 3=authored model emitter; flags bit0=possibly baked WMO source.
};
static_assert(sizeof(Light)==48,"FGL1 record layout");
inline bool valid(const Light& l) {
    for(float x:l.position)if(!std::isfinite(x)||std::fabs(x)>=100000)return false;
    float peak=0;
    for(float x:l.diffuse) {if(!std::isfinite(x)||x<0||x>10000)return false;peak=std::max(peak,x);}
    return peak>0&&std::isfinite(l.attenuationStart)&&std::isfinite(l.attenuationEnd)&&
        l.attenuationStart>=0&&l.attenuationStart<l.attenuationEnd&&l.attenuationEnd<=2000&&
        (l.kind==1||l.kind==2||l.kind==3)&&(l.flags&~(CastShadow|FirePulse|CandlePulse|GlassEmitter))==(l.kind==1?Baked:0u)&&
        (!(l.flags&(FirePulse|CandlePulse))||l.kind==3)&&
        (!(l.flags&GlassEmitter)||((l.kind==1||l.kind==3)&&(l.flags&CastShadow)))&&
        ((l.flags&(FirePulse|CandlePulse))!=(FirePulse|CandlePulse));
}
inline float pulseGain(const Light& l,uint32_t now){
    if(!(l.flags&(FirePulse|CandlePulse)))return 1.f;
    const uint32_t period=1240+uint32_t(l.sourceId%503),phase=uint32_t((l.sourceId>>32)%period);
    const auto triangle=[](uint32_t t,uint32_t p){return 1.f-std::fabs(2.f*float(t%p)/float(p)-1.f);};
    const float amount=(l.flags&CandlePulse)?.05f:.10f;
    return 1.f+amount*(triangle(now+phase,period)-.5f)+amount*.6f*(triangle(now+phase*3,period*3)-.5f);
}
// Authored range with bounded linear fade. This is explicit transport policy,
// not a claim that a baked classic WMO originally used this precise falloff.
inline float attenuation(const Light& l,float distance) {
    if(!std::isfinite(distance)||distance<0||!valid(l))return 0;
    if(distance<=l.attenuationStart)return 1;
    return std::max(0.f,(l.attenuationEnd-distance)/(l.attenuationEnd-l.attenuationStart));
}
inline bool safeMap(const std::string& map) {
    if(map.empty()||map.size()>63)return false;
    for(char c:map)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='\''||c==' '))return false;
    return true;
}
inline bool readFile(const std::string& path,std::vector<Light>& output) {
    // FGL1 is little endian, as are the supported macOS/Windows x86/ARM hosts.
    const uint32_t endian=1;if(*reinterpret_cast<const unsigned char*>(&endian)!=1)return false;
    std::ifstream f(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);if(!f)return false;
    const auto length=f.tellg();if(length<16)return false;f.seekg(0);
    char magic[4];uint32_t header[3];f.read(magic,4);f.read(reinterpret_cast<char*>(header),12);
    if(!f||std::memcmp(magic,"FGL1",4)||header[0]!=1||header[1]!=sizeof(Light)||header[2]>1000000||
       static_cast<uint64_t>(length)!=16+uint64_t(header[2])*sizeof(Light))return false;
    std::vector<Light> candidate(header[2]);
    if(!candidate.empty())f.read(reinterpret_cast<char*>(candidate.data()),candidate.size()*sizeof(Light));
    if(!f)return false;
    for(size_t i=0;i<candidate.size();++i)
        if(!valid(candidate[i])||(i&&candidate[i-1].sourceId>=candidate[i].sourceId))return false;
    output.swap(candidate);return true;
}
class Cache {
    std::string root_,map_;
    std::vector<Light> lights_;
public:
    explicit Cache(std::string root="world-cache/lights"):root_(std::move(root)){}
    void clear(){map_.clear();lights_.clear();}
    size_t totalCount()const{return lights_.size();}
    // Worker-thread API. Own one Cache per worker, or synchronize the caller.
    // Radius-intersection selection includes sources just outside maxDistance
    // whose authored influence extends into the requested region.
    bool loadLights(const std::string& map,const float* camera,float maxDistance,std::vector<Light>& output) {
        output.clear();
        if(!safeMap(map)||!camera||!std::isfinite(maxDistance)||maxDistance<0||maxDistance>100000)return false;
        for(int i=0;i<3;++i)if(!std::isfinite(camera[i])||std::fabs(camera[i])>=100000)return false;
        if(map!=map_) {
            std::vector<Light> candidate;
            if(!readFile(root_+"/"+map+".fgl",candidate))return false;
            lights_.swap(candidate);map_=map;
        }
        for(const auto& l:lights_) {
            const double radius=double(maxDistance)+l.attenuationEnd;
            const double dx=l.position[0]-camera[0];
            if(std::fabs(dx)>radius)continue;
            double d2=0;
            d2+=dx*dx;
            for(int i=1;i<3;++i){double d=l.position[i]-camera[i];d2+=d*d;}
            if(d2<=radius*radius)output.push_back(l);
        }
        // Source IDs retain deterministic order when camera moves; do not churn
        // the GI light signature by sorting the same lights by camera distance.
        return true;
    }
};
} // namespace NorthlightLocalLights
