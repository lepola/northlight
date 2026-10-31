#include <atomic>
#include <new>
#include <cstdlib>
#include <cstddef>
#include <cstdio>
static bool vertexReadLocked=false,directNullLock=false,directUnlockFailure=false;
static unsigned directUnlockAction=0;static void vertexUnlocked();
static std::atomic<long> allocations{0};static std::atomic<unsigned> allocationCalls{0};static long failAfter=-1;static bool (*failPredicate)()=nullptr;
void* operator new(std::size_t n){if(vertexReadLocked){std::fputs("TERRAIN_ALLOCATION_DURING_VERTEX_LOCK\n",stderr);std::abort();}if(failPredicate&&failPredicate()){failPredicate=nullptr;throw std::bad_alloc();}if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;void* p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();++allocations;++allocationCalls;return p;}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{if(p){--allocations;std::free(p);}}
void operator delete[](void* p)noexcept{::operator delete(p);}
void operator delete(void* p,std::size_t)noexcept{::operator delete(p);}
void operator delete[](void* p,std::size_t)noexcept{::operator delete(p);}
#include "decoded-device.h"
#include "terrain_reference.h"
#include <chrono>
#include <string>
static uint64_t revisions[]={21,22};
static uint64_t lifetimes[]={11,12};
static uint64_t identity(void*,bool index){return lifetimes[index];}
static uint64_t version(void*,bool index){return revisions[index];}
static void vertexUnlocked(){
    if(directUnlockAction==1)++revisions[0];else if(directUnlockAction==2)revisions[0]=0;
    else if(directUnlockAction==3)++revisions[1];else if(directUnlockAction==4)revisions[1]=0;
    else if(directUnlockAction==5)++lifetimes[0];
}
static std::vector<uint16_t> pattern(){std::vector<uint16_t> p(192);for(unsigned i=0;i<p.size();++i)p[i]=uint16_t(i%96);return p;}
static void setup(Device& d,const std::vector<std::vector<uint16_t>>& patterns){
    d.vb.bytes.resize(16+256*size_t(d.stride));for(unsigned i=0;i<256;++i)d.put(i,{-10200.f+float(i%3==1)*2,-1180.f+float(i%3==2)*2,float(i)});
    d.ib.desc.Format=D3DFMT_INDEX16;d.ib.bytes.clear();for(const auto& p:patterns){const size_t begin=d.ib.bytes.size();d.ib.bytes.resize(begin+p.size()*2);std::memcpy(d.ib.bytes.data()+begin,p.data(),p.size()*2);}
}
static void exact(const MeshSnapshot& mesh,const Device& d,unsigned start,unsigned count,D3DPRIMITIVETYPE topology=D3DPT_TRIANGLELIST,INT base=0,UINT streamOffset=16){
    const unsigned indexBytes=d.ib.desc.Format==D3DFMT_INDEX16?2:4;
    std::vector<Position> expected;const auto index=[&](size_t i){uint32_t n=0;std::memcpy(&n,d.ib.bytes.data()+indexBytes*(start+i),indexBytes);return n;};
    const auto position=[&](uint32_t i){Position p;std::memcpy(&p,d.vb.bytes.data()+streamOffset+4+size_t(base+INT(i))*d.stride,12);return p;};
    const unsigned triangles=topology==D3DPT_TRIANGLELIST?count/3:count-2;
    for(unsigned t=0;t<triangles;++t){const unsigned first=topology==D3DPT_TRIANGLELIST?t*3:t;auto a=index(first),b=index(first+1),c=index(first+2);if(a==b||b==c||a==c)continue;if(topology==D3DPT_TRIANGLESTRIP&&(t&1))std::swap(a,b);for(auto i:{a,b,c})expected.push_back(position(i));}
    assert(mesh.indices.size()==expected.size()&&"DECODED_XYZ_MISMATCH");for(size_t i=0;i<expected.size();++i){assert(mesh.indices[i]<mesh.positions.size());const auto& actual=mesh.positions[mesh.indices[i]];assert(!std::memcmp(&actual,&expected[i],sizeof actual)&&"DECODED_XYZ_MISMATCH");}
    Bounds reference;assert(referenceTriangleSequence<true>([&](size_t i){return expected[i];},expected.size(),D3DPT_TRIANGLELIST,reference));
    assert(!std::memcmp(reference.low,mesh.bounds.low,sizeof reference.low)&&!std::memcmp(reference.high,mesh.bounds.high,sizeof reference.high)&&"DECODED_COVERAGE_MISMATCH");assert(reference.chunks.size()==mesh.bounds.chunks.size());
    for(size_t i=0;i<reference.chunks.size();++i){const auto& a=reference.chunks[i];const auto& b=mesh.bounds.chunks[i];assert(a.x==b.x&&a.y==b.y&&a.minX==b.minX&&a.maxX==b.maxX&&a.minY==b.minY&&a.maxY==b.maxY);}
}
static bool read(FrameCache& cache,Device& d,unsigned start,unsigned count,std::shared_ptr<const MeshSnapshot>& mesh,D3DPRIMITIVETYPE topology=D3DPT_TRIANGLELIST){cache.clearFrame();return cache.readMeshShared(&d,topology,0,0,256,start,topology==D3DPT_TRIANGLELIST?count/3:count-2,d.view,mesh);}
[[maybe_unused]] static std::shared_ptr<const MeshSnapshot> capture(FrameCache& cache,Device& d,unsigned start,unsigned count=192,D3DPRIMITIVETYPE topology=D3DPT_TRIANGLELIST){std::shared_ptr<const MeshSnapshot> mesh;assert(read(cache,d,start,count,mesh,topology)&&"DECODED_XYZ_MISMATCH");exact(*mesh,d,start,count,topology);assert(cache.persistentBytes()<=cache.persistentByteLimit());assert(d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1&&d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);return mesh;}
static void tracked(FrameCache& cache){cache.setIdentityProvider(identity);cache.setVersionProvider(version);}
static void indexPlans(){
    for(bool wide:{false,true})for(auto topology:{D3DPT_TRIANGLELIST,D3DPT_TRIANGLESTRIP}){
        Device d;auto p=pattern();p[4]=p[3];setup(d,{p,p,p,p});
        if(wide){d.ib.desc.Format=D3DFMT_INDEX32;d.ib.bytes.resize(p.size()*4*4);for(size_t i=0;i<p.size()*4;++i){const uint32_t n=p[i%p.size()];std::memcpy(d.ib.bytes.data()+4*i,&n,4);}}
        auto cache=std::make_unique<FrameCache>();tracked(*cache);auto a=capture(*cache,d,0,192,topology),b=capture(*cache,d,192,192,topology);
        d.put(1,{-10198,-1180,777});++revisions[0];auto c=capture(*cache,d,384,192,topology);
        assert(cache->maintenance().indexPlanHits==1&&cache->maintenance().indexPlanIndices==192&&cache->maintenance().indexPlanVertices==96);
        assert(c->positions[1].z==777&&a->positions[1].z==1&&c!=a&&c!=b&&"INDEX_PLAN_REUSED_OLD_XYZ");
        const auto entries=cache->persistentEntries();const auto vertex=revisions[0];directUnlockAction=1;
        auto raced=capture(*cache,d,576,192,topology);directUnlockAction=0;revisions[0]=vertex;
        assert(cache->maintenance().indexPlanHits==1&&cache->maintenance().directUnstable==1&&!raced->sourceIdentity&&cache->persistentEntries()==entries&&"DIRECT_UNSTABLE_CACHE_PUBLISHED");
        a.reset();b.reset();c.reset();raced.reset();cache->clearPersistent();assert(!cache->sourceLiveBytes());
    }
    for(unsigned change=0;change<4;++change){
        Device d;setup(d,{pattern(),pattern(),pattern()});auto cache=std::make_unique<FrameCache>();tracked(*cache);
        capture(*cache,d,0);capture(*cache,d,192);
        auto topology=D3DPT_TRIANGLELIST;
        if(change==0){uint16_t n=8;std::memcpy(d.ib.bytes.data()+2*(384+100),&n,2);++revisions[1];}
        if(change==1)topology=D3DPT_TRIANGLESTRIP;
        if(change==2){d.stride=32;setup(d,{pattern(),pattern(),pattern()});++revisions[0];++revisions[1];}
        const auto known=revisions[0];if(change==3)revisions[0]=0;
        capture(*cache,d,384,192,topology);assert(!cache->maintenance().indexPlanHits);revisions[0]=known;
    }
    {Device d;std::vector<uint16_t> p={1,0,2,0,3,0};setup(d,{p,p,p});auto cache=std::make_unique<FrameCache>();tracked(*cache);
        capture(*cache,d,0,6);capture(*cache,d,6,6);d.ib.desc.Format=D3DFMT_INDEX32;++revisions[1];
        capture(*cache,d,6,3);assert(!cache->maintenance().indexPlanHits);
    }
    // A plan proves the old index sequence, never a new caller's admitted range.
    for(unsigned invalid=0;invalid<3;++invalid){Device d;setup(d,{pattern(),pattern(),pattern()});auto cache=std::make_unique<FrameCache>();tracked(*cache);capture(*cache,d,0);capture(*cache,d,192);
        cache->clearFrame();std::shared_ptr<const MeshSnapshot> mesh;Diagnostics why;
        assert(!cache->readMeshShared(&d,D3DPT_TRIANGLELIST,invalid==2?-1:0,invalid==0?1:0,invalid==1?95:256,384,64,d.view,mesh,&why));
        assert(why.reason==(invalid==2?RejectReason::VertexRange:RejectReason::IndexRange)&&!mesh);
    }
    for(unsigned failure=0;failure<4;++failure){Device d;setup(d,{pattern(),pattern(),pattern()});auto cache=std::make_unique<FrameCache>();tracked(*cache);capture(*cache,d,0);capture(*cache,d,192);const auto entries=cache->persistentEntries();
        d.vb.failLock=failure==0;directNullLock=failure==1;directUnlockFailure=failure==2;if(failure==3){d.put(1,{NAN,-1180,0});++revisions[0];}
        cache->clearFrame();std::shared_ptr<const MeshSnapshot> mesh;Diagnostics why;assert(!cache->readMeshShared(&d,D3DPT_TRIANGLELIST,0,0,256,384,64,d.view,mesh,&why));
        assert(!mesh&&cache->persistentEntries()==entries&&!vertexReadLocked&&d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);directNullLock=directUnlockFailure=false;
    }
    for(long failure=0;failure<30;++failure){const auto before=allocations.load();{Device d;setup(d,{pattern(),pattern(),pattern()});auto cache=std::make_unique<FrameCache>();tracked(*cache);capture(*cache,d,0);capture(*cache,d,192);
        std::shared_ptr<const MeshSnapshot> mesh;failAfter=failure;bool ok=false;try{ok=read(*cache,d,384,192,mesh);}catch(const std::bad_alloc&){}failAfter=-1;
        if(ok)exact(*mesh,d,384,192);assert(cache->persistentBytes()<=cache->persistentByteLimit()&&!vertexReadLocked&&d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);
        mesh.reset();cache->clearPersistent();assert(!cache->sourceLiveBytes());}assert(allocations.load()==before);
    }
    std::puts("PASS exact cold index plans: fresh XYZ, both widths/list/strip/holes, byte/format/topology/stride/unknown fallback, current ranges, racing revisions, lock/finite failures and bounded immutable ownership");
}
static void directColdCapture(){
    for(bool wide:{false,true})for(auto topology:{D3DPT_TRIANGLELIST,D3DPT_TRIANGLESTRIP}){
        Device d;auto p=pattern();for(size_t i=0;i<p.size();++i)p[i]=uint16_t(i%3==0?7:i%3==1?129:251);p[4]=p[3];setup(d,{p});
        if(wide){d.ib.desc.Format=D3DFMT_INDEX32;d.ib.bytes.resize(p.size()*4);for(size_t i=0;i<p.size();++i){const uint32_t n=p[i];std::memcpy(d.ib.bytes.data()+4*i,&n,4);}}
        auto cache=std::make_unique<FrameCache>();tracked(*cache);const uint64_t span=uint64_t(251-7)*d.stride+sizeof(Position);
        auto first=capture(*cache,d,0,192,topology);const auto m=cache->maintenance();
        assert(first->sourceIdentity&&cache->persistentEntries()==1&&m.directColdMisses==1&&!m.rawVertexCopyBytes&&m.xyzCopyBytes==3*sizeof(Position)&&m.vertexSpanBytes==span);
        assert(cache->bytesRead()==span+p.size()*(wide?4:2));
        auto again=capture(*cache,d,0,192,topology);assert(again==first&&cache->maintenance().trackedHits==1&&!cache->bytesRead()&&!cache->maintenance().vertexSpanBytes);
        ++revisions[0];again=capture(*cache,d,0,192,topology);assert(again==first&&cache->maintenance().exactReuses==1&&!cache->maintenance().directColdMisses&&!cache->maintenance().rawVertexCopyBytes);
        d.put(129,{-10200,-1180,999});++revisions[0];auto changed=capture(*cache,d,0,192,topology);
        assert(changed!=first&&first->positions[1].z==129&&changed->positions[1].z==999&&cache->maintenance().rawVertexCopyBytes==span&&!cache->maintenance().directColdMisses);
        cache->clearPersistent();const auto known=revisions[0];revisions[0]=0;capture(*cache,d,0,192,topology);
        assert(!cache->maintenance().directColdMisses&&cache->maintenance().rawVertexCopyBytes==span);revisions[0]=known;
        first.reset();again.reset();changed.reset();cache->clearPersistent();assert(!cache->sourceLiveBytes()&&!vertexReadLocked);
    }
    for(unsigned action=1;action<=5;++action){Device d;setup(d,{pattern()});auto cache=std::make_unique<FrameCache>();tracked(*cache);
        const auto vertex=revisions[0],index=revisions[1],lifetime=lifetimes[0];directUnlockAction=action;
        auto mesh=capture(*cache,d,0);directUnlockAction=0;
        assert(!mesh->sourceIdentity&&!cache->persistentEntries()&&"DIRECT_UNSTABLE_CACHE_PUBLISHED");assert(cache->maintenance().directColdMisses==1&&cache->maintenance().directUnstable==1&&!cache->maintenance().rawVertexCopyBytes);
        revisions[0]=vertex;revisions[1]=index;lifetimes[0]=lifetime;mesh.reset();assert(!cache->sourceLiveBytes()&&!vertexReadLocked);
    }
    for(unsigned failure=0;failure<4;++failure){Device d;setup(d,{pattern()});auto cache=std::make_unique<FrameCache>();tracked(*cache);
        d.vb.failLock=failure==0;directNullLock=failure==1;directUnlockFailure=failure==2;if(failure==3)d.put(0,{NAN,-1180,0});
        cache->clearFrame();std::shared_ptr<const MeshSnapshot> mesh;Diagnostics detail;
        assert(!cache->readMeshShared(&d,D3DPT_TRIANGLELIST,0,0,256,0,64,d.view,mesh,&detail));
        const auto expected=failure<2?RejectReason::VertexLock:failure==2?RejectReason::VertexUnlock:RejectReason::VertexNonfinite;
        assert(detail.reason==expected&&!mesh&&!cache->persistentEntries()&&!vertexReadLocked&&d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);
        directNullLock=directUnlockFailure=false;assert(d.vb.refs==1&&d.ib.refs==1);
    }
    std::puts("PASS direct tracked cold capture: sparse/duplicate/degenerate indices, both widths, list/strip, requested/copy bytes, warm/unknown/revalidation fallback, racing generations, failures and no allocation under vertex lock");
}
static void collision(){Device d;auto a=pattern(),b=a;b[100]=8;setup(d,{a,a,b,b});auto cache=std::make_unique<FrameCache>();tracked(*cache);auto first=capture(*cache,d,0),promoted=capture(*cache,d,192),other=capture(*cache,d,384),otherPromoted=capture(*cache,d,576);assert(first->indices.ownedBytes()&&other->indices.ownedBytes()&&!promoted->indices.ownedBytes()&&!otherPromoted->indices.ownedBytes());assert(promoted->indices.storageIdentity()!=other->indices.storageIdentity()&&cache->indexStorage().sharedBlocks==2);}
static void sharing(){
    Device d;const auto a=pattern();auto b=a;for(auto& i:b)i+=128;setup(d,{a,b,a});auto cache=std::make_unique<FrameCache>();tracked(*cache);const auto empty=cache->persistentBytes();
    auto first=capture(*cache,d,0);assert(first->indices.ownedBytes()==384&&cache->indexStorage().sharedBlocks==0);
    auto second=capture(*cache,d,192);assert(!second->indices.ownedBytes()&&cache->indexStorage().sharedBlocks==1);
    auto third=capture(*cache,d,384);const auto stats=cache->indexStorage();assert(first!=second&&first->positions.data()!=second->positions.data()&&first->indices.storageIdentity()!=second->indices.storageIdentity()&&second->indices.storageIdentity()==third->indices.storageIdentity());assert(!NorthlightSourceIdentity::same(first->sourceIdentity,second->sourceIdentity));assert(stats.sharedBlocks==1&&stats.bytes==1152&&stats.physicalBytes==768&&stats.sourceBlocks==3);
    assert(cache->persistentBytes()==cache->sourceLiveBytes()+cache->sourceMetadataBytes()+first->sourceIdentity->bytes()+second->sourceIdentity->bytes()+third->sourceIdentity->bytes());assert(cache->sourceLiveObjects()==3);
    auto value=*second;assert(value.indices.storageIdentity()!=second->indices.storageIdentity());value.indices.set(0,65536);assert(value.indices[0]==65536&&second->indices[0]==0);auto reserved=*third;reserved.indices.reserve(300,65536);reserved.indices.push_back(65537);assert(reserved.indices[192]==65537&&third->indices.size()==192);
    auto again=capture(*cache,d,0);assert(again==first&&cache->maintenance().trackedHits==1&&!cache->bytesRead());again.reset();
    d.put(0,{-10200,-1180,700});++revisions[0];auto changed=capture(*cache,d,0);assert(changed!=first&&first->positions[0].z==0&&changed->positions[0].z==700);assert(changed->indices.storageIdentity()==second->indices.storageIdentity());
    cache->setPersistentByteLimit(16ull<<20);assert(cache->persistentByteLimit()==16ull<<20);cache->setPersistentByteLimit(32ull<<20);assert(cache->persistentByteLimit()==32ull<<20);
    cache->clearPersistent();assert(cache->persistentBytes()==empty&&!cache->indexStorage().weakOwners&&cache->sourceLiveBytes()>0);first.reset();second.reset();third.reset();changed.reset();assert(!cache->sourceLiveBytes()&&!cache->sourceLiveObjects());
    auto stripped=pattern();stripped.resize(12);stripped[4]=stripped[3];setup(d,{stripped});++revisions[0];++revisions[1];capture(*cache,d,0,unsigned(stripped.size()),D3DPT_TRIANGLESTRIP);
}
static void escaped(){const auto before=NorthlightSourceIdentity::cpuLiveBytes.load();std::shared_ptr<const MeshSnapshot> held;{Device d;setup(d,{pattern(),pattern()});auto cache=std::make_unique<FrameCache>();tracked(*cache);capture(*cache,d,0);held=capture(*cache,d,192);assert(!held->indices.ownedBytes());}assert(held->indices.size()==192&&held->indices[191]==95&&NorthlightSourceIdentity::cpuLiveBytes.load()>before);held.reset();assert(NorthlightSourceIdentity::cpuLiveBytes.load()==before);}
static void expired(){
    Device d;d.stride=1000;std::vector<std::vector<uint16_t>> patterns;for(unsigned i=0;i<64;++i){auto p=pattern();p.back()=uint16_t(i);patterns.push_back(std::move(p));}setup(d,patterns);auto cache=std::make_unique<FrameCache>();const auto empty=cache->persistentBytes();for(unsigned i=0;i<64;++i)capture(*cache,d,i*192);assert(cache->indexStorage().weakOwners<=64);
    cache->setPersistentByteLimit(1);d.stride=24;setup(d,std::vector<std::vector<uint16_t>>(700,pattern()));for(unsigned i=0;i<700;++i)capture(*cache,d,i*192);const auto stats=cache->indexStorage();assert(stats.sharedBlocks==1&&stats.expiredWeakOwners>0&&stats.weakOwners<=64&&cache->sourceMetadataBytes()>=empty);
    cache->clearPersistent();assert(cache->persistentBytes()==empty&&!cache->sourceLiveBytes()&&!cache->indexStorage().weakOwners&&!cache->indexStorage().sharedBlocks);capture(*cache,d,0);assert(cache->indexStorage().sharedBlocks==0);capture(*cache,d,192);assert(cache->indexStorage().sharedBlocks==1);cache->clearPersistent();
    d.stride=24;setup(d,{pattern(),pattern()});capture(*cache,d,0);cache->setPersistentByteLimit(1);d.stride=30000;setup(d,{pattern(),pattern()});auto rejected=capture(*cache,d,192);assert(!rejected->indices.ownedBytes()&&cache->persistentEntries()==1&&cache->indexStorage().sharedBlocks==0&&cache->indexStorage().weakOwners==1);cache->clearPersistent();assert(cache->sourceLiveBytes()>0);rejected.reset();assert(!cache->sourceLiveBytes());
}
static FrameCache* failingCache=nullptr;static size_t globalBefore=0,snapshotsBefore=0;static bool retainFailed=false;
static bool failRetain(){if(failingCache&&failingCache->sourceLiveObjects()==snapshotsBefore&&NorthlightSourceIdentity::cpuLiveObjects.load()>globalBefore){retainFailed=true;return true;}return false;}
static void failures(){
    unsigned optionalOwned=0,propagated=0;for(long n=0;n<40;++n){const auto before=allocations.load();{Device d;setup(d,{pattern(),pattern()});auto cache=std::make_unique<FrameCache>();tracked(*cache);capture(*cache,d,0);std::shared_ptr<const MeshSnapshot> mesh;failAfter=n;bool ok=false;try{ok=read(*cache,d,192,192,mesh);}catch(const std::bad_alloc&){++propagated;}failAfter=-1;if(ok){exact(*mesh,d,192,192);optionalOwned+=mesh->indices.ownedBytes()!=0;}assert(cache->persistentBytes()<=cache->persistentByteLimit()&&d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);mesh.reset();cache->clearPersistent();assert(!cache->sourceLiveBytes());}assert(allocations.load()==before);}
    unsigned unpublishedOffsets=0;for(long n=0;n<40;++n){const auto before=allocations.load();{Device d;setup(d,{pattern()});auto cache=std::make_unique<FrameCache>();tracked(*cache);std::shared_ptr<const MeshSnapshot> mesh;failAfter=n;bool ok=false;try{ok=read(*cache,d,0,192,mesh);}catch(const std::bad_alloc&){}failAfter=-1;if(ok){exact(*mesh,d,0,192);if(!cache->persistentEntries()){++unpublishedOffsets;assert(!cache->indexStorage().offsetPhysicalBytes);}}assert(cache->persistentBytes()<=cache->persistentByteLimit()&&d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);mesh.reset();cache->clearPersistent();assert(!cache->sourceLiveBytes()&&!cache->indexStorage().offsetPhysicalBytes);}assert(allocations.load()==before);}assert(unpublishedOffsets);
    assert(optionalOwned&&propagated);{Device d;setup(d,{pattern(),pattern()});auto cache=std::make_unique<FrameCache>();tracked(*cache);capture(*cache,d,0);failingCache=cache.get();globalBefore=NorthlightSourceIdentity::cpuLiveObjects.load();snapshotsBefore=cache->sourceLiveObjects();failPredicate=failRetain;std::shared_ptr<const MeshSnapshot> mesh;bool threw=false;try{read(*cache,d,192,192,mesh);}catch(const std::bad_alloc&){threw=true;}failPredicate=nullptr;failingCache=nullptr;assert(threw&&retainFailed&&!mesh&&cache->sourceLiveObjects()==1&&cache->indexStorage().weakOwners==1&&!cache->indexStorage().sharedBlocks);cache->clearPersistent();assert(!cache->sourceLiveBytes());}
    std::printf("FAILURE_CONTROLS optionalOwned=%u propagated=%u unpublishedOffsets=%u retainAfterSharing=1 noLeakedCharge=1\n",optionalOwned,propagated,unpublishedOffsets);
}

struct OffsetDevice:Device{
    UINT streamOffset=16;
    HRESULT GetStreamSource(UINT stream,IDirect3DVertexBuffer9** out,UINT* offset,UINT* vertexStride)override{
        const auto result=Device::GetStreamSource(stream,out,offset,vertexStride);*offset=streamOffset;return result;
    }
};
static std::shared_ptr<const MeshSnapshot> offsetCapture(FrameCache& cache,OffsetDevice& d,unsigned start,
        D3DPRIMITIVETYPE topology=D3DPT_TRIANGLELIST,INT base=0){
    cache.clearFrame();std::shared_ptr<const MeshSnapshot> mesh;
    assert(cache.readMeshShared(&d,topology,base,0,256,start,topology==D3DPT_TRIANGLELIST?64:190,d.view,mesh));
    exact(*mesh,d,start,192,topology,base,d.streamOffset);assert(cache.persistentBytes()<=cache.persistentByteLimit());return mesh;
}
static void offsetSharing(){
    OffsetDevice d;setup(d,{pattern(),pattern(),pattern()});auto cache=std::make_unique<FrameCache>();tracked(*cache);const auto empty=cache->persistentBytes();
    auto first=offsetCapture(*cache,d,0),second=offsetCapture(*cache,d,192),third=offsetCapture(*cache,d,384);
    auto stats=cache->indexStorage();assert(stats.sourceBlocks==1&&stats.offsetBytes==576&&stats.offsetPhysicalBytes==192&&stats.offsetSharedReferences==3);
    assert(first!=second&&first->positions.data()!=second->positions.data()&&!NorthlightSourceIdentity::same(first->sourceIdentity,second->sourceIdentity));
    auto based=offsetCapture(*cache,d,0,D3DPT_TRIANGLELIST,128);assert(based->positions[0].z==128&&first->positions[0].z==0&&cache->indexStorage().sourceBlocks==1);
    assert(cache->persistentBytes()==cache->sourceLiveBytes()+cache->sourceMetadataBytes()+first->sourceIdentity->bytes()+second->sourceIdentity->bytes()+third->sourceIdentity->bytes()+based->sourceIdentity->bytes());
    // Identical raw indices do not imply the same byte offsets under a new stride.
    d.stride=32;setup(d,{pattern(),pattern(),pattern()});++revisions[0];++revisions[1];
    auto strided=offsetCapture(*cache,d,0);++revisions[0];++revisions[1];auto revalidated=offsetCapture(*cache,d,0);
    assert(revalidated==strided&&"OFFSET_LAYOUT_REVALIDATION_MISMATCH");stats=cache->indexStorage();assert(stats.sourceBlocks==1&&stats.offsetPhysicalBytes==384&&stats.offsetSharedReferences==4);
    d.streamOffset=24;auto moved=d.vb.bytes;d.vb.bytes.resize(moved.size()+8);std::memmove(d.vb.bytes.data()+24,moved.data()+16,moved.size()-16);++revisions[0];
    auto shifted=offsetCapture(*cache,d,192);assert(shifted!=second&&first->positions[0].z==0);assert(cache->indexStorage().offsetPhysicalBytes==576);
    cache->setPersistentByteLimit(16ull<<20);cache->setPersistentByteLimit(32ull<<20);cache->clearPersistent();
    assert(cache->persistentBytes()==empty&&!cache->indexStorage().offsetPhysicalBytes&&!cache->indexStorage().sourceBlocks);
    first.reset();second.reset();third.reset();based.reset();strided.reset();revalidated.reset();shifted.reset();assert(!cache->sourceLiveBytes());
    // Topology/holes change emitted triangles, while the complete source remap
    // remains independently checked and each snapshot retains its own geometry.
    d.stride=24;d.streamOffset=16;auto p=pattern();p[1]=p[0];setup(d,{p,p});++revisions[0];++revisions[1];
    auto listed=offsetCapture(*cache,d,0),stripped=offsetCapture(*cache,d,192,D3DPT_TRIANGLESTRIP);
    stats=cache->indexStorage();assert(stats.sourceBlocks==1&&stats.offsetPhysicalBytes==192&&stats.offsetSharedReferences==2);
    ++revisions[0];++revisions[1];auto unchanged=offsetCapture(*cache,d,192,D3DPT_TRIANGLESTRIP);
    assert(unchanged==stripped&&"OFFSET_LAYOUT_REVALIDATION_MISMATCH");
    d.put(2,{-10200,-1180,777});++revisions[0];auto changed=offsetCapture(*cache,d,192,D3DPT_TRIANGLESTRIP);
    assert(changed!=stripped&&stripped->positions[0].z!=777);cache->clearPersistent();
    assert(!cache->indexStorage().offsetPhysicalBytes&&!cache->indexStorage().offsetSharedReferences);
    listed.reset();stripped.reset();unchanged.reset();changed.reset();assert(!cache->sourceLiveBytes());
    // An unproven owner has no compact layout: later tracked entries stay owned.
    setup(d,{pattern(),pattern()});++revisions[0];++revisions[1];const auto knownRevision=revisions[0];revisions[0]=0;
    offsetCapture(*cache,d,0);assert(cache->persistentEntries()==1&&cache->indexStorage().sourceBlocks==1&&!cache->indexStorage().offsetPhysicalBytes);
    revisions[0]=knownRevision;offsetCapture(*cache,d,192);
    stats=cache->indexStorage();assert(stats.sourceBlocks==1&&stats.offsetPhysicalBytes==192&&!stats.offsetSharedReferences);
    cache->clearPersistent();assert(cache->persistentBytes()==empty&&!cache->indexStorage().offsetPhysicalBytes);
    assert(d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1&&d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);
    std::puts("PASS exact source-offset layouts: raw owner, base/stride/stream/topology, unchanged revalidation, mutation, unknown-revision fallback, caps and reset");
}
static void coverageAllocations(){
    const Position triangle[]={{-10200,-1180,1},{-10198,-1180,2},{-10200,-1178,3}};
    const auto at=[&](size_t i){return triangle[i];};Bounds current,reference;
    auto before=allocationCalls.load();assert(classifyTriangleSequence<true>(at,3,D3DPT_TRIANGLELIST,current));const auto inlineCalls=allocationCalls.load()-before;
    before=allocationCalls.load();assert(referenceTriangleSequence<true>(at,3,D3DPT_TRIANGLELIST,reference));const auto referenceCalls=allocationCalls.load()-before;
    assert(!std::memcmp(current.low,reference.low,sizeof current.low)&&!std::memcmp(current.high,reference.high,sizeof current.high));assert(current.chunks.size()==1&&reference.chunks.size()==1);
    assert(inlineCalls<referenceCalls&&"SMALL_COVERAGE_ALLOCATION_REGRESSION");
    std::printf("COVERAGE_ALLOCATION_CONTROL current=%u frozen=%u exactBounds=1\n",inlineCalls,referenceCalls);
}
static size_t entriesBeforeEviction=0;static bool lastOwnerEvicted=false;
static bool observeOffsetEviction(){
    if(failingCache&&failingCache->persistentEntries()<entriesBeforeEviction&&failingCache->indexStorage().sourceBlocks<entriesBeforeEviction)lastOwnerEvicted=true;
    return false;
}
static void offsetPressure(){
    constexpr unsigned count=49152,draws=10;std::vector<std::vector<uint16_t>> patterns;
    for(unsigned draw=0;draw<draws;++draw){std::vector<uint16_t> p(count);for(unsigned i=0;i<count;++i)p[i]=uint16_t(i%96);p[100]=uint16_t(10+draw);patterns.push_back(std::move(p));}patterns.push_back(patterns.back());
    Device d;setup(d,patterns);auto cache=std::make_unique<FrameCache>();tracked(*cache);cache->setPersistentByteLimit(1);const auto empty=cache->persistentBytes();
    std::shared_ptr<const MeshSnapshot> last;for(unsigned draw=0;draw<draws;++draw)last=capture(*cache,d,draw*count,count);
    entriesBeforeEviction=cache->persistentEntries();assert(entriesBeforeEviction>1&&entriesBeforeEviction<draws&&cache->indexStorage().sourceBlocks==entriesBeforeEviction);
    // Touch the remaining contiguous LRU suffix, making the raw memo's sole
    // owner oldest without replacing that memo on a tracked hit.
    for(unsigned draw=draws-unsigned(entriesBeforeEviction);draw<draws-1;++draw){capture(*cache,d,draw*count,count);assert(cache->maintenance().trackedHits==1);}
    failingCache=cache.get();lastOwnerEvicted=false;failPredicate=observeOffsetEviction;
    auto replacement=capture(*cache,d,draws*count,count);failPredicate=nullptr;failingCache=nullptr;
    assert(lastOwnerEvicted&&cache->maintenance().evictions&&"OFFSET_LAST_OWNER_EVICTION_NOT_EXERCISED");
    auto stats=cache->indexStorage();assert(stats.sourceBlocks==cache->persistentEntries()&&stats.offsetSharedReferences==cache->persistentEntries()&&stats.offsetPhysicalBytes==cache->persistentEntries()*192);
    assert(last->indices.size()==count&&replacement->indices.size()==count&&cache->persistentBytes()<=cache->persistentByteLimit());
    cache->clearPersistent();assert(cache->persistentBytes()==empty&&!cache->indexStorage().offsetPhysicalBytes&&cache->sourceLiveBytes()>0);last.reset();replacement.reset();assert(!cache->sourceLiveBytes());
    std::puts("PASS source-offset last-owner eviction with retained temporary, full recharge under pressure, escaped snapshots and reset");
}

static void unique(){
    constexpr unsigned Draws=650;std::vector<std::vector<uint16_t>> patterns;for(unsigned i=0;i<Draws;++i){auto p=pattern();p[100]=uint16_t(10+i%86);p[105]=uint16_t(2+i/86);patterns.push_back(std::move(p));}Device d;setup(d,patterns);auto cache=std::make_unique<FrameCache>();tracked(*cache);double total=0;
    for(unsigned i=0;i<Draws;++i){cache->clearFrame();std::shared_ptr<const MeshSnapshot> mesh;const auto start=std::chrono::steady_clock::now();assert(cache->readMeshShared(&d,D3DPT_TRIANGLELIST,0,0,256,i*192,64,d.view,mesh));total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();exact(*mesh,d,i*192,192);}
    const auto stats=cache->indexStorage();assert(stats.sharedBlocks==0&&stats.physicalBytes==stats.bytes&&stats.weakOwners<=64);
    assert(stats.sourceBlocks==Draws&&stats.offsetSharedReferences==Draws&&stats.offsetPhysicalBytes==stats.offsetBytes);
    assert(cache->persistentEntries()==Draws&&cache->persistentBytes()<=cache->persistentByteLimit());std::printf("UNIQUE_CONTROL draws=%u captureMs=%.6f exactGeometry=1 capBounded=1\n",Draws,total);
}
static void sharedWide(){
    constexpr unsigned count=65538;const auto before=NorthlightSourceIdentity::cpuLiveBytes.load();std::shared_ptr<const MeshSnapshot> held;
    {Device d;d.vb.bytes.resize(16+size_t(count)*24);for(unsigned i=0;i<count;++i)d.put(i,{-10200.f+float(i%3==1)*2,-1180.f+float(i%3==2)*2,float(i)});
    d.ib.desc.Format=D3DFMT_INDEX32;d.ib.bytes.resize(size_t(count)*4*3);for(unsigned draw=0;draw<3;++draw)for(unsigned i=0;i<count;++i){const uint32_t value=i;std::memcpy(d.ib.bytes.data()+4*(size_t(draw)*count+i),&value,4);}
    Limits limits;{FrameCache defaultCache;std::shared_ptr<const MeshSnapshot> rejected;Diagnostics detail;assert(!defaultCache.readMeshShared(&d,D3DPT_TRIANGLELIST,0,0,count,0,count/3,d.view,rejected,&detail)&&detail.reason==RejectReason::PrimitiveLimit);}
    limits.maxTrianglesPerDraw=count/3;auto cache=std::make_unique<FrameCache>(limits);tracked(*cache);std::shared_ptr<const MeshSnapshot> draws[3];
    for(unsigned draw=0;draw<3;++draw){cache->clearFrame();assert(cache->readMeshShared(&d,D3DPT_TRIANGLELIST,0,0,count,count*draw,count/3,d.view,draws[draw]));const auto& mesh=*draws[draw];assert(mesh.indices.size()==count&&mesh.positions.size()==count);assert(mesh.indices.visit([](const auto* p,size_t){return sizeof(*p)==4;}));
        for(unsigned i=0;i<count;++i){Position expected;std::memcpy(&expected,d.vb.bytes.data()+20+size_t(i)*24,12);assert(mesh.indices[i]==i&&!std::memcmp(&mesh.positions[mesh.indices[i]],&expected,sizeof expected)&&"SHARED_WIDE_XYZ_MISMATCH");}
        Bounds reference;assert(referenceTriangleSequence<true>([&](size_t i){return mesh.positions[i];},count,D3DPT_TRIANGLELIST,reference));assert(!std::memcmp(reference.low,mesh.bounds.low,sizeof reference.low)&&!std::memcmp(reference.high,mesh.bounds.high,sizeof reference.high));assert(reference.chunks.size()==mesh.bounds.chunks.size());for(size_t i=0;i<reference.chunks.size();++i){const auto& a=reference.chunks[i];const auto& b=mesh.bounds.chunks[i];assert(a.x==b.x&&a.y==b.y&&a.minX==b.minX&&a.maxX==b.maxX&&a.minY==b.minY&&a.maxY==b.maxY);}
        assert(cache->persistentBytes()<=cache->persistentByteLimit());}
    const auto stats=cache->indexStorage();assert(stats.sharedBlocks==1&&stats.wide==3&&stats.bytes==size_t(count)*4*3&&stats.physicalBytes==size_t(count)*4*2);
    assert(stats.sourceBlocks==1&&stats.offsetWide==3&&stats.offsetSharedReferences==3&&stats.offsetPhysicalBytes==size_t(count)*4&&stats.offsetBytes==size_t(count)*4*3);
    assert(draws[0]->indices.ownedBytes()&&!draws[1]->indices.ownedBytes()&&draws[1]->indices.storageIdentity()==draws[2]->indices.storageIdentity());auto copied=*draws[1];copied.indices.set(0,70000);copied.indices.push_back(70001);assert(copied.indices[0]==70000&&copied.indices[count]==70001&&draws[1]->indices[0]==0&&draws[1]->indices.size()==count);held=draws[2];}
    assert(held->indices[count-1]==count-1&&NorthlightSourceIdentity::cpuLiveBytes.load()>before);held.reset();assert(NorthlightSourceIdentity::cpuLiveBytes.load()==before);assert(!allocations.load());std::puts("PASS shared 32-bit 65538 unique remap values/XYZ/coverage, first-owned second-promoted third-shared, mutable deep copy, cap and escaped lifetime");
}

int main(int argc,char** argv){if(argc==2){assert(std::string(argv[1])=="--unique");unique();return 0;}
    indexPlans();directColdCapture();collision();sharing();escaped();expired();failures();offsetSharing();coverageAllocations();offsetPressure();sharedWide();
    unique();assert(!allocations.load());std::puts("PASS decoded-index exact bytes, own provenance/XYZ, deep mutable values, holes/strip, retained blocks, expired weak overhead, allocation/retain failures and unchanged caps");
}
