#include "utf8_path.h"
#include "world_gi.h"
#include "world_local_lights.h"
#include "celestial_disc.h"
#include "celestial_disc_native.h"
#include "regional_shadow_range.h"
#include <cassert>
#ifdef _WIN32
#include <windows.h>
#endif

static void words(const std::string& path,std::initializer_list<uint32_t> data){
    FILE* f=NorthlightFiles::open(path.c_str(),"wb");assert(f);
    for(auto word:data)assert(std::fwrite(&word,4,1,f)==1);
    assert(std::fclose(f)==0);
}
int main(int argc,char** argv){
    assert(argc==2);namespace fs=std::filesystem;
    const auto root=fs::u8path(argv[1])/fs::u8path(u8"Игра Ä 世界 🎮");fs::create_directories(root);
    const auto path=[&](const char* name){return (root/fs::u8path(name)).u8string();};
    // Real readers must decode UTF-8 irrespective of the Windows ANSI code page.
    words(path("scene.fgs"),{0x32534746,2,0,0,0});
    std::string error;
    NorthlightGI::WorldScene scene;assert(NorthlightGI::loadScene(path("scene.fgs").c_str(),scene,error));
    words(path("lights.fgl"),{0x314c4746,1,48,0});
    std::vector<NorthlightLocalLights::Light> lights;assert(NorthlightLocalLights::readFile(path("lights.fgl"),lights));
    words(path("sun.fct"),{0x31544346,1,1,1,0xffffffff});
    NorthlightCelestialDisc::Texture texture;assert(NorthlightCelestialDisc::loadTexture(path("sun.fct"),texture));
    words(path("sun.fcm"),{0x314d4346,1,1,1,1,21,4,0xffffffff});
    std::vector<NorthlightCelestialDisc::MatchMip> match;assert(NorthlightCelestialDisc::loadMatch(path("sun.fcm"),match));
    {std::ofstream f(fs::u8path(path("profile.ini")));f<<"[default]\n";}
    NorthlightCelestialProfiles::Table profiles;assert(NorthlightCelestialProfiles::load(path("profile.ini"),profiles,error));
    {std::ofstream f(fs::u8path(path("range.ini")));f<<"[Kalimdor:440]\nterrain_radius=4096\n";}
    NorthlightRegionalShadow::Table ranges;assert(NorthlightRegionalShadow::load(path("range.ini"),ranges,error));
    {FILE* f=NorthlightFiles::open(path("fog.frf").c_str(),"wb");assert(f);
     uint32_t header[6]={0x31465246,1,1,2,0,0},zones[256]={};
     assert(std::fwrite(header,sizeof header,1,f)==1&&std::fwrite(zones,sizeof zones,1,f)==1);assert(!std::fclose(f));}
    NorthlightRegionalFog::Tile fog;assert(NorthlightRegionalFog::loadTile(path("fog.frf"),1,2,fog));
#ifdef _WIN32
    WIN32_FIND_DATAW data{};const auto found=FindFirstFileW((root/L"*.fgs").c_str(),&data);
    assert(found!=INVALID_HANDLE_VALUE);FindClose(found);
#endif
    std::puts("PASS UTF-8 paths: geometry, lights, sky, profiles, fog and enumeration");
}
