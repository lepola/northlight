#pragma once
#include "static_shadow_scene.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstring>
#include <limits>
#include <utility>

// Deliberately fail-open: replay is removed only after complete opaque triangle
// coverage is proven in the actual world-space geometry of a resident WMO.
// Hashes, shader classification, shared textures and bounds are never proof.
namespace StaticShadowDedup {
using Triangle=std::array<NorthlightGI::Vec3,3>;
using Canonical=std::array<uint32_t,9>;
// Snapshot indices are already rebased/compacted by draw_snapshot.h. Indexed
// and nonindexed list/strip layouts remain supported; malformed data keeps replay.
template<bool Indexed,bool Strip,class Position> bool makeTrianglesImpl(const std::vector<Position>& positions,const std::vector<uint32_t>& indices,
    size_t primitiveCount,std::vector<Triangle>& output){
    const size_t required=Strip?primitiveCount+2:primitiveCount*3;
    if((Indexed&&indices.size()<required)||(!Indexed&&positions.size()<required))return false;
    output.reserve(primitiveCount);
    auto read=[&](size_t v,NorthlightGI::Vec3& out){if constexpr(Indexed)v=indices[v];if(v>=positions.size())return false;
        const auto& p=positions[v];if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))return false;out={p.x,p.y,p.z};return true;};
    if constexpr(Strip){
        Triangle t;for(unsigned j=0;j<3;++j)if(!read(j,t[j])){output.clear();return false;}output.push_back(t);
        for(size_t i=1;i<primitiveCount;++i){t[0]=t[1];t[1]=t[2];if(!read(i+2,t[2])){output.clear();return false;}output.push_back(t);}return true;
    }
    for(size_t i=0;i<primitiveCount;++i){Triangle t;
        for(unsigned j=0;j<3;++j)if(!read(3*i+j,t[j])){output.clear();return false;}
        output.push_back(t);
    }return true;
}
template<class Position> bool makeTriangles(const std::vector<Position>& positions,const std::vector<uint32_t>& indices,
    bool indexed,size_t primitiveCount,bool strip,std::vector<Triangle>& output,size_t limit=8192){
    output.clear();if(!primitiveCount||primitiveCount>std::min<size_t>(limit,8192))return false;
    if(indexed)return strip?makeTrianglesImpl<true,true>(positions,indices,primitiveCount,output):makeTrianglesImpl<true,false>(positions,indices,primitiveCount,output);
    return strip?makeTrianglesImpl<false,true>(positions,indices,primitiveCount,output):makeTrianglesImpl<false,false>(positions,indices,primitiveCount,output);
}
struct Eligibility { bool auditedRigid=false,dynamic=false,skinned=false,alphaTest=true; };
inline bool eligible(Eligibility e){return e.auditedRigid&&!e.dynamic&&!e.skinned&&!e.alphaTest;}
inline bool canonical(const Triangle& t,Canonical& out){
    std::array<std::array<uint32_t,3>,3> points{};
    for(unsigned i=0;i<3;++i){const float p[3]={t[i].x,t[i].y,t[i].z};
        for(unsigned j=0;j<3;++j){if(!std::isfinite(p[j]))return false;float v=p[j]==0?0:p[j];std::memcpy(&points[i][j],&v,4);}}
    // All directional caster passes are double-sided. Reordering triangle
    // vertices changes neither coverage nor depth; canonicalize winding/order.
    if(points[1]<points[0])std::swap(points[0],points[1]);
    if(points[2]<points[1])std::swap(points[1],points[2]);
    if(points[1]<points[0])std::swap(points[0],points[1]);
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)out[3*i+j]=points[i][j];return true;
}
struct Budget {
    size_t triangles=0,candidates=0,draws=0;
    size_t maxTriangles=16384,maxCandidates=16,maxDraws=8;
    double maxMilliseconds=.25;
    std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    bool expired()const{return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()>=maxMilliseconds;}
    bool take(size_t n){if(expired()||triangles>maxTriangles||n>maxTriangles-triangles)return false;triangles+=n;return true;}
};
struct Match {
    bool valid=false;
    size_t placementIndex=0;
    uint64_t uid=0,mapGeneration=0,modelRevision=0;
    uint32_t category=0,slotMask=0;
    std::string modelKey;
    bool covers(unsigned slot)const{return valid&&slot<32&&(slotMask&(uint32_t(1)<<slot));}
};
inline bool samePlacement(const StaticShadow::Placement& a,const StaticShadow::Placement& b){
    return a.uid==b.uid&&a.category==b.category&&a.modelKey==b.modelKey&&
        !std::memcmp(a.matrix,b.matrix,sizeof a.matrix)&&a.translation.x==b.translation.x&&a.translation.y==b.translation.y&&a.translation.z==b.translation.z;
}
inline bool contains(const StaticShadow::Placement& p,NorthlightGI::Vec3 low,NorthlightGI::Vec3 high){
    return low.x>=p.low.x&&low.y>=p.low.y&&low.z>=p.low.z&&high.x<=p.high.x&&high.y<=p.high.y&&high.z<=p.high.z;
}
class Matcher {
    struct Surface {
        StaticShadow::Placement placement;
        std::weak_ptr<const StaticShadow::Model> model;
        std::vector<Canonical> triangles;
        uint64_t touched=0;
    };
    std::vector<Surface> surfaces_;
    size_t bytes_=0,maxBytes_=8u*1024u*1024u;
    uint64_t mapGeneration_=0,tick_=0;
    std::string map_;
    bool build(const StaticShadow::Placement& p,std::shared_ptr<const StaticShadow::Model> model,Budget& budget,Surface& surface){
        size_t count=0;
        for(const auto& b:model->batches){if(b.material>=model->materials.size())return false;
            if(model->materials[b.material].alphaCutoff>0)continue;
            if(b.indexCount%3||b.firstIndex>model->indexCount()||b.indexCount>model->indexCount()-b.firstIndex)return false;count+=b.indexCount/3;}
        if(!count||count>maxBytes_/sizeof(Canonical)||!budget.take(count))return false;
        surface.placement=p;surface.model=model;surface.triangles.reserve(count);
        for(const auto& b:model->batches){if(model->materials[b.material].alphaCutoff>0)continue;
            for(size_t i=b.firstIndex;i<size_t(b.firstIndex)+b.indexCount;i+=3){if((i-b.firstIndex)%768==0&&budget.expired())return false;Triangle t;
                for(unsigned j=0;j<3;++j){uint32_t v=model->indexAt(i+j);if(v>=model->vertices.size())return false;const auto& q=model->vertices[v];t[j]=StaticShadow::transformPoint(p,{q.x,q.y,q.z});}
                Canonical c;if(!canonical(t,c))return false;surface.triangles.push_back(c);}}
        std::sort(surface.triangles.begin(),surface.triangles.end());surface.triangles.erase(std::unique(surface.triangles.begin(),surface.triangles.end()),surface.triangles.end());surface.touched=++tick_;return true;
    }
public:
    explicit Matcher(size_t bytes=8u*1024u*1024u):maxBytes_(bytes){}
    void clear(){surfaces_.clear();bytes_=0;mapGeneration_=tick_=0;map_.clear();}
    size_t bytes()const{return bytes_;}
    // readyMask(p, modelRevision) returns the directional cache slots containing
    // this COMPLETE resident placement. CPU publication or upload queued != ready.
    // Caller must recalculate/recheck this mask if readiness changes after match.
    template<class Ready> Match match(const StaticShadow::Snapshot& snapshot,const std::vector<Triangle>& draw,
        Eligibility e,Budget& budget,Ready readyMask){
        if(map_!=snapshot.map||mapGeneration_!=snapshot.mapGeneration){clear();map_=snapshot.map;mapGeneration_=snapshot.mapGeneration;}
        if(!eligible(e)||draw.empty()||budget.draws>=budget.maxDraws)return {};
        ++budget.draws;if(!budget.take(draw.size()))return {};
        NorthlightGI::Vec3 low(INFINITY,INFINITY,INFINITY),high(-INFINITY,-INFINITY,-INFINITY);
        std::vector<Canonical> submitted;submitted.reserve(draw.size());for(const auto& t:draw){Canonical c;if(!canonical(t,c))return {};submitted.push_back(c);for(auto p:t){low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);low.z=std::min(low.z,p.z);high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);}}
        for(size_t i=0;i<snapshot.placements.size();++i){const auto& p=snapshot.placements[i];
            if((i&63)==0&&budget.expired())return {};if(p.category!=2||!contains(p,low,high))continue;
            const auto found=snapshot.models.find(p.modelKey);if(found==snapshot.models.end()||!found->second)continue;
            const uint32_t mask=readyMask(p,found->second->contentRevision);if(!mask)continue;
            if(budget.candidates>=budget.maxCandidates)return {};++budget.candidates;
            auto cached=std::find_if(surfaces_.begin(),surfaces_.end(),[&](const Surface& s){return s.model.lock()==found->second&&samePlacement(s.placement,p);});
            if(cached==surfaces_.end()){
                Surface s;if(!build(p,found->second,budget,s))continue;const size_t needed=s.triangles.capacity()*sizeof(Canonical);
                if(needed>maxBytes_)continue;
                while(bytes_+needed>maxBytes_||surfaces_.size()>=128){auto oldest=std::min_element(surfaces_.begin(),surfaces_.end(),[](const Surface& a,const Surface& b){return a.touched<b.touched;});
                    if(oldest==surfaces_.end())break;bytes_-=oldest->triangles.capacity()*sizeof(Canonical);surfaces_.erase(oldest);}
                bytes_+=needed;surfaces_.push_back(std::move(s));cached=surfaces_.end()-1;
            }
            cached->touched=++tick_;bool complete=true;
            for(const auto& t:submitted)if(!std::binary_search(cached->triangles.begin(),cached->triangles.end(),t)){complete=false;break;}
            if(complete&&!budget.expired())return {true,i,p.uid,snapshot.mapGeneration,found->second->contentRevision,p.category,mask,p.modelKey};
        }
        return {};
    }
};
// Bounded memo of proofs. Evidence MUST include complete immutable mesh identity
// AND generation, original shader/program, all position constants and inverse
// view used by evaluation (or exact equivalent world transform). Never supply
// only a content hash or a recycled pointer. Exact evidence bytes are compared.
// A weak owner verifies lifetime without pinning the live GPU/snapshot cache.
class Memo {
    struct Entry {std::weak_ptr<const void> owner;std::vector<uint8_t> evidence;Match proof;};
    std::vector<Entry> entries_;
    uint64_t mapEpoch_=0,deviceEpoch_=0;
    std::string map_;
public:
    void clear(){entries_.clear();map_.clear();mapEpoch_=deviceEpoch_=0;}
    void context(const std::string& map,uint64_t mapEpoch,uint64_t deviceEpoch){if(map_!=map||mapEpoch_!=mapEpoch||deviceEpoch_!=deviceEpoch){clear();map_=map;mapEpoch_=mapEpoch;deviceEpoch_=deviceEpoch;}}
    // Readiness is deliberately not cached; a resource loss must revoke a proof.
    template<class Ready> Match find(const std::shared_ptr<const void>& owner,const std::vector<uint8_t>& evidence,Ready readyMask)const{
        if(!owner||evidence.empty())return {};
        for(auto it=entries_.rbegin();it!=entries_.rend();++it)if(it->owner.lock()==owner&&it->evidence==evidence){Match result=it->proof;result.slotMask=readyMask(result);result.valid=result.valid&&result.slotMask;return result;}return {};
    }
    void remember(std::shared_ptr<const void> owner,std::vector<uint8_t> evidence,Match proof){
        if(!owner||evidence.empty()||evidence.size()>8192||!proof.valid||proof.mapGeneration!=mapEpoch_)return;
        if(entries_.size()>=128)entries_.erase(entries_.begin());entries_.push_back({std::move(owner),std::move(evidence),std::move(proof)});
    }
};
} // namespace StaticShadowDedup
