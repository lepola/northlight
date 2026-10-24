#include "regional_shadow_range.h"
#include "settings_defaults.h"
#include <iostream>

// Validate the actual runtime parsers; personal profiles may differ from factory defaults.
static bool factoryMatches(const std::string& path,const char* embedded){
    std::ifstream file(path,std::ios::binary);std::string text,error;
    if(!file||!NorthlightCelestialProfiles::readIniText(file,text,error))return false;
    std::istringstream input(text);std::string line,normalized;
    while(std::getline(input,line)){
        line=NorthlightCelestialProfiles::trim(line.substr(0,line.find('#')));
        if(!line.empty())normalized+=line+'\n';
    }
    return !input.bad()&&normalized==embedded;
}

int main(int argc,char** argv){
    if(argc!=3||(std::string(argv[1])!="--factory"&&std::string(argv[1])!="--personal"))return 2;
    const std::string root=argv[2],celestial=root+"/celestial-profiles.ini",shadow=root+"/shadow-range-profiles.ini";
    NorthlightCelestialProfiles::Table palette;NorthlightRegionalShadow::Table ranges;std::string error;
    if(!NorthlightCelestialProfiles::load(celestial,palette,error)){
        std::cerr<<celestial<<": "<<error<<'\n';return 1;
    }
    if(!NorthlightRegionalShadow::load(shadow,ranges,error)){
        std::cerr<<shadow<<": "<<error<<'\n';return 1;
    }
    if(std::string(argv[1])=="--factory"&&
       (!factoryMatches(celestial,kSettingsFactoryCelestial)||!factoryMatches(shadow,kSettingsFactoryShadowRange))){
        std::cerr<<"Factory profiles differ from the compiled settings reset defaults\n";return 1;
    }
    std::cout<<"PASS "<<argv[1]<<": production parsers; celestial zones="<<palette.zones.size()
             <<", shadow zones="<<ranges.zones.size()<<", Elwynn terrain radius="<<ranges.at("Azeroth",12)<<'\n';
}
