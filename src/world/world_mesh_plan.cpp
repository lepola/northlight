#include "world_mesh_plan.h"
#include "world_mesh_pages.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <map>
#include <list>

namespace NorthlightWorldMesh {
namespace {
using NorthlightGI::Vec3;
bool finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
struct Group {uint64_t key=0;uint32_t count=0,cursor=0;};
struct ContentCell {
    int x=0,y=0,z=0,chunkX=-1,chunkY=-1;bool terrain=false;
    bool operator==(const ContentCell& b)const{return x==b.x&&y==b.y&&z==b.z&&chunkX==b.chunkX&&chunkY==b.chunkY&&terrain==b.terrain;}
};
struct CellHash {size_t operator()(const ContentCell& c)const{
    uint64_t h=1469598103934665603ull;
    for(auto v:{c.x,c.y,c.z,c.chunkX,c.chunkY,int(c.terrain)}){h^=uint32_t(v);h*=1099511628211ull;}return size_t(h);
}};
int cellCoordinate(float a,float b,float c){
    const double value=std::floor((double(a)+b+c)/(3.0*64.0));
    // Finite asset vertices can still exceed the indexing domain. A shared
    // overflow cell is conservative: it only causes extra invalidations.
    return value>=INT32_MIN&&value<=INT32_MAX?int(value):0;
}
NorthlightLocalShadowSignature::Digest materialIdentity(const NorthlightGI::WorldMaterial& meta,const NorthlightGI::WorldMaterial& m){
    NorthlightLocalShadowSignature::Hasher hash;
    hash.scalar(meta.alphaCutoff);hash.word(meta.addressU);hash.word(meta.addressV);
    // This is the upload's actual coverage texture, including implicit white.
    const bool cutout=meta.alphaCutoff>0&&!m.rgba.empty();
    hash.word(cutout?m.width:1);hash.word(cutout?m.height:1);
    if(cutout)for(size_t i=3;i<m.rgba.size();i+=4)hash.word(m.rgba[i]);else hash.word(255);
    return hash.finish();
}

bool terrainChunk(Vec3 a,Vec3 b,Vec3 c,int& x,int& y){
    // Identical grid origin, step and tolerance to terrain_capture_bounds.h.
    constexpr double origin=17066.666666666666,step=100.0/3.0,tolerance=.03;
    double centerX=(double(a.x)+b.x+c.x)/3,centerY=(double(a.y)+b.y+c.y)/3;
    double gx=std::floor((origin-centerY)/step),gy=std::floor((origin-centerX)/step);
    if(gx<0||gx>=1024||gy<0||gy>=1024)return false;
    int cx=int(gx),cy=int(gy);
    double lowX=origin-(cy+1)*step,highX=origin-cy*step;
    double lowY=origin-(cx+1)*step,highY=origin-cx*step;
    for(Vec3 p:{a,b,c})if(p.x<lowX-tolerance||p.x>highX+tolerance||p.y<lowY-tolerance||p.y>highY+tolerance)return false;
    x=cx;y=cy;return true;
}
// Sort key: [cell 22][low 42]. low = (y+1, x+1, material) chunk-major or (material, y+1, x+1) material-major.
constexpr uint32_t MaterialBits=20,NoCell=0x3FFFFF;
uint64_t key(uint32_t cell,uint32_t material,int x,int y,bool chunkMajor){
    const uint64_t chunk=(uint64_t(y+1)<<11)|uint64_t(x+1);
    return (uint64_t(cell)<<42)|(chunkMajor?(chunk<<MaterialBits)|material:(uint64_t(material)<<22)|chunk);
}
// Coarse cell of a triangle's centroid in the terrain chunk grid (terrain_capture_bounds.h convention).
uint32_t spatialCell(Vec3 a,Vec3 b,Vec3 c,unsigned cellChunks){
    if(!cellChunks)return 0;
    constexpr double origin=17066.666666666666,step=100.0/3.0;
    const double gx=std::floor((origin-(double(a.y)+b.y+c.y)/3)/step),gy=std::floor((origin-(double(a.x)+b.x+c.x)/3)/step);
    if(gx<0||gx>=1024||gy<0||gy>=1024)return NoCell;
    return (uint32_t(gy)/cellChunks<<11)|(uint32_t(gx)/cellChunks);
}
}

static bool buildCore(const NorthlightGI::WorldScene& scene,WorldMeshUploadPlan& output,std::string& error,const NorthlightGI::AllocationAdmission& admit,const std::vector<NorthlightGI::WorldMaterial>* textures,const PlanOrder& order,
    size_t first=0,size_t count=SIZE_MAX,bool geometryOnly=false,const std::vector<uint32_t>* textureMap=nullptr,
    const std::vector<NorthlightLocalShadowSignature::Digest>* preparedIdentities=nullptr,std::vector<uint32_t>* batchCells=nullptr,std::vector<ContentCell>* recordCells=nullptr,std::vector<uint32_t>* triangleOrder=nullptr,bool validateVertices=true){
    const size_t triangleCount=count==SIZE_MAX?scene.triangles.size():count;
    if(first>scene.triangles.size()||triangleCount>scene.triangles.size()-first){error="Invalid prepared triangle range";return false;}
    static_assert(sizeof(NorthlightGI::WorldVertex)==32,"D3D world vertex layout must remain 32 bytes");
    error.clear();WorldMeshUploadPlan next;
    const uint64_t vertexBytes=uint64_t(scene.vertices.size())*sizeof(NorthlightGI::WorldVertex);
    const uint64_t indexBytes=uint64_t(triangleCount)*3*sizeof(uint32_t);
    if(vertexBytes>UINT32_MAX||indexBytes>UINT32_MAX||scene.materials.size()>UINT32_MAX){
        error="Mesh upload exceeds D3D9 32-bit buffer size";return false;
    }
    if(validateVertices)for(const auto& v:scene.vertices)if(!finite(v.position)||!finite(v.normal)||!std::isfinite(v.u)||!std::isfinite(v.v)){
        error="Nonfinite upload vertex";return false;
    }
    // Texel data of material i (width, height, rgba): textures[i] when supplied, else the scene's own.
    auto texel=[&](size_t i)->const NorthlightGI::WorldMaterial&{return textureMap?(*textures)[(*textureMap)[i]]:textures&&i<textures->size()?(*textures)[i]:scene.materials[i];};
    if(!textureMap&&textures&&textures->size()>scene.materials.size()){error="Invalid upload material";return false;}
    for(size_t i=0;i<scene.materials.size();++i){
        const auto& m=scene.materials[i];const auto& x=texel(i);
        if(x.width>4096||x.height>4096||((x.width==0)!=(x.height==0))||
           uint64_t(x.width)*x.height*4!=x.rgba.size()||!std::isfinite(m.alphaCutoff)||m.alphaCutoff<0||m.alphaCutoff>1){
            error="Invalid upload material";return false;
        }
    }
    auto allowed=[&](uint64_t bytes){if(!admit||admit(bytes))return true;error="Geometry allocation deferred";return false;};
    if(scene.materials.size()>=(1u<<MaterialBits)){error="Too many upload materials";return false;}
    try{
        auto records=std::make_shared<NorthlightLocalShadowSignature::Records>();
        std::unordered_map<ContentCell,uint32_t,CellHash> cells;
        cells.reserve(4096);
        ContentCell previousCell;uint32_t previousRecord=0;bool previousValid=false;
        std::vector<NorthlightLocalShadowSignature::Digest> materialIdentities;
        if(!allowed(scene.materials.size()*sizeof(NorthlightLocalShadowSignature::Digest)))return false;
        materialIdentities.reserve(scene.materials.size());
        for(size_t i=0;i<scene.materials.size();++i)materialIdentities.push_back(preparedIdentities?(*preparedIdentities)[i]:materialIdentity(scene.materials[i],texel(i)));
        // Group counts first, then prefix allocation and stable linear scatter.
        // This avoids one temporary vector/allocation chain for every batch and
        // moves all sorting/packing away from the game's render thread.
        if(!allowed(triangleCount*sizeof(uint64_t)))return false;
        std::vector<uint64_t> keys;keys.reserve(triangleCount);
        std::unordered_map<uint64_t,uint32_t> counts;
        counts.reserve(std::min<size_t>(scene.materials.size()+512,16384));
        uint64_t previousKey=0;uint32_t* previousCount=nullptr;
        for(size_t triangle=first;triangle<first+triangleCount;++triangle){const auto& t=scene.triangles[triangle];
            if(t.v0>=scene.vertices.size()||t.v1>=scene.vertices.size()||t.v2>=scene.vertices.size()||t.material>=scene.materials.size()){
                error="Invalid upload triangle index";return false;
            }
            int x=-1,y=-1;
            if(scene.materials[t.material].terrain&&!terrainChunk(scene.vertices[t.v0].position,scene.vertices[t.v1].position,scene.vertices[t.v2].position,x,y))
                ++next.unmaskableTerrainTriangles;
            const uint32_t cell=spatialCell(scene.vertices[t.v0].position,scene.vertices[t.v1].position,scene.vertices[t.v2].position,order.cellChunks);
            uint64_t k=key(cell,t.material,x,y,order.chunkMajor);keys.push_back(k);
            if(!previousCount||k!=previousKey){previousCount=&counts[k];previousKey=k;}
            ++*previousCount; // unordered_map references survive a rehash
        }
        std::vector<Group> groups;groups.reserve(counts.size());
        for(const auto& pair:counts)groups.push_back({pair.first,pair.second,0});
        std::sort(groups.begin(),groups.end(),[](const Group&a,const Group&b){return a.key<b.key;});
        std::unordered_map<uint64_t,uint32_t> lookup;lookup.reserve(groups.size());
        if(!allowed(triangleCount*3*sizeof(uint32_t)))return false;
        next.indices.resize(triangleCount*3);if(triangleOrder)triangleOrder->resize(triangleCount);
        if(!allowed(groups.size()*sizeof(Batch)))return false;next.batches.reserve(groups.size());
        uint32_t offset=0;
        for(uint32_t i=0;i<groups.size();++i){
            auto& g=groups[i];g.cursor=offset;lookup.emplace(g.key,i);
            const uint64_t low=g.key&((1ull<<42)-1);
            const uint32_t material=order.chunkMajor?uint32_t(low&((1u<<MaterialBits)-1)):uint32_t(low>>22);
            const uint64_t chunk=order.chunkMajor?low>>MaterialBits:low&((1u<<22)-1);
            int x=int(chunk&2047)-1,y=int((chunk>>11)&2047)-1;
            next.batches.push_back({offset,g.count,material,x,y,scene.materials[material].terrain});if(batchCells)batchCells->push_back(uint32_t(g.key>>42));offset+=g.count*3;
        }
        uint32_t groupIndex=0;
        for(size_t i=0;i<triangleCount;++i){
            if(!i||keys[i]!=previousKey){groupIndex=lookup.at(keys[i]);previousKey=keys[i];}
            auto& group=groups[groupIndex];auto& batch=next.batches[groupIndex];const auto& t=scene.triangles[first+i];
            if(triangleOrder)(*triangleOrder)[group.cursor/3]=uint32_t(i);next.indices[group.cursor++]=t.v0;next.indices[group.cursor++]=t.v1;next.indices[group.cursor++]=t.v2;
            NorthlightLocalShadowSignature::Record* record=nullptr;
            if(records){
                const auto a=scene.vertices[t.v0].position,b=scene.vertices[t.v1].position,c=scene.vertices[t.v2].position;
                ContentCell cell{cellCoordinate(a.x,b.x,c.x),cellCoordinate(a.y,b.y,c.y),cellCoordinate(a.z,b.z,c.z),batch.chunkX,batch.chunkY,batch.terrain};
                auto found=previousValid&&cell==previousCell?cells.end():cells.find(cell);
                if(previousValid&&cell==previousCell)record=&(*records)[previousRecord];
                else if(found==cells.end()){
                    // Optional metadata must not become an unbounded allocation.
                    // Null records retain the old generation invalidation rule.
                    if(records->size()>=65536){records.reset();cells.clear();if(recordCells)recordCells->clear();}
                    else{
                        if(records->size()==records->capacity()){
                            size_t capacity=std::max<size_t>(geometryOnly?1:64,records->capacity()*2);
                            if(!allowed(capacity*sizeof(NorthlightLocalShadowSignature::Record)))return false;
                            records->reserve(capacity);
                        }
                        const uint32_t id=uint32_t(records->size());cells.emplace(cell,id);records->emplace_back();
                        if(recordCells)recordCells->push_back(cell);record=&records->back();record->chunkX=batch.chunkX;record->chunkY=batch.chunkY;record->terrain=batch.terrain;
                        previousCell=cell;previousRecord=id;previousValid=true;
                    }
                }else {record=&(*records)[found->second];previousCell=cell;previousRecord=found->second;previousValid=true;}
            }
            NorthlightLocalShadowSignature::Hasher content(materialIdentities[t.material]);
            for(uint32_t vertex:{t.v0,t.v1,t.v2}){
                const auto& v=scene.vertices[vertex];const auto p=v.position;
                batch.boundsLow.x=std::min(batch.boundsLow.x,p.x);batch.boundsHigh.x=std::max(batch.boundsHigh.x,p.x);
                batch.boundsLow.y=std::min(batch.boundsLow.y,p.y);batch.boundsHigh.y=std::max(batch.boundsHigh.y,p.y);
                batch.boundsLow.z=std::min(batch.boundsLow.z,p.z);batch.boundsHigh.z=std::max(batch.boundsHigh.z,p.z);
                if(record){
                    record->low.x=std::min(record->low.x,p.x);record->high.x=std::max(record->high.x,p.x);
                    record->low.y=std::min(record->low.y,p.y);record->high.y=std::max(record->high.y,p.y);
                    record->low.z=std::min(record->low.z,p.z);record->high.z=std::max(record->high.z,p.z);
                    content.scalar(p.x);content.scalar(p.y);content.scalar(p.z);content.scalar(v.u);content.scalar(v.v);
                }
            }
            if(record)record->content.add(content.finish(1));
        }
        next.localShadowRecords=std::move(records);
        // Per-triangle sort keys are dead after scatter. In a city they can
        // otherwise overlap another ~16 MiB of cutout upload textures.
        std::vector<uint64_t>().swap(keys);
        if(!geometryOnly){next.materials.resize(scene.materials.size());
        for(size_t i=0;i<scene.materials.size();++i){
            const auto& meta=scene.materials[i];const auto& source=texel(i);auto& dest=next.materials[i];
            dest.alphaCutoff=meta.alphaCutoff;dest.terrain=meta.terrain;
            if(meta.alphaCutoff<=0||source.rgba.empty())dest.bgra={255,255,255,255};
            else{
                dest.width=source.width;dest.height=source.height;if(!allowed(source.rgba.size()))return false;dest.bgra.resize(source.rgba.size());
                for(size_t p=0;p<source.rgba.size();p+=4){dest.bgra[p]=source.rgba[p+2];dest.bgra[p+1]=source.rgba[p+1];dest.bgra[p+2]=source.rgba[p];dest.bgra[p+3]=source.rgba[p+3];}
            }
            NorthlightLocalShadowSignature::Hasher textureHash;
            textureHash.word(dest.width);textureHash.word(dest.height);
            for(size_t p=0;p<dest.bgra.size();p+=4){uint32_t pixel;std::memcpy(&pixel,dest.bgra.data()+p,4);textureHash.word(pixel);}
            dest.textureIdentity=textureHash.finish();
            next.textureBytes+=dest.bgra.size();
        }}
    }catch(...){error="Cannot allocate world mesh upload plan";return false;}
    next.source=&scene;next.vertexCount=uint32_t(scene.vertices.size());next.triangleCount=uint32_t(triangleCount);
    next.vertexBytes=vertexBytes;next.indexBytes=indexBytes;
    output=std::move(next);return true;
}
namespace {
using PreparationCharge=NorthlightGI::SceneMemoryCharge;
struct PreparedGeometry {
    WorldMeshUploadPlan plan;
    std::vector<uint16_t> order16;std::vector<uint32_t> order32,batchCells;
    std::vector<ContentCell> recordCells;
    std::vector<NorthlightLocalShadowSignature::Digest> materials;
    std::weak_ptr<const void> source;bool weakIdentity=false;
    std::shared_ptr<const PreparationCharge> charge;
    uint32_t triangleAt(size_t i)const{return order16.empty()?order32[i]:order16[i];}
    uint64_t bytes()const{return sizeof(*this)+plan.cpuBytes()+order16.capacity()*2+(order32.capacity()+batchCells.capacity())*4+recordCells.capacity()*sizeof(ContentCell)+materials.capacity()*sizeof(materials[0])+256;}
};
struct PreparedPart {std::shared_ptr<const PreparedGeometry> data;uint32_t first=0;std::vector<uint32_t> materials;};
}
struct PreparationCache::Impl {
    std::shared_ptr<NorthlightGI::SceneMemoryBudget> budget=std::make_shared<NorthlightGI::SceneMemoryBudget>(UINT64_MAX);
    uint64_t limit=0;
    struct PageMemo {std::shared_ptr<const PreparationLayout> layout;NorthlightWorldMeshPages::Plan pages;std::shared_ptr<const PreparationCharge> charge;};
    std::shared_ptr<const PageMemo> pageMemo;
    struct Entry {std::shared_ptr<const PreparedGeometry> data;std::list<std::string>::iterator recent;};
    std::map<std::string,Entry> pieces;
    std::list<std::string> recent;
    Stats stats;
    auto erase(std::map<std::string,Entry>::iterator i){recent.erase(i->second.recent);return pieces.erase(i);}
    void evict(){erase(pieces.find(recent.front()));}
    void clean(){for(auto i=pieces.begin();i!=pieces.end();)if(i->second.data->weakIdentity&&i->second.data->source.expired())i=erase(i);else ++i;}
    void trim(){while(!pieces.empty()&&budget->used.load(std::memory_order_relaxed)>limit)evict();}
    std::shared_ptr<const PreparationCharge> charge(uint64_t bytes){
        if(bytes>limit)return {};
        if(budget->used.load(std::memory_order_relaxed)>limit-bytes)pageMemo.reset();
        while(!pieces.empty()&&budget->used.load(std::memory_order_relaxed)>limit-bytes)evict();
        if(budget->used.load(std::memory_order_relaxed)>limit-bytes)return {};
        return NorthlightGI::SceneMemoryCharge::acquire(budget,bytes);
    }
};
PreparationCache::PreparationCache():impl_(new Impl){}
PreparationCache::~PreparationCache()=default;
void PreparationCache::setLimit(uint64_t bytes){impl_->limit=bytes;if(impl_->budget->used.load()>bytes)impl_->pageMemo.reset();impl_->clean();impl_->trim();impl_->stats.bytes=impl_->budget->used.load();impl_->stats.entries=impl_->pieces.size();}
void PreparationCache::reset(){impl_->pageMemo.reset();impl_->pieces.clear();impl_->recent.clear();impl_->stats.bytes=impl_->budget->used.load();impl_->stats.entries=0;}
const PreparationCache::Stats& PreparationCache::stats()const{impl_->stats.bytes=impl_->budget->used.load(std::memory_order_relaxed);impl_->stats.entries=impl_->pieces.size();return impl_->stats;}
bool PreparationCache::reuseResident(const WorldMeshUploadPlan& input,NorthlightWorldMeshPages::Plan& output,const NorthlightMeshPageKey::KeySet& resident,const NorthlightGI::AllocationAdmission& admit){
    const auto memo=impl_->pageMemo;
    if(!memo||!input.preparationLayout||memo->layout->size()!=input.preparationLayout->size())return false;
    for(size_t i=0;i<memo->layout->size();++i){const auto& a=(*memo->layout)[i];const auto& b=(*input.preparationLayout)[i];
        if(a.owner!=b.owner||a.firstTriangle!=b.firstTriangle||a.triangleCount!=b.triangleCount||a.materials!=b.materials)return false;}
    for(const auto& page:memo->pages.pages)if(!resident.count(page.key))return false;
    // Memo pages have no CPU vertex/index payload. Active GPU residency is
    // checked on every use; a lost/evicted page falls back to ordinary packing.
    if(admit&&!admit(memo->pages.pages.size()*sizeof(NorthlightWorldMeshPages::Page)+memo->pages.batches.size()*sizeof(NorthlightWorldMeshPages::Batch)))return false;
    try{auto next=memo->pages;
    if(NorthlightWorldMeshPages::validatePages(input,next))return false;
    ++impl_->stats.pageHits;impl_->stats.reusedPages+=next.pages.size();impl_->stats.reusedPageBytes+=next.vertexBytes+next.indexBytes;
    output=std::move(next);return true;}catch(const std::bad_alloc&){return false;}
}
void PreparationCache::rememberPages(const WorldMeshUploadPlan& input,const NorthlightWorldMeshPages::Plan& pages){
    if(!input.preparationLayout||NorthlightWorldMeshPages::validatePages(input,pages))return;
    try{
        uint64_t bytes=sizeof(Impl::PageMemo)+pages.pages.size()*sizeof(NorthlightWorldMeshPages::Page)+pages.batches.size()*sizeof(NorthlightWorldMeshPages::Batch)+64;
        bytes+=sizeof(PreparationLayout)+input.preparationLayout->capacity()*sizeof(PreparationIdentity)+64;for(const auto& part:*input.preparationLayout)bytes+=part.materials.capacity()*4;
        auto charge=impl_->charge(bytes);if(!charge)return;
        auto memo=std::make_shared<Impl::PageMemo>();memo->charge=std::move(charge);memo->layout=input.preparationLayout;
        memo->pages.vertexBytes=pages.vertexBytes;memo->pages.indexBytes=pages.indexBytes;memo->pages.batches=pages.batches;memo->pages.pages.reserve(pages.pages.size());
        for(const auto& page:pages.pages){memo->pages.pages.emplace_back();auto& p=memo->pages.pages.back();p.key=page.key;p.elided=true;}
        impl_->pageMemo=std::move(memo);impl_->stats.bytes=impl_->budget->used.load();
    }catch(const std::bad_alloc&){/* Optional acceleration never refuses a prepared upload. */}
}

bool buildUploadPlan(const NorthlightGI::WorldScene& scene,WorldMeshUploadPlan& output,std::string& error,const NorthlightGI::AllocationAdmission& admit,const std::vector<NorthlightGI::WorldMaterial>* textures,const PlanOrder& order,const std::vector<PreparationPiece>* sources,PreparationCache* preparation){
    if(!preparation||!sources||sources->empty()||!preparation->impl_->limit)return buildCore(scene,output,error,admit,textures,order);
    const auto buildShared=[&]()->bool {
    auto& cache=*preparation->impl_;cache.clean();
    auto allowed=[&](uint64_t n){if(!admit||admit(n))return true;error="Geometry allocation deferred";return false;};
    try{
        if(uint64_t(scene.vertices.size())*sizeof(NorthlightGI::WorldVertex)>UINT32_MAX||uint64_t(scene.triangles.size())*12>UINT32_MAX||scene.materials.size()>UINT32_MAX){error="Mesh upload exceeds D3D9 32-bit buffer size";return false;}
        if(scene.materials.size()>=(1u<<MaterialBits)){error="Too many upload materials";return false;}
        // The public builder keeps the baseline validation, including unreferenced
        // vertices. Reused range preparation may then trust these immutable bytes.
        for(const auto& v:scene.vertices)if(!finite(v.position)||!finite(v.normal)||!std::isfinite(v.u)||!std::isfinite(v.v)){error="Nonfinite upload vertex";return false;}
        if(textures&&textures->size()>scene.materials.size()){error="Invalid upload material";return false;}
        std::vector<NorthlightLocalShadowSignature::Digest> identities;
        identities.reserve(scene.materials.size());
        for(size_t i=0;i<scene.materials.size();++i){const auto& m=scene.materials[i];const auto& x=textures&&i<textures->size()?(*textures)[i]:m;
            if(x.width>4096||x.height>4096||((x.width==0)!=(x.height==0))||uint64_t(x.width)*x.height*4!=x.rgba.size()||!std::isfinite(m.alphaCutoff)||m.alphaCutoff<0||m.alphaCutoff>1){error="Invalid upload material";return false;}
            identities.push_back(materialIdentity(m,x));}
        uint64_t end=0;
        for(const auto& p:*sources){if(!p.triangleCount||p.firstTriangle<end||uint64_t(p.firstTriangle)+p.triangleCount>scene.triangles.size())return buildCore(scene,output,error,admit,textures,order);end=uint64_t(p.firstTriangle)+p.triangleCount;
            for(auto material:p.materials)if(material!=UINT32_MAX&&material>=scene.materials.size())return buildCore(scene,output,error,admit,textures,order);}
        auto sourceKey=[&](const PreparationPiece& source){std::string result=source.identity;result.append(reinterpret_cast<const char*>(&order.cellChunks),sizeof(order.cellChunks));result.push_back(char(order.chunkMajor));return result;};
        // Protect current-region hits before an early boundary miss can evict a
        // common directional piece that appears later in legacy triangle order.
        for(const auto& source:*sources){auto found=cache.pieces.find(sourceKey(source));if(found!=cache.pieces.end())cache.recent.splice(cache.recent.end(),cache.recent,found->second.recent);}
        std::vector<PreparedPart> parts;bool allProven=true;
        auto append=[&](uint32_t first,uint32_t count,const PreparationPiece* source)->bool{
            if(!count)return true;
            std::vector<uint32_t> map;
            if(source)map=source->materials;else {allProven=false;map.resize(scene.materials.size());for(uint32_t i=0;i<map.size();++i)map[i]=i;}
            std::vector<NorthlightLocalShadowSignature::Digest> digests;digests.reserve(map.size());
            for(auto i:map){auto digest=i==UINT32_MAX?NorthlightLocalShadowSignature::Digest{}:identities[i];if(i!=UINT32_MAX)digest.triangles=scene.materials[i].terrain;digests.push_back(digest);}
            std::string key;if(source)key=sourceKey(*source);
            auto found=source?cache.pieces.find(key):cache.pieces.end();std::shared_ptr<const PreparedGeometry> prepared;
            if(found!=cache.pieces.end()&&found->second.data->plan.triangleCount==count&&found->second.data->materials==digests&&(!found->second.data->weakIdentity||!found->second.data->source.expired())){
                prepared=found->second.data;cache.recent.splice(cache.recent.end(),cache.recent,found->second.recent);++cache.stats.hits;cache.stats.reusedTriangles+=count;
            }else if(found!=cache.pieces.end())cache.erase(found);
            if(!prepared){
                ++cache.stats.misses;cache.stats.preparedTriangles+=count;
                auto next=std::make_shared<PreparedGeometry>();
                if(!buildCore(scene,next->plan,error,admit,textures,order,first,count,true,nullptr,&identities,&next->batchCells,&next->recordCells,&next->order32,false))return false;
                for(auto& batch:next->plan.batches){auto slot=std::find(map.begin(),map.end(),batch.material);if(slot==map.end()){error="Prepared range material provenance mismatch";return false;}batch.material=uint32_t(slot-map.begin());}
                next->plan.source=nullptr;std::vector<uint32_t>().swap(next->plan.indices);
                if(count<=65536){if(!allowed(next->order32.size()*2))return false;next->order16.reserve(next->order32.size());for(uint32_t i:next->order32)next->order16.push_back(uint16_t(i));std::vector<uint32_t>().swap(next->order32);}
                next->materials=std::move(digests);if(source){next->source=source->owner;next->weakIdentity=source->weakIdentity;}
                if(source&&!key.empty()){
                    // Both the ordered lookup and LRU node own the identity string.
                    next->charge=cache.charge(next->bytes()+2*(key.capacity()+1));
                    if(next->charge){cache.recent.push_back(key);try{cache.pieces.emplace(key,PreparationCache::Impl::Entry{next,std::prev(cache.recent.end())});}catch(...){cache.recent.pop_back();throw;}}
                }
                prepared=std::move(next);
            }
            if(!prepared->charge)allProven=false;parts.push_back({std::move(prepared),first,std::move(map)});return true;
        };
        uint32_t first=0;
        for(const auto& source:*sources){if(!append(first,source.firstTriangle-first,nullptr)||!append(source.firstTriangle,source.triangleCount,&source))return false;first=source.firstTriangle+source.triangleCount;}
        if(!append(first,uint32_t(scene.triangles.size()-first),nullptr))return false;
        struct Span {size_t part=0,batch=0;};
        std::map<uint64_t,std::vector<Span>> groups;
        for(size_t i=0;i<parts.size();++i){const auto& p=parts[i];for(size_t j=0;j<p.data->plan.batches.size();++j){const auto& b=p.data->plan.batches[j];groups[key(p.data->batchCells[j],p.materials[b.material],b.chunkX,b.chunkY,order.chunkMajor)].push_back({i,j});}}
        WorldMeshUploadPlan next;
        if(!allowed(scene.triangles.size()*12)||!allowed(groups.size()*sizeof(Batch)))return false;
        next.indices.reserve(scene.triangles.size()*3);next.batches.reserve(groups.size());
        for(const auto& group:groups){Batch batch;batch.start=uint32_t(next.indices.size());const auto& head=parts[group.second[0].part];const auto& original=head.data->plan.batches[group.second[0].batch];batch.material=head.materials[original.material];batch.chunkX=original.chunkX;batch.chunkY=original.chunkY;batch.terrain=original.terrain;
            for(const auto& span:group.second){const auto& p=parts[span.part];const auto& b=p.data->plan.batches[span.batch];batch.count+=b.count;
                for(uint32_t j=b.start/3;j<b.start/3+b.count;++j){const auto& t=scene.triangles[p.first+p.data->triangleAt(j)];next.indices.insert(next.indices.end(),{t.v0,t.v1,t.v2});}
                batch.boundsLow.x=std::min(batch.boundsLow.x,b.boundsLow.x);batch.boundsLow.y=std::min(batch.boundsLow.y,b.boundsLow.y);batch.boundsLow.z=std::min(batch.boundsLow.z,b.boundsLow.z);
                batch.boundsHigh.x=std::max(batch.boundsHigh.x,b.boundsHigh.x);batch.boundsHigh.y=std::max(batch.boundsHigh.y,b.boundsHigh.y);batch.boundsHigh.z=std::max(batch.boundsHigh.z,b.boundsHigh.z);}
            next.batches.push_back(batch);}
        auto records=std::make_shared<NorthlightLocalShadowSignature::Records>();std::unordered_map<ContentCell,size_t,CellHash> cells;
        for(const auto& p:parts){next.unmaskableTerrainTriangles+=p.data->plan.unmaskableTerrainTriangles;
            if(!records)continue;
            const auto& values=p.data->plan.localShadowRecords;if(!values){records.reset();continue;}
            for(size_t i=0;i<values->size();++i){const auto& cell=p.data->recordCells[i];const auto& value=(*values)[i];auto found=cells.find(cell);
                if(found==cells.end()){if(records->size()>=65536){records.reset();break;}if(!allowed((records->size()+1)*sizeof(value)))return false;cells.emplace(cell,records->size());records->push_back(value);}
                else{auto& record=(*records)[found->second];record.content.add(value.content);
                    record.low.x=std::min(record.low.x,value.low.x);record.low.y=std::min(record.low.y,value.low.y);record.low.z=std::min(record.low.z,value.low.z);
                    record.high.x=std::max(record.high.x,value.high.x);record.high.y=std::max(record.high.y,value.high.y);record.high.z=std::max(record.high.z,value.high.z);}}
        }
        next.localShadowRecords=std::move(records);
        // Texture upload remains one global operation; range caches retain no RGBA/BGRA.
        next.materials.resize(scene.materials.size());
        for(size_t i=0;i<scene.materials.size();++i){const auto& meta=scene.materials[i];const auto& source=textures&&i<textures->size()?(*textures)[i]:meta;auto& dest=next.materials[i];dest.alphaCutoff=meta.alphaCutoff;dest.terrain=meta.terrain;
            if(meta.alphaCutoff<=0||source.rgba.empty())dest.bgra={255,255,255,255};else{dest.width=source.width;dest.height=source.height;if(!allowed(source.rgba.size()))return false;dest.bgra.resize(source.rgba.size());for(size_t p=0;p<source.rgba.size();p+=4){dest.bgra[p]=source.rgba[p+2];dest.bgra[p+1]=source.rgba[p+1];dest.bgra[p+2]=source.rgba[p];dest.bgra[p+3]=source.rgba[p+3];}}
            NorthlightLocalShadowSignature::Hasher textureHash;textureHash.word(dest.width);textureHash.word(dest.height);for(size_t p=0;p<dest.bgra.size();p+=4){uint32_t pixel;std::memcpy(&pixel,dest.bgra.data()+p,4);textureHash.word(pixel);}dest.textureIdentity=textureHash.finish();next.textureBytes+=dest.bgra.size();}
        if(allProven){auto layout=std::make_shared<PreparationLayout>();for(const auto& p:parts)layout->push_back({p.data,p.first,p.data->plan.triangleCount,p.data->charge->bytes,p.materials});next.preparationLayout=std::move(layout);}
        next.source=&scene;next.vertexCount=uint32_t(scene.vertices.size());next.triangleCount=uint32_t(scene.triangles.size());next.vertexBytes=scene.vertices.size()*sizeof(NorthlightGI::WorldVertex);next.indexBytes=scene.triangles.size()*12;
        cache.stats.bytes=cache.budget->used.load(std::memory_order_relaxed);cache.stats.entries=cache.pieces.size();output=std::move(next);error.clear();return true;
    }catch(...){error="Cannot allocate shared mesh preparation";return false;}
    };
    if(buildShared())return true;
    // Optional acceleration can require different transient blocks. Its refusal
    // must not remove geometry that the baseline builder can still prepare.
    preparation->reset();return buildCore(scene,output,error,admit,textures,order);
}
} // namespace NorthlightWorldMesh
