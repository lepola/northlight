#pragma once
// Immutable terrain captures share one persistent vertex arena. Active index
// lists stay compact so directional terrain still costs exactly one draw.
// Retired vertex ranges are reused only after a non-flushing event query says
// their previous GPU readers have finished. Unsupported/busy queries never wait:
// a bounded DISCARD rollover is the correctness-preserving escape hatch.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>
#include "lock_meter.h"

namespace NorthlightLiveTerrainGPU {
struct Range {uint32_t first=0,count=0;};
class FreeRanges {
    std::vector<Range> ranges_;
    uint32_t available_=0;
public:
    uint32_t available()const{return available_;}
    void reset(uint32_t capacity){ranges_.clear();available_=capacity;if(capacity)ranges_.push_back({0,capacity});}
    void release(Range r){
        if(!r.count)return;
        available_+=r.count;
        auto it=std::lower_bound(ranges_.begin(),ranges_.end(),r.first,[](Range a,uint32_t b){return a.first<b;});
        it=ranges_.insert(it,r);
        if(it!=ranges_.begin()&&(it-1)->first+(it-1)->count==it->first){(it-1)->count+=it->count;it=ranges_.erase(it)-1;}
        if(it+1!=ranges_.end()&&it->first+it->count==(it+1)->first){it->count+=(it+1)->count;ranges_.erase(it+1);}
    }
    bool take(uint32_t count,Range& out){
        for(auto it=ranges_.begin();it!=ranges_.end();++it)if(it->count>=count){out={it->first,count};it->first+=count;it->count-=count;available_-=count;if(!it->count)ranges_.erase(it);return true;}
        return false;
    }
};

template<class Snapshot,class Vertex> class Cache {
    using Owner=std::shared_ptr<const Snapshot>;
    struct Entry {
        std::weak_ptr<const Snapshot> owner;Range range;uint64_t touched=0,directionalGeneration=UINT64_MAX;
        std::vector<uint32_t> indices,directional;
    };
    struct Retired {
        IDirect3DQuery9* query=nullptr;std::vector<Range> ranges;
        ~Retired(){if(query)query->Release();}
    };
    IDirect3DVertexBuffer9* vertices_=nullptr;
    uint32_t capacity_=0,limit_;uint64_t frame_=0;
    FreeRanges free_;
    std::unordered_map<const Snapshot*,Entry> entries_;
    std::vector<std::unique_ptr<Retired>> retired_;
    size_t indexBytes_=0;
    bool discard_=false,fresh_=false; /* 0.3.192 (DXVK3): fresh_ = the buffer was just created: its first Lock is NOOVERWRITE (nothing in flight, no 16 MiB DISCARD charge); rollover and failure resets keep DISCARD */
    static constexpr size_t IndexLimit=32u*1024u*1024u;
    // 0.3.176 (U1b): update() records the entry of each active owner (after its weak_ptr check; null:
    // none), for the owner list and frame it saw; prepare() uses it. Entries are map nodes, so the
    // pointers survive inserts; resetContents() and retirement drop only entries not recorded.
    std::vector<Entry*> active_;const void* activeSource_=nullptr;uint64_t activeFrame_=UINT64_MAX;
    std::vector<const Entry*> prepared_; /* 0.3.176 (U1a): prepare()'s entries, in order, for write() */
    void resetContents(){entries_.clear();retired_.clear();indexBytes_=0;free_.reset(capacity_);discard_=true;fresh_=false;std::fill(active_.begin(),active_.end(),nullptr);prepared_.clear();}
    void collect(IDirect3DDevice9* d){
        for(auto it=retired_.begin();it!=retired_.end();){
            // flags=0: never D3DGETDATA_FLUSH, never a blocking poll loop.
            if((*it)->query&&(*it)->query->GetData(nullptr,0,0)==S_OK){for(auto r:(*it)->ranges)free_.release(r);it=retired_.erase(it);}else ++it;
        }
        auto retired=std::make_unique<Retired>();
        // Start reclamation before exhaustion, leaving a quarter-arena runway
        // for new captures while the nonblocking completion query is pending.
        const bool pressure=free_.available()<capacity_/4;collectVisited+=entries_.size();
        for(auto it=entries_.begin();it!=entries_.end();){
            auto& e=it->second;
            if(e.touched!=frame_&&(pressure||e.owner.expired()||frame_-e.touched>90||indexBytes_>IndexLimit||entries_.size()>4096)){
                retired->ranges.push_back(e.range);indexBytes_-=(e.indices.capacity()+e.directional.capacity())*sizeof(uint32_t);it=entries_.erase(it);
            }else ++it;
        }
        if(retired->ranges.empty())return;
        if(SUCCEEDED(d->CreateQuery(D3DQUERYTYPE_EVENT,&retired->query))&&retired->query){
            if(FAILED(retired->query->Issue(D3DISSUE_END))){retired->query->Release();retired->query=nullptr;}
        }
        // Without event support these ranges stay retired until DISCARD. Cap
        // query bookkeeping too; safely abandon reclamation for old ranges.
        if(retired_.size()>=32)retired_.erase(retired_.begin());
        retired_.push_back(std::move(retired));
    }
public:
    size_t uploadedBytes=0,reusedVertices=0;unsigned rollovers=0;
    // 0.3.176 (D1) this frame: owners inserted, directional lists rebuilt (and their triangles), entries collect() visited.
    unsigned newOwners=0,directionalBuilds=0;size_t directionalTriangles=0,collectVisited=0;
    explicit Cache(uint32_t limitBytes=16u*1024u*1024u):limit_(limitBytes/sizeof(Vertex)){}
    ~Cache(){clear();}
    Cache(const Cache&)=delete;Cache& operator=(const Cache&)=delete;
    void clear(){entries_.clear();retired_.clear();indexBytes_=0;free_.reset(0);capacity_=0;frame_=0;discard_=false;fresh_=false;if(vertices_)vertices_->Release();vertices_=nullptr;
        active_.clear();activeSource_=nullptr;activeFrame_=UINT64_MAX;prepared_.clear();}
    IDirect3DVertexBuffer9* vertices()const{return vertices_;}
    uint32_t vertexCapacity()const{return capacity_;}
    size_t bytes()const{return size_t(capacity_)*sizeof(Vertex);}
    void beginFrame(){++frame_;uploadedBytes=0;reusedVertices=0;newOwners=0;directionalBuilds=0;directionalTriangles=0;collectVisited=0;}

    template<class Admission,class Convert> HRESULT update(IDirect3DDevice9* d,const std::vector<Owner>& active,Admission admit,Convert convert){
        activeSource_=nullptr;try{active_.assign(active.size(),nullptr);activeSource_=&active;activeFrame_=frame_;}catch(...){active_.clear();} /* unrecorded: prepare() looks up */
        const bool record=activeSource_!=nullptr;
        try{
            uint64_t required=0;
            for(size_t k=0;k<active.size();++k){const auto& owner=active[k];
                required+=owner->positions.size();
                auto it=entries_.find(owner.get());
                if(it!=entries_.end()&&it->second.owner.lock()==owner){it->second.touched=frame_;if(record)active_[k]=&it->second;}
            }
            if(required>limit_)return E_OUTOFMEMORY;
            if(!required){collect(d);return S_OK;}
            if(!vertices_){
                // A tighter admission may accept the original 8 MiB maximum
                // active set even when the 16 MiB retention arena is too large.
                uint32_t capacity=limit_;
                if(!admit(size_t(capacity)*sizeof(Vertex))){capacity=std::max(uint32_t(required),limit_/2);if(!admit(size_t(capacity)*sizeof(Vertex)))return S_FALSE;}
                HRESULT hr=d->CreateVertexBuffer(UINT(size_t(capacity)*sizeof(Vertex)),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&vertices_,nullptr);
                if(FAILED(hr)||!vertices_){if(vertices_)vertices_->Release();vertices_=nullptr;return FAILED(hr)?hr:E_FAIL;}
                capacity_=capacity;resetContents();fresh_=true;
            }
            // A pressure-sized arena always accommodates the renderer's active
            // cap. Tests may choose a smaller arena, so reject impossible sets.
            if(required>capacity_)return E_OUTOFMEMORY;
            collect(d);
            struct Pending {Owner owner;Entry* entry;};
            std::vector<Pending> pending;
            for(unsigned attempt=0;attempt<2;++attempt){
                bool full=false;pending.clear();
                for(size_t k=0;k<active.size();++k){const auto& owner=active[k];
                    if(record&&active_[k]){reusedVertices+=owner->positions.size();continue;} /* found and checked above */
                    auto it=entries_.find(owner.get());
                    if(it!=entries_.end()&&it->second.owner.lock()==owner){reusedVertices+=owner->positions.size();if(record)active_[k]=&it->second;continue;} /* a repeated owner */
                    // Expired address reuse is retired by collect(), never an
                    // identity hit; retain no strong owners between frames.
                    Range range;
                    if(!free_.take(uint32_t(owner->positions.size()),range)){full=true;break;}
                    Entry e;e.owner=owner;e.range=range;e.touched=frame_;e.indices.reserve(owner->indices.size());
                    for(auto i:owner->indices)e.indices.push_back(i+range.first);
                    indexBytes_+=e.indices.capacity()*sizeof(uint32_t);
                    auto inserted=entries_.emplace(owner.get(),std::move(e));++newOwners;
                    pending.push_back({owner,&inserted.first->second});if(record)active_[k]=&inserted.first->second;
                }
                if(!full)break;
                // DISCARD renames the complete arena; all surviving snapshots
                // are then uploaded into the new allocation in this frame.
                resetContents();reusedVertices=0;++rollovers;
                if(attempt)return E_OUTOFMEMORY;
            }
            std::sort(pending.begin(),pending.end(),[](const Pending& a,const Pending& b){return a.entry->range.first<b.entry->range.first;});
            for(size_t first=0;first<pending.size();){
                size_t end=first+1;uint32_t count=pending[first].entry->range.count;
                while(end<pending.size()&&pending[first].entry->range.first+count==pending[end].entry->range.first){count+=pending[end].entry->range.count;++end;}
                const UINT offset=UINT(size_t(pending[first].entry->range.first)*sizeof(Vertex));const UINT bytes=UINT(size_t(count)*sizeof(Vertex));
                void* target=nullptr;const bool discarding=discard_&&!fresh_;HRESULT hr=vertices_->Lock(offset,bytes,&target,discarding?D3DLOCK_DISCARD:D3DLOCK_NOOVERWRITE);
                if(discarding)NorthlightLockMeter::discard(NorthlightLockMeter::Arena,std::uint64_t(capacity_)*sizeof(Vertex));
                if(FAILED(hr)){resetContents();return hr;}
                if(!target){vertices_->Unlock();resetContents();return E_FAIL;}
                Vertex* output=static_cast<Vertex*>(target);
                for(size_t i=first;i<end;++i)for(const auto& position:pending[i].owner->positions)*output++=convert(position);
                hr=vertices_->Unlock();if(FAILED(hr)){resetContents();return hr;}
                discard_=false;fresh_=false;uploadedBytes+=bytes;first=end;
            }
            return S_OK;
        }catch(...){resetContents();activeSource_=nullptr;return E_OUTOFMEMORY;}
    }
    // 0.3.176 (U1a): indices() without the concatenation. Rebuilds stale directional lists exactly as
    // indices() does and returns the two totals; write() then copies all point lists in `active` order,
    // then all directional lists, into the caller's (locked) buffer: the bytes indices() would give.
    // false: an owner has no entry (the counts are the lists before it, as indices() leaves them), or an
    // allocation failed (contents reset, counts 0).
    template<class Directional> bool prepare(const std::vector<Owner>& active,uint64_t generation,Directional build,size_t& pointCount,size_t& directionalCount){
        pointCount=directionalCount=0;prepared_.clear();
        const bool recorded=activeSource_==&active&&activeFrame_==frame_&&active_.size()==active.size();
        try{
        prepared_.reserve(active.size());
        for(size_t k=0;k<active.size();++k){const auto& owner=active[k];
            Entry* entry=nullptr;
            if(recorded)entry=active_[k];
            else{auto it=entries_.find(owner.get());if(it!=entries_.end()&&it->second.owner.lock()==owner)entry=&it->second;}
            if(!entry){prepared_.clear();return false;}
            auto& e=*entry;
            if(e.directionalGeneration!=generation){indexBytes_-=e.directional.capacity()*sizeof(uint32_t);e.directional.clear();build(*owner,e.range.first,e.directional);indexBytes_+=e.directional.capacity()*sizeof(uint32_t);e.directionalGeneration=generation;
                ++directionalBuilds;directionalTriangles+=e.directional.size()/3;}
            pointCount+=e.indices.size();directionalCount+=e.directional.size();prepared_.push_back(&e);
        }
        return true;
        }catch(...){resetContents();pointCount=directionalCount=0;return false;}
    }
    // After a successful prepare() and before any other call: pointCount+directionalCount indices.
    void write(uint32_t* out)const noexcept{
        for(const Entry* e:prepared_){if(!e->indices.empty())std::memcpy(out,e->indices.data(),e->indices.size()*sizeof(uint32_t));out+=e->indices.size();}
        for(const Entry* e:prepared_){if(!e->directional.empty())std::memcpy(out,e->directional.data(),e->directional.size()*sizeof(uint32_t));out+=e->directional.size();}
    }
    // The 0.3.175 path (tests compare prepare()/write() with it).
    template<class Directional> bool indices(const std::vector<Owner>& active,uint64_t generation,Directional build,
                                             std::vector<uint32_t>& point,std::vector<uint32_t>& directional){
        point.clear();directional.clear();
        try{
        for(const auto& owner:active){
            auto it=entries_.find(owner.get());if(it==entries_.end()||it->second.owner.lock()!=owner)return false;
            auto& e=it->second;
            if(e.directionalGeneration!=generation){indexBytes_-=e.directional.capacity()*sizeof(uint32_t);e.directional.clear();build(*owner,e.range.first,e.directional);indexBytes_+=e.directional.capacity()*sizeof(uint32_t);e.directionalGeneration=generation;
                ++directionalBuilds;directionalTriangles+=e.directional.size()/3;}
            point.insert(point.end(),e.indices.begin(),e.indices.end());directional.insert(directional.end(),e.directional.begin(),e.directional.end());
        }
        return true;
        }catch(...){resetContents();point.clear();directional.clear();return false;}
    }
};
}
