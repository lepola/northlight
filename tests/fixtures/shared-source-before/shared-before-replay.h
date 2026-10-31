#pragma once
#include "draw_snapshot.h"
#include "upload_lock.h"
#include "lock_meter.h"

#include <chrono>

namespace NorthlightReplayGPU {
/* 0.3.138: resident-cache creation (Create*Buffer + Lock + copy) is also
   bounded by render-thread time per frame. A deferred mesh is drawn from the
   per-frame bulk buffers with the same bytes, exactly like the existing
   16-attempt/4 MiB deferrals, so shadows never wait on residency. The first
   attempt in a frame is always allowed (progress). */
inline constexpr bool CreateTimeBudget=true;
inline constexpr double CreateBudgetMs=1.0;
/* Process-wide knob so exact-count limit tests can disable the time bound. */
inline double& createBudgetMs(){static double value=CreateBudgetMs;return value;}
// Immutable CPU snapshots identify GPU contents, never an original mutable VB.
// Weak owners prevent the GPU cache from keeping evicted CPU scenes alive.
class Cache {
public:
    struct Stats {
        unsigned countPressure=0,bytePressure=0,roomRejected=0,promotionCountOnly=0,promotionCountRejected=0,evictions=0;
        unsigned warmup=0,uploadDeferred=0,admissionRejected=0;
        // Retained as zero for comparisons with pre-LRU diagnostic logs.
        unsigned scanPasses=0,scanVisits=0,scanMemoHits=0;
        unsigned evictionChecks=0,lruMoves=0;
        // Exclusive reasons: the original attempt-limit check takes priority.
        unsigned attemptDeferred=0,uploadByteDeferred=0,expiredEntries=0;
        unsigned timeDeferred=0,created=0;double createMs=0; /* 0.3.138, per frame */
        size_t expiredBytes=0;
    };
    struct Population {size_t entries=0,resident=0,probation=0;};
    // Lifetime totals survive beginFrame, including clear-before-beginFrame in
    // the caller's recursive bulk-upload fallback. Caller supplies the reason.
    struct ClearStats {uint64_t calls=0,entries=0,bytes=0;};
private:
    struct Entry {
        std::weak_ptr<const NorthlightDrawSnapshot::Mesh> owner;
        IDirect3DVertexBuffer9* vertices[4]={};IDirect3DIndexBuffer9* indices=nullptr;
        size_t bytes=0;uint64_t first=0,touched=0;
        const NorthlightDrawSnapshot::Mesh* key=nullptr;
        Entry* previous=nullptr;Entry* next=nullptr;
        ~Entry(){for(auto p:vertices)if(p)p->Release();if(indices)indices->Release();}
    };
    std::unordered_map<const NorthlightDrawSnapshot::Mesh*,std::unique_ptr<Entry>> entries;
    Entry* oldest_=nullptr;Entry* newest_=nullptr;
    size_t bytes_=0,limit_=Limit;uint64_t frame_=0; /* limit_: lowered under address-space pressure */
    size_t uploaded_=0,reused_=0;unsigned hits_=0,created_=0;size_t attemptedBytes_=0;unsigned attempts_=0;
    bool timeDeferredLast_=false,ignoreTimeBudget_=false; /* 0.3.138 */
    Stats stats_;
    ClearStats clearStats_;
    static constexpr size_t EntryLimit=4096,Limit=64u*1024u*1024u,UploadLimit=4u*1024u*1024u;
    // Stable Entry allocations form a non-owning queue ordered by last frame
    // touched. Equal-frame ties use first-touch order, not hash iteration order.
    // No allocation, hash scan or clock call is needed to maintain this queue.
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
    bool room(size_t bytes,bool promotion=false){
        const bool atCountLimit=entries.size()>=EntryLimit;
        // Promotion replaces an existing probation record and needs no slot.
        const bool countPressure=!promotion&&atCountLimit;
        const bool bytePressure=bytes_+bytes>limit_;
        stats_.countPressure+=countPressure;stats_.bytePressure+=bytePressure;
        stats_.promotionCountOnly+=promotion&&atCountLimit&&!bytePressure;
        while(bytes_+bytes>limit_||(!promotion&&entries.size()>=EntryLimit)){
            ++stats_.evictionChecks;
            // The head is the least recently touched entry. If it is current,
            // every entry is current: never evict this frame's used geometry.
            if(!oldest_||oldest_->touched==frame_){++stats_.roomRejected;stats_.promotionCountRejected+=promotion&&atCountLimit;return false;}
            Entry* victim=oldest_;const auto* key=victim->key;
            ++stats_.evictions;
            bytes_-=victim->bytes;unlink(*victim);entries.erase(key);
        }return true;
    }
public:
    Cache()=default;
    Cache(const Cache&)=delete;Cache& operator=(const Cache&)=delete;
    Cache(Cache&&)=delete;Cache& operator=(Cache&&)=delete;
    void clear(){++clearStats_.calls;clearStats_.entries+=entries.size();clearStats_.bytes+=bytes_;oldest_=newest_=nullptr;entries.clear();bytes_=0;}
    void beginFrame(){
        ++frame_;uploaded_=reused_=0;hits_=created_=attempts_=0;attemptedBytes_=0;stats_={};
        for(auto it=entries.begin();it!=entries.end();){
            if(it->second->owner.expired()){++stats_.expiredEntries;stats_.expiredBytes+=it->second->bytes;bytes_-=it->second->bytes;unlink(*it->second);it=entries.erase(it);}else ++it;
        }
    }
    // Memory guard, frame boundary only: evicts least recently used entries,
    // including this frame's (callers own their own references), until bytes()<=bytes.
    void trimTo(size_t bytes){while(bytes_>bytes&&oldest_){Entry* victim=oldest_;const auto* key=victim->key;++stats_.evictions;bytes_-=victim->bytes;unlink(*victim);entries.erase(key);}}
    void setLimit(size_t bytes){limit_=std::min(bytes,Limit);trimTo(limit_);}
    size_t limit()const{return limit_;}
    size_t bytes()const{return bytes_;}size_t uploaded()const{return uploaded_;}
    size_t reused()const{return reused_;}unsigned hits()const{return hits_;}
    static constexpr size_t entryLimit(){return EntryLimit;}
    /* The last bind() returned false only because of the creation time budget. */
    bool timeDeferredLast()const{return timeDeferredLast_;}
    /* Caller re-binds time-deferred meshes with the budget ignored when bulk
       would otherwise grow; every other limit still applies (0.3.137 set). */
    void ignoreTimeBudget(bool value){ignoreTimeBudget_=value;}
    const Stats& stats()const{return stats_;}
    const ClearStats& clearStats()const{return clearStats_;}
    Population population()const{Population p;p.entries=entries.size();for(const auto& item:entries)p.resident+=item.second->bytes!=0;p.probation=p.entries-p.resident;return p;}
    // Allocation/driver failures are optional-cache misses. Caller retains its
    // original bulk upload path and all casters. Partial allocations are freed.
    template<class Admission> bool bind(IDirect3DDevice9* device,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& mesh,
        IDirect3DVertexBuffer9* (&vb)[4],IDirect3DIndexBuffer9*& ib,Admission admit){
        timeDeferredLast_=false;
        if(!mesh||!mesh->byteSize()||mesh->byteSize()>limit_)return false;
        try{
            auto it=entries.find(mesh.get());
            if(it!=entries.end()&&it->second->owner.lock()!=mesh){bytes_-=it->second->bytes;unlink(*it->second);entries.erase(it);it=entries.end();}
            if(it==entries.end()){
                if(!room(0))return false;
                auto e=std::make_unique<Entry>();e->owner=mesh;e->key=mesh.get();e->first=e->touched=frame_;
                // Publish ownership before linking; a failed allocation leaves
                // no dangling node in the queue.
                auto inserted=entries.emplace(mesh.get(),std::move(e));
                if(inserted.second)append(*inserted.first->second);
                ++stats_.warmup;return false; // prove cross-frame reuse before allocating
            }
            Entry& e=*it->second;touch(e);
            if(!e.bytes){
                size_t bytes=mesh->byteSize();
                if(e.first==frame_){++stats_.warmup;return false;}
                if(attempts_>=16){++stats_.uploadDeferred;++stats_.attemptDeferred;return false;}
                if(bytes>UploadLimit-attemptedBytes_){++stats_.uploadDeferred;++stats_.uploadByteDeferred;return false;}
                if(CreateTimeBudget&&!ignoreTimeBudget_&&attempts_&&stats_.createMs>=createBudgetMs()){++stats_.uploadDeferred;++stats_.timeDeferred;timeDeferredLast_=true;return false;}
                if(!admit(bytes)){++stats_.admissionRejected;return false;}
                if(!room(bytes,true))return false;
                ++attempts_;attemptedBytes_+=bytes;
                struct Timer {double& total;std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
                    ~Timer(){total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}} timer{stats_.createMs};
                auto built=std::make_unique<Entry>();built->owner=mesh;built->first=e.first;built->touched=frame_;
                for(unsigned s=0;s<4;++s){const auto& data=mesh->streams[s].bytes;if(data.empty())continue;
                    if(FAILED(device->CreateVertexBuffer(UINT(data.size()),D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&built->vertices[s],nullptr))||!built->vertices[s])return false;
                    void* out=nullptr;if(FAILED(built->vertices[s]->Lock(0,UINT(data.size()),&out,NorthlightUpload::FreshBufferLock)))return false;
                    if(out)std::memcpy(out,data.data(),data.size());NorthlightLockMeter::staging(data.size());HRESULT hr=built->vertices[s]->Unlock();if(!out||FAILED(hr))return false;
                }
                if(!mesh->indices.empty()){
                    const UINT count=UINT(mesh->indices.size()*4);
                    if(FAILED(device->CreateIndexBuffer(count,D3DUSAGE_WRITEONLY,D3DFMT_INDEX32,D3DPOOL_DEFAULT,&built->indices,nullptr))||!built->indices)return false;
                    void* out=nullptr;if(FAILED(built->indices->Lock(0,count,&out,NorthlightUpload::FreshBufferLock)))return false;
                    if(out)std::memcpy(out,mesh->indices.data(),count);NorthlightLockMeter::staging(count);HRESULT hr=built->indices->Unlock();if(!out||FAILED(hr))return false;
                }
                // Keep the linked node stable. Publish only after every GPU
                // buffer succeeds; the temporary owns partial-failure cleanup.
                for(unsigned s=0;s<4;++s)std::swap(e.vertices[s],built->vertices[s]);
                std::swap(e.indices,built->indices);
                e.bytes=bytes;bytes_+=bytes;uploaded_+=bytes;++created_;++stats_.created;
            }else{reused_+=e.bytes;++hits_;}
            const Entry& ready=*it->second;
            // 0.3.176 (U3'): a binding already held keeps its reference (no Release/AddRef pair).
            for(unsigned s=0;s<4;++s)if(vb[s]!=ready.vertices[s]){if(vb[s])vb[s]->Release();vb[s]=ready.vertices[s];if(vb[s])vb[s]->AddRef();}
            if(ib!=ready.indices){if(ib)ib->Release();ib=ready.indices;if(ib)ib->AddRef();}return true;
        }catch(...){return false;}
    }
};
}
