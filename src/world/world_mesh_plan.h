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
#include <vector>

namespace NorthlightWorldMeshPages { struct Plan; }
namespace NorthlightWorldMesh { struct PreparationIdentity; }

// Worker-thread preparation only. No D3D, game callbacks or mutable global data.
namespace NorthlightWorldMesh {
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
};
// Cross-thread rule: only the render thread dereferences the payload (share/take); every other
// thread sees live()/bytes(), a single atomic. The pointer is touched only through std::atomic_*.
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
    // Caller must retain the SAME immutable source scene/BVH until upload and
    // drawing finish. Vertices remain there to avoid a second large CPU copy.
    const NorthlightGI::WorldScene* source=nullptr;
    // Optional shadow-only source. GI retains its original local BVH unchanged.
    std::shared_ptr<const NorthlightGI::WorldScene> ownedSource;
    // Paging needs vertices, not the already packed triangle/material scene.
    // Shadow-only builders may move their vertices here before page allocation.
    // GI and borrowed scenes remain immutable and use source as before.
    std::shared_ptr<const std::vector<NorthlightGI::WorldVertex>> pagingVertices;
    const std::vector<NorthlightGI::WorldVertex>* verticesForPaging()const{
        return pagingVertices?pagingVertices.get():(source?&source->vertices:nullptr);
    }
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
} // namespace NorthlightWorldMesh
