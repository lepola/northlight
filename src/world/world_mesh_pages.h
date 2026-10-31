#pragma once
#include "world_mesh_plan.h"
#include "mesh_page_key.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

// CPU worker preparation: bounded managed resources keep first residency from
// implicitly uploading a whole city's vertex/index allocation at once. Pages
// preserve geometry exactly; this is partitioning, never simplification.
namespace NorthlightWorldMeshPages {
constexpr uint32_t PageVertexByteLimit=512u*1024u;
constexpr uint32_t PageIndexByteLimit=512u*1024u;
constexpr uint32_t PageVertexLimit=PageVertexByteLimit/sizeof(NorthlightGI::WorldVertex);
constexpr uint32_t PageIndexLimit=PageIndexByteLimit/sizeof(uint32_t);
static_assert(sizeof(NorthlightGI::WorldVertex)==32,"Paging preserves the full world vertex");
static_assert(sizeof(NorthlightGI::Vec3)==12,"Content anchors hash three packed floats");
// 0.3.216 (issue #66): content-defined page boundaries. After a triangle whose position hash is 0 mod anchorModulus,
// and once the page holds minFillVertices, the page closes (at the end of the batch when the batch fits an empty page,
// so such batches stay whole). An insertion or removal anywhere in the plan then only changes the pages up to the next
// anchor; every later page is byte-identical to the previous build's. The hard limits stay as forced cuts.
constexpr uint32_t MinFillVertices=PageVertexLimit/4;
constexpr uint32_t AnchorModulus=6144;
// anchorModulus 0: sequential paging (tests, measurements). resident (optional): keys of pages the GPU already holds; a page
// whose key is in it is freed the moment it is finished (key computed first), so the worker never holds more than the pages
// that must be uploaded plus the one being filled. Anchors and page boundaries never depend on it. peakLiveBytes (optional)
// receives the largest live page capacity during the build (kept pages + the page just finished), for measurements.
struct Options {uint32_t anchorModulus=AnchorModulus,minFillVertices=MinFillVertices;const NorthlightMeshPageKey::KeySet* resident=nullptr;uint64_t* peakLiveBytes=nullptr;};
struct Page {
    std::vector<NorthlightGI::WorldVertex> vertices;
    std::vector<uint32_t> indices;
    NorthlightMeshPageKey::Key key; // 0.3.216: content key of vertices+indices, set by build() on the worker
    // 0.3.216: the GPU already holds exactly this page (its key was resident when the plan was built): vertices/indices
    // were freed on the worker, key and exact byte sizes remain. The render thread must find it in the buffer store.
    bool elided=false;
    size_t vertexCount()const{return key.vertexBytes/sizeof(NorthlightGI::WorldVertex);}
    size_t indexCount()const{return key.indexBytes/sizeof(uint32_t);}
};
struct Batch : NorthlightWorldMesh::Batch {
    uint32_t page=0,minVertex=0,vertexCount=0;
};
struct Plan {
    std::vector<Page> pages;
    std::vector<Batch> batches;
    uint64_t vertexBytes=0,indexBytes=0;
    void accountMemory(NorthlightMemoryFootprint::Counter& memory,bool includeObject=true)const{
        using namespace NorthlightMemoryFootprint;
        if(includeObject){if(!memory.visit(this))return;memory.add(Metadata,sizeof(*this));}
        memory.vector(Metadata,pages);memory.vector(Metadata,batches);
        for(const auto& page:pages){memory.vector(Upload,page.vertices);memory.vector(Upload,page.indices);}
    }
    uint64_t cpuBytes() const {
        uint64_t bytes=sizeof(*this)+pages.capacity()*sizeof(Page)+batches.capacity()*sizeof(Batch);
        for(const auto& p:pages)bytes+=p.vertices.capacity()*sizeof(NorthlightGI::WorldVertex)+p.indices.capacity()*sizeof(uint32_t);
        return bytes;
    }
};

// At most one page (or its preflight's first overflowing vertex) is indexed.
// Contiguous slots avoid allocating/freeing a hash node for every vertex.
class IndexMap {
    static constexpr uint32_t Capacity=2*PageVertexLimit;
    struct Slot {uint32_t key=0,value=0,epoch=0;};
    std::vector<Slot> slots_=std::vector<Slot>(Capacity);
    uint32_t epoch_=1,size_=0;
    uint32_t slot(uint32_t key)const {
        uint32_t i=(key*0x9e3779b9u)>>(32-15);
        while(slots_[i].epoch==epoch_&&slots_[i].key!=key)i=(i+1)&(Capacity-1);
        return i;
    }
public:
    static_assert(Capacity==32768,"Index hash uses the upper 15 bits");
    static constexpr uint32_t Missing=UINT32_MAX;
    void clear(){if(!++epoch_){std::fill(slots_.begin(),slots_.end(),Slot{});epoch_=1;}size_=0;}
    uint32_t size()const{return size_;}
    uint32_t find(uint32_t key)const {const auto& s=slots_[slot(key)];return s.epoch==epoch_?s.value:Missing;}
    std::pair<uint32_t,bool> insert(uint32_t key,uint32_t value) {
        auto& s=slots_[slot(key)];if(s.epoch==epoch_)return {s.value,false};
        s={key,value,epoch_};++size_;return {value,true};
    }
};

// The input indices/batches must be the immutable packed upload plan. Every
// original batch stays whole if it fits an empty page. Larger batches split
// only at triangle boundaries. One bounded source-index map is reused per page;
// a second bounded set preflights a batch without retaining a scene-sized map.
// Failure never modifies output. Materials and scene ownership remain in input.
// Pages also close at content anchors (see Options) and carry a content key; a batch that fits an empty page
// is never split by an anchor.
inline bool build(const NorthlightWorldMesh::WorldMeshUploadPlan& input,Plan& output,std::string& error,
                  const NorthlightGI::AllocationAdmission& admit = {},const Options& options = {}) {
    auto fail=[&](const char* why){error=why;return false;};
    try {
        if(input.indices.size()!=uint64_t(input.triangleCount)*3)
            return fail("Paged mesh triangle/index count mismatch");
        const auto* vertices=input.verticesForPaging();
        if(input.pagesSealed||(input.pagingVertices&&(input.source||input.ownedSource)))
            return fail("Paged mesh source ownership mismatch");
        if(!input.indices.empty()&&(!vertices||vertices->size()!=input.vertexCount))
            return fail("Paged mesh source/vertex count mismatch");
        Plan next;
        if(input.indices.empty()){
            if(!input.batches.empty())return fail("Paged mesh empty indices with batches");
            output=std::move(next);error.clear();return true;
        }
        auto allowed=[&](uint64_t bytes){return !admit||admit(bytes);};
        // Reserve admission for both bounded index tables.
        if(!allowed(2u*1024u*1024u))return fail("Paged mesh working memory deferred");
        IndexMap remap,batchUnique;
        uint64_t expectedStart=0;
        bool closeAfter=false; // an anchor closed the current page: the next triangle/batch opens a new one
        uint64_t liveKept=0,peakLive=0;
        // A finished page gets its content key; a resident one is freed right here (elided), keeping key and sizes.
        auto finishPage=[&](Page& p){
            p.key=NorthlightMeshPageKey::keyOf(p.vertices.data(),p.vertices.size()*sizeof(NorthlightGI::WorldVertex),p.indices.data(),p.indices.size()*sizeof(uint32_t));
            next.vertexBytes+=p.key.vertexBytes;next.indexBytes+=p.key.indexBytes;
            uint64_t capacity=p.vertices.capacity()*sizeof(NorthlightGI::WorldVertex)+p.indices.capacity()*sizeof(uint32_t);
            peakLive=std::max(peakLive,liveKept+capacity);
            if(options.resident&&options.resident->count(p.key)){
                std::vector<NorthlightGI::WorldVertex>().swap(p.vertices);std::vector<uint32_t>().swap(p.indices);p.elided=true;
            }else{
                // Content anchors often close city pages half full. Keep the
                // exact bytes, not a full 512 KiB vertex capacity per page.
                // Only one bounded page is copied; resident pages skip it.
                // Optional: a refused or failed copy keeps the page as it is
                // (same bytes, larger capacity) instead of failing the build.
                auto compact=[&](auto& buffer){
                    using Buffer=std::decay_t<decltype(buffer)>;
                    using Value=typename Buffer::value_type;
                    const uint64_t oldBytes=buffer.capacity()*sizeof(Value),bytes=buffer.size()*sizeof(Value);
                    if(buffer.capacity()<=buffer.size()+buffer.size()/4||oldBytes-bytes<65536)return;
                    if(!allowed(bytes))return;
                    try{
                        Buffer exact(buffer.begin(),buffer.end());
                        const uint64_t newBytes=exact.capacity()*sizeof(Value);
                        peakLive=std::max(peakLive,liveKept+capacity+newBytes);
                        buffer.swap(exact);capacity=capacity-oldBytes+newBytes;
                    }catch(const std::bad_alloc&){}
                };
                compact(p.vertices);compact(p.indices);
                liveKept+=capacity;
            }
            return true;
        };
        auto newPage=[&](){
            closeAfter=false;
            if(!next.pages.empty()&&!finishPage(next.pages.back()))return false;
            // Covers both bounded buffers plus vector growth transient copies.
            if(!allowed(2u*(PageVertexByteLimit+PageIndexByteLimit)))return false;
            if(next.pages.size()==next.pages.capacity()) {
                const size_t capacity=std::max<size_t>(1,next.pages.capacity()*2);
                if(!allowed(capacity*sizeof(Page)))return false;
                next.pages.reserve(capacity);
            }
            next.pages.emplace_back();
            // Vertices fill ~95% of a page on real data, so doubling would end at the limit anyway: reserve it once
            // (no realloc chain). Indices fill ~40-55% and keep growing on demand; a full reserve costs ~20% CPU.
            next.pages.back().vertices.reserve(PageVertexLimit);
            remap.clear();return true;
        };
        for(const auto& original:input.batches) {
            const uint64_t count=uint64_t(original.count)*3;
            if(!original.count||original.start!=expectedStart||count>input.indices.size()-expectedStart||original.material>=input.materials.size())
                return fail("Paged mesh malformed batch range/material");
            expectedStart+=count;
            // Preflight only batches that could fit; the set never exceeds one
            // page plus one entry even for an arbitrarily large original batch.
            bool fitsEmpty=count<=PageIndexLimit;
            uint32_t missing=0;
            batchUnique.clear();
            if(fitsEmpty) {
                for(uint64_t i=original.start;i<expectedStart;++i) {
                    const auto sourceIndex=input.indices[size_t(i)];
                    if(sourceIndex>=input.vertexCount)return fail("Paged mesh index outside source vertices");
                    if(batchUnique.insert(sourceIndex,0).second) {
                        if(batchUnique.size()>PageVertexLimit){fitsEmpty=false;break;}
                        if(remap.find(sourceIndex)==IndexMap::Missing)++missing;
                    }
                }
            }
            bool anchored=false; // anchor hit inside a batch that must stay whole: the page closes after the batch
            if(next.pages.empty()||closeAfter||(fitsEmpty&&(next.pages.back().indices.size()+count>PageIndexLimit||next.pages.back().vertices.size()+missing>PageVertexLimit)))
                if(!newPage())return fail("Paged mesh page allocation deferred");
            size_t currentBatch=std::numeric_limits<size_t>::max();
            for(uint64_t i=original.start;i<expectedStart;i+=3) {
                uint32_t ids[3],locals[3],newVertices=0;
                for(unsigned k=0;k<3;++k) {
                    ids[k]=input.indices[size_t(i+k)];
                    if(ids[k]>=input.vertexCount)return fail("Paged mesh index outside source vertices");
                    bool repeated=false;for(unsigned previous=0;previous<k;++previous)repeated|=ids[k]==ids[previous];
                    locals[k]=remap.find(ids[k]);
                    if(!repeated&&locals[k]==IndexMap::Missing)++newVertices;
                }
                if(closeAfter||next.pages.back().vertices.size()+newVertices>PageVertexLimit||next.pages.back().indices.size()+3>PageIndexLimit) {
                    if(!newPage())return fail("Paged mesh page allocation deferred");
                    for(auto& local:locals)local=IndexMap::Missing;
                    currentBatch=std::numeric_limits<size_t>::max();
                }
                auto& page=next.pages.back();
                if(currentBatch==std::numeric_limits<size_t>::max()) {
                    Batch b;static_cast<NorthlightWorldMesh::Batch&>(b)=original;
                    b.start=uint32_t(page.indices.size());b.count=0;b.page=uint32_t(next.pages.size()-1);
                    const float inf=std::numeric_limits<float>::infinity();
                    b.boundsLow={inf,inf,inf};b.boundsHigh={-inf,-inf,-inf};
                    b.minVertex=std::numeric_limits<uint32_t>::max();b.vertexCount=0;
                    if(next.batches.size()==next.batches.capacity()) {
                        const size_t capacity=std::max<size_t>(1,next.batches.capacity()*2);
                        if(!allowed(capacity*sizeof(Batch)))return fail("Paged mesh batch allocation deferred");
                        next.batches.reserve(capacity);
                    }
                    next.batches.push_back(b);currentBatch=next.batches.size()-1;
                }
                auto& batch=next.batches[currentBatch];
                uint32_t maxVertex=batch.vertexCount?batch.minVertex+batch.vertexCount-1:0;
                for(unsigned k=0;k<3;++k) {
                    uint32_t local=locals[k];
                    if(local==IndexMap::Missing) {
                        auto insertion=remap.insert(ids[k],uint32_t(page.vertices.size()));local=insertion.first;
                        if(insertion.second) {
                            const auto& vertex=(*vertices)[ids[k]];
                            if(!std::isfinite(vertex.position.x)||!std::isfinite(vertex.position.y)||!std::isfinite(vertex.position.z))
                                return fail("Paged mesh nonfinite vertex position");
                            page.vertices.push_back(vertex);
                        }
                    }
                    page.indices.push_back(local);
                    batch.minVertex=std::min(batch.minVertex,local);maxVertex=std::max(maxVertex,local);
                    const auto& p=page.vertices[local].position;
                    batch.boundsLow.x=std::min(batch.boundsLow.x,p.x);batch.boundsLow.y=std::min(batch.boundsLow.y,p.y);batch.boundsLow.z=std::min(batch.boundsLow.z,p.z);
                    batch.boundsHigh.x=std::max(batch.boundsHigh.x,p.x);batch.boundsHigh.y=std::max(batch.boundsHigh.y,p.y);batch.boundsHigh.z=std::max(batch.boundsHigh.z,p.z);
                }
                batch.vertexCount=maxVertex-batch.minVertex+1;++batch.count;
                if(options.anchorModulus&&page.vertices.size()>=options.minFillVertices) {
                    const auto& sv=*vertices;
                    const uint64_t anchor=NorthlightMeshPageKey::triangleHash(&sv[ids[0]].position.x,&sv[ids[1]].position.x,&sv[ids[2]].position.x);
                    if(anchor%options.anchorModulus==0){if(fitsEmpty)anchored=true;else closeAfter=true;}
                }
            }
            if(anchored)closeAfter=true;
        }
        if(expectedStart!=input.indices.size())return fail("Paged mesh batches do not cover all indices");
        if(!next.pages.empty())finishPage(next.pages.back()); // never fails: compaction is optional
        if(options.peakLiveBytes)*options.peakLiveBytes=peakLive;
        output=std::move(next);error.clear();return true;
    } catch(const std::bad_alloc&) {return fail("Paged mesh worker allocation failed");}
      catch(const std::length_error&) {return fail("Paged mesh worker allocation size overflow");}
}

// Worker, after build() and before seal(): frees the vertex/index data of every page whose content key the GPU already
// holds (`resident`, published by the render thread from its ACTIVE buffers). The pages keep key, sizes and batches, so
// the plan stays valid and the aggregate sizes stay the full mesh's; cpuBytes() drops by the freed capacity.
struct ElisionStats {size_t pages=0;uint64_t bytes=0;};
inline ElisionStats elide(Plan& plan,const NorthlightMeshPageKey::KeySet& resident) {
    ElisionStats stats;
    if(resident.empty())return stats;
    for(auto& page:plan.pages) {
        if(page.elided||!resident.count(page.key))continue;
        stats.bytes+=page.vertices.capacity()*sizeof(NorthlightGI::WorldVertex)+page.indices.capacity()*sizeof(uint32_t);
        std::vector<NorthlightGI::WorldVertex>().swap(page.vertices);std::vector<uint32_t>().swap(page.indices);
        page.elided=true;++stats.pages;
    }
    return stats;
}

// Cheap render-side validation: individual indices/vertex attributes were
// validated during immutable worker preparation. Never walk millions of them
// again when publishing a new scene. Pages and batches are in append order.
inline const char* validatePages(const NorthlightWorldMesh::WorldMeshUploadPlan& input,const Plan& pages,const std::vector<NorthlightWorldMesh::MaterialUpload>& materials) {
    if(!input.vertexCount||!input.triangleCount||materials.empty()||pages.pages.empty()||pages.batches.empty())return "empty paged upload plan";
    if(input.vertexBytes!=uint64_t(input.vertexCount)*sizeof(NorthlightGI::WorldVertex)||input.vertexBytes>UINT32_MAX||
       input.indexBytes!=uint64_t(input.triangleCount)*3*sizeof(uint32_t)||input.indexBytes>UINT32_MAX)return "paged original buffer sizes mismatch";
    uint64_t vertexBytes=0,indexBytes=0;
    for(const auto& page:pages.pages) {
        // An elided page has no data; its key carries the exact sizes. Every other page must match its key.
        if(page.elided?!page.vertices.empty()||!page.indices.empty():page.key.vertexBytes!=page.vertices.size()*sizeof(NorthlightGI::WorldVertex)||page.key.indexBytes!=page.indices.size()*sizeof(uint32_t))return "mesh page key size mismatch";
        if(!page.vertexCount()||!page.indexCount()||page.key.vertexBytes%sizeof(NorthlightGI::WorldVertex)||page.key.indexBytes%sizeof(uint32_t)||page.vertexCount()>PageVertexLimit||page.indexCount()>PageIndexLimit||page.indexCount()%3)return "invalid mesh page size";
        vertexBytes+=page.key.vertexBytes;indexBytes+=page.key.indexBytes;
    }
    if(vertexBytes!=pages.vertexBytes||indexBytes!=pages.indexBytes||indexBytes!=input.indexBytes)return "paged aggregate sizes mismatch";
    size_t pageIndex=0;uint64_t nextIndex=0;
    for(const auto& batch:pages.batches) {
        if(pageIndex>=pages.pages.size()||batch.page!=pageIndex||batch.start!=nextIndex||!batch.count||batch.material>=materials.size())return "invalid paged batch range/material";
        const auto& page=pages.pages[pageIndex];
        if(!batch.vertexCount||uint64_t(batch.minVertex)+batch.vertexCount>page.vertexCount())return "invalid paged batch vertex range";
        nextIndex+=uint64_t(batch.count)*3;
        if(nextIndex>page.indexCount())return "paged batch exceeds page indices";
        if(nextIndex==page.indexCount()){++pageIndex;nextIndex=0;}
    }
    if(pageIndex!=pages.pages.size()||nextIndex)return "incomplete paged batch coverage";
    for(const auto& material:materials)
        if(!material.width||!material.height||material.width>4096||material.height>4096||uint64_t(material.width)*material.height*4!=material.bgra.size())return "invalid paged texture size";
    return nullptr;
}
inline const char* validatePages(const NorthlightWorldMesh::WorldMeshUploadPlan& input,const Plan& pages) {return validatePages(input,pages,input.materials);}
// Render thread only (shares the payload). A released payload is not a sealed plan any more.
inline const char* validateSealed(const NorthlightWorldMesh::WorldMeshUploadPlan& input) {
    if(!input.pagesSealed||input.source||input.ownedSource||input.pagingVertices||!input.indices.empty())return "paged ownership not sealed";
    const auto payload=input.payload.share();
    if(!payload||!payload->pages)return "paged payload released";
    return validatePages(input,*payload->pages,payload->materials);
}

// Must run on the worker before publication. Copy the small placement proofs
// first, then discard the temporary merged shadow scene and original packed
// indices. No GI scene is modified, including for a non-owning original plan.
// All potentially failing work precedes mutation of input.
inline bool seal(NorthlightWorldMesh::WorldMeshUploadPlan& input,std::shared_ptr<const Plan> pages,std::string& error,
                 const NorthlightGI::AllocationAdmission& admit = {}) {
    auto fail=[&](const char* reason){error=reason;return false;};
    if(input.pagesSealed||!input.verticesForPaging()||!pages)return fail("paged seal requires unsealed source and pages");
    if(input.pagingVertices&&(input.source||input.ownedSource))return fail("paged seal source ownership mismatch");
    if(input.ownedSource&&input.source!=input.ownedSource.get())return fail("paged seal source identity mismatch");
    if(input.verticesForPaging()->size()!=input.vertexCount||input.indices.size()!=uint64_t(input.triangleCount)*3)return fail("paged seal source counts mismatch");
    if(input.source&&(input.source->triangles.size()!=input.triangleCount||input.source->materials.size()!=input.materials.size()))return fail("paged seal source counts mismatch");
    if(const char* reason=validatePages(input,*pages))return fail(reason);
    try {
        const auto& sourceOwners=input.source?input.source->completePlacements:input.completePlacements;
        uint64_t ownerBytes=sourceOwners.size()*sizeof(NorthlightGI::WorldPlacementCoverage);
        for(const auto& owner:sourceOwners)ownerBytes+=owner.modelKey.size()+1;
        if(admit&&!admit(ownerBytes))return fail("paged ownership allocation deferred");
        auto owners=sourceOwners;
        const uint64_t cpuBytes=pages->cpuBytes();
        // 0.3.214 (task 42): pages and cutout textures move into the separately owned payload.
        auto payload=std::make_shared<NorthlightWorldMesh::UploadPayload>();
        payload->pageMetadataBytes=sizeof(*pages)+pages->pages.capacity()*sizeof(Page)+pages->batches.capacity()*sizeof(Batch);
        for(const auto& page:pages->pages)payload->pageStorageBytes+=page.vertices.capacity()*sizeof(NorthlightGI::WorldVertex)+page.indices.capacity()*sizeof(uint32_t);
        payload->materialMetadataBytes=input.materials.capacity()*sizeof(NorthlightWorldMesh::MaterialUpload);
        for(const auto& material:input.materials)payload->materialPixelBytes+=material.bgra.capacity();
        payload->bytes=cpuBytes+sizeof(NorthlightWorldMesh::UploadPayload)+input.materials.capacity()*sizeof(NorthlightWorldMesh::MaterialUpload)+64;
        for(const auto& material:input.materials)payload->bytes+=material.bgra.capacity();
        payload->pages=std::move(pages);payload->materials.swap(input.materials);
        input.completePlacements=std::move(owners);input.payload.set(std::move(payload));input.pagedCpuBytes=cpuBytes;
        input.source=nullptr;input.ownedSource.reset();input.pagingVertices.reset();std::vector<uint32_t>().swap(input.indices);
        input.pagesSealed=true;error.clear();return true;
    } catch(const std::bad_alloc&) {return fail("paged ownership allocation failed");}
      catch(const std::length_error&) {return fail("paged ownership size overflow");}
}
} // namespace NorthlightWorldMeshPages
