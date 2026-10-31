#include "point_light_shadow.h"
#include "local_light_selection.h"
#include "quality_settings.h"
#include "settings_schema.h"
#include "gpu_budget.h"
#include <set>
#include <cassert>
#include <iostream>
int main(){
    using namespace NorthlightLocalLights;
    std::array<Light,7> lights{};float camera[3]={};
    for(unsigned i=0;i<lights.size();++i){auto& l=lights[i];l={{float(i),0,2},{1,.4f,.1f},1,12,100+i,3,CastShadow};assert(valid(l));}
    lights[4].flags=0;lights[4].diffuse[0]=100; // Diffuse glow must not displace a torch.
    lights[5].kind=2;lights[5].flags=0;lights[5].diffuse[0]=100; // Unclassified equipment/particle observation.
    lights[6].diffuse[0]=lights[6].diffuse[1]=lights[6].diffuse[2]=.0001f;
    const auto two=NorthlightPointShadow::selectMany(lights.data(),lights.size(),camera,2);
    assert(two.count==2&&two.index[0]==0&&two.index[1]==1);
    const auto four=NorthlightPointShadow::selectMany(lights.data(),lights.size(),camera,4);
    assert(four.count==4);for(unsigned i=0;i<4;++i)assert(four.index[i]==int(i));
    assert(NorthlightPointShadow::selectMany(lights.data(),lights.size(),camera,100).count==4);
    assert(!NorthlightPointShadow::selectMany(lights.data(),lights.size(),camera,0).count);
    assert(!NorthlightPointShadow::selectMany(nullptr,7,camera,2).count);
    auto invalid=lights[0];invalid.flags|=32;assert(!valid(invalid));invalid.flags=CastShadow|Baked;assert(!valid(invalid));
    auto glass=lights[0];glass.flags|=GlassEmitter;assert(valid(glass));
    glass.kind=2;assert(!valid(glass));glass.kind=3;glass.flags=GlassEmitter;assert(!valid(glass));
    std::vector<Light> fixed(lights.begin(),lights.begin()+4);
    NorthlightLocalLightSelection::Tracker tracker;
    auto selected=tracker.update(fixed,camera,32,.1f,false);
    assert(selected.count==4);
    for(unsigned i=0;i<selected.count;++i){assert(selected.sources[i].sourceId==fixed[i].sourceId);assert(selected.sources[i].flags==CastShadow);
        for(unsigned k=0;k<3;++k)assert(selected.sources[i].diffuse[k]==selected.color[i][k]);}
    fixed.erase(fixed.begin());selected=tracker.update(fixed,camera,32,.1f,true);
    assert(selected.count==4);for(unsigned i=0;i<4;++i)for(unsigned k=0;k<3;++k)assert(selected.sources[i].diffuse[k]==selected.color[i][k]);
    NorthlightPointShadow::RefreshSchedule a,b;a.commit(100,lights[0],1);b.commit(100,lights[1],1);
    auto moved=lights[0];moved.position[0]+=.1f;
    assert(a.due(101,moved,1,300,false));assert(!b.due(101,lights[1],1,300,false));a.invalidate();assert(b.usable);
    const auto defaults=NorthlightQuality::Settings{};assert(defaults.pointShadows==1&&defaults.pointShadowLimit==4);
    for(auto preset:{NorthlightQuality::Preset::Quality,NorthlightQuality::Preset::Balanced,NorthlightQuality::Preset::Performance})assert(NorthlightQuality::preset(preset).pointShadowLimit==4);
    std::istringstream input("[Quality]\nPointShadowLimit=4\n");std::vector<std::string> problems;
    auto config=NorthlightQuality::load(&input,nullptr,problems);assert(problems.empty()&&config.pointShadowLimit==4);
    assert(NorthlightSettings::liveQuality("PointShadowLimit")&&NorthlightSettings::liveQuality("PointShadows"));
    assert(NorthlightSettings::liveQualityUpdate(defaults,config).pointShadowLimit==4);
    static_assert(NorthlightPointShadow::MaxLights==128);
    static_assert(NorthlightLocalLightSelection::Capacity>=NorthlightPointShadow::MaxLights);
    std::vector<Light> many;
    for(unsigned i=0;i<128;++i)many.push_back({{float(i%16)*.1f,float(i/16)*.1f,2},{1,.4f,.1f},1,12,1000+i,3,CastShadow});
    for(unsigned level=0;level<4;++level)for(unsigned limit:{4u,8u,16u,32u,64u,128u}){
        const unsigned pool=NorthlightGpuBudget::lightLimit(32,level,limit);assert(pool>=limit);
        std::vector<Light> scene(many.begin(),many.begin()+limit);
        NorthlightLocalLightSelection::Tracker t;auto selection=t.update(scene,camera,pool,0,false);
        assert(selection.count==limit);
        const auto picks=NorthlightPointShadow::selectMany(selection.sources.data(),selection.count,camera,limit);
        assert(picks.count==limit);std::set<uint64_t> ids;
        for(unsigned i=0;i<picks.count;++i)assert(ids.insert(selection.sources[picks.index[i]].sourceId).second);
        unsigned direct=0,fog=0;
        for(unsigned i=0;i<selection.count;i+=8)direct+=selection.batch<8>(i).count;
        for(unsigned i=0;i<selection.count;i+=4)fog+=selection.batch<4>(i).count;
        assert(direct==selection.count&&fog==selection.count);
    }
    // GPU budgeting preserves the configured lamp limit, including without a shadow reserve.
    for(unsigned level=0;level<4;++level)for(unsigned limit:{4u,8u,16u,32u,64u,128u})
        assert(NorthlightGpuBudget::lightLimit(limit,level)==limit);
    assert(NorthlightPointShadow::selectMany(many.data(),many.size(),camera,999).count==128);
    auto full=tracker.update(many,camera,128,0,false);assert(full.count==128);
    auto changed=many;for(unsigned i=0;i<8;++i)changed[i].sourceId+=1000;
    auto fading=tracker.update(changed,camera,128,.1f,true);assert(fading.count==136);
    unsigned batched=0;for(unsigned i=0;i<fading.count;i+=8)batched+=fading.batch<8>(i).count;assert(batched==136);
    assert(NorthlightPointShadow::selectMany(fading.sources.data(),fading.count,camera,128).count==128);
    tracker.update(many,camera,128,0,false);
    std::array<uint64_t,128> previous{};for(unsigned i=0;i<128;++i)previous[i]=many[i].sourceId;
    auto reordered=many;std::reverse(reordered.begin(),reordered.end());
    auto pa=NorthlightPointShadow::selectMany(many.data(),many.size(),camera,128,previous);
    auto pb=NorthlightPointShadow::selectMany(reordered.data(),reordered.size(),camera,128,previous);
    for(unsigned i=0;i<128;++i)assert(many[pa.index[i]].sourceId==reordered[pb.index[i]].sourceId);
    auto shrinking=tracker.update(many,camera,4,.01f,true);assert(shrinking.count<=4+NorthlightLocalLightSelection::Spare);
    for(unsigned i=0;i<50;++i)shrinking=tracker.update(many,camera,4,.1f,true);
    assert(shrinking.count<=4);assert(tracker.update(many,camera,128,0,false).count==128);
    for(unsigned limit:{4u,8u,16u,32u,64u,128u}){
        problems.clear();std::istringstream in("[Quality]\nPointShadowLimit="+std::to_string(limit)+"\nLocalLightLimit=128\n");
        config=NorthlightQuality::load(&in,nullptr,problems);assert(problems.empty()&&config.pointShadowLimit==limit&&config.localLightLimit==128);
        assert(NorthlightSettings::liveQualityUpdate(defaults,config).pointShadowLimit==limit);
    }
    for(unsigned invalidLimit:{3u,129u}){
        problems.clear();std::istringstream in("[Quality]\nPointShadowLimit="+std::to_string(invalidLimit)+"\n");
        config=NorthlightQuality::load(&in,nullptr,problems);assert(!problems.empty()&&config.pointShadowLimit==4);
    }
    std::cout<<"Local shadows: classification, 4..128 selection at all GPU budget levels, batches, shrink/grow, ordering, source fades, independent refresh and live settings passed\n";
}
