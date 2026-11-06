#include "snapshot_harness.h"
#include "legacy_draw_snapshot.h"
#include <map>
#include <chrono>
namespace Old=LegacyDrawSnapshot;
static std::map<void*,uint64_t> tokens;
static uint64_t identity(void* p,bool){return tokens[p];}
static uint64_t version(void*,bool){return 1;}
template<class A,class B>static void equal(const A& a,const B& b){assert(a.indices==b.indices&&a.vertexCount==b.vertexCount&&a.primitiveCount==b.primitiveCount&&a.topology==b.topology&&a.dynamic==b.dynamic);for(unsigned i=0;i<4;++i)assert(a.streams[i].stride==b.streams[i].stride&&a.streams[i].bytes==b.streams[i].bytes);}
template<class F>static void setup(F& f,size_t cap,bool charge){f.setIdentityProvider(identity);f.setVersionProvider(version);f.setChargeResidentHits(charge);f.setSnapshotCacheLimit(cap);}
struct Devices {std::vector<std::unique_ptr<Device>> values;Devices(){for(unsigned n=0;n<64;++n){auto d=std::make_unique<Device>();d->decl.e={{0,0,2,0,0,0},{0xff,0,17,0,0,0}};d->offset[0]=0;d->stride[0]=16;d->vb[0].bytes.resize(24576*16);for(unsigned i=0;i<d->vb[0].bytes.size();++i)d->vb[0].bytes[i]=uint8_t(i+n);d->ib.bytes.resize(24576*2);for(unsigned i=0;i<24576;++i){const uint16_t index=uint16_t(i);std::memcpy(d->ib.bytes.data()+i*2,&index,2);}tokens[&d->vb[0]]=2*n+1;tokens[&d->ib]=2*n+2;values.push_back(std::move(d));}}
    uint64_t locks()const{uint64_t result=0;for(const auto& d:values)result+=d->vb[0].locks+d->ib.locks;return result;}
};
int main(){
    Devices devices;Old::Frame ow(32u<<20,0),og(2u<<20,0),oc(2u<<20,0);Frame nw(32u<<20,0),ng(2u<<20,0),nc(2u<<20,0);
    setup(ow,64u<<20,true);setup(og,24u<<20,false);setup(oc,24u<<20,false);og.setResidentPeer(&oc);oc.setResidentPeer(&og);
    setup(nw,64u<<20,true);setup(ng,24u<<20,false);setup(nc,24u<<20,false);assert(ng.shareSnapshotPool(nc)&&nw.shareSnapshotPool(ng)&&nw.sharedSourceLimit()==112u<<20);
    Old::Draw oldDraw{D3DPT_TRIANGLELIST,0,0,24576,0,8192,true};Draw newDraw{D3DPT_TRIANGLELIST,0,0,24576,0,8192,true};
    uint64_t oldLocks[2]={},newLocks[2]={},comparisons=0,oldOwners[2]={},newOwners[2]={};double oldMs[2]={},newMs[2]={};
    for(unsigned phase=0;phase<2;++phase)for(unsigned frame=0;frame<16;++frame){ow.clearFrame();og.clearFrame();oc.clearFrame();nw.clearFrame();ng.clearFrame();nc.clearFrame();
        for(unsigned slot=0;slot<4;++slot){auto& d=*devices.values[frame*4+slot];Old::Mesh oldScratch;Mesh newScratch;std::shared_ptr<const Old::Mesh> oldOwnersAt[3];std::shared_ptr<const Mesh> newOwnersAt[3];
            auto before=devices.locks();auto start=std::chrono::steady_clock::now();assert(ow.read(&d,&d.decl,oldDraw,oldScratch,nullptr,false,&oldOwnersAt[0])&&og.read(&d,&d.decl,oldDraw,oldScratch,nullptr,false,&oldOwnersAt[1])&&oc.read(&d,&d.decl,oldDraw,oldScratch,nullptr,false,&oldOwnersAt[2]));oldMs[phase]+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();oldLocks[phase]+=devices.locks()-before;
            before=devices.locks();start=std::chrono::steady_clock::now();assert(nw.read(&d,&d.decl,newDraw,newScratch,nullptr,false,&newOwnersAt[0])&&ng.read(&d,&d.decl,newDraw,newScratch,nullptr,false,&newOwnersAt[1])&&nc.read(&d,&d.decl,newDraw,newScratch,nullptr,false,&newOwnersAt[2]));newMs[phase]+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();newLocks[phase]+=devices.locks()-before;
            for(unsigned consumer=0;consumer<3;++consumer){equal(*oldOwnersAt[consumer],*newOwnersAt[consumer]);++comparisons;}assert(newOwnersAt[0]==newOwnersAt[1]&&newOwnersAt[1]==newOwnersAt[2]);
        }
        oldOwners[phase]+=ow.snapshotCacheMisses()+og.snapshotCacheMisses()+oc.snapshotCacheMisses();newOwners[phase]+=nw.snapshotCacheMisses()+ng.snapshotCacheMisses()+nc.snapshotCacheMisses();
        // The requesting world consumer retains exactly its original charged admission.
        assert(ow.bytesRead()==nw.bytesRead());
    }
    assert(comparisons==384&&oldLocks[0]==256&&newLocks[0]==128&&oldLocks[1]==128&&newLocks[1]==0&&oldOwners[0]==128&&newOwners[0]==64&&oldOwners[1]==64&&!newOwners[1]);
    const uint64_t bytesPerSource=24576*2+24575*16+12;
    for(unsigned phase=0;phase<2;++phase)std::printf("SHARED_SOURCE_FROZEN_DIFF phase=%s sources=64 consumers=3 exactFullMeshComparisons=192 oldLocks=%llu newLocks=%llu oldNewOwners=%llu newNewOwners=%llu oldLockedBytes=%llu newLockedBytes=%llu oldCpuMs=%.6f newCpuMs=%.6f\n",phase?"orbit-return":"cold",(unsigned long long)oldLocks[phase],(unsigned long long)newLocks[phase],(unsigned long long)oldOwners[phase],(unsigned long long)newOwners[phase],(unsigned long long)(oldLocks[phase]/2*bytesPerSource),(unsigned long long)(newLocks[phase]/2*bytesPerSource),oldMs[phase],newMs[phase]);
    assert(nw.snapshotCacheBytes()+ng.snapshotCacheBytes()+nc.snapshotCacheBytes()==nw.sharedSourceBytes());
    std::printf("SHARED_SOURCE_FROZEN_PASS baseline=ac966ce aggregateCapMiB=112 oldRetainedBytes=%zu newUniqueRetainedBytes=%zu sourceLiveObjects=%zu perConsumerBudgetExact=1\n",ow.snapshotCacheBytes()+og.snapshotCacheBytes()+oc.snapshotCacheBytes(),nw.sharedSourceBytes(),nw.sourceLiveObjects());
}
