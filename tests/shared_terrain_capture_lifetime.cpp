#include "terrain_capture_harness.h"
#include "legacy_terrain_capture_bounds.h"
#include <map>
static std::map<void*,uint64_t> tokens,revisions;
static uint64_t identity(void* p,bool){return tokens[p];}
static uint64_t version(void* p,bool){return revisions[p];}
template<class A,class B>static void equal(const A& a,const B& b){assert(a.positions.size()==b.positions.size()&&a.indices==b.indices&&a.bounds.chunks.size()==b.bounds.chunks.size());assert(!std::memcmp(a.positions.data(),b.positions.data(),a.positions.size()*sizeof(Position)));assert(!std::memcmp(a.bounds.low,b.bounds.low,sizeof a.bounds.low)&&!std::memcmp(a.bounds.high,b.bounds.high,sizeof a.bounds.high));for(size_t i=0;i<a.bounds.chunks.size();++i)assert(!std::memcmp(&a.bounds.chunks[i],&b.bounds.chunks[i],sizeof(ChunkBounds)));}
int main(){
    const auto liveBefore=NorthlightSourceIdentity::cpuLiveBytes.load();
    {
        Device d;d.put(4,{-10200,-1180,3});d.put(5,{-10198,-1180,4});d.put(6,{-10200,-1178,5});tokens[&d.vb]=10;tokens[&d.ib]=20;revisions[&d.vb]=revisions[&d.ib]=1;
        FrameCache current;LegacyTerrainCapture::FrameCache old;current.setIdentityProvider(identity);current.setVersionProvider(version);old.setIdentityProvider(identity);old.setVersionProvider(version);
        std::shared_ptr<const MeshSnapshot> a,out;std::shared_ptr<const LegacyTerrainCapture::MeshSnapshot> oldOut;Diagnostics why;LegacyTerrainCapture::Diagnostics oldWhy;
        auto read=[&]{const bool before=old.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,oldOut,&oldWhy),after=current.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,out,&why);assert(before==after&&unsigned(why.reason)==unsigned(oldWhy.reason));if(after)equal(*out,*oldOut);return after;};
        assert(read()&&out->sourceIdentity&&current.sourceLiveObjects()==1);a=out;const auto original=out->sourceIdentity;const auto charged=current.bytesRead();const auto locks=d.vb.locks+d.ib.locks;current.clearFrame();old.clearFrame();assert(read()&&out==a&&why.trackedCacheHit&&current.bytesRead()==0&&current.avoidedReadBytes()==charged&&d.vb.locks+d.ib.locks==locks);
        // A vertex revision touching only unused attributes keeps geometry only
        // after exact current XYZ + index verification, never by revision guess.
        d.vb.bytes[16+4*24+16]^=3;++revisions[&d.vb];current.clearFrame();old.clearFrame();assert(read()&&out==a&&why.contentCacheHit&&!why.trackedCacheHit);
        d.put(4,{-10200,-1180,30});++revisions[&d.vb];current.clearFrame();old.clearFrame();assert(read()&&out!=a&&!NorthlightSourceIdentity::same(original,out->sourceIdentity)&&a->positions[0].z==3);auto changed=out;
        ++tokens[&d.vb];revisions[&d.vb]=1;current.clearFrame();old.clearFrame();assert(read()&&out==changed&&why.contentCacheHit&&!why.trackedCacheHit); // same semantic bytes can retain the old owner only after full live-byte proof
        d.put(4,{-10200,-1180,31});++tokens[&d.vb];current.clearFrame();old.clearFrame();assert(read()&&!NorthlightSourceIdentity::same(changed->sourceIdentity,out->sourceIdentity));
        d.put(4,{-10200,-1180,40});revisions[&d.vb]=0;current.clearFrame();old.clearFrame();assert(read()&&!out->sourceIdentity&&out->positions[0].z==40);auto unknown=out;
        d.put(4,{-10200,-1180,41});d.vb.failLock=true;current.clearFrame();old.clearFrame();assert(!read()&&!out&&a->positions[0].z==3);d.vb.failLock=false;
        current.clearPersistent();assert(current.sourceLiveObjects()==3&&current.sourceLiveBytes()>0);a.reset();changed.reset();unknown.reset();assert(!current.sourceLiveObjects()&&!current.sourceLiveBytes());
        std::puts("SHARED_TERRAIN_CAPTURE_PASS frozen_ac966ce_full_positions_indices_chunks tracked_hit_same_charge semantic_UV_write_verified XYZ_revision lifetime_ABA unknown_revision_weak failure immutable_old_output reset_inflight_ledger_lastref");
    }
    assert(NorthlightSourceIdentity::cpuLiveBytes.load()==liveBefore);
}
