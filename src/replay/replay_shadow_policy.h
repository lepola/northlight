#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// Experimental quality policy, separate from capture safety and proven bounds.
// Unknown distances get first claim; a sampled vertex is a ranking heuristic,
// never a geometric culling certificate. Stable ties retain original order.
namespace NorthlightReplayShadowPolicy {
inline unsigned bucket(unsigned triangles){
    return triangles<25?0:triangles<50?1:triangles<100?2:triangles<500?3:triangles<2000?4:5;
}
inline bool small(bool skinned,unsigned triangles,unsigned minimum,bool rigid=false){
    return skinned&&!rigid&&minimum&&triangles<minimum;
}
struct Candidate {std::size_t index=0,bytes=0;float distanceSquared=0;bool known=false,keep=true;};
struct Result {std::size_t kept=0,dropped=0,keptBytes=0,droppedBytes=0,unknown=0;};
inline Result choose(std::vector<Candidate>& items,std::size_t budget){
    Result result;
    for(auto& item:items){item.keep=true;item.known=item.known&&std::isfinite(item.distanceSquared)&&item.distanceSquared>=0;result.unknown+=!item.known;}
    if(budget)std::sort(items.begin(),items.end(),[](const Candidate& a,const Candidate& b){
        if(a.known!=b.known)return !a.known;
        if(a.known&&a.distanceSquared!=b.distanceSquared)return a.distanceSquared<b.distanceSquared;
        return a.index<b.index;
    });
    for(auto& item:items){
        item.keep=!budget||item.bytes<=budget-result.keptBytes;
        if(item.keep){++result.kept;result.keptBytes+=item.bytes;}
        else{++result.dropped;result.droppedBytes+=item.bytes;}
    }
    return result;
}
// Dropped packets must remain alive: later packets can borrow their immutable
// constant storage. Preserve the original replay order and packet addresses.
template<class Packets> void retainSelected(Packets& packets,Packets& held){
    std::size_t count=0;for(const auto& p:packets)count+=!p->shadowSelected;
    held.reserve(held.size()+count); // allocate before moving any owner
    std::size_t write=0;
    for(std::size_t i=0;i<packets.size();++i){
        if(!packets[i]->shadowSelected)held.push_back(std::move(packets[i]));
        else{if(write!=i)packets[write]=std::move(packets[i]);++write;}
    }
    packets.resize(write);
}
}
