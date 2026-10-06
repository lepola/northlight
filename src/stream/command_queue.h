#pragma once
// 0.3.192 (CS): the command stream's queue. A single-producer single-consumer arena of variable-length POD commands
// {u16 id, u16 flags, u32 size, payload}: the game thread records, the replay thread executes in order. Plain bytes, no
// D3D: the generated record/dispatch code (command_stream.inl) sits on top.
//
// Memory: 1 MiB chunks from a pool (no heap allocation on the hot path once warmed), plus pooled power-of-two Blocks for
// payloads above MaxInlinePayload. Everything counts against one budget (chunks in use + live blocks + registered
// shadow bytes); when a request would exceed it the producer publishes and waits until the consumer frees memory.
//
// Publishing: a committed command becomes visible to the consumer at publish(). commit() publishes by itself every 64
// commands or 64 KiB, and a chunk switch publishes the closed chunk. Callers publish explicitly at BeginScene/EndScene/
// Clear/SetRenderTarget, sync points, Present, and on every GetData / locally answered Get (no spin loop may starve).
//
// Threading: producer methods from one thread only (the game thread; rare foreign threads are serialized by the record
// gate above this layer); consumer methods from one thread only (the replay thread). Counters and pressure are any-thread.
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <thread>
#include <vector>
#include "stream_stats.h"
#include "stream_wait.h"

namespace NorthlightStream {
struct CommandHeader {std::uint16_t id;std::uint16_t flags;std::uint32_t size;};   // size includes the header; a multiple of 8
static_assert(sizeof(CommandHeader)==8,"header is 8 bytes");

constexpr std::size_t ChunkBytes=std::size_t(1)<<20;
constexpr std::size_t MaxInlinePayload=ChunkBytes/4;   // larger payloads travel in a Block
constexpr std::size_t BudgetBytes=std::size_t(48)<<20;
constexpr std::size_t TextureShadowBudgetBytes=std::size_t(32)<<20;   // per-level texture shadows; halved under pressure; never evicted
constexpr std::size_t ShadowBudgetBytes=std::size_t(24)<<20;   // CPU shadows of DYNAMIC buffers; halved under pressure; never evicted
constexpr std::uint32_t kAutoPublishCommands=64,kAutoPublishBytes=64u<<10;
constexpr std::uint32_t kNoPayload=0xFFFFFFFFu;   // a nullable pointer's offset in a generated Args struct

// Command flags. kFlagBlock: the first kBlockSlot payload bytes hold a Block*; retire() returns that Block to the pool.
constexpr std::uint16_t kFlagBlock=1;
constexpr std::size_t kBlockSlot=8;

// A pooled large payload. data() is 8-aligned; used is the payload byte count the producer stored.
struct Block {
    std::uint32_t capacity,cls,used,pad_;
    unsigned char* data(){return reinterpret_cast<unsigned char*>(this+1);}
};
static_assert(sizeof(Block)==16,"block header keeps data 8-aligned");

enum class WaitKind {Sync,Present,Backpressure};

class Queue {
    struct Chunk {
        std::atomic<Chunk*> next{nullptr};
        std::atomic<std::uint32_t> published{0};   // bytes of this chunk the consumer may read
        std::atomic<std::uint32_t> end{0};         // 0 while open; the final byte count once the producer moved on
        alignas(8) unsigned char data[ChunkBytes];
    };
    static constexpr unsigned kMinBlockShift=12,kMaxBlockShift=27,kBlockClasses=kMaxBlockShift-kMinBlockShift+1;
    static constexpr std::size_t kMaxPooledBlockBytes=std::size_t(16)<<20;

    // Producer-private.
    Chunk* wchunk_;std::uint32_t wpos_=0,pubPos_=0,sinceCmds_=0,sinceBytes_=0,openSize_=0;bool open_=false;
    // Consumer-private.
    Chunk* rchunk_;std::uint32_t rpos_=0;
    // Shared.
    std::atomic<std::uint64_t> recorded_{0},replayed_{0},waitSeq_{0};
    std::atomic<bool> sleeping_{false},interrupted_{false},bpWaiting_{false},pressure_{false};
    std::size_t budget_;
    Event consumerEv_{false},progress_{false};
    std::mutex pool_;std::vector<Chunk*> freeChunks_;std::vector<Block*> freeBlocks_[kBlockClasses];std::size_t pooledBlockBytes_=0;

    static void relax(){
#if defined(__i386__)||defined(__x86_64__)
        __builtin_ia32_pause();
#elif defined(__aarch64__)
        asm volatile("yield");
#endif
    }
    // The queue budget: chunks in use plus live blocks. CPU shadows are accounted separately (ShadowBudgetBytes): a shadow is
    // memory the replay thread can never free, so counting it here would turn backpressure into a full drain.
    std::size_t inflight()const{return std::size_t(get(stats.chunksLive))*ChunkBytes+std::size_t(get(stats.blockBytes));}
    bool over(std::size_t extra)const{return inflight()+extra>budget();}
    bool drained()const{return replayed_.load()>=recorded_.load();}
    void noteHighWater(){raiseMax(stats.highWaterBytes,inflight());}

    // Publishes, then waits (pumped) while the request does not fit and the consumer still has work that can free memory.
    void backpressure(std::size_t extra){
        if(!over(extra)||drained())return;
        publish();
        const auto t0=nowNs();add(stats.backpressureWaits);bpWaiting_.store(true);
        while(over(extra)&&!drained())progress_.waitPumped(50);   // the timeout only bounds a missed wake; the protocol does not rely on it
        bpWaiting_.store(false);add(stats.backpressureNs,nowNs()-t0);
    }
    Chunk* acquireChunk(){
        for(;;){
            backpressure(ChunkBytes);
            Chunk* c=nullptr;
            {std::lock_guard<std::mutex> l(pool_);if(!freeChunks_.empty()){c=freeChunks_.back();freeChunks_.pop_back();}}
            if(!c){c=new(std::nothrow) Chunk;if(c)add(stats.chunkAllocs);}
            if(c){add(stats.chunksLive);noteHighWater();return c;}
            if(drained())std::abort();   // out of address space with nothing left to wait for
            publish();progress_.waitPumped(50);
        }
    }
    void nextChunk(){
        Chunk* n=acquireChunk();Chunk* c=wchunk_;
        c->next.store(n,std::memory_order_relaxed);
        c->published.store(wpos_);c->end.store(wpos_);   // closing publishes everything committed so far
        std::atomic_thread_fence(std::memory_order_seq_cst);
        wchunk_=n;wpos_=0;pubPos_=0;sinceCmds_=0;sinceBytes_=0;
        add(stats.publishes);wake();
    }
    void wake(){if(sleeping_.load())consumerEv_.set();}
    void recycle(Chunk* c){
        c->next.store(nullptr,std::memory_order_relaxed);c->published.store(0,std::memory_order_relaxed);c->end.store(0,std::memory_order_relaxed);
        {std::lock_guard<std::mutex> l(pool_);freeChunks_.push_back(c);}
        stats.chunksLive.fetch_sub(1,std::memory_order_relaxed);
    }
    // Moves the consumer past a fully consumed, closed chunk.
    void advance(){
        for(;;){
            const std::uint32_t e=rchunk_->end.load(std::memory_order_acquire);
            if(!e||rpos_<e)return;
            Chunk* n=rchunk_->next.load(std::memory_order_acquire);recycle(rchunk_);rchunk_=n;rpos_=0;
            if(bpWaiting_.load())progress_.set();
        }
    }
    const CommandHeader* peek(){
        for(;;){
            const std::uint32_t pub=rchunk_->published.load(std::memory_order_acquire);
            if(rpos_<pub)return reinterpret_cast<const CommandHeader*>(rchunk_->data+rpos_);
            const std::uint32_t e=rchunk_->end.load(std::memory_order_acquire);
            if(!e||rpos_<e)return nullptr;
            advance();
        }
    }
    static unsigned blockClass(std::size_t bytes){unsigned s=kMinBlockShift;while(s<=kMaxBlockShift&&(std::size_t(1)<<s)<bytes)++s;return s>kMaxBlockShift?kBlockClasses:s-kMinBlockShift;}

public:
    Counters stats;

    explicit Queue(std::size_t budget=BudgetBytes):budget_(budget){
        wchunk_=new Chunk;rchunk_=wchunk_;add(stats.chunkAllocs);add(stats.chunksLive);
    }
    Queue(const Queue&)=delete;Queue& operator=(const Queue&)=delete;
    ~Queue(){
        // Free unretired commands' blocks, then every chunk of the chain and the pools. Producer and consumer are done.
        for(Chunk* c=rchunk_;c;){
            std::uint32_t pos=c==rchunk_?rpos_:0;const std::uint32_t lim=c==wchunk_?wpos_:c->end.load();
            while(pos<lim){auto* h=reinterpret_cast<const CommandHeader*>(c->data+pos);if(h->flags&kFlagBlock)freeBlock(blockOf(h));pos+=h->size;}
            Chunk* n=c->next.load();delete c;c=n;
        }
        for(Chunk* c:freeChunks_)delete c;
        for(auto& v:freeBlocks_)for(Block* b:v)::operator delete(b);
    }

    // ---- Producer ----
    // Reserves a command; returns its payload (8-aligned, directly after the header). May block for backpressure.
    // payload <= MaxInlinePayload (a larger one is a caller bug: use a Block). Pair with commit().
    void* reserve(std::uint16_t id,std::uint32_t payload,std::uint16_t flags=0){
        assert(!open_);
        if(payload>MaxInlinePayload)std::abort();
        const std::uint32_t padded=(payload+7u)&~7u,total=8u+padded;
        if(wpos_+total>ChunkBytes)nextChunk();
        auto* h=reinterpret_cast<CommandHeader*>(wchunk_->data+wpos_);
        h->id=id;h->flags=flags;h->size=total;
        if(padded>payload)std::memset(reinterpret_cast<unsigned char*>(h+1)+payload,0,padded-payload);   // deterministic padding
        openSize_=total;open_=true;
        return h+1;
    }
    // reserve() for a command that owns a Block: the Block* goes into the first kBlockSlot bytes (kFlagBlock), the
    // caller's payload follows; the returned pointer is the caller's part. retire() releases the Block.
    void* reserveWithBlock(std::uint16_t id,std::uint32_t payload,Block* block,std::uint16_t flags=0){
        auto* p=static_cast<unsigned char*>(reserve(id,std::uint32_t(kBlockSlot)+payload,std::uint16_t(flags|kFlagBlock)));
        std::memset(p,0,kBlockSlot);std::memcpy(p,&block,sizeof block);return p+kBlockSlot;
    }
    void commit(){
        assert(open_);
        wpos_+=openSize_;sinceCmds_+=1;sinceBytes_+=openSize_;open_=false;
        recorded_.store(recorded_.load(std::memory_order_relaxed)+1,std::memory_order_release);
        own(stats.commands);own(stats.bytes,openSize_);
        if(sinceCmds_>=kAutoPublishCommands||sinceBytes_>=kAutoPublishBytes)publish();
    }
    // Makes everything committed visible to the consumer and wakes it if it sleeps.
    void publish(){
        if(wpos_==pubPos_)return;
        wchunk_->published.store(wpos_);pubPos_=wpos_;sinceCmds_=0;sinceBytes_=0;
        std::atomic_thread_fence(std::memory_order_seq_cst);   // pairs with the fence in next(): the store above or the sleeper flag is seen
        own(stats.publishes);raiseMax(stats.highWaterDepth,recorded_.load(std::memory_order_relaxed)-replayed_.load(std::memory_order_relaxed));
        wake();
    }
    std::uint64_t recordedSeq()const{return recorded_.load(std::memory_order_acquire);}
    // Waits (pumped) until command number seq (a recordedSeq() value) has been retired. Publishes first.
    void waitReplayed(std::uint64_t seq,WaitKind kind=WaitKind::Sync){
        if(replayed_.load()>=seq)return;
        publish();
        const auto t0=nowNs();waitSeq_.store(seq);
        while(replayed_.load()<seq)progress_.waitPumped(50);
        waitSeq_.store(0);
        const auto ns=nowNs()-t0;
        if(kind==WaitKind::Present){add(stats.presentWaits);add(stats.presentNs,ns);}
        else if(kind==WaitKind::Backpressure){add(stats.backpressureWaits);add(stats.backpressureNs,ns);}
        else{add(stats.syncNs,ns);}   // syncCalls itself is counted by the caller, which also knows the call
    }
    void waitDrained(WaitKind kind=WaitKind::Sync){waitReplayed(recordedSeq(),kind);}
    // A pooled Block of at least bytes (capacity is the power of two above), budgeted. nullptr when the request alone
    // exceeds the budget, or still does not fit once the consumer has drained: the caller takes the sync pass-through.
    Block* tryAllocBlock(std::size_t bytes){
        const unsigned k=blockClass(bytes?bytes:1);
        if(k>=kBlockClasses||(std::size_t(1)<<(k+kMinBlockShift))>budget()){own(stats.blockRefused);return nullptr;}
        const std::size_t cap=std::size_t(1)<<(k+kMinBlockShift);
        backpressure(cap);
        if(over(cap)){own(stats.blockRefused);return nullptr;}
        Block* b=nullptr;
        {std::lock_guard<std::mutex> l(pool_);auto& v=freeBlocks_[k];if(!v.empty()){b=v.back();v.pop_back();pooledBlockBytes_-=cap;}}
        if(b)add(stats.blockReuses);
        else{
            void* m=::operator new(sizeof(Block)+cap,std::nothrow);if(!m){own(stats.blockRefused);return nullptr;}
            b=static_cast<Block*>(m);b->capacity=std::uint32_t(cap);b->cls=k;add(stats.blockAllocs);
        }
        b->used=0;add(stats.blocksLive);add(stats.blockBytes,cap);noteHighWater();
        return b;
    }
    // Block memory back to the pool. Called by retire() for kFlagBlock commands; the producer calls it for a Block it
    // allocated but did not attach. Thread-safe.
    void freeBlock(Block* b){
        if(!b)return;
        const std::size_t cap=b->capacity;
        stats.blocksLive.fetch_sub(1,std::memory_order_relaxed);stats.blockBytes.fetch_sub(cap,std::memory_order_relaxed);
        bool pooled=false;
        {std::lock_guard<std::mutex> l(pool_);if(pooledBlockBytes_+cap<=kMaxPooledBlockBytes){freeBlocks_[b->cls].push_back(b);pooledBlockBytes_+=cap;pooled=true;}}
        if(!pooled)::operator delete(b);
        if(bpWaiting_.load())progress_.set();
    }
    // Registered CPU shadow bytes: their own cap (shadowAdmit), not part of the queue budget.
    void addShadowBytes(std::int64_t delta){stats.shadowBytes.fetch_add(delta,std::memory_order_relaxed);}
    std::size_t shadowCap()const{return pressure_.load()?ShadowBudgetBytes/2:ShadowBudgetBytes;}
    // A new shadow of `bytes` fits the cap now (live shadows are never evicted, a new one is simply refused).
    void addTexShadowBytes(std::int64_t delta){stats.texShadowBytes.fetch_add(delta,std::memory_order_relaxed);}
    std::size_t texShadowCap()const{return pressure_.load()?TextureShadowBudgetBytes/2:TextureShadowBudgetBytes;}
    bool texShadowAdmit(std::size_t bytes)const{const auto s=stats.texShadowBytes.load(std::memory_order_relaxed);return (s>0?std::size_t(s):0)+bytes<=texShadowCap();}
    bool shadowAdmit(std::size_t bytes)const{const auto s=stats.shadowBytes.load(std::memory_order_relaxed);return (s>0?std::size_t(s):0)+bytes<=shadowCap();}
    bool canAdmit(std::size_t bytes)const{return !over(bytes);}
    // Memory pressure (any thread): halves the budget. trim() (producer, at a quiet point) releases pooled idle memory.
    void setPressure(bool on){pressure_.store(on);}
    bool pressure()const{return pressure_.load();}
    std::size_t budget()const{return pressure_.load()?budget_/2:budget_;}
    void trim(){
        std::vector<Chunk*> c;std::vector<Block*> b;
        {std::lock_guard<std::mutex> l(pool_);c.swap(freeChunks_);for(auto& v:freeBlocks_){b.insert(b.end(),v.begin(),v.end());v.clear();}pooledBlockBytes_=0;}
        for(Chunk* x:c)delete x;
        for(Block* x:b)::operator delete(x);
    }

    // ---- Consumer ----
    // The next published, unretired command, or nullptr (!wait and empty, or interrupted). The same command is returned
    // until retire(). With wait: spins briefly, then sleeps on the event.
    const CommandHeader* next(bool wait){
        if(auto* h=peek())return h;
        if(!wait)return nullptr;
        for(;;){
            // Spin ~50 us by the clock (a pause is far shorter under Rosetta): a sync round trip then usually costs no wakeup.
            const std::uint64_t t0=nowNs();
            for(;;){
                for(int i=0;i<64;++i){if(auto* h=peek())return h;if(interrupted_.load(std::memory_order_relaxed))return nullptr;relax();}
                if(nowNs()-t0>50000)break;
            }
            if(interrupted_.load())return nullptr;
            sleeping_.store(true);
            std::atomic_thread_fence(std::memory_order_seq_cst);   // the flag is visible before the cursor is re-read: publish() sees it or we see the data
            if(auto* h=peek()){sleeping_.store(false);return h;}
            own(stats.consumerSleeps);
            if(interrupted_.load()){sleeping_.store(false);return nullptr;}
            consumerEv_.wait(250);
            sleeping_.store(false);
        }
    }
    // Marks the command returned by next() executed: releases its Block, advances replayedSeq, recycles a finished chunk.
    void retire(const CommandHeader* h){
        assert(h==reinterpret_cast<const CommandHeader*>(rchunk_->data+rpos_));
        if(h->flags&kFlagBlock)freeBlock(blockOf(h));
        rpos_+=h->size;
        const std::uint64_t n=replayed_.load(std::memory_order_relaxed)+1;
        replayed_.store(n);
        advance();
        const std::uint64_t w=waitSeq_.load();
        if(w&&n>=w)progress_.set();
        else if(bpWaiting_.load()&&n>=recorded_.load())progress_.set();   // drained: nothing more to free, let the producer re-evaluate
    }
    std::uint64_t replayedSeq()const{return replayed_.load();}
    // The Block of a kFlagBlock command and the caller part of its payload (after the slot).
    static Block* blockOf(const CommandHeader* h){Block* b=nullptr;if(h->flags&kFlagBlock)std::memcpy(&b,h+1,sizeof b);return b;}
    static const unsigned char* payload(const CommandHeader* h){return reinterpret_cast<const unsigned char*>(h+1)+((h->flags&kFlagBlock)?kBlockSlot:0);}
    // Makes a sleeping next(true) return nullptr (stop/quiesce requests); clearInterrupt() re-arms waiting.
    void interrupt(){interrupted_.store(true);consumerEv_.set();}
    void clearInterrupt(){interrupted_.store(false);consumerEv_.reset();}

    std::uint64_t depth()const{return recorded_.load()-replayed_.load();}
};
}
