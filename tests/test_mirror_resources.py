#!/usr/bin/env python3
# northlight-test: requires=cxx,zig
"""Native resource-proxy ownership, identity, containment and escape regression."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import hashlib,json,re,subprocess,tempfile
fp.use_source_modules()
ROOT=Path(__file__).resolve().parent
SDK=fp.windows_headers()/'d3d9.h'
NAMES=['IDirect3DResource9','IDirect3DBaseTexture9','IDirect3DSurface9','IDirect3DTexture9','IDirect3DCubeTexture9','IDirect3DVolumeTexture9','IDirect3DVolume9','IDirect3DVertexShader9','IDirect3DPixelShader9','IDirect3DVertexDeclaration9','IDirect3DQuery9']
def stub():
    text=SDK.read_text()
    out=['#pragma once','#include <cstdint>','#define STDMETHODCALLTYPE','template<class T>struct IID;','#define __uuidof(T) IID<T>::value','using HRESULT=int32_t;using ULONG=uint32_t;using DWORD=uint32_t;using UINT=uint32_t;using REFIID=const int&;using REFGUID=const int&;using D3DRESOURCETYPE=unsigned;using D3DTEXTUREFILTERTYPE=unsigned;using D3DCUBEMAP_FACES=unsigned;using D3DQUERYTYPE=unsigned;using HDC=void*;','constexpr HRESULT S_OK=0,D3D_OK=0,E_POINTER=-1,E_NOINTERFACE=-2,D3DERR_INVALIDCALL=-3;inline bool SUCCEEDED(HRESULT h){return h>=0;}','constexpr unsigned D3DRTYPE_SURFACE=1,D3DRTYPE_TEXTURE=3,D3DRTYPE_VOLUMETEXTURE=4,D3DRTYPE_CUBETEXTURE=5;','struct D3DSURFACE_DESC{};struct D3DLOCKED_RECT{};struct RECT{};struct D3DVOLUME_DESC{};struct D3DLOCKED_BOX{};struct D3DBOX{};struct D3DVERTEXELEMENT9{};','struct IUnknown {virtual HRESULT QueryInterface(REFIID,void**)=0;virtual ULONG AddRef()=0;virtual ULONG Release()=0;protected:virtual ~IUnknown()=default;};','struct IDirect3DDevice9:IUnknown{};']
    out += [f'constexpr int iid_{name}={i};' for i,name in enumerate(['IUnknown','IDirect3DDevice9']+NAMES,1)]
    out += [f'struct {name};' for name in NAMES]
    out += [f'template<>struct IID<{name}>{{static constexpr int value=iid_{name};}};' for name in ['IUnknown','IDirect3DDevice9']+NAMES]
    for name in NAMES:
        body=text.split('DECLARE_INTERFACE_IID_('+name+',')[1].split('};')[0]
        base=body.split(',')[0].strip();out += [f'struct {name}:{base} {{']
        for m in re.finditer(r'STDMETHOD(?:_\(([^,]+),\s*(\w+)\)|\((\w+)\))\((.*?)\) PURE;',body,re.S):
            ret,special,normal,params=m.groups();ret=ret or 'HRESULT';method=special or normal
            params=re.sub(r'\bTHIS_?\b','',params).strip();params=' '.join(params.split())
            out.append(f'virtual {ret} {method}({params}) {{'+('return;' if ret=='void' else 'return {};')+'}')
        out.append('};')
    return '\n'.join(out)
HARNESS=r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <thread>
#include <vector>
// 0.3.182 (D1): the gate's mutex asserts ownership on unlock and counts real acquisitions.
struct CheckedGateMutex {
 std::recursive_mutex mutex;std::atomic<std::thread::id> owner{};unsigned depth=0;
 static inline std::atomic<unsigned long> acquisitions{0};
 void enter(){if(!depth++)owner.store(std::this_thread::get_id());acquisitions.fetch_add(1);}
 void lock(){mutex.lock();enter();}
 bool try_lock(){if(!mutex.try_lock())return false;enter();return true;}
 void unlock(){assert(owner.load()==std::this_thread::get_id()&&depth);if(!--depth)owner.store(std::thread::id());mutex.unlock();}
};
#define NORTHLIGHT_GATE_MUTEX CheckedGateMutex
// A resource call body, entered under the gate: never two at once (D1 stress); and the count of entries.
static std::atomic<int>* resourceBody=nullptr;
static std::atomic<unsigned> resourceEntries{0};
static int allocationFailure=-1;
#ifndef NORTHLIGHT_TEST_TSAN
void* operator new(std::size_t n){if(allocationFailure==0)throw std::bad_alloc();if(allocationFailure>0)--allocationFailure;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
#endif
#include "mirror_resources.h"
using namespace NorthlightMirrorResources;
static unsigned devicesDestroyed=0;
struct Device final:IDirect3DDevice9 {
 std::atomic<unsigned> refs{1};bool selfDelete=false,disabled=false;unsigned disables=0;
 MirrorGate gate;Registry registry;
 Device(IDirect3DDevice9* raw=nullptr):registry(this,gate,&stop,this,raw?raw:this){}
 static void stop(void* c,const char*){auto& d=*static_cast<Device*>(c);d.disabled=true;++d.disables;}
 HRESULT QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=iid_IUnknown&&id!=iid_IDirect3DDevice9)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
 ULONG AddRef()override{return ++refs;}ULONG Release()override{unsigned n=--refs;if(!n&&selfDelete)delete this;return n;}
 ~Device(){assert(registry.size()==0);++devicesDestroyed;}
};
template<class T>struct Resource:T {
 std::atomic<unsigned> refs{1};Device* device;unsigned type=0;IUnknown* container=nullptr;IDirect3DSurface9* surface=nullptr;IDirect3DVolume9* volume=nullptr;bool* mustDisable=nullptr;unsigned calls=0;
 HRESULT queryResult=S_OK;void* queryData=nullptr;DWORD querySize=0,queryFlags=0,queryIssue=0;unsigned typeReads=0;
 explicit Resource(Device& d):device(&d){}
 HRESULT QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=777&&!supported<T>(id))return E_NOINTERFACE;*out=static_cast<T*>(this);AddRef();return S_OK;}
 ULONG AddRef()override{return ++refs;}ULONG Release()override{assert(refs);return --refs;}
 HRESULT GetDevice(IDirect3DDevice9** out)override{if(!out)return D3DERR_INVALIDCALL;*out=device;device->AddRef();return D3D_OK;}
 unsigned GetType(){++typeReads;return type;}
 HRESULT GetContainer(REFIID id,void** out){if(!container)return E_NOINTERFACE;return container->QueryInterface(id,out);}
 HRESULT GetSurfaceLevel(UINT,IDirect3DSurface9** out){if(!out)return D3DERR_INVALIDCALL;*out=surface;if(surface)surface->AddRef();return D3D_OK;}
 HRESULT GetCubeMapSurface(D3DCUBEMAP_FACES,UINT,IDirect3DSurface9** out){return GetSurfaceLevel(0,out);}
 HRESULT GetVolumeLevel(UINT,IDirect3DVolume9** out){if(!out)return D3DERR_INVALIDCALL;*out=volume;if(volume)volume->AddRef();return D3D_OK;}
 HRESULT GetPrivateData(REFGUID,void* data,DWORD* size){assert(!mustDisable||*mustDisable);if(resourceBody){assert(resourceBody->fetch_add(1)==0);resourceBody->fetch_sub(1);}++resourceEntries;++calls;if(size)*size=4;if(data)*static_cast<unsigned*>(data)=123;return S_OK;}
 HRESULT GetFunction(void* data,UINT* size){++calls;if(size)*size=4;if(data)*static_cast<unsigned*>(data)=456;return S_OK;}
 HRESULT GetDeclaration(D3DVERTEXELEMENT9*,UINT* count){++calls;if(count)*count=17;return S_OK;}
 HRESULT Issue(DWORD flags){++calls;queryIssue=flags;return S_OK;}
 HRESULT GetData(void* data,DWORD size,DWORD flags){++calls;queryData=data;querySize=size;queryFlags=flags;if(queryResult==S_OK&&data&&size>=4)*static_cast<DWORD*>(data)=987;return queryResult;}
};

static void queryProfiling(){
 Device d;Resource<IDirect3DQuery9> raw(d);raw.type=8;IDirect3DQuery9* p=&raw;d.registry.wrap(&p);DWORD value=77;
 NorthlightRenderThreadProbe::configure(false,false);raw.queryResult=1;
 assert(p->GetData(&value,4,1)==1&&value==77&&raw.calls==1&&raw.typeReads==0&&d.registry.queryProfile.calls==0);
 NorthlightRenderThreadProbe::configure(true,false);
 assert(p->GetData(&value,4,1)==1&&value==77&&raw.queryData==&value&&raw.querySize==4&&raw.queryFlags==1);
 raw.queryResult=S_OK;assert(p->GetData(&value,4,0)==S_OK&&value==987&&raw.queryFlags==0);
 assert(raw.typeReads==1&&d.registry.queryProfile.calls==2&&d.registry.queryProfile.pending==1&&d.registry.queryProfile.events==2&&d.registry.queryProfile.completed==1);
 d.registry.queryProfile={};raw.queryResult=1;assert(p->GetData(nullptr,0,1)==1);
 assert(p->Issue(2)==S_OK&&raw.queryIssue==2);raw.queryResult=S_OK;
 assert(p->GetData(nullptr,0,1)==S_OK&&d.registry.queryProfile.completed==0);
 raw.queryResult=D3DERR_INVALIDCALL;assert(p->GetData(&value,4,1)==D3DERR_INVALIDCALL&&d.registry.queryProfile.errors==1);
 const auto count=d.registry.queryProfile.calls;NorthlightRenderThreadProbe::configure(false,false);
 assert(p->GetData(nullptr,0,0)==D3DERR_INVALIDCALL&&d.registry.queryProfile.calls==count);
 p->Release();assert(d.registry.size()==0&&raw.refs==0);
}
template<class T>void lifetime(unsigned type=0){
 Device d;Resource<T> raw(d);raw.type=type;T* p=&raw;d.registry.wrap(&p);assert(p!=&raw&&d.refs==2&&raw.refs==1&&d.registry.size()==1);
 assert(d.registry.unwrap(p)==&raw&&!d.disabled);assert(d.registry.unwrap(static_cast<T*>(nullptr))==nullptr&&!d.disabled);
 T* twice=p;p->AddRef();d.registry.wrap(&twice);assert(twice==p);twice->Release();
 T* again=&raw;raw.AddRef();d.registry.wrap(&again);assert(again==p&&raw.refs==1);again->Release();
 for(int iid:{iid_IUnknown,__uuidof(T)}){void* q=nullptr;assert(p->QueryInterface(iid,&q)==S_OK&&q==p);static_cast<IUnknown*>(static_cast<T*>(q))->Release();}
 if constexpr(std::is_base_of<IDirect3DResource9,T>::value){void* q=nullptr;assert(p->QueryInterface(iid_IDirect3DResource9,&q)==S_OK&&q==p);static_cast<IDirect3DResource9*>(q)->Release();}
 if constexpr(std::is_base_of<IDirect3DBaseTexture9,T>::value){IDirect3DBaseTexture9* q=nullptr;assert(p->QueryInterface(iid_IDirect3DBaseTexture9,reinterpret_cast<void**>(&q))==S_OK&&q==p);d.registry.wrap(&q);assert(q==p);q->Release();}
 IDirect3DDevice9* device=nullptr;assert(p->GetDevice(&device)==S_OK&&device==&d&&d.refs==3);device->Release();assert(!d.disabled);
 assert(p->GetDevice(nullptr)==D3DERR_INVALIDCALL);assert(p->QueryInterface(iid_IUnknown,nullptr)==E_POINTER);
 p->Release();assert(raw.refs==0&&d.refs==1&&d.registry.size()==0);
}
static void containment(){
 Device d;Resource<IDirect3DTexture9> texture(d);texture.type=D3DRTYPE_TEXTURE;Resource<IDirect3DSurface9> surface(d);surface.type=D3DRTYPE_SURFACE;texture.surface=&surface;surface.container=&texture;
 IDirect3DTexture9* t=&texture;d.registry.wrap(&t);IDirect3DSurface9* s=nullptr;assert(t->GetSurfaceLevel(0,&s)==S_OK&&s!=&surface);
 for(int iid:{iid_IUnknown,iid_IDirect3DTexture9,iid_IDirect3DBaseTexture9,iid_IDirect3DResource9}){void* result=nullptr;assert(s->GetContainer(iid,&result)==S_OK&&result==t);static_cast<IUnknown*>(static_cast<IDirect3DTexture9*>(result))->Release();}
 IDirect3DResource9* r=&surface;surface.AddRef();d.registry.wrap(&r);assert(r==s);r->Release();
 IUnknown* u=&texture;texture.AddRef();d.registry.wrap(&u);assert(u==t);u->Release();
 surface.container=&d;void* device=nullptr;assert(s->GetContainer(iid_IDirect3DDevice9,&device)==S_OK&&device==&d&&!d.disabled);static_cast<IDirect3DDevice9*>(device)->Release();
 assert(s->GetContainer(iid_IUnknown,&device)==S_OK&&device==&d&&!d.disabled);static_cast<IUnknown*>(device)->Release();
 Device foreign;surface.container=&foreign;assert(s->GetContainer(iid_IDirect3DDevice9,&device)==S_OK&&device==&foreign&&d.disabled);static_cast<IDirect3DDevice9*>(device)->Release();
 d.disabled=false;assert(s->GetContainer(iid_IUnknown,&device)==S_OK&&device==&foreign&&d.disabled);static_cast<IUnknown*>(device)->Release();
 s->Release();t->Release();assert(texture.refs==0&&surface.refs==1&&d.registry.size()==0&&d.refs==1);
 Resource<IDirect3DCubeTexture9> cube(d);cube.type=D3DRTYPE_CUBETEXTURE;cube.surface=&surface;IDirect3DCubeTexture9* c=&cube;d.registry.wrap(&c);assert(c->GetCubeMapSurface(2,3,&s)==S_OK&&s!=&surface);s->Release();c->Release();
 Resource<IDirect3DVolumeTexture9> vt(d);vt.type=D3DRTYPE_VOLUMETEXTURE;Resource<IDirect3DVolume9> volume(d);vt.volume=&volume;volume.container=&vt;IDirect3DVolumeTexture9* v=&vt;d.registry.wrap(&v);IDirect3DVolume9* level=nullptr;assert(v->GetVolumeLevel(0,&level)==S_OK&&level!=&volume);void* parent=nullptr;assert(level->GetContainer(iid_IDirect3DBaseTexture9,&parent)==S_OK&&parent==v);static_cast<IDirect3DBaseTexture9*>(parent)->Release();level->Release();v->Release();assert(d.registry.size()==0&&d.refs==1);
}
static void escapes(){
 Device d;Resource<IDirect3DTexture9> raw(d);raw.type=D3DRTYPE_TEXTURE;raw.mustDisable=&d.disabled;IDirect3DTexture9* p=&raw;d.registry.wrap(&p);
 void* q=nullptr;assert(p->QueryInterface(888,&q)==E_NOINTERFACE&&!q&&!d.disabled);assert(p->QueryInterface(777,&q)==S_OK&&q==&raw&&d.disabled);static_cast<IUnknown*>(static_cast<IDirect3DTexture9*>(q))->Release();
 d.disabled=false;DWORD size=0;unsigned value=0;assert(p->GetPrivateData(7,&value,&size)==S_OK&&d.disabled&&value==123&&size==4);
 d.disabled=false;assert(d.registry.unwrap(&raw)==&raw&&d.disabled);d.disabled=false;Resource<IDirect3DTexture9> alien(d);assert(d.registry.unwrap(&alien)==&alien&&d.disabled);
 p->Release();assert(raw.refs==0&&d.registry.size()==0&&d.refs==1);
}
static void rawLookup(){
 // Registry::rawOf: game-memory numbers, map lookup only; never disables.
 Device d;Resource<IDirect3DTexture9> raw(d);raw.type=D3DRTYPE_TEXTURE;IDirect3DTexture9* p=&raw;d.registry.wrap(&p);
 const auto exposed=reinterpret_cast<std::uintptr_t>(p),rawAddress=reinterpret_cast<std::uintptr_t>(&raw);
 assert(d.registry.rawOf(exposed)==rawAddress&&d.registry.rawOf(exposed,true)==rawAddress);
 assert(d.registry.rawOf(0)==0&&d.registry.rawOf(0,true)==0);
 // Mirror active: a miss is 0, including a value that equals a live RAW address (a stale exposed pointer).
 assert(d.registry.rawOf(rawAddress)==0&&d.registry.rawOf(0x12340)==0&&!d.disabled);
 assert(d.registry.rawOf(0x12340,true)==0x12340&&!d.disabled); // escaped: pass through
 p->Release();assert(d.registry.rawOf(exposed)==0&&d.registry.size()==0&&d.refs==1); // released proxy: no longer mapped
}
static void failures(){
#ifndef NORTHLIGHT_TEST_TSAN
 unsigned failed=0,passed=0;
 for(int fail=0;fail<12;++fail){Device d;Resource<IDirect3DTexture9> raw(d);raw.type=D3DRTYPE_TEXTURE;IDirect3DTexture9* p=&raw;allocationFailure=fail;d.registry.wrap(&p);allocationFailure=-1;
  if(d.disabled){++failed;assert(p==&raw&&d.registry.size()==0&&raw.refs==1&&d.refs==1);}else{++passed;assert(p!=&raw&&d.refs==2);p->Release();assert(d.registry.size()==0&&d.refs==1&&raw.refs==0);}}
 assert(failed>=3&&passed>=1);
#endif
}
static void threads(){
 Device d;Resource<IDirect3DVertexShader9> raw(d);IDirect3DVertexShader9* retained=&raw;d.registry.wrap(&retained);
 std::vector<std::thread> threads;for(unsigned n=0;n<4;++n)threads.emplace_back([&]{for(unsigned i=0;i<2000;++i){raw.AddRef();IDirect3DVertexShader9* p=&raw;d.registry.wrap(&p);assert(p==retained&&d.registry.unwrap(p)==&raw);IDirect3DDevice9* owner=nullptr;p->GetDevice(&owner);assert(owner==&d);owner->Release();p->Release();}});
 for(auto& t:threads)t.join();assert(raw.refs==1&&d.refs==2);retained->Release();assert(raw.refs==0&&d.refs==1&&d.registry.size()==0);
}
static void foreignInputs(){
 Device a,b;Resource<IDirect3DTexture9> raw(a);raw.type=D3DRTYPE_TEXTURE;IDirect3DTexture9* p=&raw;a.registry.wrap(&p);
 assert(b.registry.unwrap(p)==&raw&&b.disabled&&!a.disabled);
 std::vector<std::thread> workers;for(unsigned i=0;i<4;++i)workers.emplace_back([&]{for(unsigned n=0;n<1000;++n){p->AddRef();assert(b.registry.unwrap(p)==&raw);p->Release();}});
 for(auto& worker:workers)worker.join();assert(raw.refs==1&&a.refs==2&&b.refs==1);p->Release();assert(a.registry.size()==0);
 {std::lock_guard<std::mutex> lock(allProxiesMutex);assert(allProxies.empty());}
 // Backend reuses exactly the same resource address after final release.
 alignas(Resource<IDirect3DVertexShader9>) unsigned char storage[sizeof(Resource<IDirect3DVertexShader9>)];
 for(unsigned i=0;i<64;++i){auto* r=new(storage) Resource<IDirect3DVertexShader9>(a);IDirect3DVertexShader9* q=r;a.registry.wrap(&q);assert(a.registry.unwrap(q)==r);q->Release();assert(r->refs==0&&a.registry.size()==0);r->~Resource();}
}
static void ownerLifetime(){
 unsigned before=devicesDestroyed;auto* d=new Device;d->selfDelete=true;Resource<IDirect3DVertexShader9> raw(*d);IDirect3DVertexShader9* p=&raw;d->registry.wrap(&p);assert(d->refs==2);d->Release();assert(devicesDestroyed==before);std::thread release([&]{p->Release();});release.join();assert(raw.refs==0&&devicesDestroyed==before+1);
}
// 0.3.136 O(1) unwrap: one learned shape per proxy class; foreign-registry
// proxies and raw objects keep the locked map path and its escape handling.
static void shapeUnwrap(){
 Device a,b;Resource<IDirect3DSurface9> s(a);s.type=D3DRTYPE_SURFACE;Resource<IDirect3DTexture9> t(a);t.type=D3DRTYPE_TEXTURE;Resource<IDirect3DCubeTexture9> c(a);c.type=D3DRTYPE_CUBETEXTURE;
 Resource<IDirect3DVolumeTexture9> v(a);v.type=D3DRTYPE_VOLUMETEXTURE;Resource<IDirect3DVolume9> vol(a);Resource<IDirect3DVertexShader9> vs(a);Resource<IDirect3DPixelShader9> ps(a);Resource<IDirect3DVertexDeclaration9> decl(a);Resource<IDirect3DQuery9> q(a);
 IDirect3DSurface9* ps0=&s;IDirect3DTexture9* pt=&t;IDirect3DCubeTexture9* pc=&c;IDirect3DVolumeTexture9* pv=&v;IDirect3DVolume9* pvol=&vol;IDirect3DVertexShader9* pvs=&vs;IDirect3DPixelShader9* pps=&ps;IDirect3DVertexDeclaration9* pd=&decl;IDirect3DQuery9* pq=&q;
 a.registry.wrap(&ps0);a.registry.wrap(&pt);a.registry.wrap(&pc);a.registry.wrap(&pv);a.registry.wrap(&pvol);a.registry.wrap(&pvs);a.registry.wrap(&pps);a.registry.wrap(&pd);a.registry.wrap(&pq);
 assert(a.registry.shapes()==9&&b.registry.shapes()==0);
 for(unsigned n=0;n<1000;++n){assert(a.registry.unwrap(ps0)==&s&&a.registry.unwrap(pt)==&t&&a.registry.unwrap(pc)==&c&&a.registry.unwrap(pv)==&v&&a.registry.unwrap(pvol)==&vol);
  assert(a.registry.unwrap(pvs)==&vs&&a.registry.unwrap(pps)==&ps&&a.registry.unwrap(pd)==&decl&&a.registry.unwrap(pq)==&q);
  assert(a.registry.unwrap(static_cast<IDirect3DBaseTexture9*>(pt))==static_cast<IDirect3DBaseTexture9*>(&t)&&a.registry.unwrap(static_cast<IDirect3DBaseTexture9*>(pc))==static_cast<IDirect3DBaseTexture9*>(&c));}
 assert(!a.disabled&&a.registry.shapes()==9);
 // Same proxy class, other registry: not taken by the shape, still an escape.
 Resource<IDirect3DTexture9> bt(b);bt.type=D3DRTYPE_TEXTURE;IDirect3DTexture9* pbt=&bt;b.registry.wrap(&pbt);assert(b.registry.shapes()==1);
 assert(a.registry.unwrap(pbt)==&bt&&a.disabled);a.disabled=false;assert(b.registry.unwrap(pt)==&t&&b.disabled);b.disabled=false;
 // Raw backend objects never match a proxy vtable.
 assert(a.registry.unwrap(static_cast<IDirect3DTexture9*>(&t))==&t&&a.disabled);a.disabled=false;
 pbt->Release();ps0->Release();pt->Release();pc->Release();pv->Release();pvol->Release();pvs->Release();pps->Release();pd->Release();pq->Release();
 assert(a.registry.size()==0&&b.registry.size()==0&&a.refs==1&&b.refs==1);
}
// 0.3.180 (D0): the gate census over the registry and resource proxies. A device call (an outer guard)
// that wraps, unwraps and walks GetSurfaceLevel makes exactly one real acquisition; the proxies' own
// calls outside a device call count as registry/resource; a std::thread's resource calls count as
// foreign with the first {tid,site}; the owner never does. The last Release on the owner thread
// destroys the device (and its gate) after the guard's scope (ASan).
static void census(){
 Device d;MirrorGate& g=d.gate;g.counting=true;assert(g.ownerTid==MirrorGuard::threadId());
 Resource<IDirect3DTexture9> texture(d);texture.type=D3DRTYPE_TEXTURE;Resource<IDirect3DSurface9> surface(d);surface.type=D3DRTYPE_SURFACE;texture.surface=&surface;surface.container=&texture;
 IDirect3DTexture9* t=&texture;IDirect3DSurface9* s=nullptr;
 {MirrorGuard call(g);d.registry.wrap(&t);assert(d.registry.unwrap(t)==&texture);assert(t->GetSurfaceLevel(0,&s)==S_OK&&s!=&surface);DWORD size=0;s->GetPrivateData(1,nullptr,&size);}
 assert(g.takeAcquired(MirrorSite::Device)==1);for(unsigned k=1;k<MirrorGate::Sites;++k)assert(!g.takeAcquired(MirrorSite(k)));
 IDirect3DDevice9* owner=nullptr;assert(s->GetDevice(&owner)==S_OK);owner->Release();s->AddRef();s->Release();
 assert(g.takeAcquired(MirrorSite::Resource)==1&&g.takeAcquired(MirrorSite::Registry)==2&&!g.takeAcquired(MirrorSite::Device));
 for(unsigned k=0;k<MirrorGate::Sites;++k)assert(!g.foreign[k]);
 std::uint32_t tid=0;std::thread other([&]{tid=MirrorGuard::threadId();IDirect3DDevice9* o=nullptr;s->GetDevice(&o);o->Release();DWORD size=0;s->GetPrivateData(1,nullptr,&size);s->AddRef();s->Release();});other.join();
 assert(g.foreign[unsigned(MirrorSite::Resource)]==2&&g.foreign[unsigned(MirrorSite::Registry)]==2&&g.firstReady&&g.firstTid==tid&&g.firstSite==unsigned(MirrorSite::Resource));
 for(unsigned k=0;k<MirrorGate::Sites;++k)assert(!g.takeAcquired(MirrorSite(k)));
 s->Release();t->Release();assert(d.registry.size()==0);
 // Owner-thread final release: the proxy's Release destroys the self-deleting device after its guard closed.
 unsigned before=devicesDestroyed;auto* e=new Device;e->selfDelete=true;Resource<IDirect3DVertexShader9> raw(*e);IDirect3DVertexShader9* p=&raw;e->registry.wrap(&p);e->Release();assert(devicesDestroyed==before);
 p->Release();assert(raw.refs==0&&devicesDestroyed==before+1&&!MirrorGuard::heldByThisThread(g));
}
// 0.3.182 (D1): the owner's elided calls over the registry and resource proxies. Registry and resource
// entries nested in an elided device call take no mutex. Foreign resource calls (GetPrivateData,
// GetDevice, AddRef/Release) never overlap the owner's elided bodies (a body counter; TSan on the fake's
// plain counters and the registry's refs); the owner elides again, with no mutex, once they stop.
static void ownerElision(unsigned rounds){
 Device d;MirrorGate& g=d.gate;
 Resource<IDirect3DTexture9> texture(d);texture.type=D3DRTYPE_TEXTURE;Resource<IDirect3DSurface9> surface(d);surface.type=D3DRTYPE_SURFACE;texture.surface=&surface;surface.container=&texture;
 IDirect3DTexture9* t=&texture;IDirect3DSurface9* s=nullptr;const unsigned long locks=CheckedGateMutex::acquisitions;
 {MirrorGuard call(g);assert(call.elided());d.registry.wrap(&t);assert(d.registry.unwrap(t)==&texture);assert(t->GetSurfaceLevel(0,&s)==S_OK&&s!=&surface);
  DWORD size=0;s->GetPrivateData(1,nullptr,&size);s->AddRef();s->Release();assert(MirrorGuard::heldByThisThread(g)&&g.inside==1);}
 assert(CheckedGateMutex::acquisitions==locks&&g.inside==0&&!MirrorGuard::heldByThisThread(g));
 // Deterministic: a foreign resource call announced during an elided owner call does not enter it.
 {std::thread other;
  {MirrorGuard call(g);assert(call.elided());const unsigned before=resourceEntries;
   other=std::thread([&]{DWORD size=0;s->GetPrivateData(1,nullptr,&size);});
   while(!g.foreignActive.load()&&resourceEntries==before)std::this_thread::yield();
   std::this_thread::sleep_for(std::chrono::milliseconds(200));assert(resourceEntries==before);}
  other.join();}
 std::atomic<int> body{0};resourceBody=&body;std::atomic<bool> stop{false};std::atomic<unsigned long> foreignCalls{0};unsigned long elided=0;
 std::vector<std::thread> foreign;
 for(unsigned k=0;k<2;++k)foreign.emplace_back([&,k]{unsigned n=k;while(!stop.load(std::memory_order_relaxed)){
   switch(n++%3){case 0:{DWORD size=0;s->GetPrivateData(1,nullptr,&size);break;}case 1:{IDirect3DDevice9* o=nullptr;s->GetDevice(&o);o->Release();break;}default:s->AddRef();s->Release();break;}
   foreignCalls.fetch_add(1);for(volatile unsigned i=0,gap=(n*2654435761u)%4096;i<gap;++i){}}});
 for(unsigned r=0;r<rounds;++r){
  {MirrorGuard call(g);elided+=call.elided();assert(body.fetch_add(1)==0);assert(d.registry.unwrap(t)==&texture);assert(body.fetch_sub(1)==1);
   DWORD size=0;s->GetPrivateData(1,nullptr,&size);s->AddRef();s->Release();}
  for(volatile unsigned i=0,gap=(r*2654435761u)%512;i<gap;++i){}
 }
 while(foreignCalls<64)std::this_thread::yield();
 stop=true;for(auto& f:foreign)f.join();resourceBody=nullptr;
 assert(g.foreignActive==0&&g.inside==0&&elided&&foreignCalls);
 const unsigned long after=CheckedGateMutex::acquisitions;
 for(unsigned r=0;r<1000;++r){MirrorGuard call(g);assert(call.elided());assert(d.registry.unwrap(t)==&texture);s->AddRef();s->Release();}
 assert(CheckedGateMutex::acquisitions==after);
 s->Release();t->Release();assert(d.registry.size()==0);
 std::printf("PASS owner elision: nested registry/resource entries take no mutex; %u owner rounds (%lu elided) vs %lu foreign resource calls, bodies never overlap, elision resumes.\n",rounds,elided,(unsigned long)foreignCalls);
}
int main(int argc,char** argv){
 if(argc>1&&!std::strcmp(argv[1],"elision")){ownerElision(10000);return 0;}
 ownerElision(10000);
 census();
 shapeUnwrap();
 queryProfiling();
 lifetime<IDirect3DSurface9>(D3DRTYPE_SURFACE);lifetime<IDirect3DTexture9>(D3DRTYPE_TEXTURE);lifetime<IDirect3DCubeTexture9>(D3DRTYPE_CUBETEXTURE);lifetime<IDirect3DVolumeTexture9>(D3DRTYPE_VOLUMETEXTURE);lifetime<IDirect3DVolume9>();lifetime<IDirect3DVertexShader9>();lifetime<IDirect3DPixelShader9>();lifetime<IDirect3DVertexDeclaration9>();lifetime<IDirect3DQuery9>();
 containment();escapes();rawLookup();failures();threads();foreignInputs();ownerLifetime();
 std::puts("PASS gate census: one real acquisition per wrapping device call, registry/resource classes, foreign resource calls with the first {tid,site}, owner-thread final release destroys the device after its guard; rawOf map-only lookup (miss 0 while active, pass-through after escape); O(1) shape unwrap for nine proxy classes incl. base-texture views, foreign-registry and raw inputs keep the map path; nine resource types, COM identity/refcounts, child/container wrapping, foreign device escape, allocation failures, raw input disables, 8000 concurrent wraps, 4000 foreign unwraps, 64 pointer reuse cycles, final owner/gate lifetime");
}
'''
def main():
    files=['mirror_resources.h','mirror_guard.h','mirror_resource_forwarders.h','mirror_write_hook.h','generate_mirror_resource_forwarders.py','test_mirror_resources.py']
    hashes={f:hashlib.sha256(fp.tracked(f).read_bytes()).hexdigest() for f in files}
    report={'status':'running','source_sha256':hashes,'runs':[],'game_launched':False,'wine_launched':False,'limitations':['Fake COM backend; does not validate full device integration or real driver behavior.','GetPrivateData conservatively disables mirror because arbitrary GUID contents may contain an interface.']}
    with tempfile.TemporaryDirectory(prefix='northlight-resource-mirror-') as tmp:
        tmp=Path(tmp);(tmp/'d3d9.h').write_text(stub());(tmp/'test.cpp').write_text(HARNESS)
        for label,flags in [('O2',['-O2']),('ASan+UBSan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']),('TSan',['-O1','-g','-fsanitize=thread','-DNORTHLIGHT_TEST_TSAN=1'])]:
            binary=tmp/label.replace('+','-');command=['clang++','-std=c++17','-pthread',*flags,'-Wno-inconsistent-missing-override','-I'+str(tmp),*fp.test_include_flags(),str(tmp/'test.cpp'),'-o',str(binary)]
            subprocess.run(command,check=True);run=subprocess.run([str(binary)],capture_output=True,text=True,check=True,timeout=900);report['runs'].append({'build':label,'stdout':run.stdout,'stderr':run.stderr,'status':'pass'})
        # 0.3.182 (D1) counterfactual: a foreign thread that skips the wait on inside overlaps the owner's body.
        binary=tmp/'skip-wait';subprocess.run(['clang++','-std=c++17','-pthread','-O2','-DNORTHLIGHT_GATE_ELISION_COUNTERFACTUAL=2','-Wno-inconsistent-missing-override','-I'+str(tmp),*fp.test_include_flags(),str(tmp/'test.cpp'),'-o',str(binary)],check=True)
        # Deterministic: the pinned elided call is entered (an assertion). A hang is never a detection.
        run=subprocess.run([str(binary),'elision'],capture_output=True,text=True,timeout=300);caught=run.returncode!=0 and ('Assertion failed' in run.stderr or 'Assertion `' in run.stderr or 'Assertion "' in run.stderr)
        print('elision counterfactual (foreign skips the wait): '+('overlap detected (expected failure)' if caught else 'NOT DETECTED'),flush=True);assert caught,(run.returncode,run.stdout,run.stderr)
        report['runs'].append({'build':'O2 -DNORTHLIGHT_GATE_ELISION_COUNTERFACTUAL=2','mode':'elision','exit_code':run.returncode,'expected':'failure'})
    assert all('ThreadSanitizer' not in run['stderr'] for run in report['runs'] if run.get('status')=='pass')
    # Regenerate into scratch storage: the freshness check must not rewrite a live source header.
    import generate_mirror_resource_forwarders
    generated=fp.GENERATED/'mirror_resource_forwarders.h';before=generated.read_text()
    with tempfile.TemporaryDirectory(prefix='mirror-forwarders-') as tmp:
        scratch=Path(tmp)/generated.name;generate_mirror_resource_forwarders.generate(scratch)
        assert scratch.read_bytes()==generated.read_bytes(),'mirror_resource_forwarders.h is stale: run scripts/generate_mirror_resource_forwarders.py'
    assert 'lock_guard' not in before and before.count('MirrorGuard lock(gate,MirrorSite::Resource);')>=100 and '#include "mirror_guard.h"' in before
    assert hashes=={f:hashlib.sha256(fp.tracked(f).read_bytes()).hexdigest() for f in files}
    report['status']='pass';out=fp.output_dir();destination=out/'resource-mirror-validation.json';destination.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
