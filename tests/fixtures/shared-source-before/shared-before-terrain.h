#pragma once
#include "intrusive_lru.h"
#include <unordered_map>
// Exact chunk coverage of the terrain triangles actually submitted this frame.
// This snapshots buffers or reuses a fully write-tracked immutable snapshot.
// It neither changes game geometry nor replays draws.
// Caller must first identify an audited Terrain shader (not DetailDoodad).
#include <d3d9.h>
#include "upload_lock.h"
#include "lock_meter_readback.h"
#include "replay_copy_reader.h"
#include "vertex_declaration_cache.h"
#include "capture_buffer_metadata.h"
#include "terrain_indices.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <unordered_set>
#include <utility>
#include <vector>

namespace NorthlightTerrainCapture {
struct ChunkBounds {
    // x follows ADT tile X (world Y); y follows ADT tile Y (world X).
    int x=0,y=0;
    float minX=0,minY=0,maxX=0,maxY=0;
};
struct Bounds {
    float low[3]={},high[3]={};
    std::vector<ChunkBounds> chunks;
};
struct Position {float x,y,z;};
struct MeshSnapshot {
    Bounds bounds;
    std::vector<Position> positions;
    // Always triangle-list indices into positions; detached from game buffers.
    NorthlightTerrainIndices::Indices indices;
};
enum class RejectReason {
    None,Arguments,PrimitiveLimit,CaptureLimit,ByteBudget,ViewRead,ViewMismatch,
    DeclarationRead,DeclarationBounds,PositionFormat,StreamFrequency,
    VertexBufferRead,IndexBufferRead,VertexDescription,IndexDescription,
    Stride,IndexFormat,IndexRange,IndexLock,IndexUnlock,VertexRange,VertexLock,
    VertexUnlock,VertexNonfinite,ChunkCoverage
};
inline const char* rejectName(RejectReason reason){
    static const char* names[]={"none","arguments","primitive-limit","capture-limit","byte-budget","view-read","view-mismatch",
        "declaration-read","declaration-bounds","position-format","stream-frequency",
        "vertex-buffer-read","index-buffer-read","vertex-description","index-description",
        "stride","index-format","index-range","index-lock","index-unlock","vertex-range","vertex-lock",
        "vertex-unlock","vertex-nonfinite","chunk-coverage"};
    const unsigned index=unsigned(reason);return index<sizeof(names)/sizeof(names[0])?names[index]:"unknown";
}
struct Diagnostics {
    RejectReason reason=RejectReason::None;
    HRESULT hr=D3D_OK;
    DWORD vertexUsage=0,indexUsage=0;
    UINT positionType=0,positionStream=0,stride=0;
    UINT firstIndex=0,lastIndex=0;
    unsigned viewComponent=0;
    float viewDelta=0,low[3]={},high[3]={};
    bool dynamicSnapshot=false,userPointerSnapshot=false,contentCacheHit=false,trackedCacheHit=false;
};
struct Limits {
    unsigned maxEntries=2048;
    unsigned maxTrianglesPerDraw=16384;
    std::uint64_t maxReadBytesPerFrame=16u*1024u*1024u;
};
template<class T> struct Ref {
    T* p=nullptr;
    ~Ref(){if(p)p->Release();}
    T* detach(){T* out=p;p=nullptr;return out;}
};

// ADT MCVT vertices are world XYZ already. Terrain shader c0..3 must be the
// independently validated camera view, without a per-object transform. Passing
// nullptr means the caller performed this exact gate outside this helper.
inline bool matchesWorldView(IDirect3DDevice9* device,const float* expected) {
    if(!expected)return true;
    float actual[16];
    if(FAILED(device->GetVertexShaderConstantF(0,actual,4)))return false;
    for(unsigned i=0;i<16;++i){
        const float tolerance=i>=12&&i<=14?.03f:.0003f;
        if(!std::isfinite(actual[i])||!std::isfinite(expected[i])||
           std::fabs(actual[i]-expected[i])>tolerance)return false;
    }
    return true;
}

// Shared pure coverage core. The accessor keeps indexed snapshots compact;
// expanded callers retain the original public API below.
template<class VertexAt>
inline bool classifyTriangleSequence(VertexAt vertexAt,std::size_t count,
                                     D3DPRIMITIVETYPE topology,Bounds& output) {
    output=Bounds{};
    if(count<3||(topology!=D3DPT_TRIANGLELIST&&topology!=D3DPT_TRIANGLESTRIP)||
       (topology==D3DPT_TRIANGLELIST&&count%3))return false;
    constexpr double origin=17066.666666666666,step=100.0/3.0;
    constexpr double tolerance=.03;
    const float infinity=std::numeric_limits<float>::infinity();
    Bounds result;for(unsigned j=0;j<3;++j){result.low[j]=infinity;result.high[j]=-infinity;}
    std::unordered_set<unsigned> seen;
    unsigned previousChunk=std::numeric_limits<unsigned>::max();
    const std::size_t triangles=topology==D3DPT_TRIANGLELIST?count/3:count-2;
    for(std::size_t triangle=0;triangle<triangles;++triangle){
        const std::size_t first=topology==D3DPT_TRIANGLELIST?triangle*3:triangle;
        const Position p[]={vertexAt(first),vertexAt(first+1),vertexAt(first+2)};
        for(unsigned i=0;i<3;++i)if(!std::isfinite(p[i].x)||!std::isfinite(p[i].y)||!std::isfinite(p[i].z))return false;
        const double area=(double(p[1].x)-p[0].x)*(double(p[2].y)-p[0].y)-
                          (double(p[1].y)-p[0].y)*(double(p[2].x)-p[0].x);
        if(std::fabs(area)<1e-9)continue; // Includes strip connectors.
        const double centerX=(double(p[0].x)+p[1].x+p[2].x)/3.;
        const double centerY=(double(p[0].y)+p[1].y+p[2].y)/3.;
        const double gridX=std::floor((origin-centerY)/step);
        const double gridY=std::floor((origin-centerX)/step);
        if(gridX<0||gridX>=1024||gridY<0||gridY>=1024)return false;
        const int x=int(gridX),y=int(gridY);
        const double minX=origin-(y+1)*step,maxX=origin-y*step;
        const double minY=origin-(x+1)*step,maxY=origin-x*step;
        for(unsigned i=0;i<3;++i){
            if(p[i].x<minX-tolerance||p[i].x>maxX+tolerance||
               p[i].y<minY-tolerance||p[i].y>maxY+tolerance)return false;
            const float values[]={p[i].x,p[i].y,p[i].z};
            for(unsigned j=0;j<3;++j){result.low[j]=std::min(result.low[j],values[j]);result.high[j]=std::max(result.high[j],values[j]);}
        }
        const unsigned chunk=unsigned(y)*1024+unsigned(x);
        // Chunk-local triangles are contiguous in normal terrain draws. Avoid
        // a hash lookup for every triangle without changing disjoint coverage.
        if(chunk!=previousChunk&&seen.insert(chunk).second)
            result.chunks.push_back({x,y,float(minX),float(minY),float(maxX),float(maxY)});
        previousChunk=chunk;
    }
    if(result.chunks.empty())return false;
    std::sort(result.chunks.begin(),result.chunks.end(),[](const ChunkBounds&a,const ChunkBounds&b){return a.y<b.y||(a.y==b.y&&a.x<b.x);});
    output=std::move(result);return true;
}

inline bool classifyTriangles(const Position* vertices,std::size_t count,
                              D3DPRIMITIVETYPE topology,Bounds& output) {
    if(!vertices){output=Bounds{};return false;}
    return classifyTriangleSequence([&](std::size_t i){return vertices[i];},count,topology,output);
}

template<class Index>
inline bool classifyIndexedBy(const Position* vertices,std::size_t vertexCount,
                                     const Index* indices,std::size_t indexCount,
                                     D3DPRIMITIVETYPE topology,Bounds& output) {
    output=Bounds{};
    if(!vertices||!indices||!vertexCount)return false;
    for(std::size_t i=0;i<indexCount;++i)if(indices[i]>=vertexCount)return false;
    return classifyTriangleSequence([&](std::size_t i){return vertices[indices[i]];},indexCount,topology,output);
}
inline bool classifyIndexedTriangles(const Position* vertices,std::size_t vertexCount,
                                     const std::uint32_t* indices,std::size_t indexCount,
                                     D3DPRIMITIVETYPE topology,Bounds& output) {
    return classifyIndexedBy(vertices,vertexCount,indices,indexCount,topology,output);
}

class FrameCache {
public:
    using IdentityFn=std::uint64_t(*)(void*,bool);
    using VersionFn=std::uint64_t(*)(void*,bool);
private:
    struct Stamp {
        std::uint64_t vb=0,ib=0,vertexVersion=0,indexVersion=0;
        bool valid()const{return vb&&ib&&vertexVersion&&indexVersion;}
        bool operator==(const Stamp& b)const{return vb==b.vb&&ib==b.ib&&vertexVersion==b.vertexVersion&&indexVersion==b.indexVersion;}
    };
    IdentityFn identity_=nullptr;VersionFn version_=nullptr;
    NorthlightCaptureMetadata::Read metadata_=nullptr;
    NorthlightVertexDeclarations::Cache* declarations_=nullptr;
    HRESULT declarationElements(IDirect3DVertexDeclaration9* declaration,D3DVERTEXELEMENT9* storage,
        UINT& count,const D3DVERTEXELEMENT9*& elements){
        elements=storage;
        if(declarations_)return declarations_->get(declaration,elements,count)?D3D_OK:D3DERR_INVALIDCALL;
        return declaration->GetDeclaration(storage,&count);
    }
    void metadata(IDirect3DVertexBuffer9* vb,IDirect3DIndexBuffer9* ib,NorthlightCaptureMetadata::Info* info){
        if(!metadata_||!identity_||!version_)return;
        const NorthlightCaptureMetadata::Request requests[]={{vb,false},{ib,true}};
        metadata_(requests,info,2);++maintenance_.metadataBatches;
    }
    Stamp stamp(IDirect3DVertexBuffer9* vb,IDirect3DIndexBuffer9* ib,const NorthlightCaptureMetadata::Info* info=nullptr){
        if(!identity_||!version_)return {};
        NorthlightCaptureMetadata::Info fresh[2];if(!info){metadata(vb,ib,fresh);info=fresh;}
        const bool v=info[0].known&&info[0].identity,i=info[1].known&&info[1].identity;
        Stamp s;s.vb=v?info[0].identity:identity_(vb,false);s.ib=i?info[1].identity:identity_(ib,true);
        s.vertexVersion=v?info[0].revision:version_(vb,false);s.indexVersion=i?info[1].revision:version_(ib,true);return s;
    }

    struct Key {
        IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
        UINT streamOffset=0,stride=0,positionOffset=0,positionType=0;
        INT base=0;UINT minimum=0,vertices=0,start=0,primitives=0;
        D3DPRIMITIVETYPE topology=D3DPT_TRIANGLELIST;
        bool operator==(const Key& b)const{
            return vb==b.vb&&ib==b.ib&&streamOffset==b.streamOffset&&stride==b.stride&&
                positionOffset==b.positionOffset&&positionType==b.positionType&&base==b.base&&
                minimum==b.minimum&&vertices==b.vertices&&start==b.start&&primitives==b.primitives&&topology==b.topology;
        }
    };
    struct ContentKey {
        Key draw;UINT stream=0;D3DFORMAT format=D3DFMT_INDEX16;UINT vertexBytes=0,indexBytes=0;
        bool operator==(const ContentKey& b)const{return draw==b.draw&&stream==b.stream&&format==b.format&&vertexBytes==b.vertexBytes&&indexBytes==b.indexBytes;}
    };
    struct SourceIndices {
        const std::vector<unsigned char> bytes;size_t entries=0;
        explicit SourceIndices(const std::vector<unsigned char>& input):bytes(input){}
    };
    static std::uint64_t sourceIndexBytes(const SourceIndices& source){return sizeof(SourceIndices)+32+source.bytes.capacity();}
    struct ContentEntry {
        ContentKey key;Stamp stamp;std::uint64_t hash=0,bytes=0,charged=0;size_t slot=0;
        NorthlightIntrusiveLRU::Links<ContentEntry> lru;
        UINT smallest=0,largest=0;
        float low[3]={},high[3]={};
        std::shared_ptr<SourceIndices> indices;std::vector<unsigned char> vertices;
        NorthlightTerrainIndices::Indices sourceOffsets; // Exact 16/32-bit byte offsets, in snapshot position order.
        std::shared_ptr<const MeshSnapshot> snapshot;
    };
    static constexpr std::uint64_t ContentLimit=32u*1024u*1024u;
    // Entry slots share the existing byte budget; small draws need not evict at 4096.
    static constexpr size_t ContentEntries=8192;
    std::array<std::unique_ptr<ContentEntry>,ContentEntries> content;
    std::array<size_t,ContentEntries> freeContent{};size_t freeContentCount=ContentEntries;
    std::unordered_multimap<std::uint64_t,ContentEntry*> contentLookup;
    NorthlightIntrusiveLRU::List<ContentEntry> contentLRU;
    // A weak one-block memo shares exact consecutive index patterns without retaining buffers
    // or an unbounded interning table. Entry references, not temporary shared_ptrs, own its charge.
    std::weak_ptr<SourceIndices> lastSourceIndices;
    // An expired weak memo can keep one make_shared control/object allocation until reassignment.
    static constexpr size_t ContentBaseBytes=sizeof(content)+sizeof(freeContent)+sizeof(lastSourceIndices)+sizeof(SourceIndices)+32;
    size_t sourceIndexBlocks_=0;std::uint64_t sourceIndexDataBytes_=0,sourceIndexLogicalBytes_=0;
    std::uint64_t contentBytes=ContentBaseBytes,contentHits=0,trackedHits_=0,avoidedReadBytes_=0;
    std::uint64_t contentLimit=ContentLimit; /* lowered under address-space pressure */
public:
    struct Maintenance {
        unsigned lookupMisses=0,evictions=0,countPressure=0,bytePressure=0;
        unsigned trackedHits=0,unproven=0,lifetimeChanged=0,vertexChanged=0,indexChanged=0,unstable=0,exactReuses=0,rebuilds=0,indexTokenHits=0;
        unsigned metadataBatches=0,metadataHits=0,metadataFallbacks=0;
    };
private:
    Maintenance maintenance_;
    static std::uint64_t contentHash(const ContentKey& k){
        std::uint64_t h=1469598103934665603ull;
        const auto mix=[&](std::uint64_t v){h^=v;h*=1099511628211ull;};
        mix(reinterpret_cast<std::uintptr_t>(k.draw.vb));mix(reinterpret_cast<std::uintptr_t>(k.draw.ib));
        mix(k.draw.streamOffset);mix(k.draw.stride);mix(k.draw.positionOffset);mix(k.draw.positionType);
        mix(std::uint32_t(k.draw.base));mix(k.draw.minimum);mix(k.draw.vertices);mix(k.draw.start);mix(k.draw.primitives);
        mix(k.draw.topology);mix(k.stream);mix(k.format);mix(k.vertexBytes);mix(k.indexBytes);return h;
    }
    ContentEntry* findContent(const ContentKey& key,std::uint64_t hash){
        const auto range=contentLookup.equal_range(hash);
        for(auto it=range.first;it!=range.second;++it)if(it->second->key==key)return it->second;
        ++maintenance_.lookupMisses;return nullptr;
    }
    void eraseContent(ContentEntry& entry){
        const auto range=contentLookup.equal_range(entry.hash);
        for(auto it=range.first;it!=range.second;++it)if(it->second==&entry){contentLookup.erase(it);break;}
        const auto slot=entry.slot;contentBytes-=entry.bytes;contentLRU.remove(entry);
        sourceIndexLogicalBytes_-=entry.indices->bytes.capacity();
        if(!--entry.indices->entries){contentBytes-=sourceIndexBytes(*entry.indices);sourceIndexDataBytes_-=entry.indices->bytes.capacity();--sourceIndexBlocks_;}
        content[slot].reset();freeContent[freeContentCount++]=slot;
    }
    void rememberContent(const ContentKey& key,std::uint64_t hash,UINT smallest,UINT largest,
                         const std::vector<unsigned char>& rawIndices,const std::vector<unsigned char>& rawVertices,
                         const std::vector<UINT>& sourceOffsets,
                         const std::shared_ptr<const MeshSnapshot>& snapshot,const Diagnostics& detail,Stamp verified){
      try {
        // Keep exact indices and a mapping to the captured XYZ for revalidation
        // after buffer writes. The other vertex attributes are not used here.
        const bool keepRaw=!verified.valid();
        const std::uint64_t maximumOffset=std::uint64_t(largest-smallest)*key.draw.stride;
        const unsigned offsetBytes=maximumOffset<=UINT16_MAX?2:4;
        auto sharedIndices=lastSourceIndices.lock();
        if(sharedIndices&&sharedIndices->bytes!=rawIndices)sharedIndices.reset();
        std::uint64_t sourceBytes=sharedIndices?sourceIndexBytes(*sharedIndices):sizeof(SourceIndices)+32+rawIndices.size();
        const auto required=[&](std::uint64_t bytes){return bytes+(!sharedIndices||!sharedIndices->entries?sourceBytes:0);};
        const std::uint64_t estimate=sizeof(ContentEntry)+sizeof(MeshSnapshot)+256+
            (keepRaw?rawVertices.size():sourceOffsets.size()*offsetBytes)+
            snapshot->positions.capacity()*sizeof(Position)+snapshot->indices.allocatedBytes()+snapshot->bounds.chunks.capacity()*sizeof(ChunkBounds);
        if(required(estimate)>contentLimit-ContentBaseBytes)return;
        const auto range=contentLookup.equal_range(hash);
        for(auto it=range.first;it!=range.second;++it)if(it->second->key==key){eraseContent(*it->second);break;}
        const auto makeRoom=[&](std::uint64_t bytes){
            // Evicting the last entry may turn a shared block into a newly charged block.
            // Recompute on every iteration; a temporary owner is not a retained cache entry.
            while(contentBytes+required(bytes)>contentLimit||!freeContentCount){
                auto* oldest=contentLRU.oldest();if(!oldest)return false;
                maintenance_.countPressure+=!freeContentCount;
                maintenance_.bytePressure+=contentBytes+required(bytes)>contentLimit;
                ++maintenance_.evictions;eraseContent(*oldest);
            }return true;
        };
        // Evict before allocation, so replacement does not transiently double
        // the retained cache. Accounting includes object/capacity/node allowance.
        if(!makeRoom(estimate))return;
        if(!sharedIndices){sharedIndices=std::make_shared<SourceIndices>(rawIndices);sourceBytes=sourceIndexBytes(*sharedIndices);}
        auto next=std::make_unique<ContentEntry>();next->key=key;next->stamp=verified;next->hash=hash;next->smallest=smallest;next->largest=largest;
        next->charged=rawIndices.size()+rawVertices.size();
        next->indices=sharedIndices;
        if(keepRaw)next->vertices=rawVertices;
        else{
            next->sourceOffsets.reserve(sourceOffsets.size(),UINT(maximumOffset));
            for(UINT offset:sourceOffsets){
                const std::uint64_t byte=std::uint64_t(offset)*key.draw.stride;
                if(byte+sizeof(Position)>rawVertices.size())return;
                next->sourceOffsets.push_back(UINT(byte));
            }
        }
        next->snapshot=snapshot;
        std::memcpy(next->low,detail.low,sizeof next->low);std::memcpy(next->high,detail.high,sizeof next->high);
        next->bytes=sizeof(ContentEntry)+sizeof(MeshSnapshot)+256+next->vertices.capacity()+next->sourceOffsets.allocatedBytes()+
            next->snapshot->positions.capacity()*sizeof(Position)+next->snapshot->indices.allocatedBytes()+
            next->snapshot->bounds.chunks.capacity()*sizeof(ChunkBounds);
        if(required(next->bytes)>contentLimit-ContentBaseBytes||!makeRoom(next->bytes))return;
        // Insert the optional hash node before publishing ownership/links. An
        // allocation failure must leave the computed snapshot valid and the
        // free-slot/LRU invariants intact. The per-entry allowance includes
        // hash-node and amortized bucket storage.
        const auto slot=freeContent[freeContentCount-1];next->slot=slot;
        contentLookup.emplace(hash,next.get());--freeContentCount;
        sourceIndexLogicalBytes_+=sharedIndices->bytes.capacity();
        if(!sharedIndices->entries++){contentBytes+=sourceBytes;sourceIndexDataBytes_+=sharedIndices->bytes.capacity();++sourceIndexBlocks_;}
        lastSourceIndices=sharedIndices;
        contentLRU.append(*next);contentBytes+=next->bytes;content[slot]=std::move(next);
      }catch(...){return;} // Cache allocation is optional; snapshot remains valid.
    }
    Limits limits;
    // Scratch retains capacity only. Misses overwrite all decoded indices and
    // reset the remap range; hits verify live indices and referenced positions.
    std::vector<std::uint32_t> scratchIndices,scratchRemap,scratchSourceOffsets;
    std::vector<unsigned char> scratchRawIndices,scratchRawVertices;
    std::uint64_t readBytes=0,chargedBytes=0;
    unsigned snapshots=0;
public:
    explicit FrameCache(Limits configured=Limits{}):limits(configured){for(size_t i=0;i<freeContent.size();++i)freeContent[i]=i;}
    FrameCache(const FrameCache&)=delete;
    FrameCache& operator=(const FrameCache&)=delete;
    ~FrameCache(){clearFrame();}
    // REQUIRED every Present/Reset, including frames where effects are disabled.
    // Persistent content entries retain no resources.
    // Tracked lifetimes/revisions prove fast hits; all other
    // entries require full live-byte verification next frame.
    void clearFrame(){
        readBytes=chargedBytes=0;snapshots=0;maintenance_={};
    }
    std::size_t entryCount()const{return snapshots;}
    std::uint64_t bytesRead()const{return readBytes;}
    // Call at device Reset; Present clears only per-frame budgets. Every cache
    // untracked hit verifies current raw buffer bytes, without retaining any lock
    // or COM resource reference. Shared snapshots are immutable and remain valid
    // if callers retain them across eviction/reset. The cache budget accounts
    // for cache-owned storage; callers must separately bound retained outputs.
    void clearPersistent(){contentLookup.clear();contentLRU.clear();lastSourceIndices.reset();for(auto& entry:content)entry.reset();sourceIndexBlocks_=0;sourceIndexDataBytes_=sourceIndexLogicalBytes_=0;contentBytes=ContentBaseBytes;contentHits=trackedHits_=avoidedReadBytes_=0;freeContentCount=ContentEntries;for(size_t i=0;i<freeContent.size();++i)freeContent[i]=i;maintenance_={};}
    // Memory guard: cap in [ContentBaseBytes+1 MiB, 32 MiB]; evicts oldest down to it now.
    void setPersistentByteLimit(std::uint64_t bytes){
        contentLimit=std::max<std::uint64_t>(ContentBaseBytes+1024u*1024u,std::min<std::uint64_t>(bytes,ContentLimit));
        while(contentBytes>contentLimit){auto* oldest=contentLRU.oldest();if(!oldest)break;eraseContent(*oldest);}
    }
    std::uint64_t persistentByteLimit()const{return contentLimit;}
    size_t persistentEntries()const{return ContentEntries-freeContentCount;}
    static constexpr size_t persistentEntryLimit(){return ContentEntries;}
    const Maintenance& maintenance()const{return maintenance_;}
    std::uint64_t persistentBytes()const{return contentBytes;}
    struct IndexStorage {size_t compact=0,wide=0,sourceBlocks=0,offsetCompact=0,offsetWide=0;std::uint64_t bytes=0,avoided=0,sourceBytes=0,sourceAvoided=0,offsetBytes=0,offsetAvoided=0;};
    // Sampled diagnostics only; never walk the persistent cache in a draw hook.
    IndexStorage indexStorage()const{
        IndexStorage stats;stats.sourceBlocks=sourceIndexBlocks_;stats.sourceBytes=sourceIndexDataBytes_;stats.sourceAvoided=sourceIndexLogicalBytes_-sourceIndexDataBytes_;
        for(const auto& entry:content)if(entry){const auto& indices=entry->snapshot->indices;const auto bytes=indices.allocatedBytes();
            const bool compact=indices.visit([](const auto* p,size_t){return sizeof(*p)==2;});
            stats.compact+=compact;stats.wide+=!compact;stats.bytes+=bytes;stats.avoided+=compact?bytes:0;
            const auto& offsets=entry->sourceOffsets;if(!offsets.empty()){
                const bool small=offsets.visit([](const auto* p,size_t){return sizeof(*p)==2;});
                const auto allocated=offsets.allocatedBytes();stats.offsetCompact+=small;stats.offsetWide+=!small;stats.offsetBytes+=allocated;stats.offsetAvoided+=small?allocated:0;}}
        return stats;
    }
    std::uint64_t persistentHits()const{return contentHits;}
    std::uint64_t trackedHits()const{return trackedHits_;}
    std::uint64_t avoidedReadBytes()const{return avoidedReadBytes_;}
    void setDeclarationCache(NorthlightVertexDeclarations::Cache* cache){declarations_=cache;}
    void setIdentityProvider(IdentityFn f){clearPersistent();identity_=f;}
    void setVersionProvider(VersionFn f){clearPersistent();version_=f;}
    // The batch must use the same lifetime/revision domain as the legacy providers.
    // Consume it only for this draw; the post-read stamp takes a fresh batch.
    void setMetadataProvider(NorthlightCaptureMetadata::Read f){clearPersistent();metadata_=f;}

    // pVertexStreamZeroData points at vertex ZERO, not MinVertexIndex. Verified
    // against the installed D9VK DrawIndexedPrimitiveUP: it copies
    // (MinVertexIndex + NumVertices) vertices and draws with baseVertex = 0.
    // Caller invokes this while the original API's supplied pointers are valid.
    bool readMeshUP(IDirect3DDevice9* device,D3DPRIMITIVETYPE topology,
                    UINT minimum,UINT vertexCount,UINT primitiveCount,
                    const void* indexData,D3DFORMAT indexFormat,
                    const void* vertexData,UINT stride,
                    const float* expectedWorldToView,MeshSnapshot& output,
                    Diagnostics* diagnostics=nullptr){
        output=MeshSnapshot{};Diagnostics detail;detail.userPointerSnapshot=true;detail.stride=stride;
        auto reject=[&](RejectReason reason,HRESULT hr=D3DERR_INVALIDCALL){
            detail.reason=reason;detail.hr=hr;if(diagnostics)*diagnostics=detail;return false;
        };
        if(!device||!indexData||!vertexData||!primitiveCount||!vertexCount||
           (topology!=D3DPT_TRIANGLELIST&&topology!=D3DPT_TRIANGLESTRIP))return reject(RejectReason::Arguments);
        if(primitiveCount>limits.maxTrianglesPerDraw)return reject(RejectReason::PrimitiveLimit);
        if(snapshots>=limits.maxEntries)return reject(RejectReason::CaptureLimit);
        if(indexFormat!=D3DFMT_INDEX16&&indexFormat!=D3DFMT_INDEX32)return reject(RejectReason::IndexFormat);
        if(expectedWorldToView){
            float actual[16];HRESULT hr=device->GetVertexShaderConstantF(0,actual,4);
            if(FAILED(hr))return reject(RejectReason::ViewRead,hr);
            bool valid=true;
            for(unsigned i=0;i<16;++i){
                const float difference=std::fabs(actual[i]-expectedWorldToView[i]);
                if(difference>detail.viewDelta){detail.viewDelta=difference;detail.viewComponent=i;}
                if(!std::isfinite(actual[i])||!std::isfinite(expectedWorldToView[i])||
                   difference>(i>=12&&i<=14?.03f:.0003f))valid=false;
            }
            if(!valid)return reject(RejectReason::ViewMismatch);
        }
        Ref<IDirect3DVertexDeclaration9> declaration;
        HRESULT hr=device->GetVertexDeclaration(&declaration.p);
        if(FAILED(hr)||!declaration.p)return reject(RejectReason::DeclarationRead,hr);
        D3DVERTEXELEMENT9 localElements[MAXD3DDECLLENGTH+1];const D3DVERTEXELEMENT9* elements=nullptr;UINT elementCount=MAXD3DDECLLENGTH+1;
        hr=declarationElements(declaration.p,localElements,elementCount,elements);
        if(FAILED(hr)||elementCount>MAXD3DDECLLENGTH+1)return reject(RejectReason::DeclarationBounds,hr);
        const D3DVERTEXELEMENT9* position=nullptr;
        for(UINT i=0;i<elementCount&&elements[i].Stream!=0xff;++i)
            if(elements[i].Usage==D3DDECLUSAGE_POSITION&&elements[i].UsageIndex==0){position=&elements[i];break;}
        if(!position)return reject(RejectReason::PositionFormat);
        detail.positionType=position->Type;detail.positionStream=position->Stream;
        if(position->Stream!=0||position->Method!=D3DDECLMETHOD_DEFAULT||
           (position->Type!=D3DDECLTYPE_FLOAT3&&position->Type!=D3DDECLTYPE_FLOAT4))return reject(RejectReason::PositionFormat);
        const unsigned componentBytes=position->Type==D3DDECLTYPE_FLOAT4?16:12;
        if(!stride||unsigned(position->Offset)+componentBytes>stride)return reject(RejectReason::Stride);
        const std::uint64_t indexCount=topology==D3DPT_TRIANGLELIST?std::uint64_t(primitiveCount)*3:std::uint64_t(primitiveCount)+2;
        const unsigned indexSize=indexFormat==D3DFMT_INDEX16?2:4;
        const std::uint64_t indexBytes=indexCount*indexSize;
        const std::uintptr_t maxAddress=std::numeric_limits<std::uintptr_t>::max();
        if(indexBytes>maxAddress-reinterpret_cast<std::uintptr_t>(indexData))return reject(RejectReason::IndexRange);
        if(chargedBytes+indexBytes>limits.maxReadBytesPerFrame)return reject(RejectReason::ByteBudget);
        auto& indices=scratchIndices;indices.resize(static_cast<std::size_t>(indexCount));
        UINT smallest=std::numeric_limits<UINT>::max(),largest=0;
        chargedBytes+=indexBytes;readBytes+=indexBytes;
        for(std::size_t i=0;i<indices.size();++i){
            if(indexSize==2){std::uint16_t value;std::memcpy(&value,(const char*)indexData+i*2,2);indices[i]=value;}
            else std::memcpy(&indices[i],(const char*)indexData+i*4,4);
            if(indices[i]<minimum||std::uint64_t(indices[i])>=std::uint64_t(minimum)+vertexCount)return reject(RejectReason::IndexRange);
            smallest=std::min(smallest,indices[i]);largest=std::max(largest,indices[i]);
        }
        detail.firstIndex=smallest;detail.lastIndex=largest;
        const std::uint64_t end=std::uint64_t(largest)*stride+position->Offset+componentBytes;
        const std::uint64_t span=std::uint64_t(largest-smallest)*stride+componentBytes;
        if(end>maxAddress-reinterpret_cast<std::uintptr_t>(vertexData))return reject(RejectReason::VertexRange);
        if(chargedBytes+span>limits.maxReadBytesPerFrame)return reject(RejectReason::ByteBudget);
        chargedBytes+=span;readBytes+=span;
        const UINT unused=std::numeric_limits<UINT>::max();
        auto& remap=scratchRemap;remap.assign(std::size_t(largest)-smallest+1,unused);
        MeshSnapshot result;result.positions.resize(std::min(indices.size(),remap.size()));UINT unique=0;
        const float infinity=std::numeric_limits<float>::infinity();
        for(unsigned j=0;j<3;++j){detail.low[j]=infinity;detail.high[j]=-infinity;}
        for(auto& index:indices){
            UINT& mapped=remap[index-smallest];
            if(mapped==unused){
                mapped=unique++;Position& p=result.positions[mapped];
                std::memcpy(&p,(const char*)vertexData+std::size_t(index)*stride+position->Offset,12);
                const float values[]={p.x,p.y,p.z};
                for(unsigned j=0;j<3;++j){
                    if(!std::isfinite(values[j]))return reject(RejectReason::VertexNonfinite);
                    detail.low[j]=std::min(detail.low[j],values[j]);detail.high[j]=std::max(detail.high[j],values[j]);
                }
            }
            index=mapped;
        }
        result.positions.resize(unique);result.indices.reserve(std::size_t(primitiveCount)*3,unique?unique-1:0);
        for(UINT primitive=0;primitive<primitiveCount;++primitive){
            const std::size_t first=topology==D3DPT_TRIANGLELIST?std::size_t(primitive)*3:primitive;
            UINT a=indices[first],b=indices[first+1],c=indices[first+2];
            if(a==b||b==c||c==a)continue;
            if(topology==D3DPT_TRIANGLESTRIP&&(primitive&1))std::swap(a,b);
            result.indices.push_back(a);result.indices.push_back(b);result.indices.push_back(c);
        }
        if(!result.indices.visit([&](const auto* data,std::size_t count){return classifyIndexedBy(result.positions.data(),result.positions.size(),data,count,D3DPT_TRIANGLELIST,result.bounds);}))return reject(RejectReason::ChunkCoverage);
        ++snapshots;output=std::move(result);if(diagnostics)*diagnostics=detail;return true;
    }

    bool readPrimitiveUP(IDirect3DDevice9* device,D3DPRIMITIVETYPE topology,
                         UINT primitiveCount,const void* vertexData,UINT stride,
                         const float* expectedWorldToView,MeshSnapshot& output,
                         Diagnostics* diagnostics=nullptr){
        output=MeshSnapshot{};
        if(!primitiveCount||primitiveCount>limits.maxTrianglesPerDraw||
           (topology!=D3DPT_TRIANGLELIST&&topology!=D3DPT_TRIANGLESTRIP)){
            if(diagnostics){*diagnostics=Diagnostics{};diagnostics->reason=primitiveCount>limits.maxTrianglesPerDraw?RejectReason::PrimitiveLimit:RejectReason::Arguments;diagnostics->hr=D3DERR_INVALIDCALL;diagnostics->userPointerSnapshot=true;}
            return false;
        }
        const UINT count=topology==D3DPT_TRIANGLELIST?primitiveCount*3:primitiveCount+2;
        std::vector<UINT> indices(count);for(UINT i=0;i<count;++i)indices[i]=i;
        return readMeshUP(device,topology,0,count,primitiveCount,indices.data(),D3DFMT_INDEX32,
                          vertexData,stride,expectedWorldToView,output,diagnostics);
    }

    // Snapshot immediately before the original draw. DYNAMIC/WRITEONLY is not
    // a rejection: the verified D9VK backend exposes its current CPU mapping
    // for READONLY locks. Cached results require exact indices and positions and
    // the full draw/layout contract; pointer identity alone is never trusted.
    // Returned immutable snapshots cannot change after later DISCARD/NOOVERWRITE
    // writes. Cache hits share the object without copying its geometry.
    // This path does not require a patched game vertex shader and supports SM1
    // Terrain too, after the caller matches its original audited shader hash.
    bool readMeshShared(IDirect3DDevice9* device,D3DPRIMITIVETYPE topology,INT base,
                  UINT minimum,UINT vertexCount,UINT start,UINT primitiveCount,
                  const float* expectedWorldToView,std::shared_ptr<const MeshSnapshot>& output,
                  Diagnostics* diagnostics=nullptr){
        output.reset();Diagnostics detail;
        auto reject=[&](RejectReason reason,HRESULT hr=D3DERR_INVALIDCALL){
            detail.reason=reason;detail.hr=hr;if(diagnostics)*diagnostics=detail;return false;
        };
        if(!device||!primitiveCount||!vertexCount||
           (topology!=D3DPT_TRIANGLELIST&&topology!=D3DPT_TRIANGLESTRIP))return reject(RejectReason::Arguments);
        if(primitiveCount>limits.maxTrianglesPerDraw)return reject(RejectReason::PrimitiveLimit);
        if(snapshots>=limits.maxEntries)return reject(RejectReason::CaptureLimit);
        HRESULT hr=D3D_OK;
        if(expectedWorldToView){
            float actual[16];hr=device->GetVertexShaderConstantF(0,actual,4);
            if(FAILED(hr))return reject(RejectReason::ViewRead,hr);
            bool valid=true;
            for(unsigned i=0;i<16;++i){
                const float difference=std::fabs(actual[i]-expectedWorldToView[i]);
                if(difference>detail.viewDelta){detail.viewDelta=difference;detail.viewComponent=i;}
                if(!std::isfinite(actual[i])||!std::isfinite(expectedWorldToView[i])||
                   difference>(i>=12&&i<=14?.03f:.0003f))valid=false;
            }
            if(!valid)return reject(RejectReason::ViewMismatch);
        }
        Ref<IDirect3DVertexDeclaration9> declaration;
        hr=device->GetVertexDeclaration(&declaration.p);
        if(FAILED(hr)||!declaration.p)return reject(RejectReason::DeclarationRead,hr);
        D3DVERTEXELEMENT9 localElements[MAXD3DDECLLENGTH+1];const D3DVERTEXELEMENT9* elements=nullptr;UINT elementCount=MAXD3DDECLLENGTH+1;
        hr=declarationElements(declaration.p,localElements,elementCount,elements);
        if(FAILED(hr)||elementCount>MAXD3DDECLLENGTH+1)return reject(RejectReason::DeclarationBounds,hr);
        const D3DVERTEXELEMENT9* position=nullptr;
        for(UINT i=0;i<elementCount&&elements[i].Stream!=0xff;++i)
            if(elements[i].Usage==D3DDECLUSAGE_POSITION&&elements[i].UsageIndex==0){position=&elements[i];break;}
        if(!position)return reject(RejectReason::PositionFormat);
        detail.positionType=position->Type;detail.positionStream=position->Stream;
        if(position->Method!=D3DDECLMETHOD_DEFAULT||
           (position->Type!=D3DDECLTYPE_FLOAT3&&position->Type!=D3DDECLTYPE_FLOAT4))return reject(RejectReason::PositionFormat);
        UINT frequency=0;hr=device->GetStreamSourceFreq(position->Stream,&frequency);
        if(FAILED(hr)||frequency!=1)return reject(RejectReason::StreamFrequency,hr);
        Ref<IDirect3DVertexBuffer9> vb;Ref<IDirect3DIndexBuffer9> ib;UINT streamOffset=0;
        hr=device->GetStreamSource(position->Stream,&vb.p,&streamOffset,&detail.stride);
        if(FAILED(hr)||!vb.p)return reject(RejectReason::VertexBufferRead,hr);
        hr=device->GetIndices(&ib.p);
        if(FAILED(hr)||!ib.p)return reject(RejectReason::IndexBufferRead,hr);
        const unsigned componentBytes=position->Type==D3DDECLTYPE_FLOAT4?16:12;
        if(!detail.stride||unsigned(position->Offset)+componentBytes>detail.stride)return reject(RejectReason::Stride);
        D3DVERTEXBUFFER_DESC vd={};D3DINDEXBUFFER_DESC id={};
        NorthlightCaptureMetadata::Info info[2];metadata(vb.p,ib.p,info);
        if(info[0].known&&info[0].identity){vd.Size=info[0].size;vd.Usage=info[0].usage;++maintenance_.metadataHits;}
        else{++maintenance_.metadataFallbacks;hr=vb.p->GetDesc(&vd);if(FAILED(hr))return reject(RejectReason::VertexDescription,hr);}
        if(info[1].known&&info[1].identity){id.Size=info[1].size;id.Usage=info[1].usage;id.Format=info[1].format;++maintenance_.metadataHits;}
        else{++maintenance_.metadataFallbacks;hr=ib.p->GetDesc(&id);if(FAILED(hr))return reject(RejectReason::IndexDescription,hr);}
        detail.vertexUsage=vd.Usage;detail.indexUsage=id.Usage;
        detail.dynamicSnapshot=((vd.Usage|id.Usage)&D3DUSAGE_DYNAMIC)!=0;
        if(id.Format!=D3DFMT_INDEX16&&id.Format!=D3DFMT_INDEX32)return reject(RejectReason::IndexFormat);
        const UINT indexSize=id.Format==D3DFMT_INDEX16?2:4;
        const std::uint64_t indexCount=topology==D3DPT_TRIANGLELIST?std::uint64_t(primitiveCount)*3:std::uint64_t(primitiveCount)+2;
        const std::uint64_t indexOffset=std::uint64_t(start)*indexSize,indexBytes=indexCount*indexSize;
        if(indexOffset+indexBytes>id.Size)return reject(RejectReason::IndexRange);
        ContentKey contentKey{{vb.p,ib.p,streamOffset,detail.stride,position->Offset,position->Type,base,minimum,vertexCount,start,primitiveCount,topology},position->Stream,id.Format,vd.Size,id.Size};
        const auto hash=contentHash(contentKey);ContentEntry* cached=findContent(contentKey,hash);
        const Stamp before=stamp(vb.p,ib.p,info);
        if(cached){
            if(!before.valid()||!cached->stamp.valid())++maintenance_.unproven;
            else if(before.vb!=cached->stamp.vb||before.ib!=cached->stamp.ib)++maintenance_.lifetimeChanged;
            else{
                maintenance_.vertexChanged+=before.vertexVersion!=cached->stamp.vertexVersion;
                maintenance_.indexChanged+=before.indexVersion!=cached->stamp.indexVersion;
            }
        }
        if(cached&&before.valid()&&cached->stamp==before){
            const std::uint64_t charged=cached->charged;
            if(charged>limits.maxReadBytesPerFrame-chargedBytes)return reject(RejectReason::ByteBudget);
            // No Lock, memcpy or memcmp: the intercepted buffer lifetime/write
            // tokens are proof that both exact live byte ranges are unchanged.
            // A pending/failed lock yields version 0 and must take slow fallback.
            if(stamp(vb.p,ib.p)==before){
                output=cached->snapshot;detail.contentCacheHit=detail.trackedCacheHit=true;
                detail.firstIndex=cached->smallest;detail.lastIndex=cached->largest;
                std::memcpy(detail.low,cached->low,sizeof detail.low);std::memcpy(detail.high,cached->high,sizeof detail.high);
                chargedBytes+=charged;avoidedReadBytes_+=charged;++trackedHits_;++contentHits;++snapshots;contentLRU.touch(*cached);
                ++maintenance_.trackedHits;
                if(diagnostics)*diagnostics=detail;return true;
            }
            ++maintenance_.unstable;
        }
        if(chargedBytes+indexBytes>limits.maxReadBytesPerFrame)return reject(RejectReason::ByteBudget);
        auto& indices=scratchIndices;
        // A vertex write does not invalidate the independently tracked index buffer.
        // Unknown/pending revisions still require the complete byte verification.
        bool sameIndices=cached&&before.valid()&&cached->stamp.valid()&&
            before.ib==cached->stamp.ib&&before.indexVersion==cached->stamp.indexVersion;
        void* raw=nullptr;
        if(sameIndices){chargedBytes+=indexBytes;avoidedReadBytes_+=indexBytes;++maintenance_.indexTokenHits;}
        else{
            // Capacity may allocate; do it before acquiring a real D3D lock.
            scratchRawIndices.reserve(size_t(indexBytes));
            NorthlightReplayCopies::Reader<IDirect3DIndexBuffer9> readIndices(ib.p);hr=readIndices.lock(UINT(indexOffset),UINT(indexBytes),&raw);
            if(FAILED(hr))return reject(RejectReason::IndexLock,hr);
            if(!raw){readIndices.unlock();return reject(RejectReason::IndexLock,E_POINTER);}
            chargedBytes+=indexBytes;readBytes+=indexBytes;
            sameIndices=cached&&cached->indices->bytes.size()==indexBytes&&!std::memcmp(raw,cached->indices->bytes.data(),size_t(indexBytes));
            if(!sameIndices){scratchRawIndices.resize(size_t(indexBytes));std::memcpy(scratchRawIndices.data(),raw,size_t(indexBytes));}
            hr=readIndices.unlock();
            if(FAILED(hr))return reject(RejectReason::IndexUnlock,hr);
        }
        UINT smallest=std::numeric_limits<UINT>::max(),largest=0;bool valid=true;
        const auto decodeIndices=[&](){
            indices.resize(static_cast<std::size_t>(indexCount));
            for(std::size_t i=0;i<indices.size();++i){
                if(indexSize==2){std::uint16_t value;std::memcpy(&value,scratchRawIndices.data()+i*2,2);indices[i]=value;}
                else std::memcpy(&indices[i],scratchRawIndices.data()+i*4,4);
                if(indices[i]<minimum||std::uint64_t(indices[i])>=std::uint64_t(minimum)+vertexCount)valid=false;
                smallest=std::min(smallest,indices[i]);largest=std::max(largest,indices[i]);
            }
        };
        if(sameIndices){smallest=cached->smallest;largest=cached->largest;}else decodeIndices();
        detail.firstIndex=smallest;detail.lastIndex=largest;
        if(!valid)return reject(RejectReason::IndexRange);
        const std::int64_t firstVertex=std::int64_t(base)+smallest,lastVertex=std::int64_t(base)+largest;
        if(firstVertex<0||lastVertex<firstVertex)return reject(RejectReason::VertexRange);
        const std::uint64_t firstByte=std::uint64_t(streamOffset)+std::uint64_t(firstVertex)*detail.stride+position->Offset;
        const std::uint64_t lastByte=std::uint64_t(streamOffset)+std::uint64_t(lastVertex)*detail.stride+position->Offset+componentBytes;
        const std::uint64_t span=lastByte-firstByte;
        if(lastByte>vd.Size||!span)return reject(RejectReason::VertexRange);
        if(chargedBytes+span>limits.maxReadBytesPerFrame)return reject(RejectReason::ByteBudget);
        scratchRawVertices.reserve(size_t(span)); // resize below cannot allocate while locked
        raw=nullptr;NorthlightReplayCopies::Reader<IDirect3DVertexBuffer9> readVertices(vb.p);hr=readVertices.lock(UINT(firstByte),UINT(span),&raw);
        if(FAILED(hr))return reject(RejectReason::VertexLock,hr);
        if(!raw){readVertices.unlock();return reject(RejectReason::VertexLock,E_POINTER);}
        chargedBytes+=span;readBytes+=span;
        bool sameVertices=sameIndices&&cached->vertices.size()==span&&!std::memcmp(raw,cached->vertices.data(),size_t(span));
        if(sameIndices&&!sameVertices&&!cached->sourceOffsets.empty()&&cached->sourceOffsets.size()==cached->snapshot->positions.size()){
            sameVertices=true;
            for(size_t i=0;i<cached->sourceOffsets.size();++i){
                const auto offset=cached->sourceOffsets[i];
                if(std::memcmp(static_cast<const unsigned char*>(raw)+offset,&cached->snapshot->positions[i],sizeof(Position))){sameVertices=false;break;}
            }
        }
        if(!sameVertices){scratchRawVertices.resize(size_t(span));std::memcpy(scratchRawVertices.data(),raw,size_t(span));}
        hr=readVertices.unlock();if(FAILED(hr))return reject(RejectReason::VertexUnlock,hr);
        if(sameVertices){
            // Exact live indices and every referenced XYZ matched; locks released.
            // Only const ownership escapes; eviction or later buffer mutation
            // cannot invalidate a previously returned snapshot.
            output=cached->snapshot;detail.contentCacheHit=true;++maintenance_.exactReuses;
            const Stamp after=stamp(vb.p,ib.p);cached->stamp=before.valid()&&after==before?before:Stamp{};
            std::memcpy(detail.low,cached->low,sizeof detail.low);std::memcpy(detail.high,cached->high,sizeof detail.high);
            contentLRU.touch(*cached);++contentHits;++snapshots;
            if(diagnostics)*diagnostics=detail;return true;
        }
        if(sameIndices){scratchRawIndices.resize(size_t(indexBytes));std::memcpy(scratchRawIndices.data(),cached->indices->bytes.data(),size_t(indexBytes));decodeIndices();}
        const UINT unused=std::numeric_limits<UINT>::max();
        auto& remap=scratchRemap;remap.assign(std::size_t(largest)-smallest+1,unused);
        MeshSnapshot result;result.positions.resize(std::min(indices.size(),remap.size()));UINT unique=0;
        scratchSourceOffsets.clear();scratchSourceOffsets.reserve(result.positions.size());
        const float infinity=std::numeric_limits<float>::infinity();
        for(unsigned j=0;j<3;++j){detail.low[j]=infinity;detail.high[j]=-infinity;}
        for(auto& index:indices){
            UINT& mapped=remap[index-smallest];
            if(mapped==unused){
                mapped=unique++;Position& p=result.positions[mapped];
                scratchSourceOffsets.push_back(index-smallest);
                std::memcpy(&p,scratchRawVertices.data()+std::size_t(index-smallest)*detail.stride,12);
                const float values[]={p.x,p.y,p.z};
                for(unsigned j=0;j<3;++j){
                    if(!std::isfinite(values[j]))valid=false;
                    detail.low[j]=std::min(detail.low[j],values[j]);detail.high[j]=std::max(detail.high[j],values[j]);
                }
            }
            index=mapped;
        }
        if(!valid)return reject(RejectReason::VertexNonfinite);
        result.positions.resize(unique);result.indices.reserve(std::size_t(primitiveCount)*3,unique?unique-1:0);
        for(UINT primitive=0;primitive<primitiveCount;++primitive){
            const std::size_t first=topology==D3DPT_TRIANGLELIST?std::size_t(primitive)*3:primitive;
            UINT a=indices[first],b=indices[first+1],c=indices[first+2];
            if(a==b||b==c||c==a)continue;
            if(topology==D3DPT_TRIANGLESTRIP&&(primitive&1))std::swap(a,b);
            result.indices.push_back(a);result.indices.push_back(b);result.indices.push_back(c);
        }
        if(!result.indices.visit([&](const auto* data,std::size_t count){return classifyIndexedBy(result.positions.data(),result.positions.size(),data,count,D3DPT_TRIANGLELIST,result.bounds);}))return reject(RejectReason::ChunkCoverage);
        std::shared_ptr<const MeshSnapshot> shared=std::make_shared<const MeshSnapshot>(std::move(result));
        const Stamp after=stamp(vb.p,ib.p);
        ++maintenance_.rebuilds;
        rememberContent(contentKey,hash,smallest,largest,scratchRawIndices,scratchRawVertices,scratchSourceOffsets,shared,detail,before.valid()&&after==before?before:Stamp{});
        ++snapshots;output=std::move(shared);if(diagnostics)*diagnostics=detail;return true;
    }

    // Compatibility API for callers that need a mutable, independent value.
    bool readMesh(IDirect3DDevice9* device,D3DPRIMITIVETYPE topology,INT base,
                  UINT minimum,UINT vertexCount,UINT start,UINT primitiveCount,
                  const float* expectedWorldToView,MeshSnapshot& output,
                  Diagnostics* diagnostics=nullptr){
        output=MeshSnapshot{};std::shared_ptr<const MeshSnapshot> shared;
        if(!readMeshShared(device,topology,base,minimum,vertexCount,start,primitiveCount,
                           expectedWorldToView,shared,diagnostics))return false;
        output=*shared;return true;
    }

    bool read(IDirect3DDevice9* device,D3DPRIMITIVETYPE topology,INT base,
              UINT minimum,UINT vertexCount,UINT start,UINT primitiveCount,
              const float* expectedWorldToView,Bounds& output){
        output=Bounds{};std::shared_ptr<const MeshSnapshot> snapshot;
        if(!readMeshShared(device,topology,base,minimum,vertexCount,start,primitiveCount,expectedWorldToView,snapshot))return false;
        output=snapshot->bounds;return true;
    }
};
} // namespace NorthlightTerrainCapture
