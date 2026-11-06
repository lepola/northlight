
#include "draw_snapshot.h"
#include <cassert>
#include <cstdio>
using namespace NorthlightDrawSnapshot;
template<class Interface,class Desc>struct Buffer:Interface{
 unsigned refs=1,locks=0,unlocks=0;bool failLock=false,failUnlock=false;Desc desc;std::vector<std::uint8_t> bytes;
 unsigned AddRef()override{return ++refs;}unsigned Release()override{return --refs;}
 HRESULT GetDesc(Desc*out)override{*out=desc;out->Size=UINT(bytes.size());return 0;}
 HRESULT Lock(UINT offset,UINT size,void**out,DWORD flags)override{assert(flags==D3DLOCK_READONLY);if(failLock)return D3DERR_INVALIDCALL;assert(std::uint64_t(offset)+size<=bytes.size());++locks;*out=bytes.data()+offset;return 0;}
 HRESULT Unlock()override{++unlocks;return failUnlock?D3DERR_INVALIDCALL:0;}
};
struct Declaration:IDirect3DVertexDeclaration9{
 unsigned refs=1;std::vector<D3DVERTEXELEMENT9> e={{0,4,2,0,0,0},{1,0,4,0,1,0},{0xff,0,17,0,0,0}};
 unsigned AddRef()override{return ++refs;}unsigned Release()override{return --refs;}
 HRESULT GetDeclaration(D3DVERTEXELEMENT9*out,UINT*n)override{assert(*n>=e.size());*n=UINT(e.size());std::memcpy(out,e.data(),e.size()*sizeof(*out));return 0;}
};
struct Device:IDirect3DDevice9{
 Buffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC> vb[4];Buffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC> ib;Declaration decl;UINT offset[4]={8,4,0,0},stride[4]={24,8,0,0},freq[4]={1,1,1,1};
 Device(){for(unsigned s=0;s<2;++s){vb[s].desc.Usage=D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY;vb[s].bytes.resize(offset[s]+12*stride[s]);for(unsigned i=0;i<vb[s].bytes.size();++i)vb[s].bytes[i]=std::uint8_t(i);}indices({999,7,9,8});}
 void indices(std::initializer_list<std::uint32_t> in,bool wide=false){ib.desc.Format=wide?D3DFMT_INDEX32:D3DFMT_INDEX16;ib.desc.Usage=D3DUSAGE_DYNAMIC;ib.bytes.resize(in.size()*(wide?4:2));unsigned i=0;for(auto v:in){if(wide)std::memcpy(ib.bytes.data()+i*4,&v,4);else{std::uint16_t n=std::uint16_t(v);std::memcpy(ib.bytes.data()+i*2,&n,2);}++i;}}
 HRESULT GetVertexShaderConstantF(UINT,float*,UINT)override{return D3DERR_INVALIDCALL;}
 HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9**out)override{*out=&decl;decl.AddRef();return 0;}
 HRESULT GetStreamSourceFreq(UINT s,UINT*out)override{*out=freq[s];return 0;}
 HRESULT GetStreamSource(UINT s,IDirect3DVertexBuffer9**out,UINT*o,UINT*st)override{*out=&vb[s];vb[s].AddRef();*o=offset[s];*st=stride[s];return 0;}
 HRESULT GetIndices(IDirect3DIndexBuffer9**out)override{*out=&ib;ib.AddRef();return 0;}
};

#include <map>
static std::map<void*,uint64_t> revisions;
static uint64_t identity(void* p,bool){return uint64_t(reinterpret_cast<uintptr_t>(p));}
static uint64_t version(void* p,bool){return revisions[p];}
static unsigned locks(Device& d){return d.vb[0].locks+d.vb[1].locks+d.ib.locks;}
int main(){
 Device d;d.indices({7,9,8});revisions[&d.vb[0]]=1;revisions[&d.vb[1]]=2;revisions[&d.ib]=3;
 Draw draw{D3DPT_TRIANGLELIST,0,0,12,0,1,true};Mesh scratch;Diagnostics why;std::shared_ptr<const Mesh> first,next;
 Frame probe;probe.setIdentityProvider(identity);probe.setVersionProvider(version);
 assert(probe.read(&d,&d.decl,draw,scratch,&why,false,&first));const auto charge=probe.bytesRead();assert(charge);
 Frame ordinary(charge,0);ordinary.setIdentityProvider(identity);ordinary.setVersionProvider(version);
 assert(ordinary.read(&d,&d.decl,draw,scratch,&why,false,&first));
 assert(!ordinary.read(&d,&d.decl,draw,scratch,&why,false,&next)&&why.error==Error::Budget);
 Frame geometry(charge,0);geometry.setIdentityProvider(identity);geometry.setVersionProvider(version);geometry.setChargeResidentHits(false);
 const auto coldLocks=locks(d);assert(!geometry.read(&d,&d.decl,draw,scratch,&why,false,&next,false,true));
 assert(locks(d)==coldLocks&&geometry.bytesRead()==0);
 assert(geometry.read(&d,&d.decl,draw,scratch,&why,false,&first));const auto before=locks(d);const auto oldBytes=first->streams[0].bytes;
 for(unsigned i=0;i<5000;++i){assert(geometry.read(&d,&d.decl,draw,scratch,&why,false,&next));assert(next==first);}
 assert(geometry.bytesRead()==charge&&geometry.trackedHits()==5000&&locks(d)==before);
 assert(geometry.read(&d,&d.decl,draw,scratch,&why,false,&next,false,true)&&next==first);
 assert(geometry.bytesRead()==charge&&locks(d)==before);
 // A new generation is still charged and must wait for the next read-budget window.
 d.vb[0].bytes[d.offset[0]+7*d.stride[0]+1]^=0x55;revisions[&d.vb[0]]=4;
 assert(!geometry.read(&d,&d.decl,draw,scratch,&why,false,&next,false,true));assert(locks(d)==before&&geometry.bytesRead()==charge);
 assert(!geometry.read(&d,&d.decl,draw,scratch,&why,false,&next)&&why.error==Error::Budget);
 geometry.clearFrame();assert(geometry.read(&d,&d.decl,draw,scratch,&why,false,&next));assert(next!=first&&next->streams[0].bytes!=oldBytes&&first->streams[0].bytes==oldBytes);
 // Reciprocal peers serve only a proven current tracked generation. They do
 // not populate the borrower, charge reads, or recurse on a mutual miss.
 {Frame capture(charge,0);capture.setIdentityProvider(identity);capture.setVersionProvider(version);capture.setChargeResidentHits(false);
  geometry.setResidentPeer(&capture);capture.setResidentPeer(&geometry);const auto resident=next;const auto noReads=locks(d);
  assert(capture.read(&d,&d.decl,draw,scratch,&why,false,&next,false,true)&&next==resident);
  assert(capture.residentPeerHits()==1&&!capture.bytesRead()&&!capture.snapshotCacheBytes()&&locks(d)==noReads);
  d.vb[0].bytes[d.offset[0]+7*d.stride[0]+1]^=0x33;++revisions[&d.vb[0]];
  assert(!capture.read(&d,&d.decl,draw,scratch,&why,false,&next,false,true)&&locks(d)==noReads);
  assert(capture.read(&d,&d.decl,draw,scratch,&why,false,&next)&&next!=resident&&capture.bytesRead()==charge);
  geometry.clearFrame();assert(geometry.read(&d,&d.decl,draw,scratch,&why,false,&first,false,true)&&first==next&&!geometry.bytesRead());
  // Identical numeric revisions from a different provider are not a shared tracking domain.
  Frame unrelated(charge,0);unrelated.setIdentityProvider(identity);unrelated.setVersionProvider([](void* p,bool i){return version(p,i);});unrelated.setChargeResidentHits(false);unrelated.setResidentPeer(&capture);
  assert(!unrelated.read(&d,&d.decl,draw,scratch,&why,false,&first,false,true));
  assert(unrelated.read(&d,&d.decl,draw,scratch,&why,false,&first)&&first!=next&&!unrelated.residentPeerHits()&&unrelated.bytesRead()==charge);
  // The ordinary admission policy may not borrow its way around a budget.
  Frame charged(charge,0);charged.setIdentityProvider(identity);charged.setVersionProvider(version);charged.setResidentPeer(&capture);
  assert(!charged.read(&d,&d.decl,draw,scratch,&why,false,&first,false,true));
  assert(charged.read(&d,&d.decl,draw,scratch,&why,false,&first)&&!charged.residentPeerHits()&&charged.bytesRead()==charge);
  geometry.setResidentPeer(nullptr);capture.setResidentPeer(nullptr);
 }
 // Unknown static versions still use normal revalidation, admission and read charge.
 revisions.clear();d.vb[0].desc.Usage=d.vb[1].desc.Usage=d.ib.desc.Usage=0;
 Frame untracked(charge,0);untracked.setIdentityProvider(identity);untracked.setVersionProvider(version);untracked.setChargeResidentHits(false);
 assert(untracked.read(&d,&d.decl,draw,scratch,&why,false,&next));
 const auto untrackedLocks=locks(d);assert(!untracked.read(&d,&d.decl,draw,scratch,&why,false,&next,false,true));assert(locks(d)==untrackedLocks);
 assert(!untracked.read(&d,&d.decl,draw,scratch,&why,false,&next)&&why.error==Error::Budget);
 // A source working set larger than the former 4 MiB cache must converge
 // under the unchanged 2 MiB/frame cold-read budget, then perform zero reads.
 constexpr UINT sources=128,vertices=2049;
 for(unsigned s=0;s<2;++s){d.vb[s].bytes.resize(d.offset[s]+sources*vertices*d.stride[s]);revisions[&d.vb[s]]=s+1;}
 const auto workingSet=[&](size_t capacity){Frame cache(2u<<20,0);cache.setIdentityProvider(identity);cache.setVersionProvider(version);cache.setChargeResidentHits(false);cache.setSnapshotCacheLimit(capacity);
     unsigned accepted=0;for(unsigned frame=0;frame<16;++frame){cache.clearFrame();accepted=0;for(unsigned i=0;i<sources;++i){Draw part{D3DPT_TRIANGLELIST,0,0,0,i*vertices,vertices/3,false};accepted+=cache.read(&d,&d.decl,part,scratch,&why,false,&next);}}
     assert(cache.snapshotCacheBytes()<=capacity);return std::array<size_t,3>{accepted,cache.snapshotCacheHits(),cache.bytesRead()};};
 const auto small=workingSet(4u<<20),large=workingSet(24u<<20);
 assert(small[1]<sources&&small[2]>0&&large[0]==sources&&large[1]==sources&&large[2]==0);
 std::printf("MATERIAL_BATCH_WORKING_SET_PASS oldHits=%zu oldReadBytes=%zu newHits=%zu newReadBytes=%zu unchangedFrameBudget=2097152\n",small[1],small[2],large[1],large[2]);
 // The city's world + M2 set exceeds either 24 MiB cache, but fits the
 // unchanged combined capacity when both passes share immutable sources.
 {constexpr UINT world=96,models=160,vertices=4095;
  for(unsigned s=0;s<2;++s)d.vb[s].bytes.resize(d.offset[s]+(world+models)*vertices*d.stride[s]);
  const auto combined=[&](bool share){Frame displacement(2u<<20,0),capture(2u<<20,0);
   for(auto* c:{&displacement,&capture}){c->setIdentityProvider(identity);c->setVersionProvider(version);c->setChargeResidentHits(false);c->setSnapshotCacheLimit(24u<<20);}
   if(share){displacement.setResidentPeer(&capture);capture.setResidentPeer(&displacement);}
   unsigned accepted=0;std::vector<std::weak_ptr<const Mesh>> previous(models);
   for(unsigned frame=0;frame<40;++frame){displacement.clearFrame();capture.clearFrame();accepted=0;
    for(unsigned i=0;i<world+models;++i){Draw part{D3DPT_TRIANGLELIST,0,0,0,i*vertices,vertices/3,false};std::shared_ptr<const Mesh> posed,captured;
     accepted+=displacement.read(&d,&d.decl,part,scratch,&why,false,&posed);
     if(i>=world){accepted+=capture.read(&d,&d.decl,part,scratch,&why,false,&captured);
      if(share&&frame>=38){assert(posed&&posed==captured);if(frame==39)assert(previous[i-world].lock()==posed);previous[i-world]=posed;}}
    }
   }
   const auto reads=displacement.bytesRead()+capture.bytesRead(),retained=displacement.snapshotCacheBytes()+capture.snapshotCacheBytes();
   const auto peerHits=displacement.residentPeerHits()+capture.residentPeerHits();assert(displacement.snapshotCacheBytes()<=(24u<<20)&&capture.snapshotCacheBytes()<=(24u<<20));
   std::printf("MATERIAL_SHARED_SOURCE_WORKING_SET share=%u accepted=%u peerHits=%u readBytes=%zu retainedBytes=%zu\n",unsigned(share),accepted,peerHits,reads,retained);
   if(share)assert(accepted==world+models*2&&peerHits&&reads==0);else assert(reads>0);return reads;
  };assert(combined(false)>combined(true));
  const auto normals=[&](bool share){Frame skin(256u<<10,0),capture(2u<<20,0);
   for(auto* c:{&skin,&capture}){c->setIdentityProvider(identity);c->setVersionProvider(version);c->setChargeResidentHits(false);}
   skin.setSnapshotCacheLimit(4u<<20);capture.setSnapshotCacheLimit(24u<<20);if(share)skin.setResidentPeer(&capture);
   for(unsigned frame=0;frame<40;++frame){skin.clearFrame();capture.clearFrame();
    for(unsigned i=0;i<models;++i){Draw part{D3DPT_TRIANGLELIST,0,0,0,i*vertices,vertices/3,false};
     const bool admitted=skin.read(&d,&d.decl,part,scratch,&why,false,&next);if(share&&frame==39)assert(admitted);
     capture.read(&d,&d.decl,part,scratch,&why,false,&next);}}
   std::printf("MATERIAL_SKIN_RANGE_SOURCE_WORKING_SET share=%u peerHits=%u readBytes=%zu\n",unsigned(share),skin.residentPeerHits(),skin.bytesRead());
   if(share)assert(skin.residentPeerHits()&&!skin.bytesRead()&&!capture.bytesRead());else assert(skin.bytesRead());return skin.bytesRead();
  };assert(normals(false)>normals(true));
 }
 puts("SNAPSHOT_GEOMETRY_POLICY_PASS default_budget tracked_5000_hits_zero_reads admission_uncharged generation_miss_budget immutable_old unknown_static_charged");
}
