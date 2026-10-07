// Native fake-COM contract test; no D3D, Wine or game is executed.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>
#include <thread>
#include <vector>
#include <mutex>
#include <random>
using LONG=int;using ULONG=unsigned;using UINT=unsigned;using DWORD=unsigned;using HRESULT=int;using REFIID=const void*;
#define STDMETHODCALLTYPE
#define __uuidof(T) iid<T>()
template<class T>const void* iid(){static char tag;return &tag;}
constexpr HRESULT D3D_OK=0,S_OK=0,E_POINTER=-1,E_NOINTERFACE=-2,D3DERR_INVALIDCALL=-3;
constexpr DWORD D3DLOCK_READONLY=16,D3DLOCK_DISCARD=8192,D3DLOCK_NOOVERWRITE=4096;
enum D3DFORMAT {D3DFMT_UNKNOWN=0,D3DFMT_INDEX16=101,D3DFMT_INDEX32=102,D3DFMT_VERTEXDATA=100};
struct GUID {uint32_t a;uint16_t b,c;unsigned char d[8];};
enum D3DPOOL {D3DPOOL_DEFAULT=0,D3DPOOL_MANAGED=1};constexpr DWORD D3DUSAGE_DYNAMIC=512;
struct D3DVERTEXBUFFER_DESC {D3DFORMAT Format=D3DFMT_VERTEXDATA;UINT Size=4096;DWORD Usage=8;D3DPOOL Pool=D3DPOOL_DEFAULT;};
struct D3DINDEXBUFFER_DESC {D3DFORMAT Format=D3DFMT_INDEX16;UINT Size=1024;DWORD Usage=512;D3DPOOL Pool=D3DPOOL_DEFAULT;};
bool FAILED(HRESULT h){return h<0;}bool SUCCEEDED(HRESULT h){return h>=0;}
LONG InterlockedIncrement(LONG* p){return __atomic_add_fetch(p,1,__ATOMIC_SEQ_CST);}LONG InterlockedDecrement(LONG* p){return __atomic_sub_fetch(p,1,__ATOMIC_SEQ_CST);}
struct IUnknown{virtual HRESULT QueryInterface(REFIID,void**)=0;virtual ULONG AddRef()=0;virtual ULONG Release()=0;};
struct IDirect3DDevice9:IUnknown{};
struct IDirect3DResource9:IUnknown{virtual HRESULT GetDevice(IDirect3DDevice9**)=0;virtual HRESULT GetPrivateData(const GUID&,void*,DWORD*)=0;virtual HRESULT SetPrivateData(const GUID&,const void*,DWORD,DWORD)=0;};
struct IDirect3DVertexBuffer9:IDirect3DResource9{virtual HRESULT Lock(UINT,UINT,void**,DWORD)=0;virtual HRESULT Unlock()=0;virtual HRESULT GetDesc(D3DVERTEXBUFFER_DESC*)=0;};
struct IDirect3DIndexBuffer9:IDirect3DResource9{virtual HRESULT Lock(UINT,UINT,void**,DWORD)=0;virtual HRESULT Unlock()=0;virtual HRESULT GetDesc(D3DINDEXBUFFER_DESC*)=0;};
template<class T>struct Forward:T{
 using Desc=std::conditional_t<std::is_same<T,IDirect3DIndexBuffer9>::value,D3DINDEXBUFFER_DESC,D3DVERTEXBUFFER_DESC>;
 T* real;explicit Forward(T* p):real(p){}
 HRESULT QueryInterface(REFIID id,void** out)override{return real->QueryInterface(id,out);}ULONG AddRef()override{return real->AddRef();}ULONG Release()override{return real->Release();}
 HRESULT GetDevice(IDirect3DDevice9** out)override{return real->GetDevice(out);}HRESULT Lock(UINT o,UINT n,void** p,DWORD f)override{return real->Lock(o,n,p,f);}HRESULT Unlock()override{return real->Unlock();}
 HRESULT GetDesc(Desc* out)override{return real->GetDesc(out);}
 HRESULT GetPrivateData(const GUID& k,void* data,DWORD* size)override{return real->GetPrivateData(k,data,size);}
 HRESULT SetPrivateData(const GUID& k,const void* data,DWORD size,DWORD flags)override{return real->SetPrivateData(k,data,size,flags);}
};
using ForwardIDirect3DVertexBuffer9=Forward<IDirect3DVertexBuffer9>;using ForwardIDirect3DIndexBuffer9=Forward<IDirect3DIndexBuffer9>;
#include "tracked_buffers.h"
#include "replay_copy_reader.h"
struct Device:IDirect3DDevice9{unsigned refs=1;HRESULT QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}ULONG AddRef()override{return ++refs;}ULONG Release()override{return --refs;}};
template<class T>struct Raw final:T{
 using Desc=typename Forward<T>::Desc;
 std::vector<unsigned char> mem;unsigned readLocks=0,lockCalls=0,unlockCalls=0;DWORD lastFlags=0; // mem: a byte-backed buffer (replay copy tests); empty = the single int below
 unsigned refs=1,descGets=0,privateGets=0,privateSets=0;bool failLock=false,failUnlock=false,failDesc=false,failPrivateSet=false,placement=false;int data=42;uint64_t token=0;DWORD tokenSize=8;Desc desc;
 std::mutex privateMutex;
 HRESULT QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}ULONG AddRef()override{return ++refs;}ULONG Release()override{unsigned n=--refs;if(!n){if(placement)this->~Raw();else delete this;}return n;}
 HRESULT GetDevice(IDirect3DDevice9**)override{return E_NOINTERFACE;}
 HRESULT Lock(UINT off,UINT,void** out,DWORD f)override{if(failLock)return D3DERR_INVALIDCALL;++lockCalls;lastFlags=f;if(f&D3DLOCK_READONLY)++readLocks;*out=mem.empty()?static_cast<void*>(&data):static_cast<void*>(mem.data()+off);return S_OK;}
 HRESULT Unlock()override{++unlockCalls;return failUnlock?D3DERR_INVALIDCALL:S_OK;}
 HRESULT GetDesc(Desc* out)override{++descGets;if(failDesc)return D3DERR_INVALIDCALL;*out=desc;return S_OK;}
 static void checkToken(const GUID& k){const unsigned char tail[]={0x9a,0x55,0x0f,0x7e,0x21,0x66,0x9d,0x4b};assert(k.a==0x6f1d2a4c&&k.b==0x3b7e&&k.c==0x4c21&&!std::memcmp(k.d,tail,8));}
 HRESULT GetPrivateData(const GUID& k,void* out,DWORD* size)override{checkToken(k);std::lock_guard<std::mutex> lock(privateMutex);++privateGets;if(!token)return D3DERR_INVALIDCALL;assert(*size>=tokenSize);std::memcpy(out,&token,tokenSize);*size=tokenSize;return S_OK;}
 HRESULT SetPrivateData(const GUID& k,const void* in,DWORD size,DWORD flags)override{checkToken(k);std::lock_guard<std::mutex> lock(privateMutex);++privateSets;assert(size==8&&flags==0);if(failPrivateSet)return D3DERR_INVALIDCALL;std::memcpy(&token,in,8);tokenSize=8;return S_OK;}
};

static NorthlightCaptureMetadata::Info metadata(void* buffer,bool index){
 NorthlightCaptureMetadata::Request request{buffer,index};NorthlightCaptureMetadata::Info result;
 NorthlightTrackedBuffers::captureMetadata(&request,&result,1);return result;
}
static void metadataTests(){
 using namespace NorthlightTrackedBuffers;using namespace NorthlightCaptureMetadata;Device owner;
 auto* raw=new Raw<IDirect3DVertexBuffer9>;auto* ri=new Raw<IDirect3DIndexBuffer9>;
 IDirect3DVertexBuffer9* vb=raw;IDirect3DIndexBuffer9* ib=ri;
 wrap<IDirect3DVertexBuffer9,ForwardIDirect3DVertexBuffer9>(&vb,&owner,false);
 wrap<IDirect3DIndexBuffer9,ForwardIDirect3DIndexBuffer9>(&ib,&owner,true);
 bool wrapped=false;
 assert(resolveInput(vb,wrapped)==raw&&wrapped);
 assert(resolveInput(ib,wrapped)==ri&&wrapped);
 assert(resolveInput(raw,wrapped)==raw&&!wrapped);
 assert(resolveInput(ri,wrapped)==ri&&!wrapped);
 assert(resolveInput(static_cast<IDirect3DVertexBuffer9*>(nullptr),wrapped)==nullptr&&!wrapped);
 auto* untracked=new Raw<IDirect3DVertexBuffer9>;
 assert(resolveInput(untracked,wrapped)==untracked&&!wrapped);untracked->Release();
 // 0.3.136 lock-free input resolution: the first wrapper class publishes its
 // shape; a differently laid-out wrapper class for the same interface and
 // every raw/untracked input still resolve through the registry map.
 assert(Shape<IDirect3DVertexBuffer9>::info.load()&&Shape<IDirect3DIndexBuffer9>::info.load());
 {struct Padded:ForwardIDirect3DVertexBuffer9{using ForwardIDirect3DVertexBuffer9::ForwardIDirect3DVertexBuffer9;char pad[40]={};};
  auto* other=new Raw<IDirect3DVertexBuffer9>;IDirect3DVertexBuffer9* padded=other;wrap<IDirect3DVertexBuffer9,Padded>(&padded,&owner,false);
  assert(padded!=other&&!Shape<IDirect3DVertexBuffer9>::match(padded)&&Shape<IDirect3DVertexBuffer9>::match(vb));
  assert(resolveInput(padded,wrapped)==other&&wrapped);assert(resolveInput(vb,wrapped)==raw&&wrapped);
  std::vector<std::thread> readers;for(unsigned t=0;t<4;++t)readers.emplace_back([&]{bool w=false;for(unsigned n=0;n<20000;++n){assert(resolveInput(vb,w)==raw&&w);assert(resolveInput(padded,w)==other&&w);assert(resolveInput(raw,w)==raw&&!w);}});
  for(unsigned n=0;n<2000;++n){auto* churn=new Raw<IDirect3DVertexBuffer9>;IDirect3DVertexBuffer9* q=churn;wrap<IDirect3DVertexBuffer9,ForwardIDirect3DVertexBuffer9>(&q,&owner,false);bool w=false;assert(resolveInput(q,w)==churn&&w);q->Release();}
  for(auto& reader:readers)reader.join();padded->Release();}
 assert(raw->descGets==1&&raw->privateGets==2&&raw->privateSets==1&&ri->descGets==1&&ri->privateGets==2&&ri->privateSets==1);
 int unknown=0;Request requests[]={{raw,false},{vb,false},{ri,true},{ib,false},{&unknown,false}};Info out[5];
 for(unsigned repeat=0;repeat<100;++repeat){captureMetadata(requests,out,5);
  assert(out[0].known&&out[0].identity&&out[0].revision==version(raw,false)&&out[0].size==4096&&out[0].usage==8&&out[0].format==D3DFMT_VERTEXDATA);
  assert(out[1].known&&out[1].identity==out[0].identity&&out[1].revision==out[0].revision);
  assert(out[2].known&&out[2].identity!=out[0].identity&&out[2].size==1024&&out[2].usage==512&&out[2].format==D3DFMT_INDEX16);
  assert(!out[3].known&&!out[4].known&&out[3].identity==0&&out[4].revision==0);
 }
 assert(raw->descGets==1&&raw->privateGets==2&&raw->privateSets==1&&ri->descGets==1&&ri->privateGets==2&&ri->privateSets==1);
 const auto id=out[0].identity;auto generation=out[0].revision;void* data=nullptr;
 assert(vb->Lock(0,4,&data,D3DLOCK_READONLY)==S_OK);auto active=metadata(raw,false);assert(active.known&&active.identity==id&&active.size==4096&&!active.revision);
 assert(vb->Unlock()==S_OK&&metadata(raw,false).revision==generation);
 raw->failLock=true;assert(FAILED(vb->Lock(0,4,&data,0)));raw->failLock=false;
 assert(metadata(raw,false).revision&&metadata(raw,false).revision!=generation);generation=metadata(raw,false).revision;
 assert(vb->Lock(0,4,&data,D3DLOCK_DISCARD)==S_OK);raw->failLock=true;assert(FAILED(vb->Lock(0,4,&data,0)));raw->failLock=false;assert(!metadata(vb,false).revision);
 assert(vb->Unlock()==S_OK&&metadata(vb,false).revision!=generation);generation=metadata(vb,false).revision;
 written(vb);assert(metadata(raw,false).revision!=generation);generation=metadata(raw,false).revision;
 invalidateAll();assert(metadata(raw,false).revision!=generation&&metadata(raw,false).identity==id);
 assert(ib->Lock(0,4,&data,0)==S_OK&&!metadata(ri,true).revision);assert(ib->Unlock()==S_OK&&metadata(ri,true).revision);
 raw->failUnlock=true;assert(vb->Lock(0,4,&data,0)==S_OK&&FAILED(vb->Unlock()));assert(metadata(raw,false).known&&!metadata(raw,false).revision);
 // A subsequent failed write must not hide the still-outstanding lock.
 raw->failLock=true;assert(FAILED(vb->Lock(0,4,&data,0)));assert(!metadata(raw,false).revision);raw->failLock=false;
 captureMetadata(nullptr,out,5);for(const auto& info:out)assert(!info.known&&!info.identity&&!info.revision);
 captureMetadata(requests,nullptr,5);captureMetadata(nullptr,nullptr,0);
 vb->Release();ib->Release();assert(owner.refs==1&&records.empty());
 assert(!metadata(raw,false).known&&!metadata(ri,true).known);

 // Failed unpaired Unlock makes tracking unsafe; a failed write or successful
 // READONLY lock cannot repair it. Only a completed real write can recover.
 auto* unsafe=new Raw<IDirect3DVertexBuffer9>;IDirect3DVertexBuffer9* uv=unsafe;
 wrap<IDirect3DVertexBuffer9,ForwardIDirect3DVertexBuffer9>(&uv,&owner,false);
 const auto stableIdentity=metadata(uv,false).identity;unsafe->failUnlock=true;
 assert(FAILED(uv->Unlock())&&!version(uv,false)&&!metadata(uv,false).revision);
 unsafe->failUnlock=false;unsafe->failLock=true;
 assert(FAILED(uv->Lock(0,4,&data,0))&&!version(uv,false)&&!metadata(uv,false).revision&&unsafe->data==42);
 unsafe->failLock=false;assert(uv->Lock(0,4,&data,D3DLOCK_READONLY)==S_OK&&*static_cast<int*>(data)==42);
 assert(uv->Unlock()==S_OK&&!metadata(uv,false).revision);
 assert(uv->Lock(0,4,&data,0)==S_OK&&!metadata(uv,false).revision);*static_cast<int*>(data)=84;
 assert(uv->Unlock()==S_OK&&metadata(uv,false).revision&&metadata(uv,false).identity==stableIdentity&&unsafe->data==84);uv->Release();

 // GetDesc or token setup failure keeps the COM wrapper, with legacy fallback.
 for(unsigned failure=0;failure<3;++failure){auto* r=new Raw<IDirect3DVertexBuffer9>;r->failDesc=failure==0;r->failPrivateSet=failure==1;if(failure==2)r->desc.Size=0;
  IDirect3DVertexBuffer9* p=r;wrap<IDirect3DVertexBuffer9,ForwardIDirect3DVertexBuffer9>(&p,&owner,false);
  assert(p!=r&&version(r,false)&&!metadata(p,false).known&&r->descGets==1);p->Release();
 }
 // Real pointer reuse is forced, not left to allocator behavior.
 using R=Raw<IDirect3DVertexBuffer9>;alignas(R) unsigned char storage[sizeof(R)];uint64_t oldIdentity=0,oldRevision=0;
 for(unsigned lifetime=0;lifetime<2;++lifetime){auto* r=new(storage) R;r->placement=true;IDirect3DVertexBuffer9* p=r;
  wrap<IDirect3DVertexBuffer9,ForwardIDirect3DVertexBuffer9>(&p,&owner,false);auto info=metadata(r,false);
  assert(info.known&&info.identity!=oldIdentity&&info.revision!=oldRevision);oldIdentity=info.identity;oldRevision=info.revision;
  p->Release();assert(!metadata(r,false).known);
 }
 // Concurrent first sight returns one object-owned token, not racing tokens.
 auto* r=new Raw<IDirect3DVertexBuffer9>;std::vector<uint64_t> ids(8);std::vector<std::thread> workers;
 for(unsigned i=0;i<ids.size();++i)workers.emplace_back([&,i]{ids[i]=identity(r,false);});
 for(auto& thread:workers)thread.join();for(auto value:ids)assert(value&&value==ids[0]);assert(r->privateSets==1);
 // Existing-token lookup does not acquire identityMutex or write private data.
 const unsigned previousGets=r->privateGets;{std::lock_guard<std::mutex> held(identityMutex);assert(identity(r,false)==ids[0]);}
 assert(r->privateGets==previousGets+1&&r->privateSets==1);
 IDirect3DVertexBuffer9* p=r;wrap<IDirect3DVertexBuffer9,ForwardIDirect3DVertexBuffer9>(&p,&owner,false);assert(metadata(p,false).identity==ids[0]&&r->privateSets==1);p->Release();
 auto* malformed=new Raw<IDirect3DIndexBuffer9>;malformed->token=31;malformed->tokenSize=4;
 auto repaired=identity(malformed,true);assert(repaired&&repaired!=31&&malformed->privateSets==1&&malformed->tokenSize==8);malformed->Release();
 assert(identity(nullptr,false)==0&&owner.refs==1&&records.empty());
 puts("PASS lock-free shape resolution with registry fallback; cached VB/IB descriptors and GUID identity, 100 five-request batches without driver reads, live generations, lock/failure suppression, fallback, forced pointer reuse, concurrent first identity and balanced lifetime");
}
static unsigned unsafeReads=0;
static void unsafeRead(IDirect3DDevice9* owner,const char* reason){assert(owner&&reason);++unsafeReads;}
static void mirrorExposure(){
 using namespace NorthlightTrackedBuffers;Device owner;
 auto* raw=new Raw<IDirect3DVertexBuffer9>;IDirect3DVertexBuffer9* p=raw;
 wrap<IDirect3DVertexBuffer9,ForwardIDirect3DVertexBuffer9>(&p,&owner,false,&unsafeRead);
 assert(isWrapped(p)&&!isWrapped(raw)&&!isWrapped(nullptr)&&unsafeReads==0);
 // Extension metadata access stays raw and does not trigger an escape.
 auto id=identity(raw,false);assert(id&&unsafeReads==0);
 const GUID key={0x6f1d2a4c,0x3b7e,0x4c21,{0x9a,0x55,0x0f,0x7e,0x21,0x66,0x9d,0x4b}};
 uint64_t value=0;DWORD size=sizeof value;
 assert(p->GetPrivateData(key,&value,&size)==S_OK&&value==id&&unsafeReads==1);
 p->Release();assert(owner.refs==1&&records.empty());
 puts("PASS mirror escape before game private-data access, raw extension metadata unaffected");
}
// 0.3.192 (DXVK3): only a DISCARD lock of a DEFAULT|DYNAMIC (direct-mapped) wrapper is charged to the Game meter, at the buffer size.
static void discardMeterTests(){
 using namespace NorthlightTrackedBuffers;Device owner;auto& frame=NorthlightLockMeter::state().frameSite[NorthlightLockMeter::Game];
 for(bool index:{false,true}){
  struct Case {D3DPOOL pool;DWORD usage;DWORD flags;bool charged;};
  const Case cases[]={{D3DPOOL_DEFAULT,D3DUSAGE_DYNAMIC,D3DLOCK_DISCARD,true},{D3DPOOL_DEFAULT,D3DUSAGE_DYNAMIC,D3DLOCK_DISCARD|D3DLOCK_NOOVERWRITE,true},
   {D3DPOOL_DEFAULT,D3DUSAGE_DYNAMIC,0,false},{D3DPOOL_DEFAULT,D3DUSAGE_DYNAMIC,D3DLOCK_NOOVERWRITE,false},{D3DPOOL_DEFAULT,D3DUSAGE_DYNAMIC,D3DLOCK_READONLY,false},
   {D3DPOOL_MANAGED,D3DUSAGE_DYNAMIC,D3DLOCK_DISCARD,false},{D3DPOOL_MANAGED,0,D3DLOCK_DISCARD,false},{D3DPOOL_DEFAULT,0,D3DLOCK_DISCARD,false}}; // DEFAULT without DYNAMIC is BUFFER mode: no direct-mapped charge
  for(const auto& c:cases){
   frame.store(0);void* data=nullptr;
   if(index){auto* raw=new Raw<IDirect3DIndexBuffer9>;raw->desc.Size=2048;raw->desc.Pool=c.pool;raw->desc.Usage=c.usage;IDirect3DIndexBuffer9* ib=raw;wrap<IDirect3DIndexBuffer9,ForwardIDirect3DIndexBuffer9>(&ib,&owner,true);
    assert(ib->Lock(0,4,&data,c.flags)==S_OK);assert(ib->Unlock()==S_OK);assert(ib->Lock(0,4,&data,c.flags)==S_OK);assert(ib->Unlock()==S_OK);ib->Release();}
   else{auto* raw=new Raw<IDirect3DVertexBuffer9>;raw->desc.Size=4096;raw->desc.Pool=c.pool;raw->desc.Usage=c.usage;IDirect3DVertexBuffer9* vb=raw;wrap<IDirect3DVertexBuffer9,ForwardIDirect3DVertexBuffer9>(&vb,&owner,false);
    assert(vb->Lock(0,4,&data,c.flags)==S_OK);assert(vb->Unlock()==S_OK);assert(vb->Lock(0,4,&data,c.flags)==S_OK);assert(vb->Unlock()==S_OK);vb->Release();}
   assert(frame.load()==(c.charged?2u*(index?2048u:4096u):0u));
  }
 }
 // A failed GetDesc leaves the size unknown: nothing to charge.
 {frame.store(0);auto* raw=new Raw<IDirect3DVertexBuffer9>;raw->failDesc=true;raw->desc.Usage=D3DUSAGE_DYNAMIC;IDirect3DVertexBuffer9* vb=raw;wrap<IDirect3DVertexBuffer9,ForwardIDirect3DVertexBuffer9>(&vb,&owner,false);
  void* data=nullptr;assert(vb->Lock(0,4,&data,D3DLOCK_DISCARD)==S_OK&&vb->Unlock()==S_OK);vb->Release();assert(frame.load()==0);}
 assert(owner.refs==1);puts("PASS Game DISCARD meter: charged only for DISCARD of DEFAULT|DYNAMIC wrappers, at the buffer size");
}

// 0.3.192 (CS): the replay-side CPU copies (replay_copies.h) against a byte-backed fake backend. The backend always holds the buffer at the
// REPLAY position (the writes below are what the stream replays: Lock, memcpy, Unlock through the wrapper); the Reader must return exactly its bytes.
namespace C=NorthlightReplayCopies;
struct VB {Raw<IDirect3DVertexBuffer9>* raw;IDirect3DVertexBuffer9* vb;};
static VB makeVB(Device& owner,UINT size,DWORD usage=D3DUSAGE_DYNAMIC){
 auto* raw=new Raw<IDirect3DVertexBuffer9>;raw->desc.Size=size;raw->desc.Usage=usage;raw->mem.assign(size,0);IDirect3DVertexBuffer9* vb=raw;
 NorthlightTrackedBuffers::wrap<IDirect3DVertexBuffer9,ForwardIDirect3DVertexBuffer9>(&vb,&owner,false);raw->AddRef();return {raw,vb};}
static void freeVB(VB b){b.vb->Release();b.raw->Release();}
static void replayWrite(VB b,UINT off,UINT n,DWORD flags,unsigned seed){ // UnlockBuffer: Lock(off,n,flags), memcpy, Unlock
 void* p=nullptr;assert(b.vb->Lock(off,n,&p,flags)==S_OK);for(UINT i=0;i<(n?n:b.raw->desc.Size-off);++i)static_cast<unsigned char*>(p)[i]=(unsigned char)(seed*131u+i*7u+off);assert(b.vb->Unlock()==S_OK);}
// readSame reads through the RAW pointer, exactly what the capture gets from ext->GetStreamSource/GetIndices (the wrapper is only the stream's write path)
static bool readSame(VB b,UINT off,UINT n){C::Reader<IDirect3DVertexBuffer9> r(b.raw);void* p=nullptr;assert(r.lock(off,n,&p)==S_OK&&p);const bool same=!std::memcmp(p,b.raw->mem.data()+off,n);assert(r.unlock()==S_OK);return same;}
static void copiesTests(){
 using namespace NorthlightTrackedBuffers;Device owner;auto& m=NorthlightLockMeter::state();
 const auto served=[&]{return m.copyServed.load();};const auto fallback=[&]{return m.copyFallback.load();};
 // gate off: no slot attached, the Reader is exactly Lock(readBackLock())/Unlock, the wrapper hooks copy nothing
 {C::enabled.store(false);VB b=makeVB(owner,4096);using Wrapped=Buffer<IDirect3DVertexBuffer9,ForwardIDirect3DVertexBuffer9>;assert(!static_cast<Wrapped*>(b.vb)->hasCopySlotForTest());
  replayWrite(b,0,64,0,1);const auto s0=served();b.raw->lockCalls=0;b.raw->unlockCalls=0;
  NorthlightUpload::ReadBackNoOverwrite.store(true);
  for(int i=0;i<3;++i){assert(readSame(b,0,64));assert(b.raw->lastFlags==(D3DLOCK_READONLY|D3DLOCK_NOOVERWRITE));}
  NorthlightUpload::ReadBackNoOverwrite.store(false);
  assert(b.raw->lockCalls==3&&b.raw->unlockCalls==3&&b.raw->readLocks==3&&served()==s0&&m.copyFills.load()==0&&b.raw->lastFlags==(D3DLOCK_READONLY|D3DLOCK_NOOVERWRITE));
  freeVB(b);}
 C::enabled.store(true);
 // fill once, then served from the copy with no backend lock; writes at the replay position keep it exact (partial, NOOVERWRITE ring, whole DISCARD, size 0)
 {VB b=makeVB(owner,4096);replayWrite(b,0,4096,D3DLOCK_DISCARD,1);
  b.raw->readLocks=0;assert(readSame(b,16,100));assert(b.raw->readLocks==1&&m.copyFills.load()==1); // the fill: ONE read of the whole buffer
  const auto base=served();b.raw->readLocks=0;
  assert(readSame(b,0,4096)&&readSame(b,200,10)&&b.raw->readLocks==0&&served()==base+2);
  replayWrite(b,128,64,D3DLOCK_NOOVERWRITE,2);assert(readSame(b,0,4096)&&b.raw->readLocks==0);
  replayWrite(b,1000,500,0,3);assert(readSame(b,990,520)&&b.raw->readLocks==0);
  replayWrite(b,0,0,D3DLOCK_DISCARD,4);assert(readSame(b,0,4096)&&b.raw->readLocks==0); // size 0 = to the end
  replayWrite(b,4000,96,0,5);assert(readSame(b,3990,106)&&b.raw->readLocks==0);
  // a partial DISCARD: the copy keeps the old bytes outside the range (undefined in D3D9; this fake keeps them too); the written range is exact
  replayWrite(b,64,32,D3DLOCK_DISCARD,6);assert(readSame(b,64,32)&&readSame(b,0,4096)&&b.raw->readLocks==0);
  // the game thread already overwrote the ring for the NEXT frame: nothing is replayed yet, the copy still equals the backend (replay position)
  std::vector<unsigned char> at(b.raw->mem);assert(readSame(b,0,4096)&&!std::memcmp(at.data(),b.raw->mem.data(),4096));
  // out of range: an ordinary lock (the backend decides)
  const auto f0=fallback();{C::Reader<IDirect3DVertexBuffer9> r(b.raw);void* p=nullptr;assert(r.lock(4090,100,&p)==S_OK&&r.unlock()==S_OK);}assert(fallback()==f0+1&&b.raw->readLocks==1);
  freeVB(b);}
 // the stream's own replayed write names its source bytes (UnlockSourceScope): the copy is fed from them, not read back from the mapped pointer. The two are made to
 // differ here (a real replay writes the same bytes to both) so the test can tell which one the copy took; a pass-through write outside a scope reads the mapped pointer.
 {VB b=makeVB(owner,4096);replayWrite(b,0,4096,D3DLOCK_DISCARD,1);assert(readSame(b,0,64)&&m.copyFills.load()>0);
  std::vector<unsigned char> src(100,0xC3);void* p=nullptr;
  {C::UnlockSourceScope scope(src.data(),200,100);assert(b.vb->Lock(200,100,&p,0)==S_OK);std::memset(p,0x5A,100);assert(b.vb->Unlock()==S_OK);}
  b.raw->readLocks=0;{C::Reader<IDirect3DVertexBuffer9> r(b.raw);void* q=nullptr;assert(r.lock(200,100,&q)==S_OK);for(int i=0;i<100;++i)assert(static_cast<unsigned char*>(q)[i]==0xC3);assert(r.unlock()==S_OK);}
  assert(b.raw->readLocks==0&&b.raw->mem[200]==0x5A); // served from the copy (no backend read), and it holds the source bytes; the backend got what the mapped pointer was written with
  // a scope for another range does not apply: the mapped pointer is read
  {C::UnlockSourceScope scope(src.data(),0,100);assert(b.vb->Lock(300,100,&p,0)==S_OK);std::memset(p,0x7E,100);assert(b.vb->Unlock()==S_OK);}
  assert(readSame(b,300,100)&&b.raw->readLocks==0);
  // outside any scope (a pass-through lock the game writes through): the mapped pointer
  assert(b.vb->Lock(500,50,&p,0)==S_OK);std::memset(p,0x11,50);assert(b.vb->Unlock()==S_OK);assert(readSame(b,500,50)&&b.raw->readLocks==0);
  freeVB(b);}
 // a write lock outstanding (pass-through): not served; the pointer the game writes through is mirrored at Unlock
 {VB b=makeVB(owner,1024);replayWrite(b,0,1024,0,7);assert(readSame(b,0,8));b.raw->readLocks=0;
  void* p=nullptr;assert(b.vb->Lock(100,50,&p,0)==S_OK);std::memset(p,0x5a,50);
  const auto f0=fallback();{C::Reader<IDirect3DVertexBuffer9> r(b.raw);void* q=nullptr;assert(r.lock(0,8,&q)==S_OK&&r.unlock()==S_OK);}assert(fallback()==f0+1&&b.raw->readLocks==1);
  // the read-back lock goes to the RAW buffer (invisible to the wrapper), so the pending write is closed by its own Unlock and mirrored exactly: the copy stays valid
  assert(b.vb->Unlock()==S_OK);b.raw->readLocks=0;assert(readSame(b,90,70)&&b.raw->readLocks==0&&readSame(b,90,70)&&b.raw->readLocks==0);
  // without a read in between the pass-through write is mirrored exactly and no backend lock is needed
  assert(b.vb->Lock(300,50,&p,0)==S_OK);std::memset(p,0x33,50);assert(b.vb->Unlock()==S_OK);b.raw->readLocks=0;assert(readSame(b,280,100)&&b.raw->readLocks==0);
  // a READONLY pass-through lock does not touch the copy
  assert(b.vb->Lock(0,10,&p,D3DLOCK_READONLY)==S_OK&&b.vb->Unlock()==S_OK&&readSame(b,0,1024)&&b.raw->readLocks==1);
  // a nested write lock cannot be mirrored: the copy is dropped, the next read fills again
  const auto inv=m.copyInvalidations.load();assert(b.vb->Lock(0,10,&p,0)==S_OK);void* p2=nullptr;assert(b.vb->Lock(20,10,&p2,0)==S_OK);assert(b.vb->Unlock()==S_OK&&b.vb->Unlock()==S_OK);
  assert(m.copyInvalidations.load()==inv+1);b.raw->readLocks=0;assert(readSame(b,0,1024)&&b.raw->readLocks==1);assert(readSame(b,0,1024)&&b.raw->readLocks==1);
  // a failed write lock / failed Unlock drop it as well
  b.raw->failLock=true;assert(FAILED(b.vb->Lock(0,4,&p,0)));b.raw->failLock=false;assert(m.copyInvalidations.load()==inv+2);
  assert(readSame(b,0,1024));b.raw->failUnlock=true;assert(b.vb->Lock(0,4,&p,0)==S_OK&&FAILED(b.vb->Unlock()));b.raw->failUnlock=false;assert(m.copyInvalidations.load()==inv+3);
  freeVB(b);}
 // a GPU-side write (ProcessVertices): invalidated for good, every read is an ordinary lock
 {VB b=makeVB(owner,1024);replayWrite(b,0,1024,0,8);assert(readSame(b,0,8)&&readSame(b,0,8));written(b.vb);b.raw->readLocks=0;
  for(int i=0;i<3;++i)assert(readSame(b,0,1024));assert(b.raw->readLocks==3);freeVB(b);}
 // a read failure of the fill: ordinary path, nothing resident
 {VB b=makeVB(owner,1024);b.raw->failLock=true;{C::Reader<IDirect3DVertexBuffer9> r(b.raw);void* p=nullptr;assert(FAILED(r.lock(0,8,&p)));}b.raw->failLock=false;assert(readSame(b,0,8));freeVB(b);}
 // eviction (LRU by last read, cap 16 MiB), thrash guard, per-buffer limit, pressure shrink, pins
 {const UINT big=3u<<20;std::vector<VB> v;for(int i=0;i<6;++i){v.push_back(makeVB(owner,big));replayWrite(v.back(),0,4096,0,10+i);}
  const auto ev=m.copyEvictions.load();
  for(auto& b:v){assert(readSame(b,0,64)&&readSame(b,0,64));assert(b.raw->readLocks==2);} // a big buffer is not filled for a small range within one frame
  C::advanceFrame();for(auto& b:v){assert(readSame(b,0,64));C::advanceFrame();} // a read in a second frame fills it
  assert(m.copyResidentBytes.load()<=C::kCapBytes&&m.copyEvictions.load()>ev&&m.copyResidentBuffers.load()==5);
  // the evicted one (the first, least recently read) is back on the ordinary lock; it fills again on its second read after the oldest of the rest is evicted
  v[0].raw->readLocks=0;assert(readSame(v[0],0,64)&&v[0].raw->readLocks==1);C::advanceFrame();assert(readSame(v[0],0,64));assert(m.copyResidentBytes.load()<=C::kCapBytes);
  for(auto& b:v)replayWrite(b,100,10,0,99);for(auto& b:v)assert(readSame(b,0,big)); // every copy stayed exact through the eviction churn
  // pins: a Reader holding a copy keeps it through a pressure shrink; the bytes stay valid (ASan)
  {VB& b=v[5];assert(readSame(b,0,64));C::Reader<IDirect3DVertexBuffer9> r(b.raw);void* p=nullptr;assert(r.lock(0,128,&p)==S_OK);
   C::setPressure(true);assert(m.copyCapBytes.load()==C::kCapBytes/2);assert(!std::memcmp(p,b.raw->mem.data(),128));assert(r.unlock()==S_OK);}
  assert(m.copyResidentBytes.load()<=C::kCapBytes/2);
  // under pressure a 3 MiB buffer is above cap/4 (2 MiB): no copy; at full cap it is allowed again
  {unsigned plain=0;for(auto& b:v){b.raw->readLocks=0;assert(readSame(b,0,64));C::advanceFrame();assert(readSame(b,0,64));assert(b.raw->readLocks==0||b.raw->readLocks==2);plain+=b.raw->readLocks==2;}
   assert(plain>=4&&m.copyResidentBuffers.load()==v.size()-plain);} // the survivors of the shrink stay served; no 3 MiB buffer is filled any more
  C::setPressure(false);assert(m.copyCapBytes.load()==C::kCapBytes);
  for(auto& b:v)freeVB(b);assert(m.copyResidentBytes.load()==0&&m.copyResidentBuffers.load()==0);}
 {VB b=makeVB(owner,5u<<20);replayWrite(b,0,64,0,1);for(int i=0;i<4;++i){C::advanceFrame();b.raw->readLocks=0;assert(readSame(b,0,64)&&b.raw->readLocks==1);}freeVB(b);} // above cap/4: ordinary while the large allowance is blocked (the pressure edges above start its back-off)
 // L1: the LARGE allowance: a ~15.8 MB buffer (above cap/4) gets a copy of its own, outside the regular cap, without memory pressure
 {const UINT L=15800000;for(unsigned i=0;i<C::kLargeBackoffFrames+2;++i)C::advanceFrame();   // the back-off of the pressure edges above is over
  VB b=makeVB(owner,L);replayWrite(b,0,4096,0,41);const auto g0=m.copyLargeGrants.load(),d0=m.copyLargeDrops.load(),res0=m.copyResidentBytes.load();
  b.raw->readLocks=0;assert(readSame(b,0,64)&&b.raw->readLocks==1&&m.copyLargeBytes.load()==0);   // frame 1: a read-back lock, no whole-buffer fill (not even for a whole-buffer request)
  {C::Reader<IDirect3DVertexBuffer9> r(b.raw);void* p=nullptr;assert(r.lock(0,L,&p)==S_OK&&r.unlock()==S_OK);assert(m.copyLargeBytes.load()==0&&b.raw->readLocks==2);}
  C::advanceFrame();assert(readSame(b,0,64)&&b.raw->readLocks==3);   // frame 2: ONE whole-buffer read fills it
  assert(m.copyLargeBytes.load()==L&&m.copyLargeGrants.load()==g0+1&&m.copyResidentBytes.load()==res0&&m.copyFillBytes.load()>=L);   // separate budget: `resident` is the regular cap only
  b.raw->readLocks=0;const auto s0=served();assert(readSame(b,0,L)&&readSame(b,100000,200)&&b.raw->readLocks==0&&served()==s0+2);   // served through the raw pointer
  replayWrite(b,5000,300,D3DLOCK_NOOVERWRITE,42);replayWrite(b,L-100,100,0,43);assert(readSame(b,0,L)&&b.raw->readLocks==0);   // kept current from the replayed writes
  // a second large buffer is refused while the first is read recently, regular buffers are unaffected
  {VB c2=makeVB(owner,L);replayWrite(c2,0,64,0,44);for(int i=0;i<4;++i){C::advanceFrame();c2.raw->readLocks=0;assert(readSame(c2,0,64)&&readSame(b,0,64));assert(c2.raw->readLocks==1);}assert(m.copyLargeBytes.load()==L);freeVB(c2);}
  // pressure: dropped (a pinned one at its unpin: its bytes stay valid until then), no re-grant within the back-off, then granted again
  {assert(readSame(b,0,64));C::Reader<IDirect3DVertexBuffer9> r(b.raw);void* p=nullptr;assert(r.lock(0,128,&p)==S_OK);
   C::setPressure(true);assert(m.copyLargeBytes.load()==L&&m.copyLargeDrops.load()==d0);assert(!std::memcmp(p,b.raw->mem.data(),128));   // pinned: kept until the unpin
   assert(r.unlock()==S_OK);assert(m.copyLargeBytes.load()==0&&m.copyLargeDrops.load()==d0+1);}
  b.raw->readLocks=0;assert(readSame(b,0,64)&&b.raw->readLocks==1);
  C::setPressure(false);
  for(unsigned i=0;i<C::kLargeBackoffFrames-2;++i){C::advanceFrame();if(i%97==0){assert(readSame(b,0,64));assert(m.copyLargeBytes.load()==0);}}   // reads in many frames: still no 16 MB fill
  assert(m.copyLargeGrants.load()==g0+1);
  for(int i=0;i<5;++i){C::advanceFrame();assert(readSame(b,0,64));}
  assert(m.copyLargeGrants.load()==g0+2&&m.copyLargeBytes.load()==L);   // the back-off is over: one fill again
  // a GPU write invalidates it for good
  written(b.vb);assert(m.copyLargeBytes.load()==0);b.raw->readLocks=0;for(int i=0;i<3;++i){C::advanceFrame();assert(readSame(b,0,64));}assert(b.raw->readLocks==3&&m.copyLargeGrants.load()==g0+2);
  freeVB(b);assert(m.copyLargeBytes.load()==0&&m.copyResidentBytes.load()==res0);}
 // R1: the lookup accepts the RAW pointer (what ext->GetStreamSource/GetIndices return) as well as the wrapper; both reach the same slot
 {VB b=makeVB(owner,4096);replayWrite(b,0,4096,D3DLOCK_DISCARD,1);const auto s0=served(),f0=fallback();
  {C::Reader<IDirect3DVertexBuffer9> r(b.raw);void* p=nullptr;assert(r.lock(0,128,&p)==S_OK&&!std::memcmp(p,b.raw->mem.data(),128)&&r.unlock()==S_OK);} // the fill, through the raw pointer
  assert(m.copyFills.load()>0);b.raw->readLocks=0;
  for(int i=0;i<3;++i){C::Reader<IDirect3DVertexBuffer9> r(b.raw);void* p=nullptr;assert(r.lock(64,256,&p)==S_OK&&!std::memcmp(p,b.raw->mem.data()+64,256)&&r.unlock()==S_OK);}
  {C::Reader<IDirect3DVertexBuffer9> r(b.vb);void* p=nullptr;assert(r.lock(0,16,&p)==S_OK&&!std::memcmp(p,b.raw->mem.data(),16)&&r.unlock()==S_OK);} // the wrapper pointer too
  assert(served()==s0+5&&fallback()==f0&&b.raw->readLocks==0);
  // an index buffer, raw as well
  freeVB(b);}
 // R2: a working set above the cap does not refill every frame: fills per frame are bounded, the rest takes the small-range locks
 {C::setPressure(true);const UINT big=1u<<20;std::vector<VB> v;for(int i=0;i<12;++i){v.push_back(makeVB(owner,big));replayWrite(v.back(),0,4096,0,30+i);} // 12 MiB > the 8 MiB pressure cap; each is <= cap/4
  const auto fills0=m.copyFills.load(),fb0=fallback();unsigned frames=100,maxPerFrame=0;
  for(unsigned f=0;f<frames;++f){const auto fillsBefore=m.copyFills.load();for(auto& b:v){assert(readSame(b,0,64)&&readSame(b,4096,64));}
   const auto n=m.copyFills.load()-fillsBefore;if(n>maxPerFrame)maxPerFrame=unsigned(n);C::advanceFrame();}
  const auto fills=m.copyFills.load()-fills0;std::printf("  thrash: fills=%llu max/frame=%u fallbackLocks=%llu\n",(unsigned long long)fills,maxPerFrame,(unsigned long long)(fallback()-fb0));
  assert(fills<=v.size()*(1+C::kMaxStrikes)+v.size()*frames/C::kBackoffFrames+8&&fills<frames); // far fewer fills than frames (a refill-per-frame policy would give ~frames*12/2)
  assert(fallback()-fb0>frames*4); // most reads are the small-range read-back locks
  // after the back-off the parked buffers are admitted again (frames pass, nothing is stuck)
  for(unsigned f=0;f<C::kBackoffFrames+10;++f)C::advanceFrame();const auto f1=m.copyFills.load();for(int i=0;i<3;++i){for(auto& b:v)assert(readSame(b,0,64));C::advanceFrame();}assert(m.copyFills.load()>f1);
  C::setPressure(false);for(auto& b:v)freeVB(b);}
 // R3: a pin keeps the wrapper (and the slot inside it) alive: the game releasing it while the Reader holds bytes is safe, the last unpin destroys it
 {Device d2;auto* raw=new Raw<IDirect3DVertexBuffer9>;raw->desc.Size=4096;raw->desc.Usage=D3DUSAGE_DYNAMIC;raw->mem.assign(4096,0);IDirect3DVertexBuffer9* vb=raw;
  NorthlightTrackedBuffers::wrap<IDirect3DVertexBuffer9,ForwardIDirect3DVertexBuffer9>(&vb,&d2,false);raw->AddRef();VB b{raw,vb};replayWrite(b,0,4096,D3DLOCK_DISCARD,1);
  {C::Reader<IDirect3DVertexBuffer9> r(raw);void* p=nullptr;assert(r.lock(0,64,&p)==S_OK&&d2.refs==2);vb->Release(); // the game's last reference goes while the Reader pins: the Reader's reference keeps the wrapper
   assert(!std::memcmp(p,raw->mem.data(),64)&&d2.refs==2&&NorthlightTrackedBuffers::records.size()==2);
   assert(r.unlock()==S_OK);}  // the last reference goes here: ~Buffer, detach, owner released
  assert(d2.refs==1&&NorthlightTrackedBuffers::records.empty());raw->Release();}
 // randomized equivalence (20000 steps): partial/whole writes of all lock flags, pending locks, reads at random ranges, GPU writes, pressure toggles
 {std::mt19937 rng(20260);const UINT sizes[]={512,4096,60000,70000,3u<<20,3u<<20,3u<<20,3u<<20};std::vector<VB> v;for(UINT s:sizes)v.push_back(makeVB(owner,s));
  unsigned reads=0,mismatch=0;const auto servedBefore=served(),fillsBefore=m.copyFills.load(),evictBefore=m.copyEvictions.load();
  for(unsigned step=0;step<20000;++step){
   VB& b=v[rng()%v.size()];const UINT len=b.raw->desc.Size;const UINT n=1+rng()%(len>8192?8192:len),off=rng()%(len-n+1);
   switch(rng()%12){
   case 0:case 1:case 2:replayWrite(b,off,n,DWORD(rng()%3==0?D3DLOCK_DISCARD:rng()%2?D3DLOCK_NOOVERWRITE:0),step);break;
   case 3:replayWrite(b,off,0,DWORD(rng()%2?D3DLOCK_DISCARD:0),step);break;
   case 4:if(rng()%8)break;{void* p=nullptr;assert(b.vb->Lock(off,n,&p,0)==S_OK);const UINT a=rng()%n;if(!readSame(b,off,n))++mismatch;std::memset(p,int(step),a);assert(b.vb->Unlock()==S_OK);break;} // reads while pending go to the backend
   case 5:if(&b==&v[0]&&rng()%25==0)written(b.vb);break; // only the first (tiny) buffer is ever GPU-written
   case 6:if(rng()%60==0)C::setPressure(rng()%2);break;
   case 7:if(rng()%10==0)C::advanceFrame();break; // a frame is ~10 steps
   default:++reads;if(!readSame(b,off,n))++mismatch;break;}
   assert(m.copyResidentBytes.load()<=m.copyCapBytes.load()||m.copyCapBytes.load()==0);
  }
  std::printf("  random run: reads=%u served=%llu fills=%llu evictions=%llu\n",reads,(unsigned long long)(served()-servedBefore),(unsigned long long)(m.copyFills.load()-fillsBefore),(unsigned long long)(m.copyEvictions.load()-evictBefore));
  assert(mismatch==0&&reads>5000&&served()-servedBefore>1500&&m.copyFills.load()>fillsBefore&&m.copyEvictions.load()>evictBefore); // the copies really did the serving
C::setPressure(false);for(auto& b:v)freeVB(b);}
 C::enabled.store(false);assert(owner.refs==1&&records.empty());
 puts("PASS replay copies: exact at the replay position through partial/NOOVERWRITE/DISCARD writes, pending and nested locks, GPU write, eviction, pressure, pins, gate off = plain lock; 20000-step equivalence");
}
int main(){using namespace NorthlightTrackedBuffers;Device owner;
 auto* raw=new Raw<IDirect3DVertexBuffer9>;IDirect3DVertexBuffer9* vb=raw;wrap<IDirect3DVertexBuffer9,ForwardIDirect3DVertexBuffer9>(&vb,&owner,false);
 assert(vb!=raw&&unwrap(vb)==raw&&unwrap(raw)==raw&&owner.refs==2);uint64_t start=version(raw,false);assert(start&&version(raw,true)==0);
 void* data=nullptr;assert(vb->Lock(0,4,&data,D3DLOCK_READONLY)==S_OK&&version(raw,false)==0);assert(vb->Unlock()==S_OK&&version(raw,false)==start);
 for(DWORD flags:{0u,D3DLOCK_DISCARD,D3DLOCK_NOOVERWRITE}){auto old=version(raw,false);assert(vb->Lock(0,4,&data,flags)==S_OK&&version(raw,false)==0);*static_cast<int*>(data)+=1;assert(vb->Unlock()==S_OK&&version(raw,false)!=old);}
 // A failed nested lock must not make an outstanding writable pointer cacheable.
 assert(vb->Lock(0,4,&data,0)==S_OK);raw->failLock=true;assert(FAILED(vb->Lock(0,4,&data,0)));assert(version(raw,false)==0);raw->failLock=false;assert(vb->Unlock()==S_OK&&version(raw,false));
 auto before=version(raw,false);written(vb);assert(version(raw,false)!=before);before=version(raw,false);invalidateAll();assert(version(raw,false)!=before);
 IDirect3DDevice9* dev=nullptr;assert(vb->GetDevice(&dev)==S_OK&&dev==&owner);dev->Release();
 void* q=nullptr;assert(vb->QueryInterface(iid<IUnknown>(),&q)==S_OK&&q==vb);static_cast<IUnknown*>(static_cast<IDirect3DVertexBuffer9*>(q))->Release();assert(vb->QueryInterface(iid<int>(),&q)==E_NOINTERFACE&&q==nullptr);
 IDirect3DVertexBuffer9* got=raw;raw->AddRef();expose(&got);assert(got==vb&&raw->refs==1);got->Release();
 raw->failUnlock=true;assert(vb->Lock(0,4,&data,0)==S_OK);assert(FAILED(vb->Unlock())&&version(raw,false)==0);
 raw->AddRef();vb->Release();assert(owner.refs==1&&version(raw,false)==0&&unwrap(raw)==raw);raw->Release();
 auto* ri=new Raw<IDirect3DIndexBuffer9>;IDirect3DIndexBuffer9* ib=ri;wrap<IDirect3DIndexBuffer9,ForwardIDirect3DIndexBuffer9>(&ib,&owner,true);assert(version(ri,true)&&!version(ri,false));ib->Release();assert(owner.refs==1&&records.empty());
 puts("PASS buffer COM ownership, VB/IB identity, normal/DISCARD/NOOVERWRITE writes, READONLY, nested failed lock, failed unlock, ProcessVertices invalidation and reset");
 metadataTests();mirrorExposure();discardMeterTests();copiesTests();
}
