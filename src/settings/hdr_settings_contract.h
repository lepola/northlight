#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <array>
#include <string>
// Shared live HDR configuration for the renderer and menu.
namespace NorthlightHdrSettingsContract {
inline bool displayKey(unsigned index){return index==1||index==2||index==3||index==7;}
struct Key {const char* name;float value,low,high,step=1;};
enum Index {Enabled,PaperWhite,UIWhite,MaxNits,Contrast,Exposure,ExposureMode,PeakMode,BloomStrength,LegacyExpansion,AutoMinEV,AutoMaxEV,LightAdaptation,DarkAdaptation,Count};
inline constexpr Key Keys[]={{"ExperimentalHDR",1,0,1},{"HDRPaperWhite",203,80,400},{"HDRUIWhite",160,40,400},{"HDRMaxNits",1000,80,10000},{"HDRContrast",1,.5f,2,.05f},{"HDRExposure",0,-4,4,.05f},
    {"HDRExposureMode",1,0,1},{"HDRPeakMode",1,0,1},{"HDRBloomStrength",1,0,2,.05f},{"HDRLegacyExpansion",0,0,2,.05f},
    {"HDRAutoMinEV",-2,-4,0,.05f},{"HDRAutoMaxEV",1,0,4,.05f},{"HDRLightAdaptation",.5f,.05f,10,.05f},{"HDRDarkAdaptation",2,.05f,10,.05f}};
static_assert(std::size(Keys)==Count);
inline bool mode(const Key& key){return std::string(key.name)=="HDRExposureMode"||std::string(key.name)=="HDRPeakMode";}
inline const char* modeName(float value){return value>.5f?"Auto":"Manual";}
inline std::array<float,Count> defaults(){std::array<float,Count> result{};for(unsigned i=0;i<Count;++i)result[i]=Keys[i].value;return result;}
inline bool parse(const std::string& text,float& value){
    char* end=nullptr;value=std::strtof(text.c_str(),&end);
    return end!=text.c_str()&&*end=='\0'&&std::isfinite(value);
}
inline float read(const Key& key,const std::string& text){
    if(mode(key)){std::string name=text;for(char& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));if(name=="auto")return 1;if(name=="manual")return 0;}
    float value=key.value;return parse(text,value)?std::clamp(value,key.low,key.high):key.value;
}
}
