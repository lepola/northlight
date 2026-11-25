#pragma once
#include "world_tone_curves.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace NorthlightGradeLut {
// One RGBA16 32^3 cube is 256 KiB. Twelve LRU entries bound the retained CPU
// cache to 3 MiB; an in-flight coalesced request can add at most three cubes.
constexpr size_t CpuCacheVolumes=12;
constexpr size_t VolumeWords=size_t(NorthlightTone::CubeSize)*NorthlightTone::CubeSize*NorthlightTone::CubeSize*4;

struct Volume {
    NorthlightTone::BakeKey key;
    std::vector<uint16_t> rgba;
};
struct Result {
    NorthlightTone::BakeKeys keys{};
    std::array<std::shared_ptr<const Volume>,NorthlightTone::StopCount> volumes{};
    uint64_t serial=0;
    unsigned bakedVolumes=0,cacheHits=0;
    double workerMs=0;
};

// The worker owns only CPU data. A request replaces queued work so a fast
// region walk bakes the most recent area instead of building a stale backlog.
class Worker {
    struct Entry {std::shared_ptr<const Volume> volume;uint64_t used=0;};
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::thread thread_;
    std::vector<Entry> cache_;
    bool started_=false,stopping_=false,pending_=false,working_=false;
    uint64_t serial_=0,workingSerial_=0,clock_=0;
    NorthlightTone::BakeKeys requested_{},workingKeys_{};
    std::shared_ptr<const Result> completed_;

    bool stale(uint64_t serial)const {
        std::lock_guard<std::mutex> lock(mutex_);
        return stopping_||serial!=serial_;
    }
    std::shared_ptr<const Volume> cacheLookup(const NorthlightTone::BakeKey& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& entry:cache_)if(entry.volume->key==key){entry.used=++clock_;return entry.volume;}
        return {};
    }
    void retain(const std::shared_ptr<const Volume>& volume) {
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto& entry:cache_)if(entry.volume->key==volume->key){entry.volume=volume;entry.used=++clock_;return;}
        if(cache_.size()>=CpuCacheVolumes){
            const auto oldest=std::min_element(cache_.begin(),cache_.end(),[](const Entry& a,const Entry& b){return a.used<b.used;});
            *oldest={volume,++clock_};
        }else cache_.push_back({volume,++clock_});
    }
    std::shared_ptr<const Volume> bake(const NorthlightTone::BakeKey& key) {
        auto volume=std::make_shared<Volume>();volume->key=key;volume->rgba.resize(VolumeWords);
        constexpr unsigned n=NorthlightTone::CubeSize;
        NorthlightTone::bakeColourVolume(volume->rgba.data(),n*4*sizeof(uint16_t),n*n*4*sizeof(uint16_t),key);
        return volume;
    }
    void run() {
        for(;;){
            NorthlightTone::BakeKeys keys{};uint64_t serial=0;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock,[&]{return stopping_||pending_;});
                if(stopping_)return;
                keys=requested_;serial=serial_;pending_=false;working_=true;workingSerial_=serial;workingKeys_=keys;
            }
            const auto started=std::chrono::steady_clock::now();
            std::shared_ptr<Result> result;
            try{result=std::make_shared<Result>();}catch(...){std::lock_guard<std::mutex> lock(mutex_);working_=false;continue;}
            result->keys=keys;result->serial=serial;
            bool complete=true;
            for(unsigned i=0;i<NorthlightTone::StopCount;++i){
                const auto& key=keys[i];if(!key.strength)continue;
                auto volume=cacheLookup(key);
                if(volume)++result->cacheHits;
                else {
                    if(stale(serial)){complete=false;break;}
                    try{volume=bake(key);}catch(...){complete=false;break;}
                    try{retain(volume);}catch(...){}
                    ++result->bakedVolumes;
                }
                result->volumes[i]=std::move(volume);
            }
            result->workerMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if(complete&&serial==serial_)completed_=std::move(result);
                working_=false;
            }
        }
    }
public:
    Worker()=default;
    Worker(const Worker&)=delete;Worker& operator=(const Worker&)=delete;
    void shutdown(){
        {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;pending_=false;}
        wake_.notify_one();if(thread_.joinable())thread_.join();
    }
    ~Worker(){shutdown();}
    uint64_t request(const NorthlightTone::BakeKeys& keys) {
        std::lock_guard<std::mutex> lock(mutex_);
        if((pending_&&requested_==keys)||(working_&&workingSerial_==serial_&&workingKeys_==keys))return serial_;
        if(completed_&&completed_->keys==keys){
            if(pending_||working_){
                ++serial_;requested_=keys;pending_=false;
            }
            return completed_->serial;
        }
        completed_.reset();++serial_;requested_=keys;
        bool any=false;for(const auto& key:keys)any|=key.strength!=0;
        if(!any){
            try{auto result=std::make_shared<Result>();result->keys=keys;result->serial=serial_;completed_=std::move(result);}catch(...){return 0;}
            pending_=false;return serial_;
        }
        if(!started_){try{thread_=std::thread([this]{run();});started_=true;}catch(...){return 0;}}
        pending_=true;wake_.notify_one();return serial_;
    }
    std::shared_ptr<const Result> ready(const NorthlightTone::BakeKeys& keys)const {
        std::lock_guard<std::mutex> lock(mutex_);
        return completed_&&completed_->keys==keys?completed_:std::shared_ptr<const Result>{};
    }
    std::shared_ptr<const Volume> findCached(const NorthlightTone::BakeKey& key){return key.strength?cacheLookup(key):std::shared_ptr<const Volume>{};}
    size_t cachedVolumes()const {std::lock_guard<std::mutex> lock(mutex_);return cache_.size();}
};
}
