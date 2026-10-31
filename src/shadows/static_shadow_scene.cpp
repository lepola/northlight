#include "utf8_path.h"
#include "static_shadow_scene.h"
#include "cpu_retirement.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>
#ifdef _WIN32
#include <windows.h>
#endif

namespace StaticShadow {
namespace {
using Clock=std::chrono::steady_clock;
using Stamp=Clock::time_point;
double ms(Stamp a,Stamp b=Clock::now()){return std::chrono::duration<double,std::milli>(b-a).count();}
Vec3 mn(Vec3 a,Vec3 b){return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};}
Vec3 mx(Vec3 a,Vec3 b){return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};}
bool finite(Vec3 a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
uint64_t hashBytes(uint64_t h,const void* p,size_t n){auto b=static_cast<const unsigned char*>(p);for(size_t i=0;i<n;++i){h^=b[i];h*=1099511628211ull;}return h;}
constexpr uint64_t HashSeed=14695981039346656037ull;
void discardOpaqueTexture(NorthlightGI::WorldMaterial& material){
    // Shadow shaders only sample alpha. A fully opaque color texture has the
    // same coverage as the implicit white texel; keep cutoff/address rules.
    for(size_t i=3;i<material.rgba.size();i+=4)if(material.rgba[i]!=255)return;
    material.width=material.height=0;
    std::vector<uint8_t>().swap(material.rgba);
}

// Run only on the loading thread. Keep original content hashes unchanged: RGB
// does not affect shadows; alpha, addressing, cutoff and indices stay exact.
void prepareUpload(Model& model){
    if(model.index16){model.uploadIndices16.reserve(model.indices.size());for(uint32_t i:model.indices)model.uploadIndices16.push_back(uint16_t(i));model.bytes+=model.uploadIndices16.capacity()*sizeof(uint16_t);model.bytes-=model.indices.capacity()*sizeof(uint32_t);std::vector<uint32_t>().swap(model.indices);}
    auto compact=std::make_shared<Model>();compact->key=model.key;compact->contentRevision=model.contentRevision;compact->batches=model.batches;compact->index16=model.index16;
    for(auto& material:model.materials){
        auto alpha=std::make_shared<std::vector<uint8_t>>(size_t(std::max(1u,material.width))*std::max(1u,material.height),255);
        for(size_t i=0;i<alpha->size();++i){size_t j=i*4;if(j+3<material.rgba.size())(*alpha)[i]=material.rgba[j+3];}
        // Only alpha reaches the shadow target. Expand to white RGB during the
        // bounded upload instead of retaining a second four-byte texel array.
        model.bytes-=material.rgba.capacity();std::vector<uint8_t>().swap(material.rgba);
        model.bytes+=alpha->capacity();model.uploadAlpha.push_back(std::move(alpha));
        NorthlightGI::WorldMaterial mat;mat.alphaCutoff=material.alphaCutoff;mat.addressU=material.addressU;mat.addressV=material.addressV;compact->materials.push_back(std::move(mat));
    }
    compact->bytes=sizeof(Model)+compact->batches.capacity()*sizeof(Batch)+compact->materials.capacity()*sizeof(NorthlightGI::WorldMaterial);
    model.bytes+=model.uploadAlpha.capacity()*sizeof(model.uploadAlpha[0])+compact->bytes;
    model.drawModel=std::move(compact);model.uploadPrepared=true;
}

using Cell=std::pair<int,int>;
using Identity=std::pair<uint32_t,uint64_t>;
struct Reader {
    FILE* f=nullptr;size_t remaining=0;
    ~Reader(){if(f)std::fclose(f);}
    bool open(const std::string& path,size_t cap){f=NorthlightFiles::open(path.c_str(),"rb");if(!f)return false;if(std::fseek(f,0,SEEK_END))return false;long n=std::ftell(f);if(n<0||size_t(n)>cap||std::fseek(f,0,SEEK_SET))return false;remaining=size_t(n);return true;}
    bool bytes(void* p,size_t n){if(n>remaining||std::fread(p,1,n,f)!=n)return false;remaining-=n;return true;}
    bool u32(uint32_t& v){unsigned char b[4];if(!bytes(b,4))return false;v=uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24);return true;}
    bool u64(uint64_t& v){uint32_t l,h;if(!u32(l)||!u32(h))return false;v=uint64_t(l)|(uint64_t(h)<<32);return true;}
    bool scalar(float& v){uint32_t u;if(!u32(u))return false;std::memcpy(&v,&u,4);return std::isfinite(v);}
    bool vec(Vec3& v){return scalar(v.x)&&scalar(v.y)&&scalar(v.z);}
};
bool readTile(const std::string& path,std::vector<Placement>& out,size_t& bytes,std::string& error,size_t cap){
    Reader r;if(!r.open(path,cap)){if(r.f)error="Invalid or oversized FGS3 file";return false;}bytes=r.remaining;
    char magic[4];uint32_t version,count;
    if(!r.bytes(magic,4)||std::memcmp(magic,"FGS3",4)||!r.u32(version)||version!=3||!r.u32(count)||count>100000||uint64_t(count)*116!=r.remaining){error="Invalid FGS3 header/count";return false;}
    const char hex[]="0123456789abcdef";
    for(uint32_t i=0;i<count;++i){
        Placement p;unsigned char hash[32];
        if(!r.bytes(hash,32)||!r.u64(p.uid)||!r.u32(p.category)||p.category>3||!r.vec(p.low)||!r.vec(p.high)){error="Invalid FGS3 placement";return false;}
        for(float& value:p.matrix)if(!r.scalar(value)){error="Invalid FGS3 matrix";return false;}
        if(!r.vec(p.translation)||p.low.x>p.high.x||p.low.y>p.high.y||p.low.z>p.high.z){error="Invalid FGS3 bounds";return false;}
        // Bounds must remain safe for cell indexing, even with malicious files.
        if(std::fabs(p.low.x)>1000000||std::fabs(p.low.y)>1000000||std::fabs(p.high.x)>1000000||std::fabs(p.high.y)>1000000){error="FGS3 bounds outside supported world";return false;}
        float det=p.matrix[0]*(p.matrix[4]*p.matrix[8]-p.matrix[5]*p.matrix[7])-p.matrix[1]*(p.matrix[3]*p.matrix[8]-p.matrix[5]*p.matrix[6])+p.matrix[2]*(p.matrix[3]*p.matrix[7]-p.matrix[4]*p.matrix[6]);
        if(!std::isfinite(det)||std::fabs(det)<1e-12f){error="Singular FGS3 matrix";return false;}
        if(!p.category)continue;
        p.modelKey.resize(64);for(unsigned j=0;j<32;++j){p.modelKey[2*j]=hex[hash[j]>>4];p.modelKey[2*j+1]=hex[hash[j]&15];}
        if(out.size()>cap/(2*(sizeof(Placement)+65))){error="FGS3 metadata budget exceeded";return false;}
        out.push_back(std::move(p));
    }
    return true;
}
float distance2(Vec3 a,Vec3 b){return NorthlightGI::dot(a-b,a-b);}
}

Vec3 transformPoint(const Placement& p,Vec3 v){const float* m=p.matrix;return {m[0]*v.x+m[1]*v.y+m[2]*v.z+p.translation.x,m[3]*v.x+m[4]*v.y+m[5]*v.z+p.translation.y,m[6]*v.x+m[7]*v.y+m[8]*v.z+p.translation.z};}
void transformBounds(const Placement& p,Vec3 low,Vec3 high,Vec3& outLow,Vec3& outHigh){Vec3 c=transformPoint(p,(low+high)*.5f),r=(high-low)*.5f;const float* m=p.matrix;Vec3 e={std::fabs(m[0])*r.x+std::fabs(m[1])*r.y+std::fabs(m[2])*r.z,std::fabs(m[3])*r.x+std::fabs(m[4])*r.y+std::fabs(m[5])*r.z,std::fabs(m[6])*r.x+std::fabs(m[7])*r.y+std::fabs(m[8])*r.z};outLow=c-e;outHigh=c+e;}
bool selected(const Request& r,Vec3 low,Vec3 high,float margin){
    Vec3 center=(low+high)*.5f,extent=(high-low)*.5f,delta=center-r.center;
    // Near fallback geometry and nearby unlit street surfaces must always stay.
    if(std::fabs(delta.x)<=288+margin+extent.x&&std::fabs(delta.y)<=288+margin+extent.y&&std::fabs(delta.z)<=320+margin+extent.z)return true;
    for(unsigned i=0;i<2;++i)if(r.active[i]){
        Vec3 d=NorthlightGI::normalized(r.directions[i]);if(NorthlightGI::dot(d,d)<.5f)continue;
        Vec3 ref=std::fabs(d.z)>.95f?Vec3(0,1,0):Vec3(0,0,1);
        Vec3 right=NorthlightGI::normalized(NorthlightGI::cross(d,ref)),up=NorthlightGI::cross(right,d);
        auto overlaps=[&](Vec3 a,float reach){float projected=std::fabs(a.x)*extent.x+std::fabs(a.y)*extent.y+std::fabs(a.z)*extent.z;return std::fabs(NorthlightGI::dot(delta,a))<=reach+margin+projected;};
        // Symmetric depth matches the existing +/-640 light-space clipping.
        // The screen-orthogonal axes depend on light, never camera orientation.
        if(overlaps(right,240)&&overlaps(up,240)&&overlaps(d,640))return true;
    }
    return false;
}

bool packModel(NorthlightGI::WorldScene&& scene,const std::string& key,Model& output,std::string& error){
    try {
        Model model;model.key=key;model.index16=scene.vertices.size()<=65536;
        model.vertices.reserve(scene.vertices.size());
        for(const auto& v:scene.vertices){if(!finite(v.position)||!std::isfinite(v.u)||!std::isfinite(v.v)){error="Nonfinite model vertex";return false;}model.vertices.push_back({v.position.x,v.position.y,v.position.z,v.u,v.v});}
        // Stable spatial batches keep city-sized WMOs from becoming one draw.
        // Long crossing triangles remain whole and enlarge their batch bounds.
        using Group=std::tuple<uint32_t,int,int,int>;
        std::map<Group,std::vector<uint32_t>> groups;
        for(const auto& t:scene.triangles){
            if(t.v0>=model.vertices.size()||t.v1>=model.vertices.size()||t.v2>=model.vertices.size()||t.material>=scene.materials.size()){error="Invalid model triangle";return false;}
            Vec3 center=(scene.vertices[t.v0].position+scene.vertices[t.v1].position+scene.vertices[t.v2].position)/3.f;
            // Finite positions may still exceed safe float-to-int conversion.
            if(std::fabs(center.x)>1000000||std::fabs(center.y)>1000000||std::fabs(center.z)>1000000){error="Model outside supported coordinates";return false;}
            Group g(t.material,int(std::floor(center.x/64)),int(std::floor(center.y/64)),int(std::floor(center.z/64)));
            if(groups.size()>=65536&&!groups.count(g)){error="Too many model spatial batches";return false;}
            auto& ids=groups[g];ids.push_back(t.v0);ids.push_back(t.v1);ids.push_back(t.v2);
        }
        model.indices.reserve(scene.triangles.size()*3);
        for(auto& group:groups){Batch b;b.firstIndex=uint32_t(model.indices.size());b.indexCount=uint32_t(group.second.size());b.material=std::get<0>(group.first);b.low=Vec3(1e30f,1e30f,1e30f);b.high=-b.low;
            for(uint32_t index:group.second){const auto& v=model.vertices[index];b.low=mn(b.low,{v.x,v.y,v.z});b.high=mx(b.high,{v.x,v.y,v.z});model.indices.push_back(index);}model.batches.push_back(b);
        }
        model.materials=std::move(scene.materials);
        for(auto& material:model.materials)discardOpaqueTexture(material);
        uint64_t revision=hashBytes(HashSeed,model.vertices.data(),model.vertices.size()*sizeof(Vertex));
        revision=hashBytes(revision,model.indices.data(),model.indices.size()*sizeof(uint32_t));
        model.bytes=model.vertices.capacity()*sizeof(Vertex)+model.indices.capacity()*sizeof(uint32_t)+model.batches.capacity()*sizeof(Batch)+model.materials.capacity()*sizeof(NorthlightGI::WorldMaterial);
        for(auto& material:model.materials){uint64_t h=HashSeed;h=hashBytes(h,&material.width,sizeof(material.width));h=hashBytes(h,&material.height,sizeof(material.height));h=hashBytes(h,&material.alphaCutoff,sizeof(material.alphaCutoff));h=hashBytes(h,&material.addressU,sizeof(material.addressU));h=hashBytes(h,&material.addressV,sizeof(material.addressV));
            // Only alpha participates in a shadow texture's shared identity.
            for(size_t i=3;i<material.rgba.size();i+=4)h=hashBytes(h,&material.rgba[i],1);
            model.materialKeys.push_back(h);revision=hashBytes(revision,&h,sizeof h);model.bytes+=material.rgba.capacity();}
        // Material assignments also define content identity, not just triangles.
        for(const auto& b:model.batches){revision=hashBytes(revision,&b.firstIndex,4);revision=hashBytes(revision,&b.indexCount,4);revision=hashBytes(revision,&b.material,4);}
        model.contentRevision=revision;prepareUpload(model);output=std::move(model);return true;
    } catch(...) {error="Cannot allocate compact static shadow model";return false;}
}

namespace {
// Streaming decoder never materializes the 32-byte GI vertices or a complete
// WorldScene. One compact shared model is the largest publication unit.
bool loadCompact(const std::string& path,const std::string& key,size_t cap,Model& model,std::string& error){
    Reader r;if(!r.open(path,cap)){error="Cannot open bounded FGS2 model";return false;}
    char magic[4];uint32_t version,nv,nt,nm;
    if(!r.bytes(magic,4)||std::memcmp(magic,"FGS2",4)||!r.u32(version)||version!=2||!r.u32(nv)||!r.u32(nt)||!r.u32(nm)||nv>2000000||nt>3000000||nm>65536||uint64_t(nv)*32+uint64_t(nt)*16+uint64_t(nm)*28>r.remaining){error="Invalid FGS2 header/count";return false;}
    model.key=key;model.index16=nv<=65536;model.vertices.resize(nv);
    // fread per scalar was a major cold-model cost (millions of stdio locks
    // for a city WMO). Decode bounded blocks without an input-sized buffer.
    auto word=[](const unsigned char* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);};
    std::array<unsigned char,16384> block{};
    for(uint32_t start=0;start<nv;){uint32_t count=std::min(512u,nv-start);if(!r.bytes(block.data(),size_t(count)*32)){error="Truncated FGS2 vertices";return false;}
        for(uint32_t i=0;i<count;++i){float values[8];for(unsigned j=0;j<8;++j){uint32_t u=word(block.data()+size_t(i)*32+j*4);std::memcpy(&values[j],&u,4);if(!std::isfinite(values[j])){error="Invalid FGS2 vertex";return false;}}model.vertices[start+i]={values[0],values[1],values[2],values[6],values[7]};}start+=count;
    }
    using Group=std::tuple<uint32_t,int,int,int>;std::map<Group,std::vector<uint32_t>> groups;
    for(uint32_t i=0;i<nt;++i){if(i%1024==0&&!r.bytes(block.data(),size_t(std::min(1024u,nt-i))*16)){error="Truncated FGS2 triangles";return false;}
        const unsigned char* record=block.data()+(i%1024)*16;uint32_t a=word(record),b=word(record+4),c=word(record+8),material=word(record+12);if(a>=nv||b>=nv||c>=nv||material>=nm){error="Invalid FGS2 triangle";return false;}
        const auto& va=model.vertices[a];const auto& vb=model.vertices[b];const auto& vc=model.vertices[c];Vec3 center={(va.x+vb.x+vc.x)/3,(va.y+vb.y+vc.y)/3,(va.z+vb.z+vc.z)/3};
        if(!finite(center)||std::fabs(center.x)>1000000||std::fabs(center.y)>1000000||std::fabs(center.z)>1000000){error="FGS2 coordinates outside supported world";return false;}
        Group group(material,int(std::floor(center.x/64)),int(std::floor(center.y/64)),int(std::floor(center.z/64)));if(groups.size()>=65536&&!groups.count(group)){error="Too many FGS2 spatial batches";return false;}
        auto& ids=groups[group];ids.push_back(a);ids.push_back(b);ids.push_back(c);
    }
    model.indices.reserve(size_t(nt)*3);model.batches.reserve(groups.size());
    for(auto& group:groups){Batch b;b.firstIndex=uint32_t(model.indices.size());b.indexCount=uint32_t(group.second.size());b.material=std::get<0>(group.first);b.low={1e30f,1e30f,1e30f};b.high=-b.low;for(uint32_t index:group.second){const auto& v=model.vertices[index];b.low=mn(b.low,{v.x,v.y,v.z});b.high=mx(b.high,{v.x,v.y,v.z});model.indices.push_back(index);}model.batches.push_back(b);std::vector<uint32_t>().swap(group.second);}
    groups.clear();model.materials.resize(nm);
    for(auto& m:model.materials){uint32_t bytes;if(!r.vec(m.albedo)||!r.u32(m.width)||!r.u32(m.height)||!r.scalar(m.alphaCutoff)||!r.u32(bytes)||m.width>4096||m.height>4096||uint64_t(m.width)*m.height*4!=bytes||bytes>r.remaining||((m.width==0)!=(m.height==0))||m.alphaCutoff<0||m.alphaCutoff>1){error="Invalid FGS2 material";return false;}m.rgba.resize(bytes);if(!r.bytes(m.rgba.data(),bytes)){error="Truncated FGS2 texture";return false;}}
    if(r.remaining){error="Trailing FGS2 data";return false;}
    for(auto& material:model.materials)discardOpaqueTexture(material);
    uint64_t revision=hashBytes(HashSeed,model.vertices.data(),model.vertices.size()*sizeof(Vertex));revision=hashBytes(revision,model.indices.data(),model.indices.size()*sizeof(uint32_t));
    model.bytes=model.vertices.capacity()*sizeof(Vertex)+model.indices.capacity()*sizeof(uint32_t)+model.batches.capacity()*sizeof(Batch)+model.materials.capacity()*sizeof(NorthlightGI::WorldMaterial);
    for(const auto& m:model.materials){uint64_t h=HashSeed;h=hashBytes(h,&m.width,4);h=hashBytes(h,&m.height,4);h=hashBytes(h,&m.alphaCutoff,4);h=hashBytes(h,&m.addressU,4);h=hashBytes(h,&m.addressV,4);for(size_t i=3;i<m.rgba.size();i+=4)h=hashBytes(h,&m.rgba[i],1);model.materialKeys.push_back(h);revision=hashBytes(revision,&h,sizeof h);model.bytes+=m.rgba.capacity();}
    for(const auto& b:model.batches){revision=hashBytes(revision,&b.firstIndex,4);revision=hashBytes(revision,&b.indexCount,4);revision=hashBytes(revision,&b.material,4);}model.contentRevision=revision;prepareUpload(model);return true;
}
}

struct Streamer::State {
    struct Tile {std::vector<Identity> refs;Stamp used;size_t bytes=0;};
    struct Resident {std::shared_ptr<const Model> model;Stamp used;};
    std::string root;Limits limits;
    mutable std::mutex mutex;mutable std::condition_variable wake;
    Request requested;uint64_t generation=0,requestSerial=0,doneSerial=0;bool stop=false,hasRequest=false,paused=false,reclaimPending=false,busy=false;
    std::shared_ptr<const Snapshot> published;
    std::thread worker;
    std::map<Cell,Tile> tiles;
    std::map<std::string,Resident> models;
    std::map<Identity,Placement> placements;
    std::map<Cell,std::vector<Identity>> cells;
    std::vector<Identity> giants;
    std::set<std::string> failedModels,lastNeeded;
    std::set<Cell> lastWanted;
    Stamp retryAfter{};Stats stats;uint64_t activeGeneration=0,revision=0;size_t residentBytes=0;
    State(std::string r,Limits l):root(std::move(r)),limits(l){if(!root.empty()&&root.back()!='/'&&root.back()!='\\')root+='/';limits.cellSize=std::max(32u,std::min(512u,limits.cellSize));worker=std::thread([this]{run();});}
    ~State(){{std::lock_guard<std::mutex> lock(mutex);stop=true;wake.notify_all();}if(worker.joinable())worker.join();}
    bool current(uint64_t gen){std::lock_guard<std::mutex> lock(mutex);return !stop&&generation==gen;}
    size_t metadataSize()const {size_t n=0;for(const auto& t:tiles)n+=t.second.refs.capacity()*sizeof(Identity);return n;}
    size_t indexSize()const {size_t n=placements.size()*(sizeof(Placement)+65+64)+giants.capacity()*sizeof(Identity);for(const auto& c:cells)n+=96+c.second.capacity()*sizeof(Identity);return n;}
    bool superseded(uint64_t serial){std::lock_guard<std::mutex> lock(mutex);return requestSerial!=serial;}
    void reset(uint64_t gen){tiles.clear();models.clear();placements.clear();cells.clear();giants.clear();failedModels.clear();lastNeeded.clear();lastWanted.clear();residentBytes=0;activeGeneration=gen;}
    void index(){
        cells.clear();giants.clear();
        std::set<Identity> live;for(const auto& tile:tiles)live.insert(tile.second.refs.begin(),tile.second.refs.end());
        for(auto it=placements.begin();it!=placements.end();)if(!live.count(it->first))it=placements.erase(it);else ++it;
        size_t cellBytes=0;const size_t remaining=limits.metadataBytes>metadataSize()+indexSize()?limits.metadataBytes-metadataSize()-indexSize():0;
        for(const auto& pair:placements){const auto& p=pair.second;int x0=int(std::floor(p.low.x/limits.cellSize)),x1=int(std::floor(p.high.x/limits.cellSize));int y0=int(std::floor(p.low.y/limits.cellSize)),y1=int(std::floor(p.high.y/limits.cellSize));
            uint64_t slots=uint64_t(x1-x0+1)*uint64_t(y1-y0+1);
            if(slots>1024||cellBytes+slots*(96+2*sizeof(Identity))>remaining){giants.push_back(pair.first);continue;}
            cellBytes+=size_t(slots)*(96+2*sizeof(Identity));
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)cells[{x,y}].push_back(pair.first);
        }
    }
    void evict(const std::set<std::string>& needed,bool pressure){
        Stamp now=Clock::now();std::vector<std::pair<Stamp,std::string>> candidates;
        for(auto& pair:models)if(!needed.count(pair.first)&&pair.second.model.use_count()==1&&(pressure||ms(pair.second.used,now)>limits.retainSeconds*1000.0))candidates.emplace_back(pair.second.used,pair.first);
        std::sort(candidates.begin(),candidates.end());for(auto& candidate:candidates){auto it=models.find(candidate.second);residentBytes-=it->second.model->bytes;models.erase(it);}
    }
    void maintain(){
        // Idle maintenance is independent of camera requests and GPU readiness.
        // It does not read files, rebuild models, or publish a new scene epoch.
        if(!activeGeneration)return;
        evict(lastNeeded,false);
        const Stamp now=Clock::now();bool removed=false;
        for(auto it=tiles.begin();it!=tiles.end();){
            if(!lastWanted.count(it->first)&&ms(it->second.used,now)>=limits.retainSeconds*1000.0){it=tiles.erase(it);removed=true;}else ++it;
        }
        if(!removed)return;
        std::set<Identity> live;for(const auto& tile:tiles)live.insert(tile.second.refs.begin(),tile.second.refs.end());
        for(auto it=placements.begin();it!=placements.end();)if(!live.count(it->first))it=placements.erase(it);else ++it;
        // Remove only expired membership. Retained cells are never rebuilt.
        for(auto it=cells.begin();it!=cells.end();){auto& refs=it->second;refs.erase(std::remove_if(refs.begin(),refs.end(),[&](const Identity& id){return !live.count(id);}),refs.end());if(refs.empty())it=cells.erase(it);else ++it;}
        giants.erase(std::remove_if(giants.begin(),giants.end(),[&](const Identity& id){return !live.count(id);}),giants.end());
    }
    void publish(const Request& r,const std::vector<Placement>& selectedPlacements,uint64_t gen,Stamp start,bool complete){
        auto next=std::make_shared<Snapshot>();next->mapGeneration=gen;next->revision=++revision;next->map=r.map;
        for(const auto& p:selectedPlacements){auto found=models.find(p.modelKey);if(found==models.end())continue;next->placements.push_back(p);next->models.emplace(p.modelKey,found->second.model);}
        for(size_t i=0;i<next->placements.size();++i)next->placementGroups[next->placements[i].modelKey].push_back(i);next->preparedIdentity=next.get();next->retirementBytes=next->placements.capacity()*sizeof(Placement);for(const auto& kv:next->models)next->retirementBytes+=kv.second->bytes;
        stats.metadataBytes=metadataSize()+indexSize();stats.metadataPeak=std::max(stats.metadataPeak,stats.metadataBytes);stats.cpuBytes=residentBytes;stats.cpuPeak=std::max(stats.cpuPeak,stats.cpuBytes);stats.tiles=uint32_t(tiles.size());stats.cells=uint32_t(cells.size());stats.placements=uint32_t(next->placements.size());stats.models=uint32_t(next->models.size());stats.complete=complete;stats.latencyMs=ms(start);++stats.publications;next->stats=stats;
        // Only this worker publishes. Readers may clear published on a map
        // change, which also changes generation. Keep their shared mutex out of
        // the O(placements + models) comparison and snapshot destruction.
        std::shared_ptr<const Snapshot> previous;
        {std::lock_guard<std::mutex> lock(mutex);if(stop||generation!=gen)return;previous=published;}
        bool sameGeometry=previous&&previous->mapGeneration==next->mapGeneration&&previous->models==next->models&&previous->placements.size()==next->placements.size();
        if(sameGeometry)for(size_t i=0;i<next->placements.size();++i){const auto& a=previous->placements[i];const auto& b=next->placements[i];if(a.uid!=b.uid||a.category!=b.category||a.modelKey!=b.modelKey||std::memcmp(a.matrix,b.matrix,sizeof(a.matrix))||std::memcmp(&a.translation,&b.translation,sizeof(Vec3))||std::memcmp(&a.low,&b.low,sizeof(Vec3))||std::memcmp(&a.high,&b.high,sizeof(Vec3))){sameGeometry=false;break;}}
        next->geometryRevision=sameGeometry?previous->geometryRevision:next->revision;
        bool same=sameGeometry&&previous->stats.complete==next->stats.complete&&previous->stats.pendingModels==next->stats.pendingModels&&previous->stats.pendingTiles==next->stats.pendingTiles&&previous->stats.failures==next->stats.failures&&previous->stats.deferred==next->stats.deferred&&previous->stats.metadataReads==next->stats.metadataReads;
        {std::lock_guard<std::mutex> lock(mutex);if(stop||generation!=gen)return;
         if(same){--stats.publications;return;}
         // previous pins the old snapshot until this lock has been released.
         published=std::move(next);}
        wake.notify_all();
    }
    void process(const Request& r,uint64_t gen,uint64_t serial){
        Stamp start=Clock::now();if(activeGeneration!=gen)reset(gen);
        bool dirty=false;stats.pendingTiles=0;Stamp now=Clock::now();
        // Existing cache tiles overlap placements at borders. An extra tile ring
        // conservatively covers large objects crossing the requested envelope.
        int tx=int(std::floor((17066.6666667-r.center.y)/533.3333333)),ty=int(std::floor((17066.6666667-r.center.x)/533.3333333));
        std::vector<Cell> wanted;
        for(int y=ty-2;y<=ty+2;++y)for(int x=tx-2;x<=tx+2;++x)if(x>=0&&x<64&&y>=0&&y<64)wanted.emplace_back(x,y);
        lastWanted=std::set<Cell>(wanted.begin(),wanted.end());
        std::sort(wanted.begin(),wanted.end(),[&](Cell a,Cell b){return (a.first-tx)*(a.first-tx)+(a.second-ty)*(a.second-ty)<(b.first-tx)*(b.first-tx)+(b.second-ty)*(b.second-ty);});
        for(auto it=tiles.begin();it!=tiles.end();){bool keep=std::find(wanted.begin(),wanted.end(),it->first)!=wanted.end();if(!keep&&(ms(it->second.used,now)>limits.retainSeconds*1000.0||tiles.size()>49)){it=tiles.erase(it);dirty=true;}else ++it;}
        for(Cell cell:wanted){if(!current(gen)){++stats.cancellations;return;}auto it=tiles.find(cell);if(it!=tiles.end()){it->second.used=now;continue;}
            Tile tile;tile.used=now;std::vector<Placement> decoded;std::string error;char suffix[96];std::snprintf(suffix,sizeof suffix,"/%d_%d.fg3",cell.first,cell.second);Stamp job=Clock::now();size_t bytes=0;
            bool ok=readTile(root+r.map+suffix,decoded,bytes,error,limits.metadataBytes/3);++stats.metadataReads;stats.bytesRead+=bytes;stats.readMs+=ms(job);
            if(!ok){decoded.clear();if(!error.empty())++stats.failures;}
            tile.refs.reserve(decoded.size());size_t added=0;
            for(const auto& p:decoded){Identity id(p.category,p.uid);tile.refs.push_back(id);if(!placements.count(id))added+=sizeof(Placement)+65+64;}
            tile.bytes=tile.refs.capacity()*sizeof(Identity);
            if(metadataSize()+indexSize()+tile.bytes+added>limits.metadataBytes*3/4){++stats.deferred;++stats.pendingTiles;continue;}
            for(auto& p:decoded)placements.emplace(Identity(p.category,p.uid),std::move(p));
            tiles.emplace(cell,std::move(tile));dirty=true;
        }
        if(dirty)index();
        Stamp selectionStart=Clock::now();std::set<Identity> candidates;
        const float reach=928.f;int x0=int(std::floor((r.center.x-reach)/limits.cellSize)),x1=int(std::floor((r.center.x+reach)/limits.cellSize));int y0=int(std::floor((r.center.y-reach)/limits.cellSize)),y1=int(std::floor((r.center.y+reach)/limits.cellSize));
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){auto it=cells.find({x,y});if(it!=cells.end())candidates.insert(it->second.begin(),it->second.end());}candidates.insert(giants.begin(),giants.end());
        std::vector<Placement> chosen;std::map<std::string,float> priority;std::set<std::string> needed;
        for(const auto& id:candidates){const auto& p=placements.at(id);if(!selected(r,p.low,p.high))continue;chosen.push_back(p);needed.insert(p.modelKey);float d=distance2((p.low+p.high)*.5f,r.center);auto it=priority.find(p.modelKey);if(it==priority.end()||d<it->second)priority[p.modelKey]=d;}
        lastNeeded=needed;
        for(const auto& key:needed){auto it=models.find(key);if(it!=models.end()){it->second.used=now;++stats.modelReuses;}}
        std::vector<std::pair<float,std::string>> queue;for(const auto& p:priority)if(!models.count(p.first)&&!failedModels.count(p.first))queue.emplace_back(p.second,p.first);std::sort(queue.begin(),queue.end());stats.pendingModels=uint32_t(queue.size());stats.selectionMs+=ms(selectionStart);
        auto fullyReady=[&]{if(stats.pendingTiles)return false;for(const auto& k:needed)if(!models.count(k))return false;return true;};
        evict(needed,false);publish(r,chosen,gen,start,fullyReady());Stamp lastPublish=Clock::now();bool firstReady=models.empty();
        for(const auto& queued:queue){if(superseded(serial))break;if(!current(gen)){++stats.cancellations;return;}if(!r.allowLoads){++stats.deferred;break;}
            std::string path=root+"models/"+queued.second+".fgs";Reader preflight;Stamp job=Clock::now();
            if(!preflight.open(path,limits.maxDecodeBytes)){++stats.failures;failedModels.insert(queued.second);--stats.pendingModels;continue;}
            size_t fileBytes=preflight.remaining;
            char magic[4];uint32_t version,nv,nt,nm;
            if(!preflight.bytes(magic,4)||std::memcmp(magic,"FGS2",4)||!preflight.u32(version)||version!=2||!preflight.u32(nv)||!preflight.u32(nt)||!preflight.u32(nm)||uint64_t(nv)*32+uint64_t(nt)*16>preflight.remaining){++stats.failures;failedModels.insert(queued.second);--stats.pendingModels;continue;}
            size_t textureBytes=preflight.remaining-size_t(nv)*32-size_t(nt)*16;
            size_t packedEstimate=size_t(nv)*20+size_t(nt)*(nv<=65536?6:12)+textureBytes*2+size_t(nm)*256;
            // At most two index copies while grouping; original GI scene absent.
            size_t transientEstimate=size_t(nv)*20+size_t(nt)*42+textureBytes*2+size_t(nm)*384+size_t(std::min(nt,65536u))*128;
            if(transientEstimate>limits.maxDecodeBytes){++stats.deferred;continue;}
            if(residentBytes+packedEstimate>limits.residentBytes)evict(needed,true);
            if(residentBytes+packedEstimate>limits.residentBytes){++stats.deferred;continue;}
            stats.transientPeak=std::max(stats.transientPeak,uint64_t(transientEstimate));
            std::string error;Stamp readStart=Clock::now();++stats.modelReads;stats.bytesRead+=fileBytes;
            auto model=std::make_shared<Model>();
            if(!loadCompact(path,queued.second,limits.maxDecodeBytes,*model,error)){++stats.failures;failedModels.insert(queued.second);--stats.pendingModels;continue;}stats.readMs+=ms(readStart);
            if(!current(gen)){++stats.cancellations;return;}
            if(residentBytes+model->bytes>limits.residentBytes){++stats.deferred;continue;}
            residentBytes+=model->bytes;stats.largestModel=std::max(stats.largestModel,uint64_t(model->bytes));models.emplace(queued.second,Resident{model,Clock::now()});--stats.pendingModels;stats.maxJobMs=std::max(stats.maxJobMs,ms(job));if(firstReady||ms(lastPublish)>=25){publish(r,chosen,gen,start,false);lastPublish=Clock::now();firstReady=false;}
        }
        publish(r,chosen,gen,start,fullyReady());
    }
    void run(){
#ifdef _WIN32
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
#endif
        for(;;){Request r;uint64_t gen=0,serial=0;bool work=false,reclaim=false;
            {std::unique_lock<std::mutex> lock(mutex);wake.wait_for(lock,std::chrono::seconds(1),[&]{return stop||reclaimPending||(!paused&&hasRequest&&doneSerial<requestSerial);});if(stop)return;
             reclaim=reclaimPending;reclaimPending=false;work=!paused&&hasRequest&&doneSerial<requestSerial;
             if(reclaim||work){gen=generation;serial=requestSerial;busy=true;if(work)r=requested;}}
            if(reclaim){reset(gen);std::lock_guard<std::mutex> lock(mutex);busy=work;if(!work){doneSerial=serial;wake.notify_all();continue;}}
            if(!work){try{maintain();}catch(...){++stats.failures;}continue;}
            try{process(r,gen,serial);}catch(...){++stats.failures;}
            {std::lock_guard<std::mutex> lock(mutex);busy=false;doneSerial=serial;retryAfter=Clock::now()+std::chrono::milliseconds(500);wake.notify_all();}
        }
    }
};
Streamer::Streamer(std::string root,Limits limits):state_(new State(std::move(root),limits)){}
Streamer::~Streamer()=default;
void Streamer::request(const Request& r){
    if(r.map.empty()||r.map.find("..")!=std::string::npos||r.map.find_first_of("/\\")!=std::string::npos||!finite(r.center)||std::fabs(r.center.x)>100000||std::fabs(r.center.y)>100000||std::fabs(r.center.z)>100000)return;
    for(unsigned i=0;i<2;++i)if(r.active[i]&&!finite(r.directions[i]))return;
    auto& s=*state_;std::shared_ptr<const Snapshot> retired;
    {std::lock_guard<std::mutex> lock(s.mutex);bool mapChanged=!s.hasRequest||s.requested.map!=r.map;bool changed=mapChanged||distance2(s.requested.center,r.center)>=32*32||s.requested.allowLoads!=r.allowLoads;
    for(unsigned i=0;i<2;++i)if(r.active[i]!=s.requested.active[i]||(r.active[i]&&NorthlightGI::dot(NorthlightGI::normalized(r.directions[i]),NorthlightGI::normalized(s.requested.directions[i]))<.999847695f))changed=true;
    if(!changed&&s.published&&(s.published->stats.pendingModels||s.published->stats.pendingTiles)&&s.doneSerial==s.requestSerial&&Clock::now()>=s.retryAfter)changed=true;
    if(!changed)return;
    if(mapChanged){++s.generation;retired=std::move(s.published);}
    s.requested=r;s.hasRequest=true;++s.requestSerial;}
    s.wake.notify_all();
    if(retired)NorthlightStreaming::cpuRetirement().retire(retired,retired->retirementBytes);
}
std::shared_ptr<const Snapshot> Streamer::snapshot()const {auto& s=*state_;std::lock_guard<std::mutex> lock(s.mutex);return s.published;}
void Streamer::setPaused(bool paused){
    auto& s=*state_;std::shared_ptr<const Snapshot> retired;
    {std::lock_guard<std::mutex> lock(s.mutex);if(s.paused==paused)return;s.paused=paused;
     ++s.requestSerial;
     if(paused){++s.generation;s.reclaimPending=true;retired=std::move(s.published);}}
    s.wake.notify_all();
    if(retired)NorthlightStreaming::cpuRetirement().retire(retired,retired->retirementBytes);
}
bool Streamer::waitIdle(unsigned timeoutMs)const {auto& s=*state_;std::unique_lock<std::mutex> lock(s.mutex);return s.wake.wait_for(lock,std::chrono::milliseconds(timeoutMs),[&]{return s.stop||(!s.busy&&!s.reclaimPending&&(!s.hasRequest||s.paused||s.doneSerial==s.requestSerial));});}
}
