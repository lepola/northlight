// 0.3.177 (r83 a1-prepare): prepare_worker.h. prepareRecord() over random capture frames with the real
// SampledVertexCache/RigidBoneCache, synthetic one-influence palette programs (test_actor_deformation's
// generator) and the client's one-influence and four-bone programs: groups of 1-8 draws, shared and owned
// snapshots, rigid and multi-bone meshes, unknown programs, undeclared layouts, repeated shader and
// declaration, declaration copies taken through the real 128-slot declaration cache.
//   - prepareRecord equals a verbatim copy of the 0.3.176 selectStableActors loop (draws, stored rigid
//     bones, distance counters) over the same records and the live declaration cache.
//   - Handover at every index (worker prefix, inline tail from the last output's state, the same caches)
//     equals fully inline, bit for bit, distance counters included; cold and pre-warmed caches agree.
//   - The threaded Worker: random publish delays, stops before the first publish, mid-record and after
//     completion, records never published (prepared by the joiner), the context camera changed after
//     the frame opened: equal to inline. An exception on record j and a stalled record (the watchdog)
//     are taken over inline with equal outputs.
//   - The renderer's prepare block (fill, publish, join, consumption, quiesce, endFrame), pasted from
//     world_shadow_experiment.inl, equals inline over random frames with delays and A/B windows.
//   - The watchdog re-arm: a settled abandoned worker returns after 10 s, at most 4 times a session.
//   - 0.3.183: an abandoned worker held in its record for 32 more frames: only the abandoned frame's replays
//     (a constant donor never published and a replay moved to held included), one arena and the programs
//     pinned at the abandon stay held; everything goes at settle.
//   - Counterfactuals that must fail: the carried state reset at the handover, unfiltered records
//     published, declaration elements read through the live cache after an eviction, the camera taken
//     at the join; (0.3.183) every replay held while unsettled, only the published ones held, the programs
//     not pinned.
// Built by test_prepare_worker.py (O2, ASan+UBSan, TSan). Native, no game or GPU.
#include "stream_hooks.h"
#include "prepare_worker.h"
#include "vertex_declaration_cache.h"
#include "actor_client_programs.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
using namespace NorthlightActorDeformation;
/*SYNTHETIC_PROGRAMS*/
using NorthlightDrawSnapshot::Mesh;using NorthlightActorPrepare::Output;using NorthlightActorPrepare::State;using NorthlightActorPrepare::Caches;
struct IDirect3DVertexShader9;
static Program compiled(const std::uint32_t* words,std::size_t n){Program p;bool ok=compile(words,n,p);assert(ok);(void)ok;return p;}
#define CLIENT_PROGRAM(name) compiled(ClientShaders::name,sizeof ClientShaders::name/4)
// A captured record as the renderer's Replay holds it (the fields prepareRecord reads).
struct Rec {
    unsigned constantGroup=0;IDirect3DVertexShader9* originalShader=nullptr;IDirect3DVertexDeclaration9* decl=nullptr;
    std::shared_ptr<const Mesh> shared;Mesh snapshot;const float* constants=nullptr;std::vector<float> storage;
    const Program* program=nullptr; /* 0.3.179 (T1): the capture metadata's program */
    std::uint64_t prepareStamp=0; /* 0.3.183: the prepare epoch at capture */
    const NorthlightActorPrepare::DeclCopy* declCopy=nullptr; /* 0.3.179 (T2): the frame's copy; null: the per-record copy */
    std::array<D3DVERTEXELEMENT9,MAXD3DDECLLENGTH+1> elements{};UINT elementCount=0;bool declared=false;
    bool shadowSelected=true,shadowSkinned=true,boneKnown=false;float bone=NAN;
    const Mesh& mesh()const{return shared?*shared:snapshot;}
    const D3DVERTEXELEMENT9* declarationElements()const{return declCopy?declCopy->elements:elements.data();}
    UINT declarationCount()const{return declCopy?declCopy->count:elementCount;}
};
// The 0.3.178 record layout: a program handle and a per-record declaration copy.
struct RecR83 {
    unsigned constantGroup=0;IDirect3DVertexShader9* originalShader=nullptr;IDirect3DVertexDeclaration9* decl=nullptr;
    std::shared_ptr<const Mesh> shared;Mesh snapshot;const float* constants=nullptr;
    std::shared_ptr<const Program> program;std::array<D3DVERTEXELEMENT9,MAXD3DDECLLENGTH+1> elements{};UINT elementCount=0;bool declared=false;
    const Mesh& mesh()const{return shared?*shared:snapshot;}
    const D3DVERTEXELEMENT9* declarationElements()const{return elements.data();}
    UINT declarationCount()const{return elementCount;}
};
struct FakeDecl final:IDirect3DVertexDeclaration9 {
    unsigned refs=1;std::vector<D3DVERTEXELEMENT9> layout;
    unsigned AddRef()override{return ++refs;}unsigned Release()override{return --refs;}
    HRESULT GetDeclaration(D3DVERTEXELEMENT9* out,UINT* n)override{if(*n<layout.size())return D3DERR_INVALIDCALL;std::copy(layout.begin(),layout.end(),out);*n=UINT(layout.size());return D3D_OK;}
};
static const std::vector<D3DVERTEXELEMENT9> gameLayout={{0,0,2,0,0,0},{0,12,8,0,1,0},{0,16,5,0,2,0},{0xff,0,17,0,0,0}};
static std::vector<Word> load(const char* path){FILE* f=std::fopen(path,"rb");assert(f);std::fseek(f,0,SEEK_END);long size=std::ftell(f);std::rewind(f);
    std::vector<Word> w(size_t(size)/4);size_t got=std::fread(w.data(),1,size_t(size),f);assert(got==size_t(size));(void)got;std::fclose(f);return w;}
// Game layout mesh: float3 position, UBYTE4N weights, UBYTE4 indices; every vertex on `bone`, or (mixed)
// alternating bones with split weights.
static std::shared_ptr<Mesh> mesh(std::mt19937& rng,unsigned vertices,unsigned bone,bool mixed){
    auto m=std::make_shared<Mesh>();m->vertexCount=vertices;m->streams[0].stride=20;m->streams[0].bytes.resize(size_t(vertices)*20);
    for(unsigned v=0;v<vertices;++v){auto* out=m->streams[0].bytes.data()+size_t(v)*20;const float p[3]={float(int(rng()%2000)-1000)*.01f,float(int(rng()%2000)-1000)*.01f,float(rng()%300)*.01f};
        std::memcpy(out,p,12);const bool split=mixed&&v%2;out[12]=split?128:255;out[13]=split?127:0;out[14]=out[15]=0;
        out[16]=std::uint8_t(bone);out[17]=std::uint8_t(split?bone+1:bone);out[18]=out[19]=0;}
    m->indexed=true;m->topology=D3DPT_TRIANGLELIST;for(unsigned t=0;t+2<vertices;++t)for(unsigned k=0;k<3;++k)m->indices.push_back(t+k);
    m->primitiveCount=unsigned(m->indices.size()/3);return m;
}
struct World {
    std::vector<std::shared_ptr<const Program>> programs; /* per shader slot; null: not an actor program */
    std::vector<FakeDecl> decls=std::vector<FakeDecl>(2),many=std::vector<FakeDecl>(200); /* many: more than an arena holds */
    NorthlightActorPrepare::DeclArena arena;bool manyDecls=false;
    NorthlightVertexDeclarations::Cache declarationCache;std::vector<std::shared_ptr<Mesh>> pool;
    static IDirect3DVertexShader9* shader(unsigned k){return reinterpret_cast<IDirect3DVertexShader9*>(std::uintptr_t(0x1000+32*k));}
    explicit World(const char* fourBone){
        for(unsigned major:{3u,2u,1u}){auto w=code(major);programs.push_back(std::make_shared<const Program>(compiled(w.data(),w.size())));}
        {auto w=load(fourBone);programs.push_back(std::make_shared<const Program>(compiled(w.data(),w.size())));}
        programs.push_back(std::make_shared<const Program>(CLIENT_PROGRAM(OneBoneVs3)));programs.push_back(nullptr);
        assert(NorthlightReplayBounds::SkinEnvelope::supports(*programs[3])&&programs[3]->paletteBase==31);
        decls[0].layout=gameLayout;decls[1].layout={{0,0,2,0,0,0},{0xff,0,17,0,0,0}}; /* no BLENDINDICES */
        for(unsigned i=0;i<many.size();++i)many[i].layout=i%3?gameLayout:decls[1].layout;
        std::mt19937 rng(177);for(unsigned i=0;i<48;++i)pool.push_back(mesh(rng,3+rng()%24,rng()%6,rng()%4==0));
    }
    std::shared_ptr<const Program> handle(const Rec& r)const{return programs[(reinterpret_cast<std::uintptr_t>(r.originalShader)-0x1000)/32];}
    // The capture-side fill (the renderer's prepareFill): the program, the frame's declaration copy or,
    // when the arena is full, a per-record copy.
    void fill(Rec& r){
        r.program=handle(r).get();r.declCopy=nullptr;r.elementCount=0;
        const auto* copy=r.program?arena.find(r.decl,[this](IDirect3DVertexDeclaration9* d,const D3DVERTEXELEMENT9*& e,UINT& n){return declarationCache.get(d,e,n);}):nullptr;
        if(copy){r.declCopy=copy;r.declared=copy->declared;return;}
        const D3DVERTEXELEMENT9* e=nullptr;UINT n=0;r.declared=r.program&&declarationCache.get(r.decl,e,n)&&n<=r.elements.size();
        if(r.declared){std::copy(e,e+n,r.elements.begin());r.elementCount=n;}
    }
    // The same record in the 0.3.178 layout (fill: a handle and a copy through the live cache).
    RecR83 r83(const Rec& r){RecR83 o;o.constantGroup=r.constantGroup;o.originalShader=r.originalShader;o.decl=r.decl;o.shared=r.shared;o.snapshot=r.snapshot;o.constants=r.constants;
        o.program=handle(r);const D3DVERTEXELEMENT9* e=nullptr;UINT n=0;o.declared=o.program&&declarationCache.get(o.decl,e,n)&&n<=o.elements.size();
        o.elementCount=o.declared?n:0;if(o.declared)std::copy(e,e+n,o.elements.begin());return o;}
    // A capture frame: constant groups of 1-8 draws in replays order; some neither selected nor skinned.
    std::vector<std::unique_ptr<Rec>> frame(std::mt19937& rng,unsigned groups){
        std::vector<std::unique_ptr<Rec>> out;arena.reset(); /* per frame, as the renderer */
        for(unsigned g=0;g<groups;++g){const unsigned draws=1+rng()%8;const unsigned groupShader=rng()%10<6?rng()%2:rng()%6;
            std::vector<float>* bank=nullptr;
            for(unsigned k=0;k<draws;++k){auto r=std::make_unique<Rec>();r->constantGroup=g+1;
                r->originalShader=shader(rng()%5?groupShader:rng()%6);r->decl=manyDecls&&rng()%2?static_cast<IDirect3DVertexDeclaration9*>(&many[rng()%many.size()]):&decls[rng()%10?0:1];
                if(rng()%4){r->shared=pool[rng()%pool.size()];}else r->snapshot=*mesh(rng,3+rng()%24,rng()%6,rng()%3==0);
                if(!bank||rng()%6==0){r->storage.resize(1024);for(auto& v:r->storage)v=float(int(rng()%2001)-1000)*.001f;
                    r->storage[0]=3;r->storage[1]=1;for(unsigned b=0;b<8;++b){const unsigned row=31+3*b;r->storage[4*row]=1;r->storage[4*(row+1)+1]=1;r->storage[4*(row+2)+2]=1;
                        r->storage[4*row+3]=float(int(rng()%200)-100);r->storage[4*(row+1)+3]=float(int(rng()%200)-100);r->storage[4*(row+2)+3]=float(rng()%50);}
                    bank=&r->storage;}
                r->constants=bank->data(); /* a group shares its bank: earlier records' storage */
                r->shadowSelected=rng()%8!=0;r->shadowSkinned=rng()%10!=0;
                if(r->shadowSelected&&r->shadowSkinned)fill(*r);
                out.push_back(std::move(r));}}
        return out;
    }
};
// ---- the 0.3.176 selectStableActors loop, verbatim (after the program-handle commit): the identity
// reference of prepareRecord. Its renderer members, with the same caches, programs and declaration cache.
struct Reference {
    using Replay=Rec;
    std::vector<std::unique_ptr<Rec>>& replays;std::unordered_map<IDirect3DVertexShader9*,std::shared_ptr<const Program>> actorPrograms;
    NorthlightVertexDeclarations::Cache& declarationCache;struct {float inverseView[16],camera[3];} context;
    NorthlightActorDeformation::SampledVertexCache& sampledVertices;NorthlightActorDeformation::RigidBoneCache& rigidBones;
    std::vector<NorthlightActorShadowSelection::Draw> actorShadowDraws;
    struct {bool stationaryHint(std::uint64_t,const float*){return false;}} actorShadowHistory;
    static NorthlightActorShadowSelection::Tuning selectionTuning(){return NorthlightActorShadowSelection::active();}
    Reference(std::vector<std::unique_ptr<Rec>>& r,NorthlightVertexDeclarations::Cache& d,Caches& c):replays(r),declarationCache(d),sampledVertices(c.sampled),rigidBones(c.bones){}
    void run(std::size_t& distanceTests,std::size_t& distanceReused){actorShadowDraws.clear();
        unsigned previousGroup=UINT_MAX;IDirect3DVertexShader9* previousShader=nullptr;
        IDirect3DVertexDeclaration9* previousDecl=nullptr;float previousDistance=0,previousAt[3]={};bool previousKnown=false,groupRigid=true,groupStationary=false;unsigned groupDraw=0;
        const auto tuning=selectionTuning();
        for(size_t index=0;index<replays.size();++index){const auto& p=*replays[index];
            if(!p.shadowSelected||!p.shadowSkinned)continue;
            NorthlightActorShadowSelection::Draw item;item.index=index;item.bytes=p.mesh().byteSize();item.group=p.constantGroup;
            // Program and declaration lookups only for draws that test a distance
            // or a palette (reused-distance draws of multi-bone groups need none).
            auto program=actorPrograms.end();const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;int declaredState=-1;
            auto declared=[&]{if(declaredState<0){program=actorPrograms.find(p.originalShader);declaredState=program!=actorPrograms.end()&&declarationCache.get(p.decl,elements,count);}return declaredState==1;};
            if(p.constantGroup==previousGroup&&p.originalShader==previousShader&&p.decl==previousDecl){
                item.known=previousKnown;item.distanceSquared=previousDistance;std::memcpy(item.at,previousAt,sizeof item.at);++distanceReused;
            }else{
                ++distanceTests;
                item.known=declared()&&sampledVertices.distance(*program->second,p.mesh(),p.shared,p.decl,elements,count,
                    p.constants,context.inverseView,context.camera,item.distanceSquared,item.at);
            }
            // A group is rigid only if every draw is: after its first multi-bone
            // draw the remaining draws need no palette test. Only a group's first
            // draw supplies the actor identity key.
            const bool first=p.constantGroup!=previousGroup;if(first)groupRigid=true;
            item.bone=groupRigid&&declared()?rigidBones.bone(*program->second,p.mesh(),p.shared,p.decl,elements,count):NAN;item.rigid=!std::isnan(item.bone);
            {Replay& stored=*replays[index];stored.boneKnown=groupRigid&&declared();stored.bone=item.bone;} /* 0.3.176 (S2): rigidObserveGroup reuses it */
            groupRigid=item.rigid;
            if(tuning.stableIdentity){
                // Stable per-draw identity: the snapshot-cache entry (VB/IB identity,
                // range, base, declaration) with the shader; a shape hash only for
                // uncached draws. The actor key is the smallest key of its draws.
                std::uint64_t key=14695981039346656037ull;auto mix=[&](uint64_t n){key=(key^n)*1099511628211ull;};
                mix(reinterpret_cast<uintptr_t>(p.originalShader));mix(reinterpret_cast<uintptr_t>(p.decl));
                if(p.shared)mix(reinterpret_cast<uintptr_t>(p.shared.get()));else{mix(p.mesh().vertexCount);mix(p.mesh().primitiveCount);mix(item.bytes);}
                item.key=key;
                // One palette root per constant group (its draws share the pose).
                if(first){if(program==actorPrograms.end())program=actorPrograms.find(p.originalShader);
                    // Only the audited palette template (c31.. row-major 3x4 bones, translation
                    // in w: the skin-envelope specialization) has a provable root.
                    item.hasRoot=program!=actorPrograms.end()&&NorthlightReplayBounds::SkinEnvelope::supports(*program->second)&&program->second->paletteBase==31&&
                        NorthlightActorDeformation::rootWorld(*program->second,p.constants,context.inverseView,item.root);}}
            else if(first){std::uint64_t key=14695981039346656037ull;auto mix=[&](uint64_t n){key=(key^n)*1099511628211ull;};
                mix(reinterpret_cast<uintptr_t>(p.originalShader));mix(reinterpret_cast<uintptr_t>(p.decl));mix(p.mesh().vertexCount);mix(p.mesh().primitiveCount);mix(item.bytes);
                item.key=key;groupDraw=0;
                // Two extra world samples per draw (first two draws) only for actors
                // the history marks as possibly stationary: the idle-pose check.
                groupStationary=tuning.stationary&&!item.rigid&&item.known&&actorShadowHistory.stationaryHint(key,item.at);}
            if(!tuning.stableIdentity){
                if(groupStationary&&groupDraw<2&&declared())for(unsigned x=0;x<2;++x)
                    if(NorthlightActorDeformation::sampledExtraWorld(*program->second,p.mesh(),elements,count,x,p.constants,context.inverseView,item.extra[item.extras]))++item.extras;
                ++groupDraw;}
            previousGroup=p.constantGroup;previousShader=p.originalShader;previousDecl=p.decl;
            previousKnown=item.known;previousDistance=item.distanceSquared;std::memcpy(previousAt,item.at,sizeof previousAt);actorShadowDraws.push_back(item);
        }
    }
};
struct Camera {float inverseView[16],camera[3];};
static Camera camera(std::mt19937& rng){Camera c{};const float yaw=float(rng()%628)*.01f;
    c.inverseView[0]=std::cos(yaw);c.inverseView[1]=std::sin(yaw);c.inverseView[4]=-std::sin(yaw);c.inverseView[5]=std::cos(yaw);c.inverseView[10]=1;c.inverseView[15]=1;
    for(unsigned k=0;k<3;++k)c.inverseView[12+k]=c.camera[k]=float(int(rng()%400)-200);return c;}
// The records the renderer publishes: selected and skinned at capture, in replays order, with their index.
struct Published {std::vector<const Rec*> records;std::vector<std::size_t> index;};
static Published published(const std::vector<std::unique_ptr<Rec>>& replays,bool filtered=true){
    Published p;for(std::size_t i=0;i<replays.size();++i)if(!filtered||(replays[i]->shadowSelected&&replays[i]->shadowSkinned)){p.records.push_back(replays[i].get());p.index.push_back(i);}return p;}
// prepareRecord over [from, to) resuming from out[from-1] (from 0: a fresh state).
static void prepare(const Published& p,std::size_t from,std::size_t to,Caches& c,const float* inverse,const float* eye,std::vector<Output>& out,bool resetState=false){
    State s=from&&!resetState?out[from-1].after:State{};
    for(std::size_t k=from;k<to;++k)NorthlightActorPrepare::prepareRecord(s,*p.records[k],p.index[k],c,inverse,eye,out[k]);
}
static bool equal(const std::vector<Output>& a,const std::vector<Output>& b,std::size_t n){for(std::size_t k=0;k<n;++k)if(!NorthlightActorPrepare::same(a[k],b[k]))return false;return true;}
static std::size_t hasRoot=0,rigid=0,known=0,reused=0;
static void stats(const std::vector<Output>& out,std::size_t n){for(std::size_t k=0;k<n;++k){hasRoot+=out[k].item.hasRoot;rigid+=out[k].item.rigid;known+=out[k].item.known;}if(n)reused+=out[n-1].after.distanceReused;}
// Handover at every index; cache independence; the carried-state and filter counterfactuals.
static void handover(World& world,unsigned sequences){
    std::mt19937 rng(1771);std::size_t handovers=0,resetDiffer=0,unfilteredDiffer=0;auto warm=std::make_unique<Caches>();
    for(unsigned q=0;q<sequences;++q){
        auto replays=world.frame(rng,6+rng()%14);const auto p=published(replays);const std::size_t n=p.records.size();const Camera c=camera(rng);
        std::vector<Output> reference(n),out(n),check(n);auto cold=std::make_unique<Caches>();
        prepare(p,0,n,*cold,c.inverseView,c.camera,reference);stats(reference,n);
        prepare(p,0,n,*warm,c.inverseView,c.camera,check);assert(equal(check,reference,n)); /* pre-warmed caches */
        prepare(p,0,n,*warm,c.inverseView,c.camera,check);assert(equal(check,reference,n));
        for(std::size_t k=0;k<=n;++k){auto caches=std::make_unique<Caches>();
            prepare(p,0,k,*caches,c.inverseView,c.camera,out);prepare(p,k,n,*caches,c.inverseView,c.camera,out);
            assert(equal(out,reference,n));++handovers;
            if(k&&k<n){prepare(p,k,n,*caches,c.inverseView,c.camera,out,true);resetDiffer+=!equal(out,reference,n);}}
        // Counterfactual: every record published (small or unskinned too): the selected ones' outputs differ.
        const auto all=published(replays,false);std::vector<Output> every(all.records.size());auto caches=std::make_unique<Caches>();
        prepare(all,0,all.records.size(),*caches,c.inverseView,c.camera,every);
        std::vector<Output> projected;for(std::size_t k=0;k<all.records.size();++k)if(all.records[k]->shadowSelected&&all.records[k]->shadowSkinned)projected.push_back(every[k]);
        unfilteredDiffer+=!equal(projected,reference,n);
    }
    assert(resetDiffer>sequences&&unfilteredDiffer>sequences/4&&hasRoot&&rigid&&known&&reused);
    std::printf("handover: %u sequences, %zu handovers equal to inline (hasRoot=%zu rigid=%zu known=%zu reused=%zu); counterfactuals differ: state reset %zu, unfiltered %zu\n",
        sequences,handovers,hasRoot,rigid,known,reused,resetDiffer,unfilteredDiffer);
}
// Counterfactual: the declaration read through the live cache after 128 other layouts evicted its slot.
static void evictedDeclaration(World& world){
    std::mt19937 rng(1772);NorthlightVertexDeclarations::Cache cache;std::vector<FakeDecl> others(200);
    for(unsigned i=0;i<others.size();++i)others[i].layout={{0,std::uint16_t(4+4*(i%3)),2,0,0,0},{0,16,5,0,2,0},{0xff,0,17,0,0,0}};
    std::size_t differ=0,frames=0;
    for(unsigned q=0;q<40;++q){auto replays=world.frame(rng,4+rng()%6);auto p=published(replays);const std::size_t n=p.records.size();const Camera c=camera(rng);
        std::vector<const D3DVERTEXELEMENT9*> live(n);std::vector<UINT> counts(n);
        for(std::size_t k=0;k<n;++k)if(p.records[k]->declared){bool ok=cache.get(p.records[k]->decl,live[k],counts[k]);assert(ok);(void)ok;}
        for(auto& d:others){const D3DVERTEXELEMENT9* e=nullptr;UINT m=0;cache.get(&d,e,m);} /* 200 > 128 slots: every slot turned over */
        std::vector<Rec> stale(n);for(std::size_t k=0;k<n;++k){stale[k].constantGroup=p.records[k]->constantGroup;stale[k].originalShader=p.records[k]->originalShader;
            stale[k].decl=p.records[k]->decl;stale[k].shared=p.records[k]->shared;stale[k].snapshot=p.records[k]->snapshot;stale[k].constants=p.records[k]->constants;
            stale[k].program=p.records[k]->program;stale[k].declared=p.records[k]->declared;stale[k].elementCount=counts[k];
            if(live[k])std::copy(live[k],live[k]+counts[k],stale[k].elements.begin());}
        Published q2;for(std::size_t k=0;k<n;++k){q2.records.push_back(&stale[k]);q2.index.push_back(p.index[k]);}
        std::vector<Output> reference(n),read(n);auto a=std::make_unique<Caches>(),b=std::make_unique<Caches>();
        prepare(p,0,n,*a,c.inverseView,c.camera,reference);prepare(q2,0,n,*b,c.inverseView,c.camera,read);
        differ+=!equal(read,reference,n);++frames;
        cache.clear();}
    assert(differ>frames/2);std::printf("declaration copy: the live cache read after eviction differs in %zu of %zu frames\n",differ,frames);
}
// prepareRecord == the 0.3.176 loop: draws, the stored rigid bones and the distance counters, bit for bit.
static void loopIdentity(World& world,unsigned frames){
    std::mt19937 rng(1775);std::size_t draws=0,tested=0;auto caches=std::make_unique<Caches>();
    std::unordered_map<IDirect3DVertexShader9*,std::shared_ptr<const Program>> programs;
    for(unsigned k=0;k<world.programs.size();++k)if(world.programs[k])programs[World::shader(k)]=world.programs[k];
    auto referenceCaches=std::make_unique<Caches>(); /* the reference's caches persist across frames, as the renderer's */
    for(unsigned f=0;f<frames;++f){
        auto replays=world.frame(rng,1+rng()%24);const auto p=published(replays);const std::size_t n=p.records.size();const Camera c=camera(rng);
        Reference r(replays,world.declarationCache,*referenceCaches);r.actorPrograms=programs;std::memcpy(r.context.inverseView,c.inverseView,64);std::memcpy(r.context.camera,c.camera,12);
        std::size_t tests=0,reusedDistances=0;r.run(tests,reusedDistances);
        std::vector<Output> out(n);prepare(p,0,n,*caches,c.inverseView,c.camera,out);
        assert(r.actorShadowDraws.size()==n);
        for(std::size_t k=0;k<n;++k){Output expected;expected.item=r.actorShadowDraws[k];expected.tested=p.records[k]->boneKnown;expected.after=out[k].after;
            assert(NorthlightActorPrepare::same(out[k],expected));assert(NorthlightActorPrepare::sameBits(&p.records[k]->bone,&out[k].item.bone,4));tested+=out[k].tested;}
        assert(!n||(out[n-1].after.distanceTests==tests&&out[n-1].after.distanceReused==reusedDistances));draws+=n;
    }
    std::printf("loop identity: prepareRecord == the 0.3.176 loop over %u frames, %zu draws (%zu rigid bones tested)\n",frames,draws,tested);
}
// 0.3.179 (T2): the record layouts side by side over the same frames: the arena (with the per-record copy
// when more than 128 declarations overflow it) and the program pointer against the 0.3.178 handle and
// per-record copy through the live cache. Bit-equal.
static void layouts(World& world,unsigned frames){
    std::mt19937 rng(1792);auto a=std::make_unique<Caches>(),b=std::make_unique<Caches>();std::size_t records=0,fromArena=0,overflow=0;
    for(unsigned f=0;f<frames;++f){world.manyDecls=f%4==0;
        auto replays=world.frame(rng,world.manyDecls?140:4+rng()%20);const auto p=published(replays);const Camera c=camera(rng);
        std::vector<RecR83> old;old.reserve(p.records.size());for(const Rec* rec:p.records)old.push_back(world.r83(*rec));
        NorthlightActorPrepare::State s1,s2;Output o1,o2;
        for(std::size_t k=0;k<p.records.size();++k){
            NorthlightActorPrepare::prepareRecord(s1,*p.records[k],p.index[k],*a,c.inverseView,c.camera,o1);
            NorthlightActorPrepare::prepareRecord(s2,old[k],p.index[k],*b,c.inverseView,c.camera,o2);
            assert(NorthlightActorPrepare::same(o1,o2));++records;fromArena+=p.records[k]->declCopy!=nullptr;overflow+=p.records[k]->program&&!p.records[k]->declCopy;}}
    world.manyDecls=false;assert(fromArena>1000&&overflow>50);
    std::printf("record layouts: %zu records bit-equal in the 0.3.179 (arena %zu, per-record copy on overflow %zu) and 0.3.178 layouts\n",records,fromArena,overflow);
}
// Counterfactual (T2): a cross-frame declaration cache keyed by pointer alone goes stale when the
// declaration is freed and another layout is allocated at its address (the fixture reuses the object);
// the per-frame arena reads the new layout.
static void staleDeclaration(World& world){
    std::mt19937 rng(1793);FakeDecl reused;reused.layout=gameLayout;NorthlightActorPrepare::DeclArena perFrame,crossFrame;
    auto get=[](IDirect3DVertexDeclaration9* d,const D3DVERTEXELEMENT9*& e,UINT& n){auto* f=static_cast<FakeDecl*>(d);e=f->layout.data();n=UINT(f->layout.size());return true;};
    std::size_t frames=0,stale=0;
    for(unsigned q=0;q<40;++q){
        for(int step=0;step<2;++step){ /* step 0: the first layout; step 1: another object at the same address */
            reused.layout=step?std::vector<D3DVERTEXELEMENT9>{{0,0,2,0,0,0},{0xff,0,17,0,0,0}}:gameLayout;perFrame.reset();
            auto replays=world.frame(rng,6);const Camera c=camera(rng);std::vector<Rec> arena,cross,fresh;
            for(const auto& rec:replays)if(rec->shadowSelected&&rec->shadowSkinned){
                Rec base;base.constantGroup=rec->constantGroup;base.originalShader=rec->originalShader;base.decl=&reused;base.shared=rec->shared;base.snapshot=rec->snapshot;base.constants=rec->constants;base.program=world.handle(*rec).get();
                Rec x=base,y=base,z=base;
                if(base.program){const auto* e=perFrame.find(base.decl,get);x.declCopy=e;x.declared=e->declared;const auto* g=crossFrame.find(base.decl,get);y.declCopy=g;y.declared=g->declared;
                    const D3DVERTEXELEMENT9* el=nullptr;UINT n=0;z.declared=get(base.decl,el,n);z.elementCount=n;std::copy(el,el+n,z.elements.begin());}
                arena.push_back(std::move(x));cross.push_back(std::move(y));fresh.push_back(std::move(z));}
            auto run=[&](std::vector<Rec>& v){std::vector<Output> out(v.size());NorthlightActorPrepare::State st;auto cc=std::make_unique<Caches>();
                for(std::size_t k=0;k<v.size();++k)NorthlightActorPrepare::prepareRecord(st,v[k],k,*cc,c.inverseView,c.camera,out[k]);return out;};
            const auto A=run(arena),B=run(cross),R=run(fresh);
            assert(equal(A,R,R.size()));if(step){++frames;stale+=!equal(B,R,R.size());}}
        crossFrame.reset();}
    assert(stale>frames/2);std::printf("stale declaration: a cross-frame pointer-keyed copy differs in %zu of %zu reused-address frames; the per-frame arena is equal\n",stale,frames);
}
// 0.3.179 (T3): the release publish and the fence-fence wake at WakeBatch=1 with producer pauses long
// enough for the worker to sleep between records: every published record is prepared without the
// join's help (a lost wakeup leaves one waiting: bounded wait, then assert), equal to inline.
static void wakeStress(World& world,unsigned frames){
    std::mt19937 rng(1794);NorthlightActorPrepare::Worker<Rec> worker;worker.setWakeBatch(1);worker.setWatchdogMs(2000);
    auto caches=std::make_unique<Caches>(),referenceCaches=std::make_unique<Caches>();std::size_t records=0,slept=0;unsigned wakes=0;
    for(unsigned f=0;f<frames;++f){
        auto replays=world.frame(rng,1+rng()%4);const auto p=published(replays);const std::size_t n=p.records.size();const Camera c=camera(rng);
        std::vector<Output> reference(n);prepare(p,0,n,*referenceCaches,c.inverseView,c.camera,reference);
        NorthlightActorPrepare::Frame frame;std::memcpy(frame.inverseView,c.inverseView,64);std::memcpy(frame.camera,c.camera,12);
        bool ok=worker.begin(frame,*caches);assert(ok);(void)ok;
        for(std::size_t k=0;k<n;++k){
            if(rng()%3==0){std::this_thread::sleep_for(std::chrono::microseconds(40+rng()%80));++slept;} /* past the 30 us spin: the worker sleeps */
            else for(unsigned i=rng()%500;i;--i)NorthlightActorPrepare::pause();
            worker.publish(p.records[k],p.index[k]);}
        for(unsigned i=0;i<20000&&worker.done()<n;++i)std::this_thread::sleep_for(std::chrono::microseconds(25)); /* bounded: 0.5 s */
        assert(worker.done()==n); /* no lost wakeup */
        const auto r=worker.stop();assert(!r.timedOut&&r.done==n);wakes+=worker.wakes();
        std::vector<Output> out(n);for(std::size_t k=0;k<n;++k)out[k]=worker.outputs()[k];assert(equal(out,reference,n));records+=n;
    }
    std::printf("wake stress (WakeBatch=1, release publish, fence-fence wake): %u frames, %zu records all prepared by the worker, %zu producer pauses past the spin, %u notifies; equal to inline\n",frames,records,slept,wakes);
}
// 0.3.179 (M1): the handoff meter against a synthetic clock whose every read costs readNs (the span of a
// timed record pays about one read): the corrected estimate equals the true (x16) cost within 5 %; raw is
// inflated past 20 % (the counterfactual without the pair subtraction fails the bound). Off: no clock read.
struct SynthClock {using rep=std::int64_t;using period=std::nano;using duration=std::chrono::nanoseconds;using time_point=std::chrono::time_point<SynthClock>;
    static constexpr bool is_steady=true;static std::int64_t nowNs,readNs;static time_point now(){nowNs+=readNs/2;const time_point t{duration(nowNs)};nowNs+=readNs-readNs/2;return t;}};
std::int64_t SynthClock::nowNs=0,SynthClock::readNs=0;
static void handoffMeter(){
    std::mt19937 rng(1795);unsigned frames=0,rawFails=0;double worst=0;
    for(unsigned f=0;f<200;++f){SynthClock::readNs=100+rng()%300;NorthlightActorPrepare::HandoffMeter<SynthClock> meter;meter.beginFrame(true,f);
        double sampledNs=0;unsigned expected=0;const unsigned records=300+rng()%900;
        for(unsigned k=0;k<records;++k){const std::int64_t work=60+rng()%120; /* ~0.1 us a record */
            const bool timed=meter.sample();expected+=(k+f)%16==0;assert(timed==((k+f)%16==0)); /* stride 16 from the frame's phase */
            if(timed){const auto t=meter.start();SynthClock::nowNs+=work;meter.stop(t);sampledNs+=double(work);}else SynthClock::nowNs+=work;}
        const double trueUs=16*sampledNs/1000;worst=std::max(worst,std::fabs(meter.correctedUs()-trueUs)/trueUs);
        assert(std::fabs(meter.pairNs()-double(SynthClock::readNs))<1&&meter.samples()==expected);
        rawFails+=meter.rawUs()>1.2*trueUs;++frames;}
    NorthlightActorPrepare::HandoffMeter<SynthClock> off;off.beginFrame(false,0);const auto before=SynthClock::nowNs;for(unsigned k=0;k<100;++k)assert(!off.sample());assert(SynthClock::nowNs==before);
    assert(worst<.05&&rawFails==frames);
    std::printf("handoff meter: corrected within %.2f %% of the true cost over %u synthetic frames (clock reads 100-400 ns); raw inflated past 20 %% in all\n",100*worst,frames);
}
// Test processes: a throw on one index, or a stall (the watchdog).
static std::atomic<std::size_t> throwAt{SIZE_MAX},stallAt{SIZE_MAX};static std::atomic<bool> stallHold{false}; /* 0.3.183: the stalled record waits on after 20 ms */
struct Faulty {template<class R> void operator()(State& s,const R& p,std::size_t index,Caches& c,const NorthlightActorPrepare::Frame& f,Output& out)const{
    if(index==throwAt.load())throw std::bad_alloc();
    if(index==stallAt.load()){std::this_thread::sleep_for(std::chrono::milliseconds(20));while(stallHold.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));} /* the record is read after it */
    NorthlightActorPrepare::prepareRecord(s,p,index,c,f.inverseView,f.camera,out);}};
static void delay(std::mt19937& rng){switch(rng()%6){case 0:std::this_thread::sleep_for(std::chrono::microseconds(rng()%80));break;
    case 1:for(unsigned i=rng()%2000;i;--i)NorthlightActorPrepare::pause();break;case 2:std::this_thread::yield();break;default:break;}}
// The threaded worker against inline, with the renderer's join: stop, then the tail over [done, n) with
// the frame's frozen camera and the same caches (ownership returned).
template<class Process> static void threaded(World& world,unsigned frames,unsigned maxGroups,bool faults){
    std::mt19937 rng(1773+frames);NorthlightActorPrepare::Worker<Rec,Process> worker;worker.setWatchdogMs(2000); /* a test-host scheduler stall is not a hang */auto caches=std::make_unique<Caches>();auto referenceCaches=std::make_unique<Caches>();
    std::size_t published_=0,byWorker=0,byJoin=0,beforeFirst=0,unpublished=0,failed=0,cameraDiffer=0,withTail=0;
    for(unsigned f=0;f<frames;++f){
        auto replays=world.frame(rng,1+rng()%maxGroups);const auto p=published(replays);const std::size_t n=p.records.size();Camera context=camera(rng);const Camera captured=context;
        std::vector<Output> reference(n);prepare(p,0,n,*referenceCaches,captured.inverseView,captured.camera,reference);
        if(faults){throwAt=rng()%3==0&&n?p.index[rng()%n]:SIZE_MAX;}
        const unsigned mode=rng()%8;const std::size_t cut=mode==0?0:mode==1?rng()%(n+1):n; /* publish all but a tail the joiner prepares */
        NorthlightActorPrepare::Frame frame;std::memcpy(frame.inverseView,context.inverseView,64);std::memcpy(frame.camera,context.camera,12);frame.timed=rng()%2;
        std::size_t k=0;
        if(mode!=0||rng()%2){bool ok=worker.begin(frame,*caches);assert(ok);(void)ok;
            for(;k<cut;++k){delay(rng);bool ok=worker.publish(p.records[k],p.index[k]);assert(ok);(void)ok;}}
        context.camera[0]+=1;context.inverseView[12]+=1; /* the context changes after the frame opened */
        if(rng()%3==0)std::this_thread::sleep_for(std::chrono::microseconds(rng()%300)); /* stop after completion */
        const auto r=worker.stop();if(r.timedOut||r.published!=k||r.done>k)std::printf("STOP unexpected: timedOut=%d published=%u k=%zu done=%u failed=%d\n",r.timedOut,r.published,k,r.done,r.failed);assert(!r.timedOut&&r.published==k&&r.done<=k);beforeFirst+=k==0;unpublished+=n-k;failed+=r.failed;
        std::vector<Output> out(n);
        for(std::size_t j=0;j<r.done;++j)out[j]=worker.outputs()[j];
        prepare(p,r.done,n,*caches,frame.inverseView,frame.camera,out);
        assert(equal(out,reference,n));
        published_+=k;byWorker+=r.done;byJoin+=n-r.done;
        if(r.done<n){++withTail;std::vector<Output> wrong=out;prepare(p,r.done,n,*caches,context.inverseView,context.camera,wrong);cameraDiffer+=!equal(wrong,reference,n);}
    }
    throwAt=SIZE_MAX;
    assert(byWorker>0&&byJoin>0&&beforeFirst>0&&unpublished>0&&cameraDiffer>withTail/2&&(!faults||failed>0));
    std::printf("threaded%s: %u frames equal to inline: %zu published, %zu by the worker, %zu by the join (%zu never published), %zu stops before the first publish, %zu worker exceptions taken over; camera at the join differs in %zu of %zu frames with a tail\n",
        faults?" with exceptions":"",frames,published_,byWorker,byJoin,unpublished,beforeFirst,failed,cameraDiffer,withTail);
}
// A record that stalls past the watchdog: stop() returns within about WatchdogMs, the worker is
// abandoned for the session and the frame is prepared inline with fresh caches; the records stay alive
// until the worker settles (it finishes the record in flight and acknowledges).
static void watchdog(World& world){
    std::mt19937 rng(1774);NorthlightActorPrepare::Worker<Rec,Faulty> worker;auto caches=std::make_unique<Caches>();
    auto replays=world.frame(rng,12);const auto p=published(replays);const std::size_t n=p.records.size();assert(n>4);const Camera c=camera(rng);
    std::vector<Output> reference(n);{auto r=std::make_unique<Caches>();prepare(p,0,n,*r,c.inverseView,c.camera,reference);}
    stallAt=p.index[2];NorthlightActorPrepare::Frame frame;std::memcpy(frame.inverseView,c.inverseView,64);std::memcpy(frame.camera,c.camera,12);
    bool ok=worker.begin(frame,*caches);assert(ok);for(std::size_t k=0;k<n;++k)worker.publish(p.records[k],p.index[k]);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto start=std::chrono::steady_clock::now();const auto r=worker.stop();const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    assert(r.timedOut&&r.done==0&&worker.abandoned()&&!worker.ready()&&!worker.begin(frame,*caches)&&ms<15&&!worker.rearm()); /* not settled: no re-arm */
    assert(!worker.settled()); /* the record in flight is still being read: the caller keeps the records */
    std::vector<Output> out(n);auto fresh=std::make_unique<Caches>();prepare(p,0,n,*fresh,c.inverseView,c.camera,out);assert(equal(out,reference,n));
    for(unsigned i=0;i<2000&&!worker.settled();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(worker.settled()); /* it finished that record, saw the stop and acknowledged: records and caches are free again */
    stallAt=SIZE_MAX;
    // Re-arm: the settled worker takes a new frame again; its outputs plus the join's tail equal inline.
    bool rearmed=worker.rearm();assert(rearmed&&!worker.abandoned()&&worker.ready()&&!worker.rearm());(void)rearmed;
    auto again=std::make_unique<Caches>();ok=worker.begin(frame,*again);assert(ok);for(std::size_t k=0;k<n;++k)worker.publish(p.records[k],p.index[k]);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));const auto r2=worker.stop();assert(!r2.timedOut&&r2.published==n);
    std::vector<Output> after(n);for(std::size_t k=0;k<r2.done;++k)after[k]=worker.outputs()[k];prepare(p,r2.done,n,*again,c.inverseView,c.camera,after);
    assert(equal(after,reference,n));(void)ok;
    std::printf("watchdog: a 20 ms record returned stop() after %.2f ms, worker abandoned, frame prepared inline equal; settled, re-armed (not before), next frame equal (%u by the worker)\n",ms,r2.done);
}
// ---- The renderer side (world_shadow_experiment.inl's prepare block, pasted by test_prepare_worker.py):
// captureModel's fill and publish, the join, the consumption in the selection, quiesce and endFrame, with
// the renderer members it uses.
static DWORD tickNow=0;static DWORD GetTickCount(){return tickNow;}
namespace NorthlightRenderThreadProbe {static bool on=false;inline bool profiling(){return on;}}
struct Renderer {
    using Replay=Rec;
    std::vector<std::unique_ptr<Rec>> replays;
    // Declared before the program maps (0.3.179's original order): only the explicit join in ~Renderer,
    // as in ~WorldRenderer, keeps an abandoned worker from outliving them (shutdownOrder()).
    NorthlightActorPrepare::Worker<Rec,Faulty> prepareWorker; /* passes through unless a test stalls or throws */
    bool joinFirst=true;~Renderer(){if(joinFirst)prepareWorker.join();}
    std::unordered_map<IDirect3DVertexShader9*,std::shared_ptr<const Program>> actorPrograms;
    struct CaptureShader {std::shared_ptr<const Program> program;};std::unordered_map<IDirect3DVertexShader9*,CaptureShader> captureShaders;
    std::vector<std::shared_ptr<const Program>> retiredPrograms;
    std::vector<std::shared_ptr<const Program>> prepareQuarantinedPrograms; /* 0.3.183: after the worker, as the maps (shutdownOrder's counterfactual) */
    std::unique_ptr<NorthlightActorPrepare::DeclArena> prepareDecls=std::make_unique<NorthlightActorPrepare::DeclArena>();
    std::vector<std::unique_ptr<NorthlightActorPrepare::DeclArena>> prepareQuarantinedDecls;
/*RETIRE_PROGRAM*/
    NorthlightVertexDeclarations::Cache& declarationCache;
    struct {float inverseView[16]={},camera[3]={};} context;
    struct {unsigned actorShadows=1;} quality;
    std::vector<NorthlightActorShadowSelection::Draw> actorShadowDraws;
    bool sampled=false;bool profileSampled()const{return sampled;}
    static NorthlightActorShadowSelection::Tuning selectionTuning(){return NorthlightActorShadowSelection::active();}
    unsigned logs=0;void logf(const char*,...){++logs;}
    std::unique_ptr<Caches> prepareCaches=std::make_unique<Caches>();
    std::unique_ptr<Caches> prepareAbandonedCaches,prepareCheckCaches;
    std::vector<std::unique_ptr<Rec>> prepareQuarantine;
    explicit Renderer(NorthlightVertexDeclarations::Cache& d):declarationCache(d){prepareWorker.setWatchdogMs(2000);}
    // 0.3.183: Tagged is the production predicate; All (0.3.182: every replay while unsettled) and Published
    // (only the records the worker was given) are counterfactuals.
    enum class Keep {Tagged,All,Published} keep=Keep::Tagged;
    bool held(const Rec& p)const{
        if(keep==Keep::All)return prepareUnsettled();
        if(keep==Keep::Published){if(!prepareUnsettled())return false;for(std::uint32_t k=0;k<prepareWorker.published();++k)if(prepareWorker.record(k)==&p)return true;return false;}
        return prepareHeld(p);}
    void recycleReplay(Rec* raw){if(held(*raw)){prepareQuarantine.emplace_back(raw);return;}delete raw;}
    // registerShader's program part: the erased entries' programs are retired (0.3.179 T1), or (the
    // counterfactual) freed at once.
    void reregister(IDirect3DVertexShader9* shader,bool deferred){
        auto old=captureShaders.find(shader);if(old!=captureShaders.end()){if(deferred)retireProgram(std::move(old->second.program));captureShaders.erase(old);}
        auto program=actorPrograms.find(shader);if(program!=actorPrograms.end()){if(deferred)retireProgram(std::move(program->second));actorPrograms.erase(program);}
    }
/*PREPARE_BLOCK*/
};
// The production integration over random frames: captures with random delays (fill, publish), the
// join, the selection's consumption: draws, rigid bones and distance counters equal to inline over the
// same replays. Also frames without a selection, mid-frame quiesce (trimMemory), deferred cache clears
// (registerShader) and the RenderProfile A/B windows. No resync, no self-check mismatch, no log.
static void integration(World& world,unsigned frames,unsigned maxGroups){
    std::mt19937 rng(1776);Renderer r(world.declarationCache);auto referenceCaches=std::make_unique<Caches>();
    for(unsigned k=0;k<world.programs.size();++k)if(world.programs[k])r.actorPrograms[World::shader(k)]=r.captureShaders[World::shader(k)].program=world.programs[k];
    std::size_t modes[3]={},selections=0,draws=0,quiesced=0,worker=0,joined=0;unsigned mismatch=0;
    for(unsigned f=0;f<frames;++f){
        auto records=world.frame(rng,1+rng()%maxGroups);const Camera c=camera(rng);
        std::memcpy(r.context.inverseView,c.inverseView,64);std::memcpy(r.context.camera,c.camera,12);
        NorthlightRenderThreadProbe::on=rng()%3==0;r.sampled=NorthlightRenderThreadProbe::on&&rng()%2;tickNow=DWORD(rng()%100000);
        const bool trim=rng()%12==0;const std::size_t trimAt=records.empty()?0:rng()%records.size();
        for(std::size_t i=0;i<records.size();++i){auto rec=std::move(records[i]);
            rec->program=nullptr;rec->declCopy=nullptr;rec->declared=false;rec->elementCount=0; /* the renderer's fill below */
            if(rng()%4==0)delay(rng);
            const bool handoff=rec->shadowSelected&&rec->shadowSkinned,timedHandoff=handoff&&r.prepareHandoffSample(); /* captureModel's M1 span */
            const auto handoffStart=timedHandoff?r.prepareMeter.start():std::chrono::steady_clock::time_point{};
            if(handoff)r.prepareFill(*rec,r.captureShaders[rec->originalShader]);
            r.replays.emplace_back(std::move(rec));r.preparePublish();if(timedHandoff)r.prepareMeter.stop(handoffStart);
            if(rng()%50==0)r.prepareCachesStale=true; /* registerShader */
            if(trim&&i==trimAt){r.prepareQuiesce();++quiesced;}}
        if(rng()%10){
            const bool open=r.prepareFrame==Renderer::PrepareFrame::Worker;
            if(open)assert(!std::memcmp(r.prepareWorker.frame().inverseView,r.context.inverseView,64)&&!std::memcmp(r.prepareWorker.frame().camera,r.context.camera,12)); /* the frozen camera equals the context at the join */
            r.prepareJoin();const char* mode=r.prepareStats.mode;modes[mode[0]=='w'?0:mode[0]=='a'?1:2]+=1;
            worker+=r.prepareStats.workerRecords;joined+=r.prepareStats.joinInline;
            std::size_t tests=0,reused=0;r.actorShadowDraws.clear();r.prepareStable(tests,reused);
            const auto p=published(r.replays);const std::size_t n=p.records.size();std::vector<Output> reference(n);
            prepare(p,0,n,*referenceCaches,c.inverseView,c.camera,reference);
            assert(r.actorShadowDraws.size()==n);
            for(std::size_t k=0;k<n;++k){Output got;got.item=r.actorShadowDraws[k];got.tested=p.records[k]->boneKnown;got.after=reference[k].after;
                assert(NorthlightActorPrepare::same(got,reference[k]));assert(NorthlightActorPrepare::sameBits(&p.records[k]->bone,&reference[k].item.bone,4));}
            assert(!n||(tests==reference[n-1].after.distanceTests&&reused==reference[n-1].after.distanceReused));
            ++selections;draws+=n;assert(!r.prepareStats.resync);mismatch+=r.prepareStats.mismatch;}
        r.prepareQuiesce();r.prepareEndFrame();
        for(auto& rec:r.replays)r.recycleReplay(rec.release());r.replays.clear();
    }
    assert(!mismatch&&!r.logs&&modes[0]&&modes[1]&&worker&&joined&&quiesced&&!r.prepareWorker.abandoned());
    std::printf("integration: %u frames, %zu selections (%zu draws) equal to inline; modes worker=%zu ab-inline=%zu inline=%zu; %zu draws by the worker, %zu by the join; %zu mid-frame quiesces; self-check mismatches 0\n",
        frames,selections,draws,modes[0],modes[1],modes[2],worker,joined,quiesced);
}
// The renderer's watchdog re-arm: 5 abandons (a stalled record), each settled; no re-arm before 10 s,
// then the worker returns, at most 4 times; after the 5th abandon inline for the session. Every frame
// equals inline.
static void rearmCap(World& world){
    std::mt19937 rng(1777);Renderer r(world.declarationCache);auto referenceCaches=std::make_unique<Caches>();
    for(unsigned k=0;k<world.programs.size();++k)if(world.programs[k])r.actorPrograms[World::shader(k)]=r.captureShaders[World::shader(k)].program=world.programs[k];
    r.prepareWorker.setWatchdogMs(5);NorthlightRenderThreadProbe::on=false;tickNow=1000;
    auto frame=[&](bool stall){
        std::vector<std::unique_ptr<Rec>> records;
        do records=world.frame(rng,8);while(published(records).records.empty());
        const Camera c=camera(rng);std::memcpy(r.context.inverseView,c.inverseView,64);std::memcpy(r.context.camera,c.camera,12);
        for(std::size_t i=0;i<records.size();++i){auto rec=std::move(records[i]);rec->program=nullptr;rec->declCopy=nullptr;rec->declared=false;rec->elementCount=0;
            if(stall&&rec->shadowSelected&&rec->shadowSkinned&&stallAt.load()==SIZE_MAX)stallAt=i;
            const bool handoff=rec->shadowSelected&&rec->shadowSkinned,timedHandoff=handoff&&r.prepareHandoffSample(); /* captureModel's M1 span */
            const auto handoffStart=timedHandoff?r.prepareMeter.start():std::chrono::steady_clock::time_point{};
            if(handoff)r.prepareFill(*rec,r.captureShaders[rec->originalShader]);
            r.replays.emplace_back(std::move(rec));r.preparePublish();if(timedHandoff)r.prepareMeter.stop(handoffStart);}
        if(stall)std::this_thread::sleep_for(std::chrono::milliseconds(2)); /* the worker is inside the stalled record */
        r.prepareJoin();const std::string mode=r.prepareStats.mode;
        std::size_t tests=0,reused=0;r.actorShadowDraws.clear();r.prepareStable(tests,reused);
        const auto p=published(r.replays);const std::size_t n=p.records.size();std::vector<Output> reference(n);
        prepare(p,0,n,*referenceCaches,c.inverseView,c.camera,reference);assert(r.actorShadowDraws.size()==n);
        for(std::size_t k=0;k<n;++k){Output got;got.item=r.actorShadowDraws[k];got.tested=p.records[k]->boneKnown;got.after=reference[k].after;assert(NorthlightActorPrepare::same(got,reference[k]));}
        r.prepareQuiesce();r.prepareEndFrame();for(auto& rec:r.replays)r.recycleReplay(rec.release());r.replays.clear();
        stallAt=SIZE_MAX;return mode;};
    for(unsigned cycle=0;cycle<5;++cycle){
        std::string mode=frame(true);assert(mode=="watchdog"&&r.prepareWorker.abandoned());
        mode=frame(false);assert(mode=="inline");
        std::this_thread::sleep_for(std::chrono::milliseconds(40)); /* the stalled record ends: settled */
        tickNow+=5000;mode=frame(false);assert(mode=="inline"&&r.prepareWorker.abandoned()); /* settled, but not 10 s yet */
        assert(r.prepareQuarantine.empty()&&!r.prepareAbandonedCaches&&r.prepareQuarantinedDecls.empty()&&r.prepareQuarantinedPrograms.empty()); /* released once settled */
        tickNow+=5001;mode=frame(false);assert(mode=="inline"); /* this frame's end re-arms (within the budget) */
        mode=frame(false);assert(mode==(cycle<4?"worker":"inline"));
        assert(r.prepareRearms==std::min(cycle+1,4u));}
    tickNow+=100000;for(unsigned i=0;i<3;++i)assert(frame(false)=="inline"); /* the cap holds */
    assert(r.prepareRearms==4&&r.prepareWorker.abandoned()&&r.logs==9); /* 5 watchdog lines, 4 re-arm lines */
    std::printf("watchdog re-arm: 5 abandons, re-armed 4 times (none before 10 s, none after the cap), every frame equal to inline\n");
}
// 0.3.179 (T1): a program erased by registerShader mid-frame is retired, not freed: the frame's records
// (and an abandoned worker inside one of them) keep reading it; it goes after the recycle once the worker
// settled. The programs are owned by the renderer's maps only. counterfactual: freed at the re-register
// (ASan reports the use-after-free; test_prepare_worker.py expects that).
static void programLifetime(World& world,bool counterfactual){
    std::mt19937 rng(1791);Renderer r(world.declarationCache);auto referenceCaches=std::make_unique<Caches>();
    for(unsigned k=0;k<world.programs.size();++k)if(world.programs[k]){auto own=std::make_shared<const Program>(*world.programs[k]);r.actorPrograms[World::shader(k)]=r.captureShaders[World::shader(k)].program=own;}
    r.prepareWorker.setWatchdogMs(5);NorthlightRenderThreadProbe::on=false;tickNow=1000;
    std::vector<std::unique_ptr<Rec>> records;do records=world.frame(rng,10);while(published(records).records.size()<4);
    const Camera c=camera(rng);std::memcpy(r.context.inverseView,c.inverseView,64);std::memcpy(r.context.camera,c.camera,12);
    IDirect3DVertexShader9* retired=nullptr;
    for(std::size_t i=0;i<records.size();++i){auto rec=std::move(records[i]);rec->program=nullptr;rec->declCopy=nullptr;rec->declared=false;rec->elementCount=0;
        if(rec->shadowSelected&&rec->shadowSkinned){if(stallAt.load()==SIZE_MAX&&r.captureShaders[rec->originalShader].program){stallAt=i;retired=rec->originalShader;}
            r.prepareFill(*rec,r.captureShaders[rec->originalShader]);}
        r.replays.emplace_back(std::move(rec));r.preparePublish();}
    std::this_thread::sleep_for(std::chrono::milliseconds(2)); /* the worker is inside the stalled record */
    r.reregister(retired,!counterfactual); /* a new shader at the address: its program leaves the maps */
    r.prepareJoin();assert(std::string(r.prepareStats.mode)=="watchdog");
    std::size_t tests=0,reused=0;r.actorShadowDraws.clear();r.prepareStable(tests,reused); /* reads the retired program */
    const auto p=published(r.replays);const std::size_t n=p.records.size();std::vector<Output> reference(n);
    prepare(p,0,n,*referenceCaches,c.inverseView,c.camera,reference);
    for(std::size_t k=0;k<n;++k){Output got;got.item=r.actorShadowDraws[k];got.tested=p.records[k]->boneKnown;got.after=reference[k].after;assert(NorthlightActorPrepare::same(got,reference[k]));}
    r.prepareQuiesce();r.prepareEndFrame();for(auto& rec:r.replays)r.recycleReplay(rec.release());r.replays.clear();r.prepareFrameRelease();
    assert(r.retiredPrograms.empty()&&!r.prepareQuarantinedPrograms.empty()); /* 0.3.183: pinned at the abandon; the worker is still inside a record that points at it */
    std::this_thread::sleep_for(std::chrono::milliseconds(40));stallAt=SIZE_MAX;
    r.prepareQuiesce();r.prepareEndFrame();r.prepareFrameRelease(); /* settled: quarantine recycled, then the programs go */
    assert(r.retiredPrograms.empty()&&r.prepareQuarantine.empty());
    std::printf("program lifetime: a program re-registered mid-frame stays alive for the frame's records and the abandoned worker's record, released once settled\n");
}
// 0.3.183: the quarantine is bounded to the abandoned frame. Frame A: the stalled record S (a group's first
// selected skinned draw with a program: it reads its program, constants and declaration copy) borrows its
// constants from an earlier record D that is never published (F1); the worker is held in S for 32 more
// inline frames (one re-registers a shader). Held: exactly frame A's replays (D and a replay moved to held
// included), one arena, the programs pinned at the abandon; all four released after settle. Counterfactuals:
// All (every replay while unsettled: the quarantine grows), Published (only the published records: D is
// freed), Unpinned (the pins dropped: S's retired program is freed); ASan or the assert reports them.
enum class Bound {Tagged,All,Published,Unpinned};
static void abandonedBound(World& world,Bound mode){
    std::mt19937 rng(1831);Renderer r(world.declarationCache);auto referenceCaches=std::make_unique<Caches>();
    for(unsigned k=0;k<world.programs.size();++k)if(world.programs[k]){auto own=std::make_shared<const Program>(*world.programs[k]);r.actorPrograms[World::shader(k)]=r.captureShaders[World::shader(k)].program=own;}
    r.keep=mode==Bound::All?Renderer::Keep::All:mode==Bound::Published?Renderer::Keep::Published:Renderer::Keep::Tagged;
    r.prepareWorker.setWatchdogMs(5);NorthlightRenderThreadProbe::on=false;tickNow=1000;
    auto capture=[&](std::vector<std::unique_ptr<Rec>>& records){
        for(std::size_t i=0;i<records.size();++i){auto rec=std::move(records[i]);rec->program=nullptr;rec->declCopy=nullptr;rec->declared=false;rec->elementCount=0;
            if(rec->shadowSelected&&rec->shadowSkinned)r.prepareFill(*rec,r.captureShaders[rec->originalShader]);
            r.replays.emplace_back(std::move(rec));r.preparePublish();}};
    auto select=[&](const Camera& c){std::size_t tests=0,reused=0;r.actorShadowDraws.clear();r.prepareStable(tests,reused);
        const auto p=published(r.replays);const std::size_t n=p.records.size();std::vector<Output> reference(n);
        prepare(p,0,n,*referenceCaches,c.inverseView,c.camera,reference);assert(r.actorShadowDraws.size()==n);
        for(std::size_t k=0;k<n;++k){Output got;got.item=r.actorShadowDraws[k];got.tested=p.records[k]->boneKnown;got.after=reference[k].after;assert(NorthlightActorPrepare::same(got,reference[k]));}};
    auto endFrame=[&](std::vector<std::unique_ptr<Rec>>& held){r.prepareQuiesce();r.prepareEndFrame();
        for(auto& rec:r.replays)r.recycleReplay(rec.release());r.replays.clear();for(auto& rec:held)r.recycleReplay(rec.release());held.clear();r.prepareFrameRelease();};
    // Frame A: D (index 0, unselected: never published) donates S's constants.
    std::vector<std::unique_ptr<Rec>> records;std::size_t s=0,sPublished=0;
    for(;;){records=world.frame(rng,10);records[0]->shadowSelected=false;s=0;
        std::size_t published_=0;unsigned previousGroup=UINT_MAX;
        for(std::size_t i=0;i<records.size();++i){const Rec& x=*records[i];if(!x.shadowSelected||!x.shadowSkinned)continue;
            if(!s&&x.constantGroup!=records[0]->constantGroup&&x.constantGroup!=previousGroup&&x.decl==&world.decls[0]&&r.captureShaders[x.originalShader].program){s=i;sPublished=published_;}
            previousGroup=x.constantGroup;++published_;}
        if(s&&published_>=4)break;}
    Rec& D=*records[0];Rec& S=*records[s];const std::vector<float> bank(S.constants,S.constants+1024);D.storage=bank;S.constants=D.storage.data(); /* D's group reads the same buffer */
    IDirect3DVertexShader9* retired=S.originalShader;IDirect3DVertexShader9* later=nullptr;for(unsigned k=0;k<world.programs.size()&&!later;++k)if(world.programs[k]&&World::shader(k)!=retired)later=World::shader(k);
    const Camera c=camera(rng);std::memcpy(r.context.inverseView,c.inverseView,64);std::memcpy(r.context.camera,c.camera,12);
    stallAt=s;stallHold=true;capture(records);
    for(unsigned i=0;i<2000&&r.prepareWorker.done()<sPublished;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(2)); /* the worker is inside S */
    r.prepareJoin();assert(std::string(r.prepareStats.mode)=="watchdog"&&r.prepareUnsettled());
    if(mode==Bound::Unpinned)r.prepareQuarantinedPrograms.clear();
    const std::size_t nA=r.replays.size(),p0=r.prepareQuarantinedPrograms.size();
    select(c);
    std::vector<std::unique_ptr<Rec>> held;for(std::size_t i=r.replays.size();i--;)if(i!=s&&r.replays[i]->shadowSelected&&r.replays[i]->shadowSkinned){held.push_back(std::move(r.replays[i]));r.replays.erase(r.replays.begin()+std::ptrdiff_t(i));break;} /* retainSelected */
    r.reregister(retired,true); /* S's program leaves the maps after the abandon: retired */
    endFrame(held);
    auto settle=[&]{stallHold=false;for(unsigned i=0;i<2000&&!r.prepareWorker.settled();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));stallAt=SIZE_MAX;};
    if(mode==Bound::Published||mode==Bound::Unpinned){settle();return;} /* ASan reports the worker's read of the freed donor or program */
    if(mode==Bound::Tagged)assert(r.prepareQuarantine.size()==nA&&r.retiredPrograms.empty()&&r.prepareQuarantinedDecls.size()==1&&r.prepareQuarantinedPrograms.size()==p0);
    for(unsigned f=0;f<32;++f){ /* inline: the worker is still held in S */
        do records=world.frame(rng,1+rng()%8);while(records.empty());const Camera d=camera(rng);std::memcpy(r.context.inverseView,d.inverseView,64);std::memcpy(r.context.camera,d.camera,12);
        capture(records);r.prepareJoin();assert(std::string(r.prepareStats.mode)=="inline");select(d);
        if(f==16)r.reregister(later,true); /* retired after the abandon: released at this frame's end */
        endFrame(held);
        assert(r.prepareQuarantine.size()==nA); /* I1: the abandoned frame's replays only */
        assert(r.prepareQuarantinedDecls.size()<=1&&r.prepareQuarantinedPrograms.size()==p0&&r.retiredPrograms.empty()&&r.prepareUnsettled()); /* I2 */}
    settle();assert(r.prepareWorker.settled());
    do records=world.frame(rng,4);while(records.empty());std::memcpy(r.context.inverseView,c.inverseView,64);std::memcpy(r.context.camera,c.camera,12);capture(records);r.prepareJoin();select(c);endFrame(held);
    assert(r.prepareQuarantine.empty()&&r.prepareQuarantinedDecls.empty()&&r.prepareQuarantinedPrograms.empty()&&!r.prepareAbandonedCaches&&r.retiredPrograms.empty()); /* I4 */
    std::printf("abandoned bound: frame A's %zu replays (a never-published constant donor and a held replay included), 1 arena and %zu pinned programs held through 32 more frames and a re-register; all released at settle\n",nA,p0);
}
// 0.3.179: destruction with an abandoned, unsettled worker still inside a record whose program only the
// renderer's maps own. The explicit join (first in ~WorldRenderer, and in the harness) waits for that
// record; counterfactual (no join, the worker declared before the maps): ASan reports the use-after-free.
static void shutdownOrder(World& world,bool counterfactual){
    std::mt19937 rng(1796);auto r=std::make_unique<Renderer>(world.declarationCache);r->joinFirst=!counterfactual;
    for(unsigned k=0;k<world.programs.size();++k)if(world.programs[k]){auto own=std::make_shared<const Program>(*world.programs[k]);r->actorPrograms[World::shader(k)]=r->captureShaders[World::shader(k)].program=own;}
    r->prepareWorker.setWatchdogMs(5);NorthlightRenderThreadProbe::on=false;
    std::vector<std::unique_ptr<Rec>> records;do records=world.frame(rng,10);while(published(records).records.size()<4);
    const Camera c=camera(rng);std::memcpy(r->context.inverseView,c.inverseView,64);std::memcpy(r->context.camera,c.camera,12);
    for(std::size_t i=0;i<records.size();++i){auto rec=std::move(records[i]);rec->program=nullptr;rec->declCopy=nullptr;rec->declared=false;rec->elementCount=0;
        if(rec->shadowSelected&&rec->shadowSkinned){if(stallAt.load()==SIZE_MAX&&r->captureShaders[rec->originalShader].program)stallAt=i;r->prepareFill(*rec,r->captureShaders[rec->originalShader]);}
        r->replays.emplace_back(std::move(rec));r->preparePublish();}
    std::this_thread::sleep_for(std::chrono::milliseconds(2)); /* the worker is inside the stalled record, before it reads the program */
    r->prepareJoin();assert(std::string(r->prepareStats.mode)=="watchdog"&&r->prepareUnsettled());
    r.reset(); /* the process exits */
    stallAt=SIZE_MAX;
    std::printf("shutdown order: destroyed with an abandoned worker inside a record; the join waited for it\n");
}
int main(int argc,char** argv){
    assert(argc>2);World world(argv[1]);const bool tsan=std::string(argv[2])=="tsan";
    if(std::string(argv[2])=="lifetime-counterfactual"){programLifetime(world,true);return 0;}
    if(std::string(argv[2])=="shutdown-counterfactual"){shutdownOrder(world,true);return 0;}
    if(std::string(argv[2])=="quarantine-all-counterfactual"){abandonedBound(world,Bound::All);return 0;}
    if(std::string(argv[2])=="quarantine-published-counterfactual"){abandonedBound(world,Bound::Published);return 0;}
    if(std::string(argv[2])=="unpinned-counterfactual"){abandonedBound(world,Bound::Unpinned);return 0;}
    handoffMeter();
    loopIdentity(world,tsan?40:600);layouts(world,tsan?20:200);staleDeclaration(world);handover(world,tsan?4:24);evictedDeclaration(world);
    threaded<NorthlightActorPrepare::Prepare>(world,tsan?10000:3000,tsan?3:12,false);
    wakeStress(world,tsan?600:1500);
    threaded<Faulty>(world,tsan?2000:1500,tsan?3:12,true);
    integration(world,tsan?3000:2000,tsan?4:16);
    rearmCap(world);
    programLifetime(world,false);
    abandonedBound(world,Bound::Tagged);
    shutdownOrder(world,false);
    watchdog(world);
    std::puts("PASS prepare worker: handover at every index, threaded worker and join, exception and watchdog takeover equal to inline; counterfactuals fail");
}
