#pragma once
#include "mesh_page_key.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

// 0.3.216 (issue #66): portable bookkeeping (no D3D) for the world mesh's GPU page buffers. The renderer owns the
// D3D vertex/index buffers in a vector indexed by the same buffer ids and only wires D3D calls to the decisions here.
//
// One store of buffers replaces the two full slots: the ACTIVE table (buffer id per Batch::page) is what is drawn;
// a staging build looks up every new page by content key among ALL buffers that still hold valid content (active or
// idle), reuses hits without any upload, and uploads the misses into idle buffers (or new ones). Invariants:
//   - a buffer referenced by the active table (Use::Active) is never claimed, written or released;
//   - a claimed buffer's key becomes invalid before its first write (markWriting; its content is then partial or stale).
//     Until then a cancelled staging (e.g. refused by memory admission) leaves the buffer's valid content reusable;
//   - the active table is replaced atomically at commit(); buffers it no longer references become Idle and KEEP
//     their key (their bytes are still valid and may match a later build);
//   - cancel() returns claimed buffers to Idle; the ones already written have an invalid key.
// The zero-hit case (every page misses) is the old double buffering: all pages upload next to the active mesh.
namespace NorthlightMeshBufferStore {
using Key=NorthlightMeshPageKey::Key;
constexpr uint32_t None=UINT32_MAX;
// GPU buffer capacity granularity. 16 KiB (was 64 KiB): content-defined pages are ~1.7x more numerous, and 64 KiB rounding
// alone cost ~8 % of the mesh (two buffers per page, ~32 KiB average waste each). Idle buffers are recreated to size.
constexpr uint32_t RoundBytes=16u<<10;
inline uint64_t roundBuffer(uint64_t bytes){return (bytes+RoundBytes-1)/RoundBytes*RoundBytes;}
enum class Use:uint8_t {Idle,Active,Claimed};
struct Entry {
    uint32_t vbCapacity=0,ibCapacity=0; // bytes of the D3D resources (0: not created)
    Key key;bool keyValid=false;        // the buffers hold exactly this page's bytes
    Use use=Use::Idle;
    uint64_t stamp=0;                   // commit clock when it last became idle (older = less recently useful)
    bool usable()const{return vbCapacity&&ibCapacity;}
    bool empty()const{return !vbCapacity&&!ibCapacity;}
    uint64_t capacity()const{return uint64_t(vbCapacity)+ibCapacity;}
};
struct Stats {
    size_t pages=0,reusedPages=0,uploadedPages=0;
    uint64_t reusedBytes=0,uploadedBytes=0;   // page payload bytes (vertex+index), not rounded capacity
    uint64_t uploadedCapacity=0;              // rounded capacity of the buffers that took the uploads (commit only)
};
struct Staging {
    bool open=false;
    std::vector<Key> keys;                    // content key per new page
    std::vector<uint32_t> table;              // buffer id per new page
    std::vector<uint32_t> upload;             // page indices to upload, ascending
    std::vector<uint32_t> uploadBuffer;       // claimed buffer id per upload entry (parallel to `upload`)
    Stats stats;
};
class Store {
    std::vector<Entry> entries_;
    std::vector<uint32_t> active_;
    Staging staging_;
    uint64_t clock_=0;
    size_t elidedMisses_=0;
    struct CapacityCache {const Entry* data=nullptr;size_t count=0;uint64_t pool=0,active=0,idle=0;bool valid=false;};
    mutable CapacityCache capacity_; // Renderer thread only, like the buffer metadata.
    const CapacityCache& capacities()const{
        if(!capacity_.valid||capacity_.data!=entries_.data()||capacity_.count!=entries_.size()){
            capacity_={};capacity_.data=entries_.data();capacity_.count=entries_.size();
            for(const auto& e:entries_){const auto bytes=e.capacity();capacity_.pool+=bytes;
                if(e.use==Use::Active)capacity_.active+=bytes;else if(e.use==Use::Idle)capacity_.idle+=bytes;}
            capacity_.valid=true;
        }
        return capacity_;
    }
public:
    size_t elidedMisses()const{return elidedMisses_;}
    // Compatibility describes resource lifetime policy; identical bytes alone
    // cannot prove that an elided page survives the next device Reset.
    // Keys of the buffers the ACTIVE table references (they cannot be released while active): what the worker may elide.
    template<class Compatible> NorthlightMeshPageKey::KeySet activeKeys(Compatible&& compatible)const{
        NorthlightMeshPageKey::KeySet set;set.reserve(active_.size());
        for(uint32_t id:active_)if(id<entries_.size()&&entries_[id].keyValid&&entries_[id].usable()&&compatible(id))set.insert(entries_[id].key);
        return set;
    }
    NorthlightMeshPageKey::KeySet activeKeys()const{return activeKeys([](uint32_t){return true;});}
    const std::vector<Entry>& entries()const{return entries_;}
    const std::vector<uint32_t>& table()const{return active_;}
    const Staging& staging()const{return staging_;}
    bool stagingOpen()const{return staging_.open;}
    // Account only resources that acquireMeshPage must create. Existing idle
    // buffers already consume address space; do not count them a second time.
    template<class HasResource> bool stagingGrowth(HasResource&& has,uint64_t& vertices,uint64_t& indices)const{
        if(!staging_.open||staging_.upload.size()!=staging_.uploadBuffer.size())return false;
        uint64_t v=0,i=0;
        for(size_t n=0;n<staging_.upload.size();++n){
            const auto page=staging_.upload[n],id=staging_.uploadBuffer[n];
            if(page>=staging_.keys.size()||!writable(id))return false;
            const auto& key=staging_.keys[page];const auto& entry=entries_[id];
            const uint64_t addV=has(id,true)&&entry.vbCapacity>=key.vertexBytes?0:roundBuffer(key.vertexBytes);
            const uint64_t addI=has(id,false)&&entry.ibCapacity>=key.indexBytes?0:roundBuffer(key.indexBytes);
            if(addV>std::numeric_limits<uint64_t>::max()-v||addI>std::numeric_limits<uint64_t>::max()-i)return false;
            v+=addV;i+=addI;
        }
        vertices=v;indices=i;return true;
    }
    // Active table lookup for drawing; None when the page does not exist.
    uint32_t bufferOf(uint32_t page)const{return page<active_.size()?active_[page]:None;}
    // The renderer may create, resize or write a buffer only when this is true.
    bool writable(uint32_t id)const{return id<entries_.size()&&entries_[id].use==Use::Claimed;}
    // Called before ANY create/write of a claimed buffer (idempotent): its previous content is gone from here on.
    bool markWriting(uint32_t id){if(!writable(id))return false;entries_[id].keyValid=false;return true;}
    // Records a created resource's capacity (claimed buffers only).
    bool setCapacity(uint32_t id,bool vertex,uint32_t bytes){
        if(!writable(id))return false;
        capacity_.valid=false;
        (vertex?entries_[id].vbCapacity:entries_[id].ibCapacity)=bytes;return true;
    }
    uint64_t poolBytes()const{return capacities().pool;}
    uint64_t idleBytes()const{return capacities().idle;}
    // Capacity of the distinct buffers the active table references.
    uint64_t activeBytes()const{
        return capacities().active;
    }
    void reset(){entries_.clear();active_.clear();staging_=Staging();clock_=0;capacity_.valid=false;}

    // Plans the upload of a new page list: hits reuse a buffer holding exactly that page (active or idle), misses
    // claim the cheapest idle buffer (tiers below; missing or too small resources are (re)created by the renderer) or, last,
    // a new slot. Returns false if a staging is already open.
    // `elided` (optional, parallel to keys): pages whose data the plan no longer holds. Each MUST hit; if any misses (its
    // buffer was released or replaced since the key set was published) nothing is claimed or changed, false is returned
    // and elidedMisses() is the count, so the caller can drop the plan and have a full one rebuilt.
    template<class Compatible> bool beginStaging(const std::vector<Key>& keys,const std::vector<uint8_t>& elided,Compatible&& compatible){
        elidedMisses_=0;
        if(staging_.open)return false;
        Staging s;s.open=true;s.keys=keys;s.table.assign(keys.size(),None);
        std::unordered_map<Key,uint32_t,NorthlightMeshPageKey::KeyHash> byKey;byKey.reserve(entries_.size());
        for(uint32_t id=0;id<entries_.size();++id){
            const Entry& e=entries_[id];
            if(!e.keyValid||!e.usable()||e.use==Use::Claimed||!compatible(id))continue;
            auto it=byKey.find(e.key);
            if(it==byKey.end())byKey.emplace(e.key,id);
            else if(e.use==Use::Active&&entries_[it->second].use!=Use::Active)it->second=id; // keep idle twins claimable
        }
        std::vector<uint8_t> reserved(entries_.size());
        for(uint32_t id:active_)if(id<reserved.size())reserved[id]=1;
        for(uint32_t i=0;i<keys.size();++i){
            auto it=byKey.find(keys[i]);
            if(it==byKey.end())continue;
            s.table[i]=it->second;reserved[it->second]=1;
        }
        for(uint32_t i=0;i<keys.size()&&i<elided.size();++i)if(elided[i]&&s.table[i]==None)++elidedMisses_;
        if(elidedMisses_)return false;
        std::unordered_map<Key,uint32_t,NorthlightMeshPageKey::KeyHash> claimed; // identical pages inside this build share a buffer
        for(uint32_t i=0;i<keys.size();++i){
            const uint64_t payload=uint64_t(keys[i].vertexBytes)+keys[i].indexBytes;
            if(s.table[i]!=None){++s.stats.reusedPages;s.stats.reusedBytes+=payload;continue;}
            auto twin=claimed.find(keys[i]);
            if(twin!=claimed.end()){s.table[i]=twin->second;++s.stats.reusedPages;s.stats.reusedBytes+=payload;continue;}
            // Choose the idle buffer that costs the pool the least. Tiers (lower is better), each by least growth, then
            // least waste, then oldest: 0 a fit within 2x of the need whose content is already invalid; 1 the same with
            // valid content; 2 any invalid buffer (recreated to size: it holds nothing worth keeping); 3 an oversized fit;
            // 4 a valid buffer to recreate larger (oldest content goes); 5 a new slot. A new slot is the last resort: it
            // is the only choice that always grows the pool by the whole page.
            const uint64_t needV=roundBuffer(keys[i].vertexBytes),needI=roundBuffer(keys[i].indexBytes),need=needV+needI;
            uint32_t pick=None;unsigned bestTier=6;uint64_t bestGrowth=0,bestWaste=0;
            for(uint32_t id=0;id<entries_.size();++id){
                const Entry& e=entries_[id];
                if(e.use!=Use::Idle||reserved[id])continue;
                const uint64_t growth=(e.vbCapacity<needV?needV-e.vbCapacity:0)+(e.ibCapacity<needI?needI-e.ibCapacity:0);
                const uint64_t after=e.capacity()+growth,waste=after-need;
                const bool fits=!growth&&e.usable(),close=fits&&after<=2*need;
                const unsigned tier=close?(e.keyValid?1u:0u):!e.keyValid?2u:fits?3u:4u;
                bool better=pick==None||tier<bestTier;
                if(!better&&tier==bestTier){
                    if(growth!=bestGrowth)better=growth<bestGrowth;
                    else if(waste!=bestWaste)better=waste<bestWaste;
                    else better=e.stamp<entries_[pick].stamp;
                }
                if(better){pick=id;bestTier=tier;bestGrowth=growth;bestWaste=waste;}
            }
            if(pick==None){pick=uint32_t(entries_.size());entries_.emplace_back();reserved.push_back(0);}
            Entry& e=entries_[pick];
            capacity_.valid=false;e.use=Use::Claimed;reserved[pick]=1; // the key stays valid until markWriting() (a staging cancelled before any write loses nothing)
            s.table[i]=pick;claimed.emplace(keys[i],pick);
            s.upload.push_back(i);s.uploadBuffer.push_back(pick);
            ++s.stats.uploadedPages;s.stats.uploadedBytes+=payload;
        }
        s.stats.pages=keys.size();
        staging_=std::move(s);return true;
    }
    bool beginStaging(const std::vector<Key>& keys,const std::vector<uint8_t>& elided={}){return beginStaging(keys,elided,[](uint32_t){return true;});}
    // Atomic switch: the uploaded buffers become valid, the new table becomes the active one, buffers it no longer
    // references go idle with their keys.
    Stats commit(){
        Stats stats=staging_.stats;
        if(!staging_.open)return stats;
        capacity_.valid=false;
        // Claimed marks the new table during this non-callback commit. Old
        // active entries left unmarked become idle; no capacity-sized bitmap.
        for(uint32_t id:staging_.table)entries_[id].use=Use::Claimed;
        for(size_t u=0;u<staging_.upload.size();++u){
            Entry& e=entries_[staging_.uploadBuffer[u]];
            e.key=staging_.keys[staging_.upload[u]];e.keyValid=true;stats.uploadedCapacity+=e.capacity();
        }
        ++clock_;
        for(uint32_t id:active_)if(id<entries_.size()&&entries_[id].use==Use::Active){entries_[id].use=Use::Idle;entries_[id].stamp=clock_;}
        for(uint32_t id:staging_.table)entries_[id].use=Use::Active;
        active_=std::move(staging_.table);staging_=Staging();return stats;
    }
    // Failure or orphaned staging: written claims hold partial content (key already invalid); untouched ones keep theirs.
    void cancel(){
        if(!staging_.open)return;
        capacity_.valid=false;
        for(uint32_t id:staging_.uploadBuffer)if(id<entries_.size()&&entries_[id].use==Use::Claimed)entries_[id].use=Use::Idle;
        staging_=Staging();
    }
    // Releases idle buffers (never active or claimed ones; nothing while a staging is open). Order: invalid keys
    // first (highest id first), then valid ones by oldest idle stamp. Stops once maxBytes were released, always
    // releasing at least one buffer when any is idle. drop(id) must free the D3D resources. Returns the bytes freed.
    template<class Drop>
    uint64_t trimIdle(uint64_t maxBytes,Drop&& drop){
        if(staging_.open)return 0;
        std::vector<uint32_t> order;
        for(uint32_t id=0;id<entries_.size();++id)if(entries_[id].use==Use::Idle&&!entries_[id].empty())order.push_back(id);
        std::sort(order.begin(),order.end(),[&](uint32_t a,uint32_t b){
            const Entry &x=entries_[a],&y=entries_[b];
            if(x.keyValid!=y.keyValid)return !x.keyValid;
            if(!x.keyValid)return a>b;
            return x.stamp!=y.stamp?x.stamp<y.stamp:a<b;
        });
        uint64_t freed=0;
        for(uint32_t id:order){
            const uint64_t bytes=entries_[id].capacity();
            if(freed&&freed+bytes>maxBytes)break;
            drop(id);capacity_.valid=false;entries_[id]=Entry();freed+=bytes;
        }
        while(!entries_.empty()&&entries_.back().use==Use::Idle&&entries_.back().empty())entries_.pop_back(); // ids of the rest stay
        return freed;
    }
};
}
