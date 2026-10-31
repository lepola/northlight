#include "terrain_gpu_harness.h"
static Owner identified(float position,uint64_t revision){auto owner=std::const_pointer_cast<Snapshot>(triangle(position));owner->sourceIdentity=NorthlightSourceIdentity::make({100,revision});return owner;}
int main(){
    IDirect3DDevice9 d;Cache cache(90*sizeof(Vertex));std::vector<Owner> active{identified(1,1)};std::vector<uint32_t> point,directional;
    cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK&&cache.newOwners==1);assert(cache.indices(active,1,allIndices,point,directional));verify(cache,active,point);draw(cache,point);const auto locks=d.driver.locks;const auto first=point;
    std::weak_ptr<const Snapshot> gone=active[0];active.clear();cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK&&gone.expired());
    for(unsigned i=0;i<5;++i){active={identified(float(10+i),10+i)};cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK);}
    active={identified(1,1)};cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK&&!cache.uploadedBytes&&!cache.newOwners&&cache.reconstructedHits==1);assert(cache.indices(active,1,allIndices,point,directional)&&point==first);verify(cache,active,point);
    active={identified(2,2)};cache.beginFrame();assert(cache.update(&d,active,admit,convert)==S_OK&&cache.uploadedBytes==3*sizeof(Vertex));assert(cache.indices(active,2,allIndices,point,directional));verify(cache,active,point);assert(point!=first&&d.driver.locks>locks);
    cache.clear();active.clear();assert(!cache.bytes()&&!cache.indexBytes());
    std::puts("SHARED_TERRAIN_IDENTITY_PASS orbit_away_back_new_CPU_owner_zero_upload no_CPU_retention exact_vertices_indices revision_change_new_range reset");
}
