#pragma once
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include "world_gi.h"

namespace NorthlightWeatherShelter {
// A small, lazy CPU worker. Unknown, stale or moved-away results disable only the supplemental layer.
class Worker {
    struct Request{std::shared_ptr<const NorthlightGI::BVH> bvh;NorthlightGI::Vec3 eye;std::string map;std::uint64_t tick=0;};
    std::mutex mutex;std::condition_variable wake;std::thread worker;bool stopping=false,pending=false,known=false,outdoors=false;
    Request request,result;
    void work(){for(;;){Request r;{std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return stopping||pending;});if(stopping)return;r=request;request.bvh.reset();pending=false;}
        bool open=true;try{for(auto offset:{NorthlightGI::Vec3(0,0,0),NorthlightGI::Vec3(.6f,0,0),NorthlightGI::Vec3(-.6f,0,0),NorthlightGI::Vec3(0,.6f,0),NorthlightGI::Vec3(0,-.6f,0)})if(r.bvh->occluded(r.eye+offset,NorthlightGI::Vec3(0,0,1),80.f,.1f)){open=false;break;}}
        catch(...){open=false;}
        r.bvh.reset();{std::lock_guard<std::mutex> lock(mutex);result=std::move(r);outdoors=open;known=true;}}}
public:
    Worker(){worker=std::thread([this]{work();});}
    ~Worker(){{std::lock_guard<std::mutex> lock(mutex);stopping=true;}wake.notify_one();worker.join();}
    bool sample(std::shared_ptr<const NorthlightGI::BVH> bvh,NorthlightGI::Vec3 eye,const std::string& map,std::uint64_t now){
        if(!bvh||!bvh->triangleCount())return false;std::lock_guard<std::mutex> lock(mutex);
        if(request.map!=map||now-request.tick>=250){request={std::move(bvh),eye,map,now};pending=true;wake.notify_one();}
        const auto d=eye-result.eye;return known&&outdoors&&result.map==map&&now-result.tick<1000&&NorthlightGI::dot(d,d)<=4;
    }
};
}
