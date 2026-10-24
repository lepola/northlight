#pragma once
#include "world_gi.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>

namespace NorthlightGI {
// Synchronized cache of immutable, asset-keyed FGS2 sources. Both the
// local placement builder and directional terrain loader use the same bank.
class SceneSourceCache {
    struct Account { std::atomic<uint64_t> live{0},decoded{0},cachedDecoded{0}; };
    struct Held {
        WorldScene scene;std::shared_ptr<Account> account;uint64_t bytes=0,decodedBytes=0;
        ~Held(){if(account){account->live.fetch_sub(bytes,std::memory_order_relaxed);account->decoded.fetch_sub(decodedBytes,std::memory_order_relaxed);if(bytes)account->cachedDecoded.fetch_sub(decodedBytes,std::memory_order_relaxed);}}
    };
    struct Stamp {
        uintmax_t bytes=0;std::filesystem::file_time_type modified{};bool valid=false;
        bool operator==(const Stamp& b)const{return valid&&b.valid&&bytes==b.bytes&&modified==b.modified;}
    };
    struct Entry {std::string path;Stamp stamp;std::shared_ptr<const WorldScene> scene;};
    struct Identity {std::string path;Stamp stamp;uint64_t token=0,bytes=0;};
    std::shared_ptr<Account> account_=std::make_shared<Account>();
    std::list<Entry> entries_;
    std::list<Identity> identities_;
    uint64_t limit_;
    mutable std::recursive_mutex mutex_;
    static Stamp stamp(const std::string& path){
        std::error_code ec;Stamp s;s.bytes=std::filesystem::file_size(path,ec);if(ec)return s;
        s.modified=std::filesystem::last_write_time(path,ec);s.valid=!ec;return s;
    }
    static uint64_t nextIdentity(){
        static std::atomic<uint64_t> next{1};auto value=next.load(std::memory_order_relaxed);
        while(value!=UINT64_MAX){if(next.compare_exchange_weak(value,value+1,std::memory_order_relaxed))return value;}return 0;
    }
    void dropIdentity(){account_->live.fetch_sub(identities_.front().bytes,std::memory_order_relaxed);identities_.pop_front();}
    void observeRevision(const std::string& path,const Stamp& current){
        for(auto i=identities_.begin();i!=identities_.end();++i)if(i->path==path){
            if(i->stamp==current)return;
            ++stats_.changed;
            account_->live.fetch_sub(i->bytes,std::memory_order_relaxed);identities_.erase(i);
            for(auto e=entries_.begin();e!=entries_.end();++e)if(e->path==path){entries_.erase(e);break;}
            return;
        }
    }
    bool fits(uint64_t bytes)const{const auto live=liveBytes();return live<=limit_&&bytes<=limit_-live;}
    bool room(uint64_t bytes,bool preserveIdentities=false){
        while(!fits(bytes)&&!entries_.empty()){entries_.pop_front();++stats_.evictions;}
        while(!preserveIdentities&&!fits(bytes)&&!identities_.empty())dropIdentity();
        return fits(bytes);
    }
    uint64_t identity(const std::string& path,const Stamp& current){
        const auto begin=std::chrono::steady_clock::now();
        observeRevision(path,current);
        // Sources are immutable during play. Preserve the verified revision's
        // token across raw payload eviction; changed stamps always get new IDs.
        for(auto i=identities_.begin();i!=identities_.end();++i)if(i->path==path&&i->stamp==current){
            const auto token=i->token;identities_.splice(identities_.end(),identities_,i);++stats_.identityHits;
            stats_.identityMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();return token;
        }
        try{
            Identity value;value.path=path;value.stamp=current;value.bytes=sizeof(Identity)+value.path.capacity()+64;
            if(!room(value.bytes))return 0;value.token=nextIdentity();if(!value.token)return 0;
            identities_.push_back(std::move(value));account_->live.fetch_add(identities_.back().bytes,std::memory_order_relaxed);
            stats_.identityMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            return identities_.back().token;
        }catch(...){return 0;}
    }
public:
    struct Stats {uint64_t hits=0,reads=0,changed=0,evictions=0,uncached=0,identityHits=0;double loadMs=0,identityMs=0;};
private:
    Stats stats_;
public:
    explicit SceneSourceCache(uint64_t limit=8u*1024u*1024u):limit_(limit){}
    ~SceneSourceCache(){reset();}
    SceneSourceCache(const SceneSourceCache&)=delete;
    SceneSourceCache& operator=(const SceneSourceCache&)=delete;
    uint64_t liveBytes()const{return account_->live.load(std::memory_order_relaxed);}
    uint64_t decodedLiveBytes()const{return account_->decoded.load(std::memory_order_relaxed);}
    uint64_t uncachedLeaseBytes()const{const auto all=decodedLiveBytes(),cached=account_->cachedDecoded.load(std::memory_order_relaxed);return all-std::min(all,cached);}
    size_t entries()const{std::lock_guard<std::recursive_mutex> lock(mutex_);return entries_.size();}
    Stats stats()const{std::lock_guard<std::recursive_mutex> lock(mutex_);return stats_;}
    // Entry pressure may need only the decoded payloads. Keep validated revision
    // tokens so independently retained BLAS/preparation can still reuse them.
    void trimPayloads(){std::lock_guard<std::recursive_mutex> lock(mutex_);entries_.clear();}
    void reset(){std::lock_guard<std::recursive_mutex> lock(mutex_);entries_.clear();while(!identities_.empty())dropIdentity();} // outstanding leases remain charged
    void setLimit(uint64_t bytes){std::lock_guard<std::recursive_mutex> lock(mutex_);limit_=bytes;room(0);}
    uint64_t findIdentity(const std::string& path){
        std::lock_guard<std::recursive_mutex> lock(mutex_);const auto current=stamp(path);observeRevision(path,current);
        for(auto i=identities_.begin();i!=identities_.end();++i)if(i->path==path){
            if(i->stamp==current){const auto token=i->token;identities_.splice(identities_.end(),identities_,i);return token;}
            account_->live.fetch_sub(i->bytes,std::memory_order_relaxed);identities_.erase(i);return 0;
        }
        return 0;
    }
    std::shared_ptr<const WorldScene> find(const std::string& path,std::string& error){
        std::lock_guard<std::recursive_mutex> lock(mutex_);error.clear();const auto current=stamp(path);observeRevision(path,current);
        for(auto i=entries_.begin();i!=entries_.end();++i)if(i->path==path){
            if(i->stamp==current){++stats_.hits;auto result=i->scene;entries_.splice(entries_.end(),entries_,i);return result;}
            entries_.erase(i);++stats_.changed;break;
        }
        return {};
    }
    std::shared_ptr<const WorldScene> get(const std::string& path,std::string& error,const AllocationAdmission& admit={}){
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if(auto ready=find(path,error))return ready;
        error.clear();const auto before=stamp(path);
        observeRevision(path,before);
        ++stats_.reads;const auto begin=std::chrono::steady_clock::now();
        std::shared_ptr<Held> held;
        try{held=std::make_shared<Held>();}catch(...){error="Cannot allocate immutable FGS2 source";return {};}
        const bool ok=loadScene(path.c_str(),held->scene,error,admit);
        stats_.loadMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        if(!ok)return {};
        held->account=account_;held->decodedBytes=held->scene.retainedBytes();account_->decoded.fetch_add(held->decodedBytes,std::memory_order_relaxed);
        const auto after=stamp(path);
        observeRevision(path,after);
        if(before.valid&&after.valid&&!(before==after)){error="FGS2 source changed during decode";return {};}
        // The installed world-cache is immutable during a session. Also reject
        // ordinary external replacements; unknown stamps never enter the cache.
        const uint64_t bytes=held->scene.retainedBytes()+sizeof(Held)+sizeof(Entry)+path.capacity()+64;
        held->scene.sourceIdentity=before==after?identity(path,after):0;
        auto result=std::shared_ptr<const WorldScene>(held,&held->scene);
        if(!(before==after)||bytes>limit_){++stats_.uncached;return result;}
        if(!room(bytes,true)){++stats_.uncached;return result;}
        try{
            entries_.push_back({path,after,result});held->bytes=bytes;account_->cachedDecoded.fetch_add(held->decodedBytes,std::memory_order_relaxed);
            account_->live.fetch_add(bytes,std::memory_order_relaxed);
        }catch(...){++stats_.uncached;}
        return result;
    }
    auto provider(){return [this](const std::string& path,std::string& error,const AllocationAdmission& admit){return get(path,error,admit);};}
};
} // namespace NorthlightGI
