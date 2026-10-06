#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <unordered_map>
#include <vector>
#include "lock_meter.h"
/* 0.3.192 (CS): REPLAY-side CPU copies of game buffers. With the command stream active, geometry capture (draw_snapshot.h,
   geometry_capture.h, terrain_capture_bounds.h) reads these bytes instead of locking a DXVK buffer READONLY, so it no longer depends on how a
   DXVK version treats a read lock (3.x marks the range dirty, then Unlock stages + GPU-copies it; see upload_lock.h readBackLock()).

   Position: a copy equals the real buffer AT THE REPLAY POSITION, never the game thread's (the game is up to a frame ahead and has already
   overwritten ring ranges). It is kept current in stream order at ONE choke point: every write the stream replays into a game buffer goes
   through the tracked wrapper NorthlightTrackedBuffers::Buffer on the replay thread (Device::CreateVertexBuffer/CreateIndexBuffer wrap, the
   stream's inner object IS the wrapper): UnlockBuffer payloads (Lock, memcpy, Unlock), first-lock/DISCARD staging (same command), and
   pass-through locks (Lock .. game writes .. Unlock). At a write Lock the wrapper remembers (offset, size, pointer); at Unlock, before it
   forwards, it copies that range out of the still-mapped pointer. ProcessVertices (the only GPU-side writer of a buffer) calls written(): the
   copy is dropped for good. Device Reset (invalidateAll) drops every copy. Nothing else writes a game buffer.
   While a write lock is outstanding (pending), a nested lock, or any lock, the copy is not served (the capture falls back to its lock).
   DISCARD: the new contents outside the written range are undefined in D3D9; the copy keeps the old bytes there. A real lock would return
   arbitrary bytes for them, so a capture or revalidate compare that read them would be reading undefined data either way (the revision
   changes with the DISCARD, so no cache keyed on it survives).

   Only the buffers the capture actually reads get a copy: the Reader (replay_copy_reader.h) fills one with ONE whole-buffer read of the real
   buffer (the readBackLock() path) on the read that follows a read of the same buffer (at once for buffers <= kEagerBytes: a whole-buffer read of a
   big static buffer to serve one small range would cost more than it saves), then it stays current from the replayed writes.
   32-bit address space: a hard cap (kCapBytes = 16 MiB; the LOCK METER read-back volume is ~0.5-1.7 MiB/frame, and the game's whole VB/IB working set
   that capture touches is a fraction of it) with LRU eviction by last capture read; halved under memory pressure (setPressure, from the memory guard's
   per-frame decision); a buffer above cap/4 never gets a copy; a buffer evicted and refilled within kThrashReads reads kMaxStrikes times in a row (a working set larger than the cap) stays on the fallback for kBackoffReads reads (thrash guard).
   A buffer without a valid copy takes today's readBackLock() path.
   Gate: `enabled` (set where the stream starts, with CommandStream=0 it stays false): no wrapper hook copies anything, no Reader changes a call.
   Threads: every writer and every capture site runs on the replay thread; the registry is still mutex-guarded (the wrapper can be released
   elsewhere). A pinned copy (a Reader between lock() and unlock()) is never evicted or freed; a write cannot reach it meanwhile (same thread). */
namespace NorthlightReplayCopies {
inline constexpr std::uint64_t kCapBytes=16u<<20;
inline constexpr UINT kEagerBytes=64u<<10;
inline constexpr unsigned kMaxStrikes=3;                 // refills within kThrashReads reads of an eviction before the buffer is left on the fallback
inline constexpr std::uint64_t kThrashReads=64,kBackoffReads=8192;   // reads (any buffer) = the time base: a frame reads dozens to hundreds
inline std::atomic<bool> enabled{false};

struct Slot {
    // immutable after attach
    bool attached=false;const void* key=nullptr;UINT size=0;const std::atomic<unsigned>* locks=nullptr;   // locks: the wrapper's outstanding lock count
    // guarded by the store mutex (state is also read lock-free by the wrapper hooks)
    enum State:unsigned char{None,Filling,Valid};
    std::atomic<unsigned char> state{None};
    std::vector<unsigned char> data;
    bool gpuWritten=false,pending=false;unsigned strikes=0,reads=0,pins=0,listAt=0;std::uint64_t used=0,evictedAt=0;
    UINT pOff=0,pEnd=0;const unsigned char* pPtr=nullptr;   // the outstanding write lock: range and mapped pointer
};
struct Store {
    std::mutex m;std::unordered_map<const void*,Slot*> registry;std::vector<Slot*> list;   // list: Filling and Valid slots
    std::uint64_t used=0,clock=0,tick=0,cap=kCapBytes;
};
inline Store& store(){static Store s;return s;}
namespace detail {
namespace M=NorthlightLockMeter;
inline void gauges(Store& s){auto& m=M::state();m.copyResidentBytes.store(s.used,std::memory_order_relaxed);m.copyResidentBuffers.store(s.list.size(),std::memory_order_relaxed);m.copyCapBytes.store(s.cap,std::memory_order_relaxed);}
enum class Why{Invalidate,Evict,Silent};
// Takes the slot out of the byte budget and the list; the bytes are freed unless a Reader still pins them (unpin frees then).
inline void drop(Store& s,Slot& c,Why why){
    if(c.state.load()==Slot::None)return;
    const std::uint64_t n=c.size;Slot* last=s.list.back();s.list[c.listAt]=last;last->listAt=c.listAt;s.list.pop_back();
    s.used-=n;c.state.store(Slot::None);c.evictedAt=s.tick;c.pending=false;c.pPtr=nullptr;
    if(!c.pins)std::vector<unsigned char>().swap(c.data);
    auto& m=M::state();if(why==Why::Evict)m.copyEvictions.fetch_add(1,std::memory_order_relaxed);else if(why==Why::Invalidate)m.copyInvalidations.fetch_add(1,std::memory_order_relaxed);
    gauges(s);
}
inline bool evictOne(Store& s){
    Slot* victim=nullptr;
    for(Slot* c:s.list)if(c->state.load()==Slot::Valid&&!c->pins&&(!victim||c->used<victim->used))victim=c;
    if(!victim)return false;
    drop(s,*victim,Why::Evict);return true;
}
}
// ---- wrapper side (tracked_buffers.h, replay thread) ----
inline void attach(Slot& c,const void* key,UINT size,const std::atomic<unsigned>* locks){
    if(!size)return;
    Store& s=store();std::lock_guard<std::mutex> g(s.m);
    try{s.registry[key]=&c;}catch(...){return;}
    c.attached=true;c.key=key;c.size=size;c.locks=locks;
}
inline void detach(Slot& c){
    if(!c.attached)return;
    Store& s=store();std::lock_guard<std::mutex> g(s.m);
    detail::drop(s,c,detail::Why::Silent);s.registry.erase(c.key);c.attached=false;
}
// A successful write Lock (not READONLY). before = locks already outstanding on the buffer. A nested write, an offset past the end or a
// null pointer cannot be mirrored exactly: the copy is dropped. Size 0 = to the end, a range past the end is clamped (as the stream does).
inline void writeLocked(Slot& c,unsigned before,UINT off,UINT size,void* ptr){
    if(c.state.load()!=Slot::Valid)return;
    Store& s=store();std::lock_guard<std::mutex> g(s.m);
    if(c.state.load()!=Slot::Valid)return;
    if(before||c.pending||!ptr||off>c.size){detail::drop(s,c,detail::Why::Invalidate);return;}
    std::uint64_t end=size?std::uint64_t(off)+size:std::uint64_t(c.size);if(end>c.size)end=c.size;
    c.pending=true;c.pOff=off;c.pEnd=UINT(end);c.pPtr=static_cast<const unsigned char*>(ptr);
}
// A rejected write Lock, a failed Unlock, a GPU write: the copy no longer mirrors the buffer.
inline void invalidate(Slot& c,bool gpuWrite=false){
    if(!c.attached||(!gpuWrite&&c.state.load()==Slot::None))return;
    Store& s=store();std::lock_guard<std::mutex> g(s.m);
    if(gpuWrite)c.gpuWritten=true;   // never copied again
    detail::drop(s,c,detail::Why::Invalidate);
}
// Before the wrapper forwards Unlock (the pointer is still mapped): copy the written range into the copy. before = locks outstanding (this one included).
inline void beforeUnlock(Slot& c,unsigned before){
    if(c.state.load()!=Slot::Valid)return;
    Store& s=store();std::lock_guard<std::mutex> g(s.m);
    if(c.state.load()!=Slot::Valid||!c.pending)return;   // a READONLY lock's Unlock: nothing was written
    if(before>1){detail::drop(s,c,detail::Why::Invalidate);return;}   // which lock does this Unlock close? unknown: drop
    if(c.pEnd>c.pOff)std::memcpy(c.data.data()+c.pOff,c.pPtr,c.pEnd-c.pOff);
    c.pending=false;c.pPtr=nullptr;
}
// ---- reader side (replay_copy_reader.h) ----
// The bytes at [off,off+size) of the buffer at its replay position, pinned until unpin(); null when no valid copy (slot is set when the buffer
// is tracked at all, to let the caller try a fill). Not served while any lock is outstanding or a write is pending.
inline const unsigned char* read(const void* key,UINT off,UINT size,Slot*& slot){
    slot=nullptr;Store& s=store();std::lock_guard<std::mutex> g(s.m);++s.tick;
    auto it=s.registry.find(key);if(it==s.registry.end())return nullptr;
    Slot& c=*it->second;slot=&c;
    if(c.state.load()!=Slot::Valid||c.pending||(c.locks&&c.locks->load())||!size||std::uint64_t(off)+size>c.size){++c.reads;return nullptr;}
    ++c.pins;c.used=++s.clock;return c.data.data()+off;
}
inline void unpin(Slot& c){
    Store& s=store();std::lock_guard<std::mutex> g(s.m);
    if(c.pins)--c.pins;
    if(!c.pins&&c.state.load()==Slot::None)std::vector<unsigned char>().swap(c.data);
}
// Reserves room for a whole-buffer copy of c (evicting LRU entries) and pins it; false = stay on the fallback.
inline bool beginFill(Slot& c,UINT requested){
    Store& s=store();std::lock_guard<std::mutex> g(s.m);auto& m=NorthlightLockMeter::state();
    const bool wanted=c.size<=kEagerBytes||c.reads>0||requested>=c.size;
    if(c.state.load()!=Slot::None||c.pins||!wanted)return false;
    if(c.evictedAt&&c.strikes>=kMaxStrikes&&s.tick-c.evictedAt<kBackoffReads){m.copyRefused.fetch_add(1,std::memory_order_relaxed);return false;}
    if(c.gpuWritten||c.size>s.cap/4||(c.locks&&c.locks->load())){m.copyRefused.fetch_add(1,std::memory_order_relaxed);return false;}
    while(s.used+c.size>s.cap)if(!detail::evictOne(s)){m.copyRefused.fetch_add(1,std::memory_order_relaxed);return false;}
    try{c.data.assign(c.size,0);s.list.push_back(&c);}catch(...){std::vector<unsigned char>().swap(c.data);m.copyRefused.fetch_add(1,std::memory_order_relaxed);return false;}
    c.listAt=unsigned(s.list.size()-1);c.state.store(Slot::Filling);c.pending=false;c.used=++s.clock;++c.pins;s.used+=c.size;
    if(c.evictedAt&&s.tick-c.evictedAt<kThrashReads)++c.strikes;else c.strikes=0;detail::gauges(s);
    return true;
}
// The fill is done (data written by the caller while the slot was Filling and pinned). ok keeps the pin (the caller reads, then unpin()s).
inline void endFill(Slot& c,bool ok){
    Store& s=store();std::lock_guard<std::mutex> g(s.m);auto& m=NorthlightLockMeter::state();
    if(ok&&c.state.load()==Slot::Filling){c.state.store(Slot::Valid);m.copyFills.fetch_add(1,std::memory_order_relaxed);m.copyFillBytes.fetch_add(c.size,std::memory_order_relaxed);return;}
    detail::drop(s,c,detail::Why::Silent);if(c.pins)--c.pins;
    if(!c.pins)std::vector<unsigned char>().swap(c.data);
}
// The memory guard's decision (replay thread, once per frame): halves the cap and evicts down to it; unpinned copies only.
inline void setPressure(bool on){
    Store& s=store();std::lock_guard<std::mutex> g(s.m);
    s.cap=on?kCapBytes/2:kCapBytes;
    while(s.used>s.cap&&detail::evictOne(s)){}
    detail::gauges(s);
}
inline void served(UINT bytes){auto& m=NorthlightLockMeter::state();m.copyServed.fetch_add(1,std::memory_order_relaxed);m.copyServedBytes.fetch_add(bytes,std::memory_order_relaxed);}
inline void fallback(){NorthlightLockMeter::state().copyFallback.fetch_add(1,std::memory_order_relaxed);}
}
