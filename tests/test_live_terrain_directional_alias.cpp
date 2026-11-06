// Differential against the materialized directional path; no D3D device or game.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main original_arena_test_main
#include "test_live_terrain_gpu.cpp"
#undef main
#pragma GCC diagnostic pop
#include "shadow_terrain.h"
#include "static_shadow_scene.h"
#include <chrono>
struct TileSnapshot {
    std::vector<Position> positions;std::vector<uint32_t> indices;
    struct Chunk {int x,y;};struct Bounds {std::vector<Chunk> chunks;} bounds;
};
using TileOwner=std::shared_ptr<const TileSnapshot>;
using TileCache=NorthlightLiveTerrainGPU::Cache<TileSnapshot,StaticShadow::Vertex>;
using Fixed=std::set<std::pair<int,int>>;
TileOwner tile(unsigned id){
    auto s=std::make_shared<TileSnapshot>();const int cx=300+id%50,cy=300+id/50;
    s->bounds.chunks.push_back({cx,cy});if(id%11==0)s->bounds.chunks.push_back({cx+1,cy});
    if(id%37==0)s->bounds.chunks.push_back({-1,-1});
    for(unsigned y=0;y<17;++y)for(unsigned x=0;x<17;++x)s->positions.push_back({float(NorthlightShadowTerrain::Origin-(cy+double(y)/16)*NorthlightShadowTerrain::Chunk),float(NorthlightShadowTerrain::Origin-(cx+double(x)/16)*NorthlightShadowTerrain::Chunk),float((x+y+id)%13)*.125f});
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){const uint32_t a=y*17+x,b=a+1,c=a+17,d=c+1;s->indices.insert(s->indices.end(),{a,b,c,b,d,c});}
    return s;
}
StaticShadow::Vertex cv(Position p){return {p.x,p.y,p.z,0,0};}
struct Model {IDirect3DDevice9 d;TileCache cache;std::vector<uint64_t> submitted;uint64_t builds=0,aliases=0,triangles=0;size_t peakIndexBytes=0;
    explicit Model(bool queries=true,uint32_t bytes=16u*1024u*1024u):cache(bytes){d.driver.querySupport=queries;}
    void update(const std::vector<TileOwner>& active){d.driver.completed=submitted.size()>2?submitted[submitted.size()-3]:0;cache.beginFrame();assert(cache.update(&d,active,admit,cv)==S_OK);}
    bool prepare(const std::vector<TileOwner>& active,const Fixed& fixed,uint64_t generation,bool alias,std::vector<uint32_t>& out,unsigned throwId=0){size_t p=0,q=0;
        auto build=[&](const auto& s,uint32_t offset,std::vector<uint32_t>& list){NorthlightShadowTerrain::appendLiveDirectional(s,fixed,offset,list);};
        auto all=[&](const auto& s){if(throwId)throw std::bad_alloc();return alias&&!s.bounds.chunks.empty()&&std::none_of(s.bounds.chunks.begin(),s.bounds.chunks.end(),[&](const auto& c){return c.x<0||c.y<0||c.x>=1024||c.y>=1024||fixed.count({c.x,c.y})!=0;});};
        if(!cache.prepare(active,generation,build,p,q,all))return false;
        builds+=cache.directionalBuilds;aliases+=cache.directionalAliases;triangles+=cache.directionalTriangles;peakIndexBytes=std::max(peakIndexBytes,cache.indexBytes());
        out.resize(p+q);cache.write(out.data());return true;
    }
    void read(const std::vector<TileOwner>& active,const std::vector<uint32_t>& out){size_t next=0;for(const auto& s:active){uint32_t lo=UINT32_MAX,hi=0;
        for(auto index:s->indices){const uint32_t i=out[next++];StaticShadow::Vertex v;std::memcpy(&v,cache.vertices()->data.data()+i*sizeof(v),sizeof v);const auto expected=s->positions[index];assert(v.x==expected.x&&v.y==expected.y&&v.z==expected.z&&v.u==0&&v.v==0);lo=std::min(lo,i);hi=std::max(hi,i);}
        cache.vertices()->read(lo*sizeof(StaticShadow::Vertex),(hi-lo+1)*sizeof(StaticShadow::Vertex));
    }submitted.push_back(d.driver.submitted);}
};
void equalGeometry(const Model& a,const Model& b,const std::vector<uint32_t>& x,const std::vector<uint32_t>& y){assert(x.size()==y.size());
    for(size_t i=0;i<x.size();++i)assert(!std::memcmp(a.cache.vertices()->data.data()+x[i]*sizeof(StaticShadow::Vertex),b.cache.vertices()->data.data()+y[i]*sizeof(StaticShadow::Vertex),sizeof(StaticShadow::Vertex)));}
void differential(){Model old,next;std::vector<TileOwner> pool;for(unsigned i=0;i<1000;++i)pool.push_back(tile(i));size_t compared=0;
    for(unsigned frame=0;frame<40;++frame){
        Fixed fixed;for(int y=300;y<322;++y)for(int x=300;x<352;++x)if((x+y+frame/3)%5==0)fixed.insert({x,y});if(frame%7==0)fixed.insert({-1,-1});
        if(frame==13)pool[500]=tile(1500);if(frame==24){old.cache.clear();next.cache.clear();}
        const unsigned start=(frame%4)*100;std::vector<TileOwner> active(pool.begin()+start,pool.begin()+start+700);if(frame%2)std::reverse(active.begin(),active.end());if(frame%9==0)active.push_back(active.front());
        old.update(active);next.update(active);std::vector<uint32_t> a,b;assert(old.prepare(active,fixed,frame+1,false,a)&&next.prepare(active,fixed,frame+1,true,b));
        assert(a==b&&old.cache.vertexCapacity()==next.cache.vertexCapacity()&&old.cache.uploadedBytes==next.cache.uploadedBytes);compared+=a.size();
        old.read(active,a);next.read(active,b);
        // Calling the original API after an aliased prepare must read the alias too.
        std::vector<uint32_t> p,q;assert(next.cache.indices(active,frame+1,[&](const auto& s,uint32_t offset,std::vector<uint32_t>& list){NorthlightShadowTerrain::appendLiveDirectional(s,fixed,offset,list);},p,q));
        assert(p.size()+q.size()==b.size()&&std::equal(p.begin(),p.end(),b.begin())&&std::equal(q.begin(),q.end(),b.begin()+p.size()));
    }
    assert(next.aliases>15000&&next.builds<old.builds/2&&next.peakIndexBytes<old.peakIndexBytes);
    std::printf("DIRECTIONAL_ALIAS_PASS frames=40 comparedIndices=%zu oldBuilds=%llu newBuilds=%llu aliases=%llu oldTriangles=%llu newTriangles=%llu oldPeakIndexBytes=%zu newPeakIndexBytes=%zu exact=1\n",compared,(unsigned long long)old.builds,(unsigned long long)next.builds,(unsigned long long)next.aliases,(unsigned long long)old.triangles,(unsigned long long)next.triangles,old.peakIndexBytes,next.peakIndexBytes);
    // Unknown/mixed membership uses the original filter; empty fixed sets prove a full list.
    const Fixed empty;std::vector<TileOwner> active={pool[37],pool[22],pool[38]};std::vector<uint32_t> a,b;
    old.update(active);next.update(active);assert(old.prepare(active,empty,100,false,a)&&next.prepare(active,empty,100,true,b)&&a==b);
    next.update(active);assert(!next.prepare(active,empty,101,true,b,1));assert(b.size()==a.size());
    next.update(active);assert(next.prepare(active,empty,102,true,b));old.update(active);assert(old.prepare(active,empty,102,false,a));equalGeometry(old,next,a,b);
    // Same owner pointers, changed geometry is represented by a new immutable capture; it must upload.
    auto changed=std::make_shared<TileSnapshot>(*active[0]);changed->positions[0].z+=1;active[0]=changed;
    old.update(active);next.update(active);assert(old.prepare(active,empty,103,false,a)&&next.prepare(active,empty,103,true,b)&&next.cache.uploadedBytes>0);equalGeometry(old,next,a,b);
    // Missing owner falls back exactly as prepare() always did, with no dangling alias.
    active.push_back(tile(1900));size_t p=0,q=0;assert(!next.cache.prepare(active,104,[](const auto&,uint32_t,std::vector<uint32_t>&){},p,q,[](const auto&){return true;}));
    // No alias is claimed for empty, out-of-grid or non-chunk metadata.
    active.clear();for(unsigned kind=0;kind<3;++kind){auto unknown=std::make_shared<TileSnapshot>(*pool[kind]);unknown->bounds.chunks.clear();
        if(kind)unknown->bounds.chunks.push_back({kind==1?-1:1024,300});active.push_back(unknown);}
    old.update(active);next.update(active);assert(old.prepare(active,empty,105,false,a)&&next.prepare(active,empty,105,true,b));equalGeometry(old,next,a,b);assert(!next.cache.directionalAliases);
}
void lifetime(bool queries){Model old(queries,867*sizeof(StaticShadow::Vertex)),next(queries,867*sizeof(StaticShadow::Vertex));
    std::vector<TileOwner> pool={tile(1),tile(2),tile(3),tile(4)};const Fixed fixed;std::vector<uint32_t> a,b;
    for(unsigned frame=0;frame<6;++frame){std::vector<TileOwner> active={pool[frame%4],pool[(frame+1)%4]};
        old.update(active);next.update(active);assert(old.prepare(active,fixed,frame+1,false,a)&&next.prepare(active,fixed,frame+1,true,b)&&a==b);old.read(active,a);next.read(active,b);}
    assert(old.cache.rollovers&&old.cache.rollovers==next.cache.rollovers);
    for(bool unlock:{false,true}){std::vector<TileOwner> active={tile(unlock?10:11)};
        next.cache.beginFrame();next.d.driver.failLock=!unlock;next.d.driver.failUnlock=unlock;assert(next.cache.update(&next.d,active,admit,cv)==E_FAIL);next.d.driver.failLock=next.d.driver.failUnlock=false;
        old.update(active);next.update(active);assert(old.prepare(active,fixed,30+unlock,false,a)&&next.prepare(active,fixed,30+unlock,true,b));equalGeometry(old,next,a,b);
    }
    std::printf("PASS directional alias lifetime queries=%d rollovers=%u busy/unsupported, Lock/Unlock failure and reset preserve exact geometry\n",int(queries),next.cache.rollovers);
}
void benchmark(){const Fixed fixed;std::vector<TileOwner> active;for(unsigned i=0;i<700;++i)active.push_back(tile(i));
    for(bool alias:{false,true}){Model model;model.update(active);std::vector<uint32_t> out;const auto begin=std::chrono::steady_clock::now();
        for(unsigned gen=1;gen<=100;++gen){model.cache.directionalBuilds=model.cache.directionalAliases=0;model.cache.directionalTriangles=0;assert(model.prepare(active,fixed,gen,alias,out));}
        const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        std::printf("DIRECTIONAL_ALIAS_BENCH alias=%d generations=100 snapshots=700 indicesPerGeneration=%zu elapsedMs=%.3f indexBytes=%zu builds=%llu aliases=%llu\n",int(alias),out.size(),ms,model.cache.indexBytes(),(unsigned long long)model.builds,(unsigned long long)model.aliases);
    }
}
int main(int argc,char**){if(argc>1)benchmark();else{differential();lifetime(true);lifetime(false);}}
