// 0.3.138 GI quality knobs: defaults are 0.3.137's literals, the default solve
// path gives bit-identical probes, GIThreads>1 prefetch (production helper)
// gives the same probes, cache contents and LRU order as the serial loop.
#include "quality_settings.h"
#include "gi_solve_pool.h"
#include "world_gi.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
using namespace NorthlightGI;
static void quad(WorldScene& s,Vec3 a,Vec3 b,Vec3 c,Vec3 d,uint32_t m){
    uint32_t n=uint32_t(s.vertices.size());Vec3 normal=normalized(cross(b-a,c-a));
    for(Vec3 p:{a,b,c,d})s.vertices.push_back({p,normal,.5f,.5f});
    s.triangles.push_back({n,n+1,n+2,m});s.triangles.push_back({n,n+2,n+3,m});
}
/* Field-wise bit comparison (Probe has tail padding after `valid`). */
static bool bits(const void* a,const void* b,size_t n){return !std::memcmp(a,b,n);}
static bool same(const Probe& a,const Probe& b){
    return bits(&a.position,&b.position,sizeof a.position)&&bits(a.sh,b.sh,sizeof a.sh)&&bits(a.moments,b.moments,sizeof a.moments)&&
        a.samples==b.samples&&a.backFaceSamples==b.backFaceSamples&&bits(&a.maxDistance,&b.maxDistance,sizeof a.maxDistance)&&a.valid==b.valid;}
/* The worker's window loop (world_renderer.h), serial or with the production prefetch helper. */
template<class Solve> static unsigned window(ProbeCache& cache,Vec3 camera,SolvePool* pool,Solve solve){
    const Vec3 origin=probeWindowOrigin(camera);const auto order=probeSolveOrder(camera);
    std::array<Probe,8> prefetched;unsigned mask=0,solved=0;
    for(unsigned cursor=0;cursor<order.size();++cursor){
        if(cursor%8==0&&pool)mask=prefetchProbeGroup(*pool,cache,origin,order,cursor,solve,prefetched);
        const Vec3 p=probeWindowPosition(origin,order[cursor]);ProbeGridKey key;const bool onGrid=probeGridKey(p,key);assert(onGrid);(void)onGrid;
        Probe probe;if(cache.get(key,probe))continue;
        probe=mask&(1u<<(cursor%8))?prefetched[cursor%8]:solve(key,p);cache.put(key,probe);++solved;
    }
    return solved;
}
int main(){
    // An empty tree returns a miss for zero and nonfinite ray directions too.
    {WorldScene scene;BVH tree;std::string error;assert(tree.build(std::move(scene),error,true));
     for(Vec3 direction: {Vec3(0,0,0),Vec3(0,0,-1),Vec3(std::numeric_limits<float>::infinity(),0,0)}){
         const auto hit=tree.trace({0,0,1},direction,.1f,10.f);
         assert(!hit.hit&&!hit.backFace&&hit.distance==10.f&&hit.triangle==UINT32_MAX);
         assert(!tree.occluded({0,0,1},direction,10.f,.1f));}}
    // A scene with triangles but no nondegenerate primitives builds an empty tree.
    {WorldScene scene;scene.materials.push_back({});scene.vertices.push_back({{0,0,0},{0,0,1},0,0});
     scene.triangles.push_back({0,0,0,0});BVH tree;std::string error;
     assert(tree.build(std::move(scene),error,true));
     assert(!tree.trace({0,0,1},{0,0,-1}).hit);assert(!tree.occluded({0,0,1},{0,0,-1},10));}
    // Zero alpha cutoff accepts every representable texture alpha, including zero.
    {WorldScene scene;WorldMaterial material;material.width=256;material.height=1;material.alphaCutoff=0.f;
     material.rgba.resize(256*4);for(unsigned i=0;i<256;++i)material.rgba[i*4+3]=uint8_t(i);
     scene.materials.push_back(std::move(material));quad(scene,{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0},0);
     BVH tree;std::string error;assert(tree.build(std::move(scene),error,true));
     assert(tree.trace({0,0,1},{0,0,-1}).hit);assert(tree.occluded({0,0,1},{0,0,-1},10));}
    const NorthlightQuality::Settings d{};
    /* Defaults are the 0.3.137 constants: rays literal 64, Lighting::maxBounces 3, quantize step 8. */
    assert(d.gi==1&&d.giRays==64&&d.giBounces==Lighting{}.maxBounces&&d.giBounces==3&&d.giProbeMoveStep==8&&d.giDynamicProbes==1&&d.giThreads==1);
    assert(NorthlightQuality::giActorCapture(d)&&d.giFastBVH==1);
    /* GI off: no actor capture, F8 cannot show the pass, no solver threads; thread cap cores-1. */
    {auto off=d;off.gi=0;assert(!NorthlightQuality::giActorCapture(off)&&!NorthlightQuality::giPass(off,true)&&NorthlightQuality::giSolverThreads(off,8)==0);
     assert(NorthlightQuality::giPass(d,true)&&!NorthlightQuality::giPass(d,false));
     auto t=d;t.giThreads=4;assert(NorthlightQuality::giSolverThreads(t,16)==4&&NorthlightQuality::giSolverThreads(t,4)==3&&NorthlightQuality::giSolverThreads(t,2)==1&&NorthlightQuality::giSolverThreads(t,0)==1);
     assert(NorthlightQuality::giSolverThreads(d,16)==1&&NorthlightQuality::giSolverThreads(d,1)==1);}
    /* GIProbeMoveStep: the request camera lags the real camera by < step*sqrt(3) per axis cell,
       so at 16 the worker's geometry trigger fires by 32+27.8 < 96 (coverage) from the build centre. */
    for(unsigned step:{8u,16u}){uint32_t seed=7;auto rnd=[&]{seed=seed*1664525u+1013904223u;return float(seed>>8)/16777216.f;};
      auto cell=[&](Vec3 p){return Vec3(std::floor(p.x/step)*step,std::floor(p.y/step)*step,std::floor(p.z/step)*step);};
      Vec3 cam(0,0,0),requested=cam;float worst=0;
      for(int i=0;i<200000;++i){cam=cam+Vec3(rnd()-.3f,rnd()-.4f,(rnd()-.5f)*.2f);
        const Vec3 a=cell(cam),b=cell(requested);if(a.x!=b.x||a.y!=b.y||a.z!=b.z)requested=cam;
        const Vec3 d3=cam-requested;worst=std::max(worst,std::sqrt(dot(d3,d3)));}
      assert(worst<float(step)*1.7321f&&32+worst<96-24);}
    auto perf=NorthlightQuality::preset(NorthlightQuality::Preset::Performance),bal=NorthlightQuality::preset(NorthlightQuality::Preset::Balanced);
    assert(perf.giRays==32&&perf.giBounces==2&&perf.giProbeMoveStep==16&&!NorthlightQuality::giActorCapture(perf)&&perf.gi==1&&perf.giThreads==1);
    assert(bal.giRays==48&&bal.giBounces==3&&bal.giProbeMoveStep==8&&bal.giDynamicProbes==0&&!NorthlightQuality::giActorCapture(bal)); /* 0.3.167 */
    {std::istringstream in("[Quality]\nGI=0\nGIThreads=4\nGIRays=15\nGIProbeMoveStep=17\n");std::vector<std::string> problems;
     auto s=NorthlightQuality::load(&in,nullptr,problems);
     assert(s.gi==0&&s.giThreads==4&&s.giRays==64&&s.giProbeMoveStep==8&&problems.size()==2&&!NorthlightQuality::giActorCapture(s));
     assert(NorthlightQuality::describe(s).find("GI=0(file)")!=std::string::npos&&NorthlightQuality::describe(s).find("GIRays=64(default)")!=std::string::npos);}
    /* A small closed courtyard with a roof gap: occlusion, bounces, back faces. */
    WorldScene scene;scene.materials.push_back({});scene.materials.back().albedo=Vec3(.6f,.5f,.4f);
    const float r=40;
    quad(scene,{-r,-r,0},{r,-r,0},{r,r,0},{-r,r,0},0);
    quad(scene,{-r,-r,0},{-r,-r,30},{r,-r,30},{r,-r,0},0);quad(scene,{-r,r,0},{r,r,0},{r,r,30},{-r,r,30},0);
    quad(scene,{-r,-r,0},{-r,r,0},{-r,r,30},{-r,-r,30},0);quad(scene,{-8,-8,0},{-8,-8,12},{8,-8,12},{8,-8,0},0);
    quad(scene,{-r,-r,30},{-r,r,30},{0,r,30},{0,-r,30},0);
    BVH bvh;std::string error;const bool built=bvh.build(std::move(scene),error);assert(built);(void)built;
    Lighting today;today.points.push_back({});today.points.back().position=Vec3(4,4,6);today.points.back().irradiance=Vec3(2,1.5f,1);today.points.back().attenuationEnd=20;
    Lighting knob=today;knob.maxBounces=d.giBounces;
    const auto todayPrepared=prepareLighting(today),knobPrepared=prepareLighting(knob);
    /* Default knob path == 0.3.137 literal path, bit for bit. */
    unsigned checked=0,valid=0;
    for(float x=-48;x<=48;x+=8)for(float y=-48;y<=48;y+=8)for(float z=-8;z<=40;z+=8){ProbeGridKey key;const Vec3 p(x,y,z);if(!probeGridKey(p,key))continue;
        const Probe a=solveProbePrepared(bvh,p,todayPrepared,64,probeSeed(key));valid+=a.valid;
        assert(same(a,solveProbePrepared(bvh,p,knobPrepared,d.giRays,probeSeed(key))));++checked;}
    std::printf("default path: %u probes, %u valid\n",checked,valid);assert(checked==13*13*7&&valid>checked/3&&valid<checked);
    /* Reduced settings still produce valid, finite probes in open space. */
    {Lighting low=today;low.maxBounces=perf.giBounces;auto lp=prepareLighting(low);ProbeGridKey key;const Vec3 p(24,24,8);const bool onGrid=probeGridKey(p,key);assert(onGrid);
     const Probe a=solveProbePrepared(bvh,p,lp,perf.giRays,probeSeed(key)),b=solveProbePrepared(bvh,p,todayPrepared,64,probeSeed(key));
     assert(a.valid&&b.valid&&a.samples==32&&std::isfinite(a.sh[0].x)&&!same(a,b));}
    /* GIThreads 2..4: identical probes, cache contents and LRU order to the serial loop,
       including a small cache that evicts inside a group, and a moved second window. */
    auto solve=[&](ProbeGridKey k,Vec3 p){return solveProbePrepared(bvh,p,knobPrepared,d.giRays,probeSeed(k));};
    for(size_t capacity:{size_t(4096),size_t(1000)}){
        ProbeCache serial(capacity);unsigned serialSolved=window(serial,Vec3(1,2,3),nullptr,solve);serialSolved+=window(serial,Vec3(21,-10,7),nullptr,solve);
        for(unsigned threads=2;threads<=4;++threads){
            SolvePool pool(threads-1,nullptr);ProbeCache parallel(capacity);
            unsigned solved=window(parallel,Vec3(1,2,3),&pool,solve);solved+=window(parallel,Vec3(21,-10,7),&pool,solve);
            assert(solved==serialSolved&&parallel.size()==serial.size());
            const auto a=serial.atlas(),b=parallel.atlas();assert(a.size()==b.size());
            for(size_t i=0;i<a.size();++i)assert(a[i].occupied==b[i].occupied&&!std::memcmp(&a[i].key,&b[i].key,sizeof a[i].key)&&same(a[i].probe,b[i].probe));
        }
        std::printf("GIThreads: capacity %zu, %u solves, 2..4 threads identical to serial\n",capacity,serialSolved);
    }
    // A single missing probe is solved exactly once and a failure still propagates.
    {SolvePool pool(2,nullptr);ProbeCache cache;std::vector<unsigned> order{0,1,2,3,4,5,6,7};
     const Vec3 origin{};for(unsigned i=1;i<8;++i){const Vec3 p=probeWindowPosition(origin,i);ProbeGridKey key;assert(probeGridKey(p,key));cache.put(key,solve(key,p));}
     std::array<Probe,8> out{};unsigned calls=0;
     auto counted=[&](ProbeGridKey key,Vec3 p){++calls;return solve(key,p);};
     assert(prefetchProbeGroup(pool,cache,origin,order,0,counted,out)==1&&calls==1);
     ProbeGridKey key;const Vec3 p=probeWindowPosition(origin,0);assert(probeGridKey(p,key)&&same(out[0],solve(key,p)));
     bool thrown=false;try{prefetchProbeGroup(pool,cache,origin,order,0,[](ProbeGridKey,Vec3)->Probe{throw std::runtime_error("single");},out);}
     catch(const std::runtime_error&){thrown=true;}assert(thrown);}
    /* Pool: exceptions propagate after all jobs stop; empty runs are no-ops. */
    {SolvePool pool(3,nullptr);pool.run(0,[](size_t){assert(false);});bool thrown=false;
     try{pool.run(64,[](size_t i){if(i==17)throw std::runtime_error("x");});}catch(const std::runtime_error&){thrown=true;}assert(thrown);
     std::vector<int> hits(1000);pool.run(hits.size(),[&](size_t i){++hits[i];});for(int h:hits)assert(h==1);}
    std::printf("gi quality: defaults == 0.3.137 literals, %u probes bit-identical on the default path, presets/parse, pool exceptions passed\n",checked);
}
