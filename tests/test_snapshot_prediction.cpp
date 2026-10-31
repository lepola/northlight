// 0.3.181 (r89 S1-S3): the snapshot cache's lookup modes. Three Frames (Map, Predict, Prefetch) read the
// same draws of the same fake devices in lockstep; every read's result, diagnostics, Mesh identity (by
// creation ordinal), output Mesh state, counters and LRU order must be equal. Directed liveness cases
// D1-D5 run under ASan. Built by test_snapshot_prediction.py, which also builds the counterfactuals
// (NORTHLIGHT_SNAPSHOT_COUNTERFACTUAL 1-7) and the 4-bit hash variant.
#include "snapshot_harness.h" /* the d3d9 stub, draw_snapshot.h and the fake Buffer/Declaration/Device */
#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <new>
#include <random>
#include <string>
#include <vector>

static std::size_t allocations=0;
void* operator new(std::size_t n){++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}

namespace NorthlightDrawSnapshot {
struct SnapshotTestAccess {
    using Key=Frame::CacheKey;
    static constexpr std::size_t size=sizeof(Key);
    static bool equal(const Key& a,const Key& b){return a==b;}
};
}
using Access=NorthlightDrawSnapshot::SnapshotTestAccess;

// Providers over one global registry: identity tokens, revisions (0: untracked or a Lock in flight).
static std::map<void*,std::uint64_t> tokens,revisions;
static std::uint64_t identity(void* p,bool){auto it=tokens.find(p);return it==tokens.end()?0:it->second;}
static std::uint64_t version(void* p,bool){auto it=revisions.find(p);return it==revisions.end()?0:it->second;}
static void metadata(const NorthlightCaptureMetadata::Request* requests,NorthlightCaptureMetadata::Info* out,unsigned count){
    for(unsigned i=0;i<count;++i){out[i]={};void* b=requests[i].buffer;if(!b||!tokens.count(b))continue;
        out[i].identity=tokens[b];out[i].revision=version(b,requests[i].index);out[i].known=true;
        if(requests[i].index){auto* ib=static_cast<Buffer<IDirect3DIndexBuffer9,D3DINDEXBUFFER_DESC>*>(static_cast<IDirect3DIndexBuffer9*>(b));
            out[i].size=UINT(ib->bytes.size());out[i].usage=ib->desc.Usage;out[i].format=ib->desc.Format;}
        else{auto* vb=static_cast<Buffer<IDirect3DVertexBuffer9,D3DVERTEXBUFFER_DESC>*>(static_cast<IDirect3DVertexBuffer9*>(b));
            out[i].size=UINT(vb->bytes.size());out[i].usage=vb->desc.Usage;}}
}
static std::uint64_t nextRevision=100,nextToken=1;
enum class Kind {Tracked,TrackedDynamic,UntrackedStatic,UntrackedDynamic};
struct Actor {
    Device d;Declaration other; /* the same layout, another declaration object */
    Kind kind;unsigned submeshes;
    explicit Actor(Kind k,unsigned n,std::mt19937& random):kind(k),submeshes(n){
        std::vector<std::uint32_t> indices;for(unsigned i=0;i<n*6;++i)indices.push_back(random()%12);
        d.ib.desc.Format=D3DFMT_INDEX16;d.ib.bytes.resize(indices.size()*2);for(unsigned i=0;i<indices.size();++i){std::uint16_t v=std::uint16_t(indices[i]);std::memcpy(d.ib.bytes.data()+2*i,&v,2);}
        for(unsigned s=0;s<2;++s)for(auto& b:d.vb[s].bytes)b=std::uint8_t(random());
        const bool dynamic=k==Kind::TrackedDynamic||k==Kind::UntrackedDynamic;
        for(unsigned s=0;s<2;++s)d.vb[s].desc.Usage=dynamic?D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY:D3DUSAGE_WRITEONLY;d.ib.desc.Usage=dynamic?D3DUSAGE_DYNAMIC:0;
        for(void* b:buffers()){tokens[b]=nextToken++;revisions[b]=k==Kind::Tracked||k==Kind::TrackedDynamic?nextRevision++:0;}
    }
    std::vector<void*> buffers(){return {static_cast<IDirect3DVertexBuffer9*>(&d.vb[0]),static_cast<IDirect3DVertexBuffer9*>(&d.vb[1]),static_cast<IDirect3DIndexBuffer9*>(&d.ib)};}
    Draw draw(unsigned submesh,unsigned variant=0)const{return Draw{D3DPT_TRIANGLELIST,0,0,12+(variant>>1),submesh*6,2-(variant&1),true};} /* keys differing in draw arguments only */
};
struct Probe {bool ok;Error error;HRESULT hr;UINT stream;long ordinal;std::size_t capacity;UINT strides[4],vertexCount,primitives;bool indexed,dynamic;
    std::vector<unsigned long long> counters;std::vector<long> lru;
    bool operator==(const Probe& o)const{return ok==o.ok&&error==o.error&&hr==o.hr&&stream==o.stream&&ordinal==o.ordinal&&capacity==o.capacity&&!std::memcmp(strides,o.strides,sizeof strides)&&
        vertexCount==o.vertexCount&&primitives==o.primitives&&indexed==o.indexed&&dynamic==o.dynamic&&counters==o.counters&&lru==o.lru;}};
struct Lane {
    Frame frame;Mesh output;std::map<const Mesh*,long> ordinals;std::vector<std::shared_ptr<const Mesh>> keep;
    unsigned long long predictHits=0,predictTried=0,fastHits=0,stableHits=0,stableFast=0,collisions=0;
    Lane(Frame::Lookup mode,std::size_t budget):frame(budget,budget/2){frame.setIdentityProvider(identity);frame.setVersionProvider(version);frame.setMetadataProvider(metadata);frame.setLookup(mode);frame.setNearReserve(budget/4);}
    long ordinal(const Mesh* m){if(!m)return -1;auto it=ordinals.find(m);if(it!=ordinals.end())return it->second;const long n=long(ordinals.size());ordinals[m]=n;return n;}
    Probe read(Actor& a,const Draw& draw,bool other,bool priority,bool nearby){
        std::shared_ptr<const Mesh> shared;Diagnostics why;Probe p{};
        p.ok=frame.read(&a.d,other?static_cast<IDirect3DVertexDeclaration9*>(&a.other):&a.d.decl,draw,output,&why,priority,&shared,nearby);
        if(shared){keep.push_back(shared);}
        p.error=why.error;p.hr=why.hr;p.stream=why.stream;p.ordinal=ordinal(shared.get());p.capacity=output.capacityBytes();
        for(unsigned s=0;s<4;++s)p.strides[s]=output.streams[s].stride;p.vertexCount=output.vertexCount;p.primitives=output.primitiveCount;p.indexed=output.indexed;p.dynamic=output.dynamic;
        const Frame& f=frame;
        p.counters={f.snapshotCacheHits(),f.trackedHits(),f.avoidedReadBytes(),f.snapshotCacheMisses(),f.snapshotRevalidated(),f.snapshotRevalidationMismatches(),f.metadataBatches(),f.metadataHits(),
            f.metadataFallbacks(),f.fastCacheHits(),f.indexCacheHits(),f.indexCacheMisses(),f.bytesRead(),f.sourceSpanVertices(),f.uniqueVertices(),f.savedVertexBytes(),f.compactedDraws(),
            f.snapshotCacheBytes(),f.snapshotCacheEntries(),f.indexCacheBytes(),f.indexCacheEntries()};
        f.visitLRU([&](const Mesh* m){p.lru.push_back(ordinal(m));});
        return p;
    }
    void endFrame(bool stable){
        predictHits+=frame.predictHits();predictTried+=frame.predictTried();fastHits+=frame.fastCacheHits();collisions+=frame.maintenance().missCollision;
        if(stable){stableHits+=frame.predictHits();stableFast+=frame.fastCacheHits();}
        frame.clearFrame();frame.sampleMaintenance(true);
    }
};
static bool sameMaintenance(const Frame& a,const Frame& b){
    const auto& x=a.maintenance();const auto& y=b.maintenance(); /* snapshotMs/indexMs are wall times */
    return x.snapshotEvictions==y.snapshotEvictions&&x.indexEvictions==y.indexEvictions&&x.missEvicted==y.missEvicted&&x.missRevision==y.missRevision&&
        x.missUnknown==y.missUnknown&&x.missCollision==y.missCollision&&x.snapshotEvictedBytes==y.snapshotEvictedBytes&&x.indexEvictedBytes==y.indexEvictedBytes;
}
[[noreturn]] static void mismatch(const char* what,unsigned frame,unsigned read,unsigned lane){
    std::fprintf(stderr,"MISMATCH %s frame=%u read=%u lane=%u\n",what,frame,read,lane);std::abort();}

// The differential: >= 2000 frames x about 50 reads over stable, permuted and repeated orders, per-actor
// submesh groups, the same key twice, write Locks and Locks in flight, DYNAMIC buffers, untracked static
// rewrites (the revalidation erase), a small cap, cap lowering, clearIndexCache and provider resets,
// budget exhaustion with the near-reserve second read, and the draw-count limits.
static void differential(unsigned seed,bool& stableRateOk){
    std::mt19937 random(seed);std::vector<std::unique_ptr<Actor>> actors;
    const Kind kinds[]={Kind::Tracked,Kind::Tracked,Kind::Tracked,Kind::TrackedDynamic,Kind::UntrackedStatic,Kind::UntrackedDynamic,Kind::Tracked,Kind::Tracked};
    for(unsigned i=0;i<10;++i)actors.push_back(std::make_unique<Actor>(kinds[i%8],3+i%4,random));
    constexpr std::size_t Budget=4096,Wide=8u*1024u*1024u; /* Budget: the exhaustion segment */
    Lane lanes[3]={Lane(Frame::Lookup::Map,Budget),Lane(Frame::Lookup::Predict,Budget),Lane(Frame::Lookup::Prefetch,Budget)};
    struct Item {unsigned actor,submesh,variant;bool other;};std::vector<Item> base;
    for(unsigned a=0;a<actors.size();++a)for(unsigned s=0;s<actors[a]->submeshes;++s)base.push_back({a,s,0,false});
    unsigned reads=0,failures=0,budgetFailures=0,nearReads=0;std::vector<void*> inFlight;
    for(auto& l:lanes)l.frame.configureBudget(Wide,Wide/2);
    for(unsigned frame=0;frame<2200;++frame){
        const bool exhaust=frame>=1400&&frame<1500; /* byte budget exhaustion; priority draws retry from the near reserve */
        for(auto& l:lanes){bool ok=l.frame.configureBudget(exhaust?Budget:Wide,(exhaust?Budget:Wide)/2);assert(ok);(void)ok;}
        const bool stable=frame>=20&&frame<400; /* no events, one order */
        std::vector<Item> order=base;
        if(!stable){
            const unsigned style=(frame/50)%4;
            if(style==1)std::shuffle(order.begin(),order.end(),random);
            else if(style==2){for(auto& item:order)item.variant=random()%3;std::shuffle(order.begin(),order.end(),random);} /* draw args differ */
            else if(style==3){std::vector<Item> repeated;for(auto& item:order){repeated.push_back(item);if(random()%4==0)repeated.push_back(item);if(random()%9==0){Item o=item;o.other=true;repeated.push_back(o);}}order=repeated;}
            // Events between frames: write Locks (a new revision), a Lock in flight for this frame (revision 0:
            // untracked until its Unlock), untracked static rewrites (the revalidation erase).
            for(unsigned e=0;e<3;++e){auto& a=*actors[random()%actors.size()];const unsigned what=random()%10;auto buffers=a.buffers();void* b=buffers[random()%3];
                if(what<3&&revisions[b]){a.d.vb[0].bytes[random()%a.d.vb[0].bytes.size()]^=0x11;revisions[buffers[0]]=nextRevision++;}
                else if(what==3&&revisions[b]){revisions[b]=0;inFlight.push_back(b);}
                else if(what==5&&a.kind==Kind::UntrackedStatic)a.d.vb[1].bytes[random()%a.d.vb[1].bytes.size()]^=0x22;
            }
            if(frame>=1200&&frame<1300&&frame%10==0){const std::size_t cap=6000+random()%4000;for(auto& l:lanes)l.frame.setSnapshotCacheLimit(cap);} /* small cap: forced evictions */
            if(frame==1300)for(auto& l:lanes)l.frame.setSnapshotCacheLimit(Frame::SnapshotCacheLimit);
        }
        const bool limits=frame>=2150&&frame<2156; /* the 4096/3072 draw-count limits */
        if(limits){std::vector<Item> many;while(many.size()<4300)for(auto& item:base)many.push_back(item);order=many;}
        const unsigned midEvent=stable?~0u:unsigned(random()%(order.size()*4)); /* sometimes inside the frame */
        const unsigned midKind=random()%4;
        for(unsigned r=0;r<order.size();++r,++reads){
            if(r==midEvent){for(auto& l:lanes){if(midKind==0)l.frame.clearIndexCache();else if(midKind==1)l.frame.setVersionProvider(version);
                else if(midKind==2)l.frame.setMetadataProvider(metadata);else l.frame.setSnapshotCacheLimit(l.frame.snapshotCacheBytes()/2);}}
            const Item& item=order[r];auto& a=*actors[item.actor];const Draw draw=a.draw(item.submesh,item.variant);
            const bool priority=limits?r%2==0:item.actor%3!=2;
            Probe p[3];for(unsigned k=0;k<3;++k)p[k]=lanes[k].read(a,draw,item.other,priority,false);
            failures+=!p[0].ok;
            budgetFailures+=!p[0].ok&&p[0].error==Error::Budget;
            if(!p[0].ok&&p[0].error==Error::Budget&&priority){for(unsigned k=0;k<3;++k)p[k]=lanes[k].read(a,draw,item.other,priority,true);nearReads+=p[0].ok;} /* the near reserve */
            for(unsigned k=1;k<3;++k)if(!(p[k]==p[0]))mismatch("read",frame,r,k);
        }
        for(unsigned k=1;k<3;++k)if(!sameMaintenance(lanes[0].frame,lanes[k].frame))mismatch("maintenance",frame,0,k);
        for(auto& l:lanes)l.frame.setSnapshotCacheLimit(Frame::SnapshotCacheLimit); /* the mid-frame lowering is one frame's */
        for(auto& l:lanes)l.endFrame(stable);
        for(void* b:inFlight)revisions[b]=nextRevision++; /* the Unlock */inFlight.clear();
        if(frame>=1200&&frame<1300&&frame%10!=9){const std::size_t cap=6000+random()%4000;for(auto& l:lanes)l.frame.setSnapshotCacheLimit(cap);}
    }
    assert(lanes[0].predictTried==0&&lanes[1].predictHits>0&&lanes[2].predictHits>0);
    assert(lanes[1].predictHits==lanes[2].predictHits&&lanes[1].predictTried==lanes[2].predictTried);
    const double rate=double(lanes[1].stableHits)/double(std::max(1ull,lanes[1].stableFast));stableRateOk=rate>=0.9;
    std::printf("differential seed=%u: %u reads x 3 lanes equal (%u failed, %u over the budget, %u near-reserve reads); predict tried=%llu hits=%llu fastHits=%llu; stable-order rate %.3f\n",seed,reads,failures,budgetFailures,nearReads,lanes[1].predictTried,lanes[1].predictHits,lanes[1].fastHits,rate);
    assert(!NORTHLIGHT_SNAPSHOT_HASH_BITS||lanes[0].collisions>1000); /* same-hash misses and replacements happen */
    assert(failures>100&&budgetFailures>100&&nearReads>50&&(NORTHLIGHT_SNAPSHOT_HASH_BITS||lanes[1].fastHits>reads/4)); /* 4-bit hashes: at most 16 entries */
}

// Directed liveness cases (Predict and Prefetch). Without the reset at the erase site, the next tracked
// read dereferences the freed predicted entry: ASan's heap-use-after-free (CF1-CF4).
struct Directed {
    std::mt19937 random{7};std::vector<std::unique_ptr<Actor>> actors;Frame frame;Mesh output;
    explicit Directed(Frame::Lookup mode,unsigned count=4,Kind kind=Kind::Tracked){
        frame.setIdentityProvider(identity);frame.setVersionProvider(version);frame.setMetadataProvider(metadata);frame.setLookup(mode);
        for(unsigned i=0;i<count;++i)actors.push_back(std::make_unique<Actor>(i==count-1?kind:Kind::Tracked,1,random));
    }
    std::vector<const Mesh*> meshes;
    const Mesh* read(unsigned a,unsigned variant=0){std::shared_ptr<const Mesh> shared;Diagnostics why;
        const bool ok=frame.read(&actors[a]->d,&actors[a]->d.decl,actors[a]->draw(0,variant),output,&why,true,&shared);assert(ok&&shared);(void)ok;return shared.get();}
    void storeAll(unsigned n){meshes.clear();for(unsigned a=0;a<n;++a)meshes.push_back(read(a));frame.clearFrame();}
    std::vector<const Mesh*> lru()const{std::vector<const Mesh*> v;frame.visitLRU([&](const Mesh* m){v.push_back(m);});return v;}
};
static void directed(){
    for(auto mode:{Frame::Lookup::Predict,Frame::Lookup::Prefetch}){
        if(!NORTHLIGHT_SNAPSHOT_HASH_BITS){ /* D1-D3, D5 with full hashes (no store replaces another) */
        {Directed t(mode);t.storeAll(3);const std::size_t three=t.frame.snapshotCacheBytes();   /* D1: A,B,C */
            t.read(0);                                              /* hit A: predicted B; B is now the oldest */
            t.frame.setSnapshotCacheLimit(three);t.read(3);         /* store D evicts B */
            t.read(1);t.read(2);}                                   /* tracked reads: the prediction is dropped */
        {Directed t(mode);t.storeAll(3);t.read(0);                   /* D2: predicted B */
            t.frame.setSnapshotCacheLimit(t.frame.snapshotCacheBytes()-1);t.read(2);}
        {Directed t(mode,3,Kind::UntrackedStatic);t.storeAll(3);     /* D3: A, B tracked, X untracked (newest) */
            t.read(0);t.read(1);                                    /* hit B: its successor X is predicted */
            for(auto& byte:t.actors[2]->d.vb[0].bytes)byte^=0x40;const auto mismatches=t.frame.snapshotRevalidationMismatches();
            t.read(2);assert(t.frame.snapshotRevalidationMismatches()==mismatches+1); /* X fails revalidation: erased */
            t.read(0);}
        for(unsigned how=0;how<3;++how){Directed t(mode);t.storeAll(3);t.read(0);  /* D5 */
            if(how==0)t.frame.clearIndexCache();else if(how==1)t.frame.setVersionProvider(version);else t.frame.setMetadataProvider(metadata);
            t.read(1);}
        }
#if NORTHLIGHT_SNAPSHOT_HASH_BITS
        {Directed t(mode,6);t.storeAll(4);const auto order=t.lru();assert(order.size()>=2); /* D4 (4-bit hashes) */
            const unsigned a=unsigned(std::find(t.meshes.begin(),t.meshes.end(),order[0])-t.meshes.begin());assert(a<4);
            const auto hits=t.frame.snapshotCacheHits();t.read(a);assert(t.frame.snapshotCacheHits()==hits+1); /* hit A: order[1] predicted */
            bool replaced=false;
            for(unsigned v=0;v<400&&!replaced;++v){t.read(4+v%2,v+1);const auto now=t.lru();replaced=std::find(now.begin(),now.end(),order[1])==now.end();}
            assert(replaced);t.read(a);}                            /* a tracked read: the prediction is dropped */
#endif
    }
    std::puts(NORTHLIGHT_SNAPSHOT_HASH_BITS?"directed D4: same-hash replacement of the predicted entry; the prediction is dropped":"directed D1-D3, D5: evict (store, cap lowering), revalidation erase, clears; the prediction is dropped at every erase");
}
// S1: == equals memcmp()==0 for random keys and every single-byte difference.
static void equality(){
    std::mt19937 random(181);unsigned checked=0;
    for(unsigned round=0;round<200;++round){Access::Key a,b;auto* pa=reinterpret_cast<unsigned char*>(&a);
        for(std::size_t i=0;i<Access::size;++i)pa[i]=std::uint8_t(random()%3?0:random());std::memcpy(static_cast<void*>(&b),&a,Access::size);
        assert(Access::equal(a,b)==(std::memcmp(&a,&b,Access::size)==0));
        auto* pb=reinterpret_cast<unsigned char*>(&b);
        for(std::size_t i=0;i<Access::size;++i){const unsigned char keep=pb[i];pb[i]^=std::uint8_t(1u<<(random()%8));
            if(Access::equal(a,b)!=(std::memcmp(&a,&b,Access::size)==0)){std::fprintf(stderr,"MISMATCH equality offset=%zu\n",i);std::abort();}pb[i]=keep;++checked;}}
    std::printf("equality: %u single-byte flips over %zu-byte keys agree with memcmp\n",checked,Access::size);
}
// S3: the meter against a synthetic clock (every read costs readNs).
struct SynthClock {using rep=std::int64_t;using period=std::nano;using duration=std::chrono::nanoseconds;using time_point=std::chrono::time_point<SynthClock>;
    static constexpr bool is_steady=true;static std::int64_t nowNs,readNs,reads;static time_point now(){++reads;nowNs+=readNs/2;const time_point t{duration(nowNs)};nowNs+=readNs-readNs/2;return t;}};
std::int64_t SynthClock::nowNs=0,SynthClock::readNs=0,SynthClock::reads=0;
static void meter(){
    using Meter=SnapshotMeter<SynthClock>;Meter m;std::mt19937 random(3);
    unsigned expectedFrames[3][3]={};
    for(unsigned f=0;f<240;++f){
        SynthClock::readNs=100+random()%200;const unsigned mode=f%3,phase=f;m.beginFrame(true,phase);
        const unsigned records=40+random()%200;unsigned timed=0;
        for(unsigned k=0;k<records;++k){const std::int64_t w=300+random()%200;
            const unsigned before=m.spans();
            {SnapshotSpan<Meter> span(m);SynthClock::nowNs+=w;if(k%5)span.end();} /* k%5==0: an early return, the destructor ends it */
            assert((m.spans()-before==1)==((k+phase)%16==0));
            timed+=(k+phase)%16==0;}
        assert(m.spans()==timed);
        const unsigned fastHits=f%7==0?100:f%7==1?300:700;const unsigned c=Meter::classOf(fastHits);
        if(timed>=Meter::MinSpans)++expectedFrames[mode][c];
        m.endFrame(mode,fastHits,10,8,2);
    }
    unsigned classes=0;
    m.report([&](unsigned c,const Meter::Summary& s){++classes;for(unsigned mode=0;mode<3;++mode){assert(s.frames[mode]==expectedFrames[mode][c]);
        if(s.frames[mode])assert(s.median[mode]>=300-5&&s.median[mode]<=500+5&&s.p25[mode]<=s.median[mode]&&s.median[mode]<=s.p75[mode]);}
        assert(s.pairNs>=100&&s.pairNs<300&&s.predictHits*10==s.predictTried*8);});
    assert(classes>=2);
    Meter off;off.beginFrame(false,0);const auto before=SynthClock::reads;for(unsigned k=0;k<100;++k){SnapshotSpan<Meter> span(off);}assert(SynthClock::reads==before&&off.spans()==0);
    std::puts("meter: stride-16 phase selection, pair cost removed (medians within the true 300-500 ns work), early-return spans, per-(mode,class) buckets; no clock read when off");
}
// A hit allocates nothing, in every mode.
static void hitAllocations(){
    if(NORTHLIGHT_SNAPSHOT_HASH_BITS)return; /* 4-bit hashes: stores replace each other, so not every read hits */
    for(auto mode:{Frame::Lookup::Map,Frame::Lookup::Predict,Frame::Lookup::Prefetch}){
        Directed t(mode,6);t.storeAll(6);for(unsigned a=0;a<6;++a)t.read(a);t.frame.clearFrame();
        const auto before=allocations;for(unsigned round=0;round<5;++round){for(unsigned a=0;a<6;++a)t.read(a);t.frame.clearFrame();}
        assert(allocations==before);}
    std::puts("hits allocate nothing in Map, Predict and Prefetch");
}
static void retainedCopies(){
    Device d;Frame reused(4096,0),up(4096,0);Mesh retained,retainedUp;
    for(auto& stream:retained.streams)stream.bytes.reserve(8192);retainedUp.streams[0].bytes.reserve(8192);
    Declaration single;single.e.erase(single.e.begin()+1);
    for(unsigned frame=0;frame<32;++frame){
        for(unsigned stream=0;stream<2;++stream)retained.streams[stream].bytes.assign(frame%3?512:0,0xa5);
        retainedUp.streams[0].bytes.assign(frame%3?512:0,0xa5);
        for(unsigned stream=0;stream<2;++stream)for(auto& byte:d.vb[stream].bytes)byte=std::uint8_t(byte+frame+stream+1);
        Draw draw{D3DPT_TRIANGLELIST,0,0,12,frame%6,2,false};reused.clearFrame();
        const auto allocationsBefore=allocations;assert(reused.read(&d,&d.decl,draw,retained));assert(allocations==allocationsBefore);
        Frame fresh(4096,0);Mesh expected;assert(fresh.read(&d,&d.decl,draw,expected));assert(reused.bytesRead()==fresh.bytesRead());
        for(unsigned stream=0;stream<4;++stream)assert(retained.streams[stream].bytes==expected.streams[stream].bytes);
        assert(retained.vertexCount==expected.vertexCount&&retained.streams[0].bytes.capacity()==8192);
        draw.start=0;up.clearFrame();const auto* input=d.vb[0].bytes.data()+d.offset[0];
        const auto beforeUp=allocations;assert(up.readUP(&single,draw,nullptr,D3DFMT_INDEX16,input,d.stride[0],retainedUp));assert(allocations==beforeUp);
        Mesh expectedUp;Frame originalUp(4096,0);assert(originalUp.readUP(&single,draw,nullptr,D3DFMT_INDEX16,input,d.stride[0],expectedUp));
        assert(retainedUp.streams[0].bytes==expectedUp.streams[0].bytes&&up.bytesRead()==originalUp.bytesRead());
    }
    std::puts("retained contiguous snapshots: 64 byte-exact VB/UP draws, poisoned growth/tails, stable capacities, zero allocations, equal budgets");
}
int main(int argc,char** argv){
    assert(Frame{}.lookup()==Frame::Lookup::Prefetch);
    const std::string mode=argc>1?argv[1]:"all";
    if(mode=="directed"){directed();return 0;}
    if(mode=="differential"){bool ok=false;differential(1811,ok);return 0;}
    equality();meter();hitAllocations();retainedCopies();directed();
    bool ok1=false,ok2=false;differential(1811,ok1);differential(90181,ok2);
    assert((ok1&&ok2)||NORTHLIGHT_SNAPSHOT_HASH_BITS);
    std::puts("PASS snapshot prediction: Map, Predict and Prefetch equal per read; liveness cases clean");
}
