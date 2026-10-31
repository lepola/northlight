#pragma once
#include "shadow_bounds.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <vector>

// Worker-prepared content, independent of transient VB/IB/material numbering.
// Only the persistent directional pass uses these identities. Live terrain,
// actors, point lights and the shadow matrix retain their existing policies.
namespace NorthlightLocalShadowSignature {
struct Digest {
    std::array<uint64_t,2> words{};
    uint64_t triangles=0;
    bool operator==(const Digest& b)const{return words==b.words&&triangles==b.triangles;}
    bool operator!=(const Digest& b)const{return !(*this==b);}
    void add(const Digest& b){for(unsigned i=0;i<2;++i)words[i]+=b.words[i];triangles+=b.triangles;}
};
// Two independently mixed 64-bit lanes; order-independent accumulation keeps
// identical triangles stable when loader/model/material grouping changes.
class Hasher {
    std::array<uint64_t,2> state_{{0xcbf29ce484222325ull,0x84222325cbf29ce4ull}};
public:
    Hasher()=default;
    explicit Hasher(const Digest& prefix){for(unsigned i=0;i<2;++i)state_[i]^=prefix.words[i];}
    void word(uint32_t value){
        static constexpr uint64_t primes[]={0x100000001b3ull,0x9e3779b185ebca87ull};
        for(unsigned i=0;i<2;++i){state_[i]^=uint64_t(value)+uint64_t(i)*0x9e3779b9u;state_[i]*=primes[i];}
    }
    void scalar(float value){uint32_t bits;std::memcpy(&bits,&value,4);word(bits);}
    Digest finish(uint64_t count=0)const{
        Digest result;result.triangles=count;
        for(unsigned i=0;i<2;++i){uint64_t h=state_[i];h^=h>>30;h*=0xbf58476d1ce4e5b9ull;h^=h>>27;h*=0x94d049bb133111ebull;result.words[i]=h^(h>>31);}
        return result;
    }
};
struct Record {
    NorthlightGI::Vec3 low={INFINITY,INFINITY,INFINITY},high={-INFINITY,-INFINITY,-INFINITY};
    Digest content;
    int chunkX=-1,chunkY=-1;
    bool terrain=false;
    bool pointOnly=false;
};
using Records=std::vector<Record>;
using FixedChunks=std::set<std::pair<int,int>>;
inline bool needsRefresh(bool previousKnown,const Digest& previous,uint64_t previousGeneration,
                         bool currentKnown,const Digest& current,uint64_t currentGeneration){
    // Unknown metadata is never proof that an earlier nonempty caster set was
    // empty. Transitions out of the bounded fallback must refresh as well.
    if(previousGeneration!=currentGeneration&&(!previousKnown||!currentKnown))return true;
    return previous!=current;
}
inline Digest signature(const Records& records,const FixedChunks& fixed,const float* matrix){
    Digest result;
    for(const auto& r:records){
        if(r.pointOnly)continue;
        if(r.terrain&&!fixed.count({r.chunkX,r.chunkY}))continue;
        // Match cached static drawing, including upstream near-plane pancaking.
        if(NorthlightShadowBounds::directionalClipReject(r.low,r.high,matrix))continue;
        result.add(r.content);
    }
    return result;
}
// One memo per cascade. A camera move within the cached extent retains the
// cached matrix; warm frames do no record iteration or geometry hashing.
class Memo {
    const Records* records_=nullptr;
    uint64_t generation_=0;
    float matrix_[16]={};
    Digest value_;
    bool valid_=false;
    uint64_t evaluations_=0;
public:
    Digest get(const Records* records,uint64_t generation,const FixedChunks& fixed,const float* matrix){
        if(!valid_||records_!=records||generation_!=generation||!matrix||std::memcmp(matrix_,matrix,sizeof matrix_)){
            value_=records?signature(*records,fixed,matrix):Digest{};
            records_=records;generation_=generation;valid_=matrix!=nullptr;
            if(matrix)std::memcpy(matrix_,matrix,sizeof matrix_);
            ++evaluations_;
        }
        return value_;
    }
    uint64_t evaluations()const{return evaluations_;}
};
} // namespace NorthlightLocalShadowSignature
