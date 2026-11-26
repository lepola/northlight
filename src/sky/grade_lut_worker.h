#pragma once
#include "grade_lut_pack.h"
#include "world_tone_curves.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
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
    enum class Origin : uint8_t {Fresh,Pack};
    NorthlightTone::BakeKey key;
    std::vector<uint16_t> rgba;
    Origin origin=Origin::Fresh;
};
struct Result {
    NorthlightTone::BakeKeys keys{};
    std::array<std::shared_ptr<const Volume>,NorthlightTone::StopCount> volumes{};
    uint64_t serial=0;
    unsigned bakedVolumes=0,packDecodedVolumes=0,cacheHits=0,packCacheHits=0,packRecords=0,packRejectedTables=0;
    Pack::Status packStatus=Pack::Status::Untried;
    std::array<uint8_t,32> packSourceSha{};
    std::array<char,80> packError{};
    double workerMs=0,packIndexMs=0,packDecodeMs=0,bakeMs=0;
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
    bool packAttempted_=false;
    // The shipped looks' tables, a pack to each look; a table is taken from the first pack that holds it.
    std::vector<std::filesystem::path> packPaths_;
    std::vector<Pack::Index> packs_;
    uint64_t serial_=0,workingSerial_=0,clock_=0;
    NorthlightTone::BakeKeys requested_{},workingKeys_{};
    std::shared_ptr<const Result> completed_;
    NorthlightTone::BakeKeys failedKeys_{};
    std::array<char,80> failureError_{};
    bool failureReady_=false;

    void fail(uint64_t serial,const NorthlightTone::BakeKeys& keys,const char* reason){
        std::lock_guard<std::mutex> lock(mutex_);
        if(serial!=serial_||stopping_)return;
        failedKeys_=keys;failureError_={};std::snprintf(failureError_.data(),failureError_.size(),"%s",reason?reason:"worker failure");failureReady_=true;
        working_=false;
    }

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
            try{result=std::make_shared<Result>();}catch(...){fail(serial,keys,"result allocation failed");continue;}
            result->keys=keys;result->serial=serial;
            bool loadPack=false;
            {std::lock_guard<std::mutex> lock(mutex_);if(!packAttempted_){packAttempted_=true;loadPack=true;}}
            if(loadPack){
                const auto indexStarted=std::chrono::steady_clock::now();
                try{packs_.resize(packPaths_.size());}catch(...){packs_.clear();}
                for(size_t p=0;p<packs_.size();++p){
                    try{packs_[p].load(packPaths_[p]);}
                    catch(...){packs_[p].status=Pack::Status::Invalid;try{packs_[p].error="exception while reading pack index";}catch(...){}}
                }
                result->packIndexMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-indexStarted).count();
            }
            // Reported as one: ready when any pack is, with every pack's records; the source and the error are those
            // of the pack a table last came from, or of the first pack that has either to report.
            result->packStatus=Pack::Status::Missing;
            const Pack::Index* reported=nullptr;
            for(const auto& pack:packs_){
                if(pack.status==Pack::Status::Ready){
                    result->packRecords+=static_cast<unsigned>(pack.records.size());
                    if(result->packStatus!=Pack::Status::Ready){result->packStatus=Pack::Status::Ready;reported=&pack;}
                }else if(pack.status==Pack::Status::Invalid&&result->packStatus==Pack::Status::Missing){result->packStatus=Pack::Status::Invalid;reported=&pack;}
            }
            if(reported){result->packSourceSha=reported->sourceSha;std::snprintf(result->packError.data(),result->packError.size(),"%s",reported->error.c_str());}
            bool complete=true;const char* failure=nullptr;
            for(unsigned i=0;i<NorthlightTone::StopCount;++i){
                const auto& key=keys[i];if(!key.strength)continue;
                std::shared_ptr<const Volume> volume=cacheLookup(key);
                if(volume){++result->cacheHits;if(volume->origin==Volume::Origin::Pack)++result->packCacheHits;}
                else {
                    if(stale(serial)){complete=false;break;}
                    Pack::Index* pack=nullptr;Pack::Record* record=nullptr;
                    for(auto& candidate:packs_)if((record=candidate.find(key))){pack=&candidate;break;}
                    if(record){
                        const auto packStarted=std::chrono::steady_clock::now();
                        std::shared_ptr<Volume> decoded;std::string reason;
                        try{decoded=std::make_shared<Volume>();decoded->key=key;decoded->origin=Volume::Origin::Pack;
                            if(!pack->decode(*record,decoded->rgba,reason))decoded.reset();}
                        catch(...){decoded.reset();reason="exception while decoding table";record->rejected=true;}
                        result->packDecodeMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-packStarted).count();
                        if(decoded){volume=decoded;++result->packDecodedVolumes;result->packSourceSha=pack->sourceSha;try{retain(volume);}catch(...){}}
                        else{
                            ++result->packRejectedTables;
                            std::snprintf(result->packError.data(),result->packError.size(),"%s",reason.c_str());
                        }
                    }
                    if(!volume){
                        const auto bakeStarted=std::chrono::steady_clock::now();
                        try{volume=bake(key);}catch(...){failure="volume bake failed";complete=false;break;}
                        result->bakeMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-bakeStarted).count();
                        try{retain(volume);}catch(...){}
                        ++result->bakedVolumes;
                    }
                }
                result->volumes[i]=std::move(volume);
            }
            result->workerMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if(complete&&serial==serial_){completed_=std::move(result);failureReady_=false;failureError_={};}
                else if(failure&&serial==serial_){failedKeys_=keys;failureError_={};std::snprintf(failureError_.data(),failureError_.size(),"%s",failure);failureReady_=true;}
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
    bool setPackPaths(std::vector<std::filesystem::path> paths){
        std::lock_guard<std::mutex> lock(mutex_);if(started_||packAttempted_)return false;
        packPaths_=std::move(paths);return true;
    }
    uint64_t request(const NorthlightTone::BakeKeys& keys) {
        std::lock_guard<std::mutex> lock(mutex_);
        if((pending_&&requested_==keys)||(working_&&workingSerial_==serial_&&workingKeys_==keys))return serial_;
        if(completed_&&completed_->keys==keys){
            if(pending_||working_){
                ++serial_;requested_=keys;pending_=false;
            }
            return completed_->serial;
        }
        completed_.reset();failureReady_=false;failureError_={};++serial_;requested_=keys;
        bool any=false;for(const auto& key:keys)any|=key.strength!=0;
        if(!any){
            try{auto result=std::make_shared<Result>();result->keys=keys;result->serial=serial_;completed_=std::move(result);}catch(...){return 0;}
            pending_=false;return serial_;
        }
        if(!started_){try{thread_=std::thread([this]{run();});started_=true;}catch(...){failureReady_=true;failedKeys_=keys;std::snprintf(failureError_.data(),failureError_.size(),"worker start failed");return 0;}}
        pending_=true;wake_.notify_one();return serial_;
    }
    std::shared_ptr<const Result> ready(const NorthlightTone::BakeKeys& keys)const {
        std::lock_guard<std::mutex> lock(mutex_);
        return completed_&&completed_->keys==keys?completed_:std::shared_ptr<const Result>{};
    }
    bool failed(const NorthlightTone::BakeKeys& keys,std::array<char,80>& reason)const{
        std::lock_guard<std::mutex> lock(mutex_);if(!failureReady_||failedKeys_!=keys)return false;reason=failureError_;return true;
    }
    std::shared_ptr<const Volume> findCached(const NorthlightTone::BakeKey& key){return key.strength?cacheLookup(key):std::shared_ptr<const Volume>{};}
    size_t cachedVolumes()const {std::lock_guard<std::mutex> lock(mutex_);return cache_.size();}
    bool packAttempted()const {std::lock_guard<std::mutex> lock(mutex_);return packAttempted_;}
};
}
