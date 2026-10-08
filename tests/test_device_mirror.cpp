#include "test_device_mirror_api.h"
#include <cassert>
#include <thread>
// 0.3.182 (D1): the gate's mutex, checked. An unlock by a thread that does not own it asserts, and every
// real acquisition is counted (an elided owner call makes none).
struct CheckedGateMutex {
 std::recursive_mutex mutex;std::atomic<std::thread::id> owner{};unsigned depth=0;
 static inline std::atomic<unsigned long> acquisitions{0};
 void enter(){if(!depth++)owner.store(std::this_thread::get_id());acquisitions.fetch_add(1);}
 void lock(){mutex.lock();enter();}
 bool try_lock(){if(!mutex.try_lock())return false;enter();return true;}
 void unlock(){assert(owner.load()==std::this_thread::get_id()&&depth);if(!--depth)owner.store(std::thread::id());mutex.unlock();}
};
#define NORTHLIGHT_GATE_MUTEX CheckedGateMutex
#define NORTHLIGHT_DEVICE_MIRROR_TEST_API
#include "device_mirror.h"
#include "mirror_audit_schedule.h"
#include "saved_state.h"
#include <cassert>
#include <cstdio>
#include <array>
#include <random>
#include <thread>
#include <vector>
#include <type_traits>
#include <memory>
#include <chrono>
#include <condition_variable>
#include <stdexcept>
#include <cstdlib>

static std::atomic<unsigned> liveObjects{0};
template<class T>struct Object final:T {
 std::atomic<unsigned> refs{1};Object(){++liveObjects;}~Object(){--liveObjects;}
 ULONG AddRef()override{return ++refs;}ULONG Release()override{auto r=--refs;if(!r)delete this;return r;}
};
template<class T>struct Ref {
 T* p=nullptr;Ref()=default;Ref(const Ref& r):p(r.p){if(p)p->AddRef();}Ref& operator=(const Ref& r){return *this=r.p;}
 Ref& operator=(T* v){if(v)v->AddRef();if(p)p->Release();p=v;return *this;}~Ref(){if(p)p->Release();}
};
template<class T>static HRESULT give(T* p,T** out){if(!out)return D3DERR_INVALIDCALL;*out=p;if(p)p->AddRef();return D3D_OK;}
struct State {
 Ref<IDirect3DVertexShader9> vs;Ref<IDirect3DPixelShader9> ps;Ref<IDirect3DVertexDeclaration9> decl;
 Ref<IDirect3DIndexBuffer9> ib;std::array<Ref<IDirect3DVertexBuffer9>,16> vb;std::array<UINT,16> offset{},stride{},freq{};
 std::array<Ref<IDirect3DBaseTexture9>,16> textures;std::array<Ref<IDirect3DSurface9>,4> targets;Ref<IDirect3DSurface9> depth;
 DWORD fvf=0,rs[256]={},sampler[16][16]={};D3DVIEWPORT9 viewport{};
 float vf[512][4]={},pf[256][4]={};int vi[32][4]={};BOOL vbconst[16]={};
};
struct Backend;
struct Block final:IDirect3DStateBlock9 {
 unsigned refs=1;Backend* owner;State saved;D3DSTATEBLOCKTYPE type;bool recorded=false;std::array<bool,256> rsMask{},fMask{};
 Block(Backend*,D3DSTATEBLOCKTYPE,bool=false);~Block();
 ULONG AddRef()override{return ++refs;}ULONG Release()override{auto r=--refs;if(!r)delete this;return r;}
 HRESULT QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=999)return E_NOINTERFACE;*out=static_cast<IDirect3DStateBlock9*>(this);AddRef();return S_OK;}
 HRESULT GetDevice(IDirect3DDevice9**)override;HRESULT Capture()override;HRESULT Apply()override;
};
struct Backend final:IDirect3DDevice9 {
 struct UpArguments {D3DPRIMITIVETYPE type=0;UINT minimum=0,vertices=0,count=0,stride=0;const void* indices=nullptr;const void* data=nullptr;D3DFORMAT format=0;} up;
 void(*onRenderState)(void*)=nullptr;void* callbackContext=nullptr;
 unsigned refs=1;State state,pending;bool recording=false,failNext=false,failGet=false,failApply=false,constantGetSupported=true;
 std::array<bool,256> rsMask{},fMask{};std::atomic<unsigned> entered{0};unsigned calls=0,gets=0,sets=0,applies=0,captures=0;size_t floatReadRegisters=0;
 struct Guard {Backend& d;explicit Guard(Backend& b):d(b){assert(d.entered.fetch_add(1)==0);++d.calls;}~Guard(){assert(d.entered.fetch_sub(1)==1);}};
 ULONG AddRef()override{return ++refs;}ULONG Release()override{assert(refs>1);return --refs;}
 State& edit(){return recording?pending:state;}HRESULT result(){bool bad=failNext;failNext=false;return bad?D3DERR_INVALIDCALL:D3D_OK;}
 bool pureDevice=false;bool badGet(){++gets;bool bad=failGet||pureDevice;failGet=false;return bad;}
#define SG Guard guard(*this);++sets
#define GG Guard guard(*this);if(badGet())return D3DERR_INVALIDCALL
#define RESOURCE(n,field,T) HRESULT Set##n(T* p)override{SG;edit().field=p;return result();} HRESULT Get##n(T** out)override{GG;return give(state.field.p,out);}
 RESOURCE(VertexShader,vs,IDirect3DVertexShader9)
 RESOURCE(PixelShader,ps,IDirect3DPixelShader9)
 RESOURCE(Indices,ib,IDirect3DIndexBuffer9)
#undef RESOURCE
 HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9* p)override{SG;edit().decl=p;edit().fvf=0;return result();}
 HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9** p)override{GG;return give(state.decl.p,p);}
 HRESULT SetFVF(DWORD v)override{SG;edit().fvf=v&0xffffu;edit().decl=nullptr;return result();}
 HRESULT GetFVF(DWORD* p)override{GG;if(!p)return D3DERR_INVALIDCALL;*p=state.fvf;return D3D_OK;}
 HRESULT SetStreamSource(UINT n,IDirect3DVertexBuffer9* p,UINT off,UINT stride)override{SG;if(n>=16)return D3DERR_INVALIDCALL;
  // Deliberately retain active buffer/offset on null, exercising read authority.
  if(p){edit().vb[n]=p;edit().offset[n]=off;edit().stride[n]=stride;}return result();}
 HRESULT GetStreamSource(UINT n,IDirect3DVertexBuffer9** p,UINT* off,UINT* stride)override{GG;if(n>=16||!p||!off||!stride)return D3DERR_INVALIDCALL;*off=state.offset[n];*stride=state.stride[n];return give(state.vb[n].p,p);}
 HRESULT SetStreamSourceFreq(UINT n,UINT f)override{SG;if(n>=16)return D3DERR_INVALIDCALL;edit().freq[n]=f?f:1;return result();}
 HRESULT GetStreamSourceFreq(UINT n,UINT* f)override{GG;if(n>=16||!f)return D3DERR_INVALIDCALL;*f=state.freq[n];return D3D_OK;}
 HRESULT SetTexture(DWORD n,IDirect3DBaseTexture9* p)override{SG;if(n>=16)return D3DERR_INVALIDCALL;edit().textures[n]=p;return result();}
 HRESULT GetTexture(DWORD n,IDirect3DBaseTexture9** p)override{GG;if(n>=16)return D3DERR_INVALIDCALL;return give(state.textures[n].p,p);}
 HRESULT SetRenderState(D3DRENDERSTATETYPE s,DWORD v)override{HRESULT hr;{SG;if(s>=256)return D3DERR_INVALIDCALL;edit().rs[s]=s==D3DRS_ALPHABLENDENABLE?DWORD(v!=0):v;if(recording)rsMask[s]=true;hr=result();}if(onRenderState){auto callback=onRenderState;onRenderState=nullptr;callback(callbackContext);}return hr;}
 HRESULT GetRenderState(D3DRENDERSTATETYPE s,DWORD* v)override{GG;if(s>=256||!v)return D3DERR_INVALIDCALL;*v=state.rs[s];return D3D_OK;}
 HRESULT SetSamplerState(DWORD n,D3DSAMPLERSTATETYPE s,DWORD v)override{SG;if(n>=16||s>=16)return D3DERR_INVALIDCALL;edit().sampler[n][s]=v&255;return result();}
 HRESULT GetSamplerState(DWORD n,D3DSAMPLERSTATETYPE s,DWORD* v)override{GG;if(n>=16||s>=16||!v)return D3DERR_INVALIDCALL;*v=state.sampler[n][s];return D3D_OK;}
 HRESULT SetViewport(const D3DVIEWPORT9* p)override{SG;if(!p)return D3DERR_INVALIDCALL;edit().viewport=*p;edit().viewport.Width&=0xffff;return result();}
 HRESULT GetViewport(D3DVIEWPORT9* p)override{GG;if(!p)return D3DERR_INVALIDCALL;*p=state.viewport;return D3D_OK;}
 HRESULT SetRenderTarget(DWORD n,IDirect3DSurface9* p)override{SG;if(n>=4)return D3DERR_INVALIDCALL;edit().targets[n]=p;if(!n)edit().viewport={0,0,800,600,0,1};return result();}
 HRESULT GetRenderTarget(DWORD n,IDirect3DSurface9** p)override{GG;if(n>=4||!p)return D3DERR_INVALIDCALL;HRESULT h=give(state.targets[n].p,p);return !state.targets[n].p?D3DERR_NOTFOUND:h;}
 HRESULT SetDepthStencilSurface(IDirect3DSurface9* p)override{SG;edit().depth=p;return result();}
 HRESULT GetDepthStencilSurface(IDirect3DSurface9** p)override{GG;if(!p)return D3DERR_INVALIDCALL;HRESULT h=give(state.depth.p,p);return !state.depth.p?D3DERR_NOTFOUND:h;}
#define CONSTANT(Name,Type,field,N,W) \
 HRESULT Set##Name(UINT first,const Type* data,UINT count)override{SG;if(!data||first>N||count>N-first)return D3DERR_INVALIDCALL;std::memcpy(reinterpret_cast<Type*>(edit().field)+first*W,data,count*W*sizeof(Type));if(recording&&std::is_same<Type,float>::value)for(unsigned i=first;i<first+count;++i)fMask[i]=true;return result();} \
 HRESULT Get##Name(UINT first,Type* data,UINT count)override{GG;if(!constantGetSupported||!data||first>N||count>N-first)return D3DERR_INVALIDCALL;std::memcpy(data,reinterpret_cast<Type*>(state.field)+first*W,count*W*sizeof(Type));if(std::is_same<Type,float>::value)floatReadRegisters+=count;return D3D_OK;}
 CONSTANT(VertexShaderConstantF,float,vf,512,4)
 CONSTANT(VertexShaderConstantI,int,vi,32,4)
 CONSTANT(PixelShaderConstantF,float,pf,256,4)
#undef CONSTANT
 HRESULT SetVertexShaderConstantB(UINT first,const WINBOOL* data,UINT count)override{SG;if(!data||first>16||count>16-first)return D3DERR_INVALIDCALL;for(unsigned i=0;i<count;++i)edit().vbconst[first+i]=data[i]?1:0;return result();}
 HRESULT GetVertexShaderConstantB(UINT first,WINBOOL* data,UINT count)override{GG;if(!data||first>16||count>16-first)return D3DERR_INVALIDCALL;std::memcpy(data,state.vbconst+first,count*sizeof(BOOL));return D3D_OK;}
 HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE t,IDirect3DStateBlock9** p)override{Guard g(*this);if(!p)return D3DERR_INVALIDCALL;*p=new Block(this,t);return D3D_OK;}
 HRESULT BeginStateBlock()override{Guard g(*this);if(recording)return D3DERR_INVALIDCALL;recording=true;pending=state;rsMask.fill(false);fMask.fill(false);return D3D_OK;}
 HRESULT EndStateBlock(IDirect3DStateBlock9** p)override{Guard g(*this);if(!recording||!p)return D3DERR_INVALIDCALL;*p=new Block(this,D3DSBT_ALL,true);recording=false;return D3D_OK;}
 HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT count,const void* data,UINT stride)override{SG;up={type,0,0,count,stride,nullptr,data,0};state.vb[0]=nullptr;state.offset[0]=state.stride[0]=0;return result();}
 HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type,UINT minimum,UINT vertices,UINT count,const void* indices,D3DFORMAT format,const void* data,UINT stride)override{SG;up={type,minimum,vertices,count,stride,indices,data,format};state.vb[0]=nullptr;state.offset[0]=state.stride[0]=0;state.ib=nullptr;return result();}
 HRESULT SetSoftwareVertexProcessing(WINBOOL)override{SG;state.rs[D3DRS_ZENABLE]=0;return result();}
 HRESULT Reset(D3DPRESENT_PARAMETERS*)override{SG;state=State{};recording=false;return result();}
#undef SG
#undef GG
};
Block::Block(Backend* b,D3DSTATEBLOCKTYPE t,bool rec):owner(b),saved(rec?b->pending:b->state),type(t),recorded(rec),rsMask(b->rsMask),fMask(b->fMask){owner->AddRef();++liveObjects;}
Block::~Block(){owner->Release();--liveObjects;}
HRESULT Block::GetDevice(IDirect3DDevice9** p){return give(static_cast<IDirect3DDevice9*>(owner),p);}
HRESULT Block::Capture(){Backend::Guard g(*owner);++owner->captures;saved=owner->state;return D3D_OK;}
HRESULT Block::Apply(){Backend::Guard g(*owner);++owner->applies;
 if(recorded){for(unsigned i=0;i<256;++i){if(rsMask[i])owner->state.rs[i]=saved.rs[i];if(fMask[i])std::memcpy(owner->state.vf[i],saved.vf[i],16);}}
 else if(type==D3DSBT_ALL){auto targets=owner->state.targets;auto depth=owner->state.depth;owner->state=saved;owner->state.targets=targets;owner->state.depth=depth;}
 else if(type==D3DSBT_PIXELSTATE)std::memcpy(owner->state.rs,saved.rs,sizeof saved.rs);
 else{owner->state.vs=saved.vs;std::memcpy(owner->state.vf,saved.vf,sizeof saved.vf);}
 bool fail=owner->failApply;owner->failApply=false;return fail?D3DERR_INVALIDCALL:D3D_OK;
}

template<class F>static void cached(Backend& b,F read){read();const unsigned calls=b.gets;read();assert(b.gets==calls);}
// 0.3.182 (D1): the owner's elided gate. A probe from another thread: does anyone hold the gate (an
// elided owner call or the mutex)?
static bool gateTaken(MirrorGate& g){if(g.inside.load(std::memory_order_seq_cst)||!g.mutex.try_lock())return true;g.mutex.unlock();return false;}
static unsigned long gateLocks(){return CheckedGateMutex::acquisitions.load();}
// Identity: one random single-thread sequence run elided, and locked (a foreign call pinned in flight
// makes every owner entry take the mutex). Answers, mirror and backend counters, backend state and the
// per-class entry counts are bit-equal; the elided run takes the mutex 0 times.
static std::vector<std::uint64_t> ownerSequence(bool locked,unsigned long& locks,IUnknown* const* objects){
 Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);MirrorGate& g=m.gate;std::mt19937 rng(18201);
 std::vector<std::uint64_t> trace;auto add=[&](std::uint64_t v){trace.push_back(v);};
 auto indexOf=[&](const void* p){for(unsigned i=0;i<3;++i)if(p==objects[i])return std::uint64_t(i+1);return std::uint64_t(p?99:0);};
 auto* shaders=reinterpret_cast<IDirect3DVertexShader9* const*>(objects);
 float values[2048],out[256*4];for(unsigned i=0;i<2048;++i){std::uint32_t v=rng();std::memcpy(values+i,&v,4);} /* a span starts at values+0..255 and reads up to 256 registers */
 IDirect3DStateBlock9* block=nullptr;assert(SUCCEEDED(game.CreateStateBlock(D3DSBT_ALL,&block)));
 g.counting=true;if(locked)g.foreignActive.store(1);const unsigned long before=gateLocks();
 for(unsigned n=0;n<6000;++n){
  const unsigned op=rng()%12;DWORD v=0;const unsigned state=rng()%64,first=rng()%256,count=1+rng()%(256-first);
  switch(op){
  case 0:add(std::uint64_t(game.SetRenderState(state,rng()%5)));break;
  case 1:add(std::uint64_t(ext.GetRenderState(state,&v)));add(v);break;
  case 2:add(std::uint64_t(game.SetVertexShaderConstantF(first,values+rng()%256,count)));break;
  case 3:{const HRESULT h=ext.GetVertexShaderConstantF(first,out,count);add(std::uint64_t(h));std::uint64_t hash=1469598103934665603ull;for(unsigned i=0;i<count*16;++i)hash=(hash^reinterpret_cast<unsigned char*>(out)[i])*1099511628211ull;add(hash);break;}
  case 4:add(std::uint64_t(game.SetVertexShader(rng()%4?shaders[rng()%3]:nullptr)));break;
  case 5:{IDirect3DVertexShader9* p=nullptr;add(std::uint64_t(ext.GetVertexShader(&p)));add(indexOf(p));if(p)p->Release();break;}
  case 6:add(std::uint64_t(block->Capture()));if(rng()%2)add(std::uint64_t(block->Apply()));break;
  case 7:{ExtensionDevice::RawScope scope(ext);add(std::uint64_t(ext.SetRenderState(state,n)));add(std::uint64_t(ext.GetRenderState(state,&v)));add(v);break;}
  case 8:{MirrorGuard outer(g);add(std::uint64_t(game.SetRenderState(state,n%3)));add(std::uint64_t(ext.GetRenderState(state,&v)));add(v);add(std::uint64_t(ext.GetVertexShaderConstantF(first,out,1)));break;}
  case 9:{NorthlightConstantEpoch::Stamp stamp;add(ext.constantStamp(stamp));break;}
  case 10:if(rng()%8==0){D3DPRESENT_PARAMETERS p;add(std::uint64_t(game.Reset(&p)));}break;
  case 11:{DWORD sampler=0;add(std::uint64_t(game.SetSamplerState(rng()%16,D3DSAMP_ADDRESSU,rng()%4)));add(std::uint64_t(ext.GetSamplerState(rng()%16,D3DSAMP_ADDRESSU,&sampler)));add(sampler);break;}
  }
 }
 locks=gateLocks()-before;if(locked)g.foreignActive.store(0);
 for(unsigned s=0;s<MirrorGate::Sites;++s)add(g.takeAcquired(MirrorSite(s)));
 for(std::uint64_t c:{m.answered,m.forwarded,m.writeThrough,m.invalidations,m.rawScopes,m.rawCalls,m.mutations,m.learnedSlots,m.distrustedSlots})add(c);
 for(unsigned c:{b.calls,b.gets,b.sets,b.applies,b.captures})add(c);add(b.floatReadRegisters);
 for(unsigned i=0;i<256;++i)add(b.state.rs[i]);for(unsigned i=0;i<16;++i)add(b.state.sampler[i][D3DSAMP_ADDRESSU]);add(indexOf(b.state.vs.p));
 for(unsigned i=0;i<256;++i){std::uint64_t w[2];std::memcpy(w,b.state.vf[i],16);add(w[0]);add(w[1]);}
 add(g.ownerLocked.load());add(!g.inside.load()&&!MirrorGuard::heldByThisThread(g));
 block->Release();return trace;
}
static void ownerIdentity(){
 IUnknown* objects[3]={new Object<IDirect3DVertexShader9>(),new Object<IDirect3DVertexShader9>(),new Object<IDirect3DVertexShader9>()};
 unsigned long elidedLocks=0,lockedLocks=0;
 auto elided=ownerSequence(false,elidedLocks,objects),locked=ownerSequence(true,lockedLocks,objects);
 assert(elidedLocks==0&&lockedLocks>1000);
 // Everything but the owner-locked fallback count (the last-but-one entry) is identical.
 assert(elided.size()==locked.size()&&elided[elided.size()-2]==0&&locked[locked.size()-2]==lockedLocks);
 elided[elided.size()-2]=locked[locked.size()-2];assert(elided==locked);
 for(IUnknown* o:objects)o->Release();
 std::printf("PASS owner identity: 6000 random calls elided vs locked, %zu bit-equal trace words; mutex acquisitions elided=%lu locked=%lu\n",elided.size(),elidedLocks,lockedLocks);
}
// Nesting: an elided outer guard; nested device, state-block, RawScope (and its raw bypass) and second-
// gate entries keep held and inside exact; the mutex is never taken; exceptions unwind the entry.
static void ownerNesting(bool exclusive=false){
 Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);MirrorGate& g=m.gate;if(exclusive)assert(g.setExclusive(true)&&g.exclusive); /* 0.3.192 (CS) */
 IDirect3DStateBlock9* block=nullptr;assert(SUCCEEDED(game.CreateStateBlock(D3DSBT_ALL,&block)));
 g.counting=true;for(unsigned s=0;s<MirrorGate::Sites;++s)g.takeAcquired(MirrorSite(s));const unsigned long locks=gateLocks();
 {MirrorGuard outer(g);assert(outer.elided()&&g.inside==1&&MirrorGuard::heldByThisThread(g));
  DWORD v=0;assert(SUCCEEDED(game.SetRenderState(40,1))&&SUCCEEDED(ext.GetRenderState(40,&v))&&v==1);assert(MirrorGuard::heldByThisThread(g));
  assert(SUCCEEDED(block->Capture())&&MirrorGuard::heldByThisThread(g));
  const auto rawCalls=m.rawCalls;{ExtensionDevice::RawScope scope(ext);assert(SUCCEEDED(ext.SetRenderState(40,2))&&m.rawCalls==rawCalls+1);assert(MirrorGuard::heldByThisThread(g)&&g.inside==1);}
  {MirrorGuard again(g);assert(!again.elided()&&MirrorGuard::heldByThisThread(g));}
  MirrorGate other;{MirrorGuard second(other);assert(second.elided()&&other.inside==1&&g.inside==1&&MirrorGuard::heldByThisThread(other));
   // A -> B -> A: the inner entry of the first gate keeps the outer elided entry (inside stays 1).
   {MirrorGuard back(g);assert(!back.elided()&&MirrorGuard::heldByThisThread(g)&&g.inside==1);DWORD w=0;assert(SUCCEEDED(ext.GetRenderState(40,&w)));}
   assert(g.inside==1&&MirrorGuard::heldByThisThread(other));}
  assert(other.inside==0&&MirrorGuard::heldByThisThread(g)&&g.inside==1);}
 assert(!MirrorGuard::heldByThisThread(g)&&g.inside==0&&gateLocks()==locks);
 assert(g.takeAcquired(MirrorSite::Device)==2);for(unsigned s=1;s<MirrorGate::Sites;++s)assert(!g.takeAcquired(MirrorSite(s))); // the outer and the A -> B -> A entry
 {ExtensionDevice::RawScope scope(ext);assert(MirrorGuard::heldByThisThread(g)&&g.inside==1);assert(SUCCEEDED(ext.SetRenderState(40,3)));}
 assert(!MirrorGuard::heldByThisThread(g)&&g.inside==0&&g.takeAcquired(MirrorSite::Raw)==1);
 try{MirrorGuard guard(g);assert(g.inside==1);throw std::runtime_error("unwind");}catch(const std::runtime_error&){}
 assert(!MirrorGuard::heldByThisThread(g)&&g.inside==0&&gateLocks()==locks&&g.ownerLocked==0);
 block->Release();
 std::puts("PASS owner nesting: elided outer guard; nested device, state-block, RawScope, raw bypass, second-gate and A -> B -> A entries; held and inside restored; mutex never taken; exceptions unwind.");
}
// A foreign call announced during an elided owner call waits until the owner's call returns; an owner
// call while a foreign one runs takes the mutex (ownerLocked) and waits; afterwards the owner elides
// again (not sticky). Each exit acts on its recorded mode: the owner's elided exit while a foreign call
// is waiting must not unlock the mutex it never took.
static void foreignWaits(bool exclusive=false){
 Backend b;DeviceMirror m;MirrorDevice game(&b,&m);MirrorGate& g=m.gate;if(exclusive)assert(g.setExclusive(true)&&g.exclusive); /* 0.3.192 (CS): the foreign call must still wait, then lock */
 std::atomic<bool> entered{false},holding{false},done{false};const unsigned long locks=gateLocks();
 std::thread foreign;
 {MirrorGuard outer(g);assert(outer.elided());
  foreign=std::thread([&]{MirrorGuard call(g);entered=true;DWORD v=0;assert(SUCCEEDED(game.GetRenderState(40,&v)));});
  // Until the foreign call is announced (or, in a broken build, has already entered), then a margin.
  while(!g.foreignActive.load()&&!entered)std::this_thread::yield();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  assert(!entered);assert(SUCCEEDED(game.SetRenderState(40,7)));assert(!entered);}
 foreign.join();assert(entered&&g.foreignActive==0);
 {MirrorGuard again(g);assert(again.elided());}
 foreign=std::thread([&]{MirrorGuard call(g);holding=true;std::this_thread::sleep_for(std::chrono::milliseconds(50));done=true;});
 while(!holding)std::this_thread::yield();
 const auto fallbacks=g.ownerLocked.load();
 {MirrorGuard owner(g);assert(!owner.elided()&&done&&g.ownerLocked==fallbacks+1);DWORD v=0;assert(SUCCEEDED(game.GetRenderState(40,&v))&&v==7);}
 foreign.join();
 const unsigned long after=gateLocks();{MirrorGuard again(g);assert(again.elided());DWORD v=0;assert(SUCCEEDED(game.GetRenderState(40,&v)));}assert(gateLocks()==after);
 assert(after==locks+3); // two foreign calls and one owner fallback
 assert(g.foreignExclusive==(exclusive?2u:0u)); // counted only in exclusive mode
 std::puts("PASS foreign waits: an announced foreign call waits for the elided owner call; an owner call during a foreign one locks and waits; elision resumes; exits act on the recorded mode.");
}
// Exclusion stress: the owner loops over elided calls of random length (outer guards with nested calls,
// and bare device calls) while 1-3 foreign threads call device, state-block, raw-scope and guarded
// bodies. A shared body counter never exceeds 1, the plain shared word is TSan's race target, and the
// backend's own guard asserts no two calls overlap. Afterwards the owner takes the mutex 0 times.
static void ownerStress(unsigned rounds,bool exclusive=false){
 unsigned long foreignTotal=0,fallbackTotal=0,elidedTotal=0;
 for(unsigned threads=1;threads<=3;++threads){
  Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);MirrorGate& g=m.gate;
  if(exclusive)assert(g.setExclusive(true));
  IDirect3DStateBlock9* block=nullptr;assert(SUCCEEDED(game.CreateStateBlock(D3DSBT_ALL,&block)));
  std::atomic<int> body{0};unsigned long shared=0,elided=0;std::atomic<bool> stop{false};std::atomic<unsigned long> foreignCalls{0};
  auto inBody=[&](unsigned spin){assert(body.fetch_add(1)==0);++shared;for(volatile unsigned i=0;i<spin;++i){}assert(body.fetch_sub(1)==1);};
  std::vector<std::thread> foreign;
  for(unsigned t=0;t<threads;++t)foreign.emplace_back([&,t]{std::mt19937 rng(t*7919+threads);
   while(!stop.load(std::memory_order_relaxed)){DWORD v=0;
    switch(rng()%5){
    case 0:{MirrorGuard call(g);inBody(rng()%32);assert(SUCCEEDED(game.SetRenderState(41+t,rng()%4)));break;}
    case 1:assert(SUCCEEDED(game.SetRenderState(41+t,rng()%4)));assert(SUCCEEDED(ext.GetRenderState(41+t,&v)));break;
    case 2:assert(SUCCEEDED(block->Capture()));break;
    case 3:{ExtensionDevice::RawScope scope(ext);inBody(rng()%8);assert(SUCCEEDED(ext.SetRenderState(45,rng()%4)));break;}
    case 4:{float c[4]={float(t),1,2,3},o[4];assert(SUCCEEDED(game.SetVertexShaderConstantF(8+t,c,1)));assert(SUCCEEDED(ext.GetVertexShaderConstantF(8+t,o,1)));break;}
    }
    foreignCalls.fetch_add(1,std::memory_order_relaxed);
    for(volatile unsigned i=0,gap=rng()%4096;i<gap;++i){} /* gaps: the owner alternates between elided and locked entries */}});
  std::mt19937 rng(threads);const auto fallbacks=g.ownerLocked.load();
  for(unsigned r=0;r<rounds;++r){DWORD v=0;
   if(r%3){MirrorGuard call(g);elided+=call.elided();inBody(rng()%64);assert(SUCCEEDED(game.SetRenderState(40,r%5))&&SUCCEEDED(ext.GetRenderState(40,&v))&&v==r%5);
    float c[4]={float(r),0,0,1},o[4];assert(SUCCEEDED(game.SetVertexShaderConstantF(4,c,1))&&SUCCEEDED(ext.GetVertexShaderConstantF(4,o,1))&&o[0]==float(r));}
   else{assert(SUCCEEDED(game.SetRenderState(39,r%7))&&SUCCEEDED(ext.GetRenderState(39,&v))&&v==r%7);}
   for(volatile unsigned i=0,gap=rng()%512;i<gap;++i){} /* owner work between calls, outside the gate */
  }
  while(foreignCalls.load()<threads*64)std::this_thread::yield();
  stop=true;for(auto& t:foreign)t.join();
  assert(g.foreignActive==0&&g.inside==0&&!b.entered);
  const unsigned long locks=gateLocks();
  for(unsigned r=0;r<1000;++r){{MirrorGuard call(g);assert(call.elided());inBody(0);}DWORD v=0;assert(SUCCEEDED(game.GetRenderState(40,&v)));}
  assert(gateLocks()==locks);
  assert(exclusive==(g.foreignExclusive>0)); /* every foreign entry under exclusive mode went through the barrier path */
  foreignTotal+=foreignCalls;fallbackTotal+=g.ownerLocked-fallbacks;elidedTotal+=elided;
  block->Release();
 }
 std::printf("PASS owner stress: %u owner rounds x 1-3 foreign threads, %lu foreign calls, %lu owner fallbacks to the mutex, %lu elided owner bodies; body never shared, owner back to 0 mutex acquisitions after the foreign threads stop.\n",rounds,foreignTotal,fallbackTotal,elidedTotal);
 assert(elidedTotal&&fallbackTotal&&foreignTotal);
}
static void bindingsAndScalars(){
 Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);
 static_assert(!std::is_copy_constructible<DeviceMirror>::value,"mutex/cache ownership must not copy");
 auto* vs=new Object<IDirect3DVertexShader9>();auto* ps=new Object<IDirect3DPixelShader9>();auto* dec=new Object<IDirect3DVertexDeclaration9>();
 auto* vb=new Object<IDirect3DVertexBuffer9>();auto* ib=new Object<IDirect3DIndexBuffer9>();auto* tex=new Object<IDirect3DBaseTexture9>();auto* surf=new Object<IDirect3DSurface9>();
 assert(SUCCEEDED(game.SetVertexShader(vs)));assert(vs->refs==2);cached(b,[&]{IDirect3DVertexShader9* p=nullptr;assert(SUCCEEDED(ext.GetVertexShader(&p))&&p==vs&&vs->refs==3);p->Release();});assert(vs->refs==2);
 assert(SUCCEEDED(game.SetPixelShader(ps)));cached(b,[&]{IDirect3DPixelShader9* p=nullptr;assert(SUCCEEDED(ext.GetPixelShader(&p))&&p==ps);p->Release();});
 assert(SUCCEEDED(game.SetVertexDeclaration(dec)));cached(b,[&]{IDirect3DVertexDeclaration9* p=nullptr;assert(SUCCEEDED(ext.GetVertexDeclaration(&p))&&p==dec);p->Release();});
 DWORD scalar=0;assert(SUCCEEDED(game.SetFVF(0x12345678)));assert(SUCCEEDED(ext.GetFVF(&scalar))&&scalar==0x5678);IDirect3DVertexDeclaration9* outDecl=nullptr;assert(SUCCEEDED(ext.GetVertexDeclaration(&outDecl))&&!outDecl);
 assert(SUCCEEDED(game.SetVertexDeclaration(dec)));assert(SUCCEEDED(ext.GetFVF(&scalar))&&!scalar);
 assert(SUCCEEDED(game.SetStreamSource(0,vb,12,28)));auto getStream=[&]{IDirect3DVertexBuffer9* p=nullptr;UINT off=0,stride=0;assert(SUCCEEDED(ext.GetStreamSource(0,&p,&off,&stride))&&p==vb&&off==12&&stride==28);p->Release();};cached(b,getStream);
 assert(SUCCEEDED(game.SetStreamSource(0,nullptr,900,999)));cached(b,getStream); // backend null-bind retention
 assert(SUCCEEDED(game.SetStreamSourceFreq(0,0)));cached(b,[&]{UINT n=0;assert(SUCCEEDED(ext.GetStreamSourceFreq(0,&n))&&n==1);});
 assert(SUCCEEDED(game.SetIndices(ib)));cached(b,[&]{IDirect3DIndexBuffer9* p=nullptr;assert(SUCCEEDED(ext.GetIndices(&p))&&p==ib);p->Release();});
 assert(SUCCEEDED(game.SetTexture(0,tex)));cached(b,[&]{IDirect3DBaseTexture9* p=nullptr;assert(SUCCEEDED(ext.GetTexture(0,&p))&&p==tex);p->Release();});
 assert(SUCCEEDED(game.SetRenderState(D3DRS_ALPHABLENDENABLE,99)));cached(b,[&]{DWORD n=0;assert(SUCCEEDED(ext.GetRenderState(D3DRS_ALPHABLENDENABLE,&n))&&n==1);});
 assert(SUCCEEDED(game.SetSamplerState(0,D3DSAMP_ADDRESSU,258)));cached(b,[&]{DWORD n=0;assert(SUCCEEDED(ext.GetSamplerState(0,D3DSAMP_ADDRESSU,&n))&&n==2);});
 D3DVIEWPORT9 vp={5,6,65539,8,0,1};assert(SUCCEEDED(game.SetViewport(&vp)));cached(b,[&]{D3DVIEWPORT9 v;assert(SUCCEEDED(ext.GetViewport(&v))&&v.X==5&&v.Width==3);});
 assert(SUCCEEDED(game.SetRenderTarget(1,surf)));assert(SUCCEEDED(ext.GetViewport(&vp))&&vp.Width==3); // only target0 resets it
 assert(SUCCEEDED(game.SetRenderTarget(0,surf)));assert(SUCCEEDED(ext.GetViewport(&vp))&&vp.Width==800);
 cached(b,[&]{IDirect3DSurface9* p=nullptr;assert(SUCCEEDED(ext.GetRenderTarget(0,&p))&&p==surf);p->Release();});
 for(unsigned j=0;j<2;++j){IDirect3DSurface9* p=surf;assert(ext.GetRenderTarget(3,&p)==D3DERR_NOTFOUND&&!p);}
 assert(SUCCEEDED(game.SetDepthStencilSurface(surf)));cached(b,[&]{IDirect3DSurface9* p=nullptr;assert(SUCCEEDED(ext.GetDepthStencilSurface(&p))&&p==surf);p->Release();});
 assert(SUCCEEDED(game.SetDepthStencilSurface(nullptr)));for(unsigned j=0;j<2;++j){IDirect3DSurface9* p=surf;assert(ext.GetDepthStencilSurface(&p)==D3DERR_NOTFOUND&&!p);}
 // Failure after mutation: refreshing must see the actual backend state.
 b.failNext=true;assert(FAILED(game.SetVertexShader(nullptr)));cached(b,[&]{IDirect3DVertexShader9* p=vs;assert(SUCCEEDED(ext.GetVertexShader(&p))&&!p);});assert(vs->refs==1);
 b.failNext=true;assert(FAILED(game.SetRenderState(D3DRS_ZENABLE,77)));assert(SUCCEEDED(ext.GetRenderState(D3DRS_ZENABLE,&scalar))&&scalar==77);
 assert(FAILED(ext.GetVertexShader(nullptr)));assert(FAILED(ext.GetRenderState(300,&scalar)));assert(FAILED(ext.GetSamplerState(16,0,&scalar)));
 assert(!ext.audit());assert(SUCCEEDED(game.SetRenderState(D3DRS_POINTSIZE,123)));assert(!m.viewportKnown);
 b.failGet=true;assert(FAILED(ext.GetViewport(&vp)));const auto reads=b.gets;assert(SUCCEEDED(ext.GetViewport(&vp))&&b.gets==reads+1);
 vs->Release();ps->Release();dec->Release();vb->Release();ib->Release();tex->Release();surf->Release();
}
static void constantBanks(){
 Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);std::mt19937 rng(12501);
 float values[1024],out[1024];int integers[64],outi[64];BOOL bools[16],outb[16];
 for(unsigned j=0;j<1024;++j){uint32_t v=rng();std::memcpy(values+j,&v,4);}uint32_t special[]={0,0x80000000,0x7fc01234,0x7f801234};std::memcpy(values,special,16);
 for(unsigned j=0;j<64;++j)integers[j]=int(rng());for(unsigned j=0;j<16;++j)bools[j]=j%3?7:0;
 assert(SUCCEEDED(game.SetVertexShaderConstantF(3,values,2)));unsigned reads=b.gets;assert(SUCCEEDED(ext.GetVertexShaderConstantF(3,out,2))&&b.gets==reads+1&&!std::memcmp(values,out,32));
 assert(SUCCEEDED(ext.GetVertexShaderConstantF(2,out,4))&&b.floatReadRegisters==6);cached(b,[&]{assert(SUCCEEDED(ext.GetVertexShaderConstantF(2,out,4)));});
 assert(SUCCEEDED(game.SetVertexShaderConstantI(4,integers,2)));reads=b.gets;assert(SUCCEEDED(ext.GetVertexShaderConstantI(4,outi,2))&&b.gets==reads+1&&!std::memcmp(integers,outi,32));
 assert(SUCCEEDED(game.SetVertexShaderConstantB(1,bools,7)));reads=b.gets;assert(SUCCEEDED(ext.GetVertexShaderConstantB(1,outb,7))&&b.gets==reads+1);for(unsigned i=0;i<7;++i)assert(outb[i]==(bools[i]!=0));
 cached(b,[&]{assert(SUCCEEDED(ext.GetVertexShaderConstantB(1,outb,7)));});
 assert(SUCCEEDED(game.SetPixelShaderConstantF(220,values,4)));reads=b.gets;assert(SUCCEEDED(ext.GetPixelShaderConstantF(220,out,4))&&b.gets==reads+1&&!std::memcmp(values,out,64));
 b.failNext=true;assert(FAILED(game.SetVertexShaderConstantF(3,values+20,2)));reads=b.gets;assert(SUCCEEDED(ext.GetVertexShaderConstantF(3,out,2))&&b.gets==reads+1&&!std::memcmp(values+20,out,32));
 assert(FAILED(ext.GetVertexShaderConstantF(511,out,2)));assert(FAILED(ext.GetVertexShaderConstantF(0,nullptr,1)));
 assert(SUCCEEDED(ext.GetVertexShaderConstantF(256,out,0)));assert(FAILED(game.SetPixelShaderConstantF(256,values,1)));
 for(unsigned draw=0;draw<30000;++draw){unsigned first=rng()%256,count=1+rng()%(256-first);unsigned mode=rng()%5;
  if(draw%193==0){D3DPRESENT_PARAMETERS p;assert(SUCCEEDED(game.Reset(&p)));}
  if(mode<3){uint32_t raw=rng();std::memcpy(values+rng()%1024,&raw,4);b.failNext=draw%101==0;const HRESULT h=game.SetVertexShaderConstantF(first,values,count);assert((FAILED(h))==(draw%101==0));}
  const auto readFirst=rng()%256,readCount=1+rng()%(256-readFirst);assert(SUCCEEDED(ext.GetVertexShaderConstantF(readFirst,out,readCount)));assert(!std::memcmp(out,b.state.vf[readFirst],readCount*16));
 }
}
static void constantCapabilities(){
 Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);
 float values[16],out[16];int iv[16],io[16];for(unsigned i=0;i<16;++i){values[i]=float(i+100);iv[i]=int(i+300);}
 b.constantGetSupported=false;
 assert(FAILED(ext.GetVertexShaderConstantF(20,out,1)));assert(SUCCEEDED(game.SetVertexShaderConstantF(20,values,1)));
 const unsigned reads=b.gets;assert(FAILED(ext.GetVertexShaderConstantF(20,out,1))&&b.gets==reads+1&&!m.vsFloatKnown[20]);
 assert(FAILED(ext.GetVertexShaderConstantI(3,io,1)));assert(SUCCEEDED(game.SetVertexShaderConstantI(3,iv,1)));assert(FAILED(ext.GetVertexShaderConstantI(3,io,1))&&!m.vsIntKnown[3]);
 assert(SUCCEEDED(game.SetPixelShaderConstantF(20,values,1)));assert(FAILED(ext.GetPixelShaderConstantF(20,out,1))&&!m.psFloatKnown[20]);
 b.constantGetSupported=true;
 // Backend accepts larger software-processing banks; a write crossing the
 // mirror boundary must update the already-known overlapping prefix.
 assert(SUCCEEDED(ext.GetVertexShaderConstantF(255,out,1)));assert(SUCCEEDED(game.SetVertexShaderConstantF(254,values,4)));assert(SUCCEEDED(ext.GetVertexShaderConstantF(255,out,1))&&!std::memcmp(out,values+4,16));
 assert(SUCCEEDED(ext.GetVertexShaderConstantI(15,io,1)));assert(SUCCEEDED(game.SetVertexShaderConstantI(14,iv,4)));assert(SUCCEEDED(ext.GetVertexShaderConstantI(15,io,1))&&!std::memcmp(io,iv+4,16));
 assert(SUCCEEDED(ext.GetPixelShaderConstantF(223,out,1)));assert(SUCCEEDED(game.SetPixelShaderConstantF(222,values,4)));assert(SUCCEEDED(ext.GetPixelShaderConstantF(223,out,1))&&!std::memcmp(out,values+4,16));
}
static void stateBlocks(){
 Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);DWORD value=0;float original[4]={1,2,3,4},later[4]={5,6,7,8},out[4];
 assert(SUCCEEDED(game.SetRenderState(D3DRS_ZENABLE,11)));assert(SUCCEEDED(game.SetVertexShaderConstantF(6,original,1)));assert(SUCCEEDED(ext.GetRenderState(D3DRS_ZENABLE,&value)));
 auto* targetA=new Object<IDirect3DSurface9>();auto* targetB=new Object<IDirect3DSurface9>();
 assert(SUCCEEDED(game.SetRenderTarget(0,targetA)));assert(SUCCEEDED(game.SetDepthStencilSurface(targetA)));
 IDirect3DStateBlock9* all=nullptr;assert(SUCCEEDED(game.CreateStateBlock(D3DSBT_ALL,&all))&&all);IDirect3DDevice9* device=nullptr;assert(SUCCEEDED(all->GetDevice(&device))&&device==&game);device->Release();
 assert(SUCCEEDED(game.SetRenderState(D3DRS_ZENABLE,22)));assert(SUCCEEDED(all->Capture()));assert(SUCCEEDED(game.SetRenderState(D3DRS_ZENABLE,33)));assert(SUCCEEDED(game.SetVertexShaderConstantF(6,later,1)));
 assert(SUCCEEDED(game.SetRenderTarget(0,targetB)));assert(SUCCEEDED(game.SetDepthStencilSurface(targetB)));
 void* queried=nullptr;assert(SUCCEEDED(all->QueryInterface(iid_IDirect3DStateBlock9,&queried))&&queried==all);static_cast<IDirect3DStateBlock9*>(queried)->Release();
 auto inv=m.invalidations;assert(SUCCEEDED(all->Apply())&&m.invalidations>inv);assert(SUCCEEDED(ext.GetRenderState(D3DRS_ZENABLE,&value))&&value==22);assert(SUCCEEDED(ext.GetVertexShaderConstantF(6,out,1))&&!std::memcmp(out,original,16));
 assert(SUCCEEDED(game.SetRenderState(D3DRS_ZENABLE,44)));b.failApply=true;inv=m.invalidations;assert(FAILED(all->Apply())&&m.invalidations>inv);assert(SUCCEEDED(ext.GetRenderState(D3DRS_ZENABLE,&value))&&value==22);IDirect3DSurface9* returned=nullptr;assert(SUCCEEDED(ext.GetRenderTarget(0,&returned))&&returned==targetB);returned->Release();assert(SUCCEEDED(ext.GetDepthStencilSurface(&returned))&&returned==targetB);returned->Release();all->Release();targetA->Release();targetB->Release();
 for(auto type:{D3DSBT_PIXELSTATE,D3DSBT_VERTEXSTATE}){IDirect3DStateBlock9* p=nullptr;assert(SUCCEEDED(game.CreateStateBlock(type,&p)));assert(SUCCEEDED(game.SetRenderState(D3DRS_ZENABLE,91)));inv=m.invalidations;assert(SUCCEEDED(p->Apply())&&m.invalidations>inv);assert(SUCCEEDED(ext.GetRenderState(D3DRS_ZENABLE,&value))&&value==b.state.rs[D3DRS_ZENABLE]);p->Release();}
 assert(SUCCEEDED(game.SetRenderState(D3DRS_ZENABLE,55)));assert(SUCCEEDED(game.BeginStateBlock())&&m.recording);
 assert(SUCCEEDED(game.SetRenderState(D3DRS_ZENABLE,66)));assert(SUCCEEDED(game.SetVertexShaderConstantF(6,later,1)));
 unsigned gets=b.gets;assert(SUCCEEDED(ext.GetRenderState(D3DRS_ZENABLE,&value))&&value==55);assert(SUCCEEDED(ext.GetRenderState(D3DRS_ZENABLE,&value))&&value==55&&b.gets==gets+2);assert(SUCCEEDED(ext.GetVertexShaderConstantF(6,out,1))&&!std::memcmp(out,original,16));
 IDirect3DStateBlock9* recorded=nullptr;assert(SUCCEEDED(game.EndStateBlock(&recorded))&&!m.recording);
 assert(SUCCEEDED(ext.GetRenderState(D3DRS_ZENABLE,&value))&&value==55);assert(SUCCEEDED(recorded->Apply()));assert(SUCCEEDED(ext.GetRenderState(D3DRS_ZENABLE,&value))&&value==66);assert(SUCCEEDED(ext.GetVertexShaderConstantF(6,out,1))&&!std::memcmp(out,later,16));recorded->Release();
 assert(b.refs==1);
 IDirect3DStateBlock9* escape=nullptr;assert(SUCCEEDED(game.CreateStateBlock(D3DSBT_ALL,&escape)));void* unknown=nullptr;assert(FAILED(escape->QueryInterface(998,&unknown))&&m.enabled&&!unknown);assert(SUCCEEDED(escape->QueryInterface(999,&unknown))&&!m.enabled&&unknown);static_cast<IDirect3DStateBlock9*>(unknown)->Release();escape->Release();assert(b.refs==1);
}
static void implicitAndDisabled(){
 Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);
 auto* vb=new Object<IDirect3DVertexBuffer9>();auto* ib=new Object<IDirect3DIndexBuffer9>();
 for(bool indexed:{false,true})for(bool fail:{false,true}){
  assert(SUCCEEDED(game.SetStreamSource(0,vb,8,16)));assert(SUCCEEDED(game.SetIndices(ib)));IDirect3DVertexBuffer9* v=nullptr;IDirect3DIndexBuffer9* i=nullptr;UINT off,stride;assert(SUCCEEDED(ext.GetStreamSource(0,&v,&off,&stride)));v->Release();assert(SUCCEEDED(ext.GetIndices(&i)));i->Release();
  b.failNext=fail;HRESULT hr=indexed?game.DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,3,1,nullptr,D3DFMT_INDEX16,nullptr,16):game.DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,nullptr,16);assert(FAILED(hr)==fail);
  assert(SUCCEEDED(ext.GetStreamSource(0,&v,&off,&stride))&&!v&&!off&&!stride);assert(SUCCEEDED(ext.GetIndices(&i))&&(indexed?!i:i==ib));if(i)i->Release();
 }
 DWORD value=0;assert(SUCCEEDED(game.SetRenderState(D3DRS_ZENABLE,88)));assert(SUCCEEDED(ext.GetRenderState(D3DRS_ZENABLE,&value)));b.failNext=true;D3DPRESENT_PARAMETERS params;assert(FAILED(game.Reset(&params)));assert(SUCCEEDED(ext.GetRenderState(D3DRS_ZENABLE,&value))&&!value);
 assert(SUCCEEDED(game.SetRenderState(D3DRS_ZENABLE,9)));assert(SUCCEEDED(ext.GetRenderState(D3DRS_ZENABLE,&value)));assert(SUCCEEDED(game.SetSoftwareVertexProcessing(1)));assert(SUCCEEDED(ext.GetRenderState(D3DRS_ZENABLE,&value))&&!value);
 m.disable("test-raw-escape");assert(!m.enabled);assert(SUCCEEDED(b.SetRenderState(D3DRS_ZENABLE,101)));unsigned reads=b.gets;assert(SUCCEEDED(ext.GetRenderState(D3DRS_ZENABLE,&value))&&value==101);assert(SUCCEEDED(b.SetRenderState(D3DRS_ZENABLE,102)));assert(SUCCEEDED(ext.GetRenderState(D3DRS_ZENABLE,&value))&&value==102&&b.gets==reads+2);
 assert(SUCCEEDED(game.Reset(&params))&&!m.enabled);m.disable("second-reason");assert(!std::strcmp(m.disableReason,"test-raw-escape"));vb->Release();ib->Release();
}
static void concurrency(){
 Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);std::vector<std::thread> threads;
 for(unsigned t=0;t<8;++t)threads.emplace_back([&,t]{for(unsigned n=0;n<5000;++n){float in[4]={float(n),float(t),-0.f,1},out[4];assert(SUCCEEDED(game.SetVertexShaderConstantF(t,in,1)));assert(SUCCEEDED(ext.GetVertexShaderConstantF(t,out,1))&&!std::memcmp(in,out,16));assert(SUCCEEDED(game.SetRenderState(40+t,n%4)));DWORD v=0;assert(SUCCEEDED(ext.GetRenderState(40+t,&v))&&v==n%4);}});
 for(auto& t:threads)t.join();assert(!b.entered&&m.enabled);
 // Setters + first authoritative float read; scalar reads forward until the slot has proven TrustAfter exact round trips.
 if(kMirrorWriteThrough)assert(m.answered==8*(4999+5000-DeviceMirror::TrustAfter)&&b.calls==8*(10000+1+DeviceMirror::TrustAfter)&&m.writeThrough==8*(5000-DeviceMirror::TrustAfter));
 else assert(m.answered>=39992&&b.calls==120008);
}

static void recordingFailures(){
 for(unsigned mode=0;mode<3;++mode){Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);IDirect3DStateBlock9* block=nullptr;
  if(mode==0){assert(SUCCEEDED(game.BeginStateBlock()));assert(FAILED(game.BeginStateBlock())&&!m.enabled);assert(SUCCEEDED(game.EndStateBlock(&block)));block->Release();}
  else if(mode==1){assert(SUCCEEDED(game.BeginStateBlock()));assert(FAILED(game.EndStateBlock(nullptr))&&!m.enabled);assert(SUCCEEDED(game.EndStateBlock(&block)));block->Release();}
  else assert(FAILED(game.EndStateBlock(&block))&&!m.enabled);
  assert(SUCCEEDED(b.SetRenderState(40,7)));DWORD value=0;auto reads=b.gets;assert(SUCCEEDED(ext.GetRenderState(40,&value))&&value==7);assert(SUCCEEDED(ext.GetRenderState(40,&value))&&b.gets==reads+2);
 }
}
static void savedStateIntegration(){
 Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);auto* target=new Object<IDirect3DSurface9>();assert(SUCCEEDED(game.SetRenderTarget(0,target)));target->Release();
 NorthlightStateBlockPool pool(&ext);D3DVIEWPORT9 original={12,14,512,256,.1f,.9f};assert(SUCCEEDED(game.SetViewport(&original)));
 const auto state=[&](DWORD expected){DWORD v=0;assert(SUCCEEDED(ext.GetRenderState(40,&v))&&v==expected);};
 assert(SUCCEEDED(game.SetRenderState(40,10)));state(10);
 {SavedState outer(&ext,&pool);assert(outer.ok);assert(SUCCEEDED(ext.SetRenderState(40,20)));state(20);
  {SavedState inner(&ext,&pool);assert(inner.ok);assert(SUCCEEDED(ext.SetRenderState(40,30)));state(30);pool.clear();}
  state(20);assert(SUCCEEDED(ext.SetRenderState(40,40)));state(40);
 }
 state(10);D3DVIEWPORT9 actual;assert(SUCCEEDED(ext.GetViewport(&actual))&&!std::memcmp(&actual,&original,sizeof actual));
 // A raw-device pool must never supply a block to an extension-device scope:
 // its unwrapped Apply would restore real state but leave a warmed mirror stale.
 NorthlightStateBlockPool wrongPool(&b);
 {SavedState save(&ext,&wrongPool);assert(save.ok&&save.pool==nullptr);assert(SUCCEEDED(ext.SetRenderState(40,70)));state(70);}
 state(10);
 // Overflow nesting and cleared active leases must release every wrapped block.
 {std::vector<std::unique_ptr<SavedState>> nested;for(unsigned i=0;i<7;++i){nested.emplace_back(new SavedState(&ext,&pool));assert(nested.back()->ok);assert(SUCCEEDED(ext.SetRenderState(40,100+i)));state(100+i);}while(!nested.empty())nested.pop_back();}
 state(10);pool.clear();wrongPool.clear();assert(b.refs==1);
}
struct DrawState {
 IDirect3DVertexShader9* shader;IDirect3DVertexBuffer9* vb;IDirect3DIndexBuffer9* ib;IDirect3DBaseTexture9* texture;
 UINT offset,stride,frequency;DWORD blend,alpha,samplerU,samplerV;D3DVIEWPORT9 viewport;float projection[16],constants[1024];
};
struct Workload {unsigned getters=0,calls=0;uint64_t ns=0;std::vector<DrawState> draws;};
static Workload workload(Backend& backend,IDirect3DDevice9& game,IDirect3DDevice9& query){
 auto* shaderA=new Object<IDirect3DVertexShader9>();auto* shaderB=new Object<IDirect3DVertexShader9>();auto* vb=new Object<IDirect3DVertexBuffer9>();auto* ib=new Object<IDirect3DIndexBuffer9>();auto* texture=new Object<IDirect3DBaseTexture9>();
 assert(SUCCEEDED(game.SetStreamSource(0,vb,0,32)));assert(SUCCEEDED(game.SetIndices(ib)));assert(SUCCEEDED(game.SetTexture(0,texture)));assert(SUCCEEDED(game.SetStreamSourceFreq(0,1)));
 assert(SUCCEEDED(game.SetSamplerState(0,D3DSAMP_ADDRESSU,1)));assert(SUCCEEDED(game.SetSamplerState(0,D3DSAMP_ADDRESSV,2)));D3DVIEWPORT9 viewport={0,0,1920,1080,0,1};assert(SUCCEEDED(game.SetViewport(&viewport)));
 Workload result;result.draws.reserve(3000);const auto begin=std::chrono::steady_clock::now();
 for(unsigned n=0;n<3000;++n){
  if(n%37==0)assert(SUCCEEDED(game.SetVertexShader((n/37)%2?shaderB:shaderA)));
  if(n%17==0)assert(SUCCEEDED(game.SetStreamSource(0,vb,n%64,32)));
  if(n%11==0)assert(SUCCEEDED(game.SetRenderState(D3DRS_ALPHABLENDENABLE,(n/11)%2)));
  if(n%29==0)assert(SUCCEEDED(game.SetRenderState(D3DRS_ALPHATESTENABLE,(n/29)%2)));
  if(n%5==0){float four[16];for(unsigned i=0;i<16;++i)four[i]=float(n+i);assert(SUCCEEDED(game.SetVertexShaderConstantF(10+(n%60),four,4)));}
  DrawState draw;std::memset(&draw,0,sizeof draw);
  assert(SUCCEEDED(query.GetVertexShader(&draw.shader)));assert(SUCCEEDED(query.GetStreamSource(0,&draw.vb,&draw.offset,&draw.stride)));assert(SUCCEEDED(query.GetStreamSourceFreq(0,&draw.frequency)));assert(SUCCEEDED(query.GetIndices(&draw.ib)));assert(SUCCEEDED(query.GetTexture(0,&draw.texture)));
  assert(SUCCEEDED(query.GetRenderState(D3DRS_ALPHABLENDENABLE,&draw.blend)));assert(SUCCEEDED(query.GetRenderState(D3DRS_ALPHATESTENABLE,&draw.alpha)));assert(SUCCEEDED(query.GetSamplerState(0,D3DSAMP_ADDRESSU,&draw.samplerU)));assert(SUCCEEDED(query.GetSamplerState(0,D3DSAMP_ADDRESSV,&draw.samplerV)));assert(SUCCEEDED(query.GetViewport(&draw.viewport)));
  assert(SUCCEEDED(query.GetVertexShaderConstantF(4,draw.projection,4)));assert(SUCCEEDED(query.GetVertexShaderConstantF(0,draw.constants,256)));assert(!std::memcmp(draw.constants,backend.state.vf,sizeof draw.constants));
  // Normalize resource identities to deterministic slots before comparing runs.
  auto* actualShader=draw.shader;assert(actualShader==shaderA||actualShader==shaderB);assert(draw.vb==vb&&draw.ib==ib&&draw.texture==texture);
  draw.shader->Release();draw.vb->Release();draw.ib->Release();draw.texture->Release();draw.shader=reinterpret_cast<IDirect3DVertexShader9*>(actualShader==shaderA?1:2);draw.vb=nullptr;draw.ib=nullptr;draw.texture=nullptr;result.draws.push_back(draw);
 }
 result.ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();result.getters=backend.gets;result.calls=backend.calls;
 shaderA->Release();shaderB->Release();vb->Release();ib->Release();texture->Release();return result;
}
static void auditChecks(){
 Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);DWORD v=0;float f[4]={1,2,3,4},out[4];
 assert(SUCCEEDED(game.SetRenderState(40,123)));assert(SUCCEEDED(ext.GetRenderState(40,&v)));assert(SUCCEEDED(game.SetVertexShaderConstantF(30,f,1)));
 assert(SUCCEEDED(ext.GetVertexShaderConstantF(30,out,1)));const auto answered=m.answered,forwarded=m.forwarded;assert(!ext.audit()&&m.checks==1&&!m.mismatches&&m.enabled);assert(m.answered==answered&&m.forwarded==forwarded);
 assert(SUCCEEDED(b.SetRenderState(40,124)));const char* field=ext.audit();assert(field&&!std::strcmp(field,"renderState")&&m.checks==2&&m.mismatches==1&&!m.enabled);assert(SUCCEEDED(ext.GetRenderState(40,&v))&&v==124);
 auto reads=b.gets;assert(!ext.audit()&&b.gets==reads&&m.checks==2);
 Backend other;DeviceMirror constants;MirrorDevice game2(&other,&constants);ExtensionDevice ext2(&other,&constants);
 assert(SUCCEEDED(game2.SetVertexShaderConstantF(30,f,1)));assert(SUCCEEDED(ext2.GetVertexShaderConstantF(30,out,1)));f[2]=9;assert(SUCCEEDED(other.SetVertexShaderConstantF(30,f,1)));field=ext2.audit();assert(field&&!std::strcmp(field,"vsFloat")&&!constants.enabled&&constants.mismatches==1);assert(SUCCEEDED(ext2.GetVertexShaderConstantF(30,out,1))&&!std::memcmp(f,out,16));
}
static void auditEveryField(){
 const char* fields[]={"vertexShader","pixelShader","declaration","indices","fvf","viewport","stream","frequency","texture","sampler","renderState","target","depth","vsFloat","vsInt","vsBool","psFloat"};
 for(unsigned which=0;which<17;++which){
  Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);
  const auto fresh=[](auto& r){using T=typename std::remove_pointer<decltype(r.p)>::type;auto* p=new Object<T>();r=p;p->Release();};
  fresh(b.state.targets[0]);fresh(b.state.depth);
  IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;IDirect3DVertexDeclaration9* decl=nullptr;IDirect3DIndexBuffer9* ib=nullptr;
  assert(SUCCEEDED(ext.GetVertexShader(&vs))&&!vs);assert(SUCCEEDED(ext.GetPixelShader(&ps))&&!ps);assert(SUCCEEDED(ext.GetVertexDeclaration(&decl))&&!decl);assert(SUCCEEDED(ext.GetIndices(&ib))&&!ib);
  DWORD value;D3DVIEWPORT9 vp;assert(SUCCEEDED(ext.GetFVF(&value)));assert(SUCCEEDED(ext.GetViewport(&vp)));
  IDirect3DVertexBuffer9* vb=nullptr;UINT offset,stride;assert(SUCCEEDED(ext.GetStreamSource(3,&vb,&offset,&stride))&&!vb);assert(SUCCEEDED(ext.GetStreamSourceFreq(3,&stride)));
  IDirect3DBaseTexture9* texture=nullptr;assert(SUCCEEDED(ext.GetTexture(2,&texture))&&!texture);assert(SUCCEEDED(ext.GetSamplerState(2,D3DSAMP_ADDRESSU,&value)));assert(SUCCEEDED(ext.GetRenderState(40,&value)));
  IDirect3DSurface9* surface=nullptr;assert(SUCCEEDED(ext.GetRenderTarget(0,&surface)));surface->Release();assert(SUCCEEDED(ext.GetDepthStencilSurface(&surface)));surface->Release();
  float f[1024];int i[64];BOOL booleans[16];assert(SUCCEEDED(ext.GetVertexShaderConstantF(0,f,256)));assert(SUCCEEDED(ext.GetVertexShaderConstantI(0,i,16)));assert(SUCCEEDED(ext.GetVertexShaderConstantB(0,booleans,16)));assert(SUCCEEDED(ext.GetPixelShaderConstantF(0,f,224)));
  assert(!ext.audit()&&m.checks==1&&!m.mismatches&&m.enabled);
  assert(m.checkedFields==17&&m.checkedRegisters==512&&m.emptyChecks==0);
  // Deliberate backend-only mutations represent a missed interception, and
  // each family must independently trip its diagnostic fail-open boundary.
  switch(which){case 0:fresh(b.state.vs);break;case 1:fresh(b.state.ps);break;case 2:fresh(b.state.decl);break;case 3:fresh(b.state.ib);break;
   case 4:++b.state.fvf;break;case 5:++b.state.viewport.Width;break;case 6:++b.state.offset[3];break;case 7:++b.state.freq[3];break;case 8:fresh(b.state.textures[2]);break;
   case 9:++b.state.sampler[2][D3DSAMP_ADDRESSU];break;case 10:++b.state.rs[40];break;case 11:fresh(b.state.targets[0]);break;case 12:fresh(b.state.depth);break;
   case 13:b.state.vf[255][3]=-0.f;break;case 14:b.state.vi[15][3]=7;break;case 15:b.state.vbconst[15]=1;break;case 16:b.state.pf[223][3]=-0.f;break;}
  const char* field=ext.audit();assert(field&&!std::strcmp(field,fields[which])&&!m.enabled&&m.checks==2&&m.mismatches==1);
  auto calls=b.calls;assert(!ext.audit()&&b.calls==calls);
 }
}
static void constantCertificates(){
 using namespace NorthlightConstantEpoch;
 // Exhaust every legal float interval against an independent per-register oracle.
 for(unsigned first=0;first<256;++first)for(unsigned count=0;count<=256-first;++count){
  Clock clock;auto before=clock.stamp(true);clock.writeFloat(first,count);auto after=clock.stamp(true);
  bool outside2=false,outside4=false;
  for(unsigned reg=first;reg<first+count;++reg){outside2|=reg<2||reg>=6;outside4|=reg<4||reg>=8;}
  assert(exact(before,after)==(count==0));
  assert(pose(before,after,1)==!outside4&&pose(before,after,2)==!outside2);
  assert(!pose(before,after,0)&&!pose(before,after,3));
  // Block serials: exactly the touched 16-register blocks change.
  for(unsigned block=0;block<FloatBlocks;++block){bool touched=false;
   for(unsigned reg=first;reg<first+count;++reg)touched|=reg/BlockRegisters==block;
   assert((before.floatBlock[block]!=after.floatBlock[block])==touched);}
  unsigned dirtyFirst=0,dirtyLast=0;dirtySpan(before,after,0,256,dirtyFirst,dirtyLast);
  assert(count?dirtyFirst==first/16*16&&dirtyLast==(first+count+15)/16*16:dirtyFirst==dirtyLast);
 }
 Clock boundary;auto before=boundary.stamp(true);boundary.writeFloat(~0u,4);
 assert(!exact(before,boundary.stamp(true))&&!pose(before,boundary.stamp(true),1)&&!pose(before,boundary.stamp(true),2));
 {unsigned a=0,z=0;dirtySpan(before,boundary.stamp(true),0,256,a,z);assert(a==z);} // SWVP-only registers
 boundary.writeFloat(250,20);{unsigned a=0,z=0;dirtySpan(before,boundary.stamp(true),0,256,a,z);assert(a==240&&z==256);}
 Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);
 auto stamp=[&]{Stamp s;assert(ext.constantStamp(s));return s;};
 float f[32]={};int i[4]={};BOOL boolean=7;float out[16];
 before=stamp();assert(SUCCEEDED(game.SetPixelShaderConstantF(0,f,1)));assert(exact(before,stamp()));
 assert(SUCCEEDED(game.SetVertexShader(nullptr)));assert(SUCCEEDED(game.SetRenderState(40,9)));assert(exact(before,stamp()));
 assert(SUCCEEDED(ext.GetVertexShaderConstantF(2,out,4)));assert(exact(before,stamp()));
 assert(SUCCEEDED(game.SetVertexShaderConstantF(2,f,4)));auto after=stamp();
 assert(!exact(before,after)&&pose(before,after,2)&&!pose(before,after,1));
 before=after;assert(SUCCEEDED(ext.SetVertexShaderConstantF(4,f,4)));after=stamp();
 assert(!exact(before,after)&&pose(before,after,1)&&!pose(before,after,2));
 before=after;assert(SUCCEEDED(game.SetVertexShaderConstantF(4,f,0)));assert(exact(before,stamp()));
 assert(SUCCEEDED(game.SetVertexShaderConstantI(0,i,1)));after=stamp();
 assert(!exact(before,after)&&!pose(before,after,1)&&!pose(before,after,2));
 before=after;assert(SUCCEEDED(game.SetVertexShaderConstantB(0,&boolean,1)));after=stamp();
 assert(!exact(before,after)&&!pose(before,after,1)&&!pose(before,after,2));
 // Even identical writes must advance: serial equality proves no intervening setter.
 before=after;assert(SUCCEEDED(game.SetVertexShaderConstantB(0,&boolean,1)));assert(!exact(before,stamp()));
 before=stamp();b.failNext=true;assert(FAILED(game.SetVertexShaderConstantF(0,f,1)));assert(before.reset!=stamp().reset);
 before=stamp();IDirect3DStateBlock9* block=nullptr;
 assert(SUCCEEDED(game.CreateStateBlock(D3DSBT_ALL,&block)));assert(exact(before,stamp()));
 assert(SUCCEEDED(block->Apply()));assert(before.reset!=stamp().reset);
 before=stamp();b.failApply=true;assert(FAILED(block->Apply()));assert(before.reset!=stamp().reset);block->Release();
 before=stamp();assert(SUCCEEDED(game.BeginStateBlock()));Stamp inactive;assert(!ext.constantStamp(inactive)&&!inactive.valid);
 assert(SUCCEEDED(game.SetVertexShaderConstantF(0,f,1)));assert(!ext.constantStamp(inactive));
 assert(SUCCEEDED(game.EndStateBlock(&block)));after=stamp();assert(after.reset!=before.reset);block->Release();
 before=after;D3DPRESENT_PARAMETERS params{};assert(SUCCEEDED(game.Reset(&params)));assert(before.reset!=stamp().reset);
 before=stamp();assert(SUCCEEDED(game.SetSoftwareVertexProcessing(TRUE)));assert(before.reset!=stamp().reset);
 before=stamp();m.invalidate();assert(!exact(before,stamp()));
 m.disable("test-escape");assert(!ext.constantStamp(inactive)&&!exact(before,inactive));
 std::puts("PASS constant certificates: exhaustive 33,152 float ranges; projection exclusions; shared game/extension writes; read-only and zero-count operations; failed setters; state-block Apply/recording; Reset/SWVP; frame invalidation and unsafe escape.");
}
static void auditCoverageAndSchedule(){
 Backend b;DeviceMirror m;ExtensionDevice ext(&b,&m);
 assert(!ext.audit()&&m.checks==1&&m.emptyChecks==1&&m.checkedFields==0&&m.checkedRegisters==0);
 float f[12];int i[8];BOOL booleans[3];DWORD value;
 assert(SUCCEEDED(ext.GetVertexShaderConstantF(2,f,3)));
 assert(SUCCEEDED(ext.GetVertexShaderConstantF(20,f,2)));
 assert(SUCCEEDED(ext.GetVertexShaderConstantI(3,i,2)));
 assert(SUCCEEDED(ext.GetVertexShaderConstantB(8,booleans,3)));
 assert(SUCCEEDED(ext.GetPixelShaderConstantF(12,f,2)));
 assert(SUCCEEDED(ext.GetRenderState(40,&value)));
 assert(!ext.audit()&&m.checks==2&&m.emptyChecks==1&&m.checkedFields==6&&m.checkedRegisters==12);
 m.invalidate();assert(!ext.audit()&&m.emptyChecks==2&&m.checkedFields==6&&m.checkedRegisters==12);
 NorthlightMirrorAuditSchedule schedule;
 for(unsigned frame=0;frame<10000;++frame){
  unsigned checks=0;
  for(unsigned candidate=1;candidate<=120;++candidate){const bool check=schedule.afterWorldCapture(frame,true);
   assert(check==(frame%120==60&&candidate==16+(frame/120)%32));checks+=check;
  }
  assert(checks==(frame%120==60?1u:0u));
 }
 NorthlightMirrorAuditSchedule sparse;
 for(unsigned n=0;n<10;++n)assert(!sparse.afterWorldCapture(60,false));
 for(unsigned n=0;n<1000;++n)assert(!sparse.afterWorldCapture(180,false));
 for(unsigned n=0;n<16;++n)assert(!sparse.afterWorldCapture(180,true));
 assert(sparse.afterWorldCapture(180,true));assert(!sparse.afterWorldCapture(180,true));
 for(unsigned n=0;n<17;++n)assert(!sparse.afterWorldCapture(300,false));
 assert(sparse.afterWorldCapture(300,false));
 std::puts("PASS mirror audit: actual field/register coverage and empty checks; 10,000 frames of warmed capture scheduling; no audits on CPU profile frames; alternate full-palette rounds bypass 1,000 terrain captures; sparse frame reset.");
}
static void crowdedWorkload(){
 Backend direct,real;DeviceMirror mirror;MirrorDevice game(&real,&mirror);ExtensionDevice ext(&real,&mirror);
 const auto old=workload(direct,direct,direct),now=workload(real,game,ext);assert(old.draws.size()==3000&&now.draws.size()==old.draws.size());
 for(size_t i=0;i<old.draws.size();++i)assert(!std::memcmp(&old.draws[i],&now.draws[i],sizeof(DrawState)));
 assert(old.getters==36000&&now.getters<1000);std::printf("WORKLOAD 3000 draws, exact same returned values: directGetters=%u mirrorGetters=%u avoided=%u directNs=%llu mirrorNs=%llu (native fake backend; timing informational)\n",old.getters,now.getters,old.getters-now.getters,(unsigned long long)old.ns,(unsigned long long)now.ns);
}

static void rawScopeBanksAndReentry(){
 Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m),other(&b,&m);
 float previous[1024]={},values[1024],out[1024];int integers[64],outI[64];BOOL booleans[16],outB[16];
 for(unsigned n=0;n<1024;++n){uint32_t bits=0x7fc00000+n;std::memcpy(values+n,&bits,4);}values[1]=-0.f;
 for(unsigned n=0;n<64;++n)integers[n]=int(n)*-177;for(unsigned n=0;n<16;++n)booleans[n]=n%2?7:0;
 assert(SUCCEEDED(ext.GetVertexShaderConstantF(0,previous,256)));
 NorthlightConstantEpoch::Stamp before,inside,after;assert(ext.constantStamp(before));
 const auto invalidations=m.invalidations,answered=m.answered;
 {
  ExtensionDevice::RawScope scope(ext);assert(!m.active()&&m.rawDepth==1&&m.invalidations==invalidations+1);
  assert(!ext.constantStamp(inside)&&!inside.valid);
  assert(SUCCEEDED(ext.SetVertexShaderConstantF(0,values,256)));
  assert(SUCCEEDED(ext.GetVertexShaderConstantF(0,out,256))&&!std::memcmp(values,out,sizeof out));
  assert(!std::memcmp(previous,m.vsFloat,sizeof previous)); // Raw writes never refill the mirror bank.
  assert(SUCCEEDED(ext.SetVertexShaderConstantI(0,integers,16)));assert(SUCCEEDED(ext.GetVertexShaderConstantI(0,outI,16))&&!std::memcmp(integers,outI,sizeof outI));
  assert(SUCCEEDED(ext.SetVertexShaderConstantB(0,booleans,16)));assert(SUCCEEDED(ext.GetVertexShaderConstantB(0,outB,16)));for(unsigned n=0;n<16;++n)assert(outB[n]==bool(booleans[n]));
  assert(SUCCEEDED(ext.SetPixelShaderConstantF(0,values,224)));assert(SUCCEEDED(ext.GetPixelShaderConstantF(0,out,224))&&!std::memcmp(values,out,224*16));
  assert(!m.vertexShaderKnown&&!m.renderStateKnown[40]);for(bool known:m.vsFloatKnown)assert(!known);
  b.constantGetSupported=false;float sentinel[4]={11,22,33,44};assert(FAILED(ext.GetVertexShaderConstantF(0,sentinel,1))&&sentinel[0]==11);b.constantGetSupported=true;
  assert(FAILED(ext.GetVertexShaderConstantF(0,nullptr,1)));assert(FAILED(ext.SetVertexShaderConstantF(513,values,1)));
  b.failNext=true;assert(FAILED(ext.SetVertexShaderConstantF(256,values,4)));assert(SUCCEEDED(ext.GetVertexShaderConstantF(256,out,4))&&!std::memcmp(values,out,64));
  const auto reads=b.gets;assert(SUCCEEDED(ext.GetVertexShaderConstantF(0,out,256)));assert(SUCCEEDED(ext.GetVertexShaderConstantF(0,out,256)));assert(b.gets==reads+2&&m.answered==answered);
  // A second proxy lacks the TLS token, but recursive game calls must also see
  // current backend state and cannot certify it while this transaction runs.
  const auto rawCalls=m.rawCalls;assert(SUCCEEDED(other.GetVertexShaderConstantF(0,out,256)));assert(m.rawCalls==rawCalls&&!std::memcmp(values,out,sizeof out));
  assert(SUCCEEDED(game.SetRenderState(40,99)));DWORD state=0;assert(SUCCEEDED(game.GetRenderState(40,&state))&&state==99&&!m.renderStateKnown[40]);
  struct Reentry {MirrorDevice& game;ExtensionDevice& ext;DeviceMirror& mirror;unsigned called=0;} callback{game,ext,m};
  b.callbackContext=&callback;b.onRenderState=[](void* raw){auto& c=*static_cast<Reentry*>(raw);DWORD current=0;NorthlightConstantEpoch::Stamp stamp;
   assert(SUCCEEDED(c.game.GetRenderState(40,&current))&&current==123);assert(!c.ext.constantStamp(stamp));
   assert(SUCCEEDED(c.game.SetRenderState(40,124)));assert(SUCCEEDED(c.ext.GetRenderState(40,&current))&&current==124&&!c.mirror.active());++c.called;};
  assert(SUCCEEDED(ext.SetRenderState(40,123))&&callback.called==1);
  {ExtensionDevice::RawScope nested(ext);assert(m.rawDepth==2);assert(SUCCEEDED(ext.SetRenderState(40,125)));}
  assert(m.rawDepth==1&&!m.active());
  b.failGet=true;assert(FAILED(ext.GetRenderState(40,&state)));assert(SUCCEEDED(ext.GetRenderState(40,&state))&&state==125);
  b.failNext=true;assert(FAILED(ext.SetVertexShaderConstantF(0,previous,256)));assert(SUCCEEDED(ext.GetVertexShaderConstantF(0,out,256))&&!std::memcmp(out,previous,sizeof out));
 }
 assert(m.active()&&!m.rawDepth&&m.rawScopes==2&&m.answered==answered);
 assert(ext.constantStamp(after)&&!NorthlightConstantEpoch::exact(before,after));
 const auto reads=b.gets;assert(SUCCEEDED(ext.GetVertexShaderConstantF(0,out,256)));assert(b.gets==reads+1);assert(SUCCEEDED(ext.GetVertexShaderConstantF(0,out,256))&&b.gets==reads+1);
 // TLS ownership does not carry to a different device, even on the same thread.
 Backend second;DeviceMirror secondMirror;ExtensionDevice secondExt(&second,&secondMirror);
 {ExtensionDevice::RawScope a(ext);{ExtensionDevice::RawScope z(secondExt);assert(SUCCEEDED(ext.SetRenderState(40,126)));assert(!m.active()&&!secondMirror.active());}assert(!m.active()&&secondMirror.active());}
 std::puts("PASS raw scope banks: exact NaN/signed-zero float/int/normalized BOOL data; zero mirror bank copies; unknown masks; live nested/game/backend callback reads; no certificates; two devices; exit refresh.");
}
static void rawScopeRestoreAndControls(){
 for(unsigned mode=0;mode<4;++mode){
  Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);NorthlightStateBlockPool pool(&ext);
  auto* target=new Object<IDirect3DSurface9>();auto* depth=new Object<IDirect3DSurface9>();auto* vb=new Object<IDirect3DVertexBuffer9>();auto* ib=new Object<IDirect3DIndexBuffer9>();
  assert(SUCCEEDED(game.SetRenderTarget(0,target)));assert(SUCCEEDED(game.SetDepthStencilSurface(depth)));assert(SUCCEEDED(game.SetStreamSource(0,vb,12,28)));assert(SUCCEEDED(game.SetIndices(ib)));
  D3DVIEWPORT9 viewport={7,9,640,360,.1f,.7f};assert(SUCCEEDED(game.SetViewport(&viewport)));assert(SUCCEEDED(game.SetRenderState(40,10)));
  bool threw=false;
  try{ExtensionDevice::RawScope scope(ext);SavedState saved(&ext,&pool);assert(saved.ok);
   assert(SUCCEEDED(ext.SetRenderState(40,20)));assert(SUCCEEDED(ext.SetRenderTarget(0,depth)));assert(SUCCEEDED(ext.SetDepthStencilSurface(target)));
   {ExtensionDevice::RawScope nested(ext);SavedState inner(&ext,&pool);assert(inner.ok);assert(SUCCEEDED(ext.SetRenderState(40,30)));pool.clear();}
   DWORD value=0;assert(SUCCEEDED(ext.GetRenderState(40,&value))&&value==20&&!m.active());
   float vertices[9]={};uint16_t indices[3]={};b.failNext=true;assert(FAILED(ext.DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,vertices,12))&&!b.state.vb[0].p);
   assert(SUCCEEDED(ext.SetStreamSource(0,vb,24,32)));assert(SUCCEEDED(ext.SetIndices(ib)));b.failNext=true;assert(FAILED(ext.DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,3,1,indices,D3DFMT_INDEX16,vertices,12))&&!b.state.vb[0].p&&!b.state.ib.p);
   if(mode==1)throw std::runtime_error("raw effect failed");
   if(mode==2)b.failApply=true;
   if(mode==3)m.disable("escape-in-raw-scope");
  }catch(const std::runtime_error&){threw=true;}
  assert(threw==(mode==1)&&m.rawDepth==0&&m.enabled==(mode!=3));
  DWORD value=0;assert(SUCCEEDED(ext.GetRenderState(40,&value))&&value==10);D3DVIEWPORT9 actual;assert(SUCCEEDED(ext.GetViewport(&actual))&&!std::memcmp(&actual,&viewport,sizeof actual));
  assert(b.state.targets[0].p==target&&b.state.depth.p==depth&&b.state.vb[0].p==vb&&b.state.ib.p==ib&&b.state.offset[0]==12&&b.state.stride[0]==28);
  if(mode==3)assert(!std::strcmp(m.disableReason,"escape-in-raw-scope"));pool.clear();assert(b.refs==1);
  target->Release();depth->Release();vb->Release();ib->Release();
 }
 for(unsigned failure=0;failure<4;++failure){
  Backend b;DeviceMirror m;ExtensionDevice ext(&b,&m);IDirect3DStateBlock9* block=nullptr;
  assert(SUCCEEDED(ext.SetRenderState(40,1)));float value[4]={1,2,3,4},out[4];
  {ExtensionDevice::RawScope scope(ext);
   assert(SUCCEEDED(ext.BeginStateBlock())&&m.recording);assert(SUCCEEDED(ext.SetRenderState(40,2)));assert(SUCCEEDED(ext.SetVertexShaderConstantF(0,value,1)));
   DWORD state=0;assert(SUCCEEDED(ext.GetRenderState(40,&state))&&state==1);assert(SUCCEEDED(ext.GetVertexShaderConstantF(0,out,1))&&out[0]==0);
   if(failure==1)assert(FAILED(ext.BeginStateBlock())&&!m.enabled);
   if(failure==2)assert(FAILED(ext.EndStateBlock(nullptr))&&!m.enabled);
   assert(SUCCEEDED(ext.EndStateBlock(&block))&&!m.recording);assert(SUCCEEDED(block->Capture())); // Wrapper remains intact.
   if(failure==3)b.failApply=true;HRESULT hr=block->Apply();assert(FAILED(hr)==(failure==3));block->Release();
   D3DPRESENT_PARAMETERS params;assert(SUCCEEDED(ext.Reset(&params))&&!m.recording&&!m.active());
   b.failNext=true;assert(FAILED(ext.Reset(&params))&&!m.active());assert(SUCCEEDED(ext.SetSoftwareVertexProcessing(TRUE))&&!m.active());
  }
  assert(m.rawDepth==0&&m.enabled==(failure!=1&&failure!=2));
 }
 {
  Backend b;DeviceMirror m;ExtensionDevice ext(&b,&m);IDirect3DStateBlock9* block=nullptr;
  assert(SUCCEEDED(ext.BeginStateBlock()));{ExtensionDevice::RawScope scope(ext);assert(SUCCEEDED(ext.SetRenderState(40,3)));}
  assert(m.recording&&!m.active());assert(SUCCEEDED(ext.EndStateBlock(&block))&&!m.recording);assert(SUCCEEDED(block->Apply()));block->Release();
  DWORD value=0;assert(SUCCEEDED(ext.GetRenderState(40,&value))&&value==3);
 }
 std::puts("PASS raw scope restoration: nested cleared pool leases; all targets/depth/viewport/stream/index restored; exceptions and failed Apply; failed UP unbind; Begin/End recording failures; Reset/SWVP controls; permanent escape disable survives.");
}
static void rawScopeConcurrency(){
 Backend b;DeviceMirror m;ExtensionDevice ext(&b,&m);MirrorDevice game(&b,&m);
 std::mutex signal;std::condition_variable wake;bool start=false,launched=false;std::atomic<bool> finished{false};
 std::thread foreign;
 {
  ExtensionDevice::RawScope scope(ext);const auto calls=b.calls;
  foreign=std::thread([&]{{std::lock_guard<std::mutex> lock(signal);launched=true;}wake.notify_one();
   {std::unique_lock<std::mutex> lock(signal);wake.wait(lock,[&]{return start;});}
   assert(SUCCEEDED(ext.SetRenderState(40,77)));DWORD result=0;assert(SUCCEEDED(game.GetRenderState(40,&result))&&result==77);finished=true;
  });
  {std::unique_lock<std::mutex> lock(signal);wake.wait(lock,[&]{return launched;});start=true;}wake.notify_one();
  // The scope owns the gate; foreign threads have no TLS bypass authority.
  // Independently probe the actual mutex from that thread, avoiding a speed-
  // dependent expectation that its setter has already reached the lock.
  std::thread probe([&]{assert(gateTaken(m.gate));});probe.join();
  assert(!finished&&b.calls==calls);assert(SUCCEEDED(ext.SetRenderState(40,55)));
 }
 foreign.join();assert(finished&&m.rawDepth==0&&m.rawCalls==1);
 for(unsigned iteration=0;iteration<100;++iteration){std::thread a([&]{ExtensionDevice::RawScope scope(ext);for(unsigned n=0;n<50;++n)assert(SUCCEEDED(ext.SetRenderState(40,n)));});
  std::thread other([&]{for(unsigned n=0;n<50;++n){DWORD value;assert(SUCCEEDED(game.GetRenderState(40,&value)));}});a.join();other.join();}
 assert(!m.rawDepth&&m.active());
 std::puts("PASS raw scope concurrency: foreign mirrored extension/game calls wait for owner; deterministic gate probe; 100 contended scopes; no simultaneous backend access. Nonmirrored private extension calls retain their existing outer-game-gate contract.");
}
static void rawScopeShadowWorkload(){
 Backend baseline,optimized;DeviceMirror oldMirror,newMirror;ExtensionDevice oldExt(&baseline,&oldMirror),newExt(&optimized,&newMirror);
 float constants[1024],out[1024];for(unsigned n=0;n<1024;++n)constants[n]=float(n)*.25f;
 auto run=[&](ExtensionDevice& device,Backend& backend){
  for(unsigned draw=0;draw<3000;++draw){constants[100]=float(draw);assert(SUCCEEDED(device.SetVertexShaderConstantF(0,constants,256)));assert(SUCCEEDED(device.SetPixelShaderConstantF(0,constants,1)));assert(SUCCEEDED(device.SetRenderState(40,draw)));assert(SUCCEEDED(device.SetSamplerState(0,D3DSAMP_ADDRESSU,draw)));}
  assert(SUCCEEDED(device.GetVertexShaderConstantF(0,out,256))&&!std::memcmp(constants,out,sizeof out));assert(backend.state.rs[40]==2999&&backend.state.sampler[0][1]==(2999&255));
 };
 run(oldExt,baseline);const auto begin=std::chrono::steady_clock::now();
 {ExtensionDevice::RawScope scope(newExt);run(newExt,optimized);}const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-begin).count();
 assert(!std::memcmp(baseline.state.vf,optimized.state.vf,sizeof baseline.state.vf));assert(!std::memcmp(baseline.state.pf,optimized.state.pf,sizeof baseline.state.pf));
 assert(baseline.sets==optimized.sets&&baseline.calls==optimized.calls&&newMirror.rawCalls==12001&&newMirror.rawScopes==1);
 float empty[1024]={};assert(!std::memcmp(newMirror.vsFloat,empty,sizeof empty));assert(!std::memcmp(oldMirror.vsFloat,constants,sizeof constants));
 std::printf("RAW WORKLOAD 3000 shadow draws: identical %u backend calls; rawCalls=%llu; redundant mirror constant writes avoided=%u bytes; scopeNs=%llu (native fake backend; not FPS)\n",optimized.calls,(unsigned long long)newMirror.rawCalls,3000u*(4096+16),(unsigned long long)elapsed);
}


static void rawScopeDispatchArguments(){
 Backend b;DeviceMirror m;ExtensionDevice ext(&b,&m);
 auto* vs=new Object<IDirect3DVertexShader9>();auto* ps=new Object<IDirect3DPixelShader9>();auto* decl=new Object<IDirect3DVertexDeclaration9>();
 auto* vb=new Object<IDirect3DVertexBuffer9>();auto* ib=new Object<IDirect3DIndexBuffer9>();auto* texture=new Object<IDirect3DBaseTexture9>();auto* surface=new Object<IDirect3DSurface9>();
 {
  ExtensionDevice::RawScope scope(ext);
  IDirect3DVertexShader9* gotVS=nullptr;assert(SUCCEEDED(ext.SetVertexShader(vs)));assert(SUCCEEDED(ext.GetVertexShader(&gotVS))&&gotVS==vs);gotVS->Release();
  IDirect3DPixelShader9* gotPS=nullptr;assert(SUCCEEDED(ext.SetPixelShader(ps)));assert(SUCCEEDED(ext.GetPixelShader(&gotPS))&&gotPS==ps);gotPS->Release();
  DWORD value=0;assert(SUCCEEDED(ext.SetFVF(0x87654321)));assert(SUCCEEDED(ext.GetFVF(&value))&&value==0x4321);
  IDirect3DVertexDeclaration9* gotDecl=nullptr;assert(SUCCEEDED(ext.SetVertexDeclaration(decl)));assert(SUCCEEDED(ext.GetVertexDeclaration(&gotDecl))&&gotDecl==decl);gotDecl->Release();assert(SUCCEEDED(ext.GetFVF(&value))&&value==0);
  IDirect3DVertexBuffer9* gotVB=nullptr;UINT offset=0,stride=0;assert(SUCCEEDED(ext.SetStreamSource(5,vb,88,44)));assert(SUCCEEDED(ext.GetStreamSource(5,&gotVB,&offset,&stride))&&gotVB==vb&&offset==88&&stride==44);gotVB->Release();
  assert(SUCCEEDED(ext.SetStreamSourceFreq(5,0x10000007)));assert(SUCCEEDED(ext.GetStreamSourceFreq(5,&value))&&value==0x10000007);
  IDirect3DIndexBuffer9* gotIB=nullptr;assert(SUCCEEDED(ext.SetIndices(ib)));assert(SUCCEEDED(ext.GetIndices(&gotIB))&&gotIB==ib);gotIB->Release();
  IDirect3DBaseTexture9* gotTexture=nullptr;assert(SUCCEEDED(ext.SetTexture(7,texture)));assert(SUCCEEDED(ext.GetTexture(7,&gotTexture))&&gotTexture==texture);gotTexture->Release();
  assert(SUCCEEDED(ext.SetSamplerState(7,D3DSAMP_ADDRESSV,257)));assert(SUCCEEDED(ext.GetSamplerState(7,D3DSAMP_ADDRESSV,&value))&&value==1);
  assert(SUCCEEDED(ext.SetRenderState(D3DRS_ALPHABLENDENABLE,77)));assert(SUCCEEDED(ext.GetRenderState(D3DRS_ALPHABLENDENABLE,&value))&&value==1);
  IDirect3DSurface9* gotSurface=nullptr;assert(SUCCEEDED(ext.SetRenderTarget(3,surface)));assert(SUCCEEDED(ext.GetRenderTarget(3,&gotSurface))&&gotSurface==surface);gotSurface->Release();
  assert(SUCCEEDED(ext.SetDepthStencilSurface(surface)));assert(SUCCEEDED(ext.GetDepthStencilSurface(&gotSurface))&&gotSurface==surface);gotSurface->Release();
  D3DVIEWPORT9 viewport={7,8,0x10003,52,.17f,.93f},actual;assert(SUCCEEDED(ext.SetViewport(&viewport)));assert(SUCCEEDED(ext.GetViewport(&actual))&&actual.X==7&&actual.Y==8&&actual.Width==3&&actual.Height==52&&actual.MinZ==.17f&&actual.MaxZ==.93f);
  float data[12]={1,2,3,4,5,6,7,8,9,10,11,12},output[12];int ints[12]={11,21,31,41,51,61,71,81,91,101,111,121},gotInts[12];BOOL booleans[3]={0,7,-3},gotBools[3];
  assert(SUCCEEDED(ext.SetVertexShaderConstantF(19,data,3)));assert(SUCCEEDED(ext.GetVertexShaderConstantF(19,output,3))&&!std::memcmp(data,output,sizeof data));
  assert(SUCCEEDED(ext.SetPixelShaderConstantF(23,data,3)));assert(SUCCEEDED(ext.GetPixelShaderConstantF(23,output,3))&&!std::memcmp(data,output,sizeof data));
  assert(SUCCEEDED(ext.SetVertexShaderConstantI(7,ints,3)));assert(SUCCEEDED(ext.GetVertexShaderConstantI(7,gotInts,3))&&!std::memcmp(ints,gotInts,sizeof ints));
  assert(SUCCEEDED(ext.SetVertexShaderConstantB(8,booleans,3)));assert(SUCCEEDED(ext.GetVertexShaderConstantB(8,gotBools,3))&&!gotBools[0]&&gotBools[1]==1&&gotBools[2]==1);
  assert(SUCCEEDED(ext.DrawPrimitiveUP(6,2,data,16)));assert(b.up.type==6&&b.up.count==2&&b.up.data==data&&b.up.stride==16);
  assert(SUCCEEDED(ext.DrawIndexedPrimitiveUP(4,7,9,3,ints,102,data,12)));assert(b.up.type==4&&b.up.minimum==7&&b.up.vertices==9&&b.up.count==3&&b.up.indices==ints&&b.up.format==102&&b.up.data==data&&b.up.stride==12);
  assert(m.rawCalls==37&&m.rawDepth==1); // All 36 generated methods plus the extra FVF read.
 }
 vs->Release();ps->Release();decl->Release();vb->Release();ib->Release();texture->Release();surface->Release();
 std::puts("PASS raw dispatch: all 36 generated state/resource/constant/UP methods preserve nondefault argument positions, output pointers, COM references, normalized values and exact backend data.");
}

// 0.3.136 learned write-through: every scenario compares against the mock's
// authoritative state, so a stale mirror answer fails the assertion directly.
static void writeThroughLearning(){
 if(!kMirrorWriteThrough||!kMirrorBorrowedPeek){std::puts("SKIP write-through scenario: switch off");return;}
 Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);DWORD value=0;
 const auto rs=[&](DWORD expected,bool hit){const auto reads=b.gets;assert(SUCCEEDED(ext.GetRenderState(40,&value))&&value==expected);assert((b.gets==reads)==hit);};
 for(DWORD n=1;n<=DeviceMirror::TrustAfter;++n){assert(SUCCEEDED(game.SetRenderState(40,n)));assert(!m.renderStateKnown[40]);rs(n,false);}
 assert(m.trust[DeviceMirror::RenderStateSlot+40]==DeviceMirror::TrustAfter&&m.learnedSlots==1);
 assert(SUCCEEDED(game.SetRenderState(40,1)));rs(1,true);assert(m.writeThrough==1);
 // A trusted slot still asks once for every value it has not seen stored exactly.
 assert(SUCCEEDED(game.SetRenderState(40,50)));rs(50,false);rs(50,true);assert(SUCCEEDED(game.SetRenderState(40,2)));rs(2,true);assert(SUCCEEDED(game.SetRenderState(40,50)));rs(50,true);
 // Recording changes no live state and never writes through; End invalidates.
 assert(SUCCEEDED(game.BeginStateBlock()));assert(SUCCEEDED(game.SetRenderState(40,3)));rs(50,false);rs(50,false);
 IDirect3DStateBlock9* recorded=nullptr;assert(SUCCEEDED(game.EndStateBlock(&recorded)));rs(50,false);
 assert(SUCCEEDED(game.SetRenderState(40,4)));rs(4,true);
 assert(SUCCEEDED(recorded->Apply()));rs(3,false);recorded->Release();
 // ALL block built on the mirrored device: Apply restores device AND mirror.
 IDirect3DStateBlock9* all=nullptr;assert(SUCCEEDED(game.CreateStateBlock(D3DSBT_ALL,&all)));
 assert(SUCCEEDED(game.SetRenderState(40,5)));rs(5,true);assert(SUCCEEDED(all->Apply()));rs(3,false);
 assert(SUCCEEDED(game.SetRenderState(40,5)));b.failApply=true;assert(FAILED(all->Apply()));rs(3,false);all->Release();
 // SavedState pool on the extension device (the 0.3.23 regression shape).
 NorthlightStateBlockPool pool(&ext);auto* target=new Object<IDirect3DSurface9>();assert(SUCCEEDED(game.SetRenderTarget(0,target)));
 assert(SUCCEEDED(game.SetRenderState(40,6)));rs(6,true);
 {SavedState saved(&ext,&pool);assert(saved.ok);assert(SUCCEEDED(ext.SetRenderState(40,7)));rs(7,true);}
 rs(6,false);pool.clear();
 // Reset clears state and the mirror, but not what the backend has proven.
 D3DPRESENT_PARAMETERS params;assert(SUCCEEDED(game.SetRenderState(40,8)));assert(SUCCEEDED(game.Reset(&params)));rs(0,false);
 assert(SUCCEEDED(game.SetRenderState(40,1)));rs(1,true);
 // A failed setter invalidates: the mock mutates before failing.
 b.failNext=true;assert(FAILED(game.SetRenderState(40,2)));rs(2,false);
 // Vendor command states never write through and invalidate everything.
 for(DWORD n=0;n<20;++n){assert(SUCCEEDED(game.SetRenderState(D3DRS_POINTSIZE,n%2)));const auto reads=b.gets;assert(SUCCEEDED(ext.GetRenderState(D3DRS_POINTSIZE,&value))&&value==n%2&&b.gets==reads+1);}
 // Normalizing slot: one differing round trip distrusts it permanently.
 for(DWORD n=0;n<DeviceMirror::TrustAfter;++n){assert(SUCCEEDED(game.SetRenderState(D3DRS_ALPHABLENDENABLE,n%2)));assert(SUCCEEDED(ext.GetRenderState(D3DRS_ALPHABLENDENABLE,&value))&&value==n%2);}
 assert(m.trust[DeviceMirror::RenderStateSlot+D3DRS_ALPHABLENDENABLE]==DeviceMirror::TrustAfter);
 assert(SUCCEEDED(game.SetRenderState(D3DRS_ALPHABLENDENABLE,99)));assert(SUCCEEDED(ext.GetRenderState(D3DRS_ALPHABLENDENABLE,&value))&&value==1); // value unproven: asked, not assumed
 assert(m.trust[DeviceMirror::RenderStateSlot+D3DRS_ALPHABLENDENABLE]==DeviceMirror::Distrusted&&m.distrustedSlots==1);
 for(DWORD n=0;n<40;++n){assert(SUCCEEDED(game.SetRenderState(D3DRS_ALPHABLENDENABLE,n%2)));const auto reads=b.gets;assert(SUCCEEDED(ext.GetRenderState(D3DRS_ALPHABLENDENABLE,&value))&&value==n%2&&b.gets==reads+1);}
 // Re-entrant backend mutation during a trusted, proven setter must not be masked.
 for(DWORD v:{123u,124u}){assert(SUCCEEDED(game.SetRenderState(40,v)));rs(v,false);}
 struct Reentry {MirrorDevice& game;} nested{game};b.callbackContext=&nested;
 b.onRenderState=[](void* raw){assert(SUCCEEDED(static_cast<Reentry*>(raw)->game.SetRenderState(40,124)));};
 const auto written=m.writeThrough;assert(SUCCEEDED(game.SetRenderState(40,123)));assert(m.writeThrough==written+1); // only the nested write
 rs(124,false);assert(b.state.rs[40]==124); // the outer write neither writes through nor keeps the nested knowledge
 // Sampled self-check still covers written-through slots.
 assert(SUCCEEDED(game.SetRenderState(40,1)));rs(1,true);assert(!ext.audit());b.state.rs[40]=17;
 const char* field=ext.audit();assert(field&&!std::strcmp(field,"renderState")&&!m.enabled);rs(17,false);
 assert(SUCCEEDED(game.SetRenderState(40,1)));rs(1,false);target->Release();assert(b.refs==1);
 std::puts("PASS write-through learning: 8 exact round trips before trust plus per-value proof; recording/End/recorded Apply; ALL Apply incl. failure; SavedState pool; Reset keeps proof not state; failed setter; vendor states; permanent distrust of normalizing slot; re-entrant backend write; audit still detects and disables.");
}
static void writeThroughImplicitState(){
 if(!kMirrorWriteThrough||!kMirrorBorrowedPeek){std::puts("SKIP write-through scenario: switch off");return;}
 Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);
 auto* surf=new Object<IDirect3DSurface9>();auto* other=new Object<IDirect3DSurface9>();auto* dec=new Object<IDirect3DVertexDeclaration9>();
 auto* vb=new Object<IDirect3DVertexBuffer9>();auto* ib=new Object<IDirect3DIndexBuffer9>();auto* ps=new Object<IDirect3DPixelShader9>();
 D3DVIEWPORT9 vp={},out={};
 for(DWORD n=0;n<DeviceMirror::TrustAfter;++n){vp={n,n,640+n,360,0,1};assert(SUCCEEDED(game.SetViewport(&vp)));assert(SUCCEEDED(ext.GetViewport(&out)));}
 vp={3,4,512,256,.25f,.75f};assert(SUCCEEDED(game.SetViewport(&vp)));assert(SUCCEEDED(ext.GetViewport(&out))); // proves this exact viewport
 assert(SUCCEEDED(game.SetViewport(&vp)));auto reads=b.gets;assert(SUCCEEDED(ext.GetViewport(&out))&&!std::memcmp(&out,&vp,sizeof vp)&&b.gets==reads);
 // Target 1 leaves the viewport; target 0 resets it and the mirror must ask.
 assert(SUCCEEDED(game.SetRenderTarget(1,surf)));reads=b.gets;assert(SUCCEEDED(ext.GetViewport(&out))&&!std::memcmp(&out,&vp,sizeof vp)&&b.gets==reads);
 assert(SUCCEEDED(game.SetRenderTarget(0,surf)));reads=b.gets;assert(SUCCEEDED(ext.GetViewport(&out))&&out.Width==800&&out.Height==600&&b.gets==reads+1);
 // A viewport the backend clamps (mock masks Width) distrusts the slot.
 vp.Width=0x10005;assert(SUCCEEDED(game.SetViewport(&vp)));assert(SUCCEEDED(ext.GetViewport(&out))&&out.Width==5);assert(m.trust[DeviceMirror::ViewportSlot]==DeviceMirror::Distrusted);
 vp.Width=6;assert(SUCCEEDED(game.SetViewport(&vp)));reads=b.gets;assert(SUCCEEDED(ext.GetViewport(&out))&&out.Width==6&&b.gets==reads+1);
 // Declaration and FVF alias each other in both directions.
 IDirect3DVertexDeclaration9* gotDecl=nullptr;DWORD fvf=0;
 for(DWORD n=0;n<DeviceMirror::TrustAfter;++n){assert(SUCCEEDED(game.SetVertexDeclaration(dec)));assert(SUCCEEDED(ext.GetVertexDeclaration(&gotDecl))&&gotDecl==dec);gotDecl->Release();}
 assert(SUCCEEDED(game.SetVertexDeclaration(dec)));reads=b.gets;assert(SUCCEEDED(ext.GetVertexDeclaration(&gotDecl))&&gotDecl==dec&&b.gets==reads);gotDecl->Release();
 assert(SUCCEEDED(game.SetFVF(0x142)));reads=b.gets;assert(SUCCEEDED(ext.GetVertexDeclaration(&gotDecl))&&!gotDecl&&b.gets==reads+1);assert(SUCCEEDED(ext.GetFVF(&fvf))&&fvf==0x142);
 assert(SUCCEEDED(game.SetVertexDeclaration(dec)));reads=b.gets;assert(SUCCEEDED(ext.GetFVF(&fvf))&&fvf==0&&b.gets==reads+1);
 assert(SUCCEEDED(game.SetVertexDeclaration(nullptr)));reads=b.gets;assert(SUCCEEDED(ext.GetVertexDeclaration(&gotDecl))&&!gotDecl&&b.gets==reads+1);
 // Streams: null keeps the backend's retained binding; UP draws unbind.
 IDirect3DVertexBuffer9* gotVB=nullptr;UINT offset=0,stride=0;
 for(UINT n=0;n<DeviceMirror::TrustAfter;++n){assert(SUCCEEDED(game.SetStreamSource(0,vb,n*4,32)));assert(SUCCEEDED(ext.GetStreamSource(0,&gotVB,&offset,&stride))&&gotVB==vb);gotVB->Release();}
 assert(SUCCEEDED(game.SetStreamSource(0,vb,16,28)));reads=b.gets;assert(SUCCEEDED(ext.GetStreamSource(0,&gotVB,&offset,&stride))&&gotVB==vb&&offset==16&&stride==28&&b.gets==reads);gotVB->Release();
 assert(SUCCEEDED(game.SetStreamSource(0,nullptr,900,999)));reads=b.gets;assert(SUCCEEDED(ext.GetStreamSource(0,&gotVB,&offset,&stride))&&gotVB==vb&&offset==16&&stride==28&&b.gets==reads+1);gotVB->Release();
 IDirect3DIndexBuffer9* gotIB=nullptr;
 for(UINT n=0;n<DeviceMirror::TrustAfter;++n){assert(SUCCEEDED(game.SetIndices(ib)));assert(SUCCEEDED(ext.GetIndices(&gotIB))&&gotIB==ib);gotIB->Release();}
 assert(SUCCEEDED(game.SetStreamSource(0,vb,8,16)));assert(SUCCEEDED(game.SetIndices(ib)));
 assert(SUCCEEDED(game.DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,nullptr,16)));assert(SUCCEEDED(ext.GetStreamSource(0,&gotVB,&offset,&stride))&&!gotVB&&!offset&&!stride);
 reads=b.gets;assert(SUCCEEDED(ext.GetIndices(&gotIB))&&gotIB==ib&&b.gets==reads);gotIB->Release();
 assert(SUCCEEDED(game.DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,3,1,nullptr,D3DFMT_INDEX16,nullptr,16)));assert(SUCCEEDED(ext.GetIndices(&gotIB))&&!gotIB);
 // Null targets/depth never write through (NOTFOUND stays authoritative).
 IDirect3DSurface9* gotSurface=nullptr;
 for(UINT n=0;n<DeviceMirror::TrustAfter;++n){assert(SUCCEEDED(game.SetRenderTarget(1,n%2?surf:other)));assert(SUCCEEDED(ext.GetRenderTarget(1,&gotSurface)));gotSurface->Release();
  assert(SUCCEEDED(game.SetDepthStencilSurface(n%2?surf:other)));assert(SUCCEEDED(ext.GetDepthStencilSurface(&gotSurface)));gotSurface->Release();}
 assert(SUCCEEDED(game.SetRenderTarget(1,nullptr)));assert(ext.GetRenderTarget(1,&gotSurface)==D3DERR_NOTFOUND&&!gotSurface);
 assert(SUCCEEDED(game.SetDepthStencilSurface(nullptr)));assert(ext.GetDepthStencilSurface(&gotSurface)==D3DERR_NOTFOUND&&!gotSurface);
 assert(SUCCEEDED(game.SetDepthStencilSurface(other)));reads=b.gets;assert(SUCCEEDED(ext.GetDepthStencilSurface(&gotSurface))&&gotSurface==other&&b.gets==reads);gotSurface->Release();
 // Borrowed peeks: no reference, only when known and active.
 IDirect3DSurface9* peeked=nullptr;const auto refs=other->refs.load();assert(ext.peekDepthStencilSurface(peeked)&&peeked==other&&other->refs==refs);
 assert(SUCCEEDED(game.SetRenderTarget(0,other)));assert(!ext.peekRenderTarget(0,peeked));assert(SUCCEEDED(ext.GetRenderTarget(0,&gotSurface)));gotSurface->Release();
 assert(ext.peekRenderTarget(0,peeked)&&peeked==other);assert(!ext.peekRenderTarget(4,peeked));
 IDirect3DPixelShader9* peekPS=nullptr;assert(!ext.peekPixelShader(peekPS));assert(SUCCEEDED(game.SetPixelShader(ps)));IDirect3DPixelShader9* gotPS=nullptr;assert(SUCCEEDED(ext.GetPixelShader(&gotPS)));gotPS->Release();
 assert(ext.peekPixelShader(peekPS)&&peekPS==ps&&ps->refs==2);
 // 0.3.196 (task 12): vertex shader and texture peeks: unknown until observed or written with a trusted mirror, then a borrowed pointer (no reference).
 IDirect3DVertexShader9* peekVS=nullptr;IDirect3DBaseTexture9* peekTex=nullptr;auto* vsObj=new Object<IDirect3DVertexShader9>();auto* texObj=new Object<IDirect3DBaseTexture9>();
 assert(!ext.peekVertexShader(peekVS)&&!ext.peekTexture(0,peekTex));
 assert(SUCCEEDED(game.SetVertexShader(vsObj))&&SUCCEEDED(game.SetTexture(0,texObj)));
 {IDirect3DVertexShader9* gotVS=nullptr;IDirect3DBaseTexture9* gotTex=nullptr;assert(SUCCEEDED(ext.GetVertexShader(&gotVS))&&SUCCEEDED(ext.GetTexture(0,&gotTex)));gotVS->Release();gotTex->Release();}
 {const auto vsRefs=vsObj->refs.load(),texRefs=texObj->refs.load();assert(ext.peekVertexShader(peekVS)&&peekVS==vsObj&&ext.peekTexture(0,peekTex)&&peekTex==texObj&&vsObj->refs==vsRefs&&texObj->refs==texRefs);}
 assert(!ext.peekTexture(1,peekTex)&&!ext.peekTexture(DeviceMirror::Textures,peekTex)&&!ext.peekTexture(DWORD(-1),peekTex)); // unknown stage and out of range
 assert(SUCCEEDED(game.SetTexture(0,nullptr))&&!ext.peekTexture(0,peekTex)); // null binding: use the Get* method
 assert(SUCCEEDED(game.SetTexture(0,texObj))&&!ext.peekTexture(0,peekTex)); // written, not yet trusted again until observed
 {IDirect3DBaseTexture9* gotTex=nullptr;assert(SUCCEEDED(ext.GetTexture(0,&gotTex)));gotTex->Release();}
 {ExtensionDevice::RawScope scope(ext);assert(!ext.peekRenderTarget(0,peeked)&&!ext.peekDepthStencilSurface(peeked)&&!ext.peekPixelShader(peekPS)&&!ext.peekVertexShader(peekVS)&&!ext.peekTexture(0,peekTex));}
 assert(SUCCEEDED(game.SetVertexShader(nullptr))&&!ext.peekVertexShader(peekVS));assert(SUCCEEDED(game.SetTexture(0,nullptr)));vsObj->Release();texObj->Release();
 assert(!ext.audit()&&m.enabled);
 assert(SUCCEEDED(game.SetPixelShader(nullptr)));
 surf->Release();other->Release();dec->Release();vb->Release();ib->Release();ps->Release();
 // Pure device: setters succeed but every state Get fails; writes must never establish Get support.
 {Backend pure;DeviceMirror pm;MirrorDevice pg(&pure,&pm);ExtensionDevice pe(&pure,&pm);pure.pureDevice=true;auto* t=new Object<IDirect3DBaseTexture9>();
  for(unsigned n=0;n<40;++n){DWORD v=0;D3DVIEWPORT9 pv={0,0,640,360,0,1},got;IDirect3DBaseTexture9* gt=nullptr;
   assert(SUCCEEDED(pg.SetRenderState(40,n%2)));assert(FAILED(pe.GetRenderState(40,&v)));assert(SUCCEEDED(pg.SetViewport(&pv)));assert(FAILED(pe.GetViewport(&got)));
   assert(SUCCEEDED(pg.SetTexture(0,t)));assert(FAILED(pe.GetTexture(0,&gt)));}
  assert(!pm.writeThrough&&!pm.answered&&!pm.learnedSlots);assert(SUCCEEDED(pg.SetTexture(0,nullptr)));t->Release();}
 std::puts("PASS write-through implicit state: pure-device Gets keep failing; RT0 viewport reset, RT1 keeps it; clamped viewport distrusted; FVF<->declaration aliasing; null stream retention; UP unbinds; null target/depth NOTFOUND; borrowed peeks without refs and never in raw scopes.");
}
// Randomized differential run: every mirrored read must equal the mock's
// authoritative state across mixed game/extension writes, state blocks,
// recording, raw scopes, failures, UP draws, Reset and frame invalidation.
static void writeThroughDifferential(){
 Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);std::mt19937 rng(136);
 IDirect3DVertexShader9* vs[2]={new Object<IDirect3DVertexShader9>(),new Object<IDirect3DVertexShader9>()};IDirect3DPixelShader9* ps[2]={new Object<IDirect3DPixelShader9>(),new Object<IDirect3DPixelShader9>()};
 IDirect3DBaseTexture9* tex[3]={new Object<IDirect3DBaseTexture9>(),new Object<IDirect3DBaseTexture9>(),new Object<IDirect3DBaseTexture9>()};
 IDirect3DSurface9* surf[3]={new Object<IDirect3DSurface9>(),new Object<IDirect3DSurface9>(),new Object<IDirect3DSurface9>()};
 IDirect3DVertexBuffer9* vb[2]={new Object<IDirect3DVertexBuffer9>(),new Object<IDirect3DVertexBuffer9>()};IDirect3DIndexBuffer9* ib[2]={new Object<IDirect3DIndexBuffer9>(),new Object<IDirect3DIndexBuffer9>()};
 IDirect3DVertexDeclaration9* dec[2]={new Object<IDirect3DVertexDeclaration9>(),new Object<IDirect3DVertexDeclaration9>()};
 assert(SUCCEEDED(game.SetRenderTarget(0,surf[0])));
 std::vector<IDirect3DStateBlock9*> blocks;bool recording=false;unsigned mismatchesSeen=0,checks=0;
 const auto verify=[&]{
  IDirect3DVertexShader9* v=nullptr;assert(SUCCEEDED(ext.GetVertexShader(&v))&&v==b.state.vs.p);if(v)v->Release();
  IDirect3DPixelShader9* p=nullptr;assert(SUCCEEDED(ext.GetPixelShader(&p))&&p==b.state.ps.p);if(p)p->Release();
  IDirect3DVertexDeclaration9* d=nullptr;assert(SUCCEEDED(ext.GetVertexDeclaration(&d))&&d==b.state.decl.p);if(d)d->Release();
  DWORD value=0;assert(SUCCEEDED(ext.GetFVF(&value))&&value==b.state.fvf);
  IDirect3DIndexBuffer9* i=nullptr;assert(SUCCEEDED(ext.GetIndices(&i))&&i==b.state.ib.p);if(i)i->Release();
  for(UINT n=0;n<2;++n){IDirect3DVertexBuffer9* s=nullptr;UINT offset=0,stride=0;assert(SUCCEEDED(ext.GetStreamSource(n,&s,&offset,&stride))&&s==b.state.vb[n].p&&offset==b.state.offset[n]&&stride==b.state.stride[n]);if(s)s->Release();}
  for(DWORD n=0;n<4;++n){IDirect3DBaseTexture9* t=nullptr;assert(SUCCEEDED(ext.GetTexture(n,&t))&&t==b.state.textures[n].p);if(t)t->Release();
   assert(SUCCEEDED(ext.GetSamplerState(n,D3DSAMP_ADDRESSU,&value))&&value==b.state.sampler[n][D3DSAMP_ADDRESSU]);}
  for(DWORD n=40;n<44;++n){assert(SUCCEEDED(ext.GetRenderState(n,&value))&&value==b.state.rs[n]);}
  assert(SUCCEEDED(ext.GetRenderState(D3DRS_ALPHABLENDENABLE,&value))&&value==b.state.rs[D3DRS_ALPHABLENDENABLE]);
  D3DVIEWPORT9 vp;assert(SUCCEEDED(ext.GetViewport(&vp))&&!std::memcmp(&vp,&b.state.viewport,sizeof vp));
  for(DWORD n=0;n<4;++n){IDirect3DSurface9* s=nullptr;HRESULT hr=ext.GetRenderTarget(n,&s);assert(s==b.state.targets[n].p&&(s?SUCCEEDED(hr):hr==D3DERR_NOTFOUND));if(s)s->Release();}
  IDirect3DSurface9* z=nullptr;HRESULT hr=ext.GetDepthStencilSurface(&z);assert(z==b.state.depth.p&&(z?SUCCEEDED(hr):hr==D3DERR_NOTFOUND));if(z)z->Release();
  IDirect3DSurface9* peeked=nullptr;if(ext.peekRenderTarget(0,peeked))assert(peeked==b.state.targets[0].p);
  if(ext.peekDepthStencilSurface(peeked))assert(peeked==b.state.depth.p);
  IDirect3DPixelShader9* peekPS=nullptr;if(ext.peekPixelShader(peekPS))assert(peekPS==b.state.ps.p);
  IDirect3DVertexShader9* peekVS=nullptr;if(ext.peekVertexShader(peekVS))assert(peekVS==b.state.vs.p);
  for(DWORD n=0;n<4;++n){IDirect3DBaseTexture9* peekTex=nullptr;if(ext.peekTexture(n,peekTex))assert(peekTex==b.state.textures[n].p);}
  ++checks;
 };
 for(unsigned step=0;step<60000;++step){
  IDirect3DDevice9& writer=rng()%4?static_cast<IDirect3DDevice9&>(game):static_cast<IDirect3DDevice9&>(ext);
  const bool fail=rng()%97==0;b.failNext=fail;
  switch(rng()%22){
   case 0:writer.SetVertexShader(rng()%5?vs[rng()%2]:nullptr);break;
   case 1:writer.SetPixelShader(rng()%5?ps[rng()%2]:nullptr);break;
   case 2:writer.SetVertexDeclaration(rng()%6?dec[rng()%2]:nullptr);break;
   case 3:writer.SetFVF(rng()%3?0x142:0x10002);break;
   case 4:writer.SetIndices(rng()%6?ib[rng()%2]:nullptr);break;
   case 5:case 6:writer.SetStreamSource(rng()%2,rng()%6?vb[rng()%2]:nullptr,4*(rng()%8),16+4*(rng()%4));break;
   case 7:case 8:writer.SetTexture(rng()%4,rng()%5?tex[rng()%3]:nullptr);break;
   case 9:writer.SetSamplerState(rng()%4,D3DSAMP_ADDRESSU,rng()%50?rng()%5:300);break;
   case 10:case 11:writer.SetRenderState(40+rng()%4,rng()%7);break;
   case 12:writer.SetRenderState(D3DRS_ALPHABLENDENABLE,rng()%9?rng()%2:5);break;
   case 13:{D3DVIEWPORT9 vp={rng()%2,rng()%2,rng()%200?640+rng()%4:0x10000+rng()%8,360,0,rng()%2?1.f:.5f};writer.SetViewport(&vp);break;}
   case 14:{DWORD n=rng()%4;writer.SetRenderTarget(n,n==0||rng()%3?surf[rng()%3]:nullptr);break;}
   case 15:writer.SetDepthStencilSurface(rng()%4?surf[rng()%3]:nullptr);break;
   case 16:if(rng()%2)writer.DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,nullptr,16);else writer.DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,3,1,nullptr,D3DFMT_INDEX16,nullptr,16);break;
   case 17:b.failNext=false;if(!recording&&blocks.size()<4){IDirect3DStateBlock9* block=nullptr;if(SUCCEEDED(game.CreateStateBlock(D3DSBT_ALL,&block)))blocks.push_back(block);}break;
   case 18:b.failNext=false;if(!recording&&!blocks.empty()){const size_t k=rng()%blocks.size();b.failApply=rng()%10==0;blocks[k]->Apply();if(rng()%2){blocks[k]->Release();blocks.erase(blocks.begin()+k);}}break;
   case 19:b.failNext=false;if(!recording){assert(SUCCEEDED(game.BeginStateBlock()));recording=true;}else{IDirect3DStateBlock9* block=nullptr;assert(SUCCEEDED(game.EndStateBlock(&block)));recording=false;if(rng()%2)block->Apply();block->Release();}break;
   case 20:b.failNext=false;if(!recording){ExtensionDevice::RawScope scope(ext);ext.SetRenderState(40+rng()%4,rng()%7);D3DVIEWPORT9 vp={0,0,320,200,0,1};ext.SetViewport(&vp);ext.SetTexture(rng()%4,tex[rng()%3]);ext.SetRenderTarget(0,surf[rng()%3]);}break;
   case 21:b.failNext=false;if(rng()%20==0&&!recording){for(auto* block:blocks)block->Release();blocks.clear();D3DPRESENT_PARAMETERS params;game.Reset(&params);assert(SUCCEEDED(game.SetRenderTarget(0,surf[0])));}else m.invalidate();break;
  }
  b.failNext=false;b.failApply=false;
  if(rng()%2)verify();
  if(step%997==0&&!recording){const char* field=ext.audit();if(field)++mismatchesSeen;}
 }
 if(recording){IDirect3DStateBlock9* block=nullptr;assert(SUCCEEDED(game.EndStateBlock(&block)));block->Release();}
 for(auto* block:blocks)block->Release();verify();
 assert(!mismatchesSeen&&m.enabled&&!m.mismatches);if(kMirrorWriteThrough)assert(m.writeThrough>10000&&m.learnedSlots>=20&&m.distrustedSlots>=3);
 std::printf("PASS write-through differential: 60000 random ops, %u full-state comparisons, writeThrough=%llu learnedSlots=%llu distrustedSlots=%llu answered=%llu forwarded=%llu audits clean\n",checks,
  (unsigned long long)m.writeThrough,(unsigned long long)m.learnedSlots,(unsigned long long)m.distrustedSlots,(unsigned long long)m.answered,(unsigned long long)m.forwarded);
 for(auto* p:vs)p->Release();for(auto* p:ps)p->Release();for(auto* p:tex)p->Release();for(auto* p:surf)p->Release();for(auto* p:vb)p->Release();for(auto* p:ib)p->Release();for(auto* p:dec)p->Release();
}
// 0.3.180 (C3): allKnown() equals the per-register loop for every (start,count) of each bank (the
// mirror's own arrays, so their real alignment), count 0 and ranges ending at the bank end, all
// unaligned starts, random masks. Counterfactuals: whole words only (no tail), first/last flag only.
static bool knownLoop(const bool* known,unsigned count){for(unsigned i=0;i<count;++i)if(!known[i])return false;return true;}
static bool knownWordsOnly(const bool* known,unsigned count){for(unsigned i=0;i+4<=count;i+=4){uint32_t word;std::memcpy(&word,known+i,4);if(word!=0x01010101u)return false;}return true;}
static bool knownEndsOnly(const bool* known,unsigned count){return !count||(known[0]&&known[count-1]);}
static void knownScan(){
 std::mt19937 rng(180);DeviceMirror m;size_t cases=0;bool wordsDiffer=false,endsDiffer=false;
 bool* banks[]={m.vsFloatKnown,m.psFloatKnown,m.vsIntKnown,m.vsBoolKnown};const unsigned sizes[]={DeviceMirror::VsFloat,DeviceMirror::PsFloat,DeviceMirror::Ints,DeviceMirror::Bools};
 static_assert(DeviceMirror::VsFloat==256&&DeviceMirror::PsFloat==224&&DeviceMirror::Ints==16&&DeviceMirror::Bools==16,"bank sizes");
 for(unsigned b=0;b<4;++b)for(unsigned round=0;round<(sizes[b]>16?6u:64u);++round){
  bool* known=banks[b];const unsigned size=sizes[b],mode=round%4;
  for(unsigned i=0;i<size;++i)known[i]=mode==0||(mode==1?rng()%48!=0:mode==2?rng()%2!=0:true);
  if(mode==3)known[rng()%size]=false;
  for(unsigned start=0;start<=size;++start)for(unsigned count=0;start+count<=size;++count){
   const bool expected=knownLoop(known+start,count);assert(allKnown(known+start,count)==expected);++cases;
   wordsDiffer|=knownWordsOnly(known+start,count)!=expected;endsDiffer|=knownEndsOnly(known+start,count)!=expected;}
 }
 assert(wordsDiffer&&endsDiffer);
 // Answers and counters through the Get paths: answered iff every register was known before the call.
 Backend backend;DeviceMirror mirror;MirrorDevice game(&backend,&mirror);ExtensionDevice ext(&backend,&mirror);
 float values[1024],out[1024];for(unsigned j=0;j<1024;++j){uint32_t v=rng();std::memcpy(values+j,&v,4);}
 assert(SUCCEEDED(game.SetVertexShaderConstantF(0,values,256)));assert(SUCCEEDED(game.SetPixelShaderConstantF(0,values,224)));
 unsigned answeredCalls=0;
 for(unsigned n=0;n<4000;++n){const bool pixel=n%2;const unsigned size=pixel?224:256,first=rng()%size,count=1+rng()%std::min(size-first,rng()%3?8u:size);
  if(n%97==0){D3DPRESENT_PARAMETERS params;assert(SUCCEEDED(game.Reset(&params)));assert(SUCCEEDED(game.SetVertexShaderConstantF(0,values,256)));assert(SUCCEEDED(game.SetPixelShaderConstantF(0,values,224)));}
  const bool expected=knownLoop((pixel?mirror.psFloatKnown:mirror.vsFloatKnown)+first,count);
  const auto answered=mirror.answered,forwarded=mirror.forwarded;const unsigned gets=backend.gets;
  assert(SUCCEEDED(pixel?ext.GetPixelShaderConstantF(first,out,count):ext.GetVertexShaderConstantF(first,out,count)));
  assert(!std::memcmp(out,pixel?backend.state.pf[first]:backend.state.vf[first],count*16));
  assert(mirror.answered==answered+expected&&mirror.forwarded==forwarded+!expected&&backend.gets==gets+!expected);answeredCalls+=expected;}
 assert(answeredCalls>1000);
 std::printf("PASS known scan: allKnown == per-register loop over %zu (start,count) ranges of the 256/224/16/16 banks; whole-word-only and ends-only counterfactuals differ; %u of 4000 Get calls answered, counters unchanged\n",cases,answeredCalls);
}
// One real acquisition per nested call chain; foreign threads still block.
static void singleGate(){
 MirrorGate a,other;
 {MirrorGuard outer(a);assert(MirrorGuard::heldByThisThread(a));{MirrorGuard inner(a);{MirrorGuard b(other);assert(MirrorGuard::heldByThisThread(other));{MirrorGuard again(a);assert(MirrorGuard::heldByThisThread(a));}assert(MirrorGuard::heldByThisThread(other));}assert(MirrorGuard::heldByThisThread(a));}
  std::thread probe([&]{assert(gateTaken(a));assert(!MirrorGuard::heldByThisThread(a));assert(!gateTaken(other));});probe.join();}
 assert(!MirrorGuard::heldByThisThread(a));std::thread free([&]{assert(!gateTaken(a));});free.join();
 try{MirrorGuard guard(a);throw std::runtime_error("unwind");}catch(const std::runtime_error&){}
 assert(!MirrorGuard::heldByThisThread(a));std::thread after([&]{assert(!gateTaken(a));});after.join();
 std::puts("PASS single gate: nested same-mutex guards skip re-locking, interleaved mutexes restore ownership, exceptions unwind, other threads still excluded.");
}

// 0.3.180 (D0): the gate census. Nested sites under an outer guard (a Device method) make exactly one
// real acquisition, RawScope and state blocks included; owner calls are never foreign; a std::thread's
// device, state-block and raw-scope calls count per class, the first recorded as {tid,site}, and the
// report fires once per class. Built with NORTHLIGHT_GATE_CENSUS_BY_HELD=1 (keyed on held_ instead of
// the thread id), the foreign calls are missed and this must fail.
static unsigned censusReports[MirrorGate::Sites];
static void gateCensus(){
 Backend b;DeviceMirror m;MirrorDevice game(&b,&m);ExtensionDevice ext(&b,&m);MirrorGate& g=m.gate;
 assert(g.ownerTid==MirrorGuard::threadId());
 g.report=[](void*,unsigned site,std::uint32_t)noexcept{++censusReports[site];};g.counting=true;
 auto taken=[&](MirrorSite s){return g.takeAcquired(s);};
 {MirrorGuard outer(g);DWORD v=0;assert(SUCCEEDED(game.SetRenderState(40,1))&&SUCCEEDED(ext.GetRenderState(40,&v))&&v==1);float f[4];assert(SUCCEEDED(ext.GetVertexShaderConstantF(0,f,1)));
  {ExtensionDevice::RawScope scope(ext);assert(SUCCEEDED(ext.SetRenderState(40,2)));assert(MirrorGuard::heldByThisThread(g));}
  assert(MirrorGuard::heldByThisThread(g));}
 assert(!MirrorGuard::heldByThisThread(g)&&taken(MirrorSite::Device)==1&&taken(MirrorSite::Raw)==0);
 {ExtensionDevice::RawScope scope(ext);assert(SUCCEEDED(ext.SetRenderState(40,3)));DWORD v=0;assert(SUCCEEDED(game.GetRenderState(40,&v))&&v==3);}
 assert(taken(MirrorSite::Raw)==1&&taken(MirrorSite::Device)==0);
 IDirect3DStateBlock9* block=nullptr;assert(SUCCEEDED(game.CreateStateBlock(D3DSBT_ALL,&block)));assert(taken(MirrorSite::Device)==1&&taken(MirrorSite::StateBlock)==0);
 assert(SUCCEEDED(block->Capture())&&SUCCEEDED(block->Apply()));assert(taken(MirrorSite::StateBlock)==2&&taken(MirrorSite::Device)==0);
 {MirrorGuard outer(g);assert(SUCCEEDED(block->Capture()));}assert(taken(MirrorSite::Device)==1&&taken(MirrorSite::StateBlock)==0);
 for(unsigned s=0;s<MirrorGate::Sites;++s)assert(!g.foreign[s]&&!censusReports[s]);assert(!g.firstReady);
 std::uint32_t foreignTid=0;
 for(unsigned round=0;round<2;++round){std::thread other([&]{foreignTid=MirrorGuard::threadId();DWORD v=0;assert(SUCCEEDED(game.GetRenderState(40,&v)));
   assert(SUCCEEDED(block->Capture()));{ExtensionDevice::RawScope scope(ext);assert(SUCCEEDED(ext.SetRenderState(41,1)));}});other.join();}
 assert(foreignTid&&foreignTid!=g.ownerTid);
 assert(g.foreign[unsigned(MirrorSite::Device)]==2&&g.foreign[unsigned(MirrorSite::StateBlock)]==2&&g.foreign[unsigned(MirrorSite::Raw)]==2);
 assert(censusReports[unsigned(MirrorSite::Device)]==1&&censusReports[unsigned(MirrorSite::StateBlock)]==1&&censusReports[unsigned(MirrorSite::Raw)]==1);
 assert(g.firstReady&&g.firstTid!=g.ownerTid&&g.firstSite==unsigned(MirrorSite::Device));
 for(unsigned s=0;s<MirrorGate::Sites;++s)assert(!g.takeAcquired(MirrorSite(s))); // foreign entries are not owner acquisitions
 g.noteBuffer();assert(!g.foreign[unsigned(MirrorSite::Buffer)]);
 {std::thread other([&]{g.noteBuffer();});other.join();}assert(g.foreign[unsigned(MirrorSite::Buffer)]==1&&censusReports[unsigned(MirrorSite::Buffer)]==1);
 g.noteFirst(g.presentTid);assert(g.presentTid==g.ownerTid);
 block->Release();assert(taken(MirrorSite::StateBlock)==1);
 std::puts("PASS gate census: one real acquisition per nested chain (device, RawScope, state blocks); owner calls never foreign; foreign device/state-block/raw/buffer entries counted per class, first {tid,site} recorded, one report per class.");
}
// A foreign thread's calls wait for, and never overlap, the owner's: TSan target together with concurrency().
// 0.3.182 (D1): "owner" runs the elision tests; "nesting", "waits" and "stress N" run one each (the
// counterfactual builds must fail them); "threads" is the TSan target.
int main(int argc,char** argv){
 const auto mode=[&](const char* name){return argc>1&&!std::strcmp(argv[1],name);};
 const unsigned rounds=argc>2?unsigned(std::atoi(argv[2])):10000;
 if(mode("census")){gateCensus();return 0;}
 if(mode("nesting")){ownerNesting();return 0;}
 if(mode("waits")){foreignWaits();return 0;}
 if(mode("stress")){ownerStress(rounds);return 0;}
 if(mode("exclusive")){ownerNesting(true);foreignWaits(true);ownerStress(rounds,true);std::puts("PASS exclusive owner mode");return 0;}
 if(mode("owner")){ownerIdentity();ownerNesting();foreignWaits();ownerStress(rounds);ownerNesting(true);foreignWaits(true);ownerStress(rounds,true);assert(!liveObjects);return 0;}
 if(mode("threads")){singleGate();gateCensus();rawScopeConcurrency();concurrency();ownerNesting();foreignWaits();ownerStress(rounds);ownerNesting(true);foreignWaits(true);ownerStress(rounds,true);std::puts("PASS threads");return 0;}
 ownerIdentity();assert(!liveObjects);ownerNesting();assert(!liveObjects);foreignWaits();assert(!liveObjects);ownerStress(rounds);assert(!liveObjects);ownerNesting(true);foreignWaits(true);ownerStress(rounds,true);assert(!liveObjects);gateCensus();assert(!liveObjects);knownScan();assert(!liveObjects);singleGate();writeThroughLearning();assert(!liveObjects);writeThroughImplicitState();assert(!liveObjects);writeThroughDifferential();assert(!liveObjects);rawScopeDispatchArguments();assert(!liveObjects);rawScopeBanksAndReentry();assert(!liveObjects);rawScopeRestoreAndControls();assert(!liveObjects);rawScopeConcurrency();assert(!liveObjects);rawScopeShadowWorkload();assert(!liveObjects);constantCertificates();assert(!liveObjects);auditCoverageAndSchedule();assert(!liveObjects);static_assert(!std::is_copy_constructible<DeviceMirror>::value,"mirror cannot copy mutex/state");bindingsAndScalars();assert(!liveObjects);constantBanks();assert(!liveObjects);constantCapabilities();assert(!liveObjects);recordingFailures();assert(!liveObjects);savedStateIntegration();assert(!liveObjects);crowdedWorkload();assert(!liveObjects);stateBlocks();assert(!liveObjects);implicitAndDisabled();assert(!liveObjects);auditChecks();assert(!liveObjects);auditEveryField();assert(!liveObjects);concurrency();assert(!liveObjects);std::puts("PASS actual device_mirror.h: authoritative cached queries; COM refs; normalized/failed setters; partial exact-bit banks; ALL/partial/recorded stateblocks including failed Apply; UP and Reset failures; target/FVF implicit changes; two proxies; disabled raw bypass; 30000 randomized banks; 8 threads x5000 iterations, serialized backend calls (80072 with learned write-through, 120008 read-through).");}
