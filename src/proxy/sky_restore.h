#pragma once
#include <cstdint>
namespace SkyRestore {
// IceCrownCitadelSky night materials: background, stars, then the first cloud layer.
// Exact mip content and draw state gate the existing early planet pass; unknown skies retain the late fallback.
constexpr std::uint64_t baseVS=0xff32338728131932ull,basePS=0xac008ac06cd451a9ull;
constexpr std::uint64_t cloudVS=0x9c37da43e2730eefull,cloudPS=0x7d592fa43d938a6cull;
struct Draw {unsigned count=0,fmt=0,w=0,h=0,src=0,dst=0;std::uint64_t vs=0,ps=0,hash=0;std::uintptr_t target=0;bool valid=false;};
inline bool base(const Draw& d){return d.valid&&d.target&&d.count==320&&d.fmt==0x35545844&&d.w==512&&d.h==512&&d.src==5&&d.dst==6&&d.vs==baseVS&&d.ps==basePS&&d.hash==0xddb1ff594d51e1aaull;}
inline bool cloud(const Draw& d){return d.valid&&d.target&&d.count==768&&d.fmt==0x35545844&&d.w==512&&d.h==512&&d.src==5&&d.dst==6&&d.vs==cloudVS&&d.ps==cloudPS&&d.hash==0x44e88f50406cbb61ull;}
struct State {unsigned frame=~0u;std::uintptr_t target=0;bool baseSeen=false,attempted=false;
 void begin(unsigned f){if(frame!=f){frame=f;target=0;baseSeen=attempted=false;}}
 void invalidate(){target=0;baseSeen=false;}
 void targetSet(std::uintptr_t next){if(target!=next)invalidate();}
 void colorWrite(std::uintptr_t destination){if(target==destination)invalidate();}
 void after(const Draw& d,bool success){if(!baseSeen&&success&&base(d)){baseSeen=true;target=d.target;}}
 bool before(const Draw& d){if(!attempted&&baseSeen&&target==d.target&&cloud(d)){attempted=true;return true;}return false;}
};
}
