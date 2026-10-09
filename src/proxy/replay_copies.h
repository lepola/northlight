#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <unordered_map>
#include <vector>
#include "lock_meter.h"
#include "unlock_source.h"
/* 0.3.192 (CS): REPLAY-side CPU copies of game buffers. With the command stream active, geometry capture (draw_snapshot.h,
   geometry_capture.h, terrain_capture_bounds.h) reads these bytes instead of locking a DXVK buffer READONLY, so it no longer depends on how a
   DXVK version treats a read lock (3.x marks the range dirty, then Unlock stages + GPU-copies it; see upload_lock.h readBackLock()).

   Position: a copy equals the real buffer AT THE REPLAY POSITION, never the game thread's (the game is up to StreamFramesAhead frames ahead and has already
   overwritten ring ranges). It is kept current in stream order at ONE choke point: every write the stream replays into a game buffer goes
   through the tracked wrapper NorthlightTrackedBuffers::Buffer on the replay thread (Device::CreateVertexBuffer/CreateIndexBuffer wrap, the
   stream's inner object IS the wrapper): UnlockBuffer payloads (Lock, memcpy, Unlock), first-lock/DISCARD staging (same command), and
   pass-through locks (Lock .. game writes .. Unlock). At a write Lock the wrapper remembers (offset, size, pointer); at Unlock, before it
   forwards, it copies that range out of the still-mapped pointer, or, for the stream's own UnlockBuffer replay, out of the command's source bytes
   (UnlockSourceScope, unlock_source.h: the mapped pointer may be uncached write-combined memory). ProcessVertices (the only GPU-side writer of a buffer) calls written(): the
   copy is dropped for good. Device Reset (invalidateAll) drops every copy. Nothing else writes a game buffer.
   While a write lock is outstanding, a nested lock, or any lock, the copy is not served (the capture falls back to its lock).
   DISCARD: the new contents outside the written range are undefined in D3D9; the copy keeps the old bytes there. A real lock would return
   arbitrary bytes for them, so a capture or revalidate compare that read them would be reading undefined data either way (the revision
   changes with the DISCARD, so no cache keyed on it survives).

   Lookup: the capture gets its buffers from ext->GetStreamSource/GetIndices, which return the RAW DXVK buffer (Device::SetStreamSource/SetIndices unwrap
   before forwarding), not the wrapper. attach() therefore registers a Slot under BOTH the raw and the exposed pointer; read() accepts either.
   Only the buffers the capture actually reads get a copy: the Reader (replay_copy_reader.h) fills one with ONE whole-buffer read of the real
   buffer (the readBackLock() path) on a read of a buffer <= kEagerBytes or a whole-buffer request, else once the buffer was read in 2 distinct recent
   frames (a whole-buffer read of a big static buffer to serve one small range would cost more than it saves), then it stays current from the replayed writes.
   32-bit address space: a hard cap (kCapBytes = 16 MiB; the LOCK METER read-back volume is ~0.5-1.7 MiB/frame, and the game's whole VB/IB working set
   that capture touches is a fraction of it) with LRU eviction by last capture read; halved under memory pressure (setPressure, from the memory guard's
   per-frame decision); a buffer above cap/4 never gets a copy. Thrash guard in FRAMES (advanceFrame(), once per frame from WorldRenderer::endFrame; a frame
   makes hundreds of reads, so reads are no time base): an eviction or invalidation clears the buffer's read-frame count (a big buffer needs 2 more frames
   of reads before it refills); a refill within kThrashFrames of that is a strike; after kMaxStrikes strikes in a row the buffer stays on the fallback for kBackoffFrames.
   LARGE allowance: a buffer above cap/4 (the game's ~15.8 MB DYNAMIC buffer) may hold a copy of up to kLargeCopyBytes (16 MiB) in a SEPARATE budget of
   kLargeCapBytes (one such buffer; outside kCapBytes, never counted in `used`), only without memory pressure. It fills only after reads in 2 distinct recent
   frames (a whole-buffer request does not shortcut that), not within kLargeRefillFrames of its own eviction/invalidation, and not within kLargeBackoffFrames
   after a pressure edge (the fill is one 16 MB read on the replay thread: it must not repeat). Pressure drops it at once, or at its unpin when pinned
   (never freed while pinned); another large buffer takes the allowance only when the holder is unpinned and unread for kLargeRefillFrames.
   0.3.204: deliberately still one holder of at most 16 MiB here, unlike the command stream's two-buffer CPU shadow allowance (command_queue.h,
   LargeShadowBudgetBytes): a larger buffer (e.g. an 18 MB one) never gets a replay-side copy and its reads keep taking the readBackLock path.
   A buffer without a valid copy takes today's readBackLock() path.
   Gate: `enabled` (set where the stream starts, with CommandStream=0 it stays false): no wrapper hook copies anything, no Reader changes a call.
   Threads: every writer and every capture site runs on the replay thread; the registry is still mutex-guarded (the wrapper can be released
   elsewhere). A pinned copy (a Reader between lock() and unlock()) is never evicted or freed; a write cannot reach it meanwhile (same thread).
   LIFETIME: the Slot lives inside the wrapper's Record (Buffer::record), freed by ~Buffer. A pin therefore holds a COM reference on the wrapper
   (Slot::owner, AddRef at pin, Release at unpin AFTER the store mutex is dropped, since the last Release runs ~Buffer -> detach() -> that mutex), so
   the wrapper and the Slot outlive every pin and the slot is never touched after its Release. */
namespace NorthlightReplayCopies {
inline constexpr std::uint64_t kCapBytes=16u<<20;
inline constexpr UINT kEagerBytes=64u<<10;
inline constexpr unsigned kMaxStrikes=3;                 // refills within kThrashFrames of an eviction/invalidation before the buffer is left on the fallback
inline constexpr std::uint64_t kLargeCapBytes=16u<<20,kLargeCopyBytes=16u<<20;
inline constexpr std::uint64_t kLargeRefillFrames=60,kLargeBackoffFrames=600;
inline constexpr std::uint64_t kThrashFrames=4,kBackoffFrames=600,kRecentFrames=8;   // frames (advanceFrame) = the time base; kRecentFrames: gap that restarts a buffer's read-frame count
inline std::atomic<bool> enabled{false};

struct Slot {
    // immutable after attach
    bool attached=false;const void* raw=nullptr;const void* exposed=nullptr;void* owner=nullptr;void(*ref)(void*,bool)=nullptr;UINT size=0;const std::atomic<unsigned>* locks=nullptr;   // locks: the wrapper's outstanding lock count; owner: the wrapper (pins hold a reference)
    // guarded by the store mutex (state is also read lock-free by the wrapper hooks)
    enum State:unsigned char{None,Filling,Valid};
    std::atomic<unsigned char> state{None};
    std::vector<unsigned char> data;
    bool gpuWritten=false,evicted=false,large=false,dropAtUnpin=false;unsigned strikes=0,frames=0,pins=0,listAt=0;std::uint64_t used=0,lastFrame=0,evictedFrame=0,parkUntil=0;   // frames: distinct recent frames with a capture read
};
// 0.3.196 (task 12): the outstanding write lock of ONE wrapper (a member of the tracked Buffer, no mutex): noteWrite() fills it at Lock, commitWrite() consumes it at Unlock
// under ONE store mutex acquisition. `drop`: the lock cannot be mirrored exactly (nested, null pointer, offset past the end): the copy is dropped at Unlock.
struct PendingWrite {bool active=false,drop=false;UINT off=0,end=0;const unsigned char* ptr=nullptr;};
struct Store {
    std::mutex m;std::unordered_map<const void*,Slot*> registry;std::vector<Slot*> list;   // list: Filling and Valid slots
    std::uint64_t used=0,clock=0,frame=1,cap=kCapBytes,largeUsed=0,largeBlockUntil=0;bool pressure=false;   // largeUsed: the large allowance's bytes (not in `used`)
    // 0.3.196 (task 12): the data vector of a slot evicted INSIDE beginFill, held only until that same call sizes the new fill (then freed): at most one vector, never kept across calls.
    std::vector<unsigned char> spare;bool recycling=false;
};
inline Store& store(){static Store s;return s;}
inline void pinRef(Slot& c){++c.pins;if(c.owner)c.ref(c.owner,true);}   // a pin holds the wrapper alive (see LIFETIME); store mutex held
namespace detail {
namespace M=NorthlightLockMeter;
inline void gauges(Store& s){auto& m=M::state();m.copyResidentBytes.store(s.used,std::memory_order_relaxed);m.copyResidentBuffers.store(s.list.size(),std::memory_order_relaxed);m.copyCapBytes.store(s.cap,std::memory_order_relaxed);m.copyLargeBytes.store(s.largeUsed,std::memory_order_relaxed);}
enum class Why{Invalidate,Evict,Silent};
// Takes the slot out of the byte budget and the list; the bytes are freed unless a Reader still pins them (unpin frees then).
inline void drop(Store& s,Slot& c,Why why){
    if(c.state.load()==Slot::None)return;
    const std::uint64_t n=c.size;Slot* last=s.list.back();s.list[c.listAt]=last;last->listAt=c.listAt;s.list.pop_back();
    if(c.large){s.largeUsed-=n;M::state().copyLargeDrops.fetch_add(1,std::memory_order_relaxed);c.large=false;}else s.used-=n;
    c.dropAtUnpin=false;c.state.store(Slot::None);
    if(why!=Why::Silent){c.evicted=true;c.evictedFrame=s.frame;c.frames=0;}   // a refill needs 2 fresh frames of reads, and counts as a strike when it comes soon
    if(!c.pins){
        if(why==Why::Evict&&s.recycling&&c.data.capacity()>s.spare.capacity())s.spare.swap(c.data);   // beginFill's own eviction: its fill may take this allocation over
        std::vector<unsigned char>().swap(c.data);}
    auto& m=M::state();if(why==Why::Evict)m.copyEvictions.fetch_add(1,std::memory_order_relaxed);else if(why==Why::Invalidate)m.copyInvalidations.fetch_add(1,std::memory_order_relaxed);
    gauges(s);
}
inline bool evictOne(Store& s){
    Slot* victim=nullptr;
    for(Slot* c:s.list)if(!c->large&&c->state.load()==Slot::Valid&&!c->pins&&(!victim||c->used<victim->used))victim=c;
    if(!victim)return false;
    drop(s,*victim,Why::Evict);return true;
}
// The large allowance is free for `c`: nobody holds it, or the holder is unpinned, Valid and unread for kLargeRefillFrames (then it is dropped).
inline bool freeLarge(Store& s,const Slot& c){
    if(s.largeUsed+c.size<=kLargeCapBytes)return true;
    Slot* victim=nullptr;
    for(Slot* o:s.list)if(o->large&&o!=&c&&o->state.load()==Slot::Valid&&!o->pins&&s.frame>=o->lastFrame+kLargeRefillFrames)victim=o;
    if(!victim)return false;
    drop(s,*victim,Why::Evict);return s.largeUsed+c.size<=kLargeCapBytes;
}
}
// ---- wrapper side (tracked_buffers.h, replay thread) ----
template<class O> void attach(Slot& c,const void* raw,const void* exposed,O* owner,UINT size,const std::atomic<unsigned>* locks){   // owner: the wrapper, AddRef/Release by pins
    if(!size)return;
    Store& s=store();std::lock_guard<std::mutex> g(s.m);
    try{s.registry[raw]=&c;s.registry[exposed]=&c;}catch(...){s.registry.erase(raw);s.registry.erase(exposed);return;}   // raw: what ext->GetStreamSource/GetIndices return
    c.attached=true;c.raw=raw;c.exposed=exposed;c.owner=owner;c.ref=[](void* o,bool add){if(add)static_cast<O*>(o)->AddRef();else static_cast<O*>(o)->Release();};c.size=size;c.locks=locks;
}
inline void detach(Slot& c){
    if(!c.attached)return;
    Store& s=store();std::lock_guard<std::mutex> g(s.m);
    detail::drop(s,c,detail::Why::Silent);s.registry.erase(c.raw);s.registry.erase(c.exposed);c.attached=false;
}
// A successful write Lock (not READONLY), no mutex (0.3.196, task 12: the Lock/Unlock pair used to take the store mutex twice). before = locks already outstanding on the buffer.
// Only records what Unlock needs; a nested write, an offset past the end or a null pointer cannot be mirrored exactly: commitWrite drops the copy. Size 0 = to the end, a
// range past the end is clamped (as the stream does). A slot that is not Valid now cannot become Valid before the Unlock (beginFill refuses while locks>0), so nothing is recorded.
inline void writeLocked(const Slot& c,PendingWrite& w,unsigned before,UINT off,UINT size,void* ptr){
    if(c.state.load()!=Slot::Valid)return;
    if(w.active||before||!ptr||off>c.size){w.active=true;w.drop=true;return;}   // a pending write of this wrapper means a nested one, as does before
    std::uint64_t end=size?std::uint64_t(off)+size:std::uint64_t(c.size);if(end>c.size)end=c.size;
    w.active=true;w.drop=false;w.off=off;w.end=UINT(end);w.ptr=static_cast<const unsigned char*>(ptr);
}
// A rejected write Lock, a failed Unlock, a GPU write: the copy no longer mirrors the buffer.
inline void invalidate(Slot& c,bool gpuWrite=false){
    if(!c.attached||(!gpuWrite&&c.state.load()==Slot::None))return;
    Store& s=store();std::lock_guard<std::mutex> g(s.m);
    if(gpuWrite)c.gpuWritten=true;   // never copied again
    detail::drop(s,c,detail::Why::Invalidate);
}
// Before the wrapper forwards Unlock (the pointer is still mapped): copy the written range into the copy. before = locks outstanding (this one included). ONE mutex acquisition;
// the slot may have been evicted or invalidated since the Lock (evictOne, setPressure, a failed nested Lock): then only the record is cleared.
inline void beforeUnlock(Slot& c,PendingWrite& w,unsigned before){
    if(!w.active)return;   // a READONLY lock's Unlock: nothing was written
    const PendingWrite p=w;w=PendingWrite{};
    if(c.state.load()!=Slot::Valid)return;
    Store& s=store();std::lock_guard<std::mutex> g(s.m);
    if(c.state.load()!=Slot::Valid)return;
    if(p.drop||before>1){detail::drop(s,c,detail::Why::Invalidate);return;}   // which lock does this Unlock close? unknown: drop
    if(p.end>p.off){   // the stream's own replayed write names its source bytes (cached memory, see unlock_source.h); a pass-through lock is read back from the mapped pointer
        const UnlockSource& u=unlockSource;
        const unsigned char* from=u.bytes&&u.off==p.off&&u.size==p.end-p.off?u.bytes:p.ptr;
        std::memcpy(c.data.data()+p.off,from,p.end-p.off);}
}
// ---- reader side (replay_copy_reader.h) ----
// Once per frame (replay thread, WorldRenderer::endFrame): the time base of the thrash guard and of the deferred fill.
inline void advanceFrame(){Store& s=store();std::lock_guard<std::mutex> g(s.m);++s.frame;}
namespace detail {
inline void touch(Store& s,Slot& c){   // one capture read in this frame
    if(c.frames&&c.lastFrame==s.frame)return;
    c.frames=(c.frames&&s.frame-c.lastFrame<=kRecentFrames)?c.frames+1:1;c.lastFrame=s.frame;
}
}
// The bytes at [off,off+size) of the buffer at its replay position, pinned until unpin(); null when no valid copy (slot is set when the buffer
// is tracked at all, to let the caller try a fill). key: the raw or the exposed pointer. Not served while any lock is outstanding (a pending write implies one).
inline const unsigned char* read(const void* key,UINT off,UINT size,Slot*& slot){
    slot=nullptr;Store& s=store();std::lock_guard<std::mutex> g(s.m);
    auto it=s.registry.find(key);if(it==s.registry.end())return nullptr;
    Slot& c=*it->second;slot=&c;detail::touch(s,c);
    if(c.state.load()!=Slot::Valid||(c.locks&&c.locks->load())||!size||std::uint64_t(off)+size>c.size)return nullptr;
    pinRef(c);c.used=++s.clock;return c.data.data()+off;
}
// Drops one pin; the bytes go if the slot was dropped meanwhile. The wrapper reference is released last, outside the mutex (it may be the final one).
inline void unpin(Slot& c){
    void* const owner=c.owner;void(*const ref)(void*,bool)=c.ref;
    {Store& s=store();std::lock_guard<std::mutex> g(s.m);
     if(c.pins)--c.pins;
     if(!c.pins&&c.dropAtUnpin&&c.large&&c.state.load()!=Slot::None)detail::drop(s,c,detail::Why::Evict);   // pressure began while it was pinned
     if(!c.pins&&c.state.load()==Slot::None)std::vector<unsigned char>().swap(c.data);}
    if(owner)ref(owner,false);   // c may be freed from here on
}
// Reserves room for a whole-buffer copy of c (evicting LRU entries) and pins it; false = stay on the fallback. Store mutex held.
// 0.3.196 (task 12): a slot evicted to make room hands its allocation to this fill when it fits tightly (capacity within 1/8 above the size: the slack is the only memory not charged to
// the budget, <= 12.5% of the slot, and only until the slot is dropped); anything else, and every leftover, is freed before returning.
inline bool beginFillLocked(Store& s,Slot& c,UINT requested){
    auto& m=NorthlightLockMeter::state();
    const bool large=c.size>s.cap/4;   // above the regular per-buffer limit: the large allowance or nothing
    const bool wanted=large?c.frames>=2:(c.size<=kEagerBytes||requested>=c.size||c.frames>=2);   // a big buffer read for a small range: only once it proved to be read in 2 recent frames
    if(c.state.load()!=Slot::None||c.pins||!wanted)return false;
    if(s.frame<c.parkUntil||c.gpuWritten||(c.locks&&c.locks->load())){m.copyRefused.fetch_add(1,std::memory_order_relaxed);return false;}
    if(large&&(c.size>kLargeCopyBytes||s.pressure||s.frame<s.largeBlockUntil||(c.evicted&&s.frame-c.evictedFrame<kLargeRefillFrames)||!detail::freeLarge(s,c))){m.copyRefused.fetch_add(1,std::memory_order_relaxed);return false;}
    const bool recent=c.evicted&&s.frame-c.evictedFrame<=kThrashFrames;
    if(recent&&c.strikes>=kMaxStrikes){c.parkUntil=s.frame+kBackoffFrames;c.strikes=0;c.evicted=false;m.copyRefused.fetch_add(1,std::memory_order_relaxed);return false;}   // thrash guard: parked
    struct SpareScope {Store& s;explicit SpareScope(Store& st):s(st){s.recycling=true;}~SpareScope(){s.recycling=false;std::vector<unsigned char>().swap(s.spare);}} spare(s);
    if(!large)while(s.used+c.size>s.cap)if(!detail::evictOne(s)){m.copyRefused.fetch_add(1,std::memory_order_relaxed);return false;}
    try{
        if(s.spare.capacity()>=c.size&&s.spare.capacity()-c.size<=std::size_t(c.size)/8)c.data.swap(s.spare);   // evicted a moment ago: same allocation, no malloc/free pair
        c.data.assign(c.size,0);s.list.push_back(&c);
    }catch(...){std::vector<unsigned char>().swap(c.data);m.copyRefused.fetch_add(1,std::memory_order_relaxed);return false;}
    c.listAt=unsigned(s.list.size()-1);c.state.store(Slot::Filling);c.used=++s.clock;pinRef(c);c.dropAtUnpin=false;
    if(large){c.large=true;s.largeUsed+=c.size;m.copyLargeGrants.fetch_add(1,std::memory_order_relaxed);}else s.used+=c.size;
    if(recent)++c.strikes;else c.strikes=0;detail::gauges(s);
    return true;
}
inline bool beginFill(Slot& c,UINT requested){Store& s=store();std::lock_guard<std::mutex> g(s.m);return beginFillLocked(s,c,requested);}
// read() plus, on a miss for a tracked buffer whose range fits, beginFill(), in ONE mutex acquisition (0.3.196, task 12: a cache miss used to take it twice). Returns the pinned bytes of a hit;
// else null, with `fill` true when the slot is now Filling and pinned (the caller reads the buffer into slot->data, then endFill()s) and `slot` set when the buffer is tracked at all.
inline const unsigned char* readOrBeginFill(const void* key,UINT off,UINT size,Slot*& slot,bool& fill){
    slot=nullptr;fill=false;Store& s=store();std::lock_guard<std::mutex> g(s.m);
    auto it=s.registry.find(key);if(it==s.registry.end())return nullptr;
    Slot& c=*it->second;slot=&c;detail::touch(s,c);
    if(c.state.load()==Slot::Valid&&!(c.locks&&c.locks->load())&&size&&std::uint64_t(off)+size<=c.size){pinRef(c);c.used=++s.clock;return c.data.data()+off;}
    if(size&&std::uint64_t(off)+size<=c.size)fill=beginFillLocked(s,c,size);
    return nullptr;
}
// The fill is done (data written by the caller while the slot was Filling and pinned). ok keeps the pin (the caller reads, then unpin()s).
inline void endFill(Slot& c,bool ok){
    void* const owner=c.owner;void(*const ref)(void*,bool)=c.ref;
    {Store& s=store();std::lock_guard<std::mutex> g(s.m);auto& m=NorthlightLockMeter::state();
     if(ok&&c.state.load()==Slot::Filling){c.state.store(Slot::Valid);m.copyFills.fetch_add(1,std::memory_order_relaxed);m.copyFillBytes.fetch_add(c.size,std::memory_order_relaxed);return;}
     detail::drop(s,c,detail::Why::Silent);if(c.pins)--c.pins;
     if(!c.pins)std::vector<unsigned char>().swap(c.data);}
    if(owner)ref(owner,false);   // the fill's pin; c may be freed from here on
}
// The memory guard's decision (replay thread, once per frame): halves the cap and evicts down to it; unpinned copies only.
inline void setPressure(bool on){
    Store& s=store();std::lock_guard<std::mutex> g(s.m);
    s.cap=on?kCapBytes/2:kCapBytes;
    if(on!=s.pressure){s.pressure=on;s.largeBlockUntil=s.frame+kLargeBackoffFrames;}   // either edge: no large grant for kLargeBackoffFrames
    if(on){std::vector<Slot*> big;for(Slot* c:s.list)if(c->large)big.push_back(c);   // (drop() edits the list)
           for(Slot* c:big){if(c->pins)c->dropAtUnpin=true;else detail::drop(s,*c,detail::Why::Evict);}}
    while(s.used>s.cap&&detail::evictOne(s)){}
    detail::gauges(s);
}
inline void served(UINT bytes){auto& m=NorthlightLockMeter::state();m.copyServed.fetch_add(1,std::memory_order_relaxed);m.copyServedBytes.fetch_add(bytes,std::memory_order_relaxed);}
inline void fallback(){NorthlightLockMeter::state().copyFallback.fetch_add(1,std::memory_order_relaxed);}
}
