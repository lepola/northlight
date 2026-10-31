#pragma once
// Worker-side, asset-derived distant shadow casters. Does not extend the GI
// BVH, change celestial directions, or depend on the camera's viewing angle.
#include "world_mesh_plan.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace NorthlightShadowTerrain {
using NorthlightGI::Vec3;
constexpr float Radius=928.f;
constexpr float LocalHorizontal=288.f,LocalVertical=320.f;
// Far cached cascade: radius 240 including its margin, depth half-span 640.
// Its enclosing sphere is <725u. Add 80u maximum camera orbit distance and
// 96u publication/upload applicability margin: 928u covers every direction.
// 0.3.169: the render thread holds a snapshot past 96u until its replacement
// applies (hard limit 160u, world_streaming.h). Between 123u and 160u, low-sun
// terrain casters at the extreme far-cascade corner can be missing for up to
// one build; the near cascade stays inside the local box.
constexpr double Origin=17066.666666666666,Chunk=100.0/3.0;
inline std::pair<int,int> chunk(Vec3 a,Vec3 b,Vec3 c){
    const double x=std::floor((Origin-(double(a.y)+b.y+c.y)/3)/Chunk);
    const double y=std::floor((Origin-(double(a.x)+b.x+c.x)/3)/Chunk);
    if(!std::isfinite(x)||!std::isfinite(y)||x<0||y<0||x>=1024||y>=1024)return {-1,-1};
    const double lx=Origin-(y+1)*Chunk,hx=Origin-y*Chunk,ly=Origin-(x+1)*Chunk,hy=Origin-x*Chunk;
    for(auto p:{a,b,c})if(p.x<lx-.03||p.x>hx+.03||p.y<ly-.03||p.y>hy+.03)return {-1,-1};
    return {int(x),int(y)};
}
inline bool outside(Vec3 lo,Vec3 hi,Vec3 a,Vec3 b){
    return hi.x<a.x||lo.x>b.x||hi.y<a.y||lo.y>b.y||hi.z<a.z||lo.z>b.z;
}
// 0.3.176 (U1c): the committed fixed-chunk set (unchanged, shared with the worker) plus a flat bitmap
// of the global chunk grid beside it, 1024x1024 bits (128 KiB, kept across commits), filled from the
// set in one pass. Coordinates outside the grid (-1: not one chunk) query the set, so membership is
// the set's. A failed allocation leaves the bitmap off: the set answers alone.
class FixedChunkBits {
    const std::set<std::pair<int,int>>* set_=nullptr;std::vector<std::uint64_t> bits_;bool on_=false;
    static constexpr int Grid=1024;
    static bool inGrid(int x,int y){return x>=0&&y>=0&&x<Grid&&y<Grid;}
    static std::size_t bit(int x,int y){return std::size_t(y)*Grid+std::size_t(x);}
public:
    void assign(const std::set<std::pair<int,int>>* set){
        set_=set;on_=false;if(!set)return;
        try{if(bits_.empty())bits_.assign(std::size_t(Grid)*Grid/64,0);else std::fill(bits_.begin(),bits_.end(),0);}catch(...){bits_.clear();return;}
        for(const auto& c:*set)if(inGrid(c.first,c.second)){const auto i=bit(c.first,c.second);bits_[i>>6]|=std::uint64_t(1)<<(i&63);}
        on_=true;
    }
    void reset(){set_=nullptr;on_=false;}
    // The set these bits describe (null: none).
    const std::set<std::pair<int,int>>* source()const{return set_;}
    bool contains(std::pair<int,int> c)const{
        if(!set_)return false;
        if(!on_||!inGrid(c.first,c.second))return set_->count(c)!=0;
        const auto i=bit(c.first,c.second);return (bits_[i>>6]>>(i&63))&1;
    }
};
// Snapshot positions/indices have already passed the terrain capture validator.
// Original indices remain intact for point lights; this is an appended stream.
// fixed: bool(std::pair<int,int> chunk), the fixed-chunk membership (0.3.176: a functor).
// triangleTests (0.3.176 D1, optional): counts the per-triangle chunk tests of a straddling snapshot.
template<class Snapshot,class Fixed> void appendLiveDirectionalBy(const Snapshot& snapshot,
        const Fixed& fixed,uint32_t offset,std::vector<uint32_t>& out,std::size_t* triangleTests=nullptr){
    bool anyFixed=false,anyLive=false;
    for(const auto& c:snapshot.bounds.chunks){if(fixed(std::pair<int,int>{c.x,c.y}))anyFixed=true;else anyLive=true;}
    if(!anyFixed){for(auto i:snapshot.indices)out.push_back(i+offset);return;}
    if(!anyLive)return;
    auto position=[&](uint32_t i){const auto& p=snapshot.positions[i];return Vec3(p.x,p.y,p.z);};
    for(size_t i=0;i<snapshot.indices.size();i+=3){
        const auto a=snapshot.indices[i],b=snapshot.indices[i+1],c=snapshot.indices[i+2];
        if(triangleTests)++*triangleTests;
        if(fixed(chunk(position(a),position(b),position(c))))continue;
        out.insert(out.end(),{a+offset,b+offset,c+offset});
    }
}
template<class Snapshot> void appendLiveDirectional(const Snapshot& snapshot,
        const std::set<std::pair<int,int>>& fixed,uint32_t offset,std::vector<uint32_t>& out,std::size_t* triangleTests=nullptr){
    appendLiveDirectionalBy(snapshot,[&](std::pair<int,int> c){return fixed.count(c)!=0;},offset,out,triangleTests);
}
template<class Scene> inline bool buildImpl(const NorthlightGI::WorldScene& local,const NorthlightGI::WorldScene& terrain,
                  Vec3 center,NorthlightWorldMesh::WorldMeshUploadPlan& output,std::string& error,
                  const NorthlightGI::AllocationAdmission& admit,float radius,
                  const NorthlightWorldMesh::PlanOrder& order,bool pagingOnly,
                  const NorthlightGI::BVH* sharedLocal,NorthlightWorldMesh::PreparationCache* preparation,const NorthlightGI::WorldScene* pointOnly,bool& reserveRefused,NorthlightGI::WorldScene* consume=nullptr,bool spooled=false){
    try {
    const Vec3 low=center-Vec3(LocalHorizontal,LocalHorizontal,LocalVertical),high=center+Vec3(LocalHorizontal,LocalHorizontal,LocalVertical);
    auto allowed=[&](uint64_t bytes){if(!admit||admit(bytes))return true;error="Geometry allocation deferred";return false;};
    auto merged=[&](){if constexpr(std::is_same_v<Scene,NorthlightGI::WorldScene>)return std::make_shared<Scene>();else return std::make_shared<Scene>(&admit,spooled);}();
    // Reserve final upper bounds once; do not copy then immediately reallocate.
    const size_t pointVertices=pointOnly?pointOnly->vertices.size():0,pointTriangles=pointOnly?pointOnly->triangles.size():0;
    if constexpr(std::is_same_v<Scene,NorthlightGI::WorldScene>)if(!allowed((local.vertices.size()+terrain.vertices.size()+pointVertices)*sizeof(NorthlightGI::WorldVertex))){reserveRefused=true;return false;}
    try{merged->vertices.reserve(local.vertices.size()+terrain.vertices.size()+pointVertices);}catch(const std::bad_alloc&){reserveRefused=true;return false;}
    if constexpr(std::is_same_v<Scene,NorthlightGI::WorldScene>)if(!allowed((local.triangles.size()+terrain.triangles.size()+pointTriangles)*sizeof(NorthlightGI::WorldTriangle))){reserveRefused=true;return false;}
    try{merged->triangles.reserve(local.triangles.size()+terrain.triangles.size()+pointTriangles);}catch(const std::bad_alloc&){reserveRefused=true;return false;}
    merged->vertices.insert(merged->vertices.end(),local.vertices.begin(),local.vertices.end());
    merged->triangles.insert(merged->triangles.end(),local.triangles.begin(),local.triangles.end());
    if(!allowed((local.materials.size()+1+(pointOnly?pointOnly->materials.size():0))*sizeof(NorthlightGI::WorldMaterial)))return false;
    merged->materials.reserve(local.materials.size()+1+(pointOnly?pointOnly->materials.size():0));
    // Metadata only: texel data stays in `local` (alive for this call) and is read through buildUploadPlan's
    // texture source, so the merged scene carries no third copy of up to 256 MiB of rgba.
    for(const auto& m:local.materials){
        NorthlightGI::WorldMaterial meta;meta.albedo=m.albedo;meta.alphaCutoff=m.alphaCutoff;meta.addressU=m.addressU;meta.addressV=m.addressV;meta.terrain=m.terrain;meta.wmo=m.wmo;meta.pointOnly=m.pointOnly;meta.lampLocalId=m.lampLocalId;meta.lampSourceId=m.lampSourceId;
        merged->materials.push_back(std::move(meta));
    }
    if(!allowed(local.completePlacements.size()*sizeof(NorthlightGI::WorldPlacementCoverage)))return false;
    merged->completePlacements=local.completePlacements;
    struct KeptRange {uint32_t first=UINT32_MAX,count=0;NorthlightMeshPageKey::Hasher membership;};
    std::vector<KeptRange> kept;
    std::vector<NorthlightWorldMesh::PreparationPiece> prepared;
    auto appendBytes=[](std::string& key,const auto& value){key.append(reinterpret_cast<const char*>(&value),sizeof(value));};
    auto identity=[&](const NorthlightGI::SourcePieceRange& range,char kind){std::string key(1,kind);appendBytes(key,range.sourceIdentity);appendBytes(key,range.membership);
        appendBytes(key,range.placement.uid);appendBytes(key,range.placement.category);appendBytes(key,range.placement.matrix);appendBytes(key,range.placement.translation);return key;};
    if(preparation)try{
        const auto localCount=!local.sourcePieces.empty()?local.sourcePieces.size():(sharedLocal?sharedLocal->pieces().size():0);
        const auto count=localCount+terrain.sourcePieces.size();
        uint64_t materialBytes=terrain.sourcePieces.size()*sizeof(uint32_t);
        if(!local.sourcePieces.empty())for(const auto& range:local.sourcePieces)materialBytes+=range.materialMap.size()*sizeof(uint32_t);
        else if(sharedLocal)for(const auto& range:sharedLocal->pieces())materialBytes+=range.materialMap.size()*sizeof(uint32_t);
        if(admit&&(!admit(count*sizeof(NorthlightWorldMesh::PreparationPiece))||!admit(terrain.sourcePieces.size()*sizeof(KeptRange))||!admit(count*160+materialBytes)))preparation=nullptr;
        if(preparation){prepared.reserve(count);kept.resize(terrain.sourcePieces.size());}
        if(preparation){
        if(!local.sourcePieces.empty())for(const auto& range:local.sourcePieces){if(!range.sourceIdentity||!range.triangleCount)continue;NorthlightWorldMesh::PreparationPiece piece;piece.identity=identity(range,'P');piece.firstTriangle=range.triangleBase;piece.triangleCount=range.triangleCount;piece.materials=range.materialMap;prepared.push_back(std::move(piece));}
        else if(sharedLocal&&&sharedLocal->scene()==&local)for(const auto& range:sharedLocal->pieces()){if(!range.geometry||range.geometry->scene().triangles.empty())continue;
            NorthlightWorldMesh::PreparationPiece piece;piece.identity="L";const auto address=reinterpret_cast<uintptr_t>(range.geometry.get());appendBytes(piece.identity,address);piece.owner=range.geometry;piece.weakIdentity=true;piece.firstTriangle=range.triangleBase;piece.triangleCount=uint32_t(range.geometry->scene().triangles.size());piece.materials=range.materialMap;prepared.push_back(std::move(piece));}
        }
    }catch(const std::bad_alloc&){preparation=nullptr;decltype(prepared)().swap(prepared);decltype(kept)().swap(kept);}
    std::set<std::pair<int,int>> fixed;
    if(!allowed(terrain.vertices.size()*sizeof(uint32_t)))return false;
    std::vector<uint32_t> remap(terrain.vertices.size(),UINT32_MAX);
    const uint32_t material=uint32_t(merged->materials.size());
    NorthlightGI::WorldMaterial opaque;opaque.terrain=true;
    merged->materials.push_back(opaque);
    size_t rangeIndex=0;
    // The consuming path owns its scratch scene exclusively. A failed build
    // discards it; only the completed plan is ever published.
    std::vector<std::pair<size_t,size_t>> releaseVertices;
    if(consume){
        const size_t block=terrain.vertices.blockSize();
        const size_t blocks=(terrain.vertices.size()+block-1)/block;
        if(!allowed(blocks*(sizeof(size_t)+sizeof(std::pair<size_t,size_t>))))return false;
        std::vector<size_t> last(blocks,0);releaseVertices.reserve(blocks);
        for(size_t i=0;i<terrain.triangles.size();++i){const auto& t=terrain.triangles[i];
            if(t.v0>=terrain.vertices.size()||t.v1>=terrain.vertices.size()||t.v2>=terrain.vertices.size()){error="Invalid shadow terrain";return false;}
            last[t.v0/block]=last[t.v1/block]=last[t.v2/block]=i;
        }
        for(size_t i=0;i<last.size();++i)releaseVertices.emplace_back(last[i],i);
        std::sort(releaseVertices.begin(),releaseVertices.end());
    }
    size_t released=0;
    for(size_t triangle=0;triangle<terrain.triangles.size();++triangle){
        if(consume){
            while(released<releaseVertices.size()&&releaseVertices[released].first<triangle)consume->vertices.releaseBlock(releaseVertices[released++].second);
            if(triangle&&triangle%terrain.triangles.blockSize()==0)consume->triangles.releaseBlock(triangle/terrain.triangles.blockSize()-1);
        }
        const auto& t=terrain.triangles[triangle];
        if(preparation)while(rangeIndex<terrain.sourcePieces.size()&&uint64_t(terrain.sourcePieces[rangeIndex].triangleBase)+terrain.sourcePieces[rangeIndex].triangleCount<=triangle)++rangeIndex;
        if(t.v0>=terrain.vertices.size()||t.v1>=terrain.vertices.size()||t.v2>=terrain.vertices.size()||
           t.material>=terrain.materials.size()||!terrain.materials[t.material].terrain){error="Invalid shadow terrain";return false;}
        const Vec3 a=terrain.vertices[t.v0].position,b=terrain.vertices[t.v1].position,c=terrain.vertices[t.v2].position;
        const Vec3 lo(std::min({a.x,b.x,c.x}),std::min({a.y,b.y,c.y}),std::min({a.z,b.z,c.z}));
        const Vec3 hi(std::max({a.x,b.x,c.x}),std::max({a.y,b.y,c.y}),std::max({a.z,b.z,c.z}));
        // The existing local loader owns all triangles intersecting this box.
        if(!outside(lo,hi,low,high))continue;
        NorthlightGI::WorldTriangle added;added.material=material;
        uint32_t* dest[]={&added.v0,&added.v1,&added.v2};const uint32_t src[]={t.v0,t.v1,t.v2};
        for(unsigned i=0;i<3;++i){
            if(remap[src[i]]==UINT32_MAX){remap[src[i]]=uint32_t(merged->vertices.size());merged->vertices.push_back(terrain.vertices[src[i]]);}
            *dest[i]=remap[src[i]];
        }
        if(rangeIndex<kept.size()){const auto& range=terrain.sourcePieces[rangeIndex];if(triangle>=range.triangleBase&&range.sourceIdentity){auto& record=kept[rangeIndex];if(record.first==UINT32_MAX)record.first=uint32_t(merged->triangles.size());++record.count;record.membership.word(uint32_t(triangle-range.triangleBase));}}
        merged->triangles.push_back(added);
        const auto key=chunk(a,b,c);
        if(key.first<0||key.second<0)continue;
        const Vec3 cl(float(Origin-(key.second+1)*Chunk),float(Origin-(key.first+1)*Chunk),low.z);
        const Vec3 ch(float(Origin-key.second*Chunk),float(Origin-key.first*Chunk),high.z);
        // Only full distant chunks are immutable. Border chunks retain the
        // previous live/cached policy, with no claims on unprovided geometry.
        if(outside(cl,ch,low,high)&&cl.x>=center.x-radius&&ch.x<=center.x+radius&&
           cl.y>=center.y-radius&&ch.y<=center.y+radius)fixed.insert(key);
    }
    if(consume){consume->vertices.free();consume->triangles.free();}
    try{for(size_t i=0;i<kept.size();++i)if(kept[i].count){const auto& range=terrain.sourcePieces[i];const auto& keptRange=kept[i];NorthlightWorldMesh::PreparationPiece piece;piece.identity=identity(range,'T');const auto membership=keptRange.membership.finish(range.triangleCount,keptRange.count);appendBytes(piece.identity,membership.lo);appendBytes(piece.identity,membership.hi);piece.firstTriangle=keptRange.first;piece.triangleCount=keptRange.count;piece.materials={material};prepared.push_back(std::move(piece));}}catch(const std::bad_alloc&){preparation=nullptr;decltype(prepared)().swap(prepared);}
    // The terrain vertex remap is dead after assembly; do not carry it into
    // the packed-index/signature allocations.
    std::vector<uint32_t>().swap(remap);
    if(pointOnly){
        const uint32_t vertexBase=uint32_t(merged->vertices.size()),materialBase=uint32_t(merged->materials.size());
        merged->vertices.insert(merged->vertices.end(),pointOnly->vertices.begin(),pointOnly->vertices.end());
        for(const auto& triangle:pointOnly->triangles){
            if(triangle.v0>=pointVertices||triangle.v1>=pointVertices||triangle.v2>=pointVertices||triangle.material>=pointOnly->materials.size()){
                error="Invalid point transmission geometry";return false;}
            merged->triangles.push_back({triangle.v0+vertexBase,triangle.v1+vertexBase,triangle.v2+vertexBase,triangle.material+materialBase});
        }
        for(const auto& source:pointOnly->materials){
            if(!allowed(source.texels().size()))return false;
            auto material=source;material.terrain=false;material.pointOnly=true;merged->materials.push_back(std::move(material));
        }
    }
    NorthlightWorldMesh::WorldMeshUploadPlan plan;
    if constexpr(std::is_same_v<Scene,NorthlightGI::WorldScene>){
    if(!NorthlightWorldMesh::buildUploadPlan(*merged,plan,error,admit,&local.materials,order,&prepared,preparation)){
        if(error=="Geometry allocation deferred")reserveRefused=true;
        return false;
    }
    if(pagingOnly){
        // The unique merged scene is temporary. Its triangles are now packed
        // in plan.indices; retaining both through paging costs tens of MiB in
        // cities. Move vertices/proofs, then destroy the unused scene here.
        plan.pagingVertices=std::make_shared<const std::vector<NorthlightGI::WorldVertex>>(std::move(*merged->vertices.flat()));
        plan.completePlacements=std::move(merged->completePlacements);plan.source=nullptr;
    }else plan.ownedSource=merged;
    }else{
        if(!NorthlightWorldMesh::buildChunkedUploadPlan(*merged,plan,error,admit,&local.materials,order))return false;
        merged->vertices.detachAdmission();
        plan.pagingChunks=std::make_shared<const NorthlightWorldMesh::ChunkedBuffer<NorthlightGI::WorldVertex>>(std::move(merged->vertices));
        plan.completePlacements=std::move(merged->completePlacements);plan.source=nullptr;
    }
    plan.fixedTerrainChunks=std::move(fixed);
    output=std::move(plan);return true;
    }catch(const std::bad_alloc&){reserveRefused=true;error="Geometry allocation deferred";return false;}
     catch(const NorthlightGI::TemporaryGeometryError& failure){error=failure.what();return false;}
     catch(...){error="Cannot allocate shadow terrain mesh";return false;}
}
inline bool build(const NorthlightGI::WorldScene& local,const NorthlightGI::WorldScene& terrain,
                  Vec3 center,NorthlightWorldMesh::WorldMeshUploadPlan& output,std::string& error,
                  const NorthlightGI::AllocationAdmission& admit = {},float radius=Radius,
                  const NorthlightWorldMesh::PlanOrder& order = {},bool pagingOnly=false,
                  const NorthlightGI::BVH* sharedLocal=nullptr,NorthlightWorldMesh::PreparationCache* preparation=nullptr,const NorthlightGI::WorldScene* pointOnly=nullptr,bool spooled=false){
    bool refused=false;
    if(spooled)return buildImpl<NorthlightWorldMesh::ChunkedShadowScene>(local,terrain,center,output,error,admit,radius,order,pagingOnly,sharedLocal,nullptr,pointOnly,refused,nullptr,true);
    if(buildImpl<NorthlightGI::WorldScene>(local,terrain,center,output,error,admit,radius,order,pagingOnly,sharedLocal,preparation,pointOnly,refused))return true;
    if(!pagingOnly||!refused)return false;
    if(preparation)preparation->reset();
    return buildImpl<NorthlightWorldMesh::ChunkedShadowScene>(local,terrain,center,output,error,admit,radius,order,pagingOnly,sharedLocal,nullptr,pointOnly,refused);
}
inline bool buildConsuming(const NorthlightGI::WorldScene& local,NorthlightGI::WorldScene& terrain,
                  Vec3 center,NorthlightWorldMesh::WorldMeshUploadPlan& output,std::string& error,
                  const NorthlightGI::AllocationAdmission& admit,float radius,
                  const NorthlightWorldMesh::PlanOrder& order,const NorthlightGI::BVH* sharedLocal,bool spooled=false){
    if(!terrain.vertices.chunked()||!terrain.triangles.chunked()){error="Consuming shadow terrain requires chunked geometry";return false;}
    bool refused=false;
    return buildImpl<NorthlightWorldMesh::ChunkedShadowScene>(local,terrain,center,output,error,admit,radius,order,true,sharedLocal,nullptr,nullptr,refused,&terrain,spooled);
}
} // namespace NorthlightShadowTerrain
