#include "replay_content_harness.h"
#include "replay_gpu_cache.h"
#include <map>
using namespace NorthlightDrawSnapshot;

static size_t gpuAlive=0;
template<class T,class Desc>struct GPUBuffer final:T {
    unsigned refs=1;std::vector<unsigned char> bytes;Desc desc{};
    explicit GPUBuffer(UINT size):bytes(size){desc.Size=size;gpuAlive+=size;}
    ~GPUBuffer(){gpuAlive-=bytes.size();}
    unsigned AddRef()override{return ++refs;}
    unsigned Release()override{const auto n=--refs;if(!n)delete this;return n;}
    HRESULT GetDesc(Desc* out)override{*out=desc;return D3D_OK;}
    HRESULT Lock(UINT offset,UINT count,void** out,DWORD)override{assert(uint64_t(offset)+count<=bytes.size());*out=bytes.data()+offset;return D3D_OK;}
    HRESULT Unlock()override{return D3D_OK;}
};
struct CaptureDevice:Device {
    unsigned gpuCreates=0;
    HRESULT CreateVertexBuffer(UINT size,DWORD,UINT,unsigned,IDirect3DVertexBuffer9** out,void*)override{++gpuCreates;*out=new GPUBuffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC>(size);return D3D_OK;}
    HRESULT CreateIndexBuffer(UINT size,DWORD,D3DFORMAT,unsigned,IDirect3DIndexBuffer9** out,void*)override{++gpuCreates;*out=new GPUBuffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC>(size);return D3D_OK;}
};
static std::map<void*,uint64_t> revisions,identities;
static void* unstableResource=nullptr;static unsigned unstableVisits=0;
static uint64_t identity(void* pointer,bool){const auto found=identities.find(pointer);return found==identities.end()?uint64_t(reinterpret_cast<uintptr_t>(pointer)):found->second;}
static uint64_t version(void* pointer,bool){if(pointer==unstableResource&&++unstableVisits==2)++revisions[pointer];return revisions[pointer];}
[[maybe_unused]] static const NorthlightSourceIdentity::Handle& replay(const Mesh& mesh){
#ifdef NORTHLIGHT_TEST_REPLAY_CONTENT_CURRENT
    return mesh.replayIdentity?mesh.replayIdentity:mesh.sourceIdentity;
#else
    return mesh.sourceIdentity;
#endif
}
static void configure(Frame& frame,bool enabled=true){frame.setIdentityProvider(identity);frame.setVersionProvider(version);frame.setChargeResidentHits(false);
#ifdef NORTHLIGHT_TEST_REPLAY_CONTENT_CURRENT
    frame.setReplayContentReuse(enabled);
#else
    (void)enabled;
#endif
}
[[maybe_unused]] static void equalPayload(const Mesh& a,const Mesh& b){
    assert(a.vertexCount==b.vertexCount&&a.primitiveCount==b.primitiveCount&&a.topology==b.topology&&a.indexed==b.indexed&&a.dynamic==b.dynamic&&a.indices==b.indices);
    for(unsigned s=0;s<4;++s)assert(a.streams[s].stride==b.streams[s].stride&&a.streams[s].bytes==b.streams[s].bytes);
}
static void gpuPayload(const Mesh& mesh,IDirect3DVertexBuffer9* (&vb)[4],IDirect3DIndexBuffer9* ib){
    for(unsigned s=0;s<4;++s)if(!mesh.streams[s].bytes.empty()){auto* buffer=static_cast<GPUBuffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC>*>(vb[s]);assert(buffer&&buffer->bytes==mesh.streams[s].bytes);}
    if(mesh.indexed){auto* buffer=static_cast<GPUBuffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC>*>(ib);assert(buffer&&buffer->bytes.size()==mesh.indices.size()*4&&!std::memcmp(buffer->bytes.data(),mesh.indices.data(),buffer->bytes.size()));}
}
static void releaseBindings(IDirect3DVertexBuffer9* (&vb)[4],IDirect3DIndexBuffer9*& ib){for(auto& p:vb){if(p)p->Release();p=nullptr;}if(ib)ib->Release();ib=nullptr;}

#ifdef NORTHLIGHT_TEST_REPLAY_CONTENT_CURRENT
namespace NorthlightDrawSnapshot {
struct SnapshotTestAccess {
    static void forceCandidate(Frame& frame,const std::shared_ptr<const Mesh>& previous){assert(frame.sources_->replayCandidates);for(auto& slot:*frame.sources_->replayCandidates)slot=previous;}
    static void prove(Frame& frame,Mesh& mesh){const auto* entry=frame.sources_->lru.oldest();assert(entry);while(entry->lru.newer)entry=entry->lru.newer;frame.replayContent(entry->key,mesh);}
    static size_t candidateBytes(){return Frame::ReplayCandidateBytes;}
    static bool hasCandidates(const Frame& frame){return bool(frame.sources_->replayCandidates);}
    static bool candidatePointsTo(const Frame& frame,const std::shared_ptr<const Mesh>& mesh){if(!frame.sources_->replayCandidates)return false;for(const auto& slot:*frame.sources_->replayCandidates)if(slot.lock()==mesh)return true;return false;}
};
}
static void proofs(){
    CaptureDevice d;d.indices({7,9,8});for(void* p:{static_cast<void*>(&d.vb[0]),static_cast<void*>(&d.vb[1]),static_cast<void*>(&d.ib)})revisions[p]=1;
    Frame frame(2u<<20,0);configure(frame);Mesh scratch;Draw draw{D3DPT_TRIANGLELIST,0,0,12,0,1,true};std::shared_ptr<const Mesh> first,next;
    assert(frame.read(&d,&d.decl,draw,scratch,nullptr,false,&first)&&first&&first->sourceIdentity&&!first->replayIdentity);const auto charge=frame.bytesRead();
    for(auto& item:revisions)++item.second;frame.clearFrame();assert(frame.read(&d,&d.decl,draw,scratch,nullptr,false,&next));
    equalPayload(*first,*next);assert(first!=next&&!NorthlightSourceIdentity::same(first->sourceIdentity,next->sourceIdentity)&&NorthlightSourceIdentity::same(replay(*first),replay(*next))&&frame.bytesRead()==charge);
    assert(frame.replayContentHits()==1&&frame.sharedSourceMetadataBytes()>=SnapshotTestAccess::candidateBytes());
    // The real GPU cache promotes an unchanged rewrite after probation. A
    // rejected upload leaves probation intact and owns no GPU allocation.
    NorthlightReplayGPU::Cache gpu;NorthlightReplayGPU::createBudgetMs()=1e9;
    IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;
    gpu.beginFrame();assert(!gpu.bind(&d,first,vb,ib,[](size_t){return true;}));
    gpu.beginFrame();assert(!gpu.bind(&d,next,vb,ib,[](size_t){return false;})&&!gpuAlive&&gpu.stats().admissionRejected==1);
    assert(gpu.bind(&d,next,vb,ib,[](size_t){return true;}));gpuPayload(*next,vb,ib);releaseBindings(vb,ib);
    // A forced shortlist collision cannot bypass ANY byte or metadata proof.
    SnapshotTestAccess::forceCandidate(frame,first);
    for(unsigned mutation=0;mutation<13;++mutation){Mesh changed=*next;changed.replayIdentity={};
        switch(mutation){case 0:++changed.vertexCount;break;case 1:++changed.primitiveCount;break;case 2:changed.topology=D3DPT_TRIANGLESTRIP;break;case 3:changed.indexed=!changed.indexed;break;case 4:changed.dynamic=!changed.dynamic;break;case 5:++changed.streams[0].stride;break;case 6:changed.streams[0].bytes[0]^=1;break;case 7:changed.streams[0].bytes.back()^=1;break;case 8:changed.streams[1].bytes[0]^=1;break;case 9:changed.indices[0]^=1;break;case 10:changed.sourceIdentity={};break;case 11:changed.sourceIdentity=NorthlightSourceIdentity::make({1,2,3});break;case 12:{auto words=changed.sourceIdentity->words;words.back()^=1;changed.sourceIdentity=NorthlightSourceIdentity::make(std::move(words));break;}}
        SnapshotTestAccess::prove(frame,changed);assert(!changed.replayIdentity);
    }
    // A real current VB write, IB rewrite and resource identity change must not
    // inherit an old GPU content token even when the weak slot is forced to it.
    d.vb[0].bytes[d.offset[0]+7*d.stride[0]+4]^=1;++revisions[&d.vb[0]];frame.clearFrame();SnapshotTestAccess::forceCandidate(frame,first);assert(frame.read(&d,&d.decl,draw,scratch,nullptr,false,&next)&&!next->replayIdentity);
    gpu.beginFrame();assert(!gpu.bind(&d,next,vb,ib,[](size_t){return true;}));gpu.beginFrame();assert(gpu.bind(&d,next,vb,ib,[](size_t){return true;}));gpuPayload(*next,vb,ib);releaseBindings(vb,ib);
    d.indices({8,9,7});++revisions[&d.ib];frame.clearFrame();SnapshotTestAccess::forceCandidate(frame,first);assert(frame.read(&d,&d.decl,draw,scratch,nullptr,false,&next)&&!next->replayIdentity);
    identities[&d.vb[0]]=55;++revisions[&d.vb[0]];frame.clearFrame();SnapshotTestAccess::forceCandidate(frame,first);assert(frame.read(&d,&d.decl,draw,scratch,nullptr,false,&next)&&!next->replayIdentity);
    gpu.beginFrame();assert(!gpu.bind(&d,next,vb,ib,[](size_t){return true;}));identities.clear();
    // A revision changing between metadata admission and completed reads has
    // no stable source proof, so it cannot acquire any prior replay token.
    unstableResource=&d.vb[0];unstableVisits=0;++revisions[unstableResource];frame.clearFrame();SnapshotTestAccess::forceCandidate(frame,first);
    assert(frame.read(&d,&d.decl,draw,scratch,nullptr,false,&next)&&next&&!next->sourceIdentity&&!next->replayIdentity);unstableResource=nullptr;
    gpu.clear();assert(!gpuAlive);
    std::weak_ptr<const Mesh> weak=first;first.reset();next.reset();frame.clearSnapshotCache();assert(weak.expired()&&!frame.sharedSourceBytes());
    frame.setSnapshotCacheLimit(16);frame.clearFrame();assert(frame.read(&d,&d.decl,draw,scratch,nullptr,false,&next)&&!next&&frame.sharedSourceBytes()<=16);
    frame.setSnapshotCacheLimit(24u<<20);frame.clearFrame();assert(frame.read(&d,&d.decl,draw,scratch,nullptr,false,&next)&&next&&!next->replayIdentity);
    frame.setSnapshotCacheLimit(0);assert(!frame.sharedSourceBytes()&&!frame.sharedSourceMetadataBytes());
    // The table can fit alone while a capture entry fits only without it.
    // Optional cache refusal must retain the current output and byte cap.
    Frame tight,uncached;configure(tight);configure(uncached,false);tight.setSnapshotCacheLimit(SnapshotTestAccess::candidateBytes()+16);uncached.setSnapshotCacheLimit(0);Mesh current,control;
    assert(tight.read(&d,&d.decl,draw,current,nullptr,false,&next)&&!next);assert(uncached.read(&d,&d.decl,draw,control,nullptr,false,&first)&&!first);equalPayload(current,control);
    assert(tight.sharedSourceBytes()<=tight.sharedSourceLimit()&&tight.sharedSourceBytes()==SnapshotTestAccess::candidateBytes());tight.clearSnapshotCache();assert(!tight.sharedSourceBytes());
    Frame disabled;disabled.setIdentityProvider(identity);disabled.setVersionProvider(version);disabled.setChargeResidentHits(false);
    assert(disabled.read(&d,&d.decl,draw,scratch,nullptr,false,&first));++revisions[&d.vb[0]];disabled.clearFrame();assert(disabled.read(&d,&d.decl,draw,scratch,nullptr,false,&next)&&!next->replayIdentity&&!disabled.replayContentTests());
    first.reset();next.reset();disabled.clearSnapshotCache();
    // Sharing a material pool does not opt world-only/OFF/far readers in.
    // A current shared hit from such a peer has no byte-equality proof.
    Frame material,farPeer;configure(material);configure(farPeer,false);assert(farPeer.shareSnapshotPool(material));
    assert(farPeer.read(&d,&d.decl,draw,scratch,nullptr,false,&first)&&first&&!SnapshotTestAccess::hasCandidates(farPeer));
    assert(material.read(&d,&d.decl,draw,scratch,nullptr,false,&next)&&next==first&&!next->replayIdentity&&!material.replayContentTests());
    ++revisions[&d.vb[0]];material.clearFrame();assert(material.read(&d,&d.decl,draw,scratch,nullptr,false,&next)&&next&&SnapshotTestAccess::candidatePointsTo(material,next));
    auto eligible=next;++revisions[&d.vb[0]];farPeer.clearFrame();assert(farPeer.read(&d,&d.decl,draw,scratch,nullptr,false,&first)&&first&&!first->replayIdentity&&!farPeer.replayContentTests()&&!farPeer.replayContentHits());
    assert(SnapshotTestAccess::candidatePointsTo(material,eligible)&&!SnapshotTestAccess::candidatePointsTo(material,first));
    material.clearFrame();assert(material.read(&d,&d.decl,draw,scratch,nullptr,false,&next)&&next==first&&!next->replayIdentity&&!material.replayContentTests());
    eligible.reset();first.reset();next.reset();material.clearSnapshotCache();assert(!SnapshotTestAccess::hasCandidates(farPeer));
    // OFF/world capture can fill the pool before the first eligible material.
    // Its first miss must reserve metadata rather than stay disabled forever.
    CaptureDevice populated;constexpr unsigned PrewarmDraws=2000;
    for(unsigned s=0;s<2;++s)populated.vb[s].bytes.resize(populated.offset[s]+size_t(PrewarmDraws)*3*populated.stride[s]);populated.indices({0,1,2});
    for(void* p:{static_cast<void*>(&populated.vb[0]),static_cast<void*>(&populated.vb[1]),static_cast<void*>(&populated.ib)})revisions[p]=1;
    Frame firstMaterial,worldOnly,reference;configure(firstMaterial);configure(worldOnly,false);configure(reference,false);
    firstMaterial.setSnapshotCacheLimit(1u<<20);worldOnly.setSnapshotCacheLimit(1u<<20);reference.setSnapshotCacheLimit(0);assert(worldOnly.shareSnapshotPool(firstMaterial));
    unsigned filled=0;while(worldOnly.sharedSourceBytes()<=worldOnly.sharedSourceLimit()-SnapshotTestAccess::candidateBytes()){
        assert(filled<PrewarmDraws-1);Draw item{D3DPT_TRIANGLELIST,INT(3*filled++),0,3,0,1,true};assert(worldOnly.read(&populated,&populated.decl,item,scratch,nullptr,false,&first)&&first);
    }
    assert(!SnapshotTestAccess::hasCandidates(worldOnly)&&!worldOnly.replayContentTests());first.reset();
    Draw admitted{D3DPT_TRIANGLELIST,INT(3*(PrewarmDraws-1)),0,3,0,1,true};Mesh expected;
    assert(reference.read(&populated,&populated.decl,admitted,expected,nullptr,false,&first)&&!first);
    firstMaterial.sampleMaintenance(true);assert(firstMaterial.read(&populated,&populated.decl,admitted,scratch,nullptr,false,&next)&&next&&SnapshotTestAccess::hasCandidates(firstMaterial));
    equalPayload(*next,expected);assert(firstMaterial.bytesRead()==reference.bytesRead()&&firstMaterial.sharedSourceBytes()<=firstMaterial.sharedSourceLimit()&&firstMaterial.maintenance().snapshotEvictions);
    auto original=next;++revisions[&populated.vb[0]];firstMaterial.clearFrame();assert(firstMaterial.read(&populated,&populated.decl,admitted,scratch,nullptr,false,&next)&&next&&next!=original);
    equalPayload(*next,*original);assert(NorthlightSourceIdentity::same(replay(*next),replay(*original))&&!NorthlightSourceIdentity::same(next->sourceIdentity,original->sourceIdentity)&&firstMaterial.bytesRead()==reference.bytesRead()&&firstMaterial.sharedSourceBytes()<=firstMaterial.sharedSourceLimit());
    original.reset();next.reset();firstMaterial.clearSnapshotCache();assert(!firstMaterial.sharedSourceBytes());
    std::puts("REPLAY_CONTENT_PROOFS current-provenance=distinct all-fields=checked forced-collision=checked VB/IB-write=checked stale-revision=checked weak-expiry=checked limits=charged admission=checked GPU-bindings=exact default=off reset=checked");
}
#endif

int main(int argc,char** argv){
#ifdef NORTHLIGHT_TEST_REPLAY_CONTENT_CURRENT
    proofs();
#endif
    constexpr unsigned Draws=220,Vertices=192,Frames=48;
    const bool changing=argc>1&&std::strcmp(argv[1],"changing")==0;
    CaptureDevice d;identities.clear();revisions.clear();identities[&d.vb[0]]=101;identities[&d.vb[1]]=102;identities[&d.ib]=103;
    for(unsigned s=0;s<2;++s){d.vb[s].bytes.resize(d.offset[s]+size_t(Draws)*Vertices*d.stride[s]);for(size_t i=0;i<d.vb[s].bytes.size();++i)d.vb[s].bytes[i]=uint8_t((i*13+s*17)%251);revisions[&d.vb[s]]=1;}
    d.ib.desc.Format=D3DFMT_INDEX16;d.ib.desc.Usage=D3DUSAGE_DYNAMIC;d.ib.bytes.resize(Vertices*2);for(unsigned i=0;i<Vertices;++i){const uint16_t value=uint16_t(i);std::memcpy(d.ib.bytes.data()+i*2,&value,2);}revisions[&d.ib]=1;
    Frame source(2u<<20,0),capture(2u<<20,0),world(2u<<20,0);configure(source);configure(capture);configure(world,false);source.setSnapshotCacheLimit(24u<<20);capture.setSnapshotCacheLimit(24u<<20);world.setSnapshotCacheLimit(64u<<20);assert(capture.shareSnapshotPool(source)&&world.shareSnapshotPool(source));
    NorthlightReplayGPU::Cache gpu;gpu.setLimit(32u<<20);NorthlightReplayGPU::createBudgetMs()=1e9;
    IDirect3DVertexBuffer9* vb[4]={};IDirect3DIndexBuffer9* ib=nullptr;Mesh scratch;uint64_t accepted=0,reads=0,resident=0,up=0,upBytes=0;
    const auto started=std::chrono::steady_clock::now();
    for(unsigned frame=0;frame<Frames;++frame){for(auto& item:revisions)++item.second;if(changing)for(unsigned i=0;i<Draws;++i)d.vb[0].bytes[d.offset[0]+(size_t(i+1)*Vertices-1)*d.stride[0]+15]^=1;source.clearFrame();capture.clearFrame();world.clearFrame();gpu.beginFrame();
        for(unsigned i=0;i<Draws;++i){Draw draw{D3DPT_TRIANGLELIST,INT(i*Vertices),0,Vertices,0,Vertices/3,true};std::shared_ptr<const Mesh> a,b;
            const bool ok=source.read(&d,&d.decl,draw,scratch,nullptr,false,&a);assert(ok&&a);accepted+=ok;
            assert(capture.read(&d,&d.decl,draw,scratch,nullptr,false,&b)&&a==b);++accepted;
            if(gpu.bind(&d,a,vb,ib,[](size_t){return true;})){++resident;gpuPayload(*a,vb,ib);}else{++up;upBytes+=a->byteSize();}
            releaseBindings(vb,ib);
        }
        reads+=source.bytesRead()+capture.bytesRead();assert(source.bytesRead()<=(2u<<20)&&capture.bytesRead()<=(2u<<20)&&source.sharedSourceBytes()<=(112u<<20)&&gpu.chargedBytes()<=(32u<<20)&&gpu.uploaded()<=(4u<<20)&&gpu.stats().created<=16&&gpu.population().entries<=4096);
    }
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
#ifdef NORTHLIGHT_TEST_REPLAY_CONTENT_CURRENT
    if(!changing)assert(resident>0&&up<uint64_t(Draws)*Frames/2);else assert(!resident&&up==uint64_t(Draws)*Frames);
#else
    assert(!resident&&up==uint64_t(Draws)*Frames);
#endif
    std::printf("REPLAY_CONTENT_WORKLOAD changing=%u draws=%u frames=%u accepted=%llu readBytes=%llu resident=%llu up=%llu upBytes=%llu gpuCreates=%u elapsedMs=%.6f sourceCap=117440512 gpuCap=33554432 readCap=2097152\n",unsigned(changing),Draws,Frames,(unsigned long long)accepted,(unsigned long long)reads,(unsigned long long)resident,(unsigned long long)up,(unsigned long long)upBytes,d.gpuCreates,elapsed);
    gpu.clear();assert(!gpuAlive);source.clearSnapshotCache();assert(!source.sourceLiveObjects()&&!source.sharedSourceBytes());std::puts("REPLAY_CONTENT_PASS");
}
