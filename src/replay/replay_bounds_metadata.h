#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

namespace NorthlightReplayMetadata {
// Render-thread only. Both immutable COM objects stay alive while their pointer
// pair identifies a prepared snapshot. The snapshot owns its program/layout
// values, so a packet may safely keep it after this bounded cache evicts it.
template<class Prepared,class Shader,class Declaration> class Cache {
    struct Key {
        Shader* shader;Declaration* declaration;
        bool operator==(const Key& other)const{return shader==other.shader&&declaration==other.declaration;}
    };
    struct Hash {
        size_t operator()(const Key& k)const{
            const auto a=std::hash<Shader*>{}(k.shader),b=std::hash<Declaration*>{}(k.declaration);
            return a^(b+size_t(0x9e3779b9)+(a<<6)+(a>>2));
        }
    };
    struct Entry {
        Shader* shader;Declaration* declaration;
        std::shared_ptr<const Prepared> prepared;
        size_t bytes;std::uint64_t touched;
        Entry(Key key,std::shared_ptr<const Prepared> value,size_t size,std::uint64_t age)
            :shader(key.shader),declaration(key.declaration),prepared(std::move(value)),bytes(size),touched(age){shader->AddRef();declaration->AddRef();}
        ~Entry(){declaration->Release();shader->Release();}
    };
    static constexpr size_t EntryLimit=128,ByteLimit=2u*1024u*1024u,PreparationsPerFrame=4;
    std::unordered_map<Key,std::unique_ptr<Entry>,Hash> entries_;
    Entry* recent_=nullptr;
    size_t bytes_=0,attempts_=0;
    std::uint64_t clock_=0;
public:
    struct Stats {size_t hits=0,misses=0,prepared=0,fallbacks=0,evictions=0;};
private:
    Stats stats_;
    void evict(){
        auto oldest=entries_.end();
        for(auto it=entries_.begin();it!=entries_.end();++it)
            if(oldest==entries_.end()||it->second->touched<oldest->second->touched)oldest=it;
        if(oldest!=entries_.end()){bytes_-=oldest->second->bytes;if(recent_==oldest->second.get())recent_=nullptr;entries_.erase(oldest);++stats_.evictions;}
    }
public:
    Cache()=default;
    Cache(const Cache&)=delete;
    Cache& operator=(const Cache&)=delete;
    void beginFrame(){stats_={};attempts_=0;}
    void clear(){recent_=nullptr;entries_.clear();bytes_=0;clock_=0;beginFrame();}
    void invalidateShader(Shader* shader){
        for(auto it=entries_.begin();it!=entries_.end();)
            if(it->first.shader==shader){bytes_-=it->second->bytes;if(recent_==it->second.get())recent_=nullptr;it=entries_.erase(it);}else ++it;
    }
    const Stats& stats()const{return stats_;}
    size_t entries()const{return entries_.size();}
    size_t bytes()const{return bytes_;}
    template<class Build> std::shared_ptr<const Prepared> get(Shader* shader,Declaration* declaration,Build build){
        if(!shader||!declaration){++stats_.fallbacks;return {};}
        if(recent_&&recent_->shader==shader&&recent_->declaration==declaration){++stats_.hits;recent_->touched=++clock_;return recent_->prepared;}
        const Key key{shader,declaration};
        auto found=entries_.find(key);
        if(found!=entries_.end()){++stats_.hits;recent_=found->second.get();recent_->touched=++clock_;return recent_->prepared;}
        ++stats_.misses;
        if(attempts_>=PreparationsPerFrame){++stats_.fallbacks;return {};}
        ++attempts_;
        try{
            auto prepared=build();
            constexpr size_t overhead=sizeof(Entry)+sizeof(Key)+128;
            if(!prepared||prepared->bytes()>ByteLimit-overhead){++stats_.fallbacks;return {};}
            const size_t bytes=prepared->bytes()+overhead;
            while(!entries_.empty()&&(entries_.size()>=EntryLimit||bytes>ByteLimit-bytes_))evict();
            auto entry=std::make_unique<Entry>(key,prepared,bytes,++clock_);
            const auto inserted=entries_.emplace(key,std::move(entry));recent_=inserted.first->second.get();bytes_+=bytes;++stats_.prepared;return prepared;
        }catch(...){++stats_.fallbacks;return {};}
    }
};
}
