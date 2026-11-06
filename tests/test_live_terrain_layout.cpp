// Same bounded arena, immutable terrain and GPU-lifetime model; only the GPU vertex stride differs.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#define main legacy_arena_test_main
#include "test_live_terrain_gpu.cpp"
#undef main
#pragma GCC diagnostic pop
#include "static_shadow_scene.h"
#include <cstddef>
using CompactVertex=StaticShadow::Vertex;
static_assert(sizeof(Vertex)==32&&sizeof(CompactVertex)==20&&offsetof(CompactVertex,u)==12,"GPU declarations");
CompactVertex compact(Position p){return {p.x,p.y,p.z,0,0};}
Position position(Vertex v){return v.position;}
Position position(CompactVertex v){assert(v.u==0&&v.v==0);return {v.x,v.y,v.z};}
struct Counts {uint64_t bytes=0,reused=0,owners=0;unsigned rollovers=0;};
template<class V> struct Model {
    IDirect3DDevice9 d;NorthlightLiveTerrainGPU::Cache<Snapshot,V> cache;
    Counts total;std::vector<uint32_t> point,directional;std::vector<uint64_t> submitted;
    explicit Model(bool queries):cache(16u*1024u*1024u){d.driver.querySupport=queries;}
    template<class Convert> void frame(const std::vector<Owner>& active,unsigned generation,Convert cv,bool fail=false){
        d.driver.completed=submitted.size()>2?submitted[submitted.size()-3]:0;
        const auto rolloversBefore=cache.rollovers;d.driver.failLock=fail;cache.beginFrame();const auto hr=cache.update(&d,active,admit,cv);d.driver.failLock=false;
        if(fail){assert(hr==E_FAIL);return;}assert(hr==S_OK&&cache.bytes()<=16u*1024u*1024u);
        total.bytes+=cache.uploadedBytes;total.reused+=cache.reusedVertices;total.owners+=cache.newOwners;total.rollovers+=cache.rollovers-rolloversBefore;
        auto select=[&](const Snapshot& s,uint32_t offset,std::vector<uint32_t>& out){
            for(size_t i=0;i+2<s.indices.size();i+=3)if((i/3+generation)%3)out.insert(out.end(),{s.indices[i]+offset,s.indices[i+1]+offset,s.indices[i+2]+offset});
        };
        assert(cache.indices(active,generation,select,point,directional));
        size_t next=0;for(const auto& owner:active){uint32_t lo=UINT32_MAX,hi=0;
            for(auto i:owner->indices){const auto actual=point[next++];V v;std::memcpy(&v,cache.vertices()->data.data()+size_t(actual)*sizeof(V),sizeof(V));
                const auto p=position(v),expected=owner->positions[i];assert(!std::memcmp(&p,&expected,sizeof p));lo=std::min(lo,actual);hi=std::max(hi,actual);
            }if(lo!=UINT32_MAX)cache.vertices()->read(lo*sizeof(V),(hi-lo+1)*sizeof(V));
        }assert(next==point.size());
        size_t p=0,q=0;assert(cache.prepare(active,generation,select,p,q));std::vector<uint32_t> written(p+q);cache.write(written.data());
        assert(p==point.size()&&q==directional.size()&&std::equal(point.begin(),point.end(),written.begin())&&std::equal(directional.begin(),directional.end(),written.begin()+p));
        submitted.push_back(d.driver.submitted);
    }
};
Owner patch(unsigned id){auto s=std::make_shared<Snapshot>();for(unsigned i=0;i<290;++i)s->positions.push_back({5200+float(id)*.125f,float(i)*.03125f,float(id%7)-float(i%13)*.0625f});
    for(unsigned i=0;i<288;++i)s->indices.push_back((i*7)%290);return s;}
void compare(bool queries){
    Model<Vertex> old(queries);Model<CompactVertex> small(queries);std::vector<Owner> pool;for(unsigned i=0;i<2000;++i)pool.push_back(patch(i));
    const unsigned starts[]={0,500,1000,1300,1000,500,0,500,1000,1300,0,1300,500,0,1000,1300,1000,500,0};
    for(unsigned f=0;f<sizeof starts/sizeof *starts;++f){
        if(f==7){for(unsigned i=500;i<520;++i)pool[i]=patch(i+3000);} // nonuniform motion/new immutable owners
        if(f==12){old.cache.clear();small.cache.clear();} // map/device reset, identical CPU owners
        std::vector<Owner> active(pool.begin()+starts[f],pool.begin()+starts[f]+700);if(f%2)std::reverse(active.begin(),active.end());
        old.frame(active,1+f/4,convert);small.frame(active,1+f/4,compact);
        assert(old.point.size()==small.point.size()&&old.directional.size()==small.directional.size());
        // Physical offsets differ; the selected logical triangle streams must match exactly.
        for(size_t i=0;i<old.directional.size();++i){Vertex a;CompactVertex b;
            std::memcpy(&a,old.cache.vertices()->data.data()+old.directional[i]*sizeof(a),sizeof a);
            std::memcpy(&b,small.cache.vertices()->data.data()+small.directional[i]*sizeof(b),sizeof b);
            const auto pa=position(a),pb=position(b);assert(!std::memcmp(&pa,&pb,sizeof pa));}
    }
    assert(small.total.bytes<old.total.bytes*5/8&&small.total.owners<old.total.owners&&small.total.reused>old.total.reused);
    assert(small.total.rollovers<=old.total.rollovers);
    std::printf("LAYOUT_ROTATION queries=%d frames=19 verticesPerFrame=203000 oldBytes=%llu newBytes=%llu oldOwners=%llu newOwners=%llu oldReused=%llu newReused=%llu oldRollovers=%u newRollovers=%u exact=1\n",int(queries),
        (unsigned long long)old.total.bytes,(unsigned long long)small.total.bytes,(unsigned long long)old.total.owners,(unsigned long long)small.total.owners,
        (unsigned long long)old.total.reused,(unsigned long long)small.total.reused,old.total.rollovers,small.total.rollovers);
    // Both failed writes invalidate their contents; retry reuploads exactly the same active set.
    std::vector<Owner> active={patch(9999)};old.frame(active,99,convert,true);small.frame(active,99,compact,true);
    old.frame(active,99,convert);small.frame(active,99,compact);assert(old.cache.uploadedBytes==290*sizeof(Vertex)&&small.cache.uploadedBytes==290*sizeof(CompactVertex));
    for(auto* d:{&old.d,&small.d}){d->driver.failUnlock=true;}
    active={patch(9998)};
    old.cache.beginFrame();small.cache.beginFrame();assert(old.cache.update(&old.d,active,admit,convert)==E_FAIL&&small.cache.update(&small.d,active,admit,compact)==E_FAIL);
    old.d.driver.failUnlock=small.d.driver.failUnlock=false;old.frame(active,100,convert);small.frame(active,100,compact);
    // The existing admission fallback stays under the unchanged 8 MiB ceiling.
    Model<CompactVertex> pressure(true);pressure.cache.beginFrame();assert(pressure.cache.update(&pressure.d,active,[](size_t n){return n<=8u*1024u*1024u;},compact)==S_OK);
    assert(pressure.cache.bytes()<=8u*1024u*1024u&&pressure.cache.vertexCapacity()>=262144);
}
int main(){compare(true);compare(false);std::puts("PASS compact live terrain: equal geometry/draw streams, rotation reuse, busy/unsupported fences, immutable replacement, map reset, write failures, bounded admission");}
