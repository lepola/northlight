#pragma once
#include <cstdint>

// Portable decision logic (no D3D/Windows; the caller passes the tick clock).
// A 32-bit process whose largest free block is ~63 MiB cannot build the 64 MiB
// terrain allocation of an extended (4096) shadow reach, and the build was retried
// every second until the camera left the region. A memory refusal of an EXTENDED
// build now rebuilds at the base reach at once; no admission margin is relaxed.
namespace NorthlightTerrainReach {
constexpr float BaseReach=928.f; // == NorthlightShadowTerrain::Radius
// Restore rule: the extended reach is tried again no sooner than BackoffMs after the
// LAST memory refusal of an extended build (each refused retry restarts the clock).
constexpr uint32_t BackoffMs=30000;
inline bool extended(float reach){return reach>BaseReach;}
struct State {
    bool refused=false;uint32_t refusedAt=0;
    // DWORD tick wrap safe: unsigned subtraction.
    bool holding(uint32_t now)const{return refused&&uint32_t(now-refusedAt)<BackoffMs;}
};
struct Choice { float reach=BaseReach;bool reduced=false,restored=false; };
// reach for the next build of a region whose profile reach is `profile`.
inline Choice choose(State& state,float profile,uint32_t now){
    Choice c;c.reach=profile;
    if(state.holding(now)){if(extended(profile)){c.reach=BaseReach;c.reduced=true;}return c;}
    c.restored=state.refused&&extended(profile);state.refused=false;return c;
}
// A stage of a build at `usedReach` was refused for MEMORY. True: rebuild at BaseReach now.
// Only an extended build reduces; non-memory errors and base-reach builds never call this.
inline bool memoryRefused(State& state,float usedReach,uint32_t now){
    if(!extended(usedReach))return false;
    state.refused=true;state.refusedAt=now;return true;
}
// One geometry-memory stall episode (log begin/end, never per retry).
struct Episode {
    bool active=false,reduced=false;uint32_t since=0;unsigned attempts=0;
    bool refused(uint32_t now){const bool begin=!active;if(begin){active=true;reduced=false;since=now;attempts=0;}++attempts;return begin;}
    void markReduced(){reduced=true;}
    bool end(uint32_t now,uint32_t& ms,unsigned& tries){
        if(!active)return false;active=false;ms=uint32_t(now-since);tries=attempts;return true;
    }
};
} // namespace NorthlightTerrainReach
