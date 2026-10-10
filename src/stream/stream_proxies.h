#pragma once
// 0.3.192 (CS): the game-facing stream proxies and the state shared with the replay thread (StreamCore, Registry).
// The game sees these objects instead of the Target's resources. A proxy answers what it can from its creation
// arguments, records the rest (generated record_* functions) or makes a sync call, and wraps the Target-level object
// (`inner`) that the replay thread creates when the queued create executes. Lifetime is reference counting by the queue:
// when game references plus StreamState binds reach zero the game thread records one Cmd::Destroy; every command that
// uses the proxy precedes it, so the replay thread needs no per-argument atomic.
//
// Locks (the rule: the game never sees bytes that differ from the real resource except bytes D3D9 leaves undefined):
//   buffers   a persistent CPU shadow (DYNAMIC at creation or later, or <= 4 MiB non-DYNAMIC; all evictable LRU, made by one readback at a
//             write re-lock, for DYNAMIC also at a DISCARD lock, while the budget admits it); every lock returns shadow memory and Unlock
//             records the locked range. Without a shadow only the first write lock and DISCARD are asynchronous (a pooled staging Block);
//             every other lock passes through to the replay thread (counted by reason).
//   textures  the first write lock of a level/face after creation (or any DISCARD lock) is asynchronous through a staging
//             Block with our own pitch; everything else passes through. Surfaces of render targets and depth buffers always do.
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include "command_queue.h"
#include "command_stream.inl"
#include "tex_readback_diag.h"

namespace NorthlightStream {
// D3D9 ABI values the stream needs (identical in d3d9.h / d3d9types.h; kept here so the code reads the same under the test stub).
namespace D3 {
constexpr DWORD kUsageRT=0x1,kUsageDS=0x2,kUsageDynamic=0x200,kUsageAutoGen=0x400;
constexpr DWORD kLockReadOnly=0x10,kLockNoOverwrite=0x1000,kLockDiscard=0x2000;
constexpr DWORD kPoolDefault=0,kPoolManaged=1,kPoolSystem=2,kPoolScratch=3;
constexpr unsigned kTypeSurface=1,kTypeVolume=2,kTypeTexture=3,kTypeVolumeTexture=4,kTypeCube=5,kTypeVB=6,kTypeIB=7;
constexpr unsigned kFmtVertexData=100,kFmtIndex16=101,kFmtIndex32=102;
constexpr DWORD kSpdIUnknown=1,kGetDataFlush=1,kIssueEnd=1,kIssueBegin=2;
constexpr unsigned kQVcache=4,kQEvent=8,kQOcclusion=9,kQTimestamp=10,kQDisjoint=11,kQFreq=12;
struct FormatInfo {unsigned bytes,bw,bh;bool ok;};
inline FormatInfo formatInfo(unsigned f){
    switch(f){
    case 21:case 22:case 31:case 32:case 33:case 34:case 35:case 62:case 63:case 64:case 67:case 71:case 75:case 77:case 79:case 83:case 112:case 114:return {4,1,1,true};
    case 23:case 24:case 25:case 26:case 29:case 30:case 51:case 60:case 61:case 70:case 73:case 80:case 81:case 111:return {2,1,1,true};
    case 28:case 50:case 52:return {1,1,1,true};
    case 36:case 110:case 113:case 115:return {8,1,1,true};
    case 116:return {16,1,1,true};
    case 0x31545844:return {8,4,4,true};                                           // DXT1
    case 0x32545844:case 0x33545844:case 0x34545844:case 0x35545844:return {16,4,4,true};   // DXT2..DXT5
    default:return {0,1,1,false};
    }
}
inline UINT mipDim(UINT v,UINT level){const UINT r=v>>level;return r?r:1;}
inline UINT fullChain(UINT w,UINT h,UINT d){UINT m=w>h?w:h;if(d>m)m=d;UINT n=1;while(m>1){m>>=1;++n;}return n;}
}

enum class Kind:std::uint8_t{Surface,Texture,CubeTexture,VolumeTexture,Volume,VertexBuffer,IndexBuffer,VertexShader,PixelShader,VertexDeclaration,StateBlock,Query,SwapChain};
// Why a lock (or create) took the synchronous path: the CSTREAM census.
enum class PassReason:std::uint8_t{ReadOnly,NoShadow,Written,Format,Size,Budget,RenderTarget,Other,Count};
inline const char* passReasonName(unsigned r){static const char* n[]={"readonly","noshadow","written","format","size","budget","rendertarget","other"};return r<8?n[r]:"?";}

struct StreamCore;
struct ProxyBase;
struct SubRes;
// Proxies alive in the process (a leak check for the tests and the CSTREAM diagnostics).
inline std::atomic<long> liveProxyObjects{0};

// Creation arguments of any resource kind; what GetDesc/GetLevelDesc/GetLevelCount answer from.
struct Info {
    UINT w=0,h=0,d=1,levels=1,length=0;DWORD usage=0,pool=0,fvf=0,msq=0;unsigned fmt=0,ms=0,type=0;
};

constexpr std::size_t kLockSlack=4096;   // bytes past a staged range / a shadow, never replayed: a small game overrun stays inside the allocation (shadows zero them, staging does not need to)

// A Task runs on the replay thread while the game thread waits (pumped): pass-through locks, creates under memory
// pressure, GetData(FLUSH), Reset, the defaults batch, final release. fn executes with the replay-side core.
struct Task {void (*fn)(void*,StreamCore&);void* arg;};

// 0.3.204 (task 21): a vector whose resize() leaves new bytes uninitialized (default-init, no memset): the slices of a buffer's ring, whose contents are undefined (DISCARD) anyway. assign(n,0) still zero-fills.
template<class T> struct DefaultInitAllocator:std::allocator<T> {
    template<class U> struct rebind{using other=DefaultInitAllocator<U>;};
    using std::allocator<T>::allocator;
    template<class U> void construct(U* p) noexcept(std::is_nothrow_default_constructible<U>::value){::new(static_cast<void*>(p)) U;}
    template<class U,class... A> void construct(U* p,A&&... a){::new(static_cast<void*>(p)) U(std::forward<A>(a)...);}
};
using SliceMem=std::vector<unsigned char,DefaultInitAllocator<unsigned char>>;
struct PrivEntry {std::string key;std::vector<unsigned char> bytes;IUnknown* unk=nullptr;};

struct ProxyBase {
    StreamCore* core;Kind kind;IUnknown* unk=nullptr;   // unk: the proxy as the interface pointer the game holds (registry key)
    IUnknown* inner=nullptr;                            // the Target-level object: replay thread only, set when the create ran
    IUnknown* raw=nullptr;                              // the backend object behind `inner` (what Device would unwrap it to), cached with it; null = not provably a pure unwrap: replay through the Device
    std::atomic<LONG> refs{1},use{1},pendingDestroy{0};  // refs: what Release reports; use = refs + the in-use children (a parent's); StreamState binds are counted in `binds`, not here
    // 0.3.204 (task 21): StreamState binds of this proxy. Single writer at any time: the game thread, or the replay thread while the game thread waits for it (the defaults batch of Reset / init: runTask is the handoff) - so it
    // is updated with a plain load+store (no locked instruction on the per-draw bind path) and read by the replay thread only in Replayer::destroy (an atomic, so the read is race-free). The proxy is "in use" (not destroyable,
    // keeps its parent in use) while use>0 || binds>0; the Destroy is recorded by whichever operation takes the total to 0: useDec when binds==0, bindRelease when use==0. Audit: `use` is written by AddRef/Release (game thread; a final
    // Release records the Destroy, which only the producer thread may do), by the replay thread's implicit-proxy hand-outs (also under a game wait), and read by Replayer::destroy; nothing else touches use/binds. A final COM Release from a foreign thread was already unsupported (it records the Destroy on the
    // producer queue) and stays so: use and binds then have one writer at a time, which the split relies on.
    std::atomic<LONG> binds{0};
    // Replayer::destroy's test. `use` is read FIRST (seq_cst): a game AddRef -> bind -> Release of a re-handed proxy then shows either use>0 or, after the
    // Release (a release RMW sequenced after the bind store), binds>0 to the binds load that follows. Two independent loads in either order could miss both.
    LONG inUse()const{const LONG u=use.load(std::memory_order_seq_cst);const LONG b=binds.load(std::memory_order_seq_cst);return u+b;}
    std::uint64_t readySeq=0;                            // 0.3.206 (task 31): the recorded seq of the command that binds `inner` (Create / Derive); 0 = unknown (replay-side implicit proxies): never an express readback
    std::atomic<bool> dead{false};                       // the real create failed: commands on it are dropped
    ProxyBase* parent=nullptr;std::vector<ProxyBase*> kids;   // children (levels, faces, back buffers): owned by the parent
    Info info;DWORD priority=0;std::vector<PrivEntry> priv;
    ProxyBase(StreamCore* c,Kind k):core(c),kind(k){}
    virtual ~ProxyBase();
    ProxyBase(const ProxyBase&)=delete;ProxyBase& operator=(const ProxyBase&)=delete;
    Queue& streamQueue();
    struct CallScope callScope(std::uint16_t id);   // 0.3.204 (task 21): sampled game-thread timer of a generated method body (defined below StreamCore)
    // The DXVK model for the device's lifetime: a top-level proxy with public references (refs above `baseline`) holds one
    // reference on the StreamDevice, so the device, the queue and the replay thread outlive every object the game still holds.
    // StreamState binds count in `binds` only (see there): they never pin the device (or it would never die). A child's public reference
    // is its parent's public reference; a child in use keeps its parent in use.
    LONG baseline=0;   // refs the proxy holds itself without pinning (the swap chain's own)
    void pinDevice();
    void unpinDevice();
    ULONG comAddRef(){
        const LONG n=refs.fetch_add(1)+1;const bool pin=!parent&&n==baseline+1;
        if(n==1&&parent)parent->comAddRef();
        useInc();if(pin)pinDevice();return ULONG(n);}
    ULONG comRelease(){
        const LONG n=refs.fetch_sub(1)-1;const bool unpin=!parent&&n==baseline,up=n==0&&parent!=nullptr;
        useDec();   // may record the Destroy: while the queue still exists
        if(up)parent->comRelease();
        if(unpin)unpinDevice();   // last: this may be the device's final release
        return n<0?0:ULONG(n);}
    void adoptKid(){comAddRef();useInc();}   // a new child starts with one public reference and one use: the same on this parent
    void useInc(){if(use.fetch_add(1)==0&&binds.load(std::memory_order_relaxed)==0&&parent)parent->useInc();}
    void useDec();                                                        // game thread: use 1->0 with no binds records Destroy (or releases the container)
    void gone();                                                          // the total (use+binds) reached 0: record Destroy, or release the container
    void bindAdd(){const LONG b=binds.load(std::memory_order_relaxed);binds.store(b+1,std::memory_order_relaxed);if(b==0&&use.load(std::memory_order_relaxed)==0&&parent)parent->useInc();}   // StreamState bind: no locked instruction
    void bindRelease(){const LONG b=binds.load(std::memory_order_relaxed)-1;binds.store(b,std::memory_order_relaxed);if(b==0&&use.load(std::memory_order_relaxed)==0)gone();}
    // Local private data (nothing of it reaches the Target).
    template<class G> static std::string guidKey(const G& g){return std::string(reinterpret_cast<const char*>(&g),sizeof g);}
    template<class G> HRESULT setPrivate(const G& guid,const void* data,DWORD size,DWORD flags){
        if(!data&&size)return D3DERR_INVALIDCALL;
        freePrivate(guid);PrivEntry e;e.key=guidKey(guid);
        if(flags&D3::kSpdIUnknown){if(size!=sizeof(IUnknown*))return D3DERR_INVALIDCALL;e.unk=*static_cast<IUnknown* const*>(data);if(e.unk)e.unk->AddRef();}
        else e.bytes.assign(static_cast<const unsigned char*>(data),static_cast<const unsigned char*>(data)+size);
        try{priv.push_back(std::move(e));}catch(...){if(e.unk)e.unk->Release();return E_OUTOFMEMORY;}
        return D3D_OK;
    }
    template<class G> HRESULT getPrivate(const G& guid,void* data,DWORD* size){
        if(!size)return D3DERR_INVALIDCALL;
        const std::string k=guidKey(guid);
        for(auto& e:priv)if(e.key==k){
            const DWORD need=e.unk?DWORD(sizeof(IUnknown*)):DWORD(e.bytes.size());
            if(!data){*size=need;return D3D_OK;}
            if(*size<need){*size=need;return D3DERR_MOREDATA;}
            if(e.unk){e.unk->AddRef();std::memcpy(data,&e.unk,sizeof e.unk);}else if(need)std::memcpy(data,e.bytes.data(),need);
            *size=need;return D3D_OK;
        }
        return D3DERR_NOTFOUND;
    }
    template<class G> HRESULT freePrivate(const G& guid){
        const std::string k=guidKey(guid);
        for(std::size_t i=0;i<priv.size();++i)if(priv[i].key==k){if(priv[i].unk)priv[i].unk->Release();priv.erase(priv.begin()+i);return D3D_OK;}
        return D3DERR_NOTFOUND;
    }
    void dropPrivate(){for(auto& e:priv)if(e.unk)e.unk->Release();priv.clear();}
    // The proxy behind an interface pointer the game passed (a virtual QueryInterface with a private IID: no lock, no lookup);
    // nullptr for an object that is not a stream proxy. No reference is taken.
    static ProxyBase* of(IUnknown* u);        // lock-free shape check (below the proxy classes)
    static ProxyBase* ofSlow(IUnknown* u);    // a virtual QueryInterface with a private IID: any pointer, however unknown its shape
    template<class... T> static bool iidIs(REFIID id){return id==__uuidof(IUnknown)||((id==__uuidof(T))||...);}
    HRESULT devGet(IDirect3DDevice9** pp);
    // Helpers every resource macro class needs: generic hooks with nothing to do, and the sync forwarder.
    template<Cmd C,class... A> void observe(CmdTag<C>,A&&...){}
    template<Cmd C,class... A> typename MethodTraits<C>::Ret syncCall(CmdTag<C> t,A... a);
};

class Registry {
    std::mutex m_;std::unordered_map<const void*,ProxyBase*> byProxy_,byInner_;
public:
    void addProxy(ProxyBase* p){std::lock_guard<std::mutex> l(m_);byProxy_[p->unk]=p;}
    ProxyBase* findProxy(const void* iface){std::lock_guard<std::mutex> l(m_);auto i=byProxy_.find(iface);return i==byProxy_.end()?nullptr:i->second;}
    // Direct replay: Registry::rawOf resolves the backend object behind a Device-level one, once, right here (replay thread, outside the lock).
    std::function<IUnknown*(IUnknown*,Kind)> rawOf;
    static bool takesRaw(Kind k){return k==Kind::Surface||k==Kind::Texture||k==Kind::CubeTexture||k==Kind::VolumeTexture||k==Kind::VertexBuffer||k==Kind::IndexBuffer||k==Kind::VertexShader||k==Kind::PixelShader||k==Kind::VertexDeclaration;}
    void bindInner(ProxyBase* p,IUnknown* inner){
        {std::lock_guard<std::mutex> l(m_);p->inner=inner;byInner_[inner]=p;}
        p->raw=rawOf&&inner&&takesRaw(p->kind)?rawOf(inner,p->kind):nullptr;
    }   // the newest proxy of an object wins identity
    void unbindInner(ProxyBase* p){p->raw=nullptr;std::lock_guard<std::mutex> l(m_);if(p->inner){auto i=byInner_.find(p->inner);if(i!=byInner_.end()&&i->second==p)byInner_.erase(i);}}
    ProxyBase* findInner(const void* inner){std::lock_guard<std::mutex> l(m_);auto i=byInner_.find(inner);return i==byInner_.end()?nullptr:i->second;}
    // Replay thread (destroy): forget the proxy; the inner entry only if it still names this proxy.
    void erase(ProxyBase* p){std::lock_guard<std::mutex> l(m_);byProxy_.erase(p->unk);if(p->inner){auto i=byInner_.find(p->inner);if(i!=byInner_.end()&&i->second==p)byInner_.erase(i);}}
    // The Target-level object behind a proxy pointer the game stored in its own memory (an opaque value, never dereferenced).
    const void* innerOf(const void* maybeProxy){std::lock_guard<std::mutex> l(m_);auto i=byProxy_.find(maybeProxy);return i==byProxy_.end()||!i->second->inner?maybeProxy:static_cast<const void*>(i->second->inner);}
    // Init failure only (the replay thread is gone or about to be): release every Target object the proxies hold, and hand the proxies out to be deleted.
    void releaseInners(){std::lock_guard<std::mutex> l(m_);for(auto& e:byProxy_)if(e.second->inner){e.second->inner->Release();e.second->inner=nullptr;e.second->dead.store(true);}byInner_.clear();}
    void takeAll(std::vector<ProxyBase*>& out){std::lock_guard<std::mutex> l(m_);for(auto& e:byProxy_)out.push_back(e.second);byProxy_.clear();byInner_.clear();}
    std::size_t proxies(){std::lock_guard<std::mutex> l(m_);return byProxy_.size();}
};

// 0.3.192 (CS): intrusive LRU list (the CPU shadows' victim order). head = least recently used, pushBack = most recent; every mutation under StreamCore::texMutex
// (the game thread touches and evicts, the replay thread removes a dying proxy's shadow). T has a member `lru`. Eviction is O(victims), a touch O(1); a touch of the
// element that is already the tail needs no lock (only the owning game thread appends, and a replay-thread removal never concerns a shadow that is being locked).
template<class T> struct LruNode {T* prev=nullptr;T* next=nullptr;bool linked=false;};
template<class T> struct LruList {
    T* head=nullptr;std::atomic<T*> tail{nullptr};
    void pushBack(T* x){auto& n=x->lru;n.prev=tail.load(std::memory_order_relaxed);n.next=nullptr;if(n.prev)n.prev->lru.next=x;else head=x;tail.store(x,std::memory_order_relaxed);n.linked=true;}
    void remove(T* x){
        auto& n=x->lru;if(!n.linked)return;
        if(n.prev)n.prev->lru.next=n.next;else head=n.next;
        if(n.next)n.next->lru.prev=n.prev;else tail.store(n.prev,std::memory_order_relaxed);
        n.prev=n.next=nullptr;n.linked=false;
    }
    bool isTail(const T* x)const{return tail.load(std::memory_order_relaxed)==x;}
};
struct BufferState;
// 0.3.192 (CS): game-side scratch for SMALL staged buffer locks (first write / DISCARD without a shadow): a handful of slots, taken and given back without a lock
// or a Block (the pooled Blocks share a mutex with the replay thread's retire). Game thread takes; give() may run on the replay thread (a buffer destroyed while locked).
struct LockScratch {
    static constexpr std::size_t kMaxBytes=std::size_t(64)<<10,kSlots=4;
    std::vector<unsigned char> slot[kSlots];std::atomic<bool> busy[kSlots]{};
    unsigned char* take(std::size_t bytes,unsigned char& idx){
        if(bytes>kMaxBytes)return nullptr;
        for(unsigned i=0;i<kSlots;++i){
            if(busy[i].load(std::memory_order_acquire))continue;
            const std::size_t need=((bytes+kLockSlack)+4095)&~std::size_t(4095);   // the slack: a small game overrun stays inside the allocation (never replayed, so not zeroed)
            if(slot[i].size()<need){try{slot[i].resize(need);}catch(...){return nullptr;}}
            busy[i].store(true,std::memory_order_relaxed);idx=(unsigned char)i;return slot[i].data();
        }
        return nullptr;
    }
    void give(unsigned char idx){busy[idx].store(false,std::memory_order_release);}
    void trim(){for(unsigned i=0;i<kSlots;++i)if(!busy[i].load(std::memory_order_acquire))std::vector<unsigned char>().swap(slot[i]);}   // game thread (pressure)
};

// Everything the game thread and the replay thread share. One per stream.
struct StreamCore {
    Queue q;Registry reg;
    IDirect3DDevice9* target=nullptr;   // the Device (Target): replay thread only after the handoff
    IDirect3DDevice9* game=nullptr;     // the StreamDevice, for GetDevice
    IDirect3DDevice9* ext=nullptr;      // the ExtensionDevice (same real device, same mirror): direct replay calls it with raw pointers; null = through the Device
    std::atomic<bool> replayFailure{false};      // a replayed setter failed: StreamState invalidates itself at its next Get
    std::atomic<bool> memoryPressure{false};     // published by the replay side (Device's memory guard), consumed at Present
    std::atomic<unsigned> availableTextureMem{0};
    std::atomic<std::uint64_t> syncOnly[14]{};    // bit per audited scalar slot, see StreamState
    std::atomic<std::uint64_t> framesReplayed{0};
    // The live level shadows (see SubRes): inserted and evicted on the game thread, removed by a proxy's destructor on the replay thread.
    // Live level shadows in LRU order (never re-locked "fresh" ones are evicted before re-locked ones) and live buffer shadows (the large allowance apart): all under texMutex.
    std::mutex texMutex;LruList<SubRes> texFresh,texRelocked;LruList<BufferState> bufRegular,bufLarge;
    // 0.3.200 (pipeline): allocations of level shadows the game thread evicted, kept (under texMutex) for the next level shadow instead of a free + malloc pair;
    // at most kTexSpares of at most kTexSpareMaxBytes each (~0.5 MiB outside the texture-shadow cap), none kept under memory pressure (trimTexSpares).
    static constexpr unsigned kTexSpares=2;static constexpr std::size_t kTexSpareMaxBytes=(std::size_t(256)<<10)+kLockSlack;
    std::vector<unsigned char> texSpare[kTexSpares];
    LockScratch scratch;             // game thread: small staged buffer locks
    unsigned framesAhead=1;          // 0.3.204 (task 21): StreamFramesAhead (clamped), set once by the StreamDevice: the buffers' slice rings hold framesAhead+2 slices at most
    // 0.3.204 (task 21): large-buffer slices that were dropped or swapped away while the replay thread may still read them (a zero-copy unlock refers to the bytes until it retires): the storage waits here
    // (under texMutex) with the seq of the last command that reads it, and is freed by trimRetired once replayedSeq has passed it. Their bytes leave shadowBytes/largeShadowBytes at once and count in the ring budget (retired*Bytes) until freed.
    struct Retired {SliceMem mem;std::uint64_t seq;std::size_t bytes;bool large;};   // large: counted in retiredLargeBytes (the ring budget of the large allowance), else in retiredRegularBytes
    std::vector<Retired> retired;
    // 0.3.204 (task 21): Diagnostics split timers. timing is the game thread's own copy of the Diagnostics switch, refreshed at each Present (StreamDevice::presentCommon);
    // timingN counts the generated calls for the 1-in-16 sampling. Both are game-thread only: the replay thread never runs a game-facing method or a Lock/Unlock path.
    bool timing=false;unsigned timingN=0;
    TexReadbackDiag texDiag;   // 0.3.206 (task 31): per-level texture readback report (game thread; recorded only while timing)
    std::uint64_t clockNs=0;   // the cost of one nowNs() as the scopes see it (measured once at the first timed Present), subtracted from every timed span
    std::uint64_t frameNo=0;         // game thread: Presents so far (the idle clock of buffer shadows)
    DWORD (*readBackLock)()=nullptr;             // flags of the stream's own READONLY read-backs of buffers (NorthlightUpload::readBackLock in the DLL); null = READONLY
    void (*logLine)(const char*)=nullptr;        // diagnostics sink (renderer.cpp's logf); may be null
    static constexpr std::size_t slicesFor(unsigned framesAhead){return std::size_t(clampFramesAhead(framesAhead))+2;}   // 0.3.204 (task 21): the least slices a buffer may own in all (current + ring)
    // 0.3.204 (task 21): a buffer that DISCARDs n times per frame has n x (framesAhead+1) + 1 slices in flight at most; never below slicesFor, never above kRingHardMax (still subject to the memory rules)
    static constexpr std::size_t kRingHardMax=12;
    static constexpr std::size_t allowedSlices(unsigned framesAhead,unsigned discardsPerFrame){
        const std::size_t want=std::size_t(discardsPerFrame)*(clampFramesAhead(framesAhead)+1)+1,base=slicesFor(framesAhead);
        return (want>base?want:base)<kRingHardMax?(want>base?want:base):kRingHardMax;}
    explicit StreamCore(std::size_t budget=BudgetBytes):q(budget){}
    void log(const char* text){if(logLine)logLine(text);}
    // Replay thread: Target-level interface pointer -> game-facing proxy (taking over the reference the Target returned).
    template<class T> T* toProxy(T* innerRef);
    template<class T> ProxyBase* toProxyBound(T* innerRef){return innerRef?proxyFor(innerRef,true):nullptr;}   // for StreamState: one use (the bind), no game reference
    template<class T> ProxyBase* proxyFor(T* innerRef,bool bound);
    ProxyBase* makeImplicit(IUnknown* inner);
    std::atomic<HRESULT> presentResult{D3D_OK};
    // 0.3.204 (task 21): the cooperative level TestCooperativeLevel reports, written ONLY by the replay thread (after each real Present, in the Reset task and in the
    // sync TestCooperativeLevel task); the game thread answers D3D_OK from it locally and goes synchronous for anything else. Loss shows up at most StreamFramesAhead frames late.
    std::atomic<HRESULT> coopState{D3D_OK};
    const char* (*callerModule)(void* address)=nullptr;   // diagnostics: "module+0xoffset" of a TestCooperativeLevel caller and, since 0.3.206 (task 31), of a texture lock caller in the TEXREADBACK lines (renderer.cpp, Windows; logged only with Diagnostics on); null in tests
    // Real HRESULT of the last Present commands (the game reads the one StreamFramesAhead frames back). 0.3.200 (pipeline): the slot is the Present's ordinal
    // (the replay's framesReplayed before it counts this one = the game's frameNo when it recorded it), seq still checked: with at most kMaxFramesAhead+1
    // Presents in flight no newer one can take the slot first (keyed by seq%kRing it could, and the game then read D3D_OK).
    static constexpr unsigned kRing=8;
    static_assert(kRing>kMaxFramesAhead+1,"every Present in flight has its own slot");
    struct PresentEntry {std::atomic<std::uint64_t> seq{0};std::atomic<HRESULT> hr{D3D_OK};};
    PresentEntry presentRing[kRing];
    // 0.3.200 (frame skip): StreamFrameSkip. The game thread classifies every frame it records: frameHazards collects why it must be drawn (FrameHazard), and at
    // its Present the bits go to frameRing[ordinal%kRing] (tag = ordinal+1, stored last), then presentsRecorded = ordinal+1 (after the Present is published,
    // before the game waits). The replay thread reads both at a frame's first command (Replayer::beginFrame); a slot is reused only kRing frames later.
    enum FrameHazard:std::uint32_t{kHazardTarget=1,kHazardQuery=2,kHazardCopy=4};   // offscreen render target / depth texture bound; a non-event query issued; a copy from the back buffer or a depth surface
    struct FrameEntry {std::atomic<std::uint64_t> tag{0};std::atomic<std::uint32_t> hazards{0};};
    FrameEntry frameRing[kRing];std::atomic<std::uint64_t> presentsRecorded{0};
    std::uint32_t frameHazards=0;   // game thread: the frame being recorded
    void replayFailureOnFail(HRESULT hr){if(FAILED(hr)){add(q.stats.replayFailures);replayFailure.store(true);}}
    template<class T> T* inner(T* proxy){
        if(!proxy)return nullptr;
        ProxyBase* b=ProxyBase::of(static_cast<IUnknown*>(proxy));
        if(!b){add(q.stats.foreignPointers);return proxy;}   // not ours (a foreign object): pass it through unchanged
        return b->dead.load(std::memory_order_relaxed)||!b->inner?nullptr:static_cast<T*>(b->inner);
    }
};
inline Queue& ProxyBase::streamQueue(){return core->q;}
// 0.3.204 (task 21): game-thread split timers, active only while core.timing (Diagnostics on); off: one bool test. Waits (sync, backpressure, present) are subtracted so they stay in their own counters.
inline std::uint64_t gameWaitsNs(const Counters& s){return get(s.syncNs)+get(s.backpressureNs)+get(s.presentNs);}
struct CallScope {   // 1 in 16 generated method bodies: 16 x (elapsed - waits - snapshot capture) into recordSampledNs
    Counters* st=nullptr;std::uint64_t t0=0,w0=0,clock=0;std::uint16_t id=0;
    CallScope(StreamCore& c,std::uint16_t cmd){if(c.timing&&(++c.timingN&15)==0){st=&c.q.stats;clock=c.clockNs;id=cmd<kMaxCmdIds?cmd:0;w0=gameWaitsNs(*st)+get(st->snapNs);t0=nowNs();}}
    CallScope(const CallScope&)=delete;CallScope& operator=(const CallScope&)=delete;
    ~CallScope(){if(st){const std::uint64_t e0=nowNs()-t0,e=e0>clock?e0-clock:0,w=gameWaitsNs(*st)+get(st->snapNs)-w0,ns=(e>w?e-w:0)*16;own(st->recordSampledNs,ns);own(st->cmdSampledNs[id],ns);own(st->cmdSamples[id]);}}
};
struct LockScope {   // every buffer/image Lock or Unlock: elapsed minus the waits inside into lockNs
    Counters* st=nullptr;std::uint64_t t0=0,w0=0,clock=0;
    explicit LockScope(StreamCore& c){if(c.timing){st=&c.q.stats;clock=c.clockNs;w0=gameWaitsNs(*st);t0=nowNs();}}
    LockScope(const LockScope&)=delete;LockScope& operator=(const LockScope&)=delete;
    ~LockScope(){if(st){const std::uint64_t e0=nowNs()-t0,e=e0>clock?e0-clock:0,w=gameWaitsNs(*st)-w0;own(st->lockNs,e>w?e-w:0);}}
};
inline CallScope ProxyBase::callScope(std::uint16_t id){return CallScope(*core,id);}
inline void ProxyBase::pinDevice(){if(core->game)core->game->AddRef();}
inline void ProxyBase::unpinDevice(){if(core->game)core->game->Release();}
inline ProxyBase::~ProxyBase(){dropPrivate();}
inline void ProxyBase::useDec(){
    if(use.fetch_sub(1)!=1||binds.load(std::memory_order_relaxed)!=0)return;
    gone();
}
inline void ProxyBase::gone(){
    if(parent){parent->useDec();return;}
    pendingDestroy.fetch_add(1);
    Queue& q=core->q;
    auto* p=static_cast<ProxyBase**>(q.reserve((std::uint16_t)Cmd::Destroy,sizeof(ProxyBase*)));*p=this;q.commit();
}
template<Cmd C,class... A> typename MethodTraits<C>::Ret ProxyBase::syncCall(CmdTag<C> t,A... a){return runSync(core->q,t,a...);}

// Runs fn(StreamCore&) on the replay thread after everything recorded so far; returns once it ran. false when it could
// not wait (nested in a pumped wait): fn did not run. fn must not throw.
template<class F> inline bool runTask(StreamCore& c,F&& fn,Cmd label=Cmd::Quiesce){   // label: the census name of this sync call
    if(inPumpedWait){add(c.q.stats.nestedSyncs);return false;}
    using Fn=typename std::remove_reference<F>::type;
    Task t{[](void* a,StreamCore& core){(*static_cast<Fn*>(a))(core);},&fn};
    Task* p=&t;own(c.q.stats.census[(std::size_t)label]);own(c.q.stats.syncCalls);
    std::memcpy(c.q.reserve((std::uint16_t)Cmd::Quiesce,sizeof p,kFlagWaitTarget),&p,sizeof p);c.q.commit();   // 0.3.196 (task 12): flagged, the wait below targets it
    c.q.waitReplayed(c.q.recordedSeq(),WaitKind::Sync);
    return true;
}

// ---- Locks: shared bookkeeping ----
// A texture level / cube face / volume level, or a standalone surface.
struct SubRes {
    bool written=false;                 // a GPU write or an earlier lock: later write locks need a shadow or pass through
    enum Mode:std::uint8_t{Free,Staged,Pass,Shadowed} mode=Free;
    Block* stage=nullptr;DWORD flags=0;bool hasRect=false;RECT rect{};D3DBOX box{};UINT rows=0,slices=1,rowBytes=0,pitch=0,slicePitch=0;
    // A CPU copy of the whole level in our own tight layout (block rows of levelRowBytes), plus slack. Locks return pointers into it
    // and Unlock records the locked rows; shadowDead: never again (GPU-written, readback failed, ...).
    std::vector<unsigned char> shadow;bool shadowOn=false,shadowDead=false;UINT levelRowBytes=0,levelRows=0,levelSlices=0;std::size_t shadowOffset=0;
    // Eviction bookkeeping (StreamCore::texFresh/texRelocked): the shadow may be evicted at any time it is not locked; the level then simply takes
    // the readback path at its next re-lock. relocked: served a second lock (or made by a readback): evicted last. fromFresh: kept from a first write.
    LruNode<SubRes> lru;bool relocked=false,fromFresh=false;   // lru: StreamCore::texFresh (never re-locked) or texRelocked, least recently locked first
    // 0.3.196 (task 12): the list this level's shadow was on when makeRoomForShadow evicted it (None: never shadowed, or shadowed again since); only for the readback-cause counters.
    enum Gone:std::uint8_t{GoneNone,GoneFresh,GoneRelocked,GoneSkipped} gone=GoneNone;
    // 0.3.206 (task 31): Diagnostics fields, ALWAYS present (memory: about 64 bytes per texture level, 8 more than a bare id; CPU when Diagnostics is off: one branch per lock, nothing else is touched).
    // diagId: id in TexReadbackDiag (0 = never recorded); diagHist: the level's lock counters of the report window (reset per window); diagLastLock / diagEpoch: frameNo+1 of the previous recorded lock and the
    // Diagnostics epoch it was in (they survive window resets, so gaps span report windows); diagFirstCaller: return address of the first recorded lock.
    std::uint32_t diagId=0,diagEpoch=0;TexReadbackDiag::LockHist diagHist;void* diagFirstCaller=nullptr;std::uint64_t diagLastLock=0;
    // 0.3.206 (task 31): the recorded seq of the last command that writes this level's real content (set at every unlock that records one; ~0 = a GPU write); 0 = none recorded.
    // A readback of the level may run as an express task once replayedSeq() has passed it (and the owner's readySeq): see lockImage.
    std::uint64_t contentSeq=0;
    std::uint64_t lastLockFrame=0;   // StreamCore::frameNo of the shadow's creation or last lock: a fresh keep may be evicted for a new one only once this is kFreshEvictAgeFrames old
};
// What a staged Unlock records (followed by nothing: the bytes are in the command's Block).
struct UnlockImageArgs {ProxyBase* proxy;UINT level,face;DWORD flags;UINT hasRect,rows,slices,rowBytes,pitch,slicePitch;LONG l,t,r,b;UINT bf,bk;UINT route;};
struct UnlockBufferArgs {ProxyBase* proxy;UINT off,size,flags,inlineData;};
// 0.3.204 (task 21): a zero-copy unlock: the replay thread reads the bytes from the game-side slice itself (src = the slice base; the range starts at src+off). The slice is kept alive until this command retires (see BufferState::sliceSeq).
struct UnlockBufferRefArgs {ProxyBase* proxy;const unsigned char* src;UINT off,size,flags,pad;};
enum ImageRoute:UINT{RouteSurface=0,RouteTexture=1,RouteCube=2,RouteVolumeTexture=3,RouteVolume=4};

inline void countPass(ProxyBase& p,PassReason r){own(p.core->q.stats.passThrough[unsigned(r)]);}   // game thread only
// Both need StreamCore::texMutex held. A shadow is game-side memory and no queued command refers to it (Unlock copies the rows into a Block).
inline LruList<SubRes>& texListOf(StreamCore& c,const SubRes& s){return s.relocked?c.texRelocked:c.texFresh;}
// 0.3.200 (pipeline): recycle (the game thread's evictions only): the allocation goes to an empty spare slot, or replaces a smaller spare, unless under pressure or too large.
inline void spareTexShadowLocked(StreamCore& c,std::vector<unsigned char>& v){
    const std::size_t cap=v.capacity();if(!cap||cap>StreamCore::kTexSpareMaxBytes||c.q.pressure())return;
    std::vector<unsigned char>* slot=nullptr;
    for(auto& x:c.texSpare)if(!slot||x.capacity()<slot->capacity())slot=&x;   // the empty or smallest slot
    if(slot->capacity()<cap){slot->swap(v);add(c.q.stats.texShadowSpared);}
}
inline void dropShadowLocked(StreamCore& c,SubRes& s,bool recycle=false){
    if(!s.shadowOn)return;
    texListOf(c,s).remove(&s);
    c.q.addTexShadowBytes(-std::int64_t(std::size_t(s.levelRows)*s.levelRowBytes*s.levelSlices));
    if(recycle)spareTexShadowLocked(c,s.shadow);
    std::vector<unsigned char>().swap(s.shadow);s.shadowOn=false;
}
// 0.3.200 (pipeline): game thread, before a level shadow is made: the smallest spare that holds `bytes` without wasting more than half of it moves into `out`
// (its assign() then neither frees nor allocates). Zero-filled as before (assign): a fresh level's unwritten bytes stay deterministic.
inline void takeTexSpare(StreamCore& c,std::vector<unsigned char>& out,std::size_t bytes){
    if(out.capacity()>=bytes)return;
    std::lock_guard<std::mutex> l(c.texMutex);std::vector<unsigned char>* best=nullptr;
    for(auto& x:c.texSpare)if(x.capacity()>=bytes&&x.capacity()-bytes<=bytes/2&&(!best||x.capacity()<best->capacity()))best=&x;
    if(best){out.swap(*best);std::vector<unsigned char>().swap(*best);add(c.q.stats.texShadowSpareReuses);}
}
inline void trimTexSpares(StreamCore& c){std::lock_guard<std::mutex> l(c.texMutex);for(auto& x:c.texSpare)std::vector<unsigned char>().swap(x);}   // game thread (pressure)
inline void dropSubShadow(StreamCore& c,SubRes& s){std::lock_guard<std::mutex> l(c.texMutex);dropShadowLocked(c,s);}
// Game thread: makes `bytes` fit the texture-shadow cap by evicting least-recently-locked shadows, never-re-locked ones (fresh keeps) first,
// never one that is locked now or `keep`. mayEvictRelocked=false leaves the re-locked ones alone (no caller: 0.3.196 (task 12): a fresh keep no longer evicts anything,
// it is taken only into free room, see lockImage). false = still no room.
// The victim is the first eligible entry of the LRU list (fresh list, then the re-locked one): O(victims), not a rescan of every level.
inline bool makeRoomForShadow(StreamCore& c,std::size_t bytes,bool mayEvictRelocked,const SubRes* keep){
    while(!c.q.texShadowAdmit(bytes)){
        std::lock_guard<std::mutex> l(c.texMutex);
        SubRes* victim=nullptr;
        for(SubRes* s=c.texFresh.head;s&&!victim;s=s->lru.next)if(s!=keep&&s->mode!=SubRes::Shadowed)victim=s;
        if(!victim&&mayEvictRelocked)for(SubRes* s=c.texRelocked.head;s&&!victim;s=s->lru.next)if(s!=keep&&s->mode!=SubRes::Shadowed)victim=s;
        if(!victim)return false;
        victim->gone=victim->relocked?SubRes::GoneRelocked:SubRes::GoneFresh;   // which list it was on: the cause of the readback its next write lock costs
        dropShadowLocked(c,*victim,true);add(c.q.stats.texShadowEvicted);   // 0.3.200 (pipeline): the allocation may serve the next shadow
    }
    return true;
}
// 0.3.196 (task 12): a FRESH keep may evict never-re-locked shadows, but only ones not locked for kFreshEvictAgeFrames Presents (the list head is the oldest, so a young head ends it): the
// cap never freezes on stale write-once levels, and a level written and re-locked within the window keeps its shadow. false = skip the keep.
// Chosen over "fresh keeps only into free room": that rule left the cap full of stale write-once keeps, so a newly written level that is locked again paid a SyncLock
// readback (a queue drain) every time (0 -> 1 per level in tests). The age limit bounds the keep churn to about one cap turnover per 60 Presents instead of removing it.
constexpr std::uint64_t kFreshEvictAgeFrames=60;
inline bool makeRoomForFreshKeep(StreamCore& c,std::size_t bytes,const SubRes* keep){
    while(!c.q.texShadowAdmit(bytes)){
        std::lock_guard<std::mutex> l(c.texMutex);
        SubRes* victim=nullptr;
        for(SubRes* s=c.texFresh.head;s&&!victim;s=s->lru.next)if(s!=keep&&s->mode!=SubRes::Shadowed)victim=s;
        if(!victim||c.frameNo-victim->lastLockFrame<kFreshEvictAgeFrames)return false;
        victim->gone=SubRes::GoneFresh;dropShadowLocked(c,*victim,true);add(c.q.stats.texShadowEvicted);   // 0.3.200 (pipeline): the fresh keep that evicted it usually takes this allocation
    }
    return true;
}
inline void listShadow(StreamCore& c,SubRes& s){std::lock_guard<std::mutex> l(c.texMutex);s.lastLockFrame=c.frameNo;texListOf(c,s).pushBack(&s);}
// Game thread: the level's shadow was locked again: most recently used now; the first re-lock moves it to the re-locked list (evicted last).
inline void touchTexShadow(StreamCore& c,SubRes& s,bool becomesRelocked){
    if(!becomesRelocked&&texListOf(c,s).isTail(&s)){s.lastLockFrame=c.frameNo;return;}
    std::lock_guard<std::mutex> l(c.texMutex);s.lastLockFrame=c.frameNo;
    texListOf(c,s).remove(&s);if(becomesRelocked)s.relocked=true;texListOf(c,s).pushBack(&s);
}
// A GPU-side write (StretchRect, UpdateTexture, ColorFill, mip generation, ...): the CPU copy is stale for good.
inline void markGpuWritten(StreamCore& c,SubRes& s){s.written=true;s.contentSeq=~std::uint64_t(0);dropSubShadow(c,s);s.shadowDead=true;}

// ---- the proxies ----
using IidType=typename std::remove_cv<typename std::remove_reference<REFIID>::type>::type;
template<class T> inline T makeProxyIid(){
    if constexpr(std::is_arithmetic<T>::value)return T(0x5ca1ab1e);
    else return T{0x8a1c2e0du,0x4f10,0x4d5e,{0x91,0xa3,0x6c,0x2b,0x77,0x1e,0x5d,0x09}};
}
inline const IidType& proxyIid(){static const IidType iid=makeProxyIid<IidType>();return iid;}
inline ProxyBase* ProxyBase::ofSlow(IUnknown* u){
    if(!u)return nullptr;void* out=nullptr;
    return u->QueryInterface(proxyIid(),&out)==S_OK?static_cast<ProxyBase*>(out):nullptr;
}
inline HRESULT ProxyBase::devGet(IDirect3DDevice9** pp){if(!pp)return D3DERR_INVALIDCALL;*pp=core->game;core->game->AddRef();return D3D_OK;}

#define NL_PROXY_COM(Main,...) \
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override{ \
        if(!out)return E_POINTER;*out=nullptr; \
        if(id==proxyIid()){*out=static_cast<ProxyBase*>(this);return S_OK;} \
        if(iidIs<Main,##__VA_ARGS__>(id)){*out=static_cast<Main*>(this);comAddRef();return S_OK;} \
        add(core->q.stats.qiMisses);return E_NOINTERFACE;} \
    ULONG STDMETHODCALLTYPE AddRef() override{return comAddRef();} \
    ULONG STDMETHODCALLTYPE Release() override{return comRelease();}
// The local methods every resource shares (the generator classifies them local): device, private data, priority, type.
#define NL_RES_LOCALS(S) \
    HRESULT local(CmdTag<Cmd::S##_GetDevice>,IDirect3DDevice9** pp){return devGet(pp);} \
    HRESULT local(CmdTag<Cmd::S##_SetPrivateData>,REFGUID g,const void* d,DWORD n,DWORD f){return setPrivate(g,d,n,f);} \
    HRESULT local(CmdTag<Cmd::S##_GetPrivateData>,REFGUID g,void* d,DWORD* n){return getPrivate(g,d,n);} \
    HRESULT local(CmdTag<Cmd::S##_FreePrivateData>,REFGUID g){return freePrivate(g);} \
    DWORD local(CmdTag<Cmd::S##_SetPriority>,DWORD v){const DWORD o=priority;priority=v;return o;} \
    DWORD local(CmdTag<Cmd::S##_GetPriority>){return priority;} \
    D3DRESOURCETYPE local(CmdTag<Cmd::S##_GetType>){return (D3DRESOURCETYPE)info.type;}
#define NL_TEX_LOCALS(S) \
    void observe(CmdTag<Cmd::S##_GenerateMipSubLevels>){for(std::size_t i=0;i<subs.size();++i)if(i%info.levels>0)markGpuWritten(*core,subs[i]);} \
    DWORD local(CmdTag<Cmd::S##_SetLOD>,DWORD v){const DWORD o=lod;if(info.pool==D3::kPoolManaged)lod=v<info.levels?v:info.levels-1;return o;} \
    DWORD local(CmdTag<Cmd::S##_GetLOD>){return lod;} \
    DWORD local(CmdTag<Cmd::S##_GetLevelCount>){return (info.usage&D3::kUsageAutoGen)?1:info.levels;} \
    D3DTEXTUREFILTERTYPE local(CmdTag<Cmd::S##_GetAutoGenFilterType>){return (D3DTEXTUREFILTERTYPE)autoGenFilter;} \
    void observe(CmdTag<Cmd::S##_SetAutoGenFilterType>,D3DTEXTUREFILTERTYPE f){autoGenFilter=unsigned(f);}

inline unsigned queryDataSize(unsigned type){
    switch(type){case D3::kQEvent:case D3::kQOcclusion:case D3::kQDisjoint:return 4;case D3::kQTimestamp:case D3::kQFreq:return 8;case D3::kQVcache:return 16;default:return 0;}
}

// ---- image locks (textures, cube faces, volume levels, standalone surfaces) ----
inline HRESULT callLock(UINT route,IUnknown* in,UINT level,UINT face,D3DLOCKED_RECT* lr,D3DLOCKED_BOX* lb,const RECT* rect,const D3DBOX* box,DWORD flags){
    switch(route){
    case RouteSurface:return static_cast<IDirect3DSurface9*>(in)->LockRect(lr,rect,flags);
    case RouteTexture:return static_cast<IDirect3DTexture9*>(in)->LockRect(level,lr,rect,flags);
    case RouteCube:return static_cast<IDirect3DCubeTexture9*>(in)->LockRect((D3DCUBEMAP_FACES)face,level,lr,rect,flags);
    case RouteVolumeTexture:return static_cast<IDirect3DVolumeTexture9*>(in)->LockBox(level,lb,box,flags);
    default:return static_cast<IDirect3DVolume9*>(in)->LockBox(lb,box,flags);
    }
}
inline HRESULT callUnlock(UINT route,IUnknown* in,UINT level,UINT face){
    switch(route){
    case RouteSurface:return static_cast<IDirect3DSurface9*>(in)->UnlockRect();
    case RouteTexture:return static_cast<IDirect3DTexture9*>(in)->UnlockRect(level);
    case RouteCube:return static_cast<IDirect3DCubeTexture9*>(in)->UnlockRect((D3DCUBEMAP_FACES)face,level);
    case RouteVolumeTexture:return static_cast<IDirect3DVolumeTexture9*>(in)->UnlockBox(level);
    default:return static_cast<IDirect3DVolume9*>(in)->UnlockBox();
    }
}
// DISCARD and NOOVERWRITE only mean something in the default pool; elsewhere the old bytes stay, so such a lock is a plain lock.
inline DWORD effectiveLockFlags(DWORD flags,DWORD pool){return pool==D3::kPoolDefault?flags:(flags&~(D3::kLockDiscard|D3::kLockNoOverwrite));}
constexpr std::size_t kMaxStagedImage=std::size_t(8)<<20;
// What the replay thread does for a recorded image unlock: lock the real level with the game's flags/rect, copy rows from the
// tight source (rows of rowBytes, `pitch` apart), unlock. Also the synchronous fallback when no Block is available.
inline bool applyImageUnlock(StreamCore& core,const UnlockImageArgs& a,const unsigned char* src){
    ProxyBase* p=a.proxy;
    if(!p->inner||p->dead.load()||!src){add(core.q.stats.replayFailures);return false;}
    D3DLOCKED_RECT lr{};D3DLOCKED_BOX lb{};RECT rect=RECT{a.l,a.t,a.r,a.b};D3DBOX box{};box.Left=UINT(a.l);box.Top=UINT(a.t);box.Right=UINT(a.r);box.Bottom=UINT(a.b);box.Front=a.bf;box.Back=a.bk;
    const bool volume=a.route==RouteVolume||a.route==RouteVolumeTexture;
    if(FAILED(callLock(a.route,p->inner,a.level,a.face,&lr,&lb,a.hasRect&&!volume?&rect:nullptr,a.hasRect&&volume?&box:nullptr,a.flags))){add(core.q.stats.replayFailures);return false;}
    auto* dst=static_cast<unsigned char*>(volume?lb.pBits:lr.pBits);const INT rowPitch=volume?lb.RowPitch:lr.Pitch;const INT slicePitch=volume?lb.SlicePitch:0;
    if(dst)for(UINT s=0;s<a.slices;++s)for(UINT r=0;r<a.rows;++r)
        std::memcpy(dst+std::ptrdiff_t(s)*slicePitch+std::ptrdiff_t(r)*rowPitch,src+std::size_t(s)*a.slicePitch+std::size_t(r)*a.pitch,a.rowBytes);
    callUnlock(a.route,p->inner,a.level,a.face);return true;
}
inline UnlockImageArgs makeUnlockArgs(ProxyBase& self,const SubRes& sub,UINT route,UINT level,UINT face){
    UnlockImageArgs a{};a.proxy=&self;a.level=level;a.face=face;a.flags=sub.flags;a.hasRect=sub.hasRect;a.rows=sub.rows;a.slices=sub.slices;a.rowBytes=sub.rowBytes;a.pitch=sub.pitch;a.slicePitch=sub.slicePitch;
    a.l=sub.rect.left;a.t=sub.rect.top;a.r=sub.rect.right;a.b=sub.rect.bottom;a.bf=sub.box.Front;a.bk=sub.box.Back;a.route=route;return a;
}
constexpr std::size_t kFreshShadowMax=std::size_t(256)<<10;   // a fresh level up to this size keeps its first-write bytes as its shadow
// self: the object the game called (its inner receives the replayed Lock); root: where creation info and `sub` live.
inline HRESULT lockImage(ProxyBase& self,ProxyBase& root,SubRes& sub,UINT route,UINT level,UINT face,UINT lw,UINT lh,UINT ld,
                         D3DLOCKED_RECT* lr,D3DLOCKED_BOX* lb,const RECT* rect,const D3DBOX* box,DWORD flags,void* caller){
    LockScope _ts(*self.core);Queue& q=self.core->q;
    if((!lr&&!lb)||sub.mode!=SubRes::Free)return D3DERR_INVALIDCALL;
    UINT l=0,t=0,r=lw,b=lh,f=0,k=ld;
    if(rect){
        if(rect->left<0||rect->top<0||UINT(rect->right)>lw||UINT(rect->bottom)>lh||rect->left>=rect->right||rect->top>=rect->bottom)return D3DERR_INVALIDCALL;
        l=UINT(rect->left);t=UINT(rect->top);r=UINT(rect->right);b=UINT(rect->bottom);
    }
    if(box){
        if(box->Right>lw||box->Bottom>lh||box->Back>ld||box->Left>=box->Right||box->Top>=box->Bottom||box->Front>=box->Back)return D3DERR_INVALIDCALL;
        l=box->Left;t=box->Top;r=box->Right;b=box->Bottom;f=box->Front;k=box->Back;
    }
    const DWORD eff=effectiveLockFlags(flags,root.info.pool);
    const auto fi=D3::formatInfo(root.info.fmt);
    const bool rtLike=(root.info.usage&(D3::kUsageRT|D3::kUsageDS))!=0,volume=route==RouteVolume||route==RouteVolumeTexture;
    const UINT rows=fi.ok?(b-t+fi.bh-1)/fi.bh:0,rowBytes=fi.ok?((r-l+fi.bw-1)/fi.bw)*fi.bytes:0,slices=k-f;
    // 0.3.206 (task 31): Diagnostics only (core.timing): the lock's record, completed by the readback / pass-through below
    StreamCore& core=*self.core;const bool tm=core.timing;TexReadbackDiag::Hit hit;
    if(tm){
        if(!sub.diagId){sub.diagId=core.texDiag.nextId++;sub.diagFirstCaller=caller;}
        TexReadbackDiag::Meta m;m.fmt=root.info.fmt;m.w=lw;m.h=lh;m.d=ld;m.level=level;m.face=face;m.pool=root.info.pool;m.usage=root.info.usage;m.baseW=root.info.w;m.baseH=root.info.h;m.caller=sub.diagFirstCaller;
        if(fi.ok)m.levelBytes=std::uint64_t((lh+fi.bh-1)/fi.bh)*(((lw+fi.bw-1)/fi.bw)*fi.bytes)*ld;
        const bool whole=l==0&&t==0&&r==lw&&b==lh&&f==0&&k==ld;
        const double cover=double(r-l)*double(b-t)*double(k-f)/(double(lw)*double(lh)*double(ld));
        hit=core.texDiag.lock(sub.diagId,sub.diagHist,sub.diagLastLock,sub.diagEpoch,m,whole,cover,core.frameNo,caller);   // m.caller = the first lock's (caller=), `caller` this lock's (rbCaller= if it reads back)
    }
    auto describe=[&]{
        sub.flags=flags;sub.hasRect=rect!=nullptr||box!=nullptr;
        sub.rect.left=LONG(l);sub.rect.top=LONG(t);sub.rect.right=LONG(r);sub.rect.bottom=LONG(b);
        if(box)sub.box=*box;
        sub.rows=rows;sub.slices=slices;sub.rowBytes=rowBytes;sub.pitch=rowBytes;sub.slicePitch=rowBytes*rows;
    };
    // ---- a level shadow: served from CPU memory, Unlock records the locked rows ----
    if(!rtLike&&fi.ok){
        const UINT lrows=(lh+fi.bh-1)/fi.bh,lrowBytes=((lw+fi.bw-1)/fi.bw)*fi.bytes;
        const std::size_t levelBytes=std::size_t(lrows)*lrowBytes*ld;
        bool justMade=false;
        if(!sub.shadowOn&&!sub.shadowDead){
            const bool undefined=!sub.written||(eff&D3::kLockDiscard)!=0;   // nothing to preserve: a zero shadow is as good as the real bytes
            const bool fresh=undefined&&levelBytes<=kFreshShadowMax,readback=!undefined&&!(flags&D3::kLockReadOnly)&&levelBytes<=kMaxStagedImage;
            if(fresh||readback){
                // 0.3.196 (task 12): a FRESH keep (a level WoW writes once and may never lock again) takes free room, or evicts never-re-locked keeps that are at least kFreshEvictAgeFrames
                // old (never a re-locked shadow); otherwise it is skipped: not made, and the lock takes the staged path below (a Block recorded with the same bytes, no sync).
                // A readback may still evict (fresh keeps first, then re-locked ones).
                const bool room=readback?makeRoomForShadow(*self.core,levelBytes,true,&sub):makeRoomForFreshKeep(*self.core,levelBytes,&sub);
                if(!room){
                    if(readback){add(q.stats.texShadowRefused);add(q.stats.texShadowRefusedBytes,levelBytes);}
                    else{add(q.stats.texShadowFreshSkipped);sub.gone=SubRes::GoneSkipped;}
                }
                else{
                    bool ok=true;
                    takeTexSpare(*self.core,sub.shadow,levelBytes+kLockSlack);   // 0.3.200 (pipeline)
                    try{sub.shadow.assign(levelBytes+kLockSlack,0);}catch(...){ok=false;}
                    if(ok&&readback){   // ONE synchronous readback: the replay thread copies the whole real level out under a READONLY lock
                        ok=false;
                        std::uint64_t lockT=0,copyT=0;bool innerGone=false,isExpress=false;const std::uint64_t t0=tm?nowNs():0;   // 0.3.206 (task 31): `tm` was read on the game thread; the task only reads this copy
                        auto body=[&](bool isExpress){
                            if(!self.inner||self.dead.load()){if(isExpress)innerGone=true;return;}
                            D3DLOCKED_RECT r2{};D3DLOCKED_BOX b2{};
                            const std::uint64_t a0=tm?nowNs():0;
                            if(FAILED(callLock(route,self.inner,level,face,&r2,&b2,nullptr,nullptr,D3::kLockReadOnly)))return;
                            const std::uint64_t a1=tm?nowNs():0;
                            const auto* src=static_cast<const unsigned char*>(volume?b2.pBits:r2.pBits);const INT rp=volume?b2.RowPitch:r2.Pitch,sp=volume?b2.SlicePitch:0;
                            if(src){for(UINT z=0;z<ld;++z)for(UINT y=0;y<lrows;++y)std::memcpy(sub.shadow.data()+(std::size_t(z)*lrows+y)*lrowBytes,src+std::ptrdiff_t(z)*sp+std::ptrdiff_t(y)*rp,lrowBytes);ok=true;}
                            const std::uint64_t a2=tm?nowNs():0;
                            callUnlock(route,self.inner,level,face);
                            if(tm){const std::uint64_t a3=nowNs();lockT=(a1-a0)+(a3-a2);copyT=a2-a1;}};
                        // 0.3.206 (task 31): EXPRESS when every command this readback depends on (the owner's and the root's create / derive, the level's last content write) has already been replayed:
                        // the replay thread runs the same body at its next command boundary instead of after the whole queue. Exact: no recorded command can change the bytes any more (the game thread is here).
                        bool ran=false;
                        const std::uint64_t need=std::max(std::max(self.readySeq,root.readySeq),sub.contentSeq);
                        const bool express=!inPumpedWait&&self.readySeq!=0&&root.readySeq!=0&&sub.contentSeq!=0&&need<=q.replayedSeq();
                        if(express){
                            q.runExpress([](void* a){(*static_cast<decltype(body)*>(a))(true);},&body);
                            if(!innerGone){ran=true;isExpress=true;}   // (innerGone: should not happen; the drain path below decides)
                        }
                        if(!ran)ran=runTask(*self.core,[&](StreamCore&){body(false);},Cmd::SyncLock);
                        if(!ran)return D3DERR_INVALIDCALL;
                        if(ok&&tm){const std::uint64_t total=nowNs()-t0,busy=lockT+copyT;
                            core.texDiag.readback(hit,levelBytes,std::uint64_t(rows)*rowBytes*slices,unsigned(sub.gone),total>busy?total-busy:0,lockT,copyT,isExpress);}   // sub.gone is still the cause here
                        if(ok){add(q.stats.texShadowReadbacks);
                            add(sub.gone==SubRes::GoneFresh?q.stats.readbackAfterFreshDrop:sub.gone==SubRes::GoneRelocked?q.stats.readbackAfterRelockedEvict:sub.gone==SubRes::GoneSkipped?q.stats.readbackAfterFreshSkip:q.stats.readbackNeverShadowed);}
                    }else if(ok)add(q.stats.texShadowFresh);
                    if(ok){sub.gone=SubRes::GoneNone;
                        sub.shadowOn=true;sub.levelRowBytes=lrowBytes;sub.levelRows=lrows;sub.levelSlices=ld;q.addTexShadowBytes(std::int64_t(levelBytes));
                        sub.relocked=readback;sub.fromFresh=fresh;listShadow(*self.core,sub);justMade=true;   // a readback is a re-lock by definition: evicted last
                    }
                    else{std::vector<unsigned char>().swap(sub.shadow);sub.shadowDead=true;}   // the real level cannot be read: pass-through as before
                }
            }
        }
        if(sub.shadowOn){
            if(!justMade){const bool first=!sub.relocked;touchTexShadow(*self.core,sub,first);if(first&&sub.fromFresh)add(q.stats.texShadowFreshUseful);}   // a kept first write that was locked again was worth keeping
            describe();sub.mode=SubRes::Shadowed;
            sub.shadowOffset=std::size_t(f)*lrows*lrowBytes+std::size_t(t/fi.bh)*lrowBytes+std::size_t(l/fi.bw)*fi.bytes;
            unsigned char* at=sub.shadow.data()+sub.shadowOffset;
            if(lr){lr->Pitch=INT(lrowBytes);lr->pBits=at;}
            if(lb){lb->RowPitch=INT(lrowBytes);lb->SlicePitch=INT(lrows*lrowBytes);lb->pBits=at;}
            add(q.stats.texShadowHits);own(q.stats.lockAsync);return D3D_OK;
        }
    }
    PassReason why=PassReason::Other;bool candidate=false;
    if(flags&D3::kLockReadOnly)why=PassReason::ReadOnly;
    else if(rtLike)why=PassReason::RenderTarget;
    else if(sub.written&&!(eff&D3::kLockDiscard))why=PassReason::Written;
    else if(!fi.ok)why=PassReason::Format;
    else candidate=true;
    if(candidate){
        const std::size_t total=std::size_t(rows)*rowBytes*slices;
        if(total>kMaxStagedImage)why=PassReason::Size;
        else if(Block* blk=q.tryAllocBlock(total+kLockSlack)){
            describe();sub.mode=SubRes::Staged;sub.stage=blk;
            blk->used=std::uint32_t(total);std::memset(blk->data(),0,total+kLockSlack);   // deterministic bytes for what the game leaves unwritten
            if(lr){lr->Pitch=INT(rowBytes);lr->pBits=blk->data();}
            if(lb){lb->RowPitch=INT(rowBytes);lb->SlicePitch=INT(rowBytes*rows);lb->pBits=blk->data();}
            own(q.stats.lockAsync);return D3D_OK;
        }else why=PassReason::Budget;
    }
    countPass(self,why);
    if(tm)core.texDiag.passSync(hit);
    HRESULT hr=D3DERR_INVALIDCALL;
    const bool ran=runTask(*self.core,[&](StreamCore&){if(self.inner&&!self.dead.load())hr=callLock(route,self.inner,level,face,lr,lb,rect,box,flags);},Cmd::SyncLock);
    if(!ran)return D3DERR_INVALIDCALL;
    if(SUCCEEDED(hr)){sub.mode=SubRes::Pass;sub.flags=flags;}
    return hr;
}
inline HRESULT unlockImage(ProxyBase& self,SubRes& sub,UINT route,UINT level,UINT face){
    LockScope _ts(*self.core);Queue& q=self.core->q;
    if(sub.mode==SubRes::Free)return D3DERR_INVALIDCALL;
    const bool volume=route==RouteVolume||route==RouteVolumeTexture;
    const std::uint16_t cmd=(std::uint16_t)(volume?Cmd::UnlockBox:Cmd::UnlockRect);
    if(sub.mode==SubRes::Staged){
        auto* a=static_cast<UnlockImageArgs*>(q.reserveWithBlock(cmd,sizeof(UnlockImageArgs),sub.stage));
        *a=makeUnlockArgs(self,sub,route,level,face);
        q.commit();sub.stage=nullptr;sub.mode=SubRes::Free;sub.written=true;sub.contentSeq=q.recordedSeq();return D3D_OK;
    }
    if(sub.mode==SubRes::Shadowed){
        sub.mode=SubRes::Free;
        if(sub.flags&D3::kLockReadOnly)return D3D_OK;
        const std::size_t total=std::size_t(sub.rows)*sub.rowBytes*sub.slices,sliceBytes=std::size_t(sub.levelRows)*sub.levelRowBytes;
        auto gather=[&](unsigned char* out){
            for(UINT z=0;z<sub.slices;++z)for(UINT y=0;y<sub.rows;++y)
                std::memcpy(out+(std::size_t(z)*sub.rows+y)*sub.rowBytes,sub.shadow.data()+sub.shadowOffset+z*sliceBytes+std::size_t(y)*sub.levelRowBytes,sub.rowBytes);};
        sub.pitch=sub.rowBytes;sub.slicePitch=sub.rowBytes*sub.rows;
        if(Block* blk=q.tryAllocBlock(total?total:1)){
            blk->used=std::uint32_t(total);gather(blk->data());
            auto* a=static_cast<UnlockImageArgs*>(q.reserveWithBlock(cmd,sizeof(UnlockImageArgs),blk));*a=makeUnlockArgs(self,sub,route,level,face);q.commit();
        }else{   // no Block: the same rows written synchronously
            std::vector<unsigned char> tmp(total?total:1);gather(tmp.data());const UnlockImageArgs a=makeUnlockArgs(self,sub,route,level,face);
            add(self.core->q.stats.passThrough[unsigned(PassReason::Budget)]);
            if(!runTask(*self.core,[&](StreamCore& c){applyImageUnlock(c,a,tmp.data());},Cmd::SyncUnlock))return D3DERR_INVALIDCALL;
        }
        sub.written=true;sub.contentSeq=q.recordedSeq();return D3D_OK;   // (the no-Block fallback ran as a task: recordedSeq() is that already replayed task)
    }
    HRESULT hr=D3DERR_INVALIDCALL;
    if(!runTask(*self.core,[&](StreamCore&){if(self.inner)hr=callUnlock(route,self.inner,level,face);},Cmd::SyncUnlock))return D3DERR_INVALIDCALL;
    sub.mode=SubRes::Free;if(!(sub.flags&D3::kLockReadOnly)){sub.written=true;sub.contentSeq=q.recordedSeq();}
    return hr;
}

// ---- buffer locks ----
// 0.3.192 (CS): kMaxStaticShadow: a NON-DYNAMIC buffer up to this size may hold a shadow (a larger one stays pass-through).
constexpr std::size_t kMaxStaticShadow=std::size_t(4)<<20;
struct BufferState {
    SliceMem shadow;bool shadowOn=false,written=false,whole=false;
    // lastUse: StreamCore::frameNo of its last shadow lock. lru: StreamCore::bufRegular / bufLarge, least recently locked first, so lastUse never decreases from the head
    // (an eviction scan stops at the first entry locked in the current frame). proxy: the owner, set when the shadow is registered.
    std::uint64_t lastUse=0;LruNode<BufferState> lru;ProxyBase* proxy=nullptr;
    enum Mode:std::uint8_t{Free,Shadow,Staged,Pass,Scratch} mode=Free;UINT off=0,size=0;DWORD flags=0;Block* stage=nullptr;unsigned char scratchSlot=0;   // Scratch: a small staged lock in StreamCore::scratch
    // canShadow: made by the game and shadow-able (DYNAMIC of any size, or non-DYNAMIC <= kMaxStaticShadow). shadowDead: never a shadow again
    // (GPU-written by ProcessVertices, or the readback failed). A buffer without a shadow (refused at creation, evicted, dropped under pressure) is not
    // "refused for good": it gets one at its next write re-lock (one readback) or, DYNAMIC, at a DISCARD lock, whenever room can be made.
    bool canShadow=false,shadowDead=false;
    bool large=false;   // the shadow lives in the large allowance (Queue::largeAdmit), not in the regular cap
    // 0.3.204 (task 21): zero-copy slices of a DYNAMIC buffer. `shadow` is the current slice; `ring` the buffer's other slices (at most kMaxSlices-1: framesAhead+2 in all), each with the seq of the last recorded zero-copy
    // unlock that reads it (0 = none) and the frame it left the current role (idle trimming). sliceSeq: the same for the current slice. The game may write, free or reuse a slice only once replayedSeq() >= its seq.
    // zc: the current lock is a DISCARD/NOOVERWRITE write lock on a slice.
    struct Slice {SliceMem mem;std::uint64_t seq=0,frame=0;};
    std::vector<Slice> ring;std::uint64_t sliceSeq=0;bool zc=false,copyP=false;   // copyP: this lock would have been zero-copy but memory pressure keeps it on the copy path
    // DISCARD locks per frame (StreamCore::frameNo): discNow in frame discFrame, discMax the largest count of the last kDiscardWindowFrames frames (it decays); they size the ring (StreamCore::allowedSlices).
    std::uint64_t discFrame=0,discMaxFrame=0;unsigned discNow=0,discMax=0;
    // 0.3.205 (#34): the zero-copy refs of the CURRENT slice the replay thread has not passed, in seq order, with their hull pendMin/pendMax (a quick "no overlap" exit). Each reads its range of the slice until it retires:
    // a NOOVERWRITE lock overlapping one would write bytes the replay thread is still reading. Such a lock is staged instead (prepareSliceWrite, lockBuffer): the game writes a Block, its unlock queues that Block, and the
    // bytes wait here as a patch until the replay thread has passed seq (every ref that read the range); applyPatches then writes them into the slice. Patches are in creation order; one that overlaps an earlier one has a
    // seq >= the earlier one's (overlapSeq counts unapplied patches too), so applying every passed one in order keeps the newest bytes. While a patch is unapplied no lock gets the slice's pointer over its range and no
    // ref or copy is recorded from it (such a lock is staged too). patchBytes: their sum, at most the buffer's length (else the lock waits); patchMinSeq: their lowest seq (applyPatches has nothing to do below it).
// stageSeq: the current Staged lock is such an overlap stage (0 = an ordinary one).
    struct PendingRef {std::uint64_t seq;UINT off,end;};
    std::vector<PendingRef> pend;UINT pendMin=0,pendMax=0;
    struct Patch {std::uint64_t seq;UINT off;std::vector<unsigned char> bytes;};
    std::vector<Patch> patches;std::size_t patchBytes=0;std::uint64_t patchMinSeq=0,stageSeq=0;
};
inline void clearPending(BufferState& s){s.pend.clear();s.pendMin=s.pendMax=0;}
// 0.3.205 (#34): the slice's contents no longer matter (a DISCARD, a rename, a dropped or fresh shadow): the patches go unapplied (their bytes are in the queue already, in the staged unlocks).
inline void dropPatches(Queue& q,BufferState& s){if(s.patches.empty())return;q.stats.zcPatchBytes.fetch_sub(std::int64_t(s.patchBytes),std::memory_order_relaxed);s.patches.clear();s.patchBytes=0;s.patchMinSeq=0;}
// 0.3.205 (#34): game thread. Writes the patches the replay thread has passed into the slice (no unreplayed ref reads their bytes any more: the ones recorded before a patch have seqs <= its seq, none is recorded over it later).
inline void applyPatches(Queue& q,BufferState& s){
    const std::uint64_t rep=q.replayedSeq();
    if(s.patches.empty()||rep<s.patchMinSeq)return;
    std::size_t j=0,freed=0;std::uint64_t minSeq=~std::uint64_t(0);
    for(std::size_t i=0;i<s.patches.size();++i){
        auto& p=s.patches[i];
        if(p.seq<=rep){std::memcpy(s.shadow.data()+p.off,p.bytes.data(),p.bytes.size());freed+=p.bytes.size();}
        else{if(p.seq<minSeq)minSeq=p.seq;if(j!=i)s.patches[j]=std::move(p);++j;}
    }
    s.patches.resize(j);s.patchMinSeq=j?minSeq:0;s.patchBytes-=freed;if(freed)q.stats.zcPatchBytes.fetch_sub(std::int64_t(freed),std::memory_order_relaxed);
}
// 0.3.205 (#34): the game-side truth of [off, off+size): the slice with the unapplied patches over it (reading the slice while the replay thread reads it too is no race).
inline void readSliceRange(const BufferState& s,unsigned char* dst,UINT off,UINT size){
    std::memcpy(dst,s.shadow.data()+off,size);
    for(const auto& p:s.patches){
        const UINT pe=p.off+UINT(p.bytes.size()),a=off>p.off?off:p.off,b=off+size<pe?off+size:pe;
        if(a<b)std::memcpy(dst+(a-off),p.bytes.data()+(a-p.off),b-a);
    }
}
// 0.3.205 (#34): drops the entries the replay has passed (and recomputes the hull of the rest).
inline void prunePending(Queue& q,BufferState& s){
    const std::uint64_t rep=q.replayedSeq();const std::size_t n=s.pend.size();std::size_t h=0;
    while(h<n&&s.pend[h].seq<=rep)++h;
    if(!h)return;
    if(h==n){clearPending(s);return;}
    s.pend.erase(s.pend.begin(),s.pend.begin()+std::ptrdiff_t(h));s.pendMin=s.pend[0].off;s.pendMax=s.pend[0].end;
    for(const auto& r:s.pend){if(r.off<s.pendMin)s.pendMin=r.off;if(r.end>s.pendMax)s.pendMax=r.end;}
}
inline void pushPending(Queue& q,BufferState& s,std::uint64_t seq,UINT off,UINT end){
    prunePending(q,s);
    if(s.pend.empty()){s.pendMin=off;s.pendMax=end;}else{if(off<s.pendMin)s.pendMin=off;if(end>s.pendMax)s.pendMax=end;}
    s.pend.push_back({seq,off,end});
}
// 0.3.205 (#34): the seq the replay thread must pass before the game may write [off,end) of the current slice: the highest among the unreplayed refs and the unapplied patches that overlap it (after applying the passed
// ones), 0 = none. No seq is above sliceSeq (the newest ref; a patch's seq is a ref's), so the scans stop once they reach it.
inline std::uint64_t overlapSeq(Queue& q,BufferState& s,UINT off,UINT end){
    prunePending(q,s);applyPatches(q,s);
    std::uint64_t best=0;
    if(!s.pend.empty()&&end>s.pendMin&&off<s.pendMax)for(auto r=s.pend.rbegin();r!=s.pend.rend()&&best<s.sliceSeq;++r)if(r->off<end&&off<r->end&&r->seq>best)best=r->seq;
    for(auto p=s.patches.rbegin();p!=s.patches.rend()&&best<s.sliceSeq;++p)if(p->off<end&&off<p->off+UINT(p->bytes.size())&&p->seq>best)best=p->seq;
    return best;
}
// Buffer shadows (game-side memory, persistent CPU copies of the buffer; every lock of one returns shadow memory and Unlock copies the locked range into
// the queue). They share one cap (ShadowBudgetBytes, adaptive, outside the queue budget) and ONE LRU: a DYNAMIC buffer gets its shadow at creation when
// the cap admits it; any buffer without one gets it later (see lockBuffer). The game thread takes and (under pressure/eviction) drops them, the replay
// thread frees one when the proxy dies; StreamCore::bufList (under texMutex) makes those safe against each other. No queued command refers to a
// shadow, so dropping an unlocked one is always safe.
inline bool dynamicBuffer(const ProxyBase& p){return (p.info.usage&D3::kUsageDynamic)!=0;}
inline LruList<BufferState>& bufListOf(StreamCore& c,const BufferState& s){return s.large?c.bufLarge:c.bufRegular;}
inline void registerShadow(ProxyBase& p,BufferState& s,bool large=false){
    s.shadowOn=true;s.large=large;s.proxy=&p;clearPending(s);dropPatches(p.core->q,s);   // 0.3.205 (#34): a fresh shadow (takeShadow / readBackShadow) has no pending refs or patches
    if(large){p.core->q.addLargeBytes(std::int64_t(p.info.length));add(p.core->q.stats.largeShadowGrants);}else p.core->q.addShadowBytes(std::int64_t(p.info.length));s.lastUse=p.core->frameNo;
    std::lock_guard<std::mutex> l(p.core->texMutex);bufListOf(*p.core,s).pushBack(&s);
}
// 0.3.204 (task 21): frees the retired slices the replay thread has passed (any thread; the game thread at every Present and before a large allocation).
inline void trimRetired(StreamCore& c){
    if(c.q.stats.retiredBytes.load(std::memory_order_relaxed)<=0)return;
    std::vector<SliceMem> dead;std::size_t freedLarge=0,freedRegular=0;
    {
        std::lock_guard<std::mutex> l(c.texMutex);const std::uint64_t rep=c.q.replayedSeq();std::size_t j=0;
        for(std::size_t i=0;i<c.retired.size();++i){
            if(c.retired[i].seq<=rep){(c.retired[i].large?freedLarge:freedRegular)+=c.retired[i].bytes;dead.push_back(std::move(c.retired[i].mem));}
            else{if(j!=i)c.retired[j]=std::move(c.retired[i]);++j;}
        }
        c.retired.resize(j);
    }
    if(freedLarge){c.q.stats.retiredLargeBytes.fetch_sub(std::int64_t(freedLarge),std::memory_order_relaxed);}
    if(freedRegular){c.q.stats.retiredRegularBytes.fetch_sub(std::int64_t(freedRegular),std::memory_order_relaxed);}
    if(freedLarge+freedRegular)c.q.stats.retiredBytes.fetch_sub(std::int64_t(freedLarge+freedRegular),std::memory_order_relaxed);
}   // `dead` is freed here, outside the lock
// texMutex held. A slice goes: freed now when no recorded command reads it any more, else moved (the storage, not the BufferState) to the retire list with its last reader's seq.
// Accounting: the current slice (a shadow) leaves shadowBytes/largeShadowBytes at once, a ring slice leaves the ring counters; what is still read counts in retired*Bytes (the ring budget, memory() totals) until trimRetired frees it.
inline void retireSliceLocked(ProxyBase& p,SliceMem& v,std::uint64_t seq,bool isRing,bool large){
    if(v.empty())return;
    StreamCore& c=*p.core;const std::size_t len=p.info.length;auto& st=c.q.stats;
    if(isRing){st.ringBytes.fetch_sub(std::int64_t(len),std::memory_order_relaxed);st.ringSlices.fetch_sub(1,std::memory_order_relaxed);(large?st.ringLargeBytes:st.ringRegularBytes).fetch_sub(std::int64_t(len),std::memory_order_relaxed);}
    else{if(large)c.q.addLargeBytes(-std::int64_t(len));else c.q.addShadowBytes(-std::int64_t(len));}
    if(seq>c.q.replayedSeq()){c.retired.push_back(StreamCore::Retired{std::move(v),seq,len,large});st.retiredBytes.fetch_add(std::int64_t(len),std::memory_order_relaxed);(large?st.retiredLargeBytes:st.retiredRegularBytes).fetch_add(std::int64_t(len),std::memory_order_relaxed);}
    SliceMem().swap(v);   // (empty after a move; frees otherwise)
}
inline void dropShadowLocked(ProxyBase& p,BufferState& s){
    if(!s.shadowOn)return;
    bufListOf(*p.core,s).remove(&s);
    retireSliceLocked(p,s.shadow,s.sliceSeq,false,s.large);for(auto& sl:s.ring)retireSliceLocked(p,sl.mem,sl.seq,true,s.large);s.ring.clear();   // 0.3.204 (task 21): every slice of the ring (large or regular cap), retired while a recorded zero-copy unlock still reads them
    if(s.large){add(p.core->q.stats.largeShadowDrops);s.large=false;}
    s.sliceSeq=0;clearPending(s);dropPatches(p.core->q,s);s.zc=false;s.shadowOn=false;
}
inline void noteShadowEvicted(StreamCore& c,const ProxyBase& p){add(dynamicBuffer(p)?c.q.stats.dynShadowEvicted:c.q.stats.stShadowEvicted);}
// Game thread: the shadow was locked: most recently used now (frame and list position). A repeated lock of the one at the tail takes no lock.
inline void touchBufferShadow(StreamCore& c,BufferState& s){
    s.lastUse=c.frameNo;
    if(bufListOf(c,s).isTail(&s))return;
    std::lock_guard<std::mutex> l(c.texMutex);auto& list=bufListOf(c,s);list.remove(&s);list.pushBack(&s);
}
// 0.3.204 (task 21): a holder whose slices unreplayed zero-copy unlocks still read frees nothing when dropped (the slices retire with their bytes counted), so it is no eviction victim until the replay has passed.
inline bool slicesBusy(const StreamCore& c,const BufferState& s){const std::uint64_t r=c.q.replayedSeq();if(r<s.sliceSeq)return true;for(const auto& sl:s.ring)if(r<sl.seq)return true;return false;}
// Game thread: makes `bytes` fit the cap by evicting shadows, least recently locked first (the head of the LRU list), never a locked one, `keep`,
// or one locked in the CURRENT frame (anti-thrash: it is in use now; the requester is refused instead; the list is in lastUse order, so the scan ends there).
// Thrash signal for the adaptive cap: evicting a HOT shadow (locked within kShadowHotFrames) or, for a re-lock (`relock`), finding no victim at all grows the
// cap one step if allowed (no pressure, interval) and then re-checks before evicting anything. A request above a quarter of the cap never evicts (one buffer
// must not flush the rest). The large allowance is a list of its own (a large shadow frees nothing of the regular cap). false = still no room.
inline bool makeRoomForBufferShadow(StreamCore& c,std::size_t bytes,bool relock,const BufferState* keep){
    while(!c.q.shadowAdmit(bytes)){
        if(bytes>c.q.shadowCap()/4)return false;
        std::lock_guard<std::mutex> l(c.texMutex);
        BufferState* victim=nullptr;
        for(BufferState* s=c.bufRegular.head;s&&s->lastUse<c.frameNo;s=s->lru.next)if(s!=keep&&s->mode==BufferState::Free&&!slicesBusy(c,*s)){victim=s;break;}   // 0.3.204 (task 21): never a holder whose slices unreplayed unlocks still read
        if(!victim){if(relock&&c.q.growShadowCap(c.frameNo))continue;return false;}
        const bool hot=c.frameNo-victim->lastUse<kShadowHotFrames;
        if(hot&&c.q.growShadowCap(c.frameNo))continue;
        ProxyBase& vp=*victim->proxy;noteShadowEvicted(c,vp);if(hot)add(c.q.stats.hotShadowEvicted);dropShadowLocked(vp,*victim);
    }
    return true;
}
// Large allowance (see kMaxLargeShadow): a DYNAMIC buffer too big for the regular cap. true = the allowance has room for it (no pressure; when taken by another
// large shadow and mayEvict, that one is dropped if unlocked and idle for kLargeIdleFrames, LRU). Game thread.
inline bool largeCandidate(const ProxyBase& p){const std::size_t len=p.info.length;return dynamicBuffer(p)&&len>p.core->q.shadowCap()/4&&len<=kMaxLargeShadow;}
inline bool makeRoomForLarge(StreamCore& c,std::size_t bytes,bool mayEvict,const BufferState* keep){
    trimRetired(c);   // 0.3.204 (task 21): retired slices the replay thread has passed count against the allowance until freed
    if(c.q.largeAdmit(bytes))return true;
    if(!mayEvict||c.q.pressure()||bytes>kMaxLargeShadow)return false;
    {   // 0.3.204: up to two holders: evict only if the idle unlocked ones (LRU prefix) free enough, else refuse and drop nothing
        std::lock_guard<std::mutex> l(c.texMutex);std::size_t freeable=0;
        for(const BufferState* s=c.bufLarge.head;s&&c.frameNo>=s->lastUse+kLargeIdleFrames;s=s->lru.next)if(s!=keep&&s->mode==BufferState::Free&&!slicesBusy(c,*s))freeable+=s->proxy->info.length;
        if(c.q.largeBytes()-std::min(freeable,c.q.largeBytes())+bytes>LargeShadowBudgetBytes)return false;
    }
    while(!c.q.largeAdmit(bytes)){
        if(c.q.pressure())return false;
        std::lock_guard<std::mutex> l(c.texMutex);
        BufferState* victim=nullptr;
        for(BufferState* s=c.bufLarge.head;s&&c.frameNo>=s->lastUse+kLargeIdleFrames;s=s->lru.next)if(s!=keep&&s->mode==BufferState::Free&&!slicesBusy(c,*s)){victim=s;break;}   // (at most a few entries)
        if(!victim)return false;
        ProxyBase& vp=*victim->proxy;noteShadowEvicted(c,vp);dropShadowLocked(vp,*victim);
    }
    return true;
}
inline bool takeShadow(ProxyBase& p,BufferState& s,bool relock=false){   // a zero-filled shadow (creation, or a DISCARD lock: the contents are undefined anyway)
    const std::size_t len=p.info.length;
    const bool large=len&&largeCandidate(p);
    if(!len||!(large?makeRoomForLarge(*p.core,len,relock,&s):makeRoomForBufferShadow(*p.core,len,relock,&s)))return false;
    try{s.shadow.assign(len+kLockSlack,0);}catch(...){return false;}
    registerShadow(p,s,large);return true;
}
inline void initShadow(ProxyBase& p,BufferState& s,bool game=true){   // game=false: an implicit proxy made on the replay thread never shadows
    if(!game)return;
    const bool dyn=dynamicBuffer(p);s.canShadow=p.info.length&&(dyn||p.info.length<=kMaxStaticShadow);
    if(!dyn||!s.canShadow)return;
    if(!takeShadow(p,s)){add(p.core->q.stats.shadowRefused);add(p.core->q.stats.shadowRefusedBytes,p.info.length);}   // no shadow yet: see BufferState::canShadow
}
inline void dropShadow(ProxyBase& p,BufferState& s){   // a GPU-side write or the proxy's end: the shadow no longer mirrors the buffer
    std::lock_guard<std::mutex> l(p.core->texMutex);dropShadowLocked(p,s);
}
// A GPU-side write (ProcessVertices into the buffer): the shadow is stale for good, and never made again (a read-back right after a GPU write is not
// one the NOOVERWRITE flag may skip the wait for).
inline void markBufferGpuWritten(ProxyBase& p,BufferState& s){dropShadow(p,s);s.written=true;s.shadowDead=true;}
// Game thread: makes a shadow of the whole, already written buffer by ONE synchronous readback (the replay thread locks the real buffer with
// readBackLock() flags: READONLY, and NOOVERWRITE on DXVK >= 3 so it does not wait for the GPU; the tracked Buffer wrapper keeps the revision because
// READONLY stays set). A buffer above a quarter of the cap (or kMaxStaticShadow when not DYNAMIC) stays pass-through. 1 = shadow made, 0 = none
// (refused or unreadable: the lock takes the old paths), -1 = could not wait (nested in a pumped wait).
inline int readBackShadow(ProxyBase& self,BufferState& s){
    StreamCore& core=*self.core;Queue& q=core.q;const UINT len=self.info.length;const bool dyn=dynamicBuffer(self);
    const bool large=largeCandidate(self);
    if(!(large?makeRoomForLarge(core,len,true,&s):(len<=q.shadowCap()/4&&makeRoomForBufferShadow(core,len,true,&s)))){add(q.stats.relockRefused);add(q.stats.relockRefusedBytes,len);return 0;}
    try{s.shadow.assign(std::size_t(len)+kLockSlack,0);}catch(...){return 0;}
    bool ok=false;
    const bool ran=runTask(core,[&](StreamCore& c){
        if(!self.inner||self.dead.load())return;
        const DWORD fl=c.readBackLock?c.readBackLock():D3::kLockReadOnly;void* got=nullptr;const bool vb=self.kind==Kind::VertexBuffer;
        const HRESULT hr=vb?static_cast<IDirect3DVertexBuffer9*>(self.inner)->Lock(0,len,&got,fl):static_cast<IDirect3DIndexBuffer9*>(self.inner)->Lock(0,len,&got,fl);
        if(FAILED(hr)||!got)return;
        std::memcpy(s.shadow.data(),got,len);ok=true;
        if(vb)static_cast<IDirect3DVertexBuffer9*>(self.inner)->Unlock();else static_cast<IDirect3DIndexBuffer9*>(self.inner)->Unlock();},Cmd::SyncLock);
    if(!ran){SliceMem().swap(s.shadow);return -1;}
    if(!ok){SliceMem().swap(s.shadow);s.shadowDead=true;return 0;}   // the real buffer cannot be read: pass-through as before
    add(dyn?q.stats.dynShadowReadbacks:q.stats.stShadowReadbacks);
    registerShadow(self,s,large);return 1;
}
// Memory pressure (game thread, at a Present): drop shadows of buffers not locked for idleFrames, then, while still over the (halved) cap, the
// least recently locked unlocked ones (both kinds, one LRU). A dropped buffer's next write re-lock reads it back again (or a DISCARD lock of a
// DYNAMIC one makes a zero-filled shadow) when the cap allows: the correctness rule is unchanged.
// texMutex held, game thread: frees the ring slices no reader needs any more (all of them under memory pressure; olderThan = idle trimming: only those free for that many frames). The current slice stays.
inline void releaseRingLocked(ProxyBase& p,BufferState& s,std::uint64_t olderThan=0){
    if(s.mode!=BufferState::Free)return;   // (a lock in progress, maybe waiting in a pumped wait: its slices stay)
    const std::uint64_t rep=p.core->q.replayedSeq();
    for(std::size_t i=s.ring.size();i-->0;){
        auto& sl=s.ring[i];
        if(rep<sl.seq||p.core->frameNo<sl.frame+olderThan)continue;
        retireSliceLocked(p,sl.mem,sl.seq,true,s.large);s.ring.erase(s.ring.begin()+i);
    }
}
// Game thread, every Present (scanning every 16th frame): ring slices that have been free for kLargeIdleFrames frames are freed (the memory returns when the crowd goes away).
inline void trimIdleRings(StreamCore& c){
    if(c.frameNo%16!=0||c.q.stats.ringSlices.load(std::memory_order_relaxed)<=0)return;   // (the idle threshold is 60 frames: a scan every 16 is plenty)
    std::lock_guard<std::mutex> l(c.texMutex);
    for(BufferState* s=c.bufRegular.head;s;s=s->lru.next)if(!s->ring.empty())releaseRingLocked(*s->proxy,*s,kLargeIdleFrames);
    for(BufferState* s=c.bufLarge.head;s;s=s->lru.next)if(!s->ring.empty())releaseRingLocked(*s->proxy,*s,kLargeIdleFrames);
}
inline void dropIdleBufferShadows(StreamCore& c,unsigned idleFrames){
    std::lock_guard<std::mutex> l(c.texMutex);
    for(BufferState* s=c.bufLarge.head;s;){   // the large allowance goes first and whole (never a locked one: its unlock or the next tick, see unlockBuffer)
        BufferState* n=s->lru.next;ProxyBase& p=*s->proxy;
        if(s->mode==BufferState::Free){noteShadowEvicted(c,p);dropShadowLocked(p,*s);}
        s=n;
    }
    for(BufferState* s=c.bufRegular.head;s&&c.frameNo>=s->lastUse+idleFrames;){   // least recent first: the scan ends at the first one used within idleFrames
        BufferState* n=s->lru.next;ProxyBase& p=*s->proxy;
        if(s->mode==BufferState::Free){noteShadowEvicted(c,p);dropShadowLocked(p,*s);}
        s=n;
    }
    for(BufferState* s=c.bufRegular.head;s;s=s->lru.next)releaseRingLocked(*s->proxy,*s);   // 0.3.204 (task 21): the free non-current slices go first with the pressure (the shadows that stay keep their current slice)
    for(BufferState* s=c.bufLarge.head;s;s=s->lru.next)releaseRingLocked(*s->proxy,*s);
    while(!c.q.shadowAdmit(0)){
        BufferState* victim=c.bufRegular.head;while(victim&&(victim->mode!=BufferState::Free||slicesBusy(c,*victim)))victim=victim->lru.next;
        if(!victim)break;
        ProxyBase& p=*victim->proxy;noteShadowEvicted(c,p);dropShadowLocked(p,*victim);
    }
}
// Range rules (our reading of DXVK, not verified against it): an offset beyond the end fails with INVALIDCALL; a size of 0 or one
// that runs past the end becomes "to the end" instead of failing, so the staged/shadow range and the replayed Lock are the clamped one.
// 0.3.204 (task 21): game thread. Waits (pumped, exact) until the replay thread has retired command seq; counted as a rename wait, a write wait (flags 0) or, 0.3.205 (#34), an overlap wait (a NOOVERWRITE lock over a pending ref); the time also lands in syncNs, so the split timers stay right.
// false = nested in a pumped wait (a message handler DXVK's pump dispatched): nothing waited, the caller takes the fallback.
enum class SliceWait {Rename,Write,Overlap};
inline bool waitSliceSeq(StreamCore& c,std::uint64_t seq,SliceWait kind){
    Queue& q=c.q;if(q.replayedSeq()>=seq)return true;
    if(inPumpedWait){add(q.stats.nestedSyncs);return false;}
    const std::uint64_t t0=nowNs();q.waitReplayed(seq,WaitKind::Sync);const std::uint64_t ns=nowNs()-t0;
    own(kind==SliceWait::Rename?q.stats.renameWaits:kind==SliceWait::Write?q.stats.writeWaits:q.stats.zcOverlapWaits);own(kind==SliceWait::Rename?q.stats.renameWaitNs:kind==SliceWait::Write?q.stats.writeWaitNs:q.stats.zcOverlapWaitNs,ns);return true;
}
constexpr std::uint64_t kDiscardWindowFrames=60;
// Game thread, at every DISCARD write lock of a slice-capable buffer: counts it in the current frame; the largest per-frame count of the last kDiscardWindowFrames frames decays. Returns the count that sizes the ring.
inline unsigned noteDiscard(StreamCore& c,BufferState& s){
    if(s.discFrame!=c.frameNo){   // a new frame: the one just ended competes for the window's maximum (an expired maximum is replaced by it)
        if(s.discNow>=s.discMax||c.frameNo>=s.discMaxFrame+kDiscardWindowFrames){s.discMax=s.discNow;s.discMaxFrame=s.discFrame;}
        s.discFrame=c.frameNo;s.discNow=0;
    }
    ++s.discNow;raiseMax(c.q.stats.maxDiscards,s.discNow);
    return s.discNow>s.discMax?s.discNow:s.discMax;
}
constexpr std::size_t kZeroCopyMin=4096;   // a smaller range keeps the inline copy (cheaper than a reference and its bookkeeping)
// Game thread: a write lock of a DYNAMIC buffer's current slice (s.shadow). The replay thread may still read that slice for zero-copy unlocks recorded earlier (s.sliceSeq), so:
// no pending reader: as it is. DISCARD (the contents are undefined): the game gets another slice of the buffer's ring, the old one stays (renamed) for its readers: the oldest free one (its readers retired); else a new one if the
// ring has room (framesAhead+2 slices in all), memory allows (no pressure; the ring budget: regular rings up to the base buffer-shadow cap, large rings up to the large allowance, both apart from the shadows' own caps) and the allocation succeeds; else the game waits for the OLDEST busy slice (exact).
// NOOVERWRITE: the D3D9 contract says the game writes only bytes the GPU does not read, but the replay thread's zero-copy unlocks (refs) read game-side bytes of ranges the GPU never uses: a NOOVERWRITE lock overlapping the range of an
// unreplayed ref (or of an unapplied patch) returns that seq in `stage` (counted zcOverlapLocks), and lockBuffer stages the lock instead of handing out the slice (0.3.205, #34: no wait; the cost is copies of the locked bytes only).
// A NOOVERWRITE lock over no such range: as it is. Any other write: waits for the readers (exact).
// false = nested in a pumped wait with the slice busy: the caller drops the shadow (it retires with its readers) and takes the unshadowed path.
// Memory pressure (the 32-bit address space is nearly full): no slice is added and no new reader is created: a DISCARD/NOOVERWRITE unlock COPIES into the queue as before zero-copy (zc=false, so the slice never gets
// new pending readers); a DISCARD whose current slice still has readers from before the pressure takes a free ring slice if there is one, else waits ONCE for the current slice (it drains within the frames in flight) and goes on in copy mode.
inline bool prepareSliceWrite(ProxyBase& self,BufferState& s,DWORD eff,bool& zc,std::uint64_t& stage){
    StreamCore& c=*self.core;Queue& q=c.q;const bool pressure=q.pressure();
    zc=(eff&(D3::kLockDiscard|D3::kLockNoOverwrite))!=0&&!pressure;stage=0;
    const unsigned perFrame=(eff&D3::kLockDiscard)?noteDiscard(c,s):0;
    if(!(eff&D3::kLockDiscard)&&(eff&D3::kLockNoOverwrite)){   // 0.3.205 (#34): the range may still be read by an unreplayed ref (this slice's, whatever its unlock flags were, and also with zc off under memory pressure)
        if((stage=overlapSeq(q,s,s.off,s.off+s.size)))own(q.stats.zcOverlapLocks);
        return true;
    }
    if(q.replayedSeq()>=s.sliceSeq){if(eff&D3::kLockDiscard)dropPatches(q,s);else applyPatches(q,s);clearPending(s);return true;}
    if(eff&D3::kLockDiscard){
        const std::size_t len=self.info.length;trimRetired(c);
        const std::size_t allowed=StreamCore::allowedSlices(c.framesAhead,perFrame);
        bool atMax=false,noRoom=false,allocFailed=false;
        const std::uint64_t rep=q.replayedSeq();std::size_t pick=s.ring.size();
        for(std::size_t i=0;i<s.ring.size();++i)if(rep>=s.ring[i].seq&&(pick==s.ring.size()||s.ring[i].seq<s.ring[pick].seq))pick=i;
        if(pick==s.ring.size()&&pressure){   // nothing is allocated under pressure: the current slice drains (waited once below)
            own(q.stats.waitDrain);
            if(!waitSliceSeq(c,s.sliceSeq,SliceWait::Rename))return false;   // (the caller re-checks shadowOn: a nested path may have dropped the shadow during the wait)
            clearPending(s);dropPatches(q,s);return true;
        }
        if(pick==s.ring.size()&&s.ring.size()+1>=allowed)atMax=true;
        else if(pick==s.ring.size()){
            // 0.3.204 (task 21): ring slices have a fixed budget of their own (not the adaptive cap, not counted in shadowBytes/largeShadowBytes): regular rings (live + retired) up to RingRegularBudgetBytes,
            // large rings up to the large allowance
            const auto rb=s.large?q.stats.ringLargeBytes.load(std::memory_order_relaxed)+q.stats.retiredLargeBytes.load(std::memory_order_relaxed)
                                 :q.stats.ringRegularBytes.load(std::memory_order_relaxed)+q.stats.retiredRegularBytes.load(std::memory_order_relaxed);
            const bool room=(rb>0?std::size_t(rb):0)+len<=(s.large?LargeShadowBudgetBytes:RingRegularBudgetBytes);
            if(!room)noRoom=true;
            else{
                try{BufferState::Slice sl;sl.mem.resize(len+kLockSlack);sl.frame=c.frameNo;s.ring.push_back(std::move(sl));pick=s.ring.size()-1;}catch(...){pick=s.ring.size();allocFailed=true;}   // (resize: uninitialized, the contents of a DISCARD are undefined)
                if(pick<s.ring.size()){q.stats.ringBytes.fetch_add(std::int64_t(len),std::memory_order_relaxed);(s.large?q.stats.ringLargeBytes:q.stats.ringRegularBytes).fetch_add(std::int64_t(len),std::memory_order_relaxed);q.stats.ringSlices.fetch_add(1,std::memory_order_relaxed);own(q.stats.renameAllocs);raiseMax(q.stats.maxRing,s.ring.size());}
            }
        }
        if(pick==s.ring.size()){   // none free, none allowed: why (the zerocopy line's waitWhy)
            own(atMax?q.stats.waitMax:noRoom?q.stats.waitBudget:allocFailed?q.stats.waitAlloc:q.stats.waitMax);
            if(s.ring.empty()){if(!waitSliceSeq(c,s.sliceSeq,SliceWait::Rename))return false;clearPending(s);dropPatches(q,s);return true;}   // no second slice: the readers must finish before the game overwrites this one
            pick=0;for(std::size_t i=1;i<s.ring.size();++i)if(s.ring[i].seq<s.ring[pick].seq)pick=i;   // the oldest busy one
            if(!waitSliceSeq(c,s.ring[pick].seq,SliceWait::Rename))return false;
            if(!s.shadowOn)return true;   // a nested path (ProcessVertices) dropped the shadow and its ring during the wait: nothing left to swap; the caller takes the unshadowed path
        }
        auto& sl=s.ring[pick];s.shadow.swap(sl.mem);sl.seq=s.sliceSeq;sl.frame=c.frameNo;s.sliceSeq=0;clearPending(s);dropPatches(q,s);own(q.stats.renames);return true;
    }
    if(!waitSliceSeq(c,s.sliceSeq,SliceWait::Write))return false;
    applyPatches(q,s);clearPending(s);return true;
}
inline HRESULT lockBuffer(ProxyBase& self,BufferState& s,UINT off,UINT size,void** pp,DWORD flags){
    LockScope _ts(*self.core);Queue& q=self.core->q;const UINT length=self.info.length;
    if(!pp||s.mode!=BufferState::Free||off>length)return D3DERR_INVALIDCALL;
    s.whole=!size||(off==0&&size>=length);
    if(!size||std::uint64_t(off)+size>length)size=length-off;
    s.off=off;s.size=size;s.flags=flags;
    const DWORD eff=effectiveLockFlags(flags,self.info.pool);
    // a buffer without a shadow (refused at creation, evicted, dropped) gets one now when room can be made: where its contents are undefined anyway (a DYNAMIC
    // buffer's DISCARD lock, default pool) a zero-filled one; at a WRITE lock of a written buffer one whole-buffer readback (a first write / non-DYNAMIC DISCARD
    // stays on the staged path; a READONLY lock stays a pass-through like a texture's); a refusal keeps the old paths
    if(!s.shadowOn&&s.canShadow&&!s.shadowDead){
        if(dynamicBuffer(self)&&(eff&D3::kLockDiscard)){if(takeShadow(self,s,true))add(q.stats.shadowLate);}
        else if(s.written&&!(eff&D3::kLockDiscard)&&!(flags&D3::kLockReadOnly)){
            if(readBackShadow(self,s)<0)return D3DERR_INVALIDCALL;
        }
    }
    s.zc=false;s.copyP=false;
    if(s.shadowOn&&!s.patches.empty()&&(flags&D3::kLockReadOnly)){   // 0.3.205 (#34): the game reads the slice: the patches over the range go in first (a wait for the newest, exact; nested in a pumped wait the shadow goes)
        std::uint64_t w=0;applyPatches(q,s);for(const auto& p:s.patches)if(p.off<off+size&&off<p.off+UINT(p.bytes.size())&&p.seq>w)w=p.seq;
        if(w){
            s.mode=BufferState::Shadow;const bool waited=waitSliceSeq(*self.core,w,SliceWait::Overlap);s.mode=BufferState::Free;
            if(s.shadowOn){if(waited)applyPatches(q,s);else{std::lock_guard<std::mutex> l(self.core->texMutex);dropShadowLocked(self,s);}}
        }
    }
    if(s.shadowOn&&dynamicBuffer(self)&&self.info.pool==D3::kPoolDefault&&!(flags&D3::kLockReadOnly)){   // 0.3.204 (task 21): a write lock of a DYNAMIC default-pool buffer's slice, large allowance or regular cap (see prepareSliceWrite)
        bool zc=false;std::uint64_t stage=0;
        // the buffer is busy while prepareSliceWrite may wait (a PUMPED wait): a nested Lock of it fails as a double lock, evictions, pressure drops and ring trimming skip it; a nested ProcessVertices
        // can still drop its shadow (checked below)
        s.mode=BufferState::Shadow;bool ok=prepareSliceWrite(self,s,eff,zc,stage);s.mode=BufferState::Free;
        if(s.shadowOn&&ok&&stage){   // 0.3.205 (#34): a NOOVERWRITE lock over bytes the replay thread still reads: the game writes a staged Block with the range's current bytes (its unlock keeps them as a patch)
            Block* b=nullptr;
            if(!q.pressure()&&s.patchBytes+size<=length&&(b=q.tryAllocBlock(size+kLockSlack))){
                readSliceRange(s,b->data(),off,size);b->used=size;s.mode=BufferState::Staged;s.stage=b;s.stageSeq=stage;touchBufferShadow(*self.core,s);*pp=b->data();own(q.stats.lockAsync);return D3D_OK;
            }
            // no Block, the patches already hold a whole buffer, or memory pressure: the exact wait (the range is free once the replay thread has passed `stage` and the passed patches are in)
            s.mode=BufferState::Shadow;ok=waitSliceSeq(*self.core,stage,SliceWait::Overlap);s.mode=BufferState::Free;
            if(s.shadowOn&&ok)applyPatches(q,s);
        }
        if(!s.shadowOn){}   // dropped during the wait: the unshadowed paths below
        else if(ok){s.zc=zc;s.copyP=!zc&&(eff&(D3::kLockDiscard|D3::kLockNoOverwrite))!=0;}
        else{   // nested in a pumped wait: the shadow (retired with its readers) goes. A DISCARD stages below; any other write copies the range's current bytes (the game-side truth) into a staged Block, which its unlock queues without a copy
            Block* b=nullptr;
            if(!(eff&D3::kLockDiscard)&&(b=q.tryAllocBlock(size+kLockSlack))){readSliceRange(s,b->data(),off,size);b->used=size;}
            {std::lock_guard<std::mutex> l(self.core->texMutex);dropShadowLocked(self,s);}
            if(b){s.mode=BufferState::Staged;s.stage=b;*pp=b->data();own(q.stats.lockAsync);return D3D_OK;}
        }
    }
    if(s.shadowOn){s.mode=BufferState::Shadow;touchBufferShadow(*self.core,s);*pp=s.shadow.data()+off;own(q.stats.lockAsync);return D3D_OK;}
    PassReason why=PassReason::NoShadow;
    if(flags&D3::kLockReadOnly)why=PassReason::ReadOnly;
    else if(!s.written||(eff&D3::kLockDiscard)){
        // 0.3.192 (CS): a small lock stages in game-side scratch and unlocks as an inline record (like the shadow path): no pooled Block, no cross-thread pool mutex.
        // The range is zeroed (bytes the game leaves unwritten replay deterministically); the slack past it is never replayed and stays as it was.
        if(unsigned char* sp=self.core->scratch.take(size,s.scratchSlot)){s.mode=BufferState::Scratch;std::memset(sp,0,size);*pp=sp;own(q.stats.lockAsync);return D3D_OK;}
        if(Block* b=q.tryAllocBlock(size+kLockSlack)){s.mode=BufferState::Staged;s.stage=b;b->used=size;std::memset(b->data(),0,size);*pp=b->data();own(q.stats.lockAsync);return D3D_OK;}
        why=PassReason::Budget;
    }
    countPass(self,why);
    HRESULT hr=D3DERR_INVALIDCALL;void* got=nullptr;
    const bool ran=runTask(*self.core,[&](StreamCore&){
        if(!self.inner||self.dead.load())return;
        hr=self.kind==Kind::VertexBuffer?static_cast<IDirect3DVertexBuffer9*>(self.inner)->Lock(off,size,&got,flags)
                                         :static_cast<IDirect3DIndexBuffer9*>(self.inner)->Lock(off,size,&got,flags);},Cmd::SyncLock);
    if(!ran)return D3DERR_INVALIDCALL;
    if(SUCCEEDED(hr)){s.mode=BufferState::Pass;*pp=got;}
    return hr;
}
inline void noteRecorded(ProxyBase& p,const BufferState& s,UINT bytes){add(p.core->q.stats.lockRecordedBytes,bytes);if(s.whole)add(p.core->q.stats.wholeLockBytes,bytes);}
inline HRESULT unlockBufferImpl(ProxyBase& self,BufferState& s,bool& zcDone,bool& overlapStaged){
    LockScope _ts(*self.core);Queue& q=self.core->q;
    switch(s.mode){
    case BufferState::Free:return D3DERR_INVALIDCALL;
    case BufferState::Shadow:{
        s.mode=BufferState::Free;const bool zc=s.zc;s.zc=false;
        if(!s.shadowOn)return D3D_OK;   // (a GPU write dropped it under a lock: nothing left to copy)
        if((s.flags&D3::kLockReadOnly)||!s.size)return D3D_OK;   // (a zero-length range must not be replayed: Lock(off,0) means to the end)
        noteRecorded(self,s,s.size);   // zero-copy unlocks count too (before/after comparisons stay valid); zeroCopyBytes says how much of it went without a copy
        if(zc&&s.size>=kZeroCopyMin){   // 0.3.204 (task 21): no copy: the replay thread reads the slice itself; the slice stays alive and unwritten (for conflicting locks) until this command retires
            // 0.3.205 (#34): the ref reads s.shadow[off, off+size) until it retires; a later NOOVERWRITE lock into that range is staged (prepareSliceWrite), so the replay thread's memcpy never overlaps a game write
            auto* a=static_cast<UnlockBufferRefArgs*>(q.reserve((std::uint16_t)Cmd::UnlockBufferRef,sizeof(UnlockBufferRefArgs),kFlagWaitTarget));   // flagged: prepareSliceWrite may wait for it
            *a=UnlockBufferRefArgs{&self,s.shadow.data(),s.off,s.size,s.flags,0};q.commit();s.sliceSeq=q.recordedSeq();pushPending(q,s,s.sliceSeq,s.off,s.off+s.size);
            own(q.stats.zeroCopyUnlocks);own(q.stats.zeroCopyBytes,s.size);zcDone=true;
        }else{
        if(s.size<=MaxInlinePayload-sizeof(UnlockBufferArgs)-16){
            auto* a=static_cast<UnlockBufferArgs*>(q.reserve((std::uint16_t)Cmd::UnlockBuffer,std::uint32_t(sizeof(UnlockBufferArgs)+s.size)));
            *a=UnlockBufferArgs{&self,s.off,s.size,s.flags,1};std::memcpy(a+1,s.shadow.data()+s.off,s.size);q.commit();
        }else if(Block* b=q.tryAllocBlock(s.size)){
            auto* a=static_cast<UnlockBufferArgs*>(q.reserveWithBlock((std::uint16_t)Cmd::UnlockBuffer,sizeof(UnlockBufferArgs),b));
            *a=UnlockBufferArgs{&self,s.off,s.size,s.flags,0};b->used=s.size;std::memcpy(b->data(),s.shadow.data()+s.off,s.size);q.commit();
        }else{   // no Block: the same range in inline pieces (DISCARD only on the first)
            const UINT piece=128u<<10;
            for(UINT done=0;done<s.size;done+=piece){
                const UINT n=s.size-done<piece?s.size-done:piece;
                auto* a=static_cast<UnlockBufferArgs*>(q.reserve((std::uint16_t)Cmd::UnlockBuffer,std::uint32_t(sizeof(UnlockBufferArgs)+n)));
                *a=UnlockBufferArgs{&self,s.off+done,n,done?(s.flags&~D3::kLockDiscard):s.flags,1};std::memcpy(a+1,s.shadow.data()+s.off+done,n);q.commit();
            }
        }
        }
        s.written=true;
        if(s.large&&q.pressure()){std::lock_guard<std::mutex> l(self.core->texMutex);noteShadowEvicted(*self.core,self);dropShadowLocked(self,s);}   // pressure began under the lock: the large shadow goes now
        return D3D_OK;}
    case BufferState::Scratch:{
        noteRecorded(self,s,s.size);
        auto* a=static_cast<UnlockBufferArgs*>(q.reserve((std::uint16_t)Cmd::UnlockBuffer,std::uint32_t(sizeof(UnlockBufferArgs)+s.size)));
        *a=UnlockBufferArgs{&self,s.off,s.size,s.flags,1};std::memcpy(a+1,self.core->scratch.slot[s.scratchSlot].data(),s.size);q.commit();
        self.core->scratch.give(s.scratchSlot);s.mode=BufferState::Free;s.written=true;return D3D_OK;}
    case BufferState::Staged:{
        noteRecorded(self,s,s.size);
        if(const std::uint64_t ov=s.stageSeq){   // 0.3.205 (#34): an overlap stage: the slice gets these bytes once the replay thread has passed ov (applyPatches); copied before the commit hands the Block over
            s.stageSeq=0;overlapStaged=true;
            if(s.shadowOn){
                bool kept=false;
                try{s.patches.push_back(BufferState::Patch{ov,s.off,std::vector<unsigned char>(s.stage->data(),s.stage->data()+s.size)});kept=true;}catch(...){}
                if(kept){s.patchBytes+=s.size;if(!s.patchMinSeq||ov<s.patchMinSeq)s.patchMinSeq=ov;q.stats.zcPatchBytes.fetch_add(std::int64_t(s.size),std::memory_order_relaxed);}
                else if(waitSliceSeq(*self.core,ov,SliceWait::Overlap)){if(s.shadowOn){applyPatches(q,s);std::memcpy(s.shadow.data()+s.off,s.stage->data(),s.size);}}   // no memory for the patch: the exact wait, then the slice directly
                else if(s.shadowOn){std::lock_guard<std::mutex> l(self.core->texMutex);dropShadowLocked(self,s);}   // (nested in a pumped wait: the shadow goes, the bytes are in the queue)
            }
        }
        auto* a=static_cast<UnlockBufferArgs*>(q.reserveWithBlock((std::uint16_t)Cmd::UnlockBuffer,sizeof(UnlockBufferArgs),s.stage));
        *a=UnlockBufferArgs{&self,s.off,s.size,s.flags,0};q.commit();s.stage=nullptr;s.mode=BufferState::Free;s.written=true;return D3D_OK;}
    default:{
        HRESULT hr=D3DERR_INVALIDCALL;
        if(!runTask(*self.core,[&](StreamCore&){if(self.inner)hr=self.kind==Kind::VertexBuffer?static_cast<IDirect3DVertexBuffer9*>(self.inner)->Unlock():static_cast<IDirect3DIndexBuffer9*>(self.inner)->Unlock();},Cmd::SyncUnlock))return D3DERR_INVALIDCALL;
        s.mode=BufferState::Free;if(!(s.flags&D3::kLockReadOnly))s.written=true;return hr;}
    }
}
// 0.3.204 (task 21): why a DISCARD/NOOVERWRITE write unlock of a DYNAMIC buffer did not go zero-copy (the CSTREAM zerocopy line): not the default pool (the flags mean nothing there), no shadow
// slice (staged, scratch, pass-through or a dropped shadow), a range below kZeroCopyMin, a NOOVERWRITE lock staged over an unreplayed ref (0.3.205, #34), anything else.
inline HRESULT unlockBuffer(ProxyBase& self,BufferState& s){
    const BufferState::Mode m=s.mode;const DWORD fl=s.flags;const UINT sz=s.size;const bool cp=s.copyP;bool zcDone=false,ovl=false;
    const HRESULT hr=unlockBufferImpl(self,s,zcDone,ovl);
    if(!zcDone&&m!=BufferState::Free&&dynamicBuffer(self)&&(fl&(D3::kLockDiscard|D3::kLockNoOverwrite))&&!(fl&D3::kLockReadOnly)&&SUCCEEDED(hr)){
        Counters& st=self.core->q.stats;
        const bool underPressure=cp&&m==BufferState::Shadow&&sz>=kZeroCopyMin&&self.info.pool==D3::kPoolDefault;
        if(underPressure){own(st.copyPressureUnlocks);own(st.copyPressureBytes,sz);}   // would have been zero-copy: copied because of memory pressure
        if(ovl){own(st.zcOverlapStaged);own(st.zcOverlapCopyBytes,sz);}   // 0.3.205 (#34): a NOOVERWRITE unlock staged because its range overlapped bytes the replay thread still read (the reason: small below kZeroCopyMin, else overlap)
        if(ovl)own(sz>=kZeroCopyMin?st.zcSkipOverlap:st.zcSkipSmall);
        else own(self.info.pool!=D3::kPoolDefault?st.zcSkipPool:m!=BufferState::Shadow?st.zcSkipNoShadow:sz<kZeroCopyMin?st.zcSkipSmall:underPressure?st.zcSkipPressure:st.zcSkipOther);
    }
    return hr;
}
inline void releaseStage(ProxyBase& p,BufferState& s){s.stageSeq=0;if(s.stage){p.core->q.freeBlock(s.stage);s.stage=nullptr;}if(s.mode==BufferState::Scratch){p.core->scratch.give(s.scratchSlot);s.mode=BufferState::Free;}}

// ---- concrete proxies ----
// The vtable pointer of each concrete proxy class, learned when the first one is constructed: ProxyBase::of recognizes a stream proxy by it.
inline std::atomic<const void*> proxyShape[13];
template<class D> struct ProxyInit {   // registers a freshly constructed proxy
    static void apply(D* d,IUnknown* u){proxyShape[unsigned(d->kind)].store(*reinterpret_cast<const void* const*>(u),std::memory_order_relaxed);d->unk=u;d->core->reg.addProxy(d);liveProxyObjects.fetch_add(1);}
};

struct StreamSurface final:IDirect3DSurface9,ProxyBase {
    SubRes own;SubRes* subp=&own;UINT sub=0;   // child of a texture level/face: subp points into the container's table
    StreamSurface(StreamCore* c,const Info& i,ProxyBase* container=nullptr,SubRes* shared=nullptr):ProxyBase(c,Kind::Surface){
        info=i;info.type=D3::kTypeSurface;parent=container;if(shared)subp=shared;
        own.written=(i.usage&(D3::kUsageRT|D3::kUsageDS))!=0;
        ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DSurface9*>(this));
    }
    ~StreamSurface(){dropSubShadow(*core,own);liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_SURFACE_METHODS
    NL_PROXY_COM(IDirect3DSurface9,IDirect3DResource9)
    NL_RES_LOCALS(Surface)
    HRESULT local(CmdTag<Cmd::Surface_GetDesc>,D3DSURFACE_DESC* d){
        if(!d)return D3DERR_INVALIDCALL;
        d->Format=(D3DFORMAT)info.fmt;d->Type=D3DRTYPE_SURFACE;d->Usage=info.usage;d->Pool=(D3DPOOL)info.pool;d->MultiSampleType=(D3DMULTISAMPLE_TYPE)info.ms;
        d->MultiSampleQuality=info.msq;d->Width=info.w;d->Height=info.h;return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID id,void** pp) override{
        if(!pp)return D3DERR_INVALIDCALL;*pp=nullptr;
        if(parent)return parent->unk->QueryInterface(id,pp);
        return core->game->QueryInterface(id,pp);
    }
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT* lr,const RECT* rect,DWORD flags) override{
        return lockImage(*this,parent?*parent:static_cast<ProxyBase&>(*this),*subp,RouteSurface,0,0,info.w,info.h,1,lr,nullptr,rect,nullptr,flags,__builtin_return_address(0));}
    HRESULT STDMETHODCALLTYPE UnlockRect() override{return unlockImage(*this,*subp,RouteSurface,0,0);}
};
struct StreamVolume final:IDirect3DVolume9,ProxyBase {
    SubRes* subp;
    StreamVolume(StreamCore* c,const Info& i,ProxyBase* container,SubRes* shared):ProxyBase(c,Kind::Volume),subp(shared){
        info=i;info.type=D3::kTypeVolume;parent=container;ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DVolume9*>(this));}
    ~StreamVolume(){liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_VOLUME_METHODS
    NL_PROXY_COM(IDirect3DVolume9)
    HRESULT local(CmdTag<Cmd::Volume_GetDevice>,IDirect3DDevice9** pp){return devGet(pp);}
    HRESULT local(CmdTag<Cmd::Volume_SetPrivateData>,REFGUID g,const void* d,DWORD n,DWORD f){return setPrivate(g,d,n,f);}
    HRESULT local(CmdTag<Cmd::Volume_GetPrivateData>,REFGUID g,void* d,DWORD* n){return getPrivate(g,d,n);}
    HRESULT local(CmdTag<Cmd::Volume_FreePrivateData>,REFGUID g){return freePrivate(g);}
    HRESULT local(CmdTag<Cmd::Volume_GetDesc>,D3DVOLUME_DESC* d){
        if(!d)return D3DERR_INVALIDCALL;
        d->Format=(D3DFORMAT)info.fmt;d->Type=D3DRTYPE_VOLUME;d->Usage=info.usage;d->Pool=(D3DPOOL)info.pool;d->Width=info.w;d->Height=info.h;d->Depth=info.d;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID id,void** pp) override{if(!pp)return D3DERR_INVALIDCALL;*pp=nullptr;return parent?parent->unk->QueryInterface(id,pp):E_NOINTERFACE;}
    HRESULT STDMETHODCALLTYPE LockBox(D3DLOCKED_BOX* lb,const D3DBOX* box,DWORD flags) override{
        return lockImage(*this,*parent,*subp,RouteVolume,0,0,info.w,info.h,info.d,nullptr,lb,nullptr,box,flags,__builtin_return_address(0));}
    HRESULT STDMETHODCALLTYPE UnlockBox() override{return unlockImage(*this,*subp,RouteVolume,0,0);}
};

// The derive command: the replay thread asks `parent.inner` for a child and stores it in `child.inner`.
enum DeriveOp:UINT{DeriveSurfaceLevel=0,DeriveCubeFace=1,DeriveVolumeLevel=2,DeriveBackBuffer=3};
struct DeriveArgs {ProxyBase* parent;ProxyBase* child;UINT op,a,b;};
inline void recordDerive(StreamCore& c,ProxyBase* parent,ProxyBase* child,UINT op,UINT a,UINT b){
    auto* d=static_cast<DeriveArgs*>(c.q.reserve((std::uint16_t)Cmd::Derive,sizeof(DeriveArgs)));*d=DeriveArgs{parent,child,op,a,b};c.q.commit();child->readySeq=c.q.recordedSeq();
}

struct StreamTexture final:IDirect3DTexture9,ProxyBase {
    std::vector<SubRes> subs;DWORD lod=0;unsigned autoGenFilter=2;   // D3DTEXF_LINEAR
    StreamTexture(StreamCore* c,UINT w,UINT h,UINT levels,DWORD usage,unsigned fmt,DWORD pool):ProxyBase(c,Kind::Texture){
        info.w=w;info.h=h;info.levels=levels?levels:D3::fullChain(w,h,1);info.usage=usage;info.fmt=fmt;info.pool=pool;info.type=D3::kTypeTexture;
        subs.resize(info.levels);kids.assign(info.levels,nullptr);
        for(UINT i=0;i<info.levels;++i)subs[i].written=(usage&(D3::kUsageRT|D3::kUsageDS))||((usage&D3::kUsageAutoGen)&&i>0);
        if(usage&D3::kUsageAutoGen)for(UINT i=1;i<info.levels;++i)subs[i].shadowDead=true;   // the GPU writes the mips
        ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DTexture9*>(this));}
    ~StreamTexture(){for(auto& s:subs)dropSubShadow(*core,s);liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_TEXTURE_METHODS
    NL_PROXY_COM(IDirect3DTexture9,IDirect3DBaseTexture9,IDirect3DResource9)
    NL_RES_LOCALS(Texture)
    NL_TEX_LOCALS(Texture)
    HRESULT local(CmdTag<Cmd::Texture_GetLevelDesc>,UINT level,D3DSURFACE_DESC* d){
        if(!d||level>=info.levels)return D3DERR_INVALIDCALL;
        d->Format=(D3DFORMAT)info.fmt;d->Type=D3DRTYPE_SURFACE;d->Usage=info.usage;d->Pool=(D3DPOOL)info.pool;d->MultiSampleType=(D3DMULTISAMPLE_TYPE)0;d->MultiSampleQuality=0;
        d->Width=D3::mipDim(info.w,level);d->Height=D3::mipDim(info.h,level);return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT level,IDirect3DSurface9** pp) override{
        if(!pp)return D3DERR_INVALIDCALL;*pp=nullptr;if(level>=info.levels)return D3DERR_INVALIDCALL;
        auto* kid=static_cast<StreamSurface*>(kids[level]);
        if(!kid){
            Info i=info;i.w=D3::mipDim(info.w,level);i.h=D3::mipDim(info.h,level);i.levels=1;
            try{kid=new StreamSurface(core,i,this,&subs[level]);}catch(...){return E_OUTOFMEMORY;}
            kids[level]=kid;adoptKid();   // the new kid's one game reference keeps this texture alive
            recordDerive(*core,this,kid,DeriveSurfaceLevel,level,0);
        }else kid->comAddRef();
        *pp=kid;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE LockRect(UINT level,D3DLOCKED_RECT* lr,const RECT* rect,DWORD flags) override{
        if(level>=info.levels)return D3DERR_INVALIDCALL;
        return lockImage(*this,*this,subs[level],RouteTexture,level,0,D3::mipDim(info.w,level),D3::mipDim(info.h,level),1,lr,nullptr,rect,nullptr,flags,__builtin_return_address(0));}
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT level) override{if(level>=info.levels)return D3DERR_INVALIDCALL;return unlockImage(*this,subs[level],RouteTexture,level,0);}
};
struct StreamCubeTexture final:IDirect3DCubeTexture9,ProxyBase {
    std::vector<SubRes> subs;DWORD lod=0;unsigned autoGenFilter=2;
    StreamCubeTexture(StreamCore* c,UINT edge,UINT levels,DWORD usage,unsigned fmt,DWORD pool):ProxyBase(c,Kind::CubeTexture){
        info.w=info.h=edge;info.levels=levels?levels:D3::fullChain(edge,edge,1);info.usage=usage;info.fmt=fmt;info.pool=pool;info.type=D3::kTypeCube;
        subs.resize(std::size_t(info.levels)*6);kids.assign(subs.size(),nullptr);
        for(std::size_t i=0;i<subs.size();++i)subs[i].written=(usage&(D3::kUsageRT|D3::kUsageDS))||((usage&D3::kUsageAutoGen)&&(i%info.levels)>0);
        ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DCubeTexture9*>(this));}
    ~StreamCubeTexture(){for(auto& s:subs)dropSubShadow(*core,s);liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_CUBETEXTURE_METHODS
    NL_PROXY_COM(IDirect3DCubeTexture9,IDirect3DBaseTexture9,IDirect3DResource9)
    NL_RES_LOCALS(CubeTexture)
    NL_TEX_LOCALS(CubeTexture)
    HRESULT local(CmdTag<Cmd::CubeTexture_GetLevelDesc>,UINT level,D3DSURFACE_DESC* d){
        if(!d||level>=info.levels)return D3DERR_INVALIDCALL;
        d->Format=(D3DFORMAT)info.fmt;d->Type=D3DRTYPE_SURFACE;d->Usage=info.usage;d->Pool=(D3DPOOL)info.pool;d->MultiSampleType=(D3DMULTISAMPLE_TYPE)0;d->MultiSampleQuality=0;
        d->Width=D3::mipDim(info.w,level);d->Height=D3::mipDim(info.h,level);return D3D_OK;}
    std::size_t idx(UINT face,UINT level)const{return std::size_t(face)*info.levels+level;}
    HRESULT STDMETHODCALLTYPE GetCubeMapSurface(D3DCUBEMAP_FACES face,UINT level,IDirect3DSurface9** pp) override{
        if(!pp)return D3DERR_INVALIDCALL;*pp=nullptr;if(level>=info.levels||unsigned(face)>=6)return D3DERR_INVALIDCALL;
        const std::size_t k=idx(unsigned(face),level);auto* kid=static_cast<StreamSurface*>(kids[k]);
        if(!kid){
            Info i=info;i.w=D3::mipDim(info.w,level);i.h=D3::mipDim(info.h,level);i.levels=1;
            try{kid=new StreamSurface(core,i,this,&subs[k]);}catch(...){return E_OUTOFMEMORY;}
            kids[k]=kid;adoptKid();
            recordDerive(*core,this,kid,DeriveCubeFace,unsigned(face),level);
        }else kid->comAddRef();
        *pp=kid;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE LockRect(D3DCUBEMAP_FACES face,UINT level,D3DLOCKED_RECT* lr,const RECT* rect,DWORD flags) override{
        if(level>=info.levels||unsigned(face)>=6)return D3DERR_INVALIDCALL;
        return lockImage(*this,*this,subs[idx(unsigned(face),level)],RouteCube,level,unsigned(face),D3::mipDim(info.w,level),D3::mipDim(info.h,level),1,lr,nullptr,rect,nullptr,flags,__builtin_return_address(0));}
    HRESULT STDMETHODCALLTYPE UnlockRect(D3DCUBEMAP_FACES face,UINT level) override{
        if(level>=info.levels||unsigned(face)>=6)return D3DERR_INVALIDCALL;return unlockImage(*this,subs[idx(unsigned(face),level)],RouteCube,level,unsigned(face));}
};
struct StreamVolumeTexture final:IDirect3DVolumeTexture9,ProxyBase {
    std::vector<SubRes> subs;DWORD lod=0;unsigned autoGenFilter=2;
    StreamVolumeTexture(StreamCore* c,UINT w,UINT h,UINT d,UINT levels,DWORD usage,unsigned fmt,DWORD pool):ProxyBase(c,Kind::VolumeTexture){
        info.w=w;info.h=h;info.d=d;info.levels=levels?levels:D3::fullChain(w,h,d);info.usage=usage;info.fmt=fmt;info.pool=pool;info.type=D3::kTypeVolumeTexture;
        subs.resize(info.levels);kids.assign(info.levels,nullptr);
        ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DVolumeTexture9*>(this));}
    ~StreamVolumeTexture(){for(auto& s:subs)dropSubShadow(*core,s);liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_VOLUMETEXTURE_METHODS
    NL_PROXY_COM(IDirect3DVolumeTexture9,IDirect3DBaseTexture9,IDirect3DResource9)
    NL_RES_LOCALS(VolumeTexture)
    NL_TEX_LOCALS(VolumeTexture)
    HRESULT local(CmdTag<Cmd::VolumeTexture_GetLevelDesc>,UINT level,D3DVOLUME_DESC* d){
        if(!d||level>=info.levels)return D3DERR_INVALIDCALL;
        d->Format=(D3DFORMAT)info.fmt;d->Type=D3DRTYPE_VOLUME;d->Usage=info.usage;d->Pool=(D3DPOOL)info.pool;
        d->Width=D3::mipDim(info.w,level);d->Height=D3::mipDim(info.h,level);d->Depth=D3::mipDim(info.d,level);return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetVolumeLevel(UINT level,IDirect3DVolume9** pp) override{
        if(!pp)return D3DERR_INVALIDCALL;*pp=nullptr;if(level>=info.levels)return D3DERR_INVALIDCALL;
        auto* kid=static_cast<StreamVolume*>(kids[level]);
        if(!kid){
            Info i=info;i.w=D3::mipDim(info.w,level);i.h=D3::mipDim(info.h,level);i.d=D3::mipDim(info.d,level);i.levels=1;
            try{kid=new StreamVolume(core,i,this,&subs[level]);}catch(...){return E_OUTOFMEMORY;}
            kids[level]=kid;adoptKid();recordDerive(*core,this,kid,DeriveVolumeLevel,level,0);
        }else kid->comAddRef();
        *pp=kid;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE LockBox(UINT level,D3DLOCKED_BOX* lb,const D3DBOX* box,DWORD flags) override{
        if(level>=info.levels)return D3DERR_INVALIDCALL;
        return lockImage(*this,*this,subs[level],RouteVolumeTexture,level,0,D3::mipDim(info.w,level),D3::mipDim(info.h,level),D3::mipDim(info.d,level),nullptr,lb,nullptr,box,flags,__builtin_return_address(0));}
    HRESULT STDMETHODCALLTYPE UnlockBox(UINT level) override{if(level>=info.levels)return D3DERR_INVALIDCALL;return unlockImage(*this,subs[level],RouteVolumeTexture,level,0);}
};

struct StreamVertexBuffer final:IDirect3DVertexBuffer9,ProxyBase {
    BufferState buf;
    StreamVertexBuffer(StreamCore* c,UINT length,DWORD usage,DWORD fvf,DWORD pool,bool game=true):ProxyBase(c,Kind::VertexBuffer){
        info.length=length;info.usage=usage;info.fvf=fvf;info.pool=pool;info.fmt=D3::kFmtVertexData;info.type=D3::kTypeVB;
        initShadow(*this,buf,game);ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DVertexBuffer9*>(this));}
    ~StreamVertexBuffer(){dropShadow(*this,buf);releaseStage(*this,buf);liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_VERTEXBUFFER_METHODS
    NL_PROXY_COM(IDirect3DVertexBuffer9,IDirect3DResource9)
    NL_RES_LOCALS(VertexBuffer)
    HRESULT local(CmdTag<Cmd::VertexBuffer_GetDesc>,D3DVERTEXBUFFER_DESC* d){
        if(!d)return D3DERR_INVALIDCALL;d->Format=(D3DFORMAT)info.fmt;d->Type=D3DRTYPE_VERTEXBUFFER;d->Usage=info.usage;d->Pool=(D3DPOOL)info.pool;d->Size=info.length;d->FVF=info.fvf;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE Lock(UINT off,UINT size,void** pp,DWORD flags) override{return lockBuffer(*this,buf,off,size,pp,flags);}
    HRESULT STDMETHODCALLTYPE Unlock() override{return unlockBuffer(*this,buf);}
};
struct StreamIndexBuffer final:IDirect3DIndexBuffer9,ProxyBase {
    BufferState buf;
    StreamIndexBuffer(StreamCore* c,UINT length,DWORD usage,unsigned fmt,DWORD pool,bool game=true):ProxyBase(c,Kind::IndexBuffer){
        info.length=length;info.usage=usage;info.fmt=fmt;info.pool=pool;info.type=D3::kTypeIB;
        initShadow(*this,buf,game);ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DIndexBuffer9*>(this));}
    ~StreamIndexBuffer(){dropShadow(*this,buf);releaseStage(*this,buf);liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_INDEXBUFFER_METHODS
    NL_PROXY_COM(IDirect3DIndexBuffer9,IDirect3DResource9)
    NL_RES_LOCALS(IndexBuffer)
    HRESULT local(CmdTag<Cmd::IndexBuffer_GetDesc>,D3DINDEXBUFFER_DESC* d){
        if(!d)return D3DERR_INVALIDCALL;d->Format=(D3DFORMAT)info.fmt;d->Type=D3DRTYPE_INDEXBUFFER;d->Usage=info.usage;d->Pool=(D3DPOOL)info.pool;d->Size=info.length;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE Lock(UINT off,UINT size,void** pp,DWORD flags) override{return lockBuffer(*this,buf,off,size,pp,flags);}
    HRESULT STDMETHODCALLTYPE Unlock() override{return unlockBuffer(*this,buf);}
};

// Bytecode length in DWORDs up to and including the 0x0000FFFF end token (comment blocks skipped by their length).
inline std::size_t shaderTokens(const DWORD* code){
    if(!code)return 0;std::size_t i=1;
    for(;i<(1u<<20);){const DWORD t=code[i];
        if((t&0xFFFF)==0xFFFE){i+=1+((t>>16)&0x7FFF);continue;}
        if(t==0x0000FFFF)return i+1;
        i+=1+((t>>24)&0xF);}
    return 0;
}
struct StreamVertexShader final:IDirect3DVertexShader9,ProxyBase {
    std::vector<DWORD> code;unsigned tags=0;std::uint64_t hash=0;
    explicit StreamVertexShader(StreamCore* c):ProxyBase(c,Kind::VertexShader){ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DVertexShader9*>(this));}
    ~StreamVertexShader(){liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_VERTEXSHADER_METHODS
    NL_PROXY_COM(IDirect3DVertexShader9)
    HRESULT local(CmdTag<Cmd::VertexShader_GetDevice>,IDirect3DDevice9** pp){return devGet(pp);}
    HRESULT local(CmdTag<Cmd::VertexShader_GetFunction>,void* data,UINT* size){
        if(!size)return D3DERR_INVALIDCALL;const UINT bytes=UINT(code.size()*4);
        if(!data){*size=bytes;return D3D_OK;}if(*size<bytes)return D3DERR_MOREDATA;std::memcpy(data,code.data(),bytes);*size=bytes;return D3D_OK;}
};
struct StreamPixelShader final:IDirect3DPixelShader9,ProxyBase {
    std::vector<DWORD> code;
    explicit StreamPixelShader(StreamCore* c):ProxyBase(c,Kind::PixelShader){ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DPixelShader9*>(this));}
    ~StreamPixelShader(){liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_PIXELSHADER_METHODS
    NL_PROXY_COM(IDirect3DPixelShader9)
    HRESULT local(CmdTag<Cmd::PixelShader_GetDevice>,IDirect3DDevice9** pp){return devGet(pp);}
    HRESULT local(CmdTag<Cmd::PixelShader_GetFunction>,void* data,UINT* size){
        if(!size)return D3DERR_INVALIDCALL;const UINT bytes=UINT(code.size()*4);
        if(!data){*size=bytes;return D3D_OK;}if(*size<bytes)return D3DERR_MOREDATA;std::memcpy(data,code.data(),bytes);*size=bytes;return D3D_OK;}
};
struct StreamVertexDeclaration final:IDirect3DVertexDeclaration9,ProxyBase {
    std::vector<D3DVERTEXELEMENT9> elements;   // including the end element
    explicit StreamVertexDeclaration(StreamCore* c):ProxyBase(c,Kind::VertexDeclaration){ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DVertexDeclaration9*>(this));}
    ~StreamVertexDeclaration(){liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_VERTEXDECLARATION_METHODS
    NL_PROXY_COM(IDirect3DVertexDeclaration9)
    HRESULT local(CmdTag<Cmd::VertexDeclaration_GetDevice>,IDirect3DDevice9** pp){return devGet(pp);}
    HRESULT local(CmdTag<Cmd::VertexDeclaration_GetDeclaration>,D3DVERTEXELEMENT9* out,UINT* count){
        if(!count)return D3DERR_INVALIDCALL;if(out)std::memcpy(out,elements.data(),elements.size()*sizeof(D3DVERTEXELEMENT9));*count=UINT(elements.size());return D3D_OK;}
};
struct StreamStateBlock final:IDirect3DStateBlock9,ProxyBase {
    explicit StreamStateBlock(StreamCore* c):ProxyBase(c,Kind::StateBlock){ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DStateBlock9*>(this));}
    ~StreamStateBlock(){liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_STATEBLOCK_METHODS
    NL_PROXY_COM(IDirect3DStateBlock9)
    HRESULT local(CmdTag<Cmd::StateBlock_GetDevice>,IDirect3DDevice9** pp){return devGet(pp);}
    std::function<void(ProxyBase&,bool)> hook;   // set by the StreamDevice: Apply (true) invalidates StreamState
    void observe(CmdTag<Cmd::StateBlock_Apply>){if(hook)hook(*this,true);}
    using ProxyBase::observe;
};
struct StreamQuery final:IDirect3DQuery9,ProxyBase {
    unsigned type;unsigned dataSize;
    std::atomic<std::uint32_t> gen{0},readyGen{0};   // gen: END issues by the game; readyGen: the replay's polled result belongs to it
    unsigned char result[64]{};DWORD resultSize=0;std::uint32_t issueSeen=0;   // issueSeen: END issues the replay thread ran
    StreamQuery(StreamCore* c,unsigned t):ProxyBase(c,Kind::Query),type(t),dataSize(queryDataSize(t)){info.type=t;ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DQuery9*>(this));}
    ~StreamQuery(){liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_QUERY_METHODS
    NL_PROXY_COM(IDirect3DQuery9)
    HRESULT local(CmdTag<Cmd::Query_GetDevice>,IDirect3DDevice9** pp){return devGet(pp);}
    D3DQUERYTYPE local(CmdTag<Cmd::Query_GetType>){return (D3DQUERYTYPE)type;}
    DWORD local(CmdTag<Cmd::Query_GetDataSize>){return dataSize;}
    void observe(CmdTag<Cmd::Query_Issue>,DWORD flags){if(type!=8)core->frameHazards|=StreamCore::kHazardQuery;if(flags&D3::kIssueEnd)gen.fetch_add(1);}   // 0.3.200 (frame skip): only an event query's frame may be skipped (its Issue still runs)
    using ProxyBase::observe;
    HRESULT STDMETHODCALLTYPE GetData(void* data,DWORD size,DWORD flags) override{
        Queue& q=core->q;
        if(dead.load())return D3DERR_INVALIDCALL;   // the real create failed: an error, not S_FALSE forever
        if((flags&D3::kGetDataFlush)||!gen.load()){   // FLUSH, or a query never issued: the real GetData with the game's buffer, after everything recorded so far
            HRESULT hr=D3DERR_INVALIDCALL;
            if(!runTask(*core,[&](StreamCore&){if(inner)hr=static_cast<IDirect3DQuery9*>(inner)->GetData(data,size,flags);},Cmd::SyncGetData))return D3DERR_INVALIDCALL;   // e.g. WoW's event-query wait: shows as SyncGetData in the census
            return hr;
        }
        q.publish();   // the replay thread must see the Issue this GetData polls for, or a spin on S_FALSE would never end
        const std::uint32_t g=gen.load();
        if(g&&readyGen.load(std::memory_order_acquire)==g){
            if(data&&size)std::memcpy(data,result,size<resultSize?size:resultSize);
            return D3D_OK;}
        return S_FALSE;}
};
struct StreamSwapChain final:IDirect3DSwapChain9,ProxyBase {
    D3DPRESENT_PARAMETERS pp{};UINT index=0;
    explicit StreamSwapChain(StreamCore* c):ProxyBase(c,Kind::SwapChain){ProxyInit<ProxyBase>::apply(this,static_cast<IDirect3DSwapChain9*>(this));}
    ~StreamSwapChain(){liveProxyObjects.fetch_sub(1);}
    NORTHLIGHT_STREAM_SWAPCHAIN_METHODS
    NL_PROXY_COM(IDirect3DSwapChain9)
    HRESULT local(CmdTag<Cmd::SwapChain_GetDevice>,IDirect3DDevice9** out){return devGet(out);}
    HRESULT local(CmdTag<Cmd::SwapChain_GetPresentParameters>,D3DPRESENT_PARAMETERS* out){if(!out)return D3DERR_INVALIDCALL;*out=pp;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE Present(const RECT* src,const RECT* dst,HWND window,const RGNDATA* dirty,DWORD flags) override;   // stream_device.h
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT index,D3DBACKBUFFER_TYPE type,IDirect3DSurface9** out) override;
};

// A game-held interface pointer to its proxy without a call or an atomic: compare the object's vtable pointer with the known proxy classes
// (mirror_resources.h does the same for its wrappers) and cast by kind. Anything else (a foreign object) takes the QueryInterface path.
inline ProxyBase* ProxyBase::of(IUnknown* u){
    if(!u)return nullptr;
    const void* vp=*reinterpret_cast<const void* const*>(u);
    for(unsigned k=0;k<13;++k){
        if(vp!=proxyShape[k].load(std::memory_order_relaxed))continue;
        switch(Kind(k)){
        case Kind::Surface:return static_cast<StreamSurface*>(static_cast<IDirect3DSurface9*>(u));
        case Kind::Texture:return static_cast<StreamTexture*>(static_cast<IDirect3DTexture9*>(u));
        case Kind::CubeTexture:return static_cast<StreamCubeTexture*>(static_cast<IDirect3DCubeTexture9*>(u));
        case Kind::VolumeTexture:return static_cast<StreamVolumeTexture*>(static_cast<IDirect3DVolumeTexture9*>(u));
        case Kind::Volume:return static_cast<StreamVolume*>(static_cast<IDirect3DVolume9*>(u));
        case Kind::VertexBuffer:return static_cast<StreamVertexBuffer*>(static_cast<IDirect3DVertexBuffer9*>(u));
        case Kind::IndexBuffer:return static_cast<StreamIndexBuffer*>(static_cast<IDirect3DIndexBuffer9*>(u));
        case Kind::VertexShader:return static_cast<StreamVertexShader*>(static_cast<IDirect3DVertexShader9*>(u));
        case Kind::PixelShader:return static_cast<StreamPixelShader*>(static_cast<IDirect3DPixelShader9*>(u));
        case Kind::VertexDeclaration:return static_cast<StreamVertexDeclaration*>(static_cast<IDirect3DVertexDeclaration9*>(u));
        case Kind::StateBlock:return static_cast<StreamStateBlock*>(static_cast<IDirect3DStateBlock9*>(u));
        case Kind::Query:return static_cast<StreamQuery*>(static_cast<IDirect3DQuery9*>(u));
        case Kind::SwapChain:return static_cast<StreamSwapChain*>(static_cast<IDirect3DSwapChain9*>(u));
        }
    }
    return ofSlow(u);
}
}
