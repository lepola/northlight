#pragma once
#include "replay_gpu_cache.h"
#include <algorithm>
#include <type_traits>
#include <vector>

namespace NorthlightReplayGPU {
/* Resident single-stream replay meshes admitted in one frame share one exact-size
   batch: a DEFAULT|WRITEONLY vertex buffer and INDEX32 buffer (the pool/usage of
   the per-mesh buffers), created and written once (NorthlightUpload::FreshBufferLock),
   like the per-mesh creation. A draw then differs from its
   neighbours only in BaseVertexIndex and StartIndex, so the replay loops keep one
   stream/index binding per batch. Same bytes at the same (index+base)*stride
   addresses: the draws are unchanged. Admission limits (16 meshes, 4 MiB, time)
   are Cache's; admissions are reserved during the bind loop and committed as one
   batch right after it (commitAdmissions), or at once for the caller's bulk-growth
   guard (ignoreTimeBudget); leftovers commit at the next frame boundary.
   Eviction takes the least recently used entry's whole batch (its entries share an
   age), so it frees buffers rather than leaving holes; a batch below half live is
   returned to probation and re-admitted. Multi-stream meshes keep the per-mesh layout.
   false: NorthlightReplayGPU::Cache, one buffer per mesh stream (0.3.142). */
inline constexpr bool BatchedReplayCache=true;
struct Placement {UINT vertexBase=0,indexStart=0;};
class BatchedCache {
public:
    using Stats=Cache::Stats;using Population=Cache::Population;using ClearStats=Cache::ClearStats;
    struct BatchStats {size_t batches=0,batchBytes=0,separateBytes=0,liveBytes=0,compactedBytes=0;unsigned batchedUploads=0,separateUploads=0,compactions=0,batchFailures=0;};
private:
    struct Batch {
        IDirect3DVertexBuffer9* vertices=nullptr;IDirect3DIndexBuffer9* indices=nullptr;
        size_t capacity=0,liveBytes=0;unsigned live=0;
        ~Batch(){if(vertices)vertices->Release();if(indices)indices->Release();}
    };
    struct Entry {
        std::weak_ptr<const NorthlightDrawSnapshot::Mesh> owner;
        Batch* batch=nullptr;Placement at;bool pending=false; /* batched (or reserved for the next batch) */
        IDirect3DVertexBuffer9* vertices[4]={};IDirect3DIndexBuffer9* indices=nullptr; /* separate (per-mesh layout) */
        size_t bytes=0,reserved=0;uint64_t first=0,touched=0;
        const NorthlightDrawSnapshot::Mesh* key=nullptr;
        Entry* previous=nullptr;Entry* next=nullptr;
        ~Entry(){for(auto p:vertices)if(p)p->Release();if(indices)indices->Release();}
    };
    std::unordered_map<const NorthlightDrawSnapshot::Mesh*,std::unique_ptr<Entry>> entries;
    std::vector<std::unique_ptr<Batch>> batches_;std::vector<Entry*> pending_;IDirect3DDevice9* device_=nullptr;
    Entry* oldest_=nullptr;Entry* newest_=nullptr;
    size_t bytes_=0,live_=0,reserved_=0,limit_=Limit;uint64_t frame_=0; /* bytes_: committed (batch capacities + separate buffers) */
    size_t uploaded_=0,reused_=0;unsigned hits_=0,created_=0;size_t attemptedBytes_=0;unsigned attempts_=0;
    bool timeDeferredLast_=false,ignoreTimeBudget_=false;
    Stats stats_;ClearStats clearStats_;BatchStats batchStats_;
    static constexpr size_t EntryLimit=4096,Limit=64u*1024u*1024u,UploadLimit=4u*1024u*1024u;
    static bool batched(const NorthlightDrawSnapshot::Mesh& m){
        const auto& v=m.streams[0];
        return v.stride&&!v.bytes.empty()&&v.bytes.size()%v.stride==0&&m.streams[1].bytes.empty()&&m.streams[2].bytes.empty()&&m.streams[3].bytes.empty();
    }
    // Committed bytes of a batched mesh, including its worst-case stride alignment.
    static size_t reservation(const NorthlightDrawSnapshot::Mesh& m){return m.byteSize()+m.streams[0].stride;}
    void freeBatch(Batch* b){
        bytes_-=b->capacity;
        auto it=std::find_if(batches_.begin(),batches_.end(),[&](const std::unique_ptr<Batch>& q){return q.get()==b;});
        if(it!=batches_.end()){std::swap(*it,batches_.back());batches_.pop_back();}
    }
    // Returns an entry to probation: its storage (batch reference, reservation or buffers) is released.
    void release(Entry& e){
        if(e.pending){reserved_-=e.reserved;e.reserved=0;e.pending=false;pending_.erase(std::find(pending_.begin(),pending_.end(),&e));}
        if(!e.bytes)return;
        live_-=e.bytes;
        if(e.batch){Batch* b=e.batch;e.batch=nullptr;e.at={};--b->live;b->liveBytes-=e.bytes;if(!b->live)freeBatch(b);}
        else {bytes_-=e.bytes;for(auto& p:e.vertices)if(p){p->Release();p=nullptr;}if(e.indices){e.indices->Release();e.indices=nullptr;}}
        e.bytes=0;
    }
    void erase(Entry& e){const auto* key=e.key;release(e);unlink(e);entries.erase(key);}
    void unlink(Entry& e){
        if(e.previous)e.previous->next=e.next;else oldest_=e.next;
        if(e.next)e.next->previous=e.previous;else newest_=e.previous;
        e.previous=e.next=nullptr;
    }
    void append(Entry& e){
        e.previous=newest_;e.next=nullptr;
        if(newest_)newest_->next=&e;else oldest_=&e;
        newest_=&e;
    }
    void touch(Entry& e){
        if(e.touched==frame_)return;
        e.touched=frame_;
        if(newest_!=&e){unlink(e);append(e);++stats_.lruMoves;}
    }
    // The least recently used entry leaves with every entry of its batch not used this frame
    // (current: this frame's too, for the memory guard), so the batch's buffers are freed.
    void evict(Entry& e,bool current=false){
        Batch* batch=e.batch;const unsigned others=batch?batch->live-1:0;erase(e);++stats_.evictions;
        if(!others)return;
        std::vector<Entry*> victims;try{victims.reserve(others);}catch(...){return;} /* no allocation below: the trim stays nothrow */
        for(auto& item:entries){Entry& o=*item.second;if(o.batch==batch&&(current||o.touched!=frame_))victims.push_back(&o);if(victims.size()==others)break;}
        for(Entry* o:victims){erase(*o);++stats_.evictions;}
    }
    // As Cache::room, on committed plus reserved bytes.
    bool room(size_t bytes,bool promotion=false){
        const bool atCountLimit=entries.size()>=EntryLimit;
        const bool countPressure=!promotion&&atCountLimit;
        const bool bytePressure=bytes_+reserved_+bytes>limit_;
        stats_.countPressure+=countPressure;stats_.bytePressure+=bytePressure;
        stats_.promotionCountOnly+=promotion&&atCountLimit&&!bytePressure;
        while(bytes_+reserved_+bytes>limit_||(!promotion&&entries.size()>=EntryLimit)){
            ++stats_.evictionChecks;
            if(!oldest_||oldest_->touched==frame_){++stats_.roomRejected;stats_.promotionCountRejected+=promotion&&atCountLimit;return false;}
            evict(*oldest_);
        }return true;
    }
    template<class Buffer> static bool write(Buffer* b,UINT offset,const void* data,size_t bytes){
        void* out=nullptr;if(FAILED(b->Lock(offset,UINT(bytes),&out,NorthlightUpload::FreshBufferLock)))return false;
        if(out)std::memcpy(out,data,bytes);NorthlightLockMeter::staging(bytes);const HRESULT hr=b->Unlock();return out&&!FAILED(hr);
    }
    bool storeSeparate(IDirect3DDevice9* device,Entry& e,const NorthlightDrawSnapshot::Mesh& mesh){
        auto built=std::make_unique<Entry>();
        for(unsigned s=0;s<4;++s){const auto& data=mesh.streams[s].bytes;if(data.empty())continue;
            if(FAILED(device->CreateVertexBuffer(UINT(data.size()),D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&built->vertices[s],nullptr))||!built->vertices[s])return false;
            if(!write(built->vertices[s],0,data.data(),data.size()))return false;
        }
        if(!mesh.indices.empty()){
            const UINT count=UINT(mesh.indices.size()*4);
            if(FAILED(device->CreateIndexBuffer(count,D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&built->indices,nullptr))||!built->indices)return false;
            if(!write(built->indices,0,mesh.indices.data(),count))return false;
        }
        for(unsigned s=0;s<4;++s)std::swap(e.vertices[s],built->vertices[s]);
        std::swap(e.indices,built->indices);
        bytes_+=mesh.byteSize();++batchStats_.separateUploads;return true;
    }
    // The reserved admissions become one batch. Failure returns them to probation.
    void commit(){
        if(pending_.empty())return;
        std::vector<Entry*> members;std::vector<std::shared_ptr<const NorthlightDrawSnapshot::Mesh>> meshes;
        try{members.reserve(pending_.size());meshes.reserve(pending_.size());}catch(...){while(!pending_.empty())release(*pending_.back());return;}
        size_t vertexBytes=0,indexBytes=0;
        for(Entry* e:pending_){auto mesh=e->owner.lock();if(!mesh)continue;const UINT stride=mesh->streams[0].stride;
            e->at.vertexBase=UINT((vertexBytes+stride-1)/stride);vertexBytes=size_t(e->at.vertexBase)*stride+mesh->streams[0].bytes.size();
            e->at.indexStart=UINT(indexBytes/4);indexBytes+=mesh->indices.size()*4;members.push_back(e);meshes.push_back(std::move(mesh));}
        while(!pending_.empty())release(*pending_.back()); /* reservations end; placements stay in members */
        if(members.empty())return;
        struct Timer {double& total;std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
            ~Timer(){total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}} timer{stats_.createMs};
        auto batch=std::make_unique<Batch>();batch->capacity=vertexBytes+indexBytes;
        auto fail=[&]{++batchStats_.batchFailures;for(Entry* e:members)e->at={};};
        if(FAILED(device_->CreateVertexBuffer(UINT(vertexBytes),D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&batch->vertices,nullptr))||!batch->vertices||
           (indexBytes&&(FAILED(device_->CreateIndexBuffer(UINT(indexBytes),D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&batch->indices,nullptr))||!batch->indices))){fail();return;}
        {void* out=nullptr;if(FAILED(batch->vertices->Lock(0,UINT(vertexBytes),&out,NorthlightUpload::FreshBufferLock))){fail();return;}
         if(out)for(size_t i=0;i<members.size();++i){const auto& v=meshes[i]->streams[0];std::memcpy(static_cast<uint8_t*>(out)+size_t(members[i]->at.vertexBase)*v.stride,v.bytes.data(),v.bytes.size());}
         NorthlightLockMeter::staging(vertexBytes);const HRESULT hr=batch->vertices->Unlock();if(!out||FAILED(hr)){fail();return;}}
        if(indexBytes){void* out=nullptr;if(FAILED(batch->indices->Lock(0,UINT(indexBytes),&out,NorthlightUpload::FreshBufferLock))){fail();return;}
         if(out)for(size_t i=0;i<members.size();++i)if(!meshes[i]->indices.empty())std::memcpy(static_cast<uint8_t*>(out)+size_t(members[i]->at.indexStart)*4,meshes[i]->indices.data(),meshes[i]->indices.size()*4);
         NorthlightLockMeter::staging(indexBytes);const HRESULT hr=batch->indices->Unlock();if(!out||FAILED(hr)){fail();return;}}
        Batch* b=batch.get();batches_.push_back(std::move(batch));bytes_+=b->capacity;
        for(size_t i=0;i<members.size();++i){Entry& e=*members[i];const size_t bytes=meshes[i]->byteSize();
            e.batch=b;e.bytes=bytes;++b->live;b->liveBytes+=bytes;live_+=bytes;uploaded_+=bytes;++created_;++stats_.created;++batchStats_.batchedUploads;}
    }
    // Frame boundary, at most one batch per frame: when dead batch bytes exceed 1 MiB and a sixteenth
    // of the live bytes, the batch with the lowest live share (below half) returns its entries to
    // probation; they are re-admitted under the normal per-frame limits.
    void compact(){
        const size_t dead=bytes_-live_;
        if(dead<=(1u<<20)||dead<=live_/16)return;
        Batch* victim=nullptr;
        for(const auto& b:batches_)if(!victim||uint64_t(b->liveBytes)*victim->capacity<uint64_t(victim->liveBytes)*b->capacity)victim=b.get();
        if(!victim||victim->liveBytes*2>victim->capacity)return;
        ++batchStats_.compactions;
        unsigned remaining=victim->live; /* the victim is released with its last entry */
        for(auto it=entries.begin();remaining&&it!=entries.end();++it){Entry& e=*it->second;
            if(e.batch==victim){--remaining;batchStats_.compactedBytes+=e.bytes;release(e);}}
    }
public:
    BatchedCache()=default;
    BatchedCache(const BatchedCache&)=delete;BatchedCache& operator=(const BatchedCache&)=delete;
    ~BatchedCache(){clear();}
    void clear(){++clearStats_.calls;clearStats_.entries+=entries.size();clearStats_.bytes+=bytes_;oldest_=newest_=nullptr;pending_.clear();entries.clear();batches_.clear();device_=nullptr;bytes_=live_=reserved_=0;}
    void beginFrame(){
        ++frame_;uploaded_=reused_=0;hits_=created_=attempts_=0;attemptedBytes_=0;stats_={};
        for(auto it=entries.begin();it!=entries.end();){
            Entry& e=*it->second;
            if(e.owner.expired()){++stats_.expiredEntries;stats_.expiredBytes+=e.bytes;release(e);unlink(e);it=entries.erase(it);}else ++it;
        }
        commit();compact();
    }
    // Memory guard, frame boundary only: whole batches of the least recently used entries, this
    // frame's too (callers hold their own references), until committed and reserved bytes fit.
    void trimTo(size_t bytes){while(bytes_+reserved_>bytes&&oldest_)evict(*oldest_,true);}
    void setLimit(size_t bytes){limit_=std::min(bytes,Limit);trimTo(limit_);}
    size_t limit()const{return limit_;}
    // After the bind loop: this frame's reservations become resident (true if any did).
    bool commitPending(){const size_t before=live_;commit();return live_!=before;}
    bool resident(const NorthlightDrawSnapshot::Mesh* mesh)const{auto it=entries.find(mesh);return it!=entries.end()&&it->second->bytes;}
    size_t bytes()const{return bytes_;}size_t uploaded()const{return uploaded_;}
    size_t reused()const{return reused_;}unsigned hits()const{return hits_;}
    static constexpr size_t entryLimit(){return EntryLimit;}
    bool timeDeferredLast()const{return timeDeferredLast_;}
    void ignoreTimeBudget(bool value){ignoreTimeBudget_=value;}
    const Stats& stats()const{return stats_;}
    const ClearStats& clearStats()const{return clearStats_;}
    BatchStats batchStats()const{BatchStats s=batchStats_;s.batches=batches_.size();for(const auto& b:batches_)s.batchBytes+=b->capacity;s.separateBytes=bytes_-s.batchBytes;s.liveBytes=live_;return s;}
    Population population()const{Population p;p.entries=entries.size();for(const auto& item:entries)p.resident+=item.second->bytes!=0;p.probation=p.entries-p.resident;return p;}
    // Cache::bind's policy (warm-up, 16 attempts, 4 MiB, time budget, admission, LRU room) verbatim;
    // a batched admission is a reservation committed at the next beginFrame. at: BaseVertexIndex/StartIndex.
    template<class Admission> bool bind(IDirect3DDevice9* device,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& mesh,
        IDirect3DVertexBuffer9* (&vb)[4],IDirect3DIndexBuffer9*& ib,Admission admit,Placement& at){
        timeDeferredLast_=false;at={};device_=device;
        if(!mesh||!mesh->byteSize()||mesh->byteSize()>limit_)return false;
        try{
            auto it=entries.find(mesh.get());
            if(it!=entries.end()&&it->second->owner.lock()!=mesh){erase(*it->second);it=entries.end();}
            if(it==entries.end()){
                if(!room(0))return false;
                auto e=std::make_unique<Entry>();e->owner=mesh;e->key=mesh.get();e->first=e->touched=frame_;
                auto inserted=entries.emplace(mesh.get(),std::move(e));
                if(inserted.second)append(*inserted.first->second);
                ++stats_.warmup;return false;
            }
            Entry& e=*it->second;touch(e);
            if(e.pending)return false; /* committed at the next frame boundary */
            if(!e.bytes){
                size_t bytes=mesh->byteSize();
                if(e.first==frame_){++stats_.warmup;return false;}
                if(attempts_>=16){++stats_.uploadDeferred;++stats_.attemptDeferred;return false;}
                if(bytes>UploadLimit-attemptedBytes_){++stats_.uploadDeferred;++stats_.uploadByteDeferred;return false;}
                if(CreateTimeBudget&&!ignoreTimeBudget_&&attempts_&&stats_.createMs>=createBudgetMs()){++stats_.uploadDeferred;++stats_.timeDeferred;timeDeferredLast_=true;return false;}
                if(!admit(bytes)){++stats_.admissionRejected;return false;}
                const bool batch=batched(*mesh);const size_t committed=batch?reservation(*mesh):bytes;
                if(!room(committed,true))return false;
                ++attempts_;attemptedBytes_+=bytes;
                if(batch){pending_.push_back(&e);e.pending=true;e.reserved=committed;reserved_+=committed;
                    // The caller's bulk-growth guard (0.3.138) needs residency now: commit this frame's batch at once.
                    if(!ignoreTimeBudget_)return false;
                    commit();if(!e.bytes)return false;}
                else {struct Timer {double& total;std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
                        ~Timer(){total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}} timer{stats_.createMs};
                    if(!storeSeparate(device,e,*mesh))return false;
                    e.bytes=bytes;live_+=bytes;uploaded_+=bytes;++created_;++stats_.created;}
            }else{reused_+=e.bytes;++hits_;}
            const Entry& ready=*it->second;
            IDirect3DVertexBuffer9* streams[4]={ready.batch?ready.batch->vertices:ready.vertices[0],ready.vertices[1],ready.vertices[2],ready.vertices[3]};
            // 0.3.176 (U3'): a binding already held keeps its reference (no Release/AddRef pair).
            for(unsigned s=0;s<4;++s)if(vb[s]!=streams[s]){if(vb[s])vb[s]->Release();vb[s]=streams[s];if(vb[s])vb[s]->AddRef();}
            IDirect3DIndexBuffer9* const index=ready.batch?(mesh->indices.empty()?nullptr:ready.batch->indices):ready.indices;
            if(ib!=index){if(ib)ib->Release();ib=index;if(ib)ib->AddRef();}
            at=ready.at;return true;
        }catch(...){return false;}
    }
};
using SelectedCache=std::conditional_t<BatchedReplayCache,BatchedCache,Cache>;
inline BatchedCache::BatchStats batchStats(const BatchedCache& c){return c.batchStats();}
inline BatchedCache::BatchStats batchStats(const Cache&){return {};}
// The replay's draw bindings for one cache lookup (uploadReplay). A miss leaves the
// bulk layout to the caller; its draws use BaseVertexIndex 0 as before.
// After uploadReplay's bind loop: commit the frame's batch and bind the replays it made resident.
template<class Replays,class Bind> void commitAdmissions(Cache&,Replays&,Bind){}
template<class Replays,class Bind> void commitAdmissions(BatchedCache& c,Replays& replays,Bind bind){
    if(!c.commitPending())return;
    for(auto& p:replays)if(!p->gpuCached&&c.resident(p->shared.get()))bind(*p);
}
template<class Replay,class Admission> bool bindResident(Cache& c,IDirect3DDevice9* d,Replay& p,bool allowCache,Admission admit){
    p.gpuCached=allowCache&&c.bind(d,p.shared,p.stream,p.index,admit);
    if(p.gpuCached){for(unsigned s=0;s<4;++s){p.offset[s]=0;p.stride[s]=p.mesh().streams[s].stride;}p.start=0;}
    p.base=0;return p.gpuCached;
}
template<class Replay,class Admission> bool bindResident(BatchedCache& c,IDirect3DDevice9* d,Replay& p,bool allowCache,Admission admit){
    Placement at;p.gpuCached=allowCache&&c.bind(d,p.shared,p.stream,p.index,admit,at);
    if(p.gpuCached){for(unsigned s=0;s<4;++s){p.offset[s]=0;p.stride[s]=p.mesh().streams[s].stride;}
        p.start=p.indexed?at.indexStart:at.vertexBase;p.base=p.indexed?INT(at.vertexBase):0;}
    else p.base=0;
    return p.gpuCached;
}
}
