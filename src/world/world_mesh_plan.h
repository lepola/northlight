#pragma once
#include "world_gi.h"
#include "local_shadow_signature.h"
#include "mesh_page_key.h"
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace NorthlightWorldMeshPages { struct Plan; }
namespace NorthlightWorldMesh { struct PreparationIdentity; }

// Worker-thread preparation only. No D3D, game callbacks or mutable global data.
namespace NorthlightWorldMesh {
// Temporary shadow geometry, addressed in fixed-size blocks when a contiguous
// scene cannot be admitted. A block never exceeds 512 KiB.
template<class T> class ChunkedBuffer {
    static_assert(std::is_trivially_copyable_v<T>,"chunked shadow elements must not throw during copy");
    static constexpr size_t BlockSize=512u*1024u/sizeof(T);
    std::vector<std::unique_ptr<T[]>> blocks_;size_t size_=0;
    const NorthlightGI::AllocationAdmission* admit_=nullptr;
public:
    explicit ChunkedBuffer(const NorthlightGI::AllocationAdmission* admit=nullptr):admit_(admit){}
    size_t size()const{return size_;}bool empty()const{return !size_;}
    const T& operator[](size_t i)const{return blocks_[i/BlockSize][i%BlockSize];}
    T& operator[](size_t i){return blocks_[i/BlockSize][i%BlockSize];}
    void reserve(size_t){}
    void push_back(const T& value){
        if(size_%BlockSize==0){if(admit_&&*admit_&&!(*admit_)(1024u*1024u))throw std::bad_alloc();blocks_.push_back(std::make_unique<T[]>(BlockSize));}
        (*this)[size_++]=value;
    }
    void resize(size_t count){while(size_<count)push_back(T{});}
    struct Iterator {const ChunkedBuffer* owner;size_t i;const T& operator*()const{return (*owner)[i];}Iterator& operator++(){++i;return *this;}bool operator!=(const Iterator& b)const{return i!=b.i;}};
    Iterator begin()const{return {this,0};}Iterator end()const{return {this,size_};}
    template<class It> void insert(Iterator,It first,It last){for(;first!=last;++first)push_back(*first);}
    void detachAdmission(){admit_=nullptr;}
    uint64_t bytes()const{return blocks_.capacity()*sizeof(blocks_[0])+blocks_.size()*BlockSize*sizeof(T);}
    void accountMemory(NorthlightMemoryFootprint::Counter& memory,NorthlightMemoryFootprint::Kind kind)const{memory.vector(NorthlightMemoryFootprint::Metadata,blocks_);for(const auto& block:blocks_)memory.allocation(kind,block.get(),BlockSize*sizeof(T));}
};
struct ChunkedShadowScene {
    explicit ChunkedShadowScene(const NorthlightGI::AllocationAdmission* admit):vertices(admit),triangles(admit){}
    ChunkedBuffer<NorthlightGI::WorldVertex> vertices;
    ChunkedBuffer<NorthlightGI::WorldTriangle> triangles;
    std::vector<NorthlightGI::WorldMaterial> materials;
    std::vector<NorthlightGI::WorldPlacementCoverage> completePlacements;
};
struct Batch {
    // start is an index-buffer element offset; count is a triangle count.
    uint32_t start=0,count=0,material=0;
    // Global ADT chunk X follows world Y, chunk Y follows world X.
    // -1 means nonterrain, or terrain not provably confined to one chunk.
    int chunkX=-1,chunkY=-1;
    bool terrain=false;
    // Exact bounds of this batch's referenced vertices, prepared on the worker.
    // Invalid empty defaults fail open in the conservative clip rejection test.
    NorthlightGI::Vec3 boundsLow={std::numeric_limits<float>::infinity(),std::numeric_limits<float>::infinity(),std::numeric_limits<float>::infinity()};
    NorthlightGI::Vec3 boundsHigh={-std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()};
    bool pointOnly=false;
    uint64_t lampSourceId=0; // Exact owning FGL source, zero for ordinary geometry.
};
struct MaterialUpload {
    uint32_t width=1,height=1;
    // Tightly packed D3DFMT_A8R8G8B8 memory bytes: B,G,R,A.
    std::vector<uint8_t> bgra;
    // Full BGRA identity, prepared on the worker. A hash match is only a
    // lookup shortcut; reuse also requires equal dimensions and exact bytes.
    NorthlightLocalShadowSignature::Digest textureIdentity;
    float alphaCutoff=0;
    bool terrain=false;
};
// 0.3.214 (task 42): the heavy GPU-upload bytes (paged vertex/index copy, cutout textures) live in
// a separately owned payload. The render thread discards it right after the mesh commit; the plan
// (shared with the worker, GI snapshots and the retirement queue) keeps only small metadata.
struct UploadPayload {
    std::shared_ptr<const NorthlightWorldMeshPages::Plan> pages;
    std::vector<MaterialUpload> materials;
    uint64_t bytes=0; // retained capacity, fixed before the payload is shared
    // Immutable diagnostic summaries. The render thread may move material
    // vectors at commit; workers must never inspect those mutable containers.
    uint64_t pageStorageBytes=0,pageMetadataBytes=0,materialPixelBytes=0,materialMetadataBytes=0;
};
// Cross-thread rule: only the render thread dereferences the payload (share/take); every other
// thread sees live()/bytes(), a single atomic. Cold memory diagnostics may share
// the payload briefly to read only immutable summaries and page identity.
// The pointer is touched only through std::atomic_*.
// Copies share the (immutable) payload; set() runs on the worker before publication.
class UploadPayloadHandle {
    std::shared_ptr<UploadPayload> p_;
    std::atomic<uint64_t> bytes_{0};
public:
    UploadPayloadHandle()=default;
    UploadPayloadHandle(const UploadPayloadHandle& o){assign(o.share());}
    UploadPayloadHandle(UploadPayloadHandle&& o)noexcept{assign(o.take());}
    UploadPayloadHandle& operator=(const UploadPayloadHandle& o){if(this!=&o)assign(o.share());return *this;}
    UploadPayloadHandle& operator=(UploadPayloadHandle&& o)noexcept{if(this!=&o)assign(o.take());return *this;}
    void set(std::shared_ptr<UploadPayload> payload){assign(std::move(payload));}
    std::shared_ptr<UploadPayload> share()const{return std::atomic_load(&p_);}
    std::shared_ptr<UploadPayload> take(){auto payload=std::atomic_exchange(&p_,std::shared_ptr<UploadPayload>());bytes_.store(0,std::memory_order_release);return payload;}
    // Drops the plan's reference; returns the bytes it covered (the caller's own reference may free them).
    uint64_t discard(){auto payload=take();return payload?payload->bytes:0;}
    bool live()const{return bytes_.load(std::memory_order_acquire)!=0;}
    uint64_t bytes()const{return bytes_.load(std::memory_order_acquire);}
private:
    void assign(std::shared_ptr<UploadPayload> payload){
        const uint64_t bytes=payload?(payload->bytes?payload->bytes:1):0;
        std::atomic_store(&p_,std::move(payload));bytes_.store(bytes,std::memory_order_release);}
};
struct PreparationIdentity {
    std::shared_ptr<const void> owner;
    uint32_t firstTriangle=0,triangleCount=0;
    uint64_t retainedBytes=0;
    std::vector<uint32_t> materials;
};
using PreparationLayout=std::vector<PreparationIdentity>;


struct WorldMeshUploadPlan {
    void accountMemory(NorthlightMemoryFootprint::Counter&,bool includeObject=true)const;
    // Caller must retain the SAME immutable source scene/BVH until upload and
    // drawing finish. Vertices remain there to avoid a second large CPU copy.
    const NorthlightGI::WorldScene* source=nullptr;
    // Optional shadow-only source. GI retains its original local BVH unchanged.
    std::shared_ptr<const NorthlightGI::WorldScene> ownedSource;
    // Paging needs vertices, not the already packed triangle/material scene.
    // Shadow-only builders may move their vertices here before page allocation.
    // GI and borrowed scenes remain immutable and use source as before.
    std::shared_ptr<const std::vector<NorthlightGI::WorldVertex>> pagingVertices;
    std::shared_ptr<const ChunkedBuffer<NorthlightGI::WorldVertex>> pagingChunks;
    std::shared_ptr<const ChunkedBuffer<uint32_t>> pagingIndices;
    const std::vector<NorthlightGI::WorldVertex>* verticesForPaging()const{
        return pagingVertices?pagingVertices.get():(source?source->vertices.flat():nullptr);
    }
    bool hasPagingVertices()const{return pagingChunks||verticesForPaging();}
    size_t pagingVertexCount()const{return pagingChunks?pagingChunks->size():(verticesForPaging()?verticesForPaging()->size():0);}
    const NorthlightGI::WorldVertex& pagingVertex(size_t i)const{return pagingChunks?(*pagingChunks)[i]:(*verticesForPaging())[i];}
    size_t pagingIndexCount()const{return pagingIndices?pagingIndices->size():indices.size();}
    uint32_t pagingIndex(size_t i)const{return pagingIndices?(*pagingIndices)[i]:indices[i];}
    std::shared_ptr<const std::vector<PreparationIdentity>> preparationLayout;
    std::set<std::pair<int,int>> fixedTerrainChunks;
    uint32_t vertexCount=0,triangleCount=0;
    std::vector<uint32_t> indices;
    std::vector<Batch> batches;
    // Exactly one slot per original material; indices need no remapping.
    std::vector<MaterialUpload> materials;
    // Compact worker-prepared spatial content identities. Commit this pointer
    // with the corresponding batches/fixedTerrainChunks, never before upload.
    std::shared_ptr<const NorthlightLocalShadowSignature::Records> localShadowRecords;
    uint64_t vertexBytes=0,indexBytes=0,textureBytes=0;
    // Sealed plans: pages + materials moved here by seal(); empty after the mesh commit.
    UploadPayloadHandle payload;
    uint64_t pagedCpuBytes=0;
    // Once paged geometry owns its bytes, keep only placement proofs from the
    // temporary shadow scene. GI continues owning its independent local BVH.
    std::vector<NorthlightGI::WorldPlacementCoverage> completePlacements;
    bool pagesSealed=false;
    uint32_t unmaskableTerrainTriangles=0;
    // Retirement admission only (not a frame-path traversal). Counts retained
    // capacity; the payload counts only while it exists (one atomic, safe on any thread); allocator/control-block overhead is conservatively estimated.
    uint64_t cpuBytes()const{
        uint64_t bytes=payload.bytes()+sizeof(*this)+indices.capacity()*sizeof(uint32_t)+batches.capacity()*sizeof(Batch)+materials.capacity()*sizeof(MaterialUpload);
        bytes+=completePlacements.capacity()*sizeof(NorthlightGI::WorldPlacementCoverage);
        if(preparationLayout){bytes+=sizeof(*preparationLayout)+preparationLayout->capacity()*sizeof(PreparationIdentity)+64;for(size_t i=0;i<preparationLayout->size();++i){const auto& piece=(*preparationLayout)[i];bytes+=piece.materials.capacity()*4;
            bool seen=false;for(size_t j=0;j<i;++j)if((*preparationLayout)[j].owner==piece.owner){seen=true;break;}if(!seen)bytes+=piece.retainedBytes;}}
        if(pagingVertices)bytes+=sizeof(*pagingVertices)+pagingVertices->capacity()*sizeof(NorthlightGI::WorldVertex)+64;
        if(pagingChunks)bytes+=sizeof(*pagingChunks)+pagingChunks->bytes()+64;
        if(pagingIndices)bytes+=sizeof(*pagingIndices)+pagingIndices->bytes()+64;
        for(const auto& owner:completePlacements)bytes+=owner.modelKey.capacity();
        for(const auto& material:materials)bytes+=material.bgra.capacity();
        bytes+=fixedTerrainChunks.size()*(sizeof(std::pair<int,int>)+4*sizeof(void*));
        if(localShadowRecords)bytes+=sizeof(NorthlightLocalShadowSignature::Records)+localShadowRecords->capacity()*sizeof(NorthlightLocalShadowSignature::Record)+64;
        if(ownedSource)bytes+=ownedSource->retainedBytes()+64;
        return bytes;
    }
};

// 0.3.216 (issue #66): batch order of the upload plan. Spatial-major: batches are ordered by a coarse cell of
// cellChunks x cellChunks terrain chunks (from the triangle centroid, in the terrain chunk grid), then by material,
// then by chunk, so a region that did not change between two builds yields the same contiguous triangle run (stable
// page content). A nonterrain material now has one batch per cell it touches (+~50 % batches at 4: ~3.8 k -> ~5.7 k in
// Stormwind; terrain batches are unchanged, one per chunk x material). cellChunks 0: the old material-major order.
// chunkMajor orders (chunk, material) inside a cell instead; it measured no better. chunkX/chunkY keep their meaning
// (terrain confined to one chunk, else -1); the cell only orders and splits batches. The order is a
// parameter of buildUploadPlan (default: production); only tests and measurements pass another one.
struct PlanOrder {unsigned cellChunks=4;bool chunkMajor=false;};

// A proven immutable contiguous source range. Tokens are small content/revision
// identities; they never own source vertices or textures. Borrowed local BLAS
// keys additionally carry a weak owner to reject allocator-address reuse.
struct PreparationPiece {
    std::string identity;
    std::weak_ptr<const void> owner;bool weakIdentity=false;
    uint32_t firstTriangle=0,triangleCount=0;
    std::vector<uint32_t> materials; // canonical local material slots -> this scene
};
// Optional worker-owned memo. Geometry identity is weak: cached preparation never
// pins a retired BVH. The caller lends unused bytes from its existing geometry
// cache budget; setLimit(0) releases all retained preparation.
class PreparationCache {
public:
    struct Stats {uint64_t hits=0,misses=0,reusedTriangles=0,preparedTriangles=0,bytes=0,entries=0,pageHits=0,reusedPages=0,reusedPageBytes=0;};
    void accountMemory(NorthlightMemoryFootprint::Counter&)const;
    PreparationCache();~PreparationCache();
    PreparationCache(const PreparationCache&)=delete;PreparationCache& operator=(const PreparationCache&)=delete;
    void setLimit(uint64_t bytes);void reset();const Stats& stats()const;
    bool reuseResident(const WorldMeshUploadPlan&,NorthlightWorldMeshPages::Plan&,const NorthlightMeshPageKey::KeySet&,const NorthlightGI::AllocationAdmission& admit={});
    void rememberPages(const WorldMeshUploadPlan&,const NorthlightWorldMeshPages::Plan&);

private:
    struct Impl;std::unique_ptr<Impl> impl_;
    friend bool buildUploadPlan(const NorthlightGI::WorldScene&,WorldMeshUploadPlan&,std::string&,const NorthlightGI::AllocationAdmission&,const std::vector<NorthlightGI::WorldMaterial>*,const PlanOrder&,const std::vector<PreparationPiece>*,PreparationCache*);
};

// Builds immutable packed indices, exact terrain-chunk batches and upload-ready
// textures on the worker. Input triangle order is preserved within each batch.
// Shadow rasterization samples alpha only: opaque materials use a white 1x1
// texture. Cutout texture colors/alpha are preserved exactly while swizzling.
// Unknown/cross-chunk terrain stays in an unmaskable batch; it is never hidden
// merely because one adjacent chunk was submitted by the game.
// textures (optional): texel source for material i<textures->size() (width/height/rgba); the scene's
// materials there carry metadata only. Read synchronously, so it need only outlive the call.
// Failure preserves output. Empty scenes are valid plans; no GPU call is made.
bool buildUploadPlan(const NorthlightGI::WorldScene& source,
                     WorldMeshUploadPlan& output,std::string& error,
                     const NorthlightGI::AllocationAdmission& admit = {},
                     const std::vector<NorthlightGI::WorldMaterial>* textures = nullptr,
                     const PlanOrder& order = {},
                     const std::vector<PreparationPiece>* sharedPieces = nullptr,
                     PreparationCache* preparation = nullptr);
bool buildChunkedUploadPlan(const ChunkedShadowScene& source,WorldMeshUploadPlan& output,std::string& error,
                            const NorthlightGI::AllocationAdmission& admit,const std::vector<NorthlightGI::WorldMaterial>* textures,const PlanOrder& order);
} // namespace NorthlightWorldMesh
