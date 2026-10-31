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
// Threading: producer methods from one thread only (the game thread; there is no record gate: a call from any other thread
// is a caller bug, counted only for the cursor calls); consumer methods from one thread only (the replay thread). Counters and pressure are any-thread.
#include <algorithm>
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
#include "stream_idle_profile.h"

namespace NorthlightStream {
struct CommandHeader {std::uint16_t id;std::uint16_t flags;std::uint32_t size;};   // size includes the header; a multiple of 8
static_assert(sizeof(CommandHeader)==8,"header is 8 bytes");

constexpr std::size_t ChunkBytes=std::size_t(1)<<20;
constexpr std::size_t MaxInlinePayload=ChunkBytes/4;   // larger payloads travel in a Block
// 0.3.192 (CS): the stream's own memory shares a 32-bit address space with the game and the world renderer. Real sessions hold 17-30 MiB in all (queue ~3-12, buffer shadows 4-50, texture shadows <= 16, rings only while a buffer DISCARDs with the replay behind;
// rings are freed after 60 idle frames, idle pools are kept small: kPoolMaxChunks, kMaxPooledBlockBytes, PoolTuner).
// 0.3.216 (issue #66 follow-up): memory pressure never refuses a shadow, a large grant or a ring slice, never drops one and never waits for a slice: each of those turned a buffer lock into a synchronous round trip to the replay thread
// (hundreds per frame, an unplayable game), which costs more than the memory. Pressure only halves the queue budget and frees pooled chunks, Blocks, scratch, spare level allocations and free ring slices. WORST CASE game-side under pressure
// (every cap at its maximum at once, which no session approaches): buffer shadows 32 (adaptive regular max) + large allowance 36 + rings 20 (regular) + 36 (large) + texture shadows 16 + queue 16 (the halved 32) = 156 MiB, plus the replay-side
// copies' 16+16 MiB (replay_copies.h; halved under pressure) = ~172-188 MiB. The same without pressure but with the queue at 32: 172 + 16.
constexpr std::size_t BudgetBytes=std::size_t(16)<<20;
// 0.3.200 (pipeline): StreamFramesAhead (1..kMaxFramesAhead) frames may be in flight; the queue budget grows by BudgetBytes per extra frame up to
// kMaxBudgetBytes (32 MiB; halved under memory pressure). 1 = BudgetBytes, the 0.3.199 queue.
constexpr unsigned kMaxFramesAhead=3;
constexpr std::size_t kMaxBudgetBytes=std::size_t(32)<<20;
constexpr unsigned clampFramesAhead(unsigned n){return n<1?1:n>kMaxFramesAhead?kMaxFramesAhead:n;}
constexpr std::size_t budgetForFramesAhead(unsigned n){return BudgetBytes*clampFramesAhead(n)<kMaxBudgetBytes?BudgetBytes*clampFramesAhead(n):kMaxBudgetBytes;}
constexpr std::size_t TextureShadowBudgetBytes=std::size_t(16)<<20;   // per-level texture shadows; not halved under pressure (0.3.216); evictable (LRU)
// 0.3.192 (CS): CPU shadows of buffers (DYNAMIC and re-locked non-DYNAMIC ones; evictable LRU, never while locked). The cap is ADAPTIVE: ShadowBudgetBytes is
// the base/start value; it grows by kShadowGrowStep (up to kShadowBudgetMaxBytes) only when the shadows thrash (also under memory pressure since 0.3.216: refusing growth sent hot buffers to the synchronous pass-through): the LRU had
// to evict a HOT shadow (locked within kShadowHotFrames Presents) or a re-lock found no victim at all. At most one step per kShadowGrowFrames Presents
// (and none in the first interval). Pressure changes nothing here (0.3.216). Growth is game-thread; the cap is read by any thread.
constexpr std::size_t ShadowBudgetBytes=std::size_t(16)<<20;
// 0.3.205: the ring budget of regular-cap buffers (live + retired slices; was ShadowBudgetBytes, 16 MiB): in crowds several skinning buffers of 2-3 MiB DISCARD many times a frame and the 16 MiB ring
// made them wait for a slice the replay thread still read (waitWhy budget). Rings are dropped under memory pressure and freed after 60 idle frames, so the extra 4 MiB is only held while they are busy.
constexpr std::size_t RingRegularBudgetBytes=std::size_t(20)<<20;
constexpr std::size_t kShadowBudgetMaxBytes=std::size_t(32)<<20,kShadowGrowStep=std::size_t(4)<<20;
constexpr std::uint64_t kShadowHotFrames=60,kShadowGrowFrames=60;
// 0.3.192 (CS): LARGE-buffer allowance, outside the regular cap: a DYNAMIC buffer above a quarter of the current cap (so never admitted by shadowAdmit) up to
// kMaxLargeShadow may keep a shadow in a separate budget of LargeShadowBudgetBytes (the game's ~15.8 MB dynamic buffer whose every lock was a synchronous pass-through, and
// 0.3.204: on some clients also an ~18 MB one, so two fit). Granted and kept under memory pressure too (0.3.216: dropping it made every lock of the buffer a synchronous readback).
// Granted at creation when the allowance is free, else at a write re-lock (readback) or a DISCARD lock; another large buffer takes it over only by LRU
// when the holder is unlocked and idle for kLargeIdleFrames (a 16 MB readback must not ping-pong). Worst case +36 MiB game-side on top of the regular cap; not dropped under memory pressure (0.3.216).
constexpr std::size_t LargeShadowBudgetBytes=std::size_t(36)<<20,kMaxLargeShadow=std::size_t(24)<<20;   // 0.3.204: was 16/16 MiB (one ~15.8 MB buffer); a player's client also has an 18 MB one
constexpr std::uint64_t kLargeIdleFrames=60;
constexpr std::size_t kPoolMaxChunks=4,kReserveChunks=2;   // idle chunks kept at most / after a quiet window
constexpr std::uint32_t kAutoPublishCommands=64,kAutoPublishBytes=64u<<10;
// 0.3.200 (pipeline): commit()'s automatic publish wakes a SLEEPING consumer only once this much has been published without a wake (a sleeping replay
// thread has caught up: waking it per 64 commands cost a SetEvent each time). Explicit publishes (scene/target/clear/sync/Present/GetData), a chunk
// switch and backpressure always wake; an explicit publish with nothing new still wakes when an automatic one held its wake back (never strands work).
constexpr std::uint32_t kLazyWakeCommands=256,kLazyWakeBytes=256u<<10;
constexpr std::uint32_t kNoPayload=0xFFFFFFFFu;   // a nullable pointer's offset in a generated Args struct

// Command flags. kFlagBlock: the first kBlockSlot payload bytes hold a Block*; retire() returns that Block to the pool.
constexpr std::uint16_t kFlagBlock=1;
// 0.3.196 (task 12): kFlagWaitTarget: the producer may later waitReplayed() for exactly this command (runSync, runTask, Present/SwapPresent). retire() runs a
// seq_cst fence before it looks at waitSeq_/bpWaiting_ only for these; every other command retires with a plain release store (no locked instruction on i386).
// Every waitReplayed(seq) target MUST carry the flag (a wait on an unflagged command is rescued only by the consumer's pre-sleep check or the 50 ms timeout).
constexpr std::uint16_t kFlagWaitTarget=2;
constexpr std::size_t kBlockSlot=8;

// A pooled large payload. data() is 8-aligned; used is the payload byte count the producer stored.
struct Block {
    std::uint32_t capacity,cls,used,pad_;
    unsigned char* data(){return reinterpret_cast<unsigned char*>(this+1);}
};
static_assert(sizeof(Block)==16,"block header keeps data 8-aligned");

enum class WaitKind {Sync,Present,Backpressure};
enum class ExpressKind {Readback,Query};

// 0.3.206 (task 31): a unit of work for the replay thread's next command boundary (Queue::runExpress). Lives on the poster's stack.
constexpr std::uint16_t kExpressIdle=0xFFFF;   // ExpressTask::behindCmd: the replay thread was idle (slept / empty queue) when the task was served
// behindCmd / startNs are written by the replay thread before it clears the mailbox (the poster reads them after that handshake): the last command executed, and nowNs() at serve start (only when `timed`).
struct ExpressTask {void (*fn)(void*);void* arg;bool (*ready)(void*);bool timed;std::uint16_t behindCmd;std::uint64_t startNs;};
struct ExpressTimes {std::uint16_t behindCmd=kExpressIdle;std::uint64_t startNs=0;};   // what a timed runExpress hands back
// Counts observations, not elapsed time: a held publication proves work existed
// while replay was asleep; it does not make the whole queue-idle span avoidable.
struct WakePublicationDiagnostics {std::uint64_t heldBatches=0,heldCommands=0,heldBytes=0,wakeSignals=0;};
class Queue {
    struct Chunk {
        std::atomic<Chunk*> next{nullptr};
        std::atomic<std::uint32_t> published{0};   // bytes of this chunk the consumer may read
        std::atomic<std::uint32_t> end{0};         // 0 while open; the final byte count once the producer moved on
        alignas(8) unsigned char data[ChunkBytes];
    };
    static constexpr unsigned kMinBlockShift=12,kMaxBlockShift=27,kBlockClasses=kMaxBlockShift-kMinBlockShift+1;
    static constexpr std::size_t kMaxPooledBlockBytes=std::size_t(4)<<20;

    // 0.3.192 (CS): every group on its own cache line (kLine=128: Apple Silicon's line; x86's 64 divides it). The producer's private
    // fields change on every command, the consumer's on every retire, recorded_/replayed_ are each written by one side and read by the
    // other; sharing a line made each side's store evict the other's. The rarely written flags share one line.
    // Producer-private.
    alignas(kLine) Chunk* wchunk_;
    std::uint32_t wpos_=0,pubPos_=0,sinceCmds_=0,sinceBytes_=0,openSize_=0;bool open_=false;
    bool lazyWake_=false;std::uint32_t lazyCmds_=0,lazyBytes_=0;   // 0.3.200 (pipeline): published by commit() without a wake since the last wake (kLazyWake*)
#ifdef NORTHLIGHT_STREAM_CHECK_WAIT
    bool openFlagged_=false;unsigned badWaits_=0;std::vector<std::uint64_t> flaggedSeqs_;   // 0.3.196 (task 12): test builds only, see unflaggedWaits()
    unsigned wakeSignals_=0;   // 0.3.200 (pipeline): consumer wakes the producer signalled (test builds only)
#endif
    // Written by the producer at every commit.
    alignas(kLine) std::atomic<std::uint64_t> recorded_{0};
    std::atomic<std::uint64_t> diagnosticPublished_{0},diagnosticPublishSerial_{0};
    Counter diagnosticHeldBatches_{0},diagnosticHeldCommands_{0},diagnosticHeldBytes_{0},diagnosticWakeSignals_{0};
    bool idleTracking_=false;   // configured by the producer before the consumer starts; immutable during replay
    // Consumer-private.
    alignas(kLine) Chunk* rchunk_;
    std::uint32_t rpos_=0;
    std::uint64_t lastSignalledSeq_=0,bpSignalledRec_=~std::uint64_t(0);   // 0.3.196 (task 12): wakeWaiters' memory (consumer only)
#ifdef NORTHLIGHT_STREAM_CHECK_WAIT
    std::atomic<unsigned> backstopSignals_{0};
    void (*idleSnapshotHook_)(void*)=nullptr;void* idleSnapshotContext_=nullptr;
#endif
    // Written by the consumer at every retire.
    alignas(kLine) std::atomic<std::uint64_t> replayed_{0};
    // Written by the producer while it waits (the consumer reads it at every retire).
    alignas(kLine) std::atomic<std::uint64_t> waitSeq_{0};
    // Rarely written flags (sleeping_ only when the consumer goes idle).
    alignas(kLine) std::atomic<bool> sleeping_{false};
    std::atomic<bool> interrupted_{false},bpWaiting_{false},pressure_{false};
    // 0.3.206 (task 31): the express mailbox (see runExpress): a task the replay thread runs at its next command boundary. Written by the producer (rarely), read by the consumer before every command.
    std::atomic<ExpressTask*> express_{nullptr};
    std::atomic<std::size_t> shadowCapCur_{ShadowBudgetBytes};std::uint64_t shadowGrowAt_=kShadowGrowFrames;   // adaptive buffer-shadow cap; first frame it may grow
    std::size_t budget_;
    unsigned timedSpinSkip_=0;   // consumer only: idle waits nextTimed() still sleeps at once after a spin found nothing
    Event consumerEv_{false},progress_{false},expressDone_{false};
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
        beginDiagnosticPublish();
        c->next.store(n,std::memory_order_relaxed);
        c->published.store(wpos_);c->end.store(wpos_);   // closing publishes everything committed so far
        endDiagnosticPublish();
        std::atomic_thread_fence(std::memory_order_seq_cst);
        wchunk_=n;wpos_=0;pubPos_=0;sinceCmds_=0;sinceBytes_=0;
        add(stats.publishes);wake();
    }
    // 0.3.200 (pipeline): one SetEvent per sleep: the producer takes the sleeper flag (exchange, only when it saw it set), so further publishes before the consumer
    // runs again do not signal again (the event is already set). The consumer sets the flag before its final peek, so a publish after that peek still finds it.
    void wake(){
        lazyWake_=false;lazyCmds_=lazyBytes_=0;
        if(sleeping_.load()&&sleeping_.exchange(false)){
            if(idleTracking_)own(diagnosticWakeSignals_);
#ifdef NORTHLIGHT_STREAM_CHECK_WAIT
            ++wakeSignals_;
#endif
            consumerEv_.set();}
    }
    // commit()'s automatic publish: the same publication, but a sleeping consumer is woken only past kLazyWakeCommands/kLazyWakeBytes (see there).
    void publishLazy(){
        const std::uint32_t c=sinceCmds_,b=sinceBytes_;
        beginDiagnosticPublish();
        wchunk_->published.store(wpos_);pubPos_=wpos_;sinceCmds_=0;sinceBytes_=0;
        endDiagnosticPublish();
        std::atomic_thread_fence(std::memory_order_seq_cst);   // as in publish()
        own(stats.publishes);raiseMax(stats.highWaterDepth,recorded_.load(std::memory_order_relaxed)-replayed_.load(std::memory_order_relaxed));
        lazyCmds_+=c;lazyBytes_+=b;
        if(lazyCmds_<kLazyWakeCommands&&lazyBytes_<kLazyWakeBytes){
            lazyWake_=true;
            if(idleTracking_&&sleeping_.load()){
                own(diagnosticHeldBatches_);own(diagnosticHeldCommands_,c);own(diagnosticHeldBytes_,b);
            }
            return;
        }
        wake();
    }
    void beginDiagnosticPublish(){if(idleTracking_)diagnosticPublishSerial_.store(diagnosticPublishSerial_.load(std::memory_order_relaxed)+1,std::memory_order_release);}
    void endDiagnosticPublish(){if(idleTracking_){diagnosticPublished_.store(recorded_.load(std::memory_order_relaxed),std::memory_order_release);diagnosticPublishSerial_.store(diagnosticPublishSerial_.load(std::memory_order_relaxed)+1,std::memory_order_release);}}
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
            std::atomic_thread_fence(std::memory_order_seq_cst);   // 0.3.196 (task 12): once per chunk; the chunk is returned before bpWaiting_ is read (retire no longer fences per command)
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
    // 0.3.196 (task 12): consumer-side idle backstop. retire() fences only for kFlagWaitTarget commands, so a wake it skipped (a drained backpressure wait whose
    // last command was unflagged) is made up here: next()/nextTimed() call it after a seq_cst fence before they block, next()'s spin loop calls it every batch.
    // Signals at most once per waited seq and once per drained backpressure episode (keyed by the recorded count), so the spin loop does not SetEvent (a wineserver round trip) every batch;
    // `force` (the pre-sleep check) signals whenever the producer could be blocked.
    void wakeWaiters(bool force=false){
        const std::uint64_t r=replayed_.load(),w=waitSeq_.load();
        if(w&&r>=w){if(force||w!=lastSignalledSeq_){lastSignalledSeq_=w;signalProgress();}return;}
        if(bpWaiting_.load()){const std::uint64_t rec=recorded_.load();if(r>=rec&&(force||rec!=bpSignalledRec_)){bpSignalledRec_=rec;signalProgress();}}
    }
    void signalProgress(){
#ifdef NORTHLIGHT_STREAM_CHECK_WAIT
        backstopSignals_.fetch_add(1,std::memory_order_relaxed);
#endif
        progress_.set();
    }
    static unsigned blockClass(std::size_t bytes){
        if(bytes<=(std::size_t(1)<<kMinBlockShift))return 0;
        if(bytes>(std::size_t(1)<<kMaxBlockShift))return kBlockClasses;
        return unsigned(sizeof(unsigned)*8)-__builtin_clz(static_cast<unsigned>(bytes-1))-kMinBlockShift;
    }

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
#ifdef NORTHLIGHT_STREAM_CHECK_WAIT
        openFlagged_=(flags&kFlagWaitTarget)!=0;
#endif
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
#ifdef NORTHLIGHT_STREAM_CHECK_WAIT
        if(openFlagged_)flaggedSeqs_.push_back(recorded_.load(std::memory_order_relaxed));
#endif
        own(stats.commands);own(stats.bytes,openSize_);
        if(sinceCmds_>=kAutoPublishCommands||sinceBytes_>=kAutoPublishBytes)publishLazy();   // 0.3.200 (pipeline): lazy wake
    }
    // Makes everything committed visible to the consumer and wakes it if it sleeps.
    void publish(){
        if(wpos_==pubPos_){if(lazyWake_)wake();return;}   // 0.3.200 (pipeline): an automatic publish held its wake back: this explicit one delivers it
        beginDiagnosticPublish();
        wchunk_->published.store(wpos_);pubPos_=wpos_;sinceCmds_=0;sinceBytes_=0;
        endDiagnosticPublish();
        std::atomic_thread_fence(std::memory_order_seq_cst);   // pairs with the fence in next(): the store above or the sleeper flag is seen
        own(stats.publishes);raiseMax(stats.highWaterDepth,recorded_.load(std::memory_order_relaxed)-replayed_.load(std::memory_order_relaxed));
        wake();
    }
    std::uint64_t recordedSeq()const{return recorded_.load(std::memory_order_acquire);}
    WakePublicationDiagnostics wakePublicationDiagnostics()const{
        return {get(diagnosticHeldBatches_),get(diagnosticHeldCommands_),get(diagnosticHeldBytes_),get(diagnosticWakeSignals_)};
    }
    // Producer only, before start/restart of the consumer. No producer-private
    // queue cursor is read by the consumer or written by this diagnostic.
    void trackIdlePublications(bool on){idleTracking_=on;if(on){diagnosticPublished_.store(recorded_.load(std::memory_order_relaxed)-sinceCmds_,std::memory_order_relaxed);diagnosticPublishSerial_.store(0,std::memory_order_relaxed);}}
    IdlePoint idlePoint()const{
        IdlePoint p;if(!idleTracking_)return p;p.known=true;
        const auto serial=diagnosticPublishSerial_.load(std::memory_order_acquire);
        p.recorded=recorded_.load(std::memory_order_acquire);p.published=diagnosticPublished_.load(std::memory_order_acquire);p.replayed=replayed_.load(std::memory_order_acquire);
#ifdef NORTHLIGHT_STREAM_CHECK_WAIT
        if(idleSnapshotHook_)idleSnapshotHook_(idleSnapshotContext_);
#endif
        p.waitSeq=waitSeq_.load(std::memory_order_relaxed);p.backpressure=bpWaiting_.load(std::memory_order_relaxed);p.express=express_.load(std::memory_order_acquire)!=nullptr;
        p.consistent=!(serial&1)&&serial==diagnosticPublishSerial_.load(std::memory_order_acquire)&&p.recorded==recorded_.load(std::memory_order_acquire)&&p.replayed==replayed_.load(std::memory_order_acquire)&&p.replayed<=p.published&&p.published<=p.recorded;
        p.syncNs=get(stats.syncNs);p.presentNs=get(stats.presentNs);p.backpressureNs=get(stats.backpressureNs);
        p.recordNs=get(stats.recordSampledNs);p.lockNs=get(stats.lockNs);p.snapshotNs=get(stats.snapNs);p.publishes=get(stats.publishes);
        return p;
    }
    // Waits (pumped) until command number seq (a recordedSeq() value) has been retired. Publishes first. seq must be a command recorded with
    // kFlagWaitTarget (see there); builds with NORTHLIGHT_STREAM_CHECK_WAIT count a wait on an unflagged seq (unflaggedWaits(), the wait itself still completes).
    void waitReplayed(std::uint64_t seq,WaitKind kind=WaitKind::Sync){
#ifdef NORTHLIGHT_STREAM_CHECK_WAIT
        if(seq&&!std::binary_search(flaggedSeqs_.begin(),flaggedSeqs_.end(),seq))++badWaits_;
#endif
        if(replayed_.load()>=seq)return;
        publish();
        const auto t0=nowNs();waitSeq_.store(seq);
        // Match the consumer's short spin: quick replies need no window-message pump.
        // A replay blocked in SendMessage still falls back to the pumped wait.
        while(replayed_.load()<seq){
            for(unsigned i=0;i<64&&replayed_.load()<seq;++i)relax();
            if(nowNs()-t0>=50000)break;
        }
        while(replayed_.load()<seq)progress_.waitPumped(50);
        waitSeq_.store(0);
        const auto ns=nowNs()-t0;
        if(kind==WaitKind::Present){add(stats.presentWaits);add(stats.presentNs,ns);}
        else if(kind==WaitKind::Backpressure){add(stats.backpressureWaits);add(stats.backpressureNs,ns);}
        else{add(stats.syncNs,ns);}   // syncCalls itself is counted by the caller, which also knows the call
    }
    // 0.3.206 (task 31): runs fn(arg) on the replay thread at its next eligible command boundary (between two commands, never inside one) and returns once it ran. Unlike a Quiesce task it does not wait for the
    // queue to drain: dependencies must already be replayed, or ready must defer until they are. A deferred task must leave published commands to make progress, or become ready on a drained queue.
    // One poster at a time (the game thread). Counted in syncNs (comparable with the
    // drain waits) and expressNs, with separate readback/query counters. The query caller owns syncCalls/census. Wake handshake: store the mailbox, fence, wake a sleeper (the consumer sets sleeping_, fences, re-checks the mailbox).
    void runExpress(void (*fn)(void*),void* arg,ExpressTimes* times=nullptr,ExpressKind kind=ExpressKind::Readback,bool (*ready)(void*)=nullptr){   // times: Diagnostics only (the replay thread then reads the clock once at serve start)
        ExpressTask t{fn,arg,ready,times!=nullptr,kExpressIdle,0};
        const auto t0=nowNs();
        express_.store(&t,std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        if(sleeping_.load()&&sleeping_.exchange(false))consumerEv_.set();
        while(express_.load(std::memory_order_acquire)!=nullptr)expressDone_.waitPumped(50);   // the timeout only bounds a missed wake
        const auto ns=nowNs()-t0;
        if(times){times->behindCmd=t.behindCmd;times->startNs=t.startNs;}
        add(stats.syncNs,ns);add(stats.expressNs,ns);
        if(kind==ExpressKind::Query){add(stats.expressQueries);add(stats.queryExpressNs,ns);}
        else{add(stats.expressReadbacks);add(stats.readbackExpressNs,ns);}
    }
    void waitDrained(WaitKind kind=WaitKind::Sync){waitReplayed(recordedSeq(),kind);}   // the last recorded command must carry kFlagWaitTarget (no production caller)
#ifdef NORTHLIGHT_STREAM_CHECK_WAIT
    unsigned unflaggedWaits()const{return badWaits_;}   // producer thread
    unsigned backstopSignals()const{return backstopSignals_.load();}   // progress_ signals made by the idle backstop (wakeWaiters)
    unsigned wakeSignals()const{return wakeSignals_;}   // 0.3.200 (pipeline): consumer wakes signalled by the producer (producer thread)
    void testIdleSnapshotHook(void (*fn)(void*),void* context){idleSnapshotHook_=fn;idleSnapshotContext_=context;}
    void testArmWait(std::uint64_t w){waitSeq_.store(w);}   // test seam: a producer 'waiting' on seq w that never wakes
#endif
    // A pooled Block of at least bytes (capacity is the power of two above), budgeted. nullptr when the request alone
    // exceeds the budget, or still does not fit once the consumer has drained: the caller takes the sync pass-through.
    Block* tryAllocBlock(std::size_t bytes){
        const unsigned k=blockClass(bytes?bytes:1);
        // The open write chunk is always in flight (inflight() counts it): a class that cannot fit beside it never fits, so refuse now instead of draining the whole queue first.
        if(k>=kBlockClasses||(std::size_t(1)<<(k+kMinBlockShift))+ChunkBytes>budget()){own(stats.blockRefused);return nullptr;}
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
        std::atomic_thread_fence(std::memory_order_seq_cst);   // 0.3.196 (task 12): the Block is returned before bpWaiting_ is read (the producer may be on the producer side of this handshake). One fence per Block command (payloads above MaxInlinePayload), not only per flagged command
        if(bpWaiting_.load())progress_.set();
    }
    // Registered CPU shadow bytes: their own cap (shadowAdmit), not part of the queue budget.
    void addShadowBytes(std::int64_t delta){stats.shadowBytes.fetch_add(delta,std::memory_order_relaxed);}
    std::size_t shadowCap()const{const std::size_t c=shadowCapCur_.load(std::memory_order_relaxed);return c;}   // adaptive (see ShadowBudgetBytes); not halved under pressure (see shadowItemLimit)
    // 0.3.216 (issue #66 follow-up): memory pressure does NOT shrink any shadow cap (buffer, large allowance, texture). A shadow that goes means the next lock of its buffer
    // (or level) is a synchronous round trip to the replay thread, and a hundred of those per frame made the game unplayable (fps 70-140 -> 20-38) to save ~10 MiB of a stream that
    // holds 17-24 MiB in all. Pressure still halves the queue budget and releases pooled memory, rings and spare level allocations: none of that creates a sync path.
    std::size_t shadowItemLimit()const{return shadowCapCur_.load(std::memory_order_relaxed)/4;}
    // Thrash signal (game thread, `frame` = Presents so far): one more step, if no pressure, below the maximum and past the interval. true = grew.
    bool growShadowCap(std::uint64_t frame){
        const std::size_t c=shadowCapCur_.load(std::memory_order_relaxed);
        if(c+kShadowGrowStep>kShadowBudgetMaxBytes||frame<shadowGrowAt_)return false;
        shadowCapCur_.store(c+kShadowGrowStep,std::memory_order_relaxed);shadowGrowAt_=frame+kShadowGrowFrames;add(stats.shadowCapGrows);return true;
    }
    // A new shadow of `bytes` fits the cap now (shadowAdmit; making room by evicting is the game thread's job, see makeRoomForBufferShadow).
    void addTexShadowBytes(std::int64_t delta){stats.texShadowBytes.fetch_add(delta,std::memory_order_relaxed);}
    std::size_t texShadowCap()const{return TextureShadowBudgetBytes;}   // 0.3.216: not halved under pressure (see shadowItemLimit)
    bool texShadowAdmit(std::size_t bytes)const{const auto s=stats.texShadowBytes.load(std::memory_order_relaxed);return (s>0?std::size_t(s):0)+bytes<=texShadowCap();}
    std::size_t largeBytes()const{const auto s=stats.largeShadowBytes.load(std::memory_order_relaxed);return s>0?std::size_t(s):0;}
    void addLargeBytes(std::int64_t delta){stats.largeShadowBytes.fetch_add(delta,std::memory_order_relaxed);}
    bool largeAdmit(std::size_t bytes)const{return bytes<=kMaxLargeShadow&&largeBytes()+bytes<=LargeShadowBudgetBytes;}   // allowance free now
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
                for(int i=0;i<64;++i){if(auto* h=peek())return h;if(interrupted_.load(std::memory_order_relaxed)||express_.load(std::memory_order_relaxed))return nullptr;relax();}
                wakeWaiters();   // 0.3.196 (task 12): a wake retire() skipped (idle now: nothing to retire)
                if(nowNs()-t0>50000)break;
            }
            if(interrupted_.load())return nullptr;
            sleeping_.store(true);
            std::atomic_thread_fence(std::memory_order_seq_cst);   // the flag is visible before the cursor is re-read: publish() sees it or we see the data
            if(auto* h=peek()){sleeping_.store(false);return h;}
            if(express_.load()){sleeping_.store(false);return nullptr;}   // 0.3.206 (task 31): an express task is posted: the replay loop runs it
            wakeWaiters(true);   // 0.3.196 (task 12): after the fence above: the retire that skipped its fence has stored replayed_, so this load sees any waitSeq_/bpWaiting_ a producer published before it
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
        // Keep quick sync replies on the same spin path as next(true), even while queries are pending. WoW keeps a query
        // pending almost every frame, so this wait recurs every ms while the game is quiet: a spin that found nothing skips
        // the next 16 waits (about 0.3% of a core instead of 5%), and a command that ends a wait turns it back on.
        if(ms&&timedSpinSkip_)--timedSpinSkip_;
        else if(ms){
            const auto t0=nowNs();
            for(;;){
                for(int i=0;i<64;++i){if(auto* h=peek())return h;if(interrupted_.load(std::memory_order_relaxed)||express_.load(std::memory_order_relaxed))return nullptr;relax();}
                wakeWaiters();
                if(nowNs()-t0>50000)break;
            }
            timedSpinSkip_=16;
        }
        if(interrupted_.load())return nullptr;
        sleeping_.store(true);
        std::atomic_thread_fence(std::memory_order_seq_cst);   // as in next(): the flag is visible before the cursor is re-read
        if(auto* h=peek()){sleeping_.store(false);return h;}
        if(express_.load()){sleeping_.store(false);return nullptr;}   // 0.3.206 (task 31)
        wakeWaiters(true);   // 0.3.196 (task 12): as in next()
        own(stats.consumerSleeps);
        if(!interrupted_.load())consumerEv_.wait(ms);
        sleeping_.store(false);
        auto* h=peek();if(h)timedSpinSkip_=0;
        return h;
    }
    // Marks the command returned by next() executed: releases its Block, advances replayedSeq, recycles a finished chunk.
    void retire(const CommandHeader* h){
        assert(h==reinterpret_cast<const CommandHeader*>(rchunk_->data+rpos_));
        const std::uint16_t fl=h->flags;   // h dies with its chunk (advance() below)
        if(fl&kFlagBlock)freeBlock(blockOf(h));
        rpos_+=h->size;
        const std::uint64_t n=replayed_.load(std::memory_order_relaxed)+1;
        // 0.3.196 (task 12): release store (the Present ring and everything the replay thread wrote for this command is visible to a producer that reads replayed_ seq_cst), no per-command locked
        // instruction. Dekker with waitReplayed (waitSeq_ store -> replayed_ load): only a command the producer may wait for pays the fence before it reads waitSeq_.
        replayed_.store(n,std::memory_order_release);
        if(fl&kFlagWaitTarget)std::atomic_thread_fence(std::memory_order_seq_cst);
        advance();
        // 0.3.200 (pipeline): the signal is remembered (lastSignalledSeq_/bpSignalledRec_, as wakeWaiters does), so neither a later retire nor the idle backstop
        // signals the same wait again while the producer has not yet run (a SetEvent each); the backstop's forced pre-sleep signal stays.
        const std::uint64_t w=waitSeq_.load();
        if(w&&n>=w){if(w!=lastSignalledSeq_){lastSignalledSeq_=w;progress_.set();}}
        else if(bpWaiting_.load()){const std::uint64_t rec=recorded_.load();if(n>=rec&&rec!=bpSignalledRec_){bpSignalledRec_=rec;progress_.set();}}   // drained: nothing more to free, let the producer re-evaluate
    }
    std::uint64_t replayedSeq()const{return replayed_.load();}
    // 0.3.206 (task 31): the replay loop, between two commands: runs a posted express task (one acquire load when none).
    void serveExpress(std::uint16_t lastCmd){   // lastCmd: the command the replay loop executed last, or kExpressIdle when it slept since
        ExpressTask* t=express_.load(std::memory_order_acquire);
        if(!t||(t->ready&&!t->ready(t->arg)))return;
        t->behindCmd=lastCmd;if(t->timed)t->startNs=nowNs();
        t->fn(t->arg);
        express_.store(nullptr,std::memory_order_release);   // t dies with the poster's wait
        expressDone_.set();
    }
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
