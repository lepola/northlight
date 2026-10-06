#pragma once
#include <d3d9.h>
#include "upload_lock.h"
#include "lock_meter_readback.h"
#include <algorithm>
#include <array>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include "intrusive_lru.h"
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>
#include "capture_buffer_metadata.h"
#include "northlight_mem.h"
// Test-only counterfactuals of the 0.3.181 snapshot lookup (tests/test_snapshot_prediction.py); 0 in the
// DLL. 1-4: no prediction reset in evictOldest / the revalidation erase / the same-hash replacement /
// clearSnapshotCache; 5: prediction accepted on the key bytes before the draw arguments; 6: prediction
// also on untracked keys, without revalidation; 7: key equality that skips the declaration.
#ifndef NORTHLIGHT_SNAPSHOT_COUNTERFACTUAL
#define NORTHLIGHT_SNAPSHOT_COUNTERFACTUAL 0
#endif
// Test-only: mask hashKey() to this many bits so same-hash replacements and collisions happen (0: off).
#ifndef NORTHLIGHT_SNAPSHOT_HASH_BITS
#define NORTHLIGHT_SNAPSHOT_HASH_BITS 0
#endif

// Immediate copies, never retained source-buffer identities. DISCARD, NOOVERWRITE,
// and ordinary writes after capture cannot change a replay. Original shader inputs
// (including blend indices/weights) and topology are preserved bit for bit.
namespace NorthlightDrawSnapshot {
struct Stream { UINT stride=0; std::vector<std::uint8_t> bytes; };
struct Mesh {
    Stream streams[4]; std::vector<std::uint32_t> indices;
    UINT vertexCount=0,primitiveCount=0; D3DPRIMITIVETYPE topology=D3DPT_TRIANGLELIST;
    // Replay indexed with base=0,min=0,start=0,INDEX32. Nonindexed start=0.
    bool indexed=false,dynamic=false;
    std::size_t byteSize() const {std::size_t n=indices.size()*4;for(auto& s:streams)n+=s.bytes.size();return n;}
    std::size_t capacityBytes()const{std::size_t n=indices.capacity()*4;for(auto& s:streams)n+=s.bytes.capacity();return n;}
    // Only metadata is reset. Every observed byte/index is overwritten below;
    // retaining vector sizes avoids zero-filling the complete mesh each frame.
    void prepare(){vertexCount=primitiveCount=0;indexed=dynamic=false;topology=D3DPT_TRIANGLELIST;for(auto& s:streams)s.stride=0;}
};
struct Draw { D3DPRIMITIVETYPE topology=D3DPT_TRIANGLELIST; INT base=0; UINT minimum=0,vertices=0,start=0,primitives=0; bool indexed=true; };
enum class Error { None, Arguments, Declaration, Stream, Instancing, IndexBuffer, IndexRange, VertexRange, Budget, Lock, Unlock };
struct Diagnostics { Error error=Error::None; HRESULT hr=D3D_OK; UINT stream=0; };
inline const char* errorName(Error e) {switch(e){
case Error::None:return "none";case Error::Arguments:return "arguments";case Error::Declaration:return "declaration";
case Error::Stream:return "stream";case Error::Instancing:return "instancing";case Error::IndexBuffer:return "index buffer";
case Error::IndexRange:return "index range";case Error::VertexRange:return "vertex range";case Error::Budget:return "budget";
case Error::Lock:return "lock";case Error::Unlock:return "unlock";}return "unknown";}
inline unsigned declarationBytes(unsigned type) {
    constexpr unsigned sizes[]={4,8,12,16,4,4,4,8,4,4,8,4,8,4,4,4,8};
    return type<sizeof(sizes)/sizeof(*sizes)?sizes[type]:0;
}
template<class T> struct Ref {T* p=nullptr;~Ref(){if(p)p->Release();} T** out(){return &p;} T* operator->()const{return p;}};
// 0.3.181 (S2): the one software prefetch (a hint: never a dereference, never a fault).
inline void prefetch(const void* address){__builtin_prefetch(address);}
// 0.3.181 (S3, r89): the snapshot span (from the capture's snapshot phase to its constants phase) timed
// on RenderProfile=1 non-sample frames, as HandoffMeter (prepare_worker.h): 1 span in Stride from a
// per-frame phase, minus the clock-pair cost (pairNs: the median of 32 back-to-back deltas at the frame's
// first timed span). At the frame's end the mean corrected ns per timed span (at least MinSpans) goes to
// its (lookup mode, class) bucket; the class is the frame's fast snapshot hits (crowd >=500, mid 200-499,
// quiet <200). Spans, not accepted records: early returns count. Buckets are preallocated. Off: no clock read.
template<class ClockType> class SnapshotMeter {
public:
    using Clock=ClockType;
    static constexpr unsigned Stride=16,Modes=3,Classes=3,MinSpans=8,Capacity=512;
    struct Summary {unsigned frames[Modes]={};double median[Modes]={},p25[Modes]={},p75[Modes]={},pairNs=-1;
        unsigned long long spans=0,predictTried=0,predictHits=0,predictMisses=0,fastHits=0;};
    static unsigned classOf(unsigned fastHits){return fastHits>=500?0:fastHits>=200?1:2;}
    static const char* className(unsigned c){return c==0?"crowd":c==1?"mid":"quiet";}
private:
    bool on_=false;unsigned phase_=0,count_=0,spans_=0;double pairNs_=-1,correctedNs_=0;
    std::vector<double> perSpan_[Modes][Classes],pairs_[Classes];
    struct Totals {unsigned long long spans=0,predictTried=0,predictHits=0,predictMisses=0,fastHits=0;} totals_[Classes];
    static double ns(typename Clock::duration d){return std::chrono::duration<double,std::nano>(d).count();}
    void calibrate(){typename Clock::time_point t[33];for(auto& x:t)x=Clock::now();double d[32];
        for(unsigned i=0;i<32;++i)d[i]=ns(t[i+1]-t[i]);std::nth_element(d,d+16,d+32);pairNs_=d[16];}
    static double rank(std::vector<double>& v,unsigned quarter){return v.empty()?-1:v[(v.size()-1)*quarter/4];}
public:
    SnapshotMeter(){for(auto& mode:perSpan_)for(auto& v:mode)v.reserve(Capacity);for(auto& v:pairs_)v.reserve(Capacity);}
    void beginFrame(bool on,unsigned phase){on_=on;phase_=phase%Stride;count_=spans_=0;pairNs_=-1;correctedNs_=0;}
    bool on()const{return on_;}
    // Per span, in capture order: whether it is timed.
    bool sample(){if(!on_)return false;const bool s=(count_++ +phase_)%Stride==0;if(s&&pairNs_<0)calibrate();return s;}
    typename Clock::time_point start()const{return Clock::now();}
    void stop(typename Clock::time_point t){correctedNs_+=std::max(0.,ns(Clock::now()-t)-pairNs_);++spans_;}
    unsigned spans()const{return spans_;}
    // The end of a capture frame (mode: 0 Map, 1 Predict, 2 Prefetch). Prediction counters and fast
    // hits are summed over the Predict and Prefetch frames (the prediction rate's denominator).
    void endFrame(unsigned mode,unsigned fastHits,unsigned tried,unsigned hits,unsigned misses){
        if(!on_)return;on_=false;
        const unsigned c=classOf(fastHits);auto& t=totals_[c];t.spans+=spans_;
        if(mode){t.predictTried+=tried;t.predictHits+=hits;t.predictMisses+=misses;t.fastHits+=fastHits;}
        if(mode<Modes&&spans_>=MinSpans&&perSpan_[mode][c].size()<Capacity)perSpan_[mode][c].push_back(correctedNs_/spans_);
        if(pairNs_>=0&&pairs_[c].size()<Capacity)pairs_[c].push_back(pairNs_);
    }
    // One Summary per class with a bucketed frame; then empty (the capacity stays).
    template<class Visit> void report(Visit&& visit){
        for(unsigned c=0;c<Classes;++c){Summary s;bool any=false;
            for(unsigned m=0;m<Modes;++m){auto& v=perSpan_[m][c];std::sort(v.begin(),v.end());s.frames[m]=unsigned(v.size());any|=!v.empty();
                s.median[m]=rank(v,2);s.p25[m]=rank(v,1);s.p75[m]=rank(v,3);v.clear();}
            std::sort(pairs_[c].begin(),pairs_[c].end());s.pairNs=rank(pairs_[c],2);pairs_[c].clear();
            const auto& t=totals_[c];s.spans=t.spans;s.predictTried=t.predictTried;s.predictHits=t.predictHits;s.predictMisses=t.predictMisses;s.fastHits=t.fastHits;totals_[c]={};
            if(any)visit(c,s);}
    }
};
// A timed span that also ends on every early return.
template<class Meter> class SnapshotSpan {
    Meter* meter_;typename Meter::Clock::time_point start_{};bool on_;
public:
    explicit SnapshotSpan(Meter& meter):meter_(&meter),on_(meter.sample()){if(on_)start_=meter.start();}
    void end(){if(on_){meter_->stop(start_);on_=false;}}
    ~SnapshotSpan(){end();}
    SnapshotSpan(const SnapshotSpan&)=delete;SnapshotSpan& operator=(const SnapshotSpan&)=delete;
};
struct SnapshotTestAccess; /* tests/test_snapshot_prediction.cpp: CacheKey equality */
class Frame {
    friend struct SnapshotTestAccess;
    std::size_t used_=0,regularUsed_=0,limit_,reserved_,nearReserve_=0;unsigned draws_=0,regularDraws_=0;bool priority_=false,near_=false;
    std::vector<std::uint8_t> rawIndexScratch_;
    std::vector<std::uint64_t> indexMaskScratch_;
    std::vector<UINT> remapScratch_,uniqueSourceScratch_;
    size_t sourceSpanVertices_=0,uniqueVertices_=0,savedVertexBytes_=0;unsigned compactedDraws_=0;
    bool compacted_=false;
    // Compact records retain source ordering; adjacent records can be copied
    // together without observing unused vertices in the gaps.
    struct CopyRun {UINT source, destination, count;};
    std::vector<CopyRun> copyRuns_;
    // Immutable index plans only: no vertex bytes or resource references. A
    // fingerprint shortlists candidates; full CURRENT raw bytes prove every hit.
    struct DecodedEntry {
        std::vector<std::uint8_t> raw;
        std::vector<UINT> indices;
        std::vector<CopyRun> runs;
        UINT minimum=0,vertices=0,low=0,high=0,count=0;
        D3DFORMAT format=D3DFMT_INDEX16;bool compacted=false;
        std::uint64_t fingerprint=0;size_t slot=0;
        NorthlightIntrusiveLRU::Links<DecodedEntry> lru;
        size_t bytes=0;
    };
    static constexpr size_t IndexCacheLimit=16u*1024u*1024u;
    std::array<std::unique_ptr<DecodedEntry>,1024> decoded_;
    std::unordered_multimap<std::uint64_t,DecodedEntry*> decodedLookup_;
    size_t decodedBytes_=sizeof(decoded_);
    NorthlightIntrusiveLRU::List<DecodedEntry> decodedLRU_;
    std::array<size_t,1024> decodedFree_{};size_t decodedFreeCount_=1024;
    unsigned indexHits_=0,indexMisses_=0;
    DecodedEntry* recent_=nullptr;
    const std::vector<CopyRun>* activeRuns_=&copyRuns_;
public:
    // Persistent snapshots for write-tracked resources (including DYNAMIC),
    // with the existing static-only path retained for untracked resources.
    // Identity comes from the owner: a token that lives and dies with the COM
    // object (private data), so a reused pointer never aliases an old entry.
    // Returning 0 marks the buffer as not identifiable: it is read as before.
    using VersionFn=std::uint64_t(*)(void* buffer,bool indexBuffer);
    using IdentityFn=std::uint64_t(*)(void* buffer,bool indexBuffer);
    static constexpr size_t SnapshotCacheLimit=64u*1024u*1024u;
private:
    struct CacheKey {
        std::uint64_t version[5]={};UINT tracked=0;
        std::uint64_t vb[4]={},ib=0;UINT offset[4]={},stride[4]={},size[4]={},extent[4]={},ibSize=0;
        INT base=0;UINT minimum=0,vertices=0,start=0,primitives=0;unsigned topology=0,format=0;UINT indexed=0;const void* declaration=nullptr;
        CacheKey(){std::memset(this,0,sizeof *this);}
        // 0.3.181 (S1, r89): every byte, padding included (zero-filled above, only ever copied whole),
        // as memcmp()==0 was: 4-byte words, XOR/OR, no early exit. The DLL's memcmp was a byte loop.
        bool operator==(const CacheKey& o)const{
            static_assert(sizeof(CacheKey)%4==0,"compared as 4-byte words");
            constexpr std::size_t compared=NORTHLIGHT_SNAPSHOT_COUNTERFACTUAL==7?offsetof(CacheKey,declaration):sizeof(CacheKey);
            const auto* a=reinterpret_cast<const unsigned char*>(this);const auto* b=reinterpret_cast<const unsigned char*>(&o);
            std::uint32_t diff=0;
            for(std::size_t i=0;i<compared;i+=4){std::uint32_t x,y;std::memcpy(&x,a+i,4);std::memcpy(&y,b+i,4);diff|=x^y;}
            return !diff;
        }
    };
    struct CacheEntry {
        CacheKey key;std::shared_ptr<const Mesh> mesh;
        std::vector<std::uint8_t> rawIndices;std::vector<CopyRun> runs;bool compacted=false;UINT low=0;
        std::uint64_t begins[4]={},lockBytes[4]={},readBytes[4]={},ibBegin=0,ibBytes=0;
        size_t bytes=0;unsigned serial=0;
        NorthlightIntrusiveLRU::Links<CacheEntry> lru;
        // Budget charged on a hit: exactly what the miss reserved (raw index bytes
        // plus per-stream read spans), so accept/reject decisions never differ.
        std::size_t charged=0;
    };
    static std::uint64_t hashKey(const CacheKey& k){
        const auto* b=reinterpret_cast<const std::uint8_t*>(&k);std::uint64_t h=1469598103934665603ull;
        // The key is fully zero-initialized, including padding. Hash whole words
        // without alignment/aliasing assumptions; the exact key comparison in
        // serve() remains mandatory. A hash collision can only cause a miss.
        size_t i=0;for(;i+sizeof(std::uint64_t)<=sizeof k;i+=sizeof(std::uint64_t)){
            std::uint64_t word;std::memcpy(&word,b+i,sizeof word);h^=word;h*=1099511628211ull;}
        for(;i<sizeof k;++i){h^=b[i];h*=1099511628211ull;}
        h^=h>>33;h*=0xff51afd7ed558ccdULL;h^=h>>33;h*=0xc4ceb9fe1a85ec53ULL;h^=h>>33;
        return NORTHLIGHT_SNAPSHOT_HASH_BITS?h&((std::uint64_t(1)<<NORTHLIGHT_SNAPSHOT_HASH_BITS)-1):h;
    }
    IdentityFn identity_=nullptr;VersionFn version_=nullptr;
    NorthlightCaptureMetadata::Read metadata_=nullptr;
    using DeclarationFn=bool(*)(void*,IDirect3DVertexDeclaration9*,const D3DVERTEXELEMENT9*&,UINT&);
    DeclarationFn declaration_=nullptr;void* declarationContext_=nullptr;
    using LayoutFn=bool(*)(void*,IDirect3DVertexDeclaration9*,UINT*);
    LayoutFn captureLayout_=nullptr;void* layoutContext_=nullptr;
    std::unordered_map<std::uint64_t,std::unique_ptr<CacheEntry>> cache_;
    size_t cacheBytes_=0,cacheLimit_=SnapshotCacheLimit;unsigned cacheSerial_=0,frame_=0; /* cacheLimit_: lowered under address-space pressure */
    NorthlightIntrusiveLRU::List<CacheEntry> cacheLRU_;
    struct History {std::uint64_t logical=0,full=0;bool valid=false,evicted=false;};
    // Diagnostic history is bounded and never authorizes a cache hit. Collisions
    // are reported as unknown, never guessed to be a new resource or a write.
    std::array<History,2048> history_{};
public:
    struct Maintenance {unsigned snapshotEvictions=0,indexEvictions=0,missEvicted=0,missRevision=0,missUnknown=0,missCollision=0;
        size_t snapshotEvictedBytes=0,indexEvictedBytes=0;double snapshotMs=0,indexMs=0;};
private:
    Maintenance maintenance_;bool sampleMaintenance_=false;
    struct MaintenanceTimer {
        double* out;std::chrono::steady_clock::time_point start;
        explicit MaintenanceTimer(double* value):out(value){if(out)start=std::chrono::steady_clock::now();}
        ~MaintenanceTimer(){if(out)*out+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}
    };
    static std::uint64_t logicalHash(CacheKey key){for(auto& v:key.version)v=0;key.tracked=0;return hashKey(key);}
    void noteMiss(const CacheKey& key,std::uint64_t full,bool collision){
        if(!sampleMaintenance_)return;
        const auto logical=logicalHash(key);const auto& h=history_[logical%history_.size()];
        if(collision)++maintenance_.missCollision;
        else if(h.valid&&h.logical==logical&&h.full!=full)++maintenance_.missRevision;
        else if(h.valid&&h.logical==logical&&h.full==full&&h.evicted)++maintenance_.missEvicted;
        else ++maintenance_.missUnknown;
    }
    void noteStored(const CacheKey& key){const auto logical=logicalHash(key);history_[logical%history_.size()]={logical,hashKey(key),true,false};}
    void noteEvicted(const CacheKey& key){const auto logical=logicalHash(key);auto& h=history_[logical%history_.size()];if(h.valid&&h.logical==logical&&h.full==hashKey(key))h.evicted=true;}

    unsigned cacheHits_=0,cacheMisses_=0,revalidated_=0,revalidationMismatches_=0,trackedHits_=0;
    size_t avoidedReadBytes_=0;
    unsigned metadataBatches_=0,metadataHits_=0,metadataFallbacks_=0,fastCacheHits_=0;
public:
    // 0.3.181 (S2, r89): the snapshot-cache lookup. Map: today's hash lookup (the prediction is kept,
    // never read). Predict: a tracked key is first compared with the LRU successor of the previous hit
    // (the entry the same frame order reaches next). Prefetch: Predict plus a depth-2 prefetch of the
    // next predicted entries. A live predicted entry with an equal key is exactly the map's hit, so all
    // three are exact; the prediction is dropped at every cache erase (liveness is its only condition).
    enum class Lookup:unsigned char{Map,Predict,Prefetch};
    void setLookup(Lookup mode){lookup_=mode;}
    Lookup lookup()const{return lookup_;}
    unsigned predictTried()const{return predictTried_;}
    unsigned predictHits()const{return predictHits_;}
    unsigned predictMisses()const{return predictMisses_;}
private:
    CacheEntry* predicted_=nullptr;Lookup lookup_=Lookup::Prefetch;
    unsigned predictTried_=0,predictHits_=0,predictMisses_=0;
    enum class Erase:unsigned {Evict=1,Revalidation,Replacement,Clear};
    void dropPrediction(Erase site){if(unsigned(site)!=NORTHLIGHT_SNAPSHOT_COUNTERFACTUAL)predicted_=nullptr;}
    static bool predictedEqual(const CacheKey& entry,const CacheKey& key){
        if(NORTHLIGHT_SNAPSHOT_COUNTERFACTUAL==5)return !std::memcmp(&entry,&key,offsetof(CacheKey,base));
        return entry==key;
    }
    void evictOldest(){
        auto* oldest=cacheLRU_.oldest();if(!oldest)return;
        const auto hash=hashKey(oldest->key);noteEvicted(oldest->key);
        ++maintenance_.snapshotEvictions;maintenance_.snapshotEvictedBytes+=oldest->bytes;
        cacheBytes_-=oldest->bytes;cacheLRU_.remove(*oldest);cache_.erase(hash);
        dropPrediction(Erase::Evict); /* the evicted entry may be the predicted one */
    }
    // Byte-exact proof that the cached snapshot still equals the live buffers.
    // Locks are READONLY; any lock failure rejects the entry conservatively.
    bool revalidate(const CacheEntry& e,Ref<IDirect3DVertexBuffer9>* vertices,IDirect3DIndexBuffer9* ib,const UINT* extent){
        ++revalidated_;const Mesh& m=*e.mesh;
        if(e.key.indexed){void* ptr=nullptr;if(!ib||FAILED(ib->Lock(UINT(e.ibBegin),UINT(e.ibBytes),&ptr,NorthlightUpload::readBackLock())))return false;NorthlightLockMeter::readBack(ib,e.ibBytes);
            const bool same=ptr&&e.rawIndices.size()==e.ibBytes&&!std::memcmp(ptr,e.rawIndices.data(),size_t(e.ibBytes));
            if(FAILED(ib->Unlock())||!same)return false;}
        for(UINT s=0;s<4;++s){if(!extent[s]||!vertices[s].p)continue;
            const auto& target=m.streams[s];const UINT stride=e.key.stride[s];void* ptr=nullptr;
            if(FAILED(vertices[s]->Lock(UINT(e.begins[s]),UINT(e.lockBytes[s]),&ptr,NorthlightUpload::readBackLock())))return false;NorthlightLockMeter::readBack(vertices[s].p,e.lockBytes[s]);
            bool same=ptr!=nullptr;
            if(same){const auto* source=static_cast<const std::uint8_t*>(ptr);
                if(!e.compacted)same=!std::memcmp(target.bytes.data(),source,size_t(e.readBytes[s]));
                else for(const auto& run:e.runs){if(!same)break;const size_t offset=size_t(std::uint64_t(run.source)-e.low)*stride;
                    const size_t bytes=size_t(run.count)*stride-(run.destination+run.count==m.vertexCount?stride-extent[s]:0);
                    same=!std::memcmp(target.bytes.data()+size_t(run.destination)*stride,source+offset,bytes);}}
            if(FAILED(vertices[s]->Unlock())||!same)return false;
        }
        return true;
    }
    CacheKey makeKey(Ref<IDirect3DVertexBuffer9>* vertices,const UINT* extent,const Draw& draw,IDirect3DVertexDeclaration9* declaration,const UINT* strides,const UINT* offsets,const D3DVERTEXBUFFER_DESC* descriptions,std::uint64_t ib,UINT ibSize,unsigned format,bool& ok,const NorthlightCaptureMetadata::Info* metadata=nullptr){
        CacheKey k;ok=true;
        for(UINT s=0;s<4;++s){if(!extent[s])continue;
            const bool known=metadata&&metadata[s].known&&metadata[s].identity;
            k.vb[s]=known?metadata[s].identity:identity_(vertices[s].p,false);
            k.version[s]=known?metadata[s].revision:(version_?version_(vertices[s].p,false):0);
            if(!k.vb[s])ok=false;k.offset[s]=offsets[s];k.stride[s]=strides[s];k.size[s]=descriptions[s].Size;k.extent[s]=extent[s];}
        k.tracked=version_?1u:0u;for(UINT s=0;s<4;++s)if(extent[s]&&!k.version[s])k.tracked=0;
        k.ib=ib;k.ibSize=ibSize;k.base=draw.base;k.minimum=draw.minimum;k.vertices=draw.vertices;k.start=draw.start;k.primitives=draw.primitives;
        k.topology=unsigned(draw.topology);k.format=format;k.indexed=draw.indexed?1u:0u;k.declaration=declaration;return k;
    }
    // Serve a hit. Charges admission and the read budget exactly like a miss
    // of the same size, so accept/reject decisions do not change with caching.
    bool serve(const CacheKey& key,Ref<IDirect3DVertexBuffer9>* vertices,IDirect3DIndexBuffer9* ib,const UINT* extent,std::shared_ptr<const Mesh>* shared,Diagnostics* why,bool& served){
        served=false;CacheEntry* found=nullptr;
        // Tracked keys only: for them the revalidation branch below is never taken, so the predicted
        // path needs no map iterator.
        if(lookup_!=Lookup::Map&&(key.tracked||NORTHLIGHT_SNAPSHOT_COUNTERFACTUAL==6)&&predicted_){++predictTried_;
            if(predictedEqual(predicted_->key,key)){found=predicted_;++predictHits_;}else ++predictMisses_;}
        if(!found){
            const auto hash=hashKey(key);auto it=cache_.find(hash);if(it==cache_.end()||!(it->second->key==key)){noteMiss(key,hash,it!=cache_.end());return true;}
            CacheEntry& e=*it->second;
            if(!key.tracked&&(version_||(e.serial&15u)==(frame_&15u))&&!revalidate(e,vertices,ib,extent)){++revalidationMismatches_;cacheBytes_-=e.bytes;cacheLRU_.remove(e);cache_.erase(it);
                dropPrediction(Erase::Revalidation); /* the erased entry may be the predicted one */return true;}
            found=&e;
        }
        CacheEntry& e=*found;
        if(!admit(why)||!reserve(e.charged,why))return false;
        // The next prediction: e's successor in LRU order, read before touch(e) moves e to the end.
        CacheEntry* s1=e.lru.newer;predicted_=s1;
        if(lookup_==Lookup::Prefetch&&s1){
            // Depth 2: s1's mesh and LRU lines were prefetched at the previous hit (as its s2), so these
            // two loads are warm; s2 is used for address arithmetic only.
            const CacheEntry* s2=s1->lru.newer;const Mesh* m1=s1->mesh.get();
            if(m1){prefetch(reinterpret_cast<const char*>(m1)-16);prefetch(m1);} /* the control block's counts and the mesh */
            for(std::size_t offset=0;offset<sizeof(CacheKey);offset+=64)prefetch(reinterpret_cast<const char*>(&s1->key)+offset);
            if(s2){prefetch(&s2->mesh);prefetch(&s2->lru);}
        }
        cacheLRU_.touch(e);++cacheHits_;if(key.tracked){++trackedHits_;avoidedReadBytes_+=e.charged;}accepted();*shared=e.mesh;served=true;
        if(why){why->error=Error::None;why->hr=D3D_OK;}return true;
    }
    bool store(const CacheKey& key,Mesh&& mesh,std::uint64_t ibBegin,std::uint64_t ibBytes,UINT low,const std::uint64_t* begins,const std::uint64_t* lockBytes,const std::uint64_t* readBytes,std::shared_ptr<const Mesh>* shared){
        try{
            auto entry=std::make_unique<CacheEntry>();entry->key=key;entry->compacted=compacted_;entry->low=low;
            entry->ibBegin=ibBegin;entry->ibBytes=ibBytes;
            if(key.indexed)entry->rawIndices.assign(rawIndexScratch_.begin(),rawIndexScratch_.begin()+std::ptrdiff_t(ibBytes));
            if(compacted_)entry->runs=*activeRuns_;
            for(UINT s=0;s<4;++s){entry->begins[s]=begins[s];entry->lockBytes[s]=lockBytes[s];entry->readBytes[s]=readBytes[s];}
            entry->charged=std::size_t(ibBytes);for(UINT s=0;s<4;++s)entry->charged+=std::size_t(readBytes[s]);
            entry->bytes=sizeof(CacheEntry)+mesh.capacityBytes()+entry->rawIndices.capacity()+entry->runs.capacity()*sizeof(CopyRun)+64;
            if(entry->bytes>cacheLimit_)return false;
            {MaintenanceTimer timer(sampleMaintenance_&&cacheBytes_+entry->bytes>cacheLimit_?&maintenance_.snapshotMs:nullptr);
                while(cacheBytes_+entry->bytes>cacheLimit_&&!cache_.empty())evictOldest();}
            auto owned=std::make_shared<Mesh>();entry->mesh=owned;
            entry->serial=cacheSerial_++;
            const auto hash=hashKey(key);auto existing=cache_.find(hash);if(existing!=cache_.end()){cacheBytes_-=existing->second->bytes;cacheLRU_.remove(*existing->second);cache_.erase(existing);
                dropPrediction(Erase::Replacement);} /* the replaced entry may be the predicted one */
            const auto bytes=entry->bytes;auto inserted=cache_.emplace(hash,std::move(entry));cacheLRU_.append(*inserted.first->second);cacheBytes_+=bytes;noteStored(key);
            *owned=std::move(mesh);*shared=owned;return true;
        }catch(...){return false;}
    }
public:
    void setIdentityProvider(IdentityFn f){identity_=f;}
    void setVersionProvider(VersionFn f){clearSnapshotCache();version_=f;}
    // Metadata providers must share the identity/version domain of the legacy
    // providers. A batch is consumed immediately, never reused across draws.
    void setMetadataProvider(NorthlightCaptureMetadata::Read f){clearSnapshotCache();metadata_=f;}
    void setDeclarationProvider(void* context,DeclarationFn f){declarationContext_=context;declaration_=f;}
    void setLayoutProvider(void* context,LayoutFn f){layoutContext_=context;captureLayout_=f;}
    void clearSnapshotCache(){cacheLRU_.clear();cache_.clear();cacheBytes_=0;for(auto& h:history_)h={};
        dropPrediction(Erase::Clear);} /* every entry is gone */
    // Memory guard: at most SnapshotCacheLimit; evicts oldest down to the new cap now.
    // Entries are shared immutable meshes, so holders of an evicted mesh keep it valid.
    void setSnapshotCacheLimit(size_t bytes){cacheLimit_=std::min(bytes,SnapshotCacheLimit);while(cacheBytes_>cacheLimit_&&!cache_.empty())evictOldest();}
    size_t snapshotCacheLimit()const{return cacheLimit_;}
    void sampleMaintenance(bool value){sampleMaintenance_=value;}
    const Maintenance& maintenance()const{return maintenance_;}
    size_t snapshotCacheBytes()const{return cacheBytes_;}
    size_t snapshotCacheEntries()const{return cache_.size();}
    unsigned snapshotCacheHits()const{return cacheHits_;}
    unsigned trackedHits()const{return trackedHits_;}
    size_t avoidedReadBytes()const{return avoidedReadBytes_;}
    unsigned snapshotCacheMisses()const{return cacheMisses_;}
    unsigned snapshotRevalidated()const{return revalidated_;}
    unsigned snapshotRevalidationMismatches()const{return revalidationMismatches_;}
    unsigned metadataBatches()const{return metadataBatches_;}
    unsigned metadataHits()const{return metadataHits_;}
    unsigned metadataFallbacks()const{return metadataFallbacks_;}
    unsigned fastCacheHits()const{return fastCacheHits_;}
    // 0.3.181 (S4): the key compare and hash in situ, 256 times each on two equal hot keys: the 0.3.180
    // compare (compiler_rt's byte loop, kept as NorthlightMem::byteCompare), today's ==, and hashKey.
    struct KeyBench {double legacyNs=-1,equalNs=-1,hashNs=-1;};
    static KeyBench benchKeys(){
        using C=std::chrono::steady_clock;constexpr unsigned N=256;CacheKey a,b;
        for(unsigned s=0;s<4;++s){a.vb[s]=b.vb[s]=s+1;a.version[s]=b.version[s]=s+7;a.stride[s]=b.stride[s]=24;a.extent[s]=b.extent[s]=20;}
        a.primitives=b.primitives=300;a.declaration=b.declaration=&a;
        auto time=[&](auto&& op){unsigned sink=0;const auto t0=C::now();
            for(unsigned i=0;i<N;++i){__asm__ __volatile__(""::"r"(&a),"r"(&b):"memory");sink+=unsigned(op());}
            const auto t1=C::now();__asm__ __volatile__(""::"r"(sink):"memory");return std::chrono::duration<double,std::nano>(t1-t0).count()/N;};
        KeyBench k;
        k.legacyNs=time([&]{return NorthlightMem::byteCompare(&a,&b,sizeof(CacheKey))==0;});
        k.equalNs=time([&]{return a==b;});
        k.hashNs=time([&]{return unsigned(hashKey(a));});
        return k;
    }
    // Tests: the cached meshes from oldest to newest (the LRU order).
    template<class Visit> void visitLRU(Visit&& visit)const{for(const CacheEntry* e=cacheLRU_.oldest();e;e=e->lru.newer)visit(e->mesh.get());}
private:
    static std::uint64_t fingerprint(const void* source,size_t bytes){
        const auto* data=static_cast<const std::uint8_t*>(source);
        std::uint64_t h=1469598103934665603ull^bytes;
        // Deliberately bounded work for large IBs. Unexamined mutations still
        // reject the candidate through the full memcmp below.
        if(bytes<4){for(size_t i=0;i<bytes;++i){h^=data[i];h*=1099511628211ull;}}
        else for(unsigned i=0;i<8;++i){std::uint32_t word;std::memcpy(&word,data+((bytes-4)*i)/7,4);h^=word;h*=1099511628211ull;}
        return h;
    }
    void rememberPlan(const void* data,size_t rawBytes,std::uint64_t hash,D3DFORMAT format,
                      const Draw& draw,const Mesh& mesh,UINT low,UINT high){
        try {
            const size_t estimate=sizeof(DecodedEntry)+128+rawBytes+mesh.indices.size()*sizeof(UINT)+copyRuns_.size()*sizeof(CopyRun);
            if(estimate>IndexCacheLimit-sizeof(decoded_))return;
            const auto makeRoom=[&](size_t bytes){
                MaintenanceTimer timer(sampleMaintenance_&&(!decodedFreeCount_||bytes>IndexCacheLimit-decodedBytes_)?&maintenance_.indexMs:nullptr);
                while(!decodedFreeCount_||bytes>IndexCacheLimit-decodedBytes_){
                    auto* oldest=decodedLRU_.oldest();if(!oldest)return decoded_.size();
                    const auto slot=oldest->slot;
                    if(recent_==oldest)recent_=nullptr;
                    auto range=decodedLookup_.equal_range(oldest->fingerprint);
                    for(auto it=range.first;it!=range.second;++it)if(it->second==oldest){decodedLookup_.erase(it);break;}
                    ++maintenance_.indexEvictions;maintenance_.indexEvictedBytes+=oldest->bytes;
                    decodedBytes_-=oldest->bytes;decodedLRU_.remove(*oldest);decoded_[slot].reset();decodedFree_[decodedFreeCount_++]=slot;
                }
                return decodedFree_[decodedFreeCount_-1];
            };
            if(makeRoom(estimate)==decoded_.size())return;
            auto entry=std::make_unique<DecodedEntry>();
            entry->raw.assign(static_cast<const uint8_t*>(data),static_cast<const uint8_t*>(data)+rawBytes);
            entry->indices=mesh.indices;entry->runs=copyRuns_;
            entry->minimum=draw.minimum;entry->vertices=draw.vertices;entry->format=format;
            entry->low=low;entry->high=high;entry->count=mesh.vertexCount;entry->compacted=compacted_;entry->fingerprint=hash;
            entry->bytes=sizeof(DecodedEntry)+128+entry->raw.capacity()+entry->indices.capacity()*sizeof(UINT)+entry->runs.capacity()*sizeof(CopyRun);
            if(entry->bytes>IndexCacheLimit-sizeof(decoded_))return;
            size_t slot=makeRoom(entry->bytes);if(slot==decoded_.size())return;
            decodedLookup_.emplace(hash,entry.get());
            entry->slot=slot;--decodedFreeCount_;decodedLRU_.append(*entry);decodedBytes_+=entry->bytes;recent_=entry.get();decoded_[slot]=std::move(entry);
        }catch(...){return;} // Optional cache allocation cannot invalidate output.
    }
    bool fail(Diagnostics* why,Error e,HRESULT hr=D3DERR_INVALIDCALL) {if(why){why->error=e;why->hr=hr;}return false;}
    // 0.3.172 near reserve: a priority draw flagged nearby may read up to limit_+nearReserve_;
    // every other draw still stops at limit_ (used_ can exceed it only through near draws).
    std::size_t limitFor(bool priority,bool nearby)const{return limit_+(priority&&nearby?nearReserve_:0);}
    bool canReserve(std::size_t n,Diagnostics* why) {const std::size_t limit=limitFor(priority_,near_);
        return used_<=limit&&n<=limit-used_&&(priority_||n<=limit_-reserved_-regularUsed_)?true:fail(why,Error::Budget);}
    bool reserve(std::size_t n,Diagnostics* why) {if(!canReserve(n,why))return false;used_+=n;if(!priority_)regularUsed_+=n;return true;}
    bool admit(Diagnostics* why){return draws_<4096&&(priority_||regularDraws_<3072)?true:fail(why,Error::Budget);}
    void accepted(){++draws_;if(!priority_)++regularDraws_;}
    bool layout(IDirect3DVertexDeclaration9* declaration,UINT extent[4],Diagnostics* why) {
        if(!declaration)return fail(why,Error::Declaration);
        if(captureLayout_)return captureLayout_(layoutContext_,declaration,extent)?true:fail(why,Error::Declaration);
        D3DVERTEXELEMENT9 local[MAXD3DDECLLENGTH+1];const D3DVERTEXELEMENT9* elements=local;UINT n=MAXD3DDECLLENGTH+1;
        if(declaration_){if(!declaration_(declarationContext_,declaration,elements,n))return fail(why,Error::Declaration);}
        else {HRESULT hr=declaration->GetDeclaration(local,&n);if(FAILED(hr)||n>MAXD3DDECLLENGTH+1)return fail(why,Error::Declaration,hr);}
        bool any=false,end=false;
        for(UINT i=0;i<n;++i){auto& e=elements[i];if(e.Stream==0xff){end=true;break;}
            unsigned size=declarationBytes(e.Type);if(e.Stream>=4||!size||e.Method!=D3DDECLMETHOD_DEFAULT)return fail(why,Error::Declaration);
            extent[e.Stream]=std::max(extent[e.Stream],UINT(e.Offset)+size);any=true;}
        return any&&end?true:fail(why,Error::Declaration);
    }
    bool indexCount(const Draw& draw,UINT& n,Diagnostics* why) {
        if(!draw.primitives||draw.primitives>87380)return fail(why,Error::Arguments);
        if(draw.topology==D3DPT_TRIANGLELIST)n=draw.primitives*3;
        else if(draw.topology==D3DPT_TRIANGLESTRIP)n=draw.primitives+2;
        else return fail(why,Error::Arguments);
        if(draw.indexed&&(!draw.vertices||std::uint64_t(draw.minimum)+draw.vertices>std::uint64_t(UINT(-1))+1))return fail(why,Error::Arguments);
        return true;
    }
    bool decode(const void* data,UINT count,D3DFORMAT format,const Draw& draw,Mesh& mesh,UINT& low,UINT& high,Diagnostics* why) {
        const size_t rawBytes=size_t(count)*(format==D3DFMT_INDEX16?2:4);
        activeRuns_=&copyRuns_;const auto hash=fingerprint(data,rawBytes);
        const auto matches=[&](const DecodedEntry* entry){return entry&&entry->fingerprint==hash&&entry->minimum==draw.minimum&&entry->vertices==draw.vertices&&entry->format==format&&entry->raw.size()==rawBytes&&
            !std::memcmp(entry->raw.data(),data,rawBytes);};
        DecodedEntry* cached=matches(recent_)?recent_:nullptr;
        if(!cached){auto range=decodedLookup_.equal_range(hash);for(auto it=range.first;it!=range.second;++it)if(it->second!=recent_&&matches(it->second)){cached=it->second;break;}}
        if(cached){
            mesh.indices.assign(cached->indices.begin(),cached->indices.end());
            low=cached->low;high=cached->high;compacted_=cached->compacted;mesh.vertexCount=cached->count;
            activeRuns_=&cached->runs;recent_=cached;decodedLRU_.touch(*cached);++indexHits_;return true;
        }
        ++indexMisses_;low=UINT(-1);high=0;mesh.indices.resize(count);compacted_=false;uniqueSourceScratch_.clear();
        auto remember=[&](){
            copyRuns_.clear();
            if(compacted_)for(UINT i=0;i<uniqueSourceScratch_.size();++i){UINT source=uniqueSourceScratch_[i];
                if(!copyRuns_.empty()&&uint64_t(copyRuns_.back().source)+copyRuns_.back().count==source)++copyRuns_.back().count;
                else copyRuns_.push_back({source,i,1});}
            rememberPlan(data,rawBytes,hash,format,draw,mesh,low,high);
        };
        for(UINT i=0;i<count;++i){UINT index=0;if(format==D3DFMT_INDEX16){std::uint16_t v;std::memcpy(&v,static_cast<const std::uint8_t*>(data)+i*2,2);index=v;}else std::memcpy(&index,static_cast<const std::uint8_t*>(data)+i*4,4);
            if(index<draw.minimum||std::uint64_t(index)>=std::uint64_t(draw.minimum)+draw.vertices)return fail(why,Error::IndexRange);
            low=std::min(low,index);high=std::max(high,index);mesh.indices[i]=index;}
        const uint64_t span=uint64_t(high)-low+1;
        // Small bounded bitset detects the common dense range without sorting
        // triangles or altering their vertex order. No allocation scales with a
        // malicious 32-bit index span: large sparse ranges use bounded sort.
        if(span<=262144){
            indexMaskScratch_.assign(size_t((span+63)/64),0);
            for(auto index:mesh.indices){UINT relative=index-low;indexMaskScratch_[relative/64]|=uint64_t(1)<<(relative%64);}
            size_t unique=0;for(auto word:indexMaskScratch_)unique+=size_t(__builtin_popcountll(word));
            if(unique==span){for(auto& index:mesh.indices)index-=low;mesh.vertexCount=UINT(span);remember();return true;}
            uniqueSourceScratch_.reserve(unique);remapScratch_.resize(size_t(span));
            for(UINT relative=0;relative<span;++relative)if(indexMaskScratch_[relative/64]&(uint64_t(1)<<(relative%64))){remapScratch_[relative]=UINT(uniqueSourceScratch_.size());uniqueSourceScratch_.push_back(low+relative);}
            for(auto& index:mesh.indices)index=remapScratch_[index-low];
        }else{
            uniqueSourceScratch_.assign(mesh.indices.begin(),mesh.indices.end());std::sort(uniqueSourceScratch_.begin(),uniqueSourceScratch_.end());
            uniqueSourceScratch_.erase(std::unique(uniqueSourceScratch_.begin(),uniqueSourceScratch_.end()),uniqueSourceScratch_.end());
            for(auto& index:mesh.indices)index=UINT(std::lower_bound(uniqueSourceScratch_.begin(),uniqueSourceScratch_.end(),index)-uniqueSourceScratch_.begin());
        }
        compacted_=true;mesh.vertexCount=UINT(uniqueSourceScratch_.size());remember();return true;
    }
    void copyVertices(std::uint8_t* target,const std::uint8_t* source,UINT stride,UINT extent,UINT low,UINT vertices)const{
        const size_t readBytes=size_t(vertices-1)*stride+extent;
        if(!compacted_)std::memcpy(target,source,readBytes);
        else for(const auto& run:*activeRuns_){const size_t offset=size_t(uint64_t(run.source)-low)*stride;
            const size_t bytes=size_t(run.count)*stride-(run.destination+run.count==vertices?stride-extent:0);
            std::memcpy(target+size_t(run.destination)*stride,source+offset,bytes);}
        std::fill(target+readBytes,target+size_t(vertices)*stride,std::uint8_t(0));
    }

public:
    // Reserve half the default frame budget for caller-audited skeletal
    // priority. DYNAMIC means mutable storage, and never grants actor priority.
    explicit Frame(std::size_t maxBytes=32u*1024u*1024u,std::size_t priorityBytes=16u*1024u*1024u):limit_(maxBytes),reserved_(std::min(maxBytes,priorityBytes)){for(size_t i=0;i<decodedFree_.size();++i)decodedFree_[i]=i;}
    bool configureBudget(std::size_t maxBytes,std::size_t priorityBytes){
        if(used_||draws_)return false;
        limit_=maxBytes;reserved_=std::min(maxBytes,priorityBytes);return true;
    }
    // Proof-only preflight for the NEXT draw's priority, never the last draw's.
    // Every accepted layout has at least one >=4-byte attribute. Failed large
    // draws do not imply exhaustion: a later small draw must still be tried.
    bool captureExhausted(bool priority,bool nearby=false)const noexcept{
        if(countExhausted(priority))return true;
        const std::size_t limit=limitFor(priority,nearby),total=used_>=limit?0:limit-used_;
        if(total<4)return true;
        const std::size_t regularLimit=limit_-reserved_;
        return !priority&&(regularUsed_>=regularLimit||regularLimit-regularUsed_<4);
    }
    // Draw-count exhaustion alone: the near reserve never bypasses it.
    bool countExhausted(bool priority)const noexcept{return draws_>=4096||(!priority&&regularDraws_>=3072);}
    // Extra bytes for near priority draws once the main budget is spent (0: off, the 0.3.171 budget).
    void setNearReserve(std::size_t bytes){nearReserve_=bytes;}
    std::size_t nearReserve()const{return nearReserve_;}
    void clearFrame(){maintenance_={};sampleMaintenance_=false;used_=regularUsed_=0;draws_=regularDraws_=0;sourceSpanVertices_=uniqueVertices_=savedVertexBytes_=0;compactedDraws_=0;indexHits_=indexMisses_=0;cacheHits_=cacheMisses_=revalidated_=revalidationMismatches_=trackedHits_=0;avoidedReadBytes_=0;metadataBatches_=metadataHits_=metadataFallbacks_=fastCacheHits_=0;predictTried_=predictHits_=predictMisses_=0;++frame_;}
    // Cache lifetime never establishes validity: reads still lock/compare current
    // IB contents and copy current VB data. Reset/destruction may drop plans.
    void clearIndexCache(){activeRuns_=&copyRuns_;recent_=nullptr;decodedLookup_.clear();decodedLRU_.clear();for(auto& entry:decoded_)entry.reset();decodedBytes_=sizeof(decoded_);decodedFreeCount_=decodedFree_.size();for(size_t i=0;i<decodedFree_.size();++i)decodedFree_[i]=i;clearSnapshotCache();}
    size_t indexCacheBytes()const{return decodedBytes_;}
    unsigned indexCacheEntries()const{return unsigned(decoded_.size()-decodedFreeCount_);}
    unsigned indexCacheHits()const{return indexHits_;}
    unsigned indexCacheMisses()const{return indexMisses_;}
    std::size_t bytesRead()const{return used_;}
    std::size_t sourceSpanVertices()const{return sourceSpanVertices_;}
    std::size_t uniqueVertices()const{return uniqueVertices_;}
    std::size_t savedVertexBytes()const{return savedVertexBytes_;}
    unsigned compactedDraws()const{return compactedDraws_;}
    // shared: identified static resources or fully write-tracked generations
    // (also DYNAMIC) may be served from / stored into the persistent
    // cache as an immutable shared mesh and output is left empty. Hits charge
    // admission and the read budget exactly like the equivalent miss.
    bool read(IDirect3DDevice9* device,IDirect3DVertexDeclaration9* declaration,const Draw& draw,Mesh& output,Diagnostics* why=nullptr,bool priority=false,std::shared_ptr<const Mesh>* shared=nullptr,bool nearby=false) {
        if(shared)shared->reset();
        Mesh mesh=std::move(output);mesh.prepare();output=Mesh{};if(why)*why={};if(!device)return fail(why,Error::Arguments);
        UINT extent[4]={},n=0;if(!layout(declaration,extent,why)||!indexCount(draw,n,why))return false;
        mesh.topology=draw.topology;mesh.primitiveCount=draw.primitives;mesh.indexed=draw.indexed;
        if(!draw.indexed)mesh.indices.clear();for(unsigned i=0;i<4;++i)if(!extent[i])mesh.streams[i].bytes.clear();
        Ref<IDirect3DVertexBuffer9> vertices[4];D3DVERTEXBUFFER_DESC descriptions[4]={};UINT offsets[4]={},strides[4]={};
        priority_=priority;near_=nearby;compacted_=false;
        bool cacheable=shared&&identity_;CacheKey key;std::uint64_t cacheIbBegin=0,cacheIbBytes=0;
        // Hold the actual bound resources alive while one registry lookup reads
        // immutable descriptions/identities and CURRENT write generations. An
        // unknown member alone takes its original GetDesc/identity/version path.
        const bool batch=metadata_&&cacheable&&version_;
        NorthlightCaptureMetadata::Request requests[5];NorthlightCaptureMetadata::Info metadata[5];
        Ref<IDirect3DIndexBuffer9> boundIndices;D3DINDEXBUFFER_DESC indexDescription={};
        bool metadataKnown=batch;
        for(UINT stream=0;stream<4;++stream){if(!extent[stream])continue;if(why)why->stream=stream;UINT frequency=0;
            HRESULT hr=device->GetStreamSource(stream,vertices[stream].out(),&offsets[stream],&strides[stream]);
            if(FAILED(hr)||!vertices[stream].p||strides[stream]<extent[stream]||strides[stream]>4096)return fail(why,Error::Stream,hr);
            if(FAILED(hr=device->GetStreamSourceFreq(stream,&frequency))||frequency!=1)return fail(why,Error::Instancing,hr);
            requests[stream]={vertices[stream].p,false};
            if(!batch){if(FAILED(hr=vertices[stream]->GetDesc(&descriptions[stream])))return fail(why,Error::Stream,hr);}
        }
        if(batch){
            if(draw.indexed){HRESULT hr=device->GetIndices(boundIndices.out());if(FAILED(hr)||!boundIndices.p)return fail(why,Error::IndexBuffer,hr);requests[4]={boundIndices.p,true};}
            metadata_(requests,metadata,5);++metadataBatches_;
            for(UINT s=0;s<4;++s){if(!extent[s])continue;if(why)why->stream=s;
                if(metadata[s].known&&metadata[s].identity){descriptions[s].Size=metadata[s].size;descriptions[s].Usage=metadata[s].usage;++metadataHits_;}
                else {metadataKnown=false;++metadataFallbacks_;HRESULT hr=vertices[s]->GetDesc(&descriptions[s]);if(FAILED(hr))return fail(why,Error::Stream,hr);}}
            if(draw.indexed){
                if(metadata[4].known&&metadata[4].identity){indexDescription.Size=metadata[4].size;indexDescription.Usage=metadata[4].usage;indexDescription.Format=metadata[4].format;++metadataHits_;}
                else {metadataKnown=false;++metadataFallbacks_;HRESULT hr=boundIndices->GetDesc(&indexDescription);if(FAILED(hr))return fail(why,Error::IndexBuffer,hr);}}
        }
        for(UINT s=0;s<4;++s)if(extent[s])mesh.dynamic|=(descriptions[s].Usage&D3DUSAGE_DYNAMIC)!=0;
        UINT low=draw.start,high=0;
        if(draw.indexed){auto& ib=boundIndices;HRESULT hr=D3D_OK;auto& desc=indexDescription;
            if(!batch){hr=device->GetIndices(ib.out());if(FAILED(hr)||!ib.p)return fail(why,Error::IndexBuffer,hr);
                if(FAILED(hr=ib->GetDesc(&desc)))return fail(why,Error::IndexBuffer,hr);}
            if(desc.Format!=D3DFMT_INDEX16&&desc.Format!=D3DFMT_INDEX32)return fail(why,Error::IndexBuffer,hr);
            mesh.dynamic|=(desc.Usage&D3DUSAGE_DYNAMIC)!=0;
            UINT size=desc.Format==D3DFMT_INDEX16?2:4;std::uint64_t begin=std::uint64_t(draw.start)*size,bytes=std::uint64_t(n)*size;
            if(begin+bytes>desc.Size)return fail(why,Error::IndexRange);
            // DYNAMIC resources may be cached only with complete write tracking.
            if(cacheable){bool ok=true;const bool known=batch&&metadata[4].known&&metadata[4].identity;
                const std::uint64_t token=known?metadata[4].identity:identity_(ib.p,true);
                key=makeKey(vertices,extent,draw,declaration,strides,offsets,descriptions,token,desc.Size,unsigned(desc.Format),ok,batch?metadata:nullptr);
                key.version[4]=known?metadata[4].revision:(version_?version_(ib.p,true):0);if(!key.version[4])key.tracked=0;cacheable=ok&&token!=0&&(!mesh.dynamic||key.tracked);
                if(cacheable){bool served=false;if(!serve(key,vertices,ib.p,extent,shared,why,served))return false;if(served){fastCacheHits_+=metadataKnown&&key.tracked;output=std::move(mesh);return true;}}}
            cacheIbBegin=begin;cacheIbBytes=bytes;
            if(!admit(why))return false;
            if(!reserve(std::size_t(bytes),why))return false;
            rawIndexScratch_.resize(static_cast<std::size_t>(bytes));auto& raw=rawIndexScratch_;void* ptr=nullptr;
            if(FAILED(hr=ib->Lock(UINT(begin),UINT(bytes),&ptr,NorthlightUpload::readBackLock())))return fail(why,Error::Lock,hr);NorthlightLockMeter::readBack(ib.p,bytes);
            if(ptr)std::memcpy(raw.data(),ptr,raw.size());HRESULT unlocked=ib->Unlock();
            if(FAILED(unlocked))return fail(why,Error::Unlock,unlocked);if(!ptr)return fail(why,Error::Lock);
            if(!decode(raw.data(),n,desc.Format,draw,mesh,low,high,why))return false;
        }else{
            // DYNAMIC resources may be cached only with complete write tracking.
            if(cacheable){bool ok=true;key=makeKey(vertices,extent,draw,declaration,strides,offsets,descriptions,0,0,0,ok,batch?metadata:nullptr);cacheable=ok&&(!mesh.dynamic||key.tracked);
                if(cacheable){bool served=false;if(!serve(key,vertices,nullptr,extent,shared,why,served))return false;if(served){fastCacheHits_+=metadataKnown&&key.tracked;output=std::move(mesh);return true;}}}
            if(!admit(why))return false;if(std::uint64_t(low)+n>std::uint64_t(UINT(-1))+1)return fail(why,Error::VertexRange);high=low+n-1;}
        std::int64_t first=std::int64_t(low)+(draw.indexed?draw.base:0),last=std::int64_t(high)+(draw.indexed?draw.base:0);
        if(first<0||last<first||last>UINT(-1))return fail(why,Error::VertexRange);
        if(!draw.indexed)mesh.vertexCount=high-low+1;
        const uint64_t sourceSpan=uint64_t(high)-low+1;
        std::uint64_t begins[4]={},lockBytes[4]={},readBytes[4]={},totalReadBytes=0,savedBytes=0;
        // Validate every stream and the complete read budget before touching
        // any VB. A later stream can no longer consume/drop earlier VB copies.
        for(UINT s=0;s<4;++s){if(!extent[s])continue;if(why)why->stream=s;
            begins[s]=uint64_t(offsets[s])+uint64_t(first)*strides[s];
            lockBytes[s]=(sourceSpan-1)*strides[s]+extent[s];
            const uint64_t bytes=uint64_t(mesh.vertexCount)*strides[s];
            readBytes[s]=bytes-strides[s]+extent[s];
            if(begins[s]+lockBytes[s]>descriptions[s].Size||begins[s]>UINT(-1)||lockBytes[s]>UINT(-1)||bytes>UINT(-1))return fail(why,Error::VertexRange);
            totalReadBytes+=readBytes[s];savedBytes+=lockBytes[s]-readBytes[s];
        }
        if(totalReadBytes>std::numeric_limits<size_t>::max()||!canReserve(size_t(totalReadBytes),why))return false;
        for(UINT s=0;s<4;++s){if(!extent[s])continue;if(why)why->stream=s;
            auto& vb=vertices[s];auto& target=mesh.streams[s];UINT stride=strides[s];HRESULT hr=D3D_OK;
            target.stride=stride;target.bytes.resize(size_t(mesh.vertexCount)*stride);
            if(!reserve(size_t(readBytes[s]),why))return false;void* ptr=nullptr;
            if(FAILED(hr=vb->Lock(UINT(begins[s]),UINT(lockBytes[s]),&ptr,NorthlightUpload::readBackLock())))return fail(why,Error::Lock,hr);NorthlightLockMeter::readBack(vb.p,lockBytes[s]);
            if(ptr)copyVertices(target.bytes.data(),static_cast<const std::uint8_t*>(ptr),stride,extent[s],low,mesh.vertexCount);HRESULT unlocked=vb->Unlock();
            if(FAILED(unlocked))return fail(why,Error::Unlock,unlocked);if(!ptr)return fail(why,Error::Lock);
        }
        sourceSpanVertices_+=size_t(sourceSpan);uniqueVertices_+=mesh.vertexCount;savedVertexBytes_+=size_t(savedBytes);compactedDraws_+=compacted_;
        accepted();if(why){why->error=Error::None;why->hr=D3D_OK;}
        if(cacheable){++cacheMisses_;if(store(key,std::move(mesh),cacheIbBegin,cacheIbBytes,low,begins,lockBytes,readBytes,shared))return true;}
        output=std::move(mesh);return true;
    }
    bool readUP(IDirect3DVertexDeclaration9* declaration,const Draw& draw,const void* indices,D3DFORMAT format,const void* vertices,UINT stride,Mesh& output,Diagnostics* why=nullptr,bool priority=false,bool nearby=false){
        Mesh mesh=std::move(output);mesh.prepare();output=Mesh{};if(why)*why={};priority_=priority;near_=nearby;compacted_=false;UINT extent[4]={},n=0;
        if(!vertices)return fail(why,Error::Arguments);
        if(!layout(declaration,extent,why)||!indexCount(draw,n,why))return false;
        if(extent[1]||extent[2]||extent[3]||!extent[0]||stride<extent[0]||stride>4096||draw.base||draw.start)return fail(why,Error::Declaration);
        mesh.topology=draw.topology;mesh.primitiveCount=draw.primitives;mesh.indexed=draw.indexed;
        if(!draw.indexed)mesh.indices.clear();for(unsigned i=1;i<4;++i)mesh.streams[i].bytes.clear();
        mesh.dynamic=true;if(!admit(why))return false;UINT low=0,high=n-1;
        if(draw.indexed){if(!indices||(format!=D3DFMT_INDEX16&&format!=D3DFMT_INDEX32))return fail(why,Error::IndexBuffer);
            if(!reserve(n*(format==D3DFMT_INDEX16?2u:4u),why)||!decode(indices,n,format,draw,mesh,low,high,why))return false;}
        if(!draw.indexed)mesh.vertexCount=high-low+1;
        const uint64_t sourceSpan=uint64_t(high)-low+1,offset=uint64_t(low)*stride,lockBytes=(sourceSpan-1)*stride+extent[0];
        const uint64_t bytes=uint64_t(mesh.vertexCount)*stride,readBytes=bytes-stride+extent[0];
        if(offset+lockBytes>std::numeric_limits<size_t>::max()||bytes>UINT(-1))return fail(why,Error::VertexRange);
        if(!reserve(size_t(readBytes),why))return false;
        mesh.streams[0].stride=stride;mesh.streams[0].bytes.resize(size_t(bytes));
        copyVertices(mesh.streams[0].bytes.data(),static_cast<const std::uint8_t*>(vertices)+size_t(offset),stride,extent[0],low,mesh.vertexCount);
        sourceSpanVertices_+=size_t(sourceSpan);uniqueVertices_+=mesh.vertexCount;savedVertexBytes_+=size_t(lockBytes-readBytes);compactedDraws_+=compacted_;
        accepted();output=std::move(mesh);return true;
    }
};
} // namespace NorthlightDrawSnapshot
