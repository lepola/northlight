#pragma once
#include "replay_constant_ranges.h"

// Exact, frame-local constant sharing. This is deliberately not an actor-ID
// cache: equipment can use different palettes, and identical geometry can be
// drawn by different actors. Device readbacks remain authoritative. Matching
// consecutive captures once saves repeated copies/comparisons in every shadow
// cascade/face, without changing draw order, geometry or animation frequency.
namespace NorthlightReplayPoses {
inline bool sameRange(NorthlightShaderConstants::Range a,NorthlightShaderConstants::Range b){
    return a.first==b.first&&a.count==b.count;
}
inline bool validRange(NorthlightShaderConstants::Range r,unsigned limit){return r.first<=limit&&r.count<=limit-r.first;}

template<class Replay> bool sameCaptured(const Replay& a,const Replay& b){
    const auto& x=a.constantUsage;const auto& y=b.constantUsage;
    if(a.projectionKind!=b.projectionKind||(a.projectionKind!=1&&a.projectionKind!=2)||
       !sameRange(x.floats,y.floats)||!sameRange(x.booleans,y.booleans)||!sameRange(x.integers,y.integers)||
       !validRange(x.floats,256)||!validRange(x.booleans,16)||!validRange(x.integers,16))return false;
    const unsigned projection=a.projectionKind==1?4:2,end=x.floats.first+x.floats.count;
    if(x.floats.first>projection||end<projection+4)return false;
    // Projection is overwritten for this pass. Compare all other captured
    // registers bit-for-bit, including signed zero/NaN payloads and every bone
    // of relative-address shaders. Uncaptured pooled memory is never read.
    if(std::memcmp(a.constants+4*x.floats.first,b.constants+4*x.floats.first,(projection-x.floats.first)*16)||
       std::memcmp(a.constants+4*(projection+4),b.constants+4*(projection+4),(end-projection-4)*16))return false;
    return (!x.booleans.count||!std::memcmp(a.bools+x.booleans.first,b.bools+x.booleans.first,x.booleans.count*sizeof(a.bools[0])))&&
           (!x.integers.count||!std::memcmp(a.ints+4*x.integers.first,b.ints+4*x.integers.first,x.integers.count*4*sizeof(a.ints[0])));
}

// One instance per uninterrupted replay pass with a fixed projection. Discard
// on any external constant writes, restore/reset, cascade or cube-face change.
// Group IDs must identify exact sameCaptured() banks within this frame. Shader
// switches are safe: DEF literals never mutate the API constant register bank.
template<class Bool> class Pass {
    NorthlightReplayConstants::BoundedDirtyRegisters<float,256,4> floatBank_;
    NorthlightReplayConstants::BoundedDirtyRegisters<Bool,16,1> boolBank_;
    NorthlightReplayConstants::BoundedDirtyRegisters<int,16,4> intBank_;
    unsigned group_=0;
    bool valid_=false;
public:
    NorthlightReplayConstants::DirtySpan floats,booleans,integers;
    size_t prepared=0,reused=0;
    const float* desired()const{return floatBank_.data();}
    template<class Replay> bool prepare(const Replay& p,const float* rows,const float* worldMatrix=nullptr){
        floats={};booleans={};integers={};
        if(valid_&&group_==p.constantGroup){++reused;return true;}
        if(!NorthlightReplayConstants::updateProjectedConstants(floatBank_,p.constants,p.constantUsage,p.projectionKind,rows,floats,worldMatrix))return false;
        booleans=boolBank_.update(p.bools,p.constantUsage.booleans);
        integers=intBank_.update(p.ints,p.constantUsage.integers);
        group_=p.constantGroup;valid_=true;++prepared;return true;
    }
};
}
