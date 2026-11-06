#pragma once
#include "native_light_snapshot.h"
#include "native_fixture_catalog.h"
#include <atomic>
#ifdef _WIN32
#include "world_context.h"
#include "native_scene_lights.h"
#endif

namespace NorthlightNativeLights {
inline std::atomic<bool> requested{false};
inline std::shared_ptr<const NorthlightNativeFixtures::Catalog> fixtureCatalog;
inline std::atomic<unsigned> fixtureRevision{0};
inline std::shared_ptr<const NorthlightNativeFixtures::Catalog> loadFixtures(const std::string& path){auto catalog=NorthlightNativeFixtures::load(path);std::atomic_store(&fixtureCatalog,catalog);fixtureRevision.fetch_add(1,std::memory_order_release);return catalog;}
#ifdef _WIN32
#ifndef NORTHLIGHT_REPLAY_STAND
// Each producer owns its discovery cache. The command stream carries only
// copied values; no pointer or cache is shared with the renderer's workers.
struct Capture {
    Reader reader;
    Snapshot latest;
    char boundMap[64]{};
    std::uint32_t sampledAt=0;
    unsigned fixtureVersion=~0u;
    void sample(const char* map,const float* camera){
        if(!requested.load(std::memory_order_relaxed)||!map||!*map){latest.clear();return;}
        static const bool supported=NorthlightWorldContext::supportedClient()&&Reader::verify(NorthlightWorldContext::readSelfLive);
        if(!supported){latest.clear();return;}
        const auto version=fixtureRevision.load(std::memory_order_acquire);if(version!=fixtureVersion){reader.fixtures(std::atomic_load(&fixtureCatalog));fixtureVersion=version;}
        if(std::strcmp(boundMap,map)){reader.reset();std::strncpy(boundMap,map,sizeof boundMap-1);boundMap[sizeof boundMap-1]=0;}
        try{reader.update(NorthlightWorldContext::readSelfLive,camera,latest);}
        catch(...){latest.clear();reader.reset();}
        if(latest.valid){std::strncpy(latest.map,map,sizeof latest.map-1);latest.map[sizeof latest.map-1]=0;}
        sampledAt=GetTickCount();
    }
    void copy(const char* map,Snapshot& output)const{
        output.clear();
        if(requested.load(std::memory_order_relaxed)&&map&&latest.valid&&
           std::strcmp(map,latest.map)==0&&std::uint32_t(GetTickCount()-sampledAt)<=250)output=latest;
    }
};
inline Capture& producer(){static thread_local Capture capture;return capture;}
#endif
inline void captureFrame(bool world,const char* map,const float* camera,Snapshot& output){
    output.clear();
#ifndef NORTHLIGHT_REPLAY_STAND
    auto& capture=producer();if(world)capture.sample(map,camera);capture.copy(map,output);
#else
    (void)world;(void)map;(void)camera; // imported by StandInputs::commitCapture
#endif
}
inline void frame(const char* map,const float* camera,Snapshot& output){
    output.clear();
    if(const auto* snapshot=NorthlightStream::activeSnapshot){
        if(snapshot->mode==NorthlightStream::GameSnapshot::Mode::Playing&&snapshot->nativeLights.valid&&
           map&&std::strcmp(snapshot->nativeLights.map,map)==0)output=snapshot->nativeLights;
        return;
    }
#ifndef NORTHLIGHT_REPLAY_STAND
    auto& capture=producer();capture.sample(map,camera);capture.copy(map,output);
#else
    (void)map;(void)camera;
#endif
}
#endif
}
