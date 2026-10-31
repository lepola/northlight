#include "snapshot_harness.h"
#include <map>
namespace NorthlightDrawSnapshot {struct SnapshotTestAccess {static bool predicted(const Frame& f){return f.predicted_!=nullptr;}};}
static std::map<void*,uint64_t> tokens,revisions;
static void* racing=nullptr;static unsigned raceReads=0;
static uint64_t identity(void* p,bool){return tokens[p];}
static uint64_t version(void* p,bool){if(p==racing&&++raceReads==2)return 0;return revisions[p];}
static unsigned locks(const Device& d){return d.ib.locks+d.vb[0].locks+d.vb[1].locks;}
static void track(Frame& f){f.setIdentityProvider(identity);f.setVersionProvider(version);}
static void equal(const Mesh& a,const Mesh& b){assert(a.indices==b.indices&&a.vertexCount==b.vertexCount&&a.primitiveCount==b.primitiveCount&&a.topology==b.topology);for(unsigned i=0;i<4;++i)assert(a.streams[i].stride==b.streams[i].stride&&a.streams[i].bytes==b.streams[i].bytes);}
int main(){
    const size_t beforeCpu=NorthlightSourceIdentity::cpuLiveBytes.load(),beforeKeys=NorthlightSourceIdentity::identityLiveBytes.load();
    {
        Device d;for(void* p:{static_cast<void*>(&d.vb[0]),static_cast<void*>(&d.vb[1]),static_cast<void*>(&d.ib)}){tokens[p]=tokens.size()+1;revisions[p]=1;}
        Frame capture(1024,0),geometry(1024,0),world(1024,0);track(capture);track(geometry);track(world);capture.setChargeResidentHits(false);geometry.setChargeResidentHits(false);
        capture.setSnapshotCacheLimit(4096);geometry.setSnapshotCacheLimit(4096);world.setSnapshotCacheLimit(4096);
        assert(geometry.shareSnapshotPool(capture)&&world.shareSnapshotPool(geometry));assert(capture.sharedSourceParticipants()==3&&world.sharedSourceLimit()==12288);
        Draw draw{D3DPT_TRIANGLELIST,-3,7,3,1,1,true};Mesh scratch;std::shared_ptr<const Mesh> a,b,c;Diagnostics why;
        assert(capture.read(&d,&d.decl,draw,scratch,&why,false,&a)&&a&&a->sourceIdentity);const auto firstLocks=locks(d),charge=capture.bytesRead();
        assert(geometry.read(&d,&d.decl,draw,scratch,&why,false,&b)&&b==a&&locks(d)==firstLocks&&geometry.bytesRead()==0&&geometry.sharedSourceHits()==1);
        assert(world.read(&d,&d.decl,draw,scratch,&why,false,&c)&&c==a&&locks(d)==firstLocks&&world.bytesRead()==charge); // requesting consumer charges its original policy
        assert(capture.snapshotCacheBytes()+geometry.snapshotCacheBytes()+world.snapshotCacheBytes()==capture.sharedSourceBytes());
        assert(capture.uniqueSourceBytes()+geometry.uniqueSourceBytes()+world.uniqueSourceBytes()==capture.sourceLiveBytes()+capture.sharedSourceMetadataBytes());
        Declaration equivalent;assert(geometry.read(&d,&equivalent,draw,scratch,&why,false,&b)&&b==a&&locks(d)==firstLocks); // exact semantic layout, different COM/request pointer
        Declaration changed;changed.e[0].Usage=5;assert(geometry.read(&d,&changed,draw,scratch,&why,false,&b)&&b!=a&&!NorthlightSourceIdentity::same(a->sourceIdentity,b->sourceIdentity));
        auto immutable=a->streams[0].bytes;auto oldIdentity=a->sourceIdentity;const auto oldPointer=a.get();assert(capture.read(&d,&d.decl,draw,scratch,&why,false,&c)&&SnapshotTestAccess::predicted(capture));geometry.clearSnapshotCache();assert(capture.sourceLiveObjects()>=2&&capture.sourceLiveBytes()>0&&!capture.sharedSourceBytes());
        assert(capture.read(&d,&d.decl,draw,scratch,&why,false,&b)&&b.get()!=oldPointer&&NorthlightSourceIdentity::same(oldIdentity,b->sourceIdentity));equal(*a,*b);
        {Frame transient;track(transient);transient.setSnapshotCacheLimit(512);assert(transient.shareSnapshotPool(world)&&capture.sharedSourceParticipants()==4&&capture.sharedSourceLimit()==12800);}
        assert(capture.sharedSourceParticipants()==3&&capture.sharedSourceLimit()==12288&&!capture.sharedSourceBytes()&&a->streams[0].bytes==immutable); // member destruction invalidates other predictions and transfers accounting safely
        d.vb[0].bytes[8+4*24+4]^=42;++revisions[&d.vb[0]];assert(capture.read(&d,&d.decl,draw,scratch,&why,false,&b)&&!NorthlightSourceIdentity::same(oldIdentity,b->sourceIdentity)&&a->streams[0].bytes==immutable);
        ++tokens[&d.vb[0]];assert(capture.read(&d,&d.decl,draw,scratch,&why,false,&c)&&!NorthlightSourceIdentity::same(c->sourceIdentity,b->sourceIdentity)); // lifetime/ABA differs even at the same address
        capture.clearSnapshotCache();racing=&d.vb[0];raceReads=0;assert(capture.read(&d,&d.decl,draw,scratch,&why,false,&b)&&!b->sourceIdentity);racing=nullptr; // unstable post-read revision cannot authorize residency
        capture.clearSnapshotCache();d.vb[0].failLock=true;assert(!capture.read(&d,&d.decl,draw,scratch,&why,false,&b));d.vb[0].failLock=false;
        capture.clearFrame();geometry.clearFrame();world.clearFrame();assert(!capture.bytesRead()&&!geometry.bytesRead()&&!world.bytesRead());
        capture.setSnapshotCacheLimit(0);geometry.setSnapshotCacheLimit(1024);world.setSnapshotCacheLimit(2048);assert(capture.sharedSourceLimit()==3072);capture.setSnapshotCacheLimit(4096);geometry.setSnapshotCacheLimit(4096);world.setSnapshotCacheLimit(4096);assert(world.sharedSourceLimit()==12288);
        // Unknown revisions remain per-consumer and use original static revalidation.
        revisions[&d.vb[1]]=0;for(auto& vb:d.vb)vb.desc.Usage=0;d.ib.desc.Usage=0;capture.clearSnapshotCache();
        assert(capture.read(&d,&d.decl,draw,scratch,&why,false,&b)&&b&&!b->sourceIdentity);assert(geometry.read(&d,&d.decl,draw,scratch,&why,false,&c)&&c&&c!=b&&!c->sourceIdentity);equal(*b,*c);
        capture.clearSnapshotCache();a.reset();b.reset();c.reset();oldIdentity.reset();assert(!capture.sourceLiveObjects()&&!capture.sourceLiveBytes());
        std::puts("SHARED_SOURCE_LIFECYCLE_PASS three_consumers_one_preparation semantic_layout_alias private_unknown charged_world_hits partial_write lifetime_ABA unstable_revision failure pressure_reset inflight_lastref unique_bytes");
    }
    assert(NorthlightSourceIdentity::cpuLiveBytes.load()==beforeCpu&&NorthlightSourceIdentity::identityLiveBytes.load()==beforeKeys);
}
