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
// 0.3.193 (CS): the stream's own memory shares a 32-bit address space with the game and the world renderer, which stalled for lack of a
// contiguous block while the stream held ~100 MiB. Real sessions peak at ~12 MiB of queue; every cap is now 16 MiB (all four together ~64 MiB at
// the very worst, ~20-30 MiB typically) and the idle pools are kept small (kPoolMaxChunks, kMaxPooledBlockBytes, PoolTuner).
constexpr std::size_t BudgetBytes=std::size_t(16)<<20;
constexpr std::size_t TextureShadowBudgetBytes=std::size_t(16)<<20;   // per-level texture shadows; halved under pressure; evictable (LRU)
constexpr std::size_t ShadowBudgetBytes=std::size_t(16)<<20;   // CPU shadows of DYNAMIC buffers; halved under pressure; dropped when idle under pressure
constexpr std::size_t kPoolMaxChunks=4,kReserveChunks=2;   // idle chunks kept at most / after a quiet window
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
    static constexpr std::size_t kMaxPooledBlockBytes=std::size_t(4)<<20;

    // 0.3.193 (CS): every group on its own cache line (kLine=128: Apple Silicon's line; x86's 64 divides it). The producer's private
    // fields change on every command, the consumer's on every retire, recorded_/replayed_ are each written by one side and read by the
    // other; sharing a line made each side's store evict the other's. The rarely written flags share one line.
    // Producer-private.
    alignas(kLine) Chunk* wchunk_;
    std::uint32_t wpos_=0,pubPos_=0,sinceCmds_=0,sinceBytes_=0,openSize_=0;bool open_=false;
    // Written by the producer at every commit.
    alignas(kLine) std::atomic<std::uint64_t> recorded_{0};
    // Consumer-private.
    alignas(kLine) Chunk* rchunk_;
    std::uint32_t rpos_=0;
    // Written by the consumer at every retire.
    alignas(kLine) std::atomic<std::uint64_t> replayed_{0};
    // Written by the producer while it waits (the consumer reads it at every retire).
    alignas(kLine) std::atomic<std::uint64_t> waitSeq_{0};
    // Rarely written flags (sleeping_ only when the consumer goes idle).
    alignas(kLine) std::atomic<bool> sleeping_{false};
    std::atomic<bool> interrupted_{false},bpWaiting_{false},pressure_{false};
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
            if(!c){c=new(std::nothrow) Chunk;if(c){add(stats.chunkAllocs);add(stats.chunksHeap);}}
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
        bool kept=false;
        {std::lock_guard<std::mutex> l(pool_);if(freeChunks_.size()<kPoolMaxChunks){freeChunks_.push_back(c);kept=true;}}
        if(!kept){delete c;stats.chunksHeap.fetch_sub(1,std::memory_order_relaxed);}   // a spike's extra chunks do not stay
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
    // Layout check (static_assert below): the producer-private, consumer-private, recorded_, replayed_, waitSeq_ and flag groups each
    // start on their own cache line, so no two of them share one.
#if defined(__clang__)||defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
    static constexpr bool layoutIsolated(){
        constexpr std::size_t o[]={offsetof(Queue,wchunk_),offsetof(Queue,recorded_),offsetof(Queue,rchunk_),offsetof(Queue,replayed_),offsetof(Queue,waitSeq_),offsetof(Queue,sleeping_)};
        for(std::size_t i=0;i<6;++i){if(o[i]%kLine)return false;for(std::size_t j=i+1;j<6;++j)if(o[i]/kLine==o[j]/kLine)return false;}
        return offsetof(Queue,budget_)/kLine==offsetof(Queue,sleeping_)/kLine&&offsetof(Queue,stats)%kLine==0;   // budget_ rides with the rarely written flags
    }
#if defined(__clang__)||defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    Counters stats;

    explicit Queue(std::size_t budget=BudgetBytes):budget_(budget){
        wchunk_=new Chunk;rchunk_=wchunk_;add(stats.chunkAllocs);add(stats.chunksHeap);add(stats.chunksLive);
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
        {std::lock_guard<std::mutex> l(pool_);auto& v=freeBlocks_[k];if(!v.empty()){b=v.back();v.pop_back();pooledBlockBytes_-=cap;stats.blockPoolBytes.fetch_sub(cap,std::memory_order_relaxed);}}
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
        {std::lock_guard<std::mutex> l(pool_);if(pooledBlockBytes_+cap<=kMaxPooledBlockBytes){freeBlocks_[b->cls].push_back(b);pooledBlockBytes_+=cap;pooled=true;stats.blockPoolBytes.fetch_add(cap,std::memory_order_relaxed);}}
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
        {std::lock_guard<std::mutex> l(pool_);c.swap(freeChunks_);for(auto& v:freeBlocks_){b.insert(b.end(),v.begin(),v.end());v.clear();}pooledBlockBytes_=0;stats.blockPoolBytes.store(0,std::memory_order_relaxed);}
        for(Chunk* x:c){delete x;stats.chunksHeap.fetch_sub(1,std::memory_order_relaxed);}
        for(Block* x:b)::operator delete(x);
    }
    // Idle pool memory: chunks and Block bytes sitting unused. Only the pools are touched (under their lock): live chunks and blocks never.
    void poolState(std::size_t& chunks,std::size_t& blockBytes){std::lock_guard<std::mutex> l(pool_);chunks=freeChunks_.size();blockBytes=pooledBlockBytes_;}
    // Frees up to `chunks` pooled chunks and about `blockBytes` of pooled Blocks (largest classes first).
    void trimPool(std::size_t chunks,std::size_t blockBytes){
        std::vector<Chunk*> c;std::vector<Block*> b;
        {std::lock_guard<std::mutex> l(pool_);
         while(chunks&&!freeChunks_.empty()){c.push_back(freeChunks_.back());freeChunks_.pop_back();--chunks;}
         for(unsigned k=kBlockClasses;k-->0&&blockBytes;){auto& v=freeBlocks_[k];
             while(blockBytes&&!v.empty()){Block* x=v.back();v.pop_back();const std::size_t cap=x->capacity;pooledBlockBytes_-=cap;stats.blockPoolBytes.fetch_sub(cap,std::memory_order_relaxed);blockBytes=blockBytes>cap?blockBytes-cap:0;b.push_back(x);}}}
        for(Chunk* x:c){delete x;stats.chunksHeap.fetch_sub(1,std::memory_order_relaxed);}
        for(Block* x:b)::operator delete(x);
    }
    // Every byte the queue holds from the process: chunks (in use and pooled), live and pooled Blocks. (Shadows are accounted by their owners.)
    std::size_t reservedBytes()const{return std::size_t(get(stats.chunksHeap))*sizeof(Chunk)+std::size_t(get(stats.blockBytes))+std::size_t(get(stats.blockPoolBytes));}

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
    // next(true) for a consumer that also has timed work (pending queries to poll): the same sleeper handshake, but the wait is
    // bounded by ms and publish()/nextChunk()'s wake() ends it at once. Returns the next command, or nullptr after ms / on interrupt.
    const CommandHeader* nextTimed(std::uint32_t ms){
        if(auto* h=peek())return h;
        if(interrupted_.load())return nullptr;
        sleeping_.store(true);
        std::atomic_thread_fence(std::memory_order_seq_cst);   // as in next(): the flag is visible before the cursor is re-read
        if(auto* h=peek()){sleeping_.store(false);return h;}
        own(stats.consumerSleeps);
        if(!interrupted_.load())consumerEv_.wait(ms);
        sleeping_.store(false);
        return peek();
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
static_assert(alignof(Queue)>=kLine,"Queue is cache-line aligned: heap objects holding one need C++17 aligned new");
static_assert(Queue::layoutIsolated(),"Queue field groups sit on distinct cache lines");

// Returns idle pool memory after a quiet window instead of keeping a spike's high-water mark forever: sampled once per frame by the producer
// (any point where it may touch the pools); a window of kWindow frames that never drew the pool below its reserve proves the excess unused.
struct PoolTuner {
    static constexpr unsigned kWindow=120;
    unsigned frames=0;std::size_t minChunks=std::size_t(-1),minBlockBytes=std::size_t(-1);
    void sample(Queue& q){
        std::size_t c=0,b=0;q.poolState(c,b);
        if(c<minChunks)minChunks=c;if(b<minBlockBytes)minBlockBytes=b;
        if(++frames<kWindow)return;
        if(minChunks>kReserveChunks||minBlockBytes>0)q.trimPool(minChunks>kReserveChunks?minChunks-kReserveChunks:0,minBlockBytes==std::size_t(-1)?0:minBlockBytes);
        frames=0;minChunks=minBlockBytes=std::size_t(-1);
    }
};
}
