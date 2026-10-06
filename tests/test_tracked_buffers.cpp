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
struct Device:IDirect3DDevice9{unsigned refs=1;HRESULT QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}ULONG AddRef()override{return ++refs;}ULONG Release()override{return --refs;}};
template<class T>struct Raw final:T{
 using Desc=typename Forward<T>::Desc;
 unsigned refs=1,descGets=0,privateGets=0,privateSets=0;bool failLock=false,failUnlock=false,failDesc=false,failPrivateSet=false,placement=false;int data=42;uint64_t token=0;DWORD tokenSize=8;Desc desc;
 std::mutex privateMutex;
 HRESULT QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}ULONG AddRef()override{return ++refs;}ULONG Release()override{unsigned n=--refs;if(!n){if(placement)this->~Raw();else delete this;}return n;}
 HRESULT GetDevice(IDirect3DDevice9**)override{return E_NOINTERFACE;}
 HRESULT Lock(UINT,UINT,void** out,DWORD)override{if(failLock)return D3DERR_INVALIDCALL;*out=&data;return S_OK;}
 HRESULT Unlock()override{return failUnlock?D3DERR_INVALIDCALL:S_OK;}
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
 metadataTests();mirrorExposure();
}
