#include "world_dynamic_probes.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>

using namespace NorthlightGI;
static void quad(WorldScene& s,Vec3 a,Vec3 b,Vec3 c,Vec3 d) {
    unsigned n=unsigned(s.vertices.size());Vec3 normal=normalized(cross(b-a,c-a));
    for(auto p:{a,b,c,d})s.vertices.push_back({p,normal});
    s.triangles.push_back({n,n+1,n+2,0});s.triangles.push_back({n,n+2,n+3,0});
}
static BVH build(WorldScene s){BVH b;std::string error;assert(b.build(std::move(s),error));return b;}
static Probe probe(ProbeGridKey key) {
    Probe p;p.position={float(key.x)*8,float(key.y)*8,float(key.z)*8};p.valid=true;p.samples=64;p.sh[0]={1,2,3};
    for(auto& moment:p.moments){moment.mean=123;moment.meanSquare=45678;}return p;
}
static void sameProbe(const Probe& a,const Probe& b) {
    assert(a.valid==b.valid&&a.samples==b.samples);
    assert(a.position.x==b.position.x&&a.position.y==b.position.y&&a.position.z==b.position.z);
    for(unsigned i=0;i<4;++i)assert(a.sh[i].x==b.sh[i].x&&a.sh[i].y==b.sh[i].y&&a.sh[i].z==b.sh[i].z);
    for(unsigned i=0;i<6;++i)assert(a.moments[i].mean==b.moments[i].mean&&a.moments[i].meanSquare==b.moments[i].meanSquare);
}
int main() {
    Lighting generation;generation.additionalDirections={{{0,0,1},{.1f,.2f,.3f}},{{1,0,0},{.4f,.3f,.2f}}};
    generation.points={{{12,34,56},{2,3,4},1,8}};
    auto compatible=[&](const Lighting& latest){return staticFallbackCompatible("Azeroth","Azeroth","Azeroth",{0,0,0},{0,0,0},{32,0,0},generation,latest);};
    assert(compatible(generation));
    auto latest=generation;latest.sunDirection.x+=.001f;latest.skyRadiance.x+=.001f;assert(compatible(latest));
    // Each mutation models a latest camera request that overwrote an earlier
    // light-change request. Compatibility must inspect data, not reason bits.
    for(unsigned changed=0;changed<15;++changed) {
        latest=generation;
        switch(changed) {
            case 0:latest.sunDirection.x+=.1f;break;
            case 1:latest.sunIrradiance.x+=.1f;break;
            case 2:latest.skyRadiance.x+=.1f;break;
            case 3:latest.additionalDirections.pop_back();break;
            case 4:latest.additionalDirections[1].direction.z+=.1f;break;
            case 5:latest.additionalDirections[1].irradiance.y+=.1f;break;
            case 6:latest.worldUp.x=.01f;break;
            case 7:latest.maxDistance+=1;break;
            case 8:latest.rayBias*=2;break;
            case 9:latest.maxBounces+=1;break;
            case 10:latest.points.clear();break;
            case 11:latest.points[0].position.x+=1;break;
            case 12:latest.points[0].irradiance.y+=.01f;break;
            case 13:latest.points[0].attenuationEnd+=1;break;
            case 14:latest.sunDirection.x=std::numeric_limits<float>::quiet_NaN();break;
        }
        assert(!compatible(latest));
    }
    assert(!staticFallbackCompatible("Azeroth","Azeroth","Kalimdor",{},{},{},generation,generation));
    assert(!staticFallbackCompatible("Azeroth","Kalimdor","Azeroth",{},{},{},generation,generation));
    assert(!staticFallbackCompatible("","","",{},{},{},generation,generation));
    assert(!staticFallbackCompatible("Azeroth","Azeroth","Azeroth",{},{},{64.01f,0,0},generation,generation));
    assert(!staticFallbackCompatible("Azeroth","Azeroth","Azeroth",{-64,0,0},{},{64,0,0},generation,generation));
    assert(staticFallbackCompatible("Azeroth","Azeroth","Azeroth",{-32,0,0},{},{64,0,0},generation,generation));
    assert(!staticFallbackCompatible("Azeroth","Azeroth","Azeroth",{},{},{std::numeric_limits<float>::infinity(),0,0},generation,generation));
    WorldScene actor;actor.materials.push_back({});quad(actor,{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0});
    DynamicProbeLayer layer;layer.reset(&actor);
    // Non-finite probe positions never gain dynamic influence or a signature.
    DynamicProbeLayer emptyLayer;emptyLayer.reset(nullptr);
    for(float value:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()})
        for(unsigned axis=0;axis<3;++axis){Vec3 position{};if(axis==0)position.x=value;else if(axis==1)position.y=value;else position.z=value;
            assert(layer.distanceSquared(position)==DynamicZeroDistance*DynamicZeroDistance);
            assert(layer.signature(position)==emptyLayer.signature(position));}
    assert(layer.weight({0,0,0})==1&&layer.weight({13,0,0})==1);
    assert(std::fabs(layer.weight({19,0,0})-.5f)<1e-6f&&layer.weight({25,0,0})==0);
    assert(std::fabs(layer.weight({13.001f,0,0})-1)<1e-6f&&layer.weight({24.999f,0,0})<1e-6f);
    // Unreferenced snapshot vertices must not enlarge dynamic influence.
    actor.vertices.push_back({{8000,8000,8000},{}});layer.reset(&actor);
    assert(layer.weight({8000,8000,8000})==0);
    ProbeCache world;
    for(int x=-7;x<9;++x)world.put({x,0,0},probe({x,0,0}));
    auto original=world.atlas();auto composed=original;unsigned reused=0,computed=0;
    auto solve=[](ProbeGridKey key,Vec3){Probe p=probe(key);p.sh[0]={4,5,6};p.moments[0].mean=1;return p;};
    assert(layer.apply(composed,solve,[]{return false;},reused,computed));assert(computed>0&&reused==0);
    for(const auto& entry:composed)if(entry.occupied) {
        const auto& base=original[probeAtlasIndex(entry.key)];
        assert(entry.key==base.key&&entry.probe.valid==base.probe.valid);
        for(unsigned i=0;i<6;++i)assert(entry.probe.moments[i].mean==base.probe.moments[i].mean);
        if(layer.weight(entry.probe.position)==0)sameProbe(entry.probe,base.probe);
    }
    assert(composed[probeAtlasIndex({0,0,0})].probe.sh[0].x==4);
    // Camera window motion touches static LRU order, not world values or actor
    // generations; all previously resident slots keep their lighting/coverage.
    Probe ignored;assert(world.get({5,0,0},ignored));auto cameraMoved=world.atlas();unsigned hits=0,misses=0;
    assert(layer.apply(cameraMoved,solve,[]{return false;},hits,misses));assert(hits==computed&&misses==0);
    for(unsigned i=0;i<cameraMoved.size();++i)if(cameraMoved[i].occupied)sameProbe(cameraMoved[i].probe,composed[i].probe);
    // Actor disappears or changes pose: entire static atlas remains resident,
    // including a far previous camera window. No stale dynamic result survives.
    layer.reset(nullptr);auto absent=world.atlas();hits=misses=0;
    assert(layer.apply(absent,solve,[]{return false;},hits,misses));assert(hits==0&&misses==0);
    for(unsigned i=0;i<absent.size();++i)if(absent[i].occupied)sameProbe(absent[i].probe,original[i].probe);
    layer.reset(&actor);auto invalid=world.atlas();
    assert(layer.apply(invalid,[](ProbeGridKey key,Vec3){auto p=probe(key);p.valid=false;return p;},[]{return false;},hits,misses));
    for(unsigned i=0;i<invalid.size();++i)if(invalid[i].occupied)sameProbe(invalid[i].probe,original[i].probe);
    // Cancellation works on an unpublished private export. Restart can reuse
    // solved work, while the old completed publication is untouched.
    layer.reset(&actor);auto partial=original;unsigned polls=0;hits=misses=0;
    assert(!layer.apply(partial,solve,[&]{return ++polls>1;},hits,misses));
    // Fallback must export the static cache again, never the partly overlaid
    // private result; compatibility above gates which request may receive it.
    auto fallback=world.atlas();
    for(unsigned i=0;i<fallback.size();++i)if(fallback[i].occupied)sameProbe(fallback[i].probe,original[i].probe);
    auto resumed=original;assert(layer.apply(resumed,solve,[]{return false;},hits,misses));
    for(unsigned i=0;i<composed.size();++i)if(composed[i].occupied)sameProbe(resumed[i].probe,composed[i].probe);
    // Real transport: a captured, lit red plane changes nearby indirect
    // radiance; removing it recovers exactly the static open-sky solution.
    BVH empty=build({});WorldScene wall;WorldMaterial red;red.albedo={.8f,.02f,.02f};wall.materials.push_back(red);
    quad(wall,{2,-8,-8},{2,-8,8},{2,8,8},{2,8,-8});BVH moving=build(wall);
    Lighting light;light.sunDirection={-1,0,0};light.sunIrradiance={3,3,3};light.skyRadiance={.1f,.1f,.1f};
    Probe staticProbe=solveProbe(empty,{0,0,0},light,4096,probeSeed({0,0,0}));assert(staticProbe.valid);
    world.clear();world.put({0,0,0},staticProbe);auto lit=world.atlas();layer.reset(&wall);light.movingGeometry=&moving;
    hits=misses=0;assert(layer.apply(lit,[&](ProbeGridKey key,Vec3 p){return solveProbe(empty,p,light,4096,probeSeed(key));},[]{return false;},hits,misses));
    const auto corrected=lit[probeAtlasIndex({0,0,0})].probe;
    assert(corrected.sh[0].x>staticProbe.sh[0].x+.02f&&corrected.sh[0].x>corrected.sh[0].y*2);
    assert(corrected.moments[0].mean==staticProbe.moments[0].mean);
    // Hard work bound even when an observed large draw overlaps every slot.
    WorldScene huge;huge.materials.push_back({});quad(huge,{-1000,-1000,0},{1000,-1000,0},{1000,1000,0},{-1000,1000,0});
    layer.reset(&huge);world.clear();for(int z=-1;z<=1;++z)for(int y=0;y<16;++y)for(int x=0;x<16;++x)world.put({x,y,z},probe({x,y,z}));
    auto many=world.atlas();hits=misses=0;assert(layer.apply(many,solve,[]{return false;},hits,misses));assert(misses==DynamicSolveBudget&&hits==0);
    // Progressive fill: later publications solve the remaining candidates by
    // budget while keeping the already solved corrections; once the 512
    // ceiling is solved, an unchanged actor scene costs no solves at all.
    for(unsigned round=1;round<4;++round){many=world.atlas();hits=misses=0;assert(layer.apply(many,solve,[]{return false;},hits,misses));assert(misses==DynamicSolveBudget&&hits==round*DynamicSolveBudget);}
    many=world.atlas();hits=misses=0;assert(layer.apply(many,solve,[]{return false;},hits,misses));assert(hits==DynamicCandidateCeiling&&misses==0);
    // Below the ceiling (256 probes on the plane): idle jitter in either
    // direction, at a non-integer anchor, keeps every correction (anchored
    // hysteresis); a real move re-solves by budget and the rest keep their
    // previous correction until their turn.
    unsigned calls=0;auto tagged=[&](ProbeGridKey key,Vec3){Probe p=probe(key);p.sh[0]={float(1000+ ++calls),5,6};return p;};
    auto shifted=[&](float dz){WorldScene s=huge;for(auto& v:s.vertices)v.position.z+=dz;return s;};
    auto count=[&](const std::vector<ProbeAtlasEntry>& a,float low,float high){unsigned n=0;for(const auto& e:a)if(e.occupied&&e.probe.sh[0].x>=low&&e.probe.sh[0].x<=high)++n;return n;};
    world.clear();for(int y=0;y<16;++y)for(int x=0;x<16;++x)world.put({x,y,0},probe({x,y,0}));
    WorldScene base=shifted(.9f);layer.reset(&base);
    many=world.atlas();hits=misses=0;assert(layer.apply(many,tagged,[]{return false;},hits,misses));assert(misses==DynamicSolveBudget&&hits==0);
    many=world.atlas();hits=misses=0;assert(layer.apply(many,tagged,[]{return false;},hits,misses));assert(misses==DynamicSolveBudget&&hits==DynamicSolveBudget);
    assert(count(many,1001,1256)==256);
    for(float dz:{.65f,1.15f,.9f,1.35f,.45f}){WorldScene pose=shifted(dz);layer.observe(&pose);
        many=world.atlas();hits=misses=0;assert(layer.apply(many,tagged,[]{return false;},hits,misses));assert(hits==256&&misses==0);}
    WorldScene moved=shifted(3.9f);layer.observe(&moved);
    many=world.atlas();hits=misses=0;assert(layer.apply(many,tagged,[]{return false;},hits,misses));assert(misses==DynamicSolveBudget&&hits==256-DynamicSolveBudget);
    assert(count(many,1257,1384)==128&&count(many,1001,1256)==128);
    many=world.atlas();hits=misses=0;assert(layer.apply(many,tagged,[]{return false;},hits,misses));assert(misses==DynamicSolveBudget&&hits==DynamicSolveBudget);
    assert(count(many,1257,1512)==256);
    // A correction solved under a different actor set is applied for at most
    // DynamicStaleLimit captures; after that the probe returns to static.
    for(unsigned i=0;i<DynamicStaleLimit+1;++i){WorldScene pose=shifted(3.9f+float(i+1)*2);layer.observe(&pose);
        many=world.atlas();hits=misses=0;assert(!layer.apply(many,tagged,[]{return true;},hits,misses));assert(hits==0&&misses==0);}
    {WorldScene pose=shifted(2);layer.observe(&pose);
     many=world.atlas();hits=misses=0;assert(layer.apply(many,tagged,[]{return false;},hits,misses));assert(misses==DynamicSolveBudget&&hits==0);
     assert(count(many,1513,1640)==128&&count(many,1,1)==128);}
    // A departed actor leaves no effect even though its corrections are retained.
    layer.observe(nullptr);many=world.atlas();hits=misses=0;assert(layer.apply(many,tagged,[]{return false;},hits,misses));assert(hits==0&&misses==0);
    for(unsigned i=0;i<many.size();++i)if(many[i].occupied)assert(many[i].probe.sh[0].x==1);
    assert(layer.retained()==256);
    for(unsigned i=0;i<17;++i)layer.observe(nullptr);assert(layer.retained()==0);
    // A second actor entering within signature range re-solves the affected
    // probes; the order of captured draws does not change identity.
    world.clear();for(int x=-7;x<9;++x)world.put({x,0,0},probe({x,0,0}));
    WorldScene pair;pair.materials.push_back({});quad(pair,{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0});
    layer.reset(&pair);auto line=world.atlas();hits=misses=0;assert(layer.apply(line,tagged,[]{return false;},hits,misses));
    const unsigned near=misses;assert(near>0&&hits==0);
    pair.materials.push_back({});quad(pair,{39,-1,0},{41,-1,0},{41,1,0},{39,1,0});for(auto& tri:pair.triangles)if(tri.v0>=4)tri.material=1;
    layer.observe(&pair);line=world.atlas();hits=misses=0;assert(layer.apply(line,tagged,[]{return false;},hits,misses));assert(hits<near&&misses>near);
    WorldScene swapped=pair;for(auto& tri:swapped.triangles)tri.material=1-tri.material;
    layer.observe(&swapped);line=world.atlas();hits=misses=0;assert(layer.apply(line,tagged,[]{return false;},hits,misses));assert(misses==0&&hits>near);
    std::puts("PASS static coverage survives actor generation changes, camera-window shifts, invalid actors and cancellation");
    std::puts("PASS exact static moments/validity, world-keyed cache reuse, smooth geometry-bound support, unreferenced vertex rejection");
    std::puts("PASS actual colored moving-geometry transport, 512-probe candidate ceiling, 128-solve budget, anchored signature reuse, stale limit, order independence and retention bound");
    std::puts("PASS static-only fallback rejects changed maps/regions/all light sources/settings/nonfinite data and discards partial dynamic overlays");
}
