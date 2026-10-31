#pragma once
#include "intrusive_lru.h"
#include "cpu_retirement.h"
#include "capture_phase_profile.h"
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
#include "source_identity.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>
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
    NorthlightSourceIdentity::Handle sourceIdentity;
    Bounds bounds;
    std::vector<Position> positions;
    // Always triangle-list indices into positions; detached from game buffers.
    NorthlightTerrainIndices::Indices indices;
};

inline bool samePositionBytes(const void* raw,const Position& position){
    static_assert(sizeof(Position)==3*sizeof(std::uint32_t));
    // Fixed-width copies allow unaligned input without aliasing or overreading.
    // Compare bits: float equality would accept a changed signed zero or reject NaNs.
    std::uint32_t a[3],b[3];std::memcpy(a,raw,sizeof a);std::memcpy(b,&position,sizeof b);
    return a[0]==b[0]&&a[1]==b[1]&&a[2]==b[2];
}
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
template<bool Validated=false,class VertexAt>
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
    std::array<unsigned,8> smallSeen{};size_t smallSeenCount=0;bool largeSeen=false;
    const auto rememberChunk=[&](unsigned chunk){
        if(!largeSeen){
            for(size_t i=0;i<smallSeenCount;++i)if(smallSeen[i]==chunk)return false;
            if(smallSeenCount<smallSeen.size()){smallSeen[smallSeenCount++]=chunk;return true;}
            seen.reserve(smallSeen.size()+1);seen.insert(smallSeen.begin(),smallSeen.end());largeSeen=true;
        }
        return seen.insert(chunk).second;
    };
    unsigned previousChunk=std::numeric_limits<unsigned>::max();
    double previousMinX=0,previousMaxX=0,previousMinY=0,previousMaxY=0;
    const std::size_t triangles=topology==D3DPT_TRIANGLELIST?count/3:count-2;
    for(std::size_t triangle=0;triangle<triangles;++triangle){
        const std::size_t first=topology==D3DPT_TRIANGLELIST?triangle*3:triangle;
        const Position p[]={vertexAt(first),vertexAt(first+1),vertexAt(first+2)};
        if constexpr(!Validated)
            for(unsigned i=0;i<3;++i)if(!std::isfinite(p[i].x)||!std::isfinite(p[i].y)||!std::isfinite(p[i].z))return false;
        const double area=(double(p[1].x)-p[0].x)*(double(p[2].y)-p[0].y)-
                          (double(p[1].y)-p[0].y)*(double(p[2].x)-p[0].x);
        if(std::fabs(area)<1e-9)continue; // Includes strip connectors.
        const double centerX=(double(p[0].x)+p[1].x+p[2].x)/3.;
        const double centerY=(double(p[0].y)+p[1].y+p[2].y)/3.;
        int x,y;double minX,maxX,minY,maxY;
        // A strict interior margin proves the original double floor's cell.
        // Borderline centroids retain the original calculation and tolerance.
        const bool reusePrevious=previousChunk!=std::numeric_limits<unsigned>::max()&&
            centerX>previousMinX+tolerance&&centerX<previousMaxX-tolerance&&
            centerY>previousMinY+tolerance&&centerY<previousMaxY-tolerance;
        if(reusePrevious){
            x=int(previousChunk%1024);y=int(previousChunk/1024);
            minX=previousMinX;maxX=previousMaxX;minY=previousMinY;maxY=previousMaxY;
        }else{
            const double gridX=std::floor((origin-centerY)/step);
            const double gridY=std::floor((origin-centerX)/step);
            if(gridX<0||gridX>=1024||gridY<0||gridY>=1024)return false;
            x=int(gridX);y=int(gridY);
            minX=origin-(y+1)*step;maxX=origin-y*step;
            minY=origin-(x+1)*step;maxY=origin-x*step;
        }
        // Finite triangle extrema prove the same per-vertex containment with
        // four double comparisons. Preserve vertex order on equal extrema.
        const float low[]={std::min(p[0].x,std::min(p[1].x,p[2].x)),std::min(p[0].y,std::min(p[1].y,p[2].y)),std::min(p[0].z,std::min(p[1].z,p[2].z))};
        const float high[]={std::max(p[0].x,std::max(p[1].x,p[2].x)),std::max(p[0].y,std::max(p[1].y,p[2].y)),std::max(p[0].z,std::max(p[1].z,p[2].z))};
        if(low[0]<minX-tolerance||high[0]>maxX+tolerance||low[1]<minY-tolerance||high[1]>maxY+tolerance)return false;
        for(unsigned j=0;j<3;++j){result.low[j]=std::min(result.low[j],low[j]);result.high[j]=std::max(result.high[j],high[j]);}
        const unsigned chunk=unsigned(y)*1024+unsigned(x);
        // Chunk-local triangles are contiguous in normal terrain draws. Avoid
        // a hash lookup for every triangle without changing disjoint coverage.
        if(chunk!=previousChunk&&rememberChunk(chunk))
            result.chunks.push_back({x,y,float(minX),float(minY),float(maxX),float(maxY)});
        previousChunk=chunk;
        previousMinX=minX;previousMaxX=maxX;previousMinY=minY;previousMaxY=maxY;
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
    NorthlightSourceIdentity::Handle sourceIdentity(const ContentKey& key,Stamp proof){
      try{if(!proof.valid())return {};const auto& d=key.draw;
        // Draw fields are exactly 32 bits; pair them without dropping any key bits.
        // Keep lifetime/revision tokens and provider addresses at their full width.
        const auto pair=[](std::uint32_t low,std::uint32_t high){return std::uint64_t(low)|(std::uint64_t(high)<<32);};
        return NorthlightSourceIdentity::make({2,reinterpret_cast<std::uintptr_t>(identity_),reinterpret_cast<std::uintptr_t>(version_),reinterpret_cast<std::uintptr_t>(metadata_),
            proof.vb,proof.ib,proof.vertexVersion,proof.indexVersion,pair(d.streamOffset,d.stride),pair(d.positionOffset,d.positionType),pair(std::uint32_t(d.base),d.minimum),pair(d.vertices,d.start),pair(d.primitives,unsigned(d.topology)),pair(key.stream,unsigned(key.format)),pair(key.vertexBytes,key.indexBytes)});
      }catch(...){return {};}
    }
    struct DecodedIndices {NorthlightTerrainIndices::Indices values;size_t entries=0;};
    struct SourceIndices {
        const std::vector<unsigned char> bytes;size_t entries=0;
        const NorthlightTerrainIndices::Indices sourceOffsets;
        const UINT stride,smallest,largest;
        const D3DFORMAT format;const D3DPRIMITIVETYPE topology;
        std::weak_ptr<DecodedIndices> plan;
        SourceIndices(const std::vector<unsigned char>& input,NorthlightTerrainIndices::Indices&& offsets,const ContentKey& key,UINT first,UINT last):
            bytes(input),sourceOffsets(std::move(offsets)),stride(key.draw.stride),smallest(first),largest(last),format(key.format),topology(key.draw.topology){}
    };
    // An expired weak plan can retain its make_shared control/object allocation.
    static constexpr size_t sourceIndexOverhead(){return sizeof(SourceIndices)+32+sizeof(NorthlightSourceIdentity::Retained<DecodedIndices>)+32;}
    static std::uint64_t sourceIndexBytes(const SourceIndices& source){return sourceIndexOverhead()+source.bytes.capacity()+source.sourceOffsets.ownedBytes();}
    static size_t decodedBytes(const DecodedIndices& block){return NorthlightSourceIdentity::allocationBytes<DecodedIndices>(block.values.ownedBytes());}
    struct DecodedMemo {
        std::array<std::weak_ptr<DecodedIndices>,64> slots;
        std::array<std::weak_ptr<const MeshSnapshot>,64> snapshots;
        size_t last=0,blocks=0,dataBytes=0,bytes=0;
        std::shared_ptr<NorthlightSourceIdentity::Ledger> live=std::make_shared<NorthlightSourceIdentity::Ledger>();
    } decoded_;
    struct DecodedChoice {std::shared_ptr<DecodedIndices> block;size_t slot=0;bool memo=false;};
    static size_t decodedSlot(const NorthlightTerrainIndices::Indices& values){
        std::uint64_t hash=values.size();
        for(auto word:{std::uint64_t(values.allocatedBytes()),std::uint64_t(values[0]),std::uint64_t(values[values.size()/2]),std::uint64_t(values[values.size()-1])})hash=(hash^word)*1099511628211ull;
        return size_t(hash%64);
    }
    static bool sameDecoded(const NorthlightTerrainIndices::Indices& prior,const NorthlightTerrainIndices::Indices& values){
        if(prior.size()!=values.size()||prior.allocatedBytes()!=values.allocatedBytes())return false;
        return values.visit([&](const auto* a,size_t count){return prior.visit([&](const auto* b,size_t){
            return sizeof(*a)==sizeof(*b)&&!std::memcmp(a,b,count*sizeof(*a));});});
    }
    DecodedChoice shareDecoded(MeshSnapshot& mesh){
      try{
        if(mesh.indices.empty())return {};
        DecodedChoice choice;choice.slot=decoded_.last;choice.memo=true;
        const auto matches=[&](size_t slot){
            choice.block=decoded_.slots[slot].lock();
            if(choice.block){if(profileCapture_)maintenance_.decodedComparedBytes+=std::min(choice.block->values.allocatedBytes(),mesh.indices.allocatedBytes());if(sameDecoded(choice.block->values,mesh.indices))return true;choice.block.reset();}
            const auto prior=decoded_.snapshots[slot].lock();
            if(profileCapture_&&prior)maintenance_.decodedComparedBytes+=std::min(prior->indices.allocatedBytes(),mesh.indices.allocatedBytes());
            return prior&&sameDecoded(prior->indices,mesh.indices);
        };
        bool repeated=matches(choice.slot);
        if(!repeated){choice.slot=decodedSlot(mesh.indices);if(choice.slot!=decoded_.last)repeated=matches(choice.slot);}
        // First occurrences remain owned. Promote only an independently decoded
        // second exact match, using its own fresh payload rather than old geometry.
        if(!repeated)return choice;
        if(!choice.block){
            choice.block=NorthlightSourceIdentity::allocate<DecodedIndices>(decoded_.live,NorthlightSourceIdentity::allocationBytes<DecodedIndices>(mesh.indices.ownedBytes()));
            choice.block->values=std::move(mesh.indices);
        }
        mesh.indices.share(std::shared_ptr<const NorthlightTerrainIndices::Indices>(choice.block,&choice.block->values));
        return choice;
      }catch(...){return {};}
    }
    struct ContentEntry {
        ContentKey key;Stamp stamp;
        std::shared_ptr<const MeshSnapshot> snapshot;
        std::uint64_t charged=0;UINT smallest=0,largest=0;float low[3]={},high[3]={};
        NorthlightIntrusiveLRU::Links<ContentEntry> lru;
        std::uint64_t hash=0,bytes=0;size_t slot=0,payloadBytes=0,identityBytes=0;
        std::shared_ptr<SourceIndices> indices;std::vector<unsigned char> vertices;
        std::shared_ptr<DecodedIndices> decoded;
        NorthlightTerrainIndices::Indices sourceOffsets; // Exact 16/32-bit byte offsets, in snapshot position order.

    };
    static constexpr std::uint64_t ContentLimit=32u*1024u*1024u;
    // Entry slots share the existing byte budget; small draws need not evict at 4096.
    static constexpr size_t ContentEntries=8192;
    struct RetiredContent {std::array<std::unique_ptr<ContentEntry>,ContentEntries> entries;};
    std::array<std::unique_ptr<ContentEntry>,ContentEntries> content;
    std::array<size_t,ContentEntries> freeContent{};size_t freeContentCount=ContentEntries;
    std::unordered_multimap<std::uint64_t,ContentEntry*> contentLookup;
    NorthlightIntrusiveLRU::List<ContentEntry> contentLRU;
    // A weak one-block memo shares exact consecutive index patterns without retaining buffers
    // or an unbounded interning table. Entry references, not temporary shared_ptrs, own its charge.
    std::weak_ptr<SourceIndices> lastSourceIndices;
    // An expired weak memo can keep one make_shared control/object allocation until reassignment.
    static constexpr size_t ContentBaseBytes=sizeof(content)+sizeof(freeContent)+sizeof(lastSourceIndices)+sizeof(SourceIndices)+32+
        sizeof(decoded_)+sizeof(NorthlightSourceIdentity::Ledger)+32+64*(sizeof(NorthlightSourceIdentity::Retained<DecodedIndices>)+sizeof(NorthlightSourceIdentity::Retained<MeshSnapshot>)+64)+4*sizeof(std::uint64_t)+2*sizeof(unsigned);
    size_t sourceIndexBlocks_=0;std::uint64_t sourceIndexDataBytes_=0,sourceIndexLogicalBytes_=0;
    std::uint64_t sourceOffsetDataBytes_=0;
    std::uint64_t contentBytes=ContentBaseBytes,contentHits=0,trackedHits_=0,avoidedReadBytes_=0;
    std::shared_ptr<NorthlightSourceIdentity::Ledger> live_=std::make_shared<NorthlightSourceIdentity::Ledger>();
    size_t retainedPayloadBytes_=0,retainedIdentityBytes_=0;
    std::uint64_t contentLimit=ContentLimit; /* lowered under address-space pressure */
public:
    struct Maintenance {
        unsigned lookupProbes=0,eraseProbes=0,roomChecks=0;
        uint64_t sourceComparedBytes=0,decodedComparedBytes=0,offsetCompared=0;
        unsigned lookupMisses=0,evictions=0,countPressure=0,bytePressure=0;
        unsigned trackedHits=0,unproven=0,lifetimeChanged=0,vertexChanged=0,indexChanged=0,unstable=0,exactReuses=0,rebuilds=0,indexTokenHits=0;
        unsigned metadataBatches=0,metadataHits=0,metadataFallbacks=0;
        std::uint64_t vertexSpanBytes=0,rawVertexCopyBytes=0,xyzCopyBytes=0;
        unsigned directColdMisses=0,directUnstable=0;
        unsigned indexPlanHits=0;std::uint64_t indexPlanVertices=0,indexPlanIndices=0;
    };
private:
    Maintenance maintenance_;
public:
    enum CapturePhase {StatePhase,LookupPhase,IndexPhase,VertexPhase,BuildPhase,RetainPhase,PhaseCount};
    using CapturePhases=NorthlightCapturePhases::Stats<PhaseCount>;
private:
    CapturePhases capturePhases_;
    bool profileCapture_=false;
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
        for(auto it=range.first;it!=range.second;++it){if(profileCapture_)++maintenance_.lookupProbes;if(it->second->key==key)return it->second;}
        ++maintenance_.lookupMisses;return nullptr;
    }
    void eraseContent(ContentEntry& entry){
        const auto range=contentLookup.equal_range(entry.hash);
        for(auto it=range.first;it!=range.second;++it){if(profileCapture_)++maintenance_.eraseProbes;if(it->second==&entry){contentLookup.erase(it);break;}}
        const auto slot=entry.slot;contentBytes-=entry.bytes;retainedPayloadBytes_-=entry.payloadBytes;retainedIdentityBytes_-=entry.identityBytes;contentLRU.remove(entry);
        sourceIndexLogicalBytes_-=entry.indices->bytes.capacity();
        if(!--entry.indices->entries){contentBytes-=sourceIndexBytes(*entry.indices);sourceIndexDataBytes_-=entry.indices->bytes.capacity();sourceOffsetDataBytes_-=entry.indices->sourceOffsets.ownedBytes();--sourceIndexBlocks_;}
        if(entry.decoded&&!--entry.decoded->entries){const auto bytes=decodedBytes(*entry.decoded);contentBytes-=bytes;decoded_.bytes-=bytes;decoded_.dataBytes-=entry.decoded->values.ownedBytes();--decoded_.blocks;}
        content[slot].reset();freeContent[freeContentCount++]=slot;
    }
    void rememberContent(const ContentKey& key,std::uint64_t hash,UINT smallest,UINT largest,
                         const std::vector<unsigned char>& rawIndices,const std::vector<unsigned char>& rawVertices,
                         const std::vector<UINT>& sourceOffsets,
                          const std::shared_ptr<const MeshSnapshot>& snapshot,const Diagnostics& detail,Stamp verified,const DecodedChoice& decoded,std::uint64_t vertexSpan,const std::shared_ptr<SourceIndices>& indexPlan={}){
      try {
        // Keep exact indices and a mapping to the captured XYZ for revalidation
        // after buffer writes. The other vertex attributes are not used here.
        const bool keepRaw=!verified.valid();
        const std::uint64_t maximumOffset=std::uint64_t(largest-smallest)*key.draw.stride;
        const unsigned offsetBytes=maximumOffset<=UINT16_MAX?2:4;
        auto sharedIndices=indexPlan?indexPlan:lastSourceIndices.lock();
        if(profileCapture_&&sharedIndices)maintenance_.sourceComparedBytes+=std::min(sharedIndices->bytes.size(),rawIndices.size());
        if(sharedIndices&&sharedIndices->bytes!=rawIndices)sharedIndices.reset();
        const bool storeOffsets=!keepRaw&&!sharedIndices;
        const bool reuseOffsets=!keepRaw&&sharedIndices&&(indexPlan==sharedIndices||sharedIndices->sourceOffsets.visit([&](const auto* stored,size_t count){
            if(count!=sourceOffsets.size()||sizeof(*stored)!=offsetBytes)return false;
            using Value=std::remove_cv_t<std::remove_reference_t<decltype(*stored)>>;
            for(size_t i=0;i<count;++i){
                if(profileCapture_)++maintenance_.offsetCompared;
                const std::uint64_t byte=std::uint64_t(sourceOffsets[i])*key.draw.stride;
                if(byte>std::numeric_limits<Value>::max()||byte+sizeof(Position)>vertexSpan)return false;
                const Value value=Value(byte);if(std::memcmp(stored+i,&value,sizeof value))return false;
            }
            return true;
        }));
        // Reuse only the entire exact typed offset sequence. Raw indices alone
        // do not prove layout across stride, format or topology changes.
        std::uint64_t sourceBytes=sharedIndices?sourceIndexBytes(*sharedIndices):sourceIndexOverhead()+rawIndices.size()+(storeOffsets?sourceOffsets.size()*offsetBytes:0);
        const auto required=[&](std::uint64_t bytes){return bytes+(!sharedIndices||!sharedIndices->entries?sourceBytes:0)+(decoded.block&&!decoded.block->entries?decodedBytes(*decoded.block):0);};
        const std::uint64_t estimate=sizeof(ContentEntry)+sizeof(MeshSnapshot)+256+
            (keepRaw?rawVertices.size():storeOffsets||reuseOffsets?0:sourceOffsets.size()*offsetBytes)+
            snapshot->positions.capacity()*sizeof(Position)+snapshot->indices.ownedBytes()+snapshot->bounds.chunks.capacity()*sizeof(ChunkBounds);
        if(required(estimate)>contentLimit-ContentBaseBytes)return;
        const auto range=contentLookup.equal_range(hash);
        for(auto it=range.first;it!=range.second;++it)if(it->second->key==key){eraseContent(*it->second);break;}
        const auto makeRoom=[&](std::uint64_t bytes){
            if(profileCapture_)++maintenance_.roomChecks;
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
        auto next=std::make_unique<ContentEntry>();next->key=key;next->stamp=verified;next->hash=hash;next->smallest=smallest;next->largest=largest;
        next->charged=rawIndices.size()+vertexSpan;
        next->decoded=decoded.block;
        if(keepRaw)next->vertices=rawVertices;
        else if(reuseOffsets)next->sourceOffsets.share(std::shared_ptr<const NorthlightTerrainIndices::Indices>(sharedIndices,&sharedIndices->sourceOffsets));
        else{
            next->sourceOffsets.reserve(sourceOffsets.size(),UINT(maximumOffset));
            for(UINT offset:sourceOffsets){
                const std::uint64_t byte=std::uint64_t(offset)*key.draw.stride;
                if(byte+sizeof(Position)>vertexSpan)return;
                next->sourceOffsets.push_back(UINT(byte));
            }
        }
        if(!sharedIndices){
            sharedIndices=std::make_shared<SourceIndices>(rawIndices,std::move(next->sourceOffsets),key,smallest,largest);sourceBytes=sourceIndexBytes(*sharedIndices);
            if(!keepRaw)next->sourceOffsets.share(std::shared_ptr<const NorthlightTerrainIndices::Indices>(sharedIndices,&sharedIndices->sourceOffsets));
        }
        next->indices=sharedIndices;
        next->snapshot=snapshot;
        std::memcpy(next->low,detail.low,sizeof next->low);std::memcpy(next->high,detail.high,sizeof next->high);
        next->payloadBytes=NorthlightSourceIdentity::allocationBytes<MeshSnapshot>(snapshot->positions.capacity()*sizeof(Position)+snapshot->indices.ownedBytes()+snapshot->bounds.chunks.capacity()*sizeof(ChunkBounds));
        next->identityBytes=snapshot->sourceIdentity?snapshot->sourceIdentity->bytes():0;
        next->bytes=sizeof(ContentEntry)+256+next->payloadBytes+next->identityBytes+next->vertices.capacity()+next->sourceOffsets.ownedBytes();
        if(required(next->bytes)>contentLimit-ContentBaseBytes||!makeRoom(next->bytes))return;
        // Insert the optional hash node before publishing ownership/links. An
        // allocation failure must leave the computed snapshot valid and the
        // free-slot/LRU invariants intact. The per-entry allowance includes
        // hash-node and amortized bucket storage.
        const auto slot=freeContent[freeContentCount-1];next->slot=slot;
        contentLookup.emplace(hash,next.get());--freeContentCount;
        sourceIndexLogicalBytes_+=sharedIndices->bytes.capacity();
        if(!sharedIndices->entries++){contentBytes+=sourceBytes;sourceIndexDataBytes_+=sharedIndices->bytes.capacity();sourceOffsetDataBytes_+=sharedIndices->sourceOffsets.ownedBytes();++sourceIndexBlocks_;}
        if(decoded.block){if(!decoded.block->entries++){const auto bytes=decodedBytes(*decoded.block);contentBytes+=bytes;decoded_.bytes+=bytes;decoded_.dataBytes+=decoded.block->values.ownedBytes();++decoded_.blocks;}
        }
        if(decoded.memo){decoded_.slots[decoded.slot]=decoded.block;decoded_.snapshots[decoded.slot]=snapshot;decoded_.last=decoded.slot;}
        // A plan owns no geometry. It is usable only with the exact original
        // index bytes, format, topology and byte-offset layout.
        if(decoded.block&&!keepRaw&&sharedIndices->stride==key.draw.stride&&sharedIndices->format==key.format&&
           sharedIndices->topology==key.draw.topology&&(storeOffsets||reuseOffsets))sharedIndices->plan=decoded.block;
        lastSourceIndices=sharedIndices;
        contentLRU.append(*next);contentBytes+=next->bytes;retainedPayloadBytes_+=next->payloadBytes;retainedIdentityBytes_+=next->identityBytes;content[slot]=std::move(next);
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
    ~FrameCache(){clearPersistent();clearFrame();}
    // REQUIRED every Present/Reset, including frames where effects are disabled.
    // Persistent content entries retain no resources.
    // Tracked lifetimes/revisions prove fast hits; all other
    // entries require full live-byte verification next frame.
    void clearFrame(){
        readBytes=chargedBytes=0;snapshots=0;maintenance_={};capturePhases_.clear();profileCapture_=false;
    }
    void profileCapture(bool enabled){profileCapture_=enabled;}
    const CapturePhases& capturePhases()const{return capturePhases_;}
    std::size_t entryCount()const{return snapshots;}
    std::uint64_t bytesRead()const{return readBytes;}
    // Call at device Reset; Present clears only per-frame budgets. Every cache
    // untracked hit verifies current raw buffer bytes, without retaining any lock
    // or COM resource reference. Shared snapshots are immutable and remain valid
    // if callers retain them across eviction/reset. The cache budget accounts
    // for cache-owned storage; callers must separately bound retained outputs.
    size_t clearPersistent(NorthlightStreaming::CpuRetirement* retirement=nullptr,NorthlightStreaming::CpuRetirementBatch* batch=nullptr){
        size_t deferred=0;
        // Only detached CPU entries escape; no buffer, declaration or D3D owner is retained.
        // Keep reset/critical clears synchronous, and preserve that fallback on refusal.
        if((retirement||batch)&&persistentEntries())try{
            auto retired=std::make_shared<RetiredContent>();
            for(auto& entry:content)if(entry&&entry->decoded)--entry->decoded->entries;
            retired->entries.swap(content);
            const auto bytes=size_t(contentBytes)+sizeof(RetiredContent);
            if(batch?batch->retire(retired,bytes):retirement->retire(retired,bytes))deferred=bytes;
        }catch(...){}
        contentLookup.clear();contentLRU.clear();lastSourceIndices.reset();for(auto& slot:decoded_.slots)slot.reset();for(auto& slot:decoded_.snapshots)slot.reset();decoded_.last=0;for(auto& entry:content){if(entry&&entry->decoded)--entry->decoded->entries;entry.reset();}decoded_.blocks=decoded_.dataBytes=decoded_.bytes=0;sourceIndexBlocks_=0;sourceIndexDataBytes_=sourceIndexLogicalBytes_=sourceOffsetDataBytes_=0;retainedPayloadBytes_=retainedIdentityBytes_=0;contentBytes=ContentBaseBytes;contentHits=trackedHits_=avoidedReadBytes_=0;freeContentCount=ContentEntries;for(size_t i=0;i<freeContent.size();++i)freeContent[i]=i;maintenance_={};return deferred;}
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
    size_t sourceLiveBytes()const{return live_->bytes.load(std::memory_order_relaxed)+decoded_.live->bytes.load(std::memory_order_relaxed);}
    size_t sourceLiveObjects()const{return live_->objects.load(std::memory_order_relaxed);}
    size_t sourceMetadataBytes()const{return size_t(contentBytes)-retainedPayloadBytes_-retainedIdentityBytes_-decoded_.bytes;}
    std::shared_ptr<const MeshSnapshot> retain(MeshSnapshot&& snapshot){const size_t bytes=NorthlightSourceIdentity::allocationBytes<MeshSnapshot>(snapshot.positions.capacity()*sizeof(Position)+snapshot.indices.ownedBytes()+snapshot.bounds.chunks.capacity()*sizeof(ChunkBounds));auto owned=NorthlightSourceIdentity::allocate<MeshSnapshot>(live_,bytes);*owned=std::move(snapshot);return owned;}
    struct IndexStorage {size_t compact=0,wide=0,sourceBlocks=0,offsetCompact=0,offsetWide=0,sharedBlocks=0,weakOwners=0,expiredWeakOwners=0,offsetSharedReferences=0;std::uint64_t bytes=0,avoided=0,sourceBytes=0,sourceAvoided=0,offsetBytes=0,offsetAvoided=0,physicalBytes=0,sharedAllocationBytes=0,liveBlockBytes=0,positionBytes=0,positionCapacityBytes=0,offsetPhysicalBytes=0;};
    // Sampled diagnostics only; never walk the persistent cache in a draw hook.
    IndexStorage indexStorage()const{
        IndexStorage stats;stats.offsetPhysicalBytes=sourceOffsetDataBytes_;stats.sourceBlocks=sourceIndexBlocks_;stats.sourceBytes=sourceIndexDataBytes_;stats.sourceAvoided=sourceIndexLogicalBytes_-sourceIndexDataBytes_;stats.sharedBlocks=decoded_.blocks;stats.physicalBytes=decoded_.dataBytes;stats.sharedAllocationBytes=decoded_.bytes;
        stats.liveBlockBytes=decoded_.live->bytes.load(std::memory_order_relaxed);const std::weak_ptr<DecodedIndices> empty;
        const std::weak_ptr<const MeshSnapshot> emptySnapshot;
        for(size_t i=0;i<decoded_.slots.size();++i){const auto& slot=decoded_.slots[i];const auto& prior=decoded_.snapshots[i];
            const bool block=slot.owner_before(empty)||empty.owner_before(slot),snapshot=prior.owner_before(emptySnapshot)||emptySnapshot.owner_before(prior);
            stats.weakOwners+=block||snapshot;stats.expiredWeakOwners+=(block&&slot.expired())||(snapshot&&prior.expired());}
        for(const auto& entry:content)if(entry){const auto& indices=entry->snapshot->indices;const auto bytes=indices.allocatedBytes();
            const bool compact=indices.visit([](const auto* p,size_t){return sizeof(*p)==2;});
            stats.compact+=compact;stats.wide+=!compact;stats.bytes+=bytes;stats.avoided+=compact?bytes:0;
            stats.physicalBytes+=indices.ownedBytes();stats.positionBytes+=entry->snapshot->positions.size()*sizeof(Position);stats.positionCapacityBytes+=entry->snapshot->positions.capacity()*sizeof(Position);
            const auto& offsets=entry->sourceOffsets;if(!offsets.empty()){
                stats.offsetPhysicalBytes+=offsets.ownedBytes();stats.offsetSharedReferences+=!offsets.ownedBytes();
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
        // The remap above already validates every source index and finite XYZ.
        // Reuse that proof instead of rechecking shared vertices per triangle.
        if(!result.indices.visit([&](const auto* data,std::size_t count){return classifyTriangleSequence<true>([&](std::size_t i){return result.positions[data[i]];},count,D3DPT_TRIANGLELIST,result.bounds);}))return reject(RejectReason::ChunkCoverage);
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
        NorthlightCapturePhases::Scope<PhaseCount> phase(profileCapture_?&capturePhases_:nullptr,StatePhase);
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
        phase.next(LookupPhase);
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
        phase.next(IndexPhase);
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
        std::shared_ptr<SourceIndices> indexPlan;
        std::shared_ptr<DecodedIndices> plannedIndices;
        if(!cached&&before.valid()){
            indexPlan=lastSourceIndices.lock();
            if(indexPlan&&indexPlan->format==id.Format&&indexPlan->topology==topology&&indexPlan->stride==detail.stride&&
               !indexPlan->sourceOffsets.empty()&&indexPlan->bytes==scratchRawIndices)plannedIndices=indexPlan->plan.lock();
            if(!plannedIndices)indexPlan.reset();
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
        if(sameIndices){smallest=cached->smallest;largest=cached->largest;}
        else if(indexPlan){
            smallest=indexPlan->smallest;largest=indexPlan->largest;
            valid=smallest>=minimum&&std::uint64_t(largest)<std::uint64_t(minimum)+vertexCount;
        }else decodeIndices();
        detail.firstIndex=smallest;detail.lastIndex=largest;
        if(!valid)return reject(RejectReason::IndexRange);
        const std::int64_t firstVertex=std::int64_t(base)+smallest,lastVertex=std::int64_t(base)+largest;
        if(firstVertex<0||lastVertex<firstVertex)return reject(RejectReason::VertexRange);
        const std::uint64_t firstByte=std::uint64_t(streamOffset)+std::uint64_t(firstVertex)*detail.stride+position->Offset;
        const std::uint64_t lastByte=std::uint64_t(streamOffset)+std::uint64_t(lastVertex)*detail.stride+position->Offset+componentBytes;
        const std::uint64_t span=lastByte-firstByte;
        if(lastByte>vd.Size||!span)return reject(RejectReason::VertexRange);
        if(chargedBytes+span>limits.maxReadBytesPerFrame)return reject(RejectReason::ByteBudget);
        // Only a proven cold miss can avoid copying unused vertex attributes.
        // Prepare every destination before Lock; revalidation/unproven paths
        // keep their complete raw-byte fallback unchanged.
        const bool directCold=!cached&&before.valid();
        MeshSnapshot result;auto& remap=scratchRemap;
        const UINT unused=std::numeric_limits<UINT>::max();
        const auto preparePositions=[&](){
            if(indexPlan){result.positions.resize(indexPlan->sourceOffsets.size());scratchSourceOffsets.clear();return;}
            remap.assign(std::size_t(largest)-smallest+1,unused);
            result.positions.resize(std::min(indices.size(),remap.size()));
            scratchSourceOffsets.clear();scratchSourceOffsets.reserve(result.positions.size());
        };
        const auto copyPositions=[&](const unsigned char* source){
            UINT unique=0;const float infinity=std::numeric_limits<float>::infinity();
            for(unsigned j=0;j<3;++j){detail.low[j]=infinity;detail.high[j]=-infinity;}
            if(indexPlan){
                indexPlan->sourceOffsets.visit([&](const auto* offsets,size_t count){
                    for(size_t i=0;i<count;++i){
                        Position& p=result.positions[i];std::memcpy(&p,source+offsets[i],sizeof p);
                        const float values[]={p.x,p.y,p.z};
                        for(unsigned j=0;j<3;++j){if(!std::isfinite(values[j]))valid=false;detail.low[j]=std::min(detail.low[j],values[j]);detail.high[j]=std::max(detail.high[j],values[j]);}
                    }
                });
                result.indices.share(std::shared_ptr<const NorthlightTerrainIndices::Indices>(plannedIndices,&plannedIndices->values));
                ++maintenance_.indexPlanHits;maintenance_.indexPlanVertices+=result.positions.size();maintenance_.indexPlanIndices+=indexCount;
                maintenance_.xyzCopyBytes+=result.positions.size()*sizeof(Position);return;
            }
            for(auto& index:indices){
                UINT& mapped=remap[index-smallest];
                if(mapped==unused){
                    mapped=unique++;Position& p=result.positions[mapped];
                    scratchSourceOffsets.push_back(index-smallest);
                    std::memcpy(&p,source+std::size_t(index-smallest)*detail.stride,sizeof(Position));
                    const float values[]={p.x,p.y,p.z};
                    for(unsigned j=0;j<3;++j){
                        if(!std::isfinite(values[j]))valid=false;
                        detail.low[j]=std::min(detail.low[j],values[j]);detail.high[j]=std::max(detail.high[j],values[j]);
                    }
                }
                index=mapped;
            }
            result.positions.resize(unique);maintenance_.xyzCopyBytes+=std::uint64_t(unique)*sizeof(Position);
        };
        if(directCold)preparePositions();
        else scratchRawVertices.reserve(size_t(span)); // resize below cannot allocate while locked
        phase.next(VertexPhase);
        raw=nullptr;NorthlightReplayCopies::Reader<IDirect3DVertexBuffer9> readVertices(vb.p);hr=readVertices.lock(UINT(firstByte),UINT(span),&raw);
        if(FAILED(hr))return reject(RejectReason::VertexLock,hr);
        if(!raw){readVertices.unlock();return reject(RejectReason::VertexLock,E_POINTER);}
        chargedBytes+=span;readBytes+=span;maintenance_.vertexSpanBytes+=span;
        bool sameVertices=sameIndices&&cached->vertices.size()==span&&!std::memcmp(raw,cached->vertices.data(),size_t(span));
        if(sameIndices&&!sameVertices&&!cached->sourceOffsets.empty()&&cached->sourceOffsets.size()==cached->snapshot->positions.size()){
            sameVertices=cached->sourceOffsets.visit([&](const auto* offsets,size_t count){
                for(size_t i=0;i<count;++i)if(!samePositionBytes(static_cast<const unsigned char*>(raw)+offsets[i],cached->snapshot->positions[i]))return false;
                return true;
            });
        }
        if(!sameVertices){
            if(directCold){copyPositions(static_cast<const unsigned char*>(raw));++maintenance_.directColdMisses;}
            else{scratchRawVertices.resize(size_t(span));std::memcpy(scratchRawVertices.data(),raw,size_t(span));maintenance_.rawVertexCopyBytes+=span;}
        }
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
        phase.next(BuildPhase);
        if(sameIndices){scratchRawIndices.resize(size_t(indexBytes));std::memcpy(scratchRawIndices.data(),cached->indices->bytes.data(),size_t(indexBytes));decodeIndices();}
        if(!directCold){preparePositions();copyPositions(scratchRawVertices.data());}
        if(!valid)return reject(RejectReason::VertexNonfinite);
        const auto unique=result.positions.size();if(!indexPlan)result.indices.reserve(std::size_t(primitiveCount)*3,unique?UINT(unique-1):0);
        for(UINT primitive=0;!indexPlan&&primitive<primitiveCount;++primitive){
            const std::size_t first=topology==D3DPT_TRIANGLELIST?std::size_t(primitive)*3:primitive;
            UINT a=indices[first],b=indices[first+1],c=indices[first+2];
            if(a==b||b==c||c==a)continue;
            if(topology==D3DPT_TRIANGLESTRIP&&(primitive&1))std::swap(a,b);
            result.indices.push_back(a);result.indices.push_back(b);result.indices.push_back(c);
        }
        // The remap above already validates every source index and finite XYZ.
        if(!result.indices.visit([&](const auto* data,std::size_t count){return classifyTriangleSequence<true>([&](std::size_t i){return result.positions[data[i]];},count,D3DPT_TRIANGLELIST,result.bounds);}))return reject(RejectReason::ChunkCoverage);
        const Stamp after=stamp(vb.p,ib.p);
        const bool verified=before.valid()&&after==before;
        phase.next(RetainPhase);
        if(verified)result.sourceIdentity=sourceIdentity(contentKey,before);
        const auto decoded=indexPlan?DecodedChoice{plannedIndices,0,false}:shareDecoded(result);
        std::shared_ptr<const MeshSnapshot> shared=retain(std::move(result));
        ++maintenance_.rebuilds;
        // A racing write removes the optional cache proof. The exact copied
        // snapshot remains usable, but no absent raw fallback is published.
        if(!directCold||verified)rememberContent(contentKey,hash,smallest,largest,scratchRawIndices,scratchRawVertices,scratchSourceOffsets,shared,detail,verified?before:Stamp{},decoded,span,indexPlan);
        else ++maintenance_.directUnstable;
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
