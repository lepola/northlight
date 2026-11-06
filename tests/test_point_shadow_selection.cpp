#include "point_light_shadow.h"
#include "local_light_selection.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <vector>
using namespace NorthlightPointShadow;
using Light=NorthlightLocalLights::Light;
static Light lamp(uint64_t id,float z=10,float color=1){
    return {{0,0,z},{color,color*.5f,color*.2f},0,6,id,3,NorthlightLocalLights::CastShadow};
}
int main(){
    float camera[3]={},inverse[16]={},projection[3]={1,1,1};
    {
        std::vector<Light> lights;
        for(unsigned kind:{1u,2u,3u}){
            auto small=lamp(kind);small.kind=kind;small.attenuationEnd=5;
            if(kind==1)small.flags|=NorthlightLocalLights::Baked;
            lights.push_back(small);
            assert(NorthlightLocalLights::valid(small)&&!eligible(small));
            small.attenuationEnd=5.001f;assert(eligible(small));
        }
        auto candle=lamp(4);candle.attenuationEnd=32;
        candle.flags|=NorthlightLocalLights::CandlePulse;lights.push_back(candle);
        // Old caches may still request candle shadows; illumination survives.
        assert(selectMany(lights.data(),lights.size(),camera,16).count==0);
        const auto direct=NorthlightLocalLightSelection::select(lights,camera);
        assert(direct.count==lights.size());
        for(unsigned i=0;i<direct.count;++i)assert(direct.color[i][0]>0);
        lights.push_back(lamp(5));
        const auto shadows=selectMany(lights.data(),lights.size(),camera,16);
        assert(shadows.count==1&&lights[shadows.index[0]].sourceId==5);
        SelectionTracker tracker;auto slots=tracker.update(&lights.back(),1,camera,1,100);
        assert(slots[0].index==0);
        lights.back().attenuationEnd=5;
        slots=tracker.update(&lights.back(),1,camera,1,116);
        assert(slots[0].index<0); // No stale correction when a light becomes small.
    }
    for(unsigned i=0;i<16;i+=5)inverse[i]=1;
    auto view=NorthlightLocalLightScissor::view(inverse,projection,.1f,.1f,512,512);assert(view.valid);
    // Brighter/closer bulbs behind the camera used to exhaust the budget even
    // though the production pass then refused to draw any of their cubes.
    for(unsigned limit:{4u,16u,30u,64u,128u}){
        std::vector<Light> lights;for(unsigned i=0;i<limit;++i)lights.push_back(lamp(i,-10,16));
        for(unsigned i=0;i<limit;++i)lights.push_back(lamp(1000+i));
        const auto old=selectMany(lights.data(),lights.size(),camera,limit);
        for(unsigned i=0;i<old.count;++i)assert(lights[old.index[i]].position[2]<0);
        const auto visiblePicks=selectMany(lights.data(),lights.size(),camera,limit,{},48,false,&view);
        assert(visiblePicks.count==limit);
        for(unsigned i=0;i<visiblePicks.count;++i)assert(lights[visiblePicks.index[i]].sourceId>=1000);
        auto invalid=view;invalid.valid=false;
        const auto fallback=selectMany(lights.data(),lights.size(),camera,limit,{},48,false,&invalid);
        assert(fallback.count==old.count);for(unsigned i=0;i<old.count;++i)assert(fallback.index[i]==old.index[i]);
    }
    // Distance is measured to the influence sphere, including the exact edge.
    for(float range:{8.f,48.f,96.f,192.f}){
        Light edge=lamp(1,range+10);edge.attenuationEnd=10;
        assert(selectMany(&edge,1,camera,16,{},range,false,&view).count==1);
        edge.position[2]+=.01f;
        assert(selectMany(&edge,1,camera,16,{},range,false,&view).count==0);
        edge.position[2]=range+5;
        assert(selectMany(&edge,1,camera,16,{},range,false,&view).count==1);
    }
    {
        SelectionTracker tracker;Light distant=lamp(1,100);distant.attenuationEnd=10;
        assert(tracker.update(&distant,1,camera,16,100,&view)[0].index<0); // default 48
        auto slots=tracker.update(&distant,1,camera,16,116,&view,96);
        assert(slots[0].index==0&&std::fabs(slots[0].gain-.08f)<1e-6f);
        for(unsigned now=132;now<=324;now+=16)slots=tracker.update(&distant,1,camera,16,now,&view,96);
        assert(slots[0].gain==1);
        slots=tracker.update(&distant,1,camera,16,340,&view,8);
        assert(slots[0].index==0&&std::fabs(slots[0].gain-.92f)<1e-6f);
        for(unsigned now=356;now<=548;now+=16)slots=tracker.update(&distant,1,camera,16,now,&view,8);
        for(const auto& slot:slots)assert(slot.index<0);
    }
    unsigned transitions=0;
    for(unsigned limit:{4u,16u,30u,64u,128u}){
        SelectionTracker tracker;std::vector<Light> lights;
        for(unsigned i=0;i<=limit;++i)lights.push_back(lamp(i,10,i==limit?.01f:1.f));
        auto slots=tracker.update(lights.data(),lights.size(),camera,limit,100,&view);
        std::map<uint64_t,float> previous;
        for(const auto& s:slots)if(s.index>=0){assert(s.gain==1);previous[lights[s.index].sourceId]=s.gain;}
        assert(previous.size()==limit&&previous.count(0)); // authored zero is a valid identity
        std::reverse(lights.begin(),lights.end());
        const auto reordered=tracker.update(lights.data(),lights.size(),camera,limit,100,&view);
        for(unsigned i=0;i<MaxLights;++i)if(slots[i].index>=0)
            assert(reordered[i].index>=0&&lights[reordered[i].index].sourceId==tracker.entries[i].id&&reordered[i].gain==1);
        lights.front().diffuse[0]=16; // a competing source overtakes an incumbent
        for(uint32_t now=116;now<=900;now+=16){
            slots=tracker.update(lights.data(),lights.size(),camera,limit,now,&view);
            std::map<uint64_t,float> current;
            for(const auto& s:slots)if(s.index>=0){assert(s.gain>=0&&s.gain<=1);assert(current.emplace(lights[s.index].sourceId,s.gain).second);}
            assert(current.size()<=limit);
            for(const auto& e:previous)assert(std::fabs(e.second-current[e.first])<=.08001f);
            for(const auto& e:current)assert(std::fabs(e.second-previous[e.first])<=.08001f);
            previous=current;++transitions;
        }
        assert(previous[limit]==1); // the new source eventually owns a full correction
        slots=tracker.update(lights.data(),lights.size(),camera,16,916,&view);
        unsigned resident=0;for(const auto& s:slots)resident+=s.index>=0;assert(resident<=16);
        // Capture/resource refusal must restart the fade, never reveal a full
        // correction the moment a deferred cube becomes ready.
        for(unsigned i=0;i<MaxLights;++i)if(slots[i].index>=0)tracker.defer(i);
        slots=tracker.update(lights.data(),lights.size(),camera,16,932,&view);
        for(const auto& s:slots)if(s.index>=0)assert(s.gain<=.08001f);
        lights.clear();slots=tracker.update(nullptr,0,camera,16,948,&view);
        for(const auto& s:slots)assert(s.index<0); // no stale LocalDirect subtraction
        tracker.reset();lights={lamp(0)};
        tracker.update(lights.data(),1,camera,16,UINT32_MAX-7,&view);tracker.defer(0);
        slots=tracker.update(lights.data(),1,camera,16,8,&view);assert(std::fabs(slots[0].gain-.08f)<1e-6f);
    }
    std::printf("PASS visible shadow selection: 8/48/96/192 yard edges and live distance fades, 4/16/30/64/128 limits, %u bounded transitions, identity/order, shrink, capture refusal, missing sources, tick wrap\n",transitions);
}
