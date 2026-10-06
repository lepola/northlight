#!/usr/bin/env python3
# northlight-test: requires=cxx,client,stormlib slow
"""0.3.143 replay bounds worker: the asynchronous pass must reproduce the 0.3.142
synchronous pass bit for bit (bounds, work classes, visit counts, cache state)
under a deterministic bounds clock, whether the render thread joins early or
late; stress mode adds cancellation, mesh churn during the job, unknown
programs and a protocol violation. Builds -O2, ASan+UBSan and TSan, then a
real-clock main-thread microbenchmark (legacy wall vs kick+join)."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import client_fixtures  # the real client programs, from the tester's client
from pathlib import Path
import ast
import json
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent

def literal(path, name):
    return next(ast.literal_eval(n.value) for n in ast.parse(path.read_text()).body
                if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in n.targets))

stub = literal(HERE / 'test_terrain_snapshot.py', 'stub')
fixture = literal(HERE / 'test_replay_skin_safety.py', 'harness').split('int main(')[0]
fixture = fixture.replace('static Status finish(', '[[maybe_unused]] static Status finish(').replace('static Mesh geometry(', '[[maybe_unused]] static Mesh geometry(').replace('static void palette(', '[[maybe_unused]] static void palette(').replace('static size_t enclosed(', '[[maybe_unused]] static size_t enclosed(')
point = fp.src('world_point_rendering.inl').read_text()
# The renderer's own kick/join/abandon glue, compiled verbatim below.
glue = point[point.index('    NorthlightReplayBoundsJob::Worker replayBoundsWorker;'):point.index('    void pointCalculateReplayBounds(){')]
# 0.3.142 pointCalculateReplayBounds pass, verbatim (logging tail excluded).
LEGACY = r"""        const auto start=std::chrono::steady_clock::now();
        using WorkKind=NorthlightReplayBounds::WorkKind;
        NorthlightReplayBounds::Budget cheapBudget,heavyBudget,buildBudget;
        cheapBudget.maxVertices=0;cheapBudget.maxOperations=163840;
        heavyBudget.maxVertices=0;heavyBudget.maxOperations=32768;
        buildBudget.maxVertices=2048;buildBudget.maxOperations=65536;
        const bool diagnostics=NorthlightDiagnostics::enabled()&&(frames==0||frames%120==0); /* logs and envelope timing fields only */
        pointBoundsValid=0;replayBoundsCache.beginFrame(diagnostics);replayBoundsMetadata.beginFrame();
        // Hints contain no entry/declaration pointers. Current pose bounds and
        // classifications never survive to another frame or recycled packet.
        for(auto& p:replays){p->pointBounds={};p->boundsWork={};p->boundsPrepared.reset();}
        size_t cheapVisited=0,heavyVisited=0,buildVisited=0;
        const auto visit=[&](size_t index,unsigned phase){
            auto& p=replays[index];if(p->pointBounds.valid)return false;
            // This check precedes declaration/program/cache lookups. In a warm
            // crowd the cold pass consequently performs no repeated lookups.
            if(phase==1&&p->boundsWork.kind!=WorkKind::Heavy)return false;
            if(phase==2&&p->boundsWork.kind!=WorkKind::Cold)return false;
            // The borrowed cheap turn cannot spend already-closed heavy/build
            // allowances. Newly built cold packets evaluate on a later frame.
            if(phase==0&&(p->boundsWork.kind==WorkKind::Heavy||p->boundsWork.kind==WorkKind::Cold||p->boundsWork.kind==WorkKind::Unsupported))return false;
            if(!p->shared){p->boundsWork.kind=WorkKind::Unsupported;return false;}
            if(phase==0&&!p->boundsPrepared){
                p->boundsPrepared=replayBoundsMetadata.get(p->originalShader,p->decl,[&]()->std::shared_ptr<const NorthlightReplayBounds::Prepared>{
                    // Key preparation used to run inside each cache lookup.
                    // Charge misses to the same budget; hits need no new timer.
                    struct PreparationCost {
                        NorthlightReplayBounds::Cache& cache;
                        std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
                        ~PreparationCost(){cache.chargeCheapPreparation(std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count()));}
                    } cost{replayBoundsCache};
                    auto program=actorPrograms.find(p->originalShader);
                    if(program==actorPrograms.end())return {};
                    const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
                    if(!declarationCache.get(p->decl,elements,count))return {};
                    return NorthlightReplayBounds::EnvelopeCache::prepareProgram(*program->second,elements,count);
                });
            }
            NorthlightReplayBounds::Status status;
            if(p->boundsPrepared){
                const auto& prepared=*p->boundsPrepared;
                if(phase==0){++cheapVisited;status=replayBoundsCache.calculateCheap(prepared,p->mesh(),p->shared,p->constants,context.inverseView,cheapBudget,p->pointBounds,p->boundsWork);}
                else if(phase==1){++heavyVisited;status=replayBoundsCache.evaluateHeavy(prepared,p->mesh(),p->shared,p->constants,context.inverseView,heavyBudget,p->pointBounds,p->boundsWork);}
                else{++buildVisited;status=replayBoundsCache.buildEnclosed(prepared,p->mesh(),p->shared,p->constants,context.inverseView,buildBudget,p->pointBounds);}
            }else{
                // Allocation/metadata-cap failures retain the audited full key
                // path and do not turn missing metadata into missing shadows.
                auto program=actorPrograms.find(p->originalShader);
                if(program==actorPrograms.end()){p->boundsWork.kind=WorkKind::Unsupported;return false;}
                const D3DVERTEXELEMENT9* elements=nullptr;UINT count=0;
                if(!declarationCache.get(p->decl,elements,count)){p->boundsWork.kind=WorkKind::Unsupported;return false;}
                if(phase==0){++cheapVisited;status=replayBoundsCache.calculateCheap(*program->second,p->mesh(),p->shared,elements,count,p->constants,context.inverseView,cheapBudget,p->pointBounds,p->boundsWork);}
                else if(phase==1){++heavyVisited;status=replayBoundsCache.evaluateHeavy(*program->second,p->mesh(),p->shared,elements,count,p->constants,context.inverseView,heavyBudget,p->pointBounds,p->boundsWork);}
                else{++buildVisited;status=replayBoundsCache.buildEnclosed(*program->second,p->mesh(),p->shared,elements,count,p->constants,context.inverseView,buildBudget,p->pointBounds);}
            }
            if(status==NorthlightReplayBounds::Status::Valid)++pointBoundsValid;
            return status==NorthlightReplayBounds::Status::Budget;
        };
        replayBoundsCache.readyResume(NorthlightReplaySchedule::pass(replays.size(),replayBoundsCache.readyStart(replays.size()),
            [&](){return replayBoundsCache.canCheap()&&cheapBudget.operations<cheapBudget.maxOperations;},[&](size_t index){return visit(index,0);}));
        replayBoundsCache.heavyResume(NorthlightReplaySchedule::pass(replays.size(),replayBoundsCache.heavyStart(replays.size()),
            [&](){return replayBoundsCache.canHeavy()&&heavyBudget.operations<heavyBudget.maxOperations;},[&](size_t index){return visit(index,1);}));
        replayBoundsCache.continuePendingBuild(buildBudget);
        replayBoundsCache.buildResume(NorthlightReplaySchedule::pass(replays.size(),replayBoundsCache.buildStart(replays.size()),
            [&](){return replayBoundsCache.canBuild()&&buildBudget.vertices<buildBudget.maxVertices&&buildBudget.operations<buildBudget.maxOperations;},[&](size_t index){return visit(index,2);}));
        const size_t reservedCheapVisited=cheapVisited;
        replayBoundsCache.finishReservedTurnsAndLend();
        cheapBudget.maxOperations=262144-heavyBudget.operations-buildBudget.operations;
        // Preserve the first turn's cursor for the next frame. Advancing it
        // through this bonus turn could strand cold/heavy packets exclusively
        // after their reserved phases on every frame of a periodic draw list.
        NorthlightReplaySchedule::pass(replays.size(),replayBoundsCache.readyStart(replays.size()),
            [&](){return replayBoundsCache.canCheap()&&cheapBudget.operations<cheapBudget.maxOperations;},[&](size_t index){return visit(index,0);});
        replayBoundsCache.settleClock();
        pointBoundVertices=buildBudget.vertices;pointBoundOperations=cheapBudget.operations+heavyBudget.operations+buildBudget.operations;
"""

clock = r"""
#include <atomic>
#include <chrono>
#include <cstdint>
struct TestClock {
    using duration=std::chrono::nanoseconds;using rep=duration::rep;using period=duration::period;
    using time_point=std::chrono::time_point<TestClock,duration>;static constexpr bool is_steady=true;
    static std::atomic<std::int64_t> ticks,step;
    static time_point now()noexcept{return time_point(duration(ticks.fetch_add(step.load(std::memory_order_relaxed),std::memory_order_relaxed)));}
};
inline std::atomic<std::int64_t> TestClock::ticks{1000000},TestClock::step{1000};
#define NORTHLIGHT_REPLAY_BOUNDS_CLOCK TestClock
#define NORTHLIGHT_TEST_CLOCK 1
"""

harness = r"""
#include "replay_bounds_job.h"
#include "replay_bounds_metadata.h"
#include <unordered_map>
#include <algorithm>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>
""" + fixture + r"""
using Prepared=NorthlightReplayBounds::Prepared;using WorkInfo=NorthlightReplayBounds::WorkInfo;using Totals=NorthlightReplayBoundsJob::Totals;
struct IDirect3DVertexShader9 {unsigned AddRef(){return 1;}unsigned Release(){return 1;}};
struct FakeDecl:IDirect3DVertexDeclaration9 {
    std::vector<D3DVERTEXELEMENT9> elements;
    unsigned AddRef()override{return 1;}unsigned Release()override{return 1;}
    HRESULT GetDeclaration(D3DVERTEXELEMENT9*,UINT*)override{return D3D_OK;}
};
#include "diagnostics_switch.h" /* 0.3.192: draw_snapshot.h includes it (LOCK METER read-back classes); off until a frame turns it on */
static const bool diagnosticsOff=(NorthlightDiagnostics::configure(false),true);
struct Packet {
    IDirect3DVertexShader9* originalShader=nullptr;IDirect3DVertexDeclaration9* decl=nullptr;
    NorthlightReplayBounds::Bounds pointBounds;WorkInfo boundsWork;std::shared_ptr<const Prepared> boundsPrepared;
    std::shared_ptr<const Mesh> shared;Mesh snapshot;const float* constants=nullptr;unsigned otherState=0;
    const Mesh& mesh()const{return shared?*shared:snapshot;}
};
struct DeclarationCache {bool get(IDirect3DVertexDeclaration9* d,const D3DVERTEXELEMENT9*& e,UINT& n){if(!d)return false;auto* f=static_cast<FakeDecl*>(d);e=f->elements.data();n=UINT(f->elements.size());return true;}};
struct Context {float inverseView[16]={};};
struct World {
    std::unordered_map<IDirect3DVertexShader9*,std::shared_ptr<const Program>>& actorPrograms; /* 0.3.177: the renderer's program handles */
    std::vector<std::unique_ptr<Packet>> replays;
    NorthlightReplayBounds::Cache replayBoundsCache;
    NorthlightReplayMetadata::Cache<Prepared,IDirect3DVertexShader9,IDirect3DVertexDeclaration9> replayBoundsMetadata;
    DeclarationCache declarationCache;Context context;unsigned frames=0,logs=0;
    unsigned pointBoundsValid=0;size_t pointBoundVertices=0,pointBoundOperations=0;Totals visits;
    explicit World(std::unordered_map<IDirect3DVertexShader9*,std::shared_ptr<const Program>>& programs):actorPrograms(programs){}
    void pointLogReplayBounds(const Totals& t,double,double,double,double,double,bool){visits=t;++logs;}
    void legacyPass(){
""" + LEGACY + r"""
        visits={cheapVisited,heavyVisited,buildVisited,reservedCheapVisited,pointBoundVertices,pointBoundOperations,pointBoundsValid};(void)start;
    }
""" + glue + r"""
};
static bool same(const NorthlightReplayBounds::Bounds& a,const NorthlightReplayBounds::Bounds& b){return a.valid==b.valid&&(!a.valid||(!std::memcmp(a.low,b.low,12)&&!std::memcmp(a.high,b.high,12)));}
[[maybe_unused]] static bool sameVisits(const Totals& a,const Totals& b){return a.cheapVisited==b.cheapVisited&&a.heavyVisited==b.heavyVisited&&a.buildVisited==b.buildVisited&&a.reservedCheapVisited==b.reservedCheapVisited&&a.boundVertices==b.boundVertices&&a.boundOperations==b.boundOperations&&a.valid==b.valid;}
[[maybe_unused]] static bool sameWork(const WorkInfo& a,const WorkInfo& b){return a.kind==b.kind&&a.programHash==b.programHash&&a.groups==b.groups&&a.operations==b.operations&&a.skin==b.skin&&a.elapsedUs==b.elapsedUs;}
static Mesh randomMesh(std::mt19937& rng,unsigned vertices,unsigned bones){
    Mesh m;m.vertexCount=vertices;m.primitiveCount=vertices/3;m.topology=D3DPT_TRIANGLELIST;m.indexed=false;
    m.streams[0].stride=32;m.streams[0].bytes.resize(size_t(vertices)*32);std::uniform_real_distribution<float> u(-1,1),w(0,1);
    const unsigned first=rng()%(76-bones);
    for(unsigned n=0;n<vertices;++n){float v[7]={u(rng)*2,u(rng)*2,u(rng)*2,w(rng),w(rng),w(rng),w(rng)};float sum=v[3]+v[4]+v[5]+v[6];
        for(unsigned k=3;k<7;++k)v[k]/=sum;auto* t=m.streams[0].bytes.data()+n*32;std::memcpy(t,v,28);
        for(unsigned k=0;k<4;++k)t[28+k]=(unsigned char)(first+(bones>1?rng()%bones:0));}
    return m;
}
static void pose(std::mt19937& rng,float* c,unsigned actor,unsigned frame){
    std::uniform_real_distribution<float> u(-1,1);for(unsigned i=0;i<124;++i)c[i]=u(rng);
    for(unsigned b=0;b<75;++b){const float a=float(b+actor)*.031f+float(frame)*.01f,e=float(b*3+actor)*.017f;
        const float r[3][3]={{std::cos(a),-std::sin(a)*std::cos(e),std::sin(a)*std::sin(e)},{std::sin(a),std::cos(a)*std::cos(e),-std::cos(a)*std::sin(e)},{0,std::sin(e),std::cos(e)}};
        for(unsigned i=0;i<3;++i){float* row=c+4*(31+3*b+i);for(unsigned j=0;j<3;++j)row[j]=r[i][j];row[3]=float(actor%40)*3.1f+u(rng)*.05f;}}
}
static void view(float* inverse,unsigned frame){
    const float a=float(frame)*.013f,e=.3f+float(frame%50)*.002f;std::fill(inverse,inverse+16,0.f);
    const float r[3][3]={{std::cos(a),std::sin(a),0},{-std::sin(a)*std::cos(e),std::cos(a)*std::cos(e),std::sin(e)},{std::sin(a)*std::sin(e),-std::cos(a)*std::sin(e),std::cos(e)}};
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)inverse[4*i+j]=r[i][j];inverse[12]=100+float(frame)*.2f;inverse[13]=-50;inverse[14]=20;inverse[15]=1;
}
static Program rigid(){
    Program p;p.major=3;p.positionRegister=0;p.inputs.push_back({0,0,0});
    Operation op;op.code=1;op.destination=0x800f0000;op.source[0].token=0x90e40000;p.operations.push_back(op);
    op={};op.code=2;op.destination=0x80070000;op.source[0].token=0x80e40000;op.source[1].token=0xa0e4000a;p.operations.push_back(op);return p;
}
struct Scene {
    IDirect3DVertexShader9 shaders[4];FakeDecl decls[2];std::unordered_map<IDirect3DVertexShader9*,std::shared_ptr<const Program>> programs;
    std::vector<std::shared_ptr<const Mesh>> pool;std::vector<unsigned> kinds;std::mt19937 rng{143};
    std::vector<std::array<float,1024>> banks;unsigned heavyBones=3,heavyShare=2;
    explicit Scene(const Program& skin,size_t meshes,unsigned bones=3,unsigned share=2):heavyBones(bones),heavyShare(share){
        Program heavy=skin;Operation copy;copy.code=1;copy.destination=0x80070001;copy.source[0].token=0x80e40001;heavy.operations.push_back(copy);
        programs[&shaders[0]]=std::make_shared<const Program>(skin);programs[&shaders[1]]=std::make_shared<const Program>(heavy);programs[&shaders[2]]=std::make_shared<const Program>(rigid()); /* shaders[3]: unknown program */
        for(auto& d:decls)d.elements.assign(declaration,declaration+4);
        for(size_t i=0;i<meshes;++i)add();
    }
    void add(){const unsigned kind=unsigned(rng()%10);kinds.push_back(kind<8-heavyShare?0:kind<8?1:2);
        const unsigned bones=kinds.back()==1?1+rng()%heavyBones:1+rng()%40;pool.push_back(std::make_shared<const Mesh>(randomMesh(rng,kinds.back()==2?12+rng()%60:30+rng()%700,bones)));}
    void churn(size_t count){for(size_t n=0;n<count;++n){const size_t i=rng()%pool.size();const unsigned bones=kinds[i]==1?1+rng()%heavyBones:1+rng()%40;
        pool[i]=std::make_shared<const Mesh>(randomMesh(rng,kinds[i]==2?12+rng()%60:30+rng()%700,bones));}}
    // Identical packet lists for both worlds: same meshes, same frozen banks.
    void frame(unsigned f,size_t count,bool unknown,std::vector<World*> worlds){
        const size_t actors=count/4+1;banks.resize(actors);for(size_t a=0;a<actors;++a)pose(rng,banks[a].data(),unsigned(a),f);
        for(auto* w:worlds){w->replays.clear();view(w->context.inverseView,f);w->frames=f%7?1:0;} /* frames==0: sampled diagnostics */
        std::mt19937 local(f*2654435761u+17);
        for(size_t i=0;i<count;++i){const size_t actor=i/4;const size_t mesh=(actor*7+i%4+(f/200))%pool.size();
            const bool owned=local()%33==0,odd=unknown&&local()%29==0;
            for(auto* w:worlds){auto p=std::make_unique<Packet>();p->originalShader=&shaders[odd?3:kinds[mesh]];p->decl=&decls[(actor/3)%2];
                if(!owned)p->shared=pool[mesh];else{p->snapshot.vertexCount=3;p->snapshot.primitiveCount=1;}
                p->constants=banks[actor].data();w->replays.push_back(std::move(p));}
        }
    }
};
static void warm(World& w,Scene& s){ // metadata warm for every pair (<=4 new per frame)
    for(unsigned round=0;round<8;++round){w.replayBoundsMetadata.beginFrame();
        for(unsigned sh=0;sh<3;++sh)for(auto& d:s.decls)w.replayBoundsMetadata.get(&s.shaders[sh],&d,[&]{return NorthlightReplayBounds::EnvelopeCache::prepareProgram(*s.programs[&s.shaders[sh]],d.elements.data(),d.elements.size());});}
}
[[maybe_unused]] static void compareCaches(const World& a,const World& b,size_t n){
    const auto& x=a.replayBoundsCache;const auto& y=b.replayBoundsCache;
    assert(!std::memcmp(&x.envelopeStats(),&y.envelopeStats(),sizeof x.envelopeStats()));
    assert(x.readyStart(n)==y.readyStart(n)&&x.heavyStart(n)==y.heavyStart(n)&&x.buildStart(n)==y.buildStart(n));
    assert(x.envelopeEntries()==y.envelopeEntries()&&x.envelopeBytes()==y.envelopeBytes()&&x.envelopeValid()==y.envelopeValid()&&x.deferred()==y.deferred());
    assert(x.envelopeReadyMilliseconds()==y.envelopeReadyMilliseconds()&&x.envelopeHeavyMilliseconds()==y.envelopeHeavyMilliseconds()&&x.envelopeBuildMilliseconds()==y.envelopeBuildMilliseconds()&&x.envelopeClassifyMilliseconds()==y.envelopeClassifyMilliseconds());
    assert(x.hasPendingBuild()==y.hasPendingBuild()&&x.pendingBuildVertices()==y.pendingBuildVertices()&&x.borrowedMicroseconds()==y.borrowedMicroseconds());
}
static void spin(double us){const auto end=std::chrono::steady_clock::now()+std::chrono::nanoseconds(std::int64_t(us*1000));while(std::chrono::steady_clock::now()<end){}}
int main(int argc,char** argv){
    assert(argc>=3);const std::string mode=argv[2];auto skin=load(argv[1]);assert(SkinEnvelope::supports(skin));
#ifdef NORTHLIGHT_TEST_CLOCK
    if(mode=="equivalence"||mode=="stress"){
        const bool stress=mode=="stress";const unsigned frames=argc>3?unsigned(std::atoi(argv[3])):600;
        Scene scene(skin,stress?260:220);World legacy(scene.programs),async(scene.programs);warm(legacy,scene);warm(async,scene);
        size_t packets=0,valid=0,budgetFrames=0,cancelled=0,violations=0,enclosedVertices=0,lateJoins=0,differentValid=0,unsupported=0;std::mt19937 pick(5);
        for(unsigned f=0;f<frames;++f){
            const size_t count=400+(size_t(f)*7919)%601;scene.frame(f,count,stress,{&legacy,&async});packets+=count;
            NorthlightDiagnostics::configure(true);const bool sampled=f%7==0;TestClock::step=200+std::int64_t((f*2654435761u)%3800);
            legacy.legacyPass();
            async.replayBoundsKick();assert(async.replayBoundsWorker.pending());
            for(auto& p:async.replays)assert(!p->pointBounds.valid&&p->boundsWork.kind==NorthlightReplayBounds::WorkKind::Unknown);
            const bool cancel=stress&&f%11==5,violate=stress&&f%13==3;size_t victim=0;
            if(violate){victim=pick()%async.replays.size();async.replays[victim]->constants=scene.banks[0].data()+4;} /* protocol breach: pointer swap */
            if(stress){for(auto& p:async.replays)p->otherState+=1;scene.churn(3);} /* concurrent render-thread work */
            switch(f%4){case 0:lateJoins++;break;case 1:spin(150);break;case 2:std::this_thread::yield();break;default:spin(900);}
            if(cancel){async.replayBoundsAbandon();++cancelled;
                for(auto& p:async.replays)assert(!p->pointBounds.valid);for(auto& item:async.replayBoundsWorker.items())assert(!item.owner&&!item.prepared);
                async.legacyPass();continue;} /* the synchronous pass then runs, as in render(); caches diverge from here */
            async.replayBoundsJoin();assert(!async.replayBoundsWorker.pending());
            for(auto& item:async.replayBoundsWorker.items())assert(!item.owner&&!item.prepared);
            assert(async.replays.size()==legacy.replays.size());
            if(!stress){
                assert(async.pointBoundsValid==legacy.pointBoundsValid&&async.pointBoundVertices==legacy.pointBoundVertices&&async.pointBoundOperations==legacy.pointBoundOperations);
                if(sampled&&!(async.logs&&sameVisits(async.visits,legacy.visits))){auto&a=async.visits;auto&b=legacy.visits;std::printf("f=%u logs=%u %zu/%zu %zu/%zu %zu/%zu %zu/%zu %zu/%zu %zu/%zu %u/%u\n",f,async.logs,a.cheapVisited,b.cheapVisited,a.heavyVisited,b.heavyVisited,a.buildVisited,b.buildVisited,a.reservedCheapVisited,b.reservedCheapVisited,a.boundVertices,b.boundVertices,a.boundOperations,b.boundOperations,a.valid,b.valid);assert(false);}
                compareCaches(legacy,async,count);
            }
            for(size_t i=0;i<count;++i){const auto& a=*async.replays[i];const auto& l=*legacy.replays[i];
                if(violate&&i==victim){assert(!a.pointBounds.valid);++violations;continue;}
                if(!stress)assert(same(a.pointBounds,l.pointBounds)&&sameWork(a.boundsWork,l.boundsWork));
                else{if(a.pointBounds.valid&&l.pointBounds.valid)assert(same(a.pointBounds,l.pointBounds));else differentValid+=a.pointBounds.valid!=l.pointBounds.valid;}
                valid+=a.pointBounds.valid;unsupported+=a.boundsWork.kind==NorthlightReplayBounds::WorkKind::Unsupported;
                if(a.pointBounds.valid&&pick()%97==0)enclosedVertices+=enclosed(*scene.programs[a.originalShader],*a.shared,a.constants,async.context.inverseView,a.pointBounds);
            }
            budgetFrames+=legacy.replayBoundsCache.envelopeStats().timeDeferred>0;
            if(!stress)scene.churn(2); /* after both passes: identical expiry for both caches */
            async.logs=0;
        }
        std::printf("{\"mode\":\"%s\",\"frames\":%u,\"packets\":%zu,\"valid\":%zu,\"timeBudgetFrames\":%zu,\"immediateJoins\":%zu,\"cancelled\":%zu,\"protocolViolationsFailedOpen\":%zu,\"validSetDifferences\":%zu,\"unsupported\":%zu,\"enclosedVertices\":%zu}\n",
            mode.c_str(),frames,packets,valid,budgetFrames,lateJoins,cancelled,violations,differentValid,unsupported,enclosedVertices);
        assert(valid>0&&enclosedVertices>0&&(stress||budgetFrames>frames/4));
        return 0;
    }
#else
    if(mode=="bench"){
        // Real clock, production budgets. Sampled logging off, as on 119 of 120 frames.
        using Ms=std::chrono::duration<double,std::milli>;const auto now=[]{return std::chrono::steady_clock::now();};
        auto median=[](std::vector<double> v,double q){std::sort(v.begin(),v.end());return v[size_t(q*double(v.size()-1))];};
        std::printf("[");bool first=true;unsigned f=0;
        for(unsigned mix=0;mix<2;++mix)for(size_t count:{400,700,1000}){
            /* mix 1: half the meshes on the general interval evaluator with up to 12 bone tuples each (budget-bound, like the game logs) */
            Scene scene(skin,260,mix?12:3,mix?4:2);World legacy(scene.programs),async(scene.programs);warm(legacy,scene);warm(async,scene);
            std::vector<double> legacyMs,kickMs,immediateMs,overlapMs,workerMs,lagMs;size_t legacyValid=0,asyncValid=0;
            for(unsigned n=0;n<330;++n,++f){scene.frame(f,count,false,{&legacy,&async});NorthlightDiagnostics::configure(false);
                auto t=now();legacy.legacyPass();const double l=Ms(now()-t).count();
                t=now();async.replayBoundsKick();const double k=Ms(now()-t).count();
                const bool overlap=n&1;if(overlap)spin(1000);
                t=now();async.replayBoundsJoin();const double j=Ms(now()-t).count();
                const auto& items=async.replayBoundsWorker.items();(void)items;
                if(n<30)continue; /* warm envelopes */
                legacyMs.push_back(l);kickMs.push_back(k);(overlap?overlapMs:immediateMs).push_back(k+j);
                legacyValid+=legacy.pointBoundsValid;asyncValid+=async.pointBoundsValid;
                for(size_t i=0;i<count;++i){const auto& a=async.replays[i]->pointBounds;const auto& b=legacy.replays[i]->pointBounds;if(a.valid&&b.valid)assert(same(a,b));}
            }
            // Worker-side timing: one more job each, finished late.
            for(unsigned n=0;n<60;++n,++f){scene.frame(f,count,false,{&async});async.replayBoundsKick();spin(1500);
                const auto r=async.replayBoundsWorker.finish();workerMs.push_back(r.workerMs);lagMs.push_back(r.startLagMs);NorthlightReplayBoundsJob::apply(async.replayBoundsWorker.items(),async.replays,true);}
            std::printf("%s{\"mix\":\"%s\",\"draws\":%zu,\"envelopes\":%zu,\"legacyMainMs\":[%.3f,%.3f],\"asyncKickMs\":[%.3f,%.3f],\"asyncMainImmediateJoinMs\":[%.3f,%.3f],\"asyncMainAfter1msWorkMs\":[%.3f,%.3f],\"workerMs\":[%.3f,%.3f],\"startLagMs\":[%.3f,%.3f],\"legacyValidPerFrame\":%.1f,\"asyncValidPerFrame\":%.1f}",
                first?"":",",mix?"heavy":"light",count,async.replayBoundsCache.envelopeEntries(),median(legacyMs,.5),median(legacyMs,.9),median(kickMs,.5),median(kickMs,.9),median(immediateMs,.5),median(immediateMs,.9),
                median(overlapMs,.5),median(overlapMs,.9),median(workerMs,.5),median(workerMs,.9),median(lagMs,.5),median(lagMs,.9),double(legacyValid)/300,double(asyncValid)/300);first=false;
        }
        // 300 distinct shader/declaration pairs (> metadata EntryLimit 128): kick cost, cold and thrashing.
        {Scene scene(skin,260);std::vector<IDirect3DVertexShader9> many(300);World w(scene.programs);
            for(auto& sh:many)scene.programs[&sh]=scene.programs[&scene.shaders[0]];
            std::vector<double> cold,steady;size_t capped=0;
            for(unsigned n=0;n<120;++n){scene.frame(n,900,false,{&w});for(size_t i=0;i<w.replays.size();++i)w.replays[i]->originalShader=&many[(i*37)%many.size()];
                const auto t=now();w.replayBoundsKick();const double k=Ms(now()-t).count();w.replayBoundsJoin();capped+=w.replayBoundsCapped;
                (n<10?cold:steady).push_back(k);}
            std::printf(",{\"distinctPairs\":300,\"draws\":900,\"kickColdMs\":[%.3f,%.3f],\"kickThrashMs\":[%.3f,%.3f],\"cappedPerFrame\":%.1f}",median(cold,.5),median(cold,.9),median(steady,.5),median(steady,.9),double(capped)/120);}
        std::printf("]\n");return 0;
    }
#endif
    return 1;
}
"""

bench = r"""
#include "replay_bounds_job.h"
#include "replay_bounds_metadata.h"
"""

def build(root, name, source, flags, defines=()):
    (root / (name + '.cpp')).write_text(source)
    subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags, *defines,
                    '-I', str(root), *fp.test_include_flags(), str(root / (name + '.cpp')), '-o', str(root / name)], check=True)
    return root / name

with tempfile.TemporaryDirectory(prefix='northlight-bounds-job-') as tmp:
    root = Path(tmp)
    (root / 'd3d9.h').write_text(stub)
    source = clock + harness
    shader = str(client_fixtures.four_bone_vs3())
    runs = [('o2', ['-O2'], [('equivalence', '600'), ('stress', '400')]),
            ('asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'], [('equivalence', '200'), ('stress', '200')]),
            ('tsan', ['-O1', '-g', '-fsanitize=thread'], [('equivalence', '150'), ('stress', '200')])]
    for name, flags, modes in runs:
        binary = build(root, name, source, flags)
        for mode, frames in modes:
            out = subprocess.run([str(binary), shader, mode, frames], capture_output=True, text=True)
            if out.returncode or 'WARNING' in out.stderr or 'ERROR' in out.stderr:
                sys.exit(f'{name} {mode} failed ({out.returncode}):\n{out.stdout}{out.stderr[-6000:]}')
            print(name, out.stdout.strip())
    binary = build(root, 'bench', harness, ['-O2'])
    bench = json.loads(subprocess.run([str(binary), shader, 'bench'], check=True, capture_output=True, text=True).stdout)
    for row in bench:
        print('bench', json.dumps(row))
print('PASS replay bounds worker: async pass bit-identical to the 0.3.142 synchronous pass (bounds, work classes, visits, cache state and cursors) under a deterministic clock with early/late joins; stress: cancellation, mesh churn during the job, unknown programs and a protocol breach fail open; ASan/UBSan and TSan clean')
