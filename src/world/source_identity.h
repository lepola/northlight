#pragma once
#include <cstdint>
#include <cstddef>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

// A full verified source contract, never a pointer/hash-only content claim.
// Retaining this small immutable key retains no CPU geometry or game resource.
namespace NorthlightSourceIdentity {
inline std::atomic<size_t> identityLiveBytes{0},cpuLiveBytes{0},cpuLiveObjects{0};
struct Ledger {std::atomic<size_t> bytes{0},objects{0};};
template<class T> struct Retained:T {
    std::shared_ptr<Ledger> ledger;const size_t charge;
    Retained(std::shared_ptr<Ledger> owner,size_t bytes):ledger(std::move(owner)),charge(bytes){ledger->bytes.fetch_add(charge,std::memory_order_relaxed);ledger->objects.fetch_add(1,std::memory_order_relaxed);cpuLiveBytes.fetch_add(charge,std::memory_order_relaxed);cpuLiveObjects.fetch_add(1,std::memory_order_relaxed);}
    ~Retained(){ledger->bytes.fetch_sub(charge,std::memory_order_relaxed);ledger->objects.fetch_sub(1,std::memory_order_relaxed);cpuLiveBytes.fetch_sub(charge,std::memory_order_relaxed);cpuLiveObjects.fetch_sub(1,std::memory_order_relaxed);}
};
template<class T> size_t allocationBytes(size_t payload){return payload+sizeof(Retained<T>)+32;}
template<class T> std::shared_ptr<T> allocate(const std::shared_ptr<Ledger>& ledger,size_t bytes){return std::make_shared<Retained<T>>(ledger,bytes);}
struct Key {
    const std::vector<std::uint64_t> words;
    const std::uint64_t hash;
    template<class Words> static std::uint64_t fingerprint(const Words& words){
        std::uint64_t h=1469598103934665603ull;for(auto word:words){h^=word;h*=1099511628211ull;}return h;
    }
    explicit Key(std::vector<std::uint64_t> value):words(std::move(value)),hash(fingerprint(words)){identityLiveBytes.fetch_add(bytes(),std::memory_order_relaxed);}
    ~Key(){identityLiveBytes.fetch_sub(bytes(),std::memory_order_relaxed);}
    Key(const Key&)=delete;Key& operator=(const Key&)=delete;
    size_t bytes()const{return sizeof(Key)+words.capacity()*sizeof(std::uint64_t)+32;}
};
using Handle=std::shared_ptr<const Key>;
inline bool same(const Handle& a,const Handle& b){return a&&b&&(a==b||(a->hash==b->hash&&a->words==b->words));}
inline Handle make(std::vector<std::uint64_t> words){return std::make_shared<const Key>(std::move(words));}
template<class Snapshot> auto of(const Snapshot& snapshot,int)->decltype(snapshot.sourceIdentity){return snapshot.sourceIdentity;}
template<class Snapshot> Handle of(const Snapshot&,long){return {};}
template<class Snapshot> Handle of(const Snapshot& snapshot){return of(snapshot,0);}
}
