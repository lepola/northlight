#pragma once
#include "geometry_memory.h"
#include <algorithm>
#include <cstdint>
#include <limits>

// One instance per owning thread. Retain candidate ADDRESSES only; neither a previous
// region size nor the age of a background sample proves current free space.
// A fresh status + free-region query precedes every successful fast sample.
namespace NorthlightMemoryAdmission {
struct Region {std::uint64_t base=0,size=0;bool free=false;};
struct Observation {
    NorthlightGeometryMemory::Sample memory;
    bool witnessOnly=false,fullScan=false;
    unsigned regionQueries=0;
};
struct Stats {
    std::uint64_t requests=0,witnessHits=0,fullScans=0,regionQueries=0,statusFailures=0,malformedRegions=0;
};
class Probe {
    bool remembered_=false;
    std::uint64_t first_=0,last_=0;
    Stats stats_;
    static bool validRange(std::uint64_t address,const Region& region,std::uint64_t maximum){
        // Avoid base+size overflow, accept the final address-space byte, and
        // require the fresh answer to contain the actual requested address.
        return address<=maximum&&region.base<=address&&region.size&&
            region.base<=maximum&&region.size-1<=maximum-region.base&&
            address-region.base<region.size;
    }
    void remember(const Region& region){
        remembered_=true;first_=region.base;last_=region.base+(region.size-1);
    }
public:
    void invalidate(){remembered_=false;}
    const Stats& stats()const{return stats_;}
    // Status: bool(uint64_t& available), fresh GlobalMemoryStatusEx equivalent.
    // Query: bool(uint64_t address,Region&), fresh VirtualQuery equivalent.
    // A successful interior query may describe only the suffix from that page.
    template<class Status,class Query>
    Observation sample(const NorthlightGeometryMemory::Budget& required,Status status,Query query,
                       std::uint64_t maximum=std::numeric_limits<std::uintptr_t>::max()){
        Observation out;++stats_.requests;
        if(!status(out.memory.available)){
            out.memory={};++stats_.statusFailures;invalidate();return out;
        }
        auto read=[&](std::uint64_t address,Region& region){
            ++out.regionQueries;++stats_.regionQueries;
            if(!query(address,region))return false;
            if(!validRange(address,region,maximum)){++stats_.malformedRegions;return false;}
            return true;
        };
        if(remembered_&&first_<=maximum&&last_<=maximum){
            // These values choose where to ASK. Only the current response can
            // authorize a budget; splits, allocations and merges are rechecked.
            const auto first=first_,last=last_;
            auto witness=[&](std::uint64_t address){
                Region region;
                if(!read(address,region)||!region.free||region.size<required.largest)return false;
                out.memory.largest=region.size;out.memory.valid=true;
                out.witnessOnly=true;++stats_.witnessHits;remember(region);return true;
            };
            if(witness(first))return out;
            // VirtualQuery reports only the suffix from the queried page. Ask
            // at the start of a sufficiently large tail window, not its last
            // byte, when an allocation has occupied/split the old base.
            const auto needed=std::max<std::uint64_t>(1,required.largest);
            // Keep the end address useful after a small tail witness. A later
            // larger budget may ask farther back; the fresh answer still has
            // to prove the entire requirement, even outside the old suffix.
            if(needed-1<=last){
                const auto tail=last-(needed-1);
                if(tail!=first&&witness(tail))return out;
            }
        }
        // A missing, occupied, malformed or too-small witness never rejects an
        // otherwise sufficient process: run the original full sweep instead.
        // Like the original walker, an API failure ends the observed scan.
        invalidate();out.fullScan=true;++stats_.fullScans;
        std::uint64_t address=0;
        for(;;){
            Region region;if(!read(address,region))break;
            if(region.free&&region.size>out.memory.largest){out.memory.largest=region.size;remember(region);}
            const auto last=region.base+(region.size-1);
            if(last==maximum)break;
            address=last+1;
        }
        out.memory.valid=out.memory.largest>0;return out;
    }
};
}
