#pragma once
// 0.3.192 (CS): storage and playback of the game-memory snapshot. The game thread records the game's own memory
// (camera, view stack, sky block, celestial identities) at trigger points; the replay thread serves
// NorthlightWorldContext::readSelf from the matching snapshot, so hooks that run later see what the game thread
// saw, not memory the game has already moved on from. Portable: the live read is a parameter, there is no Win32
// call except the image-header lookup. Included by world_context.h; game_snapshot.h adds capture and triggers.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

namespace NorthlightStream {
// Process-wide counters, flushed from per-snapshot counts so the per-read path touches no atomic.
struct SnapshotStats {
    static inline std::atomic<unsigned long long> hits{0},misses{0},triggers{0},overflow{0},unavailable{0},ageDraws{0},ageSamples{0};
};
// Code of the client image: reads there are signature / verify* statics, which may first run on the replay thread
// and must see live bytes (a snapshot never holds them; a hit would be a stale answer to a code check).
// The fixed-base 3.3.5a image has its code in [0x401000,0xA00000) (PE .text; the vtables, globals and the heap
// that every snapshot key points at all lie above 0xA00000), so that is the fallback and the bound when the
// section table is not readable. On Windows the executable sections of the main module refine it.
struct CodeRange {
    std::uintptr_t lo=0x400000,hi=0xA00000;
    bool contains(std::uintptr_t a)const{return a>=lo&&a<hi;}
};
// Executable sections from an in-memory PE32 image (headers only, bounds checked against `size`); false if it is
// not a PE or has no executable section. [base, end of the last executable section) is the range.
inline bool parseImageCode(const unsigned char* image,std::size_t size,std::uintptr_t base,CodeRange& out){
    auto u16=[&](std::size_t at){std::uint16_t v=0;std::memcpy(&v,image+at,2);return v;};
    auto u32=[&](std::size_t at){std::uint32_t v=0;std::memcpy(&v,image+at,4);return v;};
    if(!image||size<0x40||u16(0)!=0x5a4d)return false;
    const std::size_t nt=u32(0x3c);
    if(nt>size||size-nt<24||u32(nt)!=0x4550)return false;
    const std::size_t sections=u16(nt+6),optional=u16(nt+20),table=nt+24+optional;
    if(!sections||sections>96||table>size||size-table<sections*40)return false;
    std::uintptr_t end=0;
    for(std::size_t i=0;i<sections;++i){
        const std::size_t s=table+i*40;const std::uint32_t flags=u32(s+36),virtualSize=u32(s+8),rva=u32(s+12);
        if(!(flags&(0x20u|0x20000000u))||!virtualSize)continue;
        const std::uintptr_t top=base+rva+virtualSize;if(top>end)end=top;
    }
    if(!end||end<=base)return false;
    out.lo=base;out.hi=end;return true;
}
inline const CodeRange& codeRange(){
    static const CodeRange range=[]{
        CodeRange r;
#ifdef _WIN32
        const HMODULE module=GetModuleHandleW(nullptr);
        if(reinterpret_cast<std::uintptr_t>(module)==0x400000){CodeRange parsed;
            if(parseImageCode(reinterpret_cast<const unsigned char*>(module),0x1000,0x400000,parsed))r=parsed;}
#endif
        return r;
    }();
    return range;
}

// Fixed-capacity store: (addr,size) -> FIFO of byte copies in capture order. All storage is inside the object, so a
// pooled snapshot allocates nothing after construction. Overflow of any capacity makes it unusable (usable()==false):
// the replay then reads live, which is always correct, only not coherent with the game thread's moment.
class GameSnapshot {
public:
    static constexpr std::size_t ArenaBytes=64*1024,MaxKeys=64,MaxCopies=256;
    enum class Mode:std::uint8_t{Idle,Recording,Playing};
    enum class Serve:std::uint8_t{Miss,Ok,Failed};
    Mode mode=Mode::Idle;
    // Which trigger took it and the draw ordinal of that draw; the consumer's age is its own draw ordinal minus this.
    std::uint32_t triggerKind=0;std::uint64_t triggerDraw=0;
    unsigned long hits=0,misses=0; // this activation, flushed by ScopedPlayback
    void clear(){keyCount_=copyCount_=0;used_=0;overflowed_=false;mode=Mode::Idle;triggerKind=0;triggerDraw=0;hits=misses=0;}
    bool usable()const{return !overflowed_&&copyCount_>0;}
    bool overflowed()const{return overflowed_;}
    std::size_t keys()const{return keyCount_;}
    std::size_t copies()const{return copyCount_;}
    std::size_t bytes()const{return used_;}
    // Appends one read result (a failed read keeps no bytes and replays as a failure). False once full.
    bool record(std::uintptr_t address,const void* data,std::size_t size,bool ok){
        if(overflowed_)return false;
        if(size>ArenaBytes||(ok&&used_+size>ArenaBytes)||copyCount_>=MaxCopies){return overflow();}
        Key* key=find(address,size);
        if(!key){if(keyCount_>=MaxKeys)return overflow();
            key=&keys_[keyCount_++];key->address=address;key->size=std::uint32_t(size);key->head=key->tail=key->cursor=None;}
        const std::uint16_t index=std::uint16_t(copyCount_++);
        Copy& copy=copies_[index];copy.ok=ok;copy.next=None;copy.offset=std::uint32_t(used_);
        if(ok){std::memcpy(arena_+used_,data,size);used_+=size;}
        if(key->tail==None)key->head=key->cursor=index;else copies_[key->tail].next=index;
        key->tail=index;return true;
    }
    // Rewinds every FIFO: the next playback starts from the first copy of every key.
    void rewind(){for(std::size_t i=0;i<keyCount_;++i)keys_[i].cursor=keys_[i].head;hits=misses=0;}
    // The next copy for the key in capture order; the last copy once the FIFO is used up (a reader that reads more
    // often than the capture did gets the freshest value, never a different key's data).
    Serve serve(std::uintptr_t address,void* out,std::size_t size){
        Key* key=find(address,size);
        if(!key)return Serve::Miss;
        std::uint16_t index=key->cursor;
        if(index!=None)key->cursor=copies_[index].next;else index=key->tail;
        const Copy& copy=copies_[index];
        if(!copy.ok)return Serve::Failed;
        std::memcpy(out,arena_+copy.offset,size);return Serve::Ok;
    }
    bool has(std::uintptr_t address,std::size_t size)const{for(std::size_t i=0;i<keyCount_;++i)if(keys_[i].address==address&&keys_[i].size==size)return true;return false;}
private:
    static constexpr std::uint16_t None=0xffff;
    struct Key{std::uintptr_t address;std::uint32_t size;std::uint16_t head,tail,cursor;};
    struct Copy{std::uint32_t offset;std::uint16_t next;bool ok;};
    Key* find(std::uintptr_t address,std::size_t size){for(std::size_t i=0;i<keyCount_;++i)if(keys_[i].address==address&&keys_[i].size==size)return &keys_[i];return nullptr;}
    bool overflow(){overflowed_=true;SnapshotStats::overflow.fetch_add(1,std::memory_order_relaxed);return false;}
    Key keys_[MaxKeys];Copy copies_[MaxCopies];
    std::size_t keyCount_=0,copyCount_=0,used_=0;bool overflowed_=false;
    alignas(8) unsigned char arena_[ArenaBytes];
};

// The snapshot this thread reads through: set while capturing (game thread) or while a snapshot command replays
// (replay thread). Null everywhere else, and always on the direct path: readSelf then costs one null check.
inline thread_local GameSnapshot* activeSnapshot=nullptr;

// The whole readSelf policy, with the live read as a parameter. Code-range addresses are always live and never
// recorded. Recording: live read, then append. Playing: a hit is served from the snapshot, a miss reads live.
template<class Live> inline bool snapshotRead(GameSnapshot& s,std::uintptr_t address,void* out,std::size_t size,Live live,const CodeRange& range){
    if(range.contains(address)||s.mode==GameSnapshot::Mode::Idle)return live(address,out,size);
    if(s.mode==GameSnapshot::Mode::Recording){const bool ok=live(address,out,size);s.record(address,out,size,ok);return ok;}
    switch(s.serve(address,out,size)){
        case GameSnapshot::Serve::Ok:++s.hits;return true;
        case GameSnapshot::Serve::Failed:++s.hits;return false;
        default:++s.misses;return live(address,out,size);
    }
}

// Installs a snapshot for the calling thread; restores the previous one (normally null) at scope end.
class ScopedRecording {
    GameSnapshot* previous_;GameSnapshot& s_;
public:
    explicit ScopedRecording(GameSnapshot& s):previous_(activeSnapshot),s_(s){s.clear();s.mode=GameSnapshot::Mode::Recording;activeSnapshot=&s;}
    ~ScopedRecording(){s_.mode=GameSnapshot::Mode::Idle;activeSnapshot=previous_;}
    ScopedRecording(const ScopedRecording&)=delete;ScopedRecording& operator=(const ScopedRecording&)=delete;
};
class ScopedPlayback {
    GameSnapshot* previous_;GameSnapshot& s_;
public:
    explicit ScopedPlayback(GameSnapshot& s):previous_(activeSnapshot),s_(s){s.rewind();s.mode=GameSnapshot::Mode::Playing;activeSnapshot=&s;}
    ~ScopedPlayback(){
        s_.mode=GameSnapshot::Mode::Idle;activeSnapshot=previous_;
        SnapshotStats::hits.fetch_add(s_.hits,std::memory_order_relaxed);SnapshotStats::misses.fetch_add(s_.misses,std::memory_order_relaxed);
    }
    ScopedPlayback(const ScopedPlayback&)=delete;ScopedPlayback& operator=(const ScopedPlayback&)=delete;
};

// Pooled snapshots handed from the game thread to the replay thread: acquire() on the game thread, release() after
// the replay passed the snapshot command. Exhausted (cap) gives nullptr: that trigger is skipped and the replay
// reads live, so a slow replay costs coherence, never memory.
class SnapshotPool {
public:
    explicit SnapshotPool(std::size_t cap=48):cap_(cap){}
    GameSnapshot* acquire(){
        std::lock_guard<std::mutex> lock(mutex_);
        if(!free_.empty()){GameSnapshot* s=free_.back();free_.pop_back();s->clear();return s;}
        if(all_.size()>=cap_){SnapshotStats::unavailable.fetch_add(1,std::memory_order_relaxed);return nullptr;}
        try{all_.push_back(std::make_unique<GameSnapshot>());}catch(...){return nullptr;}
        all_.back()->clear();return all_.back().get();
    }
    void release(GameSnapshot* s){if(!s)return;std::lock_guard<std::mutex> lock(mutex_);try{free_.push_back(s);}catch(...){}}
    std::size_t allocated()const{std::lock_guard<std::mutex> lock(mutex_);return all_.size();}
    std::size_t idle()const{std::lock_guard<std::mutex> lock(mutex_);return free_.size();}
private:
    std::size_t cap_;mutable std::mutex mutex_;std::vector<std::unique_ptr<GameSnapshot>> all_;std::vector<GameSnapshot*> free_;
};
} // namespace NorthlightStream
