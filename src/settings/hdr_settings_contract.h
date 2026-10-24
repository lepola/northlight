#pragma once
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
// Shared creation-time configuration for the experimental HDR consumer and menu.
namespace NorthlightHdrSettingsContract {
struct Key {const char* name;float value,low,high,step=1;};
inline constexpr Key Keys[]={{"ExperimentalHDR",0,0,1},{"HDRPaperWhite",203,80,400},{"HDRUIWhite",160,40,400},{"HDRMaxNits",1000,80,10000},{"HDRContrast",1,.5f,2,.05f},{"HDRExposure",0,-4,4,.05f}};
inline bool parse(const std::string& text,float& value){
    char* end=nullptr;value=std::strtof(text.c_str(),&end);
    return end!=text.c_str()&&*end=='\0'&&std::isfinite(value);
}
inline float read(const Key& key,const std::string& text){float value=key.value;return parse(text,value)?std::clamp(value,key.low,key.high):key.value;}
}
