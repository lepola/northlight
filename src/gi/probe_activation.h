#pragma once
#include "world_probe_cache.h"
#include <string>
#include <vector>

// GPU receives the first valid time for each exact world key. Re-publication
// and camera movement never restart a resident probe's fade. Modulo collisions
// and map changes must not inherit another location's activation time.
class NorthlightProbeActivation {
    struct Slot {NorthlightGI::ProbeGridKey key;float born=0;bool valid=false;};
    std::vector<Slot> slots;
    std::string map;
    static float updateSlot(Slot& slot,const NorthlightGI::ProbeAtlasEntry& entry,float now){
        if(!entry.occupied||!entry.probe.valid){if(slot.valid)slot.valid=false;return -1.f;}
        // Compare all key components without short-circuiting resident probes.
        if(!slot.valid||((std::uint32_t(slot.key.x)^std::uint32_t(entry.key.x))|(std::uint32_t(slot.key.y)^std::uint32_t(entry.key.y))|(std::uint32_t(slot.key.z)^std::uint32_t(entry.key.z)))!=0){slot.key=entry.key;slot.born=now;slot.valid=true;}
        return slot.born;
    }
public:
    void reset(){slots.assign(NorthlightGI::probeLayout().atlasSize(),Slot{});map.clear();}
    // Sized by the process probe layout (0.3.153), which is fixed before the first upload.
    void begin(const std::string& next){if(map!=next||slots.size()!=NorthlightGI::probeLayout().atlasSize()){reset();map=next;}}
    bool covers(size_t count)const{return slots.size()>=count;}
    float update(size_t index,const NorthlightGI::ProbeAtlasEntry& entry,float now){return updateSlot(slots.at(index),entry,now);}
    // Caller must first establish covers(index+1).
    float updateKnownValid(size_t index,const NorthlightGI::ProbeAtlasEntry& entry,float now){return updateSlot(slots[index],entry,now);}
};
