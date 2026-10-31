#include "world_gi.h"
#include "worker_actor_memo.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
using namespace NorthlightGI;
struct Job {static int alive;Job(){++alive;}~Job(){--alive;}};int Job::alive=0;
static void memoTest(){
    NorthlightWorkerActorMemo<Job> memo;std::shared_ptr<const Job> none,a=std::make_shared<Job>(),b=std::make_shared<Job>();
    assert(!memo.matches(none)&&!memo.matches(a));memo.remember(a);assert(memo.matches(a)&&!memo.matches(b)&&!memo.matches(none));
    auto copy=a;assert(memo.matches(copy));a.reset();assert(memo.matches(copy));copy.reset();assert(Job::alive==1&&!memo.matches(b));
    memo.remember(b);assert(memo.matches(b));b.reset();assert(Job::alive==0);memo.remember(none);assert(memo.matches(none));
    auto next=std::make_shared<Job>();assert(!memo.matches(next));
    // Pointer equality is the old contract, even for a separately owned alias.
    memo.remember(next);auto alias=std::shared_ptr<const Job>(next.get(),[](const Job*){});assert(memo.matches(alias));
    auto emptyOwner=std::shared_ptr<const Job>(std::shared_ptr<const Job>{},next.get());
    memo.remember(emptyOwner);assert(!memo.matches(emptyOwner));memo.remember(none);
    // Failed decode never commits its identity; the next attempt must decode.
    try{throw std::bad_alloc();memo.remember(next);}catch(const std::bad_alloc&){}
    assert(!memo.matches(next)&&memo.matches(none));
}
static void quad(WorldScene& s,float z,unsigned material){
    unsigned base=unsigned(s.vertices.size());
    s.vertices.push_back({{-20,-20,z},{0,0,1},-2,-3});s.vertices.push_back({{20,-20,z},{0,0,1},3,-3});
    s.vertices.push_back({{20,20,z},{0,0,1},3,4});s.vertices.push_back({{-20,20,z},{0,0,1},-2,4});
    s.triangles.push_back({base,base+1,base+2,material});s.triangles.push_back({base,base+2,base+3,material});
}
static uint32_t seed=91273;static float random(float lo,float hi){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return lo+(hi-lo)*float(seed>>8)/16777216.f;}
struct Ray {Vec3 o,d;float lo,hi;};
int main(){
    memoTest();WorldScene scene;
    for(unsigned u=1;u<=3;++u)for(unsigned v=1;v<=3;++v){WorldMaterial m;m.addressU=u;m.addressV=v;m.width=m.height=2;m.alphaCutoff=.5f;
        m.rgba={255,0,0,0, 0,255,0,255, 0,0,255,127, 255,255,255,128};scene.materials.push_back(m);}
    scene.materials.push_back({});
    for(unsigned i=0;i<200;++i)quad(scene,float(i)*.4f,unsigned(i%10));
    BVH b;std::string error;assert(b.build(std::move(scene),error));
    std::vector<Ray> rays;for(unsigned i=0;i<50000;++i)rays.push_back({{random(-30,30),random(-30,30),random(-10,90)},
        {random(-1,1),random(-1,1),random(-2,2)},random(0,3),random(3,160)});
    for(float z:{0.f,.4f,40.f,79.6f})for(float lo:{0.f,.001f,1.f,40.f})for(float hi:{0.f,.001f,1.f,40.f,80.f})
        for(float sign:{-1.f,1.f})rays.push_back({{0,0,z},{0,0,sign},lo,hi});
    for(float invalid:{0.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        rays.push_back({{0,0,0},{0,0,invalid},0,100});rays.push_back({{invalid,0,0},{0,0,1},0,100});
        rays.push_back({{0,0,0},{0,0,1},invalid,100});rays.push_back({{0,0,0},{0,0,1},0,invalid});}
    unsigned checked=0;for(const auto& r:rays){assert(b.occluded(r.o,r.d,r.hi,r.lo)==b.trace(r.o,r.d,r.lo,r.hi).hit);++checked;}
    // Every sampler mode independently, including fully transparent and opaque
    // limits and cutoff equality. Avoid an opaque later layer hiding alpha bugs.
    for(unsigned u=1;u<=3;++u)for(unsigned v=1;v<=3;++v)for(float cutoff:{0.f,127.f/255.f,.5f,1.f}){
        WorldScene s;WorldMaterial m;m.width=m.height=2;m.addressU=u;m.addressV=v;m.alphaCutoff=cutoff;
        m.rgba={255,0,0,0, 0,255,0,255, 0,0,255,127, 255,255,255,128};s.materials.push_back(m);quad(s,1,0);BVH a;assert(a.build(std::move(s),error));
        for(unsigned i=0;i<1000;++i){Vec3 o(random(-21,21),random(-21,21),0);assert(a.occluded(o,{0,0,1},2,0)==a.trace(o,{0,0,1},0,2).hit);++checked;}}
    volatile unsigned any=0,closest=0;using Clock=std::chrono::steady_clock;
    auto t=Clock::now();for(unsigned repeat=0;repeat<4;++repeat)for(const auto& r:rays)closest+=b.trace(r.o,r.d,r.lo,r.hi).hit;
    double full=std::chrono::duration<double,std::milli>(Clock::now()-t).count();
    t=Clock::now();for(unsigned repeat=0;repeat<4;++repeat)for(const auto& r:rays)any+=b.occluded(r.o,r.d,r.hi,r.lo);
    double fast=std::chrono::duration<double,std::milli>(Clock::now()-t).count();assert(any==closest);
    std::printf("{\"status\":\"pass\",\"equivalence_cases\":%u,\"nearest_ms\":%.3f,\"any_hit_ms\":%.3f,\"speedup\":%.3f,\"memo_releases_packets\":true}\n",checked,full,fast,full/fast);
}
