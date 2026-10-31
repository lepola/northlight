#include "world_mesh_plan.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#include <unordered_map>
#include <utility>

namespace NorthlightWorldMesh {
void WorldMeshUploadPlan::accountMemory(NorthlightMemoryFootprint::Counter& memory,bool includeObject)const{
    using namespace NorthlightMemoryFootprint;
    if(includeObject){if(!memory.visit(this))return;memory.add(Metadata,sizeof(*this));}
    memory.vector(Upload,indices);memory.vector(Metadata,batches);memory.vector(Metadata,materials);
    for(const auto& material:materials)memory.vector(Texels,material.bgra);
    memory.vector(Metadata,completePlacements);for(const auto& p:completePlacements)memory.string(p.modelKey);
    for(const auto& chunk:fixedTerrainChunks)memory.allocation(Metadata,&chunk,sizeof(chunk));
    if(localShadowRecords){memory.allocation(Metadata,localShadowRecords.get(),sizeof(*localShadowRecords));memory.vector(Metadata,*localShadowRecords);}
    if(ownedSource&&memory.visit(ownedSource.get())){memory.add(Metadata,sizeof(*ownedSource));ownedSource->accountMemory(memory);}
    if(pagingVertices){memory.allocation(Metadata,pagingVertices.get(),sizeof(*pagingVertices));memory.vector(Upload,*pagingVertices);}
    const auto upload=payload.share();
    if(upload&&memory.visit(upload.get())){
        memory.add(Metadata,sizeof(*upload));
        if(upload->pages&&memory.visit(upload->pages.get())){memory.add(Metadata,upload->pageMetadataBytes);memory.add(Upload,upload->pageStorageBytes);}
        if(memory.visit(&upload->materials)){memory.add(Metadata,upload->materialMetadataBytes);memory.add(Texels,upload->materialPixelBytes);}
    }
}

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

bool buildUploadPlan(const NorthlightGI::WorldScene& scene,WorldMeshUploadPlan& output,std::string& error,const NorthlightGI::AllocationAdmission& admit,const std::vector<NorthlightGI::WorldMaterial>* textures,const PlanOrder& order){
    static_assert(sizeof(NorthlightGI::WorldVertex)==32,"D3D world vertex layout must remain 32 bytes");
    error.clear();WorldMeshUploadPlan next;
    const uint64_t vertexBytes=uint64_t(scene.vertices.size())*sizeof(NorthlightGI::WorldVertex);
    const uint64_t indexBytes=uint64_t(scene.triangles.size())*3*sizeof(uint32_t);
    if(vertexBytes>UINT32_MAX||indexBytes>UINT32_MAX||scene.materials.size()>UINT32_MAX){
        error="Mesh upload exceeds D3D9 32-bit buffer size";return false;
    }
    for(const auto& v:scene.vertices)if(!finite(v.position)||!finite(v.normal)||!std::isfinite(v.u)||!std::isfinite(v.v)){
        error="Nonfinite upload vertex";return false;
    }
    // Texel data of material i (width, height, rgba): textures[i] when supplied, else the scene's own.
    auto texel=[&](size_t i)->const NorthlightGI::WorldMaterial&{return textures&&i<textures->size()?(*textures)[i]:scene.materials[i];};
    if(textures&&textures->size()>scene.materials.size()){error="Invalid upload material";return false;}
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
        for(size_t i=0;i<scene.materials.size();++i)materialIdentities.push_back(materialIdentity(scene.materials[i],texel(i)));
        // Group counts first, then prefix allocation and stable linear scatter.
        // This avoids one temporary vector/allocation chain for every batch and
        // moves all sorting/packing away from the game's render thread.
        if(!allowed(scene.triangles.size()*sizeof(uint64_t)))return false;
        std::vector<uint64_t> keys;keys.reserve(scene.triangles.size());
        std::unordered_map<uint64_t,uint32_t> counts;
        counts.reserve(std::min<size_t>(scene.materials.size()+512,16384));
        uint64_t previousKey=0;uint32_t* previousCount=nullptr;
        for(const auto& t:scene.triangles){
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
        if(!allowed(scene.triangles.size()*3*sizeof(uint32_t)))return false;
        next.indices.resize(scene.triangles.size()*3);
        if(!allowed(groups.size()*sizeof(Batch)))return false;next.batches.reserve(groups.size());
        uint32_t offset=0;
        for(uint32_t i=0;i<groups.size();++i){
            auto& g=groups[i];g.cursor=offset;lookup.emplace(g.key,i);
            const uint64_t low=g.key&((1ull<<42)-1);
            const uint32_t material=order.chunkMajor?uint32_t(low&((1u<<MaterialBits)-1)):uint32_t(low>>22);
            const uint64_t chunk=order.chunkMajor?low>>MaterialBits:low&((1u<<22)-1);
            int x=int(chunk&2047)-1,y=int((chunk>>11)&2047)-1;
            next.batches.push_back({offset,g.count,material,x,y,scene.materials[material].terrain});offset+=g.count*3;
        }
        uint32_t groupIndex=0;
        for(size_t i=0;i<scene.triangles.size();++i){
            if(!i||keys[i]!=previousKey){groupIndex=lookup.at(keys[i]);previousKey=keys[i];}
            auto& group=groups[groupIndex];auto& batch=next.batches[groupIndex];const auto& t=scene.triangles[i];
            next.indices[group.cursor++]=t.v0;next.indices[group.cursor++]=t.v1;next.indices[group.cursor++]=t.v2;
            NorthlightLocalShadowSignature::Record* record=nullptr;
            if(records){
                const auto a=scene.vertices[t.v0].position,b=scene.vertices[t.v1].position,c=scene.vertices[t.v2].position;
                ContentCell cell{cellCoordinate(a.x,b.x,c.x),cellCoordinate(a.y,b.y,c.y),cellCoordinate(a.z,b.z,c.z),batch.chunkX,batch.chunkY,batch.terrain};
                auto found=previousValid&&cell==previousCell?cells.end():cells.find(cell);
                if(previousValid&&cell==previousCell)record=&(*records)[previousRecord];
                else if(found==cells.end()){
                    // Optional metadata must not become an unbounded allocation.
                    // Null records retain the old generation invalidation rule.
                    if(records->size()>=65536){records.reset();cells.clear();}
                    else{
                        if(records->size()==records->capacity()){
                            size_t capacity=std::max<size_t>(64,records->capacity()*2);
                            if(!allowed(capacity*sizeof(NorthlightLocalShadowSignature::Record)))return false;
                            records->reserve(capacity);
                        }
                        const uint32_t id=uint32_t(records->size());cells.emplace(cell,id);records->emplace_back();
                        record=&records->back();record->chunkX=batch.chunkX;record->chunkY=batch.chunkY;record->terrain=batch.terrain;
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
        next.materials.resize(scene.materials.size());
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
        }
    }catch(...){error="Cannot allocate world mesh upload plan";return false;}
    next.source=&scene;next.vertexCount=uint32_t(scene.vertices.size());next.triangleCount=uint32_t(scene.triangles.size());
    next.vertexBytes=vertexBytes;next.indexBytes=indexBytes;
    output=std::move(next);return true;
}
} // namespace NorthlightWorldMesh
