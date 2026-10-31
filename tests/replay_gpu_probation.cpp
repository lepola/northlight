#include "replay_gpu_harness.h"
#include "legacy_replay_gpu_cache.h"
#include <chrono>
#include <string>

using Mesh=NorthlightDrawSnapshot::Mesh;
static auto tracked(uint64_t id,unsigned bytes=240){auto m=std::const_pointer_cast<Mesh>(mesh(bytes));m->sourceIdentity=NorthlightSourceIdentity::make({0xcca69996,id});return std::shared_ptr<const Mesh>(m);}
struct Bindings {
    IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;
    ~Bindings(){clear();}
    void clear(){for(auto& p:vb){if(p)p->Release();p=nullptr;}if(ib)ib->Release();ib=nullptr;}
    void exact(const Mesh& m,bool resident){
        if(!resident)return; // The unchanged fallback consumes this exact immutable Mesh.
        for(unsigned s=0;s<4;++s){const auto& bytes=m.streams[s].bytes;if(bytes.empty()){assert(!vb[s]);continue;}assert(vb[s]);auto* b=static_cast<Buffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC>*>(vb[s]);assert(b->data==bytes);}
        if(m.indices.empty()){assert(!ib);return;}assert(ib);auto* b=static_cast<Buffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC>*>(ib);assert(b->data.size()==m.indices.size()*4&&!std::memcmp(b->data.data(),m.indices.data(),b->data.size()));
    }
};
template<class Cache> static bool bindMesh(Cache& c,Device& d,const std::shared_ptr<const Mesh>& m,Bindings& b,bool admission=true){const bool ready=c.bind(&d,m,b.vb,b.ib,[&](size_t){return admission;});b.exact(*m,ready);b.clear();return ready;}
struct Orbit {size_t returnWarmups=0,returnUPBytes=0,reuploaded=0;unsigned evictions=0;double ms=0;};
template<class Cache> static Orbit orbit(){
    Cache cache;cache.ignoreTimeBudget(true);Device driver;Bindings binding;Orbit result;
    std::vector<std::shared_ptr<const Mesh>> residents,probation;
    for(unsigned i=0;i<160;++i)residents.push_back(tracked(i,65536));
    for(unsigned i=160;i<4096;++i)probation.push_back(tracked(i));
    cache.beginFrame();for(auto& m:residents)assert(!bindMesh(cache,driver,m,binding));for(auto& m:probation)assert(!bindMesh(cache,driver,m,binding));
    for(unsigned first=0;first<160;first+=16){cache.beginFrame();for(unsigned i=first;i<first+16;++i)assert(bindMesh(cache,driver,residents[i],binding));assert(cache.uploaded()<=4u*1024u*1024u);}
    assert(cache.population().entries==4096&&cache.population().resident==160);
    // A rotating stream sees many unproven sources after the resident region.
    // Admission is independently refused, so this phase retains 3936 records.
    cache.beginFrame();for(auto& m:probation)assert(!bindMesh(cache,driver,m,binding,false));
    const auto begin=std::chrono::steady_clock::now();cache.beginFrame();
    for(unsigned i=4096;i<4346;++i)assert(!bindMesh(cache,driver,tracked(i),binding));
    result.evictions=cache.stats().evictions;
    assert(cache.population().entries==4096&&cache.chargedBytes()<=cache.limit());
    cache.beginFrame();for(auto& m:residents)if(!bindMesh(cache,driver,m,binding)){++result.returnWarmups;result.returnUPBytes+=m->byteSize();}
    result.reuploaded+=cache.uploaded();
    for(unsigned first=0;first<160;first+=16){cache.beginFrame();for(unsigned i=first;i<first+16;++i)assert(bindMesh(cache,driver,residents[i],binding));result.reuploaded+=cache.uploaded();assert(cache.uploaded()<=4u*1024u*1024u);}
    result.ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    cache.clear();assert(!alive);return result;
}
template<class Cache> static double warm(){
    Cache cache;cache.ignoreTimeBudget(true);Device driver;Bindings binding;std::vector<std::shared_ptr<const Mesh>> sources;
    cache.beginFrame();for(unsigned i=0;i<128;++i){sources.push_back(tracked(50000+i));assert(!bindMesh(cache,driver,sources.back(),binding));}
    for(unsigned first=0;first<128;first+=16){cache.beginFrame();for(unsigned i=first;i<first+16;++i)assert(bindMesh(cache,driver,sources[i],binding));}
    const auto begin=std::chrono::steady_clock::now();
    for(unsigned f=0;f<64;++f){cache.beginFrame();for(auto& m:sources)assert(bindMesh(cache,driver,m,binding));assert(!cache.uploaded()&&!cache.stats().evictions&&!cache.stats().warmup);}
    const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();cache.clear();assert(!alive);return ms;
}
template<class Cache> static std::vector<size_t> bytePressure(){
    Cache cache;cache.ignoreTimeBudget(true);Device driver;Bindings b;auto a=mesh(),p=mesh(),q=mesh();std::vector<size_t> result;
    cache.setLimit(a->byteSize()+4*sizeof(void*));
    const auto record=[&](const auto& m){result.push_back(bindMesh(cache,driver,m,b));result.push_back(cache.bytes());result.push_back(cache.stats().evictions);result.push_back(cache.stats().roomRejected);};
    cache.beginFrame();record(a);cache.beginFrame();record(a);record(p);cache.beginFrame();record(p);
    cache.beginFrame();record(p);record(q);cache.beginFrame();record(p);record(q);cache.clear();assert(!alive);return result;
}
static void controls(){
    using Cache=NorthlightReplayGPU::Cache;Cache cache;cache.ignoreTimeBudget(true);Device driver;Bindings b;auto a=mesh(),p=mesh(),q=mesh();
    // Byte pressure still evicts the original global-LRU resident, even with
    // probation entries available. Promotions need no extra count slot.
    cache.setLimit(a->byteSize()+4*sizeof(void*));cache.beginFrame();assert(!bindMesh(cache,driver,a,b));cache.beginFrame();assert(bindMesh(cache,driver,a,b));assert(!bindMesh(cache,driver,p,b));cache.beginFrame();assert(bindMesh(cache,driver,p,b));assert(cache.stats().residentEvictions==1&&cache.stats().countOnlyProbationEvictions==0&&cache.stats().bytePressure==1);assert(cache.bytes()==p->byteSize()&&cache.chargedBytes()==cache.bytes()+cache.queueBytes());
    // A currently bound resident is never evicted, including for promotion.
    cache.beginFrame();assert(bindMesh(cache,driver,p,b));assert(!bindMesh(cache,driver,q,b));cache.beginFrame();assert(bindMesh(cache,driver,p,b));assert(!bindMesh(cache,driver,q,b));assert(cache.stats().roomRejected==1&&cache.stats().residentEvictions==0);cache.clear();assert(!alive);
    // Partial upload and lock failures leave the probation queue intact.
    cache.setLimit(64u<<20);auto failed=tracked(9000);cache.beginFrame();assert(!bindMesh(cache,driver,failed,b));cache.beginFrame();driver.failAt=driver.calls+2;assert(!bindMesh(cache,driver,failed,b)&&!alive);driver.failAt=0;driver.lockFail=true;assert(!bindMesh(cache,driver,failed,b)&&!alive);driver.lockFail=false;assert(bindMesh(cache,driver,failed,b));assert(cache.population().resident==1);cache.clear();assert(!alive);
    // Unknown probation and resident expiry must unlink both queues correctly.
    auto unknown=mesh();cache.beginFrame();assert(!bindMesh(cache,driver,unknown,b));unknown.reset();cache.beginFrame();assert(cache.population().entries==0);unknown=mesh();assert(!bindMesh(cache,driver,unknown,b));cache.beginFrame();assert(bindMesh(cache,driver,unknown,b));unknown.reset();cache.beginFrame();assert(!cache.population().entries&&!alive);
    // All-probation pressure uses the same oldest victim. Current-frame
    // probation records are protected, just like current-frame residents.
    std::vector<std::shared_ptr<const Mesh>> many;cache.beginFrame();for(unsigned i=0;i<4096;++i){many.push_back(tracked(10000+i));assert(!bindMesh(cache,driver,many.back(),b));}
    assert(!bindMesh(cache,driver,tracked(20000),b)&&cache.stats().roomRejected==1&&cache.stats().evictions==0);
    cache.beginFrame();assert(!bindMesh(cache,driver,tracked(20001),b));assert(cache.population().entries==4096&&cache.stats().probationEvictions==1&&cache.stats().residentEvictions==0&&cache.stats().countOnlyProbationEvictions==1);
    cache.clear();cache.setLimit(0);assert(!cache.population().entries&&!cache.bytes()&&!cache.chargedBytes()&&!alive);cache.setLimit(64u<<20);
    // At count pressure, current probation cannot shield an old resident.
    auto resident=tracked(30000);cache.beginFrame();assert(!bindMesh(cache,driver,resident,b));cache.beginFrame();assert(bindMesh(cache,driver,resident,b));cache.beginFrame();for(unsigned i=0;i<4095;++i)assert(!bindMesh(cache,driver,tracked(31000+i),b));assert(!bindMesh(cache,driver,tracked(40000),b));assert(cache.stats().residentEvictions==1&&cache.stats().countOnlyProbationEvictions==0);cache.clear();assert(!alive);
    std::puts("REPLAY_PROBATION_CONTROLS_PASS byte_LRU current_resident current_probation all_probation promotion partial_failure lock_failure unknown_expiry pressure reset");
}
int main(int argc,char** argv){
    if(argc>1){const std::string mode=argv[1];const bool old=mode=="old"||mode=="warm-old";
        if(mode.rfind("warm-",0)==0){const double ms=old?warm<LegacyReplayGPU::Cache>():warm<NorthlightReplayGPU::Cache>();std::printf("REPLAY_PROBATION_WARM cache=%s cpuMs=%.6f residentBinds=8192\n",old?"old":"new",ms);return 0;}
        const auto r=old?orbit<LegacyReplayGPU::Cache>():orbit<NorthlightReplayGPU::Cache>();std::printf("REPLAY_PROBATION_TIMING cache=%s cpuMs=%.6f warmups=%zu upBytes=%zu reuploadedBytes=%zu evictions=%u\n",old?"old":"new",r.ms,r.returnWarmups,r.returnUPBytes,r.reuploaded,r.evictions);return 0;}
    const auto old=orbit<LegacyReplayGPU::Cache>(),next=orbit<NorthlightReplayGPU::Cache>();
    assert(old.returnWarmups==160&&next.returnWarmups==0&&old.returnUPBytes==160*size_t(65536+80+12)&&old.reuploaded==old.returnUPBytes&&!next.returnUPBytes&&!next.reuploaded&&old.evictions==250&&next.evictions==250);
    std::printf("REPLAY_PROBATION_FROZEN_PASS baseline=cc69996 entries=4096 initialResident=160 initialProbation=3936 rotatingSources=250 oldReturnWarmups=%zu newReturnWarmups=%zu oldReturnUPBytes=%zu newReturnUPBytes=%zu oldReuploadedBytes=%zu newReuploadedBytes=%zu evictionCountUnchanged=%u bytePayloadExact=1\n",old.returnWarmups,next.returnWarmups,old.returnUPBytes,next.returnUPBytes,old.reuploaded,next.reuploaded,next.evictions);
    assert(bytePressure<LegacyReplayGPU::Cache>()==bytePressure<NorthlightReplayGPU::Cache>());
    std::puts("REPLAY_PROBATION_BYTE_FROZEN_PASS baseline=cc69996 exact_bind_payload_eviction_rejection_sequence=1 same_byte_limit=1");
    controls();
}
