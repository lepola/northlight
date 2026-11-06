#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

// Cold diagnostics only. Count retained capacities, not RSS or allocator/driver
// overhead. One collector spans all owners in a scope so shared storage counts
// once. Failure to allocate diagnostic bookkeeping marks an incomplete sample;
// it must never change admission, resource lifetimes or rendering decisions.
namespace NorthlightMemoryFootprint {
enum Kind {Vertices,Triangles,Acceleration,Texels,Upload,Probes,Fog,Metadata,KindCount};
class Counter {
    std::unordered_set<const void*> seen_;
    std::array<uint64_t,KindCount> bytes_{};
    bool complete_=true;
public:
    bool visit(const void* owner) noexcept {
        if(!owner)return false;
        try{return seen_.insert(owner).second;}catch(...){complete_=false;return false;}
    }
    void add(Kind kind,uint64_t bytes) noexcept {bytes_[kind]+=bytes;}
    void allocation(Kind kind,const void* pointer,uint64_t bytes) noexcept {
        if(bytes&&visit(pointer))add(kind,bytes);
    }
    template<class T> void vector(Kind kind,const std::vector<T>& values) noexcept {
        allocation(kind,values.data(),uint64_t(values.capacity())*sizeof(T));
    }
    void string(const std::string& value) noexcept {
        // Small-string storage is already inside the containing object.
        const auto data=reinterpret_cast<uintptr_t>(value.data()),object=reinterpret_cast<uintptr_t>(&value);
        if(data<object||data-object>=sizeof(value))allocation(Metadata,value.data(),value.capacity()+1);
    }
    uint64_t bytes(Kind kind)const noexcept{return bytes_[kind];}
    uint64_t total()const noexcept {uint64_t sum=0;for(auto bytes:bytes_)sum+=bytes;return sum;}
    size_t owners()const noexcept{return seen_.size();}
    bool complete()const noexcept{return complete_;}
};
}
